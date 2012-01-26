/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2012 Red Hat, Inc.

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
#include <glib/gi18n.h>
#include "glib-compat.h"
#include "spice-client.h"
#include "spice-marshal.h"
#include "usb-device-widget.h"

/**
 * SECTION:usb-device-widget
 * @short_description: USB device selection widget
 * @title: Spice USB device selection widget
 * @section_id:
 * @see_also:
 * @stability: Stable
 * @include: usb-device-widget.h
 *
 * #SpiceUsbDeviceWidget is a gtk widget which apps can use to easily
 * add an UI to select USB devices to redirect (or unredirect).
 */

/* ------------------------------------------------------------------ */
/* Prototypes for callbacks  */
static void device_added_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data);
static void device_removed_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data);

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_USB_DEVICE_WIDGET_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_USB_DEVICE_WIDGET, \
                                 SpiceUsbDeviceWidgetPrivate))

enum {
    PROP_0,
    PROP_SESSION,
    PROP_DEVICE_FORMAT_STRING,
};

enum {
    CONNECT_FAILED,
    LAST_SIGNAL,
};

struct _SpiceUsbDeviceWidgetPrivate {
    SpiceSession *session;
    gchar *device_format_string;
    SpiceUsbDeviceManager *manager;
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE(SpiceUsbDeviceWidget, spice_usb_device_widget, GTK_TYPE_VBOX);

static void spice_usb_device_widget_get_property(GObject     *gobject,
                                                 guint        prop_id,
                                                 GValue      *value,
                                                 GParamSpec  *pspec)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(gobject);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, priv->session);
        break;
    case PROP_DEVICE_FORMAT_STRING:
        g_value_set_string(value, priv->device_format_string);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_usb_device_widget_set_property(GObject       *gobject,
                                                 guint          prop_id,
                                                 const GValue  *value,
                                                 GParamSpec    *pspec)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(gobject);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        priv->session = g_value_dup_object(value);
        break;
    case PROP_DEVICE_FORMAT_STRING:
        priv->device_format_string = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static GObject *spice_usb_device_widget_constructor(
    GType gtype, guint n_properties, GObjectConstructParam *properties)
{
    GObject *obj;
    SpiceUsbDeviceWidget *self;
    SpiceUsbDeviceWidgetPrivate *priv;
    const gchar *err_msg = NULL;
    GPtrArray *devices = NULL;
    GError *err = NULL;
    GtkWidget *label;
    gboolean enabled;
    int i;

    {
        /* Always chain up to the parent constructor */
        GObjectClass *parent_class;
        parent_class = G_OBJECT_CLASS(spice_usb_device_widget_parent_class);
        obj = parent_class->constructor(gtype, n_properties, properties);
    }

    self = SPICE_USB_DEVICE_WIDGET(obj);
    priv = self->priv;
    if (!priv->session)
        g_error("SpiceUsbDeviceWidget constructed without a session");

    g_object_get(G_OBJECT(priv->session), "enable-usbredir", &enabled, NULL);
    if (!enabled)
        err_msg = _("USB redirection is disabled");

    if (!err_msg && !spice_session_has_channel_type(priv->session,
                                                    SPICE_CHANNEL_USBREDIR))
        err_msg = _("The connected VM is not configured for USB redirection");

    if (!err_msg) {
        priv->manager = spice_usb_device_manager_get(priv->session, &err);
        if (!err) {
            g_signal_connect(priv->manager, "device-added",
                             G_CALLBACK(device_added_cb), self);
            g_signal_connect(priv->manager, "device-removed",
                             G_CALLBACK(device_removed_cb), self);
            devices = spice_usb_device_manager_get_devices(priv->manager);
        } else
            err_msg = err->message;
    }

    if (err_msg) {
        label = gtk_label_new(err_msg);
        gtk_box_pack_start(GTK_BOX(self), label, TRUE, TRUE, 5);
        g_clear_error(&err);
        return obj;
    }

    label = gtk_label_new(_("Select USB devices to redirect"));
    gtk_box_pack_start(GTK_BOX(self), label, TRUE, TRUE, 5);

    for (i = 0; i < devices->len; i++)
        device_added_cb(NULL, g_ptr_array_index(devices, i), self);

    g_ptr_array_unref(devices);

    return obj;
}

static void spice_usb_device_widget_finalize(GObject *object)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(object);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;

    if (priv->manager) {
        g_signal_handlers_disconnect_by_func(priv->manager,
                                             device_added_cb, self);
        g_signal_handlers_disconnect_by_func(priv->manager,
                                             device_removed_cb, self);
    }
    g_object_unref(priv->session);
    g_free(priv->device_format_string);
}

static void spice_usb_device_widget_class_init(
    SpiceUsbDeviceWidgetClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GParamSpec *pspec;

    g_type_class_add_private (klass, sizeof (SpiceUsbDeviceWidgetPrivate));

    gobject_class->constructor  = spice_usb_device_widget_constructor;
    gobject_class->finalize     = spice_usb_device_widget_finalize;
    gobject_class->get_property = spice_usb_device_widget_get_property;
    gobject_class->set_property = spice_usb_device_widget_set_property;

    /**
     * SpiceUsbDeviceWidget:session:
     *
     * #SpiceSession this #SpiceUsbDeviceWidget is associated with
     *
     **/
    pspec = g_param_spec_object("session",
                                "Session",
                                "SpiceSession",
                                SPICE_TYPE_SESSION,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_SESSION, pspec);

    /**
     * SpiceUsbDeviceWidget:device-format-string:
     *
     * Format string to pass to spice_usb_device_get_description() for getting
     * the device USB descriptions.
     */
    pspec = g_param_spec_string("device-format-string",
                                "Device format string",
                                "Format string for device description",
                                NULL,
                                G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                                G_PARAM_STATIC_STRINGS);
    g_object_class_install_property(gobject_class, PROP_DEVICE_FORMAT_STRING,
                                    pspec);

    /**
     * SpiceUsbDeviceWidget::connect-failed:
     * @widget: The #SpiceUsbDeviceWidget that emitted the signal
     * @device: #SpiceUsbDevice boxed object corresponding to the added device
     * @error:  #GError describing the reason why the connect failed
     *
     * The #SpiceUsbDeviceWidget::connect-failed signal is emitted whenever
     * the user has requested for a device to be redirected and this has
     * failed.
     **/
    signals[CONNECT_FAILED] =
        g_signal_new("connect-failed",
                    G_OBJECT_CLASS_TYPE(gobject_class),
                    G_SIGNAL_RUN_FIRST,
                    G_STRUCT_OFFSET(SpiceUsbDeviceWidgetClass, connect_failed),
                    NULL, NULL,
                    g_cclosure_user_marshal_VOID__BOXED_BOXED,
                    G_TYPE_NONE,
                    2,
                    SPICE_TYPE_USB_DEVICE,
                    G_TYPE_ERROR);
}

static void spice_usb_device_widget_init(SpiceUsbDeviceWidget *self)
{
    self->priv = SPICE_USB_DEVICE_WIDGET_GET_PRIVATE(self);
}

/* ------------------------------------------------------------------ */
/* public api                                                         */

/**
 * spice_usb_device_widget_new:
 * @session: #SpiceSession for which to widget will control USB redirection
 * @device_format_string: String passed to spice_usb_device_get_description()
 *
 * Returns: a new #SpiceUsbDeviceWidget instance
 */
GtkWidget *spice_usb_device_widget_new(SpiceSession    *session,
                                       const gchar     *device_format_string)
{
    return g_object_new(SPICE_TYPE_USB_DEVICE_WIDGET,
                        "session", session,
                        "device-format-string", device_format_string,
                        NULL);
}

/* ------------------------------------------------------------------ */
/* callbacks                                                          */
typedef struct _connect_cb_data {
    GtkWidget *check;
    SpiceUsbDeviceWidget *self;
} connect_cb_data;

static void connect_cb(GObject *gobject, GAsyncResult *res, gpointer user_data)
{
    SpiceUsbDeviceManager *manager = SPICE_USB_DEVICE_MANAGER(gobject);
    connect_cb_data *data = user_data;
    SpiceUsbDeviceWidget *self = data->self;
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    SpiceUsbDevice *device;
    GError *err = NULL;
    gchar *desc;

    spice_usb_device_manager_connect_device_finish(manager, res, &err);
    if (err) {
        device = g_object_get_data(G_OBJECT(data->check), "usb-device");
        desc = spice_usb_device_get_description(device,
                                                priv->device_format_string);
        g_prefix_error(&err, "Could not redirect %s: ", desc);
        g_free(desc);

        SPICE_DEBUG("%s", err->message);
        g_signal_emit(self, signals[CONNECT_FAILED], 0, device, err);
        g_error_free(err);

        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->check), FALSE);
    }

    g_object_unref(data->check);
    g_object_unref(data->self);
    g_free(data);
}

static void checkbox_clicked_cb(GtkWidget *check, gpointer user_data)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(user_data);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    SpiceUsbDevice *device;

    device = g_object_get_data(G_OBJECT(check), "usb-device");

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check))) {
        connect_cb_data *data = g_new(connect_cb_data, 1);
        data->check = g_object_ref(check);
        data->self  = g_object_ref(self);
        spice_usb_device_manager_connect_device_async(priv->manager,
                                                      device,
                                                      NULL,
                                                      connect_cb,
                                                      data);
    } else {
        spice_usb_device_manager_disconnect_device(priv->manager,
                                                   device);
    }
}

static void checkbox_usb_device_destroy_notify(gpointer data)
{
    g_boxed_free(spice_usb_device_get_type(), data);
}

static void device_added_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(user_data);
    SpiceUsbDeviceWidgetPrivate *priv = self->priv;
    GtkWidget *check;
    gchar *desc;

    desc = spice_usb_device_get_description(device,
                                            priv->device_format_string);

    check = gtk_check_button_new_with_label(desc);

    if (spice_usb_device_manager_is_device_connected(priv->manager,
                                                     device))
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);

    g_object_set_data_full(
            G_OBJECT(check), "usb-device",
            g_boxed_copy(spice_usb_device_get_type(), device),
            checkbox_usb_device_destroy_notify);
    g_signal_connect(G_OBJECT(check), "clicked",
                     G_CALLBACK(checkbox_clicked_cb), self);

    gtk_box_pack_start(GTK_BOX(self), check, TRUE, TRUE, 5);
    gtk_widget_show(check);

    g_free(desc);
}

static void destroy_widget_by_usb_device(GtkWidget *widget, gpointer user_data)
{
    if (g_object_get_data(G_OBJECT(widget), "usb-device") == user_data)
        gtk_widget_destroy(widget);
}

static void device_removed_cb(SpiceUsbDeviceManager *manager,
    SpiceUsbDevice *device, gpointer user_data)
{
    SpiceUsbDeviceWidget *self = SPICE_USB_DEVICE_WIDGET(user_data);

    gtk_container_foreach(GTK_CONTAINER(self),
                          destroy_widget_by_usb_device, device);
}
