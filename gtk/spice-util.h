#ifndef SPICE_UTIL_H
#define SPICE_UTIL_H

#include <glib.h>

G_BEGIN_DECLS

void spice_util_set_debug(gboolean enabled);
gboolean spice_util_get_debug(void);
const gchar *spice_util_get_version_string(void);

#define SPICE_DEBUG(fmt, ...)                                   \
    do {                                                        \
        if (G_UNLIKELY(spice_util_get_debug()))                 \
            g_debug(__FILE__ " " fmt, ## __VA_ARGS__);          \
    } while (0)

#define SPICE_RESERVED_PADDING 44

G_END_DECLS

#endif /* SPICE_UTIL_H */
