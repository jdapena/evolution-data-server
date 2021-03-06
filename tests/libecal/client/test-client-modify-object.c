/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>
#include <libical/ical.h>

#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_CALENDAR, NULL, E_CAL_CLIENT_SOURCE_TYPE_EVENTS };

#define EVENT_SUMMARY "Creation of new test event"

static void
setup_cal (ECalClient *cal_client)
{
	struct icaltimetype now;
	icalcomponent *icalcomp;
	gchar *uid = NULL;
	GError *error = NULL;

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	icalcomp = icalcomponent_new (ICAL_VEVENT_COMPONENT);
	icalcomponent_set_summary (icalcomp, "Initial" EVENT_SUMMARY);
	icalcomponent_set_dtstart (icalcomp, now);
	icalcomponent_set_dtend   (icalcomp, icaltime_from_timet (icaltime_as_timet (now) + 60 * 60 * 60, 0));

	if (!e_cal_client_create_object_sync (cal_client, icalcomp, &uid, NULL, &error))
		g_error ("create object sync: %s", error->message);

	icalcomponent_free (icalcomp);
	g_object_set_data_full (G_OBJECT (cal_client), "use-uid", uid, g_free);
}

static void
test_result (icalcomponent *icalcomp)
{
	g_assert (icalcomp != NULL);
	g_assert_cmpstr (icalcomponent_get_summary (icalcomp), ==, EVENT_SUMMARY);
}

static void
test_modify_object_sync (ETestServerFixture *fixture,
                         gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp = NULL;
	const gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	setup_cal (cal_client);
	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");
	g_assert (uid != NULL);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error))
		g_error ("get object sync: %s", error->message);

	icalcomponent_set_summary (icalcomp, EVENT_SUMMARY);

	if (!e_cal_client_modify_object_sync (cal_client, icalcomp, CALOBJ_MOD_ALL, NULL, &error))
		g_error ("modify object sync: %s", error->message);

	icalcomponent_free (icalcomp);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error))
		g_error ("get object sync after modification: %s", error->message);

	test_result (icalcomp);
	icalcomponent_free (icalcomp);
}

static void
async_modify_result_ready (GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp = NULL;
	const gchar *uid;
	GMainLoop *loop = (GMainLoop *) user_data;

	cal_client = E_CAL_CLIENT (source_object);
	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");

	if (!e_cal_client_modify_object_finish (cal_client, result, &error))
		g_error ("modify object finish: %s", error->message);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error))
		g_error ("get object sync after async modification: %s", error->message);

	test_result (icalcomp);
	icalcomponent_free (icalcomp);

	g_main_loop_quit (loop);
}

static void
test_modify_object_async (ETestServerFixture *fixture,
                          gconstpointer user_data)
{
	ECalClient *cal_client;
	GError *error = NULL;
	icalcomponent *icalcomp = NULL;
	const gchar *uid;

	cal_client = E_TEST_SERVER_UTILS_SERVICE (fixture, ECalClient);

	setup_cal (cal_client);
	uid = g_object_get_data (G_OBJECT (cal_client), "use-uid");
	g_assert (uid != NULL);

	if (!e_cal_client_get_object_sync (cal_client, uid, NULL, &icalcomp, NULL, &error))
		g_error ("get object sync: %s", error->message);

	icalcomponent_set_summary (icalcomp, EVENT_SUMMARY);

	e_cal_client_modify_object (cal_client, icalcomp, CALOBJ_MOD_ALL, NULL, async_modify_result_ready, fixture->loop);
	icalcomponent_free (icalcomp);

	g_main_loop_run (fixture->loop);
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
		"/ECalClient/ModifyObject/Sync", ETestServerFixture, &cal_closure,
		e_test_server_utils_setup, test_modify_object_sync, e_test_server_utils_teardown);
	g_test_add (
		"/ECalClient/ModifyObject/Async", ETestServerFixture, &cal_closure,
		e_test_server_utils_setup, test_modify_object_async, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
