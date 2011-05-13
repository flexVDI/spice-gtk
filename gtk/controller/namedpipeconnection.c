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

#include "namedpipeconnection.h"

#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>

#include <gio/gwin32inputstream.h>
#include <gio/gwin32outputstream.h>

G_DEFINE_TYPE (SpiceNamedPipeConnection, spice_named_pipe_connection,
               G_TYPE_IO_STREAM)

enum
{
  PROP_0,
  PROP_NAMED_PIPE,
};

struct _SpiceNamedPipeConnectionPrivate
{
  GInputStream   *input_stream;
  GOutputStream  *output_stream;
  SpiceNamedPipe *namedpipe;
};

static void
spice_named_pipe_connection_init (SpiceNamedPipeConnection *connection)
{
  connection->priv = G_TYPE_INSTANCE_GET_PRIVATE (connection,
                                                  SPICE_TYPE_NAMED_PIPE_CONNECTION,
                                                  SpiceNamedPipeConnectionPrivate);
  g_message (__FUNCTION__);
}

static void
spice_named_pipe_connection_get_property (GObject    *object,
                                          guint       prop_id,
                                          GValue     *value,
                                          GParamSpec *pspec)
{
  SpiceNamedPipeConnection *c = SPICE_NAMED_PIPE_CONNECTION (object);

  switch (prop_id)
    {
      case PROP_NAMED_PIPE:
        g_value_set_object (value, c->priv->namedpipe);
        break;
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
  SpiceNamedPipeConnection *c = SPICE_NAMED_PIPE_CONNECTION (object);

  switch (prop_id)
    {
      case PROP_NAMED_PIPE:
        g_message (__FUNCTION__);
        c->priv->namedpipe = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GInputStream *
spice_named_pipe_connection_get_input_stream (GIOStream *io_stream)
{
  SpiceNamedPipeConnection *c = SPICE_NAMED_PIPE_CONNECTION (io_stream);
  HANDLE h = spice_named_pipe_get_handle (c->priv->namedpipe);

  g_return_val_if_fail (h != NULL, NULL);

  if (c->priv->input_stream == NULL)
    c->priv->input_stream = g_win32_input_stream_new (h, FALSE);

  g_message ("got input stream %p", c->priv->input_stream);
  return c->priv->input_stream;
}

static GOutputStream *
spice_named_pipe_connection_get_output_stream (GIOStream *io_stream)
{
  SpiceNamedPipeConnection *c = SPICE_NAMED_PIPE_CONNECTION (io_stream);
  HANDLE h = spice_named_pipe_get_handle (c->priv->namedpipe);

  g_return_val_if_fail (h != NULL, NULL);

  if (c->priv->output_stream == NULL)
    c->priv->output_stream = g_win32_output_stream_new (h, FALSE);

  return c->priv->output_stream;
}

static void
spice_named_pipe_connection_dispose (GObject *object)
{
  SpiceNamedPipeConnection *c = SPICE_NAMED_PIPE_CONNECTION (object);

  if (c->priv->namedpipe)
    {
      g_object_unref (c->priv->namedpipe);
      c->priv->namedpipe = NULL;
    }

  if (G_OBJECT_CLASS (spice_named_pipe_connection_parent_class)->dispose)
    G_OBJECT_CLASS (spice_named_pipe_connection_parent_class)->dispose (object);
}

static void
spice_named_pipe_connection_class_init (SpiceNamedPipeConnectionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GIOStreamClass *stream_class = G_IO_STREAM_CLASS (klass);

  g_type_class_add_private (klass, sizeof (SpiceNamedPipeConnectionPrivate));
  g_message (__FUNCTION__);

  gobject_class->set_property = spice_named_pipe_connection_set_property;
  gobject_class->get_property = spice_named_pipe_connection_get_property;
  gobject_class->dispose = spice_named_pipe_connection_dispose;

  stream_class->get_input_stream = spice_named_pipe_connection_get_input_stream;
  stream_class->get_output_stream = spice_named_pipe_connection_get_output_stream;

  g_object_class_install_property (gobject_class, PROP_NAMED_PIPE,
                                   g_param_spec_object ("namedpipe",
                                                        "NamedPipe",
                                                        "The associated NamedPipe",
                                                        SPICE_TYPE_NAMED_PIPE,
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
}
