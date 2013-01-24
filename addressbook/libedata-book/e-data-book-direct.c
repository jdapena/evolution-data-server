/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012 Intel Corporation
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
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-data-book-direct.h"
#include "e-gdbus-book-direct.h"

#define E_DATA_BOOK_DIRECT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_BOOK_DIRECT, EDataBookDirectPrivate))

G_DEFINE_TYPE (EDataBookDirect, e_data_book_direct, G_TYPE_OBJECT);
#define THRESHOLD_ITEMS   32	/* how many items can be hold in a cache, before propagated to UI */
#define THRESHOLD_SECONDS  2	/* how long to wait until notifications are propagated to UI; in seconds */

struct _EDataBookDirectPrivate {
	EGdbusBookDirect *gdbus_object;
};

/* GObjectClass */
static void
e_data_book_direct_dispose (GObject *object)
{
	EDataBookDirect *direct = E_DATA_BOOK_DIRECT (object);

	if (direct->priv->gdbus_object) {
		g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (direct->priv->gdbus_object));
		g_object_unref (direct->priv->gdbus_object);
		direct->priv->gdbus_object = NULL;
	}

	G_OBJECT_CLASS (e_data_book_direct_parent_class)->dispose (object);
}

static void
e_data_book_direct_init (EDataBookDirect *direct)
{
	direct->priv = E_DATA_BOOK_DIRECT_GET_PRIVATE (direct);
	direct->priv->gdbus_object = e_gdbus_book_direct_skeleton_new ();
}

static void
e_data_book_direct_class_init (EDataBookDirectClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	g_type_class_add_private (class, sizeof (EDataBookDirectPrivate));

	object_class->dispose = e_data_book_direct_dispose;
}

/**
 * e_data_book_direct_new:
 * @backend_path: Full path to the installed backend shared library
 * @backend_factory_name: Type name of the EBookBackendFactory implemented by the library
 * @config: A backend specific configuration string
 *
 * Creates a #EDataBookDirect to report configuration data needed for direct
 * read access.
 *
 * This is returned by e_book_backend_get_direct_book() for backends
 * which support direct read access mode.
 *
 * Returns: (transfer full): A newly created #EDataBookDirect
 *
 * Since: 3.8
 */
EDataBookDirect *
e_data_book_direct_new (const gchar *backend_path,
			const gchar *backend_factory_name,
			const gchar *config)
{
	EDataBookDirect *direct;

	g_return_val_if_fail (backend_path && backend_path[0], NULL);
	g_return_val_if_fail (backend_factory_name && backend_factory_name[0], NULL);

	direct = g_object_new (E_TYPE_DATA_BOOK_DIRECT, NULL);

	e_gdbus_book_direct_set_backend_path (direct->priv->gdbus_object, backend_path);
	e_gdbus_book_direct_set_backend_name (direct->priv->gdbus_object, backend_factory_name);
	e_gdbus_book_direct_set_backend_config (direct->priv->gdbus_object, config);

	return direct;
}

/**
 * e_data_book_direct_register_gdbus_object:
 * @direct: An #EDataBookDirect
 * @connection: The #GDBusConnection to register with
 * @object_path: The object path to place the direct access configuration data
 * @error: A location to store any error which might occur while registering
 *
 * Places @direct on the @connection at @object_path
 *
 * Since: 3.8
 **/
gboolean
e_data_book_direct_register_gdbus_object (EDataBookDirect *direct,
					  GDBusConnection *connection,
					  const gchar *object_path,
					  GError **error)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_DIRECT (direct), FALSE);
	g_return_val_if_fail (connection != NULL, FALSE);
	g_return_val_if_fail (object_path != NULL, 0);

	return g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (direct->priv->gdbus_object),
						 connection, object_path, error);
}
