/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright 2010-2011 Red Hat, Inc.

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
#include <usbredirhost.h>
#include <gusb/gusb-context-private.h>
#include <gusb/gusb-device-private.h>
#include <gusb/gusb-util.h>
#include "channel-usbredir-priv.h"
#endif

#include "spice-client.h"
#include "spice-common.h"

#include "spice-channel-priv.h"

/**
 * SECTION:channel-usbredir
 * @short_description: usb redirection
 * @title: USB Redirection Channel
 * @section_id:
 * @stability: API Stable (channel in development)
 * @include: channel-usbredir.h
 *
 * The Spice protocol defines a set of messages to redirect USB devices
 * from the Spice client to the VM. This channel handles these messages.
 */

#define SPICE_USBREDIR_CHANNEL_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_USBREDIR_CHANNEL, SpiceUsbredirChannelPrivate))

struct _SpiceUsbredirChannelPrivate {
#ifdef USE_USBREDIR
    GUsbContext *context;
    GUsbDevice *device;
    struct usbredirhost *host;
    /* To catch usbredirhost error messages and report them as a GError */
    GError **catch_error;
    /* Data passed from channel handle msg to the usbredirhost read cb */
    const uint8_t *read_buf;
    int read_buf_size;
    SpiceMsgOut *msg_out;
    gboolean up;
#endif
};

G_DEFINE_TYPE(SpiceUsbredirChannel, spice_usbredir_channel, SPICE_TYPE_CHANNEL)

static void spice_usbredir_handle_msg(SpiceChannel *channel, SpiceMsgIn *msg);
static void spice_usbredir_channel_up(SpiceChannel *channel);
static void usbredir_handle_msg(SpiceChannel *channel, SpiceMsgIn *in);

#ifdef USE_USBREDIR
static void usbredir_log(void *user_data, int level, const char *msg);
static int usbredir_read_callback(void *user_data, uint8_t *data, int count);
static int usbredir_write_callback(void *user_data, uint8_t *data, int count);
#endif

/* ------------------------------------------------------------------ */

static void spice_usbredir_channel_init(SpiceUsbredirChannel *channel)
{
    channel->priv = SPICE_USBREDIR_CHANNEL_GET_PRIVATE(channel);
}

static void spice_usbredir_channel_finalize(GObject *obj)
{
#ifdef USE_USBREDIR
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(obj);

    spice_usbredir_channel_disconnect(channel);
#endif

    if (G_OBJECT_CLASS(spice_usbredir_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_usbredir_channel_parent_class)->finalize(obj);
}

static void spice_usbredir_channel_class_init(SpiceUsbredirChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->finalize     = spice_usbredir_channel_finalize;
    channel_class->handle_msg   = spice_usbredir_handle_msg;
    channel_class->channel_up   = spice_usbredir_channel_up;

    g_type_class_add_private(klass, sizeof(SpiceUsbredirChannelPrivate));
}

static const spice_msg_handler usbredir_handlers[] = {
    [ SPICE_MSG_SPICEVMC_DATA ] = usbredir_handle_msg,
};

#ifdef USE_USBREDIR

/* ------------------------------------------------------------------ */
/* private api                                                        */

G_GNUC_INTERNAL
gboolean spice_usbredir_channel_connect(SpiceUsbredirChannel *channel,
                                        GUsbContext *context,
                                        GUsbDevice *device,
                                        GError **err)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;
    libusb_device_handle *handle = NULL;
    int rc;

    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    SPICE_DEBUG("connecting usb channel %p", channel);

    spice_usbredir_channel_disconnect(channel);

    rc = libusb_open(_g_usb_device_get_device(device), &handle);
    if (rc != 0) {
        g_set_error(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                    "Could not open usb device: %s [%i]",
                    gusb_strerror(rc), rc);
        return FALSE;
    }

    priv->catch_error = err;
    priv->host = usbredirhost_open(_g_usb_context_get_context(context),
                                   handle, usbredir_log,
                                   usbredir_read_callback,
                                   usbredir_write_callback,
                                   channel, PACKAGE_STRING,
                                   spice_util_get_debug() ? usbredirparser_debug : usbredirparser_warning,
                                   usbredirhost_fl_write_cb_owns_buffer);
    priv->catch_error = NULL;
    if (!priv->host) {
        return FALSE;
    }

    priv->context = g_object_ref(context);
    priv->device  = g_object_ref(device);

    spice_channel_connect(SPICE_CHANNEL(channel));

    return TRUE;
}

G_GNUC_INTERNAL
void spice_usbredir_channel_disconnect(SpiceUsbredirChannel *channel)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    SPICE_DEBUG("disconnecting usb channel %p", channel);

    spice_channel_disconnect(SPICE_CHANNEL(channel), SPICE_CHANNEL_NONE);
    priv->up = FALSE;

    if (priv->host) {
        /* This also closes the libusb handle we passed to its _open */
        usbredirhost_close(priv->host);
        priv->host = NULL;
        g_clear_object(&priv->device);
        g_clear_object(&priv->context);
    }
}

G_GNUC_INTERNAL
GUsbDevice *spice_usbredir_channel_get_device(SpiceUsbredirChannel *channel)
{
    return channel->priv->device;
}

G_GNUC_INTERNAL
void spice_usbredir_channel_do_write(SpiceUsbredirChannel *channel)
{
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    /* No recursion allowed! */
    g_return_if_fail(priv->msg_out == NULL);

    if (!priv->up || !usbredirhost_has_data_to_write(priv->host))
        return;

    priv->msg_out = spice_msg_out_new(SPICE_CHANNEL(channel),
                                      SPICE_MSGC_SPICEVMC_DATA);

    /* Collect all pending writes in priv->msg_out->marshaller */
    usbredirhost_write_guest_data(priv->host);

    spice_msg_out_send(priv->msg_out);
    spice_msg_out_unref(priv->msg_out);
    priv->msg_out = NULL;
}

/* ------------------------------------------------------------------ */
/* callbacks (any context)                                            */

static void usbredir_log(void *user_data, int level, const char *msg)
{
    SpiceUsbredirChannel *channel = user_data;
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    if (priv->catch_error && level == usbredirparser_error) {
        SPICE_DEBUG("%s", msg);
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
            SPICE_DEBUG("%s", msg); break;
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

static int usbredir_write_callback(void *user_data, uint8_t *data, int count)
{
    SpiceUsbredirChannel *channel = user_data;
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    spice_marshaller_add_ref_full(priv->msg_out->marshaller, data, count,
                                  usbredir_free_write_cb_data, channel);
    return count;
}

#endif /* USE_USBREDIR */

/* --------------------------------------------------------------------- */
/* coroutine context                                                     */
static void spice_usbredir_handle_msg(SpiceChannel *c, SpiceMsgIn *msg)
{
    int type = spice_msg_in_type(msg);
    SpiceChannelClass *parent_class;

    g_return_if_fail(type < SPICE_N_ELEMENTS(usbredir_handlers));

    parent_class = SPICE_CHANNEL_CLASS(spice_usbredir_channel_parent_class);

    if (usbredir_handlers[type] != NULL)
        usbredir_handlers[type](c, msg);
    else if (parent_class->handle_msg)
        parent_class->handle_msg(c, msg);
    else
        g_return_if_reached();
}

static void spice_usbredir_channel_up(SpiceChannel *c)
{
#ifdef USE_USBREDIR
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(c);
    SpiceUsbredirChannelPrivate *priv = channel->priv;

    priv->up = TRUE;
    /* Flush any pending writes */
    spice_usbredir_channel_do_write(channel);
#endif
}

static void usbredir_handle_msg(SpiceChannel *c, SpiceMsgIn *in)
{
#ifdef USE_USBREDIR
    SpiceUsbredirChannel *channel = SPICE_USBREDIR_CHANNEL(c);
    SpiceUsbredirChannelPrivate *priv = channel->priv;
    int size;
    uint8_t *buf;

    g_return_if_fail(priv->host != NULL);

    /* No recursion allowed! */
    g_return_if_fail(priv->read_buf == NULL);

    buf = spice_msg_in_raw(in, &size);
    priv->read_buf = buf;
    priv->read_buf_size = size;

    usbredirhost_read_guest_data(priv->host);
    /* Send any acks, etc. which may be queued now */
    spice_usbredir_channel_do_write(channel);
#endif
}
