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
    GHashTable *associations;
    GHashTable *connections;
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
    GCancellable *cancelable;
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
        conn->cancelable = g_cancellable_new();
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
        g_object_unref(conn->cancelable);
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
    if (!g_cancellable_is_cancelled(conn->cancelable))
        g_cancellable_cancel(conn->cancelable);
    g_hash_table_remove(conn->pf->connections, GUINT_TO_POINTER(conn->id));
}

static void close_connection(Connection * conn)
{
    close_agent_connection(conn->pf, conn->id);
    close_connection_no_notify(conn);
}

typedef struct PortAddress
{
    guint16 port;
    char * address;
} PortAddress;

static gpointer new_port_address(guint16 port, const char * address)
{
    PortAddress * p = g_malloc(sizeof(PortAddress));
    p->port = port;
    p->address = g_strdup(address);
    return p;
}

static void unref_port_address(gpointer value)
{
    g_free(((PortAddress *)value)->address);
    g_free(value);
}

PortForwarder *new_port_forwarder(void *channel, port_forwarder_send_command_cb cb)
{
    PortForwarder *pf = g_malloc(sizeof(PortForwarder));
    if (pf) {
        SPICE_DEBUG("Created new port forwarder");
        pf->channel = channel;
        pf->send_command = cb;
        pf->associations = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                 NULL, unref_port_address);
        pf->connections = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                                NULL, unref_connection);
        if (!pf->associations || !pf->connections) {
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
        if (pf->associations) {
            g_hash_table_destroy(pf->associations);
        }
        if (pf->connections) {
            g_hash_table_destroy(pf->connections);
        }
        g_free(pf);
    }
}

void port_forwarder_agent_disconnected(PortForwarder *pf)
{
    SPICE_DEBUG("Agent disconnected, close all connections");
    g_hash_table_remove_all(pf->associations);
    g_hash_table_remove_all(pf->connections);
}

gboolean port_forwarder_associate(PortForwarder* pf, const gchar * bind_address,
                                  guint16 rport, const gchar * host, guint16 lport)
{
    SPICE_DEBUG("Associate guest %s, port %d -> %s port %d", bind_address, rport, host, lport);
    if (g_hash_table_lookup(pf->associations, GUINT_TO_POINTER(rport))) {
        port_forwarder_disassociate(pf, rport);
    }
    g_hash_table_insert(pf->associations, GUINT_TO_POINTER(rport),
                        new_port_address(lport, host));

    if (bind_address) {
        int msg_len = sizeof(VDAgentPortForwardListenBindMessage) + strlen(bind_address) + 1;
        VDAgentPortForwardListenBindMessage *msg = g_malloc0(msg_len);
        msg->port = rport;
        strcpy(msg->bind_address, bind_address);
        send_command(pf, VD_AGENT_PORT_FORWARD_LISTEN_BIND, (const guint8 *)msg, msg_len);
        g_free(msg);
    } else {
        VDAgentPortForwardListenMessage msg;
        msg.port = rport;
        send_command(pf, VD_AGENT_PORT_FORWARD_LISTEN, (const guint8 *)&msg, sizeof(msg));
    }
    return TRUE;
}

gboolean port_forwarder_disassociate(PortForwarder *pf, guint16 rport) {
    VDAgentPortForwardShutdownMessage msg;

    if (!g_hash_table_remove(pf->associations, GUINT_TO_POINTER(rport))) {
        g_warning("Remote port %d is not associated with a local port.", rport);
        return FALSE;
    } else {
        SPICE_DEBUG("Disassociate remote port %d", rport);
        msg.port = rport;
        send_command(pf, VD_AGENT_PORT_FORWARD_SHUTDOWN, (const guint8 *)&msg, sizeof(msg));
        return TRUE;
    }
}


#define DATA_HEAD_SIZE sizeof(VDAgentPortForwardDataMessage)
#define BUFFER_SIZE MAX_MSG_SIZE - DATA_HEAD_SIZE

static void connection_read_callback(GObject *source_object, GAsyncResult *res,
                                     gpointer user_data);

static void program_read(Connection *conn)
{
    GInputStream *stream = g_io_stream_get_input_stream((GIOStream *)conn->conn);
    guint8 *data = conn->read_buffer + DATA_HEAD_SIZE;
    g_input_stream_read_async(stream, data, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                              conn->cancelable, connection_read_callback, conn);
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

    if (g_cancellable_is_cancelled(conn->cancelable)) {
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

    if (g_cancellable_is_cancelled(conn->cancelable)) {
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

static void handle_connect(PortForwarder *pf, VDAgentPortForwardConnectMessage *msg)
{
    gpointer id = GUINT_TO_POINTER(msg->id), rport = GUINT_TO_POINTER(msg->port);
    PortAddress *local;
    Connection *conn = g_hash_table_lookup(pf->connections, id);
    if (conn) {
        g_warning("Connection %d already exists.", msg->id);
        close_connection_no_notify(conn);
    }

    local = g_hash_table_lookup(pf->associations, rport);
    SPICE_DEBUG("Connection command, id %d on remote port %d -> %s port %d",
                msg->id, msg->port, local->address, local->port);
    if (local) {
        conn = new_connection(pf, msg->id, msg->ack_interval);
        if (conn) {
            g_hash_table_insert(pf->connections, id, conn);
            conn->refs++;
            g_socket_client_connect_to_host_async(conn->socket, local->address,
                                                  local->port, conn->cancelable,
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
        case VD_AGENT_PORT_FORWARD_CONNECT:
            handle_connect(pf, (VDAgentPortForwardConnectMessage *)msg);
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
