/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2016 Red Hat, Inc.

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
#include "config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <common/macros.h>

#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved);

BOOL WINAPI DllMain (HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        char *basedir =
            g_win32_get_package_installation_directory_of_module(hinstDLL);
        char *utf8_localedir = g_build_filename(basedir, "share", "locale", NULL);
        /* bindtextdomain's 2nd argument is not UTF-8 aware */
        char *localedir = g_win32_locale_filename_from_utf8 (utf8_localedir);

        bindtextdomain(GETTEXT_PACKAGE, localedir);
        g_free(localedir);
        g_free(utf8_localedir);
        g_free(basedir);
        bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    }
    return TRUE;
}

#else

SPICE_CONSTRUCTOR_FUNC(i18n_init)
{
    bindtextdomain (GETTEXT_PACKAGE, LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
}

#endif
