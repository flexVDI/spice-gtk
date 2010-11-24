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
#include "spice-client.h"
#include "spice-common.h"

#include "spice-marshal.h"
#include "spice-channel-cache.h"
#include "spice-channel-priv.h"
#include "channel-display-priv.h"
#include "decode.h"

#include <sys/ipc.h>
#include <sys/shm.h>

#define SPICE_DISPLAY_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_DISPLAY_CHANNEL, spice_display_channel))

struct spice_display_channel {
    Ring                        surfaces;
    display_cache               images;
    display_cache               palettes;
    SpiceImageCache             image_cache;
    SpicePaletteCache           palette_cache;
    SpiceGlzDecoderWindow       *glz_window;
    display_stream              **streams;
    int                         nstreams;
    gboolean                    mark;
};

G_DEFINE_TYPE(SpiceDisplayChannel, spice_display_channel, SPICE_TYPE_CHANNEL)

enum {
    SPICE_DISPLAY_PRIMARY_CREATE,
    SPICE_DISPLAY_PRIMARY_DESTROY,
    SPICE_DISPLAY_INVALIDATE,
    SPICE_DISPLAY_MARK,

    SPICE_DISPLAY_LAST_SIGNAL,
};

static guint signals[SPICE_DISPLAY_LAST_SIGNAL];

static void spice_display_handle_msg(SpiceChannel *channel, spice_msg_in *msg);
static void spice_display_channel_init(SpiceDisplayChannel *channel);
static void spice_display_channel_up(SpiceChannel *channel);

static void palette_clear(SpicePaletteCache *cache);
static void image_clear(SpiceImageCache *cache);
static void clear_surfaces(SpiceChannel *channel);
static void clear_streams(SpiceChannel *channel);

/* ------------------------------------------------------------------ */

static void spice_display_channel_finalize(GObject *obj)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(obj)->priv;

    palette_clear(&c->palette_cache);
    image_clear(&c->image_cache);
    clear_surfaces(SPICE_CHANNEL(obj));
    clear_streams(SPICE_CHANNEL(obj));
    glz_decoder_window_destroy(c->glz_window);

    if (G_OBJECT_CLASS(spice_display_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_display_channel_parent_class)->finalize(obj);
}

static void spice_display_channel_class_init(SpiceDisplayChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->finalize     = spice_display_channel_finalize;
    channel_class->handle_msg   = spice_display_handle_msg;
    channel_class->channel_up   = spice_display_channel_up;

    signals[SPICE_DISPLAY_PRIMARY_CREATE] =
        g_signal_new("display-primary-create",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     spice_display_primary_create),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__INT_INT_INT_INT_INT_POINTER,
                     G_TYPE_NONE,
                     6,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_POINTER);

    signals[SPICE_DISPLAY_PRIMARY_DESTROY] =
        g_signal_new("display-primary-destroy",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     spice_display_primary_destroy),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    signals[SPICE_DISPLAY_INVALIDATE] =
        g_signal_new("display-invalidate",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     spice_display_invalidate),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__INT_INT_INT_INT,
                     G_TYPE_NONE,
                     4,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

    signals[SPICE_DISPLAY_MARK] =
        g_signal_new("display-mark",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     spice_display_mark),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    g_type_class_add_private(klass, sizeof(spice_display_channel));

    sw_canvas_init();
    quic_init();
    rop3_init();
}

/* ------------------------------------------------------------------ */

static void image_put(SpiceImageCache *cache, uint64_t id, pixman_image_t *image)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, image_cache);
    display_cache_item *item;

#if 1 /* temporary sanity check */
    item = cache_find(&c->images, id);
    g_return_if_fail(item == NULL);
#endif

    item = cache_add(&c->images, id);
    item->ptr = pixman_image_ref(image);
}

static pixman_image_t *image_get(SpiceImageCache *cache, uint64_t id)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, image_cache);
    display_cache_item *item;

    item = cache_find(&c->images, id);
    if (item) {
        cache_used(&c->images, item);
        return pixman_image_ref(item->ptr);
    }
    return NULL;
}

static void image_remove(SpiceImageCache *cache, uint64_t id)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, image_cache);
    display_cache_item *item;

    item = cache_find(&c->images, id);
    g_return_if_fail(item != NULL);
    pixman_image_unref(item->ptr);
    cache_del(&c->images, item);
}

static void image_clear(SpiceImageCache *cache)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, image_cache);
    display_cache_item *item;

    for (;;) {
        item = cache_get_lru(&c->images);
        if (item == NULL) {
            break;
        }
        pixman_image_unref(item->ptr);
        cache_del(&c->images, item);
    }
}

static void palette_put(SpicePaletteCache *cache, SpicePalette *palette)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, palette_cache);
    display_cache_item *item;

    item = cache_add(&c->palettes, palette->unique);
    item->ptr = palette;
}

static SpicePalette *palette_get(SpicePaletteCache *cache, uint64_t id)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, palette_cache);
    display_cache_item *item;

    item = cache_find(&c->palettes, id);
    if (item) {
        cache_ref(item);
        return item->ptr;
    }
    return NULL;
}

static void palette_remove(SpicePaletteCache *cache, uint32_t id)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, palette_cache);
    display_cache_item *item;

    item = cache_find(&c->palettes, id);
    if (item) {
        if (cache_unref(item)) {
            cache_del(&c->palettes, item);
        }
    }
}

static void palette_clear(SpicePaletteCache *cache)
{
    spice_display_channel *c =
        SPICE_CONTAINEROF(cache, spice_display_channel, palette_cache);
    display_cache_item *item;

    for (;;) {
        item = cache_get_lru(&c->palettes);
        if (item == NULL) {
            break;
        }
        cache_del(&c->palettes, item);
    }
}

static void palette_release(SpicePaletteCache *cache, SpicePalette *palette)
{
    palette_remove(cache, palette->unique);
}

static SpiceImageCacheOps image_cache_ops = {
    .put = image_put,
    .get = image_get,
};

static SpicePaletteCacheOps palette_cache_ops = {
    .put     = palette_put,
    .get     = palette_get,
    .release = palette_release,
};

static void spice_display_channel_init(SpiceDisplayChannel *channel)
{
    spice_display_channel *c;

    c = channel->priv = SPICE_DISPLAY_CHANNEL_GET_PRIVATE(channel);
    memset(c, 0, sizeof(*c));

    ring_init(&c->surfaces);
    cache_init(&c->images, "image");
    cache_init(&c->palettes, "palette");
    c->image_cache.ops = &image_cache_ops;
    c->palette_cache.ops = &palette_cache_ops;
}

/* ------------------------------------------------------------------ */

static int create_canvas(SpiceChannel *channel, display_surface *surface)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    if (surface->primary) {
        surface->shmid = shmget(IPC_PRIVATE, surface->size, IPC_CREAT | 0777);
        if (surface->shmid >= 0) {
            surface->data = shmat(surface->shmid, 0, 0);
            if (surface->data == NULL) {
                shmctl(surface->shmid, IPC_RMID, 0);
                surface->shmid = -1;
            }
        }
    } else {
        surface->shmid = -1;
    }

    if (surface->shmid == -1) {
        surface->data = spice_malloc(surface->size);
    }

    if (!c->glz_window) {
        c->glz_window = glz_decoder_window_new();
    }
    surface->glz_decoder = glz_decoder_new(c->glz_window);

    surface->canvas = canvas_create_for_data(surface->width,
                                             surface->height,
                                             surface->format,
                                             surface->data,
                                             surface->stride,
#ifdef SW_CANVAS_CACHE
                                             &c->image_cache,
                                             &c->palette_cache,
#endif
                                             NULL, // &csurfaces.base,
                                             surface->glz_decoder,
                                             NULL, // &jpeg_decoder(),
                                             NULL); // &zlib_decoder());

    g_return_val_if_fail(surface->canvas != NULL, 0);
    return 0;
}

static void destroy_canvas(display_surface *surface)
{
    if (surface == NULL)
        return;

    glz_decoder_destroy(surface->glz_decoder);

    if (surface->shmid == -1) {
        free(surface->data);
    } else {
        shmdt(surface->data);
    }
    surface->shmid = -1;
    surface->data = NULL;

    surface->canvas->ops->destroy(surface->canvas);
    surface->canvas = NULL;
}

static display_surface *find_surface(SpiceChannel *channel, int surface_id)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_surface *surface;
    RingItem *item;

    for (item = ring_get_head(&c->surfaces);
         item != NULL;
         item = ring_next(&c->surfaces, item)) {
        surface = SPICE_CONTAINEROF(item, display_surface, link);
        if (surface->surface_id == surface_id)
            return surface;
    }
    return NULL;
}

static void clear_surfaces(SpiceChannel *channel)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_surface *surface;
    RingItem *item;

    while (!ring_is_empty(&c->surfaces)) {
        item = ring_get_head(&c->surfaces);
        surface = SPICE_CONTAINEROF(item, display_surface, link);
        ring_remove(&surface->link);
        destroy_canvas(surface);
        free(surface);
    }
}

static void emit_invalidate(SpiceChannel *channel, SpiceRect *bbox)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    if (!c->mark)
        return;
    g_signal_emit(channel, signals[SPICE_DISPLAY_INVALIDATE], 0,
                  bbox->left, bbox->top,
                  bbox->right - bbox->left,
                  bbox->bottom - bbox->top);
}

/* ------------------------------------------------------------------ */

static void spice_display_channel_up(SpiceChannel *channel)
{
    spice_msg_out *out;
    SpiceMsgcDisplayInit init = {
        .pixmap_cache_id            = 1,
        .pixmap_cache_size          = DISPLAY_PIXMAP_CACHE,
        .glz_dictionary_id          = 1,
        .glz_dictionary_window_size = GLZ_WINDOW_SIZE,
    };

    out = spice_msg_out_new(channel, SPICE_MSGC_DISPLAY_INIT);
    out->marshallers->msgc_display_init(out->marshaller, &init);
    spice_msg_out_send(out);
    spice_msg_out_unref(out);
}

#define DRAW(type) {                                                    \
        display_surface *surface =                                      \
            find_surface(channel, op->base.surface_id);                 \
        g_return_if_fail(surface != NULL);                                        \
        surface->canvas->ops->draw_##type(surface->canvas, &op->base.box, \
                                          &op->base.clip, &op->data);   \
        if (surface->primary) {                                         \
            emit_invalidate(channel, &op->base.box);                    \
        }                                                               \
}

static void display_handle_mode(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayMode *mode = spice_msg_in_parsed(in);
    display_surface *surface = find_surface(channel, 0);

    g_warn_if_fail(c->mark == FALSE);

    if (surface) {
        g_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_DESTROY], 0);
        ring_remove(&surface->link);
        destroy_canvas(surface);
        free(surface);
    }

    surface = spice_new0(display_surface, 1);
    surface->format  = mode->bits == 32 ?
        SPICE_SURFACE_FMT_32_xRGB : SPICE_SURFACE_FMT_16_555;
    surface->width   = mode->x_res;
    surface->height  = mode->y_res;
    surface->stride  = surface->width * 4;
    surface->size    = surface->height * surface->stride;
    surface->primary = true;
    create_canvas(channel, surface);
    g_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_CREATE], 0,
                  surface->format, surface->width, surface->height,
                  surface->stride, surface->shmid, surface->data);
    if (surface->shmid != -1) {
        shmctl(surface->shmid, IPC_RMID, 0);
    }
    ring_add(&c->surfaces, &surface->link);
}

static void display_handle_mark(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_surface *surface = find_surface(channel, 0);

    SPICE_DEBUG("%s", __FUNCTION__);
    g_return_if_fail(surface != NULL);
    g_warn_if_fail(c->mark == FALSE);

    c->mark = TRUE;
    g_signal_emit(channel, signals[SPICE_DISPLAY_MARK], 0, TRUE);
}

static void display_handle_reset(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    g_warning("%s: TODO", __FUNCTION__);
    c->mark = FALSE;
    g_signal_emit(channel, signals[SPICE_DISPLAY_MARK], 0, FALSE);
}

static void display_handle_copy_bits(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayCopyBits *op = spice_msg_in_parsed(in);
    display_surface *surface = find_surface(channel, op->base.surface_id);

    g_return_if_fail(surface != NULL);
    surface->canvas->ops->copy_bits(surface->canvas, &op->base.box,
                                    &op->base.clip, &op->src_pos);
    if (surface->primary) {
        emit_invalidate(channel, &op->base.box);
    }
}

static void display_handle_inv_list(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceResourceList *list = spice_msg_in_parsed(in);
    int i;

    for (i = 0; i < list->count; i++) {
        switch (list->resources[i].type) {
        case SPICE_RES_TYPE_PIXMAP:
            image_remove(&c->image_cache, list->resources[i].id);
            break;
        default:
            g_return_if_reached();
            break;
        }
    }
}

static void display_handle_inv_pixmap_all(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    image_clear(&c->image_cache);
}

static void display_handle_inv_palette(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayInvalOne* op = spice_msg_in_parsed(in);

    palette_remove(&c->palette_cache, op->id);
}

static void display_handle_inv_palette_all(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    palette_clear(&c->palette_cache);
}

/* ------------------------------------------------------------------ */

static void display_update_stream_region(display_stream *st)
{
    int i;

    switch (st->clip->type) {
    case SPICE_CLIP_TYPE_RECTS:
        region_clear(&st->region);
        for (i = 0; i < st->clip->rects->num_rects; i++) {
            region_add(&st->region, &st->clip->rects->rects[i]);
        }
        st->have_region = true;
        break;
    case SPICE_CLIP_TYPE_NONE:
    default:
        st->have_region = false;
        break;
    }
}

static void display_handle_stream_create(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayStreamCreate *op = spice_msg_in_parsed(in);
    display_stream *st;

    g_message("%s: id %d", __FUNCTION__, op->id);

    if (op->id >= c->nstreams) {
        int n = c->nstreams;
        if (!c->nstreams) {
            c->nstreams = 1;
        }
        while (op->id >= c->nstreams) {
            c->nstreams *= 2;
        }
        c->streams = realloc(c->streams, c->nstreams * sizeof(c->streams[0]));
        memset(c->streams + n, 0, (c->nstreams - n) * sizeof(c->streams[0]));
    }
    g_return_if_fail(c->streams[op->id] == NULL);
    c->streams[op->id] = spice_new0(display_stream, 1);
    st = c->streams[op->id];

    st->msg_create = in;
    spice_msg_in_ref(in);
    st->clip = &op->clip;
    st->codec = op->codec_type;
    st->surface = find_surface(channel, op->surface_id);

    region_init(&st->region);
    display_update_stream_region(st);

    switch (st->codec) {
    case SPICE_VIDEO_CODEC_TYPE_MJPEG:
        stream_mjpeg_init(st);
        break;
    }
}

static void display_handle_stream_data(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayStreamData *op = spice_msg_in_parsed(in);
    display_stream *st = c->streams[op->id];

    st->msg_data = in;

    switch (st->codec) {
    case SPICE_VIDEO_CODEC_TYPE_MJPEG:
        stream_mjpeg_data(st);
        break;
    }

    if (st->out_frame) {
        SpiceMsgDisplayStreamCreate *info = spice_msg_in_parsed(st->msg_create);
        uint8_t *data;
        int stride;

        data = st->out_frame;
        stride = info->src_width * sizeof(uint32_t);
        if (!(info->flags & SPICE_STREAM_FLAGS_TOP_DOWN)) {
            data += stride * (info->src_height - 1);
            stride = -stride;
        }
        st->surface->canvas->ops->put_image
            (st->surface->canvas, &info->dest, data,
             info->src_width, info->src_height, stride,
             st->have_region ? &st->region : NULL);
        if (st->surface->primary)
            emit_invalidate(channel, &info->dest);
    }

    st->msg_data = NULL;
}

static void display_handle_stream_clip(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayStreamClip *op = spice_msg_in_parsed(in);
    display_stream *st = c->streams[op->id];

    if (st->msg_clip) {
        spice_msg_in_unref(st->msg_clip);
    }
    spice_msg_in_ref(in);
    st->msg_clip = in;
    st->clip = &op->clip;
    display_update_stream_region(st);
}

static void destroy_stream(SpiceChannel *channel, int id)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_stream *st;

    g_return_if_fail(c != NULL);

    st = c->streams[id];
    if (!st)
        return;

    switch (st->codec) {
    case SPICE_VIDEO_CODEC_TYPE_MJPEG:
        stream_mjpeg_cleanup(st);
        break;
    }

    if (st->msg_clip)
        spice_msg_in_unref(st->msg_clip);
    spice_msg_in_unref(st->msg_create);
    free(st);
    c->streams[id] = NULL;
}

static void clear_streams(SpiceChannel *channel)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    int i;

    for (i = 0; i < c->nstreams; i++) {
        destroy_stream(channel, i);
    }
    free(c->streams);
    c->streams = NULL;
    c->nstreams = 0;
}

static void display_handle_stream_destroy(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayStreamDestroy *op = spice_msg_in_parsed(in);

    g_return_if_fail(op != NULL);
    g_message("%s: id %d", __FUNCTION__, op->id);
    destroy_stream(channel, op->id);
}

static void display_handle_stream_destroy_all(SpiceChannel *channel, spice_msg_in *in)
{
    clear_streams(channel);
}

/* ------------------------------------------------------------------ */

static void display_handle_draw_fill(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawFill *op = spice_msg_in_parsed(in);
    DRAW(fill);
}

static void display_handle_draw_opaque(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawOpaque *op = spice_msg_in_parsed(in);
    DRAW(opaque);
}

static void display_handle_draw_copy(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawCopy *op = spice_msg_in_parsed(in);
    DRAW(copy);
}

static void display_handle_draw_blend(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawBlend *op = spice_msg_in_parsed(in);
    DRAW(blend);
}

static void display_handle_draw_blackness(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawBlackness *op = spice_msg_in_parsed(in);
    DRAW(blackness);
}

static void display_handle_draw_whiteness(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawWhiteness *op = spice_msg_in_parsed(in);
    DRAW(whiteness);
}

static void display_handle_draw_invers(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawInvers *op = spice_msg_in_parsed(in);
    DRAW(invers);
}

static void display_handle_draw_rop3(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawRop3 *op = spice_msg_in_parsed(in);
    DRAW(rop3);
}

static void display_handle_draw_stroke(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawStroke *op = spice_msg_in_parsed(in);
    DRAW(stroke);
}

static void display_handle_draw_text(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawText *op = spice_msg_in_parsed(in);
    DRAW(text);
}

static void display_handle_draw_transparent(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawTransparent *op = spice_msg_in_parsed(in);
    DRAW(transparent);
}

static void display_handle_draw_alpha_blend(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgDisplayDrawAlphaBlend *op = spice_msg_in_parsed(in);
    DRAW(alpha_blend);
}

static void display_handle_surface_create(SpiceChannel *channel, spice_msg_in *in)
{
    spice_display_channel *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgSurfaceCreate *create = spice_msg_in_parsed(in);
    display_surface *surface = spice_new0(display_surface, 1);

    surface->surface_id = create->surface_id;
    surface->format = create->format;
    surface->width  = create->width;
    surface->height = create->height;
    surface->stride = create->width * 4;
    surface->size   = surface->height * surface->stride;

    if (create->flags == SPICE_SURFACE_FLAGS_PRIMARY) {
        surface->primary = true;
        create_canvas(channel, surface);
        g_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_CREATE], 0,
                      surface->format, surface->width, surface->height,
                      surface->stride, surface->shmid, surface->data);
    } else {
        surface->primary = false;
        create_canvas(channel, surface);
    }

    ring_add(&c->surfaces, &surface->link);
}

static void display_handle_surface_destroy(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgSurfaceDestroy *destroy = spice_msg_in_parsed(in);
    display_surface *surface;

    g_return_if_fail(destroy != NULL);

    surface = find_surface(channel, destroy->surface_id);
    if (surface->primary) {
        g_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_DESTROY], 0);
    }

    ring_remove(&surface->link);
    destroy_canvas(surface);
    free(surface);
}

static spice_msg_handler display_handlers[] = {
    [ SPICE_MSG_SET_ACK ]                    = spice_channel_handle_set_ack,
    [ SPICE_MSG_PING ]                       = spice_channel_handle_ping,
    [ SPICE_MSG_NOTIFY ]                     = spice_channel_handle_notify,
    [ SPICE_MSG_DISCONNECTING ]              = spice_channel_handle_disconnect,
    [ SPICE_MSG_WAIT_FOR_CHANNELS ]          = spice_channel_handle_wait_for_channels,
    [ SPICE_MSG_MIGRATE ]                    = spice_channel_handle_migrate,

    [ SPICE_MSG_DISPLAY_MODE ]               = display_handle_mode,
    [ SPICE_MSG_DISPLAY_MARK ]               = display_handle_mark,
    [ SPICE_MSG_DISPLAY_RESET ]              = display_handle_reset,
    [ SPICE_MSG_DISPLAY_COPY_BITS ]          = display_handle_copy_bits,
    [ SPICE_MSG_DISPLAY_INVAL_LIST ]         = display_handle_inv_list,
    [ SPICE_MSG_DISPLAY_INVAL_ALL_PIXMAPS ]  = display_handle_inv_pixmap_all,
    [ SPICE_MSG_DISPLAY_INVAL_PALETTE ]      = display_handle_inv_palette,
    [ SPICE_MSG_DISPLAY_INVAL_ALL_PALETTES ] = display_handle_inv_palette_all,

    [ SPICE_MSG_DISPLAY_STREAM_CREATE ]      = display_handle_stream_create,
    [ SPICE_MSG_DISPLAY_STREAM_DATA ]        = display_handle_stream_data,
    [ SPICE_MSG_DISPLAY_STREAM_CLIP ]        = display_handle_stream_clip,
    [ SPICE_MSG_DISPLAY_STREAM_DESTROY ]     = display_handle_stream_destroy,
    [ SPICE_MSG_DISPLAY_STREAM_DESTROY_ALL ] = display_handle_stream_destroy_all,

    [ SPICE_MSG_DISPLAY_DRAW_FILL ]          = display_handle_draw_fill,
    [ SPICE_MSG_DISPLAY_DRAW_OPAQUE ]        = display_handle_draw_opaque,
    [ SPICE_MSG_DISPLAY_DRAW_COPY ]          = display_handle_draw_copy,
    [ SPICE_MSG_DISPLAY_DRAW_BLEND ]         = display_handle_draw_blend,
    [ SPICE_MSG_DISPLAY_DRAW_BLACKNESS ]     = display_handle_draw_blackness,
    [ SPICE_MSG_DISPLAY_DRAW_WHITENESS ]     = display_handle_draw_whiteness,
    [ SPICE_MSG_DISPLAY_DRAW_INVERS ]        = display_handle_draw_invers,
    [ SPICE_MSG_DISPLAY_DRAW_ROP3 ]          = display_handle_draw_rop3,
    [ SPICE_MSG_DISPLAY_DRAW_STROKE ]        = display_handle_draw_stroke,
    [ SPICE_MSG_DISPLAY_DRAW_TEXT ]          = display_handle_draw_text,
    [ SPICE_MSG_DISPLAY_DRAW_TRANSPARENT ]   = display_handle_draw_transparent,
    [ SPICE_MSG_DISPLAY_DRAW_ALPHA_BLEND ]   = display_handle_draw_alpha_blend,

    [ SPICE_MSG_DISPLAY_SURFACE_CREATE ]     = display_handle_surface_create,
    [ SPICE_MSG_DISPLAY_SURFACE_DESTROY ]    = display_handle_surface_destroy,
};

static void spice_display_handle_msg(SpiceChannel *channel, spice_msg_in *msg)
{
    int type = spice_msg_in_type(msg);
    g_return_if_fail(type < SPICE_N_ELEMENTS(display_handlers));
    g_return_if_fail(display_handlers[type] != NULL);
    display_handlers[type](channel, msg);
}

