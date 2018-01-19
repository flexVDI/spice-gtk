/*
   Copyright (C) 2016 Red Hat, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2 of the License,
   or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <stdio.h>
#include <glib.h>
#include <gio/gunixinputstream.h>

static int exit_status;
static GMainLoop *loop;
static GDataInputStream *stdin_stream;

static void cleanup(void)
{
    if (loop)
        g_main_loop_quit(loop);
}


static void stdin_read_complete(GObject *src, GAsyncResult *res, gpointer data G_GNUC_UNUSED)
{
    char *s = NULL;
    const char *response = NULL;
    GError *err = NULL;
    gsize len;

    s = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res,
                                             &len, &err);

    /* exit the program to return an early EOF to the caller */
    if (g_getenv("TEST_EOF"))
        goto done;

    /* Don't return any response, but continue running to simulate a
     * unresponsive binary */
    if (g_getenv("TEST_NORESPONSE"))
        return;

    /* specify a particular resonse to be returned to the caller */
    response = g_getenv("TEST_RESPONSE");
    if (!response)
        response = "SUCCESS";

    fprintf(stdout, "%s\n", response);
    fflush(stdout);

done:
    g_clear_error(&err);
    g_free(s);
    cleanup();
}

int main(void)
{
    GInputStream *stdin_unix_stream;

    loop = g_main_loop_new(NULL, FALSE);

    stdin_unix_stream = g_unix_input_stream_new(STDIN_FILENO, 0);
    stdin_stream = g_data_input_stream_new(stdin_unix_stream);
    g_data_input_stream_set_newline_type(stdin_stream,
                                         G_DATA_STREAM_NEWLINE_TYPE_LF);
    g_clear_object(&stdin_unix_stream);
    g_data_input_stream_read_line_async(stdin_stream, G_PRIORITY_DEFAULT, NULL,
                                        stdin_read_complete, NULL);

    g_main_loop_run(loop);

    g_object_unref(stdin_stream);
    g_main_loop_unref(loop);

    return exit_status;
}
