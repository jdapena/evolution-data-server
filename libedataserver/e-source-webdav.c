/*
 * e-source-webdav.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

/**
 * SECTION: e-source-webdav
 * @include: libedataserver/e-source-webdav.h
 * @short_description: #ESource extension for WebDAV settings
 *
 * The #ESourceWebdav extension tracks settings for accessing resources
 * on a remote WebDAV server.
 *
 * This class exists in libedataserver because we have several
 * WebDAV-based backends.  Each of these backends is free to use
 * this class directly or subclass it with additional settings.
 * Subclasses should override the extension name.
 *
 * The #SoupURI is parsed into components and distributed across
 * several other built-in extensions such as #ESourceAuthentication
 * and #ESourceSecurity.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/e-source-webdav.h>
 *
 *   ESourceWebdav *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
 * ]|
 **/

#include "e-source-webdav.h"

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-source-authentication.h>
#include <libedataserver/e-source-security.h>

#define E_SOURCE_WEBDAV_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_WEBDAV, ESourceWebdavPrivate))

struct _ESourceWebdavPrivate {
	GMutex *property_lock;
	gchar *display_name;
	gchar *email_address;
	gchar *resource_path;
	gboolean avoid_ifmatch;
	gboolean calendar_auto_schedule;
	gboolean ignore_invalid_cert;
	SoupURI *uri;
};

enum {
	PROP_0,
	PROP_AVOID_IFMATCH,
	PROP_CALENDAR_AUTO_SCHEDULE,
	PROP_DISPLAY_NAME,
	PROP_EMAIL_ADDRESS,
	PROP_IGNORE_INVALID_CERT,
	PROP_RESOURCE_PATH,
	PROP_SOUP_URI
};

G_DEFINE_TYPE (
	ESourceWebdav,
	e_source_webdav,
	E_TYPE_SOURCE_EXTENSION)

static gboolean
source_webdav_host_to_soup_uri (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	GObject *target;
	SoupURI *soup_uri;
	const gchar *host;

	host = g_value_get_string (source_value);

	target = g_binding_get_target (binding);
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (target), FALSE);

	soup_uri = e_source_webdav_dup_soup_uri (E_SOURCE_WEBDAV (target));
	soup_uri_set_host (soup_uri, host);
	g_value_take_boxed (target_value, soup_uri);

	return TRUE;
}

static gboolean
source_webdav_soup_uri_to_host (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	SoupURI *soup_uri;

	soup_uri = g_value_get_boxed (source_value);
	g_value_set_string (target_value, soup_uri->host);

	return TRUE;
}

static gboolean
source_webdav_path_to_soup_uri (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	GObject *target;
	SoupURI *soup_uri;
	const gchar *path;

	path = g_value_get_string (source_value);

	/* soup_uri_set_path() warns on NULL. */
	if (path == NULL)
		path = "";

	target = g_binding_get_target (binding);
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (target), FALSE);

	soup_uri = e_source_webdav_dup_soup_uri (E_SOURCE_WEBDAV (target));
	soup_uri_set_path (soup_uri, path);
	g_value_take_boxed (target_value, soup_uri);

	return TRUE;
}

static gboolean
source_webdav_soup_uri_to_path (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	SoupURI *soup_uri;

	soup_uri = g_value_get_boxed (source_value);
	g_value_set_string (target_value, soup_uri->path);

	return TRUE;
}

static gboolean
source_webdav_port_to_soup_uri (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	GObject *target;
	SoupURI *soup_uri;
	guint port;

	port = g_value_get_uint (source_value);

	target = g_binding_get_target (binding);
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (target), FALSE);

	soup_uri = e_source_webdav_dup_soup_uri (E_SOURCE_WEBDAV (target));
	soup_uri_set_port (soup_uri, port);
	g_value_take_boxed (target_value, soup_uri);

	return TRUE;
}

static gboolean
source_webdav_soup_uri_to_port (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	SoupURI *soup_uri;

	soup_uri = g_value_get_boxed (source_value);
	g_value_set_uint (target_value, soup_uri->port);

	return TRUE;
}

static gboolean
source_webdav_secure_to_soup_uri (GBinding *binding,
                                  const GValue *source_value,
                                  GValue *target_value,
                                  gpointer user_data)
{
	GObject *target;
	SoupURI *soup_uri;
	gboolean secure;

	secure = g_value_get_boolean (source_value);

	target = g_binding_get_target (binding);
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (target), FALSE);

	soup_uri = e_source_webdav_dup_soup_uri (E_SOURCE_WEBDAV (target));
	if (secure)
		soup_uri_set_scheme (soup_uri, SOUP_URI_SCHEME_HTTPS);
	else
		soup_uri_set_scheme (soup_uri, SOUP_URI_SCHEME_HTTP);
	g_value_take_boxed (target_value, soup_uri);

	return TRUE;
}

static gboolean
source_webdav_soup_uri_to_secure (GBinding *binding,
                                  const GValue *source_value,
                                  GValue *target_value,
                                  gpointer user_data)
{
	SoupURI *soup_uri;
	gboolean secure;

	soup_uri = g_value_get_boxed (source_value);
	secure = (soup_uri->scheme == SOUP_URI_SCHEME_HTTPS);
	g_value_set_boolean (target_value, secure);

	return TRUE;
}

static gboolean
source_webdav_user_to_soup_uri (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	GObject *target;
	SoupURI *soup_uri;
	const gchar *user;

	user = g_value_get_string (source_value);

	target = g_binding_get_target (binding);
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (target), FALSE);

	soup_uri = e_source_webdav_dup_soup_uri (E_SOURCE_WEBDAV (target));
	soup_uri_set_user (soup_uri, user);
	g_value_take_boxed (target_value, soup_uri);

	return TRUE;
}

static gboolean
source_webdav_soup_uri_to_user (GBinding *binding,
                                const GValue *source_value,
                                GValue *target_value,
                                gpointer user_data)
{
	SoupURI *soup_uri;

	soup_uri = g_value_get_boxed (source_value);
	g_value_set_string (target_value, soup_uri->user);

	return TRUE;
}

static gboolean
source_webdav_user_to_method (GBinding *binding,
                              const GValue *source_value,
                              GValue *target_value,
                              gpointer user_data)
{
	const gchar *user;

	user = g_value_get_string (source_value);
	if (user == NULL || *user == '\0')
		g_value_set_string (target_value, "none");
	else
		g_value_set_string (target_value, "plain/password");

	return TRUE;
}

static void
source_webdav_set_property (GObject *object,
                            guint property_id,
                            const GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AVOID_IFMATCH:
			e_source_webdav_set_avoid_ifmatch (
				E_SOURCE_WEBDAV (object),
				g_value_get_boolean (value));
			return;

		case PROP_CALENDAR_AUTO_SCHEDULE:
			e_source_webdav_set_calendar_auto_schedule (
				E_SOURCE_WEBDAV (object),
				g_value_get_boolean (value));
			return;

		case PROP_DISPLAY_NAME:
			e_source_webdav_set_display_name (
				E_SOURCE_WEBDAV (object),
				g_value_get_string (value));
			return;

		case PROP_EMAIL_ADDRESS:
			e_source_webdav_set_email_address (
				E_SOURCE_WEBDAV (object),
				g_value_get_string (value));
			return;

		case PROP_IGNORE_INVALID_CERT:
			e_source_webdav_set_ignore_invalid_cert (
				E_SOURCE_WEBDAV (object),
				g_value_get_boolean (value));
			return;

		case PROP_RESOURCE_PATH:
			e_source_webdav_set_resource_path (
				E_SOURCE_WEBDAV (object),
				g_value_get_string (value));
			return;

		case PROP_SOUP_URI:
			e_source_webdav_set_soup_uri (
				E_SOURCE_WEBDAV (object),
				g_value_get_boxed (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_webdav_get_property (GObject *object,
                            guint property_id,
                            GValue *value,
                            GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_AVOID_IFMATCH:
			g_value_set_boolean (
				value,
				e_source_webdav_get_avoid_ifmatch (
				E_SOURCE_WEBDAV (object)));
			return;

		case PROP_CALENDAR_AUTO_SCHEDULE:
			g_value_set_boolean (
				value,
				e_source_webdav_get_calendar_auto_schedule (
				E_SOURCE_WEBDAV (object)));
			return;

		case PROP_DISPLAY_NAME:
			g_value_take_string (
				value,
				e_source_webdav_dup_display_name (
				E_SOURCE_WEBDAV (object)));
			return;

		case PROP_EMAIL_ADDRESS:
			g_value_take_string (
				value,
				e_source_webdav_dup_email_address (
				E_SOURCE_WEBDAV (object)));
			return;

		case PROP_IGNORE_INVALID_CERT:
			g_value_set_boolean (
				value,
				e_source_webdav_get_ignore_invalid_cert (
				E_SOURCE_WEBDAV (object)));
			return;

		case PROP_RESOURCE_PATH:
			g_value_take_string (
				value,
				e_source_webdav_dup_resource_path (
				E_SOURCE_WEBDAV (object)));
			return;

		case PROP_SOUP_URI:
			g_value_take_boxed (
				value,
				e_source_webdav_dup_soup_uri (
				E_SOURCE_WEBDAV (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_webdav_finalize (GObject *object)
{
	ESourceWebdavPrivate *priv;

	priv = E_SOURCE_WEBDAV_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->display_name);
	g_free (priv->email_address);
	g_free (priv->resource_path);

	soup_uri_free (priv->uri);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_webdav_parent_class)->finalize (object);
}

static void
source_webdav_constructed (GObject *object)
{
	ESource *source;
	ESourceExtension *this_extension;
	ESourceExtension *other_extension;
	const gchar *extension_name;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_source_webdav_parent_class)->constructed (object);

	this_extension = E_SOURCE_EXTENSION (object);
	source = e_source_extension_get_source (this_extension);

	g_object_bind_property_full (
		this_extension, "resource-path",
		this_extension, "soup-uri",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		source_webdav_path_to_soup_uri,
		source_webdav_soup_uri_to_path,
		NULL, (GDestroyNotify) NULL);

	/* Bind to properties of other extensions for convenience. */

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	other_extension = e_source_get_extension (source, extension_name);

	g_object_bind_property_full (
		other_extension, "host",
		this_extension, "soup-uri",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		source_webdav_host_to_soup_uri,
		source_webdav_soup_uri_to_host,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property_full (
		other_extension, "port",
		this_extension, "soup-uri",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		source_webdav_port_to_soup_uri,
		source_webdav_soup_uri_to_port,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property_full (
		other_extension, "user",
		this_extension, "soup-uri",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		source_webdav_user_to_soup_uri,
		source_webdav_soup_uri_to_user,
		NULL, (GDestroyNotify) NULL);

	g_object_bind_property_full (
		other_extension, "user",
		other_extension, "method",
		G_BINDING_SYNC_CREATE,
		source_webdav_user_to_method,
		NULL,
		NULL, (GDestroyNotify) NULL);

	extension_name = E_SOURCE_EXTENSION_SECURITY;
	other_extension = e_source_get_extension (source, extension_name);

	g_object_bind_property_full (
		other_extension, "secure",
		this_extension, "soup-uri",
		G_BINDING_BIDIRECTIONAL |
		G_BINDING_SYNC_CREATE,
		source_webdav_secure_to_soup_uri,
		source_webdav_soup_uri_to_secure,
		NULL, (GDestroyNotify) NULL);
}

static void
e_source_webdav_class_init (ESourceWebdavClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceWebdavPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_webdav_set_property;
	object_class->get_property = source_webdav_get_property;
	object_class->finalize = source_webdav_finalize;
	object_class->constructed = source_webdav_constructed;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;

	g_object_class_install_property (
		object_class,
		PROP_AVOID_IFMATCH,
		g_param_spec_boolean (
			"avoid-ifmatch",
			"Avoid If-Match",
			"Work around a bug in old Apache servers",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_CALENDAR_AUTO_SCHEDULE,
		g_param_spec_boolean (
			"calendar-auto-schedule",
			"Calendar Auto-Schedule",
			"Whether the server handles meeting "
			"invitations (CalDAV-only)",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_DISPLAY_NAME,
		g_param_spec_string (
			"display-name",
			"Display Name",
			"Display name of the WebDAV resource",
			"",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_EMAIL_ADDRESS,
		g_param_spec_string (
			"email-address",
			"Email Address",
			"The user's email address",
			"",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_IGNORE_INVALID_CERT,
		g_param_spec_boolean (
			"ignore-invalid-cert",
			"Ignore Invalid Cert",
			"Ignore invalid SSL certificates",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_RESOURCE_PATH,
		g_param_spec_string (
			"resource-path",
			"Resource Path",
			"Absolute path to a WebDAV resource",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_SOUP_URI,
		g_param_spec_boxed (
			"soup-uri",
			"SoupURI",
			"WebDAV service as a SoupURI",
			SOUP_TYPE_URI,
			G_PARAM_READWRITE));
}

static void
e_source_webdav_init (ESourceWebdav *extension)
{
	extension->priv = E_SOURCE_WEBDAV_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();

	/* Initialize this enough for SOUP_URI_IS_VALID() to pass. */
	extension->priv->uri = soup_uri_new (NULL);
	extension->priv->uri->scheme = SOUP_URI_SCHEME_HTTP;
	extension->priv->uri->path = g_strdup ("");
}

/**
 * e_source_webdav_get_avoid_ifmatch:
 * @extension: an #ESourceWebdav
 *
 * This setting works around a
 * <ulink url="https://issues.apache.org/bugzilla/show_bug.cgi?id=38034">
 * bug</ulink> in older Apache mod_dav versions.
 *
 * <note>
 *   <para>
 *     We may deprecate this once Apache 2.2.8 or newer becomes
 *     sufficiently ubiquitous, or we figure out a way to detect
 *     and work around the bug automatically.
 *   </para>
 * </note>
 *
 * Returns: whether the WebDAV server is known to exhibit the bug
 *
 * Since: 3.6
 **/
gboolean
e_source_webdav_get_avoid_ifmatch (ESourceWebdav *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), FALSE);

	return extension->priv->avoid_ifmatch;
}

/**
 * e_source_webdav_set_avoid_ifmatch:
 * @extension: an #ESourceWebdav
 * @avoid_ifmatch: whether the WebDAV server is known to exhibit the bug
 *
 * This setting works around a
 * <ulink url="https://issues.apache.org/bugzilla/show_bug.cgi?id=38034">
 * bug</ulink> in older Apache mod_dav versions.
 *
 * <note>
 *   <para>
 *     We may deprecate this once Apache 2.2.8 or newer becomes
 *     sufficiently ubiquitous, or we figure out a way to detect
 *     and work around the bug automatically.
 *   </para>
 * </note>
 *
 * Since: 3.6
 **/
void
e_source_webdav_set_avoid_ifmatch (ESourceWebdav *extension,
                                   gboolean avoid_ifmatch)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV (extension));

	extension->priv->avoid_ifmatch = avoid_ifmatch;

	g_object_notify (G_OBJECT (extension), "avoid-ifmatch");
}

/**
 * e_source_webdav_get_calendar_auto_schedule:
 * @extension: an #ESourceWebdav
 *
 * FIXME Document me!
 *
 * Since: 3.6
 **/
gboolean
e_source_webdav_get_calendar_auto_schedule (ESourceWebdav *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), FALSE);

	return extension->priv->calendar_auto_schedule;
}

/**
 * e_source_webdav_set_calendar_auto_schedule:
 * @extension: an #ESourceWebdav
 * @calendar_auto_schedule: whether the server supports the
 * "calendar-auto-schedule" feature of CalDAV
 *
 * FIXME Document me!
 *
 * Since: 3.6
 **/
void
e_source_webdav_set_calendar_auto_schedule (ESourceWebdav *extension,
                                            gboolean calendar_auto_schedule)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV (extension));

	extension->priv->calendar_auto_schedule = calendar_auto_schedule;

	g_object_notify (G_OBJECT (extension), "calendar-auto-schedule");
}

/**
 * e_source_webdav_get_display_name:
 * @extension: an #ESourceWebdav
 *
 * Returns the last known display name of a WebDAV resource, which may
 * differ from the #ESource:display-name property of the #ESource to which
 * @extension belongs.
 *
 * Returns: the display name of the WebDAV resource
 *
 * Since: 3.6
 **/
const gchar *
e_source_webdav_get_display_name (ESourceWebdav *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), NULL);

	return extension->priv->display_name;
}

/**
 * e_source_webdav_dup_display_name:
 * @extension: an #ESourceWebdav
 *
 * Thread-safe variation of e_source_webdav_get_display_name().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceWebdav:display-name
 *
 * Since: 3.6
 **/
gchar *
e_source_webdav_dup_display_name (ESourceWebdav *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_webdav_get_display_name (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_webdav_set_display_name:
 * @extension: an #ESourceWebdav
 * @display_name: (allow-none): the display name of the WebDAV resource,
 *                or %NULL
 *
 * Updates the last known display name of a WebDAV resource, which may
 * differ from the #ESource:display-name property of the #ESource to which
 * @extension belongs.
 *
 * The internal copy of @display_name is automatically stripped of leading
 * and trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.6
 **/
void
e_source_webdav_set_display_name (ESourceWebdav *extension,
                                  const gchar *display_name)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV (extension));

	g_mutex_lock (extension->priv->property_lock);

	g_free (extension->priv->display_name);
	extension->priv->display_name = e_util_strdup_strip (display_name);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "display-name");
}

/**
 * e_source_webdav_get_email_address:
 * @extension: an #ESourceWebdav
 *
 * Returns the user's email address which can be passed to a CalDAV server
 * if the user wishes to receive scheduling messages.
 *
 * Returns: the user's email address
 *
 * Since: 3.6
 **/
const gchar *
e_source_webdav_get_email_address (ESourceWebdav *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), NULL);

	return extension->priv->email_address;
}

/**
 * e_source_webdav_dup_email_address:
 * @extension: an #ESourceWebdav
 *
 * Thread-safe variation of e_source_webdav_get_email_address().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: the newly-allocated copy of #ESourceWebdav:email-address
 *
 * Since: 3.6
 **/
gchar *
e_source_webdav_dup_email_address (ESourceWebdav *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_webdav_get_email_address (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_webdav_set_email_address:
 * @extension: an #ESourceWebdav
 * @email_address: (allow-none): the user's email address, or %NULL
 *
 * Sets the user's email address which can be passed to a CalDAV server if
 * the user wishes to receive scheduling messages.
 *
 * The internal copy of @email_address is automatically stripped of leading
 * and trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.6
 **/
void
e_source_webdav_set_email_address (ESourceWebdav *extension,
                                   const gchar *email_address)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV (extension));

	g_mutex_lock (extension->priv->property_lock);

	g_free (extension->priv->email_address);
	extension->priv->email_address = e_util_strdup_strip (email_address);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "email-address");
}

/**
 * e_source_webdav_get_ignore_invalid_cert:
 * @extension: an #ESourceWebdav
 *
 * Returns %TRUE if invalid SSL certificates should be ignored.
 *
 * This option allows SSL certificates to be accepted even if they have
 * signed by an unrecognized Certificate Authority.
 *
 * Returns: whether invalid SSL certificates should be ignored
 *
 * Since: 3.6
 **/
gboolean
e_source_webdav_get_ignore_invalid_cert (ESourceWebdav *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), FALSE);

	return extension->priv->ignore_invalid_cert;
}

/**
 * e_source_webdav_set_ignore_invalid_cert:
 * @extension: an #ESourceWebdav
 * @ignore_invalid_cert: whether invalid SSL certificates should be ignored
 *
 * Sets whether invalid SSL certificates should be ignored.
 *
 * This option allows SSL certificates to be accepted even if they have
 * signed by an unrecognized Certificate Authority.
 *
 * Since: 3.6
 **/
void
e_source_webdav_set_ignore_invalid_cert (ESourceWebdav *extension,
                                         gboolean ignore_invalid_cert)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV (extension));

	extension->priv->ignore_invalid_cert = ignore_invalid_cert;

	g_object_notify (G_OBJECT (extension), "ignore-invalid-cert");
}

/**
 * e_source_webdav_get_resource_path:
 * @extension: an #ESourceWebdav
 *
 * Returns the absolute path to a resource on a WebDAV server.
 *
 * Returns: the absolute path to a WebDAV resource
 *
 * Since: 3.6
 **/
const gchar *
e_source_webdav_get_resource_path (ESourceWebdav *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), NULL);

	return extension->priv->resource_path;
}

/**
 * e_source_webdav_dup_resource_path:
 * @extension: an #ESourceWebdav
 *
 * Thread-safe variation of e_source_webdav_get_resource_path().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: the newly-allocated copy of #ESourceWebdav:resource-path
 *
 * Since: 3.6
 **/
gchar *
e_source_webdav_dup_resource_path (ESourceWebdav *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_webdav_get_resource_path (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_webdav_set_resource_path:
 * @extension: an #ESourceWebdav
 * @resource_path: (allow-none): the absolute path to a WebDAV resource,
 *                 or %NULL
 *
 * Sets the absolute path to a resource on a WebDAV server.
 *
 * The internal copy of @resource_path is automatically stripped of leading
 * and trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.6
 **/
void
e_source_webdav_set_resource_path (ESourceWebdav *extension,
                                   const gchar *resource_path)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV (extension));

	g_mutex_lock (extension->priv->property_lock);

	g_free (extension->priv->resource_path);
	extension->priv->resource_path = e_util_strdup_strip (resource_path);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "resource-path");
}

/**
 * e_source_webdav_dup_soup_uri:
 * @extension: an #ESourceWebdav
 *
 * This is a convenience function which returns a newly-allocated
 * #SoupURI, its contents assembled from the #ESourceAuthentication
 * extension, the #ESourceSecurity extension, and @extension itself.
 * Free the returned #SoupURI with soup_uri_free().
 *
 * Returns: (transfer full): a newly-allocated #SoupURI
 *
 * Since: 3.6
 **/
SoupURI *
e_source_webdav_dup_soup_uri (ESourceWebdav *extension)
{
	SoupURI *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_WEBDAV (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	duplicate = soup_uri_copy (extension->priv->uri);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_webdav_set_soup_uri:
 * @extension: an #ESourceWebdav
 * @uri: a #SoupURI
 *
 * This is a convenience function which propagates the components of
 * @uri to the #ESourceAuthentication extension, the #ESourceSecurity
 * extension, and @extension itself.  (The "query" and "fragment"
 * components of @uri are ignored.)
 *
 * Since: 3.6
 **/
void
e_source_webdav_set_soup_uri (ESourceWebdav *extension,
                              SoupURI *uri)
{
	g_return_if_fail (E_IS_SOURCE_WEBDAV (extension));
	g_return_if_fail (SOUP_URI_IS_VALID (uri));

	g_mutex_lock (extension->priv->property_lock);

	soup_uri_free (extension->priv->uri);
	extension->priv->uri = soup_uri_copy (uri);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "soup-uri");
}
