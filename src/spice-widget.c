/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010 Red Hat, Inc.

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

#include <math.h>
#include <glib.h>

#ifdef HAVE_X11_XKBLIB_H
#include <X11/XKBlib.h>
#include <gdk/gdkx.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#endif
#ifdef G_OS_WIN32
#include <windows.h>
#include <dinput.h>
#include <ime.h>
#include <gdk/gdkwin32.h>
#ifndef MAPVK_VK_TO_VSC /* may be undefined in older mingw-headers */
#define MAPVK_VK_TO_VSC 0
#endif
#endif

#include "spice-widget.h"
#include "spice-widget-priv.h"
#include "spice-gtk-session-priv.h"
#include "vncdisplaykeymap.h"
#include "spice-grabsequence-priv.h"


/**
 * SECTION:spice-widget
 * @short_description: a GTK display widget
 * @title: Spice Display
 * @section_id:
 * @stability: Stable
 * @include: spice-client-gtk.h
 *
 * A GTK widget that displays a SPICE server. It sends keyboard/mouse
 * events and can also share clipboard...
 *
 * Arbitrary key events can be sent thanks to spice_display_send_keys().
 *
 * The widget will optionally grab the keyboard and the mouse when
 * focused if the properties #SpiceDisplay:grab-keyboard and
 * #SpiceDisplay:grab-mouse are #TRUE respectively.  It can be
 * ungrabbed with spice_display_mouse_ungrab(), and by setting a key
 * combination with spice_display_set_grab_keys().
 *
 * Finally, spice_display_get_pixbuf() will take a screenshot of the
 * current display and return an #GdkPixbuf (that you can then easily
 * save to disk).
 */

G_DEFINE_TYPE(SpiceDisplay, spice_display, GTK_TYPE_EVENT_BOX)

/* Properties */
enum {
    PROP_0,
    PROP_SESSION,
    PROP_CHANNEL_ID,
    PROP_KEYBOARD_GRAB,
    PROP_MOUSE_GRAB,
    PROP_RESIZE_GUEST,
    PROP_SCALING,
    PROP_ONLY_DOWNSCALE,
    PROP_DISABLE_INPUTS,
    PROP_ZOOM_LEVEL,
    PROP_MONITOR_ID,
    PROP_KEYPRESS_DELAY,
    PROP_READY
};

/* Signals */
enum {
    SPICE_DISPLAY_MOUSE_GRAB,
    SPICE_DISPLAY_KEYBOARD_GRAB,
    SPICE_DISPLAY_GRAB_KEY_PRESSED,
    SPICE_DISPLAY_LAST_SIGNAL,
};

#define DEFAULT_KEYPRESS_DELAY 100

static guint signals[SPICE_DISPLAY_LAST_SIGNAL];

#ifdef G_OS_WIN32
static HWND win32_window = NULL;
#endif

static void update_keyboard_grab(SpiceDisplay *display);
static void try_keyboard_grab(SpiceDisplay *display);
static void try_keyboard_ungrab(SpiceDisplay *display);
static void update_mouse_grab(SpiceDisplay *display);
static void try_mouse_grab(SpiceDisplay *display);
static void try_mouse_ungrab(SpiceDisplay *display);
static void recalc_geometry(GtkWidget *widget);
static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data);
static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data);
static void cursor_invalidate(SpiceDisplay *display);
static void update_area(SpiceDisplay *display, gint x, gint y, gint width, gint height);
static void release_keys(SpiceDisplay *display);
static void size_allocate(GtkWidget *widget, GtkAllocation *conf, gpointer data);
static gboolean draw_event(GtkWidget *widget, cairo_t *cr, gpointer data);
static void update_size_request(SpiceDisplay *display);
static GdkDevice *spice_gdk_window_get_pointing_device(GdkWindow *window);

/* ---------------------------------------------------------------- */

static void spice_display_get_property(GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
    SpiceDisplay *display = SPICE_DISPLAY(object);
    SpiceDisplayPrivate *d = display->priv;

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, d->session);
        break;
    case PROP_CHANNEL_ID:
        g_value_set_int(value, d->channel_id);
        break;
    case PROP_MONITOR_ID:
        g_value_set_int(value, d->monitor_id);
        break;
    case PROP_KEYBOARD_GRAB:
        g_value_set_boolean(value, d->keyboard_grab_enable);
        break;
    case PROP_MOUSE_GRAB:
        g_value_set_boolean(value, d->mouse_grab_enable);
        break;
    case PROP_RESIZE_GUEST:
        g_value_set_boolean(value, d->resize_guest_enable);
        break;
    case PROP_SCALING:
        g_value_set_boolean(value, d->allow_scaling);
        break;
    case PROP_ONLY_DOWNSCALE:
        g_value_set_boolean(value, d->only_downscale);
        break;
    case PROP_DISABLE_INPUTS:
        g_value_set_boolean(value, d->disable_inputs);
        break;
    case PROP_ZOOM_LEVEL:
        g_value_set_int(value, d->zoom_level);
        break;
    case PROP_READY:
        g_value_set_boolean(value, d->ready);
        break;
    case PROP_KEYPRESS_DELAY:
        g_value_set_uint(value, d->keypress_delay);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void scaling_updated(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(display));

    recalc_geometry(GTK_WIDGET(display));
    if (d->canvas.surface && window) { /* if not yet shown */
        gtk_widget_queue_draw(GTK_WIDGET(display));
    }
    update_size_request(display);
}

static void update_size_request(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    gint reqwidth, reqheight;

    if (d->resize_guest_enable || d->allow_scaling) {
        reqwidth = 640;
        reqheight = 480;
    } else {
        reqwidth = d->area.width;
        reqheight = d->area.height;
    }

    gtk_widget_set_size_request(GTK_WIDGET(display), reqwidth, reqheight);
    recalc_geometry(GTK_WIDGET(display));
}

static void update_keyboard_focus(SpiceDisplay *display, gboolean state)
{
    SpiceDisplayPrivate *d = display->priv;

    d->keyboard_have_focus = state;
    spice_gtk_session_set_keyboard_has_focus(d->gtk_session, state);

    /* keyboard grab gets inhibited by usb-device-manager when it is
       in the process of redirecting a usb-device (as this may show a
       policykit dialog). Making autoredir/automount setting changes while
       this is happening is not a good idea! */
    if (d->keyboard_grab_inhibit)
        return;

    spice_gtk_session_request_auto_usbredir(d->gtk_session, state);
}

static gint get_display_id(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    /* supported monitor_id only with display channel #0 */
    if (d->channel_id == 0 && d->monitor_id >= 0)
        return d->monitor_id;

    g_return_val_if_fail(d->monitor_id <= 0, -1);

    return d->channel_id;
}

static bool egl_enabled(SpiceDisplayPrivate *d)
{
#ifndef G_OS_WIN32
    return d->egl.enabled;
#else
    return false;
#endif
}

static void update_ready(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    gboolean ready = FALSE;

    if (d->monitor_ready) {
        ready = egl_enabled(d) || d->mark != 0;
    }
    /* If the 'resize-guest' property is set, the application expects spice-gtk
     * to manage the size and state of the displays, so update the 'enabled'
     * state here. If 'resize-guest' is false, we can assume that the
     * application will manage the state of the displays.
     */
    if (d->resize_guest_enable) {
        spice_main_update_display_enabled(d->main, get_display_id(display),
                                          ready, TRUE);
    }

    if (d->ready == ready)
        return;

    if (ready && gtk_widget_get_window(GTK_WIDGET(display)))
        gtk_widget_queue_draw(GTK_WIDGET(display));

    d->ready = ready;
    g_object_notify(G_OBJECT(display), "ready");
}

static void set_monitor_ready(SpiceDisplay *self, gboolean ready)
{
    SpiceDisplayPrivate *d = self->priv;

    d->monitor_ready = ready;
    update_ready(self);
}

G_GNUC_INTERNAL
void spice_display_widget_update_monitor_area(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    SpiceDisplayMonitorConfig *cfg, *c = NULL;
    GArray *monitors = NULL;
    int i;

    DISPLAY_DEBUG(display, "update monitor area");
    if (d->monitor_id < 0)
        goto whole;

    g_object_get(d->display, "monitors", &monitors, NULL);
    for (i = 0; monitors != NULL && i < monitors->len; i++) {
        cfg = &g_array_index(monitors, SpiceDisplayMonitorConfig, i);
        if (cfg->id == d->monitor_id) {
           c = cfg;
           break;
        }
    }
    if (c == NULL) {
        DISPLAY_DEBUG(display, "update monitor: no monitor %d", d->monitor_id);
        set_monitor_ready(display, false);
        if (spice_channel_test_capability(d->display, SPICE_DISPLAY_CAP_MONITORS_CONFIG)) {
            DISPLAY_DEBUG(display, "waiting until MonitorsConfig is received");
            g_clear_pointer(&monitors, g_array_unref);
            return;
        }
        goto whole;
    }

    if (c->surface_id != 0) {
        g_warning("FIXME: only support monitor config with primary surface 0, "
                  "but given config surface %u", c->surface_id);
        goto whole;
    }

    /* If only one head on this monitor, update the whole area */
    if (monitors->len == 1 && !egl_enabled(d)) {
        update_area(display, 0, 0, c->width, c->height);
    } else {
        update_area(display, c->x, c->y, c->width, c->height);
    }
    g_clear_pointer(&monitors, g_array_unref);
    return;

whole:
    g_clear_pointer(&monitors, g_array_unref);
    /* by display whole surface */
    update_area(display, 0, 0, d->canvas.width, d->canvas.height);
    set_monitor_ready(display, true);
}

static void
spice_display_set_keypress_delay(SpiceDisplay *display, guint delay)
{
    SpiceDisplayPrivate *d = display->priv;
    const gchar *env = g_getenv("SPICE_KEYPRESS_DELAY");

    if (env != NULL)
        delay = strtoul(env, NULL, 10);

    if (d->keypress_delay != delay) {
        DISPLAY_DEBUG(display, "keypress-delay is set to %u ms", delay);
        d->keypress_delay = delay;
        g_object_notify(G_OBJECT(display), "keypress-delay");
    }
}

static void spice_display_set_property(GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
    SpiceDisplay *display = SPICE_DISPLAY(object);
    SpiceDisplayPrivate *d = display->priv;

    switch (prop_id) {
    case PROP_SESSION:
        g_warn_if_fail(d->session == NULL);
        d->session = g_value_dup_object(value);
        d->gtk_session = spice_gtk_session_get(d->session);
        spice_g_signal_connect_object(d->gtk_session, "notify::pointer-grabbed",
                                      G_CALLBACK(cursor_invalidate), object,
                                      G_CONNECT_SWAPPED);
        break;
    case PROP_CHANNEL_ID:
        d->channel_id = g_value_get_int(value);
        break;
    case PROP_MONITOR_ID:
        d->monitor_id = g_value_get_int(value);
        if (d->display) /* if constructed */
            spice_display_widget_update_monitor_area(display);
        break;
    case PROP_KEYBOARD_GRAB:
        d->keyboard_grab_enable = g_value_get_boolean(value);
        update_keyboard_grab(display);
        break;
    case PROP_MOUSE_GRAB:
        d->mouse_grab_enable = g_value_get_boolean(value);
        update_mouse_grab(display);
        break;
    case PROP_RESIZE_GUEST:
        d->resize_guest_enable = g_value_get_boolean(value);
        update_ready(display);
        update_size_request(display);
        break;
    case PROP_SCALING:
        d->allow_scaling = g_value_get_boolean(value);
        scaling_updated(display);
        break;
    case PROP_ONLY_DOWNSCALE:
        d->only_downscale = g_value_get_boolean(value);
        scaling_updated(display);
        break;
    case PROP_DISABLE_INPUTS:
        d->disable_inputs = g_value_get_boolean(value);
        gtk_widget_set_can_focus(GTK_WIDGET(display), !d->disable_inputs);
        update_keyboard_grab(display);
        update_mouse_grab(display);
        break;
    case PROP_ZOOM_LEVEL:
        d->zoom_level = g_value_get_int(value);
        scaling_updated(display);
        break;
    case PROP_KEYPRESS_DELAY:
        spice_display_set_keypress_delay(display, g_value_get_uint(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void session_inhibit_keyboard_grab_changed(GObject    *gobject,
                                                  GParamSpec *pspec,
                                                  gpointer    user_data)
{
    SpiceDisplay *display = user_data;
    SpiceDisplayPrivate *d = display->priv;

    g_object_get(d->session, "inhibit-keyboard-grab",
                 &d->keyboard_grab_inhibit, NULL);
    update_keyboard_grab(display);
    update_mouse_grab(display);
}

static void spice_display_dispose(GObject *obj)
{
    SpiceDisplay *display = SPICE_DISPLAY(obj);
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "spice display dispose");

    spice_cairo_image_destroy(display);
    g_clear_object(&d->session);
    d->gtk_session = NULL;

    if (d->key_delayed_id) {
        g_source_remove(d->key_delayed_id);
        d->key_delayed_id = 0;
    }

    G_OBJECT_CLASS(spice_display_parent_class)->dispose(obj);
}

static void spice_display_finalize(GObject *obj)
{
    SpiceDisplay *display = SPICE_DISPLAY(obj);
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "Finalize spice display");

    g_clear_pointer(&d->grabseq, spice_grab_sequence_free);
    g_clear_pointer(&d->activeseq, g_free);

    g_clear_object(&d->show_cursor);
    g_clear_object(&d->mouse_cursor);
    g_clear_object(&d->mouse_pixbuf);

    G_OBJECT_CLASS(spice_display_parent_class)->finalize(obj);
}

static GdkCursor* spice_display_get_blank_cursor(SpiceDisplay *display)
{
    GdkDisplay *gdk_display;
    const gchar *cursor_name;
    GdkWindow *gdk_window = GDK_WINDOW(gtk_widget_get_window(GTK_WIDGET(display)));

    if (gdk_window == NULL)
        return NULL;

    gdk_display = gdk_window_get_display(gdk_window);
    cursor_name = g_getenv("SPICE_DEBUG_CURSOR") ? "crosshair" : "none";

    return gdk_cursor_new_from_name(gdk_display, cursor_name);
}

static gboolean grab_broken(SpiceDisplay *self, GdkEventGrabBroken *event,
                            gpointer user_data G_GNUC_UNUSED)
{
    GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(self));
    DISPLAY_DEBUG(self, "%s (implicit: %d, keyboard: %d)", __FUNCTION__,
                  event->implicit, event->keyboard);
    DISPLAY_DEBUG(self, "%s (SpiceDisplay::GdkWindow %p, event->grab_window: %p)",
                  __FUNCTION__, window, event->grab_window);
    if (window == event->grab_window) {
        /* ignore grab-broken event moving the grab to GtkEventBox::window
         * (from GtkEventBox::event_window) as we initially called
         * gdk_pointer_grab() on GtkEventBox::window, see
         * https://bugzilla.gnome.org/show_bug.cgi?id=769635
         */
        return false;
    }

    if (event->keyboard) {
        try_keyboard_ungrab(self);
        release_keys(self);
    }

    /* always release mouse when grab broken, this could be more
       generally placed in keyboard_ungrab(), but one might worry of
       breaking someone else code. */
    try_mouse_ungrab(self);

    return false;
}

static void file_transfer_callback(GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data G_GNUC_UNUSED)
{
    SpiceMainChannel *channel = SPICE_MAIN_CHANNEL(source_object);
    GError *error = NULL;

    if (spice_main_file_copy_finish(channel, result, &error))
        return;

    if (error != NULL && error->message != NULL)
        g_warning("File transfer failed with error: %s", error->message);
    else
        g_warning("File transfer failed");

    g_clear_error(&error);
}

static void drag_data_received_callback(SpiceDisplay *self,
                                        GdkDragContext *drag_context,
                                        gint x,
                                        gint y,
                                        GtkSelectionData *data,
                                        guint info,
                                        guint time,
                                        gpointer *user_data)
{
    const guchar *buf;
    gchar **file_urls;
    int n_files;
    SpiceDisplayPrivate *d = self->priv;
    int i = 0;
    GFile **files;

    /* We get a buf like:
     * file:///root/a.txt\r\nfile:///root/b.txt\r\n
     */
    DISPLAY_DEBUG(self, "%s: drag a file", __FUNCTION__);
    buf = gtk_selection_data_get_data(data);
    g_return_if_fail(buf != NULL);

    file_urls = g_uri_list_extract_uris((const gchar*)buf);
    n_files = g_strv_length(file_urls);
    files = g_new0(GFile*, n_files + 1);
    for (i = 0; i < n_files; i++) {
        files[i] = g_file_new_for_uri(file_urls[i]);
    }
    g_strfreev(file_urls);

    spice_main_file_copy_async(d->main, files, 0, NULL, NULL,
                               NULL, file_transfer_callback, NULL);
    for (i = 0; i < n_files; i++) {
        g_object_unref(files[i]);
    }
    g_free(files);

    gtk_drag_finish(drag_context, TRUE, FALSE, time);
}

static void grab_notify(SpiceDisplay *display, gboolean was_grabbed)
{
    DISPLAY_DEBUG(display, "grab notify %d", was_grabbed);

    if (was_grabbed == FALSE)
        release_keys(display);
}

#if GTK_CHECK_VERSION(3,16,0)
#ifndef G_OS_WIN32
/* Ignore GLib's too-new warnings */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static gboolean
gl_area_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data)
{
    SpiceDisplay *display = SPICE_DISPLAY(user_data);
    SpiceDisplayPrivate *d = display->priv;

    spice_egl_update_display(display);
    glFlush();
    if (d->egl.call_draw_done) {
        spice_display_gl_draw_done(SPICE_DISPLAY_CHANNEL(d->display));
        d->egl.call_draw_done = FALSE;
    }

    return TRUE;
}

static void
gl_area_realize(GtkGLArea *area, gpointer user_data)
{
    SpiceDisplay *display = SPICE_DISPLAY(user_data);
    GError *err = NULL;

    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area) != NULL)
        return;

    if (!spice_egl_init(display, &err)) {
        g_critical("egl init failed: %s", err->message);
        g_clear_error(&err);
    }
}
G_GNUC_END_IGNORE_DEPRECATIONS
#endif
#endif

static void
drawing_area_realize(GtkWidget *area, gpointer user_data)
{
#ifdef GDK_WINDOWING_X11
    SpiceDisplay *display = SPICE_DISPLAY(user_data);

    if (GDK_IS_X11_DISPLAY(gdk_display_get_default()) &&
        spice_display_get_gl_scanout(SPICE_DISPLAY_CHANNEL(display->priv->display)) != NULL)
        spice_display_widget_gl_scanout(display);

#endif
}

static void spice_display_init(SpiceDisplay *display)
{
    GtkWidget *widget = GTK_WIDGET(display);
    GtkWidget *area;
    SpiceDisplayPrivate *d;
    GtkTargetEntry targets = { "text/uri-list", 0, 0 };

    d = display->priv = SPICE_DISPLAY_GET_PRIVATE(display);
    d->stack = GTK_STACK(gtk_stack_new());
    gtk_container_add(GTK_CONTAINER(display), GTK_WIDGET(d->stack));
    area = gtk_drawing_area_new();

    g_object_connect(area,
                     "signal::draw", draw_event, display,
                     "signal::realize", drawing_area_realize, display,
                     NULL);
    gtk_stack_add_named(d->stack, area, "draw-area");
    gtk_widget_set_double_buffered(area, true);
    gtk_stack_set_visible_child(d->stack, area);

#if GTK_CHECK_VERSION(3,16,0)
#ifndef G_OS_WIN32
/* Ignore GLib's too-new warnings */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    area = gtk_gl_area_new();
    gtk_gl_area_set_required_version(GTK_GL_AREA(area), 3, 2);
    gtk_gl_area_set_auto_render(GTK_GL_AREA(area), false);
    g_object_connect(area,
                     "signal::render", gl_area_render, display,
                     "signal::realize", gl_area_realize, display,
                     NULL);
    gtk_stack_add_named(d->stack, area, "gl-area");
    gtk_widget_show_all(widget);
G_GNUC_END_IGNORE_DEPRECATIONS
#endif
#endif

    g_signal_connect(display, "grab-broken-event", G_CALLBACK(grab_broken), NULL);
    g_signal_connect(display, "grab-notify", G_CALLBACK(grab_notify), NULL);

    gtk_drag_dest_set(widget, GTK_DEST_DEFAULT_ALL, &targets, 1, GDK_ACTION_COPY);
    g_signal_connect(display, "drag-data-received",
                     G_CALLBACK(drag_data_received_callback), NULL);
    g_signal_connect(display, "size-allocate", G_CALLBACK(size_allocate), NULL);

    gtk_widget_add_events(widget,
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_BUTTON_MOTION_MASK |
                          GDK_ENTER_NOTIFY_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_KEY_PRESS_MASK |
                          GDK_SCROLL_MASK);
    gtk_widget_set_can_focus(widget, true);
    gtk_event_box_set_above_child(GTK_EVENT_BOX(widget), true);

    d->grabseq = spice_grab_sequence_new_from_string("Control_L+Alt_L");
    d->activeseq = g_new0(gboolean, d->grabseq->nkeysyms);
}

static GObject *
spice_display_constructor(GType                  gtype,
                          guint                  n_properties,
                          GObjectConstructParam *properties)
{
    GObject *obj;
    SpiceDisplay *display;
    SpiceDisplayPrivate *d;
    GList *list;
    GList *it;

    {
        /* Always chain up to the parent constructor */
        GObjectClass *parent_class;
        parent_class = G_OBJECT_CLASS(spice_display_parent_class);
        obj = parent_class->constructor(gtype, n_properties, properties);
    }

    display = SPICE_DISPLAY(obj);
    d = display->priv;

    if (!d->session)
        g_error("SpiceDisplay constructed without a session");

    spice_g_signal_connect_object(d->session, "channel-new",
                                  G_CALLBACK(channel_new), display, 0);
    spice_g_signal_connect_object(d->session, "channel-destroy",
                                  G_CALLBACK(channel_destroy), display, 0);
    list = spice_session_get_channels(d->session);
    for (it = g_list_first(list); it != NULL; it = g_list_next(it)) {
        if (SPICE_IS_MAIN_CHANNEL(it->data)) {
            channel_new(d->session, it->data, (gpointer*)display);
            break;
        }
    }
    for (it = g_list_first(list); it != NULL; it = g_list_next(it)) {
        if (!SPICE_IS_MAIN_CHANNEL(it->data))
            channel_new(d->session, it->data, (gpointer*)display);
    }
    g_list_free(list);

    spice_g_signal_connect_object(d->session, "notify::inhibit-keyboard-grab",
                                  G_CALLBACK(session_inhibit_keyboard_grab_changed),
                                  display, 0);

    return obj;
}

/**
 * spice_display_set_grab_keys:
 * @display: the display widget
 * @seq: (transfer none): key sequence
 *
 * Set the key combination to grab/ungrab the keyboard. The default is
 * "Control L + Alt L".
 **/
void spice_display_set_grab_keys(SpiceDisplay *display, SpiceGrabSequence *seq)
{
    SpiceDisplayPrivate *d;

    g_return_if_fail(SPICE_IS_DISPLAY(display));

    d = display->priv;
    g_return_if_fail(d != NULL);

    if (d->grabseq) {
        spice_grab_sequence_free(d->grabseq);
    }
    if (seq)
        d->grabseq = spice_grab_sequence_copy(seq);
    else
        d->grabseq = spice_grab_sequence_new_from_string("Control_L+Alt_L");
    g_free(d->activeseq);
    d->activeseq = g_new0(gboolean, d->grabseq->nkeysyms);
}

#ifdef G_OS_WIN32
static LRESULT CALLBACK keyboard_hook_cb(int code, WPARAM wparam, LPARAM lparam)
{
    if  (win32_window && code == HC_ACTION && wparam != WM_KEYUP) {
        KBDLLHOOKSTRUCT *hooked = (KBDLLHOOKSTRUCT*)lparam;
        DWORD dwmsg = (hooked->flags << 24) | (hooked->scanCode << 16) | 1;

        if (hooked->vkCode == VK_NUMLOCK || hooked->vkCode == VK_RSHIFT) {
            dwmsg &= ~(1 << 24);
            SendMessage(win32_window, wparam, hooked->vkCode, dwmsg);
        }
        switch (hooked->vkCode) {
        case VK_CAPITAL:
        case VK_SCROLL:
        case VK_NUMLOCK:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_RCONTROL:
        case VK_LMENU:
        case VK_RMENU:
            break;
        case VK_LCONTROL:
            /* When pressing AltGr, an extra VK_LCONTROL with a special
             * scancode with bit 9 set is sent. Let's ignore the extra
             * VK_LCONTROL, as that will make AltGr misbehave. */
            if (hooked->scanCode & 0x200)
                return 1;
            break;
        default:
            SendMessage(win32_window, wparam, hooked->vkCode, dwmsg);
            return 1;
        }
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}
#endif

/**
 * spice_display_get_grab_keys:
 * @display: the display widget
 *
 * Finds the current grab key combination for the @display.
 *
 * Returns: (transfer none): the current grab key combination.
 **/
SpiceGrabSequence *spice_display_get_grab_keys(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d;

    g_return_val_if_fail(SPICE_IS_DISPLAY(display), NULL);

    d = display->priv;
    g_return_val_if_fail(d != NULL, NULL);

    return d->grabseq;
}

#if GTK_CHECK_VERSION(3, 20, 0)
static GdkSeat *spice_display_get_default_seat(SpiceDisplay *display)
{
    GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(display));
    GdkDisplay *gdk_display = gdk_window_get_display(window);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    return gdk_display_get_default_seat(gdk_display);
    G_GNUC_END_IGNORE_DEPRECATIONS
}
#endif

/* FIXME: gdk_keyboard_grab/ungrab() is deprecated */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static void try_keyboard_grab(SpiceDisplay *display)
{
    GtkWidget *widget = GTK_WIDGET(display);
    SpiceDisplayPrivate *d = display->priv;
    GdkGrabStatus status;

    if (g_getenv("SPICE_NOGRAB"))
        return;
    if (d->disable_inputs)
        return;

    if (d->keyboard_grab_inhibit)
        return;
    if (!d->keyboard_grab_enable)
        return;
    if (d->keyboard_grab_active)
        return;
    if (!spice_gtk_session_get_keyboard_has_focus(d->gtk_session))
        return;
    if (!spice_gtk_session_get_mouse_has_pointer(d->gtk_session))
        return;
    if (d->keyboard_grab_released)
        return;

    g_return_if_fail(gtk_widget_is_focus(widget));

    DISPLAY_DEBUG(display, "grab keyboard");
    gtk_widget_grab_focus(widget);

#ifdef G_OS_WIN32
    if (d->keyboard_hook == NULL)
        d->keyboard_hook = SetWindowsHookEx(WH_KEYBOARD_LL, keyboard_hook_cb,
                                            GetModuleHandle(NULL), 0);
    g_warn_if_fail(d->keyboard_hook != NULL);
#endif
#if GTK_CHECK_VERSION(3, 20, 0)
    status = gdk_seat_grab(spice_display_get_default_seat(display),
                           gtk_widget_get_window(widget),
                           GDK_SEAT_CAPABILITY_KEYBOARD,
                           FALSE,
                           NULL,
                           NULL,
                           NULL,
                           NULL);
#else
    status = gdk_keyboard_grab(gtk_widget_get_window(widget), FALSE,
                               GDK_CURRENT_TIME);
#endif
    if (status != GDK_GRAB_SUCCESS) {
        g_warning("keyboard grab failed %u", status);
        d->keyboard_grab_active = false;
    } else {
        d->keyboard_grab_active = true;
        g_signal_emit(widget, signals[SPICE_DISPLAY_KEYBOARD_GRAB], 0, true);
    }
}

static void ungrab_keyboard(G_GNUC_UNUSED SpiceDisplay *display)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#if GTK_CHECK_VERSION(3, 20, 0)
    /* we want to ungrab just the keyboard - it is not possible using gdk_seat_ungrab().
       See also https://bugzilla.gnome.org/show_bug.cgi?id=780133 */
    GdkDevice *keyboard = gdk_seat_get_keyboard(spice_display_get_default_seat(display));
    gdk_device_ungrab(keyboard, GDK_CURRENT_TIME);
#else
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
#endif
    G_GNUC_END_IGNORE_DEPRECATIONS
}

static void try_keyboard_ungrab(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    GtkWidget *widget = GTK_WIDGET(display);

    if (!d->keyboard_grab_active)
        return;

    DISPLAY_DEBUG(display, "ungrab keyboard");
    ungrab_keyboard(display);
#ifdef G_OS_WIN32
    // do not use g_clear_pointer as Windows API have different linkage
    if (d->keyboard_hook) {
        UnhookWindowsHookEx(d->keyboard_hook);
        d->keyboard_hook = NULL;
    }
#endif
    d->keyboard_grab_active = false;
    g_signal_emit(widget, signals[SPICE_DISPLAY_KEYBOARD_GRAB], 0, false);
}
G_GNUC_END_IGNORE_DEPRECATIONS


static void update_keyboard_grab(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    if (d->keyboard_grab_enable &&
        !d->keyboard_grab_inhibit &&
        !d->disable_inputs)
        try_keyboard_grab(display);
    else
        try_keyboard_ungrab(display);
}

static void set_mouse_accel(SpiceDisplay *display, gboolean enabled)
{
    SpiceDisplayPrivate *d = display->priv;

#if defined GDK_WINDOWING_X11
    GdkWindow *w = GDK_WINDOW(gtk_widget_get_window(GTK_WIDGET(display)));

    if (!GDK_IS_X11_DISPLAY(gdk_window_get_display(w))) {
        DISPLAY_DEBUG(display, "FIXME: gtk backend is not X11");
        return;
    }

    Display *x_display = GDK_WINDOW_XDISPLAY(w);
    if (enabled) {
        /* restore mouse acceleration */
        XChangePointerControl(x_display, True, True,
                              d->x11_accel_numerator, d->x11_accel_denominator, d->x11_threshold);
    } else {
        XGetPointerControl(x_display,
                           &d->x11_accel_numerator, &d->x11_accel_denominator, &d->x11_threshold);
        /* set mouse acceleration to default */
        XChangePointerControl(x_display, True, True, -1, -1, -1);
        DISPLAY_DEBUG(display, "disabled X11 mouse motion %d %d %d",
                      d->x11_accel_numerator, d->x11_accel_denominator, d->x11_threshold);
    }
#elif defined GDK_WINDOWING_WIN32
    if (enabled) {
        g_return_if_fail(SystemParametersInfo(SPI_SETMOUSE, 0, &d->win_mouse, 0));
        g_return_if_fail(SystemParametersInfo(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)d->win_mouse_speed, 0));
    } else {
        int accel[3] = { 0, 0, 0 }; // disabled
        g_return_if_fail(SystemParametersInfo(SPI_GETMOUSE, 0, &d->win_mouse, 0));
        g_return_if_fail(SystemParametersInfo(SPI_GETMOUSESPEED, 0, &d->win_mouse_speed, 0));
        g_return_if_fail(SystemParametersInfo(SPI_SETMOUSE, 0, &accel, SPIF_SENDCHANGE));
        g_return_if_fail(SystemParametersInfo(SPI_SETMOUSESPEED, 0, (PVOID)10, SPIF_SENDCHANGE)); // default
    }
#else
    g_warning("Mouse acceleration code missing for your platform");
#endif
}

#ifdef G_OS_WIN32
static gboolean win32_clip_cursor(void)
{
    RECT window, workarea, rect;
    HMONITOR monitor;
    MONITORINFO mi = { 0, };

    g_return_val_if_fail(win32_window != NULL, FALSE);

    if (!GetWindowRect(win32_window, &window))
        goto error;

    monitor = MonitorFromRect(&window, MONITOR_DEFAULTTONEAREST);
    g_return_val_if_fail(monitor != NULL, false);

    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(monitor, &mi))
        goto error;
    workarea = mi.rcWork;

    if (!IntersectRect(&rect, &window, &workarea)) {
        g_critical("error clipping cursor");
        return false;
    }

    SPICE_DEBUG("clip rect %ld %ld %ld %ld\n",
                rect.left, rect.right, rect.top, rect.bottom);

    if (!ClipCursor(&rect))
        goto error;

    return true;

error:
    {
        DWORD errval  = GetLastError();
        gchar *errstr = g_win32_error_message(errval);
        g_warning("failed to clip cursor (%lu) %s", errval, errstr);
    }

    return false;
}
#endif

static gboolean do_pointer_grab(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    GdkWindow *window = GDK_WINDOW(gtk_widget_get_window(GTK_WIDGET(display)));
    GdkGrabStatus status;
    GdkCursor *blank = spice_display_get_blank_cursor(display);
    gboolean grab_successful = FALSE;

    if (!gtk_widget_get_realized(GTK_WIDGET(display)))
        goto end;

#ifdef G_OS_WIN32
    if (!win32_clip_cursor())
        goto end;
#endif

    try_keyboard_grab(display);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#if GTK_CHECK_VERSION(3, 20, 0)
    status = gdk_seat_grab(spice_display_get_default_seat(display),
                           window,
                           GDK_SEAT_CAPABILITY_ALL_POINTING,
                           TRUE,
                           blank,
                           NULL,
                           NULL,
                           NULL);
#else
    /*
     * from gtk-vnc:
     * For relative mouse to work correctly when grabbed we need to
     * allow the pointer to move anywhere on the local desktop, so
     * use NULL for the 'confine_to' argument. Furthermore we need
     * the coords to be reported to our VNC window, regardless of
     * what window the pointer is actally over, so use 'FALSE' for
     * 'owner_events' parameter
     */
    status = gdk_pointer_grab(window, FALSE,
                     GDK_POINTER_MOTION_MASK |
                     GDK_BUTTON_PRESS_MASK |
                     GDK_BUTTON_RELEASE_MASK |
                     GDK_BUTTON_MOTION_MASK |
                     GDK_SCROLL_MASK,
                     NULL,
                     blank,
                     GDK_CURRENT_TIME);
#endif
    G_GNUC_END_IGNORE_DEPRECATIONS
    grab_successful = (status == GDK_GRAB_SUCCESS);
    if (!grab_successful) {
        d->mouse_grab_active = false;
        g_warning("pointer grab failed %u", status);
    } else {
        d->mouse_grab_active = true;
        g_signal_emit(display, signals[SPICE_DISPLAY_MOUSE_GRAB], 0, true);
        spice_gtk_session_set_pointer_grabbed(d->gtk_session, true);
        set_mouse_accel(display, FALSE);
    }

end:
    g_clear_object(&blank);
    return grab_successful;
}

static void update_mouse_pointer(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    GdkWindow *window = GDK_WINDOW(gtk_widget_get_window(GTK_WIDGET(display)));

    if (!window)
        return;

    switch (d->mouse_mode) {
    case SPICE_MOUSE_MODE_CLIENT:
        if (gdk_window_get_cursor(window) != d->mouse_cursor)
            gdk_window_set_cursor(window, d->mouse_cursor);
        break;
    case SPICE_MOUSE_MODE_SERVER:
        if (gdk_window_get_cursor(window) != NULL)
            gdk_window_set_cursor(window, NULL);
        break;
    default:
        g_warn_if_reached();
        break;
    }
}

static void try_mouse_grab(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    if (g_getenv("SPICE_NOGRAB"))
        return;
    if (d->disable_inputs)
        return;

    if (!d->mouse_have_pointer)
        return;
    if (!d->keyboard_have_focus)
        return;

    if (!d->mouse_grab_enable)
        return;
    if (d->mouse_mode != SPICE_MOUSE_MODE_SERVER)
        return;
    if (d->mouse_grab_active)
        return;

    if (!do_pointer_grab(display))
        return;

    d->mouse_last_x = -1;
    d->mouse_last_y = -1;
}

static void mouse_wrap(SpiceDisplay *display, GdkEventMotion *motion)
{
    SpiceDisplayPrivate *d = display->priv;
    gint xr, yr;

#ifdef G_OS_WIN32
    RECT clip;
    g_return_if_fail(GetClipCursor(&clip));
    xr = clip.left + (clip.right - clip.left) / 2;
    yr = clip.top + (clip.bottom - clip.top) / 2;
    /* the clip rectangle has no offset, so we can't use gdk_wrap_pointer */
    SetCursorPos(xr, yr);
    d->mouse_last_x = -1;
    d->mouse_last_y = -1;
#else
    GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(display));
    xr = gdk_screen_get_width(screen) / 2;
    yr = gdk_screen_get_height(screen) / 2;

    if (xr != (gint)motion->x_root || yr != (gint)motion->y_root) {
        GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(display));
        /* FIXME: we try our best to ignore that next pointer move event.. */
        gdk_display_sync(gdk_screen_get_display(screen));

        gdk_device_warp(spice_gdk_window_get_pointing_device(window), screen, xr, yr);
        d->mouse_last_x = -1;
        d->mouse_last_y = -1;
    }
#endif

}

static void ungrab_pointer(G_GNUC_UNUSED SpiceDisplay *display)
{
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#if GTK_CHECK_VERSION(3, 20, 0)
    /* we want to ungrab just the pointer - it is not possible using gdk_seat_ungrab().
       See also https://bugzilla.gnome.org/show_bug.cgi?id=780133 */
    GdkDevice *pointer = gdk_seat_get_pointer(spice_display_get_default_seat(display));
    gdk_device_ungrab(pointer, GDK_CURRENT_TIME);
#else
    gdk_pointer_ungrab(GDK_CURRENT_TIME);
#endif
    G_GNUC_END_IGNORE_DEPRECATIONS
}

static void try_mouse_ungrab(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    double s;
    int x, y;
    GdkWindow *window;

    if (!d->mouse_grab_active)
        return;

    ungrab_pointer(display);
    gtk_grab_remove(GTK_WIDGET(display));
#ifdef G_OS_WIN32
    ClipCursor(NULL);
#endif
    set_mouse_accel(display, TRUE);

    d->mouse_grab_active = false;

    spice_display_get_scaling(display, &s, &x, &y, NULL, NULL);

    window = gtk_widget_get_window(GTK_WIDGET(display));
    gdk_window_get_root_coords(window,
                               x + d->mouse_guest_x * s,
                               y + d->mouse_guest_y * s,
                               &x, &y);

    gdk_device_warp(spice_gdk_window_get_pointing_device(window),
                    gtk_widget_get_screen(GTK_WIDGET(display)),
                    x, y);

    g_signal_emit(display, signals[SPICE_DISPLAY_MOUSE_GRAB], 0, false);
    spice_gtk_session_set_pointer_grabbed(d->gtk_session, false);
}

static void update_mouse_grab(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    if (d->mouse_grab_enable &&
        !d->keyboard_grab_inhibit &&
        !d->disable_inputs)
        try_mouse_grab(display);
    else
        try_mouse_ungrab(display);
}

static void recalc_geometry(GtkWidget *widget)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;
    gdouble zoom = 1.0;

    if (spice_cairo_is_scaled(display))
        zoom = (gdouble)d->zoom_level / 100;

    DISPLAY_DEBUG(display,
                  "recalc geom monitor: %d:%d, guest +%d+%d:%dx%d, window %dx%d, zoom %g",
                  d->channel_id, d->monitor_id, d->area.x, d->area.y,
                  d->area.width, d->area.height,
                  d->ww, d->wh, zoom);

    if (d->resize_guest_enable)
        spice_main_set_display(d->main, get_display_id(display),
                               d->area.x, d->area.y, d->ww / zoom, d->wh / zoom);
}

/* ---------------------------------------------------------------- */

#define CONVERT_0565_TO_0888(s)                                         \
    (((((s) << 3) & 0xf8) | (((s) >> 2) & 0x7)) |                       \
     ((((s) << 5) & 0xfc00) | (((s) >> 1) & 0x300)) |                   \
     ((((s) << 8) & 0xf80000) | (((s) << 3) & 0x70000)))

#define CONVERT_0565_TO_8888(s) (CONVERT_0565_TO_0888(s) | 0xff000000)

#define CONVERT_0555_TO_0888(s)                                         \
    (((((s) & 0x001f) << 3) | (((s) & 0x001c) >> 2)) |                  \
     ((((s) & 0x03e0) << 6) | (((s) & 0x0380) << 1)) |                  \
     ((((s) & 0x7c00) << 9) | ((((s) & 0x7000)) << 4)))

#define CONVERT_0555_TO_8888(s) (CONVERT_0555_TO_0888(s) | 0xff000000)

static gboolean do_color_convert(SpiceDisplay *display, GdkRectangle *r)
{
    SpiceDisplayPrivate *d = display->priv;
    guint32 *dest = d->canvas.data;
    guint16 *src = d->canvas.data_origin;
    gint x, y;

    g_return_val_if_fail(r != NULL, false);
    g_return_val_if_fail(d->canvas.format == SPICE_SURFACE_FMT_16_555 ||
                         d->canvas.format == SPICE_SURFACE_FMT_16_565, false);

    src += (d->canvas.stride / 2) * r->y + r->x;
    dest += d->area.width * (r->y - d->area.y) + (r->x - d->area.x);

    if (d->canvas.format == SPICE_SURFACE_FMT_16_555) {
        for (y = 0; y < r->height; y++) {
            for (x = 0; x < r->width; x++) {
                dest[x] = CONVERT_0555_TO_0888(src[x]);
            }

            dest += d->area.width;
            src += d->canvas.stride / 2;
        }
    } else if (d->canvas.format == SPICE_SURFACE_FMT_16_565) {
        for (y = 0; y < r->height; y++) {
            for (x = 0; x < r->width; x++) {
                dest[x] = CONVERT_0565_TO_0888(src[x]);
            }

            dest += d->area.width;
            src += d->canvas.stride / 2;
        }
    }

    return true;
}

#ifndef G_OS_WIN32
static void set_egl_enabled(SpiceDisplay *display, bool enabled)
{
    SpiceDisplayPrivate *d = display->priv;

    if (egl_enabled(d) == enabled)
        return;

#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(gdk_display_get_default())) {
        /* even though the function is marked as deprecated, it's the
         * only way I found to prevent glitches when the window is
         * resized. */
        GtkWidget *area = gtk_stack_get_child_by_name(d->stack, "draw-area");
        gtk_widget_set_double_buffered(GTK_WIDGET(area), !enabled);
    } else
#endif
    {
        gtk_stack_set_visible_child_name(d->stack,
                                         enabled ? "gl-area" : "draw-area");
    }

    if (enabled) {
        spice_egl_resize_display(display, d->ww, d->wh);
    }

    d->egl.enabled = enabled;
}
#endif

static gboolean draw_event(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    SpiceDisplay *display = SPICE_DISPLAY(data);
    SpiceDisplayPrivate *d = display->priv;
    g_return_val_if_fail(d != NULL, false);

#ifndef G_OS_WIN32
    if (egl_enabled(d) &&
        g_str_equal(gtk_stack_get_visible_child_name(d->stack), "draw-area")) {
        spice_egl_update_display(display);
        return false;
    }
#endif

    if (d->mark == 0 || d->canvas.data == NULL ||
        d->area.width == 0 || d->area.height == 0)
        return false;

    spice_cairo_draw_event(display, cr);
    update_mouse_pointer(display);

    return true;
}

/* ---------------------------------------------------------------- */
typedef enum {
    SEND_KEY_PRESS,
    SEND_KEY_RELEASE,
} SendKeyType;

static void key_press_and_release(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    if (d->key_delayed_scancode == 0)
        return;

    spice_inputs_key_press_and_release(d->inputs, d->key_delayed_scancode);
    d->key_delayed_scancode = 0;

    if (d->key_delayed_id) {
        g_source_remove(d->key_delayed_id);
        d->key_delayed_id = 0;
    }
}

static gboolean key_press_delayed(gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;

    if (d->key_delayed_scancode == 0)
        return FALSE;

    spice_inputs_key_press(d->inputs, d->key_delayed_scancode);
    d->key_delayed_scancode = 0;

    if (d->key_delayed_id) {
        g_source_remove(d->key_delayed_id);
        d->key_delayed_id = 0;
    }

    return FALSE;
}

static bool send_pause(SpiceDisplay *display, GdkEventType type)
{
    SpiceInputsChannel *inputs = display->priv->inputs;

    /* Send proper scancodes. This will send same scancodes
     * as hardware.
     * The 0x21d is a sort of Third-Ctrl while
     * 0x45 is the NumLock.
     */
    if (type == GDK_KEY_PRESS) {
        spice_inputs_key_press(inputs, 0x21d);
        spice_inputs_key_press(inputs, 0x45);
    } else {
        spice_inputs_key_release(inputs, 0x21d);
        spice_inputs_key_release(inputs, 0x45);
    }
    return true;
}

static void send_key(SpiceDisplay *display, int scancode, SendKeyType type, gboolean press_delayed)
{
    SpiceDisplayPrivate *d = display->priv;
    uint32_t i, b, m;

    g_return_if_fail(scancode != 0);

    if (!d->inputs)
        return;

    if (d->disable_inputs)
        return;

    i = scancode / 32;
    b = scancode % 32;
    m = (1 << b);
    g_return_if_fail(i < SPICE_N_ELEMENTS(d->key_state));

    switch (type) {
    case SEND_KEY_PRESS:
        /* ensure delayed key is pressed before any new input event */
        key_press_delayed(display);

        if (press_delayed &&
            d->keypress_delay != 0 &&
            !(d->key_state[i] & m)) {
            g_warn_if_fail(d->key_delayed_id == 0);
            d->key_delayed_id = g_timeout_add(d->keypress_delay, key_press_delayed, display);
            d->key_delayed_scancode = scancode;
        } else
            spice_inputs_key_press(d->inputs, scancode);

        d->key_state[i] |= m;
        break;

    case SEND_KEY_RELEASE:
        if (!(d->key_state[i] & m))
            break;

        if (d->key_delayed_scancode == scancode)
            key_press_and_release(display);
        else {
            /* ensure delayed key is pressed before other key are released */
            key_press_delayed(display);
            spice_inputs_key_release(d->inputs, scancode);
        }

        d->key_state[i] &= ~m;
        break;

    default:
        g_warn_if_reached();
    }
}

static void release_keys(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    uint32_t i, b;

    DISPLAY_DEBUG(display, "%s", __FUNCTION__);
    for (i = 0; i < SPICE_N_ELEMENTS(d->key_state); i++) {
        if (!d->key_state[i]) {
            continue;
        }
        for (b = 0; b < 32; b++) {
            unsigned int scancode = i * 32 + b;
            if (scancode != 0) {
                send_key(display, scancode, SEND_KEY_RELEASE, FALSE);
            }
        }
    }
}

static gboolean check_for_grab_key(SpiceDisplay *display, int type, int keyval,
                                   int check_type, int reset_type)
{
    SpiceDisplayPrivate *d = display->priv;
    int i;

    if (!d->grabseq->nkeysyms)
        return FALSE;

    if (type == check_type) {
        /* Record the new key */
        for (i = 0 ; i < d->grabseq->nkeysyms ; i++)
            if (d->grabseq->keysyms[i] == keyval)
                d->activeseq[i] = TRUE;

        /* Return if any key is missing */
        for (i = 0 ; i < d->grabseq->nkeysyms ; i++)
            if (d->activeseq[i] == FALSE)
                return FALSE;

        /* resets the whole grab sequence on success */
        memset(d->activeseq, 0, sizeof(gboolean) * d->grabseq->nkeysyms);
        return TRUE;
    } else if (type == reset_type) {
        /* reset key event type resets the whole grab sequence */
        memset(d->activeseq, 0, sizeof(gboolean) * d->grabseq->nkeysyms);
        d->seq_pressed = FALSE;
        return FALSE;
    } else
        g_warn_if_reached();

    return FALSE;
}

static gboolean check_for_grab_key_pressed(SpiceDisplay *display, int type, int keyval)
{
    return check_for_grab_key(display, type, keyval, GDK_KEY_PRESS, GDK_KEY_RELEASE);
}

static gboolean check_for_grab_key_released(SpiceDisplay *display, int type, int keyval)
{
    return check_for_grab_key(display, type, keyval, GDK_KEY_RELEASE, GDK_KEY_PRESS);
}

static void update_display(SpiceDisplay *display)
{
#ifdef G_OS_WIN32
    win32_window = display ?
                        gdk_win32_window_get_impl_hwnd(gtk_widget_get_window(GTK_WIDGET(display))) :
                        NULL;
#endif
}

static gboolean key_event(GtkWidget *widget, GdkEventKey *key)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;
    int scancode = 0;
#ifdef G_OS_WIN32
    int native_scancode;
    WORD langid = LOWORD(GetKeyboardLayout(0));
    gboolean no_key_release = FALSE;
#endif

#ifdef G_OS_WIN32
    /* Try to get scancode with gdk_event_get_scancode.
     * This API is available from 3.22 or if backported.
     */
#if HAVE_GDK_EVENT_GET_SCANCODE
    native_scancode = gdk_event_get_scancode((GdkEvent *) key);
    if (native_scancode) {
        scancode = native_scancode & 0x1ff;
        /* Windows always set extended attribute for these keys */
        if (scancode == (0x100|DIK_NUMLOCK) || scancode == (0x100|DIK_RSHIFT))
            scancode &= 0xff;
    }
#else
    native_scancode = 0;
#endif

    /* on windows, we ought to ignore the reserved key event? */
    if (!native_scancode && key->hardware_keycode == 0xff)
        return false;

    if (!d->keyboard_grab_active) {
        if (key->hardware_keycode == VK_LWIN ||
            key->hardware_keycode == VK_RWIN ||
            key->hardware_keycode == VK_APPS)
            return false;
    }

#endif
    DISPLAY_DEBUG(display, "%s %s: keycode: %d  state: %u  group %d modifier %d",
                  __FUNCTION__, key->type == GDK_KEY_PRESS ? "press" : "release",
                  key->hardware_keycode, key->state, key->group, key->is_modifier);

    if (!d->seq_pressed && check_for_grab_key_pressed(display, key->type, key->keyval)) {
        g_signal_emit(widget, signals[SPICE_DISPLAY_GRAB_KEY_PRESSED], 0);

        if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER) {
            if (d->mouse_grab_active)
                try_mouse_ungrab(display);
            else
                try_mouse_grab(display);
        }
        d->seq_pressed = TRUE;
    } else if (d->seq_pressed && check_for_grab_key_released(display, key->type, key->keyval)) {
        release_keys(display);
        if (!d->keyboard_grab_released) {
            d->keyboard_grab_released = TRUE;
            try_keyboard_ungrab(display);
        } else {
            d->keyboard_grab_released = FALSE;
            try_keyboard_grab(display);
        }
        d->seq_pressed = FALSE;
    }

    if (!d->inputs)
        return true;

    if (key->keyval == GDK_KEY_Pause
#ifdef G_OS_WIN32
        /* for some reason GDK does not fill keyval for VK_PAUSE 
         * See https://bugzilla.gnome.org/show_bug.cgi?id=769214
         */
        || key->hardware_keycode == VK_PAUSE
#endif
        ) {
        return send_pause(display, key->type);
    }
    if (!scancode)
        scancode = vnc_display_keymap_gdk2xtkbd(d->keycode_map, d->keycode_maplen,
                                                key->hardware_keycode);
#ifdef G_OS_WIN32
    if (!native_scancode) {
        native_scancode = MapVirtualKey(key->hardware_keycode, MAPVK_VK_TO_VSC);
        /* MapVirtualKey doesn't return scancode with needed higher byte */
        scancode = native_scancode | (scancode & 0xff00);
    }

    /* Some virtual-key codes are missed in MapVirtualKey(). */
    switch (langid) {
    case MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN):
        if (native_scancode == 0) {
            switch (key->hardware_keycode) {
            case VK_DBE_DBCSCHAR:       /* from Pressed Zenkaku_Hankaku */
            case VK_KANJI:              /* from Alt + Zenkaku_Hankaku */
            case VK_DBE_ENTERIMECONFIGMODE:
                                        /* from Ctrl+Alt+Zenkaku_Hankaku */
                scancode = MapVirtualKey(VK_DBE_SBCSCHAR, MAPVK_VK_TO_VSC);
                                        /* to Released Zenkaku_Hankaku */
                break;
            case VK_CAPITAL:            /* from Shift + Eisu_toggle */
            case VK_DBE_CODEINPUT:      /* from Pressed Ctrl+Alt+Eisu_toggle */
            case VK_DBE_NOCODEINPUT:    /* from Released Ctrl+Alt+Eisu_toggle */
                scancode = MapVirtualKey(VK_DBE_ALPHANUMERIC, MAPVK_VK_TO_VSC);
                                        /* to Eisu_toggle */
                break;
            case VK_DBE_ROMAN:          /* from Pressed Alt+Hiragana_Katakana */
            case VK_KANA:               /* from Ctrl+Shift+Hiragana_Katakana */
                scancode = MapVirtualKey(VK_DBE_HIRAGANA, MAPVK_VK_TO_VSC);
                                        /* to Hiragana_Katakana */
                break;
            case VK_DBE_ENTERWORDREGISTERMODE:
                                        /* from Ctrl + Alt + Muhenkan */
                scancode = MapVirtualKey(VK_NONCONVERT, MAPVK_VK_TO_VSC);
                                        /* to Muhenkan */
                break;
            }
        }
        break;
    case MAKELANGID(LANG_KOREAN, SUBLANG_KOREAN):
        if (key->hardware_keycode == VK_HANGUL && native_scancode == DIK_LALT) {
            /* Left Alt (VK_MENU) has the scancode DIK_LALT (0x38) but
             * Hangul (VK_HANGUL) has the scancode 0x138
             */
            scancode = native_scancode | 0x100;
        }
        break;
    }

    /* Emulate KeyRelease events for the following keys.
     *
     * Alt+Zenkaku_Hankaku generates WM_KEYDOWN VK_KANJI and no WM_KEYUP
     * and it caused unlimited VK_KANJI in Linux desktop and the desktop
     * hung up. We send WM_KEYUP VK_KANJI here to avoid unlimited events.
     *
     * Eisu_toggle generates WM_KEYDOWN VK_DBE_ALPHANUMERIC only in
     * English mode,  WM_KEYDOWN VK_DBE_ALPHANUMERIC and WM_KEYUP
     * VK_DBE_HIRAGANA in Japanese mode, and it caused unlimited
     * VK_DBE_ALPHANUMERIC in Linux desktop.
     * Since VK_DBE_HIRAGANA is also assigned in Hiragana key,
     * we send WM_KEYUP VK_DBE_ALPHANUMERIC here to avoid unlimited events.
     * No KeyPress VK_DBE_HIRAGANA seems harmless.
     *
     * Hiragana_Katakana generates WM_KEYDOWN VK_DBE_HIRAGANA and
     * WM_KEYUP VK_DBE_ALPHANUMERIC in English mode, WM_KEYDOWN
     * VK_DBE_HIRAGANA only in Japanese mode, and it caused unlimited
     * VK_DBE_HIRAGANA in Linux desktop.
     *
     * Alt+Hiragana_Katakana generates WM_KEYUP VK_DBE_NOROMAN and
     * WM_KEYDOWN VK_DBE_ROMAN but the KeyRelease is called before
     * KeyDown is called and it caused unlimited VK_DBE_ROMAN.
     * We ignore the scancode of VK_DBE_NOROMAN and emulate WM_KEYUP
     * VK_DBE_ROMAN.
     *
     * Ctrl+Alt+Zenkaku_Hankaku generates WM_KEYDOWN VK_DBE_ENTERIMECONFIGMODE
     * and no WM_KEYUP and it caused unlimited VK_DBE_ENTERIMECONFIGMODE
     * in Linux desktop.
     */
    switch (langid) {
    case MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN):
        switch (key->hardware_keycode) {
        case VK_KANJI:                  /* Alt + Zenkaku_Hankaku */
        case VK_DBE_ALPHANUMERIC:       /* Eisu_toggle */
        case VK_DBE_HIRAGANA:           /* Hiragana_Katakana */
        case VK_DBE_ROMAN:              /* Alt+Hiragana_Katakana */
        case VK_DBE_ENTERIMECONFIGMODE: /* Ctrl + Alt + Zenkaku_Hankaku */
            no_key_release = TRUE;
            break;
        }
        break;
    }
#endif

    switch (key->type) {
    case GDK_KEY_PRESS:
        send_key(display, scancode, SEND_KEY_PRESS, !key->is_modifier);
#ifdef G_OS_WIN32
        if (no_key_release)
            send_key(display, scancode, SEND_KEY_RELEASE, !key->is_modifier);
#endif
        break;
    case GDK_KEY_RELEASE:
        send_key(display, scancode, SEND_KEY_RELEASE, !key->is_modifier);
        break;
    default:
        g_warn_if_reached();
        break;
    }

    return true;
}

static guint get_scancode_from_keyval(SpiceDisplay *display, guint keyval)
{
    SpiceDisplayPrivate *d = display->priv;
    guint keycode = 0;
    GdkKeymapKey *keys = NULL;
    gint n_keys = 0;

    if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_default(),
                                          keyval, &keys, &n_keys)) {
        /* FIXME what about levels? */
        keycode = keys[0].keycode;
        g_free(keys);
    } else {
        g_warning("could not lookup keyval %u, please report a bug", keyval);
        return 0;
    }

    return vnc_display_keymap_gdk2xtkbd(d->keycode_map, d->keycode_maplen, keycode);
}


/**
 * spice_display_send_keys:
 * @display: The #SpiceDisplay
 * @keyvals: (array length=nkeyvals): Keyval array
 * @nkeyvals: Length of keyvals
 * @kind: #SpiceDisplayKeyEvent action
 *
 * Send keyval press/release events to the display.
 *
 **/
void spice_display_send_keys(SpiceDisplay *display, const guint *keyvals,
                             int nkeyvals, SpiceDisplayKeyEvent kind)
{
    int i;

    g_return_if_fail(SPICE_IS_DISPLAY(display));
    g_return_if_fail(keyvals != NULL);

    DISPLAY_DEBUG(display, "%s", __FUNCTION__);

    if (kind & SPICE_DISPLAY_KEY_EVENT_PRESS) {
        for (i = 0 ; i < nkeyvals ; i++)
            send_key(display, get_scancode_from_keyval(display, keyvals[i]), SEND_KEY_PRESS, FALSE);
    }

    if (kind & SPICE_DISPLAY_KEY_EVENT_RELEASE) {
        for (i = (nkeyvals-1) ; i >= 0 ; i--)
            send_key(display, get_scancode_from_keyval(display, keyvals[i]), SEND_KEY_RELEASE, FALSE);
    }
}

static gboolean enter_event(GtkWidget *widget, GdkEventCrossing *crossing G_GNUC_UNUSED)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "%s", __FUNCTION__);

    d->mouse_have_pointer = true;
    spice_gtk_session_set_mouse_has_pointer(d->gtk_session, true);
    try_keyboard_grab(display);
    update_display(display);

    return true;
}

static gboolean leave_event(GtkWidget *widget, GdkEventCrossing *crossing G_GNUC_UNUSED)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "%s", __FUNCTION__);

    if (d->mouse_grab_active)
        return true;

    d->mouse_have_pointer = false;
    spice_gtk_session_set_mouse_has_pointer(d->gtk_session, false);
    try_keyboard_ungrab(display);

    return true;
}

static gboolean focus_in_event(GtkWidget *widget, GdkEventFocus *focus G_GNUC_UNUSED)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "%s", __FUNCTION__);

    /*
     * Ignore focus in when we already have the focus
     * (this happens when doing an ungrab from the leave_event callback).
     */
    if (d->keyboard_have_focus)
        return true;

    release_keys(display);
#ifdef G_OS_WIN32
    /* Reset the IME context of the focused window.
     * Note that the focused window can be different from SpiceDisplay
     * one but the events are received and forwarder by this window. */
    HWND hwnd_focused = GetFocus();
    if (hwnd_focused != NULL) {
        ImmAssociateContext(hwnd_focused, NULL);
    }
#endif
    if (!d->disable_inputs)
        spice_gtk_session_sync_keyboard_modifiers(d->gtk_session);
    if (d->keyboard_grab_released)
        memset(d->activeseq, 0, sizeof(gboolean) * d->grabseq->nkeysyms);
    update_keyboard_focus(display, true);
    try_keyboard_grab(display);

    if (gtk_widget_get_realized(widget))
        update_display(display);

    return true;
}

static gboolean focus_out_event(GtkWidget *widget, GdkEventFocus *focus G_GNUC_UNUSED)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "%s", __FUNCTION__);
    update_display(NULL);

    /*
     * Ignore focus out after a keyboard grab
     * (this happens when doing the grab from the enter_event callback).
     */
    if (d->keyboard_grab_active)
        return true;

    release_keys(display);
    update_keyboard_focus(display, false);

    return true;
}

static int button_gdk_to_spice(guint gdk)
{
    static const int map[] = {
        [ 1 ] = SPICE_MOUSE_BUTTON_LEFT,
        [ 2 ] = SPICE_MOUSE_BUTTON_MIDDLE,
        [ 3 ] = SPICE_MOUSE_BUTTON_RIGHT,
        [ 4 ] = SPICE_MOUSE_BUTTON_UP,
        [ 5 ] = SPICE_MOUSE_BUTTON_DOWN,
    };

    if (gdk < SPICE_N_ELEMENTS(map)) {
        return map [ gdk ];
    }
    return 0;
}

static int button_mask_gdk_to_spice(int gdk)
{
    int spice = 0;

    if (gdk & GDK_BUTTON1_MASK)
        spice |= SPICE_MOUSE_BUTTON_MASK_LEFT;
    if (gdk & GDK_BUTTON2_MASK)
        spice |= SPICE_MOUSE_BUTTON_MASK_MIDDLE;
    if (gdk & GDK_BUTTON3_MASK)
        spice |= SPICE_MOUSE_BUTTON_MASK_RIGHT;
    return spice;
}

static void transform_input(SpiceDisplay *display,
                            double window_x, double window_y,
                            int *input_x, int *input_y)
{
    SpiceDisplayPrivate *d = display->priv;
    int display_x, display_y, display_w, display_h;
    double is;

    spice_display_get_scaling(display, NULL,
                              &display_x, &display_y,
                              &display_w, &display_h);

    /* For input we need a different scaling factor in order to
       be able to reach the full width of a display. For instance, consider
       a display of 100 pixels showing in a window 10 pixels wide. The normal
       scaling factor here would be 100/10==10, but if you then take the largest
       possible window coordinate, i.e. 9 and multiply by 10 you get 90, not 99,
       which is the max display coord.

       If you want to be able to reach the last pixel in the window you need
       max_window_x * input_scale == max_display_x, which is
       (window_width - 1) * input_scale == (display_width - 1)

       Note, this is the inverse of s (i.e. s ~= 1/is) as we're converting the
       coordinates in the inverse direction (window -> display) as the fb size
       (display -> window).
    */
    is = (double)(d->area.width-1) / (double)(display_w-1);

    window_x -= display_x;
    window_y -= display_y;

    *input_x = floor (window_x * is);
    *input_y = floor (window_y * is);
}

static gboolean motion_event(GtkWidget *widget, GdkEventMotion *motion)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;
    int x, y;

    if (!d->inputs)
        return true;
    if (d->disable_inputs)
        return true;

    d->seq_pressed = FALSE;

    if (d->keyboard_grab_released && d->keyboard_have_focus) {
        d->keyboard_grab_released = FALSE;
        release_keys(display);
        try_keyboard_grab(display);
    }

    transform_input(display, motion->x, motion->y, &x, &y);

    switch (d->mouse_mode) {
    case SPICE_MOUSE_MODE_CLIENT:
        if (x >= 0 && x < d->area.width &&
            y >= 0 && y < d->area.height) {
            spice_inputs_position(d->inputs, x, y, get_display_id(display),
                                  button_mask_gdk_to_spice(motion->state));
        }
        break;
    case SPICE_MOUSE_MODE_SERVER:
        if (d->mouse_grab_active) {
            gint dx = d->mouse_last_x != -1 ? x - d->mouse_last_x : 0;
            gint dy = d->mouse_last_y != -1 ? y - d->mouse_last_y : 0;

            spice_inputs_motion(d->inputs, dx, dy,
                                button_mask_gdk_to_spice(motion->state));

            d->mouse_last_x = x;
            d->mouse_last_y = y;
            if (dx != 0 || dy != 0)
                mouse_wrap(display, motion);
        }
        break;
    default:
        g_warn_if_reached();
        break;
    }
    return true;
}

static gboolean scroll_event(GtkWidget *widget, GdkEventScroll *scroll)
{
    int button;
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "%s", __FUNCTION__);

    if (!d->inputs)
        return true;
    if (d->disable_inputs)
        return true;

    if (scroll->direction == GDK_SCROLL_UP)
        button = SPICE_MOUSE_BUTTON_UP;
    else if (scroll->direction == GDK_SCROLL_DOWN)
        button = SPICE_MOUSE_BUTTON_DOWN;
    else {
        DISPLAY_DEBUG(display, "unsupported scroll direction");
        return true;
    }

    spice_inputs_button_press(d->inputs, button,
                              button_mask_gdk_to_spice(scroll->state));
    spice_inputs_button_release(d->inputs, button,
                                button_mask_gdk_to_spice(scroll->state));
    return true;
}

static gboolean button_event(GtkWidget *widget, GdkEventButton *button)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;
    int x, y;

    DISPLAY_DEBUG(display, "%s %s: button %u, state 0x%x", __FUNCTION__,
                  button->type == GDK_BUTTON_PRESS ? "press" : "release",
                  button->button, button->state);

    if (d->disable_inputs)
        return true;

    transform_input(display, button->x, button->y, &x, &y);
    if ((x < 0 || x >= d->area.width ||
         y < 0 || y >= d->area.height) &&
        d->mouse_mode == SPICE_MOUSE_MODE_CLIENT) {
        /* rule out clicks in outside region */
        return true;
    }

    gtk_widget_grab_focus(widget);
    if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER) {
        if (!d->mouse_grab_active) {
            try_mouse_grab(display);
            return true;
        }
    } else {
        /* allow to drag and drop between windows/displays:

           By default, X (and other window system) do a pointer grab
           when you press a button, so that the release event is
           received by the same window regardless of where the pointer
           is. Here, we change that behaviour, so that you can press
           and release in two differents displays. This is only
           supported in client mouse mode.

           FIXME: should be multiple widget grab, but how?
           or should know the position of the other widgets?
        */
        ungrab_pointer(display);
    }

    if (!d->inputs)
        return true;

    switch (button->type) {
    case GDK_BUTTON_PRESS:
        spice_inputs_button_press(d->inputs,
                                  button_gdk_to_spice(button->button),
                                  button_mask_gdk_to_spice(button->state));
        break;
    case GDK_BUTTON_RELEASE:
        spice_inputs_button_release(d->inputs,
                                    button_gdk_to_spice(button->button),
                                    button_mask_gdk_to_spice(button->state));
        break;
    default:
        break;
    }
    return true;
}

static void size_allocate(GtkWidget *widget, GtkAllocation *conf, gpointer data)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;

    if (conf->width == d->ww && conf->height == d->wh &&
            conf->x == d->mx && conf->y == d->my) {
        return;
    }

    if (conf->width != d->ww  || conf->height != d->wh) {
        d->ww = conf->width;
        d->wh = conf->height;
        recalc_geometry(widget);
#ifndef G_OS_WIN32
        if (egl_enabled(d))
            spice_egl_resize_display(display, conf->width, conf->height);
#endif
    }

    d->mx = conf->x;
    d->my = conf->y;

#ifdef G_OS_WIN32
    if (d->mouse_grab_active) {
        try_mouse_ungrab(display);
        try_mouse_grab(display);
    }
#endif
}

static void update_image(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;

    spice_cairo_image_create(display);
    if (d->canvas.convert)
        do_color_convert(display, &d->area);
}

static void realize(GtkWidget *widget)
{
    SpiceDisplay *display = SPICE_DISPLAY(widget);
    SpiceDisplayPrivate *d = display->priv;

    GTK_WIDGET_CLASS(spice_display_parent_class)->realize(widget);

    d->keycode_map =
        vnc_display_keymap_gdk2xtkbd_table(gtk_widget_get_window(widget),
                                           &d->keycode_maplen);

    update_image(display);
}

static void unrealize(GtkWidget *widget)
{
    spice_cairo_image_destroy(SPICE_DISPLAY(widget));
#ifndef G_OS_WIN32
    spice_egl_unrealize_display(SPICE_DISPLAY(widget));
#endif

    GTK_WIDGET_CLASS(spice_display_parent_class)->unrealize(widget);
}


/* ---------------------------------------------------------------- */

static void spice_display_class_init(SpiceDisplayClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *gtkwidget_class = GTK_WIDGET_CLASS(klass);

    gtkwidget_class->key_press_event = key_event;
    gtkwidget_class->key_release_event = key_event;
    gtkwidget_class->enter_notify_event = enter_event;
    gtkwidget_class->leave_notify_event = leave_event;
    gtkwidget_class->focus_in_event = focus_in_event;
    gtkwidget_class->focus_out_event = focus_out_event;
    gtkwidget_class->motion_notify_event = motion_event;
    gtkwidget_class->button_press_event = button_event;
    gtkwidget_class->button_release_event = button_event;
    gtkwidget_class->scroll_event = scroll_event;
    gtkwidget_class->realize = realize;
    gtkwidget_class->unrealize = unrealize;

    gobject_class->constructor = spice_display_constructor;
    gobject_class->dispose = spice_display_dispose;
    gobject_class->finalize = spice_display_finalize;
    gobject_class->get_property = spice_display_get_property;
    gobject_class->set_property = spice_display_set_property;

    /**
     * SpiceDisplay:session:
     *
     * #SpiceSession for this #SpiceDisplay
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
     * SpiceDisplay:channel-id:
     *
     * channel-id for this #SpiceDisplay
     *
     **/
    g_object_class_install_property
        (gobject_class, PROP_CHANNEL_ID,
         g_param_spec_int("channel-id",
                          "Channel ID",
                          "Channel ID for this display",
                          0, 255, 0,
                          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_KEYBOARD_GRAB,
         g_param_spec_boolean("grab-keyboard",
                              "Grab Keyboard",
                              "Whether we should grab the keyboard.",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_MOUSE_GRAB,
         g_param_spec_boolean("grab-mouse",
                              "Grab Mouse",
                              "Whether we should grab the mouse.",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_RESIZE_GUEST,
         g_param_spec_boolean("resize-guest",
                              "Resize guest",
                              "Try to adapt guest display on window resize. "
                              "Requires guest cooperation.",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplay:ready:
     *
     * Indicate whether the display is ready to be shown. It takes
     * into account several conditions, such as the channel display
     * "mark" state, whether the monitor area is visible..
     *
     * Since: 0.13
     **/
    g_object_class_install_property
        (gobject_class, PROP_READY,
         g_param_spec_boolean("ready",
                              "Ready",
                              "Ready to display",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_SCALING,
         g_param_spec_boolean("scaling", "Scaling",
                              "Whether we should use scaling",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplay:only-downscale:
     *
     * If scaling, only scale down, never up.
     *
     * Since: 0.14
     **/
    g_object_class_install_property
        (gobject_class, PROP_ONLY_DOWNSCALE,
         g_param_spec_boolean("only-downscale", "Only Downscale",
                              "If scaling, only scale down, never up",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplay:keypress-delay:
     *
     * Delay in ms of non-modifiers key press events. If the key is
     * released before this delay, a single press & release event is
     * sent to the server. If the key is pressed longer than the
     * keypress-delay, the server will receive the delayed press
     * event, and a following release event when the key is released.
     *
     * Since: 0.13
     **/
    g_object_class_install_property
        (gobject_class, PROP_KEYPRESS_DELAY,
         g_param_spec_uint("keypress-delay", "Keypress delay",
                           "Keypress delay",
                           0, G_MAXUINT, DEFAULT_KEYPRESS_DELAY,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplay:disable-inputs:
     *
     * Disable all keyboard & mouse inputs.
     *
     * Since: 0.8
     **/
    g_object_class_install_property
        (gobject_class, PROP_DISABLE_INPUTS,
         g_param_spec_boolean("disable-inputs", "Disable inputs",
                              "Whether inputs should be disabled",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));


    /**
     * SpiceDisplay:zoom-level:
     *
     * Zoom level in percentage, from 10 to 400. Default to 100.
     * (this option is only supported with cairo backend when scaling
     * is enabled)
     *
     * Since: 0.10
     **/
    g_object_class_install_property
        (gobject_class, PROP_ZOOM_LEVEL,
         g_param_spec_int("zoom-level", "Zoom Level",
                          "Zoom Level",
                          10, 400, 100,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplay:monitor-id:
     *
     * Select monitor from #SpiceDisplay to show.
     * The value -1 means the whole display is shown.
     * By default, the monitor 0 is selected.
     *
     * Since: 0.13
     **/
    g_object_class_install_property
        (gobject_class, PROP_MONITOR_ID,
         g_param_spec_int("monitor-id",
                          "Monitor ID",
                          "Select monitor ID",
                          -1, G_MAXINT, 0,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

    /**
     * SpiceDisplay::mouse-grab:
     * @display: the #SpiceDisplay that emitted the signal
     * @status: 1 if grabbed, 0 otherwise.
     *
     * Notify when the mouse grab is active or not.
     **/
    signals[SPICE_DISPLAY_MOUSE_GRAB] =
        g_signal_new("mouse-grab",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayClass, mouse_grab),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    /**
     * SpiceDisplay::keyboard-grab:
     * @display: the #SpiceDisplay that emitted the signal
     * @status: 1 if grabbed, 0 otherwise.
     *
     * Notify when the keyboard grab is active or not.
     **/
    signals[SPICE_DISPLAY_KEYBOARD_GRAB] =
        g_signal_new("keyboard-grab",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayClass, keyboard_grab),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    /**
     * SpiceDisplay::grab-keys-pressed:
     * @display: the #SpiceDisplay that emitted the signal
     *
     * Notify when the grab keys have been pressed
     **/
    signals[SPICE_DISPLAY_GRAB_KEY_PRESSED] =
        g_signal_new("grab-keys-pressed",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceDisplayClass, keyboard_grab),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    g_type_class_add_private(klass, sizeof(SpiceDisplayPrivate));
}

/* ---------------------------------------------------------------- */

#define SPICE_GDK_BUTTONS_MASK \
    (GDK_BUTTON1_MASK|GDK_BUTTON2_MASK|GDK_BUTTON3_MASK|GDK_BUTTON4_MASK|GDK_BUTTON5_MASK)

static GdkDevice *spice_gdk_window_get_pointing_device(GdkWindow *window)
{
    GdkDisplay *gdk_display = gdk_window_get_display(window);
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#if GTK_CHECK_VERSION(3, 20, 0)
    return gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display));
#else
    return gdk_device_manager_get_client_pointer(gdk_display_get_device_manager(gdk_display));
#endif
    G_GNUC_END_IGNORE_DEPRECATIONS
}

static GdkModifierType spice_display_get_modifiers_state(SpiceDisplay *display)
{
    GdkModifierType modifiers;
    GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(display));

    if (window == NULL) {
        return 0;
    }

    gdk_window_get_device_position(window,
                                   spice_gdk_window_get_pointing_device(window),
                                   NULL,
                                   NULL,
                                   &modifiers);

    return modifiers;
}

static const gchar* mouse_mode_to_str(guint mode)
{
    static const gchar *const mouse_mode_str[] = {
        [SPICE_MOUSE_MODE_CLIENT] = "client",
        [SPICE_MOUSE_MODE_SERVER] = "server",
    };

    if (mode < G_N_ELEMENTS(mouse_mode_str) && mouse_mode_str[mode] != NULL) {
        return mouse_mode_str[mode];
    }
    return "unknown";
}

static void update_mouse_mode(SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;

    g_object_get(channel, "mouse-mode", &d->mouse_mode, NULL);
    DISPLAY_DEBUG(display, "mouse mode %u (%s)", d->mouse_mode, mouse_mode_to_str(d->mouse_mode));

    switch (d->mouse_mode) {
    case SPICE_MOUSE_MODE_CLIENT:
        try_mouse_ungrab(display);
        break;
    case SPICE_MOUSE_MODE_SERVER:
        d->mouse_guest_x = -1;
        d->mouse_guest_y = -1;

        if (spice_display_get_modifiers_state(display) & SPICE_GDK_BUTTONS_MASK) {
            try_mouse_grab(display);
        }
        break;
    default:
        g_warn_if_reached();
    }

    update_mouse_pointer(display);
}

static void update_area(SpiceDisplay *display,
                        gint x, gint y, gint width, gint height)
{
    SpiceDisplayPrivate *d = display->priv;
    GdkRectangle primary;

    DISPLAY_DEBUG(display, "update area +%d+%d %dx%d", x, y, width, height);
    d->area = (GdkRectangle) {
        .x = x,
        .y = y,
        .width = width,
        .height = height
    };

#ifndef G_OS_WIN32
    if (egl_enabled(d)) {
        const SpiceGlScanout *so =
            spice_display_get_gl_scanout(SPICE_DISPLAY_CHANNEL(d->display));
        g_return_if_fail(so != NULL);
        primary = (GdkRectangle) {
            .width = so->width,
            .height = so->height
        };
    } else
#endif
    {
        primary = (GdkRectangle) {
            .width = d->canvas.width,
            .height = d->canvas.height
        };
    }

    DISPLAY_DEBUG(display, "primary: %dx%d", primary.width, primary.height);
    if (!gdk_rectangle_intersect(&primary, &d->area, &d->area)) {
        DISPLAY_DEBUG(display, "The monitor area is not intersecting primary surface");
        memset(&d->area, '\0', sizeof(d->area));
        set_monitor_ready(display, false);
        return;
    }

    if (!egl_enabled(d)) {
        spice_cairo_image_destroy(display);
        if (gtk_widget_get_realized(GTK_WIDGET(display)))
            update_image(display);
    }

    update_size_request(display);

    set_monitor_ready(display, true);
}

static void primary_create(SpiceChannel *channel, gint format,
                           gint width, gint height, gint stride,
                           gint shmid, gpointer imgdata, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;

    d->canvas.format = format;
    d->canvas.stride = stride;
    d->canvas.width = width;
    d->canvas.height = height;
    d->canvas.data_origin = d->canvas.data = imgdata;

    spice_display_widget_update_monitor_area(display);
}

static void primary_destroy(SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = SPICE_DISPLAY(data);
    SpiceDisplayPrivate *d = display->priv;

    spice_cairo_image_destroy(display);
    d->canvas.width  = 0;
    d->canvas.height = 0;
    d->canvas.stride = 0;
    d->canvas.data = NULL;
    d->canvas.data_origin = NULL;
    set_monitor_ready(display, false);
}

static void queue_draw_area(SpiceDisplay *display, gint x, gint y,
                            gint width, gint height)
{
    if (!gtk_widget_get_has_window(GTK_WIDGET(display))) {
        GtkAllocation allocation;

        gtk_widget_get_allocation(GTK_WIDGET(display), &allocation);
        x += allocation.x;
        y += allocation.y;
    }

    gtk_widget_queue_draw_area(GTK_WIDGET(display),
                               x, y, width, height);
}

static void invalidate(SpiceChannel *channel,
                       gint x, gint y, gint w, gint h, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;
    int display_x, display_y;
    int x1, y1, x2, y2;
    double s;
    GdkRectangle rect = {
        .x = x,
        .y = y,
        .width = w,
        .height = h
    };

#ifndef G_OS_WIN32
    set_egl_enabled(display, false);
#endif

    if (!gtk_widget_get_window(GTK_WIDGET(display)))
        return;

    if (!gdk_rectangle_intersect(&rect, &d->area, &rect))
        return;

    if (d->canvas.convert)
        do_color_convert(display, &rect);

    spice_display_get_scaling(display, &s,
                              &display_x, &display_y,
                              NULL, NULL);

    x1 = floor ((rect.x - d->area.x) * s);
    y1 = floor ((rect.y - d->area.y) * s);
    x2 = ceil ((rect.x - d->area.x + rect.width) * s);
    y2 = ceil ((rect.y - d->area.y + rect.height) * s);

    queue_draw_area(display,
                    display_x + x1, display_y + y1,
                    x2 - x1, y2 - y1);
}

static void mark(SpiceDisplay *display, gint mark)
{
    SpiceDisplayPrivate *d = display->priv;
    g_return_if_fail(d != NULL);

    DISPLAY_DEBUG(display, "widget mark: %d, display %p", mark, display);
    d->mark = mark;
    update_ready(display);
}

static void cursor_set(SpiceCursorChannel *channel,
                       gint width, gint height, gint hot_x, gint hot_y,
                       gpointer rgba, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;
    GdkCursor *cursor = NULL;

    cursor_invalidate(display);

    g_clear_object(&d->mouse_pixbuf);

    if (rgba != NULL) {
        d->mouse_pixbuf = gdk_pixbuf_new_from_data(g_memdup(rgba, width * height * 4),
                                                   GDK_COLORSPACE_RGB,
                                                   TRUE, 8,
                                                   width,
                                                   height,
                                                   width * 4,
                                                   (GdkPixbufDestroyNotify)g_free, NULL);
        d->mouse_hotspot.x = hot_x;
        d->mouse_hotspot.y = hot_y;
        cursor = gdk_cursor_new_from_pixbuf(gtk_widget_get_display(GTK_WIDGET(display)),
                                            d->mouse_pixbuf, hot_x, hot_y);
    } else
        g_warn_if_reached();

#ifndef G_OS_WIN32
    if (egl_enabled(d))
        spice_egl_cursor_set(display);
#endif
    if (d->show_cursor) {
        /* unhide */
        g_clear_pointer(&d->show_cursor, g_object_unref);
        if (d->mouse_mode == SPICE_MOUSE_MODE_SERVER) {
            /* keep a hidden cursor, will be shown in cursor_move() */
            d->show_cursor = cursor;
            return;
        }
    }

    if (d->mouse_cursor != NULL) {
        g_object_unref(d->mouse_cursor);
    }
    d->mouse_cursor = cursor;

    update_mouse_pointer(display);
    cursor_invalidate(display);
}

static void cursor_hide(SpiceCursorChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;

    if (d->show_cursor != NULL) /* then we are already hidden */
        return;

    cursor_invalidate(display);
    d->show_cursor = d->mouse_cursor;
    d->mouse_cursor = spice_display_get_blank_cursor(display);
    update_mouse_pointer(display);
}

G_GNUC_INTERNAL
void spice_display_get_scaling(SpiceDisplay *display,
                               double *s_out,
                               int *x_out, int *y_out,
                               int *w_out, int *h_out)
{
    SpiceDisplayPrivate *d = display->priv;
    int fbw = d->area.width, fbh = d->area.height;
    int ww, wh;
    int x, y, w, h;
    double s;

    if (gtk_widget_get_realized (GTK_WIDGET(display))) {
        ww = gtk_widget_get_allocated_width(GTK_WIDGET(display));
        wh = gtk_widget_get_allocated_height(GTK_WIDGET(display));
    } else {
        ww = fbw;
        wh = fbh;
    }

    if (!spice_cairo_is_scaled(display)) {
        s = 1.0;
        x = 0;
        y = 0;
        if (ww > d->area.width)
            x = (ww - d->area.width) / 2;
        if (wh > d->area.height)
            y = (wh - d->area.height) / 2;
        w = fbw;
        h = fbh;
    } else {
        s = MIN ((double)ww / (double)fbw, (double)wh / (double)fbh);

        if (d->only_downscale && s >= 1.0)
            s = 1.0;

        /* Round to int size */
        w = floor (fbw * s + 0.5);
        h = floor (fbh * s + 0.5);

        /* Center the display */
        x = (ww - w) / 2;
        y = (wh - h) / 2;
    }

    if (s_out)
        *s_out = s;
    if (w_out)
        *w_out = w;
    if (h_out)
        *h_out = h;
    if (x_out)
        *x_out = x;
    if (y_out)
        *y_out = y;
}

static void cursor_invalidate(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    double s;
    int x, y;

    if (!gtk_widget_get_realized (GTK_WIDGET(display)))
        return;

    if (d->mouse_pixbuf == NULL)
        return;

    if (!d->ready || !d->monitor_ready)
        return;

    spice_display_get_scaling(display, &s, &x, &y, NULL, NULL);

    queue_draw_area(display,
                    floor ((d->mouse_guest_x - d->mouse_hotspot.x - d->area.x) * s) + x,
                    floor ((d->mouse_guest_y - d->mouse_hotspot.y - d->area.y) * s) + y,
                    ceil (gdk_pixbuf_get_width(d->mouse_pixbuf) * s),
                    ceil (gdk_pixbuf_get_height(d->mouse_pixbuf) * s));
}

static void cursor_move(SpiceCursorChannel *channel, gint x, gint y, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;

    cursor_invalidate(display);

    d->mouse_guest_x = x;
    d->mouse_guest_y = y;

    cursor_invalidate(display);

    /* apparently we have to restore cursor when "cursor_move" */
    if (d->show_cursor != NULL) {
        g_clear_object(&d->mouse_cursor);
        d->mouse_cursor = d->show_cursor;
        d->show_cursor = NULL;
        update_mouse_pointer(display);
    }
}

static void cursor_reset(SpiceCursorChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    GdkWindow *window = gtk_widget_get_window(GTK_WIDGET(display));

    if (!window) {
        DISPLAY_DEBUG(display, "%s: no window, returning",  __FUNCTION__);
        return;
    }

    DISPLAY_DEBUG(display, "%s",  __FUNCTION__);
    gdk_window_set_cursor(window, NULL);
}

static void inputs_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                                 gpointer data)
{
    SpiceDisplay *display = data;
    guint delay = DEFAULT_KEYPRESS_DELAY;
    GSocket *sock;

    if (event != SPICE_CHANNEL_OPENED)
        return;

    g_object_get(channel, "socket", &sock, NULL);
    if (g_socket_get_family(sock) == G_SOCKET_FAMILY_UNIX) {
        delay = 0;
    }
    g_object_unref(sock);

    spice_display_set_keypress_delay(display, delay);
}

#ifndef G_OS_WIN32
G_GNUC_INTERNAL
void spice_display_widget_gl_scanout(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d = display->priv;
    GError *err = NULL;

    DISPLAY_DEBUG(display, "%s: got scanout",  __FUNCTION__);

#ifdef GDK_WINDOWING_X11
    GtkWidget *area = gtk_stack_get_child_by_name(d->stack, "draw-area");

    if (GDK_IS_X11_DISPLAY(gdk_display_get_default()) &&
        !d->egl.context_ready &&
        gtk_widget_get_realized(area)) {
        if (!spice_egl_init(display, &err)) {
            g_critical("egl init failed: %s", err->message);
            g_clear_error(&err);
        }

        if (!spice_egl_realize_display(display, gtk_widget_get_window(area), &err)) {
            g_critical("egl realize failed: %s", err->message);
            g_clear_error(&err);
        }
    }
#endif

    set_egl_enabled(display, true);

    if (d->egl.context_ready) {
        const SpiceGlScanout *scanout;

        scanout = spice_display_get_gl_scanout(SPICE_DISPLAY_CHANNEL(d->display));
        /* should only be called when the display has a scanout */
        g_return_if_fail(scanout != NULL);

        if (!spice_egl_update_scanout(display, scanout, &err)) {
            g_critical("update scanout failed: %s", err->message);
            g_clear_error(&err);
        }
    }
}

static void gl_draw(SpiceDisplay *display,
                    guint32 x, guint32 y, guint32 w, guint32 h)
{
    SpiceDisplayPrivate *d = display->priv;

    DISPLAY_DEBUG(display, "%s",  __FUNCTION__);

    set_egl_enabled(display, true);

    g_return_if_fail(d->egl.context_ready);

#if GTK_CHECK_VERSION(3,16,0)
    GtkWidget *gl = gtk_stack_get_child_by_name(d->stack, "gl-area");

    if (gtk_stack_get_visible_child(d->stack) == gl) {
        /* Ignore GLib's too-new warnings */
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        gtk_gl_area_queue_render(GTK_GL_AREA(gl));
        G_GNUC_END_IGNORE_DEPRECATIONS
        d->egl.call_draw_done = TRUE;
    } else
#endif
    {
        spice_egl_update_display(display);
        spice_display_gl_draw_done(SPICE_DISPLAY_CHANNEL(d->display));
    }
}
#endif

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        d->main = SPICE_MAIN_CHANNEL(channel);
        spice_g_signal_connect_object(channel, "main-mouse-update",
                                      G_CALLBACK(update_mouse_mode), display, 0);
        update_mouse_mode(channel, display);
        return;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        SpiceDisplayPrimary primary;
        if (id != d->channel_id)
            return;
        d->display = channel;
        spice_g_signal_connect_object(channel, "display-primary-create",
                                      G_CALLBACK(primary_create), display, 0);
        spice_g_signal_connect_object(channel, "display-primary-destroy",
                                      G_CALLBACK(primary_destroy), display, 0);
        spice_g_signal_connect_object(channel, "display-invalidate",
                                      G_CALLBACK(invalidate), display, 0);
        spice_g_signal_connect_object(channel, "display-mark",
                                      G_CALLBACK(mark), display, G_CONNECT_AFTER | G_CONNECT_SWAPPED);
        spice_g_signal_connect_object(channel, "notify::monitors",
                                      G_CALLBACK(spice_display_widget_update_monitor_area),
                                      display, G_CONNECT_AFTER | G_CONNECT_SWAPPED);
        if (spice_display_get_primary(channel, 0, &primary)) {
            primary_create(channel, primary.format, primary.width, primary.height,
                           primary.stride, primary.shmid, primary.data, display);
            mark(display, primary.marked);
        }

#ifndef G_OS_WIN32
        spice_g_signal_connect_object(channel, "notify::gl-scanout",
                                      G_CALLBACK(spice_display_widget_gl_scanout),
                                      display, G_CONNECT_SWAPPED);
        spice_g_signal_connect_object(channel, "gl-draw",
                                      G_CALLBACK(gl_draw), display, G_CONNECT_SWAPPED);
#endif

        spice_channel_connect(channel);
        return;
    }

    if (SPICE_IS_CURSOR_CHANNEL(channel)) {
        if (id != d->channel_id)
            return;
        d->cursor = SPICE_CURSOR_CHANNEL(channel);
        spice_g_signal_connect_object(channel, "cursor-set",
                                      G_CALLBACK(cursor_set), display, 0);
        spice_g_signal_connect_object(channel, "cursor-move",
                                      G_CALLBACK(cursor_move), display, 0);
        spice_g_signal_connect_object(channel, "cursor-hide",
                                      G_CALLBACK(cursor_hide), display, 0);
        spice_g_signal_connect_object(channel, "cursor-reset",
                                      G_CALLBACK(cursor_reset), display, 0);
        spice_channel_connect(channel);
        return;
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        d->inputs = SPICE_INPUTS_CHANNEL(channel);
        spice_channel_connect(channel);
        spice_g_signal_connect_object(channel, "channel-event",
                                      G_CALLBACK(inputs_channel_event), display, 0);
        return;
    }

#ifdef USE_SMARTCARD
    if (SPICE_IS_SMARTCARD_CHANNEL(channel)) {
        d->smartcard = SPICE_SMARTCARD_CHANNEL(channel);
        spice_channel_connect(channel);
        return;
    }
#endif

    return;
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    SpiceDisplay *display = data;
    SpiceDisplayPrivate *d = display->priv;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    DISPLAY_DEBUG(display, "channel_destroy %d", id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        d->main = NULL;
        return;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id != d->channel_id)
            return;
        primary_destroy(d->display, display);
        d->display = NULL;
        return;
    }

    if (SPICE_IS_CURSOR_CHANNEL(channel)) {
        if (id != d->channel_id)
            return;
        d->cursor = NULL;
        return;
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        d->inputs = NULL;
        return;
    }

#ifdef USE_SMARTCARD
    if (SPICE_IS_SMARTCARD_CHANNEL(channel)) {
        d->smartcard = NULL;
        return;
    }
#endif

    return;
}

/**
 * spice_display_new:
 * @session: a #SpiceSession
 * @channel_id: the display channel ID to associate with #SpiceDisplay
 *
 * Creates a new #SpiceDisplay widget.
 *
 * Returns: a new #SpiceDisplay widget.
 **/
SpiceDisplay *spice_display_new(SpiceSession *session, int channel_id)
{
    return g_object_new(SPICE_TYPE_DISPLAY, "session", session,
                        "channel-id", channel_id, NULL);
}

/**
 * spice_display_new_with_monitor:
 * @session: a #SpiceSession
 * @channel_id: the display channel ID to associate with #SpiceDisplay
 * @monitor_id: the monitor id within the display channel
 *
 * Creates a new #SpiceDisplay widget associated with the monitor id.
 *
 * Since: 0.13
 * Returns: a new #SpiceDisplay widget.
 **/
SpiceDisplay* spice_display_new_with_monitor(SpiceSession *session, gint channel_id, gint monitor_id)
{
    return g_object_new(SPICE_TYPE_DISPLAY,
                        "session", session,
                        "channel-id", channel_id,
                        "monitor-id", monitor_id,
                        NULL);
}

/**
 * spice_display_mouse_ungrab:
 * @display: a #SpiceDisplay
 *
 * Ungrab the mouse.
 **/
void spice_display_mouse_ungrab(SpiceDisplay *display)
{
    g_return_if_fail(SPICE_IS_DISPLAY(display));

    try_mouse_ungrab(display);
}

/**
 * spice_display_get_pixbuf:
 * @display: a #SpiceDisplay
 *
 * Take a screenshot of the display.
 *
 * Returns: (transfer full): a #GdkPixbuf with the screenshot image buffer
 **/
GdkPixbuf *spice_display_get_pixbuf(SpiceDisplay *display)
{
    SpiceDisplayPrivate *d;
    GdkPixbuf *pixbuf;
    guchar *data;

    g_return_val_if_fail(SPICE_IS_DISPLAY(display), NULL);

    d = display->priv;

    g_return_val_if_fail(d != NULL, NULL);
    g_return_val_if_fail(d->display != NULL, NULL);

#ifndef G_OS_WIN32
    if (egl_enabled(d)) {
        GdkPixbuf *tmp;

        data = g_malloc0(d->area.width * d->area.height * 4);
        glReadBuffer(GL_FRONT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glReadPixels(0, 0, d->area.width, d->area.height,
                     GL_RGBA, GL_UNSIGNED_BYTE, data);
        tmp = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, true,
                                       8, d->area.width, d->area.height,
                                       d->area.width * 4,
                                       (GdkPixbufDestroyNotify)g_free, NULL);
        pixbuf = gdk_pixbuf_flip(tmp, false);
        g_object_unref(tmp);
    } else
#endif
    {
        guchar *src, *dest;
        int x, y;

        /* TODO: ensure d->data has been exposed? */
        g_return_val_if_fail(d->canvas.data != NULL, NULL);
        data = g_malloc0(d->area.width * d->area.height * 3);
        src = d->canvas.data;
        dest = data;

        src += d->area.y * d->canvas.stride + d->area.x * 4;
        for (y = 0; y < d->area.height; ++y) {
            for (x = 0; x < d->area.width; ++x) {
                dest[0] = src[x * 4 + 2];
                dest[1] = src[x * 4 + 1];
                dest[2] = src[x * 4 + 0];
                dest += 3;
            }
            src += d->canvas.stride;
        }
        pixbuf = gdk_pixbuf_new_from_data(data, GDK_COLORSPACE_RGB, false,
                                          8, d->area.width, d->area.height,
                                          d->area.width * 3,
                                          (GdkPixbufDestroyNotify)g_free, NULL);
    }

    return pixbuf;
}
