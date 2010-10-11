#include "spice-client.h"
#include "spice-common.h"

#include "spice-session-priv.h"

#include <spice/vd_agent.h>

#define SPICE_MAIN_CHANNEL_GET_PRIVATE(obj)                             \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_MAIN_CHANNEL, spice_main_channel))

struct spice_main_channel {
    enum SpiceMouseMode         mouse_mode;
    bool                        agent_connected;
    bool                        agent_caps_received;
    int                         agent_tokens;
    uint8_t                     *agent_msg;
    uint8_t                     *agent_msg_pos;
    uint8_t                     *agent_msg_size;
    uint32_t                    agent_caps[VD_AGENT_CAPS_SIZE];
    struct {
        int                     x;
        int                     y;
        int                     width;
        int                     height;
    } display[1];
};

G_DEFINE_TYPE(SpiceMainChannel, spice_main_channel, SPICE_TYPE_CHANNEL)

/* Properties */
enum {
    PROP_0,
    PROP_MOUSE_MODE,
    PROP_AGENT_CONNECTED,
    PROP_AGENT_CAPS_0,
};

/* Signals */
enum {
    SPICE_MAIN_MOUSE_UPDATE,
    SPICE_MAIN_AGENT_UPDATE,

    SPICE_MAIN_LAST_SIGNAL,
};

static guint signals[SPICE_MAIN_LAST_SIGNAL];

static void spice_main_handle_msg(SpiceChannel *channel, spice_msg_in *msg);

/* ------------------------------------------------------------------ */

static const char *agent_msg_types[] = {
    [ VD_AGENT_MOUSE_STATE             ] = "mouse state",
    [ VD_AGENT_MONITORS_CONFIG         ] = "monitors config",
    [ VD_AGENT_REPLY                   ] = "reply",
    [ VD_AGENT_CLIPBOARD               ] = "clipboard",
    [ VD_AGENT_DISPLAY_CONFIG          ] = "display config",
    [ VD_AGENT_ANNOUNCE_CAPABILITIES   ] = "announce caps",
#if 0
    [ VD_AGENT_CLIPBOARD_GRAB          ] = "clipboard grab",
    [ VD_AGENT_CLIPBOARD_REQUEST       ] = "clipboard request",
    [ VD_AGENT_CLIPBOARD_RELEASE       ] = "clipboard release",
#endif
};

static const char *agent_caps[] = {
    [ VD_AGENT_CAP_MOUSE_STATE         ] = "mouse state",
    [ VD_AGENT_CAP_MONITORS_CONFIG     ] = "monitors config",
    [ VD_AGENT_CAP_REPLY               ] = "reply",
    [ VD_AGENT_CAP_CLIPBOARD           ] = "clipboard (old)",
    [ VD_AGENT_CAP_DISPLAY_CONFIG      ] = "display config",
#if 0
    [ VD_AGENT_CAP_CLIPBOARD_BY_DEMAND ] = "clipboard",
#endif
};
#define NAME(_a, _i) ((_i) < SPICE_N_ELEMENTS(_a) ? (_a[(_i)] ?: "?") : "?")

/* ------------------------------------------------------------------ */

static void spice_main_channel_init(SpiceMainChannel *channel)
{
    spice_main_channel *c;

    fprintf(stderr, "%s\n", __FUNCTION__);

    c = channel->priv = SPICE_MAIN_CHANNEL_GET_PRIVATE(channel);
    memset(c, 0, sizeof(*c));
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
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
	break;
    }
}

static void spice_main_channel_finalize(GObject *obj)
{
    fprintf(stderr, "%s\n", __FUNCTION__);

    if (G_OBJECT_CLASS(spice_main_channel_parent_class)->finalize)
        G_OBJECT_CLASS(spice_main_channel_parent_class)->finalize(obj);
}

static void spice_main_channel_class_init(SpiceMainChannelClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    SpiceChannelClass *channel_class = SPICE_CHANNEL_CLASS(klass);

    fprintf(stderr, "%s\n", __FUNCTION__);

    gobject_class->finalize     = spice_main_channel_finalize;
    gobject_class->get_property = spice_main_get_property;
    channel_class->handle_msg   = spice_main_handle_msg;

    g_object_class_install_property
        (gobject_class, PROP_MOUSE_MODE,
         g_param_spec_int("mouse-mode",
                          "Mouse mode",
                          "",
                          0, INT_MAX, 0,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NAME |
                          G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB));

    g_object_class_install_property
        (gobject_class, PROP_AGENT_CONNECTED,
         g_param_spec_boolean("agent-connected",
                              "Agent connected",
                              "",
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

    signals[SPICE_MAIN_MOUSE_UPDATE] =
        g_signal_new("spice-main-mouse-update",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceMainChannelClass, spice_main_mouse_update),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    signals[SPICE_MAIN_AGENT_UPDATE] =
        g_signal_new("spice-main-agent-update",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceMainChannelClass, spice_main_agent_update),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE,
                     0);

    g_type_class_add_private(klass, sizeof(spice_main_channel));
}

/* ------------------------------------------------------------------ */

static void agent_msg_send(SpiceChannel *channel, int type, int size, void *data)
{
    spice_msg_out *out;
    VDAgentMessage *msg;
    void *payload;

    out = spice_msg_out_new(channel, SPICE_MSGC_MAIN_AGENT_DATA);
    msg = (VDAgentMessage*)
        spice_marshaller_reserve_space(out->marshaller, sizeof(VDAgentMessage));
    payload = (VDAgentMonitorsConfig*)
        spice_marshaller_reserve_space(out->marshaller, size);

    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = type;
    msg->opaque = 0;
    msg->size = size;
    memcpy(payload, data, size);

    spice_msg_out_send(out);
    spice_msg_out_unref(out);
}

static void agent_monitors_config(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
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

    size = sizeof(VDAgentMonitorsConfig) + sizeof(VDAgentMonConfig) * monitors;
    mon = spice_malloc0(size);

    mon->num_of_monitors = monitors;
    mon->flags = 0;
    for (i = 0; i < monitors; i++) {
        mon->monitors[i].depth  = 32;
        mon->monitors[i].width  = c->display[i].width;
        mon->monitors[i].height = c->display[i].height;
        mon->monitors[i].x = c->display[i].x;
        mon->monitors[i].y = c->display[i].y;
        fprintf(stderr, "%s: #%d %dx%d+%d+%d @ %d bpp\n", __FUNCTION__, i,
                mon->monitors[i].width, mon->monitors[i].height,
                mon->monitors[i].x, mon->monitors[i].y,
                mon->monitors[i].depth);
    }

    agent_msg_send(channel, VD_AGENT_MONITORS_CONFIG, size, mon);
    free(mon);
}

static void agent_announce_caps(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
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

    agent_msg_send(channel, VD_AGENT_ANNOUNCE_CAPABILITIES, size, caps);
    free(caps);
}

static void agent_clipboard_grab(SpiceChannel *channel, int *types, int ntypes)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    VDAgentClipboardGrab *grab;
    size_t size;
    int i;

    if (!c->agent_connected)
        return;

    size = sizeof(VDAgentClipboardGrab) + sizeof(uint32_t) * ntypes;
    grab = spice_malloc0(size);
    for (i = 0; i < ntypes; i++) {
        grab->types[i] = types[i];
    }

    agent_msg_send(channel, VD_AGENT_CLIPBOARD_GRAB, size, grab);
    free(grab);
}

static void agent_start(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    SpiceMsgcMainAgentStart agent_start = {
        .num_tokens = ~0,
    };
    spice_msg_out *out;

    c->agent_connected = true;
    c->agent_caps_received = false;
    g_signal_emit(channel, signals[SPICE_MAIN_AGENT_UPDATE], 0);

    out = spice_msg_out_new(channel, SPICE_MSGC_MAIN_AGENT_START);
    out->marshallers->msgc_main_agent_start(out->marshaller, &agent_start);
    spice_msg_out_send(out);
    spice_msg_out_unref(out);

    agent_announce_caps(channel);
    agent_monitors_config(channel);
}

static void agent_stopped(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    c->agent_connected = false;
    c->agent_caps_received = false;
    g_signal_emit(channel, signals[SPICE_MAIN_AGENT_UPDATE], 0);
}

static void set_mouse_mode(SpiceChannel *channel, uint32_t supported, uint32_t current)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    if (c->mouse_mode != current) {
        c->mouse_mode = current;
        g_signal_emit(channel, signals[SPICE_MAIN_MOUSE_UPDATE], 0);
    }

    /* switch to client mode if possible */
    if ((supported & SPICE_MOUSE_MODE_CLIENT) && (current != SPICE_MOUSE_MODE_CLIENT)) {
        SpiceMsgcMainMouseModeRequest req = {
            .mode = SPICE_MOUSE_MODE_CLIENT,
        };
        spice_msg_out *out;
        out = spice_msg_out_new(channel, SPICE_MSGC_MAIN_MOUSE_MODE_REQUEST);
        out->marshallers->msgc_main_mouse_mode_request(out->marshaller, &req);
        spice_msg_out_send(out);
        spice_msg_out_unref(out);
    }
}

static void main_handle_init(SpiceChannel *channel, spice_msg_in *in)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    SpiceMsgMainInit *init = spice_msg_in_parsed(in);
    SpiceSession *session;
    spice_msg_out *out;

    g_object_get(channel, "spice-session", &session, NULL);
    spice_session_set_connection_id(session, init->session_id);

    out = spice_msg_out_new(channel, SPICE_MSGC_MAIN_ATTACH_CHANNELS);
    spice_msg_out_send(out);
    spice_msg_out_unref(out);

    set_mouse_mode(channel, init->supported_mouse_modes, init->current_mouse_mode);

    c->agent_tokens = init->agent_tokens;
    if (init->agent_connected) {
        agent_start(channel);
    }

#if 0
    set_mm_time(init->multi_media_time);
#endif
}

static void main_handle_mm_time(SpiceChannel *channel, spice_msg_in *in)
{
    fprintf(stderr, "%s: TODO\n", __FUNCTION__);
#if 0
    set_mm_time(init->multi_media_time);
#endif
}

static void main_handle_channels_list(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgChannels *msg = spice_msg_in_parsed(in);
    SpiceSession *session;
    SpiceChannel *add;
    int i;

    g_object_get(channel, "spice-session", &session, NULL);
    for (i = 0; i < msg->num_of_channels; i++) {
        add = spice_channel_new(session, msg->channels[i].type,
                                msg->channels[i].id);
    }
}

static void main_handle_mouse_mode(SpiceChannel *channel, spice_msg_in *in)
{
    SpiceMsgMainMouseMode *msg = spice_msg_in_parsed(in);
    set_mouse_mode(channel, msg->supported_modes, msg->current_mode);
}

static void main_handle_agent_connected(SpiceChannel *channel, spice_msg_in *in)
{
    agent_start(channel);
}

static void main_handle_agent_disconnected(SpiceChannel *channel, spice_msg_in *in)
{
    agent_stopped(channel);
}

static void main_handle_agent_data(SpiceChannel *channel, spice_msg_in *in)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    VDAgentMessage *msg;
    void *payload;
    int len;

    spice_msg_in_hexdump(in);

    if (!c->agent_msg) {
        msg = spice_msg_in_raw(in, &len);
        assert(len > sizeof(VDAgentMessage));
        if (msg->size + sizeof(VDAgentMessage) > len) {
            fprintf(stderr, "%s: TODO: start buffer\n", __FUNCTION__);
        } else {
            assert(msg->size + sizeof(VDAgentMessage) == len);
            goto complete;
        }
    } else {
        fprintf(stderr, "%s: TODO: fill buffer\n", __FUNCTION__);
    }
    return;

complete:
    payload = (msg+1);
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
            fprintf(stderr, "%s: cap: %d (%s)\n", __FUNCTION__,
                    i, NAME(agent_caps, i));
            VD_AGENT_SET_CAPABILITY(c->agent_caps, i);
        }
        c->agent_caps_received = true;
        g_signal_emit(channel, signals[SPICE_MAIN_AGENT_UPDATE], 0);
        if (caps->request)
            agent_announce_caps(channel);
    }
    case VD_AGENT_REPLY:
    {
        VDAgentReply *reply = payload;
        fprintf(stderr, "%s: reply: type %d, %s\n", __FUNCTION__, reply->type,
                reply->error == VD_AGENT_SUCCESS ? "success" : "error");
        break;
    }
    default:
        fprintf(stderr, "unhandled agent message type: %u (%s), size %u\n",
                msg->type, NAME(agent_msg_types, msg->type), msg->size);
    }
}

static void main_handle_agent_token(SpiceChannel *channel, spice_msg_in *in)
{
    fprintf(stderr, "%s: TODO\n", __FUNCTION__);
}

static spice_msg_handler main_handlers[] = {
    [ SPICE_MSG_SET_ACK ]                  = spice_channel_handle_set_ack,
    [ SPICE_MSG_PING ]                     = spice_channel_handle_ping,
    [ SPICE_MSG_NOTIFY ]                   = spice_channel_handle_notify,

    [ SPICE_MSG_MAIN_INIT ]                = main_handle_init,
    [ SPICE_MSG_MAIN_CHANNELS_LIST ]       = main_handle_channels_list,
    [ SPICE_MSG_MAIN_MOUSE_MODE ]          = main_handle_mouse_mode,
    [ SPICE_MSG_MAIN_MULTI_MEDIA_TIME ]    = main_handle_mm_time,

    [ SPICE_MSG_MAIN_AGENT_CONNECTED ]     = main_handle_agent_connected,
    [ SPICE_MSG_MAIN_AGENT_DISCONNECTED ]  = main_handle_agent_disconnected,
    [ SPICE_MSG_MAIN_AGENT_DATA ]          = main_handle_agent_data,
    [ SPICE_MSG_MAIN_AGENT_TOKEN ]         = main_handle_agent_token,
};

static void spice_main_handle_msg(SpiceChannel *channel, spice_msg_in *msg)
{
    int type = spice_msg_in_type(msg);
    assert(type < SPICE_N_ELEMENTS(main_handlers));
    assert(main_handlers[type] != NULL);
    main_handlers[type](channel, msg);
}

void spice_main_set_display(SpiceChannel *channel, int id,
                            int x, int y, int width, int height)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    if (id < SPICE_N_ELEMENTS(c->display)) {
        c->display[id].x      = x;
        c->display[id].y      = y;
        c->display[id].width  = width;
        c->display[id].height = height;
        agent_monitors_config(channel);
    }
}

void spice_main_clipboard_grab(SpiceChannel *channel, int *types, int ntypes)
{
    agent_clipboard_grab(channel, types, ntypes);
}

void spice_main_clipboard_release(SpiceChannel *channel)
{
    fprintf(stderr, "%s: TODO\n", __FUNCTION__);
}
