/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2011 Red Hat, Inc.

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
#include "spice-channel-cache.h"
#include "spice-marshal.h"

/**
 * SECTION:channel-smartcard
 * @short_description: smartcard authentication
 * @title: SmartCard Channel
 * @section_id:
 * @see_also: #SpiceChannel
 * @stability: In Development
 * @include: channel-smartcard.h
 *
 * TODO
 */

#define SPICE_SMARTCARD_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_SMARTCARD_CHANNEL, spice_smartcard_channel))

struct spice_smartcard_channel {
};

G_DEFINE_TYPE(SpiceSmartCardChannel, spice_smartcard_channel, SPICE_TYPE_CHANNEL)

enum {

    SPICE_SMARTCARD_LAST_SIGNAL,
};

static void spice_smartcard_handle_msg(SpiceChannel *channel, spice_msg_in *msg);

/* ------------------------------------------------------------------ */

static void spice_smartcard_channel_init(SpiceSmartCardChannel *channel)
{
    channel->priv = SPICE_SMARTCARD_CHANNEL_GET_PRIVATE(channel);
}

static void spice_smartcard_channel_finalize(GObject *obj)
{
    if (G_OBJECT_CLASS(spice_smartcard_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_smartcard_channel_parent_class)->finalize(obj);
}

static void spice_smartcard_channel_class_init(SpiceSmartCardChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->finalize     = spice_smartcard_channel_finalize;
    channel_class->handle_msg   = spice_smartcard_handle_msg;

    g_type_class_add_private(klass, sizeof(spice_smartcard_channel));
}

static const spice_msg_handler smartcard_handlers[] = {
    [ SPICE_MSG_SMARTCARD_DATA ]           = NULL,
};

/* coroutine context */
static void spice_smartcard_handle_msg(SpiceChannel *channel, spice_msg_in *msg)
{
    int type = spice_msg_in_type(msg);
    SpiceChannelClass *parent_class;

    g_return_if_fail(type < SPICE_N_ELEMENTS(smartcard_handlers));

    parent_class = SPICE_CHANNEL_CLASS(spice_smartcard_channel_parent_class);

    if (smartcard_handlers[type] != NULL)
        smartcard_handlers[type](channel, msg);
    else if (parent_class->handle_msg)
        parent_class->handle_msg(channel, msg);
    else
        g_return_if_reached();
}
