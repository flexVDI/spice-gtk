#ifndef __SPICE_CLIENT_CHANNEL_PRIV_H__
#define __SPICE_CLIENT_CHANNEL_PRIV_H__

#include <openssl/ssl.h>

struct spice_msg_out {
    int                   refcount;
    SpiceChannel          *channel;
    SpiceMessageMarshallers *marshallers;
    SpiceMarshaller       *marshaller;
    SpiceDataHeader       *header;
};

struct spice_msg_in {
    int                   refcount;
    SpiceChannel          *channel;
    SpiceDataHeader       header;
    uint8_t               *data;
    int                   hpos,dpos;
    uint8_t               *parsed;
    size_t                psize;
    message_destructor_t  pfree;
    spice_msg_in          *parent;
};

enum spice_channel_state {
    SPICE_CHANNEL_STATE_UNCONNECTED = 0,
    SPICE_CHANNEL_STATE_TLS,
    SPICE_CHANNEL_STATE_LINK_HDR,
    SPICE_CHANNEL_STATE_LINK_MSG,
    SPICE_CHANNEL_STATE_AUTH,
    SPICE_CHANNEL_STATE_READY,
};

struct spice_channel {
    SpiceSession                *session;
    char                        name[16];
    enum spice_channel_state    state;
    int                         socket;
    spice_parse_channel_func_t  parser;
    SpiceMessageMarshallers     *marshallers;
    GIOChannel                  *channel;
    guint                       channel_watch;
    SSL_CTX                     *ctx;
    SSL                         *ssl;
    int                         tls;

    int                         connection_id;
    int                         channel_id;
    int                         channel_type;
    int                         serial;
    SpiceLinkHeader             link_hdr;
    SpiceLinkMess               link_msg;
    SpiceLinkHeader             peer_hdr;
    SpiceLinkReply*             peer_msg;
    int                         peer_pos;

    spice_msg_in                *msg_in;
    int                         message_ack_window;
    int                         message_ack_count;
};

spice_msg_in *spice_msg_in_new(SpiceChannel *channel);
spice_msg_in *spice_msg_in_sub_new(SpiceChannel *channel, spice_msg_in *parent,
                                   SpiceSubMessage *sub);
void spice_msg_in_ref(spice_msg_in *in);
void spice_msg_in_unref(spice_msg_in *in);
int spice_msg_in_type(spice_msg_in *in);
void *spice_msg_in_parsed(spice_msg_in *in);
void *spice_msg_in_raw(spice_msg_in *in, int *len);
void spice_msg_in_hexdump(spice_msg_in *in);

spice_msg_out *spice_msg_out_new(SpiceChannel *channel, int type);
void spice_msg_out_ref(spice_msg_out *out);
void spice_msg_out_unref(spice_msg_out *out);
void spice_msg_out_send(spice_msg_out *out);
void spice_msg_out_hexdump(spice_msg_out *out, unsigned char *data, int len);

/* channel-base.c */
void spice_channel_handle_set_ack(SpiceChannel *channel, spice_msg_in *in);
void spice_channel_handle_ping(SpiceChannel *channel, spice_msg_in *in);
void spice_channel_handle_notify(SpiceChannel *channel, spice_msg_in *in);

#endif /* __SPICE_CLIENT_CHANNEL_PRIV_H__ */
