/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-db.h"
#include "camel-debug.h"
#include "camel-folder-search.h"
#include "camel-mime-message.h"
#include "camel-session.h"
#include "camel-store.h"
#include "camel-vee-folder.h"
#include "camel-vee-store.h"	/* for open flags */
#include "camel-vee-summary.h"
#include "camel-string-utils.h"
#include "camel-vtrash-folder.h"

#define d(x)
#define dd(x) (camel_debug ("vfolder")?(x):0)

typedef struct _FolderChangedData FolderChangedData;

struct _CamelVeeFolderPrivate {
	gboolean destroyed;
	GList *folders;			/* lock using subfolder_lock before changing/accessing */
	GList *folders_changed;		/* for list of folders that have changed between updates */
	GHashTable *ignore_changed;	/* hash of subfolder pointers to ignore the next folder's 'changed' signal */
	GHashTable *skipped_changes;	/* CamelFolder -> CamelFolderChangeInfo accumulating ignored changes */

	GMutex *summary_lock;		/* for locking vfolder summary */
	GMutex *subfolder_lock;		/* for locking the subfolder list */
	GMutex *changed_lock;		/* for locking the folders-changed list */
};

struct _update_data {
	CamelFolder *source;
	CamelVeeFolder *vee_folder;
	gchar hash[8];
	CamelVeeFolder *folder_unmatched;
	GHashTable *unmatched_uids;
	gboolean rebuilt, correlating;

	/* used for uids that needs to be updated in db later by unmatched_check_uid and
	 * folder_added_uid */
	GQueue *message_uids;
};

struct _FolderChangedData {
	CamelFolderChangeInfo *changes;
	CamelFolder *sub;
	CamelVeeFolder *vee_folder;
};

G_DEFINE_TYPE (CamelVeeFolder, camel_vee_folder, CAMEL_TYPE_FOLDER)

static void
folder_changed_data_free (FolderChangedData *data)
{
	camel_folder_change_info_free (data->changes);
	g_object_unref (data->vee_folder);
	g_object_unref (data->sub);

	g_slice_free (FolderChangedData, data);
}

/* must be called with summary_lock held */
static CamelVeeMessageInfo *
vee_folder_add_uid (CamelVeeFolder *vf,
                    CamelFolder *f,
                    const gchar *inuid,
                    const gchar hash[8])
{
	CamelVeeMessageInfo *mi = NULL;

	mi = camel_vee_summary_add ((CamelVeeSummary *)((CamelFolder *) vf)->summary, f->summary, (gchar *) inuid, hash);
	return mi;
}

/* same as vee_folder_add_uid, only returns whether uid was added or not */
static gboolean
vee_folder_add_uid_test (CamelVeeFolder *vf,
                         CamelFolder *f,
                         const gchar *inuid,
                         const gchar hash[8])
{
	CamelVeeMessageInfo *mi;

	mi = vee_folder_add_uid (vf, f, inuid, hash);

	if (mi != NULL)
		camel_message_info_free ((CamelMessageInfo *) mi);

	return mi != NULL;
}

/* A "correlating" expression has the property that whether a message matches
 * depends on the other messages being searched.  folder_changed_change on a
 * vfolder with a correlating expression may not make all the necessary updates,
 * so the query is redone on the entire changed source folder the next time
 * the vfolder is opened.
 *
 * The only current example of a correlating expression is one that uses
 * "match-threads". */
static gboolean
expression_is_correlating (const gchar *expr)
{
	/* XXX: Actually parse the expression to avoid triggering on
	 * "match-threads" in the text the user is searching for! */
	return (strstr (expr, "match-threads") != NULL);
}

/* Hold all these with summary lock and unmatched summary lock held */
static void
folder_changed_add_uid (CamelFolder *sub,
                        const gchar *uid,
                        const gchar hash[8],
                        CamelVeeFolder *vf,
                        gboolean use_db,
                        GList **m_added_l,
                        GList **unm_added_l)
{
	CamelVeeMessageInfo *vinfo;
	const gchar *vuid;
	gchar *oldkey;
	gpointer oldval;
	gint n;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;

	vinfo = vee_folder_add_uid (vf, sub, uid, hash);
	if (vinfo == NULL)
		return;

	vuid = camel_pstring_strdup (camel_message_info_uid (vinfo));
	camel_message_info_free ((CamelMessageInfo *) vinfo);

	if (use_db)
		*m_added_l = g_list_prepend (*m_added_l, (gpointer) camel_pstring_strdup (vuid));

	camel_folder_change_info_add_uid (vf->changes,  vuid);
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL) {
		if (g_hash_table_lookup_extended (unmatched_uids, vuid, (gpointer *) &oldkey, &oldval)) {
			n = GPOINTER_TO_INT (oldval);
			g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n + 1));
		} else {
			g_hash_table_insert (unmatched_uids, g_strdup (vuid), GINT_TO_POINTER (1));
		}
		vinfo = (CamelVeeMessageInfo *) camel_folder_get_message_info ((CamelFolder *) folder_unmatched, vuid);
		if (vinfo) {
			camel_folder_change_info_remove_uid (
				folder_unmatched->changes, vuid);

			*unm_added_l = g_list_prepend (*unm_added_l, (gpointer) camel_pstring_strdup (vuid));

			camel_folder_summary_remove_uid (
				CAMEL_FOLDER (folder_unmatched)->summary, vuid);
			camel_folder_free_message_info (
				CAMEL_FOLDER (folder_unmatched),
				(CamelMessageInfo *) vinfo);
		}
	}

	camel_pstring_free (vuid);
}

static void
folder_changed_remove_uid (CamelFolder *sub,
                           const gchar *uid,
                           const gchar hash[8],
                           gint keep,
                           CamelVeeFolder *vf,
                           gboolean use_db,
                           GList **m_removed_l,
                           GList **unm_removed_l)
{
	CamelFolder *folder = (CamelFolder *) vf;
	gchar *vuid, *oldkey;
	gpointer oldval;
	gint n;
	CamelVeeMessageInfo *vinfo;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;

	vuid = alloca (strlen (uid) + 9);
	memcpy (vuid, hash, 8);
	strcpy (vuid + 8, uid);

	camel_folder_change_info_remove_uid (vf->changes, vuid);
	if (use_db)
		*m_removed_l = g_list_prepend (*m_removed_l, (gpointer) camel_pstring_strdup (vuid));

	camel_folder_summary_remove_uid (folder->summary, vuid);

	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL) {
		if (keep) {
			if (g_hash_table_lookup_extended (unmatched_uids, vuid, (gpointer *) &oldkey, &oldval)) {
				n = GPOINTER_TO_INT (oldval);
				if (n == 1) {
					g_hash_table_remove (unmatched_uids, oldkey);
					if (vee_folder_add_uid_test (folder_unmatched, sub, uid, hash))
						camel_folder_change_info_add_uid (folder_unmatched->changes, oldkey);
					g_free (oldkey);
				} else {
					g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n - 1));
				}
			} else {
				if (vee_folder_add_uid_test (folder_unmatched, sub, uid, hash))
					camel_folder_change_info_add_uid (folder_unmatched->changes, vuid);
			}
		} else {
			if (g_hash_table_lookup_extended (unmatched_uids, vuid, (gpointer *) &oldkey, &oldval)) {
				g_hash_table_remove (unmatched_uids, oldkey);
				g_free (oldkey);
			}

			vinfo = (CamelVeeMessageInfo *) camel_folder_get_message_info ((CamelFolder *) folder_unmatched, vuid);
			if (vinfo) {
				camel_folder_change_info_remove_uid (
					folder_unmatched->changes, vuid);

				*unm_removed_l = g_list_prepend (*unm_removed_l, (gpointer) camel_pstring_strdup (vuid));

				camel_folder_summary_remove_uid (
					CAMEL_FOLDER (folder_unmatched)->summary, vuid);
				camel_folder_free_message_info (
					CAMEL_FOLDER (folder_unmatched),
					(CamelMessageInfo *) vinfo);
			}
		}
	}
}

static void
folder_changed_change_uid (CamelFolder *sub,
                           const gchar *uid,
                           const gchar hash[8],
                           CamelVeeFolder *vf,
                           gboolean use_db,
                           GList **m_removed_l,
                           GList **unm_removed_l)
{
	gchar *vuid;
	CamelVeeMessageInfo *vinfo, *uinfo = NULL;
	CamelMessageInfo *info;
	CamelFolder *folder = (CamelFolder *) vf;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	vuid = alloca (strlen (uid) + 9);
	memcpy (vuid, hash, 8);
	strcpy (vuid + 8, uid);

	vinfo = (CamelVeeMessageInfo *) camel_folder_summary_get (folder->summary, vuid);
	if (folder_unmatched != NULL)
		uinfo = (CamelVeeMessageInfo *) camel_folder_summary_get (((CamelFolder *) folder_unmatched)->summary, vuid);
	if (vinfo || uinfo) {
		info = camel_folder_get_message_info (sub, uid);
		if (info) {
			if (vinfo) {
				camel_folder_change_info_change_uid (vf->changes, vuid);
				vinfo->old_flags = camel_message_info_flags ((CamelMessageInfo *) vinfo);
				vinfo->info.flags |= (vinfo->old_flags & ~CAMEL_MESSAGE_FOLDER_FLAGGED);
				camel_message_info_free ((CamelMessageInfo *) vinfo);
			}

			if (uinfo) {
				camel_folder_change_info_change_uid (folder_unmatched->changes, vuid);
				uinfo->old_flags = camel_message_info_flags ((CamelMessageInfo *) uinfo);
				uinfo->info.flags |= (uinfo->old_flags & ~CAMEL_MESSAGE_FOLDER_FLAGGED);
				camel_message_info_free ((CamelMessageInfo *) uinfo);
			}

			camel_folder_free_message_info (sub, info);
		} else {
			if (vinfo) {
				folder_changed_remove_uid (sub, uid, hash, FALSE, vf, use_db, m_removed_l, unm_removed_l);
				camel_message_info_free ((CamelMessageInfo *) vinfo);
			}
			if (uinfo)
				camel_message_info_free ((CamelMessageInfo *) uinfo);
		}
	}
}

static void
vfolder_add_remove_transaction (CamelStore *parent_store,
                                const gchar *full_name,
                                GList **uids,
                                gboolean add,
                                GError **error)
{
	GList *l;

	for (l = *uids; l != NULL; l = g_list_next (l)) {
		if (add)
			 camel_db_add_to_vfolder_transaction	(parent_store->cdb_w, full_name,
								(const gchar *) l->data, error);
		else
			camel_db_delete_uid_from_vfolder_transaction
								(parent_store->cdb_w, full_name,
								 (const gchar *) l->data, error);
	}

	g_list_foreach (*uids, (GFunc) camel_pstring_free, NULL);
	g_list_free (*uids);
	*uids = NULL;
}

static void
folder_changed_change (CamelSession *session,
                       GCancellable *cancellable,
                       FolderChangedData *data,
                       GError **error)
{
	CamelFolder *sub = data->sub;
	CamelFolder *folder = CAMEL_FOLDER (data->vee_folder);
	CamelVeeFolder *vf = data->vee_folder;
	CamelFolderChangeInfo *changes = data->changes;
	gchar *vuid = NULL, hash[8];
	const gchar *uid;
	CamelVeeMessageInfo *vinfo;
	gint i, vuidlen = 0;
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	GPtrArray *matches_added = NULL, /* newly added, that match */
		*matches_changed = NULL, /* newly changed, that now match */
		*newchanged = NULL,
		*changed;
	GPtrArray *always_changed = NULL;
	GHashTable *matches_hash;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	GPtrArray *present = NULL;
	GList *m_added_l = NULL, *m_removed_l = NULL, *unm_added_l = NULL, *unm_removed_l = NULL;

	/* See vee_folder_rebuild_folder. */
	gboolean correlating = expression_is_correlating (vf->expression);

	/* Check the folder hasn't beem removed while we weren't watching */
	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
	if (g_list_find (vf->priv->folders, sub) == NULL) {
		camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
		return;
	}

	camel_vee_folder_hash_folder (sub, hash);

	/* Lookup anything before we lock anything, to avoid deadlock with build_folder */

	/* Find newly added that match */
	if (changes->uid_added->len > 0) {
		dd (printf (" Searching for added matches '%s'\n", vf->expression));
		matches_added = camel_folder_search_by_uids (sub, vf->expression, changes->uid_added, NULL);
	}

	/* TODO:
	 * In this code around here, we can work out if the search will affect the changes
	 * we had, and only re-search against them if they might have */

	/* Search for changed items that newly match, but only if we dont have them */
	changed = changes->uid_changed;
	if (changed->len > 0) {
		dd (printf (" Searching for changed matches '%s'\n", vf->expression));

		if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0) {
			newchanged = g_ptr_array_new ();
			always_changed = g_ptr_array_new ();
			for (i = 0; i < changed->len; i++) {
				uid = changed->pdata[i];
				if (strlen (uid) + 9 > vuidlen) {
					vuidlen = strlen (uid) + 64;
					vuid = g_realloc (vuid, vuidlen);
				}
				memcpy (vuid, hash, 8);
				strcpy (vuid + 8, uid);
				vinfo = (CamelVeeMessageInfo *) camel_folder_summary_get (folder->summary, vuid);
				if (vinfo == NULL) {
					g_ptr_array_add (newchanged, (gchar *) uid);
				} else {
					g_ptr_array_add (always_changed, (gchar *) uid);
					camel_message_info_free ((CamelMessageInfo *) vinfo);
				}
			}
			changed = newchanged;
		}

		if (changed->len)
			matches_changed = camel_folder_search_by_uids (sub, vf->expression, changed, NULL);
		if (always_changed && always_changed->len)
			present = camel_folder_search_by_uids (sub, vf->expression, always_changed, NULL);
	}

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	if (folder_unmatched != NULL)
		camel_vee_folder_lock (folder_unmatched, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	/* Always remove removed uid's, in any case */
	for (i = 0; i < changes->uid_removed->len; i++) {
		dd (printf ("  removing uid '%s'\n", (gchar *)changes->uid_removed->pdata[i]));
		folder_changed_remove_uid (sub, changes->uid_removed->pdata[i], hash, FALSE, vf, !correlating, &m_removed_l, &unm_removed_l);
	}

	/* Add any newly matched or to unmatched folder if they dont */
	if (matches_added) {
		matches_hash = g_hash_table_new (g_str_hash, g_str_equal);
		for (i = 0; i < matches_added->len; i++) {
			dd (printf (" %s", (gchar *)matches_added->pdata[i]));
			g_hash_table_insert (matches_hash, matches_added->pdata[i], matches_added->pdata[i]);
		}
		for (i = 0; i < changes->uid_added->len; i++) {
			uid = changes->uid_added->pdata[i];
			if (g_hash_table_lookup (matches_hash, uid)) {
				dd (printf ("  adding uid '%s' [newly matched]\n", (gchar *)uid));
				folder_changed_add_uid (sub, uid, hash, vf, !correlating, &m_added_l, &unm_added_l);
			} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
				if (strlen (uid) + 9 > vuidlen) {
					vuidlen = strlen (uid) + 64;
					vuid = g_realloc (vuid, vuidlen);
				}
				memcpy (vuid, hash, 8);
				strcpy (vuid + 8, uid);

				if (!CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL && g_hash_table_lookup (unmatched_uids, vuid) == NULL) {
					dd (printf ("  adding uid '%s' to Unmatched [newly unmatched]\n", (gchar *)uid));
					vinfo = (CamelVeeMessageInfo *) camel_folder_get_message_info ((CamelFolder *) folder_unmatched, vuid);
					if (vinfo == NULL) {
						if (vee_folder_add_uid_test (folder_unmatched, sub, uid, hash))
							camel_folder_change_info_add_uid (folder_unmatched->changes, vuid);
					} else {
						camel_folder_free_message_info ((CamelFolder *) folder_unmatched, (CamelMessageInfo *) vinfo);
					}
				}
			}
		}
		g_hash_table_destroy (matches_hash);
	}

	/* Change any newly changed */
	if (always_changed) {
		if (correlating) {
			/* Messages may be pulled in by the correlation even if
			 * they do not match the expression individually, so it
			 * would be wrong to preemptively remove anything here.
			 * vee_folder_rebuild_folder will make any necessary removals
			 * when it re-queries the entire source folder. */
			for (i = 0; i < always_changed->len; i++)
				folder_changed_change_uid (sub, always_changed->pdata[i], hash, vf, !correlating, &m_removed_l, &unm_removed_l);
		} else {
			GHashTable *ht_present = g_hash_table_new (g_str_hash, g_str_equal);

			for (i = 0; present && i < present->len; i++) {
				folder_changed_change_uid (sub, present->pdata[i], hash, vf, !correlating, &m_removed_l, &unm_removed_l);
				g_hash_table_insert (ht_present, present->pdata[i], present->pdata[i]);
			}

			for (i = 0; i < always_changed->len; i++) {
				if (!present || !g_hash_table_lookup (ht_present, always_changed->pdata[i]))
					/* XXX: IIUC, these messages haven't been deleted from the
					 * source folder, so shouldn't "keep" be set to TRUE? */
					folder_changed_remove_uid (sub, always_changed->pdata[i], hash, TRUE, vf, !correlating, &m_removed_l, &unm_removed_l);
			}

			g_hash_table_destroy (ht_present);
		}
		g_ptr_array_free (always_changed, TRUE);
	}

	/* Change/add/remove any changed */
	if (changes->uid_changed->len) {
		/* If we are auto-updating, then re-check changed uids still match */
		dd (printf (" Vfolder %supdate\nuids match:", (vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO)?"auto-":""));
		matches_hash = g_hash_table_new (g_str_hash, g_str_equal);
		for (i = 0; matches_changed && i < matches_changed->len; i++) {
			dd (printf (" %s", (gchar *)matches_changed->pdata[i]));
			g_hash_table_insert (matches_hash, matches_changed->pdata[i], matches_changed->pdata[i]);
		}
		dd (printf ("\n"));

		for (i = 0; i < changed->len; i++) {
			uid = changed->pdata[i];
			if (strlen (uid) + 9 > vuidlen) {
				vuidlen = strlen (uid) + 64;
				vuid = g_realloc (vuid, vuidlen);
			}
			memcpy (vuid, hash, 8);
			strcpy (vuid + 8, uid);
			vinfo = (CamelVeeMessageInfo *) camel_folder_summary_get (folder->summary, vuid);
			if (vinfo == NULL) {
				if (g_hash_table_lookup (matches_hash, uid)) {
					/* A uid we dont have, but now it matches, add it */
					dd (printf ("  adding uid '%s' [newly matched]\n", uid));
					folder_changed_add_uid (sub, uid, hash, vf, !correlating, &m_added_l, &unm_added_l);
				} else {
					/* A uid we still don't have, just change it (for unmatched) */
					folder_changed_change_uid (sub, uid, hash, vf, !correlating, &m_removed_l, &unm_removed_l);
				}
			} else {
				if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0
				    || g_hash_table_lookup (matches_hash, uid)) {
					/* still match, or we're not auto-updating, change event, (if it changed) */
					dd (printf ("  changing uid '%s' [still matches]\n", uid));
					folder_changed_change_uid (sub, uid, hash, vf, !correlating, &m_removed_l, &unm_removed_l);
				} else {
					/* No longer matches, remove it, but keep it in unmatched (potentially) */
					dd (printf ("  removing uid '%s' [did match]\n", uid));
					folder_changed_remove_uid (sub, uid, hash, TRUE, vf, !correlating, &m_removed_l, &unm_removed_l);
				}
				camel_message_info_free ((CamelMessageInfo *) vinfo);
			}
		}
		g_hash_table_destroy (matches_hash);
	} else {
		/* stuff didn't match but it changed - check unmatched folder for changes */
		for (i = 0; i < changed->len; i++)
			folder_changed_change_uid (sub, changed->pdata[i], hash, vf, !correlating, &m_removed_l, &unm_removed_l);
	}

	if (folder_unmatched != NULL) {
		if (camel_folder_change_info_changed (folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new ();
		}

		camel_vee_folder_unlock (folder_unmatched, CAMEL_VEE_FOLDER_SUMMARY_LOCK);
	}

	if (camel_folder_change_info_changed (vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new ();
	}

	if (matches_changed || matches_added || changes->uid_removed->len || present) {
		const gchar *full_name, *unm_full_name;
		CamelStore *parent_store;

		parent_store = camel_folder_get_parent_store (folder);
		full_name = camel_folder_get_full_name (folder);

		if (folder_unmatched)
			unm_full_name = camel_folder_get_full_name (CAMEL_FOLDER (folder_unmatched));

		camel_db_begin_transaction (parent_store->cdb_w, NULL);

		if (m_added_l)
			vfolder_add_remove_transaction (parent_store, full_name, &m_added_l, TRUE, NULL);
		if (m_removed_l)
			vfolder_add_remove_transaction (parent_store, full_name, &m_removed_l, FALSE, NULL);
		if (unm_added_l)
			vfolder_add_remove_transaction (parent_store, unm_full_name, &unm_added_l, TRUE, NULL);
		if (unm_removed_l)
			vfolder_add_remove_transaction (parent_store, unm_full_name, &unm_removed_l, FALSE, NULL);

		camel_db_end_transaction (parent_store->cdb_w, NULL);
	}

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	/* Cleanup stuff on our folder */
	if (matches_added)
		camel_folder_search_free (sub, matches_added);
	if (present)
		camel_folder_search_free (sub, present);

	if (matches_changed)
		camel_folder_search_free (sub, matches_changed);

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	/* cleanup the rest */
	if (newchanged)
		g_ptr_array_free (newchanged, TRUE);

	g_free (vuid);

	if (unmatched_changes) {
		camel_folder_changed (
			CAMEL_FOLDER (folder_unmatched), unmatched_changes);
		camel_folder_change_info_free (unmatched_changes);
	}

	/* Add to folders_changed if we need to call vee_folder_rebuild_folder, which
	 * could be the case for two reasons:
	 * - We changed the vfolder and it is not auto-updating.  Need to re-sync.
	 * - Vfolder is correlating.  Changes to non-matching source messages
	 *   won't be processed here and won't show up in vf_changes but may
	 *   still affect the vfolder contents (e.g., non-matching messages
	 *   added to a matching thread), so we re-run the query on the whole
	 *   source folder.  (For match-threads, it may be enough to do this if
	 *   changes->uid_added->len > 0, but I'm not completely sure and I'd
	 *   rather be safe than sorry.)
	 */
	if ((vf_changes && (vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0) || correlating) {
		camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
		if (g_list_find (vf->priv->folders_changed, sub) == NULL)
			vf->priv->folders_changed = g_list_prepend (vf->priv->folders_changed, sub);
		camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
	}

	if (vf_changes) {
		camel_folder_changed (CAMEL_FOLDER (vf), vf_changes);
		camel_folder_change_info_free (vf_changes);
	}
}

static void
subfolder_renamed_update (CamelVeeFolder *vf,
                          CamelFolder *sub,
                          gchar hash[8])
{
	gint i;
	CamelFolderChangeInfo *changes = NULL;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = sub->summary;
	GPtrArray *known_uids;

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	camel_folder_summary_prepare_fetch_all (((CamelFolder *) vf)->summary, NULL);

	known_uids = camel_folder_summary_get_array (((CamelFolder *) vf)->summary);
	for (i = 0; known_uids && i < known_uids->len; i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *) camel_folder_summary_get (((CamelFolder *) vf)->summary, g_ptr_array_index (known_uids, i));
		CamelVeeMessageInfo *vinfo;

		if (mi == NULL)
			continue;

		if (mi->orig_summary == ssummary) {
			gchar *uid = (gchar *) camel_message_info_uid (mi);
			gchar *oldkey;
			gpointer oldval;

			camel_folder_change_info_remove_uid (vf->changes, uid);
			camel_folder_summary_remove (((CamelFolder *) vf)->summary, (CamelMessageInfo *) mi);

			vinfo = vee_folder_add_uid (vf, sub, uid + 8, hash);
			if (vinfo) {
				camel_folder_change_info_add_uid (vf->changes, camel_message_info_uid (vinfo));

				/* check unmatched uid's table for any matches */
				if (vf == folder_unmatched
				    && g_hash_table_lookup_extended (unmatched_uids, uid, (gpointer *) &oldkey, &oldval)) {
					g_hash_table_remove (unmatched_uids, oldkey);
					g_hash_table_insert (unmatched_uids, g_strdup (camel_message_info_uid (vinfo)), oldval);
					g_free (oldkey);
				}

				camel_message_info_free ((CamelMessageInfo *) vinfo);
			}
		}

		camel_message_info_free ((CamelMessageInfo *) mi);
	}

	camel_folder_summary_free_array (known_uids);

	if (camel_folder_change_info_changed (vf->changes)) {
		changes = vf->changes;
		vf->changes = camel_folder_change_info_new ();
	}

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	if (changes) {
		camel_folder_changed (CAMEL_FOLDER (vf), changes);
		camel_folder_change_info_free (changes);
	}
}

static gint
vee_folder_rebuild_folder (CamelVeeFolder *vee_folder,
                           CamelFolder *source,
                           GError **error);

static void
unmatched_check_uid (gchar *uidin,
                     gpointer value,
                     struct _update_data *u)
{
	gchar *uid;
	gint n;

	uid = alloca (strlen (uidin) + 9);
	memcpy (uid, u->hash, 8);
	strcpy (uid + 8, uidin);
	n = GPOINTER_TO_INT (g_hash_table_lookup (u->unmatched_uids, uid));
	if (n == 0) {
		if (vee_folder_add_uid_test (u->folder_unmatched, u->source, uidin, u->hash))
			camel_folder_change_info_add_uid (u->folder_unmatched->changes, uid);
	} else {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *) camel_folder_summary_get (((CamelFolder *) u->folder_unmatched)->summary, uid);
		if (mi) {
			if (u->message_uids != NULL)
				g_queue_push_tail (u->message_uids, g_strdup (uid));

			camel_folder_summary_remove_uid (
				((CamelFolder *) u->folder_unmatched)->summary, uid);
			camel_folder_change_info_remove_uid (
				u->folder_unmatched->changes, uid);
			camel_message_info_free ((CamelMessageInfo *) mi);
		}
	}
}

static void
folder_added_uid (gchar *uidin,
                  gpointer value,
                  struct _update_data *u)
{
	CamelVeeMessageInfo *mi;
	gchar *oldkey;
	gpointer oldval;
	const gchar *uid;
	gint n;

	mi = vee_folder_add_uid (u->vee_folder, u->source, uidin, u->hash);
	if (mi == NULL)
		return;

	uid = camel_message_info_uid (mi);

	if (u->message_uids != NULL)
		g_queue_push_tail (u->message_uids, g_strdup (uid));

	camel_folder_change_info_add_uid (u->vee_folder->changes, uid);

	if (!CAMEL_IS_VEE_FOLDER (u->source) && u->unmatched_uids != NULL) {
		gboolean found_uid;

		found_uid = g_hash_table_lookup_extended (
			u->unmatched_uids, uid,
			(gpointer *) &oldkey, &oldval);

		if (found_uid) {
			n = GPOINTER_TO_INT (oldval);
			g_hash_table_insert (
				u->unmatched_uids,
				oldkey, GINT_TO_POINTER (n + 1));
		} else {
			g_hash_table_insert (
				u->unmatched_uids,
				g_strdup (uid), GINT_TO_POINTER (1));
		}
	}

	camel_message_info_free ((CamelMessageInfo *) mi);
}

static	CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s,
                      GError **error)
{
	CamelFIRecord * record = g_new0 (CamelFIRecord, 1);
	const gchar *full_name;

	/* We do this during write, so lets use write handle, though we gonna read */
	full_name = camel_folder_get_full_name (camel_folder_summary_get_folder (s));

	record->folder_name = g_strdup (full_name);

	/* we always write out the current version */
	record->version = 13;  /* FIXME: CAMEL_FOLDER_SUMMARY_VERSION; */
	record->flags  = s->flags;
	record->nextuid = camel_folder_summary_get_next_uid (s);
	record->time = s->time;

	record->saved_count = camel_folder_summary_count (s);
	record->junk_count = camel_folder_summary_get_junk_count (s);
	record->deleted_count = camel_folder_summary_get_deleted_count (s);
	record->unread_count = camel_folder_summary_get_unread_count (s);
	record->visible_count = camel_folder_summary_get_visible_count (s);
	record->jnd_count = camel_folder_summary_get_junk_not_deleted_count (s);

	return record;
}

static void
folder_changed (CamelFolder *sub,
                CamelFolderChangeInfo *changes,
                CamelVeeFolder *vee_folder)
{
	CamelVeeFolderClass *class;

	g_return_if_fail (vee_folder != NULL);
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (vee_folder));

	camel_vee_folder_lock (vee_folder, CAMEL_VEE_FOLDER_CHANGED_LOCK);
	if (g_hash_table_lookup (vee_folder->priv->ignore_changed, sub)) {
		CamelFolderChangeInfo *old_changes;
		g_hash_table_remove (vee_folder->priv->ignore_changed, sub);

		old_changes = g_hash_table_lookup (vee_folder->priv->skipped_changes, sub);
		if (!old_changes)
			old_changes = camel_folder_change_info_new ();
		camel_folder_change_info_cat (old_changes, changes);
		g_hash_table_insert (vee_folder->priv->skipped_changes, sub, old_changes);
		camel_vee_folder_unlock (vee_folder, CAMEL_VEE_FOLDER_CHANGED_LOCK);
		return;
	}
	camel_vee_folder_unlock (vee_folder, CAMEL_VEE_FOLDER_CHANGED_LOCK);

	class = CAMEL_VEE_FOLDER_GET_CLASS (vee_folder);
	class->folder_changed (vee_folder, sub, changes);
}

/* track vanishing folders */
static void
subfolder_deleted (CamelFolder *folder,
                   CamelVeeFolder *vee_folder)
{
	camel_vee_folder_remove_folder (vee_folder, folder);
}

static void
folder_renamed (CamelFolder *sub,
                const gchar *old,
                CamelVeeFolder *vee_folder)
{
	CamelVeeFolderClass *class;

	class = CAMEL_VEE_FOLDER_GET_CLASS (vee_folder);
	class->folder_renamed (vee_folder, sub, old);
}

static void
vee_folder_stop_folder (CamelVeeFolder *vf,
                        CamelFolder *sub,
                        GCancellable *cancellable)
{
	CamelVeeFolderPrivate *p = vf->priv;
	gint i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
	p->folders_changed = g_list_remove (p->folders_changed, sub);
	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);

	if (g_list_find (p->folders, sub) == NULL) {
		camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
		return;
	}

	g_signal_handlers_disconnect_by_func (sub, folder_changed, vf);
	g_signal_handlers_disconnect_by_func (sub, subfolder_deleted, vf);
	g_signal_handlers_disconnect_by_func (sub, folder_renamed, vf);

	p->folders = g_list_remove (p->folders, sub);

	/* undo the freeze state that we have imposed on this source folder */
	camel_folder_lock (CAMEL_FOLDER (vf), CAMEL_FOLDER_CHANGE_LOCK);
	for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) vf); i++)
		camel_folder_thaw (sub);
	camel_folder_unlock (CAMEL_FOLDER (vf), CAMEL_FOLDER_CHANGE_LOCK);

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	if (folder_unmatched != NULL) {
		CamelVeeFolderPrivate *up = folder_unmatched->priv;

		camel_vee_folder_lock (folder_unmatched, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
		/* if folder deleted, then blow it away from unmatched always, and remove all refs to it */
		if (sub->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
			while (g_list_find (up->folders, sub)) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
			}
		} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
			if (g_list_find (up->folders, sub) != NULL) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
			}
		}
		camel_vee_folder_unlock (folder_unmatched, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
	}

	if (CAMEL_IS_VEE_FOLDER (sub))
		return;

	g_object_unref (sub);
}

static void
vee_folder_dispose (GObject *object)
{
	CamelFolder *folder;

	folder = CAMEL_FOLDER (object);

	/* parent's class frees summary on dispose, thus depend on it */
	if (folder->summary) {
		CamelVeeFolder *vf;
		CamelVeeFolder *folder_unmatched;
		GList *node;
		CamelFIRecord * record;

		vf = CAMEL_VEE_FOLDER (object);
		vf->priv->destroyed = TRUE;

		folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

		/* Save the counts to DB */
		if (!vf->deleted) {
			CamelFolder *folder;
			CamelStore *parent_store;

			folder = CAMEL_FOLDER (vf);
			parent_store = camel_folder_get_parent_store (folder);
			record = summary_header_to_db (folder->summary, NULL);

			camel_db_begin_transaction (parent_store->cdb_w, NULL);
			camel_db_write_folder_info_record (parent_store->cdb_w, record, NULL);
			camel_db_end_transaction (parent_store->cdb_w, NULL);

			g_free (record->folder_name);
			g_free (record);
		}

		/* This may invoke sub-classes with partially destroyed state, they must deal with this */
		if (vf == folder_unmatched) {
			for (node = vf->priv->folders; node; node = g_list_next (node))
				g_object_unref (node->data);
		} else {
			/* FIXME[disk-summary] See if it is really reqd */
			camel_folder_freeze ((CamelFolder *) vf);
			while (vf->priv->folders) {
				CamelFolder *f = vf->priv->folders->data;
				vee_folder_stop_folder (vf, f, NULL);
			}
			camel_folder_thaw ((CamelFolder *) vf);
		}
	}

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_vee_folder_parent_class)->dispose (object);
}

static void
free_change_info_cb (gpointer folder,
                     gpointer change_info,
                     gpointer user_data)
{
	camel_folder_change_info_free (change_info);
}

static void
vee_folder_finalize (GObject *object)
{
	CamelVeeFolder *vf;

	vf = CAMEL_VEE_FOLDER (object);

	g_free (vf->expression);

	g_list_free (vf->priv->folders);
	g_list_free (vf->priv->folders_changed);

	camel_folder_change_info_free (vf->changes);
	g_object_unref (vf->search);

	g_hash_table_foreach (vf->priv->skipped_changes, free_change_info_cb, NULL);

	g_mutex_free (vf->priv->summary_lock);
	g_mutex_free (vf->priv->subfolder_lock);
	g_mutex_free (vf->priv->changed_lock);
	g_hash_table_destroy (vf->hashes);
	g_hash_table_destroy (vf->priv->ignore_changed);
	g_hash_table_destroy (vf->priv->skipped_changes);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_vee_folder_parent_class)->finalize (object);
}

static void
vee_folder_propagate_skipped_changes (CamelVeeFolder *vf)
{
	CamelVeeFolderClass *klass;
	GHashTableIter iter;
	gpointer psub, pchanges;

	g_return_if_fail (vf != NULL);

	klass = CAMEL_VEE_FOLDER_GET_CLASS (vf);

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);

	g_hash_table_iter_init (&iter, vf->priv->skipped_changes);
	while (g_hash_table_iter_next (&iter, &psub, &pchanges)) {
		g_warn_if_fail (pchanges != NULL);
		if (!pchanges)
			continue;

		if (g_list_find (vf->priv->folders, psub) != NULL)
			klass->folder_changed (vf, psub, pchanges);

		camel_folder_change_info_free (pchanges);
	}

	g_hash_table_remove_all (vf->priv->skipped_changes);

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
}

static GPtrArray *
vee_folder_search_by_expression (CamelFolder *folder,
                                 const gchar *expression,
                                 GError **error)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	gchar *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *) folder;
	CamelVeeFolderPrivate *p = vf->priv;
	GHashTable *searched = g_hash_table_new (NULL, NULL);
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	gboolean is_folder_unmatched = vf == folder_unmatched && folder_unmatched;
	CamelFolderSummary *folder_unmatched_summary = NULL;

	vee_folder_propagate_skipped_changes (vf);

	if (is_folder_unmatched) {
		expr = g_strdup (expression);
		folder_unmatched_summary = ((CamelFolder *) folder_unmatched)->summary;
	} else {
		expr = g_strdup_printf ("(and %s %s)", vf->expression ? vf->expression : "", expression);
	}

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		gint i;
		gchar hash[8];

		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup (searched, f) == NULL) {
			camel_vee_folder_hash_folder (f, hash);
			matches = camel_folder_search_by_expression (f, expr, NULL);
			if (matches) {
				for (i = 0; i < matches->len; i++) {
					gchar *uid = matches->pdata[i], *vuid;

					vuid = g_malloc (strlen (uid) + 9);
					memcpy (vuid, hash, 8);
					strcpy (vuid + 8, uid);

					if (!is_folder_unmatched || camel_folder_summary_check_uid (folder_unmatched_summary, vuid))
						g_ptr_array_add (result, (gpointer) camel_pstring_strdup (vuid));
					g_free (vuid);
				}
				camel_folder_search_free (f, matches);
			}
			g_hash_table_insert (searched, f, f);
		}
		node = g_list_next (node);
	}

	g_free (expr);

	g_hash_table_destroy (searched);
	d (printf ("returning %d\n", result->len));
	return result;
}

static GPtrArray *
vee_folder_search_by_uids (CamelFolder *folder,
                           const gchar *expression,
                           GPtrArray *uids,
                           GError **error)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	GPtrArray *folder_uids = g_ptr_array_new ();
	gchar *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *) folder;
	CamelVeeFolderPrivate *p = vf->priv;
	GHashTable *searched = g_hash_table_new (NULL, NULL);

	vee_folder_propagate_skipped_changes (vf);

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	expr = g_strdup_printf ("(and %s %s)", vf->expression ? vf->expression : "", expression);
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		gint i;
		gchar hash[8];

		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup (searched, f) == NULL) {
			camel_vee_folder_hash_folder (f, hash);

			/* map the vfolder uid's to the source folder uid's first */
			g_ptr_array_set_size (folder_uids, 0);
			for (i = 0; i < uids->len; i++) {
				gchar *uid = uids->pdata[i];

				if (strlen (uid) >= 8 && strncmp (uid, hash, 8) == 0)
					g_ptr_array_add (folder_uids, uid + 8);
			}
			if (folder_uids->len > 0) {
				matches = camel_folder_search_by_uids (f, expr, folder_uids, error);
				if (matches) {
					for (i = 0; i < matches->len; i++) {
						gchar *uid = matches->pdata[i], *vuid;

						vuid = g_malloc (strlen (uid) + 9);
						memcpy (vuid, hash, 8);
						strcpy (vuid + 8, uid);
						g_ptr_array_add (result, (gpointer) camel_pstring_strdup (vuid));
						g_free (vuid);
					}
					camel_folder_search_free (f, matches);
				}
			}
			g_hash_table_insert (searched, f, f);
		}
		node = g_list_next (node);
	}

	g_free (expr);
	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	g_hash_table_destroy (searched);
	g_ptr_array_free (folder_uids, TRUE);

	return result;
}

static guint32
vee_folder_count_by_expression (CamelFolder *folder,
                                const gchar *expression,
                                GError **error)
{
	GList *node;
	gchar *expr;
	guint32 count = 0;
	CamelVeeFolder *vf = (CamelVeeFolder *) folder;
	CamelVeeFolderPrivate *p = vf->priv;
	GHashTable *searched = g_hash_table_new (NULL, NULL);
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	vee_folder_propagate_skipped_changes (vf);

	if (vf != folder_unmatched)
		expr = g_strdup_printf ("(and %s %s)", vf->expression ? vf->expression : "", expression);
	else
		expr = g_strdup (expression);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		/* make sure we only search each folder once - for unmatched folder to work right */
		if (g_hash_table_lookup (searched, f) == NULL) {
			count += camel_folder_count_by_expression (f, expr, NULL);
			g_hash_table_insert (searched, f, f);
		}
		node = g_list_next (node);
	}

	g_free (expr);

	g_hash_table_destroy (searched);
	return count;
}

static void
vee_folder_delete (CamelFolder *folder)
{
	CamelVeeFolderPrivate *p = CAMEL_VEE_FOLDER (folder)->priv;

	/* NB: this is never called on UNMTACHED */

	camel_vee_folder_lock (CAMEL_VEE_FOLDER (folder), CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
	while (p->folders) {
		CamelFolder *f = p->folders->data;

		g_object_ref (f);
		camel_vee_folder_unlock (CAMEL_VEE_FOLDER (folder), CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

		camel_vee_folder_remove_folder ((CamelVeeFolder *) folder, f);
		g_object_unref (f);
		camel_vee_folder_lock (CAMEL_VEE_FOLDER (folder), CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
	}
	camel_vee_folder_unlock (CAMEL_VEE_FOLDER (folder), CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	((CamelFolderClass *) camel_vee_folder_parent_class)->delete (folder);
	((CamelVeeFolder *) folder)->deleted = TRUE;
}

static void
vee_folder_freeze (CamelFolder *folder)
{
	CamelVeeFolder *vfolder = (CamelVeeFolder *) folder;
	CamelVeeFolderPrivate *p = vfolder->priv;
	GList *node;

	camel_vee_folder_lock (vfolder, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		camel_folder_freeze (f);
		node = node->next;
	}

	camel_vee_folder_unlock (vfolder, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent_class)->freeze (folder);
}

static void
vee_folder_thaw (CamelFolder *folder)
{
	CamelVeeFolder *vfolder = (CamelVeeFolder *) folder;
	CamelVeeFolderPrivate *p = vfolder->priv;
	GList *node;

	camel_vee_folder_lock (vfolder, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		camel_folder_thaw (f);
		node = node->next;
	}

	camel_vee_folder_unlock (vfolder, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	/* call parent implementation */
	CAMEL_FOLDER_CLASS (camel_vee_folder_parent_class)->thaw (folder);
}

static gboolean
vee_folder_append_message_sync (CamelFolder *folder,
                                CamelMimeMessage *message,
                                CamelMessageInfo *info,
                                gchar **appended_uid,
                                GCancellable *cancellable,
                                GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Cannot copy or move messages into a Virtual Folder"));

	return FALSE;
}

static gboolean
vee_folder_expunge_sync (CamelFolder *folder,
                         GCancellable *cancellable,
                         GError **error)
{
	return CAMEL_FOLDER_GET_CLASS (folder)->
		synchronize_sync (folder, TRUE, cancellable, error);
}

static CamelMimeMessage *
vee_folder_get_message_sync (CamelFolder *folder,
                             const gchar *uid,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelVeeMessageInfo *mi;
	CamelMimeMessage *msg = NULL;

	mi = (CamelVeeMessageInfo *) camel_folder_summary_get (folder->summary, uid);
	if (mi) {
		msg = camel_folder_get_message_sync (
			camel_folder_summary_get_folder (mi->orig_summary), camel_message_info_uid (mi) + 8,
			cancellable, error);
		camel_message_info_free ((CamelMessageInfo *) mi);
	} else {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("No such message %s in %s"), uid,
			camel_folder_get_display_name (folder));
	}

	return msg;
}

static gboolean
vee_folder_refresh_info_sync (CamelFolder *folder,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelVeeFolder *vf = (CamelVeeFolder *) folder;
	CamelVeeFolderPrivate *p = vf->priv;
	GList *node, *list;
	gboolean success = TRUE;

	vee_folder_propagate_skipped_changes (vf);

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
	list = p->folders_changed;
	p->folders_changed = NULL;
	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);

	node = list;
	while (node) {
		CamelFolder *f = node->data;

		if (camel_vee_folder_rebuild_folder (vf, f, error) == -1) {
			success = FALSE;
			break;
		}

		node = node->next;
	}

	g_list_free (list);

	return success;
}

static gboolean
vee_folder_synchronize_sync (CamelFolder *folder,
                             gboolean expunge,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelVeeFolder *vf = (CamelVeeFolder *) folder;
	CamelVeeFolderPrivate *p = vf->priv;
	GList *node;

	vee_folder_propagate_skipped_changes (vf);

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	node = p->folders;
	while (node) {
		GError *local_error = NULL;
		CamelFolder *f = node->data;

		if (!camel_folder_synchronize_sync (f, expunge, cancellable, &local_error)) {
			if (local_error && strncmp (local_error->message, "no such table", 13) != 0 && error && !*error) {
				const gchar *desc;

				desc = camel_folder_get_description (f);
				g_warning ("%s", local_error->message);
				g_propagate_prefixed_error (
					error, local_error,
					_("Error storing '%s': "), desc);
			} else
				g_clear_error (&local_error);
		}

		/* auto update vfolders shouldn't need a rebuild */
/*		if ((vf->flags & CAMEL_STORE_VEE_FOLDER_AUTO) == 0 */
/*		    && camel_vee_folder_rebuild_folder (vf, f, ex) == -1) */
/*			break; */

		node = node->next;
	}

	if (!CAMEL_IS_VTRASH_FOLDER (vf)) {
		/* Cleanup Junk/Trash uids */
		CamelStore *parent_store;
		const gchar *full_name;
		GList *del = NULL;
		gint i;
		GPtrArray *known_uids;

		camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
		known_uids = camel_folder_summary_get_array (folder->summary);
		for (i = 0; known_uids && i < known_uids->len; i++) {
			CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *) camel_folder_summary_get (folder->summary, g_ptr_array_index (known_uids, i));
			if (mi->old_flags & CAMEL_MESSAGE_DELETED) {
				del = g_list_prepend (del, (gpointer) camel_pstring_strdup (((CamelMessageInfo *) mi)->uid));
				camel_folder_summary_remove_uid (folder->summary, ((CamelMessageInfo *) mi)->uid);

			}
			camel_message_info_free (mi);
		}
		camel_folder_summary_free_array (known_uids);

		full_name = camel_folder_get_full_name (folder);
		parent_store = camel_folder_get_parent_store (folder);
		camel_db_delete_vuids (parent_store->cdb_w, full_name, "", del, NULL);
		g_list_foreach (del, (GFunc) camel_pstring_free, NULL);
		g_list_free (del);
	}
	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	camel_object_state_write (CAMEL_OBJECT (vf));

	return TRUE;
}

static gboolean
vee_folder_transfer_messages_to_sync (CamelFolder *folder,
                                      GPtrArray *uids,
                                      CamelFolder *dest,
                                      gboolean delete_originals,
                                      GPtrArray **transferred_uids,
                                      GCancellable *cancellable,
                                      GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Cannot copy or move messages into a Virtual Folder"));

	return FALSE;
}

static void
vee_folder_set_expression (CamelVeeFolder *vee_folder,
                           const gchar *query)
{
	CamelVeeFolderPrivate *p = vee_folder->priv;
	GList *node;

	camel_vee_folder_lock (vee_folder, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	/* no change, do nothing */
	if ((vee_folder->expression && query && strcmp (vee_folder->expression, query) == 0)
	    || (vee_folder->expression == NULL && query == NULL)) {
		camel_vee_folder_unlock (vee_folder, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
		return;
	}

	/* Recreate the table when the query changes, only if we are not setting it first */
	if (vee_folder->expression) {
		CamelFolderSummary *summary;
		CamelStore *parent_store;
		CamelFolder *folder;
		const gchar *full_name;

		folder = CAMEL_FOLDER (vee_folder);
		full_name = camel_folder_get_full_name (folder);
		parent_store = camel_folder_get_parent_store (folder);
		summary = folder->summary;

		camel_folder_summary_clear (summary, NULL);
		camel_db_recreate_vfolder (parent_store->cdb_w, full_name, NULL);
	}

	g_free (vee_folder->expression);
	if (query)
		vee_folder->expression = g_strdup (query);

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;

		if (camel_vee_folder_rebuild_folder (vee_folder, f, NULL) == -1)
			break;

		node = node->next;
	}

	camel_vee_folder_lock (vee_folder, CAMEL_VEE_FOLDER_CHANGED_LOCK);
	g_list_free (p->folders_changed);
	p->folders_changed = NULL;
	camel_vee_folder_unlock (vee_folder, CAMEL_VEE_FOLDER_CHANGED_LOCK);

	camel_vee_folder_unlock (vee_folder, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
}

static void
vee_folder_add_folder (CamelVeeFolder *vee_folder,
                       CamelFolder *sub)
{
	vee_folder_rebuild_folder (vee_folder, sub, NULL);
}

static void
vee_folder_remove_folder_helper (CamelVeeFolder *vf,
                                 CamelFolder *source)
{
	gint i, n, still = FALSE;
	gchar *oldkey;
	CamelFolder *folder = (CamelFolder *) vf;
	gchar hash[8];
	CamelFolderChangeInfo *vf_changes = NULL, *unmatched_changes = NULL;
	gpointer oldval;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vf->parent_vee_store ? vf->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = source->summary;
	gint killun = FALSE;
	GPtrArray *known_uids;

	if (vf == folder_unmatched)
		return;

	if ((source->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED))
		killun = TRUE;

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	if (folder_unmatched != NULL) {
		/* check if this folder is still to be part of unmatched */
		if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !killun) {
			camel_vee_folder_lock (folder_unmatched, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
			still = g_list_find (folder_unmatched->priv->folders, source) != NULL;
			camel_vee_folder_unlock (folder_unmatched, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
			camel_vee_folder_hash_folder (source, hash);
		}

		camel_vee_folder_lock (folder_unmatched, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

		/* See if we just blow all uid's from this folder away from unmatched, regardless */
		if (killun) {
			camel_folder_summary_prepare_fetch_all (((CamelFolder *) folder_unmatched)->summary, NULL);
			known_uids = camel_folder_summary_get_array (((CamelFolder *) folder_unmatched)->summary);
			for (i = 0; known_uids && i < known_uids->len; i++) {
				CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *)
					camel_folder_summary_get (((CamelFolder *) folder_unmatched)->summary, g_ptr_array_index (known_uids, i));

				if (mi) {
					if (mi->orig_summary == ssummary) {
						camel_folder_change_info_remove_uid (folder_unmatched->changes, camel_message_info_uid (mi));
						camel_folder_summary_remove_uid (((CamelFolder *) folder_unmatched)->summary, camel_message_info_uid (mi));
					}
					camel_message_info_free ((CamelMessageInfo *) mi);
				}
			}
			camel_folder_summary_free_array (known_uids);
		}
	}

	/*FIXME: This can be optimized a lot like, searching for UID in the summary uids */
	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
	known_uids = camel_folder_summary_get_array (folder->summary);
	for (i = 0; known_uids && i < known_uids->len; i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *) camel_folder_summary_get (folder->summary, g_ptr_array_index (known_uids, i));
		if (mi) {
			if (mi->orig_summary == ssummary) {
				const gchar *uid = camel_message_info_uid (mi);

				camel_folder_change_info_remove_uid (vf->changes, uid);
				camel_folder_summary_remove_uid (folder->summary, uid);

				if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && folder_unmatched != NULL) {
					if (still) {
						if (g_hash_table_lookup_extended (unmatched_uids, uid, (gpointer *) &oldkey, &oldval)) {
							n = GPOINTER_TO_INT (oldval);
							if (n == 1) {
								g_hash_table_remove (unmatched_uids, oldkey);
								if (vee_folder_add_uid_test (folder_unmatched, source, oldkey + 8, hash)) {
									camel_folder_change_info_add_uid (folder_unmatched->changes, oldkey);
								}
								g_free (oldkey);
							} else {
								g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n - 1));
							}
						}
					} else {
						if (g_hash_table_lookup_extended (unmatched_uids, camel_message_info_uid (mi), (gpointer *) &oldkey, &oldval)) {
							g_hash_table_remove (unmatched_uids, oldkey);
							g_free (oldkey);
						}
					}
				}
			}
			camel_message_info_free ((CamelMessageInfo *) mi);
		}
	}
	camel_folder_summary_free_array (known_uids);

	if (folder_unmatched) {
		if (camel_folder_change_info_changed (folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new ();
		}

		camel_vee_folder_unlock (folder_unmatched, CAMEL_VEE_FOLDER_SUMMARY_LOCK);
	}

	if (camel_folder_change_info_changed (vf->changes)) {
		vf_changes = vf->changes;
		vf->changes = camel_folder_change_info_new ();
	}

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	if (unmatched_changes) {
		camel_folder_changed (
			CAMEL_FOLDER (folder_unmatched), unmatched_changes);
		camel_folder_change_info_free (unmatched_changes);
	}

	if (vf_changes) {
		camel_folder_changed (CAMEL_FOLDER (vf), vf_changes);
		camel_folder_change_info_free (vf_changes);
	}
}

static void
vee_folder_remove_folder (CamelVeeFolder *vee_folder,
                          CamelFolder *sub)
{
	gchar *shash, hash[8];
	CamelVeeFolder *folder_unmatched = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->folder_unmatched : NULL;

	camel_vee_folder_hash_folder (sub, hash);
	vee_folder_remove_folder_helper (vee_folder, sub);
	shash = g_strdup_printf (
		"%c%c%c%c%c%c%c%c",
		hash[0], hash[1], hash[2], hash[3],
		hash[4], hash[5], hash[6], hash[7]);
	if (g_hash_table_lookup (vee_folder->hashes, shash)) {
		g_hash_table_remove (vee_folder->hashes, shash);
	}

	if (folder_unmatched && g_hash_table_lookup (folder_unmatched->hashes, shash)) {
		g_hash_table_remove (folder_unmatched->hashes, shash);
	}

	g_free (shash);
}

static gint
vee_folder_rebuild_folder (CamelVeeFolder *vee_folder,
                           CamelFolder *source,
                           GError **error)
{
	GPtrArray *match = NULL, *all;
	GHashTable *allhash, *matchhash, *fullhash;
	GList *del_list = NULL;
	CamelFolder *folder = (CamelFolder *) vee_folder;
	gint i, n, count;
	struct _update_data u;
	CamelFolderChangeInfo *vee_folder_changes = NULL, *unmatched_changes = NULL;
	CamelVeeFolder *folder_unmatched = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->folder_unmatched : NULL;
	GHashTable *unmatched_uids = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->unmatched_uids : NULL;
	CamelFolderSummary *ssummary = source->summary;
	gboolean rebuilded = FALSE;
	gchar *shash;
	GPtrArray *known_uids;

	/* Since the source of a correlating vfolder has to be requeried in
	 * full every time it changes, caching the results in the db is not
	 * worth the effort.  Thus, DB use is conditioned on !correlating. */
	gboolean correlating = expression_is_correlating (vee_folder->expression);

	if (vee_folder == folder_unmatched)
		return 0;

	camel_vee_folder_hash_folder (source, u.hash);
	shash = g_strdup_printf ("%c%c%c%c%c%c%c%c", u.hash[0], u.hash[1], u.hash[2], u.hash[3], u.hash[4], u.hash[5], u.hash[6], u.hash[7]);
	if (!g_hash_table_lookup (vee_folder->hashes, shash)) {
		g_hash_table_insert (vee_folder->hashes, g_strdup (shash), source->summary);
	}
	if (folder_unmatched && !g_hash_table_lookup (folder_unmatched->hashes, shash)) {
		g_hash_table_insert (folder_unmatched->hashes, g_strdup (shash), source->summary);
	}

	/* if we have no expression, or its been cleared, then act as if no matches */
	if (vee_folder->expression == NULL) {
		match = g_ptr_array_new ();
	} else {
		if (!correlating) {
			/* Load the folder results from the DB. */
			match = camel_vee_summary_get_ids ((CamelVeeSummary *) folder->summary, u.hash);
		}
		if (correlating ||
			/* We take this to mean the results have not been cached.
			 * XXX: It will also trigger if the result set is empty. */
			match == NULL) {
			match = camel_folder_search_by_expression (source, vee_folder->expression, error);
			if (match == NULL) /* Search failed */
				return 0;
			rebuilded = TRUE;
		}

	}

	u.source = source;
	u.vee_folder = vee_folder;
	u.folder_unmatched = folder_unmatched;
	u.unmatched_uids = unmatched_uids;
	u.rebuilt = rebuilded;
	u.correlating = correlating;

	camel_vee_folder_lock (vee_folder, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	/* we build 2 hash tables, one for all uid's not matched, the
	 * other for all matched uid's, we just ref the real memory */
	matchhash = g_hash_table_new (g_str_hash, g_str_equal);
	for (i = 0; i < match->len; i++)
		g_hash_table_insert (matchhash, match->pdata[i], GINT_TO_POINTER (1));

	allhash = g_hash_table_new (g_str_hash, g_str_equal);
	fullhash = g_hash_table_new (g_str_hash, g_str_equal);
	all = camel_folder_summary_get_array (source->summary);
	for (i = 0; i < all->len; i++) {
		if (g_hash_table_lookup (matchhash, all->pdata[i]) == NULL)
			g_hash_table_insert (allhash, all->pdata[i], GINT_TO_POINTER (1));
		g_hash_table_insert (fullhash, all->pdata[i], GINT_TO_POINTER (1));

	}
	/* remove uids that can't be found in the source folder */
	count = match->len;
	for (i = 0; i < count; i++) {
		if (!g_hash_table_lookup (fullhash, match->pdata[i])) {
			g_hash_table_remove (matchhash, match->pdata[i]);
			del_list = g_list_prepend (del_list, match->pdata[i]); /* Free the original */
			g_ptr_array_remove_index_fast (match, i);
			i--;
			count--;
			continue;
		}
	}

	if (folder_unmatched != NULL)
		camel_vee_folder_lock (folder_unmatched, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	/* scan, looking for "old" uid's to be removed. "old" uid's
	 * are those that are from previous added sources (not in
	 * current source) */
	camel_folder_summary_prepare_fetch_all (folder->summary, NULL);
	known_uids = camel_folder_summary_get_array (folder->summary);
	for (i = 0; known_uids && i < known_uids->len; i++) {
		CamelVeeMessageInfo *mi = (CamelVeeMessageInfo *) camel_folder_summary_get (folder->summary, g_ptr_array_index (known_uids, i));

		if (mi) {
			if (mi->orig_summary == ssummary) {
				gchar *uid = (gchar *) camel_message_info_uid (mi), *oldkey;
				gpointer oldval;

				if (g_hash_table_lookup (matchhash, uid + 8) == NULL) {
					camel_folder_change_info_remove_uid (vee_folder->changes, camel_message_info_uid (mi));
					camel_folder_summary_remove_uid (folder->summary, uid);

					if (!CAMEL_IS_VEE_FOLDER (source)
					    && unmatched_uids != NULL
					    && g_hash_table_lookup_extended (unmatched_uids, uid, (gpointer *) &oldkey, &oldval)) {
						n = GPOINTER_TO_INT (oldval);
						if (n == 1) {
							g_hash_table_remove (unmatched_uids, oldkey);
							g_free (oldkey);
						} else {
							g_hash_table_insert (unmatched_uids, oldkey, GINT_TO_POINTER (n - 1));
						}
					}
				} else {
					g_hash_table_remove (matchhash, uid + 8);
				}
			}
			camel_message_info_free ((CamelMessageInfo *) mi);
		}
	}
	camel_folder_summary_free_array (known_uids);

	if (rebuilded && !correlating)
		u.message_uids = g_queue_new ();
	else
		u.message_uids = NULL;

	/* now matchhash contains any new uid's, add them, etc */
	g_hash_table_foreach (matchhash, (GHFunc) folder_added_uid, &u);

	if (u.message_uids != NULL) {
		CamelStore *parent_store;
		const gchar *full_name;
		gchar *uid;

		full_name = camel_folder_get_full_name (folder);
		parent_store = camel_folder_get_parent_store (folder);

		camel_db_begin_transaction (parent_store->cdb_w, NULL);

		while ((uid = g_queue_pop_head (u.message_uids)) != NULL) {
			camel_db_add_to_vfolder_transaction (
				parent_store->cdb_w, full_name, uid, NULL);
			g_free (uid);
		}

		camel_db_end_transaction (parent_store->cdb_w, NULL);

		g_queue_free (u.message_uids);
		u.message_uids = NULL;
	}

	if (folder_unmatched != NULL) {
		/* scan unmatched, remove any that have vanished, etc */
		GPtrArray *known_uids;

		known_uids = camel_folder_summary_get_array (((CamelFolder *) folder_unmatched)->summary);
		if (known_uids != NULL) {
			for (i = 0; i < known_uids->len; i++) {
				const gchar *uid = g_ptr_array_index (known_uids, i);

				if (uid) {
					if (strncmp (uid, u.hash, 8) == 0) {
						if (g_hash_table_lookup (allhash, uid + 8) == NULL) {
							/* no longer exists at all, just remove it entirely */
							camel_folder_summary_remove_uid (((CamelFolder *) folder_unmatched)->summary, uid);
							camel_folder_change_info_remove_uid (folder_unmatched->changes, uid);
						} else {
							g_hash_table_remove (allhash, uid + 8);
						}
					}
				}
			}
			camel_folder_summary_free_array (known_uids);
		}

		/* now allhash contains all potentially new uid's for the unmatched folder, process */
		if (!CAMEL_IS_VEE_FOLDER (source)) {

			u.message_uids = g_queue_new ();
			g_hash_table_foreach (allhash, (GHFunc) unmatched_check_uid, &u);

			if (!g_queue_is_empty (u.message_uids)) {
				CamelStore *parent_store;
				const gchar *full_name;
				gchar *uid;

				full_name = camel_folder_get_full_name (CAMEL_FOLDER (u.folder_unmatched));
				parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (u.folder_unmatched));

				camel_db_begin_transaction (parent_store->cdb_w, NULL);

				while ((uid = g_queue_pop_head (u.message_uids)) != NULL) {
					camel_db_add_to_vfolder_transaction (
							parent_store->cdb_w, full_name, uid, NULL);
					g_free (uid);
				}

				camel_db_end_transaction (parent_store->cdb_w, NULL);
			}

			g_queue_free (u.message_uids);
			u.message_uids = NULL;
		}

		/* copy any changes so we can raise them outside the lock */
		if (camel_folder_change_info_changed (folder_unmatched->changes)) {
			unmatched_changes = folder_unmatched->changes;
			folder_unmatched->changes = camel_folder_change_info_new ();
		}

		camel_vee_folder_unlock (folder_unmatched, CAMEL_VEE_FOLDER_SUMMARY_LOCK);
	}

	if (camel_folder_change_info_changed (vee_folder->changes)) {
		vee_folder_changes = vee_folder->changes;
		vee_folder->changes = camel_folder_change_info_new ();
	}

	camel_vee_folder_unlock (vee_folder, CAMEL_VEE_FOLDER_SUMMARY_LOCK);

	/* Del the unwanted things from the summary, we don't hold any locks now. */
	if (del_list) {
		if (!correlating) {
			CamelStore *parent_store;
			const gchar *full_name;

			full_name = camel_folder_get_full_name (folder);
			parent_store = camel_folder_get_parent_store (folder);
			camel_db_delete_vuids (
				parent_store->cdb_w,
				full_name, shash, del_list, NULL);
		}

		g_list_foreach (del_list, (GFunc) camel_pstring_free, NULL);
		g_list_free (del_list);
	};

	g_hash_table_destroy (matchhash);
	g_hash_table_destroy (allhash);
	g_hash_table_destroy (fullhash);

	g_free (shash);
	/* if expression not set, we only had a null list */
	if (vee_folder->expression == NULL || !rebuilded) {
		g_ptr_array_foreach (match, (GFunc) camel_pstring_free, NULL);
		g_ptr_array_free (match, TRUE);
	} else
		camel_folder_search_free (source, match);
	camel_folder_summary_free_array (all);

	if (unmatched_changes) {
		camel_folder_changed (
			CAMEL_FOLDER (folder_unmatched), unmatched_changes);
		camel_folder_change_info_free (unmatched_changes);
	}

	if (vee_folder_changes) {
		camel_folder_changed (
			CAMEL_FOLDER (vee_folder), vee_folder_changes);
		camel_folder_change_info_free (vee_folder_changes);
	}

	return 0;
}

static void
vee_folder_folder_changed (CamelVeeFolder *vee_folder,
                           CamelFolder *sub,
                           CamelFolderChangeInfo *changes)
{
	CamelVeeFolderPrivate *p = vee_folder->priv;
	FolderChangedData *data;
	CamelStore *parent_store;
	CamelSession *session;

	if (p->destroyed)
		return;

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (vee_folder));
	session = camel_service_get_session (CAMEL_SERVICE (parent_store));

	data = g_slice_new0 (FolderChangedData);
	data->changes = camel_folder_change_info_new ();
	camel_folder_change_info_cat (data->changes, changes);
	data->sub = g_object_ref (sub);
	data->vee_folder = g_object_ref (vee_folder);

	camel_session_submit_job (
		session, (CamelSessionCallback)
		folder_changed_change, data,
		(GDestroyNotify) folder_changed_data_free);
}

static void
vee_folder_folder_renamed (CamelVeeFolder *vee_folder,
                           CamelFolder *f,
                           const gchar *old)
{
	gchar hash[8];
	CamelVeeFolder *folder_unmatched = vee_folder->parent_vee_store ? vee_folder->parent_vee_store->folder_unmatched : NULL;

	/* TODO: This could probably be done in another thread, tho it is pretty quick/memory bound */

	/* Life just got that little bit harder, if the folder is renamed, it means it breaks all of our uid's.
	 * We need to remove the old uid's, fix them up, then release the new uid's, for the uid's that match this folder */

	camel_vee_folder_hash_folder (f, hash);

	subfolder_renamed_update (vee_folder, f, hash);
	if (folder_unmatched != NULL)
		subfolder_renamed_update (folder_unmatched, f, hash);
}

static void
camel_vee_folder_class_init (CamelVeeFolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	g_type_class_add_private (class, sizeof (CamelVeeFolderPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = vee_folder_dispose;
	object_class->finalize = vee_folder_finalize;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->search_by_expression = vee_folder_search_by_expression;
	folder_class->search_by_uids = vee_folder_search_by_uids;
	folder_class->count_by_expression = vee_folder_count_by_expression;
	folder_class->delete = vee_folder_delete;
	folder_class->freeze = vee_folder_freeze;
	folder_class->thaw = vee_folder_thaw;
	folder_class->append_message_sync = vee_folder_append_message_sync;
	folder_class->expunge_sync = vee_folder_expunge_sync;
	folder_class->get_message_sync = vee_folder_get_message_sync;
	folder_class->refresh_info_sync = vee_folder_refresh_info_sync;
	folder_class->synchronize_sync = vee_folder_synchronize_sync;
	folder_class->transfer_messages_to_sync = vee_folder_transfer_messages_to_sync;

	class->set_expression = vee_folder_set_expression;
	class->add_folder = vee_folder_add_folder;
	class->remove_folder = vee_folder_remove_folder;
	class->rebuild_folder = vee_folder_rebuild_folder;
	class->folder_changed = vee_folder_folder_changed;
	class->folder_renamed = vee_folder_folder_renamed;
}

static void
camel_vee_folder_init (CamelVeeFolder *vee_folder)
{
	CamelFolder *folder = CAMEL_FOLDER (vee_folder);

	vee_folder->priv = G_TYPE_INSTANCE_GET_PRIVATE (
		vee_folder, CAMEL_TYPE_VEE_FOLDER, CamelVeeFolderPrivate);

	folder->folder_flags |= (CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY |
				 CAMEL_FOLDER_HAS_SEARCH_CAPABILITY);

	/* FIXME: what to do about user flags if the subfolder doesn't support them? */
	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN;

	vee_folder->changes = camel_folder_change_info_new ();
	vee_folder->search = camel_folder_search_new ();
	vee_folder->hashes = g_hash_table_new_full (
		g_str_hash, g_str_equal, g_free, NULL);

	/* Loaded is no longer used.*/
	vee_folder->loaded = NULL;
	vee_folder->deleted = FALSE;
	vee_folder->priv->summary_lock = g_mutex_new ();
	vee_folder->priv->subfolder_lock = g_mutex_new ();
	vee_folder->priv->changed_lock = g_mutex_new ();
	vee_folder->priv->ignore_changed = g_hash_table_new (g_direct_hash, g_direct_equal);
	vee_folder->priv->skipped_changes = g_hash_table_new (g_direct_hash, g_direct_equal);
}

void
camel_vee_folder_construct (CamelVeeFolder *vf,
                            guint32 flags)
{
	CamelFolder *folder = (CamelFolder *) vf;
	CamelStore *parent_store;

	vf->flags = flags;

	folder->summary = camel_vee_summary_new (folder);

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (vf));

	if (CAMEL_IS_VEE_STORE (parent_store))
		vf->parent_vee_store = CAMEL_VEE_STORE (parent_store);
}

/**
 * camel_vee_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @full: the full path to the vfolder.
 * @flags: flags of some kind
 *
 * Create a new CamelVeeFolder object.
 *
 * Returns: A new CamelVeeFolder widget.
 **/
CamelFolder *
camel_vee_folder_new (CamelStore *parent_store,
                      const gchar *full,
                      guint32 flags)
{
	CamelVeeFolder *vf;
	gchar *tmp;

	g_return_val_if_fail (CAMEL_IS_STORE (parent_store), NULL);
	g_return_val_if_fail (full != NULL, NULL);

	if (CAMEL_IS_VEE_STORE (parent_store) && strcmp (full, CAMEL_UNMATCHED_NAME) == 0) {
		vf = ((CamelVeeStore *) parent_store)->folder_unmatched;
		g_object_ref (vf);
	} else {
		const gchar *name = strrchr (full, '/');

		if (name == NULL)
			name = full;
		else
			name++;
		vf = g_object_new (
			CAMEL_TYPE_VEE_FOLDER,
			"display-name", name, "full-name", full,
			"parent-store", parent_store, NULL);
		camel_vee_folder_construct (vf, flags);
	}

	d (printf ("returning folder %s %p, count = %d\n", full, vf, camel_folder_get_message_count ((CamelFolder *)vf)));

	if (vf) {
		CamelObject *object = CAMEL_OBJECT (vf);
		CamelURL *url;

		url = camel_service_get_camel_url (CAMEL_SERVICE (parent_store));
		tmp = g_strdup_printf ("%s/%s.cmeta", url->path, full);
		camel_object_set_state_filename (object, tmp);
		g_free (tmp);
		if (camel_object_state_read (object) == -1) {
			/* setup defaults: we have none currently */
		}
	}
	return (CamelFolder *) vf;
}

void
camel_vee_folder_set_expression (CamelVeeFolder *vf,
                                 const gchar *query)
{
	CAMEL_VEE_FOLDER_GET_CLASS (vf)->set_expression (vf, query);
}

/**
 * camel_vee_folder_add_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to add to @vf
 *
 * Adds @sub as a source folder to @vf.
 **/
void
camel_vee_folder_add_folder (CamelVeeFolder *vf,
                             CamelFolder *sub)
{
	CamelVeeFolderPrivate *p = vf->priv;
	gint i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	if (vf == (CamelVeeFolder *) sub) {
		g_warning ("Adding a virtual folder to itself as source, ignored");
		return;
	}

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	/* for normal vfolders we want only unique ones, for unmatched we want them all recorded */
	if (g_list_find (p->folders, sub) == NULL) {
		p->folders = g_list_append (
			p->folders, g_object_ref (sub));

		camel_folder_lock (CAMEL_FOLDER (vf), CAMEL_FOLDER_CHANGE_LOCK);

		/* update the freeze state of 'sub' to match our freeze state */
		for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) vf); i++)
			camel_folder_freeze (sub);

		camel_folder_unlock (CAMEL_FOLDER (vf), CAMEL_FOLDER_CHANGE_LOCK);
	}
	if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0 && !CAMEL_IS_VEE_FOLDER (sub) && folder_unmatched != NULL) {
		CamelVeeFolderPrivate *up = folder_unmatched->priv;
		up->folders = g_list_append (
			up->folders, g_object_ref (sub));

		camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);

		/* update the freeze state of 'sub' to match Unmatched's freeze state */
		for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) folder_unmatched); i++)
			camel_folder_freeze (sub);

		camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
	}

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	g_signal_connect (
		sub, "changed",
		G_CALLBACK (folder_changed), vf);

	g_signal_connect (
		sub, "deleted",
		G_CALLBACK (subfolder_deleted), vf);

	g_signal_connect (
		sub, "renamed",
		G_CALLBACK (folder_renamed), vf);

	CAMEL_VEE_FOLDER_GET_CLASS (vf)->add_folder (vf, sub);
}

/**
 * camel_vee_folder_remove_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to remove from @vf
 *
 * Removed the source folder, @sub, from the virtual folder, @vf.
 **/
void
camel_vee_folder_remove_folder (CamelVeeFolder *vf,
                                CamelFolder *sub)
{
	CamelVeeFolderPrivate *p = vf->priv;
	gint i;
	CamelVeeFolder *folder_unmatched = vf->parent_vee_store ? vf->parent_vee_store->folder_unmatched : NULL;

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
	p->folders_changed = g_list_remove (p->folders_changed, sub);
	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);

	if (g_list_find (p->folders, sub) == NULL) {
		camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
		return;
	}

	g_signal_handlers_disconnect_by_func (sub, folder_changed, vf);
	g_signal_handlers_disconnect_by_func (sub, subfolder_deleted, vf);
	g_signal_handlers_disconnect_by_func (sub, folder_renamed, vf);

	p->folders = g_list_remove (p->folders, sub);

	/* undo the freeze state that we have imposed on this source folder */
	camel_folder_lock (CAMEL_FOLDER (vf), CAMEL_FOLDER_CHANGE_LOCK);
	for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) vf); i++)
		camel_folder_thaw (sub);
	camel_folder_unlock (CAMEL_FOLDER (vf), CAMEL_FOLDER_CHANGE_LOCK);

	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	if (folder_unmatched != NULL) {
		CamelVeeFolderPrivate *up = folder_unmatched->priv;

		camel_vee_folder_lock (folder_unmatched, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
		/* if folder deleted, then blow it away from unmatched always, and remove all refs to it */
		if (sub->folder_flags & CAMEL_FOLDER_HAS_BEEN_DELETED) {
			while (g_list_find (up->folders, sub)) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
			}
		} else if ((vf->flags & CAMEL_STORE_FOLDER_PRIVATE) == 0) {
			if (g_list_find (up->folders, sub) != NULL) {
				up->folders = g_list_remove (up->folders, sub);
				g_object_unref (sub);

				/* undo the freeze state that Unmatched has imposed on this source folder */
				camel_folder_lock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
				for (i = 0; i < camel_folder_get_frozen_count ((CamelFolder *) folder_unmatched); i++)
					camel_folder_thaw (sub);
				camel_folder_unlock (CAMEL_FOLDER (folder_unmatched), CAMEL_FOLDER_CHANGE_LOCK);
			}
		}
		camel_vee_folder_unlock (folder_unmatched, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
	}

	CAMEL_VEE_FOLDER_GET_CLASS (vf)->remove_folder (vf, sub);

	if (CAMEL_IS_VEE_FOLDER (sub))
		return;

	g_object_unref (sub);
}

/**
 * camel_vee_folder_rebuild_folder:
 * @vf: Virtual Folder object
 * @sub: source CamelFolder to add to @vf
 * @error: return location for a #GError, or %NULL
 *
 * Rebuild the folder @sub, if it should be.
 **/
gint
camel_vee_folder_rebuild_folder (CamelVeeFolder *vf,
                                 CamelFolder *sub,
                                 GError **error)
{
	vee_folder_propagate_skipped_changes (vf);

	return CAMEL_VEE_FOLDER_GET_CLASS (vf)->rebuild_folder (vf, sub, error);
}

static void
remove_folders (CamelFolder *folder,
                CamelFolder *foldercopy,
                CamelVeeFolder *vf)
{
	camel_vee_folder_remove_folder (vf, folder);
	g_object_unref (folder);
}

/**
 * camel_vee_folder_set_folders:
 * @vf: a #CamelVeeFolder
 * @folders: (element-type Camel.Folder): list of folders
 *
 * Set the whole list of folder sources on a vee folder.
 **/
void
camel_vee_folder_set_folders (CamelVeeFolder *vf,
                              GList *folders)
{
	CamelVeeFolderPrivate *p = vf->priv;
	GHashTable *remove = g_hash_table_new (NULL, NULL);
	GList *l;
	CamelFolder *folder;

	/* setup a table of all folders we have currently */
	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);
	l = p->folders;
	while (l) {
		g_hash_table_insert (remove, l->data, l->data);
		g_object_ref (l->data);
		l = l->next;
	}
	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_SUBFOLDER_LOCK);

	/* if we already have the folder, ignore it, otherwise add it */
	l = folders;
	while (l) {
		if ((folder = g_hash_table_lookup (remove, l->data))) {
			g_hash_table_remove (remove, folder);
			g_object_unref (folder);
		} else {
			camel_vee_folder_add_folder (vf, l->data);
		}
		l = l->next;
	}

	/* then remove any we still have */
	g_hash_table_foreach (remove, (GHFunc) remove_folders, vf);
	g_hash_table_destroy (remove);
}

/**
 * camel_vee_folder_hash_folder:
 * @folder:
 * @:
 *
 * Create a hash string representing the folder name, which should be
 * unique, and remain static for a given folder.
 **/
void
camel_vee_folder_hash_folder (CamelFolder *folder,
                              gchar buffer[8])
{
	CamelStore *parent_store;
	GChecksum *checksum;
	guint8 *digest;
	gsize length;
	gint state = 0, save = 0;
	const gchar *full_name;
	gchar *tmp;
	gint i;

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	parent_store = camel_folder_get_parent_store (folder);
	tmp = camel_service_get_url (CAMEL_SERVICE (parent_store));
	g_checksum_update (checksum, (guchar *) tmp, -1);
	g_free (tmp);

	full_name = camel_folder_get_full_name (folder);
	g_checksum_update (checksum, (guchar *) full_name, -1);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	g_base64_encode_step (digest, 6, FALSE, buffer, &state, &save);
	g_base64_encode_close (FALSE, buffer, &state, &save);

	for (i = 0; i < 8; i++) {
		if (buffer[i] == '+')
			buffer[i] = '.';
		if (buffer[i] == '/')
			buffer[i] = '_';
	}
}

/**
 * camel_vee_folder_get_location:
 * @vf: a #CamelVeeFolder
 * @vinfo:
 * @realuid: (out) (allow-none): if not NULL, set to the uid of the real message, must be
 * g_free'd by caller.
 *
 * Find the real folder (and uid)
 *
 * Returns: (transfer none): a #CamelFolder
 **/
CamelFolder *
camel_vee_folder_get_location (CamelVeeFolder *vf,
                               const CamelVeeMessageInfo *vinfo,
                               gchar **realuid)
{
	CamelFolder *folder;

	folder = camel_folder_summary_get_folder (vinfo->orig_summary);

	/* locking?  yes?  no?  although the vfolderinfo is valid when obtained
	 * the folder in it might not necessarily be so ...? */
	if (CAMEL_IS_VEE_FOLDER (folder)) {
		CamelFolder *res;
		const CamelVeeMessageInfo *vfinfo;

		vfinfo = (CamelVeeMessageInfo *) camel_folder_get_message_info (folder, camel_message_info_uid (vinfo) + 8);
		res = camel_vee_folder_get_location ((CamelVeeFolder *) folder, vfinfo, realuid);
		camel_folder_free_message_info (folder, (CamelMessageInfo *) vfinfo);
		return res;
	} else {
		if (realuid)
			*realuid = g_strdup (camel_message_info_uid (vinfo)+8);

		return folder;
	}
}

/**
 * camel_vee_folder_ignore_next_changed_event:
 * @vf: a #CamelVeeFolder
 * @sub: a #CamelFolder folder
 *
 * The next @sub folder's 'changed' event will be silently ignored. This
 * is usually used in virtual folders when the change was done in them,
 * but it is neither vTrash nor vJunk folder. Doing this avoids unnecessary
 * removals of messages which don't satisfy search criteria anymore,
 * which could be done on asynchronous delivery of folder's 'changed' signal.
 * These ignored changes are accumulated and used on folder refresh.
 *
 * Since: 3.2
 **/
void
camel_vee_folder_ignore_next_changed_event (CamelVeeFolder *vf,
                                            CamelFolder *sub)
{
	g_return_if_fail (vf != NULL);
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (vf));
	g_return_if_fail (sub != NULL);

	camel_vee_folder_lock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
	g_hash_table_insert (vf->priv->ignore_changed, sub, GINT_TO_POINTER (1));
	camel_vee_folder_unlock (vf, CAMEL_VEE_FOLDER_CHANGED_LOCK);
}

/**
 * camel_vee_folder_sync_headers:
 *
 * Since: 2.24
 **/
void
camel_vee_folder_sync_headers (CamelFolder *vf,
                               GError **error)
{
	CamelFIRecord * record;
	CamelStore *parent_store;

	/* Save the counts to DB */
	record = summary_header_to_db (vf->summary, error);
	parent_store = camel_folder_get_parent_store (vf);
	camel_db_begin_transaction (parent_store->cdb_w, NULL);
	camel_db_write_folder_info_record (parent_store->cdb_w, record, error);
	camel_db_end_transaction (parent_store->cdb_w, NULL);

	g_free (record->folder_name);
	g_free (record);
}

/**
 * camel_vee_folder_lock:
 * @folder: a #CamelVeeFolder
 * @lock: lock type to lock
 *
 * Locks @folder's @lock. Unlock it with camel_vee_folder_unlock().
 *
 * Since: 2.32
 **/
void
camel_vee_folder_lock (CamelVeeFolder *folder,
                       CamelVeeFolderLock lock)
{
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (folder));

	switch (lock) {
		case CAMEL_VEE_FOLDER_SUMMARY_LOCK:
			g_mutex_lock (folder->priv->summary_lock);
			break;
		case CAMEL_VEE_FOLDER_SUBFOLDER_LOCK:
			g_mutex_lock (folder->priv->subfolder_lock);
			break;
		case CAMEL_VEE_FOLDER_CHANGED_LOCK:
			g_mutex_lock (folder->priv->changed_lock);
			break;
		default:
			g_return_if_reached ();
	}
}

/**
 * camel_vee_folder_unlock:
 * @folder: a #CamelVeeFolder
 * @lock: lock type to unlock
 *
 * Unlocks @folder's @lock, previously locked with camel_vee_folder_lock().
 *
 * Since: 2.32
 **/
void
camel_vee_folder_unlock (CamelVeeFolder *folder,
                         CamelVeeFolderLock lock)
{
	g_return_if_fail (CAMEL_IS_VEE_FOLDER (folder));

	switch (lock) {
		case CAMEL_VEE_FOLDER_SUMMARY_LOCK:
			g_mutex_unlock (folder->priv->summary_lock);
			break;
		case CAMEL_VEE_FOLDER_SUBFOLDER_LOCK:
			g_mutex_unlock (folder->priv->subfolder_lock);
			break;
		case CAMEL_VEE_FOLDER_CHANGED_LOCK:
			g_mutex_unlock (folder->priv->changed_lock);
			break;
		default:
			g_return_if_reached ();
	}
}
