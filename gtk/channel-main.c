#include "spice-client.h"
#include "spice-common.h"

#include "spice-session-priv.h"

#include <spice/vd_agent.h>

#define SPICE_MAIN_CHANNEL_GET_PRIVATE(obj)                             \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj), SPICE_TYPE_MAIN_CHANNEL, spice_main_channel))

struct spice_main_channel {
    enum SpiceMouseMode         mouse_mode;
    int                         agent_connected;
    int                         agent_tokens;
    uint8_t                     *agent_msg;
    uint8_t                     *agent_msg_pos;
    uint8_t                     *agent_msg_size;
    struct {
        int                     x;
        int                     y;
        int                     width;
        int                     height;
    } display[1];
};

G_DEFINE_TYPE(SpiceMainChannel, spice_main_channel, SPICE_TYPE_CHANNEL)

enum {
    SPICE_MAIN_MOUSE_MODE,
    SPICE_MAIN_AGENT_EVENT,

    SPICE_MAIN_LAST_SIGNAL,
};

static guint signals[SPICE_MAIN_LAST_SIGNAL];

static void spice_main_handle_msg(SpiceChannel *channel, spice_msg_in *msg);

/* ------------------------------------------------------------------ */

static void spice_main_channel_init(SpiceMainChannel *channel)
{
    spice_main_channel *c;

    fprintf(stderr, "%s\n", __FUNCTION__);

    c = channel->priv = SPICE_MAIN_CHANNEL_GET_PRIVATE(channel);
    memset(c, 0, sizeof(*c));
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
    channel_class->handle_msg   = spice_main_handle_msg;

    signals[SPICE_MAIN_MOUSE_MODE] =
        g_signal_new("spice-main-mouse-mode",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceMainChannelClass, spice_main_mouse_mode),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    signals[SPICE_MAIN_AGENT_EVENT] =
        g_signal_new("spice-main-agent-event",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceMainChannelClass, spice_main_agent_event),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__INT,
                     G_TYPE_NONE,
                     1,
                     G_TYPE_INT);

    g_type_class_add_private(klass, sizeof(spice_main_channel));
}

/* ------------------------------------------------------------------ */

static void agent_monitors_config(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    spice_msg_out *out;
    VDAgentMessage* msg;
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
    out = spice_msg_out_new(channel, SPICE_MSGC_MAIN_AGENT_DATA);
    msg = (VDAgentMessage*)
        spice_marshaller_reserve_space(out->marshaller, sizeof(VDAgentMessage));
    mon = (VDAgentMonitorsConfig*)
        spice_marshaller_reserve_space(out->marshaller, size);

    msg->protocol = VD_AGENT_PROTOCOL;
    msg->type = VD_AGENT_MONITORS_CONFIG;
    msg->opaque = 0;
    msg->size = size;

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

    spice_msg_out_send(out);
    spice_msg_out_put(out);
}

static void agent_start(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    SpiceMsgcMainAgentStart agent_start = {
        .num_tokens = ~0,
    };
    spice_msg_out *out;

    c->agent_connected = true;
    g_signal_emit(channel, signals[SPICE_MAIN_AGENT_EVENT], 0,
                  SPICE_AGENT_CONNECT);

    out = spice_msg_out_new(channel, SPICE_MSGC_MAIN_AGENT_START);
    out->marshallers->msgc_main_agent_start(out->marshaller, &agent_start);
    spice_msg_out_send(out);
    spice_msg_out_put(out);

    agent_monitors_config(channel);
}

static void agent_stopped(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    c->agent_connected = false;
    g_signal_emit(channel, signals[SPICE_MAIN_AGENT_EVENT], 0,
                  SPICE_AGENT_DISCONNECT);
}

static void set_mouse_mode(SpiceChannel *channel, uint32_t supported, uint32_t current)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;

    if (c->mouse_mode != current) {
        c->mouse_mode = current;
        g_signal_emit(channel, signals[SPICE_MAIN_MOUSE_MODE], 0, current);
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
        spice_msg_out_put(out);
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
    spice_msg_out_put(out);

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
    switch (msg->type) {
    case VD_AGENT_REPLY:
    {
        VDAgentReply *reply = (VDAgentReply*)(msg+1);
        fprintf(stderr, "%s: reply: type %d, %s\n", __FUNCTION__, reply->type,
                reply->error == VD_AGENT_SUCCESS ? "success" : "error");
        break;
    }
    case VD_AGENT_CLIPBOARD:
        fprintf(stderr, "%s: clipboard\n", __FUNCTION__);
        break;
    default:
        fprintf(stderr, "unsupported agent message type %u size %u\n",
                msg->type, msg->size);
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

enum SpiceMouseMode spice_main_get_mouse_mode(SpiceChannel *channel)
{
    spice_main_channel *c = SPICE_MAIN_CHANNEL(channel)->priv;
    return c->mouse_mode;
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
    fprintf(stderr, "%s: TODO (%d types)\n", __FUNCTION__, ntypes);
}

void spice_main_clipboard_release(SpiceChannel *channel)
{
    fprintf(stderr, "%s: TODO\n", __FUNCTION__);
}
