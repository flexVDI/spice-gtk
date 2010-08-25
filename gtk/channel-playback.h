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
    void (*spice_playback_start)(SpicePlaybackChannel *channel,
                                 gint format, gint channels, gint freq);
    void (*spice_playback_data)(SpicePlaybackChannel *channel, gpointer *data, gint size);
    void (*spice_playback_stop)(SpicePlaybackChannel *channel);

    /* Do not add fields to this struct */
};

GType	        spice_playback_channel_get_type(void);

G_END_DECLS

#endif /* __SPICE_CLIENT_PLAYBACK_CHANNEL_H__ */
