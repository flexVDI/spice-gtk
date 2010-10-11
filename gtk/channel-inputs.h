#ifndef __SPICE_CLIENT_INPUTS_CHANNEL_H__
#define __SPICE_CLIENT_INPUTS_CHANNEL_H__

#include "spice-client.h"

G_BEGIN_DECLS

#define SPICE_TYPE_INPUTS_CHANNEL            (spice_inputs_channel_get_type())
#define SPICE_INPUTS_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SPICE_TYPE_INPUTS_CHANNEL, SpiceInputsChannel))
#define SPICE_INPUTS_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SPICE_TYPE_INPUTS_CHANNEL, SpiceInputsChannelClass))
#define SPICE_IS_INPUTS_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SPICE_TYPE_INPUTS_CHANNEL))
#define SPICE_IS_INPUTS_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SPICE_TYPE_INPUTS_CHANNEL))
#define SPICE_INPUTS_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SPICE_TYPE_INPUTS_CHANNEL, SpiceInputsChannelClass))

typedef struct _SpiceInputsChannel SpiceInputsChannel;
typedef struct _SpiceInputsChannelClass SpiceInputsChannelClass;
typedef struct spice_inputs_channel spice_inputs_channel;

struct _SpiceInputsChannel {
    SpiceChannel parent;
    spice_inputs_channel *priv;
    /* Do not add fields to this struct */
};

struct _SpiceInputsChannelClass {
    SpiceChannelClass parent_class;

    /* signals */
    void (*spice_inputs_modifiers)(SpiceChannel *channel);

    /* Do not add fields to this struct */
};

GType	        spice_inputs_channel_get_type(void);

void spice_inputs_motion(SpiceInputsChannel *channel, gint dx, gint dy,
                         gint button_state);
void spice_inputs_position(SpiceInputsChannel *channel, gint x, gint y,
                           gint display, gint button_state);
void spice_inputs_button_press(SpiceInputsChannel *channel, gint button,
                               gint button_state);
void spice_inputs_button_release(SpiceInputsChannel *channel, gint button,
                                 gint button_state);
void spice_inputs_key_press(SpiceInputsChannel *channel, guint keyval);
void spice_inputs_key_release(SpiceInputsChannel *channel, guint keyval);

G_END_DECLS

#endif /* __SPICE_CLIENT_INPUTS_CHANNEL_H__ */
