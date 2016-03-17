/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
  Copyright (C) 2013 Red Hat, Inc.

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

#include <string.h>

#include "vmcstream.h"
#include "spice-channel-priv.h"
#include "gio-coroutine.h"

struct _SpiceVmcInputStream
{
    GInputStream parent_instance;
    GTask *task;
    struct coroutine *coroutine;

    SpiceChannel *channel;
    gboolean all;
    guint8 *buffer;
    gsize count;
    gsize pos;

    gulong cancel_id;
};

struct _SpiceVmcInputStreamClass
{
    GInputStreamClass parent_class;
};

static gssize   spice_vmc_input_stream_read        (GInputStream        *stream,
                                                    void                *buffer,
                                                    gsize                count,
                                                    GCancellable        *cancellable,
                                                    GError             **error);
static void     spice_vmc_input_stream_read_async  (GInputStream        *stream,
                                                    void                *buffer,
                                                    gsize                count,
                                                    int                  io_priority,
                                                    GCancellable        *cancellable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data);
static gssize   spice_vmc_input_stream_read_finish (GInputStream        *stream,
                                                    GAsyncResult        *result,
                                                    GError             **error);
static gssize   spice_vmc_input_stream_skip        (GInputStream        *stream,
                                                    gsize                count,
                                                    GCancellable        *cancellable,
                                                    GError             **error);
static gboolean spice_vmc_input_stream_close       (GInputStream        *stream,
                                                    GCancellable        *cancellable,
                                                    GError             **error);

G_DEFINE_TYPE(SpiceVmcInputStream, spice_vmc_input_stream, G_TYPE_INPUT_STREAM)


static void
spice_vmc_input_stream_class_init(SpiceVmcInputStreamClass *klass)
{
    GInputStreamClass *istream_class;

    istream_class = G_INPUT_STREAM_CLASS(klass);
    istream_class->read_fn = spice_vmc_input_stream_read;
    istream_class->read_async = spice_vmc_input_stream_read_async;
    istream_class->read_finish = spice_vmc_input_stream_read_finish;
    istream_class->skip = spice_vmc_input_stream_skip;
    istream_class->close_fn = spice_vmc_input_stream_close;
}

static void
spice_vmc_input_stream_init(SpiceVmcInputStream *self)
{
}

static SpiceVmcInputStream *
spice_vmc_input_stream_new(void)
{
    SpiceVmcInputStream *self;

    self = g_object_new(SPICE_TYPE_VMC_INPUT_STREAM, NULL);

    return self;
}

/* coroutine */
/**
 * Feed a SpiceVmc stream with new data from a coroutine
 *
 * The other end will be waiting on read_async() until data is fed
 * here.
 */
G_GNUC_INTERNAL void
spice_vmc_input_stream_co_data(SpiceVmcInputStream *self,
                               const gpointer d, gsize size)
{
    guint8 *data = d;

    g_return_if_fail(SPICE_IS_VMC_INPUT_STREAM(self));
    g_return_if_fail(self->coroutine == NULL);

    self->coroutine = coroutine_self();

    while (size > 0) {
        GCancellable *cancellable;
        SPICE_DEBUG("spicevmc co_data %p", self->task);
        if (!self->task)
            coroutine_yield(NULL);

        g_return_if_fail(self->task != NULL);

        gsize min = MIN(self->count, size);
        memcpy(self->buffer, data, min);

        size -= min;
        data += min;

        SPICE_DEBUG("spicevmc co_data complete: %" G_GSIZE_FORMAT
                    "/%" G_GSIZE_FORMAT, min, self->count);

        self->pos += min;
        self->buffer += min;

        if (self->all && min > 0 && self->pos != self->count)
            continue;

        g_task_return_int(self->task, self->pos);

        cancellable = g_task_get_cancellable(self->task);
        if (cancellable)
            g_cancellable_disconnect(cancellable, self->cancel_id);

        g_clear_object(&self->task);
    }

    self->coroutine = NULL;
}

static void
read_cancelled(GCancellable *cancellable,
               gpointer user_data)
{
    SpiceVmcInputStream *self = SPICE_VMC_INPUT_STREAM(user_data);

    SPICE_DEBUG("read cancelled, %p", self->task);
    g_task_return_new_error(self->task,
                            G_IO_ERROR, G_IO_ERROR_CANCELLED,
                            "read cancelled");

    g_clear_object(&self->task);

    /* See FIXME */
    /* if (self->cancellable) { */
    /*     g_cancellable_disconnect(self->cancellable, self->cancel_id); */
    /*     g_clear_object(&self->cancellable); */
    /* } */
}

G_GNUC_INTERNAL void
spice_vmc_input_stream_read_all_async(GInputStream        *stream,
                                      void                *buffer,
                                      gsize                count,
                                      int                  io_priority,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
    SpiceVmcInputStream *self = SPICE_VMC_INPUT_STREAM(stream);
    GTask *task;

    /* no concurrent read permitted by ginputstream */
    g_return_if_fail(self->task == NULL);
    g_return_if_fail(g_task_get_cancellable(self->task) == NULL);
    self->all = TRUE;
    self->buffer = buffer;
    self->count = count;
    self->pos = 0;
    task = g_task_new(self,
                      cancellable,
                      callback,
                      user_data);
    self->task = task;
    if (cancellable)
        self->cancel_id =
            g_cancellable_connect(cancellable, G_CALLBACK(read_cancelled), self, NULL);

    if (self->coroutine)
        coroutine_yieldto(self->coroutine, NULL);
}

G_GNUC_INTERNAL gssize
spice_vmc_input_stream_read_all_finish(GInputStream *stream,
                                       GAsyncResult *result,
                                       GError **error)
{
    GTask *task = G_TASK(result);
    GCancellable *cancellable;
    SpiceVmcInputStream *self = SPICE_VMC_INPUT_STREAM(stream);

    g_return_val_if_fail(g_task_is_valid(task, self), -1);

    /* FIXME: calling _finish() is required. Disconnecting in
       read_cancelled() causes a deadlock. #705395 */
    cancellable = g_task_get_cancellable(task);
    if (cancellable)
        g_cancellable_disconnect(cancellable, self->cancel_id);

    return g_task_propagate_int(task, error);
}

static void
spice_vmc_input_stream_read_async(GInputStream        *stream,
                                  void                *buffer,
                                  gsize                count,
                                  int                  io_priority,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
    SpiceVmcInputStream *self = SPICE_VMC_INPUT_STREAM(stream);
    GTask *task;

    /* no concurrent read permitted by ginputstream */
    g_return_if_fail(self->task == NULL);
    g_return_if_fail(g_task_get_cancellable(self->task) == NULL);
    self->all = FALSE;
    self->buffer = buffer;
    self->count = count;
    self->pos = 0;

    task = g_task_new(self, cancellable, callback, user_data);
    self->task = task;
    if (cancellable)
        self->cancel_id =
            g_cancellable_connect(cancellable, G_CALLBACK(read_cancelled), self, NULL);

    if (self->coroutine)
        coroutine_yieldto(self->coroutine, NULL);
}

static gssize
spice_vmc_input_stream_read_finish(GInputStream *stream,
                                   GAsyncResult *result,
                                   GError **error)
{
    GTask *task = G_TASK(result);
    GCancellable *cancellable;
    SpiceVmcInputStream *self = SPICE_VMC_INPUT_STREAM(stream);

    g_return_val_if_fail(g_task_is_valid(task, self), -1);

    /* FIXME: calling _finish() is required. Disconnecting in
       read_cancelled() causes a deadlock. #705395 */
    cancellable = g_task_get_cancellable(task);
    if (cancellable)
        g_cancellable_disconnect(cancellable, self->cancel_id);

    return g_task_propagate_int(task, error);
}

static gssize
spice_vmc_input_stream_read(GInputStream  *stream,
                            void          *buffer,
                            gsize          count,
                            GCancellable  *cancellable,
                            GError       **error)
{
    g_return_val_if_reached(-1);
}

static gssize
spice_vmc_input_stream_skip(GInputStream  *stream,
                            gsize          count,
                            GCancellable  *cancellable,
                            GError       **error)
{
    g_return_val_if_reached(-1);
}

static gboolean
spice_vmc_input_stream_close(GInputStream  *stream,
                             GCancellable  *cancellable,
                             GError       **error)
{
    SPICE_DEBUG("fake close");
    return TRUE;
}

/* OUTPUT */

struct _SpiceVmcOutputStream
{
    GOutputStream parent_instance;

    SpiceChannel *channel; /* weak */
};

struct _SpiceVmcOutputStreamClass
{
    GOutputStreamClass parent_class;
};

static gssize   spice_vmc_output_stream_write_fn     (GOutputStream   *stream,
                                                      const void      *buffer,
                                                      gsize            count,
                                                      GCancellable    *cancellable,
                                                      GError         **error);
static gssize   spice_vmc_output_stream_write_finish (GOutputStream   *stream,
                                                      GAsyncResult    *result,
                                                      GError         **error);
static void     spice_vmc_output_stream_write_async  (GOutputStream   *stream,
                                                      const void      *buffer,
                                                      gsize            count,
                                                      int              io_priority,
                                                      GCancellable    *cancellable,
                                                      GAsyncReadyCallback callback,
                                                      gpointer         user_data);

G_DEFINE_TYPE(SpiceVmcOutputStream, spice_vmc_output_stream, G_TYPE_OUTPUT_STREAM)


static void
spice_vmc_output_stream_class_init(SpiceVmcOutputStreamClass *klass)
{
    GOutputStreamClass *ostream_class;

    ostream_class = G_OUTPUT_STREAM_CLASS(klass);
    ostream_class->write_fn = spice_vmc_output_stream_write_fn;
    ostream_class->write_async = spice_vmc_output_stream_write_async;
    ostream_class->write_finish = spice_vmc_output_stream_write_finish;
}

static void
spice_vmc_output_stream_init(SpiceVmcOutputStream *self)
{
}

static SpiceVmcOutputStream *
spice_vmc_output_stream_new(SpiceChannel *channel)
{
    SpiceVmcOutputStream *self;

    self = g_object_new(SPICE_TYPE_VMC_OUTPUT_STREAM, NULL);
    self->channel = channel;

    return self;
}

static gssize
spice_vmc_output_stream_write_fn(GOutputStream   *stream,
                                 const void      *buffer,
                                 gsize            count,
                                 GCancellable    *cancellable,
                                 GError         **error)
{
    SpiceVmcOutputStream *self = SPICE_VMC_OUTPUT_STREAM(stream);
    SpiceMsgOut *msg_out;

    msg_out = spice_msg_out_new(SPICE_CHANNEL(self->channel),
                                SPICE_MSGC_SPICEVMC_DATA);
    spice_marshaller_add(msg_out->marshaller, buffer, count);
    spice_msg_out_send(msg_out);

    return count;
}

static gssize
spice_vmc_output_stream_write_finish(GOutputStream *stream,
                                     GAsyncResult *simple,
                                     GError **error)
{
    SpiceVmcOutputStream *self = SPICE_VMC_OUTPUT_STREAM(stream);
    GAsyncResult *res = g_task_propagate_pointer(G_TASK(simple), error);

    SPICE_DEBUG("spicevmc write finish");
    return spice_vmc_write_finish(self->channel, res, error);
}

static void
write_cb(GObject *source_object,
         GAsyncResult *res,
         gpointer user_data)
{
    GTask *task = user_data;

    g_task_return_pointer(task, g_object_ref(task), g_object_unref);

    g_object_unref(task);
}

static void
spice_vmc_output_stream_write_async(GOutputStream *stream,
                                    const void *buffer,
                                    gsize count,
                                    int io_priority,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    SpiceVmcOutputStream *self = SPICE_VMC_OUTPUT_STREAM(stream);
    GTask *task;

    SPICE_DEBUG("spicevmc write async");
    /* an AsyncResult to forward async op to channel */
    task = g_task_new(self, cancellable, callback, user_data);

    spice_vmc_write_async(self->channel, buffer, count,
                          cancellable, write_cb,
                          task);
}

/* STREAM */

struct _SpiceVmcStream
{
    GIOStream parent_instance;

    SpiceChannel *channel; /* weak */
    SpiceVmcInputStream *in;
    SpiceVmcOutputStream *out;
};

struct _SpiceVmcStreamClass
{
    GIOStreamClass parent_class;
};

static void            spice_vmc_stream_finalize          (GObject   *object);
static GInputStream *  spice_vmc_stream_get_input_stream  (GIOStream *stream);
static GOutputStream * spice_vmc_stream_get_output_stream (GIOStream *stream);

G_DEFINE_TYPE(SpiceVmcStream, spice_vmc_stream, G_TYPE_IO_STREAM)

static void
spice_vmc_stream_class_init(SpiceVmcStreamClass *klass)
{
    GObjectClass *object_class;
    GIOStreamClass *iostream_class;

    object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = spice_vmc_stream_finalize;

    iostream_class = G_IO_STREAM_CLASS(klass);
    iostream_class->get_input_stream = spice_vmc_stream_get_input_stream;
    iostream_class->get_output_stream = spice_vmc_stream_get_output_stream;
}

static void
spice_vmc_stream_finalize(GObject *object)
{
    SpiceVmcStream *self = SPICE_VMC_STREAM(object);

    g_clear_object(&self->in);
    g_clear_object(&self->out);

    G_OBJECT_CLASS(spice_vmc_stream_parent_class)->finalize(object);
}

static void
spice_vmc_stream_init(SpiceVmcStream *self)
{
}

G_GNUC_INTERNAL SpiceVmcStream *
spice_vmc_stream_new(SpiceChannel *channel)
{
    SpiceVmcStream *self;

    self = g_object_new(SPICE_TYPE_VMC_STREAM, NULL);
    self->channel = channel;

    return self;
}

static GInputStream *
spice_vmc_stream_get_input_stream(GIOStream *stream)
{
    SpiceVmcStream *self = SPICE_VMC_STREAM(stream);

    if (!self->in)
        self->in = spice_vmc_input_stream_new();

    return G_INPUT_STREAM(self->in);
}

static GOutputStream *
spice_vmc_stream_get_output_stream(GIOStream *stream)
{
    SpiceVmcStream *self = SPICE_VMC_STREAM(stream);

    if (!self->out)
        self->out = spice_vmc_output_stream_new(self->channel);

    return G_OUTPUT_STREAM(self->out);
}
