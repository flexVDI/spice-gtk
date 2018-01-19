/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2011 Red Hat, Inc.

   Red Hat Authors:
   Hans de Goede <hdegoede@redhat.com>

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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "usb-acl-helper.h"

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_USB_ACL_HELPER_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_USB_ACL_HELPER, SpiceUsbAclHelperPrivate))

struct _SpiceUsbAclHelperPrivate {
    GTask *task;
    GIOChannel *in_ch;
    GIOChannel *out_ch;
    GCancellable *cancellable;
    gulong cancellable_id;
};

G_DEFINE_TYPE(SpiceUsbAclHelper, spice_usb_acl_helper, G_TYPE_OBJECT);

static void spice_usb_acl_helper_init(SpiceUsbAclHelper *self)
{
    self->priv = SPICE_USB_ACL_HELPER_GET_PRIVATE(self);
}

static void spice_usb_acl_helper_cleanup(SpiceUsbAclHelper *self)
{
    SpiceUsbAclHelperPrivate *priv = self->priv;

    g_cancellable_disconnect(priv->cancellable, priv->cancellable_id);
    priv->cancellable = NULL;
    priv->cancellable_id = 0;

    g_clear_object(&priv->task);

    g_clear_pointer(&priv->in_ch, g_io_channel_unref);
    g_clear_pointer(&priv->out_ch, g_io_channel_unref);
}

static void spice_usb_acl_helper_finalize(GObject *gobject)
{
    spice_usb_acl_helper_cleanup(SPICE_USB_ACL_HELPER(gobject));

    if (G_OBJECT_CLASS(spice_usb_acl_helper_parent_class)->finalize)
        G_OBJECT_CLASS(spice_usb_acl_helper_parent_class)->finalize(gobject);
}

static void spice_usb_acl_helper_class_init(SpiceUsbAclHelperClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    gobject_class->finalize     = spice_usb_acl_helper_finalize;

    g_type_class_add_private(klass, sizeof(SpiceUsbAclHelperPrivate));
}

/* ------------------------------------------------------------------ */
/* callbacks                                                          */

static void async_result_set_cancelled(GTask *task)
{
    g_task_return_new_error(task,
                G_IO_ERROR, G_IO_ERROR_CANCELLED,
                "Setting USB device node ACL cancelled");
}

static gboolean cb_out_watch(GIOChannel    *channel,
                             GIOCondition   cond,
                             gpointer      *user_data)
{
    SpiceUsbAclHelper *self = SPICE_USB_ACL_HELPER(user_data);
    SpiceUsbAclHelperPrivate *priv = self->priv;
    gboolean success = FALSE;
    GError *err = NULL;
    GIOStatus status;
    gchar *string;
    gsize size;

    /* Check that we've not been cancelled */
    if (priv->task == NULL)
        goto done;

    g_return_val_if_fail(channel == priv->out_ch, FALSE);

    status = g_io_channel_read_line(priv->out_ch, &string, &size, NULL, &err);
    switch (status) {
        case G_IO_STATUS_NORMAL:
            string[strlen(string) - 1] = 0;
            if (!strcmp(string, "SUCCESS")) {
                success = TRUE;
                g_task_return_boolean(priv->task, TRUE);
            } else if (!strcmp(string, "CANCELED")) {
                async_result_set_cancelled(priv->task);
            } else {
                g_task_return_new_error(priv->task,
                            SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Error setting USB device node ACL: '%s'",
                            string);
            }
            g_free(string);
            break;
        case G_IO_STATUS_ERROR:
            g_task_return_error(priv->task, err);
            break;
        case G_IO_STATUS_EOF:
            g_task_return_new_error(priv->task,
                        SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        "Unexpected EOF reading from acl helper stdout");
            break;
        case G_IO_STATUS_AGAIN:
            return TRUE; /* Wait for more input */
    }

    g_cancellable_disconnect(priv->cancellable, priv->cancellable_id);
    priv->cancellable = NULL;
    priv->cancellable_id = 0;

    g_clear_object(&priv->task);

    if (!success)
        spice_usb_acl_helper_cleanup(self);

done:
    g_object_unref(self);
    return FALSE;
}

static void cancelled_cb(GCancellable *cancellable, gpointer user_data)
{
    SpiceUsbAclHelper *self = SPICE_USB_ACL_HELPER(user_data);

    spice_usb_acl_helper_cancel(self);
}

static void helper_child_watch_cb(GPid pid, gint status, gpointer user_data)
{
    /* Nothing to do, but we need the child watch to avoid zombies */
}

/* ------------------------------------------------------------------ */
/* private api                                                        */

G_GNUC_INTERNAL
SpiceUsbAclHelper *spice_usb_acl_helper_new(void)
{
    GObject *obj;

    obj = g_object_new(SPICE_TYPE_USB_ACL_HELPER, NULL);

    return SPICE_USB_ACL_HELPER(obj);
}

G_GNUC_INTERNAL
void spice_usb_acl_helper_open_acl_async(SpiceUsbAclHelper *self,
                                         gint busnum, gint devnum,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    g_return_if_fail(SPICE_IS_USB_ACL_HELPER(self));

    SpiceUsbAclHelperPrivate *priv = self->priv;
    GTask *task;
    GError *err = NULL;
    GIOStatus status;
    GPid helper_pid;
    gsize bytes_written;
    const gchar *acl_helper = g_getenv("SPICE_USB_ACL_BINARY");
    if (acl_helper == NULL)
        acl_helper = ACL_HELPER_PATH"/spice-client-glib-usb-acl-helper";
    gchar *argv[] = { (char*)acl_helper, NULL };
    gint in, out;
    gchar buf[128];

    task = g_task_new(self, cancellable, callback, user_data);

    if (priv->out_ch) {
        g_task_return_new_error(task,
                            SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Error acl-helper already has an acl open");
        goto done;
    }

    if (g_cancellable_set_error_if_cancelled(cancellable, &err)) {
        g_task_return_error(task, err);
        goto done;
    }

    if (!g_spawn_async_with_pipes(NULL, argv, NULL,
                           G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                           NULL, NULL, &helper_pid, &in, &out, NULL, &err)) {
        g_task_return_error(task, err);
        goto done;
    }
    g_child_watch_add(helper_pid, helper_child_watch_cb, NULL);

    priv->in_ch = g_io_channel_unix_new(in);
    g_io_channel_set_close_on_unref(priv->in_ch, TRUE);

    priv->out_ch = g_io_channel_unix_new(out);
    g_io_channel_set_close_on_unref(priv->out_ch, TRUE);
    status = g_io_channel_set_flags(priv->out_ch, G_IO_FLAG_NONBLOCK, &err);
    if (status != G_IO_STATUS_NORMAL) {
        g_task_return_error(task, err);
        goto done;
    }

    snprintf(buf, sizeof(buf), "%d %d\n", busnum, devnum);
    status = g_io_channel_write_chars(priv->in_ch, buf, -1,
                                      &bytes_written, &err);
    if (status != G_IO_STATUS_NORMAL) {
        g_task_return_error(task, err);
        goto done;
    }
    status = g_io_channel_flush(priv->in_ch, &err);
    if (status != G_IO_STATUS_NORMAL) {
        g_task_return_error(task, err);
        goto done;
    }

    priv->task = task;
    if (cancellable) {
        priv->cancellable = cancellable;
        priv->cancellable_id = g_cancellable_connect(cancellable,
                                                     G_CALLBACK(cancelled_cb),
                                                     self, NULL);
    }
    g_io_add_watch(priv->out_ch, G_IO_IN|G_IO_HUP,
                   (GIOFunc)cb_out_watch, g_object_ref(self));
    return;

done:
    spice_usb_acl_helper_cleanup(self);
    g_object_unref(task);
}

G_GNUC_INTERNAL
gboolean spice_usb_acl_helper_open_acl_finish(
    SpiceUsbAclHelper *self, GAsyncResult *res, GError **err)
{
    GTask *task = G_TASK(res);

    g_return_val_if_fail(g_task_is_valid(task, self),
                         FALSE);

    return g_task_propagate_boolean(task, err);
}

G_GNUC_INTERNAL
void spice_usb_acl_helper_cancel(SpiceUsbAclHelper *self)
{
    g_return_if_fail(SPICE_IS_USB_ACL_HELPER(self));

    SpiceUsbAclHelperPrivate *priv = self->priv;
    g_return_if_fail(priv->task != NULL);

    async_result_set_cancelled(priv->task);
    g_clear_object(&priv->task);
}
