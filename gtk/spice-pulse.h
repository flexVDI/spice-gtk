#ifndef __SPICE_CLIENT_PULSE_H__
#define __SPICE_CLIENT_PULSE_H__

#include "spice-client.h"

G_BEGIN_DECLS

#define SPICE_TYPE_PULSE            (spice_pulse_get_type())
#define SPICE_PULSE(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SPICE_TYPE_PULSE, SpicePulse))
#define SPICE_PULSE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SPICE_TYPE_PULSE, SpicePulseClass))
#define SPICE_IS_PULSE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SPICE_TYPE_PULSE))
#define SPICE_IS_PULSE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SPICE_TYPE_PULSE))
#define SPICE_PULSE_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SPICE_TYPE_PULSE, SpicePulseClass))


typedef struct _SpicePulse SpicePulse;
typedef struct _SpicePulseClass SpicePulseClass;
typedef struct spice_pulse spice_pulse;

struct _SpicePulse {
    GObject parent;
    spice_pulse *priv;
    /* Do not add fields to this struct */
};

struct _SpicePulseClass {
    GObjectClass parent_class;
    /* Do not add fields to this struct */
};

GType	        spice_pulse_get_type(void);

SpicePulse *spice_pulse_new(SpiceSession *session, GMainLoop *mainloop,
                            const char *name);

G_END_DECLS

#endif /* __SPICE_CLIENT_PULSE_H__ */
