/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2010-2011 Red Hat, Inc.

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

#include "spice-gtk-session.h"

struct _SpiceGtkSessionPrivate {
    SpiceSession *session;
};

/**
 * SECTION:spice-gtk-session
 * @short_description: handles GTK connection details
 * @title: Spice GTK Session
 * @section_id:
 * @see_also: #SpiceSession, and the GTK widget #SpiceDisplay
 * @stability: Stable
 * @include: spice-gtk-session.h
 *
 * The #SpiceGtkSession class is the spice-client-gtk counter part of
 * #SpiceSession. It contains functionality which should be handled per
 * session rather then per #SpiceDisplay (one session can have multiple
 * displays), but which cannot live in #SpiceSession as it depends on
 * GTK. For example the clipboard functionality.
 *
 * There should always be a 1:1 relation between #SpiceGtkSession objects
 * and #SpiceSession objects. Therefor there is no spice_gtk_session_new,
 * instead there is spice_gtk_session_get() which ensures this 1:1 relation.
 *
 * #SpiceDisplay uses #SpiceGtkSession internally, some #SpiceDisplay
 * properties map directly to #SpiceGtkSession properties, this means that
 * changing them for one #SpiceDisplay changes them for all displays.
 *
 * Depending on your UI, you may want to not show these properties on a
 * per display basis and instead show them in a global settings menu which
 * directly uses SpiceGtkSession.
 */

/* ------------------------------------------------------------------ */
/* gobject glue                                                       */

#define SPICE_GTK_SESSION_GET_PRIVATE(obj) \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), SPICE_TYPE_GTK_SESSION, SpiceGtkSessionPrivate))

G_DEFINE_TYPE (SpiceGtkSession, spice_gtk_session, G_TYPE_OBJECT);

/* Properties */
enum {
    PROP_0,
    PROP_SESSION,
};

static void spice_gtk_session_init(SpiceGtkSession *self)
{
    self->priv = SPICE_GTK_SESSION_GET_PRIVATE(self);
}

static GObject *
spice_gtk_session_constructor(GType                  gtype,
                              guint                  n_properties,
                              GObjectConstructParam *properties)
{
    GObject *obj;
    SpiceGtkSession *self;

    {
        /* Always chain up to the parent constructor */
        GObjectClass *parent_class;
        parent_class = G_OBJECT_CLASS(spice_gtk_session_parent_class);
        obj = parent_class->constructor(gtype, n_properties, properties);
    }

    self = SPICE_GTK_SESSION(obj);
    if (!self->priv->session)
        g_error("SpiceGtKSession constructed without a session");

    return obj;
}

static void spice_gtk_session_dispose(GObject *gobject)
{
#if 0
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = SPICE_GTK_SESSION_GET_PRIVATE(self);
#endif

    /* release stuff */

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_gtk_session_parent_class)->dispose)
        G_OBJECT_CLASS(spice_gtk_session_parent_class)->dispose(gobject);
}

static void spice_gtk_session_finalize(GObject *gobject)
{
#if 0
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = SPICE_GTK_SESSION_GET_PRIVATE(self);
#endif

    /* release stuff */

    /* Chain up to the parent class */
    if (G_OBJECT_CLASS(spice_gtk_session_parent_class)->finalize)
        G_OBJECT_CLASS(spice_gtk_session_parent_class)->finalize(gobject);
}

static void spice_gtk_session_get_property(GObject    *gobject,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = SPICE_GTK_SESSION_GET_PRIVATE(self);

    switch (prop_id) {
    case PROP_SESSION:
        g_value_set_object(value, s->session);
	break;
    default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
	break;
    }
}

static void spice_gtk_session_set_property(GObject      *gobject,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
    SpiceGtkSession *self = SPICE_GTK_SESSION(gobject);
    SpiceGtkSessionPrivate *s = SPICE_GTK_SESSION_GET_PRIVATE(self);

    switch (prop_id) {
    case PROP_SESSION:
        s->session = g_value_get_object(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(gobject, prop_id, pspec);
        break;
    }
}

static void spice_gtk_session_class_init(SpiceGtkSessionClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->constructor  = spice_gtk_session_constructor;
    gobject_class->dispose      = spice_gtk_session_dispose;
    gobject_class->finalize     = spice_gtk_session_finalize;
    gobject_class->get_property = spice_gtk_session_get_property;
    gobject_class->set_property = spice_gtk_session_set_property;

    /**
     * SpiceGtkSession:session:
     *
     * #SpiceSession this #SpiceGtkSession is associated with
     *
     **/
    g_object_class_install_property
        (gobject_class, PROP_SESSION,
         g_param_spec_object("session",
                             "Session",
                             "SpiceSession",
                             SPICE_TYPE_SESSION,
                             G_PARAM_READWRITE |
                             G_PARAM_CONSTRUCT_ONLY |
                             G_PARAM_STATIC_STRINGS));

    g_type_class_add_private(klass, sizeof(SpiceGtkSessionPrivate));
}

static void
spice_gtk_session_spice_session_destroyed_cb(gpointer user_data,
                                             GObject *object)
{
    SpiceGtkSession *self = user_data;

    g_object_unref(self);
}

/* ------------------------------------------------------------------ */
/* public functions                                                   */

/**
 * spice_gtk_session_get:
 * @session: #SpiceSession for which to get the #SpiceGtkSession
 *
 * Gets the #SpiceGtkSession associated with the passed in #SpiceSession.
 * A new #SpiceGtkSession instance will be created the first time this
 * function is called for a certain #SpiceSession.
 *
 * Note that this function returns a weak reference, which should not be used
 * after the #SpiceSession itself has been unref-ed by the caller.
 *
 * Returns: (transfer none): a weak reference to the #SpiceGtkSession associated with the passed in #SpiceSession
 **/
SpiceGtkSession *spice_gtk_session_get(SpiceSession *session)
{
    GObject *self;
    static GStaticMutex mutex = G_STATIC_MUTEX_INIT;

    g_static_mutex_lock(&mutex);
    self = g_object_get_data(G_OBJECT(session), "spice-gtk-session");
    if (self == NULL) {
        self = g_object_new(SPICE_TYPE_GTK_SESSION, "session", session, NULL);
        g_object_set_data(G_OBJECT(session), "spice-gtk-session", self);
        /* Ensure we are destroyed together with the SpiceSession */
        g_object_weak_ref(G_OBJECT(session),
                          spice_gtk_session_spice_session_destroyed_cb,
                          self);
    }
    g_static_mutex_unlock(&mutex);

    return SPICE_GTK_SESSION(self);
}
