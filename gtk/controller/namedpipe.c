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

#include "namedpipe.h"

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>

#include <gio/gwin32inputstream.h>
#include <gio/gwin32outputstream.h>

G_DEFINE_TYPE (SpiceNamedPipeConnection, spice_named_pipe_connection,
               SPICE_TYPE_NAMED_PIPE_CONNECTION)

struct _SpiceNamedPipeConnectionPrivate
{
  GInputStream  *input_stream;
  GOutputStream *output_stream;
  HANDLE handle;
};

static void
spice_named_pipe_connection_init (SpiceNamedPipeConnection *connection)
{
  connection->priv = G_TYPE_INSTANCE_GET_PRIVATE (connection,
                                                  SPICE_TYPE_NAMED_PIPE_CONNECTION,
                                                  SpiceNamedPipeConnectionPrivate);
}

static void
spice_named_pipe_connection_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  SpiceNamedPipeConnection *connection G_GNUC_UNUSED = SPICE_NAMED_PIPE_CONNECTION (object);

  switch (prop_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spice_named_pipe_connection_set_property (GObject      *object,
                                          guint         prop_id,
                                          const GValue *value,
                                          GParamSpec   *pspec)
{
  SpiceNamedPipeConnection *connection G_GNUC_UNUSED = SPICE_NAMED_PIPE_CONNECTION (object);

  switch (prop_id)
    {
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GInputStream *
spice_named_pipe_connection_get_input_stream (GIOStream *io_stream)
{
  SpiceNamedPipeConnection *connection = SPICE_NAMED_PIPE_CONNECTION (io_stream);

  if (connection->priv->input_stream == NULL)
    connection->priv->input_stream = (GInputStream *)
      g_win32_input_stream_new (connection->priv->handle, FALSE);

  return connection->priv->input_stream;
}

static GOutputStream *
spice_named_pipe_connection_get_output_stream (GIOStream *io_stream)
{
  SpiceNamedPipeConnection *connection = SPICE_NAMED_PIPE_CONNECTION (io_stream);

  if (connection->priv->output_stream == NULL)
    connection->priv->output_stream = (GOutputStream *)
      g_win32_output_stream_new (connection->priv->handle, FALSE);

  return connection->priv->output_stream;
}

static void
spice_named_pipe_connection_class_init (SpiceNamedPipeConnectionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GIOStreamClass *stream_class = G_IO_STREAM_CLASS (klass);

  g_type_class_add_private (klass, sizeof (SpiceNamedPipeConnectionPrivate));

  gobject_class->set_property = spice_named_pipe_connection_set_property;
  gobject_class->get_property = spice_named_pipe_connection_get_property;

  stream_class->get_input_stream = spice_named_pipe_connection_get_input_stream;
  stream_class->get_output_stream = spice_named_pipe_connection_get_output_stream;
}

G_DEFINE_TYPE (SpiceNamedPipeListener, spice_named_pipe_listener, G_TYPE_SOCKET_LISTENER);

struct _SpiceNamedPipeListenerPrivate
{
  guint               foo;
};

static void
spice_named_pipe_listener_finalize (GObject *object)
{
  SpiceNamedPipeListener *listener G_GNUC_UNUSED = SPICE_NAMED_PIPE_LISTENER (object);

  G_OBJECT_CLASS (spice_named_pipe_listener_parent_class)
    ->finalize (object);
}

static void
spice_named_pipe_listener_class_init (SpiceNamedPipeListenerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (SpiceNamedPipeListenerPrivate));
}

static void
spice_named_pipe_listener_init (SpiceNamedPipeListener *listener)
{
  listener->priv = G_TYPE_INSTANCE_GET_PRIVATE (listener,
                                                SPICE_TYPE_NAMED_PIPE_LISTENER,
                                                SpiceNamedPipeListenerPrivate);
}

void
spice_named_pipe_listener_add_named_pipe (SpiceNamedPipeListener *listener,
                                          SpiceNamedPipe         *namedpipe)
{
  g_return_if_fail (SPICE_IS_NAMED_PIPE_LISTENER (listener));
  g_return_if_fail (SPICE_IS_NAMED_PIPE (namedpipe));

}


void
spice_named_pipe_listener_accept_async (SpiceNamedPipeListener  *listener,
                                        GCancellable            *cancellable,
                                        GAsyncReadyCallback      callback,
                                        gpointer                user_data)
{
  g_return_if_fail (SPICE_IS_NAMED_PIPE_LISTENER (listener));

}

GSocketConnection *
spice_named_pipe_listener_accept_finish (SpiceNamedPipeListener *listener,
                                         GAsyncResult           *result,
                                         GObject               **source_object,
                                         GError                **error)
{
  g_return_val_if_fail (SPICE_IS_NAMED_PIPE_LISTENER (listener), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

  return NULL;
}

SpiceNamedPipeListener *
spice_named_pipe_listener_new (void)
{
  return g_object_new (SPICE_TYPE_NAMED_PIPE_LISTENER, NULL);
}

G_DEFINE_TYPE (SpiceNamedPipe, spice_named_pipe, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_NAME,
};

struct _SpiceNamedPipePrivate
{
  gchar *               name;
};

static void
spice_named_pipe_finalize (GObject *object)
{
  SpiceNamedPipe *np G_GNUC_UNUSED = SPICE_NAMED_PIPE (object);

  g_free (np->priv->name);
  np->priv->name = NULL;

  G_OBJECT_CLASS (spice_named_pipe_parent_class)
    ->finalize (object);
}

static void
spice_named_pipe_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SpiceNamedPipe *np = SPICE_NAMED_PIPE (object);

  switch (prop_id)
    {
      case PROP_NAME:
        g_value_set_string (value, np->priv->name);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spice_named_pipe_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  SpiceNamedPipe *np = SPICE_NAMED_PIPE (object);

  switch (prop_id)
    {
      case PROP_NAME:
        g_free (np->priv->name);
        np->priv->name = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spice_named_pipe_class_init (SpiceNamedPipeClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (SpiceNamedPipePrivate));

  gobject_class->set_property = spice_named_pipe_set_property;
  gobject_class->get_property = spice_named_pipe_get_property;

  g_object_class_install_property (gobject_class, PROP_NAME,
				   g_param_spec_string ("name",
                                                        "Pipe Name",
                                                        "The NamedPipe name",
                                                        NULL,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
}

static void
spice_named_pipe_init (SpiceNamedPipe *np)
{
  np->priv = G_TYPE_INSTANCE_GET_PRIVATE (np,
                                          SPICE_TYPE_NAMED_PIPE,
                                          SpiceNamedPipePrivate);
}

SpiceNamedPipe *
spice_named_pipe_new (const gchar *name)
{
  return g_object_new (SPICE_TYPE_NAMED_PIPE,
                       "name", name,
                       NULL);
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
