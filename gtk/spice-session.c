#include "spice-client.h"
#include "spice-common.h"

#include "spice-session-priv.h"
#include "tcp.h"

/* spice/common */
#include "ring.h"

struct channel {
    SpiceChannel      *channel;
    RingItem          link;
};

struct spice_session {
    char              *host;
    char              *port;
    char              *tls_port;
    char              *password;
    char              *ca_file;
    struct addrinfo   ai;
    int               connection_id;
    SpiceChannel      *cmain;
    Ring              channels;
};

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_SESSION_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_SESSION, spice_session))

G_DEFINE_TYPE (SpiceSession, spice_session, G_TYPE_OBJECT);

/* Properties */
enum {
    PROP_0,
    PROP_HOST,
    PROP_PORT,
    PROP_TLS_PORT,
    PROP_PASSWORD,
    PROP_CA_FILE,
    PROP_IPV4,
    PROP_IPV6,
};

/* signals */
enum {
    SPICE_SESSION_CHANNEL_NEW,
    SPICE_SESSION_CHANNEL_DESTROY,
    SPICE_SESSION_LAST_SIGNAL,
};

static guint signals[SPICE_SESSION_LAST_SIGNAL];

static void spice_session_init(SpiceSession *session)
{
    spice_session *s;

    s = session->priv = SPICE_SESSION_GET_PRIVATE(session);
    memset(s, 0, sizeof(*s));

    s->host = strdup("localhost");
    s->ai.ai_socktype = SOCK_STREAM;
    s->ai.ai_family = PF_UNSPEC;

    ring_init(&s->channels);
}

static void
spice_session_dispose(GObject *gobject)
{
    SpiceSession *session = SPICE_SESSION(gobject);

    spice_session_disconnect(session);

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_session_parent_class)->dispose)
        G_OBJECT_CLASS(spice_session_parent_class)->dispose(gobject);
}

static void
spice_session_finalize(GObject *gobject)
{
    SpiceSession *session = SPICE_SESSION(gobject);
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    /* release stuff */
    free(s->host);
    free(s->port);
    free(s->tls_port);
    free(s->password);
    free(s->ca_file);

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_session_parent_class)->finalize)
        G_OBJECT_CLASS(spice_session_parent_class)->finalize(gobject);
}

static void spice_session_get_property(GObject    *gobject,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    SpiceSession *session = SPICE_SESSION(gobject);
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    switch (prop_id) {
    case PROP_HOST:
        g_value_set_string(value, s->host);
	break;
    case PROP_PORT:
        g_value_set_string(value, s->port);
	break;
    case PROP_TLS_PORT:
        g_value_set_string(value, s->tls_port);
	break;
    case PROP_PASSWORD:
        g_value_set_string(value, s->password);
	break;
    case PROP_CA_FILE:
        g_value_set_string(value, s->ca_file);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
	break;
    }
}

static void spice_session_set_property(GObject      *gobject,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    SpiceSession *session = SPICE_SESSION(gobject);
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    const char *str;

    switch (prop_id) {
    case PROP_HOST:
        free(s->host);
        str = g_value_get_string(value);
        s->host = str ? strdup(str) : NULL;
        break;
    case PROP_PORT:
        free(s->port);
        str = g_value_get_string(value);
        s->port = str ? strdup(str) : NULL;
        break;
    case PROP_TLS_PORT:
        free(s->tls_port);
        str = g_value_get_string(value);
        s->tls_port = str ? strdup(str) : NULL;
        break;
    case PROP_PASSWORD:
        free(s->password);
        str = g_value_get_string(value);
        s->password = str ? strdup(str) : NULL;
        break;
    case PROP_CA_FILE:
        free(s->ca_file);
        str = g_value_get_string(value);
        s->ca_file = str ? strdup(str) : NULL;
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_session_class_init(SpiceSessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose      = spice_session_dispose;
    gobject_class->finalize     = spice_session_finalize;
    gobject_class->get_property = spice_session_get_property;
    gobject_class->set_property = spice_session_set_property;

    g_object_class_install_property
        (gobject_class, PROP_HOST,
         g_param_spec_string("host",
                             "Host",
                             "remote host",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_PORT,
         g_param_spec_string("port",
                             "Port",
                             "remote port (plaintext)",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_TLS_PORT,
         g_param_spec_string("tls-port",
                             "TLS port",
                             "remote port (encrypted)",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_PASSWORD,
         g_param_spec_string("password",
                             "Password",
                             "",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_CA_FILE,
         g_param_spec_string("ca-file",
                             "CA file",
                             "File holding the CA certificates",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    signals[SPICE_SESSION_CHANNEL_NEW] =
        g_signal_new("spice-session-channel-new",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSessionClass, spice_session_channel_new),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_CHANNEL);

    signals[SPICE_SESSION_CHANNEL_DESTROY] =
        g_signal_new("spice-session-channel-destroy",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSessionClass, spice_session_channel_destroy),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_CHANNEL);

    g_type_class_add_private(klass, sizeof(spice_session));
}

/* ------------------------------------------------------------------ */
/* public functions                                                   */

SpiceSession *spice_session_new()
{
    return SPICE_SESSION(g_object_new(SPICE_TYPE_SESSION,
                                      NULL));
}

gboolean spice_session_connect(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    spice_session_disconnect(session);
    s->cmain = spice_channel_new(session, SPICE_CHANNEL_MAIN, 0);
    return spice_channel_connect(s->cmain);
}

void spice_session_disconnect(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item;
    RingItem *ring, *next;

    if (s->cmain == NULL) {
        return;
    }

    for (ring = ring_get_head(&s->channels); ring != NULL; ring = next) {
        next = ring_next(&s->channels, ring);
        item = SPICE_CONTAINEROF(ring, struct channel, link);
        spice_channel_destroy(item->channel);
    }

    s->connection_id = 0;
    s->cmain = NULL;
}

int spice_session_get_channels(SpiceSession *session, SpiceChannel **channels, int max)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item;
    RingItem *ring;
    int i;

    for (i = 0, ring = ring_get_head(&s->channels);
         i < max && ring != NULL;
         i++, ring = ring_next(&s->channels, ring)) {
        item = SPICE_CONTAINEROF(ring, struct channel, link);
        channels[i] = item->channel;
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* private functions                                                  */

int spice_session_channel_connect(SpiceSession *session, bool use_tls)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    char *port = use_tls ? s->tls_port : s->port;

    if (port == NULL) {
        return -1;
    }
    return tcp_connect(&s->ai, NULL, NULL, s->host, port);
}

void spice_session_channel_new(SpiceSession *session, SpiceChannel *channel)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item;

    item = spice_new0(struct channel, 1);
    item->channel = channel;
    ring_add(&s->channels, &item->link);
    g_signal_emit(session, signals[SPICE_SESSION_CHANNEL_NEW], 0, channel);
}

void spice_session_channel_destroy(SpiceSession *session, SpiceChannel *channel)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item = NULL;
    RingItem *ring;

    for (ring = ring_get_head(&s->channels); ring != NULL;
         ring = ring_next(&s->channels, ring)) {
        item = SPICE_CONTAINEROF(ring, struct channel, link);
        if (item->channel == channel) {
            ring_remove(&item->link);
            free(item);
            g_signal_emit(session, signals[SPICE_SESSION_CHANNEL_DESTROY], 0, channel);
            return;
        }
    }
}

void spice_session_set_connection_id(SpiceSession *session, int id)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    s->connection_id = id;
}

int spice_session_get_connection_id(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    return s->connection_id;
}
