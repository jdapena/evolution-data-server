/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libebook/libebook.h>

#include "client-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure book_closure = { E_TEST_SERVER_ADDRESS_BOOK, NULL, 0 };

#define EMAIL_ADD "foo@bar.com"

static void
verify_premodify_and_prepare_contact (EContact *contact)
{
	EVCardAttribute *attr;

	/* ensure there is no email address to begin with, then add one */
	g_assert (!e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL));
	attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
	e_vcard_add_attribute_with_value (E_VCARD (contact), attr, EMAIL_ADD);
}

static void
verify_modify (EContact *contact)
{
	EVCardAttribute *attr;
	gchar *email_value;

	g_assert ((attr = e_vcard_get_attribute (E_VCARD (contact), EVC_EMAIL)));
	g_assert (e_vcard_attribute_is_single_valued (attr));
	email_value = e_vcard_attribute_get_value (attr);
	g_assert (!g_strcmp0 (email_value, EMAIL_ADD));
	g_free (email_value);
}

static void
test_modify_contact_sync (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBookClient *book_client;
	GError *error = NULL;
	EContact *contact, *book_contact;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "name-only", &contact))
		g_error ("Failed to add contact");

	verify_premodify_and_prepare_contact (contact);

	if (!e_book_client_modify_contact_sync (book_client, contact, NULL, &error))
		g_error ("modify contact sync: %s", error->message);

	if (!e_book_client_get_contact_sync (book_client, e_contact_get_const (contact, E_CONTACT_UID),
					     &book_contact, NULL, &error))
		g_error ("get contact sync: %s", error->message);

	verify_modify (book_contact);
	g_object_unref (book_contact);
	g_object_unref (contact);
}

typedef struct {
	EContact *contact;
	GMainLoop *loop;
} ModifyData;

static void
contact_ready_cb (GObject *source_object,
                  GAsyncResult *result,
                  gpointer user_data)
{
	EContact *contact;
	GError *error = NULL;
	GMainLoop *loop = (GMainLoop *) user_data;

	if (!e_book_client_get_contact_finish (E_BOOK_CLIENT (source_object), result, &contact, &error))
		g_error ("get contact finish: %s", error->message);

	verify_modify (contact);

	g_object_unref (contact);
	g_main_loop_quit (loop);
}

static void
contact_modified_cb (GObject *source_object,
                     GAsyncResult *result,
                     gpointer user_data)
{
	ModifyData *data = (ModifyData *) user_data;
	GError *error = NULL;

	if (!e_book_client_modify_contact_finish (E_BOOK_CLIENT (source_object), result, &error))
		g_error ("modify contact finish: %s", error->message);

	e_book_client_get_contact (
		E_BOOK_CLIENT (source_object),
		e_contact_get_const (data->contact, E_CONTACT_UID),
		NULL, contact_ready_cb, data->loop);
}

static void
test_modify_contact_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	EBookClient *book_client;
	EContact *contact;
	ModifyData data;

	book_client = E_TEST_SERVER_UTILS_SERVICE (fixture, EBookClient);

	if (!add_contact_from_test_case_verify (book_client, "name-only", &contact))
		g_error ("Failed to add contact");

	verify_premodify_and_prepare_contact (contact);

	data.contact = contact;
	data.loop = fixture->loop;
	e_book_client_modify_contact (book_client, contact, NULL, contact_modified_cb, &data);

	g_main_loop_run (fixture->loop);

	g_object_unref (contact);
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
		"/EBookClient/ModifyContact/Sync", ETestServerFixture, &book_closure,
		e_test_server_utils_setup, test_modify_contact_sync, e_test_server_utils_teardown);
	g_test_add (
		"/EBookClient/ModifyContact/Async", ETestServerFixture, &book_closure,
		e_test_server_utils_setup, test_modify_contact_async, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
