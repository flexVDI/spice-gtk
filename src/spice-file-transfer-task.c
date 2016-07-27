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

#include "spice-file-transfer-task-priv.h"

/**
 * SECTION:file-transfer-task
 * @short_description: Monitoring file transfers
 * @title: File Transfer Task
 * @section_id:
 * @see_also: #SpiceMainChannel
 * @stability: Stable
 * @include: spice-client.h
 *
 * SpiceFileTransferTask is an object that represents a particular file
 * transfer between the client and the guest. The properties and signals of the
 * object can be used to monitor the status and result of the transfer. The
 * Main Channel's #SpiceMainChannel::new-file-transfer signal will be emitted
 * whenever a new file transfer task is initiated.
 *
 * Since: 0.31
 */

struct _SpiceFileTransferTask
{
    GObject parent;

    uint32_t                       id;
    gboolean                       pending;
    GFile                          *file;
    SpiceMainChannel               *channel;
    GFileInputStream               *file_stream;
    GFileCopyFlags                 flags;
    GCancellable                   *cancellable;
    GAsyncReadyCallback            callback;
    gpointer                       user_data;
    char                           *buffer;
    uint64_t                       read_bytes;
    uint64_t                       file_size;
    gint64                         start_time;
    gint64                         last_update;
    GError                         *error;
};

struct _SpiceFileTransferTaskClass
{
    GObjectClass parent_class;
};

G_DEFINE_TYPE(SpiceFileTransferTask, spice_file_transfer_task, G_TYPE_OBJECT)

#define FILE_XFER_CHUNK_SIZE (VD_AGENT_MAX_DATA_SIZE * 32)

enum {
    PROP_TASK_ID = 1,
    PROP_TASK_CHANNEL,
    PROP_TASK_CANCELLABLE,
    PROP_TASK_FILE,
    PROP_TASK_PROGRESS,
};

enum {
    SIGNAL_FINISHED,
    LAST_TASK_SIGNAL
};

static guint task_signals[LAST_TASK_SIGNAL];

/*******************************************************************************
 * Helpers
 ******************************************************************************/

static SpiceFileTransferTask *
spice_file_transfer_task_new(SpiceMainChannel *channel, GFile *file, GCancellable *cancellable)
{
    static uint32_t xfer_id = 1;    /* Used to identify task id */

    return g_object_new(SPICE_TYPE_FILE_TRANSFER_TASK,
                        "id", xfer_id++,
                        "file", file,
                        "channel", channel,
                        "cancellable", cancellable,
                        NULL);
}

static void spice_file_transfer_task_query_info_cb(GObject *obj,
                                                   GAsyncResult *res,
                                                   gpointer user_data)
{
    SpiceFileTransferTask *self;
    GFileInfo *info;
    GTask *task;
    GError *error = NULL;

    task = G_TASK(user_data);
    self = g_task_get_source_object(task);

    g_return_if_fail(self->pending == TRUE);
    self->pending = FALSE;

    info = g_file_query_info_finish(G_FILE(obj), res, &error);
    if (self->error) {
        g_clear_object(&info);
        g_clear_error(&error);
        /* Return error previously reported */
        g_task_return_error(task, g_error_copy(self->error));
        g_object_unref(task);
        return;
    } else if (error) {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    self->file_size =
        g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_SIZE);

    /* SpiceFileTransferTask's init is done, handshake for file-transfer will
     * start soon. First "progress" can be emitted ~ 0% */
    g_object_notify(G_OBJECT(self), "progress");

    g_task_return_pointer(task, info, g_object_unref);
    g_object_unref(task);
}

static void spice_file_transfer_task_read_file_cb(GObject *obj,
                                                  GAsyncResult *res,
                                                  gpointer user_data)
{
    SpiceFileTransferTask *self;
    GTask *task;
    GError *error = NULL;

    task = G_TASK(user_data);
    self = g_task_get_source_object(task);

    g_return_if_fail(self->pending == TRUE);

    self->file_stream = g_file_read_finish(G_FILE(obj), res, &error);
    if (self->error) {
        g_clear_error(&error);
        /* Return error previously reported */
        self->pending = FALSE;
        g_task_return_error(task, g_error_copy(self->error));
        g_object_unref(task);
        return;
    } else if (error) {
        self->pending = FALSE;
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    g_file_query_info_async(self->file,
                            "standard::*",
                            G_FILE_QUERY_INFO_NONE,
                            G_PRIORITY_DEFAULT,
                            self->cancellable,
                            spice_file_transfer_task_query_info_cb,
                            task);
}

static void spice_file_transfer_task_read_stream_cb(GObject *source_object,
                                                    GAsyncResult *res,
                                                    gpointer userdata)
{
    SpiceFileTransferTask *self;
    GTask *task;
    gssize nbytes;
    GError *error = NULL;

    task = G_TASK(userdata);
    self = g_task_get_source_object(task);

    g_return_if_fail(self->pending == TRUE);
    self->pending = FALSE;

    nbytes = g_input_stream_read_finish(G_INPUT_STREAM(self->file_stream), res, &error);
    if (self->error) {
        g_clear_error(&error);
        /* On any pending error on SpiceFileTransferTask */
        g_task_return_error(task, g_error_copy(self->error));
        g_object_unref(task);
        return;
    } else if (error) {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    self->read_bytes += nbytes;

    if (spice_util_get_debug()) {
        const GTimeSpan interval = 20 * G_TIME_SPAN_SECOND;
        gint64 now = g_get_monotonic_time();

        if (interval < now - self->last_update) {
            gchar *basename = g_file_get_basename(self->file);
            self->last_update = now;
            SPICE_DEBUG("read %.2f%% of the file %s",
                        100.0 * self->read_bytes / self->file_size, basename);
            g_free(basename);
        }
    }

    g_task_return_int(task, nbytes);
    g_object_unref(task);
}

/* main context */
static void spice_file_transfer_task_close_stream_cb(GObject      *object,
                                                     GAsyncResult *close_res,
                                                     gpointer      user_data)
{
    SpiceFileTransferTask *self;
    GError *error = NULL;

    self = user_data;

    if (object) {
        GInputStream *stream = G_INPUT_STREAM(object);
        g_input_stream_close_finish(stream, close_res, &error);
        if (error) {
            /* This error dont need to report to user, just print a log */
            SPICE_DEBUG("close file error: %s", error->message);
            g_clear_error(&error);
        }
    }

    if (self->error == NULL && spice_util_get_debug()) {
        gint64 now = g_get_monotonic_time();
        gchar *basename = g_file_get_basename(self->file);
        double seconds = (double) (now - self->start_time) / G_TIME_SPAN_SECOND;
        gchar *file_size_str = g_format_size(self->file_size);
        gchar *transfer_speed_str = g_format_size(self->file_size / seconds);

        g_warn_if_fail(self->read_bytes == self->file_size);
        SPICE_DEBUG("transferred file %s of %s size in %.1f seconds (%s/s)",
                    basename, file_size_str, seconds, transfer_speed_str);

        g_free(basename);
        g_free(file_size_str);
        g_free(transfer_speed_str);
    }
    g_object_unref(self);
}


/*******************************************************************************
 * Internal API
 ******************************************************************************/

G_GNUC_INTERNAL
void spice_file_transfer_task_completed(SpiceFileTransferTask *self,
                                        GError *error)
{
    /* In case of multiple errors we only report the first error */
    if (self->error)
        g_clear_error(&error);
    if (error) {
        gchar *path = g_file_get_path(self->file);
        SPICE_DEBUG("File %s xfer failed: %s",
                    path, error->message);
        g_free(path);
        self->error = error;
    }

    if (self->pending) {
        /* Complete but pending is okay only if error is set */
        if (self->error == NULL) {
            self->error = g_error_new(SPICE_CLIENT_ERROR,
                                      SPICE_CLIENT_ERROR_FAILED,
                                      "Cannot complete task in pending state");
        }
        return;
    }

    if (!self->file_stream) {
        spice_file_transfer_task_close_stream_cb(NULL, NULL, self);
        goto signal;
    }

    g_input_stream_close_async(G_INPUT_STREAM(self->file_stream),
                               G_PRIORITY_DEFAULT,
                               self->cancellable,
                               spice_file_transfer_task_close_stream_cb,
                               self);
    self->pending = TRUE;
signal:
    g_signal_emit(self, task_signals[SIGNAL_FINISHED], 0, self->error);
}

G_GNUC_INTERNAL
guint32 spice_file_transfer_task_get_id(SpiceFileTransferTask *self)
{
    g_return_val_if_fail(self != NULL, 0);
    return self->id;
}

G_GNUC_INTERNAL
SpiceMainChannel *spice_file_transfer_task_get_channel(SpiceFileTransferTask *self)
{
    g_return_val_if_fail(self != NULL, NULL);
    return self->channel;
}

G_GNUC_INTERNAL
GCancellable *spice_file_transfer_task_get_cancellable(SpiceFileTransferTask *self)
{
    g_return_val_if_fail(self != NULL, NULL);
    return self->cancellable;
}

G_GNUC_INTERNAL
guint64 spice_file_transfer_task_get_file_size(SpiceFileTransferTask *self)
{
    g_return_val_if_fail(self != NULL, 0);
    return self->file_size;
}

G_GNUC_INTERNAL
guint64 spice_file_transfer_task_get_bytes_read(SpiceFileTransferTask *self)
{
    g_return_val_if_fail(self != NULL, 0);
    return self->read_bytes;
}

/* Helper function which only creates a SpiceFileTransferTask per GFile
 * in @files and returns a HashTable mapping task-id to the task itself
 * Note that the HashTable does not free its values upon destruction:
 * The SpiceFileTransferTask reference created here should be freed by
 * spice_file_transfer_task_completed */
G_GNUC_INTERNAL
GHashTable *spice_file_transfer_task_create_tasks(GFile **files,
                                                  SpiceMainChannel *channel,
                                                  GFileCopyFlags flags,
                                                  GCancellable *cancellable)
{
    GHashTable *xfer_ht;
    gint i;

    g_return_val_if_fail(files != NULL && files[0] != NULL, NULL);

    xfer_ht = g_hash_table_new(g_direct_hash, g_direct_equal);
    for (i = 0; files[i] != NULL && !g_cancellable_is_cancelled(cancellable); i++) {
        SpiceFileTransferTask *xfer_task;
        guint32 task_id;
        GCancellable *task_cancellable = cancellable;

        /* if a cancellable object was not provided for the overall operation,
         * create a separate object for each file so that they can be cancelled
         * separately  */
        if (!task_cancellable)
            task_cancellable = g_cancellable_new();

        /* FIXME: Move the xfer-task initialization to spice_file_transfer_task_new() */
        xfer_task = spice_file_transfer_task_new(channel, files[i], task_cancellable);
        xfer_task->flags = flags;

        task_id = spice_file_transfer_task_get_id(xfer_task);
        g_hash_table_insert(xfer_ht, GUINT_TO_POINTER(task_id), xfer_task);

        /* if we created a per-task cancellable above, unref it */
        if (!cancellable)
            g_object_unref(task_cancellable);
    }
    return xfer_ht;
}

G_GNUC_INTERNAL
void spice_file_transfer_task_init_task_async(SpiceFileTransferTask *self,
                                              GAsyncReadyCallback callback,
                                              gpointer userdata)
{
    GTask *task;

    g_return_if_fail(self != NULL);
    g_return_if_fail(self->pending == FALSE);

    task = g_task_new(self, self->cancellable, callback, userdata);

    self->pending = TRUE;
    g_file_read_async(self->file,
                      G_PRIORITY_DEFAULT,
                      self->cancellable,
                      spice_file_transfer_task_read_file_cb,
                      task);
}

G_GNUC_INTERNAL
GFileInfo *spice_file_transfer_task_init_task_finish(SpiceFileTransferTask *self,
                                                     GAsyncResult *result,
                                                     GError **error)
{
    GTask *task = G_TASK(result);

    g_return_val_if_fail(self != NULL, NULL);
    return g_task_propagate_pointer(task, error);
}

/* Any context */
G_GNUC_INTERNAL
void spice_file_transfer_task_read_async(SpiceFileTransferTask *self,
                                         GAsyncReadyCallback callback,
                                         gpointer userdata)
{
    GTask *task;

    g_return_if_fail(self != NULL);
    if (self->pending) {
        g_task_report_new_error(self, callback, userdata,
                                spice_file_transfer_task_read_async,
                                SPICE_CLIENT_ERROR,
                                SPICE_CLIENT_ERROR_FAILED,
                                "Cannot read data in pending state");
        return;
    }

    /* Notify the progress prior the read to make the info be related to the
     * data that was already sent. To notify the 100% (completed), channel-main
     * should call read-async when it expects EOF. */
    g_object_notify(G_OBJECT(self), "progress");

    task = g_task_new(self, self->cancellable, callback, userdata);

    if (self->read_bytes == self->file_size) {
        /* channel-main might request data after reading the whole file as it
         * expects EOF. Let's return immediately its request as we don't want to
         * reach a state where agent says file-transfer SUCCEED but we are in a
         * PENDING state in SpiceFileTransferTask due reading in idle */
        g_task_return_int(task, 0);
        g_object_unref(task);
        return;
    }

    self->pending = TRUE;
    g_input_stream_read_async(G_INPUT_STREAM(self->file_stream),
                              self->buffer,
                              FILE_XFER_CHUNK_SIZE,
                              G_PRIORITY_DEFAULT,
                              self->cancellable,
                              spice_file_transfer_task_read_stream_cb,
                              task);
}

G_GNUC_INTERNAL
gssize spice_file_transfer_task_read_finish(SpiceFileTransferTask *self,
                                            GAsyncResult *result,
                                            char **buffer,
                                            GError **error)
{
    gssize nbytes;
    GTask *task = G_TASK(result);

    g_return_val_if_fail(self != NULL, -1);

    nbytes = g_task_propagate_int(task, error);
    if (nbytes >= 0 && buffer != NULL)
        *buffer = self->buffer;

    return nbytes;
}

/*******************************************************************************
 * External API
 ******************************************************************************/

/**
 * spice_file_transfer_task_get_progress:
 * @self: a file transfer task
 *
 * Convenience function for retrieving the current progress of this file
 * transfer task.
 *
 * Returns: A percentage value between 0 and 100
 *
 * Since: 0.31
 **/
double spice_file_transfer_task_get_progress(SpiceFileTransferTask *self)
{
    if (self->file_size == 0)
        return 0.0;

    return (double)self->read_bytes / self->file_size;
}

/**
 * spice_file_transfer_task_cancel:
 * @self: a file transfer task
 *
 * Cancels the file transfer task. Note that depending on how the file transfer
 * was initiated, multiple file transfer tasks may share a single
 * #SpiceFileTransferTask::cancellable object, so canceling one task may result
 * in the cancellation of other tasks.
 *
 * Since: 0.31
 **/
void spice_file_transfer_task_cancel(SpiceFileTransferTask *self)
{
    g_cancellable_cancel(self->cancellable);
}

/**
 * spice_file_transfer_task_get_filename:
 * @self: a file transfer task
 *
 * Gets the name of the file being transferred in this task
 *
 * Returns: (transfer none): The basename of the file
 *
 * Since: 0.31
 **/
char* spice_file_transfer_task_get_filename(SpiceFileTransferTask *self)
{
    return g_file_get_basename(self->file);
}

/*******************************************************************************
 * GObject
 ******************************************************************************/

static void
spice_file_transfer_task_get_property(GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
    SpiceFileTransferTask *self = SPICE_FILE_TRANSFER_TASK(object);

    switch (property_id)
    {
        case PROP_TASK_ID:
            g_value_set_uint(value, self->id);
            break;
        case PROP_TASK_FILE:
            g_value_set_object(value, self->file);
            break;
        case PROP_TASK_PROGRESS:
            g_value_set_double(value, spice_file_transfer_task_get_progress(self));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
spice_file_transfer_task_set_property(GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
    SpiceFileTransferTask *self = SPICE_FILE_TRANSFER_TASK(object);

    switch (property_id)
    {
        case PROP_TASK_ID:
            self->id = g_value_get_uint(value);
            break;
        case PROP_TASK_FILE:
            self->file = g_value_dup_object(value);
            break;
        case PROP_TASK_CHANNEL:
            self->channel = g_value_dup_object(value);
            break;
        case PROP_TASK_CANCELLABLE:
            self->cancellable = g_value_dup_object(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
    }
}

static void
spice_file_transfer_task_dispose(GObject *object)
{
    SpiceFileTransferTask *self = SPICE_FILE_TRANSFER_TASK(object);

    g_clear_object(&self->file);
    g_clear_object(&self->file_stream);
    g_clear_error(&self->error);

    G_OBJECT_CLASS(spice_file_transfer_task_parent_class)->dispose(object);
}

static void
spice_file_transfer_task_finalize(GObject *object)
{
    SpiceFileTransferTask *self = SPICE_FILE_TRANSFER_TASK(object);

    g_free(self->buffer);

    G_OBJECT_CLASS(spice_file_transfer_task_parent_class)->finalize(object);
}

static void
spice_file_transfer_task_constructed(GObject *object)
{
    SpiceFileTransferTask *self = SPICE_FILE_TRANSFER_TASK(object);

    if (spice_util_get_debug()) {
        gchar *basename = g_file_get_basename(self->file);
        self->start_time = g_get_monotonic_time();
        self->last_update = self->start_time;

        SPICE_DEBUG("transfer of file %s has started", basename);
        g_free(basename);
    }
}

static void
spice_file_transfer_task_class_init(SpiceFileTransferTaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = spice_file_transfer_task_get_property;
    object_class->set_property = spice_file_transfer_task_set_property;
    object_class->finalize = spice_file_transfer_task_finalize;
    object_class->dispose = spice_file_transfer_task_dispose;
    object_class->constructed = spice_file_transfer_task_constructed;

    /**
     * SpiceFileTransferTask:id:
     *
     * The ID of the file transfer task
     *
     * Since: 0.31
     **/
    g_object_class_install_property(object_class, PROP_TASK_ID,
                                    g_param_spec_uint("id",
                                                      "id",
                                                      "The id of the task",
                                                      0, G_MAXUINT, 0,
                                                      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

    /**
     * SpiceFileTransferTask:channel:
     *
     * The main channel that owns the file transfer task
     *
     * Since: 0.31
     **/
    g_object_class_install_property(object_class, PROP_TASK_CHANNEL,
                                    g_param_spec_object("channel",
                                                        "channel",
                                                        "The channel transferring the file",
                                                        SPICE_TYPE_MAIN_CHANNEL,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

    /**
     * SpiceFileTransferTask:cancellable:
     *
     * A cancellable object used to cancel the file transfer
     *
     * Since: 0.31
     **/
    g_object_class_install_property(object_class, PROP_TASK_CANCELLABLE,
                                    g_param_spec_object("cancellable",
                                                        "cancellable",
                                                        "The object used to cancel the task",
                                                        G_TYPE_CANCELLABLE,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

    /**
     * SpiceFileTransferTask:file:
     *
     * The file that is being transferred in this file transfer task
     *
     * Since: 0.31
     **/
    g_object_class_install_property(object_class, PROP_TASK_FILE,
                                    g_param_spec_object("file",
                                                        "File",
                                                        "The file being transferred",
                                                        G_TYPE_FILE,
                                                        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

    /**
     * SpiceFileTransferTask:progress:
     *
     * The current state of the file transfer. This value indicates a
     * percentage, and ranges from 0 to 100. Listen for change notifications on
     * this property to be updated whenever the file transfer progress changes.
     *
     * Since: 0.31
     **/
    g_object_class_install_property(object_class, PROP_TASK_PROGRESS,
                                    g_param_spec_double("progress",
                                                        "Progress",
                                                        "The percentage of the file transferred",
                                                        0.0, 100.0, 0.0,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

    /**
     * SpiceFileTransferTask::finished:
     * @task: the file transfer task that emitted the signal
     * @error: (transfer none): the error state of the transfer. Will be %NULL
     * if the file transfer was successful.
     *
     * The #SpiceFileTransferTask::finished signal is emitted when the file
     * transfer has completed transferring to the guest.
     *
     * Since: 0.31
     **/
    task_signals[SIGNAL_FINISHED] = g_signal_new("finished", SPICE_TYPE_FILE_TRANSFER_TASK,
                                            G_SIGNAL_RUN_FIRST,
                                            0, NULL, NULL,
                                            g_cclosure_marshal_VOID__BOXED,
                                            G_TYPE_NONE, 1,
                                            G_TYPE_ERROR);
}

static void
spice_file_transfer_task_init(SpiceFileTransferTask *self)
{
    self->buffer = g_malloc0(FILE_XFER_CHUNK_SIZE);
}
