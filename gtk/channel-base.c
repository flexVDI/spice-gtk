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

void spice_channel_handle_set_ack(SpiceChannel *channel, spice_msg_in *in)
{
    spice_channel *c = channel->priv;
    SpiceMsgSetAck* ack = spice_msg_in_parsed(in);
    spice_msg_out *out = spice_msg_out_new(channel, SPICE_MSGC_ACK_SYNC);
    SpiceMsgcAckSync sync = {
        .generation = ack->generation,
    };

    c->message_ack_window = c->message_ack_count = ack->window;
    c->marshallers->msgc_ack_sync(out->marshaller, &sync);
    spice_msg_out_send(out);
    spice_msg_out_unref(out);
}

void spice_channel_handle_ping(SpiceChannel *channel, spice_msg_in *in)
{
    spice_channel *c = channel->priv;
    SpiceMsgPing *ping = spice_msg_in_parsed(in);
    spice_msg_out *pong = spice_msg_out_new(channel, SPICE_MSGC_PONG);

    c->marshallers->msgc_pong(pong->marshaller, ping);
    spice_msg_out_send(pong);
    spice_msg_out_unref(pong);
}

void spice_channel_handle_notify(SpiceChannel *channel, spice_msg_in *in)
{
    spice_channel *c = channel->priv;
    static const char* severity_strings[] = {"info", "warn", "error"};
    static const char* visibility_strings[] = {"!", "!!", "!!!"};

    SpiceMsgNotify *notify = spice_msg_in_parsed(in);
    const char *severity   = "?";
    const char *visibility = "?";
    const char *message_str = NULL;

    if (notify->severity <= SPICE_NOTIFY_SEVERITY_ERROR) {
        severity = severity_strings[notify->severity];
    }
    if (notify->visibilty <= SPICE_NOTIFY_VISIBILITY_HIGH) {
        visibility = visibility_strings[notify->visibilty];
    }

    if (notify->message_len &&
        notify->message_len <= in->dpos - sizeof(*notify)) {
        message_str = (char*)notify->message;
    }

    SPICE_DEBUG("%s: channel %s -- %s%s #%u%s%.*s", __FUNCTION__,
            c->name, severity, visibility, notify->what,
            message_str ? ": " : "", notify->message_len,
            message_str ? message_str : "");
}

void spice_channel_handle_disconnect(SpiceChannel *channel, spice_msg_in *in)
{
    spice_channel *c = channel->priv;
    SpiceMsgDisconnect *disconnect = spice_msg_in_parsed(in);

    SPICE_DEBUG("%s: ts: %" PRIu64", reason: %u", __FUNCTION__,
                disconnect->time_stamp, disconnect->reason);
}
