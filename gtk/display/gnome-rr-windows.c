/* gnome-rr.c
 *
 * Copyright 2011, Red Hat, Inc.
 *
 * This file is part of the Gnome Library.
 *
 * The Gnome Library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Gnome Library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with the Gnome Library; see the file COPYING.LIB.  If not,
 * write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Marc-André Lureau <marcandre.lureau@redhat.com>
 */

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include <config.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#include <windows.h>
#include <winuser.h>

#include <gtk/gtk.h>

#undef GNOME_DISABLE_DEPRECATED
#include "gnome-rr.h"
#include "gnome-rr-config.h"
#include "gnome-rr-private.h"
#include "gnome-rr-windows.h"

struct GnomeRRWindowsScreenPrivate
{
    DISPLAY_DEVICE device;
};

static void gnome_rr_windows_screen_initable_iface_init (GInitableIface *iface);
G_DEFINE_TYPE_WITH_CODE (GnomeRRWindowsScreen, gnome_rr_windows_screen, GNOME_TYPE_RR_SCREEN,
        G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, gnome_rr_windows_screen_initable_iface_init))

static gboolean
gnome_rr_windows_screen_initable_init (GInitable *initable, GCancellable *canc, GError **error)
{
    GInitableIface *iface, *parent_iface;

    iface = G_TYPE_INSTANCE_GET_INTERFACE(initable, G_TYPE_INITABLE, GInitableIface);
    parent_iface = g_type_interface_peek_parent(iface);

    if (!parent_iface->init (initable, canc, error))
      return FALSE;

    return TRUE;
}

static void
gnome_rr_windows_screen_initable_iface_init (GInitableIface *iface)
{
    iface->init = gnome_rr_windows_screen_initable_init;
}

static void
gnome_rr_windows_screen_finalize (GObject *gobject)
{
    G_OBJECT_CLASS (gnome_rr_windows_screen_parent_class)->finalize (gobject);
}

static void
gnome_rr_windows_screen_class_init (GnomeRRWindowsScreenClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    g_type_class_add_private (klass, sizeof (GnomeRRWindowsScreenPrivate));

    gobject_class->finalize = gnome_rr_windows_screen_finalize;
}

static void
gnome_rr_windows_screen_init (GnomeRRWindowsScreen *self)
{
    GnomeRRWindowsScreenPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GNOME_TYPE_RR_WINDOWS_SCREEN, GnomeRRWindowsScreenPrivate);

    self->priv = priv;
}

gboolean
fill_out_screen_info (GnomeRRScreen *screen, ScreenInfo *info,
                      gboolean needs_reprobe, GError **error)
{
    return FALSE;
}

gboolean
gnome_rr_crtc_set_config_with_time (GnomeRRCrtc      *crtc,
				    guint32           timestamp,
				    int               x,
				    int               y,
				    GnomeRRMode      *mode,
				    GnomeRRRotation   rotation,
				    GnomeRROutput   **outputs,
				    int               n_outputs,
				    GError          **error)
{
    g_return_val_if_fail (crtc != NULL, FALSE);
    g_return_val_if_fail (mode != NULL || outputs == NULL || n_outputs == 0, FALSE);
    g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

    return FALSE;
}

void
gnome_rr_screen_set_size (GnomeRRScreen *self,
			  int	      width,
			  int       height,
			  int       mm_width,
			  int       mm_height)
{
    g_return_if_fail (GNOME_IS_RR_WINDOWS_SCREEN (self));
}

void
gnome_rr_crtc_set_gamma (GnomeRRCrtc *crtc, int size,
			 unsigned short *red,
			 unsigned short *green,
			 unsigned short *blue)
{
    g_return_if_fail (crtc != NULL);
    g_return_if_fail (red != NULL);
    g_return_if_fail (green != NULL);
    g_return_if_fail (blue != NULL);
}

gboolean
gnome_rr_crtc_get_gamma (GnomeRRCrtc *crtc, int *size,
			 unsigned short **red, unsigned short **green,
			 unsigned short **blue)
{
    g_return_val_if_fail (crtc != NULL, FALSE);
    return FALSE;
}
