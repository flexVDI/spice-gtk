/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2015-2016 CodeWeavers, Inc

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#include "config.h"

#include "spice-client.h"
#include "spice-common.h"
#include "spice-channel-priv.h"

#include "channel-display-priv.h"

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>


/* GStreamer decoder implementation */

typedef struct SpiceGstDecoder {
    VideoDecoder base;

    /* ---------- GStreamer pipeline ---------- */

    GstAppSrc *appsrc;
    GstAppSink *appsink;
    GstElement *pipeline;
    GstClock *clock;

    /* ---------- Decoding and display queues ---------- */

    uint32_t last_mm_time;

    GMutex queues_mutex;
    GQueue *decoding_queue;
    GQueue *display_queue;
    guint timer_id;
} SpiceGstDecoder;

static struct {
    const gchar *dec_name;
    const gchar *dec_caps;
} gst_opts[] = {
    /* decodebin will use vaapi if installed, which for a time could
     * intentionally crash the application. So only use decodebin as a
     * fallback or when SPICE_GSTVIDEO_AUTO is set.
     * See: https://bugs.freedesktop.org/show_bug.cgi?id=90884
     */
    { "decodebin", "" },

    /* SPICE_VIDEO_CODEC_TYPE_MJPEG */
    { "jpegdec", "caps=image/jpeg" },

    /* SPICE_VIDEO_CODEC_TYPE_VP8
     *
     * typefind is unable to identify VP8 streams by design.
     * See: https://bugzilla.gnome.org/show_bug.cgi?id=756457
     */
    { "vp8dec", "caps=video/x-vp8" },

    /* SPICE_VIDEO_CODEC_TYPE_H264
     * h264 streams detection works fine and setting an incomplete cap
     * causes errors. So let typefind do all the work.
     */
    { "h264parse ! avdec_h264", "" },

    /* SPICE_VIDEO_CODEC_TYPE_VP9 */
    { "vp9dec", "caps=video/x-vp9" },
};

G_STATIC_ASSERT(G_N_ELEMENTS(gst_opts) <= SPICE_VIDEO_CODEC_TYPE_ENUM_END);

#define VALID_VIDEO_CODEC_TYPE(codec) \
    (codec > 0 && codec < G_N_ELEMENTS(gst_opts))

/* ---------- SpiceGstFrame ---------- */

typedef struct SpiceGstFrame {
    GstClockTime timestamp;
    SpiceMsgIn *msg;
    GstSample *sample;
} SpiceGstFrame;

static SpiceGstFrame *create_gst_frame(GstBuffer *buffer, SpiceMsgIn *msg)
{
    SpiceGstFrame *gstframe = spice_new(SpiceGstFrame, 1);
    gstframe->timestamp = GST_BUFFER_PTS(buffer);
    gstframe->msg = msg;
    spice_msg_in_ref(msg);
    gstframe->sample = NULL;
    return gstframe;
}

static void free_gst_frame(SpiceGstFrame *gstframe)
{
    spice_msg_in_unref(gstframe->msg);
    if (gstframe->sample) {
        gst_sample_unref(gstframe->sample);
    }
    free(gstframe);
}


/* ---------- GStreamer pipeline ---------- */

static void schedule_frame(SpiceGstDecoder *decoder);

/* main context */
static gboolean display_frame(gpointer video_decoder)
{
    SpiceGstDecoder *decoder = (SpiceGstDecoder*)video_decoder;
    SpiceGstFrame *gstframe;
    GstCaps *caps;
    gint width, height;
    GstStructure *s;
    GstBuffer *buffer;
    GstMapInfo mapinfo;

    g_mutex_lock(&decoder->queues_mutex);
    decoder->timer_id = 0;
    gstframe = g_queue_pop_head(decoder->display_queue);
    g_mutex_unlock(&decoder->queues_mutex);
    /* If the queue is empty we don't even need to reschedule */
    g_return_val_if_fail(gstframe, G_SOURCE_REMOVE);

    if (!gstframe->sample) {
        spice_warning("got a frame without a sample!");
        goto error;
    }

    caps = gst_sample_get_caps(gstframe->sample);
    if (!caps) {
        spice_warning("GStreamer error: could not get the caps of the sample");
        goto error;
    }

    s = gst_caps_get_structure(caps, 0);
    if (!gst_structure_get_int(s, "width", &width) ||
        !gst_structure_get_int(s, "height", &height)) {
        spice_warning("GStreamer error: could not get the size of the frame");
        goto error;
    }

    buffer = gst_sample_get_buffer(gstframe->sample);
    if (!gst_buffer_map(buffer, &mapinfo, GST_MAP_READ)) {
        spice_warning("GStreamer error: could not map the buffer");
        goto error;
    }

    stream_display_frame(decoder->base.stream, gstframe->msg,
                         width, height, mapinfo.data);
    gst_buffer_unmap(buffer, &mapinfo);

 error:
    free_gst_frame(gstframe);
    schedule_frame(decoder);
    return G_SOURCE_REMOVE;
}

/* main loop or GStreamer streaming thread */
static void schedule_frame(SpiceGstDecoder *decoder)
{
    guint32 now = stream_get_time(decoder->base.stream);
    g_mutex_lock(&decoder->queues_mutex);

    while (!decoder->timer_id) {
        SpiceGstFrame *gstframe = g_queue_peek_head(decoder->display_queue);
        if (!gstframe) {
            break;
        }

        SpiceStreamDataHeader *op = spice_msg_in_parsed(gstframe->msg);
        if (now < op->multi_media_time) {
            decoder->timer_id = g_timeout_add(op->multi_media_time - now,
                                              display_frame, decoder);
        } else if (g_queue_get_length(decoder->display_queue) == 1) {
            /* Still attempt to display the least out of date frame so the
             * video is not completely frozen for an extended period of time.
             */
            decoder->timer_id = g_timeout_add(0, display_frame, decoder);
        } else {
            SPICE_DEBUG("%s: rendering too late by %u ms (ts: %u, mmtime: %u), dropping",
                        __FUNCTION__, now - op->multi_media_time,
                        op->multi_media_time, now);
            stream_dropped_frame_on_playback(decoder->base.stream);
            g_queue_pop_head(decoder->display_queue);
            free_gst_frame(gstframe);
        }
    }

    g_mutex_unlock(&decoder->queues_mutex);
}

/* GStreamer thread
 *
 * We cannot use GStreamer's signals because they are not always run in
 * the main context. So use a callback (lower overhead) and have it pull
 * the sample to avoid a race with free_pipeline(). This means queuing the
 * decoded frames outside GStreamer. So while we're at it, also schedule
 * the frame display ourselves in schedule_frame().
 */
static GstFlowReturn new_sample(GstAppSink *gstappsink, gpointer video_decoder)
{
    SpiceGstDecoder *decoder = video_decoder;

    GstSample *sample = gst_app_sink_pull_sample(decoder->appsink);
    GstBuffer *buffer = sample ? gst_sample_get_buffer(sample) : NULL;
    if (sample) {
        g_mutex_lock(&decoder->queues_mutex);

        /* gst_app_sink_pull_sample() sometimes returns the same buffer twice
         * or buffers that have a modified, and thus unrecognizable, PTS.
         * Blindly removing frames from the decoding_queue until we find a
         * match would only empty the queue, resulting in later buffers not
         * finding a match either, etc. So check the buffer has a matching
         * frame first.
         */
        SpiceGstFrame *gstframe;
        GList *l = g_queue_peek_head_link(decoder->decoding_queue);
        while (l) {
            gstframe = l->data;
            if (gstframe->timestamp == GST_BUFFER_PTS(buffer)) {
                /* The frame is now ready for display */
                gstframe->sample = sample;
                g_queue_push_tail(decoder->display_queue, gstframe);

                /* Now that we know there is a match, remove it and the older
                 * frames from the decoding queue.
                 */
                while ((gstframe = g_queue_pop_head(decoder->decoding_queue))) {
                    if (gstframe->timestamp == GST_BUFFER_PTS(buffer)) {
                        break;
                    }
                    /* The GStreamer pipeline dropped the corresponding
                     * buffer.
                     */
                    SPICE_DEBUG("the GStreamer pipeline dropped a frame");
                    free_gst_frame(gstframe);
                }
                break;
            }
            l = l->next;
        }
        if (!l) {
            spice_warning("got an unexpected decoded buffer!");
            gst_sample_unref(sample);
        }

        g_mutex_unlock(&decoder->queues_mutex);
        schedule_frame(decoder);
    } else {
        spice_warning("GStreamer error: could not pull sample");
    }
    return GST_FLOW_OK;
}

static void free_pipeline(SpiceGstDecoder *decoder)
{
    if (!decoder->pipeline) {
        return;
    }

    gst_element_set_state(decoder->pipeline, GST_STATE_NULL);
    gst_object_unref(decoder->appsrc);
    gst_object_unref(decoder->appsink);
    gst_object_unref(decoder->pipeline);
    gst_object_unref(decoder->clock);
    decoder->pipeline = NULL;
}

static gboolean handle_pipeline_message(GstBus *bus, GstMessage *msg, gpointer video_decoder)
{
    SpiceGstDecoder *decoder = video_decoder;

    if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
        GError *err = NULL;
        gchar *debug_info = NULL;
        gst_message_parse_error(msg, &err, &debug_info);
        spice_warning("GStreamer error from element %s: %s",
                      GST_OBJECT_NAME(msg->src), err->message);
        if (debug_info) {
            SPICE_DEBUG("debug information: %s", debug_info);
            g_free(debug_info);
        }
        g_clear_error(&err);

        /* We won't be able to process any more frame anyway */
        free_pipeline(decoder);
    }
    return TRUE;
}

static gboolean create_pipeline(SpiceGstDecoder *decoder)
{
    gchar *desc;
    gboolean auto_enabled;
    guint opt;
    GstAppSinkCallbacks appsink_cbs = { NULL };
    GError *err = NULL;
    GstBus *bus;

    auto_enabled = (g_getenv("SPICE_GSTVIDEO_AUTO") != NULL);
    if (auto_enabled || !VALID_VIDEO_CODEC_TYPE(decoder->base.codec_type)) {
        SPICE_DEBUG("Trying %s for codec type %d %s",
                    gst_opts[0].dec_name, decoder->base.codec_type,
                    (auto_enabled) ? "(SPICE_GSTVIDEO_AUTO is set)" : "");
        opt = 0;
    } else {
        opt = decoder->base.codec_type;
    }

    /* - We schedule the frame display ourselves so set sync=false on appsink
     *   so the pipeline decodes them as fast as possible. This will also
     *   minimize the risk of frames getting lost when we rebuild the
     *   pipeline.
     * - Set max-bytes=0 on appsrc so it does not drop frames that may be
     *   needed by those that follow.
     */
    desc = g_strdup_printf("appsrc name=src is-live=true format=time max-bytes=0 block=true "
                           "%s ! %s ! videoconvert ! appsink name=sink "
                           "caps=video/x-raw,format=BGRx sync=false drop=false",
                           gst_opts[opt].dec_caps, gst_opts[opt].dec_name);
    SPICE_DEBUG("GStreamer pipeline: %s", desc);

    decoder->pipeline = gst_parse_launch_full(desc, NULL, GST_PARSE_FLAG_FATAL_ERRORS, &err);
    g_free(desc);
    if (!decoder->pipeline) {
        spice_warning("GStreamer error: %s", err->message);
        g_clear_error(&err);
        return FALSE;
    }

    decoder->appsrc = GST_APP_SRC(gst_bin_get_by_name(GST_BIN(decoder->pipeline), "src"));
    decoder->appsink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(decoder->pipeline), "sink"));

    appsink_cbs.new_sample = new_sample;
    gst_app_sink_set_callbacks(decoder->appsink, &appsink_cbs, decoder, NULL);
    bus = gst_pipeline_get_bus(GST_PIPELINE(decoder->pipeline));
    gst_bus_add_watch(bus, handle_pipeline_message, decoder);
    gst_object_unref(bus);

    decoder->clock = gst_pipeline_get_clock(GST_PIPELINE(decoder->pipeline));

    if (gst_element_set_state(decoder->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        SPICE_DEBUG("GStreamer error: Unable to set the pipeline to the playing state.");
        free_pipeline(decoder);
        return FALSE;
    }

    return TRUE;
}


/* ---------- VideoDecoder's public API ---------- */

static void spice_gst_decoder_reschedule(VideoDecoder *video_decoder)
{
    SpiceGstDecoder *decoder = (SpiceGstDecoder*)video_decoder;
    if (decoder->timer_id != 0) {
        g_source_remove(decoder->timer_id);
        decoder->timer_id = 0;
    }
    schedule_frame(decoder);
}

/* main context */
static void spice_gst_decoder_destroy(VideoDecoder *video_decoder)
{
    SpiceGstDecoder *decoder = (SpiceGstDecoder*)video_decoder;

    /* Stop and free the pipeline to ensure there will not be any further
     * new_sample() call (clearing thread-safety concerns).
     */
    free_pipeline(decoder);

    /* Even if we kept the decoder around, once we return the stream will be
     * destroyed making it impossible to display frames. So cancel any
     * scheduled display_frame() call and drop the queued frames.
     */
    if (decoder->timer_id) {
        g_source_remove(decoder->timer_id);
    }
    g_mutex_clear(&decoder->queues_mutex);
    SpiceGstFrame *gstframe;
    while ((gstframe = g_queue_pop_head(decoder->decoding_queue))) {
        free_gst_frame(gstframe);
    }
    g_queue_free(decoder->decoding_queue);
    while ((gstframe = g_queue_pop_head(decoder->display_queue))) {
        free_gst_frame(gstframe);
    }
    g_queue_free(decoder->display_queue);

    free(decoder);

    /* Don't call gst_deinit() as other parts of the client
     * may still be using GStreamer.
     */
}

static void release_buffer_data(gpointer data)
{
    SpiceMsgIn* frame_msg = (SpiceMsgIn*)data;
    spice_msg_in_unref(frame_msg);
}

/* spice_gst_decoder_queue_frame() queues the SpiceMsgIn message for decoding
 * and displaying. The steps it goes through are as follows:
 *
 * 1) A SpiceGstFrame is created to keep track of SpiceMsgIn and some additional
 *    metadata. SpiceMsgIn is reffed. The SpiceFrame is then pushed to the
 *    decoding_queue.
 * 2) The data part of SpiceMsgIn, which contains the compressed frame data,
 *    is wrapped in a GstBuffer and is pushed to the GStreamer pipeline for
 *    decoding. SpiceMsgIn is reffed.
 * 3) As soon as the GStreamer pipeline no longer needs the compressed frame it
 *    calls release_buffer_data() to unref SpiceMsgIn.
 * 4) Once the decompressed frame is available the GStreamer pipeline calls
 *    new_sample() in the GStreamer thread.
 * 5) new_sample() then matches the decompressed frame to a SpiceGstFrame from
 *    the decoding queue using the GStreamer timestamp information to deal with
 *    dropped frames. The SpiceGstFrame is popped from the decoding_queue.
 * 6) new_sample() then attaches the decompressed frame to the SpiceGstFrame,
 *    pushes it to the display_queue and calls schedule_frame().
 * 7) schedule_frame() then uses the SpiceMsgIn's mm_time to arrange for
 *    display_frame() to be called, in the main thread, at the right time for
 *    the next frame.
 * 8) display_frame() pops the first SpiceGstFrame from the display_queue and
 *    calls stream_display_frame().
 * 9) display_frame() then frees the SpiceGstFrame and the decompressed frame.
 *    SpiceMsgIn is unreffed.
 */
static gboolean spice_gst_decoder_queue_frame(VideoDecoder *video_decoder,
                                              SpiceMsgIn *frame_msg,
                                              int32_t latency)
{
    SpiceGstDecoder *decoder = (SpiceGstDecoder*)video_decoder;

    uint8_t *data;
    uint32_t size = spice_msg_in_frame_data(frame_msg, &data);
    if (size == 0) {
        SPICE_DEBUG("got an empty frame buffer!");
        return TRUE;
    }

    SpiceStreamDataHeader *frame_op = spice_msg_in_parsed(frame_msg);
    if (frame_op->multi_media_time < decoder->last_mm_time) {
        SPICE_DEBUG("new-frame-time < last-frame-time (%u < %u):"
                    " resetting stream, id %u",
                    frame_op->multi_media_time,
                    decoder->last_mm_time, frame_op->id);
        /* Let GStreamer deal with the frame anyway */
    }
    decoder->last_mm_time = frame_op->multi_media_time;

    if (latency < 0 &&
        decoder->base.codec_type == SPICE_VIDEO_CODEC_TYPE_MJPEG) {
        /* Dropping MJPEG frames has no impact on those that follow and
         * saves CPU so do it.
         */
        SPICE_DEBUG("dropping a late MJPEG frame");
        return TRUE;
    }

    if (decoder->pipeline == NULL) {
        /* An error occurred, causing the GStreamer pipeline to be freed */
        spice_warning("An error occurred, stopping the video stream");
        return FALSE;
    }

    /* ref() the frame_msg for the buffer */
    spice_msg_in_ref(frame_msg);
    GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                                    data, size, 0, size,
                                                    frame_msg, &release_buffer_data);

    GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_PTS(buffer) = gst_clock_get_time(decoder->clock) - gst_element_get_base_time(decoder->pipeline) + ((uint64_t)MAX(0, latency)) * 1000 * 1000;

    g_mutex_lock(&decoder->queues_mutex);
    g_queue_push_tail(decoder->decoding_queue, create_gst_frame(buffer, frame_msg));
    g_mutex_unlock(&decoder->queues_mutex);

    if (gst_app_src_push_buffer(decoder->appsrc, buffer) != GST_FLOW_OK) {
        SPICE_DEBUG("GStreamer error: unable to push frame of size %u", size);
        stream_dropped_frame_on_playback(decoder->base.stream);
    }
    return TRUE;
}

static gboolean gstvideo_init(void)
{
    static int success = 0;
    if (!success) {
        GError *err = NULL;
        if (gst_init_check(NULL, NULL, &err)) {
            success = 1;
        } else {
            spice_warning("Disabling GStreamer video support: %s", err->message);
            g_clear_error(&err);
            success = -1;
        }
    }
    return success > 0;
}

G_GNUC_INTERNAL
VideoDecoder* create_gstreamer_decoder(int codec_type, display_stream *stream)
{
    SpiceGstDecoder *decoder = NULL;

    if (gstvideo_init()) {
        decoder = spice_new0(SpiceGstDecoder, 1);
        decoder->base.destroy = spice_gst_decoder_destroy;
        decoder->base.reschedule = spice_gst_decoder_reschedule;
        decoder->base.queue_frame = spice_gst_decoder_queue_frame;
        decoder->base.codec_type = codec_type;
        decoder->base.stream = stream;
        g_mutex_init(&decoder->queues_mutex);
        decoder->decoding_queue = g_queue_new();
        decoder->display_queue = g_queue_new();

        if (!create_pipeline(decoder)) {
            decoder->base.destroy((VideoDecoder*)decoder);
            decoder = NULL;
        }
    }

    return (VideoDecoder*)decoder;
}

G_GNUC_INTERNAL
gboolean gstvideo_has_codec(int codec_type)
{
    gboolean has_codec = FALSE;

    VideoDecoder *decoder = create_gstreamer_decoder(codec_type, NULL);
    if (decoder) {
        has_codec = TRUE;
        decoder->destroy(decoder);
    }

    return has_codec;
}
