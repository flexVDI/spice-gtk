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
#include <gio/gio.h>
#include <glib.h>
#include "spice-client.h"
#include "spice-common.h"
#include "spice-channel-priv.h"

#include "spice-session-priv.h"

/* spice/common */
#include "ring.h"

#include "gio-coroutine.h"

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
    int               connection_id;
    int               protocol;
    SpiceChannel      *cmain;
    Ring              channels;
    guint32           mm_time;
    gboolean          client_provided_sockets;
    guint64           mm_time_at_clock;
    SpiceSession      *migration;
    guint             migration_left;
    gboolean          disconnecting;
};

/**
 * SECTION:spice-session
 * @short_description: handles connection details, and active channels
 * @title: Spice Session
 * @section_id:
 * @see_also: #SpiceChannel, and the GTK widget #SpiceDisplay
 * @stability: Stable
 * @include: spice-session.h
 *
 * The #SpiceSession class handles all the #SpiceChannel connections.
 * It's also the class that contains connections informations, such as
 * #SpiceSession:host and #SpiceSession:port.
 *
 * You can simply set the property #SpiceSession:uri to something like
 * "spice://127.0.0.1?port=5930" to configure your connection details.
 *
 * You may want to connect to #SpiceSession::channel-new signal, to be
 * informed of the availability of channels and to interact with
 * them.
 *
 * For example, when the #SpiceInputsChannel is available and get the
 * event #SPICE_CHANNEL_OPENED, you can send key events with
 * spice_inputs_key_press(). When the #SpiceMainChannel is available,
 * you can start sharing the clipboard...  .
 *
 *
 * Once #SpiceSession properties set, you can call
 * spice_session_connect() to start connecting and communicating with
 * a Spice server.
 */

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
    PROP_PROTOCOL,
    PROP_URI,
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

    ring_init(&s->channels);
}

static void
spice_session_dispose(GObject *gobject)
{
    SpiceSession *session = SPICE_SESSION(gobject);

    SPICE_DEBUG("session dispose");

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

static int spice_uri_create(SpiceSession *session, char *dest, int len)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    int pos = 0;

    if (s->host == NULL || (s->port == NULL && s->tls_port == NULL)) {
        return 0;
    }

    pos += snprintf(dest + pos, len-pos, "spice://%s?", s->host);
    if (s->port && strlen(s->port))
        pos += snprintf(dest + pos, len - pos, "port=%s;", s->port);
    if (s->tls_port && strlen(s->tls_port))
        pos += snprintf(dest + pos, len - pos, "tls-port=%s;", s->tls_port);
    return pos;
}

static int spice_uri_parse(SpiceSession *session, const char *original_uri)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    char host[128], key[32], value[128];
    char *port = NULL, *tls_port = NULL, *uri = NULL, *password = NULL;
    int len, pos = 0;

    g_return_val_if_fail(original_uri != NULL, -1);

    uri = g_uri_unescape_string(original_uri, NULL);
    g_return_val_if_fail(uri != NULL, -1);

    if (sscanf(uri, "spice://%127[-.0-9a-zA-Z]%n", host, &len) != 1)
        goto fail;
    pos += len;
    for (;;) {
        if (uri[pos] == '?' || uri[pos] == ';' || uri[pos] == '&') {
            pos++;
            continue;
        }
        if (uri[pos] == 0) {
            break;
        }
        if (sscanf(uri + pos, "%31[-a-zA-Z0-9]=%127[^;&]%n", key, value, &len) != 2)
            goto fail;
        pos += len;
        if (g_str_equal(key, "port")) {
            port = g_strdup(value);
        } else if (g_str_equal(key, "tls-port")) {
            tls_port = g_strdup(value);
        } else if (g_str_equal(key, "password")) {
            password = g_strdup(value);
            g_warning("password may be visible in process listings");
        } else {
            g_warning("unknown key in spice URI parsing: %s", key);
            goto fail;
        }
    }

    if (port == NULL && tls_port == NULL) {
        g_warning("missing port or tls-port in spice URI");
        goto fail;
    }

    /* parsed ok -> apply */
    g_free(uri);
    g_free(s->host);
    g_free(s->port);
    g_free(s->tls_port);
    g_free(s->password);
    s->host = g_strdup(host);
    s->port = port;
    s->tls_port = tls_port;
    s->password = password;
    return 0;

fail:
    g_free(uri);
    g_free(port);
    g_free(tls_port);
    g_free(password);
    return -1;
}

static void spice_session_get_property(GObject    *gobject,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    SpiceSession *session = SPICE_SESSION(gobject);
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    char buf[256];
    int len;

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
    case PROP_PROTOCOL:
        g_value_set_int(value, s->protocol);
	break;
    case PROP_URI:
        len = spice_uri_create(session, buf, sizeof(buf));
        g_value_set_string(value, len ? buf : NULL);
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
    case PROP_PROTOCOL:
        s->protocol = g_value_get_int(value);
        break;
    case PROP_URI:
        str = g_value_get_string(value);
        if (str != NULL)
            spice_uri_parse(session, str);
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
                             "Remote host",
                             "localhost",
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_PORT,
         g_param_spec_string("port",
                             "Port",
                             "Remote port (plaintext)",
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
                             "Remote port (encrypted)",
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

    g_object_class_install_property
        (gobject_class, PROP_PROTOCOL,
         g_param_spec_int("protocol",
                          "Protocol",
                          "Spice protocol major version",
                          1, 2, 2,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_NAME |
                          G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_URI,
         g_param_spec_string("uri",
                             "URI",
                             "Spice connection URI",
                             NULL,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    /**
     * SpiceSession::channel-new:
     * @session: the session that emitted the signal
     * @channel: the new #SpiceChannel
     *
     * The #SpiceSession::channel-new signal is emitted each time a #SpiceChannel is created.
     **/
    signals[SPICE_SESSION_CHANNEL_NEW] =
        g_signal_new("channel-new",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSessionClass, channel_new),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_CHANNEL);

    /**
     * SpiceSession::channel-destroy:
     * @session: the session that emitted the signal
     * @channel: the destroyed #SpiceChannel
     *
     * The #SpiceSession::channel-destroy signal is emitted each time a #SpiceChannel is destroyed.
     **/
    signals[SPICE_SESSION_CHANNEL_DESTROY] =
        g_signal_new("channel-destroy",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSessionClass, channel_destroy),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_CHANNEL);

    g_type_class_add_private(klass, sizeof(spice_session));
}

/* ------------------------------------------------------------------ */
/* public functions                                                   */

/**
 * spice_session_new:
 *
 * Creates a new Spice session.
 *
 * Returns: a new #SpiceSession
 **/
SpiceSession *spice_session_new(void)
{
    return SPICE_SESSION(g_object_new(SPICE_TYPE_SESSION, NULL));
}

G_GNUC_INTERNAL
SpiceSession *spice_session_new_from_session(SpiceSession *session)
{
    SpiceSession *copy = SPICE_SESSION(g_object_new(SPICE_TYPE_SESSION,
                                                    "host", NULL,
                                                    "ca-file", NULL,
                                                    NULL));
    spice_session *c = copy->priv, *s = session->priv;

    g_object_get(session,
                 "host", &c->host,
                 "tls-port", &c->tls_port,
                 "password", &c->password,
                 "ca-file", &c->ca_file,
                 NULL);

    c->client_provided_sockets = s->client_provided_sockets;
    c->protocol = s->protocol;
    c->connection_id = s->connection_id;

    return copy;
}

/**
 * spice_session_connect:
 * @session:
 *
 * Open the session using the #SpiceSession:host and
 * #SpiceSession:port.
 *
 * Returns: %FALSE if the connection failed.
 **/
gboolean spice_session_connect(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    g_return_val_if_fail(s != NULL, FALSE);

    spice_session_disconnect(session);

    s->client_provided_sockets = FALSE;
    s->cmain = spice_channel_new(session, SPICE_CHANNEL_MAIN, 0);
    return spice_channel_connect(s->cmain);
}

/**
 * spice_session_open_fd:
 * @session:
 * @fd: a file descriptor
 *
 * Open the session using the provided @fd socket file
 * descriptor. This is useful if you create the fd yourself, for
 * example to setup a SSH tunnel.
 *
 * Returns:
 **/
gboolean spice_session_open_fd(SpiceSession *session, int fd)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    g_return_val_if_fail(s != NULL, FALSE);
    g_return_val_if_fail(fd >= 0, FALSE);

    spice_session_disconnect(session);

    s->client_provided_sockets = TRUE;
    s->cmain = spice_channel_new(session, SPICE_CHANNEL_MAIN, 0);
    return spice_channel_open_fd(s->cmain, fd);
}

G_GNUC_INTERNAL
gboolean spice_session_get_client_provided_socket(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    g_return_val_if_fail(s != NULL, FALSE);
    return s->client_provided_sockets;
}

G_GNUC_INTERNAL
void spice_session_migrate_disconnect(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item;
    RingItem *ring, *next;

    g_return_if_fail(s != NULL);
    g_return_if_fail(s->cmain != NULL);

    /* disconnect/destroy all but main channel */

    for (ring = ring_get_head(&s->channels); ring != NULL; ring = next) {
        next = ring_next(&s->channels, ring);
        item = SPICE_CONTAINEROF(ring, struct channel, link);
        if (item->channel != s->cmain)
            spice_channel_destroy(item->channel); /* /!\ item and channel are destroy() after this call */
    }

    g_return_if_fail(!ring_is_empty(&s->channels) &&
                     ring_get_head(&s->channels) == ring_get_tail(&s->channels));
}

G_GNUC_INTERNAL
void spice_session_set_migration(SpiceSession *session, SpiceSession *migration)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    spice_session *m = SPICE_SESSION_GET_PRIVATE(migration);
    gchar *tmp;

    g_return_if_fail(s != NULL);

    g_warn_if_fail(s->migration == NULL);
    s->migration = g_object_ref(migration);

    tmp = s->host;
    s->host = m->host;
    m->host = tmp;

    tmp = s->port;
    s->port = m->port;
    m->port = tmp;

    tmp = s->tls_port;
    s->tls_port = m->tls_port;
    m->tls_port = tmp;

    SPICE_DEBUG("migration channels left:%d (in migration:%d)",
                ring_get_length(&s->channels), ring_get_length(&m->channels));
    s->migration_left = ring_get_length(&s->channels);
}

G_GNUC_INTERNAL
void spice_session_channel_migrate(SpiceSession *session, SpiceChannel *channel)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    RingItem *ring, *next;
    struct channel *c;
    gint id, type;

    g_return_if_fail(s != NULL);
    g_return_if_fail(s->migration != NULL);
    g_return_if_fail(SPICE_IS_CHANNEL(channel));

    id = spice_channel_get_channel_id(channel);
    type = spice_channel_get_channel_type(channel);
    SPICE_DEBUG("migrating channel id:%d type:%d", id, type);

    for (ring = ring_get_head(&s->migration->priv->channels);
         ring != NULL; ring = next) {
        next = ring_next(&s->migration->priv->channels, ring);
        c = SPICE_CONTAINEROF(ring, struct channel, link);
        if (c == NULL || c->channel == NULL) {
            g_warn_if_reached();
            continue;
        }

        if (id == spice_channel_get_channel_id(c->channel) &&
            type == spice_channel_get_channel_type(c->channel))
            break;
    }
    g_return_if_fail(ring != NULL);

    spice_channel_swap(channel, c->channel);
    s->migration_left--;
    if (s->migration_left == 0) {
        SPICE_DEBUG("all channel migrated");
        spice_session_disconnect(s->migration);
        g_object_unref(s->migration);
        s->migration = NULL;
    }
}

/**
 * spice_session_disconnect:
 * @session:
 *
 * Disconnect the @session, and destroy all channels.
 **/
void spice_session_disconnect(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item;
    RingItem *ring, *next;

    g_return_if_fail(s != NULL);

    SPICE_DEBUG("session: disconnecting %d", s->disconnecting);
    if (s->disconnecting)
        return;

    s->disconnecting = TRUE;
    s->cmain = NULL;

    for (ring = ring_get_head(&s->channels); ring != NULL; ring = next) {
        next = ring_next(&s->channels, ring);
        item = SPICE_CONTAINEROF(ring, struct channel, link);
        spice_channel_destroy(item->channel); /* /!\ item and channel are destroy() after this call */
    }

    s->connection_id = 0;
    s->disconnecting = FALSE;
}

/**
 * spice_session_get_channels:
 * @session:
 *
 * Get the list of current channels associated with this @session.
 *
 * Returns: a #GList of unowned SpiceChannels.
 **/
GList *spice_session_get_channels(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item;
    GList *list = NULL;
    RingItem *ring;

    g_return_val_if_fail(s != NULL, NULL);

    for (ring = ring_get_head(&s->channels);
         ring != NULL;
         ring = ring_next(&s->channels, ring)) {
        item = SPICE_CONTAINEROF(ring, struct channel, link);
        list = g_list_append(list, item->channel);
    }
    return list;
}

/* ------------------------------------------------------------------ */
/* private functions                                                  */

static GSocket *channel_connect_socket(GSocketAddress *sockaddr,
                                       GError **error)
{
    GSocket *sock = g_socket_new(g_socket_address_get_family(sockaddr),
                                 G_SOCKET_TYPE_STREAM,
                                 G_SOCKET_PROTOCOL_DEFAULT,
                                 error);

    if (!sock)
        return NULL;

    g_socket_set_blocking(sock, FALSE);
    if (!g_socket_connect(sock, sockaddr, NULL, error)) {
        if (*error && (*error)->code == G_IO_ERROR_PENDING) {
            g_clear_error(error);
            SPICE_DEBUG("Socket pending");
            g_io_wait(sock, G_IO_OUT | G_IO_ERR | G_IO_HUP);

            if (!g_socket_check_connect_result(sock, error)) {
                SPICE_DEBUG("Failed to connect %s", (*error)->message);
                g_object_unref(sock);
                return NULL;
            }
        } else {
            SPICE_DEBUG("Socket error: %s", *error ? (*error)->message : "unknown");
            g_object_unref(sock);
            return NULL;
        }
    }

    SPICE_DEBUG("Finally connected");

    return sock;
}

G_GNUC_INTERNAL
GSocket* spice_session_channel_open_host(SpiceSession *session, gboolean use_tls)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    GSocketConnectable *addr;
    GSocketAddressEnumerator *enumerator;
    GSocketAddress *sockaddr;
    GError *conn_error = NULL;
    GSocket *sock = NULL;
    int port;

    if ((use_tls && !s->tls_port) || (!use_tls && !s->port))
        return NULL;

    port = atoi(use_tls ? s->tls_port : s->port);

    SPICE_DEBUG("Resolving host %s %d", s->host, port);

    addr = g_network_address_new(s->host, port);

    enumerator = g_socket_connectable_enumerate (addr);
    g_object_unref (addr);

    /* Try each sockaddr until we succeed. Record the first
     * connection error, but not any further ones (since they'll probably
     * be basically the same as the first).
     */
    while (!sock &&
           (sockaddr = g_socket_address_enumerator_next(enumerator, NULL, &conn_error))) {
        SPICE_DEBUG("Trying one socket");
        g_clear_error(&conn_error);
        sock = channel_connect_socket(sockaddr, &conn_error);
        g_object_unref(sockaddr);
    }
    g_object_unref(enumerator);
    g_clear_error(&conn_error);
    return sock;
}


G_GNUC_INTERNAL
void spice_session_channel_new(SpiceSession *session, SpiceChannel *channel)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item;

    g_return_if_fail(s != NULL);
    g_return_if_fail(channel != NULL);

    item = spice_new0(struct channel, 1);
    item->channel = channel;
    ring_add(&s->channels, &item->link);
    g_signal_emit(session, signals[SPICE_SESSION_CHANNEL_NEW], 0, channel);
}

G_GNUC_INTERNAL
void spice_session_channel_destroy(SpiceSession *session, SpiceChannel *channel)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);
    struct channel *item = NULL;
    RingItem *ring, *next;

    g_return_if_fail(s != NULL);
    g_return_if_fail(channel != NULL);

    for (ring = ring_get_head(&s->channels); ring != NULL;
         ring = next) {
        next = ring_next(&s->channels, ring);
        item = SPICE_CONTAINEROF(ring, struct channel, link);
        if (item->channel == channel) {
            ring_remove(&item->link);
            free(item);
            g_signal_emit(session, signals[SPICE_SESSION_CHANNEL_DESTROY], 0, channel);
            return;
        }
    }

    g_warn_if_reached();
}

G_GNUC_INTERNAL
void spice_session_set_connection_id(SpiceSession *session, int id)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    g_return_if_fail(s != NULL);

    s->connection_id = id;
}

G_GNUC_INTERNAL
int spice_session_get_connection_id(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    g_return_val_if_fail(s != NULL, -1);

    return s->connection_id;
}

#if !GLIB_CHECK_VERSION(2,28,0)
static guint64 g_get_monotonic_clock(void)
{
    GTimeVal tv;

    /* TODO: support real monotonic clock? */
    g_get_current_time (&tv);

    return (((gint64) tv.tv_sec) * 1000000) + tv.tv_usec;
}
#endif

G_GNUC_INTERNAL
guint32 spice_session_get_mm_time(SpiceSession *session)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    g_return_val_if_fail(s != NULL, 0);

    return s->mm_time + (g_get_monotonic_clock() - s->mm_time_at_clock) / 1000;
}

G_GNUC_INTERNAL
void spice_session_set_mm_time(SpiceSession *session, guint32 time)
{
    spice_session *s = SPICE_SESSION_GET_PRIVATE(session);

    g_return_if_fail(s != NULL);
    SPICE_DEBUG("set mm time: %u", time);

    s->mm_time = time;
    s->mm_time_at_clock = g_get_monotonic_clock();
}

G_GNUC_INTERNAL
void spice_session_set_port(SpiceSession *session, int port, gboolean tls)
{
    const char *prop = tls ? "tls-port" : "port";
    char *tmp;

    g_return_if_fail(session != NULL);

    /* old spicec client doesn't accept port == 0, see Migrate::start */
    tmp = port > 0 ? g_strdup_printf("%d", port) : NULL;
    g_object_set(session, prop, tmp, NULL);
    g_free(tmp);
}
