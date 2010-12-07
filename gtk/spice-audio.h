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
#ifndef __SPICE_CLIENT_AUDIO_H__
#define __SPICE_CLIENT_AUDIO_H__

#include <glib-object.h>
#include "spice-util.h"
#include "spice-session.h"

G_BEGIN_DECLS

#define SPICE_TYPE_AUDIO spice_audio_get_type()

#define SPICE_AUDIO(obj)					\
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPICE_TYPE_AUDIO, SpiceAudio))

#define SPICE_AUDIO_CLASS(klass)				\
    (G_TYPE_CHECK_CLASS_CAST ((klass), SPICE_TYPE_AUDIO, SpiceAudioClass))

#define SPICE_IS_AUDIO(obj)                                     \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPICE_TYPE_AUDIO))

#define SPICE_IS_AUDIO_CLASS(klass)                             \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), SPICE_TYPE_AUDIO))

#define SPICE_AUDIO_GET_CLASS(obj)				\
    (G_TYPE_INSTANCE_GET_CLASS ((obj), SPICE_TYPE_AUDIO, SpiceAudioClass))

typedef struct _SpiceAudio SpiceAudio;
typedef struct _SpiceAudioClass SpiceAudioClass;

struct _SpiceAudio {
    GObject parent;
};

struct _SpiceAudioClass {
    GObjectClass parent_class;

    /*< private >*/
    gchar _spice_reserved[SPICE_RESERVED_PADDING];
};

GType spice_audio_get_type(void);

SpiceAudio *spice_audio_new(SpiceSession *session,
                         GMainContext *context,
                         const char *name);

G_END_DECLS

#endif /* __SPICE_CLIENT_AUDIO_H__ */
