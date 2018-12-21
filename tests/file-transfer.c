#include <gio/gio.h>

#include "spice-file-transfer-task-priv.h"

typedef struct _Fixture {
    GFile         **files;
    guint           num_files;
    guint           num_files_done;
    GCancellable   *cancellable;
    GMainLoop      *loop;
    GHashTable     *xfer_tasks;
} Fixture;

#define SINGLE_FILE     1
#define MULTIPLE_FILES  10

#define T10ns (G_TIME_SPAN_MILLISECOND / 100)

const gchar content[] = "0123456789_spice-file-transfer-task";

static void
f_setup(Fixture *f, gconstpointer user_data)
{
    guint i;
    GError *err = NULL;

    f->loop = g_main_loop_new(NULL, FALSE);
    f->num_files = GPOINTER_TO_UINT(user_data);
    f->num_files_done = 0;
    f->files = g_new0(GFile *, f->num_files + 1);
    f->cancellable = g_cancellable_new();
    for (i = 0; i < f->num_files; i++) {
        gboolean success;
        GFileIOStream *iostream;

        f->files[i] = g_file_new_tmp("spice-file-transfer-XXXXXX", &iostream, &err);
        g_assert_no_error(err);
        g_assert_nonnull(iostream);
        g_clear_object(&iostream);

        success = g_file_replace_contents (f->files[i], content, strlen(content), NULL, FALSE,
                                           G_FILE_CREATE_NONE, NULL, f->cancellable, &err);
        g_assert_no_error(err);
        g_assert_true(success);
    }
}

static void
f_teardown(Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    guint i;
    GError *err = NULL;

    g_main_loop_unref(f->loop);
    g_clear_object(&f->cancellable);
    g_clear_pointer(&f->xfer_tasks, g_hash_table_unref);

    for (i = 0; i < f->num_files; i++) {
        g_file_delete(f->files[i], NULL, &err);
        g_assert_no_error(err);
        g_object_unref(f->files[i]);
    }
    g_clear_pointer(&f->files, g_free);
}

/*******************************************************************************
 * TEST SIMPLE TRANSFER
 ******************************************************************************/
static void
transfer_xfer_task_on_finished(SpiceFileTransferTask *xfer_task G_GNUC_UNUSED,
                               GError *error G_GNUC_UNUSED,
                               gpointer user_data)
{
    Fixture *f = user_data;

    f->num_files_done++;
    if (f->num_files == f->num_files_done)
        g_main_loop_quit(f->loop);
}

static void
transfer_read_async_cb(GObject *source_object,
                       GAsyncResult *res,
                       gpointer user_data G_GNUC_UNUSED)
{
    SpiceFileTransferTask *xfer_task;
    gssize count;
    char *buffer;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(source_object);
    count = spice_file_transfer_task_read_finish(xfer_task, res, &buffer, &error);
    g_assert_no_error(error);

    if (count == 0) {
        spice_file_transfer_task_completed(xfer_task, NULL);
        return;
    }

    spice_file_transfer_task_read_async(xfer_task, transfer_read_async_cb, NULL);
}

static void
transfer_init_async_cb(GObject *obj, GAsyncResult *res, gpointer data G_GNUC_UNUSED)
{
    GFileInfo *info;
    SpiceFileTransferTask *xfer_task;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(obj);
    info = spice_file_transfer_task_init_task_finish(xfer_task, res, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);
    g_object_unref(info);

    /* read file loop */
    spice_file_transfer_task_read_async(xfer_task, transfer_read_async_cb, NULL);
}

static void
test_simple_transfer(Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    GHashTableIter iter;
    gpointer key, value;

    f->xfer_tasks = spice_file_transfer_task_create_tasks(f->files, NULL, G_FILE_COPY_NONE, f->cancellable);
    g_hash_table_iter_init(&iter, f->xfer_tasks);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        SpiceFileTransferTask *xfer_task = SPICE_FILE_TRANSFER_TASK(value);
        g_signal_connect(xfer_task, "finished", G_CALLBACK(transfer_xfer_task_on_finished), f);
        spice_file_transfer_task_init_task_async(xfer_task, transfer_init_async_cb, NULL);
    }
    g_main_loop_run (f->loop);
}

/*******************************************************************************
 * TEST CANCEL ON INIT TASK
 ******************************************************************************/
static void
transfer_cancelled_on_init_async_cb(GObject *obj, GAsyncResult *res, gpointer data)
{
    GFileInfo *info;
    SpiceFileTransferTask *xfer_task;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(obj);
    info = spice_file_transfer_task_init_task_finish(xfer_task, res, &error);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_null(info);
    g_clear_error(&error);

    transfer_xfer_task_on_finished(NULL, NULL, data);
}

static void
test_cancel_before_task_init(Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    GHashTableIter iter;
    gpointer key, value;

    f->xfer_tasks = spice_file_transfer_task_create_tasks(f->files, NULL, G_FILE_COPY_NONE, f->cancellable);
    g_hash_table_iter_init(&iter, f->xfer_tasks);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        SpiceFileTransferTask *xfer_task = SPICE_FILE_TRANSFER_TASK(value);
        g_signal_connect(xfer_task, "finished", G_CALLBACK(transfer_xfer_task_on_finished), f);
        g_cancellable_cancel(f->cancellable);
        spice_file_transfer_task_init_task_async(xfer_task, transfer_cancelled_on_init_async_cb, f);
    }
    g_main_loop_run (f->loop);
}

static void
test_cancel_after_task_init(Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    GHashTableIter iter;
    gpointer key, value;

    f->xfer_tasks = spice_file_transfer_task_create_tasks(f->files, NULL, G_FILE_COPY_NONE, f->cancellable);
    g_hash_table_iter_init(&iter, f->xfer_tasks);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        SpiceFileTransferTask *xfer_task = SPICE_FILE_TRANSFER_TASK(value);
        g_signal_connect(xfer_task, "finished", G_CALLBACK(transfer_xfer_task_on_finished), f);
        spice_file_transfer_task_init_task_async(xfer_task, transfer_cancelled_on_init_async_cb, f);
        g_cancellable_cancel(f->cancellable);
    }
    g_main_loop_run (f->loop);
}

/*******************************************************************************
 * TEST CANCEL ON READ ASYNC
 ******************************************************************************/

static void
transfer_cancelled_read_async_cb(GObject *source_object,
                                 GAsyncResult *res,
                                 gpointer user_data)
{
    SpiceFileTransferTask *xfer_task;
    gssize count;
    char *buffer;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(source_object);
    count = spice_file_transfer_task_read_finish(xfer_task, res, &buffer, &error);
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_assert_cmpint(count, ==, -1);
    g_clear_error(&error);

    transfer_xfer_task_on_finished(NULL, NULL, user_data);
}

static void
transfer_on_init_async_cb_before_read_cancel(GObject *obj, GAsyncResult *res, gpointer data)
{
    GFileInfo *info;
    SpiceFileTransferTask *xfer_task;
    GError *error = NULL;
    GCancellable *cancellable;

    xfer_task = SPICE_FILE_TRANSFER_TASK(obj);
    info = spice_file_transfer_task_init_task_finish(xfer_task, res, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);
    g_object_unref(info);

    cancellable = spice_file_transfer_task_get_cancellable(xfer_task);
    g_cancellable_cancel(cancellable);
    spice_file_transfer_task_read_async(xfer_task, transfer_cancelled_read_async_cb, data);
}

static void
transfer_on_init_async_cb_after_read_cancel(GObject *obj, GAsyncResult *res, gpointer data)
{
    GFileInfo *info;
    SpiceFileTransferTask *xfer_task;
    GError *error = NULL;
    GCancellable *cancellable;

    xfer_task = SPICE_FILE_TRANSFER_TASK(obj);
    info = spice_file_transfer_task_init_task_finish(xfer_task, res, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);
    g_object_unref(info);

    cancellable = spice_file_transfer_task_get_cancellable(xfer_task);
    spice_file_transfer_task_read_async(xfer_task, transfer_cancelled_read_async_cb, data);
    g_cancellable_cancel(cancellable);
}

static void
test_cancel_before_read_async(Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    GHashTableIter iter;
    gpointer key, value;

    f->xfer_tasks = spice_file_transfer_task_create_tasks(f->files, NULL, G_FILE_COPY_NONE, NULL);
    g_hash_table_iter_init(&iter, f->xfer_tasks);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        SpiceFileTransferTask *xfer_task = SPICE_FILE_TRANSFER_TASK(value);
        g_signal_connect(xfer_task, "finished", G_CALLBACK(transfer_xfer_task_on_finished), f);
        spice_file_transfer_task_init_task_async(xfer_task, transfer_on_init_async_cb_before_read_cancel, f);
    }
    g_main_loop_run (f->loop);
}

static void
test_cancel_after_read_async(Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    GHashTableIter iter;
    gpointer key, value;

    f->xfer_tasks = spice_file_transfer_task_create_tasks(f->files, NULL, G_FILE_COPY_NONE, NULL);
    g_hash_table_iter_init(&iter, f->xfer_tasks);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        SpiceFileTransferTask *xfer_task = SPICE_FILE_TRANSFER_TASK(value);
        g_signal_connect(xfer_task, "finished", G_CALLBACK(transfer_xfer_task_on_finished), f);
        spice_file_transfer_task_init_task_async(xfer_task, transfer_on_init_async_cb_after_read_cancel, f);
    }
    g_main_loop_run (f->loop);
}

/*******************************************************************************
 * TEST AGENT CANCEL ON READ
 ******************************************************************************/

static void
transfer_agent_cancelled_read_async_cb(GObject *source_object,
                                       GAsyncResult *res,
                                       gpointer user_data)
{
    SpiceFileTransferTask *xfer_task;
    gssize count;
    char *buffer;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(source_object);
    count = spice_file_transfer_task_read_finish(xfer_task, res, &buffer, &error);
    g_assert_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED);
    g_assert_cmpint(count, ==, -1);
    g_clear_error(&error);

    transfer_xfer_task_on_finished(NULL, NULL, user_data);
}

static void
transfer_on_init_async_cb_agent_cancel(GObject *obj, GAsyncResult *res, gpointer data)
{
    GFileInfo *info;
    SpiceFileTransferTask *xfer_task;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(obj);
    info = spice_file_transfer_task_init_task_finish(xfer_task, res, &error);
    g_assert_no_error(error);
    g_assert_nonnull(info);
    g_object_unref(info);

    spice_file_transfer_task_read_async(xfer_task, transfer_agent_cancelled_read_async_cb, data);

    error = g_error_new(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        "transfer is cancelled by spice agent");
    spice_file_transfer_task_completed(xfer_task, error);
}

static void
test_agent_cancel_on_read(Fixture *f, gconstpointer user_data G_GNUC_UNUSED)
{
    GHashTableIter iter;
    gpointer key, value;

    f->xfer_tasks = spice_file_transfer_task_create_tasks(f->files, NULL, G_FILE_COPY_NONE, NULL);
    g_hash_table_iter_init(&iter, f->xfer_tasks);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        SpiceFileTransferTask *xfer_task = SPICE_FILE_TRANSFER_TASK(value);
        g_signal_connect(xfer_task, "finished", G_CALLBACK(transfer_xfer_task_on_finished), f);
        spice_file_transfer_task_init_task_async(xfer_task, transfer_on_init_async_cb_agent_cancel, f);
    }
    g_main_loop_run (f->loop);
}

/* Tests summary:
 *
 * This tests are specific to SpiceFileTransferTask in order to verify:
 * - Cancelation from User;
 * - Error/Cancelation from Agent;
 * - Bad behavior from Agent;
 *
 * SpiceFileTransferTask is in charge of initializing, reading and finalizing
 * all IO for the file user wants to transfer but being unaware of how the
 * protocol works. As there are several combinations of events, these tests
 * intend to find errors, leaks and crashes on SpiceFileTransferTask in common
 * set of events in order to avoid regression in the drag-and-drop feature.
 *
 * Small overview of how File Transfer works.
 *
 * 1.) User calls spice_main_file_copy_async with a list of files to send to the
 *     guest
 * 2.) SpiceMainChannel creates a SpiceFileTransferTask per File and request its
 *     initialization using spice_file_transfer_task_init_task_async(). The init
 *     function will open a GFileInputStream and request the GFileInfo of file;
 * 3.) Using the GFileInfo object, SpiceMainChannel starts the File Transfer
 *     protocol with VD_AGENT_FILE_XFER_START. Agent responds with
 *     VD_AGENT_FILE_XFER_STATUS_CAN_SEND_DATA which starts the read IO using
 *     spice_file_transfer_task_read_async()
 * 4.) After the read is done, SpiceMainChannel does an async flush to the
 *     agent, using the buffer provided by SpiceFileTransferTask; The read IO
 *     will be kept going while SpiceMainChannel has agent tokens to use.
 * 5-) After SpiceMainChannel sends enough data, it can always receive:
 *     - VD_AGENT_FILE_XFER_STATUS_CAN_SEND_DATA: to send more data;
 *     - VD_AGENT_FILE_XFER_STATUS_SUCCESS: all data was sent;
 *     - VD_AGENT_FILE_XFER_STATUS_ERROR: unexpected behavior on agent side;
 *     - VD_AGENT_FILE_XFER_STATUS_CANCELLED: transfer was cancelled by agent;
 * 6-) In any case of termination, the following SpiceFileTransferTask function
 *     should always be called: spice_file_transfer_task_completed(); This will
 *     trigger from SpiceFileTransferTask all necessary events to finalize user
 *     side, memory allocation and so on.
 */
int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add("/spice-file-transfer-task/single/simple-transfer",
               Fixture, GUINT_TO_POINTER(SINGLE_FILE),
               f_setup, test_simple_transfer, f_teardown);

    g_test_add("/spice-file-transfer-task/single/cancel/before-task-init",
               Fixture, GUINT_TO_POINTER(SINGLE_FILE),
               f_setup, test_cancel_before_task_init, f_teardown);

    g_test_add("/spice-file-transfer-task/single/cancel/after-task-init",
               Fixture, GUINT_TO_POINTER(SINGLE_FILE),
               f_setup, test_cancel_after_task_init, f_teardown);

    g_test_add("/spice-file-transfer-task/single/cancel/before-read-async",
               Fixture, GUINT_TO_POINTER(SINGLE_FILE),
               f_setup, test_cancel_before_read_async, f_teardown);

    g_test_add("/spice-file-transfer-task/single/cancel/after-read-async",
               Fixture, GUINT_TO_POINTER(SINGLE_FILE),
               f_setup, test_cancel_after_read_async, f_teardown);

    g_test_add("/spice-file-transfer-task/single/agent/cancel",
               Fixture, GUINT_TO_POINTER(SINGLE_FILE),
               f_setup, test_agent_cancel_on_read, f_teardown);

    g_test_add("/spice-file-transfer-task/multiple/simple-transfer",
               Fixture, GUINT_TO_POINTER(MULTIPLE_FILES),
               f_setup, test_simple_transfer, f_teardown);

    g_test_add("/spice-file-transfer-task/multiple/cancel/before-task-init",
               Fixture, GUINT_TO_POINTER(MULTIPLE_FILES),
               f_setup, test_cancel_before_task_init, f_teardown);

    g_test_add("/spice-file-transfer-task/multiple/cancel/after-task-init",
               Fixture, GUINT_TO_POINTER(MULTIPLE_FILES),
               f_setup, test_cancel_after_task_init, f_teardown);

    g_test_add("/spice-file-transfer-task/multiple/cancel/before--read-async",
               Fixture, GUINT_TO_POINTER(MULTIPLE_FILES),
               f_setup, test_cancel_before_read_async, f_teardown);

    g_test_add("/spice-file-transfer-task/multiple/cancel/after-read-async",
               Fixture, GUINT_TO_POINTER(MULTIPLE_FILES),
               f_setup, test_cancel_after_read_async, f_teardown);

    g_test_add("/spice-file-transfer-task/multiple/agent/cancel",
               Fixture, GUINT_TO_POINTER(MULTIPLE_FILES),
               f_setup, test_agent_cancel_on_read, f_teardown);

    return g_test_run();
}
