/*
 * e-source-resource.c
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
 * SECTION: e-source-resource
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for a remote resource
 *
 * The #ESourceResource extension holds the server-assigned identity of a
 * remote calendar, address book, or whatever else an #ESource can represent.
 *
 * This extension is typically used by an #ECollectionBackend to note a
 * server-assigned resource identity in an #ESource.  Then in a later session,
 * after querying the server for available resources, a resource identity can
 * be paired with the same #ESource #ESource:uid from the previous session,
 * allowing locally cached data from the previous session to be reused.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/e-source-resource.h>
 *
 *   ESourceResource *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
 * ]|
 **/

#include "e-source-resource.h"

#include <libedataserver/e-data-server-util.h>

#define E_SOURCE_RESOURCE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_RESOURCE, ESourceResourcePrivate))

struct _ESourceResourcePrivate {
	GMutex *property_lock;
	gchar *identity;
};

enum {
	PROP_0,
	PROP_IDENTITY
};

G_DEFINE_TYPE (
	ESourceResource,
	e_source_resource,
	E_TYPE_SOURCE_EXTENSION)

static void
source_resource_set_property (GObject *object,
                              guint property_id,
                              const GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_IDENTITY:
			e_source_resource_set_identity (
				E_SOURCE_RESOURCE (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_resource_get_property (GObject *object,
                              guint property_id,
                              GValue *value,
                              GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_IDENTITY:
			g_value_take_string (
				value,
				e_source_resource_dup_identity (
				E_SOURCE_RESOURCE (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_resource_finalize (GObject *object)
{
	ESourceResourcePrivate *priv;

	priv = E_SOURCE_RESOURCE_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->identity);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_resource_parent_class)->finalize (object);
}

static void
e_source_resource_class_init (ESourceResourceClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceResourcePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_resource_set_property;
	object_class->get_property = source_resource_get_property;
	object_class->finalize = source_resource_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_RESOURCE;

	g_object_class_install_property (
		object_class,
		PROP_IDENTITY,
		g_param_spec_string (
			"identity",
			"Identity",
			"Resource identity",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_resource_init (ESourceResource *extension)
{
	extension->priv = E_SOURCE_RESOURCE_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();
}

/**
 * e_source_resource_get_identity:
 * @extension: an #ESourceResource
 *
 * Returns the server-assigned identity of the remote resource associated
 * with the #ESource to which @extension belongs.
 *
 * Returns: the identity of a remote resource
 *
 * Since: 3.6
 **/
const gchar *
e_source_resource_get_identity (ESourceResource *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_RESOURCE (extension), NULL);

	return extension->priv->identity;
}

/**
 * e_source_resource_dup_identity:
 * @extension: an #ESourceResource
 *
 * Thread-safe variation of e_source_resource_get_identity().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceResource:identity
 *
 * Since: 3.6
 **/
gchar *
e_source_resource_dup_identity (ESourceResource *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_RESOURCE (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_resource_get_identity (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_resource_set_identity:
 * @extension: an #ESourceResource
 * @identity: (allow-none): the identity of a remote resource
 *
 * Sets the server-assigned identity of the remote resource associated with
 * the #ESource to which @extension belongs.
 *
 * The internal copy of @identity is automatically stripped of leading and
 * trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.6
 **/
void
e_source_resource_set_identity (ESourceResource *extension,
                                const gchar *identity)
{
	g_return_if_fail (E_IS_SOURCE_RESOURCE (extension));

	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->identity, identity) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->identity);
	extension->priv->identity = e_util_strdup_strip (identity);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "identity");
}
