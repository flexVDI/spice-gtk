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

typedef struct Connection {
    GSocketClient *socket;
    GSocketConnection *conn;
    GCancellable *cancelator;
    GBytes *write_buffer;
    PortForwarder *pf;
    int refs;
    int id;
} Connection;

static Connection *new_connection(PortForwarder *pf, int id)
{
    Connection *conn = (Connection *)g_malloc0(sizeof(Connection));
    if (conn) {
        conn->socket = g_socket_client_new();
        if (!conn->socket) {
            g_free(conn);
            return NULL;
        }
        conn->cancelator = g_cancellable_new();
        conn->refs = 1;
        conn->id = id;
        conn->pf = pf;
    }
    return conn;
}

static void unref_connection(gpointer value)
{
    Connection * conn = (Connection *) value;
    if (!g_cancellable_is_cancelled(conn->cancelator))
        g_cancellable_cancel(conn->cancelator);
    SPICE_DEBUG("Unref connection %p with %d refs", conn, conn->refs);
    if (!--conn->refs) {
        SPICE_DEBUG("Destroy connection %p with %d refs", conn, conn->refs);
        g_object_unref(conn->cancelator);
        if (conn->conn) {
            g_io_stream_close((GIOStream *)conn->conn, NULL, NULL);
        }
        if (conn->socket) {
            g_object_unref(conn->socket);
        }
        g_free(conn);
    }
}

static void close_agent_connection(PortForwarder *pf, int id)
{
    VDAgentPortForwardCloseMessage closeMsg;

    SPICE_DEBUG("Closing connection %d", id);
    closeMsg.id = id;
    pf->send_command(pf->channel, VD_AGENT_PORT_FORWARD_CLOSE,
                     (const uint8_t *)&closeMsg, sizeof(closeMsg));
}

static void close_connection(Connection * conn)
{
    close_agent_connection(conn->pf, conn->id);
    g_hash_table_remove(conn->pf->connections, GUINT_TO_POINTER(conn->id));
    unref_connection(conn);
}

PortForwarder *new_port_forwarder(void *channel, port_forwarder_send_command_cb cb)
{
    PortForwarder *pf = g_malloc(sizeof(PortForwarder));
    if (pf) {
        pf->channel = channel;
        pf->send_command = cb;
        pf->associations = g_hash_table_new(g_direct_hash, g_direct_equal);
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
        if (pf->associations) {
            g_hash_table_destroy(pf->associations);
        }
        if (pf->connections) {
            g_hash_table_destroy(pf->connections);
        }
        g_free(pf);
    }
}

void port_forwarder_agent_connected(PortForwarder *pf)
{
    VDAgentPortForwardShutdownMessage msg;
    msg.port = 0;
    pf->send_command(pf->channel, VD_AGENT_PORT_FORWARD_SHUTDOWN,
                     (const uint8_t *)&msg, sizeof(msg));
}

void port_forwarder_agent_disconnected(PortForwarder *pf)
{
    g_hash_table_remove_all(pf->associations);
    g_hash_table_remove_all(pf->connections);
}

void port_forwarder_associate(PortForwarder* pf, uint16_t rport, uint16_t lport)
{
    VDAgentPortForwardListenMessage msg;

    if (g_hash_table_lookup(pf->associations, GUINT_TO_POINTER(rport))) {
        /* TODO: Error */
    } else {
        g_hash_table_insert(pf->associations, GUINT_TO_POINTER(rport), GUINT_TO_POINTER(lport));
        msg.port = rport;
        pf->send_command(pf->channel, VD_AGENT_PORT_FORWARD_LISTEN,
                         (const uint8_t *)&msg, sizeof(msg));
    }
}

static const size_t DATA_HEAD_SIZE = sizeof(VDAgentPortForwardDataMessage);
static const size_t BUFFER_SIZE = VD_AGENT_MAX_DATA_SIZE - sizeof(VDAgentPortForwardDataMessage);

static void connection_read_callback(GObject *source_object, GAsyncResult *res,
                                     gpointer user_data)
{
    Connection *conn = (Connection *)user_data;
    GBytes * buffer = g_input_stream_read_bytes_finish((GInputStream *)source_object, res, NULL);
    uint8_t msg_buffer[VD_AGENT_MAX_DATA_SIZE];
    VDAgentPortForwardDataMessage * msg = (VDAgentPortForwardDataMessage *)msg_buffer;

    SPICE_DEBUG("This is read callback for connection %p", conn);
    if (g_cancellable_is_cancelled(conn->cancelator)) {
        unref_connection(conn);
        return;
    }

    if (!buffer || g_bytes_get_size(buffer) == 0) {
        /* Error or connection closed by peer */
        close_connection(conn);
    } else {
        msg->id = conn->id;
        msg->size = g_bytes_get_size(buffer);
        memcpy(msg->data, g_bytes_get_data(buffer, NULL), msg->size);
        conn->pf->send_command(conn->pf->channel, VD_AGENT_PORT_FORWARD_DATA,
                               msg_buffer, DATA_HEAD_SIZE + msg->size);
        GInputStream *stream = (GInputStream *)source_object;
        SPICE_DEBUG("Programming read callback for connection %p with %d refs", conn, conn->refs);
        g_input_stream_read_bytes_async(stream, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                                        conn->cancelator, connection_read_callback, conn);
    }
    g_bytes_unref(buffer);
}

static void connection_connect_callback(GObject *source_object, GAsyncResult *res,
                                        gpointer user_data)
{
    Connection *conn = (Connection *)user_data;

    SPICE_DEBUG("This is connect callback for connection %p", conn);
    if (g_cancellable_is_cancelled(conn->cancelator)) {
        unref_connection(conn);
        return;
    }

    conn->conn = g_socket_client_connect_to_host_finish((GSocketClient *)source_object, res, NULL);
    if (!conn->conn) {
        /* Error */
        close_connection(conn);
    } else {
        GInputStream *stream = g_io_stream_get_input_stream((GIOStream *)conn->conn);
        SPICE_DEBUG("Programming read callback for connection %p with %d refs", conn, conn->refs);
        g_input_stream_read_bytes_async(stream, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                                        conn->cancelator, connection_read_callback, conn);
    }
}

static void handle_connect(PortForwarder *pf, VDAgentPortForwardConnectMessage *msg)
{
    gpointer id = GUINT_TO_POINTER(msg->id), rport = GUINT_TO_POINTER(msg->port);
    uint16_t lport;
    Connection *conn = g_hash_table_lookup(pf->connections, id);
    if (conn) {
        /* TODO: Error */
        g_hash_table_remove(pf->connections, id);
    }

    lport = GPOINTER_TO_UINT(g_hash_table_lookup(pf->associations, rport));
    SPICE_DEBUG("Connection %d on remote port %d -> local port %d", msg->id, msg->port, lport);
    if (lport) {
        conn = new_connection(pf, msg->id);
        if (conn) {
            g_hash_table_insert(pf->connections, id, conn);
            conn->refs++;
            SPICE_DEBUG("Programming connect callback for connection %p with %d refs", conn, conn->refs);
            g_socket_client_connect_to_host_async(conn->socket, "localhost",
                                                  lport, conn->cancelator,
                                                  connection_connect_callback, conn);
        } else {
            /* TODO: Error, close connection in agent */
            close_agent_connection(pf, msg->id);
        }
    } else {
        /* TODO: Error, close connection in agent */
        close_agent_connection(pf, msg->id);
    }
}

static void connection_write_callback(GObject *source_object, GAsyncResult *res,
                                      gpointer user_data)
{
    Connection *conn = (Connection *)user_data;
    GOutputStream *stream = (GOutputStream *)source_object;
    size_t num_written = g_output_stream_write_bytes_finish(stream, res, NULL);
    size_t to_write = g_bytes_get_size(conn->write_buffer);

    SPICE_DEBUG("This is write callback for connection %p", conn);
    if (g_cancellable_is_cancelled(conn->cancelator)) {
        unref_connection(conn);
        return;
    }

    if (num_written == 0) {
        /* Error or connection closed by peer */
        close_connection(conn);
    } else if (num_written < to_write) {
        GBytes *new_buffer = g_bytes_new_from_bytes(conn->write_buffer, num_written,
                                                    to_write - num_written);
        g_bytes_unref(conn->write_buffer);
        conn->write_buffer = new_buffer;
        SPICE_DEBUG("Programming write callback for connection %p with %d refs", conn, conn->refs);
        g_output_stream_write_bytes_async(stream, conn->write_buffer, G_PRIORITY_DEFAULT,
                                          conn->cancelator, connection_write_callback, conn);
    } else {
        unref_connection(conn);
    }
}

static void handle_data(PortForwarder *pf, VDAgentPortForwardDataMessage *msg)
{
    Connection *conn = g_hash_table_lookup(pf->connections, GUINT_TO_POINTER(msg->id));
    if (conn) {
        conn->refs++;
        SPICE_DEBUG("Programming write callback for connection %p with %d refs", conn, conn->refs);
        GOutputStream *stream = g_io_stream_get_output_stream((GIOStream *)conn->conn);
        conn->write_buffer = g_bytes_new(msg->data, msg->size);
        g_output_stream_write_bytes_async(stream, conn->write_buffer, G_PRIORITY_DEFAULT,
                                          conn->cancelator, connection_write_callback, conn);
    } else {
        /* TODO: Error, close connection in agent */
        close_agent_connection(pf, msg->id);
    }
}

static void handle_close(PortForwarder *pf, VDAgentPortForwardCloseMessage *msg)
{
    Connection *conn = g_hash_table_lookup(pf->connections, GUINT_TO_POINTER(msg->id));
    if (conn) {
        close_connection(conn);
    } else {
        /* TODO: Error, close connection in agent */
        close_agent_connection(pf, msg->id);
    }
}

void port_forwarder_handle_message(PortForwarder* pf, uint32_t command, gpointer msg)
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
        default:
            break;
    }
}
