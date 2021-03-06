/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/libecal.h>

#include "ecal-test-utils.h"
#include "e-test-server-utils.h"

static ETestServerClosure cal_closure =
	{ E_TEST_SERVER_DEPRECATED_CALENDAR, NULL, E_CAL_SOURCE_TYPE_EVENT };

static void
test_get_alarm_email_address (ETestServerFixture *fixture,
                              gconstpointer user_data)
{
	ECal *cal;
	gchar *address;

	cal = E_TEST_SERVER_UTILS_SERVICE (fixture, ECal);

	address = ecal_test_utils_cal_get_alarm_email_address (cal);
	test_print ("alarm email address: '%s'\n", address);
	g_free (address);
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
		"/ECal/GetAlarmEmailAddress", ETestServerFixture, &cal_closure,
		e_test_server_utils_setup, test_get_alarm_email_address, e_test_server_utils_teardown);

	return e_test_server_utils_run ();
}
