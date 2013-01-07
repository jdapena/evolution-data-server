/*
 * uoa-utils.c
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

#include "uoa-utils.h"

#include <config.h>
#include <glib/gi18n-lib.h>
#include <rest/rest-proxy.h>
#include <json-glib/json-glib.h>
#include <libsignon-glib/signon-glib.h>

/* XXX accounts-glib.h should include this */
#include <libaccounts-glib/ag-auth-data.h>

#define GOOGLE_USERINFO_URI \
	"https://www.googleapis.com/oauth2/v2/userinfo"

typedef struct _AsyncContext AsyncContext;

struct _AsyncContext {
	GCancellable *cancellable;
	gchar *user_identity;
	gchar *email_address;
};

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->cancellable != NULL)
		g_object_unref (async_context->cancellable);

	g_free (async_context->user_identity);
	g_free (async_context->email_address);

	g_slice_free (AsyncContext, async_context);
}

/****************************** Google Provider ******************************/

static void
e_ag_account_google_got_userinfo_cb (RestProxyCall *call,
                                     const GError *error,
                                     GObject *weak_object,
                                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	JsonParser *json_parser;
	JsonObject *json_object;
	JsonNode *json_node;
	const gchar *email;
	GError *local_error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (error != NULL) {
		g_simple_async_result_set_from_error (simple, error);
		goto exit;
	}

	/* This is shamelessly stolen from goagoogleprovider.c */

	if (rest_proxy_call_get_status_code (call) != 200) {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Expected status 200 when requesting guid, "
			"instead got status %d (%s)"),
			rest_proxy_call_get_status_code (call),
			rest_proxy_call_get_status_message (call));
		goto exit;
	}

	json_parser = json_parser_new ();
	json_parser_load_from_data (
		json_parser,
		rest_proxy_call_get_payload (call),
		rest_proxy_call_get_payload_length (call),
		&local_error);

	if (local_error != NULL) {
		g_prefix_error (
			&local_error,
			_("Error parsing response as JSON: "));
		g_simple_async_result_take_error (simple, local_error);
		g_object_unref (json_parser);
		goto exit;
	}

	json_node = json_parser_get_root (json_parser);
	json_object = json_node_get_object (json_node);
	email = json_object_get_string_member (json_object, "email");

	if (email != NULL) {
		async_context->user_identity = g_strdup (email);
		async_context->email_address = g_strdup (email);
	} else {
		g_simple_async_result_set_error (
			simple, G_IO_ERROR, G_IO_ERROR_FAILED,
			_("Didn't find email member in JSON data"));
	}

	g_object_unref (json_parser);

exit:
	g_simple_async_result_complete (simple);

	g_object_unref (simple);
}

static void
e_ag_account_google_session_process_cb (GObject *source_object,
                                        GAsyncResult *result,
                                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	GVariant *session_data;
	GError *error = NULL;

	simple = G_SIMPLE_ASYNC_RESULT (user_data);

	session_data = signon_auth_session_process_finish (
		SIGNON_AUTH_SESSION (source_object), result, &error);

	/* Sanity check. */
	g_return_if_fail (
		((session_data != NULL) && (error == NULL)) ||
		((session_data == NULL) && (error != NULL)));

	/* Use the access token to obtain the user's email address. */

	if (session_data != NULL) {
		RestProxy *proxy;
		RestProxyCall *call;
		gchar *access_token = NULL;

		g_variant_lookup (
			session_data, "AccessToken", "s", &access_token);

		g_variant_unref (session_data);

		proxy = rest_proxy_new (GOOGLE_USERINFO_URI, FALSE);
		call = rest_proxy_new_call (proxy);
		rest_proxy_call_set_method (call, "GET");

		/* XXX This should never be NULL, but if it is just let
		 *     the call fail and pick up the resulting GError. */
		if (access_token != NULL) {
			rest_proxy_call_add_param (
				call, "access_token", access_token);
			g_free (access_token);
		}

		/* XXX The 3rd argument is supposed to be a GObject
		 *     that RestProxyCall weakly references such that
		 *     its disposal cancels the call.  This obviously
		 *     predates GCancellable.  Too bizarre to bother. */
		rest_proxy_call_async (
			call, e_ag_account_google_got_userinfo_cb,
			NULL, g_object_ref (simple), &error);

		if (error != NULL) {
			/* Undo the reference added to the async call. */
			g_object_unref (simple);
		}

		g_object_unref (proxy);
		g_object_unref (call);
	}

	if (error != NULL) {
		g_simple_async_result_take_error (simple, error);
		g_simple_async_result_complete (simple);
	}

	g_object_unref (simple);
}

static void
e_ag_account_collect_google_userinfo (GSimpleAsyncResult *simple,
                                      AgAccount *ag_account,
                                      GCancellable *cancellable)
{
	AgAccountService *ag_account_service = NULL;
	SignonAuthSession *session;
	AgAuthData *ag_auth_data;
	GList *list;
	GError *error = NULL;

	/* First obtain an OAuth 2.0 access token. */

	list = ag_account_list_services_by_type (ag_account, "mail");
	if (list != NULL) {
		ag_account_service = ag_account_service_new (
			ag_account, (AgService *) list->data);
		ag_service_list_free (list);
	}

	g_return_if_fail (ag_account_service != NULL);

	ag_auth_data = ag_account_service_get_auth_data (ag_account_service);

	session = signon_auth_session_new (
		ag_auth_data_get_credentials_id (ag_auth_data),
		ag_auth_data_get_method (ag_auth_data), &error);

	/* Sanity check. */
	g_return_if_fail (
		((session != NULL) && (error == NULL)) ||
		((session == NULL) && (error != NULL)));

	if (session != NULL) {
		signon_auth_session_process_async (
			session,
			ag_auth_data_get_login_parameters (ag_auth_data, NULL),
			ag_auth_data_get_mechanism (ag_auth_data),
			cancellable,
			e_ag_account_google_session_process_cb,
			g_object_ref (simple));
	} else {
		g_simple_async_result_take_error (simple, error);
		g_simple_async_result_complete_in_idle (simple);
	}

	ag_auth_data_unref (ag_auth_data);

	g_object_unref (ag_account_service);
	g_object_unref (simple);
}

/************************ End Provider-Specific Code *************************/

void
e_ag_account_collect_userinfo (AgAccount *ag_account,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	const gchar *provider_name;

	g_return_if_fail (AG_IS_ACCOUNT (ag_account));

	async_context = g_slice_new0 (AsyncContext);

	if (G_IS_CANCELLABLE (cancellable))
		async_context->cancellable = g_object_ref (cancellable);

	simple = g_simple_async_result_new (
		G_OBJECT (ag_account), callback,
		user_data, e_ag_account_collect_userinfo);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	/* XXX This has to be done differently for each provider. */

	provider_name = ag_account_get_provider_name (ag_account);

	if (g_strcmp0 (provider_name, "google") == 0) {
		e_ag_account_collect_google_userinfo (
			g_object_ref (simple), ag_account, cancellable);
	} else {
		g_warn_if_reached ();
		g_simple_async_result_complete_in_idle (simple);
	}

	g_object_unref (simple);
}

gboolean
e_ag_account_collect_userinfo_finish (AgAccount *ag_account,
                                      GAsyncResult *result,
                                      gchar **out_user_identity,
                                      gchar **out_email_address,
                                      GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (ag_account),
		e_ag_account_collect_userinfo), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return FALSE;

	/* The result strings may be NULL without an error. */

	if (out_user_identity != NULL) {
		*out_user_identity = async_context->user_identity;
		async_context->user_identity = NULL;
	}

	if (out_email_address != NULL) {
		*out_email_address = async_context->email_address;
		async_context->email_address = NULL;
	}

	return TRUE;
}

