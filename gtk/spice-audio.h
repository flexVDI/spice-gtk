#ifndef __SPICE_CLIENT_AUDIO_H__
#define __SPICE_CLIENT_AUDIO_H__

GObject *spice_audio_new(SpiceSession *session, GMainLoop *mainloop,
                         const char *name);

#endif /* __SPICE_CLIENT_AUDIO_H__ */
