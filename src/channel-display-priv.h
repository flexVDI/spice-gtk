/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010 Red Hat, Inc.

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
#ifndef CHANNEL_DISPLAY_PRIV_H_
# define CHANNEL_DISPLAY_PRIV_H_

#include <pixman.h>
#ifdef WIN32
/* We need some hacks to avoid warnings from the jpeg headers */
#define HAVE_BOOLEAN
#define XMD_H
#endif
#include <jpeglib.h>

#include "common/canvas_utils.h"
#include "client_sw_canvas.h"
#include "common/ring.h"
#include "common/quic.h"
#include "common/rop3.h"

G_BEGIN_DECLS

typedef struct display_stream display_stream;

typedef struct SpiceFrame SpiceFrame;
struct SpiceFrame {
    uint32_t mm_time;
    SpiceRect dest;

    uint8_t *data;
    uint32_t size;
    gpointer data_opaque;
    void (*ref_data)(gpointer data_opaque);
    void (*unref_data)(gpointer data_opaque);

    void (*free)(SpiceFrame *frame);
};

typedef struct VideoDecoder VideoDecoder;
struct VideoDecoder {
    /* Releases the video decoder's resources */
    void (*destroy)(VideoDecoder *decoder);

    /* Notifies the decoder that the mm-time clock changed. */
    void (*reschedule)(VideoDecoder *decoder);

    /* Takes ownership of the specified frame, decompresses it,
     * and displays it at the right time.
     *
     * @decoder:   The video decoder.
     * @frame:     The compressed Spice frame.
     * @latency:   How long in milliseconds until the frame should be
     *             displayed. Negative values mean the frame is late.
     * @return:    False if the decoder can no longer decode frames,
     *             True otherwise.
     */
    gboolean (*queue_frame)(VideoDecoder *video_decoder, SpiceFrame *frame, int latency);

    /* The format of the encoded video. */
    int codec_type;

    /* The associated display stream. */
    display_stream *stream;
};


/* Instantiates the video decoder for the specified codec.
 *
 * @codec_type: The format of the video.
 * @stream:     The associated video stream.
 * @return:     A pointer to a structure implementing the VideoDecoder methods.
 */
#ifdef HAVE_BUILTIN_MJPEG
VideoDecoder* create_mjpeg_decoder(int codec_type, display_stream *stream);
#endif
#ifdef HAVE_GSTVIDEO
VideoDecoder* create_gstreamer_decoder(int codec_type, display_stream *stream);
gboolean gstvideo_has_codec(int codec_type);
#else
# define gstvideo_has_codec(codec_type) FALSE
#endif


typedef struct display_surface {
    guint32                     surface_id;
    bool                        primary;
    bool                        streaming_mode;
    enum SpiceSurfaceFmt        format;
    int                         width, height, stride, size;
    uint8_t                     *data;
    SpiceCanvas                 *canvas;
    SpiceGlzDecoder             *glz_decoder;
    SpiceZlibDecoder            *zlib_decoder;
    SpiceJpegDecoder            *jpeg_decoder;
} display_surface;

typedef struct drops_sequence_stats {
    uint32_t len;
    uint32_t start_mm_time;
    uint32_t duration;
} drops_sequence_stats;

struct display_stream {
    /* from messages */
    uint32_t                    id;
    uint32_t                    flags;
    SpiceRect                   dest;
    display_surface             *surface;
    SpiceClip                   clip;
    QRegion                     region;
    int                         have_region;

    VideoDecoder                *video_decoder;

    SpiceChannel                *channel;

    /* stats */
    uint32_t             first_frame_mm_time;
    uint32_t             arrive_late_count;
    uint64_t             arrive_late_time;
    uint32_t             num_drops_on_playback;
    uint32_t             num_input_frames;
    drops_sequence_stats cur_drops_seq_stats;
    GArray               *drops_seqs_stats_arr;
    uint32_t             num_drops_seqs;

    uint32_t             playback_sync_drops_seq_len;

    /* playback quality report to server */
    gboolean report_is_active;
    uint32_t report_id;
    uint32_t report_max_window;
    uint32_t report_timeout;
    uint64_t report_start_time;
    uint32_t report_start_frame_time;
    uint32_t report_num_frames;
    uint32_t report_num_drops;
    uint32_t report_drops_seq_len;
};

static const struct {
    int cap;
    const gchar name[8];

    /* FIXME: With gstreamer version 1.9.0 and higher, we are using playbin to
     * create the pipeline for us and for that reason we don't need to keep track of
     * decoder's name anymore. */
    const gchar *dec_name;
    const gchar *dec_caps;
} gst_opts[] = {
    /* SpiceVideoCodecType starts at index 1 */
    { 0 },

    /* SPICE_VIDEO_CODEC_TYPE_MJPEG */
    { SPICE_DISPLAY_CAP_CODEC_MJPEG, "mjpeg",
      "jpegdec", "image/jpeg" },

    /* SPICE_VIDEO_CODEC_TYPE_VP8
     *
     * typefind is unable to identify VP8 streams by design.
     * See: https://bugzilla.gnome.org/show_bug.cgi?id=756457
     */
    { SPICE_DISPLAY_CAP_CODEC_VP8, "vp8",
      "vp8dec", "video/x-vp8" },

    /* SPICE_VIDEO_CODEC_TYPE_H264
     * When setting video/x-h264, h264parse will complain if we don't have the
     * stream-format or codec_data information. As stream-format is byte-stream
     * (hardcoded in spice-server), let's add it here to avoid the warning.
     */
    { SPICE_DISPLAY_CAP_CODEC_H264, "h264",
      "h264parse ! avdec_h264", "video/x-h264" },

    /* SPICE_VIDEO_CODEC_TYPE_VP9 */
    { SPICE_DISPLAY_CAP_CODEC_VP9, "vp9",
      "vp9dec", "video/x-vp9" },
};

G_STATIC_ASSERT(G_N_ELEMENTS(gst_opts) <= SPICE_VIDEO_CODEC_TYPE_ENUM_END);

guint32 stream_get_time(display_stream *st);
void stream_dropped_frame_on_playback(display_stream *st);
#define SPICE_UNKNOWN_STRIDE 0
void stream_display_frame(display_stream *st, SpiceFrame *frame, uint32_t width, uint32_t height, int stride, uint8_t* data);
guintptr get_window_handle(display_stream *st);


G_END_DECLS

#endif // CHANNEL_DISPLAY_PRIV_H_
