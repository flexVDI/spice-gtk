/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010-2011 Red Hat, Inc.

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
#include <glib.h>

#include <sys/stat.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef USE_SMARTCARD
#include <vreader.h>
#include "smartcard-manager.h"
#endif

#ifdef WITH_FLEXVDI
#include <flexvdi-port.h>
#include "flexvdi_client_icon.h"
#endif

#include "glib-compat.h"
#include "spice-widget.h"
#include "spice-gtk-session.h"
#include "spice-audio.h"
#include "spice-common.h"
#include "spice-cmdline.h"
#include "spice-option.h"
#include "usb-device-widget.h"
#include <gdk/gdkkeysyms.h>

#include "spicy-connect.h"

typedef struct spice_connection spice_connection;

enum {
    STATE_SCROLL_LOCK,
    STATE_CAPS_LOCK,
    STATE_NUM_LOCK,
    STATE_MAX,
};

#define SPICE_TYPE_WINDOW                  (spice_window_get_type ())
#define SPICE_WINDOW(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SPICE_TYPE_WINDOW, SpiceWindow))
#define SPICE_IS_WINDOW(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SPICE_TYPE_WINDOW))
#define SPICE_WINDOW_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SPICE_TYPE_WINDOW, SpiceWindowClass))
#define SPICE_IS_WINDOW_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SPICE_TYPE_WINDOW))
#define SPICE_WINDOW_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SPICE_TYPE_WINDOW, SpiceWindowClass))

typedef struct _SpiceWindow SpiceWindow;
typedef struct _SpiceWindowClass SpiceWindowClass;

struct _SpiceWindow {
    GObject          object;
    spice_connection *conn;
    gint             id;
    gint             monitor_id;
    GtkWidget        *toplevel, *spice;
    GtkWidget        *menubar, *toolbar, *fullscreen_menubar;
    GtkWidget        *ritem, *rmenu;
    GtkWidget        *statusbar, *status, *st[STATE_MAX];
    GtkActionGroup   *ag;
    GtkUIManager     *ui;
    bool             fullscreen;
    bool             mouse_grabbed;
    SpiceChannel     *display_channel;
#ifdef G_OS_WIN32
    gint             win_x;
    gint             win_y;
#endif
    bool             enable_accels_save;
    bool             enable_mnemonics_save;
};

struct _SpiceWindowClass
{
  GObjectClass parent_class;
};

G_DEFINE_TYPE (SpiceWindow, spice_window, G_TYPE_OBJECT);

#define CHANNELID_MAX 4
#define MONITORID_MAX 4

// FIXME: turn this into an object, get rid of fixed wins array, use
// signals to replace the various callback that iterate over wins array
struct spice_connection {
    SpiceSession     *session;
    SpiceGtkSession  *gtk_session;
    SpiceMainChannel *main;
    SpiceWindow     *wins[CHANNELID_MAX * MONITORID_MAX];
    SpiceAudio       *audio;
    const char       *mouse_state;
    const char       *agent_state;
    const char       *stream_state;
    gboolean         agent_connected;
    int              channels;
    int              disconnecting;
};

static enum {
    DISCONNECT_NO_ERROR = 0,
    DISCONNECT_CONN_ERROR,
    DISCONNECT_IO_ERROR,
    DISCONNECT_AUTH_ERROR,
} disconnect_reason;

static spice_connection *connection_new(void);
static void connection_connect(spice_connection *conn);
static void connection_disconnect(spice_connection *conn, int reason);
static void connection_destroy(spice_connection *conn);
static void usb_connect_failed(GObject               *object,
                               SpiceUsbDevice        *device,
                               GError                *error,
                               gpointer               data);
static gboolean is_gtk_session_property(const gchar *property);
static void del_window(spice_connection *conn, SpiceWindow *win);

/* options */
static gboolean fullscreen = false;
static gboolean fullscreen_on_all_monitors = false;
static gboolean version = false;
static char *spicy_title = NULL;
static gboolean kiosk_mode = false;
static gboolean disable_power_events = false;
static gboolean disable_copy_to_guest = false;
static gboolean disable_paste_from_guest = false;
/* globals */
static GMainLoop     *mainloop = NULL;
static int           connections = 0;
static GKeyFile      *keyfile = NULL;
#ifndef WITH_FLEXVDI
static SpicePortChannel*stdin_port = NULL;
#endif

/* ------------------------------------------------------------------ */

static int ask_user(GtkWidget *parent, char *title, char *message,
                    char *dest, int dlen, int hide)
{
    GtkWidget *dialog, *area, *label, *entry;
    const char *txt;
    int retval;

    /* Create the widgets */
    dialog = gtk_dialog_new_with_buttons(title,
                                         parent ? GTK_WINDOW(parent) : NULL,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_OK,
                                         GTK_RESPONSE_ACCEPT,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_REJECT,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    label = gtk_label_new(message);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    gtk_box_pack_start(GTK_BOX(area), label, FALSE, FALSE, 5);

    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), dest);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    if (hide)
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_box_pack_start(GTK_BOX(area), entry, FALSE, FALSE, 5);

    /* show and wait for response */
    gtk_widget_show_all(dialog);
    switch (gtk_dialog_run(GTK_DIALOG(dialog))) {
    case GTK_RESPONSE_ACCEPT:
        txt = gtk_entry_get_text(GTK_ENTRY(entry));
        snprintf(dest, dlen, "%s", txt);
        retval = 0;
        break;
    default:
        retval = -1;
        break;
    }
    gtk_widget_destroy(dialog);
    return retval;
}

static void update_status_window(SpiceWindow *win)
{
    gchar *status;

    if (win == NULL)
        return;

    if (win->mouse_grabbed) {
        SpiceGrabSequence *sequence = spice_display_get_grab_keys(SPICE_DISPLAY(win->spice));
        gchar *seq = spice_grab_sequence_as_string(sequence);
        status = g_strdup_printf("Use %s to ungrab mouse.", seq);
        g_free(seq);
    } else {
        if (spice_util_get_debug())
            status = g_strdup_printf("mouse: %s, agent: %s | stream: %s",
                    win->conn->mouse_state, win->conn->agent_state, win->conn->stream_state);
        else
            status = g_strdup_printf("mouse: %s, agent: %s",
                    win->conn->mouse_state, win->conn->agent_state);
    }

    gtk_label_set_text(GTK_LABEL(win->status), status);
    g_free(status);
}

static void update_status(struct spice_connection *conn)
{
    int i;

    for (i = 0; i < SPICE_N_ELEMENTS(conn->wins); i++) {
        if (conn->wins[i] == NULL)
            continue;
        update_status_window(conn->wins[i]);
    }
}

static const char *spice_edit_properties[] = {
    "CopyToGuest",
    "PasteFromGuest",
};

static void update_edit_menu_window(SpiceWindow *win)
{
    int i;
    GtkAction *toggle;
    gboolean disabled_entries[G_N_ELEMENTS(spice_edit_properties)] = {
        disable_copy_to_guest,
        disable_paste_from_guest,
        false };

    if (win == NULL) {
        return;
    }

    /* Make "CopyToGuest" and "PasteFromGuest" insensitive if spice
     * agent is not connected */
    for (i = 0; i < G_N_ELEMENTS(spice_edit_properties); i++) {
        toggle = gtk_action_group_get_action(win->ag, spice_edit_properties[i]);
        if (toggle) {
            gtk_action_set_sensitive(toggle, win->conn->agent_connected && !disabled_entries[i]);
        }
    }
}

static void update_edit_menu(struct spice_connection *conn)
{
    int i;

    for (i = 0; i < SPICE_N_ELEMENTS(conn->wins); i++) {
        if (conn->wins[i]) {
            update_edit_menu_window(conn->wins[i]);
        }
    }
}

static void menu_cb_connect(GtkAction *action, void *data)
{
    struct spice_connection *conn;

    conn = connection_new();
    connection_connect(conn);
}

static void menu_cb_close(GtkAction *action, void *data)
{
    SpiceWindow *win = data;

    connection_disconnect(win->conn, DISCONNECT_NO_ERROR);
}

static void menu_cb_copy(GtkAction *action, void *data)
{
    SpiceWindow *win = data;

    spice_gtk_session_copy_to_guest(win->conn->gtk_session);
}

static void menu_cb_paste(GtkAction *action, void *data)
{
    SpiceWindow *win = data;

    spice_gtk_session_paste_from_guest(win->conn->gtk_session);
}

static void window_set_fullscreen(SpiceWindow *win, gboolean fs)
{
    if (fs) {
#ifdef G_OS_WIN32
        gtk_window_get_position(GTK_WINDOW(win->toplevel), &win->win_x, &win->win_y);
#endif
        gtk_window_fullscreen(GTK_WINDOW(win->toplevel));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(win->toplevel));
#ifdef G_OS_WIN32
        gtk_window_move(GTK_WINDOW(win->toplevel), win->win_x, win->win_y);
#endif
    }
}

static void menu_cb_fullscreen(GtkAction *action, void *data)
{
    SpiceWindow *win = data;

    window_set_fullscreen(win, !win->fullscreen);
}

static void menu_cb_minimize(GtkAction *action, void *data)
{
    SpiceWindow *win = data;

    gtk_window_iconify(GTK_WINDOW(win->toplevel));
}

#ifdef USE_SMARTCARD
static void enable_smartcard_actions(SpiceWindow *win, VReader *reader,
                                     gboolean can_insert, gboolean can_remove)
{
    GtkAction *action;

    if ((reader != NULL) && (!spice_smartcard_reader_is_software((SpiceSmartcardReader*)reader)))
    {
        /* Having menu actions to insert/remove smartcards only makes sense
         * for software smartcard readers, don't do anything when the event
         * we received was for a "real" smartcard reader.
         */
        return;
    }
    action = gtk_action_group_get_action(win->ag, "InsertSmartcard");
    g_return_if_fail(action != NULL);
    gtk_action_set_sensitive(action, can_insert);
    action = gtk_action_group_get_action(win->ag, "RemoveSmartcard");
    g_return_if_fail(action != NULL);
    gtk_action_set_sensitive(action, can_remove);
}


static void reader_added_cb(SpiceSmartcardManager *manager, VReader *reader,
                            gpointer user_data)
{
    enable_smartcard_actions(user_data, reader, TRUE, FALSE);
}

static void reader_removed_cb(SpiceSmartcardManager *manager, VReader *reader,
                              gpointer user_data)
{
    enable_smartcard_actions(user_data, reader, FALSE, FALSE);
}

static void card_inserted_cb(SpiceSmartcardManager *manager, VReader *reader,
                             gpointer user_data)
{
    enable_smartcard_actions(user_data, reader, FALSE, TRUE);
}

static void card_removed_cb(SpiceSmartcardManager *manager, VReader *reader,
                            gpointer user_data)
{
    enable_smartcard_actions(user_data, reader, TRUE, FALSE);
}

static void menu_cb_insert_smartcard(GtkAction *action, void *data)
{
    spice_smartcard_manager_insert_card(spice_smartcard_manager_get());
}

static void menu_cb_remove_smartcard(GtkAction *action, void *data)
{
    spice_smartcard_manager_remove_card(spice_smartcard_manager_get());
}
#endif

#ifdef USE_USBREDIR
static void remove_cb(GtkContainer *container, GtkWidget *widget, void *data)
{
    gtk_window_resize(GTK_WINDOW(data), 1, 1);
}

static void menu_cb_select_usb_devices(GtkAction *action, void *data)
{
    GtkWidget *dialog, *area, *usb_device_widget;
    SpiceWindow *win = data;

    /* Create the widgets */
    dialog = gtk_dialog_new_with_buttons(
                    "Select USB devices for redirection",
                    GTK_WINDOW(win->toplevel),
                    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                    GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT,
                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_container_set_border_width(GTK_CONTAINER(dialog), 12);
    gtk_box_set_spacing(GTK_BOX(gtk_bin_get_child(GTK_BIN(dialog))), 12);

    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    usb_device_widget = spice_usb_device_widget_new(win->conn->session,
                                                    NULL); /* default format */
    g_signal_connect(usb_device_widget, "connect-failed",
                     G_CALLBACK(usb_connect_failed), NULL);
    gtk_box_pack_start(GTK_BOX(area), usb_device_widget, TRUE, TRUE, 0);

    /* This shrinks the dialog when USB devices are unplugged */
    g_signal_connect(usb_device_widget, "remove",
                     G_CALLBACK(remove_cb), dialog);

    /* show and run */
    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}
#endif

static void send_keystroke_cb(GtkAction *action, void *data)
{
    SpiceWindow *win = data;
    guint keyvals[3] = {
        GDK_KEY_Control_L,
        GDK_KEY_Alt_L,
        GDK_KEY_F1
    };
    int numKeys = 3;
    if (!strcmp(gtk_action_get_name(action), "CAD")) {
        keyvals[2] = GDK_KEY_Delete;
    } else if (!strcmp(gtk_action_get_name(action), "WCR")) {
        keyvals[1] = GDK_KEY_Meta_L;
        keyvals[2] = GDK_KEY_Right;
    } else if (!strcmp(gtk_action_get_name(action), "WCL")) {
        keyvals[1] = GDK_KEY_Meta_L;
        keyvals[2] = GDK_KEY_Left;
    } else if (!strcmp(gtk_action_get_name(action), "WL")) {
        keyvals[0] = GDK_KEY_Meta_L;
        keyvals[1] = GDK_KEY_l;
        numKeys = 2;
    } else if (!strcmp(gtk_action_get_name(action), "CAF1")) {
        keyvals[2] = GDK_KEY_F1;
    } else if (!strcmp(gtk_action_get_name(action), "CAF2")) {
        keyvals[2] = GDK_KEY_F2;
    } else if (!strcmp(gtk_action_get_name(action), "CAF3")) {
        keyvals[2] = GDK_KEY_F3;
    } else if (!strcmp(gtk_action_get_name(action), "CAF4")) {
        keyvals[2] = GDK_KEY_F4;
    } else if (!strcmp(gtk_action_get_name(action), "CAF5")) {
        keyvals[2] = GDK_KEY_F5;
    } else if (!strcmp(gtk_action_get_name(action), "CAF6")) {
        keyvals[2] = GDK_KEY_F6;
    } else if (!strcmp(gtk_action_get_name(action), "CAF7")) {
        keyvals[2] = GDK_KEY_F7;
    } else if (!strcmp(gtk_action_get_name(action), "CAF8")) {
        keyvals[2] = GDK_KEY_F8;
    } else if (!strcmp(gtk_action_get_name(action), "CAF9")) {
        keyvals[2] = GDK_KEY_F9;
    } else if (!strcmp(gtk_action_get_name(action), "CAF10")) {
        keyvals[2] = GDK_KEY_F10;
    } else if (!strcmp(gtk_action_get_name(action), "CAF11")) {
        keyvals[2] = GDK_KEY_F11;
    } else if (!strcmp(gtk_action_get_name(action), "CAF12")) {
        keyvals[2] = GDK_KEY_F12;
    } else return;

    spice_display_send_keys(SPICE_DISPLAY(win->spice), keyvals, numKeys,
                            SPICE_DISPLAY_KEY_EVENT_PRESS | SPICE_DISPLAY_KEY_EVENT_RELEASE);
}

static void menu_cb_bool_prop(GtkToggleAction *action, gpointer data)
{
    SpiceWindow *win = data;
    gboolean state = gtk_toggle_action_get_active(action);
    const char *name;
    gpointer object;

    name = gtk_action_get_name(GTK_ACTION(action));
    SPICE_DEBUG("%s: %s = %s", __FUNCTION__, name, state ? "yes" : "no");

    g_key_file_set_boolean(keyfile, "general", name, state);

    if (is_gtk_session_property(name)) {
        object = win->conn->gtk_session;
    } else {
        object = win->spice;
    }
    g_object_set(object, name, state, NULL);
}

static void menu_cb_conn_bool_prop_changed(GObject    *gobject,
                                           GParamSpec *pspec,
                                           gpointer    user_data)
{
    SpiceWindow *win = user_data;
    const gchar *property = g_param_spec_get_name(pspec);
    GtkAction *toggle;
    gboolean state;

    toggle = gtk_action_group_get_action(win->ag, property);
    g_object_get(win->conn->gtk_session, property, &state, NULL);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);
}

static void menu_cb_toolbar(GtkToggleAction *action, gpointer data)
{
    SpiceWindow *win = data;
    gboolean state = gtk_toggle_action_get_active(action);

    gtk_widget_set_visible(win->toolbar, state);
    g_key_file_set_boolean(keyfile, "ui", "toolbar", state);
}

static void menu_cb_statusbar(GtkToggleAction *action, gpointer data)
{
    SpiceWindow *win = data;
    gboolean state = gtk_toggle_action_get_active(action);

    gtk_widget_set_visible(win->statusbar, state);
    g_key_file_set_boolean(keyfile, "ui", "statusbar", state);
}

static void menu_cb_about(GtkAction *action, void *data)
{
    char *comments = "Based on spicy,\nSPICE remote desktop protocol";
    static const char *program_name = "flexVDI Client";
    static const char *copyright = "(c) 2010 Red Hat, (c) 2014-2016 flexVDI";
    static const char *website = "http://www.flexvdi.com";
    static const char *authors[] = { "Javier Celaya <javier.celaya@flexvdi.com>",
                                     "Sergio L. Pascual <sergiolpascual@flexvdi.com>",
                                     "Gerd Hoffmann <kraxel@redhat.com>",
                                     "Marc-André Lureau <marcandre.lureau@redhat.com>",
                                     NULL };
    SpiceWindow *win = data;

    gtk_show_about_dialog(GTK_WINDOW(win->toplevel),
                          "program-name",    program_name,
                          "authors",         authors,
                          "comments",        comments,
                          "copyright",       copyright,
                          "logo-icon-name",  GTK_STOCK_ABOUT,
                          "website",         website,
                          "website-label",   website,
                          "version",         PACKAGE_VERSION,
                          "license",         "LGPLv2.1",
                          NULL);
}

static void power_event_cb(GtkAction *action, void *data)
{
    SpiceWindow *win = data;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win->toplevel),
                                               GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                               GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
                                               "WARNING: Are you sure you want to perform a %s action?",
                                               gtk_action_get_name(action));

    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (result == GTK_RESPONSE_YES) {
        if (!strcmp(gtk_action_get_name(action), "Reset")) {
            spice_main_power_event_request(win->conn->main, SPICE_POWER_EVENT_RESET);
        } else if (!strcmp(gtk_action_get_name(action), "Powerdown")) {
            spice_main_power_event_request(win->conn->main, SPICE_POWER_EVENT_POWERDOWN);
        } else if (!strcmp(gtk_action_get_name(action), "Shutdown")) {
            spice_main_power_event_request(win->conn->main, SPICE_POWER_EVENT_SHUTDOWN);
        }
    }
}

static gboolean delete_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    SpiceWindow *win = data;

    if (win->monitor_id == 0)
        connection_disconnect(win->conn, DISCONNECT_NO_ERROR);
    else
        del_window(win->conn, win);

    return true;
}

static void switch_sub_menu(SpiceWindow *win, const char *src, const char *dst)
{
    GtkWidget *menu_item, *sub_menu;
    menu_item = gtk_ui_manager_get_widget(win->ui, src);
    sub_menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(menu_item));
    g_object_ref(sub_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), NULL);
    menu_item = gtk_ui_manager_get_widget(win->ui, dst);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), sub_menu);
    g_object_unref(sub_menu);
}

static gboolean window_state_cb(GtkWidget *widget, GdkEventWindowState *event,
                                gpointer data)
{
    SpiceWindow *win = data;
    if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
        win->fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;
        if (win->fullscreen) {
            gtk_widget_hide(win->menubar);
            gtk_widget_hide(win->toolbar);
            gtk_widget_hide(win->statusbar);
            gtk_widget_grab_focus(win->spice);
            switch_sub_menu(win, "/MainMenu/InputMenu", "/FullscreenMenu/InputMenu");
            switch_sub_menu(win, "/MainMenu/OptionMenu", "/FullscreenMenu/OptionMenu");
#if defined(WITH_FLEXVDI) && defined(ENABLE_PRINTING)
            switch_sub_menu(win, "/MainMenu/SharePrinterMenu", "/FullscreenMenu/SharePrinterMenu");
#endif
        } else {
            gboolean state;
            GtkAction *toggle;

            gtk_widget_show(win->menubar);
            toggle = gtk_action_group_get_action(win->ag, "Toolbar");
            state = gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(toggle));
            gtk_widget_set_visible(win->toolbar, state);
            toggle = gtk_action_group_get_action(win->ag, "Statusbar");
            state = gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(toggle));
            gtk_widget_set_visible(win->statusbar, state);
            gtk_widget_hide(win->fullscreen_menubar);
            switch_sub_menu(win, "/FullscreenMenu/InputMenu", "/MainMenu/InputMenu");
            switch_sub_menu(win, "/FullscreenMenu/OptionMenu", "/MainMenu/OptionMenu");
#if defined(WITH_FLEXVDI) && defined(ENABLE_PRINTING)
            switch_sub_menu(win, "/FullscreenMenu/SharePrinterMenu", "/MainMenu/SharePrinterMenu");
#endif
        }
    }
    return TRUE;
}

static void grab_keys_pressed_cb(GtkWidget *widget, gpointer data)
{
    SpiceWindow *win = data;

    if (kiosk_mode) {
        connection_disconnect(win->conn, DISCONNECT_NO_ERROR);
    }
}

static void mouse_grab_cb(GtkWidget *widget, gint grabbed, gpointer data)
{
    SpiceWindow *win = data;

    win->mouse_grabbed = grabbed;
    update_status(win->conn);
}

static void keyboard_grab_cb(GtkWidget *widget, gint grabbed, gpointer data)
{
    SpiceWindow *win = data;
    GtkSettings *settings = gtk_widget_get_settings (widget);

    if (grabbed) {
        /* disable mnemonics & accels */
        g_object_get(settings,
                     "gtk-enable-accels", &win->enable_accels_save,
                     "gtk-enable-mnemonics", &win->enable_mnemonics_save,
                     NULL);
        g_object_set(settings,
                     "gtk-enable-accels", FALSE,
                     "gtk-enable-mnemonics", FALSE,
                     NULL);
    } else {
        g_object_set(settings,
                     "gtk-enable-accels", win->enable_accels_save,
                     "gtk-enable-mnemonics", win->enable_mnemonics_save,
                     NULL);
    }
}

static void restore_configuration(SpiceWindow *win)
{
    gboolean state;
    gchar *str;
    gchar **keys = NULL;
    gsize nkeys, i;
    GError *error = NULL;
    gpointer object;

    keys = g_key_file_get_keys(keyfile, "general", &nkeys, &error);
    if (error != NULL) {
        if (error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
            g_warning("Failed to read configuration file keys: %s", error->message);
        g_clear_error(&error);
        return;
    }

    if (nkeys > 0)
        g_return_if_fail(keys != NULL);

    for (i = 0; i < nkeys; ++i) {
        if (g_str_equal(keys[i], "grab-sequence"))
            continue;
        state = g_key_file_get_boolean(keyfile, "general", keys[i], &error);
        if (error != NULL) {
            g_clear_error(&error);
            continue;
        }

        if (is_gtk_session_property(keys[i])) {
            object = win->conn->gtk_session;
        } else {
            object = win->spice;
        }
        g_object_set(object, keys[i], state, NULL);
    }

    g_strfreev(keys);

    str = g_key_file_get_string(keyfile, "general", "grab-sequence", &error);
    if (error == NULL) {
        SpiceGrabSequence *seq = spice_grab_sequence_new_from_string(str);
        spice_display_set_grab_keys(SPICE_DISPLAY(win->spice), seq);
        spice_grab_sequence_free(seq);
        g_free(str);
    }
    g_clear_error(&error);


    state = g_key_file_get_boolean(keyfile, "ui", "toolbar", &error);
    if (error == NULL)
        gtk_widget_set_visible(win->toolbar, state);
    g_clear_error(&error);

    state = g_key_file_get_boolean(keyfile, "ui", "statusbar", &error);
    if (error == NULL)
        gtk_widget_set_visible(win->statusbar, state);
    g_clear_error(&error);
}

/* ------------------------------------------------------------------ */

static const GtkActionEntry entries[] = {
    {
        .name        = "FileMenu",
        .label       = "_File",
    },{
        .name        = "FileRecentMenu",
        .label       = "_Recent",
    },{
        .name        = "EditMenu",
        .label       = "_Edit",
    },{
        .name        = "ViewMenu",
        .label       = "_View",
    },{
        .name        = "InputMenu",
        .label       = "_Input",
    },{
        .name        = "SharePrinterMenu",
        .label       = "_Printers",
    },{
        .name        = "PowerMenu",
        .label       = "_Power",
    },{
        .name        = "OptionMenu",
        .label       = "_Options",
    },{
        .name        = "HelpMenu",
        .label       = "_Help",
    },{

        /* File menu */
        .name        = "Connect",
        .stock_id    = GTK_STOCK_CONNECT,
        .label       = "_Connect ...",
        .callback    = G_CALLBACK(menu_cb_connect),
    },{
        .name        = "Close",
        .stock_id    = GTK_STOCK_CLOSE,
        .label       = "_Close",
        .callback    = G_CALLBACK(menu_cb_close),
        .accelerator = "", /* none (disable default "<control>W") */
        .tooltip     = "Exit the client",
    },{

        /* Edit menu */
        .name        = "CopyToGuest",
        .stock_id    = GTK_STOCK_COPY,
        .label       = "_Copy to guest",
        .callback    = G_CALLBACK(menu_cb_copy),
        .accelerator = "", /* none (disable default "<control>C") */
        .tooltip     = "Copy to guest",
    },{
        .name        = "PasteFromGuest",
        .stock_id    = GTK_STOCK_PASTE,
        .label       = "_Paste from guest",
        .callback    = G_CALLBACK(menu_cb_paste),
        .accelerator = "", /* none (disable default "<control>V") */
        .tooltip     = "Paste from guest",
    },{

        /* View menu */
        .name        = "Fullscreen",
        .stock_id    = GTK_STOCK_FULLSCREEN,
        .label       = "_Fullscreen",
        .callback    = G_CALLBACK(menu_cb_fullscreen),
        .accelerator = "<shift>F11",
        .tooltip     = "Go fullscreen",
    },{
        .name        = "Restore",
        .stock_id    = "view-restore",
        .label       = "_Restore",
        .callback    = G_CALLBACK(menu_cb_fullscreen),
        .accelerator = "<shift>F11",
        .tooltip     = "Leave fullscreen",
    },{
        .name        = "Minimize",
        .stock_id    = "go-bottom",
        .label       = "_Minimize",
        .callback    = G_CALLBACK(menu_cb_minimize),
        .tooltip     = "Minimize",
    },{
#ifdef USE_SMARTCARD
	.name        = "InsertSmartcard",
	.label       = "_Insert Smartcard",
	.callback    = G_CALLBACK(menu_cb_insert_smartcard),
        .accelerator = "<shift>F8",
    },{
	.name        = "RemoveSmartcard",
	.label       = "_Remove Smartcard",
	.callback    = G_CALLBACK(menu_cb_remove_smartcard),
        .accelerator = "<shift>F9",
    },{
#endif

#ifdef USE_USBREDIR
        .name        = "SelectUsbDevices",
        .label       = "_Select USB Devices for redirection",
        .callback    = G_CALLBACK(menu_cb_select_usb_devices),
        .accelerator = "<shift>F10",
    },{
#endif
        .name        = "CAD",
        .label       = "Ctrl+Alt+Supr",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{

#ifdef G_OS_WIN32
        .name        = "WCR",
        .label       = "Ctrl+Win+Right",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "WCL",
        .label       = "Ctrl+Win+Left",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "WL",
        .label       = "Win+L",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
#else
        .name        = "CAF1",
        .label       = "Ctrl+Alt+F1",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF2",
        .label       = "Ctrl+Alt+F2",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF3",
        .label       = "Ctrl+Alt+F3",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF4",
        .label       = "Ctrl+Alt+F4",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF5",
        .label       = "Ctrl+Alt+F5",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF6",
        .label       = "Ctrl+Alt+F6",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF7",
        .label       = "Ctrl+Alt+F7",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF8",
        .label       = "Ctrl+Alt+F8",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF9",
        .label       = "Ctrl+Alt+F9",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF10",
        .label       = "Ctrl+Alt+F10",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF11",
        .label       = "Ctrl+Alt+F11",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
        .name        = "CAF12",
        .label       = "Ctrl+Alt+F12",
        .callback    = G_CALLBACK(send_keystroke_cb),
    },{
#endif

        /* Power events */
        .name        = "Reset",
        .stock_id    = "system-reboot",
        .label       = "_Reset",
        .callback    = G_CALLBACK(power_event_cb),
        .tooltip     = "Reset immediately",
    },{
        .name        = "Powerdown",
        .stock_id    = "system-log-out",
        .label       = "_Powerdown cycle",
        .callback    = G_CALLBACK(power_event_cb),
        .tooltip     = "Powerdown cycle",
    },{
        .name        = "Shutdown",
        .stock_id    = "system-shutdown",
        .label       = "_Shutdown immediately",
        .callback    = G_CALLBACK(power_event_cb),
        .tooltip     = "Shutdown immediately",
    },{

        /* Help menu */
        .name        = "About",
        .stock_id    = GTK_STOCK_ABOUT,
        .label       = "_About ...",
        .callback    = G_CALLBACK(menu_cb_about),
    }
};

static const char *spice_display_properties[] = {
    "grab-keyboard",
    "grab-mouse",
    "resize-guest",
    "scaling",
    "disable-inputs",
};

static const char *spice_gtk_session_properties[] = {
    "auto-clipboard",
    "auto-usbredir",
};

static const GtkToggleActionEntry tentries[] = {
    {
        .name        = "grab-keyboard",
        .label       = "Grab keyboard when active and focused",
        .callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
        .name        = "grab-mouse",
        .label       = "Grab mouse in server mode (no tabled/vdagent)",
        .callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
        .name        = "resize-guest",
        .label       = "Resize guest to match window size",
        .callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
        .name        = "scaling",
        .label       = "Scale display",
        .callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
        .name        = "disable-inputs",
        .label       = "Disable inputs",
        .callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
        .name        = "auto-clipboard",
        .label       = "Automagic clipboard sharing between host and guest",
        .callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
        .name        = "auto-usbredir",
        .label       = "Auto redirect newly plugged in USB devices",
        .callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
        .name        = "Statusbar",
        .label       = "Statusbar",
        .callback    = G_CALLBACK(menu_cb_statusbar),
    },{
        .name        = "Toolbar",
        .label       = "Toolbar",
        .callback    = G_CALLBACK(menu_cb_toolbar),
    }
};

static char ui_xml[] =
"<ui>\n"
"  <menubar action='MainMenu'>\n"
"    <menu action='FileMenu'>\n"
"      <menuitem action='Connect'/>\n"
"      <menu action='FileRecentMenu'/>\n"
"      <separator/>\n"
"      <menuitem action='Close'/>\n"
"    </menu>\n"
"    <menu action='EditMenu'>\n"
"      <menuitem action='CopyToGuest'/>\n"
"      <menuitem action='PasteFromGuest'/>\n"
"    </menu>\n"
"    <menu action='ViewMenu'>\n"
"      <menuitem action='Fullscreen'/>\n"
"      <menuitem action='Toolbar'/>\n"
"      <menuitem action='Statusbar'/>\n"
"    </menu>\n"
"    <menu action='InputMenu'>\n"
#ifdef USE_SMARTCARD
"      <menuitem action='InsertSmartcard'/>\n"
"      <menuitem action='RemoveSmartcard'/>\n"
#endif
#ifdef USE_USBREDIR
"      <menuitem action='SelectUsbDevices'/>\n"
#endif
"      <menuitem action='CAD'/>\n"
#ifdef G_OS_WIN32
"      <menuitem action='WCR'/>\n"
"      <menuitem action='WCL'/>\n"
"      <menuitem action='WL'/>\n"
#else
"      <menuitem action='CAF1'/>\n"
"      <menuitem action='CAF2'/>\n"
"      <menuitem action='CAF3'/>\n"
"      <menuitem action='CAF4'/>\n"
"      <menuitem action='CAF5'/>\n"
"      <menuitem action='CAF6'/>\n"
"      <menuitem action='CAF7'/>\n"
"      <menuitem action='CAF8'/>\n"
"      <menuitem action='CAF9'/>\n"
"      <menuitem action='CAF10'/>\n"
"      <menuitem action='CAF11'/>\n"
"      <menuitem action='CAF12'/>\n"
#endif
"    </menu>\n"
"    <menu action='PowerMenu'>\n"
"      <menuitem action='Reset'/>\n"
"      <menuitem action='Powerdown'/>\n"
"      <menuitem action='Shutdown'/>\n"
"    </menu>\n"
#if defined(WITH_FLEXVDI) && defined(ENABLE_PRINTING)
"    <menu action='SharePrinterMenu'/>\n"
#endif
"    <menu action='OptionMenu'>\n"
"      <menuitem action='grab-keyboard'/>\n"
"      <menuitem action='grab-mouse'/>\n"
"      <menuitem action='resize-guest'/>\n"
"      <menuitem action='scaling'/>\n"
"      <menuitem action='disable-inputs'/>\n"
"      <menuitem action='auto-clipboard'/>\n"
"      <menuitem action='auto-usbredir'/>\n"
"    </menu>\n"
"    <menu action='HelpMenu'>\n"
"      <menuitem action='About'/>\n"
"    </menu>\n"
"  </menubar>\n"
"  <toolbar action='ToolBar'>\n"
"    <toolitem action='Close'/>\n"
"    <separator/>\n"
"    <toolitem action='CopyToGuest'/>\n"
"    <toolitem action='PasteFromGuest'/>\n"
"    <separator/>\n"
"    <toolitem action='Fullscreen'/>\n"
"    <separator/>\n"
"    <toolitem action='Reset'/>\n"
"    <toolitem action='Powerdown'/>\n"
"    <toolitem action='Shutdown'/>\n"
"  </toolbar>\n"
"  <toolbar action='KioskBar'>\n"
"    <toolitem action='Close'/>\n"
"    <separator/>\n"
"    <toolitem action='CopyToGuest'/>\n"
"    <toolitem action='PasteFromGuest'/>\n"
"  </toolbar>\n"
"  <toolbar action='FullscreenBar'>\n"
"    <toolitem action='Close'/>\n"
"    <separator/>\n"
"    <toolitem action='CopyToGuest'/>\n"
"    <toolitem action='PasteFromGuest'/>\n"
"    <separator/>\n"
"    <toolitem action='Restore'/>\n"
"    <toolitem action='Minimize'/>\n"
"    <toolitem action='Reset'/>\n"
"    <toolitem action='Powerdown'/>\n"
"    <toolitem action='Shutdown'/>\n"
"  </toolbar>\n"
"  <menubar action='FullscreenMenu'>\n"
"    <menu action='InputMenu' />\n"
#if defined(WITH_FLEXVDI) && defined(ENABLE_PRINTING)
"    <menu action='SharePrinterMenu'/>\n"
#endif
"    <menu action='OptionMenu' />\n"
"  </menubar>\n"
"</ui>\n";

static gboolean is_gtk_session_property(const gchar *property)
{
    int i;

    for (i = 0; i < G_N_ELEMENTS(spice_gtk_session_properties); i++) {
        if (!strcmp(spice_gtk_session_properties[i], property)) {
            return TRUE;
        }
    }
    return FALSE;
}

static void recent_item_activated_cb(GtkRecentChooser *chooser, gpointer data)
{
    GtkRecentInfo *info;
    struct spice_connection *conn;
    const char *uri;

    info = gtk_recent_chooser_get_current_item(chooser);

    uri = gtk_recent_info_get_uri(info);
    g_return_if_fail(uri != NULL);

    conn = connection_new();
    g_object_set(conn->session, "uri", uri, NULL);
    gtk_recent_info_unref(info);
    connection_connect(conn);
}

static gboolean configure_event_cb(GtkWidget         *widget,
                                   GdkEventConfigure *event,
                                   gpointer           data)
{
    gboolean resize_guest;
    SpiceWindow *win = data;

    g_return_val_if_fail(win != NULL, FALSE);
    g_return_val_if_fail(win->conn != NULL, FALSE);

    g_object_get(win->spice, "resize-guest", &resize_guest, NULL);
    if (resize_guest && win->conn->agent_connected)
        return FALSE;

    return FALSE;
}

#if GTK_CHECK_VERSION(3,10,0)
static gboolean hide_widget_cb(gpointer data) {
    GtkWidget *widget = data;
    if (!gtk_revealer_get_reveal_child(GTK_REVEALER(widget)))
        gtk_widget_hide(widget);
    return FALSE;
}

static gboolean motion_notify_event_cb(GtkWidget * widget, GdkEventMotion * event,
                                       gpointer data) {
    SpiceWindow *win = data;
    GtkRevealer *revealer = GTK_REVEALER(win->fullscreen_menubar);
    if (win->fullscreen) {
        if (event->y == 0.0) {
            gtk_widget_show(win->fullscreen_menubar);
            gtk_revealer_set_reveal_child(revealer, TRUE);
        } else if (gtk_revealer_get_reveal_child(revealer)) {
            gtk_revealer_set_reveal_child(revealer, FALSE);
            g_timeout_add(gtk_revealer_get_transition_duration(revealer),
                          hide_widget_cb, revealer);
        }
    }
    return FALSE;
}
#else
static gboolean motion_notify_event_cb(GtkWidget *widget, GdkEventMotion *event,
                                       gpointer data) {
    SpiceWindow *win = data;
    if (win->fullscreen) {
        if (event->y <= 0.0) {
            gtk_widget_show(win->fullscreen_menubar);
#if !GTK_CHECK_VERSION(3,2,0)
            GtkRequisition size;
            gint window_width;
            GtkWidget *fixed = gtk_widget_get_parent(win->fullscreen_menubar);
            gtk_window_get_size(GTK_WINDOW(win->toplevel), &window_width, NULL);
            gtk_widget_size_request(win->fullscreen_menubar, &size);
            int x = (window_width - size.width) / 2;
            gtk_fixed_move(GTK_FIXED(fixed), win->fullscreen_menubar, x, 0);
#endif
        } else {
            gtk_widget_hide(win->fullscreen_menubar);
        }
    }
    return FALSE;
}
#endif

static gboolean leave_window_cb(GtkWidget *widget, GdkEventCrossing *event,
                                gpointer data) {
    GdkEventMotion mevent = {
        .y = event->y
    };
    return motion_notify_event_cb(widget, &mevent, data);
}

static void
spice_window_class_init (SpiceWindowClass *klass)
{
}

static void
spice_window_init (SpiceWindow *self)
{
}

#if defined(WITH_FLEXVDI) && defined(ENABLE_PRINTING)
void printer_toggled(GtkWidget *widget, gpointer win)
{
    const char * printer = gtk_menu_item_get_label(GTK_MENU_ITEM(widget));
    gboolean active = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
    g_key_file_set_boolean(keyfile, "printers", printer, active);
    if (active) {
        flexvdi_share_printer(printer);
    } else {
        flexvdi_unshare_printer(printer);
    }
}

static GtkWidget * get_printers_menu(SpiceWindow *win)
{
    GtkWidget * sharePrinterMenu = gtk_menu_new();
    GSList * printers, * printer;
    flexvdi_get_printer_list(&printers);
    for (printer = printers; printer != NULL; printer = g_slist_next(printer)) {
        const char * printerName = (const char *)printer->data;
        gboolean state = g_key_file_get_boolean(keyfile, "printers", printerName, NULL);
        GtkWidget * item = gtk_check_menu_item_new_with_label(printerName);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), state);
        gtk_menu_shell_append(GTK_MENU_SHELL(sharePrinterMenu), item);
        gtk_widget_set_visible(item, TRUE);
        g_signal_connect(G_OBJECT(item), "activate",
                         G_CALLBACK(printer_toggled), win);
    }
    g_slist_free_full(printers, g_free);
    return sharePrinterMenu;
}

static void share_current_printers(gpointer data)
{
    SpiceWindow *win = data;
    GtkWidget *mainSPMenu = gtk_ui_manager_get_widget(win->ui, "/MainMenu/SharePrinterMenu");
    GtkWidget *fullSPMenu = gtk_ui_manager_get_widget(win->ui, "/FullscreenMenu/SharePrinterMenu");
    if (!flexvdi_agent_supports_capability(FLEXVDI_CAP_PRINTING)) {
        gtk_widget_hide(mainSPMenu);
        gtk_widget_hide(fullSPMenu);
    } else {
        gtk_widget_show(mainSPMenu);
        gtk_widget_show(fullSPMenu);
        GtkWidget *subMenu = win->fullscreen ?
            gtk_menu_item_get_submenu(GTK_MENU_ITEM(fullSPMenu)) :
            gtk_menu_item_get_submenu(GTK_MENU_ITEM(mainSPMenu));
        GList *children = gtk_container_get_children(GTK_CONTAINER(subMenu)), *child;
        for (child = children; child != NULL; child = g_list_next(child)) {
            if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(child->data))) {
                const char * printer = gtk_menu_item_get_label(GTK_MENU_ITEM(child->data));
                flexvdi_share_printer(printer);
            }
        }
    }
}

#endif

void realize_window(GtkWidget * toplevel, gpointer data)
{
    if (fullscreen_on_all_monitors) {
        gdk_window_set_fullscreen_mode(gtk_widget_get_window(toplevel),
                                       GDK_FULLSCREEN_ON_ALL_MONITORS);
        fullscreen = TRUE;
    }
    if (fullscreen || kiosk_mode)
        gtk_window_fullscreen(GTK_WINDOW(toplevel));
}

static SpiceWindow *create_spice_window(spice_connection *conn, SpiceChannel *channel, int id, gint monitor_id)
{
    char title[32];
    SpiceWindow *win;
    GtkAction *toggle;
    gboolean state;
    GtkWidget *vbox, *hbox, *frame, *overlay, *fsBar, *fsMenu;
    GError *err = NULL;
    int i;
    SpiceGrabSequence *seq;

    win = g_object_new(SPICE_TYPE_WINDOW, NULL);
    win->id = id;
    win->monitor_id = monitor_id;
    win->conn = conn;
    win->display_channel = channel;

    /* toplevel */
    win->toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    if (spicy_title == NULL) {
        snprintf(title, sizeof(title), "spice display %d:%d", id, monitor_id);
    } else {
        snprintf(title, sizeof(title), "%s", spicy_title);
    }

    gtk_window_set_title(GTK_WINDOW(win->toplevel), title);
    g_signal_connect(G_OBJECT(win->toplevel), "window-state-event",
                     G_CALLBACK(window_state_cb), win);
    g_signal_connect(G_OBJECT(win->toplevel), "delete-event",
                     G_CALLBACK(delete_cb), win);

#ifdef WITH_FLEXVDI
    gtk_window_set_icon(GTK_WINDOW(win->toplevel),
                        gdk_pixbuf_new_from_inline(-1, flexvdi_icon_data, FALSE, NULL));
#endif

    /* menu + toolbar */
    win->ui = gtk_ui_manager_new();
    win->ag = gtk_action_group_new("MenuActions");
    gtk_action_group_add_actions(win->ag, entries, G_N_ELEMENTS(entries), win);
    gtk_action_group_add_toggle_actions(win->ag, tentries,
                                        G_N_ELEMENTS(tentries), win);
    gtk_ui_manager_insert_action_group(win->ui, win->ag, 0);
    gtk_window_add_accel_group(GTK_WINDOW(win->toplevel),
                               gtk_ui_manager_get_accel_group(win->ui));

    err = NULL;
    if (!gtk_ui_manager_add_ui_from_string(win->ui, ui_xml, -1, &err)) {
        g_warning("building menus failed: %s", err->message);
        g_error_free(err);
        exit(1);
    }
    win->menubar = gtk_ui_manager_get_widget(win->ui, "/MainMenu");
    win->toolbar = gtk_ui_manager_get_widget(win->ui, "/ToolBar");
#if GTK_CHECK_VERSION(3,10,0)
    win->fullscreen_menubar = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(win->fullscreen_menubar),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
#else
    win->fullscreen_menubar = gtk_hbox_new(FALSE, 0);
    hbox = gtk_hbox_new(FALSE, 1);
#endif
    GtkWidget * event_box = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(win->fullscreen_menubar), event_box);
    gtk_container_add(GTK_CONTAINER(event_box), hbox);
    GtkStyle *default_style = gtk_widget_get_default_style();
    gtk_widget_modify_bg(event_box, GTK_STATE_NORMAL, &default_style->bg[GTK_STATE_NORMAL]);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 0);
    gtk_box_set_spacing(GTK_BOX(hbox), 0);
    fsBar = kiosk_mode ?
                gtk_ui_manager_get_widget(win->ui, "/KioskBar") :
                gtk_ui_manager_get_widget(win->ui, "/FullscreenBar");
    g_object_set(fsBar, "show-arrow", FALSE, NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(fsBar), gtk_separator_tool_item_new(), -1);
    fsMenu = gtk_ui_manager_get_widget(win->ui, "/FullscreenMenu");
    gtk_box_pack_start(GTK_BOX(hbox), fsBar, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(hbox), fsMenu, FALSE, FALSE, 0);

    /* recent menu */
    win->ritem  = gtk_ui_manager_get_widget
        (win->ui, "/MainMenu/FileMenu/FileRecentMenu");

    GtkRecentFilter  *rfilter;

    win->rmenu = gtk_recent_chooser_menu_new();
    gtk_recent_chooser_set_show_icons(GTK_RECENT_CHOOSER(win->rmenu), FALSE);
    rfilter = gtk_recent_filter_new();
    gtk_recent_filter_add_mime_type(rfilter, "application/x-spice");
    gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(win->rmenu), rfilter);
    gtk_recent_chooser_set_local_only(GTK_RECENT_CHOOSER(win->rmenu), FALSE);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(win->ritem), win->rmenu);
    g_signal_connect(win->rmenu, "item-activated",
                     G_CALLBACK(recent_item_activated_cb), win);

    /* spice display */
    win->spice = GTK_WIDGET(spice_display_new_with_monitor(conn->session, id, monitor_id));
    g_signal_connect(win->spice, "configure-event", G_CALLBACK(configure_event_cb), win);
    seq = spice_grab_sequence_new_from_string("Shift_L+F12");
    spice_display_set_grab_keys(SPICE_DISPLAY(win->spice), seq);
    spice_grab_sequence_free(seq);

    g_signal_connect(G_OBJECT(win->spice), "mouse-grab",
                     G_CALLBACK(mouse_grab_cb), win);
    g_signal_connect(G_OBJECT(win->spice), "keyboard-grab",
                     G_CALLBACK(keyboard_grab_cb), win);
    g_signal_connect(G_OBJECT(win->spice), "grab-keys-pressed",
                     G_CALLBACK(grab_keys_pressed_cb), win);
    g_signal_connect(G_OBJECT(win->spice), "motion-notify-event",
                     G_CALLBACK(motion_notify_event_cb), win);
    g_signal_connect(G_OBJECT(win->spice), "leave-notify-event",
                     G_CALLBACK(leave_window_cb), win);

    /* status line */
#if GTK_CHECK_VERSION(3,0,0)
    win->statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
#else
    win->statusbar = gtk_hbox_new(FALSE, 1);
#endif

    win->status = gtk_label_new("status line");
    gtk_misc_set_alignment(GTK_MISC(win->status), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(win->status), 3, 1);
    update_status_window(win);

    frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(win->statusbar), frame, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame), win->status);

    for (i = 0; i < STATE_MAX; i++) {
        win->st[i] = gtk_label_new("?");
        gtk_label_set_width_chars(GTK_LABEL(win->st[i]), 5);
        frame = gtk_frame_new(NULL);
        gtk_box_pack_end(GTK_BOX(win->statusbar), frame, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(frame), win->st[i]);
    }

    /* Make a vbox and put stuff in */
#if GTK_CHECK_VERSION(3,0,0)
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
#else
    vbox = gtk_vbox_new(FALSE, 1);
#endif
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);
    gtk_box_pack_start(GTK_BOX(vbox), win->menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), win->toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), win->spice, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), win->statusbar, FALSE, TRUE, 0);

#if GTK_CHECK_VERSION(3,2,0)
    overlay = gtk_overlay_new();
    g_object_set(win->fullscreen_menubar, "valign", GTK_ALIGN_START, NULL);
    g_object_set(win->fullscreen_menubar, "halign", GTK_ALIGN_CENTER, NULL);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), win->fullscreen_menubar);
    gtk_container_add(GTK_CONTAINER(overlay), vbox);
#else
    overlay = gtk_vbox_new(FALSE, 0);
    GtkWidget *fixed = gtk_fixed_new();
    gtk_box_pack_start(GTK_BOX(overlay), fixed, FALSE, FALSE, 0);
    gtk_fixed_put(GTK_FIXED(fixed), win->fullscreen_menubar, 0, 0);
    gtk_widget_set_size_request(fixed, -1, 0);
    gtk_box_pack_start(GTK_BOX(overlay), vbox, TRUE, TRUE, 0);
#endif
    gtk_container_add(GTK_CONTAINER(win->toplevel), overlay);

    /* show window */
    g_signal_connect(win->toplevel, "realize", (GCallback)realize_window, NULL);

    gtk_widget_show_all(overlay);
    gtk_widget_hide(win->fullscreen_menubar);
    restore_configuration(win);

    /* init toggle actions */
    for (i = 0; i < G_N_ELEMENTS(spice_display_properties); i++) {
        toggle = gtk_action_group_get_action(win->ag,
                                             spice_display_properties[i]);
        g_object_get(win->spice, spice_display_properties[i], &state, NULL);
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);
    }

    for (i = 0; i < G_N_ELEMENTS(spice_gtk_session_properties); i++) {
        char notify[64];

        toggle = gtk_action_group_get_action(win->ag,
                                             spice_gtk_session_properties[i]);
        g_object_get(win->conn->gtk_session, spice_gtk_session_properties[i],
                     &state, NULL);
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);

        snprintf(notify, sizeof(notify), "notify::%s",
                 spice_gtk_session_properties[i]);
        spice_g_signal_connect_object(win->conn->gtk_session, notify,
                                      G_CALLBACK(menu_cb_conn_bool_prop_changed),
                                      win, 0);
    }

    if (disable_power_events) {
        gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/MainMenu/PowerMenu"));
        gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/ToolBar/Reset"));
        gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/FullscreenBar/Reset"));
        gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/ToolBar/Powerdown"));
        gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/FullscreenBar/Powerdown"));
        gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/ToolBar/Shutdown"));
        gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/FullscreenBar/Shutdown"));
    }

    update_edit_menu_window(win);

    toggle = gtk_action_group_get_action(win->ag, "Toolbar");
    state = gtk_widget_get_visible(win->toolbar);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);

    toggle = gtk_action_group_get_action(win->ag, "Statusbar");
    state = gtk_widget_get_visible(win->statusbar);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);

#ifdef USE_SMARTCARD
    gboolean smartcard;

    enable_smartcard_actions(win, NULL, FALSE, FALSE);
    g_object_get(G_OBJECT(conn->session),
                 "enable-smartcard", &smartcard,
                 NULL);
    if (smartcard) {
        g_signal_connect(G_OBJECT(spice_smartcard_manager_get()), "reader-added",
                         (GCallback)reader_added_cb, win);
        g_signal_connect(G_OBJECT(spice_smartcard_manager_get()), "reader-removed",
                         (GCallback)reader_removed_cb, win);
        g_signal_connect(G_OBJECT(spice_smartcard_manager_get()), "card-inserted",
                         (GCallback)card_inserted_cb, win);
        g_signal_connect(G_OBJECT(spice_smartcard_manager_get()), "card-removed",
                         (GCallback)card_removed_cb, win);
    }
#endif

#ifndef USE_USBREDIR
    GtkAction *usbredir = gtk_action_group_get_action(win->ag, "auto-usbredir");
    gtk_action_set_visible(usbredir, FALSE);
#endif

    gtk_widget_grab_focus(win->spice);

#if defined(WITH_FLEXVDI) && defined(ENABLE_PRINTING)
    // Printers menu
    GtkWidget * sPMenu = gtk_ui_manager_get_widget(win->ui, "/MainMenu/SharePrinterMenu");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(sPMenu), get_printers_menu(win));
    gtk_widget_hide(sPMenu);
    gtk_widget_hide(gtk_ui_manager_get_widget(win->ui, "/FullscreenMenu/SharePrinterMenu"));
    if (flexvdi_is_agent_connected())
        share_current_printers(win);
    else
        flexvdi_on_agent_connected(share_current_printers, win);
#endif

    return win;
}

static void destroy_spice_window(SpiceWindow *win)
{
    if (win == NULL)
        return;

    SPICE_DEBUG("destroy window (#%d:%d)", win->id, win->monitor_id);
    g_object_unref(win->ag);
    g_object_unref(win->ui);
    gtk_widget_destroy(win->toplevel);
    g_object_unref(win);
}

/* ------------------------------------------------------------------ */

static void recent_add(SpiceSession *session)
{
    GtkRecentManager *recent;
    GtkRecentData meta = {
        .mime_type    = (char*)"application/x-spice",
        .app_name     = (char*)"spicy",
        .app_exec     = (char*)"spicy --uri=%u",
    };
    char *uri;

    g_object_get(session, "uri", &uri, NULL);
    SPICE_DEBUG("%s: %s", __FUNCTION__, uri);

    recent = gtk_recent_manager_get_default();
    if (g_str_has_prefix(uri, "spice://"))
        meta.display_name = uri + 8;
    else if (g_str_has_prefix(uri, "spice+unix://"))
        meta.display_name = uri + 13;
    else
        g_return_if_reached();

    if (!gtk_recent_manager_add_full(recent, uri, &meta))
        g_warning("Recent item couldn't be added successfully");

    g_free(uri);
}

static void main_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                               gpointer data)
{
    const GError *error = NULL;
    spice_connection *conn = data;
    char password[64];
    int rc;

    switch (event) {
    case SPICE_CHANNEL_OPENED:
        g_message("main channel: opened");
        recent_add(conn->session);
        break;
    case SPICE_CHANNEL_SWITCHING:
        g_message("main channel: switching host");
        break;
    case SPICE_CHANNEL_CLOSED:
        /* this event is only sent if the channel was succesfully opened before */
        g_message("main channel: closed");
        connection_disconnect(conn, DISCONNECT_NO_ERROR);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        connection_disconnect(conn, DISCONNECT_IO_ERROR);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        error = spice_channel_get_error(channel);
        g_message("main channel: failed to connect");
        if (error) {
            g_message("channel error: %s", error->message);
        }

        connection_disconnect(conn, DISCONNECT_CONN_ERROR);
        break;
    case SPICE_CHANNEL_ERROR_AUTH:
        g_warning("main channel: auth failure (wrong password?)");
        strcpy(password, "");
        /* FIXME i18 */
        rc = ask_user(NULL, "Authentication",
                      "Please enter the spice server password",
                      password, sizeof(password), true);
        if (rc == 0) {
            g_object_set(conn->session, "password", password, NULL);
            connection_connect(conn);
        } else {
            connection_disconnect(conn, DISCONNECT_AUTH_ERROR);
        }
        break;
    default:
        /* TODO: more sophisticated error handling */
        g_warning("unknown main channel event: %d", event);
        /* connection_disconnect(conn); */
        break;
    }
}

static void main_mouse_update(SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    gint mode;

    g_object_get(channel, "mouse-mode", &mode, NULL);
    switch (mode) {
    case SPICE_MOUSE_MODE_SERVER:
        conn->mouse_state = "server";
        break;
    case SPICE_MOUSE_MODE_CLIENT:
        conn->mouse_state = "client";
        break;
    default:
        conn->mouse_state = "?";
        break;
    }
    update_status(conn);
}

static void main_agent_update(SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;

    g_object_get(channel, "agent-connected", &conn->agent_connected, NULL);
    conn->agent_state = conn->agent_connected ? "yes" : "no";
    update_status(conn);
    update_edit_menu(conn);
}

static void inputs_modifiers(SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int m, i;

    g_object_get(channel, "key-modifiers", &m, NULL);
    for (i = 0; i < SPICE_N_ELEMENTS(conn->wins); i++) {
        if (conn->wins[i] == NULL)
            continue;

        gtk_label_set_text(GTK_LABEL(conn->wins[i]->st[STATE_SCROLL_LOCK]),
                           m & SPICE_KEYBOARD_MODIFIER_FLAGS_SCROLL_LOCK ? "SCROLL" : "");
        gtk_label_set_text(GTK_LABEL(conn->wins[i]->st[STATE_CAPS_LOCK]),
                           m & SPICE_KEYBOARD_MODIFIER_FLAGS_CAPS_LOCK ? "CAPS" : "");
        gtk_label_set_text(GTK_LABEL(conn->wins[i]->st[STATE_NUM_LOCK]),
                           m & SPICE_KEYBOARD_MODIFIER_FLAGS_NUM_LOCK ? "NUM" : "");
    }
}

static void display_mark(SpiceChannel *channel, gint mark, SpiceWindow *win)
{
    g_return_if_fail(win != NULL);
    g_return_if_fail(win->toplevel != NULL);

    if (mark == TRUE) {
        gtk_widget_show(win->toplevel);
    } else {
        gtk_widget_hide(win->toplevel);
    }
}

static void update_auto_usbredir_sensitive(spice_connection *conn)
{
#ifdef USE_USBREDIR
    int i;
    GtkAction *ac;
    gboolean sensitive;

    sensitive = spice_session_has_channel_type(conn->session,
                                               SPICE_CHANNEL_USBREDIR);
    for (i = 0; i < SPICE_N_ELEMENTS(conn->wins); i++) {
        if (conn->wins[i] == NULL)
            continue;
        ac = gtk_action_group_get_action(conn->wins[i]->ag, "auto-usbredir");
        gtk_action_set_sensitive(ac, sensitive);
    }
#endif
}

static SpiceWindow* get_window(spice_connection *conn, int channel_id, int monitor_id)
{
    g_return_val_if_fail(channel_id < CHANNELID_MAX, NULL);
    g_return_val_if_fail(monitor_id < MONITORID_MAX, NULL);

    return conn->wins[channel_id * CHANNELID_MAX + monitor_id];
}

static void add_window(spice_connection *conn, SpiceWindow *win)
{
    g_return_if_fail(win != NULL);
    g_return_if_fail(win->id < CHANNELID_MAX);
    g_return_if_fail(win->monitor_id < MONITORID_MAX);
    g_return_if_fail(conn->wins[win->id * CHANNELID_MAX + win->monitor_id] == NULL);

    SPICE_DEBUG("add display monitor %d:%d", win->id, win->monitor_id);
    conn->wins[win->id * CHANNELID_MAX + win->monitor_id] = win;
}

static void del_window(spice_connection *conn, SpiceWindow *win)
{
    if (win == NULL)
        return;

    g_return_if_fail(win->id < CHANNELID_MAX);
    g_return_if_fail(win->monitor_id < MONITORID_MAX);

    g_debug("del display monitor %d:%d", win->id, win->monitor_id);
    conn->wins[win->id * CHANNELID_MAX + win->monitor_id] = NULL;
    if (win->id > 0)
        spice_main_set_display_enabled(conn->main, win->id, FALSE);
    else
        spice_main_set_display_enabled(conn->main, win->monitor_id, FALSE);
    spice_main_send_monitor_config(conn->main);

    destroy_spice_window(win);
}

static void display_monitors(SpiceChannel *display, GParamSpec *pspec,
                             spice_connection *conn)
{
    GArray *monitors = NULL;
    int id;
    guint i;

    g_object_get(display,
                 "channel-id", &id,
                 "monitors", &monitors,
                 NULL);
    g_return_if_fail(monitors != NULL);

    for (i = 0; i < monitors->len; i++) {
        SpiceWindow *w;

        if (!get_window(conn, id, i)) {
            w = create_spice_window(conn, display, id, i);
            add_window(conn, w);
            spice_g_signal_connect_object(display, "display-mark",
                                          G_CALLBACK(display_mark), w, 0);
            if (monitors->len == 1)
                gtk_window_set_position(GTK_WINDOW(w->toplevel), GTK_WIN_POS_CENTER_ALWAYS);
            gtk_widget_show(w->toplevel);
            update_auto_usbredir_sensitive(conn);
        }
    }

    for (; i < MONITORID_MAX; i++)
        del_window(conn, get_window(conn, id, i));

    g_clear_pointer(&monitors, g_array_unref);
}

static void stream_report(SpiceChannel *display, GParamSpec *pspec,
                          spice_connection *conn)
{
    g_object_get(display, "stream-report", &conn->stream_state, NULL);
    update_status(conn);
}

#ifndef WITH_FLEXVDI
static void port_write_cb(GObject *source_object,
                          GAsyncResult *res,
                          gpointer user_data)
{
    SpicePortChannel *port = SPICE_PORT_CHANNEL(source_object);
    GError *error = NULL;

    spice_port_write_finish(port, res, &error);
    if (error != NULL)
        g_warning("%s", error->message);
    g_clear_error(&error);
}

static void port_flushed_cb(GObject *source_object,
                            GAsyncResult *res,
                            gpointer user_data)
{
    SpiceChannel *channel = SPICE_CHANNEL(source_object);
    GError *error = NULL;

    spice_channel_flush_finish(channel, res, &error);
    if (error != NULL)
        g_warning("%s", error->message);
    g_clear_error(&error);

    spice_channel_disconnect(channel, SPICE_CHANNEL_CLOSED);
}

static gboolean input_cb(GIOChannel *gin, GIOCondition condition, gpointer data)
{
    char buf[4096];
    gsize bytes_read;
    GIOStatus status;

    if (!(condition & G_IO_IN))
        return FALSE;

    status = g_io_channel_read_chars(gin, buf, sizeof(buf), &bytes_read, NULL);
    if (status != G_IO_STATUS_NORMAL)
        return FALSE;

    if (stdin_port != NULL)
        spice_port_write_async(stdin_port, buf, bytes_read, NULL, port_write_cb, NULL);

    return TRUE;
}
#endif

#ifndef WITH_FLEXVDI
static void port_opened(SpiceChannel *channel, GParamSpec *pspec,
                        spice_connection *conn)
{
    SpicePortChannel *port = SPICE_PORT_CHANNEL(channel);
    gchar *name = NULL;
    gboolean opened = FALSE;

    g_object_get(channel,
                 "port-name", &name,
                 "port-opened", &opened,
                 NULL);

    g_printerr("port %p %s: %s\n", channel, name, opened ? "opened" : "closed");

    if (opened) {
        /* only send a break event and disconnect */
        if (g_strcmp0(name, "org.spice.spicy.break") == 0) {
            spice_port_event(port, SPICE_PORT_EVENT_BREAK);
            spice_channel_flush_async(channel, NULL, port_flushed_cb, conn);
        }

        /* handle the first spicy port and connect it to stdin/out */
        if (g_strcmp0(name, "org.spice.spicy") == 0 && stdin_port == NULL) {
            stdin_port = port;
        }
    } else {
        if (port == stdin_port)
            stdin_port = NULL;
    }

    g_free(name);
}

static void port_data(SpicePortChannel *port,
                      gpointer data, int size, spice_connection *conn)
{
    int r;

    if (port != stdin_port)
        return;

    r = write(fileno(stdout), data, size);
    if (r != size) {
        g_warning("port write failed result %d/%d errno %d", r, size, errno);
    }
}
#endif

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    conn->channels++;
    SPICE_DEBUG("new channel (#%d)", id);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("new main channel");
        conn->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "channel-event",
                         G_CALLBACK(main_channel_event), conn);
        g_signal_connect(channel, "main-mouse-update",
                         G_CALLBACK(main_mouse_update), conn);
        g_signal_connect(channel, "main-agent-update",
                         G_CALLBACK(main_agent_update), conn);
        main_mouse_update(channel, conn);
        main_agent_update(channel, conn);
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id >= SPICE_N_ELEMENTS(conn->wins))
            return;
        if (conn->wins[id] != NULL)
            return;
        SPICE_DEBUG("new display channel (#%d)", id);
        g_signal_connect(channel, "notify::monitors",
                         G_CALLBACK(display_monitors), conn);
        g_signal_connect(channel, "notify::stream-report",
                         G_CALLBACK(stream_report), conn);
        spice_channel_connect(channel);
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        SPICE_DEBUG("new inputs channel");
        g_signal_connect(channel, "inputs-modifiers",
                         G_CALLBACK(inputs_modifiers), conn);
    }

    if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SPICE_DEBUG("new audio channel");
        conn->audio = spice_audio_get(s, NULL);
    }

    if (SPICE_IS_USBREDIR_CHANNEL(channel)) {
        update_auto_usbredir_sensitive(conn);
    }

    if (SPICE_IS_PORT_CHANNEL(channel)) {
#ifndef WITH_FLEXVDI
        g_signal_connect(channel, "notify::port-opened",
                         G_CALLBACK(port_opened), conn);
        g_signal_connect(channel, "port-data",
                         G_CALLBACK(port_data), conn);
#endif
        spice_channel_connect(channel);
    }
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("zap main channel");
        conn->main = NULL;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id >= SPICE_N_ELEMENTS(conn->wins))
            return;
        SPICE_DEBUG("zap display channel (#%d)", id);
        /* FIXME destroy widget only */
    }

    if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SPICE_DEBUG("zap audio channel");
    }

    if (SPICE_IS_USBREDIR_CHANNEL(channel)) {
        update_auto_usbredir_sensitive(conn);
    }

#ifndef WITH_FLEXVDI
    if (SPICE_IS_PORT_CHANNEL(channel)) {
        if (SPICE_PORT_CHANNEL(channel) == stdin_port)
            stdin_port = NULL;
    }
#endif

    conn->channels--;
    if (conn->channels > 0) {
        return;
    }

    connection_destroy(conn);
}

static void migration_state(GObject *session,
                            GParamSpec *pspec, gpointer data)
{
    SpiceSessionMigration mig;

    g_object_get(session, "migration-state", &mig, NULL);
    if (mig == SPICE_SESSION_MIGRATION_SWITCHING)
        g_message("migrating session");
}

static spice_connection *connection_new(void)
{
    spice_connection *conn;
    SpiceUsbDeviceManager *manager;

    conn = g_new0(spice_connection, 1);
    conn->session = spice_session_new();
    conn->gtk_session = spice_gtk_session_get(conn->session);
    g_object_set(conn->gtk_session,
                 "disable-copy-to-guest", disable_copy_to_guest,
                 "disable-paste-from-guest", disable_paste_from_guest,
                 NULL);
    g_signal_connect(conn->session, "channel-new",
                     G_CALLBACK(channel_new), conn);
    g_signal_connect(conn->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), conn);
    g_signal_connect(conn->session, "notify::migration-state",
                     G_CALLBACK(migration_state), conn);

    manager = spice_usb_device_manager_get(conn->session, NULL);
    if (manager) {
        g_signal_connect(manager, "auto-connect-failed",
                         G_CALLBACK(usb_connect_failed), NULL);
        g_signal_connect(manager, "device-error",
                         G_CALLBACK(usb_connect_failed), NULL);
    }

    conn->stream_state = "";

    connections++;
    SPICE_DEBUG("%s (%d)", __FUNCTION__, connections);
    return conn;
}

static void connection_connect(spice_connection *conn)
{
    conn->disconnecting = false;
    spice_session_connect(conn->session);
}

static void connection_disconnect(spice_connection *conn, int reason)
{
    if (conn->disconnecting)
        return;
    conn->disconnecting = true;
    disconnect_reason = reason;
    spice_session_disconnect(conn->session);
}

static void connection_destroy(spice_connection *conn)
{
    g_object_unref(conn->session);
    free(conn);

    connections--;
    SPICE_DEBUG("%s (%d)", __FUNCTION__, connections);
    if (connections > 0) {
        return;
    }

    g_main_loop_quit(mainloop);
}

/* ------------------------------------------------------------------ */

static GOptionEntry cmd_entries[] = {
    {
        .long_name        = "full-screen",
        .short_name       = 'f',
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &fullscreen,
        .description      = "Open in full screen mode",
    },{
        .long_name        = "full-screen-on-all-monitors",
        .short_name       = 'F',
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &fullscreen_on_all_monitors,
        .description      = "Open in full screen mode on all monitors",
    },{
        .long_name        = "version",
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &version,
        .description      = "Display version and quit",
    },{
        .long_name        = "title",
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &spicy_title,
        .description      = "Set the window title",
        .arg_description  = "<title>",
    },{
        .long_name        = "kiosk-mode",
        .short_name       = 'k',
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &kiosk_mode,
        .description      = "Use kiosk mode",
    },{
        .long_name        = "disable-power-events",
        .short_name       = '\0',
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &disable_power_events,
        .description      = "Disable power events",
    },{
        .long_name        = "disable-copy-to-guest",
        .short_name       = '\0',
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &disable_copy_to_guest,
        .description      = "Disable copy to guest",
    },{
        .long_name        = "disable-paste-from-guest",
        .short_name       = '\0',
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &disable_paste_from_guest,
        .description      = "Disable paste from guest",
    },{
        /* end of list */
    }
};

static void usb_connect_failed(GObject               *object,
                               SpiceUsbDevice        *device,
                               GError                *error,
                               gpointer               data)
{
    GtkWidget *dialog;

    if (error->domain == G_IO_ERROR && error->code == G_IO_ERROR_CANCELLED)
        return;

    dialog = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_CLOSE,
                                    "USB redirection error");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                             "%s", error->message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

#ifndef WITH_FLEXVDI
static void setup_terminal(gboolean reset)
{
    int stdinfd = fileno(stdin);

    if (!isatty(stdinfd))
        return;

#ifdef HAVE_TERMIOS_H
    static struct termios saved_tios;
    struct termios tios;

    if (reset)
        tios = saved_tios;
    else {
        tcgetattr(stdinfd, &tios);
        saved_tios = tios;
        tios.c_lflag &= ~(ICANON | ECHO);
    }

    tcsetattr(stdinfd, TCSANOW, &tios);
#endif
}

static void watch_stdin(void)
{
    int stdinfd = fileno(stdin);
    GIOChannel *gin;

    setup_terminal(false);
    gin = g_io_channel_unix_new(stdinfd);
    g_io_channel_set_flags(gin, G_IO_FLAG_NONBLOCK, NULL);
    g_io_add_watch(gin, G_IO_IN|G_IO_ERR|G_IO_HUP|G_IO_NVAL, input_cb, NULL);
}
#endif

int main(int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;
    spice_connection *conn;
    gchar *conf_file, *conf;
    char *host = NULL, *port = NULL, *tls_port = NULL, *ws_port = NULL, *unix_path = NULL;
    disconnect_reason = DISCONNECT_NO_ERROR;

#if !GLIB_CHECK_VERSION(2,31,18)
    g_thread_init(NULL);
#endif
    keyfile = g_key_file_new();

    int mode = S_IRWXU;
    conf_file = g_build_filename(g_get_user_config_dir(), "spicy", NULL);
    if (g_mkdir_with_parents(conf_file, mode) == -1)
        SPICE_DEBUG("failed to create config directory");
    g_free(conf_file);

    conf_file = g_build_filename(g_get_user_config_dir(), "spicy", "settings", NULL);
    if (!g_key_file_load_from_file(keyfile, conf_file,
                                   G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS, &error)) {
        SPICE_DEBUG("Couldn't load configuration: %s", error->message);
        g_clear_error(&error);
    }

    /* parse opts */
    gtk_init(&argc, &argv);
    context = g_option_context_new("- spice client test application");
    g_option_context_set_summary(context, "Gtk+ test client to connect to Spice servers.");
    g_option_context_set_description(context, "Report bugs to " PACKAGE_BUGREPORT ".");
    g_option_context_add_group(context, spice_get_option_group());
    g_option_context_set_main_group(context, spice_cmdline_get_option_group());
    g_option_context_add_main_entries(context, cmd_entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));
#ifdef WITH_FLEXVDI
    g_option_context_add_group(context, flexvdi_get_option_group());
#endif
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print("option parsing failed: %s\n", error->message);
        exit(1);
    }
    g_option_context_free(context);

    if (version) {
        g_print("spicy " PACKAGE_VERSION "\n");
        exit(0);
    }

#if !GLIB_CHECK_VERSION(2,36,0)
    g_type_init();
#endif
    mainloop = g_main_loop_new(NULL, false);

    conn = connection_new();
    spice_set_session_option(conn->session);
    spice_cmdline_session_setup(conn->session);
#ifdef WITH_FLEXVDI
    flexvdi_port_register_session(conn->session);
#endif
    g_object_set(gtk_settings_get_default(), "gtk-icon-theme-name", "flexvdi", NULL);

    g_object_get(conn->session,
                 "unix-path", &unix_path,
                 "host", &host,
                 "port", &port,
                 "tls-port", &tls_port,
                 "ws-port", &ws_port,
                 NULL);
    /* If user doesn't provide hostname and port, show the dialog window
       instead of connecting to server automatically */
    if ((host == NULL || (port == NULL && tls_port == NULL)) && unix_path == NULL) {
        if (!spicy_connect_dialog(conn->session)) {
            exit(0);
        }
    }
    g_free(host);
    g_free(port);
    g_free(tls_port);
    g_free(ws_port);
    g_free(unix_path);

#ifndef WITH_FLEXVDI
    watch_stdin();
#endif

    connection_connect(conn);
    if (connections > 0)
        g_main_loop_run(mainloop);
    g_main_loop_unref(mainloop);

#ifdef WITH_FLEXVDI
    flexvdi_cleanup();
#endif

    if ((conf = g_key_file_to_data(keyfile, NULL, &error)) == NULL ||
        !g_file_set_contents(conf_file, conf, -1, &error)) {
        SPICE_DEBUG("Couldn't save configuration: %s", error->message);
        g_error_free(error);
        error = NULL;
    }

    g_free(conf_file);
    g_free(conf);
    g_key_file_free(keyfile);

    g_free(spicy_title);

#ifndef WITH_FLEXVDI
    setup_terminal(true);
#endif
    return disconnect_reason;
}
