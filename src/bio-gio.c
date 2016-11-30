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

#if OPENSSL_VERSION_NUMBER < 0x10100000
static BIO_METHOD one_static_bio;

static int BIO_meth_set_read(BIO_METHOD *biom,
                             int (*bread) (BIO *, char *, int))
{
    biom->bread = bread;
    return 1;
}

static int BIO_meth_set_write(BIO_METHOD *biom,
                              int (*bwrite) (BIO *, const char *, int))
{
    biom->bwrite = bwrite;
    return 1;
}

static int BIO_meth_set_puts(BIO_METHOD *biom,
                             int (*bputs) (BIO *, const char *))
{
    biom->bputs = bputs;
    return 1;
}

static int BIO_meth_set_ctrl(BIO_METHOD *biom,
                             long (*ctrl) (BIO *, int, long, void *))
{
    biom->ctrl = ctrl;
    return 1;
}

#define BIO_TYPE_START 128

static int BIO_get_new_index(void)
{
    static int bio_index = BIO_TYPE_START;
    return bio_index++;
}

static void BIO_set_init(BIO *a, int init)
{
	a->init = init;
}

static void BIO_set_data(BIO *a, void *ptr)
{
    a->ptr = ptr;
}

static void *BIO_get_data(BIO *a)
{
    return a->ptr;
}

static BIO_METHOD *BIO_meth_new(int type, const char *name)
{
    BIO_METHOD *biom = &one_static_bio;

    biom->type = type;
    biom->name = name;
    return biom;
}

static void BIO_meth_free(BIO_METHOD *biom)
{
}

#endif

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

    stream = g_io_stream_get_output_stream(BIO_get_data(bio));
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

    stream = g_io_stream_get_input_stream(BIO_get_data(bio));
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

static BIO_METHOD *bio_gio_method;

G_GNUC_INTERNAL
BIO* bio_new_giostream(GIOStream *stream)
{
    BIO *bio;

    if (!bio_gio_method) {
        bio_gio_method = BIO_meth_new(BIO_get_new_index() |
                                      BIO_TYPE_SOURCE_SINK,
                                      "gio stream");
        if (!bio_gio_method)
            return NULL;

        if (!BIO_meth_set_write(bio_gio_method, bio_gio_write) ||
            !BIO_meth_set_read(bio_gio_method, bio_gio_read) ||
            !BIO_meth_set_puts(bio_gio_method, bio_gio_puts) ||
            !BIO_meth_set_ctrl(bio_gio_method, bio_gio_ctrl)) {
            BIO_meth_free(bio_gio_method);
            bio_gio_method = NULL;
            return NULL;
        }
    }

    bio = BIO_new(bio_gio_method);
    if (!bio)
        return NULL;

    BIO_set_init(bio, 1);
    BIO_set_data(bio, stream);
    return bio;
}
