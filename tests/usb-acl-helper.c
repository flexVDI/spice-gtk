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
#include "usb-acl-helper.h"

typedef struct {
    SpiceUsbAclHelper *acl_helper;
    GCancellable *cancellable;
    GMainLoop *loop;
    guint timeout_source;
} Fixture;

gboolean abort_test(gpointer user_data)
{
    Fixture *fixture = user_data;
    g_cancellable_cancel(fixture->cancellable);
    fixture->timeout_source = 0;
    return G_SOURCE_REMOVE;
}

gboolean cancel_test(gpointer user_data)
{
    Fixture *fixture = user_data;
    g_cancellable_cancel(fixture->cancellable);
    return G_SOURCE_REMOVE;
}

static void data_setup(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    g_setenv("SPICE_USB_ACL_BINARY", TESTDIR"/test-mock-acl-helper", TRUE);
    fixture->cancellable = g_cancellable_new();
    fixture->acl_helper = spice_usb_acl_helper_new();
    fixture->loop = g_main_loop_new(NULL, FALSE);
    /* abort test after 2 seconds if it hasn't yet completed */
    fixture->timeout_source = g_timeout_add_seconds(2, abort_test, fixture);
}

static void data_teardown(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    if (fixture->timeout_source)
        g_source_remove(fixture->timeout_source);
    g_object_unref(fixture->cancellable);
    g_object_unref(fixture->acl_helper);
    g_main_loop_unref(fixture->loop);
    g_unsetenv("SPICE_USB_ACL_BINARY");
}


static void success_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    GError *error = NULL;
    if (!spice_usb_acl_helper_open_acl_finish(SPICE_USB_ACL_HELPER(source), result, &error))
        g_error("%s", error->message);
    g_main_loop_quit(f->loop);
}

static void test_acl_helper_success(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    spice_usb_acl_helper_open_acl_async(fixture->acl_helper, 1, 1,
                                        fixture->cancellable, success_cb, fixture);
    g_main_loop_run(fixture->loop);
}

static void spawn_fail_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    GError *error = NULL;
    gboolean success = spice_usb_acl_helper_open_acl_finish(SPICE_USB_ACL_HELPER(source), result, &error);
    g_assert(!success);
    g_assert (error->domain == G_SPAWN_ERROR);
    g_clear_error(&error);
    g_main_loop_quit(f->loop);
}

static void test_acl_helper_spawn_fail(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    g_setenv("SPICE_USB_ACL_BINARY", "does-not-exist", TRUE);
    spice_usb_acl_helper_open_acl_async(fixture->acl_helper, 1, 1,
                                        fixture->cancellable, spawn_fail_cb,
                                        fixture);
    g_main_loop_run(fixture->loop);
}

static void early_eof_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    GError *error = NULL;
    gboolean success = spice_usb_acl_helper_open_acl_finish(SPICE_USB_ACL_HELPER(source), result, &error);
    g_assert(!success);
    g_assert(error->domain == SPICE_CLIENT_ERROR);
    g_assert(error->code == SPICE_CLIENT_ERROR_FAILED);
    g_clear_error(&error);
    g_main_loop_quit(f->loop);
}

/* helper sends EOF before sending a response */
static void test_acl_helper_early_eof(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    g_setenv("TEST_EOF", "1", TRUE);
    spice_usb_acl_helper_open_acl_async(fixture->acl_helper, 1, 1,
                                        fixture->cancellable, early_eof_cb, fixture);
    g_main_loop_run(fixture->loop);
    g_unsetenv("TEST_EOF");
}

static void helper_canceled_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    GError *error = NULL;
    gboolean success = spice_usb_acl_helper_open_acl_finish(SPICE_USB_ACL_HELPER(source), result, &error);
    g_assert(!success);
    g_assert(error->domain == G_IO_ERROR);
    g_assert(error->code == G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    g_main_loop_quit(f->loop);
}

static void test_acl_helper_helper_canceled(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    g_setenv("TEST_RESPONSE", "CANCELED", TRUE);
    spice_usb_acl_helper_open_acl_async(fixture->acl_helper, 1, 1,
                                        fixture->cancellable, helper_canceled_cb, fixture);
    g_main_loop_run(fixture->loop);
    g_unsetenv("TEST_RESPONSE");
}

static void helper_error_response_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    GError *error = NULL;
    gboolean success = spice_usb_acl_helper_open_acl_finish(SPICE_USB_ACL_HELPER(source), result, &error);
    g_assert(!success);
    g_assert(error->domain == SPICE_CLIENT_ERROR);
    g_assert(error->code == SPICE_CLIENT_ERROR_FAILED);
    g_clear_error(&error);
    g_main_loop_quit(f->loop);
}

static void test_acl_helper_error_response(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    g_setenv("TEST_RESPONSE", "Not authorized", TRUE);
    spice_usb_acl_helper_open_acl_async(fixture->acl_helper, 1, 1,
                                        fixture->cancellable, helper_error_response_cb, fixture);
    g_main_loop_run(fixture->loop);
    g_unsetenv("TEST_RESPONSE");
}

static void client_canceled_cb(GObject *source, GAsyncResult *result, gpointer user_data)
{
    Fixture *f = user_data;
    GError *error = NULL;
    gboolean success = spice_usb_acl_helper_open_acl_finish(SPICE_USB_ACL_HELPER(source), result, &error);
    g_assert(!success);
    g_assert(error->domain == G_IO_ERROR);
    g_assert(error->code == G_IO_ERROR_CANCELLED);
    g_clear_error(&error);
    g_main_loop_quit(f->loop);
}

static void test_acl_helper_client_canceled(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    /* ensure that the acl-helper does not have respond, so we can cancel the
     * task before we get a response from the helper binary */
    g_setenv("TEST_NORESPONSE", "1", TRUE);
    spice_usb_acl_helper_open_acl_async(fixture->acl_helper, 1, 1,
                                        fixture->cancellable, client_canceled_cb, fixture);
    g_idle_add(cancel_test, fixture);
    g_main_loop_run(fixture->loop);
    g_unsetenv("TEST_NORESPONSE");
}

static void test_acl_helper_no_response(Fixture *fixture, gconstpointer user_data G_GNUC_UNUSED)
{
    /* ensure that the acl-helper does not have respond, so we can cancel the
     * task before we get a response from the helper binary */
    g_setenv("TEST_NORESPONSE", "1", TRUE);
    spice_usb_acl_helper_open_acl_async(fixture->acl_helper, 1, 1,
                                        fixture->cancellable, client_canceled_cb, fixture);
    g_main_loop_run(fixture->loop);
    g_unsetenv("TEST_NORESPONSE");
}

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);

    g_test_add("/usb-acl-helper/success", Fixture, NULL,
               data_setup, test_acl_helper_success, data_teardown);
    g_test_add("/usb-acl-helper/spawn-fail", Fixture, NULL,
               data_setup, test_acl_helper_spawn_fail, data_teardown);
    g_test_add("/usb-acl-helper/early-eof", Fixture, NULL,
               data_setup, test_acl_helper_early_eof, data_teardown);
    g_test_add("/usb-acl-helper/helper-canceled", Fixture, NULL,
               data_setup, test_acl_helper_helper_canceled, data_teardown);
    g_test_add("/usb-acl-helper/helper-error", Fixture, NULL,
               data_setup, test_acl_helper_error_response, data_teardown);
    g_test_add("/usb-acl-helper/client-canceled", Fixture, NULL,
               data_setup, test_acl_helper_client_canceled, data_teardown);
    g_test_add("/usb-acl-helper/no-response", Fixture, NULL,
               data_setup, test_acl_helper_no_response, data_teardown);
    /* additional possible test cases:
     * - unable to set nonblocking flag on io channel?
     * - unable to write bus number to helper binary
     * - unable to flush channel
     * - read_line from helper binary returns something other than G_IO_STATUS_NORMAL
     */

    return g_test_run ();
}

