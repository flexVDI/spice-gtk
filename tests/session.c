#include <spice-client.h>

static void test_session_uri_bad(void)
{
    SpiceSession *s;
    guint i;
    const struct {
        const gchar *uri;
        struct {
            const GLogLevelFlags log_level;
            const gchar *message;
        } messages[2];
    } uris[] = {
        {
            "scheme://host?port",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "Expected a URI scheme of 'spice://'*",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },{
            "spice://[ipv6-host:42",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "Missing closing ']' in authority for URI*",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },{
            "spice://host??",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "Failed to parse key in URI '?'",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },{
            "spice://host:5900?unknown=value",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "unknown key in spice URI parsing: 'unknown'",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },{
            "spice://hostname",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "Missing port or tls-port in spice URI*",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },{
            "spice://hostname?port=1234&port=3456",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "Double set of 'port' in URI*",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },{
            "spice://hostname?tls-port=1234&port=3456&tls-port=5678",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "Double set of 'tls-port' in URI*",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },{
            "spice://hostname:5900?tls-port=1234&port=3456",
            {
                {
                    G_LOG_LEVEL_WARNING,
                    "Double set of 'port' in URI*",
                },{
                    G_LOG_LEVEL_CRITICAL,
                    "*assertion 's->port != NULL || s->tls_port != NULL' failed",
                },
            }
        },
    };

    s = spice_session_new();

    for (i = 0; i < G_N_ELEMENTS(uris); i++) {
        gchar *uri = NULL;
        guint j;
        for (j = 0; j < G_N_ELEMENTS(uris[i].messages) && uris[i].messages[j].message != NULL; j++)
            g_test_expect_message(G_LOG_DOMAIN,
                                  uris[i].messages[j].log_level,
                                  uris[i].messages[j].message);
        g_object_set(s, "uri", uris[i].uri, NULL);
        g_object_get(s, "uri", &uri, NULL);
        g_test_assert_expected_messages();
        g_assert_cmpstr(uri, ==, NULL);
        g_free(uri);
    }

    g_object_unref(s);
}

static void test_session_uri_good(void)
{
    SpiceSession *s;
    guint i;

    struct {
        gchar *port;
        gchar *tls_port;
        gchar *host;
        gchar *username;
        gchar *password;
        gchar *uri_input;
        gchar *uri_output;
        gchar *message;
    } tests[] = {
        /* Arguments with empty value */
        { "5900", NULL,
          "localhost",
          NULL, NULL,
          "spice://localhost?port=5900&tls-port=",
          "spice://localhost?port=5900&" },
        { "5910", NULL,
          "localhost",
          "user", NULL,
          "spice://user@localhost?tls-port=&port=5910",
          "spice://localhost?port=5910&" },
        { NULL, "5920",
          "localhost",
          "user", "password",
          "spice://user@localhost?tls-port=5920&port=&password=password",
          "spice://localhost?tls-port=5920",
          "password may be visible in process listings"},
        { NULL, "5930",
          "localhost",
          NULL, NULL,
          "spice://localhost?port=&tls-port=5930",
          "spice://localhost?tls-port=5930" },
        { "42", NULL,
          "localhost",
          NULL, NULL,
          "spice://localhost:42",
          "spice://localhost?port=42&" },
        { "42", "5930",
          "localhost",
          NULL, NULL,
          "spice://localhost:42?tls-port=5930",
          "spice://localhost?port=42&tls-port=5930" },
        { "42", "5930",
          "127.0.0.1",
          NULL, NULL,
          "spice://127.0.0.1:42?tls-port=5930",
          "spice://127.0.0.1?port=42&tls-port=5930" },
    };

    /* Set URI and check URI, port and tls_port */
    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        gchar *uri, *port, *tls_port, *host, *username, *password;

        s = spice_session_new();
        if (tests[i].message != NULL)
            g_test_expect_message(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, tests[i].message);
        g_object_set(s, "uri", tests[i].uri_input, NULL);
        g_object_get(s,
                     "uri", &uri,
                     "port", &port,
                     "tls-port", &tls_port,
                     "host", &host,
                     "username", &username,
                     "password", &password,
                      NULL);
        g_assert_cmpstr(tests[i].uri_output, ==, uri);
        g_assert_cmpstr(tests[i].port, ==, port);
        g_assert_cmpstr(tests[i].tls_port, ==, tls_port);
        g_assert_cmpstr(tests[i].host, ==, host);
        g_assert_cmpstr(tests[i].username, ==, username);
        g_assert_cmpstr(tests[i].password, ==, password);
        g_test_assert_expected_messages();
        g_clear_pointer(&uri, g_free);
        g_clear_pointer(&port, g_free);
        g_clear_pointer(&tls_port, g_free);
        g_clear_pointer(&host, g_free);
        g_clear_pointer(&username, g_free);
        g_clear_pointer(&password, g_free);
        g_object_unref(s);
    }

    /* Set port and tls_port, check URI */
    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        gchar *uri;

        s = spice_session_new();
        g_object_set(s,
                     "port", tests[i].port,
                     "tls-port", tests[i].tls_port,
                     "host", tests[i].host,
                     "username", tests[i].username,
                     "password", tests[i].password,
                      NULL);
        g_object_get(s, "uri", &uri, NULL);
        g_assert_cmpstr(tests[i].uri_output, ==, uri);
        g_clear_pointer(&uri, g_free);
        g_object_unref(s);
    }
}

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/session/bad-uri", test_session_uri_bad);
    g_test_add_func("/session/good-uri", test_session_uri_good);

    return g_test_run();
}
