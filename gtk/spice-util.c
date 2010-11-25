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
