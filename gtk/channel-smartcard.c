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

struct _SpiceSmartCardChannelMessage {
    VSCMsgType message_type;
    spice_msg_out *message;
};
typedef struct _SpiceSmartCardChannelMessage SpiceSmartCardChannelMessage;


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

    /* next commands to be sent to the spice server. This is needed since
     * we have to wait for a command answer before sending the next one
     */
    GQueue *message_queue;

    /* message that is currently being processed by the spice server (ie last
     * message that was sent to the server)
     */
    SpiceSmartCardChannelMessage *in_flight_message;
};

G_DEFINE_TYPE(SpiceSmartCardChannel, spice_smartcard_channel, SPICE_TYPE_CHANNEL)

enum {

    SPICE_SMARTCARD_LAST_SIGNAL,
};

static void spice_smartcard_handle_msg(SpiceChannel *channel, spice_msg_in *msg);
static void spice_smartcard_channel_up(SpiceChannel *channel);
static void handle_smartcard_msg(SpiceChannel *channel, spice_msg_in *in);
static void smartcard_message_free(SpiceSmartCardChannelMessage *message);

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
    priv->message_queue = g_queue_new();

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
    if (channel->priv->message_queue != NULL) {
        g_queue_foreach(channel->priv->message_queue,
                        (GFunc)smartcard_message_free, NULL);
        g_queue_free(channel->priv->message_queue);
        channel->priv->message_queue = NULL;
    }
    if (channel->priv->in_flight_message != NULL) {
        smartcard_message_free(channel->priv->in_flight_message);
        channel->priv->in_flight_message = NULL;
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
    channel_class->channel_up   = spice_smartcard_channel_up;

    g_type_class_add_private(klass, sizeof(spice_smartcard_channel));
}

static const spice_msg_handler smartcard_handlers[] = {
    [ SPICE_MSG_SMARTCARD_DATA ]           = handle_smartcard_msg,
};

/* ------------------------------------------------------------------ */
/* private api                                                        */
static gboolean is_attached_to_server(VReader *reader)
{
    return (vreader_get_id(reader) != (vreader_id_t)-1);
}

static gboolean
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

static gboolean
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

static void
spice_channel_drop_pending_reader_removal(SpiceSmartCardChannel *channel,
                                          VReader *reader)
{
    g_hash_table_remove(channel->priv->pending_reader_removals, reader);
}

static SpiceSmartCardChannelMessage *
smartcard_message_new(VSCMsgType msg_type, spice_msg_out *msg_out)
{
    SpiceSmartCardChannelMessage *message;

    message = g_slice_new0(SpiceSmartCardChannelMessage);
    message->message = msg_out;
    message->message_type = msg_type;

    return message;
}

static void
smartcard_message_free(SpiceSmartCardChannelMessage *message)
{
    spice_msg_out_unref(message->message);
    g_slice_free(SpiceSmartCardChannelMessage, message);
}

/* Indicates that handling of the message that is currently in flight has
 * been completed. If needed, sends the next queued command to the server. */
static void
smartcard_message_complete_in_flight(SpiceSmartCardChannel *channel)
{
    if (channel->priv->in_flight_message == NULL) {
        g_assert(g_queue_is_empty(channel->priv->message_queue));
        return;
    }

    smartcard_message_free(channel->priv->in_flight_message);
    channel->priv->in_flight_message = g_queue_pop_head(channel->priv->message_queue);
    if (channel->priv->in_flight_message != NULL)
        spice_msg_out_send(channel->priv->in_flight_message->message);
}

static void smartcard_message_send(SpiceSmartCardChannel *channel,
                                   VSCMsgType msg_type,
                                   spice_msg_out *msg_out, gboolean queue)
{
    SpiceSmartCardChannelMessage *message;

    if (!queue) {
        spice_msg_out_send(msg_out);
        spice_msg_out_unref(msg_out);
        return;
    }

    message = smartcard_message_new(msg_type, msg_out);
    if (channel->priv->in_flight_message == NULL) {
        g_assert(g_queue_is_empty(channel->priv->message_queue));
        channel->priv->in_flight_message = message;
        spice_msg_out_send(msg_out);
    } else {
        g_queue_push_tail(channel->priv->message_queue, message);
    }
}

static void
send_msg_generic_with_data(SpiceSmartCardChannel *channel, VReader *reader,
                           VSCMsgType msg_type,
                           const uint8_t *data, gsize data_len,
                           gboolean serialize_msg)
{
    spice_msg_out *msg_out;
    VSCMsgHeader header = {
        .type = msg_type,
        .length = data_len
    };

    if(vreader_get_id(reader) == -1)
        header.reader_id = VSCARD_UNDEFINED_READER_ID;
    else
        header.reader_id = vreader_get_id(reader);

    msg_out = spice_msg_out_new(SPICE_CHANNEL(channel),
                                SPICE_MSGC_SMARTCARD_DATA);
    msg_out->marshallers->msgc_smartcard_header(msg_out->marshaller, &header);
    if ((data != NULL) && (data_len != 0)) {
        spice_marshaller_add(msg_out->marshaller, data, data_len);
    }

    smartcard_message_send(channel, msg_type, msg_out, serialize_msg);
}

static void send_msg_generic(SpiceSmartCardChannel *channel, VReader *reader,
                             VSCMsgType msg_type)
{
    send_msg_generic_with_data(channel, reader, msg_type, NULL, 0, TRUE);
}

static void send_msg_atr(SpiceSmartCardChannel *channel, VReader *reader)
{
#define MAX_ATR_LEN 40 //this should be defined in libcacard
    uint8_t atr[MAX_ATR_LEN];
    int atr_len = MAX_ATR_LEN;

    g_assert(vreader_get_id(reader) != VSCARD_UNDEFINED_READER_ID);
    vreader_power_on(reader, atr, &atr_len);
    send_msg_generic_with_data(channel, reader, VSC_ATR, atr, atr_len, TRUE);
}

static void reader_added_cb(SpiceSmartCardManager *manager, VReader *reader,
                            gpointer user_data)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(user_data);
    const char *reader_name = vreader_get_name(reader);

    channel->priv->pending_reader_additions =
        g_list_append(channel->priv->pending_reader_additions, reader);

    send_msg_generic_with_data(channel, reader, VSC_ReaderAdd,
                               (uint8_t*)reader_name, strlen(reader_name), TRUE);
}

static void reader_removed_cb(SpiceSmartCardManager *manager, VReader *reader,
                              gpointer user_data)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(user_data);

    if (is_attached_to_server(reader)) {
        send_msg_generic(channel, reader, VSC_ReaderRemove);
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
        send_msg_atr(channel, reader);
    } else {
        spice_channel_queue_card_insertion(channel, reader);
    }
}

static void card_removed_cb(SpiceSmartCardManager *manager, VReader *reader,
                            gpointer user_data)
{
    SpiceSmartCardChannel *channel = SPICE_SMARTCARD_CHANNEL(user_data);

    if (is_attached_to_server(reader)) {
        send_msg_generic(channel, reader, VSC_CardRemove);
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

static void spice_smartcard_channel_up(SpiceChannel *channel)
{
    SpiceSession *session;

    g_object_get(channel, "spice-session", &session, NULL);
    spice_smartcard_manager_init_libcacard(session);
    g_object_unref(session);
}

static void handle_smartcard_msg(SpiceChannel *channel, spice_msg_in *in)
{
    spice_smartcard_channel *priv = SPICE_SMARTCARD_CHANNEL_GET_PRIVATE(channel);
    SpiceMsgSmartcard *msg = spice_msg_in_parsed(in);
    VReader *reader;

    priv = SPICE_SMARTCARD_CHANNEL_GET_PRIVATE(channel);
    switch (msg->type) {
        case VSC_Error:
            g_return_if_fail(priv->in_flight_message != NULL);
            switch (priv->in_flight_message->message_type) {
                case VSC_ReaderAdd:
                    g_return_if_fail(priv->pending_reader_additions != NULL);
                    reader = priv->pending_reader_additions->data;
                    g_assert(reader != NULL);
                    g_assert(vreader_get_id(reader) == -1);
                    priv->pending_reader_additions =
                        g_list_delete_link(priv->pending_reader_additions,
                                           priv->pending_reader_additions);
                    vreader_set_id(reader, msg->reader_id);

                    if (spice_channel_has_pending_card_insertion(SPICE_SMARTCARD_CHANNEL(channel), reader)) {
                        send_msg_atr(SPICE_SMARTCARD_CHANNEL(channel), reader);
                        spice_channel_drop_pending_card_insertion(SPICE_SMARTCARD_CHANNEL(channel), reader);
                    }

                    if (spice_channel_has_pending_reader_removal(SPICE_SMARTCARD_CHANNEL(channel), reader)) {
                        send_msg_generic(SPICE_SMARTCARD_CHANNEL(channel),
                                         reader, VSC_CardRemove);
                        spice_channel_drop_pending_reader_removal(SPICE_SMARTCARD_CHANNEL(channel), reader);
                    }
                    break;
                case VSC_APDU:
                case VSC_ATR:
                case VSC_CardRemove:
                case VSC_Error:
                case VSC_ReaderRemove:
                    break;
                default:
                    g_warning("Unexpected message: %d", priv->in_flight_message->message_type);
                    break;
            }
            smartcard_message_complete_in_flight(SPICE_SMARTCARD_CHANNEL(channel));

            break;

        case VSC_APDU:
        case VSC_Init: {
            const unsigned int APDU_BUFFER_SIZE = 270;
            VReaderStatus reader_status;
            uint8_t data_out[APDU_BUFFER_SIZE + sizeof(uint32_t)];
            int data_out_len = sizeof(data_out);

            g_return_if_fail(msg->reader_id != VSCARD_UNDEFINED_READER_ID);
            reader = vreader_get_reader_by_id(msg->reader_id);
            g_return_if_fail(reader != NULL); //FIXME: add log message

            reader_status = vreader_xfr_bytes(reader,
                                              msg->data, msg->length,
                                              data_out, &data_out_len);
            if (reader_status == VREADER_OK) {
                send_msg_generic_with_data(SPICE_SMARTCARD_CHANNEL(channel),
                                           reader, VSC_APDU,
                                           data_out, data_out_len, FALSE);
            } else {
                uint32_t error_code;
                error_code = GUINT32_TO_LE(reader_status);
                send_msg_generic_with_data(SPICE_SMARTCARD_CHANNEL(channel),
                                           reader, VSC_Error,
                                           (uint8_t*)&error_code,
                                           sizeof (error_code), FALSE);
            }
            break;
        }
        default:
            g_return_if_reached();
    }
}

