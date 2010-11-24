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

#include "spice-marshal.h"

#define SPICE_RECORD_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_RECORD_CHANNEL, spice_record_channel))

struct spice_record_channel {
    int                         mode;
};

G_DEFINE_TYPE(SpiceRecordChannel, spice_record_channel, SPICE_TYPE_CHANNEL)

enum {
    SPICE_RECORD_START,
    SPICE_RECORD_STOP,

    SPICE_RECORD_LAST_SIGNAL,
};

static guint signals[SPICE_RECORD_LAST_SIGNAL];

static void spice_record_handle_msg(SpiceChannel *channel, spice_msg_in *msg);

/* ------------------------------------------------------------------ */

static void spice_record_channel_init(SpiceRecordChannel *channel)
{
    spice_record_channel *c;

    c = channel->priv = SPICE_RECORD_CHANNEL_GET_PRIVATE(channel);
    memset(c, 0, sizeof(*c));
}

static void spice_record_channel_finalize(GObject *obj)
{
    if (G_OBJECT_CLASS(spice_record_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_record_channel_parent_class)->finalize(obj);
}

static void spice_record_channel_class_init(SpiceRecordChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->finalize     = spice_record_channel_finalize;
    channel_class->handle_msg   = spice_record_handle_msg;

    signals[SPICE_RECORD_START] =
        g_signal_new("record-start",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceRecordChannelClass, spice_record_start),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__INT_INT_INT,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

    signals[SPICE_RECORD_STOP] =
        g_signal_new("record-stop",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceRecordChannelClass, spice_record_stop),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    g_type_class_add_private(klass, sizeof(spice_record_channel));
}

/* ------------------------------------------------------------------ */

static void record_handle_start(SpiceChannel *channel, spice_msg_in *in)
{
    spice_record_channel *c = SPICE_RECORD_CHANNEL(channel)->priv;
    SpiceMsgRecordStart *start = spice_msg_in_parsed(in);

    SPICE_DEBUG("%s: fmt %d channels %d freq %d", __FUNCTION__,
            start->format, start->channels, start->frequency);

    switch (c->mode) {
    case SPICE_AUDIO_DATA_MODE_RAW:
        g_signal_emit(channel, signals[SPICE_RECORD_START], 0,
                      start->format, start->channels, start->frequency);
        break;
    default:
        g_warning("%s: unhandled mode", __FUNCTION__);
        break;
    }
}

static void record_handle_stop(SpiceChannel *channel, spice_msg_in *in)
{
    g_signal_emit(channel, signals[SPICE_RECORD_STOP], 0);
}

static spice_msg_handler record_handlers[] = {
    [ SPICE_MSG_SET_ACK ]                  = spice_channel_handle_set_ack,
    [ SPICE_MSG_PING ]                     = spice_channel_handle_ping,
    [ SPICE_MSG_NOTIFY ]                   = spice_channel_handle_notify,
    [ SPICE_MSG_DISCONNECTING ]            = spice_channel_handle_disconnect,
    [ SPICE_MSG_WAIT_FOR_CHANNELS ]        = spice_channel_handle_wait_for_channels,
    [ SPICE_MSG_MIGRATE ]                  = spice_channel_handle_migrate,

    [ SPICE_MSG_RECORD_START ]             = record_handle_start,
    [ SPICE_MSG_RECORD_STOP ]              = record_handle_stop,
};

static void spice_record_handle_msg(SpiceChannel *channel, spice_msg_in *msg)
{
    int type = spice_msg_in_type(msg);
    g_return_if_fail(type < SPICE_N_ELEMENTS(record_handlers));
    g_return_if_fail(record_handlers[type] != NULL);
    record_handlers[type](channel, msg);
}
