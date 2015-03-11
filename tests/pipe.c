#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <locale.h>

#include "giopipe.h"

typedef struct _Fixture {
    GIOStream *p1;
    GIOStream *p2;

    GInputStream *ip1;
    GOutputStream *op1;
    GInputStream *ip2;
    GOutputStream *op2;

    gchar buf[16];

    GMainLoop *loop;
    GCancellable *cancellable;
    guint timeout;
} Fixture;

static gboolean
stop_loop (gpointer data)
{
    GMainLoop *loop = data;

    g_main_loop_quit (loop);
    g_assert_not_reached();

    return G_SOURCE_REMOVE;
}

static void
fixture_set_up(Fixture *fixture,
               gconstpointer user_data)
{
    int i;

    spice_make_pipe(&fixture->p1, &fixture->p2);
    g_assert_true(G_IS_IO_STREAM(fixture->p1));
    g_assert_true(G_IS_IO_STREAM(fixture->p2));

    fixture->op1 = g_io_stream_get_output_stream(fixture->p1);
    g_assert_true(G_IS_OUTPUT_STREAM(fixture->op1));
    fixture->ip1 = g_io_stream_get_input_stream(fixture->p1);
    g_assert_true(G_IS_INPUT_STREAM(fixture->ip1));
    fixture->op2 = g_io_stream_get_output_stream(fixture->p2);
    g_assert_true(G_IS_OUTPUT_STREAM(fixture->op2));
    fixture->ip2 = g_io_stream_get_input_stream(fixture->p2);
    g_assert_true(G_IS_INPUT_STREAM(fixture->ip2));

    for (i = 0; i < sizeof(fixture->buf); i++) {
        fixture->buf[i] = 0x42 + i;
    }

    fixture->cancellable = g_cancellable_new();
    fixture->loop = g_main_loop_new (NULL, FALSE);
    fixture->timeout = g_timeout_add (1000, stop_loop, fixture->loop);
}

static void
fixture_tear_down(Fixture *fixture,
                  gconstpointer user_data)
{
    g_clear_object(&fixture->p1);
    g_clear_object(&fixture->p2);

    g_clear_object(&fixture->cancellable);
    g_source_remove(fixture->timeout);
    g_main_loop_unref(fixture->loop);
}

static void
test_pipe_readblock(Fixture *f, gconstpointer user_data)
{
    GError *error = NULL;
    gssize size;

    size = g_input_stream_read(f->ip2, f->buf, 1,
                               f->cancellable, &error);

    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);

    g_clear_error(&error);
}

static void
test_pipe_writeblock(Fixture *f, gconstpointer user_data)
{
    GError *error = NULL;
    gssize size;

    size = g_output_stream_write(f->op1, "", 1,
                                 f->cancellable, &error);

    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);

    g_clear_error(&error);
}

static void
write_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    GMainLoop *loop = user_data;
    gssize nbytes;

    nbytes = g_output_stream_write_finish(G_OUTPUT_STREAM(source), result, &error);

    g_assert_no_error(error);
    g_assert_cmpint(nbytes, >, 0);
    g_clear_error(&error);

    g_main_loop_quit (loop);
}

static void
read_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gssize nbytes, expected = GPOINTER_TO_INT(user_data);

    nbytes = g_input_stream_read_finish(G_INPUT_STREAM(source), result, &error);

    g_assert_cmpint(nbytes, ==, expected);
    g_assert_no_error(error);
    g_clear_error(&error);
}

static void
test_pipe_writeread(Fixture *f, gconstpointer user_data)
{
    g_output_stream_write_async(f->op1, "", 1, G_PRIORITY_DEFAULT,
                                f->cancellable, write_cb, f->loop);
    g_input_stream_read_async(f->ip2, f->buf, 1, G_PRIORITY_DEFAULT,
                              f->cancellable, read_cb, GINT_TO_POINTER(1));

    g_main_loop_run (f->loop);

    g_output_stream_write_async(f->op1, "", 1, G_PRIORITY_DEFAULT,
                                f->cancellable, write_cb, f->loop);
    g_input_stream_read_async(f->ip2, f->buf, 1, G_PRIORITY_DEFAULT,
                              f->cancellable, read_cb, GINT_TO_POINTER(1));

    g_main_loop_run (f->loop);
}

static void
test_pipe_readwrite(Fixture *f, gconstpointer user_data)
{
    g_input_stream_read_async(f->ip2, f->buf, 1, G_PRIORITY_DEFAULT,
                              f->cancellable, read_cb, GINT_TO_POINTER(1));
    g_output_stream_write_async(f->op1, "", 1, G_PRIORITY_DEFAULT,
                                f->cancellable, write_cb, f->loop);

    g_main_loop_run (f->loop);
}

static void
read8_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gssize nbytes;
    GMainLoop *loop = user_data;

    nbytes = g_input_stream_read_finish(G_INPUT_STREAM(source), result, &error);

    g_assert_cmpint(nbytes, ==, 8);
    g_assert_no_error(error);
    g_clear_error(&error);
}

static void
test_pipe_write16read8(Fixture *f, gconstpointer user_data)
{
    g_output_stream_write_async(f->op1, "0123456789abcdef", 16, G_PRIORITY_DEFAULT,
                                f->cancellable, write_cb, f->loop);
    g_input_stream_read_async(f->ip2, f->buf, 8, G_PRIORITY_DEFAULT,
                              f->cancellable, read8_cb, GINT_TO_POINTER(8));

    g_main_loop_run (f->loop);

    /* check next read would block */
    test_pipe_readblock(f, user_data);
}

static void
test_pipe_write8read16(Fixture *f, gconstpointer user_data)
{
    g_output_stream_write_async(f->op1, "01234567", 8, G_PRIORITY_DEFAULT,
                                f->cancellable, write_cb, f->loop);
    g_input_stream_read_async(f->ip2, f->buf, 16, G_PRIORITY_DEFAULT,
                              f->cancellable, read8_cb, GINT_TO_POINTER(8));

    g_main_loop_run (f->loop);

    /* check next read would block */
    test_pipe_writeblock(f, user_data);
}

static void
readclose_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gssize nbytes;
    GMainLoop *loop = user_data;

    nbytes = g_input_stream_read_finish(G_INPUT_STREAM(source), result, &error);

    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CLOSED);
    g_clear_error(&error);

    g_main_loop_quit (loop);
}

static void
test_pipe_readclosestream(Fixture *f, gconstpointer user_data)
{
    GError *error = NULL;

    g_input_stream_read_async(f->ip2, f->buf, 1, G_PRIORITY_DEFAULT,
                              f->cancellable, readclose_cb, f->loop);
    g_io_stream_close(f->p1, f->cancellable, &error);

    g_main_loop_run (f->loop);
}

static void
test_pipe_readclose(Fixture *f, gconstpointer user_data)
{
    GError *error = NULL;

    g_input_stream_read_async(f->ip2, f->buf, 1, G_PRIORITY_DEFAULT,
                              f->cancellable, readclose_cb, f->loop);
    g_output_stream_close(f->op1, f->cancellable, &error);

    g_main_loop_run (f->loop);
}

static void
readcancel_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    GError *error = NULL;
    gssize nbytes;
    GMainLoop *loop = user_data;

    nbytes = g_input_stream_read_finish(G_INPUT_STREAM(source), result, &error);

    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CLOSED);
    g_clear_error(&error);

    g_main_loop_quit (loop);
}

static void
test_pipe_readcancel(Fixture *f, gconstpointer user_data)
{
    GError *error = NULL;

    g_input_stream_read_async(f->ip2, f->buf, 1, G_PRIORITY_DEFAULT,
                              f->cancellable, readcancel_cb, f->loop);
    g_output_stream_close(f->op1, f->cancellable, &error);

    g_main_loop_run (f->loop);
}

int main(int argc, char* argv[])
{
    setlocale(LC_ALL, "");

    g_test_init(&argc, &argv, NULL);

    g_test_add("/pipe/readblock", Fixture, NULL,
               fixture_set_up, test_pipe_readblock,
               fixture_tear_down);

    g_test_add("/pipe/writeblock", Fixture, NULL,
               fixture_set_up, test_pipe_writeblock,
               fixture_tear_down);

    g_test_add("/pipe/writeread", Fixture, NULL,
               fixture_set_up, test_pipe_writeread,
               fixture_tear_down);

    g_test_add("/pipe/readwrite", Fixture, NULL,
               fixture_set_up, test_pipe_readwrite,
               fixture_tear_down);

    g_test_add("/pipe/write16read8", Fixture, NULL,
               fixture_set_up, test_pipe_write16read8,
               fixture_tear_down);

    g_test_add("/pipe/write8read16", Fixture, NULL,
               fixture_set_up, test_pipe_write8read16,
               fixture_tear_down);

    g_test_add("/pipe/readclosestream", Fixture, NULL,
               fixture_set_up, test_pipe_readclosestream,
               fixture_tear_down);

    g_test_add("/pipe/readclose", Fixture, NULL,
               fixture_set_up, test_pipe_readclose,
               fixture_tear_down);

    g_test_add("/pipe/readcancel", Fixture, NULL,
               fixture_set_up, test_pipe_readcancel,
               fixture_tear_down);

    return g_test_run();
}
