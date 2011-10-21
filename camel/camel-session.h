/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-session.h : Abstract class for an email session */

/*
 *
 * Author :
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_SESSION_H
#define CAMEL_SESSION_H

#include <camel/camel-enums.h>
#include <camel/camel-filter-driver.h>
#include <camel/camel-junk-filter.h>
#include <camel/camel-msgport.h>
#include <camel/camel-provider.h>
#include <camel/camel-service.h>

/* Standard GObject macros */
#define CAMEL_TYPE_SESSION \
	(camel_session_get_type ())
#define CAMEL_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SESSION, CamelSession))
#define CAMEL_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SESSION, CamelSessionClass))
#define CAMEL_IS_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SESSION))
#define CAMEL_IS_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SESSION))
#define CAMEL_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SESSION, CamelSessionClass))

G_BEGIN_DECLS

typedef struct _CamelSession CamelSession;
typedef struct _CamelSessionClass CamelSessionClass;
typedef struct _CamelSessionPrivate CamelSessionPrivate;

enum {
	CAMEL_SESSION_PASSWORD_REPROMPT = 1 << 0,
	CAMEL_SESSION_PASSWORD_SECRET = 1 << 2,
	CAMEL_SESSION_PASSWORD_STATIC = 1 << 3,
	CAMEL_SESSION_PASSPHRASE = 1 << 4
};

/**
 * CamelSessionLock:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_SESSION_SESSION_LOCK,
	CAMEL_SESSION_THREAD_LOCK
} CamelSessionLock;

struct _CamelSession {
	CamelObject parent;
	CamelSessionPrivate *priv;
};

/**
 * CamelSessionCallback:
 * @session: a #CamelSession
 * @cancellable: a #CamelOperation cast as a #GCancellable
 * @user_data: data passed to camel_session_submit_job()
 * @error: return location for a #GError
 *
 * This is the callback signature for jobs submitted to the CamelSession
 * via camel_session_submit_job().  The @error pointer is always non-%NULL,
 * so it's safe to dereference to check if a #GError has been set.
 *
 * Since: 3.2
 **/
typedef void	(*CamelSessionCallback)		(CamelSession *session,
						 GCancellable *cancellable,
						 gpointer user_data,
						 GError **error);

struct _CamelSessionClass {
	CamelObjectClass parent_class;

	CamelService *	(*add_service)		(CamelSession *session,
						 const gchar *uid,
						 const gchar *uri_string,
						 CamelProviderType type,
						 GError **error);
	gchar *		(*get_password)		(CamelSession *session,
						 CamelService *service,
						 const gchar *prompt,
						 const gchar *item,
						 guint32 flags,
						 GError **error);
	gboolean	(*forget_password)	(CamelSession *session,
						 CamelService *service,
						 const gchar *item,
						 GError **error);
	gboolean	(*alert_user)		(CamelSession *session,
						 CamelSessionAlertType type,
						 const gchar *prompt,
						 gboolean cancel);
	CamelFilterDriver *
			(*get_filter_driver)	(CamelSession *session,
						 const gchar *type,
						 GError **error);
	gboolean	(*lookup_addressbook)	(CamelSession *session,
						 const gchar *name);
	gboolean	(*forward_to)		(CamelSession *session,
						 CamelFolder *folder,
						 CamelMimeMessage *message,
						 const gchar *address,
						 GError **error);
	void		(*get_socks_proxy)	(CamelSession *session,
						 const gchar *for_host,
						 gchar **host_ret,
						 gint *port_ret);

	/* Synchronous I/O Methods */
	gboolean	(*authenticate_sync)	(CamelSession *session,
						 CamelService *service,
						 const gchar *mechanism,
						 GCancellable *cancellable,
						 GError **error);

	/* Asynchronous I/O Methods (all have defaults) */
	void		(*authenticate)		(CamelSession *session,
						 CamelService *service,
						 const gchar *mechanism,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
	gboolean	(*authenticate_finish)	(CamelSession *session,
						 GAsyncResult *result,
						 GError **error);

	/* Signals */
	void		(*job_started)		(CamelSession *session,
						 GCancellable *cancellable);
	void		(*job_finished)		(CamelSession *session,
						 GCancellable *cancellable,
						 const GError *error);
};

GType		camel_session_get_type		(void);
const gchar *	camel_session_get_user_data_dir	(CamelSession *session);
const gchar *	camel_session_get_user_cache_dir
						(CamelSession *session);
void            camel_session_get_socks_proxy   (CamelSession *session,
						 const gchar *for_host,
						 gchar **host_ret,
						 gint *port_ret);
CamelService *	camel_session_add_service	(CamelSession *session,
						 const gchar *uid,
						 const gchar *uri_string,
						 CamelProviderType type,
						 GError **error);
gboolean	camel_session_remove_service	(CamelSession *session,
						 const gchar *uid);
CamelService *	camel_session_get_service	(CamelSession *session,
						 const gchar *uid);
CamelService *	camel_session_get_service_by_url
						(CamelSession *session,
						 CamelURL *url,
						 CamelProviderType type);
GList *		camel_session_list_services	(CamelSession *session);
void		camel_session_remove_services	(CamelSession *session);
gchar *		camel_session_get_password	(CamelSession *session,
						 CamelService *service,
						 const gchar *prompt,
						 const gchar *item,
						 guint32 flags,
						 GError **error);
gboolean	camel_session_forget_password	(CamelSession *session,
						 CamelService *service,
						 const gchar *item,
						 GError **error);
gboolean	camel_session_alert_user	(CamelSession *session,
						 CamelSessionAlertType type,
						 const gchar *prompt,
						 gboolean cancel);
gchar *		camel_session_build_password_prompt
						(const gchar *type,
						 const gchar *user,
						 const gchar *host);
gboolean	camel_session_get_online	(CamelSession *session);
void		camel_session_set_online	(CamelSession *session,
						 gboolean online);
CamelFilterDriver *
		camel_session_get_filter_driver	(CamelSession *session,
						 const gchar *type,
						 GError **error);
CamelJunkFilter *
		camel_session_get_junk_filter	(CamelSession *session);
void		camel_session_set_junk_filter	(CamelSession *session,
						 CamelJunkFilter *junk_filter);
gboolean	camel_session_get_check_junk	(CamelSession *session);
void		camel_session_set_check_junk	(CamelSession *session,
						 gboolean check_junk);
void		camel_session_submit_job	(CamelSession *session,
						 CamelSessionCallback callback,
						 gpointer user_data,
						 GDestroyNotify notify);
gboolean	camel_session_get_network_available
						(CamelSession *session);
void		camel_session_set_network_available
						(CamelSession *session,
						 gboolean network_available);
const GHashTable *
		camel_session_get_junk_headers	(CamelSession *session);
void		camel_session_set_junk_headers	(CamelSession *session,
						 const gchar **headers,
						 const gchar **values,
						 gint len);
gboolean	camel_session_lookup_addressbook (CamelSession *session,
						 const gchar *name);
gboolean	camel_session_forward_to	(CamelSession *session,
						 CamelFolder *folder,
						 CamelMimeMessage *message,
						 const gchar *address,
						 GError **error);
void		camel_session_lock		(CamelSession *session,
						 CamelSessionLock lock);
void		camel_session_unlock		(CamelSession *session,
						 CamelSessionLock lock);

gboolean	camel_session_authenticate_sync	(CamelSession *session,
						 CamelService *service,
						 const gchar *mechanism,
						 GCancellable *cancellable,
						 GError **error);
void		camel_session_authenticate	(CamelSession *session,
						 CamelService *service,
						 const gchar *mechanism,
						 gint io_priority,
						 GCancellable *cancellable,
						 GAsyncReadyCallback callback,
						 gpointer user_data);
gboolean	camel_session_authenticate_finish
						(CamelSession *session,
						 GAsyncResult *result,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_SESSION_H */
