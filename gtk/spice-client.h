#ifndef __SPICE_CLIENT_CLIENT_H__
#define __SPICE_CLIENT_CLIENT_H__

/* glib */
#include <glib.h>
#include <glib-object.h>

/* spice-protocol */
#include <spice/enums.h>
#include <spice/protocol.h>

/* spice/client -- FIXME */
#include "marshallers.h"
#include "demarshallers.h"

/* spice/gtk */
#include "spice-types.h"
#include "spice-session.h"
#include "spice-channel.h"

#include "channel-main.h"
#include "channel-display.h"
#include "channel-cursor.h"
#include "channel-inputs.h"
#include "channel-playback.h"

/* debug bits */
#define PANIC(fmt, ...)                                 \
    { fprintf(stderr, "%s:%d " fmt "\n",                \
              __FUNCTION__, __LINE__, ## __VA_ARGS__);  \
        exit(1); }

#define ASSERT(x) if (!(x)) {                               \
    { fprintf(stderr,"%s::%d ASSERT(%s) failed\n",          \
              __FUNCTION__, __LINE__, #x);                  \
        abort(); }                                          \
}

#endif /* __SPICE_CLIENT_CLIENT_H__ */
