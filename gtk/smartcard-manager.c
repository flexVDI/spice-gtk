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

#include "smartcard-manager.h"

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
};

G_DEFINE_TYPE(SpiceSmartCardManager, spice_smartcard_manager, G_TYPE_OBJECT);

/* Properties */
enum {
    PROP_0,
};

/* Signals */
enum {

    SPICE_SMARTCARD_MANAGER_LAST_SIGNAL,
};

G_GNUC_UNUSED static guint signals[SPICE_SMARTCARD_MANAGER_LAST_SIGNAL];

static void spice_smartcard_manager_init(SpiceSmartCardManager *smartcard_manager)
{
    smartcard_manager->priv = SPICE_SMARTCARD_MANAGER_GET_PRIVATE(smartcard_manager);
}

static void spice_smartcard_manager_dispose(GObject *gobject)
{
    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->dispose)
        G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->dispose(gobject);
}

static void spice_smartcard_manager_finalize(GObject *gobject)
{
    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->finalize)
        G_OBJECT_CLASS(spice_smartcard_manager_parent_class)->finalize(gobject);
}

static void spice_smartcard_manager_class_init(SpiceSmartCardManagerClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

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
