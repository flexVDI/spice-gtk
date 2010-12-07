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
#ifndef __SPICE_CLIENT_PLAYBACK_CHANNEL_H__
#define __SPICE_CLIENT_PLAYBACK_CHANNEL_H__

#include "spice-client.h"

G_BEGIN_DECLS

#define SPICE_TYPE_PLAYBACK_CHANNEL            (spice_playback_channel_get_type())
#define SPICE_PLAYBACK_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SPICE_TYPE_PLAYBACK_CHANNEL, SpicePlaybackChannel))
#define SPICE_PLAYBACK_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SPICE_TYPE_PLAYBACK_CHANNEL, SpicePlaybackChannelClass))
#define SPICE_IS_PLAYBACK_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SPICE_TYPE_PLAYBACK_CHANNEL))
#define SPICE_IS_PLAYBACK_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SPICE_TYPE_PLAYBACK_CHANNEL))
#define SPICE_PLAYBACK_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SPICE_TYPE_PLAYBACK_CHANNEL, SpicePlaybackChannelClass))

typedef struct _SpicePlaybackChannel SpicePlaybackChannel;
typedef struct _SpicePlaybackChannelClass SpicePlaybackChannelClass;
typedef struct spice_playback_channel spice_playback_channel;

struct _SpicePlaybackChannel {
    SpiceChannel parent;
    spice_playback_channel *priv;
    /* Do not add fields to this struct */
};

struct _SpicePlaybackChannelClass {
    SpiceChannelClass parent_class;

    /* signals */
    void (*playback_start)(SpicePlaybackChannel *channel,
                           gint format, gint channels, gint freq);
    void (*playback_data)(SpicePlaybackChannel *channel, gpointer *data, gint size);
    void (*playback_stop)(SpicePlaybackChannel *channel);

    /*< private >*/
    /*
     * If adding fields to this struct, remove corresponding
     * amount of padding to avoid changing overall struct size
     */
    gchar _spice_reserved[SPICE_RESERVED_PADDING];
};

GType	        spice_playback_channel_get_type(void);

G_END_DECLS

#endif /* __SPICE_CLIENT_PLAYBACK_CHANNEL_H__ */
