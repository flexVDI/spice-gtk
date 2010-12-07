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
#include <celt051/celt.h>

#include "spice-client.h"
#include "spice-common.h"
#include "spice-channel-priv.h"

#include "spice-marshal.h"

/**
 * SECTION:channel-playback
 * @short_description: audio stream for playback
 * @title: Playback Channel
 * @section_id:
 * @see_also: #SpiceChannel, and #SpiceAudio
 * @stability: Stable
 * @include: channel-playback.h
 *
 * #SpicePlaybackChannel class handles an audio playback stream. The
 * audio data is received via #SpicePlaybackChannel::playback-data
 * signal, and is controlled by the guest with
 * #SpicePlaybackChannel::playback-stop and
 * #SpicePlaybackChannel::playback-start signal events.
 *
 * Note: You may be interested to let the #SpiceAudio class play and
 * record audio channels for your application.
 */

#define SPICE_PLAYBACK_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_PLAYBACK_CHANNEL, spice_playback_channel))

struct spice_playback_channel {
    int                         mode;
    CELTMode                    *celt_mode;
    CELTDecoder                 *celt_decoder;
};

G_DEFINE_TYPE(SpicePlaybackChannel, spice_playback_channel, SPICE_TYPE_CHANNEL)

enum {
    SPICE_PLAYBACK_START,
    SPICE_PLAYBACK_DATA,
    SPICE_PLAYBACK_STOP,

    SPICE_PLAYBACK_LAST_SIGNAL,
};

static guint signals[SPICE_PLAYBACK_LAST_SIGNAL];

static void spice_playback_handle_msg(SpiceChannel *channel, spice_msg_in *msg);

/* ------------------------------------------------------------------ */

static void spice_playback_channel_init(SpicePlaybackChannel *channel)
{
    spice_playback_channel *c;

    c = channel->priv = SPICE_PLAYBACK_CHANNEL_GET_PRIVATE(channel);
    memset(c, 0, sizeof(*c));
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_PLAYBACK_CAP_CELT_0_5_1);
}

static void spice_playback_channel_finalize(GObject *obj)
{
    spice_playback_channel *c = SPICE_PLAYBACK_CHANNEL(obj)->priv;

    if (c->celt_decoder) {
        celt051_decoder_destroy(c->celt_decoder);
        c->celt_decoder = NULL;
    }

    if (c->celt_mode) {
        celt051_mode_destroy(c->celt_mode);
        c->celt_mode = NULL;
    }

    if (G_OBJECT_CLASS(spice_playback_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_playback_channel_parent_class)->finalize(obj);
}

static void spice_playback_channel_class_init(SpicePlaybackChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->finalize     = spice_playback_channel_finalize;
    channel_class->handle_msg   = spice_playback_handle_msg;

    /**
     * SpicePlaybackChannel::playback-start:
     * @channel: the #SpicePlaybackChannel that emitted the signal
     * @format: a #SPICE_AUDIO_FMT
     * @channels: number of channels
     * @rate: audio rate
     *
     * Notify when the playback should start, and provide audio format
     * characteristics.
     **/
    signals[SPICE_PLAYBACK_START] =
        g_signal_new("playback-start",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpicePlaybackChannelClass, playback_start),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__INT_INT_INT,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_INT, G_TYPE_INT, G_TYPE_INT);

    /**
     * SpicePlaybackChannel::playback-data:
     * @channel: the #SpicePlaybackChannel that emitted the signal
     * @data: pointer to audio data
     * @data_size: size in byte of @data
     *
     * Provide audio data to be played.
     **/
    signals[SPICE_PLAYBACK_DATA] =
        g_signal_new("playback-data",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpicePlaybackChannelClass, playback_data),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__POINTER_INT,
                     G_TYPE_NONE,
                     2,
                     G_TYPE_POINTER, G_TYPE_INT);

    /**
     * SpicePlaybackChannel::playback-stop:
     * @channel: the #SpicePlaybackChannel that emitted the signal
     *
     * Notify when the playback should stop.
     **/
    signals[SPICE_PLAYBACK_STOP] =
        g_signal_new("playback-stop",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpicePlaybackChannelClass, playback_stop),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    g_type_class_add_private(klass, sizeof(spice_playback_channel));
}

/* signal trampoline---------------------------------------------------------- */

struct SPICE_PLAYBACK_START {
    gint format;
    gint channels;
    gint frequency;
};

struct SPICE_PLAYBACK_DATA {
    uint8_t *data;
    gsize data_size;
};

struct SPICE_PLAYBACK_STOP {
};

/* main context */
static void do_emit_main_context(GObject *object, int signum, gpointer params)
{
    switch (signum) {
    case SPICE_PLAYBACK_STOP: {
        g_signal_emit(object, signals[signum], 0);
        break;
    }
    case SPICE_PLAYBACK_START: {
        struct SPICE_PLAYBACK_START *p = params;
        g_signal_emit(object, signals[signum], 0,
                      p->format, p->channels, p->frequency);
        break;
    }
    case SPICE_PLAYBACK_DATA: {
        struct SPICE_PLAYBACK_DATA *p = params;
        g_signal_emit(object, signals[signum], 0,
                      p->data, p->data_size);
        break;
    }
    default:
        g_warn_if_reached();
    }
}

/* coroutine context */
#define emit_main_context(object, event, args...)                       \
    G_STMT_START {                                                      \
        g_signal_emit_main_context(G_OBJECT(object), do_emit_main_context, \
                                   event, &((struct event) { args }));  \
    } G_STMT_END

/* ------------------------------------------------------------------ */

/* coroutine context */
static void playback_handle_data(SpiceChannel *channel, spice_msg_in *in)
{
    spice_playback_channel *c = SPICE_PLAYBACK_CHANNEL(channel)->priv;
    SpiceMsgPlaybackPacket *packet = spice_msg_in_parsed(in);

    SPICE_DEBUG("%s: time %d data %p size %d", __FUNCTION__,
            packet->time, packet->data, packet->data_size);

    switch (c->mode) {
    case SPICE_AUDIO_DATA_MODE_RAW:
        emit_main_context(channel, SPICE_PLAYBACK_DATA,
                          packet->data, packet->data_size);
        break;
    case SPICE_AUDIO_DATA_MODE_CELT_0_5_1: {
        celt_int16_t pcm[256 * 2];

        g_return_if_fail(c->celt_decoder != NULL);

        if (celt051_decode(c->celt_decoder, packet->data,
                           packet->data_size, pcm) != CELT_OK) {
            g_warning("celt_decode() error");
            return;
        }

        emit_main_context(channel, SPICE_PLAYBACK_DATA,
                          (uint8_t *)pcm, sizeof(pcm));
        break;
    }
    default:
        g_warning("%s: unhandled mode", __FUNCTION__);
        break;
    }
}

/* coroutine context */
static void playback_handle_mode(SpiceChannel *channel, spice_msg_in *in)
{
    spice_playback_channel *c = SPICE_PLAYBACK_CHANNEL(channel)->priv;
    SpiceMsgPlaybackMode *mode = spice_msg_in_parsed(in);

    SPICE_DEBUG("%s: time %d mode %d data %p size %d", __FUNCTION__,
            mode->time, mode->mode, mode->data, mode->data_size);

    c->mode = mode->mode;
    switch (c->mode) {
    case SPICE_AUDIO_DATA_MODE_RAW:
    case SPICE_AUDIO_DATA_MODE_CELT_0_5_1:
        break;
    default:
        g_warning("%s: unhandled mode", __FUNCTION__);
        break;
    }
}

/* coroutine context */
static void playback_handle_start(SpiceChannel *channel, spice_msg_in *in)
{
    spice_playback_channel *c = SPICE_PLAYBACK_CHANNEL(channel)->priv;
    SpiceMsgPlaybackStart *start = spice_msg_in_parsed(in);
    int celt_mode_err;

    SPICE_DEBUG("%s: fmt %d channels %d freq %d time %d", __FUNCTION__,
            start->format, start->channels, start->frequency, start->time);

    switch (c->mode) {
    case SPICE_AUDIO_DATA_MODE_RAW:
        emit_main_context(channel, SPICE_PLAYBACK_START,
                          start->format, start->channels, start->frequency);
        break;
    case SPICE_AUDIO_DATA_MODE_CELT_0_5_1: {
        /* TODO: only support one setting now */
        int frame_size = 256;
        if (!c->celt_mode)
            c->celt_mode = celt051_mode_create(start->frequency, start->channels,
                                               frame_size, &celt_mode_err);
        if (!c->celt_mode)
            g_warning("create celt mode failed %d", celt_mode_err);

        if (!c->celt_decoder)
            c->celt_decoder = celt051_decoder_create(c->celt_mode);

        if (!c->celt_decoder)
            g_warning("create celt decoder failed");

        emit_main_context(channel, SPICE_PLAYBACK_START,
                          start->format, start->channels, start->frequency);
        break;
    }
    default:
        g_warning("%s: unhandled mode", __FUNCTION__);
        break;
    }
}

/* coroutine context */
static void playback_handle_stop(SpiceChannel *channel, spice_msg_in *in)
{
    emit_main_context(channel, SPICE_PLAYBACK_STOP);
}

static spice_msg_handler playback_handlers[] = {
    [ SPICE_MSG_SET_ACK ]                  = spice_channel_handle_set_ack,
    [ SPICE_MSG_PING ]                     = spice_channel_handle_ping,
    [ SPICE_MSG_NOTIFY ]                   = spice_channel_handle_notify,
    [ SPICE_MSG_DISCONNECTING ]            = spice_channel_handle_disconnect,
    [ SPICE_MSG_WAIT_FOR_CHANNELS ]        = spice_channel_handle_wait_for_channels,
    [ SPICE_MSG_MIGRATE ]                  = spice_channel_handle_migrate,

    [ SPICE_MSG_PLAYBACK_DATA ]            = playback_handle_data,
    [ SPICE_MSG_PLAYBACK_MODE ]            = playback_handle_mode,
    [ SPICE_MSG_PLAYBACK_START ]           = playback_handle_start,
    [ SPICE_MSG_PLAYBACK_STOP ]            = playback_handle_stop,
};

/* coroutine context */
static void spice_playback_handle_msg(SpiceChannel *channel, spice_msg_in *msg)
{
    int type = spice_msg_in_type(msg);
    g_return_if_fail(type < SPICE_N_ELEMENTS(playback_handlers));
    g_return_if_fail(playback_handlers[type] != NULL);
    playback_handlers[type](channel, msg);
}
