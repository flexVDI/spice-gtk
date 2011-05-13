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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <glib/gi18n.h>

#include "spice-client.h"
#include "spice-common.h"
#include "spice-cmdline.h"

static char *host;
static char *port;
static char *tls_port;
static char *password;
static char *uri;
static char *ca_file;
static char *host_subject;
static char *certificate_db;
static char *certificates;

static GOptionEntry spice_entries[] = {
    {
        .long_name        = "uri",
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &uri,
        .description      = N_("Spice server uri"),
        .arg_description  = N_("<uri>"),
    },{
        .long_name        = "host",
        .short_name       = 'h',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &host,
        .description      = N_("Spice server address"),
        .arg_description  = N_("<host>"),
    },{
        .long_name        = "port",
        .short_name       = 'p',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &port,
        .description      = N_("Spice server port"),
        .arg_description  = N_("<port>"),
    },{
        .long_name        = "secure-port",
        .short_name       = 's',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &tls_port,
        .description      = N_("Spice server secure port"),
        .arg_description  = N_("<port>"),
    },{
        .long_name        = "ca-file",
        .arg              = G_OPTION_ARG_FILENAME,
        .arg_data         = &ca_file,
        .description      = N_("Truststore file for secure connections"),
        .arg_description  = N_("<file>"),
    },{
        .long_name        = "password",
        .short_name       = 'w',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &password,
        .description      = N_("Server password"),
        .arg_description  = N_("<password>"),
    },{
        .long_name        = "host-subject",
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &host_subject,
        .description      = N_("Subject of the host certificate (field=value pairs separated by commas)"),
        .arg_description  = N_("<host-subject>"),
    },{
#ifdef USE_SMARTCARD
        .long_name        = "certificates",
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &certificates,
        .description      = N_("Certificates to use for software smartcards (field=values separated by commas)"),
        .arg_description  = N_("<certificates>"),
    },{
        .long_name        = "certificate-db",
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &certificate_db,
        .description      = N_("Path to the local certificate database to use for software smartcard certificates"),
        .arg_description  = N_("<certificate-db>"),
    },{
#endif
        /* end of list */
    }
};

static GOptionGroup *spice_group;

GOptionGroup *spice_cmdline_get_option_group(void)
{
    if (spice_group == NULL) {
        spice_group = g_option_group_new("spice",
                                         _("Spice Options:"),
                                         _("Show spice Options"),
                                         NULL, NULL);
        g_option_group_add_entries(spice_group, spice_entries);
    }
    return spice_group;
}

void spice_cmdline_session_setup(SpiceSession *session)
{
    if (ca_file == NULL) {
        const char *homedir = g_getenv("HOME");
        if (!homedir)
            homedir = g_get_home_dir();
        ca_file = g_strdup_printf("%s/.spicec/spice_truststore.pem", homedir);
    }

    if (uri)
        g_object_set(session, "uri", uri, NULL);
    if (host)
        g_object_set(session, "host", host, NULL);
    if (port)
        g_object_set(session, "port", port, NULL);
    if (tls_port)
        g_object_set(session, "tls-port", tls_port, NULL);
    if (password)
        g_object_set(session, "password", password, NULL);
    if (ca_file)
        g_object_set(session, "ca-file", ca_file, NULL);
    if (host_subject)
        g_object_set(session, "cert-subject", host_subject, NULL);
    if (certificates) {
        GStrv certs_strv;
        certs_strv = g_strsplit(certificates, ",", -1);
        if (certs_strv)
            g_object_set(session, "certificates", certs_strv, NULL);
        g_strfreev(certs_strv);
    }
    if (certificate_db)
        g_object_set(session, "certificate-db", certificate_db, NULL);
}
