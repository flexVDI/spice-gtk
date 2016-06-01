/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2016 Red Hat, Inc.

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
#include <glib.h>
#include <spice-client.h>
#include "spice-uri-priv.h"

struct test_case {
    gchar *uri;
    gchar *scheme;
    gchar *hostname;
    guint port;
    gchar *user;
    gchar *password;
    gchar *error_msg;
};

static void test_spice_uri_bad(const struct test_case invalid_test_cases[], const guint cases_cnt)
{
    guint i;

    SpiceURI *uri = spice_uri_new();
    g_assert_nonnull(uri);

    for (i = 0; i < cases_cnt; i++) {
        GError *error = NULL;
        g_assert_false(spice_uri_parse(uri, invalid_test_cases[i].uri, &error));
        g_assert_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED);
        g_assert_cmpstr(error->message, ==, invalid_test_cases[i].error_msg);
        g_error_free(error);
    }

    g_object_unref(uri);
}

static void test_spice_uri_good(const struct test_case valid_test_cases[], const guint cases_cnt)
{
    guint i;

    SpiceURI *uri = spice_uri_new();
    g_assert_nonnull(uri);

    for (i = 0; i < cases_cnt; i++) {
        GError *error = NULL;
        g_assert_true(spice_uri_parse(uri, valid_test_cases[i].uri, &error));
        g_assert_cmpstr(spice_uri_get_scheme(uri), ==, valid_test_cases[i].scheme);
        g_assert_cmpstr(spice_uri_get_hostname(uri), ==, valid_test_cases[i].hostname);
        g_assert_cmpstr(spice_uri_get_user(uri), ==, valid_test_cases[i].user);
        g_assert_cmpstr(spice_uri_get_password(uri), ==, valid_test_cases[i].password);
        g_assert_cmpuint(spice_uri_get_port(uri), ==, valid_test_cases[i].port);
        g_assert_no_error(error);
    }

    g_object_unref(uri);
}

static void test_spice_uri_ipv4_bad(void)
{
    const struct test_case invalid_test_cases[] = {
        {"http://:80", "http", NULL, 80, NULL, NULL, "Invalid hostname in uri address"},
        {"http://", "http", NULL, 3128, NULL, NULL, "Invalid hostname in uri address"},
        {"http://127.0.0.1:port", "http", "127.0.0.1", 3128, NULL, NULL,
          "Invalid uri port: port"},
        {"http://127.0.0.1:", "http", "127.0.0.1", 3128, NULL, NULL, "Missing uri port"},
    };

    test_spice_uri_bad(invalid_test_cases, G_N_ELEMENTS(invalid_test_cases));
}

static void test_spice_uri_ipv4_good(void)
{
    const struct test_case valid_test_cases[] = {
        {"http://127.0.0.1/", "http", "127.0.0.1", 3128, NULL, NULL, NULL},
        {"https://127.0.0.1", "https", "127.0.0.1", 3129, NULL, NULL, NULL},
        {"127.0.0.1", "http", "127.0.0.1", 3128, NULL, NULL, NULL},
        {"http://user:password@host:80", "http", "host", 80, "user", "password", NULL},
        {"https://host:42", "https", "host", 42, NULL, NULL, NULL}, /* tests resetting of username & password */
    };

    test_spice_uri_good(valid_test_cases, G_N_ELEMENTS(valid_test_cases));
}

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/spice_uri/ipv4/bad-uri", test_spice_uri_ipv4_bad);
    g_test_add_func("/spice_uri/ipv4/good-uri", test_spice_uri_ipv4_good);

    return g_test_run();
}
