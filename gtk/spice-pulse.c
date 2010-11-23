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
#include "spice-pulse.h"
#include "spice-common.h"

#include <pulse/glib-mainloop.h>
#include <pulse/context.h>
#include <pulse/stream.h>
#include <pulse/sample.h>

#define SPICE_PULSE_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_PULSE, spice_pulse))

struct stream {
    pa_sample_spec          spec;
    pa_stream               *stream;
    int                     state;
};

struct spice_pulse {
    SpiceSession            *session;
    SpiceChannel            *pchannel;
    SpiceChannel            *rchannel;

    pa_glib_mainloop        *mainloop;
    pa_context              *context;
    int                     state;
    struct stream           playback;
    struct stream           record;
};

G_DEFINE_TYPE(SpicePulse, spice_pulse, G_TYPE_OBJECT)

static const char *stream_state_names[] = {
    [ PA_STREAM_UNCONNECTED ] = "unconnected",
    [ PA_STREAM_CREATING    ] = "creating",
    [ PA_STREAM_READY       ] = "ready",
    [ PA_STREAM_FAILED      ] = "failed",
    [ PA_STREAM_TERMINATED  ] = "terminated",
};

static const char *context_state_names[] = {
    [ PA_CONTEXT_UNCONNECTED  ] = "unconnected",
    [ PA_CONTEXT_CONNECTING   ] = "connecting",
    [ PA_CONTEXT_AUTHORIZING  ] = "authorizing",
    [ PA_CONTEXT_SETTING_NAME ] = "setting_name",
    [ PA_CONTEXT_READY        ] = "ready",
    [ PA_CONTEXT_FAILED       ] = "failed",
    [ PA_CONTEXT_TERMINATED   ] = "terminated",
};
#define STATE_NAME(array, state) \
    ((state < G_N_ELEMENTS(array)) ? array[state] : NULL)

static void spice_pulse_finalize(GObject *obj)
{
    G_OBJECT_CLASS(spice_pulse_parent_class)->finalize(obj);
}

static void spice_pulse_init(SpicePulse *pulse)
{
    spice_pulse *p;

    p = pulse->priv = SPICE_PULSE_GET_PRIVATE(pulse);
    memset(p, 0, sizeof(*p));
}

static void spice_pulse_class_init(SpicePulseClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->finalize = spice_pulse_finalize;

    g_type_class_add_private(klass, sizeof(spice_pulse));
}

/* ------------------------------------------------------------------ */

static void playback_start(SpicePlaybackChannel *channel, gint format, gint channels,
                           gint frequency, gpointer data)
{
    SpicePulse *pulse = data;
    spice_pulse *p = SPICE_PULSE_GET_PRIVATE(pulse);
    pa_context_state_t state;

    state = pa_context_get_state(p->context);
    switch (state) {
    case PA_CONTEXT_READY:
        if (p->state != state) {
            g_message("%s: pulse context ready", __FUNCTION__);
        }
        if (p->playback.stream &&
            (p->playback.spec.rate != frequency ||
             p->playback.spec.channels != channels)) {
            pa_stream_disconnect(p->playback.stream);
            pa_stream_unref(p->playback.stream);
            p->playback.stream = NULL;
        }
        if (p->playback.stream == NULL) {
            g_return_if_fail(format == SPICE_AUDIO_FMT_S16);
            p->playback.state = PA_STREAM_READY;
            p->playback.spec.format   = PA_SAMPLE_S16LE;
            p->playback.spec.rate     = frequency;
            p->playback.spec.channels = channels;
            p->playback.stream = pa_stream_new(p->context, "playback",
                                               &p->playback.spec, NULL);
            pa_stream_connect_playback(p->playback.stream, NULL, NULL, 0, NULL, NULL);
        }
        if (pa_stream_is_corked(p->playback.stream)) {
            pa_stream_cork(p->playback.stream, 0, NULL, NULL);
        }
        break;
    default:
        if (p->state != state) {
            g_warning("%s: pulse context not ready (%s)",
                      __FUNCTION__, STATE_NAME(context_state_names, state));
        }
        break;
    }
    p->state = state;
}

static void playback_data(SpicePlaybackChannel *channel, gpointer *audio, gint size,
                       gpointer data)
{
    SpicePulse *pulse = data;
    spice_pulse *p = SPICE_PULSE_GET_PRIVATE(pulse);
    pa_stream_state_t state;

    if (!p->playback.stream)
        return;

    state = pa_stream_get_state(p->playback.stream);
    switch (state) {
    case PA_STREAM_READY:
        if (p->playback.state != state) {
            g_message("%s: pulse playback stream ready", __FUNCTION__);
        }
        pa_stream_write(p->playback.stream, audio, size, NULL, 0, PA_SEEK_RELATIVE);
        break;
    default:
        if (p->playback.state != state) {
            g_warning("%s: pulse playback stream not ready (%s)",
                      __FUNCTION__, STATE_NAME(stream_state_names, state));
        }
        break;
    }
    p->playback.state = state;
}

static void playback_stop(SpicePlaybackChannel *channel, gpointer data)
{
    SpicePulse *pulse = data;
    spice_pulse *p = pulse->priv;

    if (!p->playback.stream)
        return;

    pa_stream_cork(p->playback.stream, 1, NULL, NULL);
}

static void record_start(SpicePlaybackChannel *channel, gint format, gint channels,
                         gint frequency, gpointer data)
{
    SPICE_DEBUG("%s", __FUNCTION__);
}

static void record_stop(SpicePlaybackChannel *channel, gpointer data)
{
    SPICE_DEBUG("%s", __FUNCTION__);
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpicePulse *pulse = data;
    spice_pulse *p = pulse->priv;

    if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        p->pchannel = channel;
        g_signal_connect(channel, "playback-start",
                         G_CALLBACK(playback_start), pulse);
        g_signal_connect(channel, "playback-data",
                         G_CALLBACK(playback_data), pulse);
        g_signal_connect(channel, "playback-stop",
                         G_CALLBACK(playback_stop), pulse);
        spice_channel_connect(channel);
    }

    if (SPICE_IS_RECORD_CHANNEL(channel)) {
        p->rchannel = channel;
        g_signal_connect(channel, "record-start",
                         G_CALLBACK(record_start), pulse);
        g_signal_connect(channel, "record-stop",
                         G_CALLBACK(record_stop), pulse);
        spice_channel_connect(channel);
    }
}

SpicePulse *spice_pulse_new(SpiceSession *session, GMainContext *context,
                            const char *name)
{
    SpicePulse *pulse;
    spice_pulse *p;
    GList *list;

    pulse = g_object_new(SPICE_TYPE_PULSE, NULL);
    p = SPICE_PULSE_GET_PRIVATE(pulse);
    p->session = session;

    g_signal_connect(session, "channel-new",
                     G_CALLBACK(channel_new), pulse);
    list = spice_session_get_channels(session);
    for (list = g_list_first(list); list != NULL; list = g_list_next(list)) {
        channel_new(session, list->data, (gpointer*)pulse);
    }
    g_list_free(list);

    p->mainloop = pa_glib_mainloop_new(context);
    p->state = PA_CONTEXT_READY;
    p->context = pa_context_new(pa_glib_mainloop_get_api(p->mainloop), name);
    pa_context_connect(p->context, NULL, 0, NULL);

    return pulse;
}
