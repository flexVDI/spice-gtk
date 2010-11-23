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

#include "spice-client.h"
#include "spice-common.h"

#include "spice-audio.h"
#include "spice-pulse.h"

G_DEFINE_ABSTRACT_TYPE(SpiceAudio, spice_audio, G_TYPE_OBJECT)


static void spice_audio_class_init(SpiceAudioClass *klass G_GNUC_UNUSED)
{
}

static void spice_audio_init(SpiceAudio *self G_GNUC_UNUSED)
{
}


SpiceAudio *spice_audio_new(SpiceSession *session, GMainContext *context,
                         const char *name)
{
    SpiceAudio *audio = NULL;

    if (context == NULL)
      context = g_main_context_default();
    if (name == NULL)
      name = "spice";

    audio = SPICE_AUDIO(spice_pulse_new(session, context, name));
    return audio;
}
