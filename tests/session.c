#include <spice-client.h>

static void test_session_uri(void)
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

    g_test_add_func("/session/uri", test_session_uri);

    return g_test_run();
}
