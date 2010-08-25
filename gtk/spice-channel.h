#ifndef __SPICE_CLIENT_CHANNEL_H__
#define __SPICE_CLIENT_CHANNEL_H__

G_BEGIN_DECLS

#define SPICE_TYPE_CHANNEL            (spice_channel_get_type ())
#define SPICE_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPICE_TYPE_CHANNEL, SpiceChannel))
#define SPICE_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPICE_TYPE_CHANNEL, SpiceChannelClass))
#define SPICE_IS_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPICE_TYPE_CHANNEL))
#define SPICE_IS_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPICE_TYPE_CHANNEL))
#define SPICE_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPICE_TYPE_CHANNEL, SpiceChannelClass))

enum SpiceAgentEvent {
    SPICE_AGENT_NONE = 0,
    SPICE_AGENT_CONNECT,
    SPICE_AGENT_DISCONNECT,
};

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

/* Hmm, should better be private ... */
struct spice_msg_out {
    int                   refcount;
    SpiceChannel          *channel;
    SpiceMessageMarshallers *marshallers;
    SpiceMarshaller       *marshaller;
    SpiceDataHeader       *header;
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

    /* common signals */
    void (*spice_channel_event)(SpiceChannel *channel, enum SpiceChannelEvent event);

    /* display signals */
    void (*spice_display_primary_create)(SpiceChannel *channel, gint format,
                                         gint width, gint height, gint stride,
                                         gint shmid, gpointer data);
    void (*spice_display_primary_destroy)(SpiceChannel *channel);
    void (*spice_display_invalidate)(SpiceChannel *channel,
                                     gint x, gint y, gint w, gint h);

#if 0
    /*
     * If adding fields to this struct, remove corresponding
     * amount of padding to avoid changing overall struct size
     */
    gpointer _spice_reserved[42];
#endif
};

GType spice_channel_get_type(void) G_GNUC_CONST;

G_END_DECLS

typedef void (*spice_msg_handler)(SpiceChannel *channel, spice_msg_in *in);

SpiceChannel *spice_channel_new(SpiceSession *s, int type, int id);
void spice_channel_destroy(SpiceChannel *channel);
gboolean spice_channel_connect(SpiceChannel *channel);
void spice_channel_disconnect(SpiceChannel *channel, enum SpiceChannelEvent event);
int spice_channel_id(SpiceChannel *channel);

enum SpiceMouseMode spice_main_get_mouse_mode(SpiceChannel *channel);
void spice_main_set_display(SpiceChannel *channel, int id,
                            int x, int y, int width, int height);

spice_msg_in *spice_msg_in_new(SpiceChannel *channel);
void spice_msg_in_get(spice_msg_in *in);
void spice_msg_in_put(spice_msg_in *in);
int spice_msg_in_type(spice_msg_in *in);
void *spice_msg_in_parsed(spice_msg_in *in);
void *spice_msg_in_raw(spice_msg_in *in, int *len);
void spice_msg_in_hexdump(spice_msg_in *in);

spice_msg_out *spice_msg_out_new(SpiceChannel *channel, int type);
void spice_msg_out_get(spice_msg_out *out);
void spice_msg_out_put(spice_msg_out *out);
void spice_msg_out_send(spice_msg_out *out);
void spice_msg_out_hexdump(spice_msg_out *out, unsigned char *data, int len);

/* channel-base.c */
void spice_channel_handle_set_ack(SpiceChannel *channel, spice_msg_in *in);
void spice_channel_handle_ping(SpiceChannel *channel, spice_msg_in *in);
void spice_channel_handle_notify(SpiceChannel *channel, spice_msg_in *in);

#endif /* __SPICE_CLIENT_CHANNEL_H__ */
