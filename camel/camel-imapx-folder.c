/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.c : class for a imap folder */
/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <glib/gi18n-lib.h>

#include "camel-imapx-folder.h"
#include "camel-imapx-search.h"
#include "camel-imapx-server.h"
#include "camel-imapx-store.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-utils.h"

#include <stdlib.h>
#include <string.h>

#define d(...) camel_imapx_debug(debug, '?', __VA_ARGS__)

#define CAMEL_IMAPX_FOLDER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_FOLDER, CamelIMAPXFolderPrivate))

struct _CamelIMAPXFolderPrivate {
	GMutex property_lock;
	gchar **quota_root_names;

	GMutex move_to_hash_table_lock;
	GHashTable *move_to_real_junk_uids;
	GHashTable *move_to_real_trash_uids;
};

/* The custom property ID is a CamelArg artifact.
 * It still identifies the property in state files. */
enum {
	PROP_0,
	PROP_QUOTA_ROOT_NAMES,
	PROP_APPLY_FILTERS = 0x2501
};

G_DEFINE_TYPE (CamelIMAPXFolder, camel_imapx_folder, CAMEL_TYPE_OFFLINE_FOLDER)

static gboolean imapx_folder_get_apply_filters (CamelIMAPXFolder *folder);

static void
imapx_folder_claim_move_to_real_junk_uids (CamelIMAPXFolder *folder,
                                           GPtrArray *out_uids_to_copy)
{
	GList *keys;

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	keys = g_hash_table_get_keys (folder->priv->move_to_real_junk_uids);
	g_hash_table_steal_all (folder->priv->move_to_real_junk_uids);

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);

	while (keys != NULL) {
		g_ptr_array_add (out_uids_to_copy, keys->data);
		keys = g_list_delete_link (keys, keys);
	}
}

static void
imapx_folder_claim_move_to_real_trash_uids (CamelIMAPXFolder *folder,
                                            GPtrArray *out_uids_to_copy)
{
	GList *keys;

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	keys = g_hash_table_get_keys (folder->priv->move_to_real_trash_uids);
	g_hash_table_steal_all (folder->priv->move_to_real_trash_uids);

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);

	while (keys != NULL) {
		g_ptr_array_add (out_uids_to_copy, keys->data);
		keys = g_list_delete_link (keys, keys);
	}
}

static gboolean
imapx_folder_get_apply_filters (CamelIMAPXFolder *folder)
{
	g_return_val_if_fail (folder != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), FALSE);

	return folder->apply_filters;
}

static void
imapx_folder_set_apply_filters (CamelIMAPXFolder *folder,
                                gboolean apply_filters)
{
	g_return_if_fail (folder != NULL);
	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));

	if (folder->apply_filters == apply_filters)
		return;

	folder->apply_filters = apply_filters;

	g_object_notify (G_OBJECT (folder), "apply-filters");
}

static void
imapx_folder_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_APPLY_FILTERS:
			imapx_folder_set_apply_filters (
				CAMEL_IMAPX_FOLDER (object),
				g_value_get_boolean (value));
			return;

		case PROP_QUOTA_ROOT_NAMES:
			camel_imapx_folder_set_quota_root_names (
				CAMEL_IMAPX_FOLDER (object),
				g_value_get_boxed (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_folder_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_APPLY_FILTERS:
			g_value_set_boolean (
				value,
				imapx_folder_get_apply_filters (
				CAMEL_IMAPX_FOLDER (object)));
			return;

		case PROP_QUOTA_ROOT_NAMES:
			g_value_take_boxed (
				value,
				camel_imapx_folder_dup_quota_root_names (
				CAMEL_IMAPX_FOLDER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_folder_dispose (GObject *object)
{
	CamelIMAPXFolder *folder = CAMEL_IMAPX_FOLDER (object);
	CamelStore *parent_store;

	if (folder->cache != NULL) {
		g_object_unref (folder->cache);
		folder->cache = NULL;
	}

	if (folder->search != NULL) {
		g_object_unref (folder->search);
		folder->search = NULL;
	}

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (folder));
	if (parent_store) {
		camel_store_summary_disconnect_folder_summary (
			(CamelStoreSummary *) ((CamelIMAPXStore *) parent_store)->summary,
			CAMEL_FOLDER (folder)->summary);
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_folder_parent_class)->dispose (object);
}

static void
imapx_folder_finalize (GObject *object)
{
	CamelIMAPXFolder *folder = CAMEL_IMAPX_FOLDER (object);

	if (folder->ignore_recent != NULL)
		g_hash_table_unref (folder->ignore_recent);

	g_mutex_clear (&folder->search_lock);
	g_mutex_clear (&folder->stream_lock);

	g_mutex_clear (&folder->priv->property_lock);
	g_strfreev (folder->priv->quota_root_names);

	g_mutex_clear (&folder->priv->move_to_hash_table_lock);
	g_hash_table_destroy (folder->priv->move_to_real_junk_uids);
	g_hash_table_destroy (folder->priv->move_to_real_trash_uids);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_folder_parent_class)->finalize (object);
}

/* Algorithm for selecting a folder:
 *
 *  - If uidvalidity == old uidvalidity
 *    and exsists == old exists
 *    and recent == old recent
 *    and unseen == old unseen
 *    Assume our summary is correct
 *  for each summary item
 *    mark the summary item as 'old/not updated'
 *  rof
 *  fetch flags from 1:*
 *  for each fetch response
 *    info = summary[index]
 *    if info.uid != uid
 *      info = summary_by_uid[uid]
 *    fi
 *    if info == NULL
 *      create new info @ index
 *    fi
 *    if got.flags
 *      update flags
 *    fi
 *    if got.header
 *      update based on header
 *      mark as retrieved
 *    else if got.body
 *      update based on imap body
 *      mark as retrieved
 *    fi
 *
 *  Async fetch response:
 *    info = summary[index]
 *    if info == null
 *       if uid == null
 *          force resync/select?
 *       info = empty @ index
 *    else if uid && info.uid != uid
 *       force a resync?
 *       return
 *    fi
 *
 *    if got.flags {
 *      info.flags = flags
 *    }
 *    if got.header {
 *      info.init (header)
 *      info.empty = false
 *    }
 *
 * info.state - 2 bit field in flags
 *   0 = empty, nothing set
 *   1 = uid & flags set
 *   2 = update required
 *   3 = up to date
 */

static void
imapx_search_free (CamelFolder *folder,
                   GPtrArray *uids)
{
	CamelIMAPXFolder *ifolder = CAMEL_IMAPX_FOLDER (folder);

	g_return_if_fail (ifolder->search);

	g_mutex_lock (&ifolder->search_lock);

	camel_folder_search_free_result (ifolder->search, uids);

	g_mutex_unlock (&ifolder->search_lock);
}

static GPtrArray *
imapx_search_by_uids (CamelFolder *folder,
                      const gchar *expression,
                      GPtrArray *uids,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelIMAPXFolder *ifolder;
	CamelIMAPXSearch *isearch;
	CamelIMAPXServer *server = NULL;
	CamelStore *parent_store;
	GPtrArray *matches;
	const gchar *folder_name;
	gboolean online;

	if (uids->len == 0)
		return g_ptr_array_new ();

	ifolder = CAMEL_IMAPX_FOLDER (folder);
	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	online = camel_offline_store_get_online (
		CAMEL_OFFLINE_STORE (parent_store));

	if (online) {
		server = camel_imapx_store_get_server (
			CAMEL_IMAPX_STORE (parent_store),
			folder_name, cancellable, error);
		if (server == NULL)
			return NULL;
	}

	g_mutex_lock (&ifolder->search_lock);

	isearch = CAMEL_IMAPX_SEARCH (ifolder->search);
	camel_imapx_search_set_server (isearch, server);

	camel_folder_search_set_folder (ifolder->search, folder);

	matches = camel_folder_search_search (
		ifolder->search, expression, uids, cancellable, error);

	camel_imapx_search_set_server (isearch, NULL);

	g_mutex_unlock (&ifolder->search_lock);

	if (server != NULL)
		g_object_unref (server);

	return matches;
}

static guint32
imapx_count_by_expression (CamelFolder *folder,
                           const gchar *expression,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelIMAPXFolder *ifolder;
	CamelIMAPXSearch *isearch;
	CamelIMAPXServer *server = NULL;
	CamelStore *parent_store;
	const gchar *folder_name;
	gboolean online;
	guint32 matches;

	ifolder = CAMEL_IMAPX_FOLDER (folder);
	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	online = camel_offline_store_get_online (
		CAMEL_OFFLINE_STORE (parent_store));

	if (online) {
		server = camel_imapx_store_get_server (
			CAMEL_IMAPX_STORE (parent_store),
			folder_name, cancellable, error);
		if (server == NULL)
			return 0;
	}

	g_mutex_lock (&ifolder->search_lock);

	isearch = CAMEL_IMAPX_SEARCH (ifolder->search);
	camel_imapx_search_set_server (isearch, server);

	camel_folder_search_set_folder (ifolder->search, folder);

	matches = camel_folder_search_count (
		ifolder->search, expression, cancellable, error);

	camel_imapx_search_set_server (isearch, NULL);

	g_mutex_unlock (&ifolder->search_lock);

	if (server != NULL)
		g_object_unref (server);

	return matches;
}

static GPtrArray *
imapx_search_by_expression (CamelFolder *folder,
                            const gchar *expression,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelIMAPXFolder *ifolder;
	CamelIMAPXSearch *isearch;
	CamelIMAPXServer *server = NULL;
	CamelStore *parent_store;
	GPtrArray *matches;
	const gchar *folder_name;
	gboolean online;

	ifolder = CAMEL_IMAPX_FOLDER (folder);
	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	online = camel_offline_store_get_online (
		CAMEL_OFFLINE_STORE (parent_store));

	if (online) {
		server = camel_imapx_store_get_server (
			CAMEL_IMAPX_STORE (parent_store),
			folder_name, cancellable, error);
		if (server == NULL)
			return NULL;
	}

	g_mutex_lock (&ifolder->search_lock);

	isearch = CAMEL_IMAPX_SEARCH (ifolder->search);
	camel_imapx_search_set_server (isearch, server);

	camel_folder_search_set_folder (ifolder->search, folder);

	matches = camel_folder_search_search (
		ifolder->search, expression, NULL, cancellable, error);

	camel_imapx_search_set_server (isearch, NULL);

	g_mutex_unlock (&ifolder->search_lock);

	if (server != NULL)
		g_object_unref (server);

	return matches;
}

static gchar *
imapx_get_filename (CamelFolder *folder,
                    const gchar *uid,
                    GError **error)
{
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;

	return camel_data_cache_get_filename (ifolder->cache, "cache", uid);
}

static gboolean
imapx_append_message_sync (CamelFolder *folder,
                           CamelMimeMessage *message,
                           CamelMessageInfo *info,
                           gchar **appended_uid,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	gboolean success = FALSE;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (appended_uid)
		*appended_uid = NULL;

	server = camel_imapx_store_get_server (istore, NULL, cancellable, error);
	if (server) {
		success = camel_imapx_server_append_message (
			server, folder, message, info, appended_uid, cancellable, error);
		g_object_unref (server);
	}

	return success;
}

static gboolean
imapx_expunge_sync (CamelFolder *folder,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	const gchar *folder_name;
	gboolean success = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server (
		istore, folder_name, cancellable, error);
	if (server != NULL) {
		success = camel_imapx_server_expunge (
			server, folder, cancellable, error);
		camel_imapx_store_op_done (istore, server, folder_name);
		g_object_unref (server);
	}

	return success;
}

static gboolean
imapx_fetch_messages_sync (CamelFolder *folder,
                           CamelFetchType type,
                           gint limit,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelService *service;
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	const gchar *folder_name;
	gboolean success = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);
	service = CAMEL_SERVICE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (!camel_service_connect_sync (service, cancellable, error))
		return FALSE;

	server = camel_imapx_store_get_server (
		istore, folder_name, cancellable, error);
	if (server != NULL) {
		success = camel_imapx_server_fetch_messages (
			server, folder, type, limit, cancellable, error);
		camel_imapx_store_op_done (istore, server, folder_name);
		g_object_unref (server);
	}

	return success;
}

static CamelMimeMessage *
imapx_get_message_sync (CamelFolder *folder,
                        const gchar *uid,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelMimeMessage *msg = NULL;
	CamelStream *stream = NULL;
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXFolder *ifolder = (CamelIMAPXFolder *) folder;
	CamelIMAPXServer *server;
	const gchar *folder_name;
	const gchar *path = NULL;
	gboolean offline_message = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (!strchr (uid, '-'))
		path = "cur";
	else {
		path = "new";
		offline_message = TRUE;
	}

	stream = camel_data_cache_get (ifolder->cache, path, uid, NULL);
	if (stream == NULL) {
		if (offline_message) {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_UID,
				"Offline message vanished from disk: %s", uid);
			return NULL;
		}

		if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("You must be working online to complete this operation"));
			return NULL;
		}

		server = camel_imapx_store_get_server (
			istore, folder_name, cancellable, error);
		if (server == NULL)
			return NULL;

		stream = camel_imapx_server_get_message (
			server, folder, uid, cancellable, error);
		camel_imapx_store_op_done (istore, server, folder_name);
		g_object_unref (server);
	}

	if (stream != NULL) {
		gboolean success;

		msg = camel_mime_message_new ();

		g_mutex_lock (&ifolder->stream_lock);
		success = camel_data_wrapper_construct_from_stream_sync (
			CAMEL_DATA_WRAPPER (msg), stream, cancellable, error);
		if (!success) {
			g_object_unref (msg);
			msg = NULL;
		}
		g_mutex_unlock (&ifolder->stream_lock);
		g_object_unref (stream);
	}

	if (msg != NULL) {
		CamelMessageInfo *mi;

		mi = camel_folder_summary_get (folder->summary, uid);
		if (mi != NULL) {
			CamelMessageFlags flags;
			gboolean has_attachment;

			flags = camel_message_info_flags (mi);
			has_attachment = camel_mime_message_has_attachment (msg);
			if (((flags & CAMEL_MESSAGE_ATTACHMENTS) && !has_attachment) ||
			    ((flags & CAMEL_MESSAGE_ATTACHMENTS) == 0 && has_attachment)) {
				camel_message_info_set_flags (
					mi, CAMEL_MESSAGE_ATTACHMENTS,
					has_attachment ? CAMEL_MESSAGE_ATTACHMENTS : 0);
			}

			camel_message_info_free (mi);
		}
	}

	return msg;
}

static CamelFolderQuotaInfo *
imapx_get_quota_info_sync (CamelFolder *folder,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXServer *server;
	CamelFolderQuotaInfo *quota_info = NULL;
	const gchar *folder_name;
	gchar **quota_root_names;
	gboolean success = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);

	server = camel_imapx_store_get_server (
		CAMEL_IMAPX_STORE (parent_store),
		folder_name, cancellable, error);

	if (server != NULL) {
		success = camel_imapx_server_update_quota_info (
			server, folder_name, cancellable, error);
		g_object_unref (server);
	}

	if (!success)
		return NULL;

	quota_root_names = camel_imapx_folder_dup_quota_root_names (
		CAMEL_IMAPX_FOLDER (folder));

	/* XXX Just return info for the first quota root name, I guess. */
	if (quota_root_names != NULL && quota_root_names[0] != NULL)
		quota_info = camel_imapx_store_dup_quota_info (
			CAMEL_IMAPX_STORE (parent_store),
			quota_root_names[0]);

	g_strfreev (quota_root_names);

	if (quota_info == NULL)
		g_set_error (
			error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
			_("No quota information available for folder '%s'"),
			folder_name);

	return quota_info;
}

static gboolean
imapx_purge_message_cache_sync (CamelFolder *folder,
                                gchar *start_uid,
                                gchar *end_uid,
                                GCancellable *cancellable,
                                GError **error)
{
	/* Not Implemented for now. */
	return TRUE;
}

static gboolean
imapx_refresh_info_sync (CamelFolder *folder,
                         GCancellable *cancellable,
                         GError **error)
{
	CamelService *service;
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	const gchar *folder_name;
	gboolean success = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);
	service = CAMEL_SERVICE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	if (!camel_service_connect_sync (service, cancellable, error))
		return FALSE;

	server = camel_imapx_store_get_server (
		istore, folder_name, cancellable, error);
	if (server != NULL) {
		success = camel_imapx_server_refresh_info (
			server, folder, cancellable, error);
		camel_imapx_store_op_done (istore, server, folder_name);
		g_object_unref (server);
	}

	return success;
}

/* Helper for imapx_synchronize_sync() */
static gboolean
imapx_move_to_real_junk (CamelIMAPXServer *server,
                         CamelFolder *folder,
                         GCancellable *cancellable,
                         gboolean *out_need_to_expunge,
                         GError **error)
{
	CamelIMAPXSettings *settings;
	GPtrArray *uids_to_copy;
	gchar *real_junk_path = NULL;
	gboolean success = TRUE;

	*out_need_to_expunge = FALSE;

	uids_to_copy = g_ptr_array_new_with_free_func (
		(GDestroyNotify) camel_pstring_free);

	settings = camel_imapx_server_ref_settings (server);
	if (camel_imapx_settings_get_use_real_junk_path (settings)) {
		real_junk_path =
			camel_imapx_settings_dup_real_junk_path (settings);
		imapx_folder_claim_move_to_real_junk_uids (
			CAMEL_IMAPX_FOLDER (folder), uids_to_copy);
	}
	g_object_unref (settings);

	if (uids_to_copy->len > 0) {
		CamelFolder *destination = NULL;
		CamelIMAPXStore *store;

		store = camel_imapx_server_ref_store (server);

		if (real_junk_path != NULL) {
			destination = camel_store_get_folder_sync (
				CAMEL_STORE (store),
				real_junk_path, 0,
				cancellable, error);
		} else {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_PATH,
				_("No destination folder specified"));
		}

		/* Avoid duplicating messages in the Junk folder. */
		if (destination == folder) {
			success = TRUE;
		} else if (destination != NULL) {
			success = camel_imapx_server_copy_message (
				server, folder, destination,
				uids_to_copy, TRUE,
				cancellable, error);
			*out_need_to_expunge = success;
		} else {
			success = FALSE;
		}

		if (!success) {
			g_prefix_error (
				error, "%s: ",
				_("Unable to move junk messages"));
		}

		g_object_unref (store);
	}

	g_ptr_array_unref (uids_to_copy);
	g_free (real_junk_path);

	return success;
}

/* Helper for imapx_synchronize_sync() */
static gboolean
imapx_move_to_real_trash (CamelIMAPXServer *server,
                          CamelFolder *folder,
                          GCancellable *cancellable,
                          gboolean *out_need_to_expunge,
                          GError **error)
{
	CamelIMAPXSettings *settings;
	GPtrArray *uids_to_copy;
	gchar *real_trash_path = NULL;
	gboolean success = TRUE;

	*out_need_to_expunge = FALSE;

	uids_to_copy = g_ptr_array_new_with_free_func (
		(GDestroyNotify) camel_pstring_free);

	settings = camel_imapx_server_ref_settings (server);
	if (camel_imapx_settings_get_use_real_trash_path (settings)) {
		real_trash_path =
			camel_imapx_settings_dup_real_trash_path (settings);
		imapx_folder_claim_move_to_real_trash_uids (
			CAMEL_IMAPX_FOLDER (folder), uids_to_copy);
	}
	g_object_unref (settings);

	if (uids_to_copy->len > 0) {
		CamelFolder *destination = NULL;
		CamelIMAPXStore *store;

		store = camel_imapx_server_ref_store (server);

		if (real_trash_path != NULL) {
			destination = camel_store_get_folder_sync (
				CAMEL_STORE (store),
				real_trash_path, 0,
				cancellable, error);
		} else {
			g_set_error (
				error, CAMEL_FOLDER_ERROR,
				CAMEL_FOLDER_ERROR_INVALID_PATH,
				_("No destination folder specified"));
		}

		/* Avoid duplicating messages in the Trash folder. */
		if (destination == folder) {
			success = TRUE;
		} else if (destination != NULL) {
			success = camel_imapx_server_copy_message (
				server, folder, destination,
				uids_to_copy, TRUE,
				cancellable, error);
			*out_need_to_expunge = success;
		} else {
			success = FALSE;
		}

		if (!success) {
			g_prefix_error (
				error, "%s: ",
				_("Unable to move deleted messages"));
		}

		g_object_unref (store);
	}

	g_ptr_array_unref (uids_to_copy);
	g_free (real_trash_path);

	return success;
}

static gboolean
imapx_synchronize_sync (CamelFolder *folder,
                        gboolean expunge,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	const gchar *folder_name;
	gboolean success = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server (
		istore, folder_name, cancellable, error);
	if (server != NULL) {
		gboolean need_to_expunge;

		success = camel_imapx_server_sync_changes (
			server, folder, cancellable, error);

		if (success) {
			success = imapx_move_to_real_junk (
				server, folder, cancellable,
				&need_to_expunge, error);
			expunge |= need_to_expunge;
		}

		if (success) {
			success = imapx_move_to_real_trash (
				server, folder, cancellable,
				&need_to_expunge, error);
			expunge |= need_to_expunge;
		}

		/* Sync twice - make sure deleted flags are written out,
		 * then sync again incase expunge changed anything */

		if (success && expunge)
			success = camel_imapx_server_expunge (
				server, folder, cancellable, error);

		camel_imapx_store_op_done (istore, server, folder_name);
		g_object_unref (server);
	}

	return success;
}

static gboolean
imapx_synchronize_message_sync (CamelFolder *folder,
                                const gchar *uid,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	const gchar *folder_name;
	gboolean success = FALSE;

	folder_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server (
		istore, folder_name, cancellable, error);
	if (server != NULL) {
		success = camel_imapx_server_sync_message (
			server, folder, uid, cancellable, error);
		camel_imapx_store_op_done (istore, server, folder_name);
		g_object_unref (server);
	}

	return success;
}

static gboolean
imapx_transfer_messages_to_sync (CamelFolder *source,
                                 GPtrArray *uids,
                                 CamelFolder *dest,
                                 gboolean delete_originals,
                                 GPtrArray **transferred_uids,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	CamelIMAPXServer *server;
	const gchar *folder_name;
	gboolean success = FALSE;

	folder_name = camel_folder_get_full_name (source);
	parent_store = camel_folder_get_parent_store (source);
	istore = CAMEL_IMAPX_STORE (parent_store);

	if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (istore))) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	server = camel_imapx_store_get_server (
		istore, folder_name, cancellable, error);
	if (server != NULL) {
		success = camel_imapx_server_copy_message (
			server, source, dest, uids,
			delete_originals, cancellable, error);
		camel_imapx_store_op_done (istore, server, folder_name);
		g_object_unref (server);
	}

	imapx_refresh_info_sync (dest, cancellable, NULL);

	return success;
}

static void
imapx_rename (CamelFolder *folder,
              const gchar *new_name)
{
	CamelStore *parent_store;
	CamelIMAPXStore *istore;
	const gchar *folder_name;

	parent_store = camel_folder_get_parent_store (folder);
	istore = CAMEL_IMAPX_STORE (parent_store);

	camel_store_summary_disconnect_folder_summary (
		CAMEL_STORE_SUMMARY (istore->summary),
		folder->summary);

	/* Chain up to parent's rename() method. */
	CAMEL_FOLDER_CLASS (camel_imapx_folder_parent_class)->
		rename (folder, new_name);

	folder_name = camel_folder_get_full_name (folder);

	camel_store_summary_connect_folder_summary (
		CAMEL_STORE_SUMMARY (istore->summary),
		folder_name, folder->summary);
}

static void
camel_imapx_folder_class_init (CamelIMAPXFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_folder_set_property;
	object_class->get_property = imapx_folder_get_property;
	object_class->dispose = imapx_folder_dispose;
	object_class->finalize = imapx_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->rename = imapx_rename;
	folder_class->search_by_expression = imapx_search_by_expression;
	folder_class->search_by_uids = imapx_search_by_uids;
	folder_class->count_by_expression = imapx_count_by_expression;
	folder_class->search_free = imapx_search_free;
	folder_class->get_filename = imapx_get_filename;
	folder_class->append_message_sync = imapx_append_message_sync;
	folder_class->expunge_sync = imapx_expunge_sync;
	folder_class->fetch_messages_sync = imapx_fetch_messages_sync;
	folder_class->get_message_sync = imapx_get_message_sync;
	folder_class->get_quota_info_sync = imapx_get_quota_info_sync;
	folder_class->purge_message_cache_sync = imapx_purge_message_cache_sync;
	folder_class->refresh_info_sync = imapx_refresh_info_sync;
	folder_class->synchronize_sync = imapx_synchronize_sync;
	folder_class->synchronize_message_sync = imapx_synchronize_message_sync;
	folder_class->transfer_messages_to_sync = imapx_transfer_messages_to_sync;

	g_object_class_install_property (
		object_class,
		PROP_APPLY_FILTERS,
		g_param_spec_boolean (
			"apply-filters",
			"Apply Filters",
			_("Apply message _filters to this folder"),
			FALSE,
			G_PARAM_READWRITE |
			CAMEL_PARAM_PERSISTENT));

	g_object_class_install_property (
		object_class,
		PROP_QUOTA_ROOT_NAMES,
		g_param_spec_boxed (
			"quota-root-names",
			"Quota Root Names",
			"Quota root names for this folder",
			G_TYPE_STRV,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_imapx_folder_init (CamelIMAPXFolder *imapx_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (imapx_folder);
	GHashTable *move_to_real_junk_uids;
	GHashTable *move_to_real_trash_uids;

	move_to_real_junk_uids = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) camel_pstring_free,
		(GDestroyNotify) NULL);

	move_to_real_trash_uids = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) camel_pstring_free,
		(GDestroyNotify) NULL);

	imapx_folder->priv = CAMEL_IMAPX_FOLDER_GET_PRIVATE (imapx_folder);

	folder->folder_flags |= CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY;

	folder->permanent_flags =
		CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN |
		CAMEL_MESSAGE_USER;

	camel_folder_set_lock_async (folder, TRUE);

	g_mutex_init (&imapx_folder->priv->property_lock);

	g_mutex_init (&imapx_folder->priv->move_to_hash_table_lock);
	imapx_folder->priv->move_to_real_junk_uids = move_to_real_junk_uids;
	imapx_folder->priv->move_to_real_trash_uids = move_to_real_trash_uids;
}

CamelFolder *
camel_imapx_folder_new (CamelStore *store,
                        const gchar *folder_dir,
                        const gchar *folder_name,
                        GError **error)
{
	CamelFolder *folder;
	CamelService *service;
	CamelSettings *settings;
	CamelIMAPXFolder *ifolder;
	const gchar *short_name;
	gchar *state_file;
	gboolean filter_all;
	gboolean filter_inbox;
	gboolean filter_junk;
	gboolean filter_junk_inbox;

	d ("opening imap folder '%s'\n", folder_dir);

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	g_object_get (
		settings,
		"filter-all", &filter_all,
		"filter-inbox", &filter_inbox,
		"filter-junk", &filter_junk,
		"filter-junk-inbox", &filter_junk_inbox,
		NULL);

	g_object_unref (settings);

	short_name = strrchr (folder_name, '/');
	if (short_name)
		short_name++;
	else
		short_name = folder_name;

	folder = g_object_new (
		CAMEL_TYPE_IMAPX_FOLDER,
		"display-name", short_name,
		"full_name", folder_name,
		"parent-store", store, NULL);
	ifolder = (CamelIMAPXFolder *) folder;

	((CamelIMAPXFolder *) folder)->raw_name = g_strdup (folder_name);

	folder->summary = camel_imapx_summary_new (folder);
	if (!folder->summary) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("Could not create folder summary for %s"),
			short_name);
		return NULL;
	}

	ifolder->cache = camel_data_cache_new (folder_dir, error);
	if (!ifolder->cache) {
		g_prefix_error (
			error, _("Could not create cache for %s: "),
			short_name);
		return NULL;
	}

	state_file = g_build_filename (folder_dir, "cmeta", NULL);
	camel_object_set_state_filename (CAMEL_OBJECT (folder), state_file);
	g_free (state_file);
	camel_object_state_read (CAMEL_OBJECT (folder));

	ifolder->search = camel_imapx_search_new ();
	g_mutex_init (&ifolder->search_lock);
	g_mutex_init (&ifolder->stream_lock);
	ifolder->ignore_recent = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);
	ifolder->exists_on_server = 0;
	ifolder->unread_on_server = 0;
	ifolder->modseq_on_server = 0;
	ifolder->uidnext_on_server = 0;

	if (!g_ascii_strcasecmp (folder_name, "INBOX")) {
		if (filter_inbox || filter_all)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
		if (filter_junk)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;
	} else {
		if (filter_junk && !filter_junk_inbox)
			folder->folder_flags |= CAMEL_FOLDER_FILTER_JUNK;

		if (filter_all || imapx_folder_get_apply_filters (ifolder))
			folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;
	}

	camel_store_summary_connect_folder_summary (
		(CamelStoreSummary *) ((CamelIMAPXStore *) store)->summary,
		folder_name, folder->summary);

	return folder;
}

gchar **
camel_imapx_folder_dup_quota_root_names (CamelIMAPXFolder *folder)
{
	gchar **duplicate;

	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), NULL);

	g_mutex_lock (&folder->priv->property_lock);

	duplicate = g_strdupv (folder->priv->quota_root_names);

	g_mutex_unlock (&folder->priv->property_lock);

	return duplicate;
}

void
camel_imapx_folder_set_quota_root_names (CamelIMAPXFolder *folder,
                                         const gchar **quota_root_names)
{
	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));

	g_mutex_lock (&folder->priv->property_lock);

	g_strfreev (folder->priv->quota_root_names);
	folder->priv->quota_root_names =
		g_strdupv ((gchar **) quota_root_names);

	g_mutex_unlock (&folder->priv->property_lock);

	g_object_notify (G_OBJECT (folder), "quota-root-names");
}

/**
 * camel_imapx_folder_add_move_to_real_junk:
 * @folder: a #CamelIMAPXFolder
 * @message_uid: a message UID
 *
 * Adds @message_uid to a pool of messages to be moved to a real junk
 * folder the next time @folder is explicitly synchronized by way of
 * camel_folder_synchronize() or camel_folder_synchronize_sync().
 *
 * This only applies when using a real folder to track junk messages,
 * as specified by the #CamelIMAPXSettings:use-real-junk-path setting.
 *
 * Since: 3.8
 **/
void
camel_imapx_folder_add_move_to_real_junk (CamelIMAPXFolder *folder,
                                          const gchar *message_uid)
{
	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	g_hash_table_add (
		folder->priv->move_to_real_junk_uids,
		(gpointer) camel_pstring_strdup (message_uid));

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);
}

/**
 * camel_imapx_folder_add_move_to_real_trash:
 * @folder: a #CamelIMAPXFolder
 * @message_uid: a message UID
 *
 * Adds @message_uid to a pool of messages to be moved to a real trash
 * folder the next time @folder is explicitly synchronized by way of
 * camel_folder_synchronize() or camel_folder_synchronize_sync().
 *
 * This only applies when using a real folder to track deleted messages,
 * as specified by the #CamelIMAPXSettings:use-real-trash-path setting.
 *
 * Since: 3.8
 **/
void
camel_imapx_folder_add_move_to_real_trash (CamelIMAPXFolder *folder,
                                           const gchar *message_uid)
{
	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));
	g_return_if_fail (message_uid != NULL);

	g_mutex_lock (&folder->priv->move_to_hash_table_lock);

	g_hash_table_add (
		folder->priv->move_to_real_trash_uids,
		(gpointer) camel_pstring_strdup (message_uid));

	g_mutex_unlock (&folder->priv->move_to_hash_table_lock);
}

