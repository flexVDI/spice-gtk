/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2012 Red Hat, Inc.

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
#include <glib.h>

#include "spice-util.h"
#include "bio-gio.h"

static long bio_gio_ctrl(G_GNUC_UNUSED BIO *b,
                         int cmd,
                         G_GNUC_UNUSED long num,
                         G_GNUC_UNUSED void *ptr)
{
    return (cmd == BIO_CTRL_FLUSH);
}

static int bio_gio_write(BIO *bio, const char *in, int inl)
{
    GOutputStream *stream;
    gssize ret;
    GError *error = NULL;

    stream = g_io_stream_get_output_stream(bio->ptr);
    ret = g_pollable_output_stream_write_nonblocking(G_POLLABLE_OUTPUT_STREAM(stream),
                                                     in, inl, NULL, &error);
    BIO_clear_retry_flags(bio);

    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
        BIO_set_retry_write(bio);
    if (error != NULL) {
        g_warning("%s", error->message);
        g_clear_error(&error);
    }

    return ret;
}

static int bio_gio_read(BIO *bio, char *out, int outl)
{
    GInputStream *stream;
    gssize ret;
    GError *error = NULL;

    stream = g_io_stream_get_input_stream(bio->ptr);
    ret = g_pollable_input_stream_read_nonblocking(G_POLLABLE_INPUT_STREAM(stream),
                                                   out, outl, NULL, &error);
    BIO_clear_retry_flags(bio);

    if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
        BIO_set_retry_read(bio);
    else if (error != NULL)
        g_warning("%s", error->message);

    g_clear_error(&error);

    return ret;
}

static int bio_gio_puts(BIO *bio, const char *str)
{
    int n, ret;

    n = strlen(str);
    ret = bio_gio_write(bio, str, n);

    return ret;
}

#define BIO_TYPE_START 128

G_GNUC_INTERNAL
BIO* bio_new_giostream(GIOStream *stream)
{
    BIO *bio;
    static BIO_METHOD bio_gio_method;

    if (bio_gio_method.name == NULL) {
        bio_gio_method.type = BIO_TYPE_START | BIO_TYPE_SOURCE_SINK;
        bio_gio_method.name = "gio stream";
    }

    bio = BIO_new(&bio_gio_method);
    if (!bio)
        return NULL;

    bio->method->bwrite = bio_gio_write;
    bio->method->bread = bio_gio_read;
    bio->method->bputs = bio_gio_puts;
    bio->method->ctrl = bio_gio_ctrl;

    bio->init = 1;
    bio->ptr = stream;

    return bio;
}
