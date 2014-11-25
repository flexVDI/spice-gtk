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

static void send_command(PortForwarder *pf, uint32_t command,
                         const uint8_t *data, uint32_t data_size)
{
    SPICE_DEBUG("Sending command %d with %d bytes", (int)command, (int)data_size);
    pf->send_command(pf->channel, command, data, data_size);
}

typedef struct Connection {
    GSocketClient *socket;
    GSocketConnection *conn;
    GCancellable *cancelator;
    GQueue *write_buffer;
    gboolean connecting;
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
        conn->connecting = TRUE;
        conn->write_buffer = g_queue_new();
    }
    return conn;
}

static void unref_connection(gpointer value)
{
    Connection * conn = (Connection *) value;
    if (!--conn->refs) {
        SPICE_DEBUG("Closing connection %d", conn->id);
        g_object_unref(conn->cancelator);
        if (conn->conn) {
            g_io_stream_close((GIOStream *)conn->conn, NULL, NULL);
        }
        if (conn->socket) {
            g_object_unref(conn->socket);
        }
        g_queue_free_full(conn->write_buffer, (GDestroyNotify)g_bytes_unref);
        g_free(conn);
    }
}

static void close_agent_connection(PortForwarder *pf, int id)
{
    VDAgentPortForwardCloseMessage closeMsg;
    closeMsg.id = id;
    send_command(pf, VD_AGENT_PORT_FORWARD_CLOSE, (const uint8_t *)&closeMsg, sizeof(closeMsg));
}

static void close_connection_no_notify(Connection * conn)
{
    SPICE_DEBUG("Start closing connection %d", conn->id);
    if (!g_cancellable_is_cancelled(conn->cancelator))
        g_cancellable_cancel(conn->cancelator);
    g_hash_table_remove(conn->pf->connections, GUINT_TO_POINTER(conn->id));
    unref_connection(conn);
}

static void close_connection(Connection * conn)
{
    close_agent_connection(conn->pf, conn->id);
    close_connection_no_notify(conn);
}

PortForwarder *new_port_forwarder(void *channel, port_forwarder_send_command_cb cb)
{
    PortForwarder *pf = g_malloc(sizeof(PortForwarder));
    if (pf) {
        SPICE_DEBUG("Created new port forwarder");
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

gboolean port_forwarder_associate(PortForwarder* pf, uint16_t rport, uint16_t lport)
{
    VDAgentPortForwardListenMessage msg;

    SPICE_DEBUG("Associate remote port %d -> local port %d", rport, lport);
    if (g_hash_table_lookup(pf->associations, GUINT_TO_POINTER(rport))) {
        port_forwarder_disassociate(pf, rport);
    }
    g_hash_table_insert(pf->associations, GUINT_TO_POINTER(rport), GUINT_TO_POINTER(lport));
    msg.port = rport;
    send_command(pf, VD_AGENT_PORT_FORWARD_LISTEN, (const uint8_t *)&msg, sizeof(msg));
    return TRUE;
}

gboolean port_forwarder_disassociate(PortForwarder *pf, uint16_t rport) {
    VDAgentPortForwardShutdownMessage msg;

    if (!g_hash_table_remove(pf->associations, GUINT_TO_POINTER(rport))) {
        g_warning("Remote port %d is not associated with a local port.", rport);
        return FALSE;
    } else {
        SPICE_DEBUG("Disassociate remote port %d", rport);
        msg.port = rport;
        send_command(pf, VD_AGENT_PORT_FORWARD_SHUTDOWN, (const uint8_t *)&msg, sizeof(msg));
        return TRUE;
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

    if (g_cancellable_is_cancelled(conn->cancelator)) {
        unref_connection(conn);
        return;
    }

    if (!buffer || g_bytes_get_size(buffer) == 0) {
        /* Error or connection closed by peer */
        SPICE_DEBUG("Read error or connection %d reset by peer", conn->id);
        close_connection(conn);
    } else {
        msg->id = conn->id;
        msg->size = g_bytes_get_size(buffer);
        SPICE_DEBUG("Read %d bytes on connection %d", (int)msg->size, conn->id);
        memcpy(msg->data, g_bytes_get_data(buffer, NULL), msg->size);
        send_command(conn->pf, VD_AGENT_PORT_FORWARD_DATA,
                     msg_buffer, DATA_HEAD_SIZE + msg->size);
        GInputStream *stream = (GInputStream *)source_object;
        g_input_stream_read_bytes_async(stream, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                                        conn->cancelator, connection_read_callback, conn);
    }
    g_bytes_unref(buffer);
}

static void connection_write_callback(GObject *source_object, GAsyncResult *res,
                                      gpointer user_data)
{
    Connection *conn = (Connection *)user_data;
    GOutputStream *stream = (GOutputStream *)source_object;
    GBytes * bytes = (GBytes *)g_queue_pop_head(conn->write_buffer);
    size_t num_written = g_output_stream_write_bytes_finish(stream, res, NULL);
    size_t to_write = g_bytes_get_size(bytes);

    if (g_cancellable_is_cancelled(conn->cancelator)) {
        unref_connection(conn);
        return;
    }

    SPICE_DEBUG("Written %d bytes on connection %d", (int)num_written, conn->id);
    if (num_written == 0) {
        /* Error or connection closed by peer */
        close_connection(conn);
    } else if (num_written < to_write) {
        SPICE_DEBUG("Still %d bytes to go on connection %d", (int)(to_write - num_written), conn->id);
        GBytes *new_bytes = g_bytes_new_from_bytes(bytes, num_written, to_write - num_written);
        g_bytes_unref(bytes);
        g_queue_push_head(conn->write_buffer, new_bytes);
    }

    if(!g_queue_is_empty(conn->write_buffer)) {
        g_output_stream_write_bytes_async(stream, g_queue_peek_head(conn->write_buffer),
                                          G_PRIORITY_DEFAULT, conn->cancelator,
                                          connection_write_callback, conn);
    } else {
        unref_connection(conn);
    }
}

static void connection_connect_callback(GObject *source_object, GAsyncResult *res,
                                        gpointer user_data)
{
    Connection *conn = (Connection *)user_data;

    if (g_cancellable_is_cancelled(conn->cancelator)) {
        unref_connection(conn);
        return;
    }

    conn->conn = g_socket_client_connect_to_host_finish((GSocketClient *)source_object, res, NULL);
    if (!conn->conn) {
        /* Error */
        SPICE_DEBUG("Connection %d could not connect", conn->id);
        close_connection(conn);
    } else {
        conn->connecting = FALSE;
        GInputStream *stream = g_io_stream_get_input_stream((GIOStream *)conn->conn);
        g_input_stream_read_bytes_async(stream, BUFFER_SIZE, G_PRIORITY_DEFAULT,
                                        conn->cancelator, connection_read_callback, conn);
        if(!g_queue_is_empty(conn->write_buffer)) {
            conn->refs++;
            GOutputStream *stream = g_io_stream_get_output_stream((GIOStream *)conn->conn);
            g_output_stream_write_bytes_async(stream, g_queue_peek_head(conn->write_buffer),
                                            G_PRIORITY_DEFAULT, conn->cancelator,
                                            connection_write_callback, conn);
        }
    }
}

static void handle_connect(PortForwarder *pf, VDAgentPortForwardConnectMessage *msg)
{
    gpointer id = GUINT_TO_POINTER(msg->id), rport = GUINT_TO_POINTER(msg->port);
    uint16_t lport;
    Connection *conn = g_hash_table_lookup(pf->connections, id);
    if (conn) {
        g_warning("Connection %d already exists.", msg->id);
        close_connection_no_notify(conn);
    }

    lport = GPOINTER_TO_UINT(g_hash_table_lookup(pf->associations, rport));
    SPICE_DEBUG("Connection command, id %d on remote port %d -> local port %d", msg->id, msg->port, lport);
    if (lport) {
        conn = new_connection(pf, msg->id);
        if (conn) {
            g_hash_table_insert(pf->connections, id, conn);
            conn->refs++;
            g_socket_client_connect_to_host_async(conn->socket, "localhost",
                                                  lport, conn->cancelator,
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
    if (conn) {
        SPICE_DEBUG("Data command, %d bytes on connection %d", (int)msg->size, conn->id);
        g_queue_push_tail(conn->write_buffer, g_bytes_new(msg->data, msg->size));
        if (conn->connecting) {
            SPICE_DEBUG("Deferring %d bytes on connection %d", (int)msg->size, conn->id);
        } else {
            conn->refs++;
            GOutputStream *stream = g_io_stream_get_output_stream((GIOStream *)conn->conn);
            g_output_stream_write_bytes_async(stream, g_queue_peek_head(conn->write_buffer),
                                              G_PRIORITY_DEFAULT, conn->cancelator,
                                              connection_write_callback, conn);
        }
    } else {
        /* Error, close connection in agent */
        g_warning("Connection %d does not exists.", msg->id);
        close_agent_connection(pf, msg->id);
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
