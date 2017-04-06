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

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include "spice-client.h"
#include "spice-common.h"

#include "spice-marshal.h"
#include "spice-channel-priv.h"
#include "spice-session-priv.h"
#include "channel-display-priv.h"
#include "decode.h"

/**
 * SECTION:channel-display
 * @short_description: remote display area
 * @title: Display Channel
 * @section_id:
 * @see_also: #SpiceChannel, and the GTK widget #SpiceDisplay
 * @stability: Stable
 * @include: spice-client.h
 *
 * A class that handles the rendering of the remote display and inform
 * of its updates.
 *
 * The creation of the main graphic buffer is signaled with
 * #SpiceDisplayChannel::display-primary-create.
 *
 * The update of regions is notified by
 * #SpiceDisplayChannel::display-invalidate signals.
 */

#define SPICE_DISPLAY_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_DISPLAY_CHANNEL, SpiceDisplayChannelPrivate))

#define MONITORS_MAX 256

struct _SpiceDisplayChannelPrivate {
    GHashTable                  *surfaces;
    display_surface             *primary;
    display_cache               *images;
    display_cache               *palettes;
    SpiceImageCache             image_cache;
    SpicePaletteCache           palette_cache;
    SpiceImageSurfaces          image_surfaces;
    SpiceGlzDecoderWindow       *glz_window;
    display_stream              **streams;
    int                         nstreams;
    gboolean                    mark;
    guint                       mark_false_event_id;
    GArray                      *monitors;
    guint                       monitors_max;
    gboolean                    enable_adaptive_streaming;
#ifdef G_OS_WIN32
    HDC dc;
#endif
    SpiceGlScanout scanout;
};

G_DEFINE_TYPE(SpiceDisplayChannel, spice_display_channel, SPICE_TYPE_CHANNEL)

/* Properties */
enum {
    PROP_0,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_MONITORS,
    PROP_MONITORS_MAX,
    PROP_GL_SCANOUT,
};

enum {
    SPICE_DISPLAY_PRIMARY_CREATE,
    SPICE_DISPLAY_PRIMARY_DESTROY,
    SPICE_DISPLAY_INVALIDATE,
    SPICE_DISPLAY_MARK,
    SPICE_DISPLAY_GL_DRAW,

    SPICE_DISPLAY_LAST_SIGNAL,
};

static guint signals[SPICE_DISPLAY_LAST_SIGNAL];

static void spice_display_channel_up(SpiceChannel *channel);
static void channel_set_handlers(SpiceChannelClass *klass);

static void clear_surfaces(SpiceChannel *channel, gboolean keep_primary);
static void clear_streams(SpiceChannel *channel);
static display_surface *find_surface(SpiceDisplayChannelPrivate *c, guint32 surface_id);
static void spice_display_channel_reset(SpiceChannel *channel, gboolean migrating);
static void spice_display_channel_reset_capabilities(SpiceChannel *channel);
static void destroy_canvas(display_surface *surface);
static void destroy_stream(SpiceChannel *channel, int id);
static void display_session_mm_time_reset_cb(SpiceSession *session, gpointer data);
static SpiceGlScanout* spice_gl_scanout_copy(const SpiceGlScanout *scanout);

G_DEFINE_BOXED_TYPE(SpiceGlScanout, spice_gl_scanout,
                    (GBoxedCopyFunc)spice_gl_scanout_copy,
                    (GBoxedFreeFunc)spice_gl_scanout_free)

/* ------------------------------------------------------------------ */

static SpiceGlScanout*
spice_gl_scanout_copy(const SpiceGlScanout *scanout)
{
    SpiceGlScanout *so = g_new(SpiceGlScanout, 1);

    *so = *scanout;
    so->fd = dup(so->fd);

    return so;
}

void
spice_gl_scanout_free(SpiceGlScanout *scanout)
{
    close(scanout->fd);

    g_free(scanout);
}

static void spice_display_channel_dispose(GObject *object)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(object)->priv;

    if (c->mark_false_event_id != 0) {
        g_source_remove(c->mark_false_event_id);
        c->mark_false_event_id = 0;
    }

    if (c->scanout.fd >= 0) {
        close(c->scanout.fd);
        c->scanout.fd = -1;
    }

    if (G_OBJECT_CLASS(spice_display_channel_parent_class)->dispose)
        G_OBJECT_CLASS(spice_display_channel_parent_class)->dispose(object);
}

static void spice_display_channel_finalize(GObject *object)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(object)->priv;

    g_clear_pointer(&c->monitors, g_array_unref);
    clear_surfaces(SPICE_CHANNEL(object), FALSE);
    g_hash_table_unref(c->surfaces);
    clear_streams(SPICE_CHANNEL(object));
    g_clear_pointer(&c->palettes, cache_free);

    if (G_OBJECT_CLASS(spice_display_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_display_channel_parent_class)->finalize(object);
}

static void spice_display_channel_constructed(GObject *object)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(object)->priv;
    SpiceSession *s = spice_channel_get_session(SPICE_CHANNEL(object));

    g_return_if_fail(s != NULL);
    spice_session_get_caches(s, &c->images, &c->glz_window);
    c->palettes = cache_new(g_free);

    g_return_if_fail(c->glz_window != NULL);
    g_return_if_fail(c->images != NULL);
    g_return_if_fail(c->palettes != NULL);

    c->monitors = g_array_new(FALSE, TRUE, sizeof(SpiceDisplayMonitorConfig));
    spice_g_signal_connect_object(s, "mm-time-reset",
                                  G_CALLBACK(display_session_mm_time_reset_cb),
                                  SPICE_CHANNEL(object), 0);


    if (G_OBJECT_CLASS(spice_display_channel_parent_class)->constructed)
        G_OBJECT_CLASS(spice_display_channel_parent_class)->constructed(object);
}

static void spice_display_get_property(GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    SpiceDisplayChannel *channel = SPICE_DISPLAY_CHANNEL(object);
    SpiceDisplayChannelPrivate *c = channel->priv;

    switch (prop_id) {
    case PROP_WIDTH: {
        g_value_set_uint(value, c->primary ? c->primary->width : 0);
        break;
    }
    case PROP_HEIGHT: {
        g_value_set_uint(value, c->primary ? c->primary->height : 0);
        break;
    }
    case PROP_MONITORS: {
        g_value_set_boxed(value, c->monitors);
        break;
    }
    case PROP_MONITORS_MAX: {
        g_value_set_uint(value, c->monitors_max);
        break;
    }
    case PROP_GL_SCANOUT: {
        g_value_set_static_boxed(value, spice_display_get_gl_scanout(channel));
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void spice_display_set_property(GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    switch (prop_id) {
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* main or coroutine context */
static void spice_display_channel_reset(SpiceChannel *channel, gboolean migrating)
{
    /* palettes, images, and glz_window are cleared in the session */
    clear_streams(channel);
    clear_surfaces(channel, TRUE);

    SPICE_CHANNEL_CLASS(spice_display_channel_parent_class)->channel_reset(channel, migrating);
}

static void spice_display_channel_class_init(SpiceDisplayChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->finalize     = spice_display_channel_finalize;
    gobject_class->dispose      = spice_display_channel_dispose;
    gobject_class->get_property = spice_display_get_property;
    gobject_class->set_property = spice_display_set_property;
    gobject_class->constructed = spice_display_channel_constructed;

    channel_class->channel_up   = spice_display_channel_up;
    channel_class->channel_reset = spice_display_channel_reset;
    channel_class->channel_reset_capabilities = spice_display_channel_reset_capabilities;

    g_object_class_install_property
        (gobject_class, PROP_HEIGHT,
         g_param_spec_uint("height",
                           "Display height",
                           "The primary surface height",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_WIDTH,
         g_param_spec_uint("width",
                           "Display width",
                           "The primary surface width",
                           0, G_MAXUINT, 0,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplayChannel:monitors:
     *
     * Current monitors configuration.
     *
     * Since: 0.13
     */
    g_object_class_install_property
        (gobject_class, PROP_MONITORS,
         g_param_spec_boxed("monitors",
                            "Display monitors",
                            "The monitors configuration",
                            G_TYPE_ARRAY,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplayChannel:monitors-max:
     *
     * The maximum number of monitors the server or guest supports.
     * May change during client lifetime, for instance guest may
     * reboot or dynamically adjust this.
     *
     * Since: 0.13
     */
    g_object_class_install_property
        (gobject_class, PROP_MONITORS_MAX,
         g_param_spec_uint("monitors-max",
                           "Max display monitors",
                           "The current maximum number of monitors",
                           1, MONITORS_MAX, 1,
                           G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplayChannel:gl-scanout:
     * @display: the #SpiceDisplayChannel that emitted the signal
     *
     * The last #SpiceGlScanout received.
     *
     * Since: 0.31
     */
    g_object_class_install_property
        (gobject_class, PROP_GL_SCANOUT,
         g_param_spec_boxed("gl-scanout",
                            "GL scanout",
                            "GL scanout",
                            SPICE_TYPE_GL_SCANOUT,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplayChannel::display-primary-create:
     * @display: the #SpiceDisplayChannel that emitted the signal
     * @format: %SPICE_SURFACE_FMT_32_xRGB or %SPICE_SURFACE_FMT_16_555;
     * @width: width resolution
     * @height: height resolution
     * @stride: the buffer stride ("width" padding)
     * @shmid: identifier of the shared memory segment associated with
     * the @imgdata, or -1 if not shm
     * @imgdata: pointer to surface buffer
     *
     * The #SpiceDisplayChannel::display-primary-create signal
     * provides main display buffer data.
     **/
    signals[SPICE_DISPLAY_PRIMARY_CREATE] =
        g_signal_new("display-primary-create",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     display_primary_create),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__INT_INT_INT_INT_INT_POINTER,
                     G_TYPE_NONE,
                     6,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_POINTER);

    /**
     * SpiceDisplayChannel::display-primary-destroy:
     * @display: the #SpiceDisplayChannel that emitted the signal
     *
     * The #SpiceDisplayChannel::display-primary-destroy signal is
     * emitted when the primary surface is freed and should not be
     * accessed anymore.
     **/
    signals[SPICE_DISPLAY_PRIMARY_DESTROY] =
        g_signal_new("display-primary-destroy",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     display_primary_destroy),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    /**
     * SpiceDisplayChannel::display-invalidate:
     * @display: the #SpiceDisplayChannel that emitted the signal
     * @x: x position
     * @y: y position
     * @width: width
     * @height: height
     *
     * The #SpiceDisplayChannel::display-invalidate signal is emitted
     * when the rectangular region x/y/w/h of the primary buffer is
     * updated.
     **/
    signals[SPICE_DISPLAY_INVALIDATE] =
        g_signal_new("display-invalidate",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     display_invalidate),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__INT_INT_INT_INT,
                     G_TYPE_NONE,
                     4,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

    /**
     * SpiceDisplayChannel::display-mark:
     * @display: the #SpiceDisplayChannel that emitted the signal
     * @mark: %TRUE when the display mark has been received
     *
     * The #SpiceDisplayChannel::display-mark signal is emitted when
     * the %RED_DISPLAY_MARK command is received, and the display
     * should be exposed.
     **/
    signals[SPICE_DISPLAY_MARK] =
        g_signal_new("display-mark",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayChannelClass,
                                     display_mark),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    /**
     * SpiceDisplayChannel::gl-draw:
     * @display: the #SpiceDisplayChannel that emitted the signal
     * @x: x position
     * @y: y position
     * @width: width
     * @height: height
     *
     * The #SpiceDisplayChannel::draw signal is emitted when the
     * rectangular region x/y/w/h of the GL scanout is updated and
     * must be drawn. When the draw is finished, you must call
     * spice_display_gl_draw_done() in order to release the GL
     * resources.
     *
     * Since: 0.31
     **/
    signals[SPICE_DISPLAY_GL_DRAW] =
        g_signal_new("gl-draw",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     0, 0, NULL, NULL,
                     g_cclosure_user_marshal_VOID__UINT_UINT_UINT_UINT,
                     G_TYPE_NONE,
                     4,
                     G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

    g_type_class_add_private(klass, sizeof(SpiceDisplayChannelPrivate));

    channel_set_handlers(SPICE_CHANNEL_CLASS(klass));
}

/**
 * spice_display_get_primary:
 * @channel: a #SpiceDisplayChannel
 * @surface_id: a surface id
 * @primary: a #SpiceDisplayPrimary
 *
 * Retrieve primary display surface @surface_id.
 *
 * Returns: %TRUE if the primary surface was found and its details
 * collected in @primary.
 */
gboolean spice_display_get_primary(SpiceChannel *channel, guint32 surface_id,
                                   SpiceDisplayPrimary *primary)
{
    g_return_val_if_fail(SPICE_IS_DISPLAY_CHANNEL(channel), FALSE);
    g_return_val_if_fail(primary != NULL, FALSE);

    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_surface *surface = find_surface(c, surface_id);

    if (surface == NULL)
        return FALSE;

    g_return_val_if_fail(surface->primary, FALSE);

    primary->format = surface->format;
    primary->width = surface->width;
    primary->height = surface->height;
    primary->stride = surface->stride;
    primary->shmid = -1;
    primary->data = surface->data;
    primary->marked = c->mark;
    CHANNEL_DEBUG(channel, "get primary %p", primary->data);

    return TRUE;
}

/**
 * spice_display_change_preferred_compression:
 * @channel: a #SpiceDisplayChannel
 * @compression: a #SpiceImageCompression
 *
 * Tells the spice server to change the preferred image compression
 * for the @channel.
 *
 * Since: 0.31
 */
void spice_display_change_preferred_compression(SpiceChannel *channel, gint compression)
{
    SpiceMsgOut *out;
    SpiceMsgcDisplayPreferredCompression pref_comp_msg;

    g_return_if_fail(SPICE_IS_DISPLAY_CHANNEL(channel));
    g_return_if_fail(compression > SPICE_IMAGE_COMPRESSION_INVALID &&
                     compression < SPICE_IMAGE_COMPRESSION_ENUM_END);

    if (!spice_channel_test_capability(channel, SPICE_DISPLAY_CAP_PREF_COMPRESSION)) {
        CHANNEL_DEBUG(channel, "does not have capability to change the preferred compression");
        return;
    }

    CHANNEL_DEBUG(channel, "changing preferred compression to %d", compression);

    pref_comp_msg.image_compression = compression;
    out = spice_msg_out_new(channel, SPICE_MSGC_DISPLAY_PREFERRED_COMPRESSION);
    out->marshallers->msgc_display_preferred_compression(out->marshaller, &pref_comp_msg);
    spice_msg_out_send_internal(out);
}

static void spice_display_send_client_preferred_video_codecs(SpiceChannel *channel,
                                                             const GArray *codecs)
{
    guint i;
    SpiceMsgOut *out;
    SpiceMsgcDisplayPreferredVideoCodecType *msg;

    msg = g_malloc0(sizeof(SpiceMsgcDisplayPreferredVideoCodecType) +
                    (sizeof(SpiceVideoCodecType) * codecs->len));
    msg->num_of_codecs = codecs->len;
    for (i = 0; i < codecs->len; i++) {
        msg->codecs[i] = g_array_index(codecs, gint, i);
    }

    out = spice_msg_out_new(channel, SPICE_MSGC_DISPLAY_PREFERRED_VIDEO_CODEC_TYPE);
    out->marshallers->msgc_display_preferred_video_codec_type(out->marshaller, msg);
    spice_msg_out_send_internal(out);
    g_free(msg);
}

/**
 * spice_display_change_preferred_video_codec:
 * @channel: a #SpiceDisplayChannel
 * @codec_type: a #SpiceVideoCodecType
 *
 * Tells the spice server to change the preferred video codec type for
 * streaming in @channel. Application can set only one preferred video codec per
 * display channel.
 *
 * Since: 0.34
 */
void spice_display_change_preferred_video_codec_type(SpiceChannel *channel, gint codec_type)
{
    GArray *codecs;

    g_return_if_fail(SPICE_IS_DISPLAY_CHANNEL(channel));
    g_return_if_fail(codec_type >= SPICE_VIDEO_CODEC_TYPE_MJPEG &&
                     codec_type < SPICE_VIDEO_CODEC_TYPE_ENUM_END);

    if (!spice_channel_test_capability(channel, SPICE_DISPLAY_CAP_PREF_VIDEO_CODEC_TYPE)) {
        CHANNEL_DEBUG(channel, "does not have capability to change the preferred video codec type");
        return;
    }

    /* FIXME: We should detect video codecs that client machine can do hw
     * decoding, store this information (as GArray) and send it to the server.
     * This array can be rearranged to have @codec_type in the front (which is
     * the preferred for the client side) */
    CHANNEL_DEBUG(channel, "changing preferred video codec type to %d", codec_type);
    codecs = g_array_new(FALSE, FALSE, sizeof(gint));
    g_array_append_val(codecs, codec_type);
    spice_display_send_client_preferred_video_codecs(channel, codecs);
    g_array_unref(codecs);
}

/**
 * spice_display_get_gl_scanout:
 * @channel: a #SpiceDisplayChannel
 *
 * Retrieves the GL scanout if available
 *
 * Returns: the current GL scanout, or %NULL if none or not valid
 *
 * Since: 0.31
 **/
const SpiceGlScanout *
spice_display_get_gl_scanout(SpiceDisplayChannel *channel)
{
    g_return_val_if_fail(SPICE_IS_DISPLAY_CHANNEL(channel), NULL);

    return channel->priv->scanout.fd != -1 ? &channel->priv->scanout : NULL;
}

/* ------------------------------------------------------------------ */

static void image_put(SpiceImageCache *cache, uint64_t id, pixman_image_t *image)
{
    SpiceDisplayChannelPrivate *c =
        SPICE_CONTAINEROF(cache, SpiceDisplayChannelPrivate, image_cache);

    cache_add(c->images, id, pixman_image_ref(image));
}

typedef struct _WaitImageData
{
    gboolean lossy;
    SpiceImageCache *cache;
    uint64_t id;
    pixman_image_t *image;
} WaitImageData;

static gboolean wait_image(gpointer data)
{
    gboolean lossy;
    WaitImageData *wait = data;
    SpiceDisplayChannelPrivate *c =
        SPICE_CONTAINEROF(wait->cache, SpiceDisplayChannelPrivate, image_cache);
    pixman_image_t *image = cache_find_lossy(c->images, wait->id, &lossy);

    if (!image || (lossy && !wait->lossy))
        return FALSE;

    wait->image = pixman_image_ref(image);

    return TRUE;
}

static pixman_image_t *image_get(SpiceImageCache *cache, uint64_t id)
{
    WaitImageData wait = {
        .lossy = TRUE,
        .cache = cache,
        .id = id,
        .image = NULL
    };
    if (!g_coroutine_condition_wait(g_coroutine_self(), wait_image, &wait))
        SPICE_DEBUG("wait image got cancelled");

    return wait.image;
}

static void palette_put(SpicePaletteCache *cache, SpicePalette *palette)
{
    SpiceDisplayChannelPrivate *c =
        SPICE_CONTAINEROF(cache, SpiceDisplayChannelPrivate, palette_cache);

    cache_add(c->palettes, palette->unique,
              g_memdup(palette, sizeof(SpicePalette) +
                       palette->num_ents * sizeof(palette->ents[0])));
}

static SpicePalette *palette_get(SpicePaletteCache *cache, uint64_t id)
{
    SpiceDisplayChannelPrivate *c =
        SPICE_CONTAINEROF(cache, SpiceDisplayChannelPrivate, palette_cache);

    /* here the returned pointer is weak, no ref given to caller.  it
     * seems spice canvas usage is exclusively temporary, so it's ok.
     * palette_release is a noop. */
    return cache_find(c->palettes, id);
}

static void palette_remove(SpicePaletteCache *cache, uint64_t id)
{
    SpiceDisplayChannelPrivate *c =
        SPICE_CONTAINEROF(cache, SpiceDisplayChannelPrivate, palette_cache);

    cache_remove(c->palettes, id);
}

static void palette_release(SpicePaletteCache *cache, SpicePalette *palette)
{
    /* there is no refcount of palette, see palette_get() */
}

static void image_put_lossy(SpiceImageCache *cache, uint64_t id,
                            pixman_image_t *surface)
{
    SpiceDisplayChannelPrivate *c =
        SPICE_CONTAINEROF(cache, SpiceDisplayChannelPrivate, image_cache);

#ifndef NDEBUG
    g_warn_if_fail(cache_find(c->images, id) == NULL);
#endif

    cache_add_lossy(c->images, id, pixman_image_ref(surface), TRUE);
}

static void image_replace_lossy(SpiceImageCache *cache, uint64_t id,
                                pixman_image_t *surface)
{
    image_put(cache, id, surface);
}

static pixman_image_t* image_get_lossless(SpiceImageCache *cache, uint64_t id)
{
    WaitImageData wait = {
        .lossy = FALSE,
        .cache = cache,
        .id = id,
        .image = NULL
    };
    if (!g_coroutine_condition_wait(g_coroutine_self(), wait_image, &wait))
        SPICE_DEBUG("wait lossless got cancelled");

    return wait.image;
}

static SpiceCanvas *surfaces_get(SpiceImageSurfaces *surfaces,
                                 uint32_t surface_id)
{
    SpiceDisplayChannelPrivate *c =
        SPICE_CONTAINEROF(surfaces, SpiceDisplayChannelPrivate, image_surfaces);

    display_surface *s =
        find_surface(c, surface_id);

    return s ? s->canvas : NULL;
}

static SpiceImageCacheOps image_cache_ops = {
    .put = image_put,
    .get = image_get,

    .put_lossy = image_put_lossy,
    .replace_lossy = image_replace_lossy,
    .get_lossless = image_get_lossless,
};

static SpicePaletteCacheOps palette_cache_ops = {
    .put     = palette_put,
    .get     = palette_get,
    .release = palette_release,
};

static SpiceImageSurfacesOps image_surfaces_ops = {
    .get = surfaces_get
};

#if defined(G_OS_WIN32)
static HDC create_compatible_dc(void)
{
    HDC dc = CreateCompatibleDC(NULL);
    if (!dc) {
        g_warning("create compatible DC failed");
    }
    return dc;
}
#endif

static void spice_display_channel_reset_capabilities(SpiceChannel *channel)
{
    guint i;
    static const struct {
        SpiceVideoCodecType type;
        int cap;
        const gchar name[8];
    } gst_codecs[] = {
        {SPICE_VIDEO_CODEC_TYPE_MJPEG, SPICE_DISPLAY_CAP_CODEC_MJPEG, "mjpeg"},
        {SPICE_VIDEO_CODEC_TYPE_VP8, SPICE_DISPLAY_CAP_CODEC_VP8, "vp8"},
        {SPICE_VIDEO_CODEC_TYPE_H264, SPICE_DISPLAY_CAP_CODEC_H264, "h264"},
        {SPICE_VIDEO_CODEC_TYPE_VP9, SPICE_DISPLAY_CAP_CODEC_VP9, "vp9"},
    };

    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_SIZED_STREAM);
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_MONITORS_CONFIG);
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_COMPOSITE);
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_A8_SURFACE);
#ifdef USE_LZ4
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_LZ4_COMPRESSION);
#endif
    if (SPICE_DISPLAY_CHANNEL(channel)->priv->enable_adaptive_streaming) {
        spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_STREAM_REPORT);
    }
#ifdef G_OS_UNIX
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_GL_SCANOUT);
#endif
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_MULTI_CODEC);
#ifdef HAVE_BUILTIN_MJPEG
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_DISPLAY_CAP_CODEC_MJPEG);
#endif
    for (i = 0; i < G_N_ELEMENTS(gst_codecs); i++) {
        if (gstvideo_has_codec(gst_codecs[i].type)) {
            spice_channel_set_capability(SPICE_CHANNEL(channel), gst_codecs[i].cap);
        } else {
            SPICE_DEBUG("GStreamer does not support the %s codec", gst_codecs[i].name);
        }
    }
}

static void destroy_surface(gpointer data)
{
    display_surface *surface = data;

    destroy_canvas(surface);
    g_free(surface);
}

static void spice_display_channel_init(SpiceDisplayChannel *channel)
{
    SpiceDisplayChannelPrivate *c;

    c = channel->priv = SPICE_DISPLAY_CHANNEL_GET_PRIVATE(channel);

    c->surfaces = g_hash_table_new_full(NULL, NULL, NULL, destroy_surface);
    c->image_cache.ops = &image_cache_ops;
    c->palette_cache.ops = &palette_cache_ops;
    c->image_surfaces.ops = &image_surfaces_ops;
#if defined(G_OS_WIN32)
    c->dc = create_compatible_dc();
#endif
    c->monitors_max = 1;
    c->scanout.fd = -1;

    if (g_getenv("SPICE_DISABLE_ADAPTIVE_STREAMING")) {
        SPICE_DEBUG("adaptive video disabled");
        c->enable_adaptive_streaming = FALSE;
    } else {
        c->enable_adaptive_streaming = TRUE;
    }
    spice_display_channel_reset_capabilities(SPICE_CHANNEL(channel));
}

/* ------------------------------------------------------------------ */

static int create_canvas(SpiceChannel *channel, display_surface *surface)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    if (surface->primary) {
        if (c->primary) {
            if (c->primary->width == surface->width &&
                c->primary->height == surface->height) {
                CHANNEL_DEBUG(channel, "Reusing existing primary surface");
                return 0;
            }

            g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_DESTROY], 0);

            g_hash_table_remove(c->surfaces, GINT_TO_POINTER(c->primary->surface_id));
        }

        CHANNEL_DEBUG(channel, "Create primary canvas");
    }

    surface->data = g_malloc0(surface->size);

    g_return_val_if_fail(c->glz_window, 0);
    g_warn_if_fail(surface->canvas == NULL);
    g_warn_if_fail(surface->glz_decoder == NULL);
    g_warn_if_fail(surface->zlib_decoder == NULL);
    g_warn_if_fail(surface->jpeg_decoder == NULL);

    surface->glz_decoder = glz_decoder_new(c->glz_window);
    surface->zlib_decoder = zlib_decoder_new();
    surface->jpeg_decoder = jpeg_decoder_new();

    surface->canvas = canvas_create_for_data(surface->width,
                                             surface->height,
                                             surface->format,
                                             surface->data,
                                             surface->stride,
                                             &c->image_cache,
                                             &c->palette_cache,
                                             &c->image_surfaces,
                                             surface->glz_decoder,
                                             surface->jpeg_decoder,
                                             surface->zlib_decoder);

    g_return_val_if_fail(surface->canvas != NULL, 0);
    g_hash_table_insert(c->surfaces, GINT_TO_POINTER(surface->surface_id), surface);

    if (surface->primary) {
        g_warn_if_fail(c->primary == NULL);
        c->primary = surface;
        g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_CREATE], 0,
                                surface->format, surface->width, surface->height,
                                surface->stride, -1, surface->data);

        if (!spice_channel_test_capability(channel, SPICE_DISPLAY_CAP_MONITORS_CONFIG)) {
            g_array_set_size(c->monitors, 1);
            SpiceDisplayMonitorConfig *config = &g_array_index(c->monitors, SpiceDisplayMonitorConfig, 0);
            config->x = config->y = 0;
            config->width = surface->width;
            config->height = surface->height;
            g_coroutine_object_notify(G_OBJECT(channel), "monitors");
        }
    }

    return 0;
}

static void destroy_canvas(display_surface *surface)
{
    if (surface == NULL)
        return;

    glz_decoder_destroy(surface->glz_decoder);
    zlib_decoder_destroy(surface->zlib_decoder);
    jpeg_decoder_destroy(surface->jpeg_decoder);

    g_clear_pointer(&surface->data, g_free);
    g_clear_pointer(&surface->canvas, surface->canvas->ops->destroy);
}

static display_surface *find_surface(SpiceDisplayChannelPrivate *c, guint32 surface_id)
{
    if (c->primary && c->primary->surface_id == surface_id)
        return c->primary;

    return g_hash_table_lookup(c->surfaces, GINT_TO_POINTER(surface_id));
}

/* main or coroutine context */
static void clear_surfaces(SpiceChannel *channel, gboolean keep_primary)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    GHashTableIter iter;
    display_surface *surface;

    if (!keep_primary) {
        c->primary = NULL;
        g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_DESTROY], 0);
    }

    g_hash_table_iter_init(&iter, c->surfaces);
    while (g_hash_table_iter_next(&iter, NULL, (gpointer*)&surface)) {

        if (keep_primary && surface->primary) {
            CHANNEL_DEBUG(channel, "keeping existing primary surface, migration or reset");
            continue;
        }

        g_hash_table_iter_remove(&iter);
    }
}

/* coroutine context */
static void emit_invalidate(SpiceChannel *channel, SpiceRect *bbox)
{
    g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_INVALIDATE], 0,
                            bbox->left, bbox->top,
                            bbox->right - bbox->left,
                            bbox->bottom - bbox->top);
}

/* ------------------------------------------------------------------ */

/* coroutine context */
static void spice_display_channel_up(SpiceChannel *channel)
{
    SpiceMsgOut *out;
    SpiceSession *s = spice_channel_get_session(channel);
    SpiceMsgcDisplayInit init;
    int cache_size;
    int glz_window_size;
    SpiceImageCompression preferred_compression = SPICE_IMAGE_COMPRESSION_INVALID;

    g_object_get(s,
                 "cache-size", &cache_size,
                 "glz-window-size", &glz_window_size,
                 "preferred-compression", &preferred_compression,
                 NULL);
    CHANNEL_DEBUG(channel, "%s: cache_size %d, glz_window_size %d (bytes)", __FUNCTION__,
                  cache_size, glz_window_size);
    init.pixmap_cache_id = 1;
    init.glz_dictionary_id = 1;
    init.pixmap_cache_size = cache_size / 4; /* pixels */
    init.glz_dictionary_window_size = glz_window_size / 4; /* pixels */
    out = spice_msg_out_new(channel, SPICE_MSGC_DISPLAY_INIT);
    out->marshallers->msgc_display_init(out->marshaller, &init);
    spice_msg_out_send_internal(out);

    /* notify of existence of this monitor */
    g_coroutine_object_notify(G_OBJECT(channel), "monitors");

    if (preferred_compression != SPICE_IMAGE_COMPRESSION_INVALID) {
        spice_display_change_preferred_compression(channel, preferred_compression);
    }
}

#define DRAW(type) {                                                    \
        display_surface *surface =                                      \
            find_surface(SPICE_DISPLAY_CHANNEL(channel)->priv,          \
                op->base.surface_id);                                   \
        g_return_if_fail(surface != NULL);                              \
        surface->canvas->ops->draw_##type(surface->canvas, &op->base.box, \
                                          &op->base.clip, &op->data);   \
        if (surface->primary) {                                         \
            emit_invalidate(channel, &op->base.box);                    \
        }                                                               \
}

/* coroutine context */
static void display_handle_mode(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayMode *mode = spice_msg_in_parsed(in);
    display_surface *surface;

    g_warn_if_fail(c->mark == FALSE);

    surface = g_new0(display_surface, 1);
    surface->format  = mode->bits == 32 ?
        SPICE_SURFACE_FMT_32_xRGB : SPICE_SURFACE_FMT_16_555;
    surface->width   = mode->x_res;
    surface->height  = mode->y_res;
    surface->stride  = surface->width * 4;
    surface->size    = surface->height * surface->stride;
    surface->primary = true;
    create_canvas(channel, surface);
}

/* coroutine context */
static void display_handle_mark(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    CHANNEL_DEBUG(channel, "%s", __FUNCTION__);
    g_return_if_fail(c->primary != NULL);
#ifdef EXTRA_CHECKS
    g_warn_if_fail(c->mark == FALSE);
#endif

    c->mark = TRUE;
    g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_MARK], 0, TRUE);
}

/* coroutine context */
static void display_handle_reset(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_surface *surface = c->primary;

    CHANNEL_DEBUG(channel, "%s: TODO detach_from_screen", __FUNCTION__);

    if (surface != NULL)
        surface->canvas->ops->clear(surface->canvas);

    cache_clear(c->palettes);

    c->mark = FALSE;
    g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_MARK], 0, FALSE);
}

/* coroutine context */
static void display_handle_copy_bits(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayCopyBits *op = spice_msg_in_parsed(in);
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_surface *surface = find_surface(c, op->base.surface_id);

    g_return_if_fail(surface != NULL);
    surface->canvas->ops->copy_bits(surface->canvas, &op->base.box,
                                    &op->base.clip, &op->src_pos);
    if (surface->primary) {
        emit_invalidate(channel, &op->base.box);
    }
}

/* coroutine context */
static void display_handle_inv_list(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceResourceList *list = spice_msg_in_parsed(in);
    int i;

    for (i = 0; i < list->count; i++) {
        guint64 id = list->resources[i].id;

        switch (list->resources[i].type) {
        case SPICE_RES_TYPE_PIXMAP:
            if (!cache_remove(c->images, id))
                SPICE_DEBUG("fail to remove image %" G_GUINT64_FORMAT, id);
            break;
        default:
            g_return_if_reached();
            break;
        }
    }
}

/* coroutine context */
static void display_handle_inv_pixmap_all(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    spice_channel_handle_wait_for_channels(channel, in);
    cache_clear(c->images);
}

/* coroutine context */
static void display_handle_inv_palette(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayInvalOne* op = spice_msg_in_parsed(in);

    palette_remove(&c->palette_cache, op->id);
}

/* coroutine context */
static void display_handle_inv_palette_all(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    cache_clear(c->palettes);
}

/* ------------------------------------------------------------------ */

static void display_update_stream_region(display_stream *st)
{
    int i;

    switch (st->clip.type) {
    case SPICE_CLIP_TYPE_RECTS:
        region_clear(&st->region);
        for (i = 0; i < st->clip.rects->num_rects; i++) {
            region_add(&st->region, &st->clip.rects->rects[i]);
        }
        st->have_region = true;
        break;
    case SPICE_CLIP_TYPE_NONE:
    default:
        st->have_region = false;
        break;
    }
}

static void report_invalid_stream(SpiceChannel *channel, uint32_t id)
{
    if (spice_channel_test_capability(channel, SPICE_DISPLAY_CAP_STREAM_REPORT)) {
        SpiceMsgcDisplayStreamReport report;
        SpiceMsgOut *msg;

        /* Send a special stream report (UINT_MAX dropped frames out of zero)
         * to indicate there is no such stream.
         */
        spice_printerr("notify the server that stream %u does not exist", id);
        memset(&report, 0, sizeof(report));
        report.stream_id = id;
        report.num_frames = 0;
        report.num_drops = UINT_MAX;

        msg = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_DISPLAY_STREAM_REPORT);
        msg->marshallers->msgc_display_stream_report(msg->marshaller, &report);
        spice_msg_out_send(msg);
    }
}

static display_stream *get_stream_by_id(SpiceChannel *channel, uint32_t id)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    if (c != NULL && c->streams != NULL && id < c->nstreams &&
        c->streams[id] != NULL) {
        return c->streams[id];
    }

    report_invalid_stream(channel, id);
    return NULL;
}

/* coroutine context */
static void display_handle_stream_create(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgDisplayStreamCreate *op = spice_msg_in_parsed(in);
    display_stream *st;

    CHANNEL_DEBUG(channel, "%s: id %u", __FUNCTION__, op->id);

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
    c->streams[op->id] = g_new0(display_stream, 1);
    st = c->streams[op->id];

    st->flags = op->flags;
    st->dest = op->dest;
    st->clip = op->clip;
    st->surface = find_surface(c, op->surface_id);
    st->channel = channel;
    st->drops_seqs_stats_arr = g_array_new(FALSE, FALSE, sizeof(drops_sequence_stats));

    region_init(&st->region);
    display_update_stream_region(st);

    switch (op->codec_type) {
#ifdef HAVE_BUILTIN_MJPEG
    case SPICE_VIDEO_CODEC_TYPE_MJPEG:
        st->video_decoder = create_mjpeg_decoder(op->codec_type, st);
        break;
#endif
    default:
#ifdef HAVE_GSTVIDEO
        st->video_decoder = create_gstreamer_decoder(op->codec_type, st);
#else
        st->video_decoder = NULL;
#endif
    }
    if (st->video_decoder == NULL) {
        spice_printerr("could not create a video decoder for codec %u", op->codec_type);
        destroy_stream(channel, op->id);
        report_invalid_stream(channel, op->id);
    }
}

static const SpiceRect *stream_get_dest(display_stream *st, SpiceMsgIn *frame_msg)
{
    if (frame_msg == NULL ||
        spice_msg_in_type(frame_msg) != SPICE_MSG_DISPLAY_STREAM_DATA_SIZED) {
        return &st->dest;
    } else {
        SpiceMsgDisplayStreamDataSized *op = spice_msg_in_parsed(frame_msg);

        return &op->dest;
   }

}

G_GNUC_INTERNAL
uint32_t spice_msg_in_frame_data(SpiceMsgIn *frame_msg, uint8_t **data)
{
    switch (spice_msg_in_type(frame_msg)) {
    case SPICE_MSG_DISPLAY_STREAM_DATA: {
        SpiceMsgDisplayStreamData *op = spice_msg_in_parsed(frame_msg);
        *data = op->data;
        return op->data_size;
    }
    case SPICE_MSG_DISPLAY_STREAM_DATA_SIZED: {
        SpiceMsgDisplayStreamDataSized *op = spice_msg_in_parsed(frame_msg);
        *data = op->data;
        return op->data_size;
    }
    default:
        *data = NULL;
        g_return_val_if_reached(0);
    }
}

G_GNUC_INTERNAL
guint32 stream_get_time(display_stream *st)
{
    SpiceSession *session = spice_channel_get_session(st->channel);
    return session ? spice_session_get_mm_time(session) : 0;
}

/* coroutine or main context */
G_GNUC_INTERNAL
void stream_dropped_frame_on_playback(display_stream *st)
{
    st->num_drops_on_playback++;
}

/* main context */
G_GNUC_INTERNAL
void stream_display_frame(display_stream *st, SpiceMsgIn *frame_msg,
                          uint32_t width, uint32_t height, uint8_t *data)
{
    const SpiceRect *dest;
    int stride;

    dest = stream_get_dest(st, frame_msg);

    stride = width * sizeof(uint32_t);
    if (!(st->flags & SPICE_STREAM_FLAGS_TOP_DOWN)) {
        data += stride * (height - 1);
        stride = -stride;
    }

    st->surface->canvas->ops->put_image(st->surface->canvas,
#ifdef G_OS_WIN32
                                        SPICE_DISPLAY_CHANNEL(st->channel)->priv->dc,
#endif
                                        dest, data,
                                        width, height, stride,
                                        st->have_region ? &st->region : NULL);

    if (st->surface->primary)
        g_signal_emit(st->channel, signals[SPICE_DISPLAY_INVALIDATE], 0,
                      dest->left, dest->top,
                      dest->right - dest->left,
                      dest->bottom - dest->top);
}

/* after a sequence of 3 drops, push a report to the server, even
 * if the report window is bigger */
#define STREAM_REPORT_DROP_SEQ_LEN_LIMIT 3

static void display_update_stream_report(SpiceDisplayChannel *channel, uint32_t stream_id,
                                         uint32_t frame_time, int32_t latency)
{
    display_stream *st = get_stream_by_id(SPICE_CHANNEL(channel), stream_id);
    guint64 now;

    g_return_if_fail(st != NULL);
    if (!st->report_is_active) {
        return;
    }
    now = g_get_monotonic_time();

    if (st->report_num_frames == 0) {
        st->report_start_frame_time = frame_time;
        st->report_start_time = now;
    }
    st->report_num_frames++;

    if (latency < 0) { // drop
        st->report_num_drops++;
        st->report_drops_seq_len++;
    } else {
        st->report_drops_seq_len = 0;
    }

    if (st->report_num_frames >= st->report_max_window ||
        now - st->report_start_time >= st->report_timeout ||
        st->report_drops_seq_len >= STREAM_REPORT_DROP_SEQ_LEN_LIMIT) {
        SpiceMsgcDisplayStreamReport report;
        SpiceSession *session = spice_channel_get_session(SPICE_CHANNEL(channel));
        SpiceMsgOut *msg;

        report.stream_id = stream_id;
        report.unique_id = st->report_id;
        report.start_frame_mm_time = st->report_start_frame_time;
        report.end_frame_mm_time = frame_time;
        report.num_frames = st->report_num_frames;
        report.num_drops = st-> report_num_drops;
        report.last_frame_delay = latency;
        if (spice_session_is_playback_active(session)) {
            report.audio_delay = spice_session_get_playback_latency(session);
        } else {
            report.audio_delay = UINT_MAX;
        }

        msg = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_DISPLAY_STREAM_REPORT);
        msg->marshallers->msgc_display_stream_report(msg->marshaller, &report);
        spice_msg_out_send(msg);

        st->report_start_time = 0;
        st->report_start_frame_time = 0;
        st->report_num_frames = 0;
        st->report_num_drops = 0;
        st->report_drops_seq_len = 0;
    }
}

/*
 * Migration can occur between 2 spice-servers with different mm-times.
 * Then, the following cases can happen after migration completes:
 * (We refer to src/dst-time as the mm-times on the src/dst servers):
 *
 * (case 1) Frames with time ~= dst-time arrive to the client before the
 *          playback-channel updates the session's mm-time (i.e., the mm_time
 *          of the session is still based on the src-time).
 *     (a) If src-time < dst-time:
 *         display_stream_schedule schedules the next rendering to
 *         ~(dst-time - src-time) milliseconds from now.
 *         Since we assume monotonic mm_time, display_stream_schedule,
 *         returns immediately when a rendering timeout
 *         has already been set, and doesn't update the timeout,
 *         even after the mm_time is updated.
 *         When src-time << dst-time, a significant video frames loss will occur.
 *     (b) If src-time > dst-time
 *         Frames will be dropped till the mm-time will be updated.
 * (case 2) mm-time is synced with dst-time, but frames that were in the command
 *         ring during migration still arrive (such frames hold src-time).
 *    (a) If src-time < dst-time
 *        The frames that hold src-time will be dropped, since their
 *        mm_time < session-mm_time. But all the new frames that are generated in
 *        the driver after migration, will be rendered appropriately.
 *    (b) If src-time > dst-time
 *        Similar consequences as in 1 (a)
 * case 2 is less likely, since at takes at least 20 frames till the dst-server re-identifies
 * the video stream and starts sending stream data
 *
 * display_session_mm_time_reset_cb handles case 1.a by notifying the
 * video decoders through their reschedule() method, and case 2.b is handled
 * directly by the video decoders in their queue_frame() method
 */

/* main context */
static void display_session_mm_time_reset_cb(SpiceSession *session, gpointer data)
{
    SpiceChannel *channel = data;
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    guint i;

    CHANNEL_DEBUG(channel, "%s", __FUNCTION__);

    for (i = 0; i < c->nstreams; i++) {
        display_stream *st;

        if (c->streams[i] == NULL) {
            continue;
        }
        SPICE_DEBUG("%s: stream-id %u", __FUNCTION__, i);
        st = c->streams[i];
        st->video_decoder->reschedule(st->video_decoder);
    }
}

#define STREAM_PLAYBACK_SYNC_DROP_SEQ_LEN_LIMIT 5

/* coroutine context */
static void display_handle_stream_data(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceStreamDataHeader *op = spice_msg_in_parsed(in);
    display_stream *st = get_stream_by_id(channel, op->id);
    guint32 mmtime;
    int32_t latency;

    g_return_if_fail(st != NULL);
    mmtime = stream_get_time(st);

    if (spice_msg_in_type(in) == SPICE_MSG_DISPLAY_STREAM_DATA_SIZED) {
        CHANNEL_DEBUG(channel, "stream %u contains sized data", op->id);
    }

    if (op->multi_media_time == 0) {
        g_critical("Received frame with invalid 0 timestamp! perhaps wrong graphic driver?");
        op->multi_media_time = mmtime + 100; /* workaround... */
    }

    if (!st->num_input_frames) {
        st->first_frame_mm_time = op->multi_media_time;
    }
    st->num_input_frames++;

    latency = op->multi_media_time - mmtime;
    if (latency < 0) {
        CHANNEL_DEBUG(channel, "stream data too late by %u ms (ts: %u, mmtime: %u), dropping",
                      mmtime - op->multi_media_time, op->multi_media_time, mmtime);
        st->arrive_late_time += mmtime - op->multi_media_time;
        st->arrive_late_count++;

        if (!st->cur_drops_seq_stats.len) {
            st->cur_drops_seq_stats.start_mm_time = op->multi_media_time;
        }
        st->cur_drops_seq_stats.len++;
        st->playback_sync_drops_seq_len++;
    } else {
        CHANNEL_DEBUG(channel, "video latency: %d", latency);
        if (st->cur_drops_seq_stats.len) {
            st->cur_drops_seq_stats.duration = op->multi_media_time -
                                               st->cur_drops_seq_stats.start_mm_time;
            g_array_append_val(st->drops_seqs_stats_arr, st->cur_drops_seq_stats);
            memset(&st->cur_drops_seq_stats, 0, sizeof(st->cur_drops_seq_stats));
            st->num_drops_seqs++;
        }
        st->playback_sync_drops_seq_len = 0;
    }

    /* Let the video decoder queue the frames so it can optimize their
     * decoding and best decide if/when to drop them when they are late,
     * taking into account the impact on later frames.
     */
    if (!st->video_decoder->queue_frame(st->video_decoder, in, latency)) {
        destroy_stream(channel, op->id);
        report_invalid_stream(channel, op->id);
        return;
    }
    if (c->enable_adaptive_streaming) {
        display_update_stream_report(SPICE_DISPLAY_CHANNEL(channel), op->id,
                                     op->multi_media_time, latency);
        if (st->playback_sync_drops_seq_len >= STREAM_PLAYBACK_SYNC_DROP_SEQ_LEN_LIMIT) {
            spice_session_sync_playback_latency(spice_channel_get_session(channel));
            st->playback_sync_drops_seq_len = 0;
        }
    }
}

/* coroutine context */
static void display_handle_stream_clip(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayStreamClip *op = spice_msg_in_parsed(in);
    display_stream *st = get_stream_by_id(channel, op->id);

    g_return_if_fail(st != NULL);

    st->clip = op->clip;
    display_update_stream_region(st);
}

static void destroy_stream(SpiceChannel *channel, int id)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_stream *st;
    int i;

    g_return_if_fail(c != NULL);
    g_return_if_fail(c->streams != NULL);
    g_return_if_fail(c->nstreams > id);

    st = c->streams[id];
    if (!st)
        return;

    if (st->num_input_frames > 0) {
        guint64 drops_duration_total = 0;
        guint32 num_out_frames = st->num_input_frames - st->arrive_late_count - st->num_drops_on_playback;
        CHANNEL_DEBUG(channel, "%s: id=%d #in-frames=%u out/in=%.2f "
            "#drops-on-receive=%u avg-late-time(ms)=%.2f "
            "#drops-on-playback=%u", __FUNCTION__,
            id,
            st->num_input_frames,
            num_out_frames / (double)st->num_input_frames,
            st->arrive_late_count,
            st->arrive_late_count ? st->arrive_late_time / ((double)st->arrive_late_count): 0,
            st->num_drops_on_playback);
        if (st->num_drops_seqs) {
            CHANNEL_DEBUG(channel, "%s: #drops-sequences=%u ==>", __FUNCTION__, st->num_drops_seqs);
        }
        for (i = 0; i < st->num_drops_seqs; i++) {
            drops_sequence_stats *stats = &g_array_index(st->drops_seqs_stats_arr,
                                                         drops_sequence_stats,
                                                         i);
            drops_duration_total += stats->duration;
            CHANNEL_DEBUG(channel, "%s: \t len=%u start-ms=%u duration-ms=%u", __FUNCTION__,
                                   stats->len,
                                   stats->start_mm_time - st->first_frame_mm_time,
                                   stats->duration);
        }
        if (st->num_drops_seqs) {
            CHANNEL_DEBUG(channel, "%s: drops-total-duration=%"G_GUINT64_FORMAT" ==>", __FUNCTION__, drops_duration_total);
        }
    }

    g_array_free(st->drops_seqs_stats_arr, TRUE);

    if (st->video_decoder) {
        st->video_decoder->destroy(st->video_decoder);
    }

    g_free(st);
    c->streams[id] = NULL;
}

static void clear_streams(SpiceChannel *channel)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    int i;

    for (i = 0; i < c->nstreams; i++) {
        destroy_stream(channel, i);
    }
    g_clear_pointer(&c->streams, g_free);
    c->nstreams = 0;
}

/* coroutine context */
static void display_handle_stream_destroy(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayStreamDestroy *op = spice_msg_in_parsed(in);

    g_return_if_fail(op != NULL);
    CHANNEL_DEBUG(channel, "%s: id %u", __FUNCTION__, op->id);
    destroy_stream(channel, op->id);
}

/* coroutine context */
static void display_handle_stream_destroy_all(SpiceChannel *channel, SpiceMsgIn *in)
{
    clear_streams(channel);
}

/* coroutine context */
static void display_handle_stream_activate_report(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayStreamActivateReport *op = spice_msg_in_parsed(in);
    display_stream *st = get_stream_by_id(channel, op->stream_id);

    g_return_if_fail(st != NULL);
    st->report_is_active = TRUE;
    st->report_id = op->unique_id;
    st->report_max_window = op->max_window_size;
    st->report_timeout = op->timeout_ms * 1000;
    st->report_start_time = 0;
    st->report_start_frame_time = 0;
    st->report_num_frames = 0;
    st->report_num_drops = 0;
    st->report_drops_seq_len = 0;
}

/* ------------------------------------------------------------------ */

/* coroutine context */
static void display_handle_draw_fill(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawFill *op = spice_msg_in_parsed(in);
    DRAW(fill);
}

/* coroutine context */
static void display_handle_draw_opaque(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawOpaque *op = spice_msg_in_parsed(in);
    DRAW(opaque);
}

/* coroutine context */
static void display_handle_draw_copy(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawCopy *op = spice_msg_in_parsed(in);
    DRAW(copy);
}

/* coroutine context */
static void display_handle_draw_blend(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawBlend *op = spice_msg_in_parsed(in);
    DRAW(blend);
}

/* coroutine context */
static void display_handle_draw_blackness(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawBlackness *op = spice_msg_in_parsed(in);
    DRAW(blackness);
}

static void display_handle_draw_whiteness(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawWhiteness *op = spice_msg_in_parsed(in);
    DRAW(whiteness);
}

/* coroutine context */
static void display_handle_draw_invers(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawInvers *op = spice_msg_in_parsed(in);
    DRAW(invers);
}

/* coroutine context */
static void display_handle_draw_rop3(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawRop3 *op = spice_msg_in_parsed(in);
    DRAW(rop3);
}

/* coroutine context */
static void display_handle_draw_stroke(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawStroke *op = spice_msg_in_parsed(in);
    DRAW(stroke);
}

/* coroutine context */
static void display_handle_draw_text(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawText *op = spice_msg_in_parsed(in);
    DRAW(text);
}

/* coroutine context */
static void display_handle_draw_transparent(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawTransparent *op = spice_msg_in_parsed(in);
    DRAW(transparent);
}

/* coroutine context */
static void display_handle_draw_alpha_blend(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawAlphaBlend *op = spice_msg_in_parsed(in);
    DRAW(alpha_blend);
}

/* coroutine context */
static void display_handle_draw_composite(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayDrawComposite *op = spice_msg_in_parsed(in);
    DRAW(composite);
}

/* coroutine context */
static void display_handle_surface_create(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    SpiceMsgSurfaceCreate *create = spice_msg_in_parsed(in);
    display_surface *surface = g_new0(display_surface, 1);

    surface->surface_id = create->surface_id;
    surface->format = create->format;
    surface->width  = create->width;
    surface->height = create->height;
    surface->stride = create->width * 4;
    surface->size   = surface->height * surface->stride;

    if (create->flags & SPICE_SURFACE_FLAGS_PRIMARY) {
        SPICE_DEBUG("primary flags: %x", create->flags);
        surface->primary = true;
        create_canvas(channel, surface);
        if (c->mark_false_event_id != 0) {
            g_source_remove(c->mark_false_event_id);
            c->mark_false_event_id = FALSE;
        }
    } else {
        surface->primary = false;
        create_canvas(channel, surface);
    }
}

static gboolean display_mark_false(gpointer data)
{
    SpiceChannel *channel = data;
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;

    c->mark = FALSE;
    g_signal_emit(channel, signals[SPICE_DISPLAY_MARK], 0, FALSE);

    c->mark_false_event_id = 0;
    return FALSE;
}

/* coroutine context */
static void display_handle_surface_destroy(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgSurfaceDestroy *destroy = spice_msg_in_parsed(in);
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    display_surface *surface;

    g_return_if_fail(destroy != NULL);

    surface = find_surface(c, destroy->surface_id);
    if (surface == NULL) {
        /* this is not a problem in spicec, it happens as well and returns.. */
        /* g_warn_if_reached(); */
        return;
    }
    if (surface->primary) {
        int id = spice_channel_get_channel_id(channel);
        CHANNEL_DEBUG(channel, "%d: FIXME primary destroy, but is display really disabled?", id);
        /* this is done with a timeout in spicec as well, it's *ugly* */
        if (id != 0 && c->mark_false_event_id == 0) {
            c->mark_false_event_id = g_timeout_add_seconds(1, display_mark_false, channel);
        }
        c->primary = NULL;
        g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_PRIMARY_DESTROY], 0);
    }

    g_hash_table_remove(c->surfaces, GINT_TO_POINTER(surface->surface_id));
}

#define CLAMP_CHECK(x, low, high)  (((x) > (high)) ? TRUE : (((x) < (low)) ? TRUE : FALSE))

/* coroutine context */
static void display_handle_monitors_config(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayMonitorsConfig *config = spice_msg_in_parsed(in);
    SpiceDisplayChannelPrivate *c = SPICE_DISPLAY_CHANNEL(channel)->priv;
    guint i;

    g_return_if_fail(config != NULL);

    if (config->count == 0) {
        CHANNEL_DEBUG(channel, "received empty monitor config");
        return;
    }

    CHANNEL_DEBUG(channel, "received new monitors config from guest: n: %d/%d", config->count, config->max_allowed);

    c->monitors_max = config->max_allowed;
    if (CLAMP_CHECK(c->monitors_max, 1, MONITORS_MAX)) {
        g_warning("MonitorConfig max_allowed is not within permitted range, clamping");
        c->monitors_max = CLAMP(c->monitors_max, 1, MONITORS_MAX);
    }

    if (CLAMP_CHECK(config->count, 1, c->monitors_max)) {
        g_warning("MonitorConfig count is not within permitted range, clamping");
        config->count = CLAMP(config->count, 1, c->monitors_max);
    }

    c->monitors = g_array_set_size(c->monitors, config->count);

    for (i = 0; i < config->count; i++) {
        SpiceDisplayMonitorConfig *mc = &g_array_index(c->monitors, SpiceDisplayMonitorConfig, i);
        SpiceHead *head = &config->heads[i];
        CHANNEL_DEBUG(channel, "monitor id: %u, surface id: %u, +%u+%u-%ux%u",
                    head->id, head->surface_id,
                    head->x, head->y, head->width, head->height);
        mc->id = head->id;
        mc->surface_id = head->surface_id;
        mc->x = head->x;
        mc->y = head->y;
        mc->width = head->width;
        mc->height = head->height;
    }

    g_coroutine_object_notify(G_OBJECT(channel), "monitors");
}


#ifdef G_OS_UNIX
/* coroutine context */
static void display_handle_gl_scanout_unix(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceDisplayChannel *display = SPICE_DISPLAY_CHANNEL(channel);
    SpiceDisplayChannelPrivate *c = display->priv;
    SpiceMsgDisplayGlScanoutUnix *scanout = spice_msg_in_parsed(in);

    scanout->drm_dma_buf_fd = -1;
    if (scanout->drm_fourcc_format != 0) {
        scanout->drm_dma_buf_fd = spice_channel_unix_read_fd(channel);
        CHANNEL_DEBUG(channel, "gl scanout fd: %d", scanout->drm_dma_buf_fd);
    }

    c->scanout.y0top = scanout->flags & SPICE_GL_SCANOUT_FLAGS_Y0TOP;
    if (c->scanout.fd >= 0)
        close(c->scanout.fd);
    c->scanout.fd = scanout->drm_dma_buf_fd;
    c->scanout.width = scanout->width;
    c->scanout.height = scanout->height;
    c->scanout.stride = scanout->stride;
    c->scanout.format = scanout->drm_fourcc_format;

    g_coroutine_object_notify(G_OBJECT(channel), "gl-scanout");
}
#endif

/* coroutine context */
static void display_handle_gl_draw(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgDisplayGlDraw *draw = spice_msg_in_parsed(in);

    CHANNEL_DEBUG(channel, "gl draw %ux%u+%u+%u",
                  draw->w, draw->h, draw->x, draw->y);

    g_coroutine_signal_emit(channel, signals[SPICE_DISPLAY_GL_DRAW], 0,
                            draw->x, draw->y,
                            draw->w, draw->h);
}

/**
 * spice_display_gl_draw_done:
 * @channel: a #SpiceDisplayChannel
 *
 * After a SpiceDisplayChannel::gl-draw is emitted, the client should
 * draw the current display with the current GL scanout, and must
 * release the GL resource with a call to spice_display_gl_draw_done()
 * (failing to do so for each gl-draw may result in a frozen display).
 *
 * Since: 0.31
 **/
void spice_display_gl_draw_done(SpiceDisplayChannel *display)
{
    SpiceChannel *channel;
    SpiceMsgOut *out;

    g_return_if_fail(SPICE_IS_DISPLAY_CHANNEL(display));
    channel = SPICE_CHANNEL(display);

    out = spice_msg_out_new(channel, SPICE_MSGC_DISPLAY_GL_DRAW_DONE);
    out->marshallers->msgc_display_gl_draw_done(out->marshaller, NULL);
    spice_msg_out_send_internal(out);
}

static void channel_set_handlers(SpiceChannelClass *klass)
{
    static const spice_msg_handler handlers[] = {
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
        [ SPICE_MSG_DISPLAY_STREAM_DATA_SIZED ]  = display_handle_stream_data,
        [ SPICE_MSG_DISPLAY_STREAM_ACTIVATE_REPORT ] = display_handle_stream_activate_report,

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
        [ SPICE_MSG_DISPLAY_DRAW_COMPOSITE ]     = display_handle_draw_composite,

        [ SPICE_MSG_DISPLAY_SURFACE_CREATE ]     = display_handle_surface_create,
        [ SPICE_MSG_DISPLAY_SURFACE_DESTROY ]    = display_handle_surface_destroy,

        [ SPICE_MSG_DISPLAY_MONITORS_CONFIG ]    = display_handle_monitors_config,
#ifdef G_OS_UNIX
        [ SPICE_MSG_DISPLAY_GL_SCANOUT_UNIX ]    = display_handle_gl_scanout_unix,
#endif
        [ SPICE_MSG_DISPLAY_GL_DRAW ]            = display_handle_gl_draw,
    };

    spice_channel_set_handlers(klass, handlers, G_N_ELEMENTS(handlers));
}
