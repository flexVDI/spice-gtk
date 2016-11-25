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

#include "gtk-compat.h"
#include "spice-widget.h"
#include "spice-widget-priv.h"
#include "spice-gtk-session-priv.h"

#ifdef USE_VA
#include "va_display_x11.h"
#endif


G_GNUC_INTERNAL
int spicex_image_create(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    if (d->ximage != NULL)
        return 0;

    if (d->format == SPICE_SURFACE_FMT_16_555 ||
        d->format == SPICE_SURFACE_FMT_16_565) {
        d->convert = TRUE;
        d->data = g_malloc0(d->area.width * d->area.height * 4);

        d->ximage = cairo_image_surface_create_for_data
            (d->data, CAIRO_FORMAT_RGB24, d->area.width, d->area.height, d->area.width * 4);

    } else {
        d->convert = FALSE;

        d->ximage = cairo_image_surface_create_for_data
            (d->data, CAIRO_FORMAT_RGB24, d->width, d->height, d->stride);
    }

    return 0;
}

G_GNUC_INTERNAL
void spicex_image_destroy(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    if (d->ximage) {
        cairo_surface_destroy(d->ximage);
        d->ximage = NULL;
    }
    if (d->convert && d->data) {
        g_free(d->data);
        d->data = NULL;
    }
    d->convert = FALSE;
}

G_GNUC_INTERNAL
void spicex_draw_event(SpiceDisplay *display, cairo_t *cr)
{
    SpiceDisplayPrivate *d = display->priv;
    cairo_rectangle_int_t rect;
    cairo_region_t *region;
    double s;
    int x, y;
    int ww, wh;
    int w, h;

    spice_display_get_scaling(display, &s, &x, &y, &w, &h);

    gdk_drawable_get_size(gtk_widget_get_window(GTK_WIDGET(display)), &ww, &wh);

    /* We need to paint the bg color around the image */
    rect.x = 0;
    rect.y = 0;
    rect.width = ww;
    rect.height = wh;
    region = cairo_region_create_rectangle(&rect);

    /* Optionally cut out the inner area where the pixmap
       will be drawn. This avoids 'flashing' since we're
       not double-buffering. */
    if (d->ximage) {
        rect.x = x;
        rect.y = y;
        rect.width = w;
        rect.height = h;
        cairo_region_subtract_rectangle(region, &rect);
    }

    gdk_cairo_region (cr, region);
    cairo_region_destroy (region);

    /* Need to set a real solid color, because the default is usually
       transparent these days, and non-double buffered windows can't
       render transparently */
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_fill(cr);

    /* Draw the display */
    if (d->ximage) {
        cairo_translate(cr, x, y);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_scale(cr, s, s);
        if (!d->convert)
            cairo_translate(cr, -d->area.x, -d->area.y);
        cairo_set_source_surface(cr, d->ximage, 0, 0);
        cairo_fill(cr);

#ifdef USE_VA
        GSList *va_sessions = NULL, *v;
        g_object_get(d->display, "va-sessions", &va_sessions, NULL);
        for (v = va_sessions; v != NULL; v = g_slist_next(v)) {
            tinyjpeg_session *session = (tinyjpeg_session *)v->data;
            va_x11_draw_frame(session, cr);
        }
        g_slist_free(va_sessions);
#endif

        if (d->time_to_inactivity < 30000) {
            cairo_translate(cr, 0, 0);
            cairo_rectangle(cr, 0, 0, d->width, d->height);
            const double fadeout = 10000.0;
            const double alpha_max = 0.8;
            double alpha = ((30000.0 - d->time_to_inactivity) / fadeout) * alpha_max;
            if (alpha > alpha_max) alpha = alpha_max;
            cairo_set_source_rgba(cr, 0, 0, 0, alpha);
            cairo_fill(cr);

            cairo_text_extents_t extents;
            double size = 20.0;
            const char * pattern = "Your connection will close in %d seconds due to inactivity";
            cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, size);
            cairo_text_extents(cr, pattern, &extents);
            size *= (d->width*0.8)/extents.width;
            cairo_set_font_size(cr, size);

            int seconds = (d->time_to_inactivity + 999) / 1000;
            char * msg = g_strdup_printf(pattern, seconds);
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, alpha);
            cairo_text_extents(cr, msg, &extents);
            cairo_move_to(cr, (d->width - extents.width)/2, (d->height - extents.height)/2);
            cairo_show_text(cr, msg);
            g_free(msg);
        }

        if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER &&
            d->mouse_guest_x != -1 && d->mouse_guest_y != -1 &&
            !d->show_cursor &&
            spice_gtk_session_get_pointer_grabbed(d->gtk_session)) {
            GdkPixbuf *image = d->mouse_pixbuf;
            if (image != NULL) {
                gdk_cairo_set_source_pixbuf(cr, image,
                                            d->mouse_guest_x - d->mouse_hotspot.x,
                                            d->mouse_guest_y - d->mouse_hotspot.y);
                cairo_paint(cr);
            }
        }
    }
}

#if ! GTK_CHECK_VERSION (2, 91, 0)
G_GNUC_INTERNAL
void spicex_expose_event(SpiceDisplay *display, GdkEventExpose *expose)
{
    cairo_t *cr;

    cr = gdk_cairo_create(gtk_widget_get_window(GTK_WIDGET(display)));
    cairo_rectangle(cr,
                    expose->area.x,
                    expose->area.y,
                    expose->area.width,
                    expose->area.height);
    cairo_clip(cr);

    spicex_draw_event(display, cr);

    cairo_destroy(cr);
}
#endif

G_GNUC_INTERNAL
gboolean spicex_is_scaled(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    return d->allow_scaling;
}
