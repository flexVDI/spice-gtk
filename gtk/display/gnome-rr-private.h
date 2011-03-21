/* gnome-rr-private.h
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

#ifndef GNOME_RR_PRIVATE_H
#define GNOME_RR_PRIVATE_H

#include <config.h>

#ifdef HAVE_RANDR
#include <X11/extensions/Xrandr.h>
#endif

typedef struct ScreenInfo ScreenInfo;

struct ScreenInfo
{
    int			min_width;
    int			max_width;
    int			min_height;
    int			max_height;

#ifdef HAVE_RANDR
    XRRScreenResources *resources;
#endif

    GnomeRROutput **	outputs;
    GnomeRRCrtc **	crtcs;
    GnomeRRMode **	modes;

    GnomeRRScreen *	screen;

    GnomeRRMode **	clone_modes;

#ifdef HAVE_RANDR
    RROutput            primary;
#endif
};

struct GnomeRRScreenPrivate
{
    GdkScreen *			gdk_screen;
    GdkWindow *			gdk_root;
    ScreenInfo *		info;

#ifdef HAVE_X11
    Display *			xdisplay;
    Screen *			xscreen;
    Window			xroot;

    int				randr_event_base;
    int				rr_major_version;
    int				rr_minor_version;
    Atom                        connector_type_atom;
#endif
};

struct GnomeRROutputInfoPrivate
{
    char *		name;

    gboolean		on;
    int			width;
    int			height;
    int			rate;
    int			x;
    int			y;
    GnomeRRRotation	rotation;

    gboolean		connected;
    gchar		vendor[4];
    guint		product;
    guint		serial;
    double		aspect;
    int			pref_width;
    int			pref_height;
    char *		display_name;
    gboolean            primary;
};

struct GnomeRRConfigPrivate
{
  gboolean clone;
  GnomeRRScreen *screen;
  GnomeRROutputInfo **outputs;
};

#ifndef HAVE_RANDR
/* This is to avoid a ton of ifdefs wherever we use a type from libXrandr */
typedef int RROutput;
typedef int RRCrtc;
typedef int RRMode;
typedef int Rotation;
#define RR_Rotate_0		1
#define RR_Rotate_90		2
#define RR_Rotate_180		4
#define RR_Rotate_270		8
#define RR_Reflect_X		16
#define RR_Reflect_Y		32
#endif

struct GnomeRROutput
{
    ScreenInfo *	info;
    RROutput		id;

    char *		name;
    GnomeRRCrtc *	current_crtc;
    gboolean		connected;
    gulong		width_mm;
    gulong		height_mm;
    GnomeRRCrtc **	possible_crtcs;
    GnomeRROutput **	clones;
    GnomeRRMode **	modes;
    int			n_preferred;
    guint8 *		edid_data;
    int         edid_size;
    char *              connector_type;
};

struct GnomeRROutputWrap
{
    RROutput		id;
};

struct GnomeRRCrtc
{
    ScreenInfo *	info;
    RRCrtc		id;

    GnomeRRMode *	current_mode;
    GnomeRROutput **	current_outputs;
    GnomeRROutput **	possible_outputs;
    int			x;
    int			y;

    GnomeRRRotation	current_rotation;
    GnomeRRRotation	rotations;
    int			gamma_size;
};

struct GnomeRRMode
{
    ScreenInfo *	info;
    RRMode		id;
    char *		name;
    int			width;
    int			height;
    int			freq;		/* in mHz */
};

#endif
