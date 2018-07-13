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
#include <gdk/gdk.h>

#ifdef HAVE_X11_XKBLIB_H
#include <X11/XKBlib.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#endif
#ifdef G_OS_WIN32
#include <windows.h>
#include <gdk/gdkwin32.h>
#ifndef MAPVK_VK_TO_VSC /* may be undefined in older mingw-headers */
#define MAPVK_VK_TO_VSC 0
#endif
#endif

#include <gtk/gtk.h>
#include <spice/vd_agent.h>
#include "desktop-integration.h"
#include "spice-common.h"
#include "spice-gtk-session.h"
#include "spice-gtk-session-priv.h"
#include "spice-session-priv.h"
#include "spice-util-priv.h"
#include "spice-channel-priv.h"

#define CLIPBOARD_LAST (VD_AGENT_CLIPBOARD_SELECTION_SECONDARY + 1)

struct _SpiceGtkSessionPrivate {
    SpiceSession            *session;
    /* Clipboard related */
    gboolean                auto_clipboard_enable;
    SpiceMainChannel        *main;
    GtkClipboard            *clipboard;
    GtkClipboard            *clipboard_primary;
    GtkTargetEntry          *clip_targets[CLIPBOARD_LAST];
    guint                   nclip_targets[CLIPBOARD_LAST];
    gboolean                clip_hasdata[CLIPBOARD_LAST];
    gboolean                clip_grabbed[CLIPBOARD_LAST];
    gboolean                clipboard_by_guest[CLIPBOARD_LAST];
    /* auto-usbredir related */
    gboolean                auto_usbredir_enable;
    int                     auto_usbredir_reqs;
    gboolean                pointer_grabbed;
    gboolean                disable_copy_to_guest;
    gboolean                disable_paste_from_guest;
    gboolean                keyboard_has_focus;
    gboolean                mouse_has_pointer;
    gboolean                sync_modifiers;
};

/**
 * SECTION:spice-gtk-session
 * @short_description: handles GTK connection details
 * @title: Spice GTK Session
 * @section_id:
 * @see_also: #SpiceSession, and the GTK widget #SpiceDisplay
 * @stability: Stable
 * @include: spice-client-gtk.h
 *
 * The #SpiceGtkSession class is the spice-client-gtk counter part of
 * #SpiceSession. It contains functionality which should be handled per
 * session rather then per #SpiceDisplay (one session can have multiple
 * displays), but which cannot live in #SpiceSession as it depends on
 * GTK. For example the clipboard functionality.
 *
 * There should always be a 1:1 relation between #SpiceGtkSession objects
 * and #SpiceSession objects. Therefor there is no spice_gtk_session_new,
 * instead there is spice_gtk_session_get() which ensures this 1:1 relation.
 *
 * Client and guest clipboards will be shared automatically if
 * #SpiceGtkSession:auto-clipboard is set to #TRUE. Alternatively, you
 * can send / receive clipboard data from client to guest with
 * spice_gtk_session_copy_to_guest() / spice_gtk_session_paste_from_guest().
 */

/* ------------------------------------------------------------------ */
/* Prototypes for private functions */
static void clipboard_owner_change(GtkClipboard *clipboard,
                                   GdkEventOwnerChange *event,
                                   gpointer user_data);
static void channel_new(SpiceSession *session, SpiceChannel *channel,
                        gpointer user_data);
static void channel_destroy(SpiceSession *session, SpiceChannel *channel,
                            gpointer user_data);
static gboolean read_only(SpiceGtkSession *self);

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_GTK_SESSION_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_GTK_SESSION, SpiceGtkSessionPrivate))

G_DEFINE_TYPE (SpiceGtkSession, spice_gtk_session, G_TYPE_OBJECT);

/* Properties */
enum {
    PROP_0,
    PROP_SESSION,
    PROP_AUTO_CLIPBOARD,
    PROP_AUTO_USBREDIR,
    PROP_POINTER_GRABBED,
    PROP_DISABLE_COPY_TO_GUEST,
    PROP_DISABLE_PASTE_FROM_GUEST,
    PROP_SYNC_MODIFIERS,
};

static guint32 get_keyboard_lock_modifiers(void)
{
    guint32 modifiers = 0;
#if GTK_CHECK_VERSION(3,18,0)
/* Ignore GLib's too-new warnings */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GdkKeymap *keyboard = gdk_keymap_get_default();

    if (gdk_keymap_get_caps_lock_state(keyboard)) {
        modifiers |= SPICE_INPUTS_CAPS_LOCK;
    }

    if (gdk_keymap_get_num_lock_state(keyboard)) {
        modifiers |= SPICE_INPUTS_NUM_LOCK;
    }

    if (gdk_keymap_get_scroll_lock_state(keyboard)) {
        modifiers |= SPICE_INPUTS_SCROLL_LOCK;
    }
G_GNUC_END_IGNORE_DEPRECATIONS
#else
#ifdef HAVE_X11_XKBLIB_H
    Display *x_display = NULL;
    XKeyboardState keyboard_state;

    GdkScreen *screen = gdk_screen_get_default();
    if (!GDK_IS_X11_DISPLAY(gdk_screen_get_display(screen))) {
        SPICE_DEBUG("FIXME: gtk backend is not X11");
        return 0;
    }

    x_display = GDK_SCREEN_XDISPLAY(screen);
    XGetKeyboardControl(x_display, &keyboard_state);

    if (keyboard_state.led_mask & 0x01) {
        modifiers |= SPICE_INPUTS_CAPS_LOCK;
    }
    if (keyboard_state.led_mask & 0x02) {
        modifiers |= SPICE_INPUTS_NUM_LOCK;
    }
    if (keyboard_state.led_mask & 0x04) {
        modifiers |= SPICE_INPUTS_SCROLL_LOCK;
    }
#elif defined(G_OS_WIN32)
    if (GetKeyState(VK_CAPITAL) & 1) {
        modifiers |= SPICE_INPUTS_CAPS_LOCK;
    }
    if (GetKeyState(VK_NUMLOCK) & 1) {
        modifiers |= SPICE_INPUTS_NUM_LOCK;
    }
    if (GetKeyState(VK_SCROLL) & 1) {
        modifiers |= SPICE_INPUTS_SCROLL_LOCK;
    }
#else
    g_warning("get_keyboard_lock_modifiers not implemented");
#endif // HAVE_X11_XKBLIB_H
#endif // GTK_CHECK_VERSION(3,18,0)
    return modifiers;
}

static void spice_gtk_session_sync_keyboard_modifiers_for_channel(SpiceGtkSession *self,
                                                                  SpiceInputsChannel* inputs,
                                                                  gboolean force)
{
    guint32 guest_modifiers = 0, client_modifiers = 0;

    g_return_if_fail(SPICE_IS_INPUTS_CHANNEL(inputs));

    if (SPICE_IS_GTK_SESSION(self) && !self->priv->sync_modifiers) {
        SPICE_DEBUG("Syncing modifiers is disabled");
        return;
    }

    g_object_get(inputs, "key-modifiers", &guest_modifiers, NULL);
    client_modifiers = get_keyboard_lock_modifiers();

    if (force || client_modifiers != guest_modifiers) {
        CHANNEL_DEBUG(inputs, "client_modifiers:0x%x, guest_modifiers:0x%x",
                      client_modifiers, guest_modifiers);
        spice_inputs_channel_set_key_locks(inputs, client_modifiers);
    }
}

static void spice_gtk_session_init(SpiceGtkSession *self)
{
    SpiceGtkSessionPrivate *s;

    s = self->priv = SPICE_GTK_SESSION_GET_PRIVATE(self);

    s->clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    g_signal_connect(G_OBJECT(s->clipboard), "owner-change",
                     G_CALLBACK(clipboard_owner_change), self);
    s->clipboard_primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    g_signal_connect(G_OBJECT(s->clipboard_primary), "owner-change",
                     G_CALLBACK(clipboard_owner_change), self);
}

static void
spice_gtk_session_constructed(GObject *gobject)
{
    SpiceGtkSession *self;
    SpiceGtkSessionPrivate *s;
    GList *list;
    GList *it;

    self = SPICE_GTK_SESSION(gobject);
    s = self->priv;
    if (!s->session)
        g_error("SpiceGtKSession constructed without a session");

    g_signal_connect(s->session, "channel-new",
                     G_CALLBACK(channel_new), self);
    g_signal_connect(s->session, "channel-destroy",
                     G_CALLBACK(channel_destroy), self);
    list = spice_session_get_channels(s->session);
    for (it = g_list_first(list); it != NULL; it = g_list_next(it)) {
        channel_new(s->session, it->data, (gpointer*)self);
    }
    g_list_free(list);
}

static void spice_gtk_session_dispose(GObject *gobject)
{
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = self->priv;

    /* release stuff */
    if (s->clipboard) {
        g_signal_handlers_disconnect_by_func(s->clipboard,
                G_CALLBACK(clipboard_owner_change), self);
        s->clipboard = NULL;
    }

    if (s->clipboard_primary) {
        g_signal_handlers_disconnect_by_func(s->clipboard_primary,
                G_CALLBACK(clipboard_owner_change), self);
        s->clipboard_primary = NULL;
    }

    if (s->session) {
        g_signal_handlers_disconnect_by_func(s->session,
                                             G_CALLBACK(channel_new),
                                             self);
        g_signal_handlers_disconnect_by_func(s->session,
                                             G_CALLBACK(channel_destroy),
                                             self);
        s->session = NULL;
    }

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_gtk_session_parent_class)->dispose)
        G_OBJECT_CLASS(spice_gtk_session_parent_class)->dispose(gobject);
}

static void spice_gtk_session_finalize(GObject *gobject)
{
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = self->priv;
    int i;

    /* release stuff */
    for (i = 0; i < CLIPBOARD_LAST; ++i) {
        g_clear_pointer(&s->clip_targets[i], g_free);
    }

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_gtk_session_parent_class)->finalize)
        G_OBJECT_CLASS(spice_gtk_session_parent_class)->finalize(gobject);
}

static void spice_gtk_session_get_property(GObject    *gobject,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, s->session);
	break;
    case PROP_AUTO_CLIPBOARD:
        g_value_set_boolean(value, s->auto_clipboard_enable);
        break;
    case PROP_AUTO_USBREDIR:
        g_value_set_boolean(value, s->auto_usbredir_enable);
        break;
    case PROP_POINTER_GRABBED:
        g_value_set_boolean(value, s->pointer_grabbed);
        break;
    case PROP_DISABLE_COPY_TO_GUEST:
        g_value_set_boolean(value, s->disable_copy_to_guest);
        break;
    case PROP_DISABLE_PASTE_FROM_GUEST:
        g_value_set_boolean(value, s->disable_paste_from_guest);
        break;
    case PROP_SYNC_MODIFIERS:
        g_value_set_boolean(value, s->sync_modifiers);
        break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
	break;
    }
}

static void spice_gtk_session_set_property(GObject      *gobject,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = self->priv;

    switch (prop_id) {
    case PROP_SESSION:
        s->session = g_value_get_object(value);
        break;
    case PROP_AUTO_CLIPBOARD:
        s->auto_clipboard_enable = g_value_get_boolean(value);
        break;
    case PROP_AUTO_USBREDIR: {
        SpiceDesktopIntegration *desktop_int;
        gboolean orig_value = s->auto_usbredir_enable;

        s->auto_usbredir_enable = g_value_get_boolean(value);
        if (s->auto_usbredir_enable == orig_value)
            break;

        if (s->auto_usbredir_reqs) {
            SpiceUsbDeviceManager *manager =
                spice_usb_device_manager_get(s->session, NULL);

            if (!manager)
                break;

            g_object_set(manager, "auto-connect", s->auto_usbredir_enable,
                         NULL);

            desktop_int = spice_desktop_integration_get(s->session);
            if (s->auto_usbredir_enable)
                spice_desktop_integration_inhibit_automount(desktop_int);
            else
                spice_desktop_integration_uninhibit_automount(desktop_int);
        }
        break;
    }
    case PROP_DISABLE_COPY_TO_GUEST:
        s->disable_copy_to_guest = g_value_get_boolean(value);
        break;
    case PROP_DISABLE_PASTE_FROM_GUEST:
        s->disable_paste_from_guest = g_value_get_boolean(value);
        break;
    case PROP_SYNC_MODIFIERS:
        s->sync_modifiers = g_value_get_boolean(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_gtk_session_class_init(SpiceGtkSessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->constructed  = spice_gtk_session_constructed;
    gobject_class->dispose      = spice_gtk_session_dispose;
    gobject_class->finalize     = spice_gtk_session_finalize;
    gobject_class->get_property = spice_gtk_session_get_property;
    gobject_class->set_property = spice_gtk_session_set_property;

    /**
     * SpiceGtkSession:session:
     *
     * #SpiceSession this #SpiceGtkSession is associated with
     *
     * Since: 0.8
     **/
    g_object_class_install_property
        (gobject_class, PROP_SESSION,
         g_param_spec_object("session",
                             "Session",
                             "SpiceSession",
                             SPICE_TYPE_SESSION,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS));

    /**
     * SpiceGtkSession:auto-clipboard:
     *
     * When this is true the clipboard gets automatically shared between host
     * and guest.
     *
     * Since: 0.8
     **/
    g_object_class_install_property
        (gobject_class, PROP_AUTO_CLIPBOARD,
         g_param_spec_boolean("auto-clipboard",
                              "Auto clipboard",
                              "Automatically relay clipboard changes between "
                              "host and guest.",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceGtkSession:auto-usbredir:
     *
     * Automatically redirect newly plugged in USB devices. Note the auto
     * redirection only happens when a #SpiceDisplay associated with the
     * session had keyboard focus.
     *
     * Since: 0.8
     **/
    g_object_class_install_property
        (gobject_class, PROP_AUTO_USBREDIR,
         g_param_spec_boolean("auto-usbredir",
                              "Auto USB Redirection",
                              "Automatically redirect newly plugged in USB"
                              "Devices to the guest.",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceGtkSession:pointer-grabbed:
     *
     * Returns %TRUE if the pointer is currently grabbed by this session.
     *
     * Since: 0.27
     **/
    g_object_class_install_property
        (gobject_class, PROP_POINTER_GRABBED,
         g_param_spec_boolean("pointer-grabbed",
                              "Pointer grabbed",
                              "Whether the pointer is grabbed",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceGtkSession:disable-copy-to-guest:
     *
     * When this is true the clipboard does not work from client to guest.
     *
     * Since: 0.29
     **/
    g_object_class_install_property
        (gobject_class, PROP_DISABLE_COPY_TO_GUEST,
         g_param_spec_boolean("disable-copy-to-guest",
                              "Disable copy to guest",
                              "Disable clipboard from client to guest.",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceGtkSession:disable-paste-from-guest:
     *
     * When this is true the clipboard does not work from guest to client.
     *
     * Since: 0.29
     **/
    g_object_class_install_property
        (gobject_class, PROP_DISABLE_PASTE_FROM_GUEST,
         g_param_spec_boolean("disable-paste-from-guest",
                              "Disable paste from guest",
                              "Disable clipboard from guest to client.",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceGtkSession:sync-modifiers:
     *
     * Automatically sync modifiers (Caps, Num and Scroll locks) with the guest.
     *
     * Since: 0.32
     **/
    g_object_class_install_property
        (gobject_class, PROP_SYNC_MODIFIERS,
         g_param_spec_boolean("sync-modifiers",
                              "Sync modifiers",
                              "Automatically sync modifiers",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_type_class_add_private(klass, sizeof(SpiceGtkSessionPrivate));
}

/* ---------------------------------------------------------------- */
/* private functions (clipboard related)                            */

static GtkClipboard* get_clipboard_from_selection(SpiceGtkSessionPrivate *s,
                                                  guint selection)
{
    if (selection == VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) {
        return s->clipboard;
    } else if (selection == VD_AGENT_CLIPBOARD_SELECTION_PRIMARY) {
        return s->clipboard_primary;
    } else {
        g_warning("Unhandled clipboard selection: %u", selection);
        return NULL;
    }
}

static gint get_selection_from_clipboard(SpiceGtkSessionPrivate *s,
                                         GtkClipboard* cb)
{
    if (cb == s->clipboard) {
        return VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD;
    } else if (cb == s->clipboard_primary) {
        return VD_AGENT_CLIPBOARD_SELECTION_PRIMARY;
    } else {
        g_warning("Unhandled clipboard");
        return -1;
    }
}

static const struct {
    const char  *xatom;
    uint32_t    vdagent;
} atom2agent[] = {
    {
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "UTF8_STRING",
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "text/plain;charset=utf-8"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "STRING"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "TEXT"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_UTF8_TEXT,
        .xatom   = "text/plain"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_PNG,
        .xatom   = "image/png"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/bmp"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/x-bmp"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/x-MS-bmp"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_BMP,
        .xatom   = "image/x-win-bitmap"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_TIFF,
        .xatom   = "image/tiff"
    },{
        .vdagent = VD_AGENT_CLIPBOARD_IMAGE_JPG,
        .xatom   = "image/jpeg"
    }
};

static GWeakRef* get_weak_ref(gpointer object)
{
    GWeakRef *weakref = g_new(GWeakRef, 1);
    g_weak_ref_init(weakref, object);
    return weakref;
}

static gpointer free_weak_ref(gpointer data)
{
    GWeakRef *weakref = data;
    gpointer object = g_weak_ref_get(weakref);

    g_weak_ref_clear(weakref);
    g_free(weakref);
    if (object != NULL) {
        /* The main reference still exists as object is not NULL, so we can
         * remove the strong reference given by g_weak_ref_get */
        g_object_unref(object);
    }
    return object;
}

static void clipboard_get_targets(GtkClipboard *clipboard,
                                  GdkAtom *atoms,
                                  gint n_atoms,
                                  gpointer user_data)
{
    SpiceGtkSession *self = free_weak_ref(user_data);

    SPICE_DEBUG("%s:", __FUNCTION__);

    if (self == NULL)
        return;

    g_return_if_fail(SPICE_IS_GTK_SESSION(self));

    if (atoms == NULL) {
        SPICE_DEBUG("Retrieving the clipboard data has failed");
        return;
    }

    SpiceGtkSessionPrivate *s = self->priv;
    guint32 types[SPICE_N_ELEMENTS(atom2agent)] = { 0 };
    gint num_types;
    int a;
    int selection;

    if (s->main == NULL)
        return;

    selection = get_selection_from_clipboard(s, clipboard);
    g_return_if_fail(selection != -1);

    if (s->clip_grabbed[selection]) {
        SPICE_DEBUG("Clipboard is already grabbed, ignoring %d atoms", n_atoms);
        return;
    }

    /* Set all Atoms that matches our current protocol implementation */
    num_types = 0;
    for (a = 0; a < n_atoms; a++) {
        guint m;
        gchar *name = gdk_atom_name(atoms[a]);

        SPICE_DEBUG(" \"%s\"", name);

        for (m = 0; m < SPICE_N_ELEMENTS(atom2agent); m++) {
            guint t;

            if (strcasecmp(name, atom2agent[m].xatom) != 0) {
                continue;
            }

            /* check if type is already in list */
            for (t = 0; t < num_types; t++) {
                if (types[t] == atom2agent[m].vdagent) {
                    break;
                }
            }

            if (t == num_types) {
                /* add type to empty slot */
                types[t] = atom2agent[m].vdagent;
                num_types++;
            }
        }
        g_free(name);
    }

    if (num_types == 0) {
        SPICE_DEBUG("No GdkAtoms will be sent from %d", n_atoms);
        return;
    }

    s->clip_grabbed[selection] = TRUE;

    if (spice_main_channel_agent_test_capability(s->main, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND))
        spice_main_channel_clipboard_selection_grab(s->main, selection, types, num_types);

    /* Sending a grab causes the agent to do an implicit release */
    s->nclip_targets[selection] = 0;
}

static void clipboard_owner_change(GtkClipboard        *clipboard,
                                   GdkEventOwnerChange *event,
                                   gpointer            user_data)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(user_data));

    SpiceGtkSession *self = user_data;
    SpiceGtkSessionPrivate *s = self->priv;
    int selection;

    selection = get_selection_from_clipboard(s, clipboard);
    g_return_if_fail(selection != -1);

    if (s->main == NULL)
        return;

    if (s->clip_grabbed[selection]) {
        s->clip_grabbed[selection] = FALSE;
        if (spice_main_channel_agent_test_capability(s->main, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND))
            spice_main_channel_clipboard_selection_release(s->main, selection);
    }

    switch (event->reason) {
    case GDK_OWNER_CHANGE_NEW_OWNER:
        if (gtk_clipboard_get_owner(clipboard) == G_OBJECT(self))
            break;

        s->clipboard_by_guest[selection] = FALSE;
        s->clip_hasdata[selection] = TRUE;
        if (s->auto_clipboard_enable && !read_only(self) && !s->disable_copy_to_guest)
            gtk_clipboard_request_targets(clipboard, clipboard_get_targets,
                                          get_weak_ref(self));
        break;
    default:
        s->clip_hasdata[selection] = FALSE;
        break;
    }
}

typedef struct
{
    SpiceGtkSession *self;
    GMainLoop *loop;
    GtkSelectionData *selection_data;
    guint info;
    guint selection;
} RunInfo;

static void clipboard_got_from_guest(SpiceMainChannel *main, guint selection,
                                     guint type, const guchar *data, guint size,
                                     gpointer user_data)
{
    RunInfo *ri = user_data;
    SpiceGtkSessionPrivate *s = ri->self->priv;
    gchar *conv = NULL;

    g_return_if_fail(selection == ri->selection);

    SPICE_DEBUG("clipboard got data");

    if (atom2agent[ri->info].vdagent == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
        /* on windows, gtk+ would already convert to LF endings, but
           not on unix */
        if (spice_main_channel_agent_test_capability(s->main, VD_AGENT_CAP_GUEST_LINEEND_CRLF)) {
            conv = spice_dos2unix((gchar*)data, size);
            size = strlen(conv);
        }

        gtk_selection_data_set_text(ri->selection_data, conv ?: (gchar*)data, size);
    } else {
        gtk_selection_data_set(ri->selection_data,
            gdk_atom_intern_static_string(atom2agent[ri->info].xatom),
            8, data, size);
    }

    if (g_main_loop_is_running (ri->loop))
        g_main_loop_quit (ri->loop);

    g_free(conv);
}

static void clipboard_agent_connected(RunInfo *ri)
{
    g_warning("agent status changed, cancel clipboard request");

    if (g_main_loop_is_running(ri->loop))
        g_main_loop_quit(ri->loop);
}

static void clipboard_get(GtkClipboard *clipboard,
                          GtkSelectionData *selection_data,
                          guint info, gpointer user_data)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(user_data));

    RunInfo ri = { NULL, };
    SpiceGtkSession *self = user_data;
    SpiceGtkSessionPrivate *s = self->priv;
    gboolean agent_connected = FALSE;
    gulong clipboard_handler;
    gulong agent_handler;
    int selection;

    SPICE_DEBUG("clipboard get");

    selection = get_selection_from_clipboard(s, clipboard);
    g_return_if_fail(selection != -1);
    g_return_if_fail(info < SPICE_N_ELEMENTS(atom2agent));
    g_return_if_fail(s->main != NULL);

    ri.selection_data = selection_data;
    ri.info = info;
    ri.loop = g_main_loop_new(NULL, FALSE);
    ri.selection = selection;
    ri.self = self;

    clipboard_handler = g_signal_connect(s->main, "main-clipboard-selection",
                                         G_CALLBACK(clipboard_got_from_guest),
                                         &ri);
    agent_handler = g_signal_connect_swapped(s->main, "notify::agent-connected",
                                     G_CALLBACK(clipboard_agent_connected),
                                     &ri);

    spice_main_channel_clipboard_selection_request(s->main, selection,
                                                   atom2agent[info].vdagent);


    g_object_get(s->main, "agent-connected", &agent_connected, NULL);
    if (!agent_connected) {
        SPICE_DEBUG("canceled clipboard_get, before running loop");
        goto cleanup;
    }

    /* This is modeled on the implementation of gtk_dialog_run() even though
     * these thread functions are deprecated and appears to be needed to avoid
     * dead-lock from gtk_dialog_run().
     */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gdk_threads_leave();
    g_main_loop_run(ri.loop);
    gdk_threads_enter();
    G_GNUC_END_IGNORE_DEPRECATIONS

cleanup:
    g_clear_pointer(&ri.loop, g_main_loop_unref);
    g_signal_handler_disconnect(s->main, clipboard_handler);
    g_signal_handler_disconnect(s->main, agent_handler);
}

static void clipboard_clear(GtkClipboard *clipboard, gpointer user_data)
{
    SPICE_DEBUG("clipboard_clear");
    /* We watch for clipboard ownership changes and act on those, so we
       don't need to do anything here */
}

static gboolean clipboard_grab(SpiceMainChannel *main, guint selection,
                               guint32* types, guint32 ntypes,
                               gpointer user_data)
{
    g_return_val_if_fail(SPICE_IS_GTK_SESSION(user_data), FALSE);

    SpiceGtkSession *self = user_data;
    SpiceGtkSessionPrivate *s = self->priv;
    GtkTargetEntry targets[SPICE_N_ELEMENTS(atom2agent)];
    gboolean target_selected[SPICE_N_ELEMENTS(atom2agent)] = { FALSE, };
    gboolean found;
    GtkClipboard* cb;
    int m, n, i;

    cb = get_clipboard_from_selection(s, selection);
    g_return_val_if_fail(cb != NULL, FALSE);

    i = 0;
    for (n = 0; n < ntypes; ++n) {
        found = FALSE;
        for (m = 0; m < SPICE_N_ELEMENTS(atom2agent); m++) {
            if (atom2agent[m].vdagent == types[n] && !target_selected[m]) {
                found = TRUE;
                g_return_val_if_fail(i < SPICE_N_ELEMENTS(atom2agent), FALSE);
                targets[i].target = (gchar*)atom2agent[m].xatom;
                targets[i].info = m;
                target_selected[m] = TRUE;
                i += 1;
            }
        }
        if (!found) {
            g_warning("clipboard: couldn't find a matching type for: %u",
                      types[n]);
        }
    }

    g_free(s->clip_targets[selection]);
    s->nclip_targets[selection] = i;
    s->clip_targets[selection] = g_memdup(targets, sizeof(GtkTargetEntry) * i);
    /* Receiving a grab implies we've released our own grab */
    s->clip_grabbed[selection] = FALSE;

    if (read_only(self) ||
        !s->auto_clipboard_enable ||
        s->disable_paste_from_guest ||
        s->nclip_targets[selection] == 0)
        goto skip_grab_clipboard;

    if (!gtk_clipboard_set_with_owner(cb, targets, i,
                                      clipboard_get, clipboard_clear, G_OBJECT(self))) {
        g_warning("clipboard grab failed");
        return FALSE;
    }
    s->clipboard_by_guest[selection] = TRUE;
    s->clip_hasdata[selection] = FALSE;

skip_grab_clipboard:
    return TRUE;
}

static gboolean check_clipboard_size_limits(SpiceGtkSession *session,
                                            gint clipboard_len)
{
    int max_clipboard;

    g_object_get(session->priv->main, "max-clipboard", &max_clipboard, NULL);
    if (max_clipboard != -1 && clipboard_len > max_clipboard) {
        g_warning("discarded clipboard of size %d (max: %d)",
                  clipboard_len, max_clipboard);
        return FALSE;
    } else if (clipboard_len <= 0) {
        SPICE_DEBUG("discarding empty clipboard");
        return FALSE;
    }

    return TRUE;
}

/* This will convert line endings if needed (between Windows/Unix conventions),
 * and will make sure 'len' does not take into account any trailing \0 as this could
 * cause some confusion guest side.
 * The 'len' argument will be modified by this function to the length of the modified
 * string
 */
static char *fixup_clipboard_text(SpiceGtkSession *self, const char *text, int *len)
{
    char *conv = NULL;

    if (spice_main_channel_agent_test_capability(self->priv->main,
                                                 VD_AGENT_CAP_GUEST_LINEEND_CRLF)) {
        conv = spice_unix2dos(text, *len);
        *len = strlen(conv);
    } else {
        /* On Windows, with some versions of gtk+, GtkSelectionData::length
         * will include the final '\0'. When a string with this trailing '\0'
         * is pasted in some linux applications, it will be pasted as <NIL> or
         * as an invisible character, which is unwanted. Ensure the length we
         * send to the agent does not include any trailing '\0'
         * This is gtk+ bug https://bugzilla.gnome.org/show_bug.cgi?id=734670
         */
        *len = strlen(text);
    }

    return conv;
}

static void clipboard_received_text_cb(GtkClipboard *clipboard,
                                       const gchar *text,
                                       gpointer user_data)
{
    SpiceGtkSession *self = free_weak_ref(user_data);
    char *conv = NULL;
    int len = 0;
    int selection;
    const guchar *data = NULL;

    if (self == NULL)
        return;

    selection = get_selection_from_clipboard(self->priv, clipboard);
    g_return_if_fail(selection != -1);

    if (text == NULL) {
        SPICE_DEBUG("Failed to retrieve clipboard text");
        goto notify_agent;
    }

    g_return_if_fail(SPICE_IS_GTK_SESSION(self));

    len = strlen(text);
    if (!check_clipboard_size_limits(self, len)) {
        SPICE_DEBUG("Failed size limits of clipboard text (%d bytes)", len);
        goto notify_agent;
    }

    /* gtk+ internal utf8 newline is always LF, even on windows */
    conv = fixup_clipboard_text(self, text, &len);
    if (!check_clipboard_size_limits(self, len)) {
        SPICE_DEBUG("Failed size limits of clipboard text (%d bytes)", len);
        goto notify_agent;
    }

    data = (const guchar *) (conv != NULL ? conv : text);
notify_agent:
    spice_main_channel_clipboard_selection_notify(self->priv->main, selection,
                                                  VD_AGENT_CLIPBOARD_UTF8_TEXT,
                                                  data,
                                                  (data != NULL) ? len : 0);
    g_free(conv);
}

static void clipboard_received_cb(GtkClipboard *clipboard,
                                  GtkSelectionData *selection_data,
                                  gpointer user_data)
{
    SpiceGtkSession *self = free_weak_ref(user_data);

    if (self == NULL)
        return;

    g_return_if_fail(SPICE_IS_GTK_SESSION(self));

    SpiceGtkSessionPrivate *s = self->priv;
    gint len = 0, m;
    guint32 type = VD_AGENT_CLIPBOARD_NONE;
    gchar* name;
    GdkAtom atom;
    int selection;

    selection = get_selection_from_clipboard(s, clipboard);
    g_return_if_fail(selection != -1);

    len = gtk_selection_data_get_length(selection_data);
    if (!check_clipboard_size_limits(self, len)) {
        return;
    } else {
        atom = gtk_selection_data_get_data_type(selection_data);
        name = gdk_atom_name(atom);
        for (m = 0; m < SPICE_N_ELEMENTS(atom2agent); m++) {
            if (strcasecmp(name, atom2agent[m].xatom) == 0) {
                break;
            }
        }

        if (m >= SPICE_N_ELEMENTS(atom2agent)) {
            g_warning("clipboard_received for unsupported type: %s", name);
        } else {
            type = atom2agent[m].vdagent;
        }

        g_free(name);
    }

    const guchar *data = gtk_selection_data_get_data(selection_data);

    /* text should be handled through clipboard_received_text_cb(), not
     * clipboard_received_cb().
     */
    g_warn_if_fail(type != VD_AGENT_CLIPBOARD_UTF8_TEXT);

    spice_main_channel_clipboard_selection_notify(s->main, selection, type, data, len);
}

static gboolean clipboard_request(SpiceMainChannel *main, guint selection,
                                  guint type, gpointer user_data)
{
    g_return_val_if_fail(SPICE_IS_GTK_SESSION(user_data), FALSE);

    SpiceGtkSession *self = user_data;
    SpiceGtkSessionPrivate *s = self->priv;
    GdkAtom atom;
    GtkClipboard* cb;
    int m;

    g_return_val_if_fail(s->clipboard_by_guest[selection] == FALSE, FALSE);
    g_return_val_if_fail(s->clip_grabbed[selection], FALSE);

    if (read_only(self))
        return FALSE;

    cb = get_clipboard_from_selection(s, selection);
    g_return_val_if_fail(cb != NULL, FALSE);

    if (type == VD_AGENT_CLIPBOARD_UTF8_TEXT) {
        gtk_clipboard_request_text(cb, clipboard_received_text_cb,
                                   get_weak_ref(self));
    } else {
        for (m = 0; m < SPICE_N_ELEMENTS(atom2agent); m++) {
            if (atom2agent[m].vdagent == type)
                break;
        }

        g_return_val_if_fail(m < SPICE_N_ELEMENTS(atom2agent), FALSE);

        atom = gdk_atom_intern_static_string(atom2agent[m].xatom);
        gtk_clipboard_request_contents(cb, atom, clipboard_received_cb,
                                       get_weak_ref(self));
    }

    return TRUE;
}

static void clipboard_release(SpiceMainChannel *main, guint selection,
                              gpointer user_data)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(user_data));

    SpiceGtkSession *self = user_data;
    SpiceGtkSessionPrivate *s = self->priv;
    GtkClipboard* clipboard = get_clipboard_from_selection(s, selection);

    if (!clipboard)
        return;

    s->nclip_targets[selection] = 0;

    if (!s->clipboard_by_guest[selection])
        return;
    gtk_clipboard_clear(clipboard);
    s->clipboard_by_guest[selection] = FALSE;
}

static void channel_new(SpiceSession *session, SpiceChannel *channel,
                        gpointer user_data)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(user_data));

    SpiceGtkSession *self = user_data;
    SpiceGtkSessionPrivate *s = self->priv;

    if (SPICE_IS_MAIN_CHANNEL(channel)) {
        SPICE_DEBUG("Changing main channel from %p to %p", s->main, channel);
        s->main = SPICE_MAIN_CHANNEL(channel);
        g_signal_connect(channel, "main-clipboard-selection-grab",
                         G_CALLBACK(clipboard_grab), self);
        g_signal_connect(channel, "main-clipboard-selection-request",
                         G_CALLBACK(clipboard_request), self);
        g_signal_connect(channel, "main-clipboard-selection-release",
                         G_CALLBACK(clipboard_release), self);
    }
    if (SPICE_IS_INPUTS_CHANNEL(channel)) {
        spice_gtk_session_sync_keyboard_modifiers_for_channel(self, SPICE_INPUTS_CHANNEL(channel), TRUE);
    }
}

static void channel_destroy(SpiceSession *session, SpiceChannel *channel,
                            gpointer user_data)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(user_data));

    SpiceGtkSession *self = user_data;
    SpiceGtkSessionPrivate *s = self->priv;
    guint i;

    if (SPICE_IS_MAIN_CHANNEL(channel) && SPICE_MAIN_CHANNEL(channel) == s->main) {
        s->main = NULL;
        for (i = 0; i < CLIPBOARD_LAST; ++i) {
            if (s->clipboard_by_guest[i]) {
                GtkClipboard *cb = get_clipboard_from_selection(s, i);
                if (cb)
                    gtk_clipboard_clear(cb);
                s->clipboard_by_guest[i] = FALSE;
            }
            s->clip_grabbed[i] = FALSE;
            s->nclip_targets[i] = 0;
        }
    }
}

static gboolean read_only(SpiceGtkSession *self)
{
    return spice_session_get_read_only(self->priv->session);
}

/* ---------------------------------------------------------------- */
/* private functions (usbredir related)                             */
G_GNUC_INTERNAL
void spice_gtk_session_request_auto_usbredir(SpiceGtkSession *self,
                                             gboolean state)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(self));

    SpiceGtkSessionPrivate *s = self->priv;
    SpiceDesktopIntegration *desktop_int;
    SpiceUsbDeviceManager *manager;

    if (state) {
        s->auto_usbredir_reqs++;
        if (s->auto_usbredir_reqs != 1)
            return;
    } else {
        g_return_if_fail(s->auto_usbredir_reqs > 0);
        s->auto_usbredir_reqs--;
        if (s->auto_usbredir_reqs != 0)
            return;
    }

    if (!s->auto_usbredir_enable)
        return;

    manager = spice_usb_device_manager_get(s->session, NULL);
    if (!manager)
        return;

    g_object_set(manager, "auto-connect", state, NULL);

    desktop_int = spice_desktop_integration_get(s->session);
    if (state)
        spice_desktop_integration_inhibit_automount(desktop_int);
    else
        spice_desktop_integration_uninhibit_automount(desktop_int);
}

/* ------------------------------------------------------------------ */
/* public functions                                                   */

/**
 * spice_gtk_session_get:
 * @session: #SpiceSession for which to get the #SpiceGtkSession
 *
 * Gets the #SpiceGtkSession associated with the passed in #SpiceSession.
 * A new #SpiceGtkSession instance will be created the first time this
 * function is called for a certain #SpiceSession.
 *
 * Note that this function returns a weak reference, which should not be used
 * after the #SpiceSession itself has been unref-ed by the caller.
 *
 * Returns: (transfer none): a weak reference to the #SpiceGtkSession associated with the passed in #SpiceSession
 *
 * Since 0.8
 **/
SpiceGtkSession *spice_gtk_session_get(SpiceSession *session)
{
    g_return_val_if_fail(SPICE_IS_SESSION(session), NULL);

    SpiceGtkSession *self;
    static GMutex mutex;

    g_mutex_lock(&mutex);
    self = g_object_get_data(G_OBJECT(session), "spice-gtk-session");
    if (self == NULL) {
        self = g_object_new(SPICE_TYPE_GTK_SESSION, "session", session, NULL);
        g_object_set_data_full(G_OBJECT(session), "spice-gtk-session", self, g_object_unref);
    }
    g_mutex_unlock(&mutex);

    return SPICE_GTK_SESSION(self);
}

/**
 * spice_gtk_session_copy_to_guest:
 * @self: #SpiceGtkSession
 *
 * Copy client-side clipboard to guest clipboard.
 *
 * Since 0.8
 **/
void spice_gtk_session_copy_to_guest(SpiceGtkSession *self)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(self));
    g_return_if_fail(read_only(self) == FALSE);

    SpiceGtkSessionPrivate *s = self->priv;
    if (s->disable_copy_to_guest) return;
    int selection = VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD;

    if (s->clip_hasdata[selection] && !s->clip_grabbed[selection]) {
        gtk_clipboard_request_targets(s->clipboard, clipboard_get_targets,
                                      get_weak_ref(self));
    }
}

/**
 * spice_gtk_session_paste_from_guest:
 * @self: #SpiceGtkSession
 *
 * Copy guest clipboard to client-side clipboard.
 *
 * Since 0.8
 **/
void spice_gtk_session_paste_from_guest(SpiceGtkSession *self)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(self));
    g_return_if_fail(read_only(self) == FALSE);

    SpiceGtkSessionPrivate *s = self->priv;
    if (s->disable_paste_from_guest) return;
    int selection = VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD;

    if (s->nclip_targets[selection] == 0) {
        g_warning("Guest clipboard is not available.");
        return;
    }

    if (!gtk_clipboard_set_with_owner(s->clipboard, s->clip_targets[selection], s->nclip_targets[selection],
                                      clipboard_get, clipboard_clear, G_OBJECT(self))) {
        g_warning("Clipboard grab failed");
        return;
    }
    s->clipboard_by_guest[selection] = TRUE;
    s->clip_hasdata[selection] = FALSE;
}

G_GNUC_INTERNAL
void spice_gtk_session_sync_keyboard_modifiers(SpiceGtkSession *self)
{
    GList *l = NULL, *channels = spice_session_get_channels(self->priv->session);

    for (l = channels; l != NULL; l = l->next) {
        if (SPICE_IS_INPUTS_CHANNEL(l->data)) {
            SpiceInputsChannel *inputs = SPICE_INPUTS_CHANNEL(l->data);
            spice_gtk_session_sync_keyboard_modifiers_for_channel(self, inputs, TRUE);
        }
    }
    g_list_free(channels);
}

G_GNUC_INTERNAL
void spice_gtk_session_set_pointer_grabbed(SpiceGtkSession *self, gboolean grabbed)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(self));

    self->priv->pointer_grabbed = grabbed;
    g_object_notify(G_OBJECT(self), "pointer-grabbed");
}

G_GNUC_INTERNAL
gboolean spice_gtk_session_get_pointer_grabbed(SpiceGtkSession *self)
{
    g_return_val_if_fail(SPICE_IS_GTK_SESSION(self), FALSE);

    return self->priv->pointer_grabbed;
}

G_GNUC_INTERNAL
void spice_gtk_session_set_keyboard_has_focus(SpiceGtkSession *self,
                                                gboolean keyboard_has_focus)
{
    g_return_if_fail(SPICE_IS_GTK_SESSION(self));

    self->priv->keyboard_has_focus = keyboard_has_focus;
}

G_GNUC_INTERNAL
void spice_gtk_session_set_mouse_has_pointer(SpiceGtkSession *self,
                                                gboolean mouse_has_pointer)
{
   g_return_if_fail(SPICE_IS_GTK_SESSION(self));
   self->priv->mouse_has_pointer = mouse_has_pointer;
}

G_GNUC_INTERNAL
gboolean spice_gtk_session_get_keyboard_has_focus(SpiceGtkSession *self)
{
    g_return_val_if_fail(SPICE_IS_GTK_SESSION(self), FALSE);

    return self->priv->keyboard_has_focus;
}

G_GNUC_INTERNAL
gboolean spice_gtk_session_get_mouse_has_pointer(SpiceGtkSession *self)
{
    g_return_val_if_fail(SPICE_IS_GTK_SESSION(self), FALSE);

    return self->priv->mouse_has_pointer;
}
