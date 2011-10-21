/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-session.c : Abstract class for an email session */

/*
 * Authors:
 *  Dan Winship <danw@ximian.com>
 *  Jeffrey Stedfast <fejj@ximian.com>
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-debug.h"
#include "camel-file-utils.h"
#include "camel-folder.h"
#include "camel-marshal.h"
#include "camel-mime-message.h"
#include "camel-sasl.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-string-utils.h"
#include "camel-transport.h"
#include "camel-url.h"

#define CAMEL_SESSION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SESSION, CamelSessionPrivate))

#define JOB_PRIORITY G_PRIORITY_LOW

#define d(x)

typedef struct _AsyncContext AsyncContext;
typedef struct _JobData JobData;

struct _CamelSessionPrivate {
	GMutex *lock;		/* for locking everything basically */
	GMutex *thread_lock;	/* locking threads */

	gchar *user_data_dir;
	gchar *user_cache_dir;

	GHashTable *services;
	GHashTable *junk_headers;
	CamelJunkFilter *junk_filter;

	GMainContext *context;

	guint check_junk        : 1;
	guint network_available : 1;
	guint online            : 1;
};

struct _AsyncContext {
	CamelService *service;
	gchar *auth_mechanism;
};

struct _JobData {
	CamelSession *session;
	GCancellable *cancellable;
	CamelSessionCallback callback;
	gpointer user_data;
	GDestroyNotify notify;
};

enum {
	PROP_0,
	PROP_CHECK_JUNK,
	PROP_JUNK_FILTER,
	PROP_NETWORK_AVAILABLE,
	PROP_ONLINE,
	PROP_USER_DATA_DIR,
	PROP_USER_CACHE_DIR
};

enum {
	JOB_STARTED,
	JOB_FINISHED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (CamelSession, camel_session, CAMEL_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->service != NULL)
		g_object_unref (async_context->service);

	g_free (async_context->auth_mechanism);

	g_slice_free (AsyncContext, async_context);
}

static void
job_data_free (JobData *job_data)
{
	g_object_unref (job_data->session);
	g_object_unref (job_data->cancellable);

	if (job_data->notify != NULL)
		job_data->notify (job_data->user_data);

	g_slice_free (JobData, job_data);
}

static void
session_finish_job_cb (CamelSession *session,
                       GSimpleAsyncResult *simple)
{
	JobData *job_data;
	GError *error = NULL;

	g_simple_async_result_propagate_error (simple, &error);
	job_data = g_simple_async_result_get_op_res_gpointer (simple);

	g_signal_emit (
		job_data->session,
		signals[JOB_FINISHED], 0,
		job_data->cancellable, error);

	g_clear_error (&error);
}

static void
session_do_job_cb (GSimpleAsyncResult *simple,
                   CamelSession *session,
                   GCancellable *cancellable)
{
	JobData *job_data;
	GError *error = NULL;

	job_data = g_simple_async_result_get_op_res_gpointer (simple);

	job_data->callback (
		session, cancellable,
		job_data->user_data, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static gboolean
session_start_job_cb (JobData *job_data)
{
	GSimpleAsyncResult *simple;

	g_signal_emit (
		job_data->session,
		signals[JOB_STARTED], 0,
		job_data->cancellable);

	simple = g_simple_async_result_new (
		G_OBJECT (job_data->session),
		(GAsyncReadyCallback) session_finish_job_cb,
		NULL, camel_session_submit_job);

	g_simple_async_result_set_op_res_gpointer (
		simple, job_data, (GDestroyNotify) job_data_free);

	g_simple_async_result_run_in_thread (
		simple, (GSimpleAsyncThreadFunc)
		session_do_job_cb, JOB_PRIORITY,
		job_data->cancellable);

	g_object_unref (simple);

	return FALSE;
}

static void
session_set_user_data_dir (CamelSession *session,
                           const gchar *user_data_dir)
{
	g_return_if_fail (user_data_dir != NULL);
	g_return_if_fail (session->priv->user_data_dir == NULL);

	session->priv->user_data_dir = g_strdup (user_data_dir);
}

static void
session_set_user_cache_dir (CamelSession *session,
                            const gchar *user_cache_dir)
{
	g_return_if_fail (user_cache_dir != NULL);
	g_return_if_fail (session->priv->user_cache_dir == NULL);

	session->priv->user_cache_dir = g_strdup (user_cache_dir);
}

static void
session_set_property (GObject *object,
                      guint property_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHECK_JUNK:
			camel_session_set_check_junk (
				CAMEL_SESSION (object),
				g_value_get_boolean (value));
			return;

		case PROP_JUNK_FILTER:
			camel_session_set_junk_filter (
				CAMEL_SESSION (object),
				g_value_get_object (value));
			return;

		case PROP_NETWORK_AVAILABLE:
			camel_session_set_network_available (
				CAMEL_SESSION (object),
				g_value_get_boolean (value));
			return;

		case PROP_ONLINE:
			camel_session_set_online (
				CAMEL_SESSION (object),
				g_value_get_boolean (value));
			return;

		case PROP_USER_DATA_DIR:
			session_set_user_data_dir (
				CAMEL_SESSION (object),
				g_value_get_string (value));
			return;

		case PROP_USER_CACHE_DIR:
			session_set_user_cache_dir (
				CAMEL_SESSION (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
session_get_property (GObject *object,
                      guint property_id,
                      GValue *value,
                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CHECK_JUNK:
			g_value_set_boolean (
				value, camel_session_get_check_junk (
				CAMEL_SESSION (object)));
			return;

		case PROP_JUNK_FILTER:
			g_value_set_object (
				value, camel_session_get_junk_filter (
				CAMEL_SESSION (object)));
			return;

		case PROP_NETWORK_AVAILABLE:
			g_value_set_boolean (
				value, camel_session_get_network_available (
				CAMEL_SESSION (object)));
			return;

		case PROP_ONLINE:
			g_value_set_boolean (
				value, camel_session_get_online (
				CAMEL_SESSION (object)));
			return;

		case PROP_USER_DATA_DIR:
			g_value_set_string (
				value, camel_session_get_user_data_dir (
				CAMEL_SESSION (object)));
			return;

		case PROP_USER_CACHE_DIR:
			g_value_set_string (
				value, camel_session_get_user_cache_dir (
				CAMEL_SESSION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
session_dispose (GObject *object)
{
	CamelSessionPrivate *priv;

	priv = CAMEL_SESSION_GET_PRIVATE (object);

	g_hash_table_remove_all (priv->services);

	if (priv->junk_filter != NULL) {
		g_object_unref (priv->junk_filter);
		priv->junk_filter = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_session_parent_class)->dispose (object);
}

static void
session_finalize (GObject *object)
{
	CamelSessionPrivate *priv;

	priv = CAMEL_SESSION_GET_PRIVATE (object);

	g_free (priv->user_data_dir);
	g_free (priv->user_cache_dir);

	g_hash_table_destroy (priv->services);

	if (priv->context != NULL)
		g_main_context_unref (priv->context);

	g_mutex_free (priv->lock);
	g_mutex_free (priv->thread_lock);

	if (priv->junk_headers) {
		g_hash_table_remove_all (priv->junk_headers);
		g_hash_table_destroy (priv->junk_headers);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_session_parent_class)->finalize (object);
}

static CamelService *
session_add_service (CamelSession *session,
                     const gchar *uid,
                     const gchar *url_string,
                     CamelProviderType type,
                     GError **error)
{
	CamelURL *url;
	CamelService *service;
	CamelProvider *provider;
	GType service_type = G_TYPE_INVALID;

	service = camel_session_get_service (session, uid);
	if (CAMEL_IS_SERVICE (service))
		return service;

	url = camel_url_new (url_string, error);
	if (url == NULL)
		return NULL;

	/* Try to find a suitable CamelService subclass. */
	provider = camel_provider_get (url->protocol, error);
	if (provider != NULL)
		service_type = provider->object_types[type];

	if (service_type == G_TYPE_INVALID) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("No provider available for protocol '%s'"),
			url->protocol);
		camel_url_free (url);
		return NULL;
	}

	if (!g_type_is_a (service_type, CAMEL_TYPE_SERVICE)) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_INVALID,
			_("Invalid GType registered for protocol '%s'"),
			url->protocol);
		camel_url_free (url);
		return NULL;
	}

	/* If the provider does not use paths but the URL contains one,
	 * ignore it. */
	if (!CAMEL_PROVIDER_ALLOWS (provider, CAMEL_URL_PART_PATH))
		camel_url_set_path (url, NULL);

	service = g_initable_new (
		service_type, NULL, error,
		"provider", provider, "session",
		session, "uid", uid, "url", url, NULL);

	/* The hash table takes ownership of the new CamelService. */
	if (service != NULL) {
		camel_session_lock (session, CAMEL_SESSION_SESSION_LOCK);

		g_hash_table_insert (
			session->priv->services,
			g_strdup (uid), service);

		camel_session_unlock (session, CAMEL_SESSION_SESSION_LOCK);
	}

	camel_url_free (url);

	return service;
}

static gboolean
session_authenticate_sync (CamelSession *session,
                           CamelService *service,
                           const gchar *mechanism,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelServiceAuthType *authtype = NULL;
	CamelAuthenticationResult result;
	GError *local_error = NULL;

	/* XXX This authenticate_sync() implementation serves only as
	 *     a rough example and is not intended to be used as is.
	 *
	 *     Any CamelSession subclass should override this method
	 *     and implement a more complete authentication loop that
	 *     handles user prompts and password storage.
	 */

	g_warning (
		"The default CamelSession.authenticate_sync() "
		"method is not intended for production use.");

	/* If a SASL mechanism was given and we can't find
	 * a CamelServiceAuthType for it, fail immediately. */
	if (mechanism != NULL) {
		authtype = camel_sasl_authtype (mechanism);
		if (authtype == NULL) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("No support for %s authentication"),
				mechanism);
			return FALSE;
		}
	}

	/* If the SASL mechanism does not involve a user
	 * password, then it gets one shot to authenticate. */
	if (authtype != NULL && !authtype->need_password) {
		result = camel_service_authenticate_sync (
			service, mechanism, cancellable, error);
		if (result == CAMEL_AUTHENTICATION_REJECTED)
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
				_("%s authentication failed"), mechanism);
		return (result == CAMEL_AUTHENTICATION_ACCEPTED);
	}

	/* Some SASL mechanisms can attempt to authenticate without a
	 * user password being provided (e.g. single-sign-on credentials),
	 * but can fall back to a user password.  Handle that case next. */
	if (mechanism != NULL) {
		CamelProvider *provider;
		CamelSasl *sasl;
		const gchar *service_name;
		gboolean success = FALSE;

		provider = camel_service_get_provider (service);
		service_name = provider->protocol;

		/* XXX Would be nice if camel_sasl_try_empty_password_sync()
		 *     returned CamelAuthenticationResult so it's easier to
		 *     detect errors. */
		sasl = camel_sasl_new (service_name, mechanism, service);
		if (sasl != NULL) {
			success = camel_sasl_try_empty_password_sync (
				sasl, cancellable, &local_error);
			g_object_unref (sasl);
		}

		if (success)
			return TRUE;
	}

	/* Abort authentication if we got cancelled.
	 * Otherwise clear any errors and press on. */
	if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return FALSE;

	g_clear_error (&local_error);

retry:
	/* XXX This is where things get bogus.  In a real implementation you
	 *     would want to fetch a stored password or prompt the user here.
	 *     Password should be stashed using camel_service_set_password()
	 *     before calling camel_service_authenticate_sync(). */

	result = camel_service_authenticate_sync (
		service, mechanism, cancellable, error);

	if (result == CAMEL_AUTHENTICATION_REJECTED) {
		/* XXX Request a different password here. */
		goto retry;
	}

	if (result == CAMEL_AUTHENTICATION_ACCEPTED) {
		/* XXX Possibly store the password here using
		 *     GNOME Keyring or something equivalent. */
	}

	return (result == CAMEL_AUTHENTICATION_ACCEPTED);
}

static void
session_authenticate_thread (GSimpleAsyncResult *simple,
                             GObject *object,
                             GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_session_authenticate_sync (
		CAMEL_SESSION (object), async_context->service,
		async_context->auth_mechanism, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
session_authenticate (CamelSession *session,
                      CamelService *service,
                      const gchar *mechanism,
                      gint io_priority,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->service = g_object_ref (service);
	async_context->auth_mechanism = g_strdup (mechanism);

	simple = g_simple_async_result_new (
		G_OBJECT (session), callback, user_data, session_authenticate);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, session_authenticate_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
session_authenticate_finish (CamelSession *session,
                             GAsyncResult *result,
                             GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (session), session_authenticate), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
camel_session_class_init (CamelSessionClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelSessionPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = session_set_property;
	object_class->get_property = session_get_property;
	object_class->dispose = session_dispose;
	object_class->finalize = session_finalize;

	class->add_service = session_add_service;

	class->authenticate_sync = session_authenticate_sync;

	class->authenticate = session_authenticate;
	class->authenticate_finish = session_authenticate_finish;

	g_object_class_install_property (
		object_class,
		PROP_CHECK_JUNK,
		g_param_spec_boolean (
			"check-junk",
			"Check Junk",
			"Check incoming messages for junk",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_JUNK_FILTER,
		g_param_spec_object (
			"junk-filter",
			"Junk Filter",
			"Classifies messages as junk or not junk",
			CAMEL_TYPE_JUNK_FILTER,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_NETWORK_AVAILABLE,
		g_param_spec_boolean (
			"network-available",
			"Network Available",
			"Whether the network is available",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_ONLINE,
		g_param_spec_boolean (
			"online",
			"Online",
			"Whether the shell is online",
			TRUE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USER_DATA_DIR,
		g_param_spec_string (
			"user-data-dir",
			"User Data Directory",
			"User-specific base directory for mail data",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	g_object_class_install_property (
		object_class,
		PROP_USER_CACHE_DIR,
		g_param_spec_string (
			"user-cache-dir",
			"User Cache Directory",
			"User-specific base directory for mail cache",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));

	signals[JOB_STARTED] = g_signal_new (
		"job-started",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (CamelSessionClass, job_started),
		NULL, NULL,
		g_cclosure_marshal_VOID__OBJECT,
		G_TYPE_NONE, 1,
		G_TYPE_CANCELLABLE);

	signals[JOB_FINISHED] = g_signal_new (
		"job-finished",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_LAST,
		G_STRUCT_OFFSET (CamelSessionClass, job_finished),
		NULL, NULL,
		camel_marshal_VOID__OBJECT_POINTER,
		G_TYPE_NONE, 2,
		G_TYPE_CANCELLABLE,
		G_TYPE_POINTER);
}

static void
camel_session_init (CamelSession *session)
{
	GHashTable *services;

	services = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) g_object_unref);

	session->priv = CAMEL_SESSION_GET_PRIVATE (session);

	session->priv->lock = g_mutex_new ();
	session->priv->thread_lock = g_mutex_new ();
	session->priv->services = services;
	session->priv->junk_headers = NULL;

	session->priv->context = g_main_context_get_thread_default ();
	if (session->priv->context != NULL)
		g_main_context_ref (session->priv->context);
}

/**
 * camel_session_get_user_data_dir:
 * @session: a #CamelSession
 *
 * Returns the base directory under which to store user-specific mail data.
 *
 * Returns: the base directory for mail data
 *
 * Since: 3.2
 **/
const gchar *
camel_session_get_user_data_dir (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);

	return session->priv->user_data_dir;
}

/**
 * camel_session_get_user_cache_dir:
 * @session: a #CamelSession
 *
 * Returns the base directory under which to store user-specific mail cache.
 *
 * Returns: the base directory for mail cache
 *
 * Since: 3.4
 **/
const gchar *
camel_session_get_user_cache_dir (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);

	return session->priv->user_cache_dir;
}

/**
 * camel_session_add_service:
 * @session: a #CamelSession
 * @uid: a unique identifier string
 * @uri_string: a URI string describing the service
 * @type: the provider type (#CAMEL_PROVIDER_STORE or
 * #CAMEL_PROVIDER_TRANSPORT) to get, since some URLs may be able to
 * specify either type
 * @error: return location for a #GError, or %NULL
 *
 * Instantiates a new #CamelService for @session.  The @uid identifies the
 * service for future lookup.  The @uri_string describes which provider to
 * use, authentication details, provider-specific options, etc.  The @type
 * explicitly designates the service as a #CamelStore or #CamelTransport.
 *
 * If the given @uid has already been added, the existing #CamelService
 * with that @uid is returned regardless of whether it agrees with the
 * given @uri_string and @type.
 *
 * If the @uri_string is invalid or no #CamelProvider is available to
 * handle the @uri_string, the function sets @error and returns %NULL.
 *
 * Returns: (transfer none): a #CamelService instance, or %NULL
 *
 * Since: 3.2
 **/
CamelService *
camel_session_add_service (CamelSession *session,
                           const gchar *uid,
                           const gchar *uri_string,
                           CamelProviderType type,
                           GError **error)
{
	CamelSessionClass *class;
	CamelService *service;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (uid != NULL, NULL);
	g_return_val_if_fail (uri_string != NULL, NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->add_service != NULL, NULL);

	service = class->add_service (session, uid, uri_string, type, error);
	CAMEL_CHECK_GERROR (session, add_service, service != NULL, error);

	return service;
}

/**
 * camel_session_remove_service:
 * @session: a #CamelSession
 * @uid: a unique identifier for #CamelService to remove
 *
 * Removes previously added #CamelService by camel_session_add_service().
 * Internally stored #CamelService is unreffed, if found.
 *
 * Returns: %TRUE when service with given @uid was found and removed,
 *    %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
camel_session_remove_service (CamelSession *session,
                              const gchar *uid)
{
	gboolean removed;

	g_return_val_if_fail (session, FALSE);
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	camel_session_lock (session, CAMEL_SESSION_SESSION_LOCK);

	removed = g_hash_table_remove (session->priv->services, uid);

	camel_session_unlock (session, CAMEL_SESSION_SESSION_LOCK);

	return removed;
}

/**
 * camel_session_get_service:
 * @session: a #CamelSession
 * @uid: a unique identifier string
 *
 * Looks up a #CamelService by its unique identifier string.  The service
 * must have been previously added using camel_session_add_service().
 *
 * Returns: a #CamelService instance, or %NULL
 **/
CamelService *
camel_session_get_service (CamelSession *session,
                           const gchar *uid)
{
	CamelService *service;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	camel_session_lock (session, CAMEL_SESSION_SESSION_LOCK);

	service = g_hash_table_lookup (session->priv->services, uid);

	camel_session_unlock (session, CAMEL_SESSION_SESSION_LOCK);

	return service;
}

/**
 * camel_session_get_service_by_url:
 * @session: a #CamelSession
 * @url: a #CamelURL
 * @type: a #CamelProviderType
 *
 * Looks up a #CamelService by trying to match its #CamelURL against the
 * given @url and then checking that the object is of the desired @type.
 * The service must have been previously added using
 * camel_session_add_service().
 *
 * Note this function is significantly slower than camel_session_get_service().
 *
 * Returns: a #CamelService instance, or %NULL
 *
 * Since: 3.2
 **/
CamelService *
camel_session_get_service_by_url (CamelSession *session,
                                  CamelURL *url,
                                  CamelProviderType type)
{
	CamelService *match = NULL;
	GList *list, *iter;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (url != NULL, NULL);

	list = camel_session_list_services (session);

	for (iter = list; iter != NULL; iter = g_list_next (iter)) {
		CamelProvider *provider;
		CamelService *service;
		CamelURL *service_url;

		service = CAMEL_SERVICE (iter->data);
		provider = camel_service_get_provider (service);
		service_url = camel_service_get_camel_url (service);

		if (provider->url_equal == NULL)
			continue;

		if (!provider->url_equal (url, service_url))
			continue;

		switch (type) {
			case CAMEL_PROVIDER_STORE:
				if (CAMEL_IS_STORE (service))
					match = service;
				break;
			case CAMEL_PROVIDER_TRANSPORT:
				if (CAMEL_IS_TRANSPORT (service))
					match = service;
				break;
			default:
				g_warn_if_reached ();
				break;
		}

		if (match != NULL)
			break;
	}

	g_list_free (list);

	return match;
}

/**
 * camel_session_list_services:
 * @session: a #CamelSession
 *
 * Returns a list of all #CamelService objects previously added using
 * camel_session_add_service().  Free the returned list using g_list_free().
 *
 * Returns: an unsorted list of #CamelService objects
 *
 * Since: 3.2
 **/
GList *
camel_session_list_services (CamelSession *session)
{
	GList *list;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);

	camel_session_lock (session, CAMEL_SESSION_SESSION_LOCK);

	list = g_hash_table_get_values (session->priv->services);

	camel_session_unlock (session, CAMEL_SESSION_SESSION_LOCK);

	return list;
}

/**
 * camel_session_remove_services:
 * @session: a #CamelSession
 *
 * Removes all #CamelService instances added by camel_session_add_service().
 *
 * This can be useful during application shutdown to ensure all #CamelService
 * instances are freed properly, especially since #CamelSession instances are
 * prone to reference cycles.
 *
 * Since: 3.2
 **/
void
camel_session_remove_services (CamelSession *session)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	camel_session_lock (session, CAMEL_SESSION_SESSION_LOCK);

	g_hash_table_remove_all (session->priv->services);

	camel_session_unlock (session, CAMEL_SESSION_SESSION_LOCK);
}

/**
 * camel_session_get_password:
 * @session: a #CamelSession
 * @service: the #CamelService this query is being made by
 * @prompt: prompt to provide to user
 * @item: an identifier, unique within this service, for the information
 * @flags: %CAMEL_SESSION_PASSWORD_REPROMPT, the prompt should force a reprompt
 * %CAMEL_SESSION_PASSWORD_SECRET, whether the password is secret
 * %CAMEL_SESSION_PASSWORD_STATIC, the password is remembered externally
 * @error: return location for a #GError, or %NULL
 *
 * This function is used by a #CamelService to ask the application and
 * the user for a password or other authentication data.
 *
 * @service and @item together uniquely identify the piece of data the
 * caller is concerned with.
 *
 * @prompt is a question to ask the user (if the application doesn't
 * already have the answer cached). If %CAMEL_SESSION_PASSWORD_SECRET
 * is set, the user's input will not be echoed back.
 *
 * If %CAMEL_SESSION_PASSWORD_STATIC is set, it means the password returned
 * will be stored statically by the caller automatically, for the current
 * session.
 *
 * The authenticator should set @error to %G_IO_ERROR_CANCELLED if
 * the user did not provide the information. The caller must g_free()
 * the information returned when it is done with it.
 *
 * Returns: the authentication information or %NULL
 **/
gchar *
camel_session_get_password (CamelSession *session,
                            CamelService *service,
                            const gchar *prompt,
                            const gchar *item,
                            guint32 flags,
                            GError **error)
{
	CamelSessionClass *class;
	gchar *password;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (prompt != NULL, NULL);
	g_return_val_if_fail (item != NULL, NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->get_password != NULL, NULL);

	password = class->get_password (
		session, service, prompt, item, flags, error);
	CAMEL_CHECK_GERROR (session, get_password, password != NULL, error);

	return password;
}

/**
 * camel_session_forget_password:
 * @session: a #CamelSession
 * @service: the #CamelService rejecting the password
 * @item: an identifier, unique within this service, for the information
 * @error: return location for a #GError, or %NULL
 *
 * This function is used by a #CamelService to tell the application
 * that the authentication information it provided via
 * camel_session_get_password() was rejected by the service. If the
 * application was caching this information, it should stop,
 * and if the service asks for it again, it should ask the user.
 *
 * @service and @item identify the rejected authentication information,
 * as with camel_session_get_password().
 *
 * Returns: %TRUE on success, %FALSE on failure
 **/
gboolean
camel_session_forget_password (CamelSession *session,
                               CamelService *service,
                               const gchar *item,
                               GError **error)
{
	CamelSessionClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (item != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->forget_password, FALSE);

	success = class->forget_password (session, service, item, error);
	CAMEL_CHECK_GERROR (session, forget_password, success, error);

	return success;
}

/**
 * camel_session_alert_user:
 * @session: a #CamelSession
 * @type: the type of alert (info, warning, or error)
 * @prompt: the message for the user
 * @cancel: whether or not to provide a "Cancel" option in addition to
 * an "OK" option.
 *
 * Presents the given @prompt to the user, in the style indicated by
 * @type. If @cancel is %TRUE, the user will be able to accept or
 * cancel. Otherwise, the message is purely informational.
 *
 * Returns: %TRUE if the user accepts, %FALSE if they cancel.
 */
gboolean
camel_session_alert_user (CamelSession *session,
                          CamelSessionAlertType type,
                          const gchar *prompt,
                          gboolean cancel)
{
	CamelSessionClass *class;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (prompt != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->alert_user != NULL, FALSE);

	return class->alert_user (session, type, prompt, cancel);
}

/**
 * camel_session_lookup_addressbook:
 *
 * Since: 2.22
 **/
gboolean
camel_session_lookup_addressbook (CamelSession *session,
                                  const gchar *name)
{
	CamelSessionClass *class;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (name != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->lookup_addressbook != NULL, FALSE);

	return class->lookup_addressbook (session, name);
}

/**
 * camel_session_build_password_prompt:
 * @type: account type (e.g. "IMAP")
 * @user: user name for the account
 * @host: host name for the account
 *
 * Constructs a localized password prompt from @type, @user and @host,
 * suitable for passing to camel_session_get_password().  The resulting
 * string contains markup tags.  Use g_free() to free it.
 *
 * Returns: a newly-allocated password prompt string
 *
 * Since: 2.22
 **/
gchar *
camel_session_build_password_prompt (const gchar *type,
                                     const gchar *user,
                                     const gchar *host)
{
	gchar *user_markup;
	gchar *host_markup;
	gchar *prompt;

	g_return_val_if_fail (type != NULL, NULL);
	g_return_val_if_fail (user != NULL, NULL);
	g_return_val_if_fail (host != NULL, NULL);

	/* Add bold tags to the "user" and "host" strings.  We use
	 * separate strings here to avoid putting markup tags in the
	 * translatable string below. */
	user_markup = g_markup_printf_escaped ("<b>%s</b>", user);
	host_markup = g_markup_printf_escaped ("<b>%s</b>", host);

	/* Translators: The first argument is the account type
	 * (e.g. "IMAP"), the second is the user name, and the
	 * third is the host name. */
	prompt = g_strdup_printf (
		_("Please enter the %s password for %s on host %s."),
		type, user_markup, host_markup);

	g_free (user_markup);
	g_free (host_markup);

	return prompt;
}

/**
 * camel_session_get_online:
 * @session: a #CamelSession
 *
 * Returns: whether or not @session is online
 **/
gboolean
camel_session_get_online (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	return session->priv->online;
}

/**
 * camel_session_set_online:
 * @session: a #CamelSession
 * @online: whether or not the session should be online
 *
 * Sets the online status of @session to @online.
 **/
void
camel_session_set_online (CamelSession *session,
                          gboolean online)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	session->priv->online = online;

	g_object_notify (G_OBJECT (session), "online");
}

/**
 * camel_session_get_filter_driver:
 * @session: a #CamelSession
 * @type: the type of filter (eg, "incoming")
 * @error: return location for a #GError, or %NULL
 *
 * Returns: a filter driver, loaded with applicable rules
 **/
CamelFilterDriver *
camel_session_get_filter_driver (CamelSession *session,
                                 const gchar *type,
                                 GError **error)
{
	CamelSessionClass *class;
	CamelFilterDriver *driver;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);
	g_return_val_if_fail (type != NULL, NULL);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->get_filter_driver != NULL, NULL);

	driver = class->get_filter_driver (session, type, error);
	CAMEL_CHECK_GERROR (session, get_filter_driver, driver != NULL, error);

	return driver;
}

/**
 * camel_session_get_junk_filter:
 * @session: a #CamelSession
 *
 * Returns the #CamelJunkFilter instance used to classify messages as
 * junk or not junk during filtering.
 *
 * Note that #CamelJunkFilter itself is just an interface.  The application
 * must implement the interface and install a #CamelJunkFilter instance for
 * junk filtering to take place.
 *
 * Returns: a #CamelJunkFilter, or %NULL
 *
 * Since: 3.2
 **/
CamelJunkFilter *
camel_session_get_junk_filter (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);

	return session->priv->junk_filter;
}

/**
 * camel_session_set_junk_filter:
 * @session: a #CamelSession
 * @junk_filter: a #CamelJunkFilter, or %NULL
 *
 * Installs the #CamelJunkFilter instance used to classify messages as
 * junk or not junk during filtering.
 *
 * Note that #CamelJunkFilter itself is just an interface.  The application
 * must implement the interface and install a #CamelJunkFilter instance for
 * junk filtering to take place.
 *
 * Since: 3.2
 **/
void
camel_session_set_junk_filter (CamelSession *session,
                               CamelJunkFilter *junk_filter)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	if (junk_filter != NULL) {
		g_return_if_fail (CAMEL_IS_JUNK_FILTER (junk_filter));
		g_object_ref (junk_filter);
	}

	if (session->priv->junk_filter != NULL)
		g_object_unref (session->priv->junk_filter);

	session->priv->junk_filter = junk_filter;

	g_object_notify (G_OBJECT (session), "junk-filter");
}

/**
 * camel_session_submit_job:
 * @session: a #CamelSession
 * @callback: a #CamelSessionCallback
 * @user_data: user data passed to the callback
 * @notify: a #GDestroyNotify function
 *
 * This function provides a simple mechanism for providers to initiate
 * low-priority background jobs.  Jobs can be submitted from any thread,
 * but execution of the jobs is always as follows:
 *
 * 1) The #CamelSession:job-started signal is emitted from the thread
 *    in which @session was created.  This is typically the same thread
 *    that hosts the global default #GMainContext, or "main" thread.
 *
 * 2) The @callback function is invoked from a different thread where
 *    it's safe to call synchronous functions.
 *
 * 3) Once @callback has returned, the #CamelSesson:job-finished signal
 *    is emitted from the same thread as #CamelSession:job-started was
 *    emitted.
 *
 * 4) Finally if a @notify function was provided, it is invoked and
 *    passed @user_data so that @user_data can be freed.
 *
 * Since: 3.2
 **/
void
camel_session_submit_job (CamelSession *session,
                          CamelSessionCallback callback,
                          gpointer user_data,
                          GDestroyNotify notify)
{
	GSource *source;
	JobData *job_data;

	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (callback != NULL);

	job_data = g_slice_new0 (JobData);
	job_data->session = g_object_ref (session);
	job_data->cancellable = camel_operation_new ();
	job_data->callback = callback;
	job_data->user_data = user_data;
	job_data->notify = notify;

	source = g_idle_source_new ();
	g_source_set_priority (source, JOB_PRIORITY);
	g_source_set_callback (
		source, (GSourceFunc) session_start_job_cb,
		job_data, (GDestroyNotify) NULL);
	g_source_attach (source, job_data->session->priv->context);
	g_source_unref (source);
}

/**
 * camel_session_get_check_junk:
 * @session: a #CamelSession
 *
 * Do we have to check incoming messages to be junk?
 *
 * Returns: whether or not we are checking incoming messages for junk
 **/
gboolean
camel_session_get_check_junk (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	return session->priv->check_junk;
}

/**
 * camel_session_set_check_junk:
 * @session: a #CamelSession
 * @check_junk: whether to check incoming messages for junk
 *
 * Set check_junk flag, if set, incoming mail will be checked for being junk.
 **/
void
camel_session_set_check_junk (CamelSession *session,
                              gboolean check_junk)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	session->priv->check_junk = check_junk;

	g_object_notify (G_OBJECT (session), "check-junk");
}

/**
 * camel_session_get_network_available:
 * @session: a #CamelSession
 *
 * Since: 2.32
 **/
gboolean
camel_session_get_network_available (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);

	return session->priv->network_available;
}

/**
 * camel_session_set_network_available:
 * @session: a #CamelSession
 * @network_available: whether a network is available
 *
 * Since: 2.32
 **/
void
camel_session_set_network_available (CamelSession *session,
                                     gboolean network_available)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	session->priv->network_available = network_available;

	g_object_notify (G_OBJECT (session), "network-available");
}

/**
 * camel_session_set_junk_headers:
 *
 * Since: 2.22
 **/
void
camel_session_set_junk_headers (CamelSession *session,
                                const gchar **headers,
                                const gchar **values,
                                gint len)
{
	gint i;

	g_return_if_fail (CAMEL_IS_SESSION (session));

	if (session->priv->junk_headers) {
		g_hash_table_remove_all (session->priv->junk_headers);
		g_hash_table_destroy (session->priv->junk_headers);
	}

	session->priv->junk_headers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	for (i = 0; i < len; i++) {
		g_hash_table_insert (session->priv->junk_headers, g_strdup (headers[i]), g_strdup (values[i]));
	}
}

/**
 * camel_session_get_junk_headers:
 *
 * Since: 2.22
 **/
const GHashTable *
camel_session_get_junk_headers (CamelSession *session)
{
	g_return_val_if_fail (CAMEL_IS_SESSION (session), NULL);

	return session->priv->junk_headers;
}

/**
 * camel_session_forward_to:
 * Forwards message to some address(es) in a given type. The meaning of the forward_type defines session itself.
 * @session #CameSession.
 * @folder #CamelFolder where is @message located.
 * @message Message to forward.
 * @address Where forward to.
 * @ex Exception.
 *
 * Since: 2.26
 **/
gboolean
camel_session_forward_to (CamelSession *session,
                          CamelFolder *folder,
                          CamelMimeMessage *message,
                          const gchar *address,
                          GError **error)
{
	CamelSessionClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), FALSE);
	g_return_val_if_fail (address != NULL, FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->forward_to != NULL, FALSE);

	success = class->forward_to (session, folder, message, address, error);
	CAMEL_CHECK_GERROR (session, forward_to, success, error);

	return success;
}

/**
 * camel_session_lock:
 * @session: a #CamelSession
 * @lock: lock type to lock
 *
 * Locks @session's @lock. Unlock it with camel_session_unlock().
 *
 * Since: 2.32
 **/
void
camel_session_lock (CamelSession *session,
                    CamelSessionLock lock)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	switch (lock) {
		case CAMEL_SESSION_SESSION_LOCK:
			g_mutex_lock (session->priv->lock);
			break;
		case CAMEL_SESSION_THREAD_LOCK:
			g_mutex_lock (session->priv->thread_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_session_unlock:
 * @session: a #CamelSession
 * @lock: lock type to unlock
 *
 * Unlocks @session's @lock, previously locked with camel_session_lock().
 *
 * Since: 2.32
 **/
void
camel_session_unlock (CamelSession *session,
                      CamelSessionLock lock)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));

	switch (lock) {
		case CAMEL_SESSION_SESSION_LOCK:
			g_mutex_unlock (session->priv->lock);
			break;
		case CAMEL_SESSION_THREAD_LOCK:
			g_mutex_unlock (session->priv->thread_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_session_get_socks_proxy:
 * @session: A #CamelSession
 * @for_host: Host name to which the connection will be requested
 * @host_ret: Location to return the SOCKS proxy hostname
 * @port_ret: Location to return the SOCKS proxy port
 *
 * Queries the SOCKS proxy that is configured for a @session.  This will
 * put %NULL in @hosts_ret if there is no proxy configured or when
 * the @for_host is listed in proxy ignore list.
 *
 * Since: 2.32
 */
void
camel_session_get_socks_proxy (CamelSession *session,
                               const gchar *for_host,
                               gchar **host_ret,
                               gint *port_ret)
{
	CamelSessionClass *klass;

	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (for_host != NULL);
	g_return_if_fail (host_ret != NULL);
	g_return_if_fail (port_ret != NULL);

	klass = CAMEL_SESSION_GET_CLASS (session);
	g_return_if_fail (klass->get_socks_proxy != NULL);

	klass->get_socks_proxy (session, for_host, host_ret, port_ret);
}

/**
 * camel_session_authenticate_sync:
 * @session: a #CamelSession
 * @service: a #CamelService
 * @mechanism: a SASL mechanism name, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Authenticates @service, which may involve repeated calls to
 * camel_service_authenticate() or camel_service_authenticate_sync().
 * A #CamelSession subclass is largely responsible for implementing this,
 * and should handle things like user prompts and secure password storage.
 * These issues are out-of-scope for Camel.
 *
 * If an error occurs, or if authentication is aborted, the function sets
 * @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.4
 **/
gboolean
camel_session_authenticate_sync (CamelSession *session,
                                 CamelService *service,
                                 const gchar *mechanism,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelSessionClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (CAMEL_IS_SERVICE (service), FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->authenticate_sync != NULL, FALSE);

	success = class->authenticate_sync (
		session, service, mechanism, cancellable, error);
	CAMEL_CHECK_GERROR (session, authenticate_sync, success, error);

	return success;
}

/**
 * camel_session_authenticate:
 * @session: a #CamelSession
 * @service: a #CamelService
 * @mechanism: a SASL mechanism name, or %NULL
 * @io_priority: the I/O priority for the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously authenticates @service, which may involve repeated calls
 * to camel_service_authenticate() or camel_service_authenticate_sync().
 * A #CamelSession subclass is largely responsible for implementing this,
 * and should handle things like user prompts and secure password storage.
 * These issues are out-of-scope for Camel.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_session_authenticate_finish() to get the result of
 * the operation.
 *
 * Since: 3.4
 **/
void
camel_session_authenticate (CamelSession *session,
                            CamelService *service,
                            const gchar *mechanism,
                            gint io_priority,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	CamelSessionClass *class;

	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_if_fail (class->authenticate != NULL);

	class->authenticate (
		session, service, mechanism, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_session_authenticate_finish:
 * @session: a #CamelSession
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_session_authenticate().
 *
 * If an error occurred, or if authentication was aborted, the function
 * sets @error and returns %FALSE.
 *
 * Returns: %TRUE on success, %FALSE on failure
 *
 * Since: 3.4
 **/
gboolean
camel_session_authenticate_finish (CamelSession *session,
                                   GAsyncResult *result,
                                   GError **error)
{
	CamelSessionClass *class;

	g_return_val_if_fail (CAMEL_IS_SESSION (session), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_SESSION_GET_CLASS (session);
	g_return_val_if_fail (class->authenticate_finish != NULL, FALSE);

	return class->authenticate_finish (session, result, error);
}

