/* -*- Mode: C; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
   Copyright (C) 2012 Red Hat, Inc.

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
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include "spice-client.h"
#include "spice-uri.h"
#include "spice-uri-priv.h"

/**
 * SECTION:spice-uri
 * @short_description: URIs handling
 * @title: SpiceURI
 * @section_id:
 * @stability: Stable
 * @include: spice-client.h
 *
 * A SpiceURI represents a (parsed) URI.
 * Since: 0.24
 */

struct _SpiceURI {
    GObject parent_instance;
    gchar *scheme;
    gchar *hostname;
    guint port;
    gchar *user;
    gchar *password;
};

struct _SpiceURIClass {
    GObjectClass parent_class;
};

G_DEFINE_TYPE(SpiceURI, spice_uri, G_TYPE_OBJECT);

enum  {
    SPICE_URI_DUMMY_PROPERTY,
    SPICE_URI_SCHEME,
    SPICE_URI_USER,
    SPICE_URI_PASSWORD,
    SPICE_URI_HOSTNAME,
    SPICE_URI_PORT
};

#ifndef HAVE_STRTOK_R
static char *strtok_r(char *s, const char *delim, char **save_ptr)
{
    char *token;

    if (s == NULL)
        s = *save_ptr;

    /* Scan leading delimiters. */
    s += strspn (s, delim);
    if (*s == '\0')
        return NULL;

    /* Find the end of the token. */
    token = s;
    s = strpbrk (token, delim);
    if (s == NULL)
        /* This token finishes the string. */
        *save_ptr = strchr (token, '\0');
    else
    {
        /* Terminate the token and make *SAVE_PTR point past it. */
        *s = '\0';
        *save_ptr = s + 1;
    }
    return token;
}
#endif

G_GNUC_INTERNAL
SpiceURI* spice_uri_new(void)
{
    SpiceURI * self = NULL;
    self = (SpiceURI*)g_object_new(SPICE_TYPE_URI, NULL);
    return self;
}

static void spice_uri_reset(SpiceURI *self)
{
    g_clear_pointer(&self->scheme, g_free);
    g_clear_pointer(&self->hostname, g_free);
    g_clear_pointer(&self->user, g_free);
    g_clear_pointer(&self->password, g_free);
    self->port = 0;
}

G_GNUC_INTERNAL
gboolean spice_uri_parse(SpiceURI *self, const gchar *_uri, GError **error)
{
    gchar *dup, *uri, **uriv = NULL;
    const gchar *uri_port = NULL;
    char *uri_scheme = NULL;
    gboolean success = FALSE;
    size_t len;

    g_return_val_if_fail(self != NULL, FALSE);

    spice_uri_reset(self);

    g_return_val_if_fail(_uri != NULL, FALSE);

    uri = dup = g_strdup(_uri);
    /* FIXME: use GUri when it is ready... only support http atm */
    /* the code is voluntarily not parsing thoroughly the uri */
    uri_scheme = g_uri_parse_scheme(uri);
    if (uri_scheme == NULL) {
        spice_uri_set_scheme(self, "http");
    } else {
        spice_uri_set_scheme(self, uri_scheme);
        uri += strlen(uri_scheme) + 3; /* scheme + "://" */
    }
    if (g_ascii_strcasecmp(spice_uri_get_scheme(self), "http") == 0) {
        spice_uri_set_port(self, 3128);
    } else if (g_ascii_strcasecmp(spice_uri_get_scheme(self), "https") == 0) {
        spice_uri_set_port(self, 3129);
    } else {
        g_set_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                    "Invalid uri scheme for proxy: %s", spice_uri_get_scheme(self));
        goto end;
    }
    /* remove trailing slash */
    len = strlen(uri);
    for (; len > 0; len--)
        if (uri[len-1] == '/')
            uri[len-1] = '\0';
        else
            break;


    /* yes, that parser is bad, we need GUri... */
    if (strstr(uri, "@")) {
        gchar *saveptr = NULL, *saveptr2 = NULL;
        gchar *next = strstr(uri, "@") + 1;
        gchar *auth = strtok_r(uri, "@", &saveptr);
        const gchar *user = strtok_r(auth, ":", &saveptr2);
        const gchar *pass = strtok_r(NULL, ":", &saveptr2);
        spice_uri_set_user(self, user);
        spice_uri_set_password(self, pass);
        uri = next;
    }

    if (*uri == '[') { /* ipv6 address */
        uriv = g_strsplit(uri + 1, "]", 2);
        if (uriv[1] == NULL) {
            g_set_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        "Missing ']' in ipv6 uri");
            goto end;
        }
        if (*uriv[1] == ':') {
            uri_port = uriv[1] + 1;
        } else if (strlen(uriv[1]) > 0) { /* invalid string after the hostname */
            g_set_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        "Invalid uri address");
            goto end;
        }
    } else {
        /* max 2 parts, host:port */
        uriv = g_strsplit(uri, ":", 2);
        if (uriv[0] != NULL)
            uri_port = uriv[1];
    }

    if (uriv[0] == NULL || strlen(uriv[0]) == 0) {
        g_set_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                    "Invalid hostname in uri address");
        goto end;
    }

    spice_uri_set_hostname(self, uriv[0]);

    if (uri_port != NULL) {
        gchar *endptr;
        gint64 port = g_ascii_strtoll(uri_port, &endptr, 10);
        if (*endptr != '\0') {
            g_set_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED,
                        "Invalid uri port: %s", uri_port);
            goto end;
        } else if (endptr == uri_port) {
            g_set_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED, "Missing uri port");
            goto end;
        }
        if (port <= 0 || port > 65535) {
            g_set_error(error, SPICE_CLIENT_ERROR, SPICE_CLIENT_ERROR_FAILED, "Port out of range");
            goto end;
        }
        spice_uri_set_port(self, port);
    }

    success = TRUE;

end:
    g_free(uri_scheme);
    g_free(dup);
    g_strfreev(uriv);
    return success;
}

/**
 * spice_uri_get_scheme:
 * @uri: a #SpiceURI
 *
 * Gets @uri's scheme.
 *
 * Returns: @uri's scheme.
 * Since: 0.24
 **/
const gchar* spice_uri_get_scheme(SpiceURI *self)
{
    g_return_val_if_fail(SPICE_IS_URI(self), NULL);
    return self->scheme;
}

/**
 * spice_uri_set_scheme:
 * @uri: a #SpiceURI
 * @scheme: the scheme
 *
 * Sets @uri's scheme to @scheme.
 * Since: 0.24
 **/
void spice_uri_set_scheme(SpiceURI *self, const gchar *scheme)
{
    g_return_if_fail(SPICE_IS_URI(self));

    g_free(self->scheme);
    self->scheme = g_strdup(scheme);
    g_object_notify((GObject *)self, "scheme");
}

/**
 * spice_uri_get_hostname:
 * @uri: a #SpiceURI
 *
 * Gets @uri's hostname.
 *
 * Returns: @uri's hostname.
 * Since: 0.24
 **/
const gchar* spice_uri_get_hostname(SpiceURI *self)
{
    g_return_val_if_fail(SPICE_IS_URI(self), NULL);
    return self->hostname;
}


/**
 * spice_uri_set_hostname:
 * @uri: a #SpiceURI
 * @hostname: the hostname
 *
 * Sets @uri's hostname to @hostname.
 * Since: 0.24
 **/
void spice_uri_set_hostname(SpiceURI *self, const gchar *hostname)
{
    g_return_if_fail(SPICE_IS_URI(self));

    g_free(self->hostname);
    self->hostname = g_strdup(hostname);
    g_object_notify((GObject *)self, "hostname");
}

/**
 * spice_uri_get_port:
 * @uri: a #SpiceURI
 *
 * Gets @uri's port.
 *
 * Returns: @uri's port.
 * Since: 0.24
 **/
guint spice_uri_get_port(SpiceURI *self)
{
    g_return_val_if_fail(SPICE_IS_URI(self), 0);
    return self->port;
}

/**
 * spice_uri_set_port:
 * @uri: a #SpiceURI
 * @port: the port
 *
 * Sets @uri's port to @port.
 * Since: 0.24
 **/
void spice_uri_set_port(SpiceURI *self, guint port)
{
    g_return_if_fail(SPICE_IS_URI(self));
    self->port = port;
    g_object_notify((GObject *)self, "port");
}

static void spice_uri_get_property(GObject *object, guint property_id,
                                     GValue *value, GParamSpec *pspec)
{
    SpiceURI *self;
    self = G_TYPE_CHECK_INSTANCE_CAST(object, SPICE_TYPE_URI, SpiceURI);

    switch (property_id) {
    case SPICE_URI_SCHEME:
        g_value_set_string(value, spice_uri_get_scheme(self));
        break;
    case SPICE_URI_HOSTNAME:
        g_value_set_string(value, spice_uri_get_hostname(self));
        break;
    case SPICE_URI_PORT:
        g_value_set_uint(value, spice_uri_get_port(self));
        break;
    case SPICE_URI_USER:
        g_value_set_string(value, spice_uri_get_user(self));
        break;
    case SPICE_URI_PASSWORD:
        g_value_set_string(value, spice_uri_get_password(self));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


static void spice_uri_set_property(GObject *object, guint property_id,
                                     const GValue *value, GParamSpec *pspec)
{
    SpiceURI * self;
    self = G_TYPE_CHECK_INSTANCE_CAST(object, SPICE_TYPE_URI, SpiceURI);

    switch (property_id) {
    case SPICE_URI_SCHEME:
        spice_uri_set_scheme(self, g_value_get_string(value));
        break;
    case SPICE_URI_HOSTNAME:
        spice_uri_set_hostname(self, g_value_get_string(value));
        break;
    case SPICE_URI_USER:
        spice_uri_set_user(self, g_value_get_string(value));
        break;
    case SPICE_URI_PASSWORD:
        spice_uri_set_password(self, g_value_get_string(value));
        break;
    case SPICE_URI_PORT:
        spice_uri_set_port(self, g_value_get_uint(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void spice_uri_finalize(GObject* obj)
{
    SpiceURI *self;

    self = G_TYPE_CHECK_INSTANCE_CAST(obj, SPICE_TYPE_URI, SpiceURI);
    spice_uri_reset(self);

    G_OBJECT_CLASS (spice_uri_parent_class)->finalize (obj);
}

static void spice_uri_init (SpiceURI *self G_GNUC_UNUSED)
{
}


static void spice_uri_class_init(SpiceURIClass *klass)
{
    spice_uri_parent_class = g_type_class_peek_parent (klass);

    G_OBJECT_CLASS (klass)->get_property = spice_uri_get_property;
    G_OBJECT_CLASS (klass)->set_property = spice_uri_set_property;
    G_OBJECT_CLASS (klass)->finalize = spice_uri_finalize;

    g_object_class_install_property(G_OBJECT_CLASS (klass),
                                    SPICE_URI_SCHEME,
                                    g_param_spec_string ("scheme",
                                                         "scheme",
                                                         "scheme",
                                                         NULL,
                                                         G_PARAM_STATIC_STRINGS |
                                                         G_PARAM_READWRITE));

    g_object_class_install_property(G_OBJECT_CLASS (klass),
                                    SPICE_URI_HOSTNAME,
                                    g_param_spec_string ("hostname",
                                                         "hostname",
                                                         "hostname",
                                                         NULL,
                                                         G_PARAM_STATIC_STRINGS |
                                                         G_PARAM_READWRITE));

    g_object_class_install_property(G_OBJECT_CLASS (klass),
                                    SPICE_URI_PORT,
                                    g_param_spec_uint ("port",
                                                       "port",
                                                       "port",
                                                       0, G_MAXUINT, 0,
                                                       G_PARAM_STATIC_STRINGS |
                                                       G_PARAM_READWRITE));

    g_object_class_install_property(G_OBJECT_CLASS (klass),
                                    SPICE_URI_USER,
                                    g_param_spec_string ("user",
                                                         "user",
                                                         "user",
                                                         NULL,
                                                         G_PARAM_STATIC_STRINGS |
                                                         G_PARAM_READWRITE));

    g_object_class_install_property(G_OBJECT_CLASS (klass),
                                    SPICE_URI_PASSWORD,
                                    g_param_spec_string ("password",
                                                         "password",
                                                         "password",
                                                         NULL,
                                                         G_PARAM_STATIC_STRINGS |
                                                         G_PARAM_READWRITE));
}

/**
 * spice_uri_to_string:
 * @uri: a #SpiceURI
 *
 * Returns a string representing @uri.
 *
 * Returns: a string representing @uri, which the caller must free.
 * Since: 0.24
 **/
gchar* spice_uri_to_string(SpiceURI* self)
{
    g_return_val_if_fail(SPICE_IS_URI(self), NULL);

    if (self->scheme == NULL || self->hostname == NULL)
        return NULL;

    if (self->user || self->password)
        return g_strdup_printf("%s://%s:%s@%s:%u",
                               self->scheme,
                               self->user, self->password,
                               self->hostname, self->port);
    else
        return g_strdup_printf("%s://%s:%u",
                               self->scheme, self->hostname, self->port);
}

/**
 * spice_uri_get_user:
 * @uri: a #SpiceURI
 *
 * Gets @uri's user.
 *
 * Returns: @uri's user.
 * Since: 0.24
 **/
const gchar* spice_uri_get_user(SpiceURI *self)
{
    g_return_val_if_fail(SPICE_IS_URI(self), NULL);
    return self->user;
}

/**
 * spice_uri_set_user:
 * @uri: a #SpiceURI
 * @user: the user, or %NULL.
 *
 * Sets @uri's user to @user.
 * Since: 0.24
 **/
void spice_uri_set_user(SpiceURI *self, const gchar *user)
{
    g_return_if_fail(SPICE_IS_URI(self));

    g_free(self->user);
    self->user = g_strdup(user);
    g_object_notify((GObject *)self, "user");
}

/**
 * spice_uri_get_password:
 * @uri: a #SpiceURI
 *
 * Gets @uri's password.
 *
 * Returns: @uri's password.
 * Since: 0.24
 **/
const gchar* spice_uri_get_password(SpiceURI *self)
{
    g_return_val_if_fail(SPICE_IS_URI(self), NULL);
    return self->password;
}

/**
 * spice_uri_set_password:
 * @uri: a #SpiceURI
 * @password: the password, or %NULL.
 *
 * Sets @uri's password to @password.
 * Since: 0.24
 **/
void spice_uri_set_password(SpiceURI *self, const gchar *password)
{
    g_return_if_fail(SPICE_IS_URI(self));

    g_free(self->password);
    self->password = g_strdup(password);
    g_object_notify((GObject *)self, "password");
}
