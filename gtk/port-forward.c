/**
 * Copyright Flexible Software Solutions S.L. 2014
 **/

#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include <spice/vd_agent.h>
#include "spice-util.h"
#include "port-forward.h"

struct PortForwarder {
    void *channel;
    port_forwarder_send_command_cb send_command;
    GHashTable *remote_assocs;
    GHashTable *connections;
    GSocketListener *listener;
    GCancellable *listener_cancellable;
};

static void send_command(PortForwarder *pf, guint32 command,
                         const guint8 *data, guint32 data_size)
{
    SPICE_DEBUG("Sending command %u with %u bytes", command, data_size);
    pf->send_command(pf->channel, command, data, data_size);
}

#define WINDOW_SIZE 10*1024*1024
#define MAX_MSG_SIZE VD_AGENT_MAX_DATA_SIZE - sizeof(VDAgentMessage)

typedef struct Connection {
    GSocketClient *socket;
    GSocketConnection *conn;
    GCancellable *cancellable;
    GQueue *write_buffer;
    guint8 *read_buffer;
    guint32 data_sent, data_received, ack_interval;
    gboolean connecting;
    PortForwarder *pf;
    int refs;
    int id;
} Connection;

static Connection *new_connection(PortForwarder *pf, int id, guint32 ack_int)
{
    Connection *conn = (Connection *)g_malloc0(sizeof(Connection));
    if (conn) {
        conn->socket = g_socket_client_new();
        if (!conn->socket) {
            g_free(conn);
            return NULL;
        }
        conn->cancellable = g_cancellable_new();
        conn->refs = 1;
        conn->id = id;
        conn->pf = pf;
        conn->ack_interval = ack_int;
        conn->connecting = TRUE;
        conn->write_buffer = g_queue_new();
        conn->read_buffer = (guint8 *)g_malloc(MAX_MSG_SIZE);
    }
    return conn;
}

static void unref_connection(gpointer value)
{
    Connection * conn = (Connection *) value;
    if (!--conn->refs) {
        SPICE_DEBUG("Closing connection %d", conn->id);
        g_object_unref(conn->cancellable);
        if (conn->conn) {
            g_io_stream_close((GIOStream *)conn->conn, NULL, NULL);
        }
        if (conn->socket) {
            g_object_unref(conn->socket);
        }
        g_queue_free_full(conn->write_buffer, (GDestroyNotify)g_bytes_unref);
        g_free(conn->read_buffer);
        g_free(conn);
    }
}

static void close_agent_connection(PortForwarder *pf, int id)
{
    VDAgentPortForwardCloseMessage closeMsg;
    closeMsg.id = id;
    send_command(pf, VD_AGENT_PORT_FORWARD_CLOSE, (const guint8 *)&closeMsg, sizeof(closeMsg));
}

static void close_connection_no_notify(Connection * conn)
{
    SPICE_DEBUG("Start closing connection %d", conn->id);
    if (!g_cancellable_is_cancelled(conn->cancellable))
        g_cancellable_cancel(conn->cancellable);
    g_hash_table_remove(conn->pf->connections, GUINT_TO_POINTER(conn->id));
}

static void close_connection(Connection * conn)
{
    close_agent_connection(conn->pf, conn->id);
    close_connection_no_notify(conn);
}

#define TYPE_ADDRESS_PORT            (address_port_get_type ())
#define ADDRESS_PORT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_ADDRESS_PORT, AddressPort))
#define IS_ADDRESS_PORT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_ADDRESS_PORT))
#define ADDRESS_PORT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_ADDRESS_PORT, AddressPortClass))
#define IS_ADDRESS_PORT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_ADDRESS_PORT))
#define ADDRESS_PORT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_ADDRESS_PORT, AddressPortClass))

typedef struct _AddressPort
{
  /* Parent instance structure */
  GObject parent_instance;

  /* instance members */
  guint16 port;
  gchar *address;
} AddressPort;

typedef struct _AddressPortClass
{
  /* Parent class structure */
  GObjectClass parent_class;

  /* class members */
} AddressPortClass;

G_DEFINE_TYPE(AddressPort, address_port, G_TYPE_OBJECT);

static void address_port_dispose(GObject *gobject)
{
  G_OBJECT_CLASS(address_port_parent_class)->dispose(gobject);
}

static void address_port_finalize(GObject *gobject)
{
  AddressPort *self = ADDRESS_PORT(gobject);
  g_free(self->address);
  G_OBJECT_CLASS(address_port_parent_class)->finalize(gobject);
}

static void address_port_class_init(AddressPortClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->dispose = address_port_dispose;
    gobject_class->finalize = address_port_finalize;
}

static void address_port_init (AddressPort *self)
{
}

static gpointer address_port_new(guint16 port, const char * address)
{
    AddressPort * p = g_object_new(TYPE_ADDRESS_PORT, NULL);
    p->port = port;
    p->address = g_strdup(address);
    return p;
}

static void listener_accept_callback(GObject *source_object, GAsyncResult *res,
                                     gpointer user_data);

PortForwarder *new_port_forwarder(void *channel, port_forwarder_send_command_cb cb)
{
    PortForwarder *pf = g_malloc(sizeof(PortForwarder));
    if (pf) {
        SPICE_DEBUG("Created new port forwarder");
        pf->channel = channel;
        pf->send_command = cb;
        pf->remote_assocs = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                  NULL, g_object_unref);
        pf->connections = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                NULL, unref_connection);
        pf->listener = g_socket_listener_new();
        pf->listener_cancellable = g_cancellable_new();
        if (!pf->remote_assocs || !pf->connections ||
                !pf->listener || !pf->listener_cancellable) {
            delete_port_forwarder(pf);
            pf = NULL;
        }
    }
    return pf;
}

void delete_port_forwarder(PortForwarder *pf)
{
    if (pf) {
        SPICE_DEBUG("Deleting port forwarder");
        if (pf->remote_assocs) {
            g_hash_table_destroy(pf->remote_assocs);
        }
        if (pf->connections) {
            g_hash_table_destroy(pf->connections);
        }
        if (pf->listener_cancellable) {
            g_object_unref(pf->listener_cancellable);
        }
        if (pf->listener) {
            g_socket_listener_close(pf->listener);
        }
        g_free(pf);
    }
}

void port_forwarder_agent_disconnected(PortForwarder *pf)
{
    SPICE_DEBUG("Agent disconnected, close all connections");
    g_hash_table_remove_all(pf->remote_assocs);
    g_hash_table_remove_all(pf->connections);
}

gboolean port_forwarder_associate_remote(PortForwarder *pf, const gchar * bind_address,
                                         guint16 rport, const gchar * host, guint16 lport)
{
    SPICE_DEBUG("Associate guest %s, port %d -> %s port %d", bind_address, rport, host, lport);
    if (g_hash_table_lookup(pf->remote_assocs, GUINT_TO_POINTER(rport))) {
        port_forwarder_disassociate_remote(pf, rport);
    }
    g_hash_table_insert(pf->remote_assocs, GUINT_TO_POINTER(rport),
                        address_port_new(lport, host));

    if (!bind_address) {
        bind_address = "localhost";
    }
    int msg_len = sizeof(VDAgentPortForwardListenMessage) + strlen(bind_address) + 1;
    VDAgentPortForwardListenMessage *msg = g_malloc0(msg_len);
    msg->port = rport;
    strcpy(msg->bind_address, bind_address);
    send_command(pf, VD_AGENT_PORT_FORWARD_LISTEN, (const guint8 *)msg, msg_len);
    g_free(msg);
    return TRUE;
}

gboolean port_forwarder_disassociate_remote(PortForwarder *pf, guint16 rport) {
    VDAgentPortForwardShutdownMessage msg;

    if (!g_hash_table_remove(pf->remote_assocs, GUINT_TO_POINTER(rport))) {
        g_warning("Remote port %d is not associated with a local port.", rport);
        return FALSE;
    } else {
        SPICE_DEBUG("Disassociate remote port %d", rport);
        msg.port = rport;
        send_command(pf, VD_AGENT_PORT_FORWARD_SHUTDOWN, (const guint8 *)&msg, sizeof(msg));
        return TRUE;
    }
}

gboolean port_forwarder_associate_local(PortForwarder *pf, const gchar *bind_address,
                                        guint16 lport, const gchar *host, guint16 rport)
{
    // Listen and wait for a connection
    gboolean res;
    if (bind_address) {
        GInetAddress *inet_address = g_inet_address_new_from_string(bind_address);
        GSocketAddress *socket_address = g_inet_socket_address_new(inet_address, lport);
        res = g_socket_listener_add_address(pf->listener, socket_address,
                                            G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_DEFAULT,
                                            address_port_new(rport, host), NULL, NULL);
    } else {
        res = g_socket_listener_add_inet_port(pf->listener, lport,
                                              address_port_new(rport, host), NULL);
    }
    g_cancellable_cancel(pf->listener_cancellable);
    g_object_unref(pf->listener_cancellable);
    pf->listener_cancellable = g_cancellable_new();
    g_socket_listener_accept_async(pf->listener, pf->listener_cancellable,
                                   listener_accept_callback, pf);
    return res;
}

gboolean port_forwarder_disassociate_local(PortForwarder *pf, guint16 lport)
{
    return TRUE;
}

#define DATA_HEAD_SIZE sizeof(VDAgentPortForwardDataMessage)
#define BUFFER_SIZE MAX_MSG_SIZE - DATA_HEAD_SIZE

static void listener_accept_callback(GObject *source_object, GAsyncResult *res,
                                     gpointer user_data)
{
    PortForwarder *pf = (PortForwarder *)user_data;
    GError *error = NULL;
    GSocketConnection *c = g_socket_listener_accept_finish(pf->listener, res,
                                                           &source_object, &error);
    if (error) {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            g_warning("Could not accept connection");
    } else {
        GInetSocketAddress * local_address =
                (GInetSocketAddress *)g_socket_connection_get_local_address(c, NULL);
        guint16 port = g_inet_socket_address_get_port(local_address);
        AddressPort * host = ADDRESS_PORT(source_object);
        SPICE_DEBUG("Accepted connection on port %d to %s:%d",
                    port, host->address, host->port);

        g_socket_listener_accept_async(pf->listener, pf->listener_cancellable,
                                       listener_accept_callback, pf);
    }
}

static void connection_read_callback(GObject *source_object, GAsyncResult *res,
                                     gpointer user_data);

static void program_read(Connection *conn)
{
    GInputStream *stream = g_io_stream_get_input_stream((GIOStream *)conn->conn);
    guint8 *data = conn->read_buffer + DATA_HEAD_SIZE;
    g_input_stream_read_async(stream, data, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                              conn->cancellable, connection_read_callback, conn);
}

static void connection_read_callback(GObject *source_object, GAsyncResult *res,
                                     gpointer user_data)
{
    Connection *conn = (Connection *)user_data;
    PortForwarder *pf = conn->pf;
    GError *error = NULL;
    GInputStream *stream = (GInputStream *)source_object;
    gssize bytes = g_input_stream_read_finish(stream, res, &error);
    VDAgentPortForwardDataMessage *msg = (VDAgentPortForwardDataMessage *)conn->read_buffer;

    if (g_cancellable_is_cancelled(conn->cancellable)) {
        unref_connection(conn);
        return;
    }

    if (error || bytes == 0) {
        /* Error or connection closed by peer */
        if (error)
            SPICE_DEBUG("Read error on connection %d: %s", conn->id, error->message);
        else
            SPICE_DEBUG("Connection %d reset by peer", conn->id);
        close_connection(conn);
        unref_connection(conn);
    } else {
        msg->id = conn->id;
        msg->size = bytes;
        SPICE_DEBUG("Read %lu bytes on connection %d", msg->size, conn->id);
        send_command(pf, VD_AGENT_PORT_FORWARD_DATA,
                     conn->read_buffer, DATA_HEAD_SIZE + msg->size);
        conn->data_sent += msg->size;
        if (conn->data_sent < WINDOW_SIZE) {
            program_read(conn);
        } else {
            unref_connection(conn);
        }
    }
}

static void connection_write_callback(GObject *source_object, GAsyncResult *res,
                                      gpointer user_data)
{
    Connection *conn = (Connection *)user_data;
    GOutputStream *stream = (GOutputStream *)source_object;
    GBytes *bytes, *new_bytes;
    GError *error = NULL;
    int num_written = g_output_stream_write_bytes_finish(stream, res, &error);
    int remaining;
    VDAgentPortForwardAckMessage msg;

    if (error != NULL) {
        /* Error or connection closed by peer */
        SPICE_DEBUG("Write error on connection %d: %s", conn->id, error->message);
        close_connection(conn);
        unref_connection(conn);
    } else {
        bytes = (GBytes *)g_queue_pop_head(conn->write_buffer);
        SPICE_DEBUG("Written %d bytes on connection %d", num_written, conn->id);
        remaining = g_bytes_get_size(bytes) - num_written;
        if (remaining) {
            SPICE_DEBUG("Still %d bytes to go on connection %d", remaining, conn->id);
            new_bytes = g_bytes_new_from_bytes(bytes, num_written, remaining);
            g_queue_push_head(conn->write_buffer, new_bytes);
        }
        if(!g_queue_is_empty(conn->write_buffer)) {
            g_output_stream_write_bytes_async(stream, g_queue_peek_head(conn->write_buffer),
                                              G_PRIORITY_DEFAULT, NULL,
                                              connection_write_callback, conn);
        } else {
            unref_connection(conn);
        }
        g_bytes_unref(bytes);

        conn->data_received += num_written;
        if (conn->data_received >= conn->ack_interval) {
            msg.id = conn->id;
            msg.size = conn->data_received;
            conn->data_received = 0;
            send_command(conn->pf, VD_AGENT_PORT_FORWARD_ACK,
                         (const guint8 *)&msg, sizeof(msg));
        }
    }
}

static void connection_connect_callback(GObject *source_object, GAsyncResult *res,
                                        gpointer user_data)
{
    Connection *conn = (Connection *)user_data;
    VDAgentPortForwardAckMessage msg = {.id = conn->id, .size = WINDOW_SIZE/2};

    if (g_cancellable_is_cancelled(conn->cancellable)) {
        unref_connection(conn);
        return;
    }

    conn->conn = g_socket_client_connect_to_host_finish((GSocketClient *)source_object, res, NULL);
    if (!conn->conn) {
        /* Error */
        SPICE_DEBUG("Connection %d could not connect", conn->id);
        close_connection(conn);
        unref_connection(conn);
    } else {
        conn->connecting = FALSE;
        program_read(conn);
        send_command(conn->pf, VD_AGENT_PORT_FORWARD_ACK,
                     (const guint8 *)&msg, sizeof(msg));
    }
}

static void handle_accepted(PortForwarder *pf, VDAgentPortForwardAcceptedMessage *msg)
{
    gpointer id = GUINT_TO_POINTER(msg->id), rport = GUINT_TO_POINTER(msg->port);
    AddressPort *local;
    Connection *conn = g_hash_table_lookup(pf->connections, id);
    if (conn) {
        g_warning("Connection %d already exists.", msg->id);
        close_connection_no_notify(conn);
    }

    local = ADDRESS_PORT(g_hash_table_lookup(pf->remote_assocs, rport));
    SPICE_DEBUG("Connection command, id %d on remote port %d -> %s port %d",
                msg->id, msg->port, local->address, local->port);
    if (local) {
        conn = new_connection(pf, msg->id, msg->ack_interval);
        if (conn) {
            g_hash_table_insert(pf->connections, id, conn);
            conn->refs++;
            g_socket_client_connect_to_host_async(conn->socket, local->address,
                                                  local->port, conn->cancellable,
                                                  connection_connect_callback, conn);
        } else {
            /* Error, close connection in agent */
            close_agent_connection(pf, msg->id);
        }
    } else {
        g_warning("Remote port %d is not associated with a local port.", msg->port);
        close_agent_connection(pf, msg->id);
    }
}

static void handle_data(PortForwarder *pf, VDAgentPortForwardDataMessage *msg)
{
    Connection *conn = g_hash_table_lookup(pf->connections, GUINT_TO_POINTER(msg->id));
    GBytes *chunk;
    GOutputStream *stream;

    if (!conn) {
        /* Ignore, this is usually an already closed connection */
        g_warning("Connection %d does not exists.", msg->id);
    } else if (conn->connecting) {
        g_warning("Connection %d is still not connected!", conn->id);
    } else {
        SPICE_DEBUG("Data command, %d bytes on connection %d", (int)msg->size, conn->id);
        chunk = g_bytes_new(msg->data, msg->size);
        g_queue_push_tail(conn->write_buffer, chunk);
        if (g_queue_get_length(conn->write_buffer) == 1) {
            conn->refs++;
            stream = g_io_stream_get_output_stream((GIOStream *)conn->conn);
            g_output_stream_write_bytes_async(stream, chunk,
                                              G_PRIORITY_DEFAULT, NULL,
                                              connection_write_callback, conn);
        }
    }
}

static void handle_close(PortForwarder *pf, VDAgentPortForwardCloseMessage *msg)
{
    Connection *conn = g_hash_table_lookup(pf->connections, GUINT_TO_POINTER(msg->id));
    if (conn) {
        SPICE_DEBUG("Close command for connection %d", conn->id);
        close_connection_no_notify(conn);
    } else {
        /* Error, close connection in agent */
        g_warning("Connection %d does not exists.", msg->id);
        close_agent_connection(pf, msg->id);
    }
}

static void handle_ack(PortForwarder *pf, VDAgentPortForwardAckMessage *msg)
{
    Connection *conn = g_hash_table_lookup(pf->connections, GUINT_TO_POINTER(msg->id));
    if (conn) {
        SPICE_DEBUG("ACK command for connection %d with %d bytes", conn->id, (int)msg->size);
        guint32 data_sent_before = conn->data_sent;
        conn->data_sent -= msg->size;
        if (conn->data_sent < WINDOW_SIZE && data_sent_before >= WINDOW_SIZE) {
            conn->refs++;
            program_read(conn);
        }
    } else {
        /* Ignore, this is usually an already closed connection */
        g_warning("Connection %d does not exists.", msg->id);
    }
}

void port_forwarder_handle_message(PortForwarder* pf, guint32 command, gpointer msg)
{
    switch (command) {
        case VD_AGENT_PORT_FORWARD_ACCEPTED:
            handle_accepted(pf, (VDAgentPortForwardAcceptedMessage *)msg);
            break;
        case VD_AGENT_PORT_FORWARD_DATA:
            handle_data(pf, (VDAgentPortForwardDataMessage *)msg);
            break;
        case VD_AGENT_PORT_FORWARD_CLOSE:
            handle_close(pf, (VDAgentPortForwardCloseMessage *)msg);
            break;
        case VD_AGENT_PORT_FORWARD_ACK:
            handle_ack(pf, (VDAgentPortForwardAckMessage *)msg);
            break;
        default:
            break;
    }
}
