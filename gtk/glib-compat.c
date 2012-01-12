/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
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

#include "glib-compat.h"

#if !GLIB_CHECK_VERSION(2,26,0)
G_DEFINE_BOXED_TYPE (GError, spice_error, g_error_copy, g_error_free)
#endif

#if !GLIB_CHECK_VERSION(2,28,0)
/**
 * spice_simple_async_result_take_error: (skip)
 * @simple: a #GSimpleAsyncResult
 * @error: a #GError
 *
 * Sets the result from @error, and takes over the caller's ownership
 * of @error, so the caller does not need to free it any more.
 *
 * Since: 2.28
 **/
G_GNUC_INTERNAL void
g_simple_async_result_take_error (GSimpleAsyncResult *simple,
                                  GError             *error)
{
    /* this code is different from upstream */
    /* we can't avoid extra copy/free, since the simple struct is
       opaque */
    g_simple_async_result_set_from_error (simple, error);
    g_error_free (error);
}
#endif

#if !GLIB_CHECK_VERSION(2,30,0)
G_DEFINE_BOXED_TYPE (GMainContext, spice_main_context, g_main_context_ref, g_main_context_unref)
#endif

