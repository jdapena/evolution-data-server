/*
 * e-collection-backend.h
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

#ifndef E_COLLECTION_BACKEND_H
#define E_COLLECTION_BACKEND_H

#include <libebackend/e-backend.h>

/* Standard GObject macros */
#define E_TYPE_COLLECTION_BACKEND \
	(e_collection_backend_get_type ())
#define E_COLLECTION_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_COLLECTION_BACKEND, ECollectionBackend))
#define E_COLLECTION_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_COLLECTION_BACKEND, ECollectionBackendClass))
#define E_IS_COLLECTION_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_COLLECTION_BACKEND))
#define E_IS_COLLECTION_BACKEND_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_COLLECTION_BACKEND))
#define E_COLLECTION_BACKEND_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_COLLECTION_BACKEND, ECollectionBackendClass))

G_BEGIN_DECLS

struct _ESourceRegistryServer;

typedef struct _ECollectionBackend ECollectionBackend;
typedef struct _ECollectionBackendClass ECollectionBackendClass;
typedef struct _ECollectionBackendPrivate ECollectionBackendPrivate;

/**
 * ECollectionBackend:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.6
 **/
struct _ECollectionBackend {
	EBackend parent;
	ECollectionBackendPrivate *priv;
};

struct _ECollectionBackendClass {
	EBackendClass parent_class;

	/* Methods */
	void		(*populate)		(ECollectionBackend *backend);

	/* Signals */
	void		(*child_added)		(ECollectionBackend *backend,
						 ESource *child_source);
	void		(*child_removed)	(ECollectionBackend *backend,
						 ESource *child_source);

	gpointer reserved[16];
};

GType		e_collection_backend_get_type	(void) G_GNUC_CONST;
ESource *	e_collection_backend_new_child	(ECollectionBackend *backend,
						 const gchar *resource_id);
struct _ESourceRegistryServer *
		e_collection_backend_ref_server	(ECollectionBackend *backend);
GList *		e_collection_backend_list_calendar_sources
						(ECollectionBackend *backend);
GList *		e_collection_backend_list_contacts_sources
						(ECollectionBackend *backend);
GList *		e_collection_backend_list_mail_sources
						(ECollectionBackend *backend);

G_END_DECLS

#endif /* E_COLLECTION_BACKEND_H */
