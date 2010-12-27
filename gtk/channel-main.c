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
#include "spice-client.h"
#include "spice-common.h"
#include "spice-marshal.h"

#include "spice-channel-priv.h"
#include "spice-session-priv.h"

#include <spice/vd_agent.h>

/**
 * SECTION:channel-main
 * @short_description: the main Spice channel
 * @title: Main Channel
 * @section_id:
 * @see_also: #SpiceChannel, and the GTK widget #SpiceDisplay
 * @stability: Stable
 * @include: channel-main.h
 *
 * The main channel is the Spice session control channel. It handles
 * communication initialization (channels list), migrations, mouse
 * modes, multimedia time, and agent communication.
 *
 *
 */

#define SPICE_MAIN_CHANNEL_GET_PRIVATE(obj)                             \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_MAIN_CHANNEL, spice_main_channel))

struct spice_main_channel {
    enum SpiceMouseMode         mouse_mode;
    bool                        agent_connected;
    bool                        agent_caps_received;

    gboolean                    agent_display_config_sent;
    guint8                      display_color_depth;
    gboolean                    display_disable_wallpaper:1;
    gboolean                    display_disable_font_smooth:1;
    gboolean                    display_disable_animation:1;
    gboolean                    display_set_color_depth:1;

    int                         agent_tokens;
    VDAgentMessage              agent_msg; /* partial msg reconstruction */
    guint8                      *agent_msg_data;
    uint8_t                     agent_msg_pos;
    uint8_t                     agent_msg_size;
    uint32_t                    agent_caps[VD_AGENT_CAPS_SIZE];
    struct {
        int                     x;
        int                     y;
        int                     width;
        int                     height;
    } display[1];
    gint                        timer_id;
    GQueue                      *agent_msg_queue;
};

G_DEFINE_TYPE(SpiceMainChannel, spice_main_channel, SPICE_TYPE_CHANNEL)

/* Properties */
enum {
    PROP_0,
    PROP_MOUSE_MODE,
    PROP_AGENT_CONNECTED,
    PROP_AGENT_CAPS_0,
    PROP_DISPLAY_DISABLE_WALLPAPER,
    PROP_DISPLAY_DISABLE_FONT_SMOOTH,
    PROP_DISPLAY_DISABLE_ANIMATION,
    PROP_DISPLAY_SET_COLOR_DEPTH,
    PROP_DISPLAY_COLOR_DEPTH,
};

/* Signals */
enum {
    SPICE_MAIN_MOUSE_UPDATE,
    SPICE_MAIN_AGENT_UPDATE,
    SPICE_MAIN_CLIPBOARD,
    SPICE_MAIN_CLIPBOARD_GRAB,
    SPICE_MAIN_CLIPBOARD_REQUEST,
    SPICE_MAIN_CLIPBOARD_RELEASE,
    SPICE_MAIN_LAST_SIGNAL,
};

static guint signals[SPICE_MAIN_LAST_SIGNAL];

static void spice_main_handle_msg(SpiceChannel *channel, spice_msg_in *msg);
static void agent_send_msg_queue(SpiceMainChannel *channel);

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
};

static const char *agent_caps[] = {
    [ VD_AGENT_CAP_MOUSE_STATE         ] = "mouse state",
    [ VD_AGENT_CAP_MONITORS_CONFIG     ] = "monitors config",
    [ VD_AGENT_CAP_REPLY               ] = "reply",
    [ VD_AGENT_CAP_CLIPBOARD           ] = "clipboard (old)",
    [ VD_AGENT_CAP_DISPLAY_CONFIG      ] = "display config",
    [ VD_AGENT_CAP_CLIPBOARD_BY_DEMAND ] = "clipboard",
};
#define NAME(_a, _i) ((_i) < SPICE_N_ELEMENTS(_a) ? (_a[(_i)] ?: "?") : "?")

/* ------------------------------------------------------------------ */

static void spice_main_channel_init(SpiceMainChannel *channel)
{
    spice_main_channel *c;

    c = channel->priv = SPICE_MAIN_CHANNEL_GET_PRIVATE(channel);
    memset(c, 0, sizeof(*c));
    c->agent_msg_queue = g_queue_new();
}

static void spice_main_get_property(GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(object)->priv;

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
    case PROP_DISPLAY_SET_COLOR_DEPTH:
        g_value_set_boolean(value, c->display_set_color_depth);
        break;
    case PROP_DISPLAY_COLOR_DEPTH:
        g_value_set_uint(value, c->display_color_depth);
        break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	break;
    }
}

static void spice_main_set_property(GObject *gobject, guint prop_id,
                                    const GValue *value, GParamSpec *pspec)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(gobject)->priv;

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
    case PROP_DISPLAY_SET_COLOR_DEPTH:
        c->display_set_color_depth = g_value_get_boolean(value);
        break;
    case PROP_DISPLAY_COLOR_DEPTH:
        c->display_color_depth = g_value_get_uint(value);
        break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
	break;
    }
}

static void spice_main_channel_finalize(GObject *obj)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(obj)->priv;

    if (c->timer_id) {
        g_source_remove(c->timer_id);
    }

    g_free(c->agent_msg_data);
    g_queue_free(c->agent_msg_queue);

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

static void spice_main_channel_class_init(SpiceMainChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    gobject_class->finalize     = spice_main_channel_finalize;
    gobject_class->get_property = spice_main_get_property;
    gobject_class->set_property = spice_main_set_property;

    channel_class->handle_msg    = spice_main_handle_msg;
    channel_class->iterate_write = spice_channel_iterate_write;

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
        (gobject_class, PROP_DISPLAY_SET_COLOR_DEPTH,
         g_param_spec_boolean("set-color-depth",
                              "set color depth",
                              "Set display color depth", FALSE,
                              G_PARAM_READWRITE |
                              G_PARAM_CONSTRUCT |
                              G_PARAM_STATIC_STRINGS));

    g_object_class_install_property
        (gobject_class, PROP_DISPLAY_COLOR_DEPTH,
         g_param_spec_uint("color-depth",
                           "Color depth",
                           "Color depth", 8, 32, 32,
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
     * SpiceMainChannel::main-clipboard:
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
     **/
    signals[SPICE_MAIN_CLIPBOARD] =
        g_signal_new("main-clipboard",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__UINT_POINTER_UINT,
                     G_TYPE_NONE,
                     3,
                     G_TYPE_UINT, G_TYPE_POINTER, G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-grab:
     * @main: the #SpiceMainChannel that emitted the signal
     * @types: the VD_AGENT_CLIPBOARD data types
     * @ntypes: the number of @types
     *
     * Inform when clipboard data is available from the guest, and for
     * which @types.
     **/
    signals[SPICE_MAIN_CLIPBOARD_GRAB] =
        g_signal_new("main-clipboard-grab",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_BOOLEAN__POINTER_UINT,
                     G_TYPE_BOOLEAN,
                     2,
                     G_TYPE_POINTER, G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-request:
     * @main: the #SpiceMainChannel that emitted the signal
     * @types: the VD_AGENT_CLIPBOARD request type
     * Returns: %TRUE if the request is successful
     *
     * Request clipbard data from the client.
     *
     **/
    signals[SPICE_MAIN_CLIPBOARD_REQUEST] =
        g_signal_new("main-clipboard-request",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_user_marshal_BOOLEAN__UINT,
                     G_TYPE_BOOLEAN,
                     1,
                     G_TYPE_UINT);

    /**
     * SpiceMainChannel::main-clipboard-release:
     * @main: the #SpiceMainChannel that emitted the signal
     *
     * Inform when the clipboard is released from the guest, when no
     * clipboard data is available from the guest.
     *
     **/
    signals[SPICE_MAIN_CLIPBOARD_RELEASE] =
        g_signal_new("main-clipboard-release",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_LAST,
                     0,
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    g_type_class_add_private(klass, sizeof(spice_main_channel));
}

/* signal trampoline---------------------------------------------------------- */

struct SPICE_MAIN_CLIPBOARD_RELEASE {
};

struct SPICE_MAIN_AGENT_UPDATE {
};

struct SPICE_MAIN_MOUSE_UPDATE {
};

struct SPICE_MAIN_CLIPBOARD {
    guint type;
    gpointer data;
    gsize size;
};

struct SPICE_MAIN_CLIPBOARD_GRAB {
    gpointer types;
    gsize ntypes;
    gboolean *ret;
};

struct SPICE_MAIN_CLIPBOARD_REQUEST {
    guint type;
    gboolean *ret;
};

/* main context */
static void do_emit_main_context(GObject *object, int signum, gpointer params)
{
    switch (signum) {
    case SPICE_MAIN_CLIPBOARD_RELEASE:
    case SPICE_MAIN_AGENT_UPDATE:
    case SPICE_MAIN_MOUSE_UPDATE: {
        g_signal_emit(object, signals[signum], 0);
        break;
    }
    case SPICE_MAIN_CLIPBOARD: {
        struct SPICE_MAIN_CLIPBOARD *p = params;
        g_signal_emit(object, signals[signum], 0,
                      p->type, p->data, p->size);
        break;
    }
    case SPICE_MAIN_CLIPBOARD_GRAB: {
        struct SPICE_MAIN_CLIPBOARD_GRAB *p = params;
        g_signal_emit(object, signals[signum], 0,
                      p->types, p->ntypes, p->ret);
        break;
    }
    case SPICE_MAIN_CLIPBOARD_REQUEST: {
        struct SPICE_MAIN_CLIPBOARD_REQUEST *p = params;
        g_signal_emit(object, signals[signum], 0,
                      p->type, p->ret);
        break;
    }
    default:
        g_warn_if_reached();
    }
}

/* coroutine context */
#define emit_main_context(object, event, args...)                       \
    G_STMT_START {                                                      \
        g_signal_emit_main_context(G_OBJECT(object), do_emit_main_context, \
                                   event, &((struct event) { args }));  \
    } G_STMT_END

/* ------------------------------------------------------------------ */

/* coroutine context */
static void agent_send_msg_queue(SpiceMainChannel *channel)
{
    spice_main_channel *c = channel->priv;
    spice_msg_out *out;

    while (c->agent_tokens > 0 &&
           !g_queue_is_empty(c->agent_msg_queue)) {
        c->agent_tokens--;
        out = g_queue_pop_head(c->agent_msg_queue);
        spice_msg_out_send_internal(out);
        spice_msg_out_unref(out);
    }
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_msg_queue(SpiceMainChannel *channel, int type, int size, void *data)
{
    spice_main_channel *c = channel->priv;
    spice_msg_out *out;
    VDAgentMessage *msg;
    void *payload;

    out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_AGENT_DATA);
    msg = (VDAgentMessage*)
        spice_marshaller_reserve_space(out->marshaller, sizeof(VDAgentMessage));
    payload = (VDAgentMonitorsConfig*)
        spice_marshaller_reserve_space(out->marshaller, size);

    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = type;
    msg->opaque = 0;
    msg->size = size;
    memcpy(payload, data, size);

    g_queue_push_tail(c->agent_msg_queue, out);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
void agent_monitors_config(SpiceMainChannel *channel)
{
    spice_main_channel *c = channel->priv;
    VDAgentMonitorsConfig *mon;
    int i, monitors = 1;
    size_t size;

    if (!c->agent_connected)
        return;
    for (i = 0; i < monitors; i++) {
        if (!c->display[i].width ||
            !c->display[i].height)
            return;
    }

    size = sizeof(VDAgentMonitorsConfig) + sizeof(VDAgentMonConfig) * monitors * 2;
    mon = spice_malloc0(size);

    mon->num_of_monitors = monitors;
    mon->flags = 0;
    for (i = 0; i < monitors; i++) {
        mon->monitors[i].depth  = 32;
        mon->monitors[i].width  = c->display[i].width;
        mon->monitors[i].height = c->display[i].height;
        mon->monitors[i].x = c->display[i].x;
        mon->monitors[i].y = c->display[i].y;
        SPICE_DEBUG("%s: #%d %dx%d+%d+%d @ %d bpp", __FUNCTION__, i,
                    mon->monitors[i].width, mon->monitors[i].height,
                    mon->monitors[i].x, mon->monitors[i].y,
                    mon->monitors[i].depth);
    }

    agent_msg_queue(channel, VD_AGENT_MONITORS_CONFIG, size, mon);
    free(mon);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_display_config(SpiceMainChannel *channel)
{
    spice_main_channel *c = channel->priv;
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

    if (c->display_set_color_depth) {
        config.flags |= VD_AGENT_DISPLAY_CONFIG_FLAG_SET_COLOR_DEPTH;
        config.depth = c->display_color_depth;
    }

#if 0 // TODO
    if (!_display_setting.is_empty()) {
        _agent_reply_wait_type = VD_AGENT_DISPLAY_CONFIG;
    }
#endif

    SPICE_DEBUG("display_config: flags: %u, depth: %u", config.flags, config.depth);

    agent_msg_queue(channel, VD_AGENT_DISPLAY_CONFIG, sizeof(VDAgentDisplayConfig), &config);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_announce_caps(SpiceMainChannel *channel)
{
    spice_main_channel *c = channel->priv;
    VDAgentAnnounceCapabilities *caps;
    size_t size;

    if (!c->agent_connected)
        return;

    size = sizeof(VDAgentAnnounceCapabilities) + VD_AGENT_CAPS_BYTES;
    caps = spice_malloc0(size);
    if (!c->agent_caps_received)
        caps->request = 1;
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_MOUSE_STATE);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_MONITORS_CONFIG);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_REPLY);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_DISPLAY_CONFIG);
    VD_AGENT_SET_CAPABILITY(caps->caps, VD_AGENT_CAP_CLIPBOARD_BY_DEMAND);

    agent_msg_queue(channel, VD_AGENT_ANNOUNCE_CAPABILITIES, size, caps);
    free(caps);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_grab(SpiceMainChannel *channel, guint32 *types, int ntypes)
{
    spice_main_channel *c = channel->priv;
    VDAgentClipboardGrab *grab;
    size_t size;
    int i;

    if (!c->agent_connected)
        return;

    g_return_if_fail(VD_AGENT_HAS_CAPABILITY(c->agent_caps,
        sizeof(c->agent_caps), VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));

    size = sizeof(VDAgentClipboardGrab) + sizeof(uint32_t) * ntypes;
    grab = spice_malloc0(size);
    for (i = 0; i < ntypes; i++) {
        grab->types[i] = types[i];
    }

    agent_msg_queue(channel, VD_AGENT_CLIPBOARD_GRAB, size, grab);
    free(grab);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_notify(SpiceMainChannel *channel,
                                   guint32 type, const guchar *data, size_t size)
{
    spice_main_channel *c = channel->priv;
    VDAgentClipboard *cb;
    size_t msgsize;

    g_return_if_fail(c->agent_connected);

    g_return_if_fail(VD_AGENT_HAS_CAPABILITY(c->agent_caps,
        sizeof(c->agent_caps), VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));

    msgsize = sizeof(VDAgentClipboard) + size;
    cb = spice_malloc0(msgsize);
    cb->type = type;
    memcpy(cb->data, data, size);

    agent_msg_queue(channel, VD_AGENT_CLIPBOARD, msgsize, cb);
    free(cb);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_request(SpiceMainChannel *channel, guint32 type)
{
    spice_main_channel *c = channel->priv;
    VDAgentClipboardRequest request = { 0, };

    g_return_if_fail(c->agent_connected);

    g_return_if_fail(VD_AGENT_HAS_CAPABILITY(c->agent_caps,
        sizeof(c->agent_caps), VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));

    request.type = type;

    agent_msg_queue(channel, VD_AGENT_CLIPBOARD_REQUEST, sizeof(VDAgentClipboardRequest), &request);
}

/* any context: the message is not flushed immediately,
   you can wakeup() the channel coroutine or send_msg_queue() */
static void agent_clipboard_release(SpiceMainChannel *channel)
{
    spice_main_channel *c = channel->priv;

    g_return_if_fail(c->agent_connected);

    g_return_if_fail(VD_AGENT_HAS_CAPABILITY(c->agent_caps,
        sizeof(c->agent_caps), VD_AGENT_CAP_CLIPBOARD_BY_DEMAND));

    agent_msg_queue(channel, VD_AGENT_CLIPBOARD_REQUEST, 0, NULL);
}

/* coroutine context  */
static void agent_start(SpiceMainChannel *channel)
{
    spice_main_channel *c = channel->priv;
    SpiceMsgcMainAgentStart agent_start = {
        .num_tokens = ~0,
    };
    spice_msg_out *out;

    c->agent_connected = true;
    c->agent_caps_received = false;
    emit_main_context(channel, SPICE_MAIN_AGENT_UPDATE);

    out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_AGENT_START);
    out->marshallers->msgc_main_agent_start(out->marshaller, &agent_start);
    spice_msg_out_send_internal(out);
    spice_msg_out_unref(out);

    agent_announce_caps(channel);
    agent_monitors_config(channel);
    agent_send_msg_queue(channel);
}

/* coroutine context  */
static void agent_stopped(SpiceMainChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    c->agent_connected = false;
    c->agent_caps_received = false;
    c->agent_display_config_sent = false;
    emit_main_context(channel, SPICE_MAIN_AGENT_UPDATE);
}

/* coroutine context */
static void set_mouse_mode(SpiceMainChannel *channel, uint32_t supported, uint32_t current)
{
    spice_main_channel *c = channel->priv;

    if (c->mouse_mode != current) {
        c->mouse_mode = current;
        emit_main_context(channel, SPICE_MAIN_MOUSE_UPDATE);
        g_object_notify_main_context(G_OBJECT(channel), "mouse-mode");
    }

    /* switch to client mode if possible */
    if ((supported & SPICE_MOUSE_MODE_CLIENT) && (current != SPICE_MOUSE_MODE_CLIENT)) {
        SpiceMsgcMainMouseModeRequest req = {
            .mode = SPICE_MOUSE_MODE_CLIENT,
        };
        spice_msg_out *out;
        out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_MOUSE_MODE_REQUEST);
        out->marshallers->msgc_main_mouse_mode_request(out->marshaller, &req);
        spice_msg_out_send_internal(out);
        spice_msg_out_unref(out);
    }
}

/* coroutine context */
static void main_handle_init(SpiceChannel *channel, spice_msg_in *in)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    SpiceMsgMainInit *init = spice_msg_in_parsed(in);
    SpiceSession *session;
    spice_msg_out *out;

    g_object_get(channel, "spice-session", &session, NULL);
    session = spice_channel_get_session(channel);
    spice_session_set_connection_id(session, init->session_id);

    out = spice_msg_out_new(SPICE_CHANNEL(channel), SPICE_MSGC_MAIN_ATTACH_CHANNELS);
    spice_msg_out_send_internal(out);
    spice_msg_out_unref(out);

    set_mouse_mode(SPICE_MAIN_CHANNEL(channel), init->supported_mouse_modes,
                   init->current_mouse_mode);

    c->agent_tokens = init->agent_tokens;
    if (init->agent_connected) {
        agent_start(SPICE_MAIN_CHANNEL(channel));
    }

    spice_session_set_mm_time(session, init->multi_media_time);
}

/* coroutine context */
static void main_handle_mm_time(SpiceChannel *channel, spice_msg_in *in)
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

static gboolean _channel_new(channel_new_t *c)
{
    SpiceChannel *channel;

    g_return_val_if_fail(c != NULL, FALSE);

    channel = spice_channel_new(c->session, c->type, c->id);

    g_warn_if_fail(channel != NULL);
    g_object_unref(c->session);
    g_free(c);

    return FALSE;
}

/* coroutine context */
static void main_handle_channels_list(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgChannels *msg = spice_msg_in_parsed(in);
    SpiceSession *session;
    int i;

    session = spice_channel_get_session(channel);
    for (i = 0; i < msg->num_of_channels; i++) {
        channel_new_t *c;

        c = g_new(channel_new_t, 1);
        c->session = g_object_ref(session);
        c->type = msg->channels[i].type;
        c->id = msg->channels[i].id;
        /* no need to explicitely switch to main context, since
           synchronous call is not needed. */
        g_idle_add((GSourceFunc)_channel_new, c);
    }
}

/* coroutine context */
static void main_handle_mouse_mode(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgMainMouseMode *msg = spice_msg_in_parsed(in);
    set_mouse_mode(SPICE_MAIN_CHANNEL(channel), msg->supported_modes, msg->current_mode);
}

/* coroutine context */
static void main_handle_agent_connected(SpiceChannel *channel, spice_msg_in *in)
{
    agent_start(SPICE_MAIN_CHANNEL(channel));
}

/* coroutine context */
static void main_handle_agent_disconnected(SpiceChannel *channel, spice_msg_in *in)
{
    agent_stopped(SPICE_MAIN_CHANNEL(channel));
}

/* coroutine context */
static void main_agent_handle_msg(SpiceChannel *channel,
                                  VDAgentMessage *msg, gpointer payload)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

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
        emit_main_context(channel, SPICE_MAIN_AGENT_UPDATE);

        if (caps->request)
            agent_announce_caps(SPICE_MAIN_CHANNEL(channel));

        if (VD_AGENT_HAS_CAPABILITY(caps->caps, sizeof(c->agent_caps), VD_AGENT_CAP_DISPLAY_CONFIG) &&
            !c->agent_display_config_sent) {
            agent_display_config(SPICE_MAIN_CHANNEL(channel));
            agent_send_msg_queue(SPICE_MAIN_CHANNEL(channel));
            c->agent_display_config_sent = true;
        }
        break;
    }
    case VD_AGENT_CLIPBOARD:
    {
        VDAgentClipboard *cb = payload;
        emit_main_context(channel, SPICE_MAIN_CLIPBOARD,
                          cb->type, cb->data, msg->size - sizeof(VDAgentClipboard));
        break;
    }
    case VD_AGENT_CLIPBOARD_GRAB:
    {
        gboolean ret;
        emit_main_context(channel, SPICE_MAIN_CLIPBOARD_GRAB,
                          payload, msg->size / sizeof(uint32_t), &ret);
        break;
    }
    case VD_AGENT_CLIPBOARD_REQUEST:
    {
        gboolean ret;
        VDAgentClipboardRequest *req = payload;
        emit_main_context(channel, SPICE_MAIN_CLIPBOARD_REQUEST,
                          req->type, &ret);
        break;
    }
    case VD_AGENT_CLIPBOARD_RELEASE:
    {
        emit_main_context(channel, SPICE_MAIN_CLIPBOARD_RELEASE);
        break;
    }
    case VD_AGENT_REPLY:
    {
        VDAgentReply *reply = payload;
        SPICE_DEBUG("%s: reply: type %d, %s", __FUNCTION__, reply->type,
                    reply->error == VD_AGENT_SUCCESS ? "success" : "error");
        break;
    }
    default:
        g_warning("unhandled agent message type: %u (%s), size %u",
                  msg->type, NAME(agent_msg_types, msg->type), msg->size);
    }
}

/* coroutine context */
static void main_handle_agent_data_msg(SpiceChannel *channel, guint* msg_size, guchar** msg_pos)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    int n;

    if (c->agent_msg_pos < sizeof(VDAgentMessage)) {
        n = MIN(sizeof(VDAgentMessage) - c->agent_msg_pos, *msg_size);
        memcpy((uint8_t*)&c->agent_msg + c->agent_msg_pos, *msg_pos, n);
        c->agent_msg_pos += n;
        *msg_size -= n;
        *msg_pos += n;
        if (c->agent_msg_pos == sizeof(VDAgentMessage)) {
            SPICE_DEBUG("agent msg start: msg_size=%d, protocol=%d, type=%d",
                        c->agent_msg.size, c->agent_msg.protocol, c->agent_msg.type);
            g_return_if_fail(c->agent_msg.protocol == VD_AGENT_PROTOCOL);
            g_return_if_fail(c->agent_msg_data == NULL);
            c->agent_msg_data = g_malloc(c->agent_msg.size);
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
static void main_handle_agent_data(SpiceChannel *channel, spice_msg_in *in)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    VDAgentMessage *msg;
    guint msg_size;
    guchar* msg_pos;
    int len;

    msg = spice_msg_in_raw(in, &len);
    msg_size = msg->size;
    msg_pos = msg->data;

    if (c->agent_msg_pos == 0 &&
        msg_size + sizeof(VDAgentMessage) == len) {
        main_agent_handle_msg(channel, msg, msg + 1);
        return;
    }

    while (msg_size > 0) {
        main_handle_agent_data_msg(channel, &msg_size, &msg_pos);
    }
}

/* coroutine context */
static void main_handle_agent_token(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgMainAgentTokens *tokens = spice_msg_in_parsed(in);
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    c->agent_tokens = tokens->num_tokens;
    agent_send_msg_queue(SPICE_MAIN_CHANNEL(channel));
}

/* coroutine context */
static void main_handle_migrate_begin(SpiceChannel *channel, spice_msg_in *in)
{
    /* SpiceMsgMainMigrationBegin *mig = spice_msg_in_parsed(in); */

    g_warning("%s: TODO", __FUNCTION__);
}

/* coroutine context */
static void main_handle_migrate_switch_host(SpiceChannel *channel, spice_msg_in *in)
{
    /* SpiceMsgMainMigrationSwitchHost *mig = spice_msg_in_parsed(in); */

    g_warning("%s: TODO", __FUNCTION__);
}

/* coroutine context */
static void main_handle_migrate_cancel(SpiceChannel *channel,
                                       spice_msg_in *in G_GNUC_UNUSED)
{
    g_warning("%s: TODO", __FUNCTION__);
}

static spice_msg_handler main_handlers[] = {
    [ SPICE_MSG_SET_ACK ]                  = spice_channel_handle_set_ack,
    [ SPICE_MSG_PING ]                     = spice_channel_handle_ping,
    [ SPICE_MSG_NOTIFY ]                   = spice_channel_handle_notify,
    [ SPICE_MSG_DISCONNECTING ]            = spice_channel_handle_disconnect,
    [ SPICE_MSG_WAIT_FOR_CHANNELS ]        = spice_channel_handle_wait_for_channels,
    [ SPICE_MSG_MIGRATE ]                  = spice_channel_handle_migrate,

    [ SPICE_MSG_MAIN_INIT ]                = main_handle_init,
    [ SPICE_MSG_MAIN_CHANNELS_LIST ]       = main_handle_channels_list,
    [ SPICE_MSG_MAIN_MOUSE_MODE ]          = main_handle_mouse_mode,
    [ SPICE_MSG_MAIN_MULTI_MEDIA_TIME ]    = main_handle_mm_time,

    [ SPICE_MSG_MAIN_AGENT_CONNECTED ]     = main_handle_agent_connected,
    [ SPICE_MSG_MAIN_AGENT_DISCONNECTED ]  = main_handle_agent_disconnected,
    [ SPICE_MSG_MAIN_AGENT_DATA ]          = main_handle_agent_data,
    [ SPICE_MSG_MAIN_AGENT_TOKEN ]         = main_handle_agent_token,

    [ SPICE_MSG_MAIN_MIGRATE_BEGIN ]       = main_handle_migrate_begin,
    [ SPICE_MSG_MAIN_MIGRATE_CANCEL ]      = main_handle_migrate_cancel,
    [ SPICE_MSG_MAIN_MIGRATE_SWITCH_HOST ] = main_handle_migrate_switch_host,
};

/* coroutine context */
static void spice_main_handle_msg(SpiceChannel *channel, spice_msg_in *msg)
{
    int type = spice_msg_in_type(msg);

    g_return_if_fail(type < SPICE_N_ELEMENTS(main_handlers));
    g_return_if_fail(main_handlers[type] != NULL);

    main_handlers[type](channel, msg);
}

/* system context*/
static gboolean timer_set_display(gpointer data)
{
    SpiceChannel *channel = data;
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    c->timer_id = 0;
    agent_monitors_config(SPICE_MAIN_CHANNEL(channel));
    spice_channel_wakeup(channel);

    return false;
}

/**
 * spice_main_set_display:
 * @channel:
 * @id: display channel ID
 * @x: x position
 * @y: y position
 * @width: display width
 * @height: display height
 *
 * Notify the guest of screen resolution change. The notification is
 * sent 1 second later, if no further changes happen.
 **/
void spice_main_set_display(SpiceMainChannel *channel, int id,
                            int x, int y, int width, int height)
{
    spice_main_channel *c;

    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    c = SPICE_MAIN_CHANNEL(channel)->priv;

    g_return_if_fail(id < SPICE_N_ELEMENTS(c->display));

    c->display[id].x      = x;
    c->display[id].y      = y;
    c->display[id].width  = width;
    c->display[id].height = height;

    if (c->timer_id) {
        g_source_remove(c->timer_id);
    }
    c->timer_id = g_timeout_add_seconds(1, timer_set_display, channel);
}

/**
 * spice_main_clipboard_grab:
 * @channel:
 * @types: an array of #VD_AGENT_CLIPBOARD types available in the clipboard
 * @ntypes: the number of @types
 *
 * Grab the guest clipboard, with #VD_AGENT_CLIPBOARD @types.
 **/
void spice_main_clipboard_grab(SpiceMainChannel *channel, guint32 *types, int ntypes)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    agent_clipboard_grab(channel, types, ntypes);
    spice_channel_wakeup(SPICE_CHANNEL(channel));
}

/**
 * spice_main_clipboard_release:
 * @channel:
 *
 * Release the clipboard (for example, when the client looses the
 * clipboard grab): Inform the guest no clipboard data is available.
 **/
void spice_main_clipboard_release(SpiceMainChannel *channel)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    agent_clipboard_release(channel);
    spice_channel_wakeup(SPICE_CHANNEL(channel));
}

/**
 * spice_main_clipboard_notify:
 * @channel:
 * @type: a #VD_AGENT_CLIPBOARD type
 * @data: clipboard data
 * @size: data length in bytes
 *
 * Send the clipboard data to the guest.
 **/
void spice_main_clipboard_notify(SpiceMainChannel *channel,
                                 guint32 type, const guchar *data, size_t size)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    agent_clipboard_notify(channel, type, data, size);
    spice_channel_wakeup(SPICE_CHANNEL(channel));
}

/**
 * spice_main_clipboard_request:
 * @channel:
 * @type: a #VD_AGENT_CLIPBOARD type
 *
 * Request clipboard data of @type from the guest. The reply is sent
 * through the #SpiceMainChannel::main-clipboard signal.
 **/
void spice_main_clipboard_request(SpiceMainChannel *channel, guint32 type)
{
    g_return_if_fail(channel != NULL);
    g_return_if_fail(SPICE_IS_MAIN_CHANNEL(channel));

    agent_clipboard_request(channel, type);
    spice_channel_wakeup(SPICE_CHANNEL(channel));
}
