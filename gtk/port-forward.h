/**
 * Copyright Flexible Software Solutions S.L. 2014
 **/

#ifndef __PORT_FORWARD_H
#define __PORT_FORWARD_H

#include <glib.h>

typedef struct PortForwarder PortForwarder;

/*
 * Callback to send commands to the vdagent.
 */
typedef void (*port_forwarder_send_command_cb)(
    void *channel, guint32 command,
    const guint8 *data, guint32 data_size);

PortForwarder *new_port_forwarder(void *channel, port_forwarder_send_command_cb cb);

void delete_port_forwarder(PortForwarder *pf);

void port_forwarder_agent_disconnected(PortForwarder *pf);

/*
 * Associate a remote port with a local port.
 */
gboolean port_forwarder_associate(PortForwarder *pf, const gchar *bind_address,
                                  guint16 rport, const gchar *host, guint16 lport);

/*
 * Disassociate a remote port.
 */
gboolean port_forwarder_disassociate(PortForwarder *pf, guint16 rport);

/*
 * Handle a message received from the agent.
 */
void port_forwarder_handle_message(PortForwarder *pf, guint32 command, gpointer msg);

#endif /* __PORT_FORWARD_H */
