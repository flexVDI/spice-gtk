/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010-2015 Red Hat, Inc.

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

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "spice-common.h"
#include "spicy-connect.h"

static struct {
    const char *text;
    const char *prop;
    GtkWidget *entry;
} connect_entries[] = {
    { .text = N_("Hostname"),   .prop = "host"      },
    { .text = N_("Port"),       .prop = "port"      },
    { .text = N_("TLS Port"),   .prop = "tls-port"  },
};

#ifndef G_OS_WIN32
static void recent_selection_changed_dialog_cb(GtkRecentChooser *chooser, gpointer data)
{
    GtkRecentInfo *info;
    gchar *txt = NULL;
    const gchar *uri;
    SpiceSession *session = data;

    info = gtk_recent_chooser_get_current_item(chooser);
    if (info == NULL)
        return;

    uri = gtk_recent_info_get_uri(info);
    g_return_if_fail(uri != NULL);

    g_object_set(session, "uri", uri, NULL);

    g_object_get(session, "host", &txt, NULL);
    gtk_entry_set_text(GTK_ENTRY(connect_entries[0].entry), txt ? txt : "");
    g_free(txt);

    g_object_get(session, "port", &txt, NULL);
    gtk_entry_set_text(GTK_ENTRY(connect_entries[1].entry), txt ? txt : "");
    g_free(txt);

    g_object_get(session, "tls-port", &txt, NULL);
    gtk_entry_set_text(GTK_ENTRY(connect_entries[2].entry), txt ? txt : "");
    g_free(txt);

    gtk_recent_info_unref(info);
}

static void recent_item_activated_dialog_cb(GtkRecentChooser *chooser, gpointer data)
{
   gtk_dialog_response (GTK_DIALOG (data), GTK_RESPONSE_ACCEPT);
}
#endif

int spicy_connect_dialog(SpiceSession *session)
{
    GtkWidget *dialog, *area, *label;
    GtkTable *table;
    int i, retval;

    /* Create the widgets */
    dialog = gtk_dialog_new_with_buttons(_("Connect to SPICE"),
                                         NULL,
                                         GTK_DIALOG_DESTROY_WITH_PARENT,
                                         GTK_STOCK_CANCEL,
                                         GTK_RESPONSE_REJECT,
                                         GTK_STOCK_CONNECT,
                                         GTK_RESPONSE_ACCEPT,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    table = GTK_TABLE(gtk_table_new(3, 2, 0));
    gtk_box_pack_start(GTK_BOX(area), GTK_WIDGET(table), TRUE, TRUE, 0);
    gtk_table_set_row_spacings(table, 5);
    gtk_table_set_col_spacings(table, 5);

    for (i = 0; i < SPICE_N_ELEMENTS(connect_entries); i++) {
        gchar *txt;
        label = gtk_label_new(connect_entries[i].text);
        gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
        gtk_table_attach_defaults(table, label, 0, 1, i, i+1);
        connect_entries[i].entry = GTK_WIDGET(gtk_entry_new());
        gtk_table_attach_defaults(table, connect_entries[i].entry, 1, 2, i, i+1);
        g_object_get(session, connect_entries[i].prop, &txt, NULL);
        SPICE_DEBUG("%s: #%i [%s]: \"%s\"",
                __FUNCTION__, i, connect_entries[i].prop, txt);
        if (txt) {
            gtk_entry_set_text(GTK_ENTRY(connect_entries[i].entry), txt);
            g_free(txt);
        }
    }

    label = gtk_label_new(_("Recent connections:"));
    gtk_box_pack_start(GTK_BOX(area), label, TRUE, TRUE, 0);
    gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
#ifndef G_OS_WIN32
    GtkRecentFilter *rfilter;
    GtkWidget *recent;

    recent = GTK_WIDGET(gtk_recent_chooser_widget_new());
    gtk_recent_chooser_set_show_icons(GTK_RECENT_CHOOSER(recent), FALSE);
    gtk_box_pack_start(GTK_BOX(area), recent, TRUE, TRUE, 0);

    rfilter = gtk_recent_filter_new();
    gtk_recent_filter_add_mime_type(rfilter, "application/x-spice");
    gtk_recent_chooser_set_filter(GTK_RECENT_CHOOSER(recent), rfilter);
    gtk_recent_chooser_set_local_only(GTK_RECENT_CHOOSER(recent), FALSE);
    g_signal_connect(recent, "selection-changed",
                     G_CALLBACK(recent_selection_changed_dialog_cb), session);
    g_signal_connect(recent, "item-activated",
                     G_CALLBACK(recent_item_activated_dialog_cb), dialog);
#endif
    /* show and wait for response */
    gtk_widget_show_all(dialog);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        for (i = 0; i < SPICE_N_ELEMENTS(connect_entries); i++) {
            const gchar *txt;
            txt = gtk_entry_get_text(GTK_ENTRY(connect_entries[i].entry));
            g_object_set(session, connect_entries[i].prop, txt, NULL);
        }
        retval = 0;
    } else
        retval = -1;
    gtk_widget_destroy(dialog);
    return retval;
}
