/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010 Red Hat, Inc.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <glib-object.h>
#include "spice-util.h"

/**
 * SECTION:spice-util
 * @short_description: version and debugging functions
 * @title: Utilities
 * @section_id:
 * @stability: Stable
 * @include: spice-util.h
 *
 * Various functions for debugging and informational purposes.
 */

static gboolean debugFlag = FALSE;

/**
 * spice_util_set_debug:
 * @enabled: %TRUE or %FALSE
 *
 * Enable or disable Spice-GTK debugging messages.
 **/
void spice_util_set_debug(gboolean enabled)
{
    debugFlag = enabled;
}

gboolean spice_util_get_debug(void)
{
    return debugFlag || g_getenv("SPICE_DEBUG") != NULL;
}

/**
 * spice_util_get_version_string:
 *
 * Returns: Spice-GTK version as a const string.
 **/
const gchar *spice_util_get_version_string(void)
{
    return VERSION;
}

G_GNUC_INTERNAL
gboolean spice_strv_contains(const GStrv strv, const gchar *str)
{
    int i;

    if (strv == NULL)
        return FALSE;

    for (i = 0; strv[i] != NULL; i++)
        if (g_str_equal(strv[i], str))
            return TRUE;

    return FALSE;
}
