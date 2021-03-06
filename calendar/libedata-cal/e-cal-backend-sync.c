/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef CONFIG_H
#include <config.h>
#endif

#include <libedataserver/libedataserver.h>

#include "e-cal-backend-sync.h"
#include <libical/icaltz-util.h>

#define E_CAL_BACKEND_SYNC_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_SYNC, ECalBackendSyncPrivate))

G_DEFINE_TYPE (ECalBackendSync, e_cal_backend_sync, E_TYPE_CAL_BACKEND)

struct _ECalBackendSyncPrivate {
	GMutex sync_mutex;

	gboolean mutex_lock;
};

#define LOCK_WRAPPER(func, args) G_STMT_START {									\
	gboolean locked = backend->priv->mutex_lock;								\
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->func, NotSupported);		\
	if (locked)												\
		g_mutex_lock (&backend->priv->sync_mutex);							\
	(* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->func) args;							\
	if (locked)												\
		g_mutex_unlock (&backend->priv->sync_mutex);							\
	} G_STMT_END

#define LOCK_WRAPPER_RET_VAL(func, args) G_STMT_START {								\
	gboolean locked = backend->priv->mutex_lock;								\
	e_return_data_cal_error_val_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->func, NotSupported);	\
	if (locked)												\
		g_mutex_lock (&backend->priv->sync_mutex);							\
	res = (* E_CAL_BACKEND_SYNC_GET_CLASS (backend)->func) args;						\
	if (locked)												\
		g_mutex_unlock (&backend->priv->sync_mutex);							\
	} G_STMT_END

/**
 * e_cal_backend_sync_set_lock:
 * @backend: An ECalBackendSync object.
 * @lock: Lock mode.
 *
 * Sets the lock mode on the ECalBackendSync object. If TRUE, the backend
 * will create a locking mutex for every operation, so that only one can
 * happen at a time. If FALSE, no lock would be done and many operations
 * can happen at the same time.
 */
void
e_cal_backend_sync_set_lock (ECalBackendSync *backend,
                             gboolean lock)
{
	g_return_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend));

	backend->priv->mutex_lock = lock;
}

/**
 * e_cal_backend_sync_open:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @only_if_exists: Whether to open the calendar if and only if it already exists
 * or just create it when it does not exist.
 * @error: Out parameter for a #GError.
 *
 * Calls the open_sync method on the given backend.
 */
void
e_cal_backend_sync_open (ECalBackendSync *backend,
                         EDataCal *cal,
                         GCancellable *cancellable,
                         gboolean only_if_exists,
                         GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (open_sync, (backend, cal, cancellable, only_if_exists, error));
}

/**
 * e_cal_backend_sync_refresh:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @error: Out parameter for a #GError.
 *
 * Calls the refresh_sync method on the given backend.
 *
 * Since: 2.30
 */
void
e_cal_backend_sync_refresh (ECalBackendSync *backend,
                            EDataCal *cal,
                            GCancellable *cancellable,
                            GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->refresh_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (refresh_sync, (backend, cal, cancellable, error));
}

/**
 * e_cal_backend_sync_get_backend_property:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @prop_name: Property name whose value to retrieve.
 * @prop_value: Return value of the @prop_name.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_backend_property_sync method on the given backend.
 *
 * Returns whether processed this property. Returning FALSE means to pass
 * the call to the ECalBackend parent class, thus neither @error should be
 * set in this case.
 *
 * Since: 3.2
 **/
gboolean
e_cal_backend_sync_get_backend_property (ECalBackendSync *backend,
                                         EDataCal *cal,
                                         GCancellable *cancellable,
                                         const gchar *prop_name,
                                         gchar **prop_value,
                                         GError **error)
{
	gboolean res = FALSE;

	e_return_data_cal_error_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_val_if_fail (prop_name, InvalidArg);
	e_return_data_cal_error_val_if_fail (prop_value, InvalidArg);

	LOCK_WRAPPER_RET_VAL (get_backend_property_sync, (backend, cal, cancellable, prop_name, prop_value, error));

	return res;
}

/**
 * e_cal_backend_sync_set_backend_property:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @prop_name: Property name to set.
 * @prop_value: New value of the @prop_name.
 * @error: Out parameter for a #GError.
 *
 * Calls the set_backend_property_sync method on the given backend.
 *
 * Returns whether processed this property. Returning FALSE means to pass
 * the call to the ECalBackend parent class, thus neither @error should be
 * set in this case.
 *
 * Since: 3.2
 **/
gboolean
e_cal_backend_sync_set_backend_property (ECalBackendSync *backend,
                                         EDataCal *cal,
                                         GCancellable *cancellable,
                                         const gchar *prop_name,
                                         const gchar *prop_value,
                                         GError **error)
{
	gboolean res = FALSE;

	e_return_data_cal_error_val_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_val_if_fail (prop_name, InvalidArg);
	e_return_data_cal_error_val_if_fail (prop_value, InvalidArg);

	LOCK_WRAPPER_RET_VAL (set_backend_property_sync, (backend, cal, cancellable, prop_name, prop_value, error));

	return res;
}

/**
 * e_cal_backend_sync_get_object:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @uid: UID of the object to get.
 * @rid: Recurrence ID of the specific instance to get, or NULL if getting the
 * master object.
 * @calobj: Placeholder for returned object.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_object_sync method on the given backend.
 */
void
e_cal_backend_sync_get_object (ECalBackendSync *backend,
                               EDataCal *cal,
                               GCancellable *cancellable,
                               const gchar *uid,
                               const gchar *rid,
                               gchar **calobj,
                               GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (calobj, InvalidArg);

	LOCK_WRAPPER (get_object_sync, (backend, cal, cancellable, uid, rid, calobj, error));
}

/**
 * e_cal_backend_sync_get_object_list:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @sexp: Search query.
 * @calobjs: Placeholder for list of returned objects.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_object_list_sync method on the given backend.
 */
void
e_cal_backend_sync_get_object_list (ECalBackendSync *backend,
                                    EDataCal *cal,
                                    GCancellable *cancellable,
                                    const gchar *sexp,
                                    GSList **calobjs,
                                    GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (calobjs, InvalidArg);

	LOCK_WRAPPER (get_object_list_sync, (backend, cal, cancellable, sexp, calobjs, error));
}

/**
 * e_cal_backend_sync_get_free_busy:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @users: List of users to get F/B info from.
 * @start: Time range start.
 * @end: Time range end.
 * @freebusyobjects: Placeholder for F/B information.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_free_busy_sync method on the given backend.
 */
void
e_cal_backend_sync_get_free_busy (ECalBackendSync *backend,
                                  EDataCal *cal,
                                  GCancellable *cancellable,
                                  const GSList *users,
                                  time_t start,
                                  time_t end,
                                  GSList **freebusyobjects,
                                  GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (get_free_busy_sync, (backend, cal, cancellable, users, start, end, freebusyobjects, error));
}

/**
 * e_cal_backend_sync_create_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @calobjs: The objects to be added.
 * @uids: Placeholder for server-generated UIDs.
 * @new_components: (out) (transfer full): Placeholder for returned #ECalComponent objects.
 * @error: Out parameter for a #GError.
 *
 * Calls the create_objects_sync method on the given backend.
 *
 * Since: 3.6
 */
void
e_cal_backend_sync_create_objects (ECalBackendSync *backend,
                                   EDataCal *cal,
                                   GCancellable *cancellable,
                                   const GSList *calobjs,
                                   GSList **uids,
                                   GSList **new_components,
                                   GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->create_objects_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (create_objects_sync, (backend, cal, cancellable, calobjs, uids, new_components, error));
}

/**
 * e_cal_backend_sync_modify_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @calobjs: Objects to be modified.
 * @mod: Type of modification to be done.
 * @old_components: (out) (transfer full): Placeholder for returning the old components as they were stored on the
 * backend.
 * @new_components: (out) (transfer full): Placeholder for returning the new components as they have been stored
 * on the backend.
 * @error: Out parameter for a #GError.
 *
 * Calls the modify_objects_sync method on the given backend.
 *
 * Since: 3.6
 */
void
e_cal_backend_sync_modify_objects (ECalBackendSync *backend,
                                   EDataCal *cal,
                                   GCancellable *cancellable,
                                   const GSList *calobjs,
                                   CalObjModType mod,
                                   GSList **old_components,
                                   GSList **new_components,
                                   GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->modify_objects_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (modify_objects_sync, (backend, cal, cancellable, calobjs, mod, old_components, new_components, error));
}

/**
 * e_cal_backend_sync_remove_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @ids: List of #ECalComponentId objects identifying the objects to remove.
 * @mod: Type of removal.
 * @old_components: (out) (transfer full): Placeholder for returning the old components as they were stored on the
 * backend.
 * @new_components: (out) (transfer full): Placeholder for returning the new components as they have been stored
 * on the backend (when removing individual instances). If removing whole objects,
 * they will be set to %NULL.
 * @error: Out parameter for a #GError.
 *
 * Calls the remove_objects_sync method on the given backend.
 *
 * Since: 3.6
 */
void
e_cal_backend_sync_remove_objects (ECalBackendSync *backend,
                                   EDataCal *cal,
                                   GCancellable *cancellable,
                                   const GSList *ids,
                                   CalObjModType mod,
                                   GSList **old_components,
                                   GSList **new_components,
                                   GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->remove_objects_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (remove_objects_sync, (backend, cal, cancellable, ids, mod, old_components, new_components, error));
}

/**
 * e_cal_backend_sync_receive_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @calobj: iCalendar object to receive.
 * @error: Out parameter for a #GError.
 *
 * Calls the receive_objects_sync method on the given backend.
 */
void
e_cal_backend_sync_receive_objects (ECalBackendSync *backend,
                                    EDataCal *cal,
                                    GCancellable *cancellable,
                                    const gchar *calobj,
                                    GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->receive_objects_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (receive_objects_sync, (backend, cal, cancellable, calobj, error));
}

/**
 * e_cal_backend_sync_send_objects:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @calobj: The iCalendar object to send.
 * @users: List of users to send notifications to.
 * @modified_calobj: Placeholder for the iCalendar object after being modified.
 * @error: Out parameter for a #GError.
 *
 * Calls the send_objects_sync method on the given backend.
 */
void
e_cal_backend_sync_send_objects (ECalBackendSync *backend,
                                 EDataCal *cal,
                                 GCancellable *cancellable,
                                 const gchar *calobj,
                                 GSList **users,
                                 gchar **modified_calobj,
                                 GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->send_objects_sync != NULL, UnsupportedMethod);

	LOCK_WRAPPER (send_objects_sync, (backend, cal, cancellable, calobj, users, modified_calobj, error));
}

/**
 * e_cal_backend_sync_get_attachment_uris:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @uid: Unique id of the calendar object.
 * @rid: Recurrence id of the calendar object.
 * @attachments: Placeholder for list of returned attachment uris.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_attachment_uris_sync method on the given backend.
 *
 * Since: 3.2
 */
void
e_cal_backend_sync_get_attachment_uris (ECalBackendSync *backend,
                                        EDataCal *cal,
                                        GCancellable *cancellable,
                                        const gchar *uid,
                                        const gchar *rid,
                                        GSList **attachments,
                                        GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (attachments, InvalidArg);

	LOCK_WRAPPER (get_attachment_uris_sync, (backend, cal, cancellable, uid, rid, attachments, error));
}

/**
 * e_cal_backend_sync_discard_alarm:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @uid: Unique id of the calendar object.
 * @rid: Recurrence id of the calendar object.
 * @auid: Alarm ID to remove.
 * @error: Out parameter for a #GError.
 *
 * Calls the discard_alarm_sync method on the given backend.
 **/
void
e_cal_backend_sync_discard_alarm (ECalBackendSync *backend,
                                  EDataCal *cal,
                                  GCancellable *cancellable,
                                  const gchar *uid,
                                  const gchar *rid,
                                  const gchar *auid,
                                  GError **error)
{
	e_return_data_cal_error_if_fail (backend && E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);
	e_return_data_cal_error_if_fail (uid, InvalidArg);
	e_return_data_cal_error_if_fail (auid, InvalidArg);

	LOCK_WRAPPER (discard_alarm_sync, (backend, cal, cancellable, uid, rid, auid, error));
}

/**
 * e_cal_backend_sync_get_timezone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @tzid: ID of the timezone to retrieve.
 * @tzobject: Placeholder for the returned timezone.
 * @error: Out parameter for a #GError.
 *
 * Calls the get_timezone_sync method on the given backend.
 * This method is not mandatory on the backend, because here
 * is used internal_get_timezone call to fetch timezone from
 * it and that is transformed to a string. In other words,
 * any object deriving from ECalBackendSync can implement only
 * internal_get_timezone and can skip implementation of
 * get_timezone_sync completely.
 */
void
e_cal_backend_sync_get_timezone (ECalBackendSync *backend,
                                 EDataCal *cal,
                                 GCancellable *cancellable,
                                 const gchar *tzid,
                                 gchar **tzobject,
                                 GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	if (E_CAL_BACKEND_SYNC_GET_CLASS (backend)->get_timezone_sync) {
		LOCK_WRAPPER (get_timezone_sync, (backend, cal, cancellable, tzid, tzobject, error));
	}

	if (tzobject && !*tzobject) {
		icaltimezone *zone = NULL;

		if (backend->priv->mutex_lock)
			g_mutex_lock (&backend->priv->sync_mutex);
		zone = e_timezone_cache_get_timezone (
			E_TIMEZONE_CACHE (backend), tzid);
		if (backend->priv->mutex_lock)
			g_mutex_unlock (&backend->priv->sync_mutex);

		if (!zone) {
			g_propagate_error (error, e_data_cal_create_error (ObjectNotFound, NULL));
		} else {
			icalcomponent *icalcomp;

			icalcomp = icaltimezone_get_component (zone);

			if (!icalcomp) {
				g_propagate_error (error, e_data_cal_create_error (InvalidObject, NULL));
			} else {
				*tzobject = icalcomponent_as_ical_string_r (icalcomp);
			}
		}
	}
}

/**
 * e_cal_backend_sync_add_timezone:
 * @backend: An ECalBackendSync object.
 * @cal: An EDataCal object.
 * @cancellable: a #GCancellable for the operation
 * @tzobject: VTIMEZONE object to be added.
 * @error: Out parameter for a #GError.
 *
 * Calls the add_timezone_sync method on the given backend.
 */
void
e_cal_backend_sync_add_timezone (ECalBackendSync *backend,
                                 EDataCal *cal,
                                 GCancellable *cancellable,
                                 const gchar *tzobject,
                                 GError **error)
{
	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_SYNC (backend), InvalidArg);

	LOCK_WRAPPER (add_timezone_sync, (backend, cal, cancellable, tzobject, error));
}

static void
cal_backend_open (ECalBackend *backend,
                  EDataCal *cal,
                  guint32 opid,
                  GCancellable *cancellable,
                  gboolean only_if_exists)
{
	GError *error = NULL;

	e_cal_backend_sync_open (E_CAL_BACKEND_SYNC (backend), cal, cancellable, only_if_exists, &error);

	e_data_cal_respond_open (cal, opid, error);
}

static void
cal_backend_refresh (ECalBackend *backend,
                     EDataCal *cal,
                     guint32 opid,
                     GCancellable *cancellable)
{
	GError *error = NULL;

	e_cal_backend_sync_refresh (E_CAL_BACKEND_SYNC (backend), cal, cancellable, &error);

	e_data_cal_respond_refresh (cal, opid, error);
}

static void
cal_backend_get_backend_property (ECalBackend *backend,
                                  EDataCal *cal,
                                  guint32 opid,
                                  GCancellable *cancellable,
                                  const gchar *prop_name)
{
	GError *error = NULL;
	gchar *prop_value = NULL;

	if (e_cal_backend_sync_get_backend_property (E_CAL_BACKEND_SYNC (backend), cal, cancellable, prop_name, &prop_value, &error))
		e_data_cal_respond_get_backend_property (cal, opid, error, prop_value);
	else
		(* E_CAL_BACKEND_CLASS (e_cal_backend_sync_parent_class)->get_backend_property) (backend, cal, opid, cancellable, prop_name);

	g_free (prop_value);
}

static void
cal_backend_set_backend_property (ECalBackend *backend,
                                  EDataCal *cal,
                                  guint32 opid,
                                  GCancellable *cancellable,
                                  const gchar *prop_name,
                                  const gchar *prop_value)
{
	GError *error = NULL;

	if (e_cal_backend_sync_set_backend_property (E_CAL_BACKEND_SYNC (backend), cal, cancellable, prop_name, prop_value, &error))
		e_data_cal_respond_set_backend_property (cal, opid, error);
	else
		(* E_CAL_BACKEND_CLASS (e_cal_backend_sync_parent_class)->set_backend_property) (backend, cal, opid, cancellable, prop_name, prop_value);
}

static void
cal_backend_get_object (ECalBackend *backend,
                        EDataCal *cal,
                        guint32 opid,
                        GCancellable *cancellable,
                        const gchar *uid,
                        const gchar *rid)
{
	GError *error = NULL;
	gchar *calobj = NULL;

	e_cal_backend_sync_get_object (E_CAL_BACKEND_SYNC (backend), cal, cancellable, uid, rid, &calobj, &error);

	e_data_cal_respond_get_object (cal, opid, error, calobj);

	g_free (calobj);
}

static void
cal_backend_get_object_list (ECalBackend *backend,
                             EDataCal *cal,
                             guint32 opid,
                             GCancellable *cancellable,
                             const gchar *sexp)
{
	GError *error = NULL;
	GSList *calobjs = NULL;

	e_cal_backend_sync_get_object_list (E_CAL_BACKEND_SYNC (backend), cal, cancellable, sexp, &calobjs, &error);

	e_data_cal_respond_get_object_list (cal, opid, error, calobjs);

	g_slist_foreach (calobjs, (GFunc) g_free, NULL);
	g_slist_free (calobjs);
}

static void
cal_backend_get_free_busy (ECalBackend *backend,
                           EDataCal *cal,
                           guint32 opid,
                           GCancellable *cancellable,
                           const GSList *users,
                           time_t start,
                           time_t end)
{
	GError *error = NULL;
	GSList *freebusyobjs = NULL;

	e_cal_backend_sync_get_free_busy (E_CAL_BACKEND_SYNC (backend), cal, cancellable, users, start, end, &freebusyobjs, &error);

	if (freebusyobjs)
		e_data_cal_report_free_busy_data (cal, freebusyobjs);
	e_data_cal_respond_get_free_busy (cal, opid, error);

	g_slist_foreach (freebusyobjs, (GFunc) g_free, NULL);
	g_slist_free (freebusyobjs);
}

static GSList *
ecalcomponent_slist_from_strings (const GSList *strings)
{
	GSList *ecalcomps = NULL;
	const GSList *l;

	for (l = strings; l; l = l->next) {
		ECalComponent *component = e_cal_component_new_from_string (l->data);
		ecalcomps = g_slist_prepend (ecalcomps, component);
	}

	return g_slist_reverse (ecalcomps);
}

static void
cal_backend_create_objects (ECalBackend *backend,
                            EDataCal *cal,
                            guint32 opid,
                            GCancellable *cancellable,
                            const GSList *calobjs)
{
	GError *error = NULL;
	GSList *uids = NULL;
	GSList *new_components = NULL;

	e_cal_backend_sync_create_objects (E_CAL_BACKEND_SYNC (backend), cal, cancellable, calobjs, &uids, &new_components, &error);

	if (!new_components)
		new_components = ecalcomponent_slist_from_strings (calobjs);

	e_data_cal_respond_create_objects (cal, opid, error, uids, new_components);

	g_slist_free_full (uids, g_free);
	e_util_free_nullable_object_slist (new_components);
}

static void
cal_backend_modify_objects (ECalBackend *backend,
                            EDataCal *cal,
                            guint32 opid,
                            GCancellable *cancellable,
                            const GSList *calobjs,
                            CalObjModType mod)
{
	GError *error = NULL;
	GSList *old_components = NULL, *new_components = NULL;

	e_cal_backend_sync_modify_objects (E_CAL_BACKEND_SYNC (backend), cal, cancellable, calobjs, mod, &old_components, &new_components, &error);

	if (!old_components)
		old_components = ecalcomponent_slist_from_strings (calobjs);

	e_data_cal_respond_modify_objects (cal, opid, error, old_components, new_components);

	e_util_free_nullable_object_slist (old_components);
	e_util_free_nullable_object_slist (new_components);
}

static void
cal_backend_remove_objects (ECalBackend *backend,
                            EDataCal *cal,
                            guint32 opid,
                            GCancellable *cancellable,
                            const GSList *ids,
                            CalObjModType mod)
{
	GError *error = NULL;
	GSList *old_components = NULL, *new_components = NULL;

	e_cal_backend_sync_remove_objects (E_CAL_BACKEND_SYNC (backend), cal, cancellable, ids, mod, &old_components, &new_components, &error);

	e_data_cal_respond_remove_objects (cal, opid, error, ids, old_components, new_components);

	e_util_free_nullable_object_slist (old_components);
	e_util_free_nullable_object_slist (new_components);
}

static void
cal_backend_receive_objects (ECalBackend *backend,
                             EDataCal *cal,
                             guint32 opid,
                             GCancellable *cancellable,
                             const gchar *calobj)
{
	GError *error = NULL;

	e_cal_backend_sync_receive_objects (E_CAL_BACKEND_SYNC (backend), cal, cancellable, calobj, &error);

	e_data_cal_respond_receive_objects (cal, opid, error);
}

static void
cal_backend_send_objects (ECalBackend *backend,
                          EDataCal *cal,
                          guint32 opid,
                          GCancellable *cancellable,
                          const gchar *calobj)
{
	GError *error = NULL;
	GSList *users = NULL;
	gchar *modified_calobj = NULL;

	e_cal_backend_sync_send_objects (E_CAL_BACKEND_SYNC (backend), cal, cancellable, calobj, &users, &modified_calobj, &error);

	e_data_cal_respond_send_objects (cal, opid, error, users, modified_calobj ? modified_calobj : calobj);

	g_slist_foreach (users, (GFunc) g_free, NULL);
	g_slist_free (users);
	g_free (modified_calobj);
}

static void
cal_backend_get_attachment_uris (ECalBackend *backend,
                                 EDataCal *cal,
                                 guint32 opid,
                                 GCancellable *cancellable,
                                 const gchar *uid,
                                 const gchar *rid)
{
	GError *error = NULL;
	GSList *attachments = NULL;

	e_cal_backend_sync_get_attachment_uris (E_CAL_BACKEND_SYNC (backend), cal, cancellable, uid, rid, &attachments, &error);

	e_data_cal_respond_get_attachment_uris (cal, opid, error, attachments);

	g_slist_foreach (attachments, (GFunc) g_free, NULL);
	g_slist_free (attachments);
}

static void
cal_backend_discard_alarm (ECalBackend *backend,
                           EDataCal *cal,
                           guint32 opid,
                           GCancellable *cancellable,
                           const gchar *uid,
                           const gchar *rid,
                           const gchar *auid)
{
	GError *error = NULL;

	e_cal_backend_sync_discard_alarm (E_CAL_BACKEND_SYNC (backend), cal, cancellable, uid, rid, auid, &error);

	e_data_cal_respond_discard_alarm (cal, opid, error);
}

static void
cal_backend_get_timezone (ECalBackend *backend,
                          EDataCal *cal,
                          guint32 opid,
                          GCancellable *cancellable,
                          const gchar *tzid)
{
	GError *error = NULL;
	gchar *object = NULL;

	e_cal_backend_sync_get_timezone (E_CAL_BACKEND_SYNC (backend), cal, cancellable, tzid, &object, &error);

	if (!object && tzid) {
		/* fallback if tzid contains only the location of timezone */
		gint i, slashes = 0;

		for (i = 0; tzid[i]; i++) {
			if (tzid[i] == '/')
				slashes++;
		}

		if (slashes == 1) {
			icalcomponent *icalcomp = NULL, *free_comp = NULL;

			icaltimezone *zone = icaltimezone_get_builtin_timezone (tzid);
			if (!zone) {
				/* Try fetching the timezone from zone directory. There are some timezones like MST, US/Pacific etc. which do not appear in
				zone.tab, so they will not be available in the libical builtin timezone */
				icalcomp = free_comp = icaltzutil_fetch_timezone (tzid);
			}

			if (zone)
				icalcomp = icaltimezone_get_component (zone);

			if (icalcomp) {
				icalcomponent *clone = icalcomponent_new_clone (icalcomp);
				icalproperty *prop;

				prop = icalcomponent_get_first_property (clone, ICAL_TZID_PROPERTY);
				if (prop) {
					/* change tzid to our, because the component has the buildin tzid */
					icalproperty_set_tzid (prop, tzid);

					object = icalcomponent_as_ical_string_r (clone);
					g_clear_error (&error);
				}
				icalcomponent_free (clone);
			}

			if (free_comp)
				icalcomponent_free (free_comp);
		}

		/* also cache this timezone to backend */
		if (object)
			e_cal_backend_sync_add_timezone (E_CAL_BACKEND_SYNC (backend), cal, cancellable, object, NULL);
	}

	e_data_cal_respond_get_timezone	 (cal, opid, error, object);

	g_free (object);
}

static void
cal_backend_add_timezone (ECalBackend *backend,
                          EDataCal *cal,
                          guint32 opid,
                          GCancellable *cancellable,
                          const gchar *tzobject)
{
	GError *error = NULL;

	e_cal_backend_sync_add_timezone (E_CAL_BACKEND_SYNC (backend), cal, cancellable, tzobject, &error);

	e_data_cal_respond_add_timezone (cal, opid, error);
}

static gboolean
cal_backend_sync_get_backend_property (ECalBackendSync *backend,
                                       EDataCal *cal,
                                       GCancellable *cancellable,
                                       const gchar *prop_name,
                                       gchar **prop_value,
                                       GError **error)
{
	/* to indicate to pass to the ECalBackend parent class */
	return FALSE;
}

static gboolean
cal_backend_sync_set_backend_property (ECalBackendSync *backend,
                                       EDataCal *cal,
                                       GCancellable *cancellable,
                                       const gchar *prop_name,
                                       const gchar *prop_value,
                                       GError **error)
{
	/* to indicate to pass to the ECalBackend parent class */
	return FALSE;
}

static void
e_cal_backend_sync_finalize (GObject *object)
{
	ECalBackendSyncPrivate *priv;

	priv = E_CAL_BACKEND_SYNC_GET_PRIVATE (object);

	g_mutex_clear (&priv->sync_mutex);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_backend_sync_parent_class)->finalize (object);
}

static void
e_cal_backend_sync_class_init (ECalBackendSyncClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (ECalBackendSyncPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = e_cal_backend_sync_finalize;

	backend_class = E_CAL_BACKEND_CLASS (class);
	backend_class->open			= cal_backend_open;
	backend_class->refresh			= cal_backend_refresh;
	backend_class->get_backend_property	= cal_backend_get_backend_property;
	backend_class->set_backend_property	= cal_backend_set_backend_property;
	backend_class->get_object		= cal_backend_get_object;
	backend_class->get_object_list		= cal_backend_get_object_list;
	backend_class->get_free_busy		= cal_backend_get_free_busy;
	backend_class->create_objects		= cal_backend_create_objects;
	backend_class->modify_objects		= cal_backend_modify_objects;
	backend_class->remove_objects		= cal_backend_remove_objects;
	backend_class->receive_objects		= cal_backend_receive_objects;
	backend_class->send_objects		= cal_backend_send_objects;
	backend_class->get_attachment_uris	= cal_backend_get_attachment_uris;
	backend_class->discard_alarm		= cal_backend_discard_alarm;
	backend_class->get_timezone		= cal_backend_get_timezone;
	backend_class->add_timezone		= cal_backend_add_timezone;

	class->get_backend_property_sync	= cal_backend_sync_get_backend_property;
	class->set_backend_property_sync	= cal_backend_sync_set_backend_property;
}

static void
e_cal_backend_sync_init (ECalBackendSync *backend)
{
	backend->priv = E_CAL_BACKEND_SYNC_GET_PRIVATE (backend);
	g_mutex_init (&backend->priv->sync_mutex);
}

