/*
 * simple audio init dispatcher
 */

#include "spice-client.h"
#include "spice-common.h"

#include "spice-audio.h"
#include "spice-pulse.h"

GObject *spice_audio_new(SpiceSession *session, GMainContext *context,
                         const char *name)
{
    GObject *audio = NULL;

    if (context == NULL)
      context = g_main_context_default();
    if (name == NULL)
      name = "spice";

    audio = G_OBJECT(spice_pulse_new(session, context, name));
    return audio;
}
