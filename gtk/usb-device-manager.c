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

#include "glib-compat.h"

#ifdef USE_USBREDIR
#include <gusb/gusb-source.h>
#include <gusb/gusb-device-list.h>
#include "channel-usbredir-priv.h"
#endif

#include "spice-session-priv.h"
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
 *
 * There should always be a 1:1 relation between #SpiceUsbDeviceManager objects
 * and #SpiceSession objects. Therefor there is no
 * spice_usb_device_manager_new, instead there is
 * spice_usb_device_manager_get() which ensures this 1:1 relation.
 */

/* ------------------------------------------------------------------ */
/* Prototypes for private functions */
static void channel_new(SpiceSession *session, SpiceChannel *channel,
                        gpointer user_data);
static void channel_destroy(SpiceSession *session, SpiceChannel *channel,
                            gpointer user_data);

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_USB_DEVICE_MANAGER_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_USB_DEVICE_MANAGER, SpiceUsbDeviceManagerPrivate))

enum {
    PROP_0,
    PROP_SESSION,
    PROP_MAIN_CONTEXT,
    PROP_AUTO_CONNECT,
};

enum
{
    DEVICE_ADDED,
    DEVICE_REMOVED,
    AUTO_CONNECT_FAILED,
    LAST_SIGNAL,
};

struct _SpiceUsbDeviceManagerPrivate {
    SpiceSession *session;
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
    GList *list;
    GList *it;
    SpiceUsbDeviceManager *self;
    SpiceUsbDeviceManagerPrivate *priv;
#ifdef USE_USBREDIR
    GError *my_err = NULL;
#endif

    g_return_val_if_fail(SPICE_IS_USB_DEVICE_MANAGER(initable), FALSE);
    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    if (cancellable != NULL) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Cancellable initialization not supported");
        return FALSE;
    }

    self = SPICE_USB_DEVICE_MANAGER(initable);
    priv = self->priv;

    if (!priv->session) {
        g_set_error_literal(err, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                "SpiceUsbDeviceManager constructed without a session");
        return FALSE;
    }

    g_signal_connect(priv->session, "channel-new",
                     G_CALLBACK(channel_new), self);
    g_signal_connect(priv->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), self);
    list = spice_session_get_channels(priv->session);
    for (it = g_list_first(list); it != NULL; it = g_list_next(it)) {
        channel_new(priv->session, it->data, (gpointer*)self);
    }
    g_list_free(list);

#ifdef USE_USBREDIR
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
    case PROP_SESSION:
        g_value_set_object(value, priv->session);
        break;
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
    case PROP_SESSION:
        priv->session = g_value_get_object(value);
        break;
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
     * SpiceUsbDeviceManager:session:
     *
     * #SpiceSession this #SpiceUsbDeviceManager is associated with
     *
     **/
    g_object_class_install_property
        (gobject_class, PROP_SESSION,
         g_param_spec_object("session",
                             "Session",
                             "SpiceSession",
                             SPICE_TYPE_SESSION,
                             G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS));

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

    /**
     * SpiceUsbDeviceManager::auto-connect-failed:
     * @manager: the #SpiceUsbDeviceManager that emitted the signal
     * @device: #SpiceUsbDevice boxed object corresponding to the device which failed to auto connect
     * @error: #GError describing the reason why the autoconnect failed
     *
     * The #SpiceUsbDeviceManager::auto-connect-failed signal is emitted
     * whenever the auto-connect property is true, and a newly plugged in
     * device could not be auto-connected.
     **/
    signals[AUTO_CONNECT_FAILED] =
        g_signal_new("auto-connect-failed",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceUsbDeviceManagerClass, auto_connect_failed),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__BOXED_BOXED,
                     G_TYPE_NONE,
                     2,
                     SPICE_TYPE_USB_DEVICE,
                     G_TYPE_ERROR);

    g_type_class_add_private(klass, sizeof(SpiceUsbDeviceManagerPrivate));
}

/* ------------------------------------------------------------------ */
/* callbacks                                                          */

static void channel_new(SpiceSession *session, SpiceChannel *channel,
                        gpointer user_data)
{
    SpiceUsbDeviceManager *self = user_data;

    if (SPICE_IS_USBREDIR_CHANNEL(channel))
        g_ptr_array_add(self->priv->channels, channel);
}

static void channel_destroy(SpiceSession *session, SpiceChannel *channel,
                            gpointer user_data)
{
    SpiceUsbDeviceManager *self = user_data;

    if (SPICE_IS_USBREDIR_CHANNEL(channel))
        g_ptr_array_remove(self->priv->channels, channel);
}

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
                                               GUsbDevice     *_device,
                                               GUdevDevice    *udev,
                                               gpointer        user_data)
{
    SpiceUsbDeviceManager *manager = user_data;
    SpiceUsbDeviceManagerPrivate *priv = manager->priv;
    SpiceUsbDevice *device = (SpiceUsbDevice *)_device;

    g_ptr_array_add(priv->devices, g_object_ref(device));

    if (priv->auto_connect) {
        GError *err = NULL;
        spice_usb_device_manager_connect_device(manager, device, &err);
        if (err) {
            gchar *desc = spice_usb_device_get_description(device);
            g_prefix_error(&err, "Could not auto-redirect %s: ", desc);
            g_free(desc);

            g_warning("%s", err->message);
            g_signal_emit(manager, signals[AUTO_CONNECT_FAILED], 0, device, err);
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
 * @session: #SpiceSession for which to get the #SpiceUsbDeviceManager
 * @main_context: #GMainContext to use. If %NULL, the default context is used.
 *
 * Gets the #SpiceUsbDeviceManager associated with the passed in #SpiceSession.
 * A new #SpiceUsbDeviceManager instance will be created the first time this
 * function is called for a certain #SpiceSession.
 *
 * Note that this function returns a weak reference, which should not be used
 * after the #SpiceSession itself has been unref-ed by the caller.
 *
 * Returns: (transfer none): a weak reference to the #SpiceUsbDeviceManager associated with the passed in #SpiceSession
 */
SpiceUsbDeviceManager *spice_usb_device_manager_get(SpiceSession *session,
                                                    GMainContext *main_context,
                                                    GError **err)
{
    SpiceUsbDeviceManager *self;
    static GStaticMutex mutex = G_STATIC_MUTEX_INIT;

    g_return_val_if_fail(err == NULL || *err == NULL, NULL);

    g_static_mutex_lock(&mutex);
    self = session->priv->usb_manager;
    if (self == NULL) {
        self = g_initable_new(SPICE_TYPE_USB_DEVICE_MANAGER, NULL, err,
                              "session", session,
                              "main-context", main_context, NULL);
        session->priv->usb_manager = self;
    }
    g_static_mutex_unlock(&mutex);

    return self;
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

/**
 * spice_usb_device_get_description:
 * @device: #SpiceUsbDevice to get the description of
 *
 * Get a string describing the device which is suitable as a description of
 * the device for the end user. The returned string should be freed with
 * g_free() when no longer needed.
 *
 * Returns: a newly-allocated string holding the description
 */
gchar *spice_usb_device_get_description(SpiceUsbDevice *device)
{
#ifdef USE_USBREDIR
    /* FIXME, extend gusb to get vid:pid + usb descriptor strings, use those */
    int bus, address;

    g_return_val_if_fail(device != NULL, NULL);

    bus = g_usb_device_get_bus((GUsbDevice *)device);
    address = g_usb_device_get_address((GUsbDevice *)device);

    return g_strdup_printf("USB device at %d-%d", bus, address);
#else
    return NULL;
#endif
}
