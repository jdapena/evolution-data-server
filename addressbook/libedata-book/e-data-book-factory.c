/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>

#include "e-book-backend.h"
#include "e-book-backend-factory.h"
#include "e-data-book.h"
#include "e-data-book-factory.h"

#include "e-gdbus-book-factory.h"

#define d(x)

#define E_DATA_BOOK_FACTORY_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_DATA_BOOK_FACTORY, EDataBookFactoryPrivate))

struct _EDataBookFactoryPrivate {
	ESourceRegistry *registry;
	EGdbusBookFactory *gdbus_object;

	GMutex books_lock;
	/* A hash of object paths for book URIs to EDataBooks */
	GHashTable *books;

	GMutex connections_lock;
	/* This is a hash of client addresses to GList* of EDataBooks */
	GHashTable *connections;
};

enum {
	PROP_0,
	PROP_REGISTRY
};

/* Forward Declarations */
static void	e_data_book_factory_initable_init
						(GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EDataBookFactory,
	e_data_book_factory,
	E_TYPE_DATA_FACTORY,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_book_factory_initable_init))

static EBackend *
data_book_factory_ref_backend (EDataFactory *factory,
                               ESource *source,
                               GError **error)
{
	EBackend *backend;
	ESourceBackend *extension;
	const gchar *extension_name;
	gchar *backend_name;

	/* For address books the hash key is simply the backend name.
	 * (cf. calendar hash keys, which are slightly more complex) */

	extension_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;
	extension = e_source_get_extension (source, extension_name);
	backend_name = e_source_backend_dup_backend_name (extension);

	if (backend_name == NULL || *backend_name == '\0') {
		g_set_error (
			error, E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("No backend name in source '%s'"),
			e_source_get_display_name (source));
		g_free (backend_name);
		return NULL;
	}

	backend = e_data_factory_ref_backend (factory, backend_name, source);

	if (backend == NULL)
		g_set_error (
			error, E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("Invalid backend name '%s' in source '%s'"),
			backend_name, e_source_get_display_name (source));

	g_free (backend_name);

	return backend;
}

static gchar *
construct_book_factory_path (void)
{
	static volatile gint counter = 1;

	g_atomic_int_inc (&counter);

	return g_strdup_printf (
		"/org/gnome/evolution/dataserver/AddressBook/%d/%u",
		getpid (), counter);
}

static gboolean
remove_dead_pointer_cb (gpointer path,
                        gpointer live,
                        gpointer dead)
{
	return live == dead;
}

static void
book_freed_cb (EDataBookFactory *factory,
               GObject *dead)
{
	EDataBookFactoryPrivate *priv = factory->priv;
	GHashTableIter iter;
	gpointer hkey, hvalue;

	d (g_debug ("in factory %p (%p) is dead", factory, dead));

	g_mutex_lock (&priv->books_lock);
	g_mutex_lock (&priv->connections_lock);

	g_hash_table_foreach_remove (
		priv->books, remove_dead_pointer_cb, dead);

	g_hash_table_iter_init (&iter, priv->connections);
	while (g_hash_table_iter_next (&iter, &hkey, &hvalue)) {
		GList *books = hvalue;

		if (g_list_find (books, dead)) {
			books = g_list_remove (books, dead);
			if (books != NULL)
				g_hash_table_insert (
					priv->connections,
					g_strdup (hkey), books);
			else
				g_hash_table_remove (priv->connections, hkey);

			break;
		}
	}

	g_mutex_unlock (&priv->connections_lock);
	g_mutex_unlock (&priv->books_lock);

	e_dbus_server_release (E_DBUS_SERVER (factory));
}

static gboolean
impl_BookFactory_get_book (EGdbusBookFactory *object,
                           GDBusMethodInvocation *invocation,
                           const gchar *uid,
                           EDataBookFactory *factory)
{
	EDataBook *book;
	EBackend *backend;
	EDataBookFactoryPrivate *priv = factory->priv;
	GDBusConnection *connection;
	ESourceRegistry *registry;
	ESource *source;
	gchar *object_path;
	const gchar *sender;
	GList *list;
	GError *error = NULL;

	sender = g_dbus_method_invocation_get_sender (invocation);
	connection = g_dbus_method_invocation_get_connection (invocation);

	registry = e_data_book_factory_get_registry (factory);

	if (uid == NULL || *uid == '\0') {
		error = g_error_new_literal (
			E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("Missing source UID"));
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	source = e_source_registry_ref_source (registry, uid);

	if (source == NULL) {
		error = g_error_new (
			E_DATA_BOOK_ERROR,
			E_DATA_BOOK_STATUS_NO_SUCH_BOOK,
			_("No such source for UID '%s'"), uid);
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	backend = data_book_factory_ref_backend (
		E_DATA_FACTORY (factory), source, &error);

	g_object_unref (source);

	if (error != NULL) {
		g_dbus_method_invocation_return_gerror (invocation, error);
		g_error_free (error);

		return TRUE;
	}

	g_return_val_if_fail (E_IS_BACKEND (backend), FALSE);

	e_dbus_server_hold (E_DBUS_SERVER (factory));

	object_path = construct_book_factory_path ();

	book = e_data_book_new (
		E_BOOK_BACKEND (backend),
		connection, object_path, &error);

	if (book != NULL) {
		g_mutex_lock (&priv->books_lock);
		g_hash_table_insert (
			priv->books, g_strdup (object_path), book);
		g_mutex_unlock (&priv->books_lock);

		e_book_backend_add_client (E_BOOK_BACKEND (backend), book);

		g_object_weak_ref (
			G_OBJECT (book), (GWeakNotify)
			book_freed_cb, factory);

		/* Update the hash of open connections. */
		g_mutex_lock (&priv->connections_lock);
		list = g_hash_table_lookup (priv->connections, sender);
		list = g_list_prepend (list, book);
		g_hash_table_insert (
			priv->connections, g_strdup (sender), list);
		g_mutex_unlock (&priv->connections_lock);
	}

	g_object_unref (backend);

	e_gdbus_book_factory_complete_get_book (
		object, invocation, object_path, error);

	if (error != NULL)
		g_error_free (error);

	g_free (object_path);

	return TRUE;
}

static void
remove_data_book_cb (EDataBook *data_book)
{
	EBookBackend *backend;

	g_return_if_fail (data_book != NULL);

	backend = e_data_book_get_backend (data_book);
	e_book_backend_remove_client (backend, data_book);

	g_object_unref (data_book);
}

static void
data_book_factory_get_property (GObject *object,
                                guint property_id,
                                GValue *value,
                                GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_REGISTRY:
			g_value_set_object (
				value,
				e_data_book_factory_get_registry (
				E_DATA_BOOK_FACTORY (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_book_factory_dispose (GObject *object)
{
	EDataBookFactoryPrivate *priv;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (object);

	if (priv->registry != NULL) {
		g_object_unref (priv->registry);
		priv->registry = NULL;
	}

	if (priv->gdbus_object != NULL) {
		g_object_unref (priv->gdbus_object);
		priv->gdbus_object = NULL;
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->dispose (object);
}

static void
data_book_factory_finalize (GObject *object)
{
	EDataBookFactoryPrivate *priv;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (object);

	g_hash_table_destroy (priv->books);
	g_hash_table_destroy (priv->connections);

	g_mutex_clear (&priv->books_lock);
	g_mutex_clear (&priv->connections_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_factory_parent_class)->finalize (object);
}

static void
data_book_factory_bus_acquired (EDBusServer *server,
                                GDBusConnection *connection)
{
	EDataBookFactoryPrivate *priv;
	guint registration_id;
	GError *error = NULL;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (server);

	registration_id = e_gdbus_book_factory_register_object (
		priv->gdbus_object,
		connection,
		"/org/gnome/evolution/dataserver/AddressBookFactory",
		&error);

	if (error != NULL) {
		g_error (
			"Failed to register a BookFactory object: %s",
			error->message);
		g_assert_not_reached ();
	}

	g_assert (registration_id > 0);

	/* Chain up to parent's bus_acquired() method. */
	E_DBUS_SERVER_CLASS (e_data_book_factory_parent_class)->
		bus_acquired (server, connection);
}

static void
data_book_factory_bus_name_lost (EDBusServer *server,
                                 GDBusConnection *connection)
{
	EDataBookFactoryPrivate *priv;
	GList *list = NULL;
	gchar *key;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (server);

	g_mutex_lock (&priv->connections_lock);

	while (g_hash_table_lookup_extended (
		priv->connections,
		ADDRESS_BOOK_DBUS_SERVICE_NAME,
		(gpointer) &key, (gpointer) &list)) {
		GList *copy;

		/* this should trigger the book's weak ref notify
		 * function, which will remove it from the list before
		 * it's freed, and will remove the connection from
		 * priv->connections once they're all gone */
		copy = g_list_copy (list);
		g_list_foreach (copy, (GFunc) remove_data_book_cb, NULL);
		g_list_free (copy);
	}

	g_mutex_unlock (&priv->connections_lock);

	/* Chain up to parent's bus_name_lost() method. */
	E_DBUS_SERVER_CLASS (e_data_book_factory_parent_class)->
		bus_name_lost (server, connection);
}

static void
data_book_factory_quit_server (EDBusServer *server,
                               EDBusServerExitCode exit_code)
{
	/* This factory does not support reloading, so stop the signal
	 * emission and return without chaining up to prevent quitting. */
	if (exit_code == E_DBUS_SERVER_EXIT_RELOAD) {
		g_signal_stop_emission_by_name (server, "quit-server");
		return;
	}

	/* Chain up to parent's quit_server() method. */
	E_DBUS_SERVER_CLASS (e_data_book_factory_parent_class)->
		quit_server (server, exit_code);
}

static gboolean
data_book_factory_initable_init (GInitable *initable,
                                 GCancellable *cancellable,
                                 GError **error)
{
	EDataBookFactoryPrivate *priv;

	priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (initable);

	priv->registry = e_source_registry_new_sync (cancellable, error);

	return (priv->registry != NULL);
}

static void
e_data_book_factory_class_init (EDataBookFactoryClass *class)
{
	GObjectClass *object_class;
	EDBusServerClass *dbus_server_class;
	EDataFactoryClass *data_factory_class;
	const gchar *modules_directory = BACKENDDIR;
	const gchar *modules_directory_env;

	modules_directory_env = g_getenv (EDS_ADDRESS_BOOK_MODULES);
	if (modules_directory_env &&
	    g_file_test (modules_directory_env, G_FILE_TEST_IS_DIR))
		modules_directory = g_strdup (modules_directory_env);

	g_type_class_add_private (class, sizeof (EDataBookFactoryPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = data_book_factory_get_property;
	object_class->dispose = data_book_factory_dispose;
	object_class->finalize = data_book_factory_finalize;

	dbus_server_class = E_DBUS_SERVER_CLASS (class);
	dbus_server_class->bus_name = ADDRESS_BOOK_DBUS_SERVICE_NAME;
	dbus_server_class->module_directory = modules_directory;
	dbus_server_class->bus_acquired = data_book_factory_bus_acquired;
	dbus_server_class->bus_name_lost = data_book_factory_bus_name_lost;
	dbus_server_class->quit_server = data_book_factory_quit_server;

	data_factory_class = E_DATA_FACTORY_CLASS (class);
	data_factory_class->backend_factory_type = E_TYPE_BOOK_BACKEND_FACTORY;

	g_object_class_install_property (
		object_class,
		PROP_REGISTRY,
		g_param_spec_object (
			"registry",
			"Registry",
			"Data source registry",
			E_TYPE_SOURCE_REGISTRY,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_data_book_factory_initable_init (GInitableIface *interface)
{
	interface->init = data_book_factory_initable_init;
}

static void
e_data_book_factory_init (EDataBookFactory *factory)
{
	factory->priv = E_DATA_BOOK_FACTORY_GET_PRIVATE (factory);

	factory->priv->gdbus_object = e_gdbus_book_factory_stub_new ();
	g_signal_connect (
		factory->priv->gdbus_object, "handle-get-book",
		G_CALLBACK (impl_BookFactory_get_book), factory);

	g_mutex_init (&factory->priv->books_lock);
	factory->priv->books = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	g_mutex_init (&factory->priv->connections_lock);
	factory->priv->connections = g_hash_table_new_full (
		g_str_hash, g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);
}

EDBusServer *
e_data_book_factory_new (GCancellable *cancellable,
                         GError **error)
{
	return g_initable_new (
		E_TYPE_DATA_BOOK_FACTORY,
		cancellable, error, NULL);
}

/**
 * e_data_book_factory_get_registry:
 * @factory: an #EDataBookFactory
 *
 * Returns the #ESourceRegistry owned by @factory.
 *
 * Returns: the #ESourceRegistry
 *
 * Since: 3.6
 **/
ESourceRegistry *
e_data_book_factory_get_registry (EDataBookFactory *factory)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_FACTORY (factory), NULL);

	return factory->priv->registry;
}

