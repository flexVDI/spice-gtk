#ifndef __SPICE_CLIENT_AUDIO_H__
#define __SPICE_CLIENT_AUDIO_H__

#include <glib-object.h>
#include "spice-session.h"

G_BEGIN_DECLS

GObject *spice_audio_new(SpiceSession *session,
                         GMainContext *context,
                         const char *name);

G_END_DECLS

#endif /* __SPICE_CLIENT_AUDIO_H__ */
