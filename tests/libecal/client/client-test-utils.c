/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdio.h>
#include <libedataserver/libedataserver.h>

#include "client-test-utils.h"

void
print_ecomp (ECalComponent *ecalcomp)
{
	const gchar *uid = NULL;
	ECalComponentText summary = { 0 };

	g_return_if_fail (ecalcomp != NULL);

	e_cal_component_get_uid (ecalcomp, &uid);
	e_cal_component_get_summary (ecalcomp, &summary);

	g_print ("   Component: %s\n", uid ? uid : "no-uid");
	g_print ("   Summary: %s\n", summary.value ? summary.value : "NULL");
	g_print ("\n");
}

void
print_icomp (icalcomponent *icalcomp)
{
	ECalComponent *ecomp;

	g_return_if_fail (icalcomp != NULL);

	ecomp = e_cal_component_new ();
	icalcomp = icalcomponent_new_clone (icalcomp);

	if (!e_cal_component_set_icalcomponent (ecomp, icalcomp)) {
		icalcomponent_free (icalcomp);
		g_object_unref (ecomp);
		g_printerr ("%s: Failed to assing icalcomp to ECalComponent\n", G_STRFUNC);
		g_print ("\n");
		return;
	}

	print_ecomp (ecomp);

	g_object_unref (ecomp);
}

void
report_error (const gchar *operation,
              GError **error)
{
	g_return_if_fail (operation != NULL);

	g_printerr ("Failed to %s: %s\n", operation, (error && *error) ? (*error)->message : "Unknown error");

	g_clear_error (error);
}

void
main_initialize (void)
{
	static gboolean initialized = FALSE;

	if (initialized)
		return;

	g_type_init ();
	e_gdbus_templates_init_main_thread ();

	initialized = TRUE;
}

struct IdleData {
	GThreadFunc func;
	gpointer data;
	gboolean run_in_thread; /* FALSE to run in idle callback */
};

static gboolean
idle_cb (gpointer data)
{
	struct IdleData *idle = data;

	g_return_val_if_fail (idle != NULL, FALSE);
	g_return_val_if_fail (idle->func != NULL, FALSE);

	if (idle->run_in_thread) {
		GThread *thread;

		thread = g_thread_new (NULL, idle->func, idle->data);
		g_thread_unref (thread);
	} else {
		idle->func (idle->data);
	}

	g_free (idle);

	return FALSE;
}

static GMainLoop *loop = NULL;
static gint main_stop_result = 0;

static void
do_start (GThreadFunc func,
          gpointer data)
{
	main_initialize ();

	g_return_if_fail (loop == NULL);

	loop = g_main_loop_new (NULL, FALSE);

	if (func)
		func (data);

	g_main_loop_run (loop);

	g_main_loop_unref (loop);
	loop = NULL;
}

/* Starts new main-loop, but just before that calls 'func'.
 * Main-loop is kept running, and this function blocks,
 * until call of stop_main_loop (). */
void
start_main_loop (GThreadFunc func,
                 gpointer data)
{
	g_return_if_fail (loop == NULL);

	do_start (func, data);
}

/* Starts new main-loop and then invokes func in a new thread.
 * Main-loop is kept running, and this function blocks,
 * until call of stop_main_loop (). */
void
start_in_thread_with_main_loop (GThreadFunc func,
                                gpointer data)
{
	struct IdleData *idle;

	g_return_if_fail (func != NULL);
	g_return_if_fail (loop == NULL);

	main_initialize ();

	idle = g_new0 (struct IdleData, 1);
	idle->func = func;
	idle->data = data;
	idle->run_in_thread = TRUE;

	g_idle_add (idle_cb, idle);

	do_start (NULL, NULL);
}

/* Starts new main-loop and then invokes func in an idle callback.
 * Main-loop is kept running, and this function blocks,
 * until call of stop_main_loop (). */
void
start_in_idle_with_main_loop (GThreadFunc func,
                              gpointer data)
{
	struct IdleData *idle;

	g_return_if_fail (func != NULL);
	g_return_if_fail (loop == NULL);

	main_initialize ();

	idle = g_new0 (struct IdleData, 1);
	idle->func = func;
	idle->data = data;
	idle->run_in_thread = FALSE;

	g_idle_add (idle_cb, idle);

	do_start (NULL, NULL);
}

/* Stops main-loop previously run by start_main_loop,
 * start_in_thread_with_main_loop or start_in_idle_with_main_loop.
*/
void
stop_main_loop (gint stop_result)
{
	g_return_if_fail (loop != NULL);

	main_stop_result = stop_result;
	g_main_loop_quit (loop);
}

/* returns value used in stop_main_loop() */
gint
get_main_loop_stop_result (void)
{
	return main_stop_result;
}

void
foreach_configured_source (ESourceRegistry *registry,
                           ECalClientSourceType source_type,
                           void (*func) (ESource *source,
                           ECalClientSourceType source_type))
{
	gpointer foreach_async_data;
	ESource *source = NULL;

	g_return_if_fail (func != NULL);

	main_initialize ();

	foreach_async_data = foreach_configured_source_async_start (registry, source_type, &source);
	if (!foreach_async_data)
		return;

	do {
		func (source, source_type);
	} while (foreach_configured_source_async_next (&foreach_async_data, &source));
}

struct ForeachConfiguredData {
	ECalClientSourceType source_type;
	GList *list;
};

gpointer
foreach_configured_source_async_start (ESourceRegistry *registry,
                                       ECalClientSourceType source_type,
                                       ESource **source)
{
	struct ForeachConfiguredData *async_data;
	const gchar *extension_name;
	GList *list;

	g_return_val_if_fail (source != NULL, NULL);

	main_initialize ();

	switch (source_type) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
			extension_name = E_SOURCE_EXTENSION_CALENDAR;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			extension_name = E_SOURCE_EXTENSION_TASK_LIST;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
			extension_name = E_SOURCE_EXTENSION_MEMO_LIST;
			break;
		default:
			g_assert_not_reached ();
	}

	list = e_source_registry_list_sources (registry, extension_name);

	async_data = g_new0 (struct ForeachConfiguredData, 1);
	async_data->source_type = source_type;
	async_data->list = list;

	*source = async_data->list->data;

	return async_data;
}

gboolean
foreach_configured_source_async_next (gpointer *foreach_async_data,
                                      ESource **source)
{
	struct ForeachConfiguredData *async_data;

	g_return_val_if_fail (foreach_async_data != NULL, FALSE);
	g_return_val_if_fail (source != NULL, FALSE);

	async_data = *foreach_async_data;
	g_return_val_if_fail (async_data != NULL, FALSE);

	if (async_data->list) {
		g_object_unref (async_data->list->data);
		async_data->list = async_data->list->next;
	}
	if (async_data->list) {
		*source = async_data->list->data;
		return TRUE;
	}

	g_free (async_data);

	*foreach_async_data = NULL;

	return FALSE;
}

ECalClientSourceType
foreach_configured_source_async_get_source_type (gpointer foreach_async_data)
{
	struct ForeachConfiguredData *async_data = foreach_async_data;

	g_return_val_if_fail (foreach_async_data != NULL, E_CAL_CLIENT_SOURCE_TYPE_LAST);

	return async_data->source_type;
}

ECalClient *
new_temp_client (ECalClientSourceType source_type,
                 gchar **uri)
{
#if 0  /* ACCOUNT_MGMT */
	ECalClient *cal_client;
	ESource *source;
	gchar *abs_uri, *filename;
	GError *error = NULL;

	filename = g_build_filename (g_get_tmp_dir (), "e-cal-client-test-XXXXXX/", NULL);
	abs_uri = g_strconcat ("local:", filename, NULL);
	g_free (filename);

	source = e_source_new_with_absolute_uri ("Test cal", abs_uri);
	if (uri)
		*uri = abs_uri;
	else
		g_free (abs_uri);

	g_return_val_if_fail (source != NULL, NULL);

	cal_client = e_cal_client_new (source, source_type, &error);
	g_object_unref (source);

	if (error)
		report_error ("new temp client", &error);

	return cal_client;
#endif /* ACCOUNT_MGMT */

	return NULL;
}
