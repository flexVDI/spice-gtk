#include <config.h>

#include "spice-util.h"

static gboolean debugFlag = FALSE;

void spice_util_set_debug(gboolean enabled)
{
    debugFlag = enabled;
}

gboolean spice_util_get_debug(void)
{
    return debugFlag || g_getenv("SPICE_DEBUG") != NULL;
}

const gchar *spice_util_get_version_string(void)
{
    return VERSION;
}
