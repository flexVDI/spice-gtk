/**
 * Copyright Flexible Software Solutions S.L. 2014
 **/

#ifndef __PORT_FORWARD_H
#define __PORT_FORWARD_H

#include <glib.h>
#include <stdint.h>

typedef struct PortForwarder PortForwarder;

/*
 * Callback to send commands to the vdagent.
 */
typedef void (*port_forwarder_send_command_cb)(
    void *channel, uint32_t command,
    const uint8_t *data, uint32_t data_size);

PortForwarder *new_port_forwarder(void *channel, port_forwarder_send_command_cb cb);

void delete_port_forwarder(PortForwarder *pf);

void port_forwarder_agent_disconnected(PortForwarder *pf);

/*
 * Associate a remote port with a local port.
 */
gboolean port_forwarder_associate(PortForwarder *pf, uint16_t rport, uint16_t lport);

/*
 * Disassociate a remote port.
 */
gboolean port_forwarder_disassociate(PortForwarder *pf, uint16_t rport);

/*
 * Handle a message received from the agent.
 */
void port_forwarder_handle_message(PortForwarder *pf, uint32_t command, gpointer msg);

#endif /* __PORT_FORWARD_H */
