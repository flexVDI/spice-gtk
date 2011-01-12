/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010 Red Hat, Inc.
   Copyright (C) 2006 Anthony Liguori <anthony@codemonkey.ws>
   Copyright (C) 2009-2010 Daniel P. Berrange <dan@berrange.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.0 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
*/
#ifndef __GIO_COROUTINE_H__
#define __GIO_COROUTINE_H__

#include <gio/gio.h>
#include "coroutine.h"

G_BEGIN_DECLS

struct wait_queue
{
    gboolean waiting;
    struct coroutine *context;
};

/*
 * A special GSource impl which allows us to wait on a certain
 * condition to be satisfied. This is effectively a boolean test
 * run on each iteration of the main loop. So whenever a file has
 * new I/O, or a timer occurs, etc we'll do the check. This is
 * pretty efficient compared to a normal GLib Idle func which has
 * to busy wait on a timeout, since our condition is only checked
 * when some other source's state changes
 */
typedef gboolean (*g_condition_wait_func)(gpointer);

struct g_condition_wait_source
{
    GSource src;
    struct coroutine *co;
    g_condition_wait_func func;
    gpointer data;
};

typedef void (*GSignalEmitMainFunc)(GObject *object, int signum, gpointer params);

GIOCondition g_io_wait              (GSocket *sock, GIOCondition cond);
gboolean     g_condition_wait       (g_condition_wait_func func, gpointer data);
void         g_io_wakeup            (struct wait_queue *wait);
GIOCondition g_io_wait_interruptable(struct wait_queue *wait, GSocket *sock, GIOCondition cond);
void         g_signal_emit_main_context(GObject *object, GSignalEmitMainFunc func,
                                        int signum, gpointer params, const char *debug_info);
void         g_object_notify_main_context(GObject *object, const gchar *property_name);

G_END_DECLS

#endif /* __GIO_COROUTINE_H__ */
