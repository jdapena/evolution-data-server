/*
 * evolution-source-registry.c
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

#include <config.h>
#include <locale.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#if defined (ENABLE_MAINTAINER_MODE) && defined (HAVE_GTK)
#include <gtk/gtk.h>
#endif

#include <libebackend/libebackend.h>

#include "evolution-source-registry-resource.h"

#define RESOURCE_PATH_RO_SOURCES "/org/gnome/evolution-data-server/ro-sources"
#define RESOURCE_PATH_RW_SOURCES "/org/gnome/evolution-data-server/rw-sources"

/* Forward Declarations */
void evolution_source_registry_migrate_basedir (void);
void evolution_source_registry_migrate_sources (void);

gboolean	evolution_source_registry_migrate_imap_to_imapx
						(ESourceRegistryServer *server,
						 GKeyFile *key_file,
						 const gchar *uid);

static void
evolution_source_registry_load_error (ESourceRegistryServer *server,
                                      GFile *file,
                                      const GError *error)
{
	gchar *uri = g_file_get_uri (file);

	g_printerr (
		"** Failed to load key file at '%s': %s\n",
		uri, error->message);

	g_free (uri);
}

static gboolean
evolution_source_registry_load_all (ESourceRegistryServer *server,
                                    GError **error)
{
	ESourcePermissionFlags flags;
	GResource *resource;
	const gchar *path;
	gboolean success;

	g_return_val_if_fail (E_IS_SOURCE_REGISTRY_SERVER (server), FALSE);

	/* Load the user's sources directory first so that user-specific
	 * data sources overshadow predefined data sources with identical
	 * UIDs.  The 'local' data source is one such example. */

	path = e_server_side_source_get_user_dir ();
	flags = E_SOURCE_PERMISSION_REMOVABLE |
		E_SOURCE_PERMISSION_WRITABLE;
	success = e_source_registry_server_load_directory (
		server, path, flags, error);
	g_prefix_error (error, "%s: ", path);

	if (!success)
		return FALSE;

	resource = evolution_source_registry_get_resource ();

	path = RESOURCE_PATH_RO_SOURCES;
	flags = E_SOURCE_PERMISSION_NONE;
	success = e_source_registry_server_load_resource (
		server, resource, path, flags, error);
	g_prefix_error (error, "%s: ", path);

	if (!success)
		return FALSE;

	path = RESOURCE_PATH_RW_SOURCES;
	flags = E_SOURCE_PERMISSION_WRITABLE;
	success = e_source_registry_server_load_resource (
		server, resource, path, flags, error);
	g_prefix_error (error, "%s: ", path);

	if (!success)
		return FALSE;

	/* Signal that all files are now loaded.  One thing this
	 * does is tell the cache-reaper module to start scanning
	 * for orphaned cache directories. */
	g_signal_emit_by_name (server, "files-loaded");

	return TRUE;
}

gint
main (gint argc,
      gchar **argv)
{
	EDBusServer *server;
	EDBusServerExitCode exit_code;
	GError *error = NULL;

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

#if defined (ENABLE_MAINTAINER_MODE) && defined (HAVE_GTK)
	/* This is only to load gtk-modules, like
	 * bug-buddy's gnomesegvhandler, if possible */
	gtk_init_check (&argc, &argv);
#else
	g_type_init ();
#endif

	e_gdbus_templates_init_main_thread ();

reload:
	/* Migrate user data from ~/.evolution to XDG base directories. */
	evolution_source_registry_migrate_basedir ();

	/* Migrate ESource data from GConf XML blobs to key files.
	 * Do this AFTER XDG base directory migration since the key
	 * files are saved according to XDG base directory settings. */
	evolution_source_registry_migrate_sources ();

	server = e_source_registry_server_new ();

	g_signal_connect (
		server, "load-error", G_CALLBACK (
		evolution_source_registry_load_error),
		NULL);

	/* Convert "imap" mail accounts to "imapx". */
	g_signal_connect (
		server, "tweak-key-file", G_CALLBACK (
		evolution_source_registry_migrate_imap_to_imapx),
		NULL);

	/* Failure here is fatal.  Don't even try to keep going. */
	evolution_source_registry_load_all (
		E_SOURCE_REGISTRY_SERVER (server), &error);

	if (error != NULL) {
		g_printerr ("%s\n", error->message);
		g_object_unref (server);
		exit (EXIT_FAILURE);
	}

	g_print ("Server is up and running...\n");

	/* Keep the server from quitting on its own.
	 * We don't have a way of tracking number of
	 * active clients, so once the server is up,
	 * it's up until the session bus closes. */
	e_dbus_server_hold (server);

	exit_code = e_dbus_server_run (server, FALSE);

	g_object_unref (server);

	if (exit_code == E_DBUS_SERVER_EXIT_RELOAD) {
		const gchar *config_dir;
		gchar *dirname;

		g_print ("Reloading...\n");

		/* It's possible the Reload is called after restore, where
		 * the ~/.config/evolution/sources directory can be missing,
		 * thus create it, because e_server_side_source_get_user_dir()
		 * may have its static variable already set to non-NULL value.
		*/
		config_dir = e_get_user_config_dir ();
		dirname = g_build_filename (config_dir, "sources", NULL);
		g_mkdir_with_parents (dirname, 0700);
		g_free (dirname);

		goto reload;
	}

	g_print ("Bye.\n");

	return 0;
}
