#ifndef __SPICE_CLIENT_CURSOR_CHANNEL_H__
#define __SPICE_CLIENT_CURSOR_CHANNEL_H__

#include "spice-client.h"

G_BEGIN_DECLS

#define SPICE_TYPE_CURSOR_CHANNEL            (spice_cursor_channel_get_type())
#define SPICE_CURSOR_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SPICE_TYPE_CURSOR_CHANNEL, SpiceCursorChannel))
#define SPICE_CURSOR_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SPICE_TYPE_CURSOR_CHANNEL, SpiceCursorChannelClass))
#define SPICE_IS_CURSOR_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SPICE_TYPE_CURSOR_CHANNEL))
#define SPICE_IS_CURSOR_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SPICE_TYPE_CURSOR_CHANNEL))
#define SPICE_CURSOR_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SPICE_TYPE_CURSOR_CHANNEL, SpiceCursorChannelClass))

typedef struct _SpiceCursorChannel SpiceCursorChannel;
typedef struct _SpiceCursorChannelClass SpiceCursorChannelClass;
typedef struct spice_cursor_channel spice_cursor_channel;

struct _SpiceCursorChannel {
    SpiceChannel parent;
    spice_cursor_channel *priv;
    /* Do not add fields to this struct */
};

struct _SpiceCursorChannelClass {
    SpiceChannelClass parent_class;

    /* signals */
    void (*spice_cursor_set)(SpiceCursorChannel *channel, gint width, gint height,
                             gint hot_x, gint hot_y, gpointer rgba);
    void (*spice_cursor_move)(SpiceCursorChannel *channel, gint x, gint y);
    void (*spice_cursor_hide)(SpiceCursorChannel *channel);
    void (*spice_cursor_reset)(SpiceCursorChannel *channel);

    /* Do not add fields to this struct */
};

GType	        spice_cursor_channel_get_type(void);

G_END_DECLS

#endif /* __SPICE_CLIENT_CURSOR_CHANNEL_H__ */
