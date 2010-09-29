#ifndef __SPICE_CLIENT_CHANNEL_PRIV_H__
#define __SPICE_CLIENT_CHANNEL_PRIV_H__

#include <openssl/ssl.h>

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
    spice_watch                 *watch;
    SSL_CTX                     *ctx;
    SSL                         *ssl;

    int                         protocol;
    int                         tls;

    int                         connection_id;
    int                         channel_id;
    int                         channel_type;
    int                         serial;
    SpiceLinkHeader             link_hdr;
    SpiceLinkMess               link_msg;
    SpiceLinkHeader             peer_hdr;
    SpiceLinkReply*             peer_msg;

    spice_msg_in                *msg_in;
    int                         message_ack_window;
    int                         message_ack_count;
};

#endif /* __SPICE_CLIENT_CHANNEL_PRIV_H__ */
