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
#include <gst/video/gstvideometa.h>


typedef struct SpiceGstFrame SpiceGstFrame;

/* GStreamer decoder implementation */

typedef struct SpiceGstDecoder {
    VideoDecoder base;

    /* ---------- GStreamer pipeline ---------- */

    GstAppSrc *appsrc;
    GstAppSink *appsink;
    GstElement *pipeline;
    GstClock *clock;

    guintptr win_handle;

    /* ---------- Decoding and display queues ---------- */

    uint32_t last_mm_time;

    GMutex queues_mutex;
    GQueue *decoding_queue;
    SpiceGstFrame *display_frame;
    guint timer_id;
    guint pending_samples;
} SpiceGstDecoder;

#define VALID_VIDEO_CODEC_TYPE(codec) \
    (codec > 0 && codec < G_N_ELEMENTS(gst_opts))

/* GstPlayFlags enum is in plugin's header which should not be exported.
 * https://bugzilla.gnome.org/show_bug.cgi?id=784279
 */
typedef enum {
  GST_PLAY_FLAG_VIDEO             = (1 << 0),
  GST_PLAY_FLAG_AUDIO             = (1 << 1),
  GST_PLAY_FLAG_TEXT              = (1 << 2),
  GST_PLAY_FLAG_VIS               = (1 << 3),
  GST_PLAY_FLAG_SOFT_VOLUME       = (1 << 4),
  GST_PLAY_FLAG_NATIVE_AUDIO      = (1 << 5),
  GST_PLAY_FLAG_NATIVE_VIDEO      = (1 << 6),
  GST_PLAY_FLAG_DOWNLOAD          = (1 << 7),
  GST_PLAY_FLAG_BUFFERING         = (1 << 8),
  GST_PLAY_FLAG_DEINTERLACE       = (1 << 9),
  GST_PLAY_FLAG_SOFT_COLORBALANCE = (1 << 10),
  GST_PLAY_FLAG_FORCE_FILTERS     = (1 << 11),
} SpiceGstPlayFlags;

/* ---------- SpiceGstFrame ---------- */

struct SpiceGstFrame {
    GstClockTime timestamp;
    SpiceFrame *frame;
    GstSample *sample;
};

static SpiceGstFrame *create_gst_frame(GstBuffer *buffer, SpiceFrame *frame)
{
    SpiceGstFrame *gstframe = g_new(SpiceGstFrame, 1);
    gstframe->timestamp = GST_BUFFER_PTS(buffer);
    gstframe->frame = frame;
    gstframe->sample = NULL;
    return gstframe;
}

static void free_gst_frame(SpiceGstFrame *gstframe)
{
    gstframe->frame->free(gstframe->frame);
    if (gstframe->sample) {
        gst_sample_unref(gstframe->sample);
    }
    g_free(gstframe);
}


/* ---------- GStreamer pipeline ---------- */

static void schedule_frame(SpiceGstDecoder *decoder);
static void fetch_pending_sample(SpiceGstDecoder *decoder);

static int spice_gst_buffer_get_stride(GstBuffer *buffer)
{
    GstVideoMeta *video = gst_buffer_get_video_meta(buffer);
    return video && video->n_planes > 0 ? video->stride[0] : SPICE_UNKNOWN_STRIDE;
}

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
    gstframe = decoder->display_frame;
    decoder->display_frame = NULL;
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

    stream_display_frame(decoder->base.stream, gstframe->frame,
                         width, height, spice_gst_buffer_get_stride(buffer), mapinfo.data);
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
        while (decoder->display_frame == NULL && decoder->pending_samples) {
            fetch_pending_sample(decoder);
        }

        SpiceGstFrame *gstframe = decoder->display_frame;
        if (!gstframe) {
            break;
        }

        if (spice_mmtime_diff(now, gstframe->frame->mm_time) < 0) {
            decoder->timer_id = g_timeout_add(gstframe->frame->mm_time - now,
                                              display_frame, decoder);
        } else if (decoder->display_frame && !decoder->pending_samples) {
            /* Still attempt to display the least out of date frame so the
             * video is not completely frozen for an extended period of time.
             */
            decoder->timer_id = g_timeout_add(0, display_frame, decoder);
        } else {
            SPICE_DEBUG("%s: rendering too late by %u ms (ts: %u, mmtime: %u), dropping",
                        __FUNCTION__, now - gstframe->frame->mm_time,
                        gstframe->frame->mm_time, now);
            stream_dropped_frame_on_playback(decoder->base.stream);
            decoder->display_frame = NULL;
            free_gst_frame(gstframe);
        }
    }

    g_mutex_unlock(&decoder->queues_mutex);
}

static void fetch_pending_sample(SpiceGstDecoder *decoder)
{
    GstSample *sample = gst_app_sink_pull_sample(decoder->appsink);
    if (sample) {
        // account for the fetched sample
        decoder->pending_samples--;

        GstBuffer *buffer = gst_sample_get_buffer(sample);

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
                decoder->display_frame = gstframe;

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
    } else {
        // no more samples to get, possibly some sample was dropped
        decoder->pending_samples = 0;
        spice_warning("GStreamer error: could not pull sample");
    }
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

    g_mutex_lock(&decoder->queues_mutex);
    decoder->pending_samples++;
    if (decoder->timer_id && decoder->display_frame) {
        g_mutex_unlock(&decoder->queues_mutex);
        return GST_FLOW_OK;
    }
    g_mutex_unlock(&decoder->queues_mutex);

    schedule_frame(decoder);

    return GST_FLOW_OK;
}

static void free_pipeline(SpiceGstDecoder *decoder)
{
    if (!decoder->pipeline) {
        return;
    }

    gst_element_set_state(decoder->pipeline, GST_STATE_NULL);
    gst_object_unref(decoder->appsrc);
    if (decoder->appsink) {
        gst_object_unref(decoder->appsink);
    }
    gst_object_unref(decoder->pipeline);
    gst_object_unref(decoder->clock);
    decoder->pipeline = NULL;
}

static gboolean handle_pipeline_message(GstBus *bus, GstMessage *msg, gpointer video_decoder)
{
    SpiceGstDecoder *decoder = video_decoder;

    switch(GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
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
        break;
    }
    case GST_MESSAGE_STREAM_START: {
        gchar *filename = g_strdup_printf("spice-gtk-gst-pipeline-debug-%" G_GUINT32_FORMAT "-%s",
                                          decoder->base.stream->id,
                                          gst_opts[decoder->base.codec_type].name);
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(decoder->pipeline),
                                  GST_DEBUG_GRAPH_SHOW_ALL
#if GST_CHECK_VERSION(1,5,1)
                                    | GST_DEBUG_GRAPH_SHOW_FULL_PARAMS
#endif
                                    | GST_DEBUG_GRAPH_SHOW_STATES,
                                    filename);
        g_free(filename);
        break;
    }
    case GST_MESSAGE_ELEMENT: {
        if (gst_is_video_overlay_prepare_window_handle_message(msg)) {
            GstVideoOverlay *overlay;

            SPICE_DEBUG("prepare-window-handle msg received (handle: %" PRIuPTR ")", decoder->win_handle);
            if (decoder->win_handle != 0) {
                overlay = GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(msg));
                gst_video_overlay_set_window_handle(overlay, decoder->win_handle);
                gst_video_overlay_handle_events(overlay, false);
            }
        }
        break;
    }
    default:
        /* not being handled */
        break;
    }
    return TRUE;
}

#if GST_CHECK_VERSION(1,9,0)
static void app_source_setup(GstElement *pipeline G_GNUC_UNUSED,
                             GstElement *source,
                             SpiceGstDecoder *decoder)
{
    GstCaps *caps;

    /* - We schedule the frame display ourselves so set sync=false on appsink
     *   so the pipeline decodes them as fast as possible. This will also
     *   minimize the risk of frames getting lost when we rebuild the
     *   pipeline.
     * - Set max-bytes=0 on appsrc so it does not drop frames that may be
     *   needed by those that follow.
     */
    caps = gst_caps_from_string(gst_opts[decoder->base.codec_type].dec_caps);
    g_object_set(source,
                 "caps", caps,
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "max-bytes", G_GINT64_CONSTANT(0),
                 "block", TRUE,
                 NULL);
    gst_caps_unref(caps);
    decoder->appsrc = GST_APP_SRC(gst_object_ref(source));
}
#endif

static gboolean create_pipeline(SpiceGstDecoder *decoder)
{
    GstBus *bus;
#if GST_CHECK_VERSION(1,9,0)
    GstElement *playbin, *sink;
    SpiceGstPlayFlags flags;
    GstCaps *caps;

    playbin = gst_element_factory_make("playbin", "playbin");
    if (playbin == NULL) {
        spice_warning("error upon creation of 'playbin' element");
        return FALSE;
    }

    /* Will try to get window handle in order to apply the GstVideoOverlay
     * interface, setting overlay to this window will happen only when
     * prepare-window-handle message is received
     */
    decoder->win_handle = get_window_handle(decoder->base.stream);
    SPICE_DEBUG("Creating Gstreamer pipline (handle for overlay %s)\n",
                decoder->win_handle ? "received" : "not received");
    if (decoder->win_handle == 0) {
        sink = gst_element_factory_make("appsink", "sink");
        if (sink == NULL) {
            spice_warning("error upon creation of 'appsink' element");
            gst_object_unref(playbin);
            return FALSE;
        }
        caps = gst_caps_from_string("video/x-raw,format=BGRx");
        g_object_set(sink,
                 "caps", caps,
                 "sync", FALSE,
                 "drop", FALSE,
                 NULL);
        gst_caps_unref(caps);
        g_object_set(playbin,
                 "video-sink", gst_object_ref(sink),
                 NULL);

        decoder->appsink = GST_APP_SINK(sink);
    } else {
        /* handle has received, it means playbin will render directly into
         * widget using the gstvideoooverlay interface instead of app-sink.
         * Also avoid using vaapisink if exist since vaapisink could be
         * buggy when it is combined with playbin. changing its rank to
         * none will make playbin to avoid of using it.
         */
        GstRegistry *registry = NULL;
        GstPluginFeature *vaapisink = NULL;

        registry = gst_registry_get();
        if (registry) {
            vaapisink = gst_registry_lookup_feature(registry, "vaapisink");
        }
        if (vaapisink) {
            gst_plugin_feature_set_rank(vaapisink, GST_RANK_NONE);
            gst_object_unref(vaapisink);
        }
    }

    g_signal_connect(playbin, "source-setup", G_CALLBACK(app_source_setup), decoder);

    g_object_set(playbin,
                 "uri", "appsrc://",
                 NULL);

    /* Disable audio in playbin */
    g_object_get(playbin, "flags", &flags, NULL);
    flags &= ~(GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT);
    g_object_set(playbin, "flags", flags, NULL);

    g_warn_if_fail(decoder->appsrc == NULL);
    decoder->pipeline = playbin;
#else
    gchar *desc;
    GError *err = NULL;

    /* - We schedule the frame display ourselves so set sync=false on appsink
     *   so the pipeline decodes them as fast as possible. This will also
     *   minimize the risk of frames getting lost when we rebuild the
     *   pipeline.
     * - Set max-bytes=0 on appsrc so it does not drop frames that may be
     *   needed by those that follow.
     */
    desc = g_strdup_printf("appsrc name=src is-live=true format=time max-bytes=0 block=true "
                           "caps=%s ! %s ! videoconvert ! appsink name=sink "
                           "caps=video/x-raw,format=BGRx sync=false drop=false",
                           gst_opts[decoder->base.codec_type].dec_caps,
                           gst_opts[decoder->base.codec_type].dec_name);
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
#endif

    if (decoder->appsink) {
        GstAppSinkCallbacks appsink_cbs = { NULL };
        appsink_cbs.new_sample = new_sample;
        gst_app_sink_set_callbacks(decoder->appsink, &appsink_cbs, decoder, NULL);
        gst_app_sink_set_max_buffers(decoder->appsink, 2);
        gst_app_sink_set_drop(decoder->appsink, FALSE);
    }
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
    guint timer_id;

    g_mutex_lock(&decoder->queues_mutex);
    timer_id = decoder->timer_id;
    decoder->timer_id = 0;
    g_mutex_unlock(&decoder->queues_mutex);

    if (timer_id != 0) {
        g_source_remove(timer_id);
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
    if (decoder->display_frame) {
        free_gst_frame(decoder->display_frame);
    }

    g_free(decoder);

    /* Don't call gst_deinit() as other parts of the client
     * may still be using GStreamer.
     */
}


/* spice_gst_decoder_queue_frame() queues the SpiceFrame for decoding and
 * displaying. The steps it goes through are as follows:
 *
 * 1) A SpiceGstFrame is created to keep track of SpiceFrame and some additional
 *    metadata. The SpiceGstFrame is then pushed to the decoding_queue.
 * 2) frame->data, which contains the compressed frame data, is reffed and
 *    wrapped in a GstBuffer which is pushed to the GStreamer pipeline for
 *    decoding.
 * 3) As soon as the GStreamer pipeline no longer needs the compressed frame it
 *    will call frame->unref_data() to free it.
 *
 * If GstVideoOverlay is used (win_handle was obtained by pipeline creation):
 *   4) Decompressed frames will be renderd to widget directly from gstreamer's pipeline
 *      using some gstreamer sink plugin which implements the GstVideoOverlay interface
 *      (last step).
 *
 * Otherwise appsink is used:
 *   4) Once the decompressed frame is available the GStreamer pipeline calls
 *      new_sample() in the GStreamer thread.
 *   5) new_sample() then matches the decompressed frame to a SpiceGstFrame from
 *      the decoding queue using the GStreamer timestamp information to deal with
 *      dropped frames. The SpiceGstFrame is popped from the decoding_queue.
 *   6) new_sample() then attaches the decompressed frame to the SpiceGstFrame,
 *      set into display_frame and calls schedule_frame().
 *   7) schedule_frame() then uses gstframe->frame->mm_time to arrange for
 *      display_frame() to be called, in the main thread, at the right time for
 *      the next frame.
 *   8) display_frame() use SpiceGstFrame from display_frame and
 *      calls stream_display_frame().
 *   9) display_frame() then frees the SpiceGstFrame, which frees the SpiceFrame
 *      and decompressed frame with it.
 */
static gboolean spice_gst_decoder_queue_frame(VideoDecoder *video_decoder,
                                              SpiceFrame *frame, int latency)
{
    SpiceGstDecoder *decoder = (SpiceGstDecoder*)video_decoder;

    if (frame->size == 0) {
        SPICE_DEBUG("got an empty frame buffer!");
        frame->free(frame);
        return TRUE;
    }

    if (spice_mmtime_diff(frame->mm_time, decoder->last_mm_time) < 0) {
        SPICE_DEBUG("new-frame-time < last-frame-time (%u < %u):"
                    " resetting stream",
                    frame->mm_time, decoder->last_mm_time);
        /* Let GStreamer deal with the frame anyway */
    }
    decoder->last_mm_time = frame->mm_time;

    if (latency < 0 &&
        decoder->base.codec_type == SPICE_VIDEO_CODEC_TYPE_MJPEG) {
        /* Dropping MJPEG frames has no impact on those that follow and
         * saves CPU so do it.
         */
        SPICE_DEBUG("dropping a late MJPEG frame");
        frame->free(frame);
        return TRUE;
    }

    if (decoder->pipeline == NULL) {
        /* An error occurred, causing the GStreamer pipeline to be freed */
        spice_warning("An error occurred, stopping the video stream");
        frame->free(frame);
        return FALSE;
    }

#if GST_CHECK_VERSION(1,9,0)
    if (decoder->appsrc == NULL) {
        spice_warning("Error: Playbin has not yet initialized the Appsrc element");
        stream_dropped_frame_on_playback(decoder->base.stream);
        frame->free(frame);
        return TRUE;
    }
#endif

    /* ref() the frame data for the buffer */
    frame->ref_data(frame->data_opaque);
    GstBuffer *buffer = gst_buffer_new_wrapped_full(GST_MEMORY_FLAG_PHYSICALLY_CONTIGUOUS,
                                                    frame->data, frame->size, 0, frame->size,
                                                    frame->data_opaque, frame->unref_data);

    GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    GST_BUFFER_PTS(buffer) = gst_clock_get_time(decoder->clock) - gst_element_get_base_time(decoder->pipeline) + ((uint64_t)MAX(0, latency)) * 1000 * 1000;

    if (decoder->appsink != NULL) {
        SpiceGstFrame *gst_frame = create_gst_frame(buffer, frame);
        g_mutex_lock(&decoder->queues_mutex);
        g_queue_push_tail(decoder->decoding_queue, gst_frame);
        g_mutex_unlock(&decoder->queues_mutex);
    } else {
        frame->free(frame);
        frame = NULL;
    }

    if (gst_app_src_push_buffer(decoder->appsrc, buffer) != GST_FLOW_OK) {
        SPICE_DEBUG("GStreamer error: unable to push frame");
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

    g_return_val_if_fail(VALID_VIDEO_CODEC_TYPE(codec_type), NULL);

    if (gstvideo_init()) {
        decoder = g_new0(SpiceGstDecoder, 1);
        decoder->base.destroy = spice_gst_decoder_destroy;
        decoder->base.reschedule = spice_gst_decoder_reschedule;
        decoder->base.queue_frame = spice_gst_decoder_queue_frame;
        decoder->base.codec_type = codec_type;
        decoder->base.stream = stream;
        g_mutex_init(&decoder->queues_mutex);
        decoder->decoding_queue = g_queue_new();

        if (!create_pipeline(decoder)) {
            decoder->base.destroy((VideoDecoder*)decoder);
            decoder = NULL;
        }
    }

    return (VideoDecoder*)decoder;
}

static void gstvideo_debug_available_decoders(int codec_type,
                                              GList *all_decoders,
                                              GList *codec_decoders)
{
    GList *l;
    GString *msg = g_string_new(NULL);
    /* Print list of available decoders to make debugging easier */
    g_string_printf(msg, "From %3u video decoder elements, %2u can handle caps %12s: ",
                    g_list_length(all_decoders), g_list_length(codec_decoders),
                    gst_opts[codec_type].dec_caps);

    for (l = codec_decoders; l != NULL; l = l->next) {
        GstPluginFeature *pfeat = GST_PLUGIN_FEATURE(l->data);
        g_string_append_printf(msg, "%s, ", gst_plugin_feature_get_name(pfeat));
    }

    /* Drop trailing ", " */
    g_string_truncate(msg, msg->len - 2);
    spice_debug("%s", msg->str);
    g_string_free(msg, TRUE);
}

G_GNUC_INTERNAL
gboolean gstvideo_has_codec(int codec_type)
{
    GList *all_decoders, *codec_decoders;
    GstCaps *caps;
    GstElementFactoryListType type;

    g_return_val_if_fail(gstvideo_init(), FALSE);
    g_return_val_if_fail(VALID_VIDEO_CODEC_TYPE(codec_type), FALSE);

    type = GST_ELEMENT_FACTORY_TYPE_DECODER |
           GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO |
           GST_ELEMENT_FACTORY_TYPE_MEDIA_IMAGE;
    all_decoders = gst_element_factory_list_get_elements(type, GST_RANK_NONE);
    if (all_decoders == NULL) {
        spice_warning("No video decoders from GStreamer were found");
        return FALSE;
    }

    caps = gst_caps_from_string(gst_opts[codec_type].dec_caps);
    codec_decoders = gst_element_factory_list_filter(all_decoders, caps, GST_PAD_SINK, FALSE);
    gst_caps_unref(caps);

    if (codec_decoders == NULL) {
        spice_debug("From %u decoders, none can handle '%s'",
                    g_list_length(all_decoders), gst_opts[codec_type].dec_caps);
        gst_plugin_feature_list_free(all_decoders);
        return FALSE;
    }

    if (spice_util_get_debug())
        gstvideo_debug_available_decoders(codec_type, all_decoders, codec_decoders);

    gst_plugin_feature_list_free(codec_decoders);
    gst_plugin_feature_list_free(all_decoders);
    return TRUE;
}
