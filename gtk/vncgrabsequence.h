/*
 * GTK VNC Widget
 *
 * Copyright (C) 2006  Anthony Liguori <anthony@codemonkey.ws>
 * Copyright (C) 2009-2010 Daniel P. Berrange <dan@berrange.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.0 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef VNC_GRAB_SEQUENCE_H
#define VNC_GRAB_SEQUENCE_H

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define VNC_TYPE_GRAB_SEQUENCE            (vnc_grab_sequence_get_type ())

typedef struct _VncGrabSequence VncGrabSequence;

struct _VncGrabSequence {
	guint nkeysyms;
	guint *keysyms;

	/* Do not add fields to this struct */
};

GType vnc_grab_sequence_get_type(void);

VncGrabSequence *vnc_grab_sequence_new(guint nkeysyms, guint *keysyms);
VncGrabSequence *vnc_grab_sequence_new_from_string(const gchar *str);
VncGrabSequence *vnc_grab_sequence_copy(VncGrabSequence *sequence);
void vnc_grab_sequence_free(VncGrabSequence *sequence);
gchar *vnc_grab_sequence_as_string(VncGrabSequence *sequence);


G_END_DECLS

#endif /* VNC_GRAB_SEQUENCE_H */

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
