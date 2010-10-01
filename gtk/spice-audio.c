/*
 * simple audio init dispatcher
 */

#include "spice-client.h"
#include "spice-common.h"

#include "spice-audio.h"
#include "spice-pulse.h"

GObject *spice_audio_new(SpiceSession *session, GMainLoop *mainloop,
                         const char *name)
{
    GObject *audio = NULL;

    audio = G_OBJECT(spice_pulse_new(session, mainloop, name));
    return audio;
}
