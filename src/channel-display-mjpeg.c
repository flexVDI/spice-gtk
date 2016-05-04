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
#include "config.h"

#include "spice-client.h"
#include "spice-common.h"
#include "spice-channel-priv.h"

#include "channel-display-priv.h"


/* MJpeg decoder implementation */

typedef struct MJpegDecoder {
    VideoDecoder base;

    /* ---------- The builtin mjpeg decoder ---------- */

    SpiceMsgIn *frame_msg;
    struct jpeg_source_mgr         mjpeg_src;
    struct jpeg_decompress_struct  mjpeg_cinfo;
    struct jpeg_error_mgr          mjpeg_jerr;

    /* ---------- Output frame data ---------- */

    uint8_t *out_frame;
    uint32_t out_size;
} MJpegDecoder;


/* ---------- The JPEG library callbacks ---------- */

static void mjpeg_src_init(struct jpeg_decompress_struct *cinfo)
{
    MJpegDecoder *decoder = SPICE_CONTAINEROF(cinfo->src, MJpegDecoder, mjpeg_src);

    uint8_t *data;
    cinfo->src->bytes_in_buffer = spice_msg_in_frame_data(decoder->frame_msg, &data);
    cinfo->src->next_input_byte = data;
}

static boolean mjpeg_src_fill(struct jpeg_decompress_struct *cinfo)
{
    g_critical("need more input data");
    return 0;
}

static void mjpeg_src_skip(struct jpeg_decompress_struct *cinfo,
                           long num_bytes)
{
    cinfo->src->next_input_byte += num_bytes;
}

static void mjpeg_src_term(struct jpeg_decompress_struct *cinfo)
{
    /* nothing */
}


/* ---------- VideoDecoder's public API ---------- */

static uint8_t* mjpeg_decoder_decode_frame(VideoDecoder *video_decoder,
                                           SpiceMsgIn *frame_msg)
{
    MJpegDecoder *decoder = (MJpegDecoder*)video_decoder;
    gboolean back_compat = decoder->base.stream->channel->priv->peer_hdr.major_version == 1;
    int width;
    int height;
    uint8_t *dest;
    uint8_t *lines[4];

    decoder->frame_msg = frame_msg;
    stream_get_dimensions(decoder->base.stream, frame_msg, &width, &height);
    if (decoder->out_size < width * height * 4) {
        g_free(decoder->out_frame);
        decoder->out_size = width * height * 4;
        decoder->out_frame = g_malloc(decoder->out_size);
    }
    dest = decoder->out_frame;

    jpeg_read_header(&decoder->mjpeg_cinfo, 1);
#ifdef JCS_EXTENSIONS
    // requires jpeg-turbo
    if (back_compat)
        decoder->mjpeg_cinfo.out_color_space = JCS_EXT_RGBX;
    else
        decoder->mjpeg_cinfo.out_color_space = JCS_EXT_BGRX;
#else
#warning "You should consider building with libjpeg-turbo"
    decoder->mjpeg_cinfo.out_color_space = JCS_RGB;
#endif

#ifndef SPICE_QUALITY
    decoder->mjpeg_cinfo.dct_method = JDCT_IFAST;
    decoder->mjpeg_cinfo.do_fancy_upsampling = FALSE;
    decoder->mjpeg_cinfo.do_block_smoothing = FALSE;
    decoder->mjpeg_cinfo.dither_mode = JDITHER_ORDERED;
#endif
    // TODO: in theory should check cinfo.output_height match with our height
    jpeg_start_decompress(&decoder->mjpeg_cinfo);
    /* rec_outbuf_height is the recommended size of the output buffer we
     * pass to libjpeg for optimum performance
     */
    if (decoder->mjpeg_cinfo.rec_outbuf_height > G_N_ELEMENTS(lines)) {
        jpeg_abort_decompress(&decoder->mjpeg_cinfo);
        g_return_val_if_reached(NULL);
    }

    while (decoder->mjpeg_cinfo.output_scanline < decoder->mjpeg_cinfo.output_height) {
        /* only used when JCS_EXTENSIONS is undefined */
        G_GNUC_UNUSED unsigned int lines_read;

        for (unsigned int j = 0; j < decoder->mjpeg_cinfo.rec_outbuf_height; j++) {
            lines[j] = dest;
#ifdef JCS_EXTENSIONS
            dest += 4 * width;
#else
            dest += 3 * width;
#endif
        }
        lines_read = jpeg_read_scanlines(&decoder->mjpeg_cinfo, lines,
                                decoder->mjpeg_cinfo.rec_outbuf_height);
#ifndef JCS_EXTENSIONS
        {
            uint8_t *s = lines[0];
            uint32_t *d = (uint32_t *)s;

            if (back_compat) {
                for (unsigned int j = lines_read * width; j > 0; ) {
                    j -= 1; // reverse order, bad for cache?
                    d[j] = s[j * 3 + 0] |
                        s[j * 3 + 1] << 8 |
                        s[j * 3 + 2] << 16;
                }
            } else {
                for (unsigned int j = lines_read * width; j > 0; ) {
                    j -= 1; // reverse order, bad for cache?
                    d[j] = s[j * 3 + 0] << 16 |
                        s[j * 3 + 1] << 8 |
                        s[j * 3 + 2];
                }
            }
        }
#endif
        dest = &(decoder->out_frame[decoder->mjpeg_cinfo.output_scanline * width * 4]);
    }
    jpeg_finish_decompress(&decoder->mjpeg_cinfo);

    return decoder->out_frame;
}

static void mjpeg_decoder_destroy(VideoDecoder* video_decoder)
{
    MJpegDecoder *decoder = (MJpegDecoder*)video_decoder;
    jpeg_destroy_decompress(&decoder->mjpeg_cinfo);
    g_free(decoder->out_frame);
    free(decoder);
}

G_GNUC_INTERNAL
VideoDecoder* create_mjpeg_decoder(int codec_type, display_stream *stream)
{
    g_return_val_if_fail(codec_type == SPICE_VIDEO_CODEC_TYPE_MJPEG, NULL);

    MJpegDecoder *decoder = spice_new0(MJpegDecoder, 1);

    decoder->base.destroy = mjpeg_decoder_destroy;
    decoder->base.decode_frame = mjpeg_decoder_decode_frame;
    decoder->base.codec_type = codec_type;
    decoder->base.stream = stream;

    decoder->mjpeg_cinfo.err = jpeg_std_error(&decoder->mjpeg_jerr);
    jpeg_create_decompress(&decoder->mjpeg_cinfo);

    decoder->mjpeg_src.init_source         = mjpeg_src_init;
    decoder->mjpeg_src.fill_input_buffer   = mjpeg_src_fill;
    decoder->mjpeg_src.skip_input_data     = mjpeg_src_skip;
    decoder->mjpeg_src.resync_to_restart   = jpeg_resync_to_restart;
    decoder->mjpeg_src.term_source         = mjpeg_src_term;
    decoder->mjpeg_cinfo.src               = &decoder->mjpeg_src;

    /* All the other fields are initialized to zero by spice_new0(). */

    return (VideoDecoder*)decoder;
}
