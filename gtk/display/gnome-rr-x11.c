/* gnome-rr-x11.c
 *
 * Copyright 2007, 2008, Red Hat, Inc.
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
 * Author: Soren Sandmann <sandmann@redhat.com>
 */

#define GNOME_DESKTOP_USE_UNSTABLE_API

#include <config.h>
#include <glib/gi18n-lib.h>
#include <string.h>

#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

#include <gtk/gtk.h>

#ifdef HAVE_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>
#endif

#undef GNOME_DISABLE_DEPRECATED
#include "gnome-rr.h"
#include "gnome-rr-config.h"

#include "gnome-rr-private.h"

#define DISPLAY(o) ((o)->info->screen->priv->xdisplay)

#ifdef HAVE_RANDR
#define RANDR_LIBRARY_IS_AT_LEAST_1_3 (RANDR_MAJOR > 1 || (RANDR_MAJOR == 1 && RANDR_MINOR >= 3))
#else
#define RANDR_LIBRARY_IS_AT_LEAST_1_3 0
#endif

#define SERVERS_RANDR_IS_AT_LEAST_1_3(priv) (priv->rr_major_version > 1 || (priv->rr_major_version == 1 && priv->rr_minor_version >= 3))
