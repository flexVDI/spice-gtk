#ifndef __SPICE_CLIENT_RECORD_CHANNEL_H__
#define __SPICE_CLIENT_RECORD_CHANNEL_H__

#include "spice-client.h"

G_BEGIN_DECLS

#define SPICE_TYPE_RECORD_CHANNEL            (spice_record_channel_get_type())
#define SPICE_RECORD_CHANNEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), SPICE_TYPE_RECORD_CHANNEL, SpiceRecordChannel))
#define SPICE_RECORD_CHANNEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), SPICE_TYPE_RECORD_CHANNEL, SpiceRecordChannelClass))
#define SPICE_IS_RECORD_CHANNEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), SPICE_TYPE_RECORD_CHANNEL))
#define SPICE_IS_RECORD_CHANNEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), SPICE_TYPE_RECORD_CHANNEL))
#define SPICE_RECORD_CHANNEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), SPICE_TYPE_RECORD_CHANNEL, SpiceRecordChannelClass))

typedef struct _SpiceRecordChannel SpiceRecordChannel;
typedef struct _SpiceRecordChannelClass SpiceRecordChannelClass;
typedef struct spice_record_channel spice_record_channel;

struct _SpiceRecordChannel {
    SpiceChannel parent;
    spice_record_channel *priv;
    /* Do not add fields to this struct */
};

struct _SpiceRecordChannelClass {
    SpiceChannelClass parent_class;

    /* signals */
    void (*spice_record_start)(SpiceRecordChannel *channel,
                                 gint format, gint channels, gint freq);
    void (*spice_record_data)(SpiceRecordChannel *channel, gpointer *data, gint size);
    void (*spice_record_stop)(SpiceRecordChannel *channel);

    /* Do not add fields to this struct */
};

GType	        spice_record_channel_get_type(void);

G_END_DECLS

#endif /* __SPICE_CLIENT_RECORD_CHANNEL_H__ */
