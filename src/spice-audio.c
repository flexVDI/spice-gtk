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
/*
 * simple audio init dispatcher
 */

/**
 * SECTION:spice-audio
 * @short_description: a helper to play and to record audio channels
 * @title: Spice Audio
 * @section_id:
 * @see_also: #SpiceRecordChannel, and #SpicePlaybackChannel
 * @stability: Stable
 * @include: spice-client.h
 *
 * A class that handles the playback and record channels for your
 * application, and connect them to the default sound system.
 */

#include "config.h"

#include "spice-client.h"
#include "spice-common.h"

#include "spice-audio.h"
#include "spice-session-priv.h"
#include "spice-channel-priv.h"
#include "spice-audio-priv.h"

#ifdef HAVE_PULSE
#include "spice-pulse.h"
#endif
#ifdef HAVE_GSTAUDIO
#include "spice-gstaudio.h"
#endif

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE(SpiceAudio, spice_audio, G_TYPE_OBJECT)

enum {
    PROP_0,
    PROP_SESSION,
    PROP_MAIN_CONTEXT,
};

static void spice_audio_finalize(GObject *gobject)
{
    SpiceAudio *self = SPICE_AUDIO(gobject);
    SpiceAudioPrivate *priv = self->priv;

    g_clear_pointer(&priv->main_context, g_main_context_unref);

    if (G_OBJECT_CLASS(spice_audio_parent_class)->finalize)
        G_OBJECT_CLASS(spice_audio_parent_class)->finalize(gobject);
}

static void spice_audio_get_property(GObject *gobject,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
    SpiceAudio *self = SPICE_AUDIO(gobject);
    SpiceAudioPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, priv->session);
        break;
    case PROP_MAIN_CONTEXT:
        g_value_set_boxed(value, priv->main_context);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_audio_set_property(GObject *gobject,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
    SpiceAudio *self = SPICE_AUDIO(gobject);
    SpiceAudioPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        priv->session = g_value_get_object(value);
        break;
    case PROP_MAIN_CONTEXT:
        priv->main_context = g_value_dup_boxed(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_audio_class_init(SpiceAudioClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    gobject_class->finalize     = spice_audio_finalize;
    gobject_class->get_property = spice_audio_get_property;
    gobject_class->set_property = spice_audio_set_property;

    /**
     * SpiceAudio:session:
     *
     * #SpiceSession this #SpiceAudio is associated with
     *
     **/
    pspec = g_param_spec_object("session", "Session", "SpiceSession",
                                SPICE_TYPE_SESSION,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_SESSION, pspec);

    /**
     * SpiceAudio:main-context:
     */
    pspec = g_param_spec_boxed("main-context", "Main Context",
                               "GMainContext to use for the event source",
                               G_TYPE_MAIN_CONTEXT,
                               G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_MAIN_CONTEXT, pspec);
}

static void spice_audio_init(SpiceAudio *self)
{
    self->priv = spice_audio_get_instance_private(self);
}

static void connect_channel(SpiceAudio *self, SpiceChannel *channel)
{
    if (channel->priv->state != SPICE_CHANNEL_STATE_UNCONNECTED)
        return;

    if (SPICE_AUDIO_GET_CLASS(self)->connect_channel(self, channel))
        spice_channel_connect(channel);
}

static void update_audio_channels(SpiceAudio *self, SpiceSession *session)
{
    GList *list, *tmp;

    if (!spice_session_get_audio_enabled(session)) {
        SPICE_DEBUG("FIXME: disconnect audio channels");
        return;
    }

    list = spice_session_get_channels(session);
    for (tmp = g_list_first(list); tmp != NULL; tmp = g_list_next(tmp)) {
        connect_channel(self, tmp->data);
    }
    g_list_free(list);
}

static void channel_new(SpiceSession *session, SpiceChannel *channel, SpiceAudio *self)
{
    connect_channel(self, channel);
}

static void session_enable_audio(GObject *gobject, GParamSpec *pspec,
                                 gpointer user_data)
{
    update_audio_channels(SPICE_AUDIO(user_data), SPICE_SESSION(gobject));
}

void spice_audio_get_playback_volume_info_async(SpiceAudio *audio,
                                                GCancellable *cancellable,
                                                SpiceMainChannel *main_channel,
                                                GAsyncReadyCallback callback,
                                                gpointer user_data)
{
    g_return_if_fail(audio != NULL);
    SPICE_AUDIO_GET_CLASS(audio)->get_playback_volume_info_async(audio,
            cancellable, main_channel, callback, user_data);
}

gboolean spice_audio_get_playback_volume_info_finish(SpiceAudio *audio,
                                                     GAsyncResult *res,
                                                     gboolean *mute,
                                                     guint8 *nchannels,
                                                     guint16 **volume,
                                                     GError **error)
{
    g_return_val_if_fail(audio != NULL, FALSE);
    return SPICE_AUDIO_GET_CLASS(audio)->get_playback_volume_info_finish(audio,
            res, mute, nchannels, volume, error);
}

void spice_audio_get_record_volume_info_async(SpiceAudio *audio,
                                              GCancellable *cancellable,
                                              SpiceMainChannel *main_channel,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data)
{
    g_return_if_fail(audio != NULL);
    SPICE_AUDIO_GET_CLASS(audio)->get_record_volume_info_async(audio,
            cancellable, main_channel, callback, user_data);
}

gboolean spice_audio_get_record_volume_info_finish(SpiceAudio *audio,
                                                   GAsyncResult *res,
                                                   gboolean *mute,
                                                   guint8 *nchannels,
                                                   guint16 **volume,
                                                   GError **error)
{
    g_return_val_if_fail(audio != NULL, FALSE);
    return SPICE_AUDIO_GET_CLASS(audio)->get_record_volume_info_finish(audio,
            res, mute, nchannels, volume, error);
}

G_GNUC_INTERNAL
SpiceAudio *spice_audio_new_priv(SpiceSession *session, GMainContext *context,
                                 const char *name)
{
    SpiceAudio *self = NULL;

    if (context == NULL)
        context = g_main_context_default();
    if (name == NULL)
        name = g_get_application_name();

#ifdef HAVE_PULSE
    self = SPICE_AUDIO(spice_pulse_new(session, context, name));
#endif
#ifdef HAVE_GSTAUDIO
    if (!self)
        self = SPICE_AUDIO(spice_gstaudio_new(session, context, name));
#endif
    if (!self)
        return NULL;

    spice_g_signal_connect_object(session, "notify::enable-audio", G_CALLBACK(session_enable_audio), self, 0);
    spice_g_signal_connect_object(session, "channel-new", G_CALLBACK(channel_new), self, G_CONNECT_AFTER);
    update_audio_channels(self, session);

    return self;
}

/**
 * spice_audio_new:
 * @session: the #SpiceSession to connect to
 * @context: (allow-none): a #GMainContext to attach to (or %NULL for
 * default).
 * @name: (allow-none): a name for the audio channels (or %NULL for
 * application name).
 *
 * Once instantiated, #SpiceAudio will handle the playback and record
 * channels to stream to your local audio system.
 *
 * Returns: a new #SpiceAudio instance or %NULL if no backend or failed.
 * Deprecated: 0.8: Use spice_audio_get() instead
 **/
SpiceAudio *spice_audio_new(SpiceSession *session, GMainContext *context,
                            const char *name)
{
    return spice_audio_new_priv(session, context, name);
}
