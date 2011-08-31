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

#include <glib-object.h>
#include <gio/gio.h> /* For GInitable */

#ifdef USE_USBREDIR
#include <gusb/gusb-source.h>
#include <gusb/gusb-device-list.h>
#include "channel-usbredir-priv.h"
#endif

#include "spice-client.h"
#include "spice-marshal.h"

/**
 * SECTION:usb-device-manager
 * @short_description: USB device management
 * @title: Spice USB Manager
 * @section_id:
 * @see_also:
 * @stability: Stable
 * @include: usb-device-manager.h
 *
 * #SpiceUsbDeviceManager monitors USB redirection channels and USB
 * devices plugging/unplugging. If #SpiceUsbDeviceManager:auto-connect
 * is set to %TRUE, it will automatically connect newly plugged USB
 * devices to available channels.
 */

#define SPICE_USB_DEVICE_MANAGER_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_USB_DEVICE_MANAGER, SpiceUsbDeviceManagerPrivate))

enum {
    PROP_0,
    PROP_MAIN_CONTEXT,
    PROP_AUTO_CONNECT,
};

enum
{
    DEVICE_ADDED,
    DEVICE_REMOVED,
    LAST_SIGNAL,
};

struct _SpiceUsbDeviceManagerPrivate {
    GMainContext *main_context;
    gboolean auto_connect;
#ifdef USE_USBREDIR
    GUsbContext *context;
    GUsbDeviceList *devlist;
    GUsbSource *source;
#endif
    GPtrArray *devices;
    GPtrArray *channels;
};

#ifdef USE_USBREDIR
static void spice_usb_device_manager_dev_added(GUsbDeviceList *devlist,
                                               GUsbDevice     *device,
                                               GUdevDevice    *udev,
                                               gpointer        user_data);
static void spice_usb_device_manager_dev_removed(GUsbDeviceList *devlist,
                                                 GUsbDevice     *device,
                                                 GUdevDevice    *udev,
                                                 gpointer        user_data);
#endif
static void spice_usb_device_manager_initable_iface_init(GInitableIface *iface);

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE_WITH_CODE(SpiceUsbDeviceManager, spice_usb_device_manager, G_TYPE_OBJECT,
     G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, spice_usb_device_manager_initable_iface_init));

G_DEFINE_BOXED_TYPE(SpiceUsbDevice, spice_usb_device, g_object_ref, g_object_unref)

static void spice_usb_device_manager_init(SpiceUsbDeviceManager *self)
{
    SpiceUsbDeviceManagerPrivate *priv;

    priv = SPICE_USB_DEVICE_MANAGER_GET_PRIVATE(self);
    self->priv = priv;

    priv->main_context = NULL;
    priv->channels = g_ptr_array_new();
    priv->devices  = g_ptr_array_new_with_free_func((GDestroyNotify)
                                                    g_object_unref);
#ifdef USE_USBREDIR
    priv->context = NULL;
    priv->source  = NULL;
    priv->devlist = NULL;
#endif
}

static gboolean spice_usb_device_manager_initable_init(GInitable  *initable,
                                                    GCancellable  *cancellable,
                                                    GError        **err)
{
#ifdef USE_USBREDIR
    GError *my_err = NULL;
    SpiceUsbDeviceManager *self;
    SpiceUsbDeviceManagerPrivate *priv;

    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(initable), FALSE);
    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    if (cancellable != NULL) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Cancellable initialization not supported");
    }

    self = SPICE_USB_DEVICE_MANAGER(initable);
    priv = self->priv;

    priv->context = g_usb_context_new(&my_err);
    if (priv->context == NULL) {
        g_warning("Could not get a GUsbContext, disabling USB support: %s",
                  my_err->message);
        if (err) {
            *err = my_err;
        } else {
            g_error_free(my_err);
        }
        return FALSE;
    }

    priv->devlist = g_usb_device_list_new(priv->context);
    g_signal_connect(G_OBJECT(priv->devlist), "device_added",
                     G_CALLBACK(spice_usb_device_manager_dev_added),
                     self);
    g_signal_connect(G_OBJECT(priv->devlist), "device_removed",
                     G_CALLBACK(spice_usb_device_manager_dev_removed),
                     self);
    g_usb_device_list_coldplug(priv->devlist);
    return TRUE;
#else
    g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        "USB redirection support not compiled in");
    return FALSE;
#endif
}

static void spice_usb_device_manager_finalize(GObject *gobject)
{
    SpiceUsbDeviceManager *self = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDeviceManagerPrivate *priv = self->priv;

#ifdef USE_USBREDIR
    if (priv->source)
        g_usb_source_destroy(priv->source);
    if (priv->devlist) {
        g_object_unref(priv->devlist);
        g_object_unref(priv->context);
    }
#endif

    g_ptr_array_unref(priv->channels);
    g_ptr_array_unref(priv->devices);

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_usb_device_manager_parent_class)->finalize)
        G_OBJECT_CLASS(spice_usb_device_manager_parent_class)->finalize(gobject);
}

static void spice_usb_device_manager_initable_iface_init(GInitableIface *iface)
{
    iface->init = spice_usb_device_manager_initable_init;
}

static void spice_usb_device_manager_get_property(GObject     *gobject,
                                                  guint        prop_id,
                                                  GValue      *value,
                                                  GParamSpec  *pspec)
{
    SpiceUsbDeviceManager *self = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDeviceManagerPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_MAIN_CONTEXT:
        g_value_set_pointer(value, priv->main_context);
        break;
    case PROP_AUTO_CONNECT:
        g_value_set_boolean(value, priv->auto_connect);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_usb_device_manager_set_property(GObject       *gobject,
                                                  guint          prop_id,
                                                  const GValue  *value,
                                                  GParamSpec    *pspec)
{
    SpiceUsbDeviceManager *self = SPICE_USB_DEVICE_MANAGER(gobject);
    SpiceUsbDeviceManagerPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_MAIN_CONTEXT:
        priv->main_context = g_value_get_pointer(value);
        break;
    case PROP_AUTO_CONNECT:
        priv->auto_connect = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_usb_device_manager_class_init(SpiceUsbDeviceManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
    GParamSpec *pspec;

    gobject_class->finalize     = spice_usb_device_manager_finalize;
    gobject_class->get_property = spice_usb_device_manager_get_property;
    gobject_class->set_property = spice_usb_device_manager_set_property;

    /**
     * SpiceUsbDeviceManager:main-context:
     */
    pspec = g_param_spec_pointer("main-context", "Main Context",
                                 "GMainContext to use for the event source",
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                 G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_MAIN_CONTEXT, pspec);

    /**
     * SpiceUsbDeviceManager:auto-connect:
     */
    pspec = g_param_spec_boolean("auto-connect", "Auto Connect",
                                 "Auto connect plugged in USB devices",
                                 FALSE,
                                 G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_AUTO_CONNECT, pspec);

    /**
     * SpiceUsbDeviceManager::device-added:
     * @manager: the #SpiceUsbDeviceManager that emitted the signal
     * @device: #SpiceUsbDevice boxed object corresponding to the added device
     *
     * The #SpiceUsbDeviceManager::device-added signal is emitted whenever
     * a new USB device has been plugged in.
     **/
    signals[DEVICE_ADDED] =
        g_signal_new("device-added",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceUsbDeviceManagerClass, device_added),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_USB_DEVICE);

    /**
     * SpiceUsbDeviceManager::device-removed:
     * @manager: the #SpiceUsbDeviceManager that emitted the signal
     * @device: #SpiceUsbDevice boxed object corresponding to the removed device
     *
     * The #SpiceUsbDeviceManager::device-removed signal is emitted whenever
     * an USB device has been removed.
     **/
    signals[DEVICE_REMOVED] =
        g_signal_new("device-removed",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceUsbDeviceManagerClass, device_removed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_USB_DEVICE);

    g_type_class_add_private(klass, sizeof(SpiceUsbDeviceManagerPrivate));
}

/* ------------------------------------------------------------------ */
/* callbacks                                                          */

#ifdef USE_USBREDIR
static gboolean spice_usb_device_manager_source_callback(gpointer user_data)
{
    SpiceUsbDeviceManager *self = user_data;
    SpiceUsbDeviceManagerPrivate *priv = self->priv;
    guint i;

    /*
     * Flush any writes which may have been caused by async usb packets
     * completing.
     */
    for (i = 0; i < priv->channels->len; i++) {
        SpiceUsbredirChannel *channel = g_ptr_array_index(priv->channels, i);

        spice_usbredir_channel_do_write(channel);
    }

    return TRUE;
}

static void spice_usb_device_manager_dev_added(GUsbDeviceList *devlist,
                                               GUsbDevice     *device,
                                               GUdevDevice    *udev,
                                               gpointer        user_data)
{
    SpiceUsbDeviceManager *manager = user_data;
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    g_ptr_array_add(priv->devices, g_object_ref(device));

    if (priv->auto_connect) {
        GError *err = NULL;
        spice_usb_device_manager_connect_device(manager,
                                                (SpiceUsbDevice *)device,
                                                &err);
        if (err) {
            g_warning("Could not auto-redirect USB device: %s", err->message);
            g_error_free(err);
        }
    }

    SPICE_DEBUG("device added %p", device);
    g_signal_emit(manager, signals[DEVICE_ADDED], 0, device);
}

static void spice_usb_device_manager_dev_removed(GUsbDeviceList *devlist,
                                                 GUsbDevice     *device,
                                                 GUdevDevice    *udev,
                                                 gpointer        user_data)
{
    SpiceUsbDeviceManager *manager = user_data;
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;

    spice_usb_device_manager_disconnect_device(manager,
                                               (SpiceUsbDevice *)device);

    SPICE_DEBUG("device removed %p", device);
    g_signal_emit(manager, signals[DEVICE_REMOVED], 0, device);
    g_ptr_array_remove(priv->devices, device);
}
#endif

struct spice_usb_device_manager_new_params {
    GMainContext *main_context;
    GError **err;
};

static SpiceUsbDeviceManager *spice_usb_device_manager_new(void *p)
{
    struct spice_usb_device_manager_new_params *params = p;

    return g_initable_new(SPICE_TYPE_USB_DEVICE_MANAGER, NULL, params->err,
                          "main-context", params->main_context, NULL);
}

/* ------------------------------------------------------------------ */
/* private api                                                        */
static SpiceUsbredirChannel *spice_usb_device_manager_get_channel_for_dev(
    SpiceUsbDeviceManager *manager, SpiceUsbDevice *_device)
{
#ifdef USE_USBREDIR
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    GUsbDevice *device = (GUsbDevice *)_device;
    guint i;

    for (i = 0; i < priv->channels->len; i++) {
        SpiceUsbredirChannel *channel = g_ptr_array_index(priv->channels, i);
        if (spice_usbredir_channel_get_device(channel) == device)
            return channel;
    }
#endif
    return NULL;
}

/* ------------------------------------------------------------------ */
/* public api                                                         */

/**
 * spice_usb_device_manager_get:
 * @main_context: #GMainContext to use. If %NULL, the default context is used.
 *
 * #SpiceUsbDeviceManager is a singleton, use this function to get a pointer
 * to it. A new #SpiceUsbDeviceManager instance will be created the first
 * time this function is called
 *
 * Returns: (transfer none): a weak reference to the #SpiceUsbDeviceManager singleton
 */
SpiceUsbDeviceManager *spice_usb_device_manager_get(GMainContext *main_context,
                                                    GError **err)
{
    static GOnce manager_singleton_once = G_ONCE_INIT;
    struct spice_usb_device_manager_new_params params;

    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    params.main_context = main_context;
    params.err = err;

    return g_once(&manager_singleton_once,
                  (GThreadFunc)spice_usb_device_manager_new,
                  &params);
}

/**
 * spice_usb_device_manager_register_channel:
 * @manager: the #SpiceUsbDeviceManager manager
 * @channel: a #SpiceUsbredirChannel to register
 *
 * Register @channel to be managed by the USB device @manager.  When a
 * new device is added/plugged, the @manager will use an available
 * channel to establish the redirection with the Spice server.
 *
 * Note that this function takes a weak reference to the channel, it is the
 * callers responsibility to call spice_usb_device_manager_unregister_channel()
 * before it unrefs its own reference.
 **/
void spice_usb_device_manager_register_channel(SpiceUsbDeviceManager *self,
                                               SpiceUsbredirChannel *channel)
{
    SpiceUsbDeviceManagerPrivate *priv;
    guint i;

    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(self));
    g_return_if_fail(SPICE_IS_USBREDIR_CHANNEL(channel));

    priv = self->priv;

    for (i = 0; i < priv->channels->len; i++) {
        if (g_ptr_array_index(priv->channels, i) == channel) {
            g_return_if_reached();
        }
    }
    g_ptr_array_add(self->priv->channels, channel);
}

/**
 * spice_usb_device_manager_unregister_channel:
 * @manager: the #SpiceUsbDeviceManager manager
 * @channel: a #SpiceUsbredirChannel to unregister
 *
 * Remove @channel from the list of USB channels to be managed by @manager.
 */
void spice_usb_device_manager_unregister_channel(SpiceUsbDeviceManager *self,
                                                 SpiceUsbredirChannel *channel)
{
    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(self));
    g_return_if_fail(SPICE_IS_USBREDIR_CHANNEL(channel));

    g_warn_if_fail(g_ptr_array_remove(self->priv->channels, channel));
}

/**
 * spice_usb_device_manager_get_devices:
 * @manager: the #SpiceUsbDeviceManager manager
 *
 * Returns: (element-type SpiceUsbDevice) (transfer full): a %GPtrArray array of %SpiceUsbDevice
 */
GPtrArray* spice_usb_device_manager_get_devices(SpiceUsbDeviceManager *self)
{
    SpiceUsbDeviceManagerPrivate *priv;
    GPtrArray *devices_copy;
    guint i;

    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(self), NULL);

    priv = self->priv;
    devices_copy = g_ptr_array_new_with_free_func((GDestroyNotify)
                                                  g_object_unref);
    for (i = 0; i < priv->devices->len; i++) {
        SpiceUsbDevice *device = g_ptr_array_index(priv->devices, i);
        g_ptr_array_add(devices_copy, g_object_ref(device));
    }

    return devices_copy;
}

/**
 * spice_usb_device_manager_is_device_connected:
 * @manager: the #SpiceUsbDeviceManager manager
 * @device: a #SpiceUsbDevice
 *
 * Returns: %TRUE if @device has an associated USB redirection channel
 */
gboolean spice_usb_device_manager_is_device_connected(SpiceUsbDeviceManager *self,
                                                      SpiceUsbDevice *device)
{
    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(self), FALSE);
    g_return_val_if_fail(device != NULL, FALSE);

    return !!spice_usb_device_manager_get_channel_for_dev(self, device);
}

/**
 * spice_usb_device_manager_connect_device:
 * @manager: the #SpiceUsbDeviceManager manager
 * @device: a #SpiceUsbDevice to redirect
 *
 * Returns: %TRUE if @device has been successfully connected and
 * associated with a redirection chanel
 */
gboolean spice_usb_device_manager_connect_device(SpiceUsbDeviceManager *self,
                                                 SpiceUsbDevice *device, GError **err)
{
    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(self), FALSE);
    g_return_val_if_fail(device != NULL, FALSE);
    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    SPICE_DEBUG("connecting device %p", device);

#ifdef USE_USBREDIR
    SpiceUsbDeviceManagerPrivate *priv = self->priv;
    guint i;

    if (spice_usb_device_manager_is_device_connected(self, device)) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Cannot connect an already connected usb device");
        return FALSE;
    }

    if (!priv->source) {
        priv->source = g_usb_source_new(priv->main_context, priv->context, err);
        if (*err)
            return FALSE;

        g_usb_source_set_callback(priv->source,
                                  spice_usb_device_manager_source_callback,
                                  self, NULL);
    }

    for (i = 0; i < priv->channels->len; i++) {
        SpiceUsbredirChannel *channel = g_ptr_array_index(priv->channels, i);

        if (spice_usbredir_channel_get_device(channel))
            continue; /* Skip already used channels */

        return spice_usbredir_channel_connect(channel, priv->context,
                                              (GUsbDevice *)device, err);
    }
#endif

    g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        "No free USB channel");
    return FALSE;
}

/**
 * spice_usb_device_manager_disconnect_device:
 * @manager: the #SpiceUsbDeviceManager manager
 * @device: a #SpiceUsbDevice to disconnect
 *
 * Returns: %TRUE if @device has an associated USB redirection channel
 */
void spice_usb_device_manager_disconnect_device(SpiceUsbDeviceManager *self,
                                                SpiceUsbDevice *device)
{
    g_return_if_fail(SPICE_IS_USB_DEVICE_MANAGER(self));
    g_return_if_fail(device != NULL);

    SPICE_DEBUG("disconnecting device %p", device);

#ifdef USE_USBREDIR
    SpiceUsbredirChannel *channel;

    channel = spice_usb_device_manager_get_channel_for_dev(self, device);
    if (channel)
        spice_usbredir_channel_disconnect(channel);
#endif
}
