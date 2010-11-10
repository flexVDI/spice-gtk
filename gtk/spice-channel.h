#ifndef __SPICE_CLIENT_CHANNEL_H__
#define __SPICE_CLIENT_CHANNEL_H__

G_BEGIN_DECLS

#define SPICE_TYPE_CHANNEL            (spice_channel_get_type ())
#define SPICE_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPICE_TYPE_CHANNEL, SpiceChannel))
#define SPICE_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPICE_TYPE_CHANNEL, SpiceChannelClass))
#define SPICE_IS_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPICE_TYPE_CHANNEL))
#define SPICE_IS_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPICE_TYPE_CHANNEL))
#define SPICE_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPICE_TYPE_CHANNEL, SpiceChannelClass))

typedef struct spice_msg_in  spice_msg_in;
typedef struct spice_msg_out spice_msg_out;

enum SpiceChannelEvent {
    SPICE_CHANNEL_NONE = 0,
    SPICE_CHANNEL_OPENED = 10,
    SPICE_CHANNEL_CLOSED,
    SPICE_CHANNEL_ERROR_CONNECT = 20,
    SPICE_CHANNEL_ERROR_TLS,
    SPICE_CHANNEL_ERROR_LINK,
    SPICE_CHANNEL_ERROR_AUTH,
    SPICE_CHANNEL_ERROR_IO,
};

struct _SpiceChannel
{
    GObject parent;
    spice_channel *priv;
    /* Do not add fields to this struct */
};

struct _SpiceChannelClass
{
    GObjectClass parent_class;

    /* virtual methods */
    void (*handle_msg)(SpiceChannel *channel, spice_msg_in *msg);
    void (*channel_up)(SpiceChannel *channel);

    /* signals */
    void (*spice_channel_event)(SpiceChannel *channel, enum SpiceChannelEvent event);

#if 0
    /*
     * If adding fields to this struct, remove corresponding
     * amount of padding to avoid changing overall struct size
     */
    gpointer _spice_reserved[42];
#endif
};

GType spice_channel_get_type(void) G_GNUC_CONST;

typedef void (*spice_msg_handler)(SpiceChannel *channel, spice_msg_in *in);

SpiceChannel *spice_channel_new(SpiceSession *s, int type, int id);
void spice_channel_destroy(SpiceChannel *channel);
gboolean spice_channel_connect(SpiceChannel *channel);
void spice_channel_disconnect(SpiceChannel *channel, enum SpiceChannelEvent event);

G_END_DECLS

#endif /* __SPICE_CLIENT_CHANNEL_H__ */
