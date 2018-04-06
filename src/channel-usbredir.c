/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010-2012 Red Hat, Inc.

   Red Hat Authors:
   Hans de Goede <hdegoede@redhat.com>
   Richard Hughes <rhughes@redhat.com>

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

#ifdef USE_USBREDIR
#include <glib/gi18n-lib.h>
#include <usbredirhost.h>
#ifdef USE_LZ4
#include <lz4.h>
#endif
#ifdef USE_POLKIT
#include "usb-acl-helper.h"
#endif
#include "channel-usbredir-priv.h"
#include "usb-device-manager-priv.h"
#include "usbutil.h"
#endif

#include "common/log.h"
#include "spice-client.h"
#include "spice-common.h"

#include "spice-channel-priv.h"

/**
 * SECTION:channel-usbredir
 * @short_description: usb redirection
 * @title: USB Redirection Channel
 * @section_id:
 * @stability: Stable
 * @include: spice-client.h
 *
 * The Spice protocol defines a set of messages to redirect USB devices
 * from the Spice client to the VM. This channel handles these messages.
 */

#ifdef USE_USBREDIR

#define COMPRESS_THRESHOLD 1000
#define SPICE_USBREDIR_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_USBREDIR_CHANNEL, SpiceUsbredirChannelPrivate))

enum SpiceUsbredirChannelState {
    STATE_DISCONNECTED,
#ifdef USE_POLKIT
    STATE_WAITING_FOR_ACL_HELPER,
#endif
    STATE_CONNECTED,
    STATE_DISCONNECTING,
};

struct _SpiceUsbredirChannelPrivate {
    libusb_device *device;
    SpiceUsbDevice *spice_device;
    libusb_context *context;
    struct usbredirhost *host;
    /* To catch usbredirhost error messages and report them as a GError */
    GError **catch_error;
    /* Data passed from channel handle msg to the usbredirhost read cb */
    const uint8_t *read_buf;
    int read_buf_size;
    enum SpiceUsbredirChannelState state;
#ifdef USE_POLKIT
    GTask *task;
    SpiceUsbAclHelper *acl_helper;
#endif
    GMutex device_connect_mutex;
    SpiceUsbDeviceManager *usb_device_manager;
};

static void channel_set_handlers(SpiceChannelClass *klass);
static void spice_usbredir_channel_up(SpiceChannel *channel);
static void spice_usbredir_channel_dispose(GObject *obj);
static void spice_usbredir_channel_finalize(GObject *obj);
static void usbredir_handle_msg(SpiceChannel *channel, SpiceMsgIn *in);

static void usbredir_log(void *user_data, int level, const char *msg);
static int usbredir_read_callback(void *user_data, uint8_t *data, int count);
static int usbredir_write_callback(void *user_data, uint8_t *data, int count);
static void usbredir_write_flush_callback(void *user_data);
#if USBREDIR_VERSION >= 0x000701
static uint64_t usbredir_buffered_output_size_callback(void *user_data);
#endif

static void *usbredir_alloc_lock(void);
static void usbredir_lock_lock(void *user_data);
static void usbredir_unlock_lock(void *user_data);
static void usbredir_free_lock(void *user_data);

#endif

G_DEFINE_TYPE(SpiceUsbredirChannel, spice_usbredir_channel, SPICE_TYPE_CHANNEL)

/* ------------------------------------------------------------------ */

static void spice_usbredir_channel_init(SpiceUsbredirChannel *channel)
{
#ifdef USE_USBREDIR
    channel->priv = SPICE_USBREDIR_CHANNEL_GET_PRIVATE(channel);
    g_mutex_init(&channel->priv->device_connect_mutex);
#endif
}

#ifdef USE_USBREDIR

static void _channel_reset_finish(SpiceUsbredirChannel *channel)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    spice_usbredir_channel_lock(channel);

    usbredirhost_close(priv->host);
    priv->host = NULL;

    /* Call set_context to re-create the host */
    spice_usbredir_channel_set_context(channel, priv->context);

    spice_usbredir_channel_unlock(channel);
}

static void _channel_reset_cb(GObject *gobject,
                              GAsyncResult *result,
                              gpointer user_data)
{
    SpiceChannel *spice_channel =  SPICE_CHANNEL(gobject);
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(spice_channel);
    gboolean migrating = GPOINTER_TO_UINT(user_data);
    GError *err = NULL;

    _channel_reset_finish(channel);

    SPICE_CHANNEL_CLASS(spice_usbredir_channel_parent_class)->channel_reset(spice_channel, migrating);

    spice_usbredir_channel_disconnect_device_finish(channel, result, &err);
    g_object_unref(result);
}

static void spice_usbredir_channel_reset(SpiceChannel *c, gboolean migrating)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(c);
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    if (priv->host) {
        if (priv->state == STATE_CONNECTED) {
            spice_usbredir_channel_disconnect_device_async(channel, NULL,
                _channel_reset_cb, GUINT_TO_POINTER(migrating));
        } else {
            _channel_reset_finish(channel);
        }
    } else {
        SPICE_CHANNEL_CLASS(spice_usbredir_channel_parent_class)->channel_reset(c, migrating);
    }
}
#endif

static void spice_usbredir_channel_class_init(SpiceUsbredirChannelClass *klass)
{
#ifdef USE_USBREDIR
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->dispose       = spice_usbredir_channel_dispose;
    gobject_class->finalize      = spice_usbredir_channel_finalize;
    channel_class->channel_up    = spice_usbredir_channel_up;
    channel_class->channel_reset = spice_usbredir_channel_reset;

    g_type_class_add_private(klass, sizeof(SpiceUsbredirChannelPrivate));
    channel_set_handlers(SPICE_CHANNEL_CLASS(klass));
#endif
}

#ifdef USE_USBREDIR
static void spice_usbredir_channel_dispose(GObject *obj)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(obj);

    spice_usbredir_channel_disconnect_device(channel);
    /* This should have been set to NULL during device disconnection,
     * but better not to leak it if this does not happen for some reason
     */
    g_warn_if_fail(channel->priv->usb_device_manager == NULL);
    g_clear_object(&channel->priv->usb_device_manager);

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_usbredir_channel_parent_class)->dispose)
        G_OBJECT_CLASS(spice_usbredir_channel_parent_class)->dispose(obj);
}

/*
 * Note we don't unref our device / acl_helper / result references in our
 * finalize. The reason for this is that depending on our state at dispose
 * time they are either:
 * 1) Already unreferenced
 * 2) Will be unreferenced by the disconnect_device call from dispose
 * 3) Will be unreferenced by spice_usbredir_channel_open_acl_cb
 *
 * Now the last one may seem like an issue, since what will happen if
 * spice_usbredir_channel_open_acl_cb will run after finalization?
 *
 * This will never happens since the GTask created before we
 * get into the STATE_WAITING_FOR_ACL_HELPER takes a reference to its
 * source object, which is our SpiceUsbredirChannel object, so
 * the finalize won't hapen until spice_usbredir_channel_open_acl_cb runs,
 * and unrefs priv->result which will in turn unref ourselve once the
 * complete_in_idle call it does has completed. And once
 * spice_usbredir_channel_open_acl_cb has run, all references we hold have
 * been released even in the 3th scenario.
 */
static void spice_usbredir_channel_finalize(GObject *obj)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(obj);

    if (channel->priv->host)
        usbredirhost_close(channel->priv->host);
#ifdef USE_USBREDIR
    g_mutex_clear(&channel->priv->device_connect_mutex);
#endif

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_usbredir_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_usbredir_channel_parent_class)->finalize(obj);
}

static void channel_set_handlers(SpiceChannelClass *klass)
{
    static const spice_msg_handler handlers[] = {
        [ SPICE_MSG_SPICEVMC_DATA ] = usbredir_handle_msg,
        [ SPICE_MSG_SPICEVMC_COMPRESSED_DATA ] = usbredir_handle_msg,
    };

    spice_channel_set_handlers(klass, handlers, G_N_ELEMENTS(handlers));
}

/* ------------------------------------------------------------------ */
/* private api                                                        */

G_GNUC_INTERNAL
void spice_usbredir_channel_set_context(SpiceUsbredirChannel *channel,
                                        libusb_context       *context)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    g_return_if_fail(priv->host == NULL);

    priv->context = context;
    priv->host = usbredirhost_open_full(
                                   context, NULL,
                                   usbredir_log,
                                   usbredir_read_callback,
                                   usbredir_write_callback,
                                   usbredir_write_flush_callback,
                                   usbredir_alloc_lock,
                                   usbredir_lock_lock,
                                   usbredir_unlock_lock,
                                   usbredir_free_lock,
                                   channel, PACKAGE_STRING,
                                   spice_util_get_debug() ? usbredirparser_debug : usbredirparser_warning,
                                   usbredirhost_fl_write_cb_owns_buffer);
    if (!priv->host)
        g_error("Out of memory allocating usbredirhost");

#if USBREDIR_VERSION >= 0x000701
    usbredirhost_set_buffered_output_size_cb(priv->host, usbredir_buffered_output_size_callback);
#endif
#ifdef USE_LZ4
    spice_channel_set_capability(channel, SPICE_SPICEVMC_CAP_DATA_COMPRESS_LZ4);
#endif
}

static gboolean spice_usbredir_channel_open_device(
    SpiceUsbredirChannel *channel, GError **err)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;
    SpiceSession *session;
    libusb_device_handle *handle = NULL;
    int rc, status;
    SpiceUsbDeviceManager *manager;

    g_return_val_if_fail(priv->state == STATE_DISCONNECTED
#ifdef USE_POLKIT
                         || priv->state == STATE_WAITING_FOR_ACL_HELPER
#endif
                         , FALSE);

    rc = libusb_open(priv->device, &handle);
    if (rc != 0) {
        g_set_error(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                    "Could not open usb device: %s [%i]",
                    spice_usbutil_libusb_strerror(rc), rc);
        return FALSE;
    }

    priv->catch_error = err;
    status = usbredirhost_set_device(priv->host, handle);
    priv->catch_error = NULL;
    if (status != usb_redir_success) {
        g_return_val_if_fail(err == NULL || *err != NULL, FALSE);
        return FALSE;
    }

    session = spice_channel_get_session(SPICE_CHANNEL(channel));
    manager = spice_usb_device_manager_get(session, NULL);
    g_return_val_if_fail(manager != NULL, FALSE);

    priv->usb_device_manager = g_object_ref(manager);
    if (!spice_usb_device_manager_start_event_listening(priv->usb_device_manager, err)) {
        usbredirhost_set_device(priv->host, NULL);
        return FALSE;
    }

    priv->state = STATE_CONNECTED;

    return TRUE;
}

#ifdef USE_POLKIT
static void spice_usbredir_channel_open_acl_cb(
    GObject *gobject, GAsyncResult *acl_res, gpointer user_data)
{
    SpiceUsbAclHelper *acl_helper = SPICE_USB_ACL_HELPER(gobject);
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(user_data);
    SpiceUsbredirChannelPrivate *priv = channel->priv;
    GError *err = NULL;

    g_return_if_fail(acl_helper == priv->acl_helper);
    g_return_if_fail(priv->state == STATE_WAITING_FOR_ACL_HELPER ||
                     priv->state == STATE_DISCONNECTING);

    spice_usb_acl_helper_open_acl_finish(acl_helper, acl_res, &err);
    if (!err && priv->state == STATE_DISCONNECTING) {
        err = g_error_new_literal(G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                  "USB redirection channel connect cancelled");
    }
    if (!err) {
        spice_usbredir_channel_open_device(channel, &err);
    }
    if (err) {
        g_clear_pointer(&priv->device, libusb_unref_device);
        g_boxed_free(spice_usb_device_get_type(), priv->spice_device);
        priv->spice_device = NULL;
        priv->state  = STATE_DISCONNECTED;
        g_task_return_error(priv->task, err);
    } else {
        g_task_return_boolean(priv->task, TRUE);
    }

    g_clear_object(&priv->acl_helper);
    g_object_set(spice_channel_get_session(SPICE_CHANNEL(channel)),
                 "inhibit-keyboard-grab", FALSE, NULL);

    g_clear_object(&priv->task);
}
#endif

#ifndef USE_POLKIT
static void
_open_device_async_cb(GTask *task,
                      gpointer object,
                      gpointer task_data,
                      GCancellable *cancellable)
{
    GError *err = NULL;
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(object);
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    spice_usbredir_channel_lock(channel);

    if (!spice_usbredir_channel_open_device(channel, &err)) {
        g_clear_pointer(&priv->device, libusb_unref_device);
        g_boxed_free(spice_usb_device_get_type(), priv->spice_device);
        priv->spice_device = NULL;
    }

    spice_usbredir_channel_unlock(channel);

    if (err) {
        g_task_return_error(task, err);
    } else {
        g_task_return_boolean(task, TRUE);
    }
}
#endif

G_GNUC_INTERNAL
void spice_usbredir_channel_connect_device_async(
                                          SpiceUsbredirChannel *channel,
                                          libusb_device        *device,
                                          SpiceUsbDevice       *spice_device,
                                          GCancellable         *cancellable,
                                          GAsyncReadyCallback   callback,
                                          gpointer              user_data)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;
    GTask *task;

    g_return_if_fail(SPICE_IS_USBREDIR_CHANNEL(channel));
    g_return_if_fail(device != NULL);

    CHANNEL_DEBUG(channel, "connecting device %04x:%04x (%p) to channel %p",
                  spice_usb_device_get_vid(spice_device),
                  spice_usb_device_get_pid(spice_device),
                  spice_device, channel);

    task = g_task_new(channel, cancellable, callback, user_data);

    if (!priv->host) {
        g_task_return_new_error(task,
                            SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Error libusb context not set");
        goto done;
    }

    if (priv->state != STATE_DISCONNECTED) {
        g_task_return_new_error(task,
                            SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Error channel is busy");
        goto done;
    }

    priv->device = libusb_ref_device(device);
    priv->spice_device = g_boxed_copy(spice_usb_device_get_type(),
                                      spice_device);
#ifdef USE_POLKIT
    priv->task = task;
    priv->state  = STATE_WAITING_FOR_ACL_HELPER;
    priv->acl_helper = spice_usb_acl_helper_new();
    g_object_set(spice_channel_get_session(SPICE_CHANNEL(channel)),
                 "inhibit-keyboard-grab", TRUE, NULL);
    spice_usb_acl_helper_open_acl_async(priv->acl_helper,
                                        libusb_get_bus_number(device),
                                        libusb_get_device_address(device),
                                        cancellable,
                                        spice_usbredir_channel_open_acl_cb,
                                        channel);
    return;
#else
    g_task_run_in_thread(task, _open_device_async_cb);
#endif

done:
    g_object_unref(task);
}

G_GNUC_INTERNAL
gboolean spice_usbredir_channel_connect_device_finish(
                                               SpiceUsbredirChannel *channel,
                                               GAsyncResult         *res,
                                               GError              **err)
{
    GTask *task = G_TASK(res);

    g_return_val_if_fail(g_task_is_valid(task, channel), FALSE);

    return g_task_propagate_boolean(task, err);
}

G_GNUC_INTERNAL
void spice_usbredir_channel_disconnect_device(SpiceUsbredirChannel *channel)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    CHANNEL_DEBUG(channel, "disconnecting device from usb channel %p", channel);

    spice_usbredir_channel_lock(channel);

    switch (priv->state) {
    case STATE_DISCONNECTED:
    case STATE_DISCONNECTING:
        break;
#ifdef USE_POLKIT
    case STATE_WAITING_FOR_ACL_HELPER:
        priv->state = STATE_DISCONNECTING;
        /* We're still waiting for the acl helper -> cancel it */
        spice_usb_acl_helper_cancel(priv->acl_helper);
        break;
#endif
    case STATE_CONNECTED:
        /*
         * This sets the usb event thread run condition to FALSE, therefor
         * it must be done before usbredirhost_set_device NULL, as
         * usbredirhost_set_device NULL will interrupt the
         * libusb_handle_events call in the thread.
         */
        g_warn_if_fail(priv->usb_device_manager != NULL);
        spice_usb_device_manager_stop_event_listening(priv->usb_device_manager);
        g_clear_object(&priv->usb_device_manager);

        /* This also closes the libusb handle we passed from open_device */
        usbredirhost_set_device(priv->host, NULL);
        g_clear_pointer(&priv->device, libusb_unref_device);
        g_boxed_free(spice_usb_device_get_type(), priv->spice_device);
        priv->spice_device = NULL;
        priv->state  = STATE_DISCONNECTED;
        break;
    }

    spice_usbredir_channel_unlock(channel);
}

static void
_disconnect_device_thread(GTask *task,
                          gpointer object,
                          gpointer task_data,
                          GCancellable *cancellable)
{
    spice_usbredir_channel_disconnect_device(SPICE_USBREDIR_CHANNEL(object));
    g_task_return_boolean(task, TRUE);
}

G_GNUC_INTERNAL
gboolean spice_usbredir_channel_disconnect_device_finish(
                                               SpiceUsbredirChannel *channel,
                                               GAsyncResult         *res,
                                               GError              **err)
{
    return g_task_propagate_boolean(G_TASK(res), err);
}

G_GNUC_INTERNAL
void spice_usbredir_channel_disconnect_device_async(SpiceUsbredirChannel *channel,
                                                    GCancellable *cancellable,
                                                    GAsyncReadyCallback callback,
                                                    gpointer user_data)
{
    GTask* task = g_task_new(channel, cancellable, callback, user_data);

    g_return_if_fail(channel != NULL);
    g_task_run_in_thread(task, _disconnect_device_thread);
    g_object_unref(task);
}

#ifdef USE_LZ4
static SpiceUsbDevice *
spice_usbredir_channel_get_spice_usb_device(SpiceUsbredirChannel *channel)
{
    return channel->priv->spice_device;
}
#endif

G_GNUC_INTERNAL
libusb_device *spice_usbredir_channel_get_device(SpiceUsbredirChannel *channel)
{
    return channel->priv->device;
}

G_GNUC_INTERNAL
void spice_usbredir_channel_get_guest_filter(
                          SpiceUsbredirChannel               *channel,
                          const struct usbredirfilter_rule  **rules_ret,
                          int                                *rules_count_ret)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    g_return_if_fail(priv->host != NULL);

    usbredirhost_get_guest_filter(priv->host, rules_ret, rules_count_ret);
}

/* ------------------------------------------------------------------ */
/* callbacks (any context)                                            */

#if USBREDIR_VERSION >= 0x000701
static uint64_t usbredir_buffered_output_size_callback(void *user_data)
{
    g_return_val_if_fail(SPICE_IS_USBREDIR_CHANNEL(user_data), 0);
    return spice_channel_get_queue_size(SPICE_CHANNEL(user_data));
}
#endif

/* Note that this function must be re-entrant safe, as it can get called
   from both the main thread as well as from the usb event handling thread */
static void usbredir_write_flush_callback(void *user_data)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(user_data);
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    if (spice_channel_get_state(SPICE_CHANNEL(channel)) !=
            SPICE_CHANNEL_STATE_READY)
        return;

    if (!priv->host)
        return;

    usbredirhost_write_guest_data(priv->host);
}

static void usbredir_log(void *user_data, int level, const char *msg)
{
    SpiceUsbredirChannel *channel = user_data;
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    if (priv->catch_error && level == usbredirparser_error) {
        CHANNEL_DEBUG(channel, "%s", msg);
        /* Remove "usbredirhost: " prefix from usbredirhost messages */
        if (strncmp(msg, "usbredirhost: ", 14) == 0)
            g_set_error_literal(priv->catch_error, SPICE_CLIENT_ERROR,
                                SPICE_CLIENT_ERROR_FAILED, msg + 14);
        else
            g_set_error_literal(priv->catch_error, SPICE_CLIENT_ERROR,
                                SPICE_CLIENT_ERROR_FAILED, msg);
        return;
    }

    switch (level) {
        case usbredirparser_error:
            g_critical("%s", msg); break;
        case usbredirparser_warning:
            g_warning("%s", msg); break;
        default:
            CHANNEL_DEBUG(channel, "%s", msg); break;
    }
}

static int usbredir_read_callback(void *user_data, uint8_t *data, int count)
{
    SpiceUsbredirChannel *channel = user_data;
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    if (priv->read_buf_size < count) {
        count = priv->read_buf_size;
    }

    memcpy(data, priv->read_buf, count);

    priv->read_buf_size -= count;
    if (priv->read_buf_size) {
        priv->read_buf += count;
    } else {
        priv->read_buf = NULL;
    }

    return count;
}

static void usbredir_free_write_cb_data(uint8_t *data, void *user_data)
{
    SpiceUsbredirChannel *channel = user_data;
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    usbredirhost_free_write_buffer(priv->host, data);
}

#ifdef USE_LZ4
static int try_write_compress_LZ4(SpiceUsbredirChannel *channel, uint8_t *data, int count)
{
    SpiceChannelPrivate *c;
    SpiceMsgOut *msg_out_compressed;
    int bound, compressed_data_count;
    uint8_t *compressed_buf;
    SpiceMsgCompressedData compressed_data_msg = {
        .type = SPICE_DATA_COMPRESSION_TYPE_LZ4,
        .uncompressed_size = count
    };

    c = SPICE_CHANNEL(channel)->priv;
    if (g_socket_get_family(c->sock) == G_SOCKET_FAMILY_UNIX) {
        /* AF_LOCAL socket - data will not be compressed */
        return FALSE;
    }
    if (count <= COMPRESS_THRESHOLD) {
        /* Not enough data to compress */
        return FALSE;
    }
    if (!spice_channel_test_capability(SPICE_CHANNEL(channel),
                                       SPICE_SPICEVMC_CAP_DATA_COMPRESS_LZ4)) {
        /* No server compression capability - data will not be compressed */
        return FALSE;
    }
    if (spice_usb_device_is_isochronous(spice_usbredir_channel_get_spice_usb_device(channel))) {
        /* Don't compress - one of the device endpoints is isochronous */
        return FALSE;
    }
    bound = LZ4_compressBound(count);
    if (bound == 0) {
        /* Invalid bound - data will not be compressed */
        return FALSE;
    }

    compressed_buf = g_malloc(bound);
    compressed_data_count = LZ4_compress_default((char*)data,
                                                 (char*)compressed_buf,
                                                 count,
                                                 bound);
    if (compressed_data_count > 0 && compressed_data_count < count) {
        compressed_data_msg.compressed_data = compressed_buf;
        msg_out_compressed = spice_msg_out_new(SPICE_CHANNEL(channel),
                                               SPICE_MSGC_SPICEVMC_COMPRESSED_DATA);
        msg_out_compressed->marshallers->msg_SpiceMsgCompressedData(msg_out_compressed->marshaller,
                                                                    &compressed_data_msg);
        spice_marshaller_add_by_ref_full(msg_out_compressed->marshaller,
                                         compressed_data_msg.compressed_data,
                                         compressed_data_count,
                                         (spice_marshaller_item_free_func)g_free,
                                         channel);
        spice_msg_out_send(msg_out_compressed);
        return TRUE;
    }

    /* if not - free & fallback to sending the message uncompressed */
    g_free(compressed_buf);
    return FALSE;
}
#endif

static int usbredir_write_callback(void *user_data, uint8_t *data, int count)
{
    SpiceUsbredirChannel *channel = user_data;
    SpiceMsgOut *msg_out;

#ifdef USE_LZ4
    if (try_write_compress_LZ4(channel, data, count)) {
        usbredirhost_free_write_buffer(channel->priv->host, data);
        return count;
    }
#endif
    msg_out = spice_msg_out_new(SPICE_CHANNEL(channel),
                                SPICE_MSGC_SPICEVMC_DATA);
    spice_marshaller_add_by_ref_full(msg_out->marshaller, data, count,
                                     usbredir_free_write_cb_data, channel);
    spice_msg_out_send(msg_out);

    return count;
}

static void *usbredir_alloc_lock(void) {
    GMutex *mutex;

    mutex = g_new0(GMutex, 1);
    g_mutex_init(mutex);

    return mutex;
}

G_GNUC_INTERNAL
void spice_usbredir_channel_lock(SpiceUsbredirChannel *channel)
{
    g_mutex_lock(&channel->priv->device_connect_mutex);
}

G_GNUC_INTERNAL
void spice_usbredir_channel_unlock(SpiceUsbredirChannel *channel)
{
    g_mutex_unlock(&channel->priv->device_connect_mutex);
}

static void usbredir_lock_lock(void *user_data) {
    GMutex *mutex = user_data;

    g_mutex_lock(mutex);
}

static void usbredir_unlock_lock(void *user_data) {
    GMutex *mutex = user_data;

    g_mutex_unlock(mutex);
}

static void usbredir_free_lock(void *user_data) {
    GMutex *mutex = user_data;

    g_mutex_clear(mutex);
    g_free(mutex);
}

/* --------------------------------------------------------------------- */

typedef struct device_error_data {
    SpiceUsbredirChannel *channel;
    SpiceUsbDevice *spice_device;
    GError *error;
    struct coroutine *caller;
} device_error_data;

/* main context */
static gboolean device_error(gpointer user_data)
{
    device_error_data *data = user_data;
    SpiceUsbredirChannel *channel = data->channel;
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    /* Check that the device has not changed before we manage to run */
    if (data->spice_device == priv->spice_device) {
        spice_usbredir_channel_disconnect_device(channel);
        spice_usb_device_manager_device_error(
                spice_usb_device_manager_get(
                    spice_channel_get_session(SPICE_CHANNEL(channel)), NULL),
                data->spice_device, data->error);
    }

    coroutine_yieldto(data->caller, NULL);
    return FALSE;
}

/* --------------------------------------------------------------------- */
/* coroutine context                                                     */
static void spice_usbredir_channel_up(SpiceChannel *c)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(c);
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    g_return_if_fail(priv->host != NULL);
    /* Flush any pending writes */
    usbredirhost_write_guest_data(priv->host);
}

static int try_handle_compressed_msg(SpiceMsgCompressedData *compressed_data_msg,
                                     uint8_t **buf,
                                     int *size) {
    int decompressed_size = 0;
    char *decompressed = NULL;

    if (compressed_data_msg->uncompressed_size == 0) {
        spice_warning("Invalid uncompressed_size");
        return FALSE;
    }

    switch (compressed_data_msg->type) {
#ifdef USE_LZ4
    case SPICE_DATA_COMPRESSION_TYPE_LZ4:
        decompressed = g_malloc(compressed_data_msg->uncompressed_size);
        decompressed_size = LZ4_decompress_safe ((char*)compressed_data_msg->compressed_data,
                                                 decompressed,
                                                 compressed_data_msg->compressed_size,
                                                 compressed_data_msg->uncompressed_size);
        break;
#endif
    default:
        spice_warning("Unknown Compression Type");
        return FALSE;
    }
    if (decompressed_size != compressed_data_msg->uncompressed_size) {
        spice_warning("Decompress Error decompressed_size=%d expected=%u",
                      decompressed_size, compressed_data_msg->uncompressed_size);
        g_free(decompressed);
        return FALSE;
    }

    *size = decompressed_size;
    *buf = (uint8_t*)decompressed;
    return TRUE;

}

static void usbredir_handle_msg(SpiceChannel *c, SpiceMsgIn *in)
{
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(c);
    SpiceUsbredirChannelPrivate *priv = channel->priv;
    int r = 0, size;
    uint8_t *buf;

    g_return_if_fail(priv->host != NULL);

    /* No recursion allowed! */
    g_return_if_fail(priv->read_buf == NULL);

    if (spice_msg_in_type(in) == SPICE_MSG_SPICEVMC_COMPRESSED_DATA) {
        SpiceMsgCompressedData *compressed_data_msg = spice_msg_in_parsed(in);
        if (try_handle_compressed_msg(compressed_data_msg, &buf, &size)) {
            priv->read_buf_size = size;
            priv->read_buf = buf;
        } else {
            r = usbredirhost_read_parse_error;
        }
    } else { /* Regular SPICE_MSG_SPICEVMC_DATA msg */
        buf = spice_msg_in_raw(in, &size);
        priv->read_buf_size = size;
        priv->read_buf = buf;
    }

    spice_usbredir_channel_lock(channel);
    if (r == 0)
        r = usbredirhost_read_guest_data(priv->host);
    if (r != 0) {
        SpiceUsbDevice *spice_device = priv->spice_device;
        device_error_data err_data;
        gchar *desc;
        GError *err;

        if (spice_device == NULL) {
            spice_usbredir_channel_unlock(channel);
            return;
        }

        desc = spice_usb_device_get_description(spice_device, NULL);
        switch (r) {
        case usbredirhost_read_parse_error:
            err = g_error_new(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                              _("usbredir protocol parse error for %s"), desc);
            break;
        case usbredirhost_read_device_rejected:
            err = g_error_new(SPICE_CLIENT_ERROR,
                              SPICE_CLIENT_ERROR_USB_DEVICE_REJECTED,
                              _("%s rejected by host"), desc);
            break;
        case usbredirhost_read_device_lost:
            err = g_error_new(SPICE_CLIENT_ERROR,
                              SPICE_CLIENT_ERROR_USB_DEVICE_LOST,
                              _("%s disconnected (fatal IO error)"), desc);
            break;
        default:
            err = g_error_new(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                              _("Unknown error (%d) for %s"), r, desc);
        }
        g_free(desc);

        CHANNEL_DEBUG(c, "%s", err->message);

        err_data.channel = channel;
        err_data.caller = coroutine_self();
        err_data.spice_device = g_boxed_copy(spice_usb_device_get_type(), spice_device);
        err_data.error = err;
        spice_usbredir_channel_unlock(channel);
        g_idle_add(device_error, &err_data);
        coroutine_yield(NULL);

        g_boxed_free(spice_usb_device_get_type(), err_data.spice_device);

        g_error_free(err);
    } else {
        spice_usbredir_channel_unlock(channel);
    }
}

#endif /* USE_USBREDIR */
