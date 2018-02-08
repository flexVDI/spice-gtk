#include <spice-client.h>

typedef struct {
    const gchar *port;
    const gchar *tls_port;
    const gchar *host;
    const gchar *username;
    const gchar *password;
    const gchar *uri_input;
    const gchar *uri_output;
    const gchar *message;
    const gchar *unix_path;
} TestCase;

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

static void test_session_uri_good(const TestCase *tests, const guint cases)
{
    SpiceSession *s;
    guint i;

    /* Set URI and check URI, port and tls_port */
    for (i = 0; i < cases; i++) {
        gchar *uri, *port, *tls_port, *host, *username, *password, *unix_path;

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
                     "unix-path", &unix_path,
                      NULL);
        g_assert_cmpstr(tests[i].uri_output ?: tests[i].uri_input, ==, uri);
        g_assert_cmpstr(tests[i].port, ==, port);
        g_assert_cmpstr(tests[i].tls_port, ==, tls_port);
        g_assert_cmpstr(tests[i].host, ==, host);
        g_assert_cmpstr(tests[i].username, ==, username);
        g_assert_cmpstr(tests[i].password, ==, password);
        g_test_assert_expected_messages();
        g_assert_cmpstr(tests[i].unix_path, ==, unix_path);
        g_clear_pointer(&uri, g_free);
        g_clear_pointer(&port, g_free);
        g_clear_pointer(&tls_port, g_free);
        g_clear_pointer(&host, g_free);
        g_clear_pointer(&username, g_free);
        g_clear_pointer(&password, g_free);
        g_clear_pointer(&unix_path, g_free);
        g_object_unref(s);
    }

    /* Set port and tls_port, check URI */
    for (i = 0; i < cases; i++) {
        gchar *uri;

        s = spice_session_new();
        g_object_set(s,
                     "port", tests[i].port,
                     "tls-port", tests[i].tls_port,
                     "host", tests[i].host,
                     "username", tests[i].username,
                     "password", tests[i].password,
                     "unix-path", tests[i].unix_path,
                      NULL);
        g_object_get(s, "uri", &uri, NULL);
        g_assert_cmpstr(tests[i].uri_output ?: tests[i].uri_input, ==, uri);
        g_clear_pointer(&uri, g_free);
        g_object_unref(s);
    }
}

static void test_session_uri_ipv4_good(void)
{
    const TestCase tests[] = {
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

    test_session_uri_good(tests, G_N_ELEMENTS(tests));
}

static void test_session_uri_ipv6_good(void)
{
    const TestCase tests[] = {
        /* Arguments with empty value */
        { "5900", NULL,
          "[2010:836B:4179::836B:4179]",
          NULL, NULL,
          "spice://[2010:836B:4179::836B:4179]?port=5900&tls-port=",
          "spice://[2010:836B:4179::836B:4179]?port=5900&" },
        { "5910", NULL,
          "[::192.9.5.5]",
          "user", NULL,
          "spice://user@[::192.9.5.5]?tls-port=&port=5910",
          "spice://[::192.9.5.5]?port=5910&" },
        { NULL, "5920",
          "[3ffe:2a00:100:7031::1]",
          "user", "password",
          "spice://user@[3ffe:2a00:100:7031::1]?tls-port=5920&port=&password=password",
          "spice://[3ffe:2a00:100:7031::1]?tls-port=5920",
          "password may be visible in process listings"},
        { NULL, "5930",
          "[1080:0:0:0:8:800:200C:417A]",
          NULL, NULL,
          "spice://[1080:0:0:0:8:800:200C:417A]?port=&tls-port=5930",
          "spice://[1080:0:0:0:8:800:200C:417A]?tls-port=5930" },
        { "42", NULL,
          "[FEDC:BA98:7654:3210:FEDC:BA98:7654:3210]",
          NULL, NULL,
          "spice://[FEDC:BA98:7654:3210:FEDC:BA98:7654:3210]:42",
          "spice://[FEDC:BA98:7654:3210:FEDC:BA98:7654:3210]?port=42&" },
        { "42", "5930",
          "[::192.9.5.5]",
          NULL, NULL,
          "spice://[::192.9.5.5]:42?tls-port=5930",
          "spice://[::192.9.5.5]?port=42&tls-port=5930" },
        { "42", "5930",
          "[::FFFF:129.144.52.38]",
          NULL, NULL,
          "spice://[::FFFF:129.144.52.38]:42?tls-port=5930",
          "spice://[::FFFF:129.144.52.38]?port=42&tls-port=5930" },
    };

    test_session_uri_good(tests, G_N_ELEMENTS(tests));
}

static void test_session_uri_unix_good(void)
{
    const TestCase tests[] = {
        { .uri_input = "spice+unix:///tmp/foo.sock",
          .unix_path = "/tmp/foo.sock" },
        /* perhaps not very clever, but this doesn't raise an error/warning */
        { .uri_input = "spice+unix://",
          .unix_path = "" },
        /* unix uri don't support passing password or other kind of options */
        { .uri_input = "spice+unix:///tmp/foo.sock?password=frobnicate",
          .unix_path = "/tmp/foo.sock?password=frobnicate" },
    };

    test_session_uri_good(tests, G_N_ELEMENTS(tests));
}

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/session/bad-uri", test_session_uri_bad);
    g_test_add_func("/session/good-ipv4-uri", test_session_uri_ipv4_good);
    g_test_add_func("/session/good-ipv6-uri", test_session_uri_ipv6_good);
    g_test_add_func("/session/good-unix", test_session_uri_unix_good);

    return g_test_run();
}
