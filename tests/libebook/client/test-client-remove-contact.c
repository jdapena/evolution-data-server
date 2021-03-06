/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

static void
check_removed_contact (EBookClient *book_client,
		       const gchar *uid)
{
	GError *error = NULL;
	EContact *contact = NULL;

	if (e_book_client_get_contact_sync (book_client, uid, &contact, NULL, &error))
		g_error ("succeeded to fetch removed contact");
	else if (!g_error_matches (error, E_BOOK_CLIENT_ERROR, E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND))
		g_error ("Wrong error in get contact sync on removed contact: %s (domain: %s, code: %d)",
			 error->message, g_quark_to_string (error->domain), error->code);
	else
		g_clear_error (&error);
}

static void
test_remove_contact_sync (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact = NULL;
	gchar *uid;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact))
		g_error ("Failed to add contact");

	uid = e_contact_get (contact, E_CONTACT_UID);

	if (!e_book_client_remove_contact_sync (book_client, contact, NULL, &error))
		g_error ("remove contact sync: %s", error->message);

	g_object_unref (contact);

	check_removed_contact (book_client, uid);

	g_free (uid);
}

typedef struct {
	const gchar *uid;
	GMainLoop *loop;
} RemoveData;

static void
remove_contact_cb (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
	RemoveData *data = (RemoveData *) user_data;
	GError *error = NULL;

	if (!e_book_client_remove_contact_finish (E_BOOK_CLIENT (source_object), result, &error))
		g_error ("remove contact finish: %s", error->message);

	check_removed_contact (E_BOOK_CLIENT (source_object), data->uid);

	g_main_loop_quit (data->loop);
}

static void
test_remove_contact_async (ETestServerFixture *fixture,
                           gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact = NULL;
	gchar *uid;
	RemoveData data;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "simple-1", &contact))
		g_error ("Failed to add contact");

	uid = e_contact_get (contact, E_CONTACT_UID);

	data.uid = uid;
	data.loop = fixture->loop;
	e_book_client_remove_contact (book_client, contact, NULL, remove_contact_cb, &data);

	g_object_unref (contact);

	g_main_loop_run (fixture->loop);
	g_free (uid);
}

gint
main (gint argc,
      gchar **argv)
{
#if !GLIB_CHECK_VERSION (2, 35, 1)
	g_type_init ();
#endif
	g_test_init (&argc, &argv, NULL);

	g_test_add (
		"/EBookClient/RemoveContact/Sync", ETestServerFixture, &book_closure,
		e_test_server_utils_setup, test_remove_contact_sync, e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/RemoveContact/Async", ETestServerFixture, &book_closure,
		e_test_server_utils_setup, test_remove_contact_async, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
