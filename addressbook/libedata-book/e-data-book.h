/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of version 2.1 of the GNU Lesser General Public License as
 * published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Author: Ross Burton <ross@linux.intel.com>
 */

#if !defined (__LIBEDATA_BOOK_H_INSIDE__) && !defined (LIBEDATA_BOOK_COMPILATION)
#error "Only <libedata-book/libedata-book.h> should be included directly."
#endif

#ifndef E_DATA_BOOK_H
#define E_DATA_BOOK_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_DATA_BOOK \
	(e_data_book_get_type ())
#define E_DATA_BOOK(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_DATA_BOOK, EDataBook))
#define E_DATA_BOOK_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_DATA_BOOK, EDataBookClass))
#define E_IS_DATA_BOOK(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_DATA_BOOK))
#define E_IS_DATA_BOOK_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_DATA_BOOK))
#define E_DATA_BOOK_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_DATA_BOOK, EDataBookClass))

G_BEGIN_DECLS

struct _EBookBackend;

typedef struct _EDataBook EDataBook;
typedef struct _EDataBookClass EDataBookClass;
typedef struct _EDataBookPrivate EDataBookPrivate;

struct _EDataBook {
	GObject parent;
	EDataBookPrivate *priv;
};

struct _EDataBookClass {
	GObjectClass parent_class;
};

GQuark e_data_book_error_quark (void);

/**
 * E_DATA_BOOK_ERROR:
 *
 * Since: 2.30
 **/
#define E_DATA_BOOK_ERROR e_data_book_error_quark ()

/**
 * e_data_book_create_error:
 * @status: #EDataBookStatus code
 * @custom_msg: Custom message to use for the error. When NULL,
 *              then uses a default message based on the @status code.
 *
 * Returns: NULL, when the @status is E_DATA_BOOK_STATUS_SUCCESS,
 *          or a newly allocated GError, which should be freed
 *          with g_error_free() call.
 **/
GError *	e_data_book_create_error	(EDataBookStatus status,
						 const gchar *custom_msg);

/**
 * e_data_book_create_error_fmt:
 *
 * Similar as e_data_book_create_error(), only here, instead of custom_msg,
 * is used a printf() format to create a custom_msg for the error.
 **/
GError *	e_data_book_create_error_fmt	(EDataBookStatus status,
						 const gchar *custom_msg_fmt,
						 ...) G_GNUC_PRINTF (2, 3);

const gchar *	e_data_book_status_to_string	(EDataBookStatus status);

/**
 * e_return_data_book_error_if_fail:
 *
 * Since: 2.32
 **/
#define e_return_data_book_error_if_fail(expr, _code)				\
	G_STMT_START {								\
		if (G_LIKELY (expr)) {						\
		} else {							\
			g_log (G_LOG_DOMAIN,					\
				G_LOG_LEVEL_CRITICAL,				\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			g_set_error (error, E_DATA_BOOK_ERROR, (_code),		\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			return;							\
		}								\
	} G_STMT_END

/**
 * e_return_data_book_error_val_if_fail:
 *
 * Same as e_return_data_book_error_if_fail(), only returns FALSE on a failure
 *
 * Since: 3.2
 **/
#define e_return_data_book_error_val_if_fail(expr, _code)			\
	G_STMT_START {								\
		if (G_LIKELY (expr)) {						\
		} else {							\
			g_log (G_LOG_DOMAIN,					\
				G_LOG_LEVEL_CRITICAL,				\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			g_set_error (error, E_DATA_BOOK_ERROR, (_code),		\
				"file %s: line %d (%s): assertion `%s' failed",	\
				__FILE__, __LINE__, G_STRFUNC, #expr);		\
			return FALSE;						\
		}								\
	} G_STMT_END

GType		e_data_book_get_type		(void) G_GNUC_CONST;
EDataBook *	e_data_book_new			(struct _EBookBackend *backend,
						 GDBusConnection *connection,
						 const gchar *object_path,
						 GError **error);
EDataBook *	e_data_book_new_direct		(ESourceRegistry *registry,
						 ESource *source,
						 const gchar *backend_path,
						 const gchar *backend_name,
						 const gchar *config);
struct _EBookBackend *
		e_data_book_get_backend		(EDataBook *book);
GDBusConnection *
		e_data_book_get_connection	(EDataBook *book);
const gchar *	e_data_book_get_object_path	(EDataBook *book);

void		e_data_book_respond_open	(EDataBook *book,
						 guint32 opid,
						 GError *error);
void		e_data_book_respond_refresh	(EDataBook *book,
						 guint32 opid,
						 GError *error);
void		e_data_book_respond_get_backend_property
						(EDataBook *book,
						 guint32 opid,
						 GError *error,
						 const gchar *prop_value);
void		e_data_book_respond_set_backend_property
						(EDataBook *book,
						 guint32 opid,
						 GError *error);
void		e_data_book_respond_create_contacts
						(EDataBook *book,
						 guint32 opid,
						 GError *error,
						 const GSList *contacts);
void		e_data_book_respond_remove_contacts
						(EDataBook *book,
						 guint32 opid,
						 GError *error,
						 const GSList *ids);
void		e_data_book_respond_modify_contacts
						(EDataBook *book,
						 guint32 opid,
						 GError *error,
						 const GSList *contacts);
void		e_data_book_respond_get_contact	(EDataBook *book,
						 guint32 opid,
						 GError *error,
						 const gchar *vcard);
void		e_data_book_respond_get_contact_list
						(EDataBook *book,
						 guint32 opid,
						 GError *error,
						 const GSList *cards);
void		e_data_book_respond_get_contact_list_uids
						(EDataBook *book,
						 guint32 opid,
						 GError *error,
						 const GSList *uids);

void		e_data_book_report_error	(EDataBook *book,
						 const gchar *message);
void		e_data_book_report_readonly	(EDataBook *book,
						 gboolean readonly);
void		e_data_book_report_online	(EDataBook *book,
						 gboolean is_online);
void		e_data_book_report_opened	(EDataBook *book,
						 const GError *error);
void		e_data_book_report_backend_property_changed
						(EDataBook *book,
						 const gchar *prop_name,
						 const gchar *prop_value);

gchar *		e_data_book_string_slist_to_comma_string
						(const GSList *strings);

/* Direct read access apis */
void		e_data_book_open		                (EDataBook *book, gboolean only_if_exists, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_data_book_open_finish                         (EDataBook *book, GAsyncResult *result, GError **error);
gboolean	e_data_book_open_sync		                (EDataBook *book, gboolean only_if_exists, GCancellable *cancellable, GError **error);

void		e_data_book_close		                (EDataBook *book, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_data_book_close_finish                        (EDataBook *book, GAsyncResult *result, GError **error);
gboolean	e_data_book_close_sync		                (EDataBook *book, GCancellable *cancellable, GError **error);

void		e_data_book_get_contact			        (EDataBook *book, const gchar *uid, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_data_book_get_contact_finish                  (EDataBook *book, GAsyncResult *result, EContact **contact, GError **error);
gboolean	e_data_book_get_contact_sync                    (EDataBook *book, const gchar *uid, EContact **contact, GCancellable *cancellable, GError **error);

void		e_data_book_get_contacts			(EDataBook *book, const gchar *sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_data_book_get_contacts_finish                 (EDataBook *book, GAsyncResult *result, GSList **contacts, GError **error);
gboolean	e_data_book_get_contacts_sync			(EDataBook *book, const gchar *sexp, GSList **contacts, GCancellable *cancellable, GError **error);

void		e_data_book_get_contacts_uids			(EDataBook *book, const gchar *sexp, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean	e_data_book_get_contacts_uids_finish		(EDataBook *book, GAsyncResult *result, GSList **contacts_uids, GError **error);
gboolean	e_data_book_get_contacts_uids_sync		(EDataBook *book, const gchar *sexp, GSList **contacts_uids, GCancellable *cancellable, GError **error);


G_END_DECLS

#endif /* E_DATA_BOOK_H */
