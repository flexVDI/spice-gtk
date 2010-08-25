#ifndef __SPICE_CLIENT_SESSION_H__
#define __SPICE_CLIENT_SESSION_H__

G_BEGIN_DECLS

#define SPICE_TYPE_SESSION            (spice_session_get_type ())
#define SPICE_SESSION(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPICE_TYPE_SESSION, SpiceSession))
#define SPICE_SESSION_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SPICE_TYPE_SESSION, SpiceSessionClass))
#define SPICE_IS_SESSION(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPICE_TYPE_SESSION))
#define SPICE_IS_SESSION_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SPICE_TYPE_SESSION))
#define SPICE_SESSION_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), SPICE_TYPE_SESSION, SpiceSessionClass))

struct _SpiceSession
{
    GObject parent;
    spice_session *priv;
    /* Do not add fields to this struct */
};

struct _SpiceSessionClass
{
    GObjectClass parent_class;

    /* Signals */
    void (*spice_session_channel_new)(SpiceSession *session, SpiceChannel *channel);
    void (*spice_session_channel_destroy)(SpiceSession *session, SpiceChannel *channel);

#if 0
    /*
     * If adding fields to this struct, remove corresponding
     * amount of padding to avoid changing overall struct size
     */
    gpointer _spice_reserved[42];
#endif
};

GType spice_session_get_type(void) G_GNUC_CONST;

SpiceSession *spice_session_new(void);
gboolean spice_session_connect(SpiceSession *session);
void spice_session_disconnect(SpiceSession *session);
int spice_session_get_channels(SpiceSession *session, SpiceChannel **channels, int max);

G_END_DECLS

#endif /* __SPICE_CLIENT_SESSION_H__ */
