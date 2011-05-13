/*
   Copyright (C) 2011 Red Hat, Inc.

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

#include "namedpipelistener.h"

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>

static GSource *g_win32_handle_source_add (HANDLE      handle,
                                           GSourceFunc callback,
                                           gpointer    user_data);

G_DEFINE_TYPE (SpiceNamedPipeListener, spice_named_pipe_listener, G_TYPE_OBJECT);

struct _SpiceNamedPipeListenerPrivate
{
  GList              *namedpipes;
  GQueue             avail_namedpipes;
};

static void
spice_named_pipe_listener_dispose (GObject *object)
{
  SpiceNamedPipeListener *listener = SPICE_NAMED_PIPE_LISTENER (object);
  SpiceNamedPipe *p;

  while (p = g_queue_pop_head (&listener->priv->avail_namedpipes))
    g_object_unref (p);

  g_return_if_fail (g_queue_get_length (&listener->priv->avail_namedpipes) == 0);
  g_queue_clear (&listener->priv->avail_namedpipes);

  g_list_free_full (listener->priv->namedpipes, g_object_unref);
  listener->priv->namedpipes = NULL;

  if (G_OBJECT_CLASS (spice_named_pipe_listener_parent_class)->dispose)
    G_OBJECT_CLASS (spice_named_pipe_listener_parent_class)->dispose (object);
}

static void
spice_named_pipe_listener_class_init (SpiceNamedPipeListenerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (SpiceNamedPipeListenerPrivate));

  gobject_class->dispose = spice_named_pipe_listener_dispose;
}

static void
spice_named_pipe_listener_init (SpiceNamedPipeListener *listener)
{
  listener->priv = G_TYPE_INSTANCE_GET_PRIVATE (listener,
                                                SPICE_TYPE_NAMED_PIPE_LISTENER,
                                                SpiceNamedPipeListenerPrivate);

  g_queue_init (&listener->priv->avail_namedpipes);
}

void
spice_named_pipe_listener_add_named_pipe (SpiceNamedPipeListener *listener,
                                          SpiceNamedPipe         *namedpipe)
{
  g_return_if_fail (SPICE_IS_NAMED_PIPE_LISTENER (listener));
  g_return_if_fail (SPICE_IS_NAMED_PIPE (namedpipe));

  listener->priv->namedpipes = g_list_prepend (listener->priv->namedpipes,
                                               g_object_ref (namedpipe));

  g_queue_push_head (&listener->priv->avail_namedpipes, g_object_ref (namedpipe));
}

typedef struct {
  GCancellable *cancellable;
  GSource *cancelled_idle;
  GSimpleAsyncResult *async_result;
  SpiceNamedPipe *np;
  OVERLAPPED overlapped;
} ConnectData;

static void
connect_cancelled (GCancellable *cancellable,
                   gpointer      user_data)
{
  ConnectData *c = user_data;

  g_message("TODO: connect_cancelled");
}

static gboolean
connect_ready (gpointer user_data)
{
  ConnectData *c = user_data;

  /* Clean up cancellation-related stuff first */
  if (c->cancelled_idle)
    {
      g_source_destroy (c->cancelled_idle);
      g_source_unref (c->cancelled_idle);
      c->cancelled_idle = NULL;
    }
  if (c->cancellable)
    {
      g_signal_handlers_disconnect_by_func (c->cancellable, connect_cancelled, c);
      g_object_unref (c->cancellable);
      c->cancellable = NULL;
    }

  /* Now complete the result (assuming it wasn't already completed) */
  g_return_val_if_fail (c->async_result != NULL, FALSE);

  g_simple_async_result_complete (c->async_result);
  g_object_unref (c->async_result); /* TODO: that sould free c? */

  return FALSE;
}

static void
connect_data_free (gpointer data)
{
  ConnectData *c = data;

  /* TODO: complete me */
  g_free (c);
}

void
spice_named_pipe_listener_accept_async (SpiceNamedPipeListener  *listener,
                                        GCancellable            *cancellable,
                                        GAsyncReadyCallback      callback,
                                        gpointer                 user_data)
{
  ConnectData *c;
  SpiceNamedPipe *namedpipe;

  g_return_if_fail (SPICE_IS_NAMED_PIPE_LISTENER (listener));

  namedpipe = SPICE_NAMED_PIPE (g_queue_pop_head (&listener->priv->avail_namedpipes));
  /* do not unref, we keep that ref */
  g_return_if_fail (namedpipe != NULL);
  c = g_new0 (ConnectData, 1);
  c->np = namedpipe; /* transfer what used to be the avail_namedpipes ref */
  c->async_result = g_simple_async_result_new (G_OBJECT (listener), callback, user_data, NULL);
  c->overlapped.hEvent = CreateEvent(NULL, /* default security attribute */
                                     TRUE, /* manual-reset event */
                                     TRUE, /* initial state = signaled */
                                     NULL); /* unnamed event object */
  g_simple_async_result_set_op_res_gpointer (c->async_result, c, connect_data_free);

  if (ConnectNamedPipe (spice_named_pipe_get_handle (namedpipe), &c->overlapped) != 0)
    {
      /* we shouldn't get there if the listener is in non-blocking */
      g_warn_if_reached ();
    }

  switch (GetLastError ())
    {
      case ERROR_SUCCESS:
      case ERROR_IO_PENDING:
        break;
      case ERROR_PIPE_CONNECTED:
        g_debug ("TODO: pipe already connected");
        break;
      default:
        g_critical ("TODO: ConnectNamedPipe() failed %ld", GetLastError());
    }

  g_win32_handle_source_add (spice_named_pipe_get_handle (namedpipe), connect_ready, c);

  if (cancellable)
    {
      c->cancellable = g_object_ref (cancellable);
      g_signal_connect (cancellable, "cancelled",
                        G_CALLBACK (connect_cancelled), c);
    }
}

SpiceNamedPipeConnection *
spice_named_pipe_listener_accept_finish (SpiceNamedPipeListener *listener,
                                         GAsyncResult           *result,
                                         GObject               **source_object,
                                         GError                **error)
{
  GSimpleAsyncResult *simple;
  ConnectData *c;
  gulong cbret;
  SpiceNamedPipeConnection *connection;
  gboolean success;

  g_return_val_if_fail (SPICE_IS_NAMED_PIPE_LISTENER (listener), NULL);
  g_return_val_if_fail (G_IS_SIMPLE_ASYNC_RESULT (result), NULL);

  simple = G_SIMPLE_ASYNC_RESULT (result);
  c = g_simple_async_result_get_op_res_gpointer (simple);

  success = GetOverlappedResult (c->np, &c->overlapped, &cbret, FALSE);
  if (!success)
    {
      int errsv = GetLastError ();
      gchar *emsg = g_win32_error_message (errsv);

      g_critical ("TODO connect_finish: %s %d", emsg, errsv);

      g_free (emsg);
      return NULL;
    }

  g_message (__FUNCTION__);
  connection = g_object_new (SPICE_TYPE_NAMED_PIPE_CONNECTION,
                             "namedpipe", c->np,
                             NULL);
  g_message (__FUNCTION__);
  return connection;
}

SpiceNamedPipeListener *
spice_named_pipe_listener_new (void)
{
  return g_object_new (SPICE_TYPE_NAMED_PIPE_LISTENER, NULL);
}

/* Windows HANDLE GSource - from gio/gwin32resolver.c */

typedef struct {
  GSource source;
  GPollFD pollfd;
} GWin32HandleSource;

static gboolean
g_win32_handle_source_prepare (GSource *source,
                               gint    *timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
g_win32_handle_source_check (GSource *source)
{
  GWin32HandleSource *hsource = (GWin32HandleSource *)source;

  return hsource->pollfd.revents;
}

static gboolean
g_win32_handle_source_dispatch (GSource     *source,
                                GSourceFunc  callback,
                                gpointer     user_data)
{
  return (*callback) (user_data);
}

static void
g_win32_handle_source_finalize (GSource *source)
{
  ;
}

GSourceFuncs g_win32_handle_source_funcs = {
  g_win32_handle_source_prepare,
  g_win32_handle_source_check,
  g_win32_handle_source_dispatch,
  g_win32_handle_source_finalize
};

static GSource *
g_win32_handle_source_add (HANDLE      handle,
                           GSourceFunc callback,
                           gpointer    user_data)
{
  GWin32HandleSource *hsource;
  GSource *source;

  source = g_source_new (&g_win32_handle_source_funcs, sizeof (GWin32HandleSource));
  hsource = (GWin32HandleSource *)source;
  hsource->pollfd.fd = (gint)handle;
  hsource->pollfd.events = G_IO_IN;
  hsource->pollfd.revents = 0;
  g_source_add_poll (source, &hsource->pollfd);

  g_source_set_callback (source, callback, user_data, NULL);
  g_source_attach (source, g_main_context_get_thread_default ());
  return source;
}
