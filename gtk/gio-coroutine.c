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

#include "gio-coroutine.h"

/* Main loop helper functions */
static gboolean g_io_wait_helper(GSocket *sock G_GNUC_UNUSED,
				 GIOCondition cond,
				 gpointer data)
{
    struct coroutine *to = data;
    coroutine_yieldto(to, &cond);
    return FALSE;
}

GIOCondition g_io_wait(GSocket *sock, GIOCondition cond)
{
    GIOCondition *ret;
    GSource *src = g_socket_create_source(sock,
                                          cond | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                          NULL);
    g_source_set_callback(src, (GSourceFunc)g_io_wait_helper, coroutine_self(), NULL);
    g_source_attach(src, NULL);
    ret = coroutine_yield(NULL);
    g_source_unref(src);
    return *ret;
}


GIOCondition g_io_wait_interruptible(struct wait_queue *wait,
                                     GSocket *sock,
                                     GIOCondition cond)
{
    GIOCondition *ret;
    gint id;

    g_return_val_if_fail(wait != NULL, 0);
    g_return_val_if_fail(sock != NULL, 0);

    wait->context = coroutine_self();
    GSource *src = g_socket_create_source(sock,
                                          cond | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                          NULL);
    g_source_set_callback(src, (GSourceFunc)g_io_wait_helper,
                          wait->context, NULL);
    id = g_source_attach(src, NULL);
    wait->waiting = TRUE;
    ret = coroutine_yield(NULL);
    wait->waiting = FALSE;
    g_source_unref(src);

    if (ret == NULL) {
        g_source_remove(id);
        return 0;
    } else
        return *ret;
}

void g_io_wakeup(struct wait_queue *wait)
{
    if (wait->waiting)
        coroutine_yieldto(wait->context, NULL);
}


/*
 * Call immediately before the main loop does an iteration. Returns
 * true if the condition we're checking is ready for dispatch
 */
static gboolean g_condition_wait_prepare(GSource *src,
					 int *timeout) {
    struct g_condition_wait_source *vsrc = (struct g_condition_wait_source *)src;
    *timeout = -1;
    return vsrc->func(vsrc->data);
}

/*
 * Call immediately after the main loop does an iteration. Returns
 * true if the condition we're checking is ready for dispatch
 */
static gboolean g_condition_wait_check(GSource *src)
{
    struct g_condition_wait_source *vsrc = (struct g_condition_wait_source *)src;
    return vsrc->func(vsrc->data);
}

static gboolean g_condition_wait_dispatch(GSource *src G_GNUC_UNUSED,
					  GSourceFunc cb,
					  gpointer data) {
    return cb(data);
}

GSourceFuncs waitFuncs = {
    .prepare = g_condition_wait_prepare,
    .check = g_condition_wait_check,
    .dispatch = g_condition_wait_dispatch,
};

static gboolean g_condition_wait_helper(gpointer data)
{
    struct coroutine *co = (struct coroutine *)data;
    coroutine_yieldto(co, NULL);
    return FALSE;
}

gboolean g_condition_wait(g_condition_wait_func func, gpointer data)
{
    GSource *src;
    struct g_condition_wait_source *vsrc;

    /* Short-circuit check in case we've got it ahead of time */
    if (func(data)) {
        return TRUE;
    }

    /*
     * Don't have it, so yield to the main loop, checking the condition
     * on each iteration of the main loop
     */
    src = g_source_new(&waitFuncs, sizeof(struct g_condition_wait_source));
    vsrc = (struct g_condition_wait_source *)src;

    vsrc->func = func;
    vsrc->data = data;
    vsrc->co = coroutine_self();

    g_source_attach(src, NULL);
    g_source_set_callback(src, g_condition_wait_helper, coroutine_self(), NULL);
    coroutine_yield(NULL);
    g_source_unref(src);

    return TRUE;
}

struct signal_data
{
    GObject *object;
    struct coroutine *caller;
    int signum;
    gpointer params;
    GSignalEmitMainFunc func;
    const char *debug_info;
};

static gboolean emit_main_context(gpointer opaque)
{
    struct signal_data *signal = opaque;

    signal->func(signal->object, signal->signum, signal->params);
    coroutine_yieldto(signal->caller, NULL);

    return FALSE;
}

/* coroutine -> main context */
void g_signal_emit_main_context(GObject *object,
                                GSignalEmitMainFunc emit_main_func,
                                int signum,
                                gpointer params,
                                const char *debug_info)
{
    struct signal_data data;

    data.object = object;
    data.caller = coroutine_self();
    data.signum = signum;
    data.params = params;
    data.func = emit_main_func;
    data.debug_info = debug_info;
    g_idle_add(emit_main_context, &data);

    /* This switches to the system coroutine context, lets
     * the idle function run to dispatch the signal, and
     * finally returns once complete. ie this is synchronous
     * from the POV of the coroutine despite there being
     * an idle function involved
     */
    coroutine_yield(NULL);
}

static gboolean notify_main_context(gpointer opaque)
{
    struct signal_data *signal = opaque;

    g_object_notify(signal->object, signal->params);
    coroutine_yieldto(signal->caller, NULL);

    return FALSE;
}

/* coroutine -> main context */
void g_object_notify_main_context(GObject *object,
                                  const gchar *property_name)
{
    struct signal_data data;

    data.object = object;
    data.caller = coroutine_self();
    data.params = (gpointer)property_name;

    g_idle_add(notify_main_context, &data);

    /* This switches to the system coroutine context, lets
     * the idle function run to dispatch the signal, and
     * finally returns once complete. ie this is synchronous
     * from the POV of the coroutine despite there being
     * an idle function involved
     */
    coroutine_yield(NULL);
}
