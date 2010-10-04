#ifndef __SPICE_CLIENT_MAIN_CHANNEL_H__
#define __SPICE_CLIENT_MAIN_CHANNEL_H__

#include "spice-client.h"

G_BEGIN_DECLS

#define SPICE_TYPE_MAIN_CHANNEL            (spice_main_channel_get_type())
#define SPICE_MAIN_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SPICE_TYPE_MAIN_CHANNEL, SpiceMainChannel))
#define SPICE_MAIN_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SPICE_TYPE_MAIN_CHANNEL, SpiceMainChannelClass))
#define SPICE_IS_MAIN_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SPICE_TYPE_MAIN_CHANNEL))
#define SPICE_IS_MAIN_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SPICE_TYPE_MAIN_CHANNEL))
#define SPICE_MAIN_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SPICE_TYPE_MAIN_CHANNEL, SpiceMainChannelClass))

typedef struct _SpiceMainChannel SpiceMainChannel;
typedef struct _SpiceMainChannelClass SpiceMainChannelClass;
typedef struct spice_main_channel spice_main_channel;

struct _SpiceMainChannel {
    SpiceChannel parent;
    spice_main_channel *priv;
    /* Do not add fields to this struct */
};

struct _SpiceMainChannelClass {
    SpiceChannelClass parent_class;

    /* signals */
    void (*spice_main_mouse_update)(SpiceChannel *channel);
    void (*spice_main_agent_update)(SpiceChannel *channel);

    /* Do not add fields to this struct */
};

GType	        spice_main_channel_get_type(void);

void spice_main_set_display(SpiceChannel *channel, int id,
                            int x, int y, int width, int height);

void spice_main_clipboard_grab(SpiceChannel *channel, int *types, int ntypes);
void spice_main_clipboard_release(SpiceChannel *channel);

G_END_DECLS

#endif /* __SPICE_CLIENT_MAIN_CHANNEL_H__ */
