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
#include <spice/vd_agent.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "spice-client.h"
#include "spice-common.h"
#include "spice-marshal.h"
#include "port-forward.h"

#include "spice-util-priv.h"
#include "spice-channel-priv.h"
#include "spice-session-priv.h"
#include "spice-audio-priv.h"
#include "spice-file-transfer-task-priv.h"

/**
 * SECTION:channel-main
 * @short_description: the main Spice channel
 * @title: Main Channel
 * @section_id:
 * @see_also: #SpiceChannel, and the GTK widget #SpiceDisplay
 * @stability: Stable
 * @include: spice-client.h
 *
 * The main channel is the Spice session control channel. It handles
 * communication initialization (channels list), migrations, mouse
 * modes, multimedia time, and agent communication.
 *
 *
 */

#define MAX_DISPLAY 16 /* Note must fit in a guint32, see monitors_align */

typedef struct spice_migrate spice_migrate;

typedef enum {
    DISPLAY_UNDEFINED,
    DISPLAY_DISABLED,
    DISPLAY_ENABLED,
} SpiceDisplayState;

typedef struct {
    int                     x;
    int                     y;
    int                     width;
    int                     height;
    SpiceDisplayState       display_state;
} SpiceDisplayConfig;

typedef struct {
    GHashTable                 *xfer_task;
    SpiceMainChannel           *channel;
    GFileProgressCallback       progress_callback;
    gpointer                    progress_callback_data;
    GTask                      *task;
    struct {
        goffset                 total_sent;
        goffset                 transfer_size;
        guint                   num_files;
        guint                   succeed;
        guint                   cancelled;
        guint                   failed;
    } stats;
} FileTransferOperation;

struct _SpiceMainChannelPrivate  {
    enum SpiceMouseMode         mouse_mode;
    enum SpiceMouseMode         requested_mouse_mode;
    bool                        agent_connected;
    bool                        agent_caps_received;

    gboolean                    agent_display_config_sent;
    guint8                      display_color_depth;
    gboolean                    display_disable_wallpaper:1;
    gboolean                    display_disable_font_smooth:1;
    gboolean                    display_disable_animation:1;
    gboolean                    disable_display_position:1;
    gboolean                    disable_display_align:1;

    int                         agent_tokens;
    VDAgentMessage              agent_msg; /* partial msg reconstruction */
    guint8                      *agent_msg_data;
    guint                       agent_msg_pos;
    uint8_t                     agent_msg_size;
    uint32_t                    agent_caps[VD_AGENT_CAPS_SIZE];
    SpiceDisplayConfig          display[MAX_DISPLAY];
    gint                        timer_id;
    GQueue                      *agent_msg_queue;
    GHashTable                  *file_xfer_tasks;
    GHashTable                  *flushing;
    PortForwarder               *port_forwarder;

    guint                       switch_host_delayed_id;
    guint                       migrate_delayed_id;
    spice_migrate               *migrate_data;
    int                         max_clipboard;

    gboolean                    agent_volume_playback_sync;
    gboolean                    agent_volume_record_sync;
    GCancellable                *cancellable_volume_info;
};

struct spice_migrate {
    struct coroutine *from;
    SpiceMigrationDstInfo *info;
    SpiceSession *session;
    guint nchannels;
    SpiceChannel *src_channel;
    SpiceChannel *dst_channel;
    bool do_seamless; /* used as input and output for the seamless migration handshake.
                         input: whether to send to the dest SPICE_MSGC_MAIN_MIGRATE_DST_DO_SEAMLESS
                         output: whether the dest approved seamless migration
                         (SPICE_MSG_MAIN_MIGRATE_DST_SEAMLESS_ACK/NACK)
                       */
    uint32_t src_mig_version;
};

G_DEFINE_TYPE_WITH_PRIVATE(SpiceMainChannel, spice_main_channel, SPICE_TYPE_CHANNEL)

/* Properties */
enum {
    PROP_0,
    PROP_MOUSE_MODE,
    PROP_AGENT_CONNECTED,
    PROP_AGENT_CAPS_0,
    PROP_DISPLAY_DISABLE_WALLPAPER,
    PROP_DISPLAY_DISABLE_FONT_SMOOTH,
    PROP_DISPLAY_DISABLE_ANIMATION,
    PROP_DISPLAY_COLOR_DEPTH,
    PROP_DISABLE_DISPLAY_POSITION,
    PROP_DISABLE_DISPLAY_ALIGN,
    PROP_MAX_CLIPBOARD,
};

/* Signals */
enum {
    SPICE_MAIN_MOUSE_UPDATE,
    SPICE_MAIN_AGENT_UPDATE,
    SPICE_MAIN_CLIPBOARD,
    SPICE_MAIN_CLIPBOARD_GRAB,
    SPICE_MAIN_CLIPBOARD_REQUEST,
    SPICE_MAIN_CLIPBOARD_RELEASE,
    SPICE_MAIN_CLIPBOARD_SELECTION,
    SPICE_MAIN_CLIPBOARD_SELECTION_GRAB,
    SPICE_MAIN_CLIPBOARD_SELECTION_REQUEST,
    SPICE_MAIN_CLIPBOARD_SELECTION_RELEASE,
    SPICE_MIGRATION_STARTED,
    SPICE_MAIN_NEW_FILE_TRANSFER,
    SPICE_MAIN_LAST_SIGNAL,
};

static guint signals[SPICE_MAIN_LAST_SIGNAL];

static void spice_main_handle_msg(SpiceChannel *channel, SpiceMsgIn *msg);
static void channel_set_handlers(SpiceChannelClass *klass);
static void agent_send_msg_queue(SpiceMainChannel *channel);
static void agent_free_msg_queue(SpiceMainChannel *channel);
static void migrate_channel_event_cb(SpiceChannel *channel, SpiceChannelEvent event,
                                     gpointer data);
static gboolean main_migrate_handshake_done(gpointer data);
static void spice_main_channel_send_migration_handshake(SpiceChannel *channel);
static void file_xfer_flushed(SpiceMainChannel *channel, gboolean success);
static void file_xfer_read_async_cb(GObject *source_object,
                                    GAsyncResult *res,
                                    gpointer user_data);
static void spice_main_set_max_clipboard(SpiceMainChannel *self, gint max);
static void set_agent_connected(SpiceMainChannel *channel, gboolean connected);
static void agent_send_port_redirections(SpiceMainChannel *channel);

static void file_transfer_operation_free(FileTransferOperation *xfer_op);
static void spice_main_channel_reset_all_xfer_operations(SpiceMainChannel *channel);
static SpiceFileTransferTask *spice_main_channel_find_xfer_task_by_task_id(SpiceMainChannel *channel,
                                                                           guint32 task_id);
static void file_transfer_operation_task_finished(SpiceFileTransferTask *xfer_task,
                                                  GError *error,
                                                  gpointer userdata);
static void file_transfer_operation_send_progress(SpiceFileTransferTask *xfer_task);

/* ------------------------------------------------------------------ */

static const char *agent_msg_types[] = {
    [ VD_AGENT_MOUSE_STATE             ] = "mouse state",
    [ VD_AGENT_MONITORS_CONFIG         ] = "monitors config",
    [ VD_AGENT_REPLY                   ] = "reply",
    [ VD_AGENT_CLIPBOARD               ] = "clipboard",
    [ VD_AGENT_DISPLAY_CONFIG          ] = "display config",
    [ VD_AGENT_ANNOUNCE_CAPABILITIES   ] = "announce caps",
    [ VD_AGENT_CLIPBOARD_GRAB          ] = "clipboard grab",
    [ VD_AGENT_CLIPBOARD_REQUEST       ] = "clipboard request",
    [ VD_AGENT_CLIPBOARD_RELEASE       ] = "clipboard release",
    [ VD_AGENT_AUDIO_VOLUME_SYNC       ] = "volume-sync",
};

static const char *agent_caps[] = {
    [ VD_AGENT_CAP_MOUSE_STATE         ] = "mouse state",
    [ VD_AGENT_CAP_MONITORS_CONFIG     ] = "monitors config",
    [ VD_AGENT_CAP_REPLY               ] = "reply",
    [ VD_AGENT_CAP_CLIPBOARD           ] = "clipboard (old)",
    [ VD_AGENT_CAP_DISPLAY_CONFIG      ] = "display config",
    [ VD_AGENT_CAP_CLIPBOARD_BY_DEMAND ] = "clipboard",
    [ VD_AGENT_CAP_CLIPBOARD_SELECTION ] = "clipboard selection",
    [ VD_AGENT_CAP_SPARSE_MONITORS_CONFIG ] = "sparse monitors",
    [ VD_AGENT_CAP_GUEST_LINEEND_LF    ] = "line-end lf",
    [ VD_AGENT_CAP_GUEST_LINEEND_CRLF  ] = "line-end crlf",
    [ VD_AGENT_CAP_MAX_CLIPBOARD       ] = "max-clipboard",
    [ VD_AGENT_CAP_AUDIO_VOLUME_SYNC   ] = "volume-sync",
    [ VD_AGENT_CAP_PORT_FORWARDING     ] = "port-forwarding",
    [ VD_AGENT_CAP_MONITORS_CONFIG_POSITION ] = "monitors config position",
    [ VD_AGENT_CAP_FILE_XFER_DISABLED ] = "file transfer disabled",
};
#define NAME(_a, _i) ((_i) < SPICE_N_ELEMENTS(_a) ? (_a[(_i)] ?: "?") : "?")

/* ------------------------------------------------------------------ */

static gboolean test_agent_cap(SpiceMainChannel *channel, guint32 cap)
{
    SpiceMainChannelPrivate *c = channel->priv;

    if (!c->agent_caps_received)
        return FALSE;

    return VD_AGENT_HAS_CAPABILITY(c->agent_caps, G_N_ELEMENTS(c->agent_caps), cap);
}

static void spice_main_channel_reset_capabilties(SpiceChannel *channel)
{
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_MAIN_CAP_SEMI_SEAMLESS_MIGRATE);
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_MAIN_CAP_NAME_AND_UUID);
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_MAIN_CAP_AGENT_CONNECTED_TOKENS);
    spice_channel_set_capability(SPICE_CHANNEL(channel), SPICE_MAIN_CAP_SEAMLESS_MIGRATE);
}

static void port_forwarder_send_command(void *channel, uint32_t command,
                                        const uint8_t *data, uint32_t data_size);

static void spice_main_channel_init(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c;

    c = channel->priv = spice_main_channel_get_instance_private(channel);
    c->agent_msg_queue = g_queue_new();
    c->file_xfer_tasks = g_hash_table_new(g_direct_hash, g_direct_equal);
    c->flushing = g_hash_table_new(g_direct_hash, g_direct_equal);
    c->cancellable_volume_info = g_cancellable_new();
    c->port_forwarder = new_port_forwarder(channel, port_forwarder_send_command);

    spice_main_channel_reset_capabilties(SPICE_CHANNEL(channel));
    c->requested_mouse_mode = SPICE_MOUSE_MODE_CLIENT;
}

static gint spice_main_get_max_clipboard(SpiceMainChannel *self)
{
    g_return_val_if_fail(SPICE_IS_MAIN_CHANNEL(self), 0);

    if (g_getenv("SPICE_MAX_CLIPBOARD"))
        return atoi(g_getenv("SPICE_MAX_CLIPBOARD"));

    return self->priv->max_clipboard;
}

static void spice_main_get_property(GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    SpiceMainChannel *self = SPICE_MAIN_CHANNEL(object);
    SpiceMainChannelPrivate *c = self->priv;

    switch (prop_id) {
    case PROP_MOUSE_MODE:
        g_value_set_int(value, c->mouse_mode);
	break;
    case PROP_AGENT_CONNECTED:
        g_value_set_boolean(value, c->agent_connected);
	break;
    case PROP_AGENT_CAPS_0:
        g_value_set_int(value, c->agent_caps[0]);
	break;
    case PROP_DISPLAY_DISABLE_WALLPAPER:
        g_value_set_boolean(value, c->display_disable_wallpaper);
        break;
    case PROP_DISPLAY_DISABLE_FONT_SMOOTH:
        g_value_set_boolean(value, c->display_disable_font_smooth);
        break;
    case PROP_DISPLAY_DISABLE_ANIMATION:
        g_value_set_boolean(value, c->display_disable_animation);
        break;
    case PROP_DISPLAY_COLOR_DEPTH:
        g_value_set_uint(value, c->display_color_depth);
        break;
    case PROP_DISABLE_DISPLAY_POSITION:
        g_value_set_boolean(value, c->disable_display_position);
        break;
    case PROP_DISABLE_DISPLAY_ALIGN:
        g_value_set_boolean(value, c->disable_display_align);
        break;
    case PROP_MAX_CLIPBOARD:
        g_value_set_int(value, spice_main_get_max_clipboard(self));
        break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	break;
    }
}

static void spice_main_set_property(GObject *gobject, guint prop_id,
                                    const GValue *value, GParamSpec *pspec)
{
    SpiceMainChannel *self = SPICE_MAIN_CHANNEL(gobject);
    SpiceMainChannelPrivate *c = self->priv;

    switch (prop_id) {
    case PROP_DISPLAY_DISABLE_WALLPAPER:
        c->display_disable_wallpaper = g_value_get_boolean(value);
        break;
    case PROP_DISPLAY_DISABLE_FONT_SMOOTH:
        c->display_disable_font_smooth = g_value_get_boolean(value);
        break;
    case PROP_DISPLAY_DISABLE_ANIMATION:
        c->display_disable_animation = g_value_get_boolean(value);
        break;
    case PROP_DISPLAY_COLOR_DEPTH: {
        guint color_depth = g_value_get_uint(value);
        g_return_if_fail(color_depth % 8 == 0);
        c->display_color_depth = color_depth;
        break;
    }
    case PROP_DISABLE_DISPLAY_POSITION:
        c->disable_display_position = g_value_get_boolean(value);
        break;
    case PROP_DISABLE_DISPLAY_ALIGN:
        c->disable_display_align = g_value_get_boolean(value);
        break;
    case PROP_MAX_CLIPBOARD:
        spice_main_set_max_clipboard(self, g_value_get_int(value));
        break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
	break;
    }
}

static void spice_main_channel_dispose(GObject *obj)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(obj)->priv;

    if (c->timer_id) {
        g_source_remove(c->timer_id);
        c->timer_id = 0;
    }

    if (c->switch_host_delayed_id) {
        g_source_remove(c->switch_host_delayed_id);
        c->switch_host_delayed_id = 0;
    }

    if (c->migrate_delayed_id) {
        g_source_remove(c->migrate_delayed_id);
        c->migrate_delayed_id = 0;
    }

    g_clear_pointer(&c->file_xfer_tasks, g_hash_table_unref);
    g_clear_pointer (&c->flushing, g_hash_table_unref);
    if (c->port_forwarder)
        delete_port_forwarder(c->port_forwarder);

    g_cancellable_cancel(c->cancellable_volume_info);
    g_clear_object(&c->cancellable_volume_info);

    if (G_OBJECT_CLASS(spice_main_channel_parent_class)->dispose)
        G_OBJECT_CLASS(spice_main_channel_parent_class)->dispose(obj);
}

static void spice_main_channel_finalize(GObject *obj)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(obj)->priv;

    g_free(c->agent_msg_data);
    agent_free_msg_queue(SPICE_MAIN_CHANNEL(obj));

    if (G_OBJECT_CLASS(spice_main_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_main_channel_parent_class)->finalize(obj);
}

/* coroutine context */
static void spice_channel_iterate_write(SpiceChannel *channel)
{
    agent_send_msg_queue(SPICE_MAIN_CHANNEL(channel));

    if (SPICE_CHANNEL_CLASS(spice_main_channel_parent_class)->iterate_write)
        SPICE_CHANNEL_CLASS(spice_main_channel_parent_class)->iterate_write(channel);
}

/* main or coroutine context */
static void spice_main_channel_reset_agent(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c = channel->priv;

    c->agent_connected = FALSE;
    c->agent_caps_received = FALSE;
    c->agent_display_config_sent = FALSE;
    c->agent_msg_pos = 0;
    g_clear_pointer(&c->agent_msg_data, g_free);
    c->agent_msg_size = 0;

    spice_main_channel_reset_all_xfer_operations(channel);
    file_xfer_flushed(channel, FALSE);
    port_forwarder_agent_disconnected(c->port_forwarder);
}

/* main or coroutine context */
static void spice_main_channel_reset(SpiceChannel *channel, gboolean migrating)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;

    /* This is not part of reset_agent, since the spice-server expects any
       pending multi-chunk messages to be completed by the client, even after
       it has send an agent-disconnected msg as that is what the original
       spicec did. Also see the TODO in server/reds.c reds_reset_vdp() */
    c->agent_tokens = 0;
    agent_free_msg_queue(SPICE_MAIN_CHANNEL(channel));
    c->agent_msg_queue = g_queue_new();

    c->agent_volume_playback_sync = FALSE;
    c->agent_volume_record_sync = FALSE;

    set_agent_connected(SPICE_MAIN_CHANNEL(channel), FALSE);

    SPICE_CHANNEL_CLASS(spice_main_channel_parent_class)->channel_reset(channel, migrating);
}

static void spice_main_constructed(GObject *object)
{
    SpiceMainChannel *self = SPICE_MAIN_CHANNEL(object);
    SpiceMainChannelPrivate *c = self->priv;

    /* update default value */
    c->max_clipboard = spice_main_get_max_clipboard(self);

    if (G_OBJECT_CLASS(spice_main_channel_parent_class)->constructed)
        G_OBJECT_CLASS(spice_main_channel_parent_class)->constructed(object);
}

static void spice_main_channel_class_init(SpiceMainChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->dispose      = spice_main_channel_dispose;
    gobject_class->finalize     = spice_main_channel_finalize;
    gobject_class->get_property = spice_main_get_property;
    gobject_class->set_property = spice_main_set_property;
    gobject_class->constructed  = spice_main_constructed;

    channel_class->handle_msg    = spice_main_handle_msg;
    channel_class->iterate_write = spice_channel_iterate_write;
    channel_class->channel_reset = spice_main_channel_reset;
    channel_class->channel_reset_capabilities = spice_main_channel_reset_capabilties;
    channel_class->channel_send_migration_handshake = spice_main_channel_send_migration_handshake;

    /**
     * SpiceMainChannel:mouse-mode:
     *
     * Spice protocol specifies two mouse modes, client mode and
     * server mode. In client mode (%SPICE_MOUSE_MODE_CLIENT), the
     * affective mouse is the client side mouse: the client sends
     * mouse position within the display and the server sends mouse
     * shape messages. In server mode (%SPICE_MOUSE_MODE_SERVER), the
     * client sends relative mouse movements and the server sends
     * position and shape commands.
     **/
    g_object_class_install_property
        (gobject_class, PROP_MOUSE_MODE,
         g_param_spec_int("mouse-mode",
                          "Mouse mode",
                          "Mouse mode",
                          0, INT_MAX, 0,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NAME |
                          G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_AGENT_CONNECTED,
         g_param_spec_boolean("agent-connected",
                              "Agent connected",
                              "Whether the agent is connected",
                              FALSE,
                              G_PARAM_READABLE |
                              G_PARAM_STATIC_NAME |
                              G_PARAM_STATIC_NICK |
                              G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_AGENT_CAPS_0,
         g_param_spec_int("agent-caps-0",
                          "Agent caps 0",
                          "Agent capability bits 0 -> 31",
                          0, INT_MAX, 0,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NAME |
                          G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_DISPLAY_DISABLE_WALLPAPER,
         g_param_spec_boolean("disable-wallpaper",
                              "Disable guest wallpaper",
                              "Disable guest wallpaper",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_DISPLAY_DISABLE_FONT_SMOOTH,
         g_param_spec_boolean("disable-font-smooth",
                              "Disable guest font smooth",
                              "Disable guest font smoothing",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_DISPLAY_DISABLE_ANIMATION,
         g_param_spec_boolean("disable-animation",
                              "Disable guest animations",
                              "Disable guest animations",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_DISABLE_DISPLAY_POSITION,
         g_param_spec_boolean("disable-display-position",
                              "Disable display position",
                              "Disable using display position when setting monitor config",
                              TRUE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_DISPLAY_COLOR_DEPTH,
         g_param_spec_uint("color-depth",
                           "Color depth",
                           "Color depth", 0, 32, 0,
                           G_PARAM_READWRITE |
                           G_PARAM_CONSTRUCT |
                           G_PARAM_STATIC_STRINGS));

    /**
     * SpiceMainChannel:disable-display-align:
     *
     * Disable automatic horizontal display position alignment.
     *
     * Since: 0.13
     */
    g_object_class_install_property
        (gobject_class, PROP_DISABLE_DISPLAY_ALIGN,
         g_param_spec_boolean("disable-display-align",
                              "Disable display align",
                              "Disable display position alignment",
                              FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    /**
     * SpiceMainChannel:max-clipboard:
     *
     * Maximum size of clipboard operations in bytes (default 100MB,
     * -1 for unlimited size);
     *
     * Since: 0.22
     **/
    g_object_class_install_property
        (gobject_class, PROP_MAX_CLIPBOARD,
         g_param_spec_int("max-clipboard",
                          "max clipboard",
                          "Maximum clipboard data size",
                          -1, G_MAXINT, 100 * 1024 * 1024,
                          G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT |
                          G_PARAM_STATIC_STRINGS));

    /* TODO use notify instead */
    /**
     * SpiceMainChannel::main-mouse-update:
     * @main: the #SpiceMainChannel that emitted the signal
     *
     * Notify when the mouse mode has changed.
     **/
    signals[SPICE_MAIN_MOUSE_UPDATE] =
        g_signal_new("main-mouse-update",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceMainChannelClass, mouse_update),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    /* TODO use notify instead */
    /**
     * SpiceMainChannel::main-agent-update:
     * @main: the #SpiceMainChannel that emitted the signal
     *
     * Notify when the %SpiceMainChannel:agent-connected or
     * %SpiceMainChannel:agent-caps-0 property change.
     **/
    signals[SPICE_MAIN_AGENT_UPDATE] =
        g_signal_new("main-agent-update",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceMainChannelClass, agent_update),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);
    /**
     * SpiceMainChannel::main-clipboard:
     * @main: the #SpiceMainChannel that emitted the signal
     * @type: the VD_AGENT_CLIPBOARD data type
     * @data: clipboard data
     * @size: size of @data in bytes
     *
     * Provides guest clipboard data requested by spice_main_clipboard_request().
     *
     * Deprecated: 0.6: use SpiceMainChannel::main-clipboard-selection instead.
     **/
    signals[SPICE_MAIN_CLIPBOARD] =
        g_signal_new("main-clipboard",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__UINT_POINTER_UINT,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-selection:
     * @main: the #SpiceMainChannel that emitted the signal
     * @selection: a VD_AGENT_CLIPBOARD_SELECTION clipboard
     * @type: the VD_AGENT_CLIPBOARD data type
     * @data: clipboard data
     * @size: size of @data in bytes
     *
     * Informs that clipboard selection data are available.
     *
     * Since: 0.6
     **/
    signals[SPICE_MAIN_CLIPBOARD_SELECTION] =
        g_signal_new("main-clipboard-selection",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__UINT_UINT_POINTER_UINT,
                     G_TYPE_NONE,
                     4,
                     G_TYPE_UINT, G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-grab:
     * @main: the #SpiceMainChannel that emitted the signal
     * @types: the VD_AGENT_CLIPBOARD data types
     * @ntypes: the number of @types
     *
     * Inform when clipboard data is available from the guest, and for
     * which @types.
     *
     * Deprecated: 0.6: use SpiceMainChannel::main-clipboard-selection-grab instead.
     **/
    signals[SPICE_MAIN_CLIPBOARD_GRAB] =
        g_signal_new("main-clipboard-grab",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_BOOLEAN__POINTER_UINT,
                     G_TYPE_BOOLEAN,
                     2,
                     G_TYPE_POINTER, G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-selection-grab:
     * @main: the #SpiceMainChannel that emitted the signal
     * @selection: a VD_AGENT_CLIPBOARD_SELECTION clipboard
     * @types: the VD_AGENT_CLIPBOARD data types
     * @ntypes: the number of @types
     *
     * Inform when clipboard data is available from the guest, and for
     * which @types.
     *
     * Since: 0.6
     **/
    signals[SPICE_MAIN_CLIPBOARD_SELECTION_GRAB] =
        g_signal_new("main-clipboard-selection-grab",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_BOOLEAN__UINT_POINTER_UINT,
                     G_TYPE_BOOLEAN,
                     3,
                     G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-request:
     * @main: the #SpiceMainChannel that emitted the signal
     * @types: the VD_AGENT_CLIPBOARD request type
     *
     * Request clipboard data from the client.
     *
     * Return value: %TRUE if the request is successful
     *
     * Deprecated: 0.6: use SpiceMainChannel::main-clipboard-selection-request instead.
     **/
    signals[SPICE_MAIN_CLIPBOARD_REQUEST] =
        g_signal_new("main-clipboard-request",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_BOOLEAN__UINT,
                     G_TYPE_BOOLEAN,
                     1,
                     G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-selection-request:
     * @main: the #SpiceMainChannel that emitted the signal
     * @selection: a VD_AGENT_CLIPBOARD_SELECTION clipboard
     * @types: the VD_AGENT_CLIPBOARD request type
     *
     * Request clipboard data from the client.
     *
     * Return value: %TRUE if the request is successful
     *
     * Since: 0.6
     **/
    signals[SPICE_MAIN_CLIPBOARD_SELECTION_REQUEST] =
        g_signal_new("main-clipboard-selection-request",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_BOOLEAN__UINT_UINT,
                     G_TYPE_BOOLEAN,
                     2,
                     G_TYPE_UINT, G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-release:
     * @main: the #SpiceMainChannel that emitted the signal
     *
     * Inform when the clipboard is released from the guest, when no
     * clipboard data is available from the guest.
     *
     * Deprecated: 0.6: use SpiceMainChannel::main-clipboard-selection-release instead.
     **/
    signals[SPICE_MAIN_CLIPBOARD_RELEASE] =
        g_signal_new("main-clipboard-release",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_DEPRECATED,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    /**
     * SpiceMainChannel::main-clipboard-selection-release:
     * @main: the #SpiceMainChannel that emitted the signal
     * @selection: a VD_AGENT_CLIPBOARD_SELECTION clipboard
     *
     * Inform when the clipboard is released from the guest, when no
     * clipboard data is available from the guest.
     *
     * Since: 0.6
     **/
    signals[SPICE_MAIN_CLIPBOARD_SELECTION_RELEASE] =
        g_signal_new("main-clipboard-selection-release",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__UINT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_UINT);

    /**
     * SpiceMainChannel::migration-started:
     * @main: the #SpiceMainChannel that emitted the signal
     * @session: a migration #SpiceSession
     *
     * Inform when migration is starting. Application wishing to make
     * connections themself can set the #SpiceSession:client-sockets
     * to @TRUE, then follow #SpiceSession::channel-new creation, and
     * use spice_channel_open_fd() once the socket is created.
     *
     **/
    signals[SPICE_MIGRATION_STARTED] =
        g_signal_new("migration-started",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_OBJECT);

    /**
     * SpiceMainChannel::new-file-transfer:
     * @main: the #SpiceMainChannel that emitted the signal
     * @task: a #SpiceFileTransferTask
     *
     * This signal is emitted when a new file transfer task has been initiated
     * on this channel. Client applications may take a reference on the @task
     * object and use it to monitor the status of the file transfer task.
     *
     * Since: 0.31
     **/
    signals[SPICE_MAIN_NEW_FILE_TRANSFER] =
        g_signal_new("new-file-transfer",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_OBJECT);

    channel_set_handlers(SPICE_CHANNEL_CLASS(klass));
}

/* ------------------------------------------------------------------ */


static void agent_free_msg_queue(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c = channel->priv;
    SpiceMsgOut *out;

    if (!c->agent_msg_queue)
        return;

    while (!g_queue_is_empty(c->agent_msg_queue)) {
        out = g_queue_pop_head(c->agent_msg_queue);
        spice_msg_out_unref(out);
    }

    g_clear_pointer(&c->agent_msg_queue, g_queue_free);
}

static gboolean flush_foreach_remove(gpointer key G_GNUC_UNUSED,
                                     gpointer value, gpointer user_data)
{
    gboolean success = GPOINTER_TO_UINT(user_data);
    GTask *result = value;
    g_task_return_boolean(result, success);

    return TRUE;
}

static void file_xfer_flushed(SpiceMainChannel *channel, gboolean success)
{
    SpiceMainChannelPrivate *c = channel->priv;
    g_hash_table_foreach_remove(c->flushing, flush_foreach_remove,
                                GUINT_TO_POINTER(success));
}

static void file_xfer_flush_async(SpiceFileTransferTask *xfer_task,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
    GTask *task;
    SpiceMainChannel *channel;
    SpiceMainChannelPrivate *c;
    gboolean was_empty;

    channel = spice_file_transfer_task_get_channel(xfer_task);
    task = g_task_new(xfer_task,
                      spice_file_transfer_task_get_cancellable(xfer_task),
                      callback,
                      user_data);

    c = channel->priv;
    was_empty = g_queue_is_empty(c->agent_msg_queue);
    if (was_empty) {
        g_task_return_boolean(task, TRUE);
        g_object_unref(task);
        return;
    }

    /* wait until the last message currently in the queue has been sent */
    g_hash_table_insert(c->flushing, g_queue_peek_tail(c->agent_msg_queue), task);
}

static gboolean file_xfer_flush_finish(SpiceFileTransferTask *xfer_task,
                                       GAsyncResult *result,
                                       GError **error)
{
    GTask *task = G_TASK(result);

    g_return_val_if_fail(g_task_is_valid(result, xfer_task), FALSE);

    return g_task_propagate_boolean(task, error);
}

/* coroutine context */
static void agent_send_msg_queue(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c = channel->priv;
    SpiceMsgOut *out;

    while (c->agent_tokens > 0 &&
           !g_queue_is_empty(c->agent_msg_queue)) {
        GTask *task;
        c->agent_tokens--;
        out = g_queue_pop_head(c->agent_msg_queue);
        spice_msg_out_send_internal(out);

        task = g_hash_table_lookup(c->flushing, out);
        if (task) {
            /* if there's a flush task waiting for this message, finish it */
            g_task_return_boolean(task, TRUE);
            g_object_unref(task);
            g_hash_table_remove(c->flushing, out);
        }
    }
    if (g_queue_is_empty(c->agent_msg_queue) &&
        g_hash_table_size(c->flushing) != 0) {
        g_warning("unexpected flush task in list, clearing");
        file_xfer_flushed(channel, TRUE);
    }
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue()

   expected arguments, pair of data/data_size to send terminated with NULL:
   agent_msg_queue_many(main, VD_AGENT_...,
                        &foo, sizeof(Foo),
                        data, data_size, NULL);
*/
G_GNUC_NULL_TERMINATED
static void agent_msg_queue_many(SpiceMainChannel *channel, int type, const void *data, ...)
{
    va_list args;
    SpiceMainChannelPrivate *c = channel->priv;
    SpiceMsgOut *out;
    VDAgentMessage msg;
    guint8 *payload;
    gsize paysize, s, mins, size = 0;
    const guint8 *d;

    G_STATIC_ASSERT(VD_AGENT_MAX_DATA_SIZE > sizeof(VDAgentMessage));

    va_start(args, data);
    for (d = data; d != NULL; d = va_arg(args, void*)) {
        size += va_arg(args, gsize);
    }
    va_end(args);

    msg.protocol = VD_AGENT_PROTOCOL;
    msg.type = type;
    msg.opaque = 0;
    msg.size = size;

    paysize = MIN(VD_AGENT_MAX_DATA_SIZE, size + sizeof(VDAgentMessage));
    out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_AGENT_DATA);
    payload = spice_marshaller_reserve_space(out->marshaller, paysize);
    memcpy(payload, &msg, sizeof(VDAgentMessage));
    payload += sizeof(VDAgentMessage);
    paysize -= sizeof(VDAgentMessage);
    if (paysize == 0) {
        g_queue_push_tail(c->agent_msg_queue, out);
        out = NULL;
    }

    va_start(args, data);
    for (d = data; size > 0; d = va_arg(args, void*)) {
        s = va_arg(args, gsize);
        while (s > 0) {
            if (out == NULL) {
                paysize = MIN(VD_AGENT_MAX_DATA_SIZE, size);
                out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_AGENT_DATA);
                payload = spice_marshaller_reserve_space(out->marshaller, paysize);
            }
            mins = MIN(paysize, s);
            memcpy(payload, d, mins);
            d += mins;
            payload += mins;
            s -= mins;
            size -= mins;
            paysize -= mins;
            if (paysize == 0) {
                g_queue_push_tail(c->agent_msg_queue, out);
                out = NULL;
            }
        }
    }
    va_end(args);
    g_warn_if_fail(out == NULL);
}

static int monitors_cmp(const void *p1, const void *p2, gpointer user_data)
{
    const VDAgentMonConfig *m1 = p1;
    const VDAgentMonConfig *m2 = p2;
    double d1 = sqrt(m1->x * m1->x + m1->y * m1->y);
    double d2 = sqrt(m2->x * m2->x + m2->y * m2->y);
    int diff = d1 - d2;

    return diff == 0 ? (char*)p1 - (char*)p2 : diff;
}

static void monitors_align(VDAgentMonConfig *monitors, int nmonitors)
{
    gint i, j, x = 0;
    guint32 used = 0;
    VDAgentMonConfig *sorted_monitors;

    if (nmonitors == 0)
        return;

    /* sort by distance from origin */
    sorted_monitors = g_memdup(monitors, nmonitors * sizeof(VDAgentMonConfig));
    g_qsort_with_data(sorted_monitors, nmonitors, sizeof(VDAgentMonConfig), monitors_cmp, NULL);

    /* super-KISS ltr alignment, feel free to improve */
    for (i = 0; i < nmonitors; i++) {
        /* Find where this monitor is in the sorted order */
        for (j = 0; j < nmonitors; j++) {
            /* Avoid using the same entry twice, this happens with older
               virt-viewer versions which always set x and y to 0 */
            if (used & (1 << j))
                continue;
            if (memcmp(&monitors[j], &sorted_monitors[i],
                       sizeof(VDAgentMonConfig)) == 0)
                break;
        }
        used |= 1 << j;
        monitors[j].x = x;
        monitors[j].y = 0;
        x += monitors[j].width;
        if (monitors[j].width || monitors[j].height)
            SPICE_DEBUG("#%d +%d+%d-%ux%u", j, monitors[j].x, monitors[j].y,
                        monitors[j].width, monitors[j].height);
    }
    g_free(sorted_monitors);
}


#define agent_msg_queue(Channel, Type, Size, Data) \
    agent_msg_queue_many((Channel), (Type), (Data), (Size), NULL)

/**
 * spice_main_send_monitor_config:
 * @channel: a #SpiceMainChannel
 *
 * Send monitors configuration previously set with
 * spice_main_set_display() and spice_main_set_display_enabled()
 *
 * Returns: %TRUE on success.
 *
 * Deprecated: 0.35: use spice_main_channel_send_monitor_config() instead.
 **/
gboolean spice_main_send_monitor_config(SpiceMainChannel *channel)
{
    return spice_main_channel_send_monitor_config(channel);
}

/**
 * spice_main_channel_send_monitor_config:
 * @channel: a #SpiceMainChannel
 *
 * Send monitors configuration previously set with
 * spice_main_set_display() and spice_main_set_display_enabled()
 *
 * Returns: %TRUE on success.
 *
 * Since: 0.35
 **/
gboolean spice_main_channel_send_monitor_config(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c;
    VDAgentMonitorsConfig *mon;
    int i, j, monitors;
    size_t size;

    g_return_val_if_fail(SPICE_IS_MAIN_CHANNEL(channel), FALSE);
    c = channel->priv;
    g_return_val_if_fail(c->agent_connected, FALSE);

    if (spice_main_channel_agent_test_capability(channel, VD_AGENT_CAP_SPARSE_MONITORS_CONFIG)) {
        monitors = SPICE_N_ELEMENTS(c->display);
    } else {
        monitors = 0;
        for (i = 0; i < SPICE_N_ELEMENTS(c->display); i++) {
            if (c->display[i].display_state == DISPLAY_ENABLED)
                monitors += 1;
        }
    }

    size = sizeof(VDAgentMonitorsConfig) + sizeof(VDAgentMonConfig) * monitors;
    mon = g_malloc0(size);

    mon->num_of_monitors = monitors;
    if (c->disable_display_position == FALSE ||
        c->disable_display_align == FALSE)
        mon->flags |= VD_AGENT_CONFIG_MONITORS_FLAG_USE_POS;

    CHANNEL_DEBUG(channel, "sending new monitors config to guest");
    j = 0;
    for (i = 0; i < SPICE_N_ELEMENTS(c->display); i++) {
        if (c->display[i].display_state != DISPLAY_ENABLED) {
            if (spice_main_channel_agent_test_capability(channel,
                                                         VD_AGENT_CAP_SPARSE_MONITORS_CONFIG))
                j++;
            continue;
        }
        mon->monitors[j].depth  = c->display_color_depth ? c->display_color_depth : 32;
        mon->monitors[j].width  = c->display[i].width;
        mon->monitors[j].height = c->display[i].height;
        mon->monitors[j].x = c->display[i].x;
        mon->monitors[j].y = c->display[i].y;
        CHANNEL_DEBUG(channel, "monitor #%d: %ux%u+%d+%d @ %u bpp", j,
                      mon->monitors[j].width, mon->monitors[j].height,
                      mon->monitors[j].x, mon->monitors[j].y,
                      mon->monitors[j].depth);
        j++;
    }

    if (c->disable_display_align == FALSE)
        monitors_align(mon->monitors, mon->num_of_monitors);

    agent_msg_queue(channel, VD_AGENT_MONITORS_CONFIG, size, mon);
    g_free(mon);

    spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
    if (c->timer_id != 0) {
        g_source_remove(c->timer_id);
        c->timer_id = 0;
    }

    return TRUE;
}

static SpiceAudio *spice_main_get_audio(const SpiceMainChannel *channel)
{
    return spice_audio_get(spice_channel_get_session(SPICE_CHANNEL(channel)), NULL);
}

static void audio_playback_volume_info_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
    SpiceMainChannel *main_channel = user_data;
    SpiceAudio *audio = spice_main_get_audio(main_channel);
    VDAgentAudioVolumeSync *avs;
    guint16 *volume;
    guint8 nchannels;
    gboolean mute, ret;
    gsize array_size;
    GError *error = NULL;

    ret = spice_audio_get_playback_volume_info_finish(audio, res, &mute, &nchannels,
                                                      &volume, &error);
    if (ret == FALSE || volume == NULL || nchannels == 0) {
        if (error != NULL) {
            SPICE_DEBUG("Failed to get playback async volume info: %s", error->message);
            g_error_free(error);
        } else {
            SPICE_DEBUG("Failed to get playback async volume info");
        }
        main_channel->priv->agent_volume_playback_sync = FALSE;
        return;
    }

    array_size = sizeof(uint16_t) * nchannels;
    avs = g_malloc0(sizeof(VDAgentAudioVolumeSync) + array_size);
    avs->is_playback = TRUE;
    avs->mute = mute;
    avs->nchannels = nchannels;
    memcpy(avs->volume, volume, array_size);

    SPICE_DEBUG("%s mute=%s nchannels=%u volume[0]=%u",
                __func__, spice_yes_no(mute), nchannels, volume[0]);
    g_free(volume);
    agent_msg_queue(main_channel, VD_AGENT_AUDIO_VOLUME_SYNC,
                    sizeof(VDAgentAudioVolumeSync) + array_size, avs);
    g_free (avs);
}

static void agent_sync_audio_playback(SpiceMainChannel *main_channel)
{
    SpiceAudio *audio = spice_main_get_audio(main_channel);
    SpiceMainChannelPrivate *c = main_channel->priv;

    if (audio == NULL ||
        !test_agent_cap(main_channel, VD_AGENT_CAP_AUDIO_VOLUME_SYNC) ||
        c->agent_volume_playback_sync == TRUE) {
        SPICE_DEBUG("%s - is not going to sync audio with guest", __func__);
        return;
    }
    /* only one per connection */
    g_cancellable_reset(c->cancellable_volume_info);
    c->agent_volume_playback_sync = TRUE;
    spice_audio_get_playback_volume_info_async(audio, c->cancellable_volume_info, main_channel,
                                               audio_playback_volume_info_cb, main_channel);
}

static void audio_record_volume_info_cb(GObject *object, GAsyncResult *res, gpointer user_data)
{
    SpiceMainChannel *main_channel = user_data;
    SpiceAudio *audio = spice_main_get_audio(main_channel);
    VDAgentAudioVolumeSync *avs;
    guint16 *volume;
    guint8 nchannels;
    gboolean ret, mute;
    gsize array_size;
    GError *error = NULL;

    ret = spice_audio_get_record_volume_info_finish(audio, res, &mute, &nchannels, &volume, &error);
    if (ret == FALSE || volume == NULL || nchannels == 0) {
        if (error != NULL) {
            SPICE_DEBUG("Failed to get record async volume info: %s", error->message);
            g_error_free(error);
        } else {
            SPICE_DEBUG("Failed to get record async volume info");
        }
        main_channel->priv->agent_volume_record_sync = FALSE;
        return;
    }

    array_size = sizeof(uint16_t) * nchannels;
    avs = g_malloc0(sizeof(VDAgentAudioVolumeSync) + array_size);
    avs->is_playback = FALSE;
    avs->mute = mute;
    avs->nchannels = nchannels;
    memcpy(avs->volume, volume, array_size);

    SPICE_DEBUG("%s mute=%s nchannels=%u volume[0]=%u",
                __func__, spice_yes_no(mute), nchannels, volume[0]);
    g_free(volume);
    agent_msg_queue(main_channel, VD_AGENT_AUDIO_VOLUME_SYNC,
                    sizeof(VDAgentAudioVolumeSync) + array_size, avs);
    g_free (avs);
}

static void agent_sync_audio_record(SpiceMainChannel *main_channel)
{
    SpiceAudio *audio = spice_main_get_audio(main_channel);
    SpiceMainChannelPrivate *c = main_channel->priv;

    if (audio == NULL ||
        !test_agent_cap(main_channel, VD_AGENT_CAP_AUDIO_VOLUME_SYNC) ||
        c->agent_volume_record_sync == TRUE) {
        SPICE_DEBUG("%s - is not going to sync audio with guest", __func__);
        return;
    }
    /* only one per connection */
    g_cancellable_reset(c->cancellable_volume_info);
    c->agent_volume_record_sync = TRUE;
    spice_audio_get_record_volume_info_async(audio, c->cancellable_volume_info, main_channel,
                                             audio_record_volume_info_cb, main_channel);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_display_config(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c = channel->priv;
    VDAgentDisplayConfig config = { 0, };

    if (c->display_disable_wallpaper) {
        config.flags |= VD_AGENT_DISPLAY_CONFIG_FLAG_DISABLE_WALLPAPER;
    }

    if (c->display_disable_font_smooth) {
        config.flags |= VD_AGENT_DISPLAY_CONFIG_FLAG_DISABLE_FONT_SMOOTH;
    }

    if (c->display_disable_animation) {
        config.flags |= VD_AGENT_DISPLAY_CONFIG_FLAG_DISABLE_ANIMATION;
    }

    if (c->display_color_depth != 0) {
        config.flags |= VD_AGENT_DISPLAY_CONFIG_FLAG_SET_COLOR_DEPTH;
        config.depth = c->display_color_depth;
    }

    CHANNEL_DEBUG(channel, "display_config: flags: %u, depth: %u", config.flags, config.depth);

    agent_msg_queue(channel, VD_AGENT_DISPLAY_CONFIG, sizeof(VDAgentDisplayConfig), &config);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_announce_caps(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c = channel->priv;
    VDAgentAnnounceCapabilities *caps;
    size_t size;

    if (!c->agent_connected)
        return;

    size = sizeof(VDAgentAnnounceCapabilities) + VD_AGENT_CAPS_BYTES;
    caps = g_malloc0(size);
    if (!c->agent_caps_received)
        caps->request = 1;
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_MOUSE_STATE);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_MONITORS_CONFIG);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_REPLY);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_DISPLAY_CONFIG);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_CLIPBOARD_SELECTION);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_PORT_FORWARDING);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_MONITORS_CONFIG_POSITION);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_FILE_XFER_DETAILED_ERRORS);

    agent_msg_queue(channel, VD_AGENT_ANNOUNCE_CAPABILITIES, size, caps);
    g_free(caps);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_grab(SpiceMainChannel *channel, guint selection,
                                 guint32 *types, int ntypes)
{
    SpiceMainChannelPrivate *c = channel->priv;
    guint8 *msg;
    VDAgentClipboardGrab *grab;
    size_t size;
    int i;

    if (!c->agent_connected)
        return;

    g_return_if_fail(test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));

    size = sizeof(VDAgentClipboardGrab) + sizeof(uint32_t) * ntypes;
    if (test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
        size += 4;
    } else if (selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) {
        CHANNEL_DEBUG(channel, "Ignoring clipboard grab");
        return;
    }

    msg = g_alloca(size);
    memset(msg, 0, size);

    grab = (VDAgentClipboardGrab *)msg;

    if (test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
        msg[0] = selection;
        grab = (VDAgentClipboardGrab *)(msg + 4);
    }

    for (i = 0; i < ntypes; i++) {
        grab->types[i] = types[i];
    }

    agent_msg_queue(channel, VD_AGENT_CLIPBOARD_GRAB, size, msg);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_notify(SpiceMainChannel *self, guint selection,
                                   guint32 type, const guchar *data, size_t size)
{
    SpiceMainChannelPrivate *c = self->priv;
    VDAgentClipboard *cb;
    guint8 *msg;
    size_t msgsize;
    gint max_clipboard = spice_main_get_max_clipboard(self);

    g_return_if_fail(c->agent_connected);
    g_return_if_fail(test_agent_cap(self, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));
    g_return_if_fail(max_clipboard == -1 || size < max_clipboard);

    msgsize = sizeof(VDAgentClipboard);
    if (test_agent_cap(self, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
        msgsize += 4;
    } else if (selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) {
        CHANNEL_DEBUG(self, "Ignoring clipboard notify");
        return;
    }

    msg = g_alloca(msgsize);
    memset(msg, 0, msgsize);

    cb = (VDAgentClipboard *)msg;

    if (test_agent_cap(self, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
        msg[0] = selection;
        cb = (VDAgentClipboard *)(msg + 4);
    }

    cb->type = type;
    agent_msg_queue_many(self, VD_AGENT_CLIPBOARD, msg, msgsize, data, size, NULL);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_request(SpiceMainChannel *channel, guint selection, guint32 type)
{
    SpiceMainChannelPrivate *c = channel->priv;
    VDAgentClipboardRequest *request;
    guint8 *msg;
    size_t msgsize;

    g_return_if_fail(c->agent_connected);
    g_return_if_fail(test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));

    msgsize = sizeof(VDAgentClipboardRequest);
    if (test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
        msgsize += 4;
    } else if (selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) {
        SPICE_DEBUG("Ignoring clipboard request");
        return;
    }

    msg = g_alloca(msgsize);
    memset(msg, 0, msgsize);

    request = (VDAgentClipboardRequest *)msg;

    if (test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
        msg[0] = selection;
        request = (VDAgentClipboardRequest *)(msg + 4);
    }

    request->type = type;

    agent_msg_queue(channel, VD_AGENT_CLIPBOARD_REQUEST, msgsize, msg);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_release(SpiceMainChannel *channel, guint selection)
{
    SpiceMainChannelPrivate *c = channel->priv;
    guint8 msg[4] = { 0, };
    guint8 msgsize = 0;

    g_return_if_fail(c->agent_connected);
    g_return_if_fail(test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));

    if (test_agent_cap(channel, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
        msg[0] = selection;
        msgsize += 4;
    } else if (selection != VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD) {
        SPICE_DEBUG("Ignoring clipboard release");
        return;
    }

    agent_msg_queue(channel, VD_AGENT_CLIPBOARD_RELEASE, msgsize, msg);
}

static gboolean any_display_has_dimensions(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c;
    guint i;

    g_return_val_if_fail(SPICE_IS_MAIN_CHANNEL(channel), FALSE);
    c = channel->priv;

    for (i = 0; i < MAX_DISPLAY; i++) {
        if (c->display[i].width > 0 && c->display[i].height > 0)
            return TRUE;
    }

    return FALSE;
}

/* main context*/
static gboolean timer_set_display(gpointer data)
{
    SpiceMainChannel *channel = data;
    SpiceMainChannelPrivate *c = channel->priv;
    SpiceSession *session;
    gint i;

    c->timer_id = 0;
    if (!c->agent_connected)
        return FALSE;

    if (!any_display_has_dimensions(channel)) {
        SPICE_DEBUG("Not sending monitors config, at least one monitor must have dimensions");
        return FALSE;
    }

    session = spice_channel_get_session(SPICE_CHANNEL(channel));

    if (!spice_main_channel_agent_test_capability(channel, VD_AGENT_CAP_SPARSE_MONITORS_CONFIG)) {
        /* ensure we have an explicit monitor configuration at least for
           number of display channels */
        for (i = 0; i < spice_session_get_n_display_channels(session); i++)
            if (c->display[i].display_state == DISPLAY_UNDEFINED) {
                SPICE_DEBUG("Not sending monitors config, missing monitors");
                return FALSE;
            }
    }
    spice_main_channel_send_monitor_config(channel);

    return FALSE;
}

/* any context  */
static void update_display_timer(SpiceMainChannel *channel, guint seconds)
{
    SpiceMainChannelPrivate *c = channel->priv;

    if (c->timer_id)
        g_source_remove(c->timer_id);

    if (seconds != 0) {
        c->timer_id = g_timeout_add_seconds(seconds, timer_set_display, channel);
    } else {
        /* We need to special case 0, as we want the callback to fire as soon
         * as possible. g_timeout_add_seconds(0) would set up a timer which would fire
         * at the next second boundary, which might be nearly 1 full second later.
         */
        c->timer_id = g_timeout_add(0, timer_set_display, channel);
    }

}

/* coroutine context  */
static void set_agent_connected(SpiceMainChannel *channel, gboolean connected)
{
    SpiceMainChannelPrivate *c = channel->priv;

    SPICE_DEBUG("agent connected: %s", spice_yes_no(connected));
    if (connected != c->agent_connected) {
        c->agent_connected = connected;
        g_coroutine_object_notify(G_OBJECT(channel), "agent-connected");
    }
    if (!connected)
        spice_main_channel_reset_agent(SPICE_MAIN_CHANNEL(channel));

    g_coroutine_signal_emit(channel, signals[SPICE_MAIN_AGENT_UPDATE], 0);
}

/* coroutine context  */
static void agent_start(SpiceMainChannel *channel)
{
    SpiceMainChannelPrivate *c = channel->priv;
    SpiceMsgcMainAgentStart agent_start = {
        .num_tokens = ~0,
    };
    SpiceMsgOut *out;

    c->agent_volume_playback_sync = FALSE;
    c->agent_volume_record_sync = FALSE;
    c->agent_caps_received = false;
    set_agent_connected(channel, TRUE);

    out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_AGENT_START);
    out->marshallers->msgc_main_agent_start(out->marshaller, &agent_start);
    spice_msg_out_send_internal(out);

    if (c->agent_connected) {
        agent_announce_caps(channel);
        agent_send_msg_queue(channel);
    }
}

/* coroutine context  */
static void agent_stopped(SpiceMainChannel *channel)
{
    set_agent_connected(channel, FALSE);
}

/**
 * spice_main_request_mouse_mode:
 * @channel: a %SpiceMainChannel
 * @mode: a SPICE_MOUSE_MODE
 *
 * Request a mouse mode to the server. The server may not be able to
 * change the mouse mode, but spice-gtk will try to request it
 * when possible.
 *
 * Since: 0.32
 * Deprecated: 0.35: use spice_main_channel_request_mouse_mode() instead.
 **/
void spice_main_request_mouse_mode(SpiceMainChannel *channel, int mode)
{
    spice_main_channel_request_mouse_mode(channel, mode);
}

/**
 * spice_main_channel_request_mouse_mode:
 * @channel: a %SpiceMainChannel
 * @mode: a SPICE_MOUSE_MODE
 *
 * Request a mouse mode to the server. The server may not be able to
 * change the mouse mode, but spice-gtk will try to request it
 * when possible.
 *
 * Since: 0.35
 **/
void spice_main_channel_request_mouse_mode(SpiceMainChannel *channel, int mode)
{
    SpiceMsgcMainMouseModeRequest req = {
        .mode = mode,
    };
    SpiceMsgOut *out;
    SpiceMainChannelPrivate *c;

    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));
    c = channel->priv;

    if (spice_channel_get_read_only(SPICE_CHANNEL(channel)))
        return;

    CHANNEL_DEBUG(channel, "request mouse mode %d", mode);
    c->requested_mouse_mode = mode;

    out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_MOUSE_MODE_REQUEST);
    out->marshallers->msgc_main_mouse_mode_request(out->marshaller, &req);
    spice_msg_out_send(out);
}

/* coroutine context */
static void set_mouse_mode(SpiceMainChannel *channel, uint32_t supported, uint32_t current)
{
    SpiceMainChannelPrivate *c = channel->priv;

    if (c->mouse_mode != current) {
        c->mouse_mode = current;
        g_coroutine_signal_emit(channel, signals[SPICE_MAIN_MOUSE_UPDATE], 0);
        g_coroutine_object_notify(G_OBJECT(channel), "mouse-mode");
    }

    if (c->requested_mouse_mode != c->mouse_mode &&
        c->requested_mouse_mode & supported) {
        spice_main_channel_request_mouse_mode(SPICE_MAIN_CHANNEL(channel), c->requested_mouse_mode);
    }
}

/* coroutine context */
static void main_handle_init(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;
    SpiceMsgMainInit *init = spice_msg_in_parsed(in);
    SpiceSession *session;
    SpiceMsgOut *out;

    session = spice_channel_get_session(channel);
    spice_session_set_connection_id(session, init->session_id);

    set_mouse_mode(SPICE_MAIN_CHANNEL(channel), init->supported_mouse_modes,
                   init->current_mouse_mode);

    spice_session_set_mm_time(session, init->multi_media_time);
    spice_session_set_caches_hints(session, init->ram_hint, init->display_channels_hint);

    c->agent_tokens = init->agent_tokens;
    if (init->agent_connected)
        agent_start(SPICE_MAIN_CHANNEL(channel));

    if (spice_session_migrate_after_main_init(session))
        return;

    out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_ATTACH_CHANNELS);
    spice_msg_out_send_internal(out);
}

/* coroutine context */
static void main_handle_name(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgMainName *name = spice_msg_in_parsed(in);
    SpiceSession *session = spice_channel_get_session(channel);

    SPICE_DEBUG("server name: %s", name->name);
    spice_session_set_name(session, (const gchar *)name->name);
}

/* coroutine context */
static void main_handle_uuid(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgMainUuid *uuid = spice_msg_in_parsed(in);
    SpiceSession *session = spice_channel_get_session(channel);
    gchar *uuid_str = spice_uuid_to_string(uuid->uuid);

    SPICE_DEBUG("server uuid: %s", uuid_str);
    spice_session_set_uuid(session, uuid->uuid);

    g_free(uuid_str);
}

/* coroutine context */
static void main_handle_mm_time(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceSession *session;
    SpiceMsgMainMultiMediaTime *msg = spice_msg_in_parsed(in);

    session = spice_channel_get_session(channel);
    spice_session_set_mm_time(session, msg->time);
}

typedef struct channel_new {
    SpiceSession *session;
    int type;
    int id;
} channel_new_t;

/* main context */
static gboolean _channel_new(channel_new_t *c)
{
    g_return_val_if_fail(c != NULL, FALSE);

    spice_channel_new(c->session, c->type, c->id);

    g_object_unref(c->session);
    g_free(c);

    return FALSE;
}

/* coroutine context */
static void main_handle_channels_list(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgChannels *msg = spice_msg_in_parsed(in);
    SpiceSession *session;
    int i;

    session = spice_channel_get_session(channel);

    /* guarantee that uuid is notified before setting up the channels, even if
     * the server is older and doesn't actually send the uuid */
    g_coroutine_object_notify(G_OBJECT(session), "uuid");

    for (i = 0; i < msg->num_of_channels; i++) {
        channel_new_t *c;

        c = g_new(channel_new_t, 1);
        c->session = g_object_ref(session);
        c->type = msg->channels[i].type;
        c->id = msg->channels[i].id;
        /* no need to explicitely switch to main context, since
           synchronous call is not needed. */
        /* no need to track idle, session is refed */
        g_idle_add((GSourceFunc)_channel_new, c);
    }
}

/* coroutine context */
static void main_handle_mouse_mode(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgMainMouseMode *msg = spice_msg_in_parsed(in);
    set_mouse_mode(SPICE_MAIN_CHANNEL(channel), msg->supported_modes, msg->current_mode);
}

/* coroutine context */
static void main_handle_agent_connected(SpiceChannel *channel, SpiceMsgIn *in)
{
    agent_start(SPICE_MAIN_CHANNEL(channel));
}

/* coroutine context */
static void main_handle_agent_connected_tokens(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;
    SpiceMsgMainAgentConnectedTokens *msg = spice_msg_in_parsed(in);

    c->agent_tokens = msg->num_tokens;
    agent_start(SPICE_MAIN_CHANNEL(channel));
}

/* coroutine context */
static void main_handle_agent_disconnected(SpiceChannel *channel, SpiceMsgIn *in)
{
    agent_stopped(SPICE_MAIN_CHANNEL(channel));
}

static void file_xfer_data_flushed_cb(GObject *source_object,
                                      GAsyncResult *res,
                                      gpointer user_data)
{
    SpiceFileTransferTask *xfer_task = SPICE_FILE_TRANSFER_TASK(source_object);
    GError *error = NULL;

    file_xfer_flush_finish(xfer_task, res, &error);
    if (error) {
        spice_file_transfer_task_completed(xfer_task, error);
        return;
    }

    /* task might be completed while on idle */
    if (!spice_file_transfer_task_is_completed(xfer_task)) {
        file_transfer_operation_send_progress(xfer_task);
        /* Read more data */
        spice_file_transfer_task_read_async(xfer_task, file_xfer_read_async_cb, user_data);
    }
}

static void file_xfer_queue_msg_to_agent(SpiceMainChannel *channel,
                                         guint32 task_id,
                                         gchar *buffer,
                                         gint data_size)
{
    VDAgentFileXferDataMessage msg;

    g_return_if_fail(channel != NULL);

    msg.id = task_id;
    msg.size = data_size;
    agent_msg_queue_many(channel, VD_AGENT_FILE_XFER_DATA,
                         &msg, sizeof(msg),
                         buffer, data_size, NULL);
    spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
}

/* main context */
static void file_xfer_read_async_cb(GObject *source_object,
                                    GAsyncResult *res,
                                    gpointer user_data)
{
    FileTransferOperation *xfer_op;
    SpiceFileTransferTask *xfer_task;
    SpiceMainChannel *channel;
    gssize count;
    char *buffer;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(source_object);
    xfer_op = user_data;

    channel = spice_file_transfer_task_get_channel(xfer_task);
    count = spice_file_transfer_task_read_finish(xfer_task, res, &buffer, &error);
    if (count < 0) {
        spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
        spice_file_transfer_task_completed(xfer_task, error);
        return;
    }

    if (count == 0 && spice_file_transfer_task_get_total_bytes(xfer_task) > 0) {
        /* If we have sent all payload to the agent, we should not send 0 bytes
         * as it will cause https://bugs.freedesktop.org/show_bug.cgi?id=97227.
         * Only when file has 0 bytes of size is when we should send 0 bytes to
         * agent, see: https://bugzilla.redhat.com/show_bug.cgi?id=1135099 */
        return;
    }

    file_xfer_queue_msg_to_agent(channel, spice_file_transfer_task_get_id(xfer_task), buffer, count);
    if (count == 0 || spice_file_transfer_task_is_completed(xfer_task)) {
        /* on EOF just wait for VD_AGENT_FILE_XFER_STATUS from agent
         * in case the task was completed, nothing to do. */
        return;
    }

    xfer_op->stats.total_sent += count;

    file_xfer_flush_async(xfer_task, file_xfer_data_flushed_cb, xfer_op);
}

/* coroutine context */
static void main_agent_handle_xfer_status(SpiceMainChannel *channel,
                                          VDAgentFileXferStatusMessage *msg)
{
    SpiceFileTransferTask *xfer_task;
    FileTransferOperation *xfer_op;
    GError *error = NULL;

    SPICE_DEBUG("xfer-task %u received response %u", msg->id, msg->result);

    xfer_task = spice_main_channel_find_xfer_task_by_task_id(channel, msg->id);
    g_return_if_fail(xfer_task != NULL);
    xfer_op = g_hash_table_lookup(channel->priv->file_xfer_tasks, GUINT_TO_POINTER(msg->id));

    switch (msg->result) {
    case VD_AGENT_FILE_XFER_STATUS_CAN_SEND_DATA:
        g_return_if_fail(spice_file_transfer_task_is_completed(xfer_task) == FALSE);
        spice_file_transfer_task_read_async(xfer_task, file_xfer_read_async_cb, xfer_op);
        return;
    case VD_AGENT_FILE_XFER_STATUS_CANCELLED:
        error = g_error_new_literal(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                    _("The spice agent cancelled the file transfer"));
        break;
    case VD_AGENT_FILE_XFER_STATUS_ERROR:
        error = g_error_new_literal(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                    _("The spice agent reported an error during the file transfer"));
        break;
    case VD_AGENT_FILE_XFER_STATUS_NOT_ENOUGH_SPACE: {
        uint64_t *free_space = SPICE_ALIGNED_CAST(uint64_t *, msg->data);
        gchar *free_space_str = g_format_size(*free_space);
        gchar *file_size_str = g_format_size(spice_file_transfer_task_get_total_bytes(xfer_task));
        error = g_error_new(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            _("File transfer failed due to lack of free space on remote machine "
                            "(%s free, %s to transfer)"), free_space_str, file_size_str);
        g_free(free_space_str);
        g_free(file_size_str);
        break;
    }
    case VD_AGENT_FILE_XFER_STATUS_SESSION_LOCKED:
        error = g_error_new_literal(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                    _("User's session is locked and cannot transfer files, "
                                      "unlock it and try again."));
        break;
    case VD_AGENT_FILE_XFER_STATUS_VDAGENT_NOT_CONNECTED:
        error = g_error_new_literal(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                    _("Session agent not connected."));
        break;
    case VD_AGENT_FILE_XFER_STATUS_DISABLED:
        error = g_error_new_literal(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                                    _("File transfer is disabled."));
        break;
    case VD_AGENT_FILE_XFER_STATUS_SUCCESS:
        break;
    default:
        g_warn_if_reached();
        error = g_error_new(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "unhandled status type: %u", msg->result);
        break;
    }

    spice_file_transfer_task_completed(xfer_task, error);
}


/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_max_clipboard(SpiceMainChannel *self)
{
    VDAgentMaxClipboard msg = { .max = spice_main_get_max_clipboard(self) };

    if (!test_agent_cap(self, VD_AGENT_CAP_MAX_CLIPBOARD))
        return;

    agent_msg_queue(self, VD_AGENT_MAX_CLIPBOARD, sizeof(VDAgentMaxClipboard), &msg);
}

static void spice_main_set_max_clipboard(SpiceMainChannel *self, gint max)
{
    SpiceMainChannelPrivate *c;

    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(self));
    g_return_if_fail(max >= -1);

    c = self->priv;
    if (max == spice_main_get_max_clipboard(self))
        return;

    c->max_clipboard = max;
    agent_max_clipboard(self);
    spice_channel_wakeup(SPICE_CHANNEL(self), FALSE);
}

/* coroutine context */
static void main_agent_handle_msg(SpiceChannel *channel,
                                  VDAgentMessage *msg, gpointer payload)
{
    SpiceMainChannel *self = SPICE_MAIN_CHANNEL(channel);
    SpiceMainChannelPrivate *c = self->priv;
    guint8 selection = VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD;

    g_return_if_fail(msg->protocol == VD_AGENT_PROTOCOL);

    switch (msg->type) {
    case VD_AGENT_CLIPBOARD_RELEASE:
    case VD_AGENT_CLIPBOARD_REQUEST:
    case VD_AGENT_CLIPBOARD_GRAB:
    case VD_AGENT_CLIPBOARD:
        if (test_agent_cap(self, VD_AGENT_CAP_CLIPBOARD_SELECTION)) {
            selection = *((guint8*)payload);
            payload = ((guint8*)payload) + 4;
            msg->size -= 4;
        }
        break;
    default:
        break;
    }

    switch (msg->type) {
    case VD_AGENT_ANNOUNCE_CAPABILITIES:
    {
        VDAgentAnnounceCapabilities *caps = payload;
        int i, size;

        size = VD_AGENT_CAPS_SIZE_FROM_MSG_SIZE(msg->size);
        if (size > VD_AGENT_CAPS_SIZE)
            size = VD_AGENT_CAPS_SIZE;
        memset(c->agent_caps, 0, sizeof(c->agent_caps));
        for (i = 0; i < size * 32; i++) {
            if (!VD_AGENT_HAS_CAPABILITY(caps->caps, size, i))
                continue;
            SPICE_DEBUG("%s: cap: %d (%s)", __FUNCTION__,
                        i, NAME(agent_caps, i));
            VD_AGENT_SET_CAPABILITY(c->agent_caps, i);
        }
        c->agent_caps_received = true;
        g_coroutine_signal_emit(self, signals[SPICE_MAIN_AGENT_UPDATE], 0);
        update_display_timer(SPICE_MAIN_CHANNEL(channel), 0);

        if (caps->request)
            agent_announce_caps(self);

        if (test_agent_cap(self, VD_AGENT_CAP_DISPLAY_CONFIG) &&
            !c->agent_display_config_sent) {
            agent_display_config(self);
            c->agent_display_config_sent = true;
        }

        agent_sync_audio_playback(self);
        agent_sync_audio_record(self);

        agent_max_clipboard(self);

        agent_send_msg_queue(self);
        agent_send_port_redirections(self);

        break;
    }
    case VD_AGENT_CLIPBOARD:
    {
        VDAgentClipboard *cb = payload;
        g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD_SELECTION], 0, selection,
                                cb->type, cb->data, msg->size - sizeof(VDAgentClipboard));

       if (selection == VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD)
           g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD], 0,
                              cb->type, cb->data, msg->size - sizeof(VDAgentClipboard));
        break;
    }
    case VD_AGENT_CLIPBOARD_GRAB:
    {
        gboolean ret;
        g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD_SELECTION_GRAB], 0, selection,
                          (guint8*)payload, msg->size / sizeof(uint32_t), &ret);
        if (selection == VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD)
            g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD_GRAB], 0,
                              payload, msg->size / sizeof(uint32_t), &ret);
        break;
    }
    case VD_AGENT_CLIPBOARD_REQUEST:
    {
        gboolean ret;
        VDAgentClipboardRequest *req = payload;
        g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD_SELECTION_REQUEST], 0, selection,
                          req->type, &ret);

        if (selection == VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD)
            g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD_REQUEST], 0,
                              req->type, &ret);
        break;
    }
    case VD_AGENT_CLIPBOARD_RELEASE:
    {
        g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD_SELECTION_RELEASE], 0, selection);

        if (selection == VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD)
            g_coroutine_signal_emit(self, signals[SPICE_MAIN_CLIPBOARD_RELEASE], 0);
        break;
    }
    case VD_AGENT_REPLY:
    {
        VDAgentReply *reply = payload;
        SPICE_DEBUG("%s: reply: type %u, %s", __FUNCTION__, reply->type,
                    reply->error == VD_AGENT_SUCCESS ? "success" : "error");
        break;
    }
    case VD_AGENT_FILE_XFER_STATUS:
        main_agent_handle_xfer_status(self, payload);
        break;
    case VD_AGENT_PORT_FORWARD_ACCEPTED:
    case VD_AGENT_PORT_FORWARD_DATA:
    case VD_AGENT_PORT_FORWARD_ACK:
    case VD_AGENT_PORT_FORWARD_CLOSE:
        port_forwarder_handle_message(c->port_forwarder, msg->type, payload);
        break;
    default:
        g_warning("unhandled agent message type: %u (%s), size %u",
                  msg->type, NAME(agent_msg_types, msg->type), msg->size);
    }
}

/* coroutine context */
static void main_handle_agent_data_msg(SpiceChannel* channel, int* msg_size, guchar** msg_pos)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;
    int n;

    if (c->agent_msg_pos < sizeof(VDAgentMessage)) {
        n = MIN(sizeof(VDAgentMessage) - c->agent_msg_pos, *msg_size);
        memcpy((uint8_t*)&c->agent_msg + c->agent_msg_pos, *msg_pos, n);
        c->agent_msg_pos += n;
        *msg_size -= n;
        *msg_pos += n;
        if (c->agent_msg_pos == sizeof(VDAgentMessage)) {
            SPICE_DEBUG("agent msg start: msg_size=%u, protocol=%u, type=%u",
                        c->agent_msg.size, c->agent_msg.protocol, c->agent_msg.type);
            g_return_if_fail(c->agent_msg_data == NULL);
            c->agent_msg_data = g_malloc0(c->agent_msg.size);
        }
    }

    if (c->agent_msg_pos >= sizeof(VDAgentMessage)) {
        n = MIN(sizeof(VDAgentMessage) + c->agent_msg.size - c->agent_msg_pos, *msg_size);
        memcpy(c->agent_msg_data + c->agent_msg_pos - sizeof(VDAgentMessage), *msg_pos, n);
        c->agent_msg_pos += n;
        *msg_size -= n;
        *msg_pos += n;
    }

    if (c->agent_msg_pos == sizeof(VDAgentMessage) + c->agent_msg.size) {
        main_agent_handle_msg(channel, &c->agent_msg, c->agent_msg_data);
        g_free(c->agent_msg_data);
        c->agent_msg_data = NULL;
        c->agent_msg_pos = 0;
    }
}

/* coroutine context */
static void main_handle_agent_data(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;
    guint8 *data;
    int len;

    g_warn_if_fail(c->agent_connected);

    /* shortcut to avoid extra message allocation & copy if possible */
    if (c->agent_msg_pos == 0) {
        VDAgentMessage *msg;
        guint msg_size;

        msg = spice_msg_in_raw(in, &len);
        msg_size = msg->size;

        if (msg_size + sizeof(VDAgentMessage) == len) {
            main_agent_handle_msg(channel, msg, msg->data);
            return;
        }
    }

    data = spice_msg_in_raw(in, &len);
    while (len > 0) {
        main_handle_agent_data_msg(channel, &len, &data);
    }
}

/* coroutine context */
static void main_handle_agent_token(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgMainAgentTokens *tokens = spice_msg_in_parsed(in);
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;

    c->agent_tokens += tokens->num_tokens;

    agent_send_msg_queue(SPICE_MAIN_CHANNEL(channel));
}

/* main context */
static void migrate_channel_new_cb(SpiceSession *s, SpiceChannel *channel, gpointer data)
{
    g_signal_connect(channel, "channel-event",
                     G_CALLBACK(migrate_channel_event_cb), data);
}

static SpiceChannel* migrate_channel_connect(spice_migrate *mig, int type, int id)
{
    SPICE_DEBUG("migrate_channel_connect %d:%d", type, id);

    SpiceChannel *newc = spice_channel_new(mig->session, type, id);
    spice_channel_connect(newc);
    mig->nchannels++;

    return newc;
}

/* coroutine context */
static void spice_main_channel_send_migration_handshake(SpiceChannel *channel)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;

    if (!spice_channel_test_capability(channel, SPICE_MAIN_CAP_SEAMLESS_MIGRATE)) {
        c->migrate_data->do_seamless = false;
        g_idle_add(main_migrate_handshake_done, c->migrate_data);
    } else {
        SpiceMsgcMainMigrateDstDoSeamless msg_data;
        SpiceMsgOut *msg_out;

        msg_data.src_version = c->migrate_data->src_mig_version;

        msg_out = spice_msg_out_new(channel, SPICE_MSGC_MAIN_MIGRATE_DST_DO_SEAMLESS);
        msg_out->marshallers->msgc_main_migrate_dst_do_seamless(msg_out->marshaller, &msg_data);
        spice_msg_out_send_internal(msg_out);
    }
}

/* main context */
static void migrate_channel_event_cb(SpiceChannel *channel, SpiceChannelEvent event,
                                     gpointer data)
{
    spice_migrate *mig = data;
    SpiceChannelPrivate  *c = SPICE_CHANNEL(channel)->priv;

    g_return_if_fail(mig->nchannels > 0);
    g_signal_handlers_disconnect_by_func(channel, migrate_channel_event_cb, data);

    switch (event) {
    case SPICE_CHANNEL_OPENED:
        if (c->channel_type == SPICE_CHANNEL_MAIN) {
            SpiceSession *session = spice_channel_get_session(mig->src_channel);
            if (mig->do_seamless) {
                SpiceMainChannelPrivate *main_priv = SPICE_MAIN_CHANNEL(channel)->priv;

                c->state = SPICE_CHANNEL_STATE_MIGRATION_HANDSHAKE;
                mig->dst_channel = channel;
                main_priv->migrate_data = mig;
            } else {
                c->state = SPICE_CHANNEL_STATE_MIGRATING;
                mig->nchannels--;
            }
            /* now connect the rest of the channels */
            GList *channels, *l;
            l = channels = spice_session_get_channels(session);
            while (l != NULL) {
                SpiceChannelPrivate  *curc = SPICE_CHANNEL(l->data)->priv;
                l = l->next;
                if (curc->channel_type == SPICE_CHANNEL_MAIN)
                    continue;
                migrate_channel_connect(mig, curc->channel_type, curc->channel_id);
            }
            g_list_free(channels);
        } else {
            c->state = SPICE_CHANNEL_STATE_MIGRATING;
            mig->nchannels--;
        }

        SPICE_DEBUG("migration: channel opened chan:%p, left %u", channel, mig->nchannels);
        if (mig->nchannels == 0)
            coroutine_yieldto(mig->from, NULL);
        break;
    default:
        CHANNEL_DEBUG(channel, "error or unhandled channel event during migration: %u", event);
        /* go back to main channel to report error */
        coroutine_yieldto(mig->from, NULL);
    }
}

/* main context */
static gboolean main_migrate_handshake_done(gpointer data)
{
    spice_migrate *mig = data;
    SpiceChannelPrivate  *c = SPICE_CHANNEL(mig->dst_channel)->priv;

    g_return_val_if_fail(c->channel_type == SPICE_CHANNEL_MAIN, FALSE);
    g_return_val_if_fail(c->state == SPICE_CHANNEL_STATE_MIGRATION_HANDSHAKE, FALSE);

    c->state = SPICE_CHANNEL_STATE_MIGRATING;
    mig->nchannels--;
    if (mig->nchannels == 0)
        coroutine_yieldto(mig->from, NULL);
    return FALSE;
}

#ifdef __GNUC__
typedef struct __attribute__ ((__packed__)) OldRedMigrationBegin {
#else
typedef struct __declspec(align(1)) OldRedMigrationBegin {
#endif
    uint16_t port;
    uint16_t sport;
    char host[0];
} OldRedMigrationBegin;

/* main context */
static gboolean migrate_connect(gpointer data)
{
    spice_migrate *mig = data;
    SpiceChannelPrivate  *c;
    int port, sport;
    const char *host;

    g_return_val_if_fail(mig != NULL, FALSE);
    g_return_val_if_fail(mig->info != NULL, FALSE);
    g_return_val_if_fail(mig->nchannels == 0, FALSE);
    c = SPICE_CHANNEL(mig->src_channel)->priv;
    g_return_val_if_fail(c != NULL, FALSE);
    g_return_val_if_fail(mig->session != NULL, FALSE);

    spice_session_set_migration_state(mig->session, SPICE_SESSION_MIGRATION_CONNECTING);

    if ((c->peer_hdr.major_version == 1) &&
        (c->peer_hdr.minor_version < 1)) {
        OldRedMigrationBegin *info = (OldRedMigrationBegin *)mig->info;
        SPICE_DEBUG("migrate_begin old %s %d %d",
                    info->host, info->port, info->sport);
        port = info->port;
        sport = info->sport;
        host = info->host;
    } else {
        SpiceMigrationDstInfo *info = mig->info;
        SPICE_DEBUG("migrate_begin %u %s %d %d",
                    info->host_size, info->host_data, info->port, info->sport);
        port = info->port;
        sport = info->sport;
        host = (char*)info->host_data;

        if ((c->peer_hdr.major_version == 1) ||
            (c->peer_hdr.major_version == 2 && c->peer_hdr.minor_version < 1)) {
            GByteArray *pubkey = g_byte_array_new();

            g_byte_array_append(pubkey, info->pub_key_data, info->pub_key_size);
            g_object_set(mig->session,
                         "pubkey", pubkey,
                         "verify", SPICE_SESSION_VERIFY_PUBKEY,
                         NULL);
            g_byte_array_unref(pubkey);
        } else if (info->cert_subject_size == 0 ||
                   strlen((const char*)info->cert_subject_data) == 0) {
            /* only verify hostname if no cert subject */
            g_object_set(mig->session, "verify", SPICE_SESSION_VERIFY_HOSTNAME, NULL);
        } else {
            gchar *subject = g_alloca(info->cert_subject_size + 1);
            strncpy(subject, (const char*)info->cert_subject_data, info->cert_subject_size);
            subject[info->cert_subject_size] = '\0';

            // session data are already copied
            g_object_set(mig->session,
                         "cert-subject", subject,
                         "verify", SPICE_SESSION_VERIFY_SUBJECT,
                         NULL);
        }
    }

    if (g_getenv("SPICE_MIG_HOST"))
        host = g_getenv("SPICE_MIG_HOST");

    g_object_set(mig->session, "host", host, NULL);
    spice_session_set_port(mig->session, port, FALSE);
    spice_session_set_port(mig->session, sport, TRUE);
    g_signal_connect(mig->session, "channel-new",
                     G_CALLBACK(migrate_channel_new_cb), mig);

    g_signal_emit(mig->src_channel, signals[SPICE_MIGRATION_STARTED], 0,
                  mig->session);

    /* the migration process is in 2 steps, first the main channel and
       then the rest of the channels */
    migrate_channel_connect(mig, SPICE_CHANNEL_MAIN, 0);

    return FALSE;
}

/* coroutine context */
static void main_migrate_connect(SpiceChannel *channel,
                                 SpiceMigrationDstInfo *dst_info, bool do_seamless,
                                 uint32_t src_mig_version)
{
    SpiceMainChannelPrivate *main_priv = SPICE_MAIN_CHANNEL(channel)->priv;
    int reply_type = SPICE_MSGC_MAIN_MIGRATE_CONNECT_ERROR;
    spice_migrate mig = { 0, };
    SpiceMsgOut *out;
    SpiceSession *session;

    mig.src_channel = channel;
    mig.info = dst_info;
    mig.from = coroutine_self();
    mig.do_seamless = do_seamless;
    mig.src_mig_version = src_mig_version;

    CHANNEL_DEBUG(channel, "migrate connect");
    session = spice_channel_get_session(channel);
    mig.session = spice_session_new_from_session(session);
    if (mig.session == NULL)
        goto end;
    if (!spice_session_set_migration_session(session, mig.session))
        goto end;

    main_priv->migrate_data = &mig;

    /* no need to track idle, call is sync for this coroutine */
    g_idle_add(migrate_connect, &mig);

    /* switch to main loop and wait for connections */
    coroutine_yield(NULL);

    if (mig.nchannels != 0) {
        CHANNEL_DEBUG(channel, "migrate failed: some channels failed to connect");
        spice_session_abort_migration(session);
    } else {
        if (mig.do_seamless) {
            SPICE_DEBUG("migration (seamless): connections all ok");
            reply_type = SPICE_MSGC_MAIN_MIGRATE_CONNECTED_SEAMLESS;
        } else {
            SPICE_DEBUG("migration (semi-seamless): connections all ok");
            reply_type = SPICE_MSGC_MAIN_MIGRATE_CONNECTED;
        }
        spice_session_start_migrating(spice_channel_get_session(channel),
                                      mig.do_seamless);
    }

end:
    CHANNEL_DEBUG(channel, "migrate connect reply %d", reply_type);
    out = spice_msg_out_new(SPICE_CHANNEL(channel), reply_type);
    spice_msg_out_send(out);
}

/* coroutine context */
static void main_handle_migrate_begin(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgMainMigrationBegin *msg = spice_msg_in_parsed(in);

    main_migrate_connect(channel, &msg->dst_info, false, 0);
}

/* coroutine context */
static void main_handle_migrate_begin_seamless(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgMainMigrateBeginSeamless *msg = spice_msg_in_parsed(in);

    main_migrate_connect(channel, &msg->dst_info, true, msg->src_mig_version);
}

static void main_handle_migrate_dst_seamless_ack(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceChannelPrivate  *c = SPICE_CHANNEL(channel)->priv;
    SpiceMainChannelPrivate *main_priv = SPICE_MAIN_CHANNEL(channel)->priv;

    g_return_if_fail(c->state == SPICE_CHANNEL_STATE_MIGRATION_HANDSHAKE);
    main_priv->migrate_data->do_seamless = true;
    g_idle_add(main_migrate_handshake_done, main_priv->migrate_data);
}

static void main_handle_migrate_dst_seamless_nack(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceChannelPrivate  *c = SPICE_CHANNEL(channel)->priv;
    SpiceMainChannelPrivate *main_priv = SPICE_MAIN_CHANNEL(channel)->priv;

    g_return_if_fail(c->state == SPICE_CHANNEL_STATE_MIGRATION_HANDSHAKE);
    main_priv->migrate_data->do_seamless = false;
    g_idle_add(main_migrate_handshake_done, main_priv->migrate_data);
}

/* main context */
static gboolean migrate_delayed(gpointer data)
{
    SpiceChannel *channel = data;
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;

    g_warn_if_fail(c->migrate_delayed_id != 0);
    c->migrate_delayed_id = 0;

    spice_session_migrate_end(channel->priv->session);

    return FALSE;
}

/* coroutine context */
static void main_handle_migrate_end(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;

    SPICE_DEBUG("migrate end");

    g_return_if_fail(c->migrate_delayed_id == 0);
    g_return_if_fail(spice_channel_test_capability(channel, SPICE_MAIN_CAP_SEMI_SEAMLESS_MIGRATE));

    c->migrate_delayed_id = g_idle_add(migrate_delayed, channel);
}

/* main context */
static gboolean switch_host_delayed(gpointer data)
{
    SpiceChannel *channel = data;
    SpiceSession *session;
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;

    g_warn_if_fail(c->switch_host_delayed_id != 0);
    c->switch_host_delayed_id = 0;

    session = spice_channel_get_session(channel);

    spice_channel_disconnect(channel, SPICE_CHANNEL_SWITCHING);
    spice_session_switching_disconnect(session);

    return FALSE;
}

/* coroutine context */
static void main_handle_migrate_switch_host(SpiceChannel *channel, SpiceMsgIn *in)
{
    SpiceMsgMainMigrationSwitchHost *mig = spice_msg_in_parsed(in);
    SpiceSession *session;
    char *host = (char *)mig->host_data;
    char *subject = NULL;
    SpiceMainChannelPrivate *c = SPICE_MAIN_CHANNEL(channel)->priv;

    g_return_if_fail(host[mig->host_size - 1] == '\0');

    if (mig->cert_subject_size) {
        subject = (char *)mig->cert_subject_data;
        g_return_if_fail(subject[mig->cert_subject_size - 1] == '\0');
    }

    SPICE_DEBUG("migrate_switch %s %d %d %s",
                host, mig->port, mig->sport, subject);

    if (c->switch_host_delayed_id != 0) {
        g_warning("Switching host already in progress, aborting it");
        g_warn_if_fail(g_source_remove(c->switch_host_delayed_id));
        c->switch_host_delayed_id = 0;
    }

    session = spice_channel_get_session(channel);
    spice_session_set_migration_state(session, SPICE_SESSION_MIGRATION_SWITCHING);
    g_object_set(session,
                 "host", host,
                 "cert-subject", subject,
                 NULL);
    spice_session_set_port(session, mig->port, FALSE);
    spice_session_set_port(session, mig->sport, TRUE);

    c->switch_host_delayed_id = g_idle_add(switch_host_delayed, channel);
}

/* coroutine context */
static void main_handle_migrate_cancel(SpiceChannel *channel,
                                       SpiceMsgIn *in G_GNUC_UNUSED)
{
    SpiceSession *session;

    SPICE_DEBUG("migrate_cancel");
    session = spice_channel_get_session(channel);
    spice_session_abort_migration(session);
}

static void channel_set_handlers(SpiceChannelClass *klass)
{
    static const spice_msg_handler handlers[] = {
        [ SPICE_MSG_MAIN_INIT ]                = main_handle_init,
        [ SPICE_MSG_MAIN_NAME ]                = main_handle_name,
        [ SPICE_MSG_MAIN_UUID ]                = main_handle_uuid,
        [ SPICE_MSG_MAIN_CHANNELS_LIST ]       = main_handle_channels_list,
        [ SPICE_MSG_MAIN_MOUSE_MODE ]          = main_handle_mouse_mode,
        [ SPICE_MSG_MAIN_MULTI_MEDIA_TIME ]    = main_handle_mm_time,

        [ SPICE_MSG_MAIN_AGENT_CONNECTED ]     = main_handle_agent_connected,
        [ SPICE_MSG_MAIN_AGENT_DISCONNECTED ]  = main_handle_agent_disconnected,
        [ SPICE_MSG_MAIN_AGENT_DATA ]          = main_handle_agent_data,
        [ SPICE_MSG_MAIN_AGENT_TOKEN ]         = main_handle_agent_token,

        [ SPICE_MSG_MAIN_MIGRATE_BEGIN ]       = main_handle_migrate_begin,
        [ SPICE_MSG_MAIN_MIGRATE_END ]         = main_handle_migrate_end,
        [ SPICE_MSG_MAIN_MIGRATE_CANCEL ]      = main_handle_migrate_cancel,
        [ SPICE_MSG_MAIN_MIGRATE_SWITCH_HOST ] = main_handle_migrate_switch_host,
        [ SPICE_MSG_MAIN_AGENT_CONNECTED_TOKENS ]   = main_handle_agent_connected_tokens,
        [ SPICE_MSG_MAIN_MIGRATE_BEGIN_SEAMLESS ]   = main_handle_migrate_begin_seamless,
        [ SPICE_MSG_MAIN_MIGRATE_DST_SEAMLESS_ACK]  = main_handle_migrate_dst_seamless_ack,
        [ SPICE_MSG_MAIN_MIGRATE_DST_SEAMLESS_NACK] = main_handle_migrate_dst_seamless_nack,
    };

    spice_channel_set_handlers(klass, handlers, G_N_ELEMENTS(handlers));
}

/* coroutine context */
static void spice_main_handle_msg(SpiceChannel *channel, SpiceMsgIn *msg)
{
    int type = spice_msg_in_type(msg);
    SpiceChannelClass *parent_class;
    SpiceChannelPrivate *c = SPICE_CHANNEL(channel)->priv;

    parent_class = SPICE_CHANNEL_CLASS(spice_main_channel_parent_class);

    if (c->state == SPICE_CHANNEL_STATE_MIGRATION_HANDSHAKE) {
        if (type != SPICE_MSG_MAIN_MIGRATE_DST_SEAMLESS_ACK &&
            type != SPICE_MSG_MAIN_MIGRATE_DST_SEAMLESS_NACK) {
            g_critical("unexpected msg (%d)."
                       "Only MIGRATE_DST_SEAMLESS_ACK/NACK are allowed", type);
            return;
        }
    }

    parent_class->handle_msg(channel, msg);
}

/**
 * spice_main_agent_test_capability:
 * @channel: a #SpiceMainChannel
 * @cap: an agent capability identifier
 *
 * Test capability of a remote agent.
 *
 * Returns: %TRUE if @cap (channel kind capability) is available.
 *
 * Deprecated: 0.35: use spice_main_channel_agent_test_capability() instead.
 **/
gboolean spice_main_agent_test_capability(SpiceMainChannel *channel, guint32 cap)
{
    return spice_main_channel_agent_test_capability(channel, cap);
}

/**
 * spice_main_channel_agent_test_capability:
 * @channel: a #SpiceMainChannel
 * @cap: an agent capability identifier
 *
 * Test capability of a remote agent.
 *
 * Returns: %TRUE if @cap (channel kind capability) is available.
 *
 * Since: 0.35
 **/
gboolean spice_main_channel_agent_test_capability(SpiceMainChannel *channel, guint32 cap)
{
    g_return_val_if_fail(SPICE_IS_MAIN_CHANNEL(channel), FALSE);

    return test_agent_cap(channel, cap);
}

/**
 * spice_main_update_display:
 * @channel: a #SpiceMainChannel
 * @id: display ID
 * @x: x position
 * @y: y position
 * @width: display width
 * @height: display height
 * @update: if %TRUE, update guest resolution after 1sec.
 *
 * Update the display @id resolution.
 *
 * If @update is %TRUE, the remote configuration will be updated too
 * after 1 second without further changes. You can send when you want
 * without delay the new configuration to the remote with
 * spice_main_send_monitor_config()
 *
 * Deprecated: 0.35: use spice_main_channel_update_display() instead.
 **/
void spice_main_update_display(SpiceMainChannel *channel, int id,
                               int x, int y, int width, int height,
                               gboolean update)
{
    spice_main_channel_update_display(channel, id, x, y, width, height, update);
}

/**
 * spice_main_channel_update_display:
 * @channel: a #SpiceMainChannel
 * @id: display ID
 * @x: x position
 * @y: y position
 * @width: display width
 * @height: display height
 * @update: if %TRUE, update guest resolution after 1sec.
 *
 * Update the display @id resolution.
 *
 * If @update is %TRUE, the remote configuration will be updated too
 * after 1 second without further changes. You can send when you want
 * without delay the new configuration to the remote with
 * spice_main_send_monitor_config()
 *
 * Since: 0.35
 **/
void spice_main_channel_update_display(SpiceMainChannel *channel, int id, int x, int y, int width,
                               int height, gboolean update)
{
    SpiceMainChannelPrivate *c;

    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));
    g_return_if_fail(x >= 0);
    g_return_if_fail(y >= 0);
    g_return_if_fail(width >= 0);
    g_return_if_fail(height >= 0);

    c = SPICE_MAIN_CHANNEL(channel)->priv;

    g_return_if_fail(id < SPICE_N_ELEMENTS(c->display));

    SpiceDisplayConfig display = {
        .x = x, .y = y, .width = width, .height = height,
        .display_state = c->display[id].display_state
    };

    if (memcmp(&display, &c->display[id], sizeof(SpiceDisplayConfig)) == 0)
        return;

    c->display[id] = display;

    if (update)
        update_display_timer(channel, 1);
}

/**
 * spice_main_set_display:
 * @channel: a #SpiceMainChannel
 * @id: display ID
 * @x: x position
 * @y: y position
 * @width: display width
 * @height: display height
 *
 * Notify the guest of screen resolution change. The notification is
 * sent 1 second later, if no further changes happen.
 *
 * Deprecated: 0.35: use spice_main_channel_update_display() instead.
 **/
void spice_main_set_display(SpiceMainChannel *channel, int id,
                            int x, int y, int width, int height)
{
    spice_main_channel_update_display(channel, id, x, y, width, height, TRUE);
}

/**
 * spice_main_clipboard_grab:
 * @channel: a #SpiceMainChannel
 * @types: an array of #VD_AGENT_CLIPBOARD types available in the clipboard
 * @ntypes: the number of @types
 *
 * Grab the guest clipboard, with #VD_AGENT_CLIPBOARD @types.
 *
 * Deprecated: 0.6: use spice_main_channel_clipboard_selection_grab() instead.
 **/
void spice_main_clipboard_grab(SpiceMainChannel *channel, guint32 *types, int ntypes)
{
    spice_main_channel_clipboard_selection_grab(channel, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,
                                                types, ntypes);
}

/**
 * spice_main_clipboard_selection_grab:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 * @types: an array of #VD_AGENT_CLIPBOARD types available in the clipboard
 * @ntypes: the number of @types
 *
 * Grab the guest clipboard, with #VD_AGENT_CLIPBOARD @types.
 *
 * Since: 0.6
 * Deprecated: 0.35: use spice_main_channel_clipboard_selection_grab() instead.
 **/
void spice_main_clipboard_selection_grab(SpiceMainChannel *channel, guint selection,
                                         guint32 *types, int ntypes)
{
    spice_main_channel_clipboard_selection_grab(channel, selection, types, ntypes);
}

/**
 * spice_main_channel_clipboard_selection_grab:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 * @types: an array of #VD_AGENT_CLIPBOARD types available in the clipboard
 * @ntypes: the number of @types
 *
 * Grab the guest clipboard, with #VD_AGENT_CLIPBOARD @types.
 *
 * Since: 0.35
 **/
void spice_main_channel_clipboard_selection_grab(SpiceMainChannel *channel, guint selection,
                                                 guint32 *types, int ntypes)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    agent_clipboard_grab(channel, selection, types, ntypes);
    spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
}

/**
 * spice_main_clipboard_release:
 * @channel: a #SpiceMainChannel
 *
 * Release the clipboard (for example, when the client loses the
 * clipboard grab): Inform the guest no clipboard data is available.
 *
 * Deprecated: 0.6: use spice_main_channel_clipboard_selection_release() instead.
 **/
void spice_main_clipboard_release(SpiceMainChannel *channel)
{
    spice_main_channel_clipboard_selection_release(channel, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD);
}

/**
 * spice_main_clipboard_selection_release:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 *
 * Release the clipboard (for example, when the client loses the
 * clipboard grab): Inform the guest no clipboard data is available.
 *
 * Since: 0.6
 * Deprecated: 0.35: use spice_main_channel_clipboard_selection_release() instead.
 **/
void spice_main_clipboard_selection_release(SpiceMainChannel *channel, guint selection)
{
    spice_main_channel_clipboard_selection_release(channel, selection);
}

/**
 * spice_main_channel_clipboard_selection_release:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 *
 * Release the clipboard (for example, when the client loses the
 * clipboard grab): Inform the guest no clipboard data is available.
 *
 * Since: 0.35
 **/
void spice_main_channel_clipboard_selection_release(SpiceMainChannel *channel, guint selection)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    SpiceMainChannelPrivate *c = channel->priv;

    if (!c->agent_connected)
        return;

    agent_clipboard_release(channel, selection);
    spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
}

/**
 * spice_main_clipboard_notify:
 * @channel: a #SpiceMainChannel
 * @type: a #VD_AGENT_CLIPBOARD type
 * @data: clipboard data
 * @size: data length in bytes
 *
 * Send the clipboard data to the guest.
 *
 * Deprecated: 0.6: use spice_main_channel_clipboard_selection_notify() instead.
 **/
void spice_main_clipboard_notify(SpiceMainChannel *channel,
                                 guint32 type, const guchar *data, size_t size)
{
    spice_main_channel_clipboard_selection_notify(channel, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,
                                                  type, data, size);
}

/**
 * spice_main_clipboard_selection_notify:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 * @type: a #VD_AGENT_CLIPBOARD type
 * @data: clipboard data
 * @size: data length in bytes
 *
 * Send the clipboard data to the guest.
 *
 * Since: 0.6
 * Deprecated: 0.35: use spice_main_channel_clipboard_selection_notify() instead.
 **/
void spice_main_clipboard_selection_notify(SpiceMainChannel *channel, guint selection,
                                           guint32 type, const guchar *data, size_t size)
{
    spice_main_channel_clipboard_selection_notify(channel, selection, type, data, size);
}

/**
 * spice_main_channel_clipboard_selection_notify:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 * @type: a #VD_AGENT_CLIPBOARD type
 * @data: clipboard data
 * @size: data length in bytes
 *
 * Send the clipboard data to the guest.
 *
 * Since: 0.35
 **/
void spice_main_channel_clipboard_selection_notify(SpiceMainChannel *channel, guint selection,
                                           guint32 type, const guchar *data, size_t size)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    agent_clipboard_notify(channel, selection, type, data, size);
    spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
}

/**
 * spice_main_clipboard_request:
 * @channel: a #SpiceMainChannel
 * @type: a #VD_AGENT_CLIPBOARD type
 *
 * Request clipboard data of @type from the guest. The reply is sent
 * through the #SpiceMainChannel::main-clipboard signal.
 *
 * Deprecated: 0.6: use spice_main_channel_clipboard_selection_request() instead.
 **/
void spice_main_clipboard_request(SpiceMainChannel *channel, guint32 type)
{
    spice_main_channel_clipboard_selection_request(channel, VD_AGENT_CLIPBOARD_SELECTION_CLIPBOARD,
                                                   type);
}

/**
 * spice_main_clipboard_selection_request:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 * @type: a #VD_AGENT_CLIPBOARD type
 *
 * Request clipboard data of @type from the guest. The reply is sent
 * through the #SpiceMainChannel::main-clipboard-selection signal.
 *
 * Since: 0.6
 * Deprecated: 0.35: use spice_main_channel_clipboard_selection_request() instead.
 **/
void spice_main_clipboard_selection_request(SpiceMainChannel *channel, guint selection, guint32 type)
{
    spice_main_channel_clipboard_selection_request(channel, selection, type);
}

/**
 * spice_main_channel_clipboard_selection_request:
 * @channel: a #SpiceMainChannel
 * @selection: one of the clipboard #VD_AGENT_CLIPBOARD_SELECTION_*
 * @type: a #VD_AGENT_CLIPBOARD type
 *
 * Request clipboard data of @type from the guest. The reply is sent
 * through the #SpiceMainChannel::main-clipboard-selection signal.
 *
 * Since: 0.35
 **/
void spice_main_channel_clipboard_selection_request(SpiceMainChannel *channel, guint selection,
                                                    guint32 type)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    agent_clipboard_request(channel, selection, type);
    spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
}

/**
 * spice_main_update_display_enabled:
 * @channel: a #SpiceMainChannel
 * @id: display ID (if -1: set all displays)
 * @enabled: wether display @id is enabled
 * @update: if %TRUE, update guest display state after 1sec.
 *
 * When sending monitor configuration to agent guest, if @enabled is %FALSE,
 * don't set display @id, which the agent translates to disabling the display
 * id. If @enabled is %TRUE, the monitor will be included in the next monitor
 * update. Note: this will take effect next time the monitor configuration is
 * sent.
 *
 * If @update is %FALSE, no server update will be triggered by this call, but
 * the value will be saved and used in the next configuration update.
 *
 * Since: 0.30
 * Deprecated: 0.35: use spice_main_channel_update_display_enabled() instead.
 **/
void spice_main_update_display_enabled(SpiceMainChannel *channel, int id, gboolean enabled,
                                       gboolean update)
{
    spice_main_channel_update_display_enabled(channel, id, enabled, update);
}

/**
 * spice_main_channel_update_display_enabled:
 * @channel: a #SpiceMainChannel
 * @id: display ID (if -1: set all displays)
 * @enabled: wether display @id is enabled
 * @update: if %TRUE, update guest display state after 1sec.
 *
 * When sending monitor configuration to agent guest, if @enabled is %FALSE,
 * don't set display @id, which the agent translates to disabling the display
 * id. If @enabled is %TRUE, the monitor will be included in the next monitor
 * update. Note: this will take effect next time the monitor configuration is
 * sent.
 *
 * If @update is %FALSE, no server update will be triggered by this call, but
 * the value will be saved and used in the next configuration update.
 *
 * Since: 0.35
 **/
void spice_main_channel_update_display_enabled(SpiceMainChannel *channel, int id, gboolean enabled,
                                               gboolean update)
{
    SpiceDisplayState display_state = enabled ? DISPLAY_ENABLED : DISPLAY_DISABLED;
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));
    g_return_if_fail(id >= -1);

    SpiceMainChannelPrivate *c = channel->priv;

    if (id == -1) {
        gint i;
        for (i = 0; i < G_N_ELEMENTS(c->display); i++) {
            c->display[i].display_state = display_state;
        }
    } else {
        g_return_if_fail(id < G_N_ELEMENTS(c->display));
        if (c->display[id].display_state == display_state)
            return;
        c->display[id].display_state = display_state;
    }

    if (update)
        update_display_timer(channel, 1);
}

/**
 * spice_main_set_display_enabled:
 * @channel: a #SpiceMainChannel
 * @id: display ID (if -1: set all displays)
 * @enabled: wether display @id is enabled
 *
 * When sending monitor configuration to agent guest, don't set
 * display @id, which the agent translates to disabling the display
 * id. Note: this will take effect next time the monitor
 * configuration is sent.
 *
 * Since: 0.6
 * Deprecated: 0.35: use spice_main_channel_update_display_enabled() instead.
 **/
void spice_main_set_display_enabled(SpiceMainChannel *channel, int id, gboolean enabled)
{
    spice_main_channel_update_display_enabled(channel, id, enabled, TRUE);
}

static void file_xfer_init_task_async_cb(GObject *obj, GAsyncResult *res, gpointer data)
{
    GFileInfo *info;
    SpiceFileTransferTask *xfer_task;
    SpiceMainChannel *channel;
    gchar *string;
    const gchar *basename;
    GKeyFile *keyfile;
    VDAgentFileXferStartMessage msg;
    guint64 file_size;
    gsize data_len;
    FileTransferOperation *xfer_op;
    GError *error = NULL;

    xfer_task = SPICE_FILE_TRANSFER_TASK(obj);

    info = spice_file_transfer_task_init_task_finish(xfer_task, res, &error);
    if (info == NULL)
        goto failed;

    channel = spice_file_transfer_task_get_channel(xfer_task);
    basename = g_file_info_get_attribute_byte_string(info, G_FILE_ATTRIBUTE_STANDARD_NAME);
    file_size = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_SIZE);

    xfer_op = data;
    xfer_op->stats.transfer_size += file_size;

    keyfile = g_key_file_new();
    g_key_file_set_string(keyfile, "vdagent-file-xfer", "name", basename);
    g_key_file_set_uint64(keyfile, "vdagent-file-xfer", "size", file_size);

    /* Save keyfile content to memory. TODO: more file attributions
       need to be sent to guest */
    string = g_key_file_to_data(keyfile, &data_len, &error);
    g_key_file_free(keyfile);
    if (error)
        goto failed;

    /* Create file-xfer start message */
    msg.id = spice_file_transfer_task_get_id(xfer_task);
    agent_msg_queue_many(channel, VD_AGENT_FILE_XFER_START,
                         &msg, sizeof(msg),
                         string, data_len + 1, NULL);
    g_free(string);
    spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
    g_object_unref(info);
    return;

failed:
    g_clear_object(&info);
    spice_file_transfer_task_completed(xfer_task, error);
}

static void file_transfer_operation_free(FileTransferOperation *xfer_op)
{
    g_return_if_fail(xfer_op != NULL);

    if (xfer_op->stats.failed != 0) {
        GError *error = g_error_new(SPICE_CLIENT_ERROR,
                                    SPICE_CLIENT_ERROR_FAILED,
                                    "Transferring %u files: %u succeed, %u cancelled, %u failed",
                                    xfer_op->stats.num_files,
                                    xfer_op->stats.succeed,
                                    xfer_op->stats.cancelled,
                                    xfer_op->stats.failed);
        SPICE_DEBUG("Transfer failed (%p) %s", xfer_op, error->message);
        g_task_return_error(xfer_op->task, error);
    } else if (xfer_op->stats.cancelled != 0 && xfer_op->stats.succeed == 0) {
        GError *error = g_error_new(G_IO_ERROR,
                                    G_IO_ERROR_CANCELLED,
                                    "Transferring %u files: %u succeed, %u cancelled, %u failed",
                                    xfer_op->stats.num_files,
                                    xfer_op->stats.succeed,
                                    xfer_op->stats.cancelled,
                                    xfer_op->stats.failed);
        SPICE_DEBUG("Transfer cancelled (%p) %s", xfer_op, error->message);
        g_task_return_error(xfer_op->task, error);
    } else {
        SPICE_DEBUG("Transfer successful (%p)", xfer_op);
        g_task_return_boolean(xfer_op->task, TRUE);
    }
    g_object_unref(xfer_op->task);
    g_hash_table_unref(xfer_op->xfer_task);

    spice_debug("Freeing file-transfer-operation %p", xfer_op);
    g_free(xfer_op);
}

static void spice_main_channel_reset_all_xfer_operations(SpiceMainChannel *channel)
{
    GList *it, *keys;

    /* Mark each of SpiceFileTransferTask as completed due error */
    keys = g_hash_table_get_keys(channel->priv->file_xfer_tasks);
    for (it = keys; it != NULL; it = it->next) {
        FileTransferOperation *xfer_op;
        SpiceFileTransferTask *xfer_task;
        GError *error;

        xfer_op = g_hash_table_lookup(channel->priv->file_xfer_tasks, it->data);
        if (xfer_op == NULL)
            continue;

        xfer_task = g_hash_table_lookup(xfer_op->xfer_task, it->data);
        if (xfer_task == NULL) {
            spice_warning("(reset-all) can't complete task %u - completed already?",
                          GPOINTER_TO_UINT(it->data));
            continue;
        }

        error = g_error_new(SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                            "Agent connection closed");
        spice_file_transfer_task_completed(xfer_task, error);
    }
    g_list_free(keys);
}

static SpiceFileTransferTask *spice_main_channel_find_xfer_task_by_task_id(SpiceMainChannel *channel,
                                                                           guint32 task_id)
{
    FileTransferOperation *xfer_op;

    xfer_op = g_hash_table_lookup(channel->priv->file_xfer_tasks, GUINT_TO_POINTER(task_id));
    g_return_val_if_fail(xfer_op != NULL, NULL);
    return g_hash_table_lookup(xfer_op->xfer_task, GUINT_TO_POINTER(task_id));
}

static void file_transfer_operation_task_finished(SpiceFileTransferTask *xfer_task,
                                                  GError *error,
                                                  gpointer userdata)
{
    SpiceMainChannel *channel;
    FileTransferOperation *xfer_op;
    guint32 task_id;

    channel = spice_file_transfer_task_get_channel(xfer_task);
    g_return_if_fail(channel != NULL);
    task_id = spice_file_transfer_task_get_id(xfer_task);
    g_return_if_fail(task_id != 0);

    if (error) {
        VDAgentFileXferStatusMessage msg;
        msg.id = task_id;
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            msg.result = VD_AGENT_FILE_XFER_STATUS_CANCELLED;
        } else {
            msg.result = VD_AGENT_FILE_XFER_STATUS_ERROR;
        }
        agent_msg_queue_many(channel, VD_AGENT_FILE_XFER_STATUS,
                             &msg, sizeof(msg), NULL);
    }

    xfer_op = g_hash_table_lookup(channel->priv->file_xfer_tasks, GUINT_TO_POINTER(task_id));
    if (xfer_op == NULL) {
        /* Likely the operation has ended before the remove-task was called. One
         * situation that this can easily happen is if the agent is disconnected
         * while there are pending files. */
        return;
    }

    if (error) {
        /* On error or cancellation of a SpiceFileTransferTask we remove the
         * remaining bytes from transfer-size in order to keep the coherence of
         * the information we provide to the user (total-sent and transfer-size)
         * in the progress-callback */
        guint64 file_size = spice_file_transfer_task_get_total_bytes(xfer_task);
        guint64 bytes_read = spice_file_transfer_task_get_transferred_bytes(xfer_task);
        xfer_op->stats.transfer_size -= (file_size - bytes_read);
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
            xfer_op->stats.cancelled++;
        } else {
            xfer_op->stats.failed++;
        }
    } else {
        xfer_op->stats.succeed++;
    }

    /* Remove and free SpiceFileTransferTask */
    g_hash_table_remove(xfer_op->xfer_task, GUINT_TO_POINTER(task_id));

    /* Keep file_xfer_tasks up to date. If no more elements, operation is over */
    g_hash_table_remove(channel->priv->file_xfer_tasks, GUINT_TO_POINTER(task_id));

    /* No more pending operations */
    if (g_hash_table_size(xfer_op->xfer_task) == 0)
        file_transfer_operation_free(xfer_op);
}

static void file_transfer_operation_send_progress(SpiceFileTransferTask *xfer_task)
{
    FileTransferOperation *xfer_op;
    SpiceMainChannel *channel;
    guint32 task_id;

    channel = spice_file_transfer_task_get_channel(xfer_task);
    task_id = spice_file_transfer_task_get_id(xfer_task);
    xfer_op = g_hash_table_lookup(channel->priv->file_xfer_tasks, GUINT_TO_POINTER(task_id));
    g_return_if_fail(xfer_op != NULL);

    if (xfer_op->progress_callback)
        xfer_op->progress_callback(xfer_op->stats.total_sent,
                                   xfer_op->stats.transfer_size,
                                   xfer_op->progress_callback_data);
}

/**
 * spice_main_file_copy_async:
 * @channel: a #SpiceMainChannel
 * @sources: (array zero-terminated=1): a %NULL-terminated array of #GFile objects to be transferred
 * @flags: set of #GFileCopyFlags
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore
 * @progress_callback: (allow-none) (scope call): function to callback with
 *     progress information, or %NULL if progress information is not needed
 * @progress_callback_data: (closure): user data to pass to @progress_callback
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: the data to pass to callback function
 *
 * See: spice_main_channel_file_copy_async()
 *
 * Deprecated: 0.35: use spice_main_channel_file_copy_async() instead.
 **/
void spice_main_file_copy_async(SpiceMainChannel *channel,
                                GFile **sources,
                                GFileCopyFlags flags,
                                GCancellable *cancellable,
                                GFileProgressCallback progress_callback,
                                gpointer progress_callback_data,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
    spice_main_channel_file_copy_async(channel, sources, flags, cancellable, progress_callback,
                                       progress_callback_data, callback, user_data);
}

/**
 * spice_main_channel_file_copy_async:
 * @channel: a #SpiceMainChannel
 * @sources: (array zero-terminated=1): a %NULL-terminated array of #GFile objects to be transferred
 * @flags: set of #GFileCopyFlags
 * @cancellable: (allow-none): optional #GCancellable object, %NULL to ignore
 * @progress_callback: (allow-none) (scope call): function to callback with
 *     progress information, or %NULL if progress information is not needed
 * @progress_callback_data: (closure): user data to pass to @progress_callback
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: the data to pass to callback function
 *
 * Copies the file @sources to guest
 *
 * If @cancellable is not %NULL, then the operation can be cancelled by
 * triggering the cancellable object from another thread. If the operation
 * was cancelled, the error %G_IO_ERROR_CANCELLED will be returned.
 *
 * If @progress_callback is not %NULL, then the operation can be monitored by
 * setting this to a #GFileProgressCallback function. @progress_callback_data
 * will be passed to this function. It is guaranteed that this callback will
 * be called after all data has been transferred with the total number of bytes
 * copied during the operation. Note that before release 0.31, progress_callback
 * was broken since it only provided status for a single file transfer, but did
 * not provide a way to determine which file it referred to. In release 0.31,
 * this behavior was changed so that progress_callback provides the status of
 * all ongoing file transfers. If you need to monitor the status of individual
 * files, please connect to the #SpiceMainChannel::new-file-transfer signal.
 *
 * When the operation is finished, callback will be called. You can then call
 * spice_main_file_copy_finish() to get the result of the operation. Note that
 * before release 0.33 the callback was called for each file in multiple file
 * transfer. This behavior was changed for the same reason as the
 * progress_callback (above). If you need to monitor the ending of individual
 * files, you can connect to "finished" signal from each SpiceFileTransferTask.
 *
 * Since: 0.35
 **/
void spice_main_channel_file_copy_async(SpiceMainChannel *channel,
                                        GFile **sources,
                                        GFileCopyFlags flags,
                                        GCancellable *cancellable,
                                        GFileProgressCallback progress_callback,
                                        gpointer progress_callback_data,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
    SpiceMainChannelPrivate *c;
    FileTransferOperation *xfer_op;
    GError *error = NULL;
    GList *it, *keys;

    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));
    g_return_if_fail(sources != NULL);

    c = channel->priv;
    if (!c->agent_connected) {
        error = g_error_new(SPICE_CLIENT_ERROR,
                            SPICE_CLIENT_ERROR_FAILED,
                            "The agent is not connected");
    } else if (test_agent_cap(channel, VD_AGENT_CAP_FILE_XFER_DISABLED)) {
        error = g_error_new(SPICE_CLIENT_ERROR,
                            SPICE_CLIENT_ERROR_FAILED,
                            _("The file transfer is disabled"));
    }

    xfer_op = g_new0(FileTransferOperation, 1);
    xfer_op->channel = channel;
    xfer_op->progress_callback = progress_callback;
    xfer_op->progress_callback_data = progress_callback_data;
    xfer_op->task = g_task_new(channel, cancellable, callback, user_data);
    xfer_op->xfer_task = spice_file_transfer_task_create_tasks(sources,
                                                               channel,
                                                               flags,
                                                               cancellable);
    xfer_op->stats.num_files = g_hash_table_size(xfer_op->xfer_task);
    keys = g_hash_table_get_keys(xfer_op->xfer_task);
    for (it = keys; it != NULL; it = it->next) {
        guint32 task_id;
        SpiceFileTransferTask *xfer_task = g_hash_table_lookup(xfer_op->xfer_task, it->data);

        task_id = spice_file_transfer_task_get_id(xfer_task);

        SPICE_DEBUG("Insert a xfer task:%u to task list", task_id);

        g_hash_table_insert(c->file_xfer_tasks, it->data, xfer_op);
        g_signal_connect(xfer_task, "finished", G_CALLBACK(file_transfer_operation_task_finished), NULL);
        g_signal_emit(channel, signals[SPICE_MAIN_NEW_FILE_TRANSFER], 0, xfer_task);

        if (error == NULL) {
            spice_file_transfer_task_init_task_async(xfer_task,
                                                     file_xfer_init_task_async_cb,
                                                     xfer_op);
        } else {
            spice_file_transfer_task_completed(xfer_task, g_error_copy(error));
        }
    }
    g_list_free(keys);
    g_clear_error(&error);
}

/**
 * spice_main_file_copy_finish:
 * @channel: a #SpiceMainChannel
 * @result: a #GAsyncResult.
 * @error: a #GError, or %NULL
 *
 * Finishes copying the file started with
 * spice_main_file_copy_async().
 *
 * Returns: a %TRUE on success, %FALSE on error.
 **/
gboolean spice_main_file_copy_finish(SpiceMainChannel *channel,
                                     GAsyncResult *result,
                                     GError **error)
{
    return spice_main_channel_file_copy_finish(channel, result, error);
}

/**
 * spice_main_channel_file_copy_finish:
 * @channel: a #SpiceMainChannel
 * @result: a #GAsyncResult.
 * @error: a #GError, or %NULL
 *
 * Finishes copying the file started with
 * spice_main_file_copy_async().
 *
 * Returns: a %TRUE on success, %FALSE on error.
 **/
gboolean spice_main_channel_file_copy_finish(SpiceMainChannel *channel,
                                             GAsyncResult *result,
                                             GError **error)
{
    GTask *task = G_TASK(result);

    g_return_val_if_fail(SPICE_IS_MAIN_CHANNEL(channel), FALSE);
    g_return_val_if_fail(g_task_is_valid(task, channel), FALSE);

    return g_task_propagate_boolean(task, error);
}

static void port_forwarder_send_command(void *channel, uint32_t command,
                                        const uint8_t *data, uint32_t data_size)
{
    agent_msg_queue((SpiceMainChannel *)channel, command, data_size, data);
    if (&SPICE_CHANNEL(channel)->priv->coroutine != g_coroutine_self())
        spice_channel_wakeup(SPICE_CHANNEL(channel), FALSE);
    else
        agent_send_msg_queue((SpiceMainChannel *)channel);
}


static gboolean tokenize_redirection(gchar *redir, gchar **bind_address, gchar **port,
                                     gchar **host, gchar **host_port)
{
    if ((*bind_address = strtok(redir, ":")) &&
            (*port = strtok(NULL, ":")) &&
            (*host = strtok(NULL, ":"))) {
        if (!(*host_port = strtok(NULL, ":"))) {
            // bind_address was not provided
            *host_port = *host;
            *host = *port;
            *port = *bind_address;
            *bind_address = NULL;
        }
        return TRUE;
    } else
        return FALSE;
}

static void agent_send_port_redirections(SpiceMainChannel *channel)
{
    SpiceSession *session = spice_channel_get_session(SPICE_CHANNEL(channel));
    GStrv redirected_ports = NULL;

    g_object_get(session, "redirected-remote-ports", &redirected_ports, NULL);
    while (redirected_ports && *redirected_ports) {
        gchar *redir = g_strdup(*redirected_ports);
        gchar *bind_address, *guest_port, *host, *host_port;
        if (tokenize_redirection(redir, &bind_address, &guest_port, &host, &host_port)) {
            spice_main_port_forward_remote(channel, bind_address, atoi(guest_port),
                                           host, atoi(host_port));
        } else
            SPICE_DEBUG("Failed redirecting %s\n", *redirected_ports);
        g_free(redir);
        ++redirected_ports;
    }

    redirected_ports = NULL;
    g_object_get(session, "redirected-local-ports", &redirected_ports, NULL);
    while (redirected_ports && *redirected_ports) {
        gchar *redir = g_strdup(*redirected_ports);
        gchar *bind_address, *local_port, *host, *host_port;
        if (tokenize_redirection(redir, &bind_address, &local_port, &host, &host_port)) {
            spice_main_port_forward_local(channel, bind_address, atoi(local_port),
                                          host, atoi(host_port));
        } else
            SPICE_DEBUG("Failed redirecting %s\n", *redirected_ports);
        g_free(redir);
        ++redirected_ports;
    }
}

/**
 * spice_main_port_forward_remote:
 * @bind_address: the address to bind to in the agent side.
 * @rport: the port forwarded in the agent side.
 * @host: the address to connect to in the local side.
 * @lport: the target port in the local side.
 *
 * Instructs the agent to start forwarding a port, and associate it with a
 * local port and address.
 *
 * Returns: a %TRUE on success, %FALSE on error.
 **/
gboolean spice_main_port_forward_remote(SpiceMainChannel *channel,
                                        const char *bind_address, uint16_t rport,
                                        const char *host, uint16_t lport)
{
    SpiceMainChannelPrivate *c = channel->priv;
    g_return_val_if_fail(c->agent_connected, FALSE);
    if (!test_agent_cap(channel, VD_AGENT_CAP_PORT_FORWARDING)) return FALSE;
    return port_forwarder_associate_remote(c->port_forwarder, bind_address,
                                           rport, host, lport);
}

/**
 * spice_main_port_forward_disassociate_remote:
 * @rport: the port forwarded in the agent side.
 *
 * Instructs the agent to stop forwarding a port.
 *
 * Returns: a %TRUE on success, %FALSE on error.
 **/
gboolean spice_main_port_forward_disassociate_remote(SpiceMainChannel *channel,
                                                     uint16_t rport)
{
    SpiceMainChannelPrivate *c = channel->priv;
    g_return_val_if_fail(c->agent_connected, FALSE);
    if (!test_agent_cap(channel, VD_AGENT_CAP_PORT_FORWARDING)) return FALSE;
    return port_forwarder_disassociate_remote(c->port_forwarder, rport);
}

/**
 * spice_main_port_forward_local:
 * @bind_address: the address to bind to in the local side.
 * @lport: the local port to listen on.
 * @host: the address to connect to in the agent side.
 * @rport: the port to connect to in the agent side.
 *
 * Instructs the agent to start forwarding a port, and associate it with a
 * local port.
 *
 * Returns: a %TRUE on success, %FALSE on error.
 **/
gboolean spice_main_port_forward_local(SpiceMainChannel *channel,
                                       const char *bind_address, uint16_t lport,
                                       const char *host, uint16_t rport)
{
    SpiceMainChannelPrivate *c = channel->priv;
    g_return_val_if_fail(c->agent_connected, FALSE);
    if (!test_agent_cap(channel, VD_AGENT_CAP_PORT_FORWARDING)) return FALSE;
    return port_forwarder_associate_local(c->port_forwarder, bind_address,
                                          lport, host, rport);
}

/**
 * spice_main_port_forward_disassociate_local:
 * @lport: the port forwarded in the local side.
 *
 * Instructs the agent to stop forwarding a port.
 *
 * Returns: a %TRUE on success, %FALSE on error.
 **/
gboolean spice_main_port_forward_disassociate_local(SpiceMainChannel *channel,
                                                    uint16_t lport)
{
    SpiceMainChannelPrivate *c = channel->priv;
    g_return_val_if_fail(c->agent_connected, FALSE);
    if (!test_agent_cap(channel, VD_AGENT_CAP_PORT_FORWARDING)) return FALSE;
    return port_forwarder_disassociate_local(c->port_forwarder, lport);
}

/**
 * spice_main_power_event_request:
 * @event_id: Power event being requested, of enum SpicePowerEvent
 *
 * Sends a power event request to the server, to perform a power action on the guest
 * (reset, shutdown, powerdown, ...)
 *
 **/
void spice_main_power_event_request(SpiceMainChannel *channel, SpicePowerEvent event_id) {
    if (spice_channel_test_capability(SPICE_CHANNEL(channel), SPICE_MAIN_CAP_POWER_REQUEST)) {
        SpiceMsgcMainPowerEventRequest msg_data;
        SpiceMsgOut *msg_out;

        msg_data.event_id = event_id;

        msg_out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_POWER_EVENT_REQUEST);
        msg_out->marshallers->msgc_main_power_event_request(msg_out->marshaller, &msg_data);
        spice_msg_out_send_internal(msg_out);
    }
}
