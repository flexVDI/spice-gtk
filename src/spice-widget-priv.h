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
#ifndef __SPICE_WIDGET_PRIV_H__
#define __SPICE_WIDGET_PRIV_H__

#include "config.h"

#ifdef WIN32
#include <windows.h>
#endif

#ifdef HAVE_EPOXY_EGL_H
#include <epoxy/egl.h>
#endif

#include "spice-widget.h"
#include "spice-common.h"
#include "spice-gtk-session.h"

G_BEGIN_DECLS

#define DISPLAY_DEBUG(display, fmt, ...) \
    SPICE_DEBUG("%d:%d " fmt, \
                SPICE_DISPLAY(display)->priv->channel_id, \
                SPICE_DISPLAY(display)->priv->monitor_id, \
                ## __VA_ARGS__)

typedef struct _SpiceDisplayPrivate SpiceDisplayPrivate;

struct _SpiceDisplay {
    GtkEventBox parent;
    SpiceDisplayPrivate *priv;
};

struct _SpiceDisplayClass {
    GtkEventBoxClass parent_class;

    /* signals */
    void (*mouse_grab)(SpiceChannel *channel, gint grabbed);
    void (*keyboard_grab)(SpiceChannel *channel, gint grabbed);
};

struct _SpiceDisplayPrivate {
    GtkStack                *stack;
    gint                    channel_id;
    gint                    monitor_id;

    /* options */
    bool                    keyboard_grab_enable;
    gboolean                keyboard_grab_inhibit;
    bool                    mouse_grab_enable;
    bool                    resize_guest_enable;

    /* state */
    gboolean                ready;
    gboolean                monitor_ready;
    struct {
        enum SpiceSurfaceFmt    format;
        gint                    width, height, stride;
        gpointer                data_origin; /* the original display image data */
        gpointer                data; /* converted if necessary to 32 bits */
        bool                    convert;
        cairo_surface_t         *surface;
    } canvas;
    GdkRectangle            area;
    /* window border */
    gint                    ww, wh, mx, my;

    gboolean                allow_scaling;
    gboolean                only_downscale;
    gboolean                disable_inputs;

    SpiceSession            *session;
    SpiceGtkSession         *gtk_session;
    SpiceMainChannel        *main;
    SpiceDisplayChannel     *display;
    SpiceCursorChannel      *cursor;
    SpiceInputsChannel      *inputs;
    SpiceSmartcardChannel   *smartcard;

    enum SpiceMouseMode     mouse_mode;
    int                     mouse_grab_active;
    bool                    mouse_have_pointer;
    GdkCursor               *mouse_cursor;
    GdkPixbuf               *mouse_pixbuf;
    GdkPoint                mouse_hotspot;
    GdkCursor               *show_cursor;
    int                     mouse_last_x;
    int                     mouse_last_y;
    int                     mouse_guest_x;
    int                     mouse_guest_y;

    bool                    keyboard_grab_active;
    bool                    keyboard_have_focus;

    const guint16          *keycode_map;
    size_t                  keycode_maplen;
    uint32_t                key_state[512 / 32];
    int                     key_delayed_scancode;
    guint                   key_delayed_id;
    SpiceGrabSequence         *grabseq; /* the configured key sequence */
    gboolean                *activeseq; /* the currently pressed keys */
    gboolean                seq_pressed;
    gboolean                keyboard_grab_released;
    gint                    mark;
#ifdef WIN32
    HHOOK                   keyboard_hook;
    int                     win_mouse[3];
    int                     win_mouse_speed;
#endif
    guint                   keypress_delay;
    gint                    zoom_level;
#ifdef GDK_WINDOWING_X11
    int                     x11_accel_numerator;
    int                     x11_accel_denominator;
    int                     x11_threshold;
#endif
#if HAVE_EGL
    struct {
        gboolean            context_ready;
        gboolean            enabled;
        EGLSurface          surface;
        EGLDisplay          display;
        EGLConfig           conf;
        EGLContext          ctx;
        gint                mproj, attr_pos, attr_tex;
        guint               vbuf_id;
        guint               tex_id;
        guint               tex_pointer_id;
        guint               prog;
        EGLImageKHR         image;
        gboolean            call_draw_done;
        SpiceGlScanout      scanout;
    } egl;
#endif // HAVE_EGL
    double scroll_delta_y;
};

int      spice_cairo_image_create                 (SpiceDisplay *display);
void     spice_cairo_image_destroy                (SpiceDisplay *display);
void     spice_cairo_draw_event                   (SpiceDisplay *display, cairo_t *cr);
gboolean spice_cairo_is_scaled                    (SpiceDisplay *display);
void     spice_display_get_scaling           (SpiceDisplay *display, double *s, int *x, int *y, int *w, int *h);
gboolean spice_egl_init                      (SpiceDisplay *display, GError **err);
gboolean spice_egl_realize_display           (SpiceDisplay *display, GdkWindow *win,
                                              GError **err);
void     spice_egl_unrealize_display         (SpiceDisplay *display);
void     spice_egl_update_display            (SpiceDisplay *display);
void     spice_egl_resize_display            (SpiceDisplay *display, int w, int h);
gboolean spice_egl_update_scanout            (SpiceDisplay *display,
                                              const SpiceGlScanout *scanout,
                                              GError **err);
void     spice_egl_cursor_set                (SpiceDisplay *display);

#ifdef HAVE_EGL
void     spice_display_widget_gl_scanout     (SpiceDisplay *display);
#endif
void     spice_display_widget_update_monitor_area(SpiceDisplay *display);

G_END_DECLS

#endif
