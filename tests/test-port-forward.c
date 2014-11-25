/**
 * Copyright Flexible Software Solutions S.L. 2014
 **/

#include <glib.h>
#include <gio/gio.h>
#include <stdint.h>
#include <string.h>
#include <spice/vd_agent.h>
#include <time.h>
#include "../gtk/spice-util.h"
#include "port-forward.h"

const uint16_t rport = 80, lport = 8080;
typedef struct TestFixture {
    PortForwarder * pf;
    GSocketListener *listener;
} TestFixture;

uint32_t last_command;
uint8_t *last_data = NULL;
uint32_t last_data_size;
void test_send_command(void *channel, uint32_t command,
                       const uint8_t *data, uint32_t data_size)
{
    last_command = command;
    if (last_data != NULL) {
        g_free(last_data);
    }
    last_data_size = data_size;
    last_data = g_memdup(data, data_size);
}

void loop_for_2_seconds(void ** ended) {
    if (ended == NULL) ended = (void **)&last_data;
    time_t start = time(NULL);
    while (!*ended && (time(NULL) - start) < 2) {
        g_main_context_iteration(NULL, FALSE);
    }
}

void setup(TestFixture * fixture, gconstpointer user_data)
{
    fixture->pf = new_port_forwarder(NULL, test_send_command);
    fixture->listener = g_socket_listener_new();
}

void teardown(TestFixture * fixture, gconstpointer user_data)
{
    g_socket_listener_close(fixture->listener);
    port_forwarder_agent_disconnected(fixture->pf);
    while (g_main_context_pending(NULL))
        g_main_context_iteration(NULL, TRUE);
    delete_port_forwarder(fixture->pf);
}

void test_create_port_forwarder(TestFixture * fixture, gconstpointer user_data)
{
    VDAgentPortForwardShutdownMessage *msg;

    g_assert(fixture->pf);
}

static void test_accept_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GSocketConnection * conn = g_socket_listener_accept_finish((GSocketListener *)source_object,
                                                               res, NULL, NULL);
    g_assert(conn);
    *((gpointer *)user_data) = conn;
}

void test_listen_to_port(TestFixture * fixture, gconstpointer user_data)
{
    VDAgentPortForwardListenMessage *msgListen;
    VDAgentPortForwardConnectMessage msgConnect = { .port = rport, .id = 1 };

    g_assert(g_socket_listener_add_inet_port(fixture->listener, lport, NULL, NULL));
    port_forwarder_associate(fixture->pf, rport, lport);
    g_assert_cmpuint(last_command, ==, VD_AGENT_PORT_FORWARD_LISTEN);
    g_assert_cmpuint(last_data_size, ==, sizeof(VDAgentPortForwardListenMessage));
    msgListen = (VDAgentPortForwardListenMessage *)last_data;
    g_assert(msgListen != NULL);
    g_assert_cmpuint(msgListen->port, ==, rport);
    port_forwarder_handle_message(fixture->pf, VD_AGENT_PORT_FORWARD_CONNECT,
                                  (gpointer)&msgConnect);
    gpointer ended = NULL;
    g_socket_listener_accept_async(fixture->listener, NULL, test_accept_callback, &ended);
    loop_for_2_seconds(&ended);
}

/* Test a connect message to a port that is not associated */

/* Test a connect message to 0 port */

/* Test a connect message with an id that already exists */

void test_direct_close(TestFixture * fixture, gconstpointer user_data)
{
    VDAgentPortForwardConnectMessage msgConnect = { .port = rport, .id = 1 };
    VDAgentPortForwardCloseMessage *msgClose;

    g_assert(g_socket_listener_add_inet_port(fixture->listener, lport, NULL, NULL));
    port_forwarder_associate(fixture->pf, rport, lport);
    port_forwarder_handle_message(fixture->pf, VD_AGENT_PORT_FORWARD_CONNECT,
                                  (gpointer)&msgConnect);
    gpointer ended = NULL;
    g_socket_listener_accept_async(fixture->listener, NULL, test_accept_callback, &ended);
    loop_for_2_seconds(&ended);
    GSocketConnection * conn = (GSocketConnection *)ended;
    g_io_stream_close((GIOStream *)conn, NULL, NULL);
    last_data = NULL;
    loop_for_2_seconds(NULL);
    g_assert_cmpuint(last_command, ==, VD_AGENT_PORT_FORWARD_CLOSE);
    g_assert_cmpuint(last_data_size, ==, sizeof(VDAgentPortForwardCloseMessage));
    msgClose = (VDAgentPortForwardCloseMessage *)last_data;
    g_assert(msgClose != NULL);
    g_assert_cmpuint(msgClose->id, ==, 1);
}

void test_send_data(TestFixture * fixture, gconstpointer user_data)
{
    VDAgentPortForwardConnectMessage msgConnect = { .port = rport, .id = 1 };
    VDAgentPortForwardDataMessage *msgData;

    g_assert(g_socket_listener_add_inet_port(fixture->listener, lport, NULL, NULL));
    port_forwarder_associate(fixture->pf, rport, lport);
    port_forwarder_handle_message(fixture->pf, VD_AGENT_PORT_FORWARD_CONNECT,
                                  (gpointer)&msgConnect);
    gpointer ended = NULL;
    g_socket_listener_accept_async(fixture->listener, NULL, test_accept_callback, &ended);
    loop_for_2_seconds(&ended);
    GSocketConnection * conn = (GSocketConnection *)ended;
    GOutputStream * ostream = g_io_stream_get_output_stream((GIOStream *)conn);
    g_output_stream_write(ostream, "foobar", 7, NULL, NULL);
    last_data = NULL;
    loop_for_2_seconds(NULL);
    g_assert_cmpuint(last_command, ==, VD_AGENT_PORT_FORWARD_DATA);
    g_assert_cmpuint(last_data_size, ==, sizeof(VDAgentPortForwardDataMessage) + 7);
    msgData = (VDAgentPortForwardDataMessage *)last_data;
    g_assert(msgData != NULL);
    g_assert_cmpuint(msgData->id, ==, 1);
    g_assert_cmpuint(msgData->size, ==, 7);
    g_assert_cmpstr(msgData->data, ==, "foobar");
}

static void test_read_callback(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GBytes * buffer = g_input_stream_read_bytes_finish((GInputStream *)source_object, res, NULL);
    g_assert(buffer);
    *((gpointer *)user_data) = buffer;
}

void test_receive_data(TestFixture * fixture, gconstpointer user_data)
{
    VDAgentPortForwardConnectMessage msgConnect = { .port = rport, .id = 1 };
    uint8_t msg_buffer[VD_AGENT_MAX_DATA_SIZE];
    VDAgentPortForwardDataMessage * msgData = (VDAgentPortForwardDataMessage *)msg_buffer;

    g_assert(g_socket_listener_add_inet_port(fixture->listener, lport, NULL, NULL));
    port_forwarder_associate(fixture->pf, rport, lport);
    port_forwarder_handle_message(fixture->pf, VD_AGENT_PORT_FORWARD_CONNECT,
                                  (gpointer)&msgConnect);
    gpointer ended = NULL;
    g_socket_listener_accept_async(fixture->listener, NULL, test_accept_callback, &ended);
    loop_for_2_seconds(&ended);
    GSocketConnection * conn = (GSocketConnection *)ended;
    msgData->id = 1;
    msgData->size = 7;
    memcpy(msgData->data, "foobar", 7);
    size_t size = sizeof(VDAgentPortForwardDataMessage) + 7;
    port_forwarder_handle_message(fixture->pf, VD_AGENT_PORT_FORWARD_DATA, (gpointer)msgData);
    GInputStream *stream = g_io_stream_get_input_stream((GIOStream *)conn);
    g_input_stream_read_bytes_async(stream, size, G_PRIORITY_DEFAULT,
                                    NULL, test_read_callback, &ended);
    ended = NULL;
    loop_for_2_seconds(&ended);
    GBytes * data = (GBytes *)ended;
    g_assert_cmpuint(g_bytes_get_size(data), ==, 7);
    g_assert_cmpstr(g_bytes_get_data(data, NULL), ==, "foobar");
}

void test_agent_close(TestFixture * fixture, gconstpointer user_data)
{
    VDAgentPortForwardConnectMessage msgConnect = { .port = rport, .id = 1 };
    uint8_t msg_buffer[VD_AGENT_MAX_DATA_SIZE];
    VDAgentPortForwardCloseMessage msgClose = { .id = 1 };

    g_assert(g_socket_listener_add_inet_port(fixture->listener, lport, NULL, NULL));
    port_forwarder_associate(fixture->pf, rport, lport);
    port_forwarder_handle_message(fixture->pf, VD_AGENT_PORT_FORWARD_CONNECT,
                                  (gpointer)&msgConnect);
    gpointer ended = NULL;
    g_socket_listener_accept_async(fixture->listener, NULL, test_accept_callback, &ended);
    loop_for_2_seconds(&ended);
    port_forwarder_handle_message(fixture->pf, VD_AGENT_PORT_FORWARD_CLOSE, (gpointer)&msgClose);
    GSocketConnection * conn = (GSocketConnection *)ended;
    GInputStream *stream = g_io_stream_get_input_stream((GIOStream *)conn);
    g_input_stream_read_bytes_async(stream, 1, G_PRIORITY_DEFAULT,
                                    NULL, test_read_callback, &ended);
    ended = NULL;
    loop_for_2_seconds(&ended);
    GBytes * data = (GBytes *)ended;
    g_assert_cmpuint(g_bytes_get_size(data), ==, 0);
}

#define TEST(x) \
    g_test_add("/port-forward/" G_STRINGIFY(x), TestFixture, NULL, setup, x, teardown)

int main(int argc, char* argv[])
{
  g_test_init(&argc, &argv, NULL);
  spice_util_set_debug(TRUE);

  TEST(test_create_port_forwarder);
  TEST(test_listen_to_port);
  TEST(test_direct_close);
  TEST(test_send_data);
  TEST(test_receive_data);
  TEST(test_agent_close);

  return g_test_run();
}
