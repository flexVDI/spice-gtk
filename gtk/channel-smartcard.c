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
#include <vreader.h>

#include "spice-client.h"
#include "spice-common.h"

#include "spice-channel-priv.h"
#include "smartcard-manager.h"

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
    /* track readers that have been added but for which we didn't receive
     * an ack from the spice server yet. We rely on the fact that the
     * readers in this list are ordered by the time we sent the request to
     * the server. When we get an ack from the server for a reader addition,
     * we can pop the 1st entry to get the reader the ack corresponds to. */
    GList *pending_reader_additions;

    /* used to removals of readers that were not ack'ed yet by the spice
     * server */
    GHashTable *pending_reader_removals;

    /* used to track card insertions on readers that were not ack'ed yet
     * by the spice server */
    GHashTable *pending_card_insertions;
};

G_DEFINE_TYPE(SpiceSmartCardChannel, spice_smartcard_channel, SPICE_TYPE_CHANNEL)

enum {

    SPICE_SMARTCARD_LAST_SIGNAL,
};

static void spice_smartcard_handle_msg(SpiceChannel *channel, spice_msg_in *msg);

/* ------------------------------------------------------------------ */

static void reader_added_cb(SpiceSmartCardManager *manager, VReader *reader,
                            gpointer user_data);
static void reader_removed_cb(SpiceSmartCardManager *manager, VReader *reader,
                              gpointer user_data);
static void card_inserted_cb(SpiceSmartCardManager *manager, VReader *reader,
                             gpointer user_data);
static void card_removed_cb(SpiceSmartCardManager *manager, VReader *reader,
                            gpointer user_data);

static void spice_smartcard_channel_init(SpiceSmartCardChannel *channel)
{
    SpiceSmartCardManager *manager;
    spice_smartcard_channel *priv;

    channel->priv = SPICE_SMARTCARD_CHANNEL_GET_PRIVATE(channel);
    priv = channel->priv;
    priv->pending_card_insertions =
        g_hash_table_new_full(g_direct_hash, g_direct_equal,
                              (GDestroyNotify)vreader_free, NULL);
    priv->pending_reader_removals =
         g_hash_table_new_full(g_direct_hash, g_direct_equal,
                               (GDestroyNotify)vreader_free, NULL);

    manager = spice_smartcard_manager_get();
    g_signal_connect(G_OBJECT(manager), "reader-added",
                     (GCallback)reader_added_cb, channel);
    g_signal_connect(G_OBJECT(manager), "reader-removed",
                     (GCallback)reader_removed_cb, channel);
    g_signal_connect(G_OBJECT(manager), "card-inserted",
                     (GCallback)card_inserted_cb, channel);
    g_signal_connect(G_OBJECT(manager), "card-removed",
                     (GCallback)card_removed_cb, channel);
}

static void spice_smartcard_channel_finalize(GObject *obj)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(obj);

    if (channel->priv->pending_card_insertions != NULL) {
        g_hash_table_destroy(channel->priv->pending_card_insertions);
        channel->priv->pending_card_insertions = NULL;
    }

    if (channel->priv->pending_reader_removals != NULL) {
        g_hash_table_destroy(channel->priv->pending_reader_removals);
        channel->priv->pending_reader_removals = NULL;
    }

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

/* ------------------------------------------------------------------ */
/* private api                                                        */
static gboolean is_attached_to_server(VReader *reader)
{
    return (vreader_get_id(reader) != (vreader_id_t)-1);
}

G_GNUC_UNUSED static gboolean
spice_channel_has_pending_card_insertion(SpiceSmartCardChannel *channel,
                                         VReader *reader)
{
    return (g_hash_table_lookup(channel->priv->pending_card_insertions, reader) != NULL);
}

static void
spice_channel_queue_card_insertion(SpiceSmartCardChannel *channel,
                                   VReader *reader)
{
    vreader_reference(reader);
    g_hash_table_insert(channel->priv->pending_card_insertions,
                        reader, reader);
}

static void
spice_channel_drop_pending_card_insertion(SpiceSmartCardChannel *channel,
                                          VReader *reader)
{
    g_hash_table_remove(channel->priv->pending_card_insertions, reader);
}

G_GNUC_UNUSED static gboolean
spice_channel_has_pending_reader_removal(SpiceSmartCardChannel *channel,
                                         VReader *reader)
{
    return (g_hash_table_lookup(channel->priv->pending_reader_removals, reader) != NULL);
}

static void
spice_channel_queue_reader_removal(SpiceSmartCardChannel *channel,
                                   VReader *reader)
{
    vreader_reference(reader);
    g_hash_table_insert(channel->priv->pending_reader_removals,
                        reader, reader);
}

G_GNUC_UNUSED static void
spice_channel_drop_pending_reader_removal(SpiceSmartCardChannel *channel,
                                          VReader *reader)
{
    g_hash_table_remove(channel->priv->pending_reader_removals, reader);
}


static void reader_added_cb(SpiceSmartCardManager *manager, VReader *reader,
                            gpointer user_data)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(user_data);

    channel->priv->pending_reader_additions =
        g_list_append(channel->priv->pending_reader_additions, reader);
}

static void reader_removed_cb(SpiceSmartCardManager *manager, VReader *reader,
                              gpointer user_data)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(user_data);

    if (is_attached_to_server(reader)) {
    } else {
        spice_channel_queue_reader_removal(channel, reader);
    }
}

/* ------------------------------------------------------------------ */
/* callbacks                                                          */
static void card_inserted_cb(SpiceSmartCardManager *manager, VReader *reader,
                             gpointer user_data)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(user_data);

    if (is_attached_to_server(reader)) {
    } else {
        spice_channel_queue_card_insertion(channel, reader);
    }
}

static void card_removed_cb(SpiceSmartCardManager *manager, VReader *reader,
                            gpointer user_data)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(user_data);

    if (is_attached_to_server(reader)) {
    } else {
        /* this does nothing when reader has no card insertion pending */
        spice_channel_drop_pending_card_insertion(channel, reader);
    }
}

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
