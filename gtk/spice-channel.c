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
#include "spice-client.h"
#include "spice-common.h"

#include "spice-channel-priv.h"
#include "spice-session-priv.h"
#include "spice-marshal.h"

#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <sys/socket.h>

#include "gio-coroutine.h"

static void spice_channel_send_msg(SpiceChannel *channel, spice_msg_out *out, gboolean buffered);
static void spice_channel_send_link(SpiceChannel *channel);
static void channel_disconnect(SpiceChannel *channel);

/**
 * SECTION:spice-channel
 * @short_description: the base channel class
 * @title: Spice Channel
 * @section_id:
 * @see_also: #SpiceSession, #SpiceMainChannel and other channels
 * @stability: Stable
 * @include: spice-channel.h
 *
 * #SpiceChannel is the base class for the different kind of Spice
 * channel connections, such as #SpiceMainChannel, or
 * #SpiceInputsChannel.
 */

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_CHANNEL, spice_channel))

G_DEFINE_TYPE(SpiceChannel, spice_channel, G_TYPE_OBJECT);

/* Properties */
enum {
    PROP_0,
    PROP_SESSION,
    PROP_CHANNEL_TYPE,
    PROP_CHANNEL_ID,
};

/* Signals */
enum {
    SPICE_CHANNEL_EVENT,
    SPICE_CHANNEL_OPEN_FD,

    SPICE_CHANNEL_LAST_SIGNAL,
};

static guint signals[SPICE_CHANNEL_LAST_SIGNAL];

static const char *channel_desc[] = {
    [ SPICE_CHANNEL_MAIN ]     = "main",
    [ SPICE_CHANNEL_DISPLAY ]  = "display",
    [ SPICE_CHANNEL_CURSOR ]   = "cursor",
    [ SPICE_CHANNEL_INPUTS ]   = "inputs",
    [ SPICE_CHANNEL_RECORD ]   = "record",
    [ SPICE_CHANNEL_PLAYBACK ] = "playback",
    [ SPICE_CHANNEL_TUNNEL ]   = "tunnel",
};

static void spice_channel_iterate_write(SpiceChannel *channel);
static void spice_channel_iterate_read(SpiceChannel *channel);

static void spice_channel_init(SpiceChannel *channel)
{
    spice_channel *c;

    c = channel->priv = SPICE_CHANNEL_GET_PRIVATE(channel);

    c->serial = 1;
    c->fd = -1;
    strcpy(c->name, "?");
    c->caps = g_array_new(FALSE, TRUE, sizeof(guint32));
    c->common_caps = g_array_new(FALSE, TRUE, sizeof(guint32));
    c->remote_caps = g_array_new(FALSE, TRUE, sizeof(guint32));
    c->remote_common_caps = g_array_new(FALSE, TRUE, sizeof(guint32));
}

static void spice_channel_constructed(GObject *gobject)
{
    SpiceChannel *channel = SPICE_CHANNEL(gobject);
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    const char *desc = NULL;

    if (c->channel_type < SPICE_N_ELEMENTS(channel_desc))
        desc = channel_desc[c->channel_type];

    snprintf(c->name, sizeof(c->name), "%s-%d:%d",
             desc ? desc : "unknown", c->channel_type, c->channel_id);
    SPICE_DEBUG("%s: %s", c->name, __FUNCTION__);

    c->connection_id = spice_session_get_connection_id(c->session);
    spice_session_channel_new(c->session, channel);

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_channel_parent_class)->constructed)
        G_OBJECT_CLASS(spice_channel_parent_class)->constructed(gobject);
}

static void spice_channel_dispose(GObject *gobject)
{
    SpiceChannel *channel = SPICE_CHANNEL(gobject);
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    if (c->session)
        spice_session_channel_destroy(c->session, channel);

    spice_channel_disconnect(channel, SPICE_CHANNEL_CLOSED);

    if (c->session) {
         g_object_unref(c->session);
         c->session = NULL;
    }

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_channel_parent_class)->dispose)
        G_OBJECT_CLASS(spice_channel_parent_class)->dispose(gobject);
}

static void spice_channel_finalize(GObject *gobject)
{
    SpiceChannel *channel = SPICE_CHANNEL(gobject);
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    SPICE_DEBUG("%s: %s", c->name, __FUNCTION__);

    if (c->caps)
        g_array_free(c->caps, TRUE);

    if (c->common_caps)
        g_array_free(c->common_caps, TRUE);

    if (c->remote_caps)
        g_array_free(c->remote_caps, TRUE);

    if (c->remote_common_caps)
        g_array_free(c->remote_common_caps, TRUE);

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_channel_parent_class)->finalize(gobject);
}

static void spice_channel_get_property(GObject    *gobject,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    SpiceChannel *channel = SPICE_CHANNEL(gobject);
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, c->session);
        break;
    case PROP_CHANNEL_TYPE:
        g_value_set_int(value, c->channel_type);
        break;
    case PROP_CHANNEL_ID:
        g_value_set_int(value, c->channel_id);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_channel_set_property(GObject      *gobject,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    SpiceChannel *channel = SPICE_CHANNEL(gobject);
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    switch (prop_id) {
    case PROP_SESSION:
        c->session = g_value_dup_object(value);
        break;
    case PROP_CHANNEL_TYPE:
        c->channel_type = g_value_get_int(value);
        break;
    case PROP_CHANNEL_ID:
        c->channel_id = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_channel_class_init(SpiceChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    klass->iterate_write = spice_channel_iterate_write;
    klass->iterate_read  = spice_channel_iterate_read;

    gobject_class->constructed  = spice_channel_constructed;
    gobject_class->dispose      = spice_channel_dispose;
    gobject_class->finalize     = spice_channel_finalize;
    gobject_class->get_property = spice_channel_get_property;
    gobject_class->set_property = spice_channel_set_property;

    g_object_class_install_property
        (gobject_class, PROP_SESSION,
         g_param_spec_object("spice-session",
                             "Spice session",
                             "",
                             SPICE_TYPE_SESSION,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_NAME |
                             G_PARAM_STATIC_NICK |
                             G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_CHANNEL_TYPE,
         g_param_spec_int("channel-type",
                          "Channel type",
                          "",
                          -1, INT_MAX, -1,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_NAME |
                          G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_CHANNEL_ID,
         g_param_spec_int("channel-id",
                          "Channel ID",
                          "",
                          -1, INT_MAX, -1,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_NAME |
                          G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

    /**
     * SpiceChannel::channel-event:
     * @channel: the channel that emitted the signal
     * @event: a #SpiceChannelEvent
     *
     * The #SpiceChannel::channel-event signal is emitted when the
     * state of the connection change.
     **/
    signals[SPICE_CHANNEL_EVENT] =
        g_signal_new("channel-event",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceChannelClass, channel_event),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    /**
     * SpiceChannel::open-fd:
     * @channel: the channel that emitted the signal
     * @with_tls: wether TLS connection is requested
     *
     * The #SpiceChannel::open-fd signal is emitted when a new
     * connection is requested. This signal is emitted when the
     * connection is made with spice_session_open_fd().
     **/
    signals[SPICE_CHANNEL_OPEN_FD] =
        g_signal_new("open-fd",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceChannelClass, open_fd),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    g_type_class_add_private(klass, sizeof(spice_channel));

    SSL_library_init();
    SSL_load_error_strings();
}

/* ---------------------------------------------------------------- */
/* private msg api                                                  */

G_GNUC_INTERNAL
spice_msg_in *spice_msg_in_new(SpiceChannel *channel)
{
    spice_msg_in *in;

    g_return_val_if_fail(channel != NULL, NULL);

    in = spice_new0(spice_msg_in, 1);
    in->refcount = 1;
    in->channel  = channel;
    return in;
}

G_GNUC_INTERNAL
spice_msg_in *spice_msg_in_sub_new(SpiceChannel *channel, spice_msg_in *parent,
                                   SpiceSubMessage *sub)
{
    spice_msg_in *in;

    g_return_val_if_fail(channel != NULL, NULL);

    in = spice_msg_in_new(channel);
    in->header.type = sub->type;
    in->header.size = sub->size;
    in->data = (uint8_t*)(sub+1);
    in->dpos = sub->size;
    in->parent = parent;
    spice_msg_in_ref(parent);
    return in;
}

G_GNUC_INTERNAL
void spice_msg_in_ref(spice_msg_in *in)
{
    g_return_if_fail(in != NULL);

    in->refcount++;
}

G_GNUC_INTERNAL
void spice_msg_in_unref(spice_msg_in *in)
{
    g_return_if_fail(in != NULL);

    in->refcount--;
    if (in->refcount > 0)
        return;
    if (in->parsed)
        in->pfree(in->parsed);
    if (in->parent) {
        spice_msg_in_unref(in->parent);
    } else {
        free(in->data);
    }
    free(in);
}

G_GNUC_INTERNAL
int spice_msg_in_type(spice_msg_in *in)
{
    g_return_val_if_fail(in != NULL, -1);

    return in->header.type;
}

G_GNUC_INTERNAL
void *spice_msg_in_parsed(spice_msg_in *in)
{
    g_return_val_if_fail(in != NULL, NULL);

    return in->parsed;
}

G_GNUC_INTERNAL
void *spice_msg_in_raw(spice_msg_in *in, int *len)
{
    g_return_val_if_fail(in != NULL, NULL);
    g_return_val_if_fail(len != NULL, NULL);

    *len = in->dpos;
    return in->data;
}

static void hexdump(char *prefix, unsigned char *data, int len)
{
    int i;

    for (i = 0; i < len; i++) {
        if (i % 16 == 0)
            fprintf(stderr, "%s:", prefix);
        if (i % 4 == 0)
            fprintf(stderr, " ");
        fprintf(stderr, " %02x", data[i]);
        if (i % 16 == 15)
            fprintf(stderr, "\n");
    }
    if (i % 16 != 0)
        fprintf(stderr, "\n");
}

G_GNUC_INTERNAL
void spice_msg_in_hexdump(spice_msg_in *in)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(in->channel);

    fprintf(stderr, "--\n<< hdr: %s serial %ld type %d size %d sub-list %d\n",
            c->name, in->header.serial, in->header.type,
            in->header.size, in->header.sub_list);
    hexdump("<< msg", in->data, in->dpos);
}

G_GNUC_INTERNAL
void spice_msg_out_hexdump(spice_msg_out *out, unsigned char *data, int len)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(out->channel);

    fprintf(stderr, "--\n>> hdr: %s serial %ld type %d size %d sub-list %d\n",
            c->name, out->header->serial, out->header->type,
            out->header->size, out->header->sub_list);
    hexdump(">> msg", data, len);
}

G_GNUC_INTERNAL
spice_msg_out *spice_msg_out_new(SpiceChannel *channel, int type)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    spice_msg_out *out;

    g_return_val_if_fail(c != NULL, NULL);

    out = spice_new0(spice_msg_out, 1);
    out->refcount = 1;
    out->channel  = channel;

    out->marshallers = c->marshallers;
    out->marshaller = spice_marshaller_new();
    out->header = (SpiceDataHeader *)
        spice_marshaller_reserve_space(out->marshaller, sizeof(SpiceDataHeader));
    spice_marshaller_set_base(out->marshaller, sizeof(SpiceDataHeader));
    out->header->serial = c->serial++;
    out->header->type = type;
    out->header->sub_list = 0;
    return out;
}

G_GNUC_INTERNAL
void spice_msg_out_ref(spice_msg_out *out)
{
    g_return_if_fail(out != NULL);

    out->refcount++;
}

G_GNUC_INTERNAL
void spice_msg_out_unref(spice_msg_out *out)
{
    g_return_if_fail(out != NULL);

    out->refcount--;
    if (out->refcount > 0)
        return;
    spice_marshaller_destroy(out->marshaller);
    free(out);
}

/* system context */
G_GNUC_INTERNAL
void spice_msg_out_send(spice_msg_out *out)
{
    g_return_if_fail(out != NULL);

    out->header->size =
        spice_marshaller_get_total_size(out->marshaller) - sizeof(SpiceDataHeader);
    spice_channel_send_msg(out->channel, out, TRUE);

    /* TODO: we currently flush/wakeup immediately all buffered messages */
    spice_channel_wakeup(out->channel);
}

/* coroutine context */
G_GNUC_INTERNAL
void spice_msg_out_send_internal(spice_msg_out *out)
{
    g_return_if_fail(out != NULL);

    out->header->size =
        spice_marshaller_get_total_size(out->marshaller) - sizeof(SpiceDataHeader);
    spice_channel_send_msg(out->channel, out, FALSE);
}

/* ---------------------------------------------------------------- */

struct SPICE_CHANNEL_EVENT {
    SpiceChannelEvent event;
};

/* main context */
static void do_emit_main_context(GObject *object, int signum, gpointer params)
{
    switch (signum) {
    case SPICE_CHANNEL_EVENT: {
        struct SPICE_CHANNEL_EVENT *p = params;
        g_signal_emit(object, signals[signum], 0, p->event);
        break;
    }
    case SPICE_CHANNEL_OPEN_FD:
        g_warning("this signal is only sent directly from main context");
        break;
    default:
        g_warn_if_reached();
    }
}

/* coroutine context */
#define emit_main_context(object, event, args...)                       \
    G_STMT_START {                                                      \
        g_signal_emit_main_context(G_OBJECT(object), do_emit_main_context, \
                                   event, &((struct event) { args }));  \
    } G_STMT_END


/*
 * Write all 'data' of length 'datalen' bytes out to
 * the wire
 */
/* coroutine context */
static void spice_channel_flush_wire(SpiceChannel *channel,
                                     const void *data,
                                     size_t datalen)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    const char *ptr = data;
    size_t offset = 0;

    while (offset < datalen) {
        int ret;
        GIOCondition cond;

        if (c->has_error) return;

        cond = 0;
        if (c->tls) {
            ret = SSL_write(c->ssl, ptr+offset, datalen-offset);
            if (ret < 0) {
                ret = SSL_get_error(c->ssl, ret);
                if (ret == SSL_ERROR_WANT_READ)
                    cond |= G_IO_IN;
                if (ret == SSL_ERROR_WANT_WRITE)
                    cond |= G_IO_OUT;
                ret = -1;
            }
        } else {
            GError *error = NULL;
            ret = g_socket_send(c->sock, ptr+offset, datalen-offset,
                                NULL, &error);
            if (ret < 0) {
                if (error) {
                    if (error->code == G_IO_ERROR_WOULD_BLOCK)
                        cond |= G_IO_OUT;
                    g_error_free(error);
                }
                ret = -1;
            }
        }
        if (ret == -1) {
            if (cond != 0) {
                g_io_wait(c->sock, cond);
            } else {
                SPICE_DEBUG("Closing the channel: spice_channel_flush %d", errno);
                c->has_error = TRUE;
                return;
            }
        }
        if (ret == 0) {
            SPICE_DEBUG("Closing the connection: spice_channel_flush");
            c->has_error = TRUE;
            return;
        }
        offset += ret;
    }
}

/* coroutine context */
static void spice_channel_write(SpiceChannel *channel, const void *data, size_t len)
{
    spice_channel_flush_wire(channel, data, len);
}

/*
 * Read at least 1 more byte of data straight off the wire
 * into the requested buffer.
 */
/* coroutine context */
static int spice_channel_read_wire(SpiceChannel *channel, void *data, size_t len)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    int ret;
    GIOCondition cond;

reread:

    if (c->has_error) return 0; /* has_error is set by disconnect(), return no error */

    cond = 0;
    if (c->tls) {
        ret = SSL_read(c->ssl, data, len);
        if (ret < 0) {
            ret = SSL_get_error(c->ssl, ret);
            if (ret == SSL_ERROR_WANT_READ)
                cond |= G_IO_IN;
            if (ret == SSL_ERROR_WANT_WRITE)
                cond |= G_IO_OUT;
            ret = -1;
        }
    } else {
        GError *error = NULL;
        ret = g_socket_receive(c->sock, data, len, NULL, &error);
        if (ret < 0) {
            if (error) {
                if (error->code == G_IO_ERROR_WOULD_BLOCK)
                    cond = G_IO_IN;
                g_error_free(error);
            } else {
                SPICE_DEBUG("Read error %s", error->message);
            }
            ret = -1;
        }
    }

    if (ret == -1) {
        if (cond != 0) {
            if (c->wait_interruptable) {
                if (!g_io_wait_interruptable(&c->wait, c->sock, cond)) {
                    // SPICE_DEBUG("Read blocking interrupted %d", priv->has_error);
                    return -EAGAIN;
                }
            } else {
                g_io_wait(c->sock, cond);
            }
            goto reread;
        } else {
            c->has_error = TRUE;
            return -errno;
        }
    }
    if (ret == 0) {
        SPICE_DEBUG("Closing the connection: spice_channel_read() - ret=0");
        c->has_error = TRUE;
        return 0;
    }

    return ret;
}

/*
 * Fill the 'data' buffer up with exactly 'len' bytes worth of data
 */
/* coroutine context */
static int spice_channel_read(SpiceChannel *channel, void *data, size_t len)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    if (c->has_error) return 0; /* has_error is set by disconnect(), return no error */

    return spice_channel_read_wire(channel, data, len);
}

/* coroutine context */
static void spice_channel_send_auth(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    EVP_PKEY *pubkey;
    int nRSASize;
    BIO *bioKey;
    RSA *rsa;
    const char *password;
    uint8_t *encrypted;
    int rc;

    bioKey = BIO_new(BIO_s_mem());
    g_return_if_fail(bioKey != NULL);

    BIO_write(bioKey, c->peer_msg->pub_key, SPICE_TICKET_PUBKEY_BYTES);
    pubkey = d2i_PUBKEY_bio(bioKey, NULL);
    rsa = pubkey->pkey.rsa;
    nRSASize = RSA_size(rsa);

    encrypted = g_alloca(nRSASize);
    /*
      The use of RSA encryption limit the potential maximum password length.
      for RSA_PKCS1_OAEP_PADDING it is RSA_size(rsa) - 41.
    */
    g_object_get(c->session, "password", &password, NULL);
    if (password == NULL)
        password = "";
    rc = RSA_public_encrypt(strlen(password) + 1, (uint8_t*)password,
                            encrypted, rsa, RSA_PKCS1_OAEP_PADDING);
    g_return_if_fail(rc > 0);

    spice_channel_write(channel, encrypted, nRSASize);
    memset(encrypted, 0, nRSASize);
    BIO_free(bioKey);
}

/* coroutine context */
static void spice_channel_recv_auth(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    uint32_t link_res;
    int rc;

    rc = spice_channel_read(channel, &link_res, sizeof(link_res));
    if (rc != sizeof(link_res)) {
        g_critical("incomplete auth reply (%d/%zd)", rc, sizeof(link_res));
        return;
    }

    if (link_res != SPICE_LINK_ERR_OK) {
        emit_main_context(channel, SPICE_CHANNEL_EVENT, SPICE_CHANNEL_ERROR_AUTH);
        return;
    }

    SPICE_DEBUG("%s: channel up", c->name);
    c->state = SPICE_CHANNEL_STATE_READY;

    emit_main_context(channel, SPICE_CHANNEL_EVENT, SPICE_CHANNEL_OPENED);
    if (SPICE_CHANNEL_GET_CLASS(channel)->channel_up)
        SPICE_CHANNEL_GET_CLASS(channel)->channel_up(channel);
}

/* coroutine context */
static void spice_channel_send_link(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    uint8_t *buffer, *p;
    int protocol, i;

    c->link_hdr.magic = SPICE_MAGIC;
    c->link_hdr.size = sizeof(c->link_msg);

    g_object_get(c->session, "protocol", &protocol, NULL);
    switch (protocol) {
    case 1: /* protocol 1 == major 1, old 0.4 protocol, last active minor */
        c->link_hdr.major_version = 1;
        c->link_hdr.minor_version = 3;
        c->parser = spice_get_server_channel_parser1(c->channel_type, NULL);
        c->marshallers = spice_message_marshallers_get1();
        break;
    case SPICE_VERSION_MAJOR: /* protocol 2 == current */
        c->link_hdr.major_version = SPICE_VERSION_MAJOR;
        c->link_hdr.minor_version = SPICE_VERSION_MINOR;
        c->parser = spice_get_server_channel_parser(c->channel_type, NULL);
        c->marshallers = spice_message_marshallers_get();
        break;
    default:
        g_critical("unknown major %d", protocol);
        return;
    }

    c->link_msg.connection_id = c->connection_id;
    c->link_msg.channel_type  = c->channel_type;
    c->link_msg.channel_id    = c->channel_id;
    c->link_msg.caps_offset   = sizeof(c->link_msg);

    c->link_msg.num_common_caps = c->common_caps->len;
    c->link_msg.num_channel_caps = c->caps->len;
    c->link_hdr.size += (c->link_msg.num_common_caps +
                         c->link_msg.num_channel_caps) * sizeof(uint32_t);

    buffer = spice_malloc(sizeof(c->link_hdr) + c->link_hdr.size);
    p = buffer;

    memcpy(p, &c->link_hdr, sizeof(c->link_hdr)); p += sizeof(c->link_hdr);
    memcpy(p, &c->link_msg, sizeof(c->link_msg)); p += sizeof(c->link_msg);

    for (i = 0; i < c->common_caps->len; i++) {
        *(uint32_t *)p = g_array_index(c->common_caps, uint32_t, i);
        p += sizeof(uint32_t);
    }
    for (i = 0; i < c->caps->len; i++) {
        *(uint32_t *)p = g_array_index(c->caps, uint32_t, i);
        p += sizeof(uint32_t);
    }

    spice_channel_write(channel, buffer, p - buffer);
}

/* coroutine context */
static void spice_channel_recv_link_hdr(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    int rc;

    rc = spice_channel_read(channel, &c->peer_hdr, sizeof(c->peer_hdr));
    if (rc != sizeof(c->peer_hdr)) {
        g_critical("incomplete link header (%d/%zd)", rc, sizeof(c->peer_hdr));
        return;
    }
    g_return_if_fail(c->peer_hdr.magic == SPICE_MAGIC);

    if (c->peer_hdr.major_version != c->link_hdr.major_version) {
        if (c->peer_hdr.major_version == 1) {
            /* enter spice 0.4 mode */
            g_object_set(c->session, "protocol", 1, NULL);
            SPICE_DEBUG("%s: switching to protocol 1 (spice 0.4)", c->name);
            channel_disconnect(channel);
            spice_channel_connect(channel);
            return;
        }
        g_critical("major mismatch (got %d, expected %d)",
                   c->peer_hdr.major_version, c->link_hdr.major_version);
        return;
    }

    c->peer_msg = spice_malloc(c->peer_hdr.size);
    c->state = SPICE_CHANNEL_STATE_LINK_MSG;
}

/* coroutine context */
static void spice_channel_recv_link_msg(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    int rc, num_caps, i;

    g_return_if_fail(channel != NULL);

    rc = spice_channel_read(channel, (uint8_t*)c->peer_msg + c->peer_pos,
                            c->peer_hdr.size - c->peer_pos);
    c->peer_pos += rc;
    if (c->peer_pos != c->peer_hdr.size) {
        g_warning("%s: %s: incomplete link reply (%d/%d)",
                  c->name, __FUNCTION__, rc, c->peer_hdr.size);
        return;
    }
    switch (c->peer_msg->error) {
    case SPICE_LINK_ERR_OK:
        /* nothing */
        break;
    case SPICE_LINK_ERR_NEED_SECURED:
        c->tls = true;
        SPICE_DEBUG("%s: switching to tls", c->name);
        channel_disconnect(channel);
        spice_channel_connect(channel);
        return;
    default:
        g_warning("%s: %s: unhandled error %d",
                c->name, __FUNCTION__, c->peer_msg->error);
        channel_disconnect(channel);
        emit_main_context(channel, SPICE_CHANNEL_EVENT, SPICE_CHANNEL_ERROR_LINK);
        return;
    }

    num_caps = c->peer_msg->num_channel_caps + c->peer_msg->num_common_caps;
    SPICE_DEBUG("%s: %s: %d caps", c->name, __FUNCTION__, num_caps);

    /* see original spice/client code: */
    /* g_return_if_fail(c->peer_msg + c->peer_msg->caps_offset * sizeof(uint32_t) > c->peer_msg + c->peer_hdr.size); */

    uint32_t *caps = (uint32_t *)((uint8_t *)c->peer_msg + c->peer_msg->caps_offset);

    g_array_set_size(c->remote_common_caps, c->peer_msg->num_common_caps);
    for (i = 0; i < c->peer_msg->num_common_caps; i++, caps++) {
        g_array_index(c->remote_common_caps, uint32_t, i) = *caps;
        SPICE_DEBUG("got caps %u %u", i, *caps);
    }

    g_array_set_size(c->remote_caps, c->peer_msg->num_channel_caps);
    for (i = 0; i < c->peer_msg->num_channel_caps; i++, caps++) {
        g_array_index(c->remote_caps, uint32_t, i) = *caps;
        SPICE_DEBUG("got caps %u %u", i, *caps);
    }

    c->state = SPICE_CHANNEL_STATE_AUTH;
    spice_channel_send_auth(channel);
}

/* system context */
static void spice_channel_buffered_write(SpiceChannel *channel, const void *data, size_t size)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    size_t left;

    left = c->xmit_buffer_capacity - c->xmit_buffer_size;
    if (left < size) {
        c->xmit_buffer_capacity += size + 4095;
        c->xmit_buffer_capacity &= ~4095;

        c->xmit_buffer = g_realloc(c->xmit_buffer, c->xmit_buffer_capacity);
    }

    memcpy(&c->xmit_buffer[c->xmit_buffer_size], data, size);

    c->xmit_buffer_size += size;
}

/* system context */
/* TODO: we currently flush/wakeup immediately all buffered messages */
G_GNUC_INTERNAL
void spice_channel_wakeup(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    g_io_wakeup(&c->wait);
}

/* coroutine context if @buffered is TRUE,
   system context if @buffered is FALSE */
static void spice_channel_send_msg(SpiceChannel *channel, spice_msg_out *out, gboolean buffered)
{
    uint8_t *data;
    int free_data;
    size_t len;

    g_return_if_fail(channel != NULL);
    g_return_if_fail(out != NULL);

    data = spice_marshaller_linearize(out->marshaller, 0,
                                      &len, &free_data);
    /* spice_msg_out_hexdump(out, data, len); */
    if (buffered)
        spice_channel_buffered_write(channel, data, len);
    else
        spice_channel_write(channel, data, len);
    if (free_data) {
        free(data);
    }
}

/* coroutine context */
static void spice_channel_recv_msg(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    spice_msg_in *in;
    int rc;

    if (!c->msg_in) {
        c->msg_in = spice_msg_in_new(channel);
    }
    in = c->msg_in;

    /* receive message */
    if (in->hpos < sizeof(in->header)) {
        rc = spice_channel_read(channel, (uint8_t*)&in->header + in->hpos,
                                sizeof(in->header) - in->hpos);
        if (rc < 0) {
            g_critical("recv hdr: %s", strerror(errno));
            return;
        }
        in->hpos += rc;
        if (in->hpos < sizeof(in->header))
            return;
        in->data = spice_malloc(in->header.size);
    }
    if (in->dpos < in->header.size) {
        rc = spice_channel_read(channel, in->data + in->dpos,
                                in->header.size - in->dpos);
        if (rc < 0) {
            g_critical("recv msg: %s", strerror(errno));
            return;
        }
        in->dpos += rc;
        if (in->dpos < in->header.size)
            return;
    }

    if (in->header.sub_list) {
        SpiceSubMessageList *sub_list;
        SpiceSubMessage *sub;
        spice_msg_in *sub_in;
        int i;

        sub_list = (SpiceSubMessageList *)(in->data + in->header.sub_list);
        for (i = 0; i < sub_list->size; i++) {
            sub = (SpiceSubMessage *)(in->data + sub_list->sub_messages[i]);
            sub_in = spice_msg_in_sub_new(channel, in, sub);
            sub_in->parsed = c->parser(sub_in->data, sub_in->data + sub_in->dpos,
                                       sub_in->header.type, c->peer_hdr.minor_version,
                                       &sub_in->psize, &sub_in->pfree);
            if (sub_in->parsed == NULL) {
                g_critical("failed to parse sub-message: %s type %d",
                           c->name, sub_in->header.type);
                return;
            }
            SPICE_CHANNEL_GET_CLASS(channel)->handle_msg(channel, sub_in);
            spice_msg_in_unref(sub_in);
        }
    }

    /* ack message */
    if (c->message_ack_count) {
        c->message_ack_count--;
        if (!c->message_ack_count) {
            spice_msg_out *out = spice_msg_out_new(channel, SPICE_MSGC_ACK);
            spice_msg_out_send_internal(out);
            spice_msg_out_unref(out);
            c->message_ack_count = c->message_ack_window;
        }
    }

    /* parse message */
    in->parsed = c->parser(in->data, in->data + in->dpos, in->header.type,
                           c->peer_hdr.minor_version, &in->psize, &in->pfree);
    if (in->parsed == NULL) {
        g_critical("failed to parse message: %s type %d",
                   c->name, in->header.type);
        return;
    }

    /* process message */
    SPICE_CHANNEL_GET_CLASS(channel)->handle_msg(channel, in);

    /* release message */
    spice_msg_in_unref(c->msg_in);
    c->msg_in = NULL;
}

/**
 * spice_channel_new:
 * @s: the @SpiceSession the channel is linked to
 * @type: the requested SPICE_CHANNEL type
 * @id: the channel-id
 *
 * Create a new #SpiceChannel of type @type, and channel ID @id.
 *
 * Returns: a #SpiceChannel
 **/
SpiceChannel *spice_channel_new(SpiceSession *s, int type, int id)
{
    SpiceChannel *channel;
    GType gtype = 0;

    g_return_val_if_fail(s != NULL, NULL);

    switch (type) {
    case SPICE_CHANNEL_MAIN:
        gtype = SPICE_TYPE_MAIN_CHANNEL;
        break;
    case SPICE_CHANNEL_DISPLAY:
        gtype = SPICE_TYPE_DISPLAY_CHANNEL;
        break;
    case SPICE_CHANNEL_CURSOR:
        gtype = SPICE_TYPE_CURSOR_CHANNEL;
        break;
    case SPICE_CHANNEL_INPUTS:
        gtype = SPICE_TYPE_INPUTS_CHANNEL;
        break;
    case SPICE_CHANNEL_PLAYBACK:
        gtype = SPICE_TYPE_PLAYBACK_CHANNEL;
        break;
    case SPICE_CHANNEL_RECORD:
        gtype = SPICE_TYPE_RECORD_CHANNEL;
        break;
    default:
        return NULL;
    }
    channel = SPICE_CHANNEL(g_object_new(gtype,
                                         "spice-session", s,
                                         "channel-type", type,
                                         "channel-id", id,
                                         NULL));
    return channel;
}

/**
 * spice_channel_destroy:
 * @channel:
 *
 * Disconnect and unref the @channel. Called by @spice_session_disconnect()
 *
 **/
void spice_channel_destroy(SpiceChannel *channel)
{
    g_return_if_fail(channel != NULL);

    SPICE_DEBUG("channel destroy");
    spice_channel_disconnect(channel, SPICE_CHANNEL_NONE);
    g_object_unref(channel);
}

/* coroutine context */
static int tls_verify(int preverify_ok, X509_STORE_CTX *ctx)
{
    spice_channel *c;
    char *hostname;
    SSL *ssl;

    ssl = X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx());
    c = SSL_get_app_data(ssl);

    g_object_get(c->session, "host", &hostname, NULL);
    /* TODO: check hostname */

    return preverify_ok;
}

/* coroutine context */
static void spice_channel_iterate_write(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    if (c->xmit_buffer_size) {
        spice_channel_write(channel, c->xmit_buffer, c->xmit_buffer_size);
        c->xmit_buffer_size = 0;
    }
}

/* coroutine context */
static void spice_channel_iterate_read(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    /* TODO: get rid of state, and use coroutine state */
    switch (c->state) {
    case SPICE_CHANNEL_STATE_LINK_HDR:
        spice_channel_recv_link_hdr(channel);
        break;
    case SPICE_CHANNEL_STATE_LINK_MSG:
        spice_channel_recv_link_msg(channel);
        break;
    case SPICE_CHANNEL_STATE_AUTH:
        spice_channel_recv_auth(channel);
        break;
    case SPICE_CHANNEL_STATE_READY:
        spice_channel_recv_msg(channel);
        break;
    default:
        g_critical("unknown state %d", c->state);
    }
}

/* coroutine context */
static gboolean spice_channel_iterate(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    GIOCondition ret;

    do {
        if (c->has_error) {
            SPICE_DEBUG("channel has error, breaking loop");
            return FALSE;
        }

        SPICE_CHANNEL_GET_CLASS(channel)->iterate_write(channel);
    } while (!(ret = g_io_wait_interruptable(&c->wait, c->sock, G_IO_IN)));
    /* TODO: check ret if error */

    SPICE_CHANNEL_GET_CLASS(channel)->iterate_read(channel);

    return TRUE;
}

/* we use an idle function to allow the coroutine to exit before we actually
 * unref the object since the coroutine's state is part of the object */
static gboolean spice_channel_delayed_unref(gpointer data)
{
    SpiceChannel *channel = SPICE_CHANNEL(data);
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(data);

    g_return_val_if_fail(channel != NULL, FALSE);
    SPICE_DEBUG("Delayed unref channel=%p", channel);

    g_return_val_if_fail(c->coroutine.exited == TRUE, FALSE);

    g_object_unref(G_OBJECT(data));

    return FALSE;
}

/* coroutine context */
static void *spice_channel_coroutine(void *data)
{
    SpiceChannel *channel = SPICE_CHANNEL(data);
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(data);
    int ret;

    SPICE_DEBUG("Started background coroutine");

    if (spice_session_get_client_provided_socket(c->session)) {
        if (c->fd < 0) {
            g_critical("fd not provided!");
            goto cleanup;
        }

	if (!(c->sock = g_socket_new_from_fd(c->fd, NULL))) {
		SPICE_DEBUG("Failed to open socket from fd %d", c->fd);
		return FALSE;
	}

	g_socket_set_blocking(c->sock, FALSE);
        goto connected;
    }

reconnect:
    c->sock = spice_session_channel_open_host(c->session, c->tls);
    if (c->sock == NULL) {
        if (!c->tls) {
            SPICE_DEBUG("connection failed, trying with TLS port");
            c->tls = true; /* FIXME: does that really work with provided fd */
            goto reconnect;
        }
        SPICE_DEBUG("Connect error");
        emit_main_context(channel, SPICE_CHANNEL_EVENT, SPICE_CHANNEL_ERROR_CONNECT);
        goto cleanup;
    }

    if (c->tls) {
        gchar *ca_file;
        int rc;

        c->ctx = SSL_CTX_new(TLSv1_method());
        if (c->ctx == NULL) {
            g_critical("SSL_CTX_new failed");
            goto cleanup;
        }

        g_object_get(c->session, "ca-file", &ca_file, NULL);
        if (ca_file) {
            rc = SSL_CTX_load_verify_locations(c->ctx, ca_file, NULL);
            if (rc <= 0) {
                g_warning("loading ca certs from %s failed", ca_file);
            }
        }
        SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, tls_verify);

        c->ssl = SSL_new(c->ctx);
        if (c->ssl == NULL) {
            g_critical("SSL_new failed");
            goto cleanup;
        }
        rc = SSL_set_fd(c->ssl, c->fd);
        if (rc <= 0) {
            g_critical("SSL_set_fd failed");
            goto cleanup;
        }
        SSL_set_app_data(c->ssl, c);
ssl_reconnect:
        rc = SSL_connect(c->ssl);
        if (rc <= 0) {
            rc = SSL_get_error(c->ssl, rc);
            if (rc == SSL_ERROR_WANT_READ || rc == SSL_ERROR_WANT_WRITE) {
                g_io_wait(c->sock, G_IO_OUT|G_IO_ERR|G_IO_HUP);
                goto ssl_reconnect;
            } else {
                g_warning("%s: SSL_connect: %s",
                          c->name, ERR_error_string(rc, NULL));
                emit_main_context(channel, SPICE_CHANNEL_EVENT, SPICE_CHANNEL_ERROR_TLS);
                goto cleanup;
            }
        }
    }

connected:
    c->state = SPICE_CHANNEL_STATE_LINK_HDR;
    spice_channel_send_link(channel);

    while ((ret = spice_channel_iterate(channel)))
        ;

cleanup:
    SPICE_DEBUG("Doing final channel cleanup");
    channel_disconnect(channel);
    emit_main_context(channel, SPICE_CHANNEL_EVENT, SPICE_CHANNEL_CLOSED);

    g_idle_add(spice_channel_delayed_unref, data);

    /* Co-routine exits now - the SpiceChannel object may no longer exist,
       so don't do anything else now unless you like SEGVs */
    return NULL;
}

static gboolean channel_connect(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);
    struct coroutine *co;

    g_return_val_if_fail(c != NULL, FALSE);

    if (c->session == NULL || c->channel_type == -1 || c->channel_id == -1) {
        /* unset properties or unknown channel type */
        g_warning("%s: channel setup incomplete", __FUNCTION__);
        return false;
    }
    if (c->state != SPICE_CHANNEL_STATE_UNCONNECTED) {
        return true;
    }

    if (spice_session_get_client_provided_socket(c->session)) {
        if (c->fd == -1) {
            g_signal_emit(channel, signals[SPICE_CHANNEL_OPEN_FD], 0, c->tls);
            return true;
        }
    }

    g_return_val_if_fail(c->sock == NULL, FALSE);
    g_object_ref(G_OBJECT(channel)); /* Unref'd when co-routine exits */

    /* ----------- FIXME gtk-vnc does that in idle */
    SPICE_DEBUG("Open coroutine starting");
    c->open_id = 0;

    co = &c->coroutine;

    co->stack_size = 16 << 20;
    co->entry = spice_channel_coroutine;
    co->release = NULL;

    coroutine_init(co);
    coroutine_yieldto(co, channel);

    return true;
}

/**
 * spice_channel_connect:
 * @channel:
 *
 * Connect the channel, using #SpiceSession connection informations
 *
 * Returns: %TRUE on success.
 **/
gboolean spice_channel_connect(SpiceChannel *channel)
{
    return channel_connect(channel);
}

/**
 * spice_channel_open_fd:
 * @channel:
 * @fd: a file descriptor (socket)
 *
 * Connect the channel using @fd socket.
 *
 * Returns: %TRUE on success.
 **/
gboolean spice_channel_open_fd(SpiceChannel *channel, int fd)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    g_return_val_if_fail(c != NULL, FALSE);
    g_return_val_if_fail(fd >= 0, FALSE);

    c->fd = fd;

    return channel_connect(channel);
}

/* TODO: make this a vmethod, and implement in all childs? */
/* system or coroutine context */
static void channel_disconnect(SpiceChannel *channel)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    g_return_if_fail(c != NULL);

    if (c->state == SPICE_CHANNEL_STATE_UNCONNECTED) {
        return;
    }

    if (c->open_id) {
        g_source_remove(c->open_id);
        c->open_id = 0;
    }

    if (c->tls) {
        if (c->ssl) {
            SSL_free(c->ssl);
            c->ssl = NULL;
        }
        if (c->ctx) {
            SSL_CTX_free(c->ctx);
            c->ctx = NULL;
        }
    }

    if (c->sock) {
        g_socket_close(c->sock, NULL);
        g_object_unref(c->sock);
        c->sock = NULL;
    }
    c->state = SPICE_CHANNEL_STATE_UNCONNECTED;
    free(c->peer_msg);
    c->peer_msg = NULL;
    c->peer_pos = 0;

    if (c->xmit_buffer) {
        g_free(c->xmit_buffer);
        c->xmit_buffer = NULL;
        c->xmit_buffer_size = 0;
        c->xmit_buffer_capacity = 0;
    }

    g_array_set_size(c->remote_common_caps, 0);
    g_array_set_size(c->remote_caps, 0);
    g_array_set_size(c->common_caps, 0);
    g_array_set_size(c->caps, 0);
}

/**
 * spice_channel_disconnect:
 * @channel:
 * @reason: a channel event emitted on main context (or #SPICE_CHANNEL_NONE)
 *
 * Close the socket and reset connection specific data. Finally, emit
 * @reason #SpiceChannel::channel-event on main context if not
 * #SPICE_CHANNEL_NONE.
 **/
void spice_channel_disconnect(SpiceChannel *channel, SpiceChannelEvent reason)
{
    spice_channel *c = SPICE_CHANNEL_GET_PRIVATE(channel);

    g_return_if_fail(c != NULL);

    if (c->state == SPICE_CHANNEL_STATE_UNCONNECTED) {
        return;
    }

    c->fd = -1;
    c->has_error = 1;
    spice_channel_wakeup(channel);

    /* channel_disconnect(channel); */

    if (reason != SPICE_CHANNEL_NONE) {
        g_signal_emit(G_OBJECT(channel), signals[SPICE_CHANNEL_EVENT], 0, reason);
    }
}

static gboolean test_capability(GArray *caps, guint32 cap)
{
    guint32 word_index = cap / 32;

    if (caps == NULL)
        return FALSE;

    if (caps->len < word_index + 1)
        return FALSE;

    return (g_array_index(caps, guint32, word_index) & (1 << (cap % 32))) != 0;
}

/**
 * spice_channel_test_capability:
 * @self:
 * @cap:
 *
 * Test availability of specific channel-kind capability of the remote.
 *
 * Returns: %TRUE if @cap, a specific channel capability, is available.
 **/
gboolean spice_channel_test_capability(SpiceChannel *self, guint32 cap)
{
    spice_channel *c;

    g_return_val_if_fail(SPICE_IS_CHANNEL(self), FALSE);

    c = SPICE_CHANNEL_GET_PRIVATE(self);
    return test_capability(c->remote_caps, cap);
}

static void set_capability(GArray *caps, guint32 cap)
{
    guint word_index = cap / 32;

    g_return_if_fail(caps != NULL);

    if (caps->len <= word_index)
        g_array_set_size(caps, word_index + 1);

    g_array_index(caps, guint32, word_index) =
        g_array_index(caps, guint32, word_index) | (1 << (cap % 32));
}

/**
 * spice_channel_set_capability:
 * @channel:
 * @cap: a capability
 *
 * Enable specific channel-kind capability.
 **/
void spice_channel_set_capability(SpiceChannel *channel, guint32 cap)
{
    spice_channel *c;

    g_return_if_fail(SPICE_IS_CHANNEL(channel));

    c = SPICE_CHANNEL_GET_PRIVATE(channel);
    set_capability(c->caps, cap);
}
