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
#include "spice-widget.h"
#include "spice-widget-priv.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

G_GNUC_INTERNAL
int spicex_image_create(SpiceDisplay *display)
{
    spice_display *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->format == SPICE_SURFACE_FMT_16_555 ||
        d->format == SPICE_SURFACE_FMT_16_565) {
        d->convert = TRUE;
        d->data = g_malloc0(d->height * d->stride); /* pixels are 32 bits */
    } else {
        d->convert = FALSE;
    }

    d->ximage = cairo_image_surface_create_for_data
        (d->data, CAIRO_FORMAT_RGB24, d->width, d->height, d->stride);

    return 0;
}

G_GNUC_INTERNAL
void spicex_image_destroy(SpiceDisplay *display)
{
    spice_display *d = SPICE_DISPLAY_GET_PRIVATE(display);

    if (d->ximage_cache) {
        cairo_surface_finish(d->ximage_cache);
        d->ximage_cache = NULL;
    }

    if (d->ximage) {
        cairo_surface_finish(d->ximage);
        d->ximage = NULL;
    }
    if (d->convert && d->data) {
        g_free(d->data);
        d->data = NULL;
    }
}

static void setup_surface_cache(spice_display *d, cairo_t *crWin)
{
    cairo_surface_t *win = cairo_get_target(crWin);
    cairo_t *crCache;

    if (d->ximage_cache)
        return;

    g_return_if_fail(d->ximage);

    /* Creates a Pixmap on the X11 server matching the Window */
    d->ximage_cache = cairo_surface_create_similar(win,
                                                   CAIRO_CONTENT_COLOR,
                                                   d->width, d->height);
    crCache = cairo_create(d->ximage_cache);

    /* Copy our local framebuffer contents to the Pixmap */
    cairo_set_source_surface(crCache, d->ximage, 0, 0);
    cairo_paint(crCache);

    cairo_destroy(crCache);
}

static gboolean draw_event(SpiceDisplay *display, cairo_t *cr)
{
    spice_display *d = SPICE_DISPLAY_GET_PRIVATE(display);
    int fbw = d->width, fbh = d->height;
    int mx = 0, my = 0;
    int ww, wh;

    if (!d->ximage_cache) {
        setup_surface_cache(d, cr);
    }

    gdk_drawable_get_size(gtk_widget_get_window(GTK_WIDGET(display)), &ww, &wh);

    if (ww > fbw)
        mx = (ww - fbw) / 2;
    if (wh > fbh)
        my = (wh - fbh) / 2;

    /* If we don't have a pixmap, or we're not scaling, then
       we need to fill with background color */
    if (!d->ximage ||
        !d->allow_scaling) {
        cairo_rectangle(cr, 0, 0, ww, wh);
        /* Optionally cut out the inner area where the pixmap
           will be drawn. This avoids 'flashing' since we're
           not double-buffering. Note we're using the undocumented
           behaviour of drawing the rectangle from right to left
           to cut out the whole */
        if (d->ximage)
            cairo_rectangle(cr, mx + fbw, my,
                            -1 * fbw, fbh);
        cairo_fill(cr);
    }

    /* Draw the display */
    if (d->ximage) {
        if (d->allow_scaling) {
            double sx, sy;
            /* Scale to fill window */
            sx = (double)ww / (double)fbw;
            sy = (double)wh / (double)fbh;
            cairo_scale(cr, sx, sy);
            cairo_set_source_surface(cr, d->ximage_cache, 0, 0);
        } else {
            cairo_set_source_surface(cr, d->ximage_cache, mx, my);
        }
        cairo_paint(cr);
    }
    return TRUE;
}

G_GNUC_INTERNAL
void spicex_expose_event(SpiceDisplay *display, GdkEventExpose *expose)
{
    cairo_t *cr;
    gboolean ret;

    cr = gdk_cairo_create(gtk_widget_get_window(GTK_WIDGET(display)));
    cairo_rectangle(cr,
                    expose->area.x,
                    expose->area.y,
                    expose->area.width,
                    expose->area.height);
    cairo_clip(cr);

    ret = draw_event(display, cr);

    cairo_destroy(cr);
}

G_GNUC_INTERNAL
void spicex_image_invalidate(SpiceDisplay *display,
                             gint *x, gint *y, gint *w, gint *h)
{
    spice_display *d = SPICE_DISPLAY_GET_PRIVATE(display);
    int ww, wh;

    gdk_drawable_get_size(gtk_widget_get_window(GTK_WIDGET(display)), &ww, &wh);

    /* If we have a pixmap, update the region which changed.
     * If we don't have a pixmap, the entire thing will be
     * created & rendered during the drawing handler
     */
    if (d->ximage_cache) {
        cairo_t *cr = cairo_create(d->ximage_cache);

        cairo_rectangle(cr, *x, *y, *w, *h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, d->ximage, 0, 0);
        cairo_paint(cr);

        cairo_destroy(cr);
    }

    if (d->allow_scaling) {
        double sx, sy;

        /* Scale the exposed region */
        sx = (double)ww / (double)d->width;
        sy = (double)wh / (double)d->height;

        *x *= sx;
        *y *= sy;
        *w *= sx;
        *h *= sy;

        /* FIXME: same hack as gtk-vnc */
        /* Without this, we get horizontal & vertical line artifacts
         * when drawing. This "fix" is somewhat dubious though. The
         * true mistake & fix almost certainly lies elsewhere.
         */
        *x -= 2;
        *y -= 2;
        *w += 4;
        *h += 4;
    } else {
        /* Offset the Spice region to produce expose region */
        *x += d->mx;
        *y += d->my;
    }
}

G_GNUC_INTERNAL
gboolean spicex_is_scaled(SpiceDisplay *display)
{
    spice_display *d = SPICE_DISPLAY_GET_PRIVATE(display);
    return d->allow_scaling;
}
