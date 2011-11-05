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
 * @include: spice-audio.h
 *
 * A class that handles the playback and record channels for your
 * application, and connect them to the default sound system.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "spice-client.h"
#include "spice-common.h"

#include "spice-audio.h"

#ifdef WITH_PULSE
#include "spice-pulse.h"
#endif
#ifdef WITH_GSTAUDIO
#include "spice-gstaudio.h"
#endif

G_DEFINE_ABSTRACT_TYPE(SpiceAudio, spice_audio, G_TYPE_OBJECT)


static void spice_audio_class_init(SpiceAudioClass *klass G_GNUC_UNUSED)
{
}

static void spice_audio_init(SpiceAudio *self G_GNUC_UNUSED)
{
}


/**
 * spice_audio_new:
 * @session: the #SpiceSession to connect to
 * @context: a #GMainContext to attach to (or %NULL for default).
 * @name: a name for the audio channels (or %NULL for default).
 *
 * Once instantiated, #SpiceAudio will handle the playback and record
 * channels to stream to your local audio system.
 *
 * Returns: a new #SpiceAudio instance or %NULL if no backend or failed.
 **/
SpiceAudio *spice_audio_new(SpiceSession *session, GMainContext *context,
                         const char *name)
{
    SpiceAudio *audio = NULL;

    if (context == NULL)
      context = g_main_context_default();
    if (name == NULL)
      name = "spice";

#ifdef WITH_PULSE
    audio = SPICE_AUDIO(spice_pulse_new(session, context, name));
#endif
#ifdef WITH_GSTAUDIO
    audio = SPICE_AUDIO(spice_gstaudio_new(session, context, name));
#endif
    return audio;
}

/**
 * spice_audio_get:
 * @session: the #SpiceSession to connect to
 * @context: (allow-none): a #GMainContext to attach to (or %NULL for default).
 *
 * Gets the #SpiceAudio associated with the passed in #SpiceSession.
 * A new #SpiceAudio instance will be created the first time this
 * function is called for a certain #SpiceSession.
 *
 * Note that this function returns a weak reference, which should not be used
 * after the #SpiceSession itself has been unref-ed by the caller.
 *
 * Returns: (transfer none): a weak reference to a #SpiceAudio
 * instance or %NULL if failed.
 **/
SpiceAudio *spice_audio_get(SpiceSession *session, GMainContext *context)
{
    static GStaticMutex mutex = G_STATIC_MUTEX_INIT;
    SpiceAudio *self;

    g_static_mutex_lock(&mutex);
    self = g_object_get_data(G_OBJECT(session), "spice-audio");
    if (self == NULL) {
        self = spice_audio_new(session, context, NULL);
        g_object_set_data(G_OBJECT(session), "spice-audio", self);
        if (self)
            g_object_weak_ref(G_OBJECT(session), (GWeakNotify)g_object_unref, self);
    }
    g_static_mutex_unlock(&mutex);

    return self;
}
