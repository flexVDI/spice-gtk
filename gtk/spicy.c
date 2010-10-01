#include "spice-widget.h"
#include "spice-audio.h"
#include "spice-common.h"

typedef struct spice_window spice_window;
struct spice_window {
    int            id;
    GtkWidget      *toplevel, *spice;
    GtkWidget      *fstatus, *status;
    GtkWidget      *menubar, *toolbar;
    GtkActionGroup *ag;
    GtkAccelGroup  *accel;
    GtkUIManager   *ui;
    bool           fullscreen;
};

/* config */
static char *host      = "localhost";
static char *port      = "5920";
static char *tls_port  = "5921";
static char *password;
static char *ca_file;
static bool fullscreen = false;

/* state */
static GMainLoop    *mainloop;
static SpiceSession *session;
static spice_window *wins[4];
static GObject      *audio;

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
    gtk_container_add(GTK_CONTAINER(area), label);

    entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), dest);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    if (hide)
	gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_container_add(GTK_CONTAINER(area), entry);

    gtk_box_set_spacing(GTK_BOX(area), 10);
    gtk_container_set_border_width(GTK_CONTAINER(area), 10);

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

/* ------------------------------------------------------------------ */

static void menu_cb_quit(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    gtk_widget_destroy(win->toplevel);
}

static void menu_cb_copy(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    spice_display_copy_to_guest(win->spice);
}

static void menu_cb_paste(GtkAction *action, void *data)
{
    struct spice_window *win = data;

    spice_display_paste_from_guest(win->spice);
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

    spice_display_mouse_ungrab(win->spice);
}

static void menu_cb_bool_prop(GtkToggleAction *action, gpointer data)
{
    struct spice_window *win = data;
    gboolean state = gtk_toggle_action_get_active(action);

    fprintf(stderr, "%s: %s = %s\n", __FUNCTION__,
            gtk_action_get_name(GTK_ACTION(action)), state ? "yes" : "no");
    g_object_set(G_OBJECT(win->spice),
                 gtk_action_get_name(GTK_ACTION(action)), state,
                 NULL);
}

static void menu_cb_about(GtkAction *action, void *data)
{
    static char *comments = "gtk client app for the\n"
        "spice remote desktop protocol";
    static char *copyright = "(c) 2010 Red Hat";
    static char *website = "http://www.spice-space.org";
    static char *authors[] = { "Gerd Hoffmann <kraxel@redhat.com>", NULL };
    struct spice_window *win = data;

    gtk_show_about_dialog(GTK_WINDOW(win->toplevel),
                          "authors",         authors,
                          "comments",        comments,
                          "copyright",       copyright,
                          "logo-icon-name",  GTK_STOCK_ABOUT,
			  "website",         website,
//                        "version",         VERSION,
//			  "license",         "GPLv2+",
                          NULL);
}

static void destroy_cb(GtkWidget *widget, gpointer data)
{
    struct spice_window *win = data;

    if (win->id == 0) {
        g_main_loop_quit(mainloop);
    }
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
            gtk_widget_hide(win->fstatus);
            gtk_widget_grab_focus(win->spice);
        } else {
            gtk_widget_show(win->menubar);
            gtk_widget_show(win->toolbar);
            gtk_widget_show(win->fstatus);
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */

static const GtkActionEntry entries[] = {
    {
	.name        = "FileMenu",
	.label       = "_File",
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
	.name        = "Quit",
	.stock_id    = GTK_STOCK_QUIT,
	.label       = "_Quit",
	.callback    = G_CALLBACK(menu_cb_quit),
        .accelerator = "", /* none (disable default "<control>Q") */
    },{

	/* Edit menu */
	.name        = "CopyToGuest",
	.stock_id    = GTK_STOCK_COPY,
	.label       = "_Copy to guest",
	.callback    = G_CALLBACK(menu_cb_copy),
        .accelerator = "", /* none (disable default "<control>Q") */
    },{
	.name        = "PasteFromGuest",
	.stock_id    = GTK_STOCK_PASTE,
	.label       = "_Paste from guest",
	.callback    = G_CALLBACK(menu_cb_paste),
        .accelerator = "", /* none (disable default "<control>Q") */
    },{

	/* View menu */
	.name        = "Fullscreen",
	.stock_id    = GTK_STOCK_FULLSCREEN,
	.label       = "_Fullscreen",
	.callback    = G_CALLBACK(menu_cb_fullscreen),
        .accelerator = "<shift>F11",
    },{

	/* Input menu */
	.name        = "UngrabMouse",
	.label       = "_Ungrab mouse",
	.callback    = G_CALLBACK(menu_cb_ungrab),
        .accelerator = "<shift>F12",
    },{

	/* Help menu */
	.name        = "About",
	.stock_id    = GTK_STOCK_ABOUT,
	.label       = "_About ...",
	.callback    = G_CALLBACK(menu_cb_about),
    }
};

static const GtkToggleActionEntry tentries[] = {
    {
	.name        = "grab-keyboard",
	.label       = "Grab keyboard",
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "grab-mouse",
	.label       = "Grab mouse",
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "resize-guest",
	.label       = "Resize guest",
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    },{
	.name        = "auto-clipboard",
	.label       = "Automagic clipboard relay",
	.callback    = G_CALLBACK(menu_cb_bool_prop),
    }
};

static char ui_xml[] =
"<ui>\n"
"  <menubar action='MainMenu'>\n"
"    <menu action='FileMenu'>\n"
"      <menuitem action='Quit'/>\n"
"    </menu>\n"
"    <menu action='EditMenu'>\n"
"      <menuitem action='CopyToGuest'/>\n"
"      <menuitem action='PasteFromGuest'/>\n"
"    </menu>\n"
"    <menu action='ViewMenu'>\n"
"      <menuitem action='Fullscreen'/>\n"
"    </menu>\n"
"    <menu action='InputMenu'>\n"
"      <menuitem action='UngrabMouse'/>\n"
"    </menu>\n"
"    <menu action='OptionMenu'>\n"
"      <menuitem action='grab-keyboard'/>\n"
"      <menuitem action='grab-mouse'/>\n"
"      <menuitem action='resize-guest'/>\n"
"      <menuitem action='auto-clipboard'/>\n"
"    </menu>\n"
"    <menu action='HelpMenu'>\n"
"      <menuitem action='About'/>\n"
"    </menu>\n"
"  </menubar>\n"
"  <toolbar action='ToolBar'>\n"
"    <toolitem action='Quit'/>\n"
"    <separator/>\n"
"    <toolitem action='Fullscreen'/>\n"
"  </toolbar>\n"
"</ui>\n";

static spice_window *create_spice_window(SpiceSession *s, int id)
{
    char title[32];
    struct spice_window *win;
    GtkWidget *vbox;
    GError *err = NULL;
    int i;

    win = malloc(sizeof(*win));
    if (NULL == win)
        return NULL;
    memset(win,0,sizeof(*win));
    win->id = id;

    /* toplevel */
    win->toplevel = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    snprintf(title, sizeof(title), "spice display %d", id);
    gtk_window_set_title(GTK_WINDOW(win->toplevel), title);
    g_signal_connect(G_OBJECT(win->toplevel), "destroy",
                     G_CALLBACK(destroy_cb), win);
    g_signal_connect(G_OBJECT(win->toplevel), "window-state-event",
		     G_CALLBACK(window_state_cb), win);

    /* menu + toolbar */
    win->ui = gtk_ui_manager_new();
    win->ag = gtk_action_group_new("MenuActions");
    gtk_action_group_add_actions(win->ag, entries, G_N_ELEMENTS(entries), win);
    gtk_action_group_add_toggle_actions(win->ag, tentries,
					G_N_ELEMENTS(tentries), win);
    gtk_ui_manager_insert_action_group(win->ui, win->ag, 0);
    win->accel = gtk_ui_manager_get_accel_group(win->ui);
    gtk_window_add_accel_group(GTK_WINDOW(win->toplevel), win->accel);

    err = NULL;
    if (!gtk_ui_manager_add_ui_from_string(win->ui, ui_xml, -1, &err)) {
	g_message("building menus failed: %s", err->message);
	g_error_free(err);
	exit(1);
    }
    win->menubar = gtk_ui_manager_get_widget(win->ui, "/MainMenu");
    win->toolbar = gtk_ui_manager_get_widget(win->ui, "/ToolBar");

    /* spice display */
    win->spice = spice_display_new(s, id);

    /* status line */
    win->status = gtk_label_new("status line");
    gtk_misc_set_alignment(GTK_MISC(win->status), 0, 0.5);
    gtk_misc_set_padding(GTK_MISC(win->status), 3, 1);

    /* Make a vbox and put stuff in */
    vbox = gtk_vbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 0);
    gtk_container_add(GTK_CONTAINER(win->toplevel), vbox);
    gtk_box_pack_start(GTK_BOX(vbox), win->menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), win->toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), win->spice, TRUE, TRUE, 0);
    win->fstatus = gtk_frame_new(NULL);
    gtk_box_pack_end(GTK_BOX(vbox), win->fstatus, FALSE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(win->fstatus), win->status);

    /* init toggle actions */
    for (i = 0; i < G_N_ELEMENTS(tentries); i++) {
        GtkAction *toggle;
        gboolean state;
        toggle = gtk_action_group_get_action(win->ag, tentries[i].name);
        g_object_get(win->spice, tentries[i].name, &state, NULL);
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(toggle), state);
    }

    /* show window */
    if (fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(win->toplevel));
    gtk_widget_show_all(win->toplevel);
    return win;
}

/* ------------------------------------------------------------------ */

static void main_channel_event(SpiceChannel *channel, enum SpiceChannelEvent event,
                               gpointer *data)
{
    char message[256];
    char password[64];
    int rc;

    switch (event) {
    case SPICE_CHANNEL_OPENED:
        /* nothing */
        break;
    case SPICE_CHANNEL_CLOSED:
        fprintf(stderr, "main channel: closed\n");
        g_main_loop_quit(mainloop);
        break;
    case SPICE_CHANNEL_ERROR_AUTH:
        fprintf(stderr, "main channel: auth failure (wrong password?)\n");
        snprintf(message, sizeof(message),
                 "Please enter the password for the\n"
                 "spice session to %s", host);
        strcpy(password, "");
        rc = ask_user(NULL, "Authentication", message,
                      password, sizeof(password), true);
        if (rc == 0) {
            g_object_set(session, "password", password, NULL);
            spice_session_connect(session);
        } else {
            g_main_loop_quit(mainloop);
        }
        break;
    default:
        /* TODO: more sophisticated error handling */
        fprintf(stderr, "unknown main channel event: %d\n", event);
        g_main_loop_quit(mainloop);
        break;
    }
}

static void channel_new(SpiceSession *s, SpiceChannel *channel, gpointer *data)
{
    int id = spice_channel_id(channel);

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        g_signal_connect(channel, "spice-channel-event",
                         G_CALLBACK(main_channel_event), NULL);
        fprintf(stderr, "new main channel\n");
        return;
    }

    if (SPICE_IS_DISPLAY_CHANNEL(channel)) {
        if (id >= SPICE_N_ELEMENTS(wins))
            return;
        if (wins[id] != NULL)
            return;
        fprintf(stderr, "new display channel (#%d), creating window\n", id);
        wins[id] = create_spice_window(s, id);
        return;
    }

    if (SPICE_IS_PLAYBACK_CHANNEL(channel)) {
        if (audio != NULL)
            return;
        audio = spice_audio_new(s, mainloop, "spice");
        return;
    }
}

/* ------------------------------------------------------------------ */

static void usage(FILE *fp)
{
    fprintf(fp,
            "usage:\n"
            "  spicy [ options ]\n"
            "\n"
            "options:\n"
            "  -h <host>    remote host               [ %s ]\n"
            "  -p <port>    remote port               [ %s ]\n"
            "  -s <port>    remote tls port           [ %s ]\n"
            "  -w <secret>  password\n"
            "  -f           fullscreen\n"
            "\n",
            host, port, tls_port);
}

int main(int argc, char *argv[])
{
    int c;

    /* parse opts */
    gtk_init(&argc, &argv);
    for (;;) {
        if (-1 == (c = getopt(argc, argv, "h:p:s:w:f")))
            break;
        switch (c) {
	case 'h':
            host = optarg;
	    break;
	case 'p':
            port = optarg;
	    break;
	case 's':
            tls_port = optarg;
	    break;
	case 'w':
            password = optarg;
	    break;
	case 'f':
            fullscreen = true;
	    break;
#if 0 /* --help */
        case 'h':
            usage(stdout);
            exit(0);
#endif
        default:
            usage(stderr);
            exit(1);
        }
    }

    if (ca_file == NULL) {
        char *home = getenv("HOME");
        size_t size = strlen(home) + 32;
        ca_file = malloc(size);
        snprintf(ca_file, size, "%s/.spicec/spice_truststore.pem", home);
    }

    g_type_init();
    mainloop = g_main_loop_new(NULL, false);

    session = spice_session_new();
    g_signal_connect(session, "spice-session-channel-new",
                     G_CALLBACK(channel_new), NULL);

    if (host)
        g_object_set(session, "host", host, NULL);
    if (port)
        g_object_set(session, "port", port, NULL);
    if (tls_port)
        g_object_set(session, "tls-port", tls_port, NULL);
    if (password)
        g_object_set(session, "password", password, NULL);
    if (ca_file)
        g_object_set(session, "ca-file", ca_file, NULL);

    if (!spice_session_connect(session)) {
        fprintf(stderr, "spice_session_connect failed\n");
        exit(1);
    }

    g_main_loop_run(mainloop);
    return 0;
}
