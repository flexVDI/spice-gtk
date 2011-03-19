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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <glib/gi18n.h>

#include <sys/stat.h>

#define GNOME_DESKTOP_USE_UNSTABLE_API 2
#include "display/gnome-rr.h"
#include "display/gnome-rr-config.h"

#include "spice-widget.h"
#include "spice-audio.h"
#include "spice-common.h"
#include "spice-cmdline.h"

#include "spice-cmdline.h"

/* config */
static gboolean fullscreen = false;
static gboolean version = false;

enum {
    STATE_SCROLL_LOCK,
    STATE_CAPS_LOCK,
    STATE_NUM_LOCK,
    STATE_MAX,
};

typedef struct spice_window spice_window;
typedef struct spice_connection spice_connection;

struct spice_window {
    spice_connection *conn;
    int              id;
    GtkWidget        *toplevel, *spice;
    GtkWidget        *menubar, *toolbar;
    GtkWidget        *ritem, *rmenu;
    GtkWidget        *statusbar, *status, *st[STATE_MAX];
    GtkActionGroup   *ag;
    GtkUIManager     *ui;
    bool             fullscreen;
    bool             mouse_grabbed;
    SpiceChannel     *display_channel;
};

struct spice_connection {
    SpiceSession     *session;
    spice_window     *wins[4];
    SpiceAudio       *audio;
    char             *mouse_state;
    char             *agent_state;
    gboolean         agent_connected;
    int              channels;
    int              disconnecting;
};

static GMainLoop     *mainloop = NULL;
static int           connections = 0;
static GKeyFile      *keyfile = NULL;
static GnomeRRScreen *rrscreen = NULL;
static GnomeRRConfig *rrsaved = NULL;
static GnomeRRConfig *rrcurrent = NULL;

static spice_connection *connection_new(void);
static void connection_connect(spice_connection *conn);
static void connection_disconnect(spice_connection *conn);
static void connection_destroy(spice_connection *conn);

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

static struct {
    const char *text;
    const char *prop;
    GtkWidget *entry;
} connect_entries[] = {
    { .text = N_("Hostname"),   .prop = "host"      },
    { .text = N_("Port"),       .prop = "port"      },
    { .text = N_("TLS Port"),   .prop = "tls-port"  },
};

static void recent_item_activated_dialog_cb(GtkRecentChooser *chooser, gpointer data)
{
    GtkRecentInfo *info;
    gchar *txt = NULL;
    const gchar *uri;
    SpiceSession *session = data;

    info = gtk_recent_chooser_get_current_item(chooser);

    uri = gtk_recent_info_get_uri(info);
    g_return_if_fail(uri != NULL);

    g_object_set(session, "uri", uri, NULL);

    g_object_get(session, "host", &txt, NULL);
    gtk_entry_set_text(GTK_ENTRY(connect_entries[0].entry), txt ? txt : "");
    g_free(txt);

    g_object_get(session, "port", &txt, NULL);
    gtk_entry_set_text(GTK_ENTRY(connect_entries[1].entry), txt ? txt : "");
    g_free(txt);

    g_object_get(session, "tls-port", &txt, NULL);
    gtk_entry_set_text(GTK_ENTRY(connect_entries[2].entry), txt ? txt : "");
    g_free(txt);

    gtk_recent_info_unref(info);
}

static int connect_dialog(GtkWidget *parent, SpiceSession *session)
{
    GtkWidget *dialog, *area, *label, *recent;
    GtkTable *table;
    const gchar *txt;
    int i, retval;
    GtkRecentFilter  *rfilter;

    /* Create the widgets */
    dialog = gtk_dialog_new_with_buttons(_("Connect to SPICE"),
					 parent ? GTK_WINDOW(parent) : NULL,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK,
					 GTK_RESPONSE_ACCEPT,
					 GTK_STOCK_CANCEL,
					 GTK_RESPONSE_REJECT,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    table = GTK_TABLE(gtk_table_new(3, 2, 0));
    gtk_box_pack_start(GTK_BOX(area), GTK_WIDGET(table), TRUE, TRUE, 0);
    gtk_table_set_row_spacings(table, 5);
    gtk_table_set_col_spacings(table, 5);

    for (i = 0; i < SPICE_N_ELEMENTS(connect_entries); i++) {
        label = gtk_label_new(connect_entries[i].text);
        gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
        gtk_table_attach_defaults(table, label, 0, 1, i, i+1);
        connect_entries[i].entry = GTK_WIDGET(gtk_entry_new());
        gtk_table_attach_defaults(table, connect_entries[i].entry, 1, 2, i, i+1);
        g_object_get(session, connect_entries[i].prop, &txt, NULL);
        SPICE_DEBUG("%s: #%i [%s]: \"%s\"",
                __FUNCTION__, i, connect_entries[i].prop, txt);
        if (txt) {
            gtk_entry_set_text(GTK_ENTRY(connect_entries[i].entry), txt);
        }
    }

    label = gtk_label_new("Recent connexions:");
    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 0);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
    recent = GTK_WIDGET(gtk_recent_chooser_widget_new());
    gtk_box_pack_start(GTK_BOX(area), recent, TRUE, TRUE, 0);

    rfilter = gtk_recent_filter_new();
    gtk_recent_filter_add_mime_type(rfilter, "application/x-spice");
    gtk_recent_chooser_set_filter(GTK_RECENT_CHOOSER(recent), rfilter);
    gtk_recent_chooser_set_local_only(GTK_RECENT_CHOOSER(recent), FALSE);
    g_signal_connect(recent, "item-activated",
                     G_CALLBACK(recent_item_activated_dialog_cb), session);

    /* show and wait for response */
    gtk_widget_show_all(dialog);
    switch (gtk_dialog_run(GTK_DIALOG(dialog))) {
    case GTK_RESPONSE_ACCEPT:
        for (i = 0; i < SPICE_N_ELEMENTS(connect_entries); i++) {
            txt = gtk_entry_get_text(GTK_ENTRY(connect_entries[i].entry));
            g_object_set(session, connect_entries[i].prop, txt, NULL);
        }
	retval = 0;
	break;
    default:
	retval = -1;
	break;
    }
    gtk_widget_destroy(dialog);
    return retval;
}

/* ------------------------------------------------------------------ */

static void update_status(struct spice_window *win)
{
    char status[256];

    if (win == NULL)
        return;
    if (win->mouse_grabbed) {
        snprintf(status, sizeof(status), _("Use Shift+F12 to ungrab mouse."));
    } else {
        snprintf(status, sizeof(status), _("mouse: %s, agent: %s"),
                 win->conn->mouse_state, win->conn->agent_state);
    }
    gtk_label_set_text(GTK_LABEL(win->status), status);
}

static void menu_cb_connect(GtkAction *action, void *data)
{
    struct spice_connection *conn;

    conn = connection_new();
    connection_connect(conn);
}

static void menu_cb_close(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    connection_disconnect(win->conn);
}

static void menu_cb_copy(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    spice_display_copy_to_guest(SPICE_DISPLAY(win->spice));
}

static void menu_cb_paste(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    spice_display_paste_from_guest(SPICE_DISPLAY(win->spice));
}

static void menu_cb_fullscreen(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    if (win->fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(win->toplevel));
    } else {
        gtk_window_fullscreen(GTK_WINDOW(win->toplevel));
    }
}

static void menu_cb_ungrab(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    spice_display_mouse_ungrab(SPICE_DISPLAY(win->spice));
}

static void menu_cb_bool_prop(GtkToggleAction *action, gpointer data)
{
    struct spice_window *win = data;
    gboolean state = gtk_toggle_action_get_active(action);
    const char *name;

    name = gtk_action_get_name(GTK_ACTION(action));
    SPICE_DEBUG("%s: %s = %s", __FUNCTION__, name, state ? _("yes") : _("no"));

    g_key_file_set_boolean(keyfile, "general", name, state);
    g_object_set(G_OBJECT(win->spice), name, state, NULL);
}

static void menu_cb_toolbar(GtkToggleAction *action, gpointer data)
{
    struct spice_window *win = data;
    gboolean state = gtk_toggle_action_get_active(action);

    gtk_widget_set_visible(win->toolbar, state);
    g_key_file_set_boolean(keyfile, "ui", "toolbar", state);
}

static void menu_cb_statusbar(GtkToggleAction *action, gpointer data)
{
    struct spice_window *win = data;
    gboolean state = gtk_toggle_action_get_active(action);

    gtk_widget_set_visible(win->statusbar, state);
    g_key_file_set_boolean(keyfile, "ui", "statusbar", state);
}

static void menu_cb_about(GtkAction *action, void *data)
{
    char *comments = _("gtk client app for the\n"
        "spice remote desktop protocol");
    static char *copyright = "(c) 2010 Red Hat";
    static char *website = "http://www.spice-space.org";
    static char *authors[] = { "Gerd Hoffmann <kraxel@redhat.com>",
                               "Marc-André Lureau <marcandre.lureau@redhat.com>",
                               NULL };
    struct spice_window *win = data;

    gtk_show_about_dialog(GTK_WINDOW(win->toplevel),
                          "authors",         authors,
                          "comments",        comments,
                          "copyright",       copyright,
                          "logo-icon-name",  GTK_STOCK_ABOUT,
			  "website",         website,
                          "version",         PACKAGE_VERSION,
			  "license",         "LGPLv2.1",
                          NULL);
}

static gboolean delete_cb(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    struct spice_window *win = data;

    connection_disconnect(win->conn);
    return true;
}

static void destroy_cb(GtkWidget *widget, gpointer data)
{
#if 0
    struct spice_window *win = data;

    if (win->id == 0) {
        g_main_loop_quit(mainloop);
    }
#endif
}

static gboolean window_state_cb(GtkWidget *widget, GdkEventWindowState *event,
				gpointer data)
{
    struct spice_window *win = data;
    if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN) {
        win->fullscreen = event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN;
        if (win->fullscreen) {
            gtk_widget_hide(win->menubar);
            gtk_widget_hide(win->toolbar);
            gtk_widget_hide(win->statusbar);
            gtk_widget_grab_focus(win->spice);
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
        }
    }
    return TRUE;
}

static void mouse_grab_cb(GtkWidget *widget, gint grabbed, gpointer data)
{
    struct spice_window *win = data;

    win->mouse_grabbed = grabbed;
    update_status(win);
}

static void restore_configuration(struct spice_window *win)
{
    gboolean state;
    gchar **keys = NULL;
    gsize nkeys, i;
    GError *error = NULL;

    keys = g_key_file_get_keys(keyfile, "general", &nkeys, &error);
    if (error != NULL) {
        if (error->code != G_KEY_FILE_ERROR_GROUP_NOT_FOUND)
            g_warning("Failed to read configuration file keys: %s", error->message);
        g_clear_error(&error);
        return;
    }

    g_return_if_fail(nkeys >= 0);
    if (nkeys > 0)
        g_return_if_fail(keys != NULL);

    for (i = 0; i < nkeys; ++i) {
        state = g_key_file_get_boolean(keyfile, "general", keys[i], &error);
        if (error != NULL) {
            g_clear_error(&error);
            continue;
        }
        g_object_set(G_OBJECT(win->spice), keys[i], state, NULL);
    }

    g_strfreev(keys);

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
	.name        = "OptionMenu",
	.label       = "_Options",
    },{
	.name        = "HelpMenu",
	.label       = "_Help",
    },{

	/* File menu */
	.name        = "Connect",
	.stock_id    = GTK_STOCK_CONNECT,
	.label       = N_("_Connect ..."),
	.callback    = G_CALLBACK(menu_cb_connect),
        .accelerator = "", /* none (disable default "<control>Q") */
    },{
	.name        = "Close",
	.stock_id    = GTK_STOCK_CLOSE,
	.label       = N_("_Close"),
	.callback    = G_CALLBACK(menu_cb_close),
//        .accelerator = "", /* none (disable default "<control>Q") */
    },{

	/* Edit menu */
	.name        = "CopyToGuest",
	.stock_id    = GTK_STOCK_COPY,
	.label       = N_("_Copy to guest"),
	.callback    = G_CALLBACK(menu_cb_copy),
        .accelerator = "", /* none (disable default "<control>Q") */
    },{
	.name        = "PasteFromGuest",
	.stock_id    = GTK_STOCK_PASTE,
	.label       = N_("_Paste from guest"),
	.callback    = G_CALLBACK(menu_cb_paste),
        .accelerator = "", /* none (disable default "<control>Q") */
    },{

	/* View menu */
	.name        = "Fullscreen",
	.stock_id    = GTK_STOCK_FULLSCREEN,
	.label       = N_("_Fullscreen"),
	.callback    = G_CALLBACK(menu_cb_fullscreen),
        .accelerator = "<shift>F11",
    },{

	/* Input menu */
	.name        = "UngrabMouse",
	.label       = N_("_Ungrab mouse"),
	.callback    = G_CALLBACK(menu_cb_ungrab),
        .accelerator = "<shift>F12",
    },{

	/* Help menu */
	.name        = "About",
	.stock_id    = GTK_STOCK_ABOUT,
	.label       = N_("_About ..."),
	.callback    = G_CALLBACK(menu_cb_about),
    }
};

static const char *spice_properties[] = {
    "grab-keyboard",
    "grab-mouse",
    "resize-guest",
    "scaling",
    "auto-clipboard"
};

static const GtkToggleActionEntry tentries[] = {
    {
	.name        = "grab-keyboard",
	.label       = N_("Grab keyboard when active and focused"),
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "grab-mouse",
	.label       = N_("Grab mouse in server mode (no tabled/vdagent)"),
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "resize-guest",
	.label       = N_("Resize guest to match window size"),
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "scaling",
	.label       = N_("Scale display"),
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "auto-clipboard",
	.label       = N_("Automagic clipboard sharing between host and guest"),
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "Statusbar",
	.label       = N_("Statusbar"),
	.callback    = G_CALLBACK(menu_cb_statusbar),
    },{
	.name        = "Toolbar",
	.label       = N_("Toolbar"),
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
"      <menuitem action='UngrabMouse'/>\n"
"    </menu>\n"
"    <menu action='OptionMenu'>\n"
"      <menuitem action='grab-keyboard'/>\n"
"      <menuitem action='grab-mouse'/>\n"
"      <menuitem action='resize-guest'/>\n"
"      <menuitem action='scaling'/>\n"
"      <menuitem action='auto-clipboard'/>\n"
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
"  </toolbar>\n"
"</ui>\n";

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

static GnomeRROutputInfo *
get_nearest_output (GnomeRRConfig *configuration, int x, int y)
{
  int i;
  int nearest_index;
  int nearest_dist;
  GnomeRROutputInfo **outputs;

  nearest_index = -1;
  nearest_dist = G_MAXINT;

  outputs = gnome_rr_config_get_outputs (configuration);
  for (i = 0; outputs[i] != NULL; i++)
    {
      int dist_x, dist_y;
      int output_x, output_y, output_width, output_height;

      if (!(gnome_rr_output_info_is_connected (outputs[i]) && gnome_rr_output_info_is_active (outputs[i])))
	continue;

      gnome_rr_output_info_get_geometry (outputs[i], &output_x, &output_y, &output_width, &output_height);

      if (x < output_x)
	dist_x = output_x - x;
      else if (x >= output_x + output_width)
	dist_x = x - (output_x + output_width) + 1;
      else
	dist_x = 0;

      if (y < output_y)
	dist_y = output_y - y;
      else if (y >= output_y + output_height)
	dist_y = y - (output_y + output_height) + 1;
      else
	dist_y = 0;

      if (MIN (dist_x, dist_y) < nearest_dist)
	{
	  nearest_dist = MIN (dist_x, dist_y);
	  nearest_index = i;
	}
    }

  if (nearest_index != -1)
    return outputs[nearest_index];
  else
    return NULL;
}

#if !GTK_CHECK_VERSION (2, 91, 0)
#define gdk_window_get_geometry(win,x,y,w,h) gdk_window_get_geometry(win,x,y,w,h,NULL)
#endif

static GnomeRROutputInfo *
get_output_for_window(GnomeRRConfig *configuration, GdkWindow *window)
{
  GdkRectangle win_rect;
  int i;
  int largest_area;
  int largest_index;
  GnomeRROutputInfo **outputs;

  gdk_window_get_geometry (window, &win_rect.x, &win_rect.y, &win_rect.width, &win_rect.height);
  gdk_window_get_origin (window, &win_rect.x, &win_rect.y);

  largest_area = 0;
  largest_index = -1;

  outputs = gnome_rr_config_get_outputs (configuration);
  for (i = 0; outputs[i] != NULL; i++)
    {
      GdkRectangle output_rect, intersection;

      gnome_rr_output_info_get_geometry (outputs[i], &output_rect.x, &output_rect.y, &output_rect.width, &output_rect.height);

      if (gnome_rr_output_info_is_connected (outputs[i]) && gdk_rectangle_intersect (&win_rect, &output_rect, &intersection))
	{
	  int area;

	  area = intersection.width * intersection.height;
	  if (area > largest_area)
	    {
	      largest_area = area;
	      largest_index = i;
	    }
	}
    }

  if (largest_index != -1)
    return outputs[largest_index];
  else
    return get_nearest_output (configuration,
			       win_rect.x + win_rect.width / 2,
			       win_rect.y + win_rect.height / 2);
}

#if 0
static GnomeRROutputInfo *get_primary_output() {
    GnomeRROutputInfo **outputs;
    GnomeRROutputInfo *output;
    guint i;

    outputs = gnome_rr_config_get_outputs (rrcurrent);
    for (i = 0; outputs[i] != NULL; ++i) {
        output = outputs[i];
        if (!gnome_rr_output_info_is_connected (output))
            continue;
        if (gnome_rr_output_info_get_primary (output))
            break;
    }
    output = outputs[i];
    g_return_if_fail(output != NULL);
    return output;
}
#endif

static void
on_screen_changed(GnomeRRScreen *scr, gpointer data)
{
    GError *error = NULL;
    GnomeRRConfig *current;

    current = gnome_rr_config_new_current(rrscreen, &error);
    if (!current) {
        g_warning("Can't get current display config: %s", error->message);
        goto end;
    }

    if (rrcurrent)
        g_object_unref(rrcurrent);
    rrcurrent = current;

end:
    g_clear_error(&error);
}

static void resolution_fullscreen(struct spice_window *win)
{
    GnomeRROutputInfo *output;
    int x, y, width, height;
    GError *error = NULL;;

    if (!rrsaved) {
        rrsaved = gnome_rr_config_new_current(rrscreen, &error);
        g_clear_error(&error);
    }

    output = get_output_for_window(rrcurrent, gtk_widget_get_window(win->spice));
    g_return_if_fail(output != NULL);

    gnome_rr_output_info_get_geometry (output, &x, &y, &width, &height);
    g_object_get(win->display_channel, "width", &width, "height", &height, NULL);
    gnome_rr_output_info_set_geometry (output, x, y, width, height);

    if (!gnome_rr_config_apply_with_time(rrcurrent, rrscreen,
                                         gtk_get_current_event_time (), &error)) {
        g_warning("Can't set display config: %s", error->message);
    }
    g_clear_error(&error);
}

static void resolution_restore(void)
{
    GError *error = NULL;;

    if (!rrsaved)
        return;

    if (!gnome_rr_config_apply_with_time(rrsaved, rrscreen,
                                         gtk_get_current_event_time (), &error)) {
        g_warning("Can't restore display config: %s", error->message);
    }
    g_clear_error(&error);

    g_object_unref(rrsaved);
    rrsaved = NULL;
}

static gboolean configure_event_cb(GtkWidget         *widget,
                                   GdkEventConfigure *event,
                                   gpointer           data)
{
    gboolean resize_guest;
    struct spice_window *win = data;

    g_return_val_if_fail(win != NULL, FALSE);
    g_return_val_if_fail(win->conn != NULL, FALSE);

    g_object_get(win->spice, "resize-guest", &resize_guest, NULL);
    if (resize_guest && win->conn->agent_connected)
        return FALSE;

    if (win->fullscreen)
        resolution_fullscreen(win);
    else
        resolution_restore();

    return FALSE;
}

static spice_window *create_spice_window(spice_connection *conn, int id, SpiceChannel *channel)
{
    char title[32];
    struct spice_window *win;
    GtkAction *toggle;
    gboolean state;
    GtkWidget *vbox, *frame;
    GError *err = NULL;
    int i;
    SpiceGrabSequence *seq;
    GtkRecentFilter  *rfilter;

    win = malloc(sizeof(*win));
    if (NULL == win)
        return NULL;
    memset(win,0,sizeof(*win));
    win->id = id;
    win->conn = conn;
    win->display_channel = channel;
    g_message("create window (#%d)", win->id);

    /* toplevel */
    win->toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    snprintf(title, sizeof(title), _("spice display %d"), id);
    gtk_window_set_title(GTK_WINDOW(win->toplevel), title);
    g_signal_connect(G_OBJECT(win->toplevel), "window-state-event",
		     G_CALLBACK(window_state_cb), win);
    g_signal_connect(G_OBJECT(win->toplevel), "delete-event",
		     G_CALLBACK(delete_cb), win);
    g_signal_connect(G_OBJECT(win->toplevel), "destroy",
                     G_CALLBACK(destroy_cb), win);

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

    /* recent menu */
    win->ritem  = gtk_ui_manager_get_widget
        (win->ui, "/MainMenu/FileMenu/FileRecentMenu");
    win->rmenu = gtk_recent_chooser_menu_new();
    rfilter = gtk_recent_filter_new();
    gtk_recent_filter_add_mime_type(rfilter, "application/x-spice");
    gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(win->rmenu), rfilter);
    gtk_recent_chooser_set_local_only(GTK_RECENT_CHOOSER(win->rmenu), FALSE);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(win->ritem), win->rmenu);
    g_signal_connect(win->rmenu, "item-activated",
                     G_CALLBACK(recent_item_activated_cb), win);

    /* spice display */
    win->spice = GTK_WIDGET(spice_display_new(conn->session, id));
    g_signal_connect(win->spice, "configure-event", G_CALLBACK(configure_event_cb), win);
    seq = spice_grab_sequence_new_from_string("Shift+F12");
    spice_display_set_grab_keys(SPICE_DISPLAY(win->spice), seq);
    spice_grab_sequence_free(seq);

    g_signal_connect(G_OBJECT(win->spice), "mouse-grab",
		     G_CALLBACK(mouse_grab_cb), win);

    /* status line */
    win->statusbar = gtk_hbox_new(FALSE, 1);

    win->status = gtk_label_new("status line");
    gtk_misc_set_alignment(GTK_MISC(win->status), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(win->status), 3, 1);
    update_status(win);

    frame = gtk_frame_new(NULL);
    gtk_box_pack_start(GTK_BOX(win->statusbar), frame, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(frame), win->status);

    for (i = 0; i < STATE_MAX; i++) {
        win->st[i] = gtk_label_new(_("?"));
        gtk_label_set_width_chars(GTK_LABEL(win->st[i]), 5);
        frame = gtk_frame_new(NULL);
        gtk_box_pack_end(GTK_BOX(win->statusbar), frame, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(frame), win->st[i]);
    }

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);
    gtk_container_add(GTK_CONTAINER(win->toplevel), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), win->menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), win->toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), win->spice, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(vbox), win->statusbar, FALSE, TRUE, 0);

    /* show window */
    if (fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(win->toplevel));

    gtk_widget_show_all(win->toplevel);

    restore_configuration(win);

    /* init toggle actions */
    for (i = 0; i < G_N_ELEMENTS(spice_properties); i++) {
        toggle = gtk_action_group_get_action(win->ag, spice_properties[i]);
        g_object_get(win->spice, spice_properties[i], &state, NULL);
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);
    }

    toggle = gtk_action_group_get_action(win->ag, "Toolbar");
    state = gtk_widget_get_visible(win->toolbar);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);

    toggle = gtk_action_group_get_action(win->ag, "Statusbar");
    state = gtk_widget_get_visible(win->statusbar);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);

    gtk_widget_grab_focus(win->spice);
    return win;
}

static void destroy_spice_window(spice_window *win)
{
    SPICE_DEBUG("destroy window (#%d)", win->id);
    g_object_unref(win->ag);
    g_object_unref(win->ui);
    gtk_widget_destroy(win->toplevel);
    free(win);
}

/* ------------------------------------------------------------------ */

static void recent_add(SpiceSession *session)
{
    GtkRecentManager *recent;
    GtkRecentData meta = {
        .mime_type    = "application/x-spice",
        .app_name     = "spicy",
        .app_exec     = "spicy --uri=%u",
    };
    char *uri;

    g_object_get(session, "uri", &uri, NULL);
    SPICE_DEBUG("%s: %s", __FUNCTION__, uri);

    g_return_if_fail(g_str_has_prefix(uri, "spice://"));

    recent = gtk_recent_manager_get_default();
    meta.display_name = uri + 8;
    if (!gtk_recent_manager_add_full(recent, uri, &meta))
        g_warning("Recent item couldn't be added successfully");

    g_free(uri);
}

static void main_channel_event(SpiceChannel *channel, SpiceChannelEvent event,
                               gpointer data)
{
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
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_IO:
        connection_disconnect(conn);
        break;
    case SPICE_CHANNEL_ERROR_TLS:
    case SPICE_CHANNEL_ERROR_LINK:
    case SPICE_CHANNEL_ERROR_CONNECT:
        g_message("main channel: failed to connect");
        rc = connect_dialog(NULL, conn->session);
        if (rc == 0) {
            connection_connect(conn);
        } else {
            connection_disconnect(conn);
        }
        break;
    case SPICE_CHANNEL_ERROR_AUTH:
        g_warning("main channel: auth failure (wrong password?)");
        strcpy(password, "");
        /* FIXME i18 */
        rc = ask_user(NULL, _("Authentication"),
                      _("Please enter the spice server password"),
                      password, sizeof(password), true);
        if (rc == 0) {
            g_object_set(conn->session, "password", password, NULL);
            connection_connect(conn);
        } else {
            connection_disconnect(conn);
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
    update_status(conn->wins[0]);
}

static void main_agent_update(SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;

    g_object_get(channel, "agent-connected", &conn->agent_connected, NULL);
    conn->agent_state = conn->agent_connected ? _("yes") : _("no");
    update_status(conn->wins[0]);
}

static void inputs_modifiers(SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int m;

    g_object_get(channel, "key-modifiers", &m, NULL);
    gtk_label_set_text(GTK_LABEL(conn->wins[0]->st[STATE_SCROLL_LOCK]),
                       m & SPICE_KEYBOARD_MODIFIER_FLAGS_SCROLL_LOCK ? _("SCROLL") : "");
    gtk_label_set_text(GTK_LABEL(conn->wins[0]->st[STATE_CAPS_LOCK]),
                       m & SPICE_KEYBOARD_MODIFIER_FLAGS_CAPS_LOCK ? _("CAPS") : "");
    gtk_label_set_text(GTK_LABEL(conn->wins[0]->st[STATE_NUM_LOCK]),
                       m & SPICE_KEYBOARD_MODIFIER_FLAGS_NUM_LOCK ? _("NUM") : "");
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    conn->channels++;

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("new main channel");
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
        conn->wins[id] = create_spice_window(conn, id, channel);
    }

    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        SPICE_DEBUG("new inputs channel");
        g_signal_connect(channel, "inputs-modifiers",
                         G_CALLBACK(inputs_modifiers), conn);
    }

    if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        if (conn->audio != NULL)
            return;
        SPICE_DEBUG("new audio channel");
        conn->audio = spice_audio_new(s, NULL, NULL);
    }
}

static void channel_destroy(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    spice_connection *conn = data;
    int id;

    g_object_get(channel, "channel-id", &id, NULL);
    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("zap main channel");
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id >= SPICE_N_ELEMENTS(conn->wins))
            return;
        if (conn->wins[id] == NULL)
            return;
        SPICE_DEBUG("zap display channel (#%d)", id);
        destroy_spice_window(conn->wins[id]);
        conn->wins[id] = NULL;
    }

    if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        SPICE_DEBUG("zap audio channel");
        if (conn->audio != NULL) {
            g_object_unref(conn->audio);
            conn->audio = NULL;
        }
    }

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

    conn = g_new0(spice_connection, 1);
    conn->session = spice_session_new();
    g_signal_connect(conn->session, "channel-new",
                     G_CALLBACK(channel_new), conn);
    g_signal_connect(conn->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), conn);
    g_signal_connect(conn->session, "notify::migration-state",
                     G_CALLBACK(migration_state), conn);
    connections++;
    SPICE_DEBUG("%s (%d)", __FUNCTION__, connections);
    return conn;
}

static void connection_connect(spice_connection *conn)
{
    conn->disconnecting = false;
    spice_session_connect(conn->session);
}

static void connection_disconnect(spice_connection *conn)
{
    if (conn->disconnecting)
        return;
    conn->disconnecting = true;
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
        .description      = N_("Open in full screen mode"),
    },
    {
        .long_name        = "version",
        .arg              = G_OPTION_ARG_NONE,
        .arg_data         = &version,
        .description      = N_("Display version and quit"),
    },
    {
        /* end of list */
    }
};

static void (* segv_handler) (int) = SIG_DFL;
static void (* abrt_handler) (int) = SIG_DFL;
static void (* fpe_handler)  (int) = SIG_DFL;
static void (* ill_handler)  (int) = SIG_DFL;
#ifndef WIN32
static void (* bus_handler)  (int) = SIG_DFL;
#endif

static void
signal_handler(int signum)
{
    static gint recursion = FALSE;

    /*
     * reset all signal handlers: any further crashes should just be allowed
     * to crash normally.
     * */
    signal(SIGSEGV, segv_handler);
    signal(SIGABRT, abrt_handler);
    signal(SIGFPE,  fpe_handler);
    signal(SIGILL,  ill_handler);
#ifndef WIN32
    signal(SIGBUS,  bus_handler);
#endif

    /* Stop bizarre loops */
    if (recursion)
        abort ();

    recursion = TRUE;

    g_main_loop_quit(mainloop);
}

int main(int argc, char *argv[])
{
    GError *error = NULL;
    GOptionContext *context;
    spice_connection *conn;
    gchar *conf_file, *conf;

    g_thread_init(NULL);
    bindtextdomain(GETTEXT_PACKAGE, SPICE_GTK_LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    segv_handler = signal(SIGSEGV, signal_handler);
    abrt_handler = signal(SIGABRT, signal_handler);
    fpe_handler  = signal(SIGFPE,  signal_handler);
    ill_handler  = signal(SIGILL,  signal_handler);
#ifndef WIN32
    signal(SIGHUP, signal_handler);
    bus_handler  = signal(SIGBUS,  signal_handler);
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
    context = g_option_context_new(_("- spice client application"));
    g_option_context_set_summary(context, _("A Gtk client to connect to Spice servers."));
    g_option_context_set_description(context, _("Report bugs to " PACKAGE_BUGREPORT "."));
    g_option_context_add_main_entries(context, cmd_entries, NULL);
    g_option_context_add_group(context, gtk_get_option_group(TRUE));
    g_option_context_add_group(context, spice_cmdline_get_option_group());
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print(_("option parsing failed: %s\n"), error->message);
        exit(1);
    }

    if (version) {
        g_print("spicy " PACKAGE_VERSION "\n");
        exit(0);
    }

    g_type_init();
    mainloop = g_main_loop_new(NULL, false);
    rrscreen = gnome_rr_screen_new(gdk_screen_get_default (), &error);
    g_warn_if_fail(rrscreen != NULL);
    g_signal_connect(rrscreen, "changed", G_CALLBACK(on_screen_changed), NULL);
    on_screen_changed(rrscreen, NULL);

    conn = connection_new();
    spice_cmdline_session_setup(conn->session);
    connection_connect(conn);

    if (connections > 0)
        g_main_loop_run(mainloop);

    if ((conf = g_key_file_to_data(keyfile, NULL, &error)) == NULL ||
        !g_file_set_contents(conf_file, conf, -1, &error)) {
        SPICE_DEBUG("Couldn't save configuration: %s", error->message);
        g_error_free(error);
        error = NULL;
    }

    g_free(conf_file);

    resolution_restore();

    return 0;
}
