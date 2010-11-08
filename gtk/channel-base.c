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

    g_debug("%s: channel %s -- %s%s #%u%s%.*s", __FUNCTION__,
            c->name, severity, visibility, notify->what,
            message_str ? ": " : "", notify->message_len,
            message_str ? message_str : "");
}
