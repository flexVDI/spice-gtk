#include <pixman.h>
#include <jpeglib.h>

/* spice/common */
#include "canvas_base.h"
#include "canvas_utils.h"
#include "sw_canvas.h"
#include "ring.h"
#include "quic.h"
#include "rop3.h"

#define DISPLAY_PIXMAP_CACHE (1024 * 1024 * 32)
#define GLZ_WINDOW_SIZE      (1024 * 1024 * 16)

typedef struct display_surface {
    RingItem                    link;
    int                         surface_id;
    bool                        primary;
    enum SpiceSurfaceFmt        format;
    int                         width, height, stride, size;
    int                         shmid;
    uint8_t                     *data;
    SpiceCanvas                 *canvas;
    SpiceGlzDecoder             *glz_decoder;
} display_surface;

typedef struct display_stream {
    spice_msg_in                *msg_create;
    spice_msg_in                *msg_clip;
    spice_msg_in                *msg_data;

    /* from messages */
    display_surface             *surface;
    SpiceClip                   *clip;
    int                         codec;

    /* mjpeg decoder */
    struct jpeg_source_mgr         mjpeg_src;
    struct jpeg_decompress_struct  mjpeg_cinfo;
    struct jpeg_error_mgr          mjpeg_jerr;

    uint8_t                     *out_frame;
} display_stream;

/* channel-display-mjpeg.c */
void stream_mjpeg_init(display_stream *st);
void stream_mjpeg_data(display_stream *st);
void stream_mjpeg_cleanup(display_stream *st);
