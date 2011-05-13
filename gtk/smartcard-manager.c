/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2011 Red Hat, Inc.

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

#include <glib-object.h>
#include <vcard_emul.h>
#include <vevent.h>
#include <vreader.h>


#include "smartcard-manager.h"
#include "spice-marshal.h"

/**
 * SECTION:spice-smartcard-manager
 * @short_description: the base smartcard-manager class
 * @title: Spice SmartCardManager
 * @section_id:
 * @see_also:
 * @stability: Stable
 * @include: spice-smartcard-manager.h
 *
 * #SpiceSmartCardManager is the base class for the different kind of Spice
 * smartcard_manager connections, such as #SpiceMainSmartCardManager, or
 * #SpiceInputsSmartCardManager.
 */

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_SMARTCARD_MANAGER_GET_PRIVATE(obj)                                  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_SMARTCARD_MANAGER, spice_smartcard_manager))

struct spice_smartcard_manager {
    guint monitor_id;
};

G_DEFINE_TYPE(SpiceSmartCardManager, spice_smartcard_manager, G_TYPE_OBJECT);
G_DEFINE_BOXED_TYPE(VReader, spice_smartcard_reader, vreader_reference, vreader_free);

/* Properties */
enum {
    PROP_0,
};

/* Signals */
enum {
    SPICE_SMARTCARD_MANAGER_READER_ADDED,
    SPICE_SMARTCARD_MANAGER_READER_REMOVED,
    SPICE_SMARTCARD_MANAGER_CARD_INSERTED,
    SPICE_SMARTCARD_MANAGER_CARD_REMOVED,

    SPICE_SMARTCARD_MANAGER_LAST_SIGNAL,
};

static guint signals[SPICE_SMARTCARD_MANAGER_LAST_SIGNAL];

typedef gboolean (*SmartCardSourceFunc)(VEvent *event, gpointer user_data);
static guint smartcard_monitor_add(SmartCardSourceFunc callback,
                                   gpointer user_data);
static gboolean smartcard_monitor_dispatch(VEvent *event, gpointer user_data);

/* ------------------------------------------------------------------ */

static void spice_smartcard_manager_init(SpiceSmartCardManager *smartcard_manager)
{
    spice_smartcard_manager *priv;

    priv = SPICE_SMARTCARD_MANAGER_GET_PRIVATE(smartcard_manager);
    smartcard_manager->priv = priv;
    priv->monitor_id = smartcard_monitor_add(smartcard_monitor_dispatch,
                                             smartcard_manager);
}

static void spice_smartcard_manager_dispose(GObject *gobject)
{
    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->dispose)
        G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->dispose(gobject);
}

static void spice_smartcard_manager_finalize(GObject *gobject)
{
    spice_smartcard_manager *priv;

    priv = SPICE_SMARTCARD_MANAGER_GET_PRIVATE(gobject);
    if (priv->monitor_id != 0) {
        g_source_remove(priv->monitor_id);
        priv->monitor_id = 0;
    }

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->finalize)
        G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->finalize(gobject);
}

static void spice_smartcard_manager_class_init(SpiceSmartCardManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

    /**
     * SpiceSmartCardManager::reader-added:
     * @manager: the #SpiceSmartCardManager that emitted the signal
     * @vreader: #VReader boxed object corresponding to the added reader
     *
     * The #SpiceSmartCardManager::reader-added signal is emitted whenever
     * a new smartcard reader (software or hardware) has been plugged in.
     **/
    signals[SPICE_SMARTCARD_MANAGER_READER_ADDED] =
        g_signal_new("reader-added",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSmartCardManagerClass, reader_added),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_SMARTCARD_READER);

    /**
     * SpiceSmartCardManager::reader-removed:
     * @manager: the #SpiceSmartCardManager that emitted the signal
     * @vreader: #VReader boxed object corresponding to the removed reader
     *
     * The #SpiceSmartCardManager::reader-removed signal is emitted whenever
     * a smartcard reader (software or hardware) has been removed.
     **/
    signals[SPICE_SMARTCARD_MANAGER_READER_REMOVED] =
        g_signal_new("reader-removed",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSmartCardManagerClass, reader_removed),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_SMARTCARD_READER);

    /**
     * SpiceSmartCardManager::card-inserted:
     * @manager: the #SpiceSmartCardManager that emitted the signal
     * @vreader: #VReader boxed object corresponding to the reader a new
     * card was inserted in
     *
     * The #SpiceSmartCardManager::card-inserted signal is emitted whenever
     * a smartcard is inserted in a reader
     **/
    signals[SPICE_SMARTCARD_MANAGER_CARD_INSERTED] =
        g_signal_new("card-inserted",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSmartCardManagerClass, card_inserted),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_SMARTCARD_READER);

    /**
     * SpiceSmartCardManager::card-removed:
     * @manager: the #SpiceSmartCardManager that emitted the signal
     * @vreader: #VReader boxed object corresponding to the reader a card
     * was removed from
     *
     * The #SpiceSmartCardManager::card-removed signal is emitted whenever
     * a smartcard was removed from a reader.
     **/
    signals[SPICE_SMARTCARD_MANAGER_CARD_REMOVED] =
        g_signal_new("card-removed",
                     G_OBJECT_CLASS_TYPE(gobject_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(SpiceSmartCardManagerClass, card_removed),
                     NULL, NULL,
                     g_cclosure_user_marshal_VOID__BOXED,
                     G_TYPE_NONE,
                     1,
                     SPICE_TYPE_SMARTCARD_READER);
    gobject_class->dispose      = spice_smartcard_manager_dispose;
    gobject_class->finalize     = spice_smartcard_manager_finalize;

    g_type_class_add_private(klass, sizeof(spice_smartcard_manager));
}

/* ------------------------------------------------------------------ */
/* private api                                                        */

static SpiceSmartCardManager *spice_smartcard_manager_new(void)
{
    return g_object_new(SPICE_TYPE_SMARTCARD_MANAGER, NULL);
}

/* ------------------------------------------------------------------ */
/* public api                                                         */

SpiceSmartCardManager *spice_smartcard_manager_get(void)
{
    static GOnce manager_singleton_once = G_ONCE_INIT;

    return g_once(&manager_singleton_once,
                  (GThreadFunc)spice_smartcard_manager_new,
                  NULL);
}

static gboolean smartcard_monitor_dispatch(VEvent *event, gpointer user_data)
{
    g_return_val_if_fail(event != NULL, TRUE);

    switch (event->type) {
        case VEVENT_READER_INSERT:
            g_signal_emit(G_OBJECT(user_data),
                          signals[SPICE_SMARTCARD_MANAGER_READER_ADDED],
                          0, event->reader);
            break;

        case VEVENT_READER_REMOVE:
            g_signal_emit(G_OBJECT(user_data),
                          signals[SPICE_SMARTCARD_MANAGER_READER_REMOVED],
                          0, event->reader);
            break;

        case VEVENT_CARD_INSERT:
            g_signal_emit(G_OBJECT(user_data),
                          signals[SPICE_SMARTCARD_MANAGER_CARD_INSERTED],
                          0, event->reader);
            break;
        case VEVENT_CARD_REMOVE:
            g_signal_emit(G_OBJECT(user_data),
                          signals[SPICE_SMARTCARD_MANAGER_CARD_REMOVED],
                          0, event->reader);
            break;
        case VEVENT_LAST:
            break;
    }

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* smartcard monitoring GSource                                       */
struct _SmartCardSource {
    GSource parent_source;
    VEvent *pending_event;
};
typedef struct _SmartCardSource SmartCardSource;

typedef gboolean (*SmartCardSourceFunc)(VEvent *event, gpointer user_data);

static gboolean smartcard_source_prepare(GSource *source, gint *timeout)
{
    SmartCardSource *smartcard_source = (SmartCardSource *)source;

    if (smartcard_source->pending_event == NULL)
        smartcard_source->pending_event = vevent_get_next_vevent();

    if (timeout != NULL)
        *timeout = -1;

    return (smartcard_source->pending_event != NULL);
}

static gboolean smartcard_source_check(GSource *source)
{
    return smartcard_source_prepare(source, NULL);
}

static gboolean smartcard_source_dispatch(GSource *source,
                                          GSourceFunc callback,
                                          gpointer user_data)
{
    SmartCardSource *smartcard_source = (SmartCardSource *)source;
    SmartCardSourceFunc smartcard_callback = (SmartCardSourceFunc)callback;

    g_assert(smartcard_source->pending_event != NULL);

    if (callback) {
        gboolean event_consumed;
        event_consumed = smartcard_callback(smartcard_source->pending_event,
                                            user_data);
        if (event_consumed) {
            vevent_delete(smartcard_source->pending_event);
            smartcard_source->pending_event = NULL;
        }
    }

    return TRUE;
}

static void smartcard_source_finalize(GSource *source)
{
    SmartCardSource *smartcard_source = (SmartCardSource *)source;

    if (smartcard_source->pending_event) {
        vevent_delete(smartcard_source->pending_event);
        smartcard_source->pending_event = NULL;
    }
}

static GSource *smartcard_monitor_source_new(void)
{
    static GSourceFuncs source_funcs = {
        .prepare = smartcard_source_prepare,
        .check = smartcard_source_check,
        .dispatch = smartcard_source_dispatch,
        .finalize = smartcard_source_finalize
    };
    GSource *source;

    source = g_source_new(&source_funcs, sizeof(SmartCardSource));
    g_source_set_name(source, "Smartcard event source");
    return source;
}

static guint smartcard_monitor_add(SmartCardSourceFunc callback,
                                   gpointer user_data)
{
    GSource *source;
    guint id;

    source = smartcard_monitor_source_new();
    g_source_set_callback(source, (GSourceFunc)callback, user_data, NULL);
    id = g_source_attach(source, NULL);
    g_source_unref(source);

    return id;
}

gboolean spice_smartcard_manager_init_libcacard(SpiceSession *session)
{
    return (vcard_emul_init(NULL) == VCARD_EMUL_OK);
}
