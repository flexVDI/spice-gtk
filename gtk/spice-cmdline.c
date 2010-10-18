#include "spice-client.h"
#include "spice-common.h"
#include "spice-cmdline.h"

static char *host;
static char *port;
static char *tls_port;
static char *password;
static char *uri;
static char *ca_file;

static GOptionEntry spice_entries[] = {
    {
        .long_name        = "uri",
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &uri,
        .description      = "spice server uri",
        .arg_description  = "<uri>",
    },{
        .long_name        = "host",
        .short_name       = 'h',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &host,
        .description      = "spice server address",
        .arg_description  = "<host>",
    },{
        .long_name        = "port",
        .short_name       = 'p',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &port,
        .description      = "spice server port",
        .arg_description  = "<port>",
    },{
        .long_name        = "secure-port",
        .short_name       = 's',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &tls_port,
        .description      = "spice server secure port",
        .arg_description  = "<port>",
    },{
        .long_name        = "ca-file",
        .arg              = G_OPTION_ARG_FILENAME,
        .arg_data         = &ca_file,
        .description      = "truststore file for secure connections",
        .arg_description  = "<file>",
    },{
        .long_name        = "password",
        .short_name       = 'w',
        .arg              = G_OPTION_ARG_STRING,
        .arg_data         = &password,
        .description      = "server password",
        .arg_description  = "<password>",
    },{
        /* end of list */
    }
};

static GOptionGroup *spice_group;

GOptionGroup *spice_cmdline_get_option_group(void)
{
    if (spice_group == NULL) {
        spice_group = g_option_group_new("spice",
                                         "Spice Options:",
                                         "Show spice Options",
                                         NULL, NULL);
        g_option_group_add_entries(spice_group, spice_entries);
    }
    return spice_group;
}

void spice_cmdline_session_setup(SpiceSession *session)
{
    if (ca_file == NULL) {
        char *home = getenv("HOME");
        size_t size = strlen(home) + 32;
        ca_file = malloc(size);
        snprintf(ca_file, size, "%s/.spicec/spice_truststore.pem", home);
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
}
