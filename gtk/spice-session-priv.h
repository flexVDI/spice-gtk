#ifndef __SPICE_CLIENT_SESSION_PRIV_H__
#define __SPICE_CLIENT_SESSION_PRIV_H__

void spice_session_set_connection_id(SpiceSession *session, int id);
int spice_session_get_connection_id(SpiceSession *session);

int spice_session_channel_connect(SpiceSession *session, bool use_tls);
void spice_session_channel_new(SpiceSession *session, SpiceChannel *channel);
void spice_session_channel_destroy(SpiceSession *session, SpiceChannel *channel);

#endif /* __SPICE_CLIENT_SESSION_PRIV_H__ */
