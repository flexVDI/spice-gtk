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
#ifndef __SPICE_CLIENT_SESSION_PRIV_H__
#define __SPICE_CLIENT_SESSION_PRIV_H__

G_BEGIN_DECLS

void spice_session_set_connection_id(SpiceSession *session, int id);
int spice_session_get_connection_id(SpiceSession *session);

int spice_session_channel_connect(SpiceSession *session, bool use_tls);
void spice_session_channel_new(SpiceSession *session, SpiceChannel *channel);
void spice_session_channel_destroy(SpiceSession *session, SpiceChannel *channel);

G_END_DECLS

#endif /* __SPICE_CLIENT_SESSION_PRIV_H__ */
