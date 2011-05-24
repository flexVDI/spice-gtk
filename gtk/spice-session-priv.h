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

#include <glib.h>
#include <gio/gio.h>
#include "spice-channel-cache.h"
#include "decode.h"

G_BEGIN_DECLS

SpiceSession *spice_session_new_from_session(SpiceSession *session);

void spice_session_set_connection_id(SpiceSession *session, int id);
int spice_session_get_connection_id(SpiceSession *session);
gboolean spice_session_get_client_provided_socket(SpiceSession *session);

GSocket* spice_session_channel_open_host(SpiceSession *session, gboolean use_tls);
void spice_session_channel_new(SpiceSession *session, SpiceChannel *channel);
void spice_session_channel_destroy(SpiceSession *session, SpiceChannel *channel);
void spice_session_channel_migrate(SpiceSession *session, SpiceChannel *channel);

void spice_session_set_mm_time(SpiceSession *session, guint32 time);
guint32 spice_session_get_mm_time(SpiceSession *session);

void spice_session_switching_disconnect(SpiceSession *session);
void spice_session_set_migration(SpiceSession *session, SpiceSession *migration);
void spice_session_abort_migration(SpiceSession *session);
void spice_session_set_migration_state(SpiceSession *session, SpiceSessionMigration state);

void spice_session_set_port(SpiceSession *session, int port, gboolean tls);
void spice_session_get_pubkey(SpiceSession *session, guint8 **pubkey, guint *size);
guint spice_session_get_verify(SpiceSession *session);
const gchar* spice_session_get_password(SpiceSession *session);
const gchar* spice_session_get_host(SpiceSession *session);
const gchar* spice_session_get_cert_subject(SpiceSession *session);
const gchar* spice_session_get_ciphers(SpiceSession *session);
const gchar* spice_session_get_ca_file(SpiceSession *session);

void spice_session_get_caches(SpiceSession *session,
                              display_cache **images,
                              display_cache **palettes,
                              SpiceGlzDecoderWindow **glz_window);
void spice_session_palettes_clear(SpiceSession *session);
void spice_session_images_clear(SpiceSession *session);

G_END_DECLS

#endif /* __SPICE_CLIENT_SESSION_PRIV_H__ */
