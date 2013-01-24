/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2012 Intel Corporation
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
 * Author: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "e-gdbus-book-direct.h"

#include <string.h>
#ifdef G_OS_UNIX
#  include <gio/gunixfdlist.h>
#endif

typedef struct
{
  GDBusArgInfo parent_struct;
  gboolean use_gvariant;
} _ExtendedGDBusArgInfo;

typedef struct
{
  GDBusMethodInfo parent_struct;
  const gchar *signal_name;
  gboolean pass_fdlist;
} _ExtendedGDBusMethodInfo;

typedef struct
{
  GDBusSignalInfo parent_struct;
  const gchar *signal_name;
} _ExtendedGDBusSignalInfo;

typedef struct
{
  GDBusPropertyInfo parent_struct;
  const gchar *hyphen_name;
  gboolean use_gvariant;
} _ExtendedGDBusPropertyInfo;

typedef struct
{
  GDBusInterfaceInfo parent_struct;
  const gchar *hyphen_name;
} _ExtendedGDBusInterfaceInfo;

typedef struct
{
  const _ExtendedGDBusPropertyInfo *info;
  guint prop_id;
  GValue orig_value; /* the value before the change */
} ChangedProperty;

static void
_changed_property_free (ChangedProperty *data)
{
  g_value_unset (&data->orig_value);
  g_free (data);
}

static gboolean
_g_strv_equal0 (gchar **a, gchar **b)
{
  gboolean ret = FALSE;
  guint n;
  if (a == NULL && b == NULL)
    {
      ret = TRUE;
      goto out;
    }
  if (a == NULL || b == NULL)
    goto out;
  if (g_strv_length (a) != g_strv_length (b))
    goto out;
  for (n = 0; a[n] != NULL; n++)
    if (g_strcmp0 (a[n], b[n]) != 0)
      goto out;
  ret = TRUE;
out:
  return ret;
}

static gboolean
_g_variant_equal0 (GVariant *a, GVariant *b)
{
  gboolean ret = FALSE;
  if (a == NULL && b == NULL)
    {
      ret = TRUE;
      goto out;
    }
  if (a == NULL || b == NULL)
    goto out;
  ret = g_variant_equal (a, b);
out:
  return ret;
}

G_GNUC_UNUSED static gboolean
_g_value_equal (const GValue *a, const GValue *b)
{
  gboolean ret = FALSE;
  g_assert (G_VALUE_TYPE (a) == G_VALUE_TYPE (b));
  switch (G_VALUE_TYPE (a))
    {
      case G_TYPE_BOOLEAN:
        ret = (g_value_get_boolean (a) == g_value_get_boolean (b));
        break;
      case G_TYPE_UCHAR:
        ret = (g_value_get_uchar (a) == g_value_get_uchar (b));
        break;
      case G_TYPE_INT:
        ret = (g_value_get_int (a) == g_value_get_int (b));
        break;
      case G_TYPE_UINT:
        ret = (g_value_get_uint (a) == g_value_get_uint (b));
        break;
      case G_TYPE_INT64:
        ret = (g_value_get_int64 (a) == g_value_get_int64 (b));
        break;
      case G_TYPE_UINT64:
        ret = (g_value_get_uint64 (a) == g_value_get_uint64 (b));
        break;
      case G_TYPE_DOUBLE:
        {
          /* Avoid -Wfloat-equal warnings by doing a direct bit compare */
          gdouble da = g_value_get_double (a);
          gdouble db = g_value_get_double (b);
          ret = memcmp (&da, &db, sizeof (gdouble)) == 0;
        }
        break;
      case G_TYPE_STRING:
        ret = (g_strcmp0 (g_value_get_string (a), g_value_get_string (b)) == 0);
        break;
      case G_TYPE_VARIANT:
        ret = _g_variant_equal0 (g_value_get_variant (a), g_value_get_variant (b));
        break;
      default:
        if (G_VALUE_TYPE (a) == G_TYPE_STRV)
          ret = _g_strv_equal0 (g_value_get_boxed (a), g_value_get_boxed (b));
        else
          g_critical ("_g_value_equal() does not handle type %s", g_type_name (G_VALUE_TYPE (a)));
        break;
    }
  return ret;
}

/* ------------------------------------------------------------------------
 * Code for interface org.gnome.evolution.dataserver.AddressBookDirect
 * ------------------------------------------------------------------------
 */

/**
 * SECTION:EGdbusBookDirect
 * @title: EGdbusBookDirect
 * @short_description: Generated C code for the org.gnome.evolution.dataserver.AddressBookDirect D-Bus interface
 *
 * This section contains code for working with the <link linkend="gdbus-interface-org-gnome-evolution-dataserver-AddressBookDirect.top_of_page">org.gnome.evolution.dataserver.AddressBookDirect</link> D-Bus interface in C.
 */

/* ---- Introspection data for org.gnome.evolution.dataserver.AddressBookDirect ---- */

static const _ExtendedGDBusPropertyInfo _e_gdbus_book_direct_property_info_backend_path =
{
  {
    -1,
    (gchar *) "BackendPath",
    (gchar *) "s",
    G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
    NULL
  },
  "backend-path",
  FALSE
};

static const _ExtendedGDBusPropertyInfo _e_gdbus_book_direct_property_info_backend_name =
{
  {
    -1,
    (gchar *) "BackendName",
    (gchar *) "s",
    G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
    NULL
  },
  "backend-name",
  FALSE
};

static const _ExtendedGDBusPropertyInfo _e_gdbus_book_direct_property_info_backend_config =
{
  {
    -1,
    (gchar *) "BackendConfig",
    (gchar *) "s",
    G_DBUS_PROPERTY_INFO_FLAGS_READABLE,
    NULL
  },
  "backend-config",
  FALSE
};

static const _ExtendedGDBusPropertyInfo * const _e_gdbus_book_direct_property_info_pointers[] =
{
  &_e_gdbus_book_direct_property_info_backend_path,
  &_e_gdbus_book_direct_property_info_backend_name,
  &_e_gdbus_book_direct_property_info_backend_config,
  NULL
};

static const _ExtendedGDBusInterfaceInfo _e_gdbus_book_direct_interface_info =
{
  {
    -1,
    (gchar *) "org.gnome.evolution.dataserver.AddressBookDirect",
    NULL,
    NULL,
    (GDBusPropertyInfo **) &_e_gdbus_book_direct_property_info_pointers,
    NULL
  },
  "address-book-direct",
};


/**
 * e_gdbus_book_direct_interface_info:
 *
 * Gets a machine-readable description of the <link linkend="gdbus-interface-org-gnome-evolution-dataserver-AddressBookDirect.top_of_page">org.gnome.evolution.dataserver.AddressBookDirect</link> D-Bus interface.
 *
 * Returns: (transfer none): A #GDBusInterfaceInfo. Do not free.
 */
GDBusInterfaceInfo *
e_gdbus_book_direct_interface_info (void)
{
  return (GDBusInterfaceInfo *) &_e_gdbus_book_direct_interface_info.parent_struct;
}

/**
 * e_gdbus_book_direct_override_properties:
 * @klass: The class structure for a #GObject<!-- -->-derived class.
 * @property_id_begin: The property id to assign to the first overridden property.
 *
 * Overrides all #GObject properties in the #EGdbusBookDirect interface for a concrete class.
 * The properties are overridden in the order they are defined.
 *
 * Returns: The last property id.
 */
guint
e_gdbus_book_direct_override_properties (GObjectClass *klass, guint property_id_begin)
{
  g_object_class_override_property (klass, property_id_begin++, "backend-path");
  g_object_class_override_property (klass, property_id_begin++, "backend-name");
  g_object_class_override_property (klass, property_id_begin++, "backend-config");
  return property_id_begin - 1;
}



/**
 * EGdbusBookDirect:
 *
 * Abstract interface type for the D-Bus interface <link linkend="gdbus-interface-org-gnome-evolution-dataserver-AddressBookDirect.top_of_page">org.gnome.evolution.dataserver.AddressBookDirect</link>.
 */

/**
 * EGdbusBookDirectIface:
 * @parent_iface: The parent interface.
 * @get_backend_config: Getter for the #EGdbusBookDirect:backend-config property.
 * @get_backend_name: Getter for the #EGdbusBookDirect:backend-name property.
 * @get_backend_path: Getter for the #EGdbusBookDirect:backend-path property.
 *
 * Virtual table for the D-Bus interface <link linkend="gdbus-interface-org-gnome-evolution-dataserver-AddressBookDirect.top_of_page">org.gnome.evolution.dataserver.AddressBookDirect</link>.
 */
typedef EGdbusBookDirectIface EGdbusBookDirectInterface;
G_DEFINE_INTERFACE (EGdbusBookDirect, e_gdbus_book_direct, G_TYPE_OBJECT);

static void
e_gdbus_book_direct_default_init (EGdbusBookDirectIface *iface)
{
  /* GObject properties for D-Bus properties: */
  g_object_interface_install_property (iface,
    g_param_spec_string ("backend-path", "BackendPath", "BackendPath", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_interface_install_property (iface,
    g_param_spec_string ("backend-name", "BackendName", "BackendName", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_interface_install_property (iface,
    g_param_spec_string ("backend-config", "BackendConfig", "BackendConfig", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/**
 * e_gdbus_book_direct_get_backend_path: (skip)
 * @object: A #EGdbusBookDirect.
 *
 * Gets the value of the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendPath">"BackendPath"</link> D-Bus property.
 *
 * Since this D-Bus property is readable, it is meaningful to use this function on both the client- and service-side.
 *
 * <warning>The returned value is only valid until the property changes so on the client-side it is only safe to use this function on the thread where @object was constructed. Use e_gdbus_book_direct_dup_backend_path() if on another thread.</warning>
 *
 * Returns: (transfer none): The property value or %NULL if the property is not set. Do not free the returned value, it belongs to @object.
 */
const gchar *
e_gdbus_book_direct_get_backend_path (EGdbusBookDirect *object)
{
  return E_GDBUS_BOOK_DIRECT_GET_IFACE (object)->get_backend_path (object);
}

/**
 * e_gdbus_book_direct_dup_backend_path: (skip)
 * @object: A #EGdbusBookDirect.
 *
 * Gets a copy of the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendPath">"BackendPath"</link> D-Bus property.
 *
 * Since this D-Bus property is readable, it is meaningful to use this function on both the client- and service-side.
 *
 * Returns: (transfer full): The property value or %NULL if the property is not set. The returned value should be freed with g_free().
 */
gchar *
e_gdbus_book_direct_dup_backend_path (EGdbusBookDirect *object)
{
  gchar *value;
  g_object_get (G_OBJECT (object), "backend-path", &value, NULL);
  return value;
}

/**
 * e_gdbus_book_direct_set_backend_path: (skip)
 * @object: A #EGdbusBookDirect.
 * @value: The value to set.
 *
 * Sets the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendPath">"BackendPath"</link> D-Bus property to @value.
 *
 * Since this D-Bus property is not writable, it is only meaningful to use this function on the service-side.
 */
void
e_gdbus_book_direct_set_backend_path (EGdbusBookDirect *object, const gchar *value)
{
  g_object_set (G_OBJECT (object), "backend-path", value, NULL);
}

/**
 * e_gdbus_book_direct_get_backend_name: (skip)
 * @object: A #EGdbusBookDirect.
 *
 * Gets the value of the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendName">"BackendName"</link> D-Bus property.
 *
 * Since this D-Bus property is readable, it is meaningful to use this function on both the client- and service-side.
 *
 * <warning>The returned value is only valid until the property changes so on the client-side it is only safe to use this function on the thread where @object was constructed. Use e_gdbus_book_direct_dup_backend_name() if on another thread.</warning>
 *
 * Returns: (transfer none): The property value or %NULL if the property is not set. Do not free the returned value, it belongs to @object.
 */
const gchar *
e_gdbus_book_direct_get_backend_name (EGdbusBookDirect *object)
{
  return E_GDBUS_BOOK_DIRECT_GET_IFACE (object)->get_backend_name (object);
}

/**
 * e_gdbus_book_direct_dup_backend_name: (skip)
 * @object: A #EGdbusBookDirect.
 *
 * Gets a copy of the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendName">"BackendName"</link> D-Bus property.
 *
 * Since this D-Bus property is readable, it is meaningful to use this function on both the client- and service-side.
 *
 * Returns: (transfer full): The property value or %NULL if the property is not set. The returned value should be freed with g_free().
 */
gchar *
e_gdbus_book_direct_dup_backend_name (EGdbusBookDirect *object)
{
  gchar *value;
  g_object_get (G_OBJECT (object), "backend-name", &value, NULL);
  return value;
}

/**
 * e_gdbus_book_direct_set_backend_name: (skip)
 * @object: A #EGdbusBookDirect.
 * @value: The value to set.
 *
 * Sets the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendName">"BackendName"</link> D-Bus property to @value.
 *
 * Since this D-Bus property is not writable, it is only meaningful to use this function on the service-side.
 */
void
e_gdbus_book_direct_set_backend_name (EGdbusBookDirect *object, const gchar *value)
{
  g_object_set (G_OBJECT (object), "backend-name", value, NULL);
}

/**
 * e_gdbus_book_direct_get_backend_config: (skip)
 * @object: A #EGdbusBookDirect.
 *
 * Gets the value of the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendConfig">"BackendConfig"</link> D-Bus property.
 *
 * Since this D-Bus property is readable, it is meaningful to use this function on both the client- and service-side.
 *
 * <warning>The returned value is only valid until the property changes so on the client-side it is only safe to use this function on the thread where @object was constructed. Use e_gdbus_book_direct_dup_backend_config() if on another thread.</warning>
 *
 * Returns: (transfer none): The property value or %NULL if the property is not set. Do not free the returned value, it belongs to @object.
 */
const gchar *
e_gdbus_book_direct_get_backend_config (EGdbusBookDirect *object)
{
  return E_GDBUS_BOOK_DIRECT_GET_IFACE (object)->get_backend_config (object);
}

/**
 * e_gdbus_book_direct_dup_backend_config: (skip)
 * @object: A #EGdbusBookDirect.
 *
 * Gets a copy of the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendConfig">"BackendConfig"</link> D-Bus property.
 *
 * Since this D-Bus property is readable, it is meaningful to use this function on both the client- and service-side.
 *
 * Returns: (transfer full): The property value or %NULL if the property is not set. The returned value should be freed with g_free().
 */
gchar *
e_gdbus_book_direct_dup_backend_config (EGdbusBookDirect *object)
{
  gchar *value;
  g_object_get (G_OBJECT (object), "backend-config", &value, NULL);
  return value;
}

/**
 * e_gdbus_book_direct_set_backend_config: (skip)
 * @object: A #EGdbusBookDirect.
 * @value: The value to set.
 *
 * Sets the <link linkend="gdbus-property-org-gnome-evolution-dataserver-AddressBookDirect.BackendConfig">"BackendConfig"</link> D-Bus property to @value.
 *
 * Since this D-Bus property is not writable, it is only meaningful to use this function on the service-side.
 */
void
e_gdbus_book_direct_set_backend_config (EGdbusBookDirect *object, const gchar *value)
{
  g_object_set (G_OBJECT (object), "backend-config", value, NULL);
}

/* ------------------------------------------------------------------------ */

/**
 * EGdbusBookDirectProxy:
 *
 * The #EGdbusBookDirectProxy structure contains only private data and should only be accessed using the provided API.
 */

/**
 * EGdbusBookDirectProxyClass:
 * @parent_class: The parent class.
 *
 * Class structure for #EGdbusBookDirectProxy.
 */

struct _EGdbusBookDirectProxyPrivate
{
  GData *qdata;
};

static void e_gdbus_book_direct_proxy_iface_init (EGdbusBookDirectIface *iface);

G_DEFINE_TYPE_WITH_CODE (EGdbusBookDirectProxy, e_gdbus_book_direct_proxy, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (E_GDBUS_TYPE_BOOK_DIRECT, e_gdbus_book_direct_proxy_iface_init));

static void
e_gdbus_book_direct_proxy_finalize (GObject *object)
{
  EGdbusBookDirectProxy *proxy = E_GDBUS_BOOK_DIRECT_PROXY (object);
  g_datalist_clear (&proxy->priv->qdata);
  G_OBJECT_CLASS (e_gdbus_book_direct_proxy_parent_class)->finalize (object);
}

static void
e_gdbus_book_direct_proxy_get_property (GObject      *object,
  guint         prop_id,
  GValue       *value,
  GParamSpec   *pspec)
{
  const _ExtendedGDBusPropertyInfo *info;
  GVariant *variant;
  g_assert (prop_id != 0 && prop_id - 1 < 3);
  info = _e_gdbus_book_direct_property_info_pointers[prop_id - 1];
  variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (object), info->parent_struct.name);
  if (info->use_gvariant)
    {
      g_value_set_variant (value, variant);
    }
  else
    {
      if (variant != NULL)
        g_dbus_gvariant_to_gvalue (variant, value);
    }
  if (variant != NULL)
    g_variant_unref (variant);
}

static void
e_gdbus_book_direct_proxy_set_property_cb (GDBusProxy *proxy,
  GAsyncResult *res,
  gpointer      user_data)
{
  const _ExtendedGDBusPropertyInfo *info = user_data;
  GError *error;
  error = NULL;
  if (!g_dbus_proxy_call_finish (proxy, res, &error))
    {
      g_warning ("Error setting property `%s' on interface org.gnome.evolution.dataserver.AddressBookDirect: %s (%s, %d)",
                 info->parent_struct.name, 
                 error->message, g_quark_to_string (error->domain), error->code);
      g_error_free (error);
    }
}

static void
e_gdbus_book_direct_proxy_set_property (GObject      *object,
  guint         prop_id,
  const GValue *value,
  GParamSpec   *pspec)
{
  const _ExtendedGDBusPropertyInfo *info;
  GVariant *variant;
  g_assert (prop_id != 0 && prop_id - 1 < 3);
  info = _e_gdbus_book_direct_property_info_pointers[prop_id - 1];
  variant = g_dbus_gvalue_to_gvariant (value, G_VARIANT_TYPE (info->parent_struct.signature));
  g_dbus_proxy_call (G_DBUS_PROXY (object),
    "org.freedesktop.DBus.Properties.Set",
    g_variant_new ("(ssv)", "org.gnome.evolution.dataserver.AddressBookDirect", info->parent_struct.name, variant),
    G_DBUS_CALL_FLAGS_NONE,
    -1,
    NULL, (GAsyncReadyCallback) e_gdbus_book_direct_proxy_set_property_cb, (GDBusPropertyInfo *) &info->parent_struct);
  g_variant_unref (variant);
}

static void
e_gdbus_book_direct_proxy_g_signal (GDBusProxy *proxy,
  const gchar *sender_name,
  const gchar *signal_name,
  GVariant *parameters)
{
  _ExtendedGDBusSignalInfo *info;
  GVariantIter iter;
  GVariant *child;
  GValue *paramv;
  guint num_params;
  guint n;
  guint signal_id;
  info = (_ExtendedGDBusSignalInfo *) g_dbus_interface_info_lookup_signal ((GDBusInterfaceInfo *) &_e_gdbus_book_direct_interface_info.parent_struct, signal_name);
  if (info == NULL)
    return;
  num_params = g_variant_n_children (parameters);
  paramv = g_new0 (GValue, num_params + 1);
  g_value_init (&paramv[0], E_GDBUS_TYPE_BOOK_DIRECT);
  g_value_set_object (&paramv[0], proxy);
  g_variant_iter_init (&iter, parameters);
  n = 1;
  while ((child = g_variant_iter_next_value (&iter)) != NULL)
    {
      _ExtendedGDBusArgInfo *arg_info = (_ExtendedGDBusArgInfo *) info->parent_struct.args[n - 1];
      if (arg_info->use_gvariant)
        {
          g_value_init (&paramv[n], G_TYPE_VARIANT);
          g_value_set_variant (&paramv[n], child);
          n++;
        }
      else
        g_dbus_gvariant_to_gvalue (child, &paramv[n++]);
      g_variant_unref (child);
    }
  signal_id = g_signal_lookup (info->signal_name, E_GDBUS_TYPE_BOOK_DIRECT);
  g_signal_emitv (paramv, signal_id, 0, NULL);
  for (n = 0; n < num_params + 1; n++)
    g_value_unset (&paramv[n]);
  g_free (paramv);
}

static void
e_gdbus_book_direct_proxy_g_properties_changed (GDBusProxy *_proxy,
  GVariant *changed_properties,
  const gchar *const *invalidated_properties)
{
  EGdbusBookDirectProxy *proxy = E_GDBUS_BOOK_DIRECT_PROXY (_proxy);
  guint n;
  const gchar *key;
  GVariantIter *iter;
  _ExtendedGDBusPropertyInfo *info;
  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_next (iter, "{&sv}", &key, NULL))
    {
      info = (_ExtendedGDBusPropertyInfo *) g_dbus_interface_info_lookup_property ((GDBusInterfaceInfo *) &_e_gdbus_book_direct_interface_info.parent_struct, key);
      g_datalist_remove_data (&proxy->priv->qdata, key);
      if (info != NULL)
        g_object_notify (G_OBJECT (proxy), info->hyphen_name);
    }
  g_variant_iter_free (iter);
  for (n = 0; invalidated_properties[n] != NULL; n++)
    {
      info = (_ExtendedGDBusPropertyInfo *) g_dbus_interface_info_lookup_property ((GDBusInterfaceInfo *) &_e_gdbus_book_direct_interface_info.parent_struct, invalidated_properties[n]);
      g_datalist_remove_data (&proxy->priv->qdata, invalidated_properties[n]);
      if (info != NULL)
        g_object_notify (G_OBJECT (proxy), info->hyphen_name);
    }
}

static const gchar *
e_gdbus_book_direct_proxy_get_backend_path (EGdbusBookDirect *object)
{
  EGdbusBookDirectProxy *proxy = E_GDBUS_BOOK_DIRECT_PROXY (object);
  GVariant *variant;
  const gchar *value = NULL;
  variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (proxy), "BackendPath");
  if (variant != NULL)
    {
      value = g_variant_get_string (variant, NULL);
      g_variant_unref (variant);
    }
  return value;
}

static const gchar *
e_gdbus_book_direct_proxy_get_backend_name (EGdbusBookDirect *object)
{
  EGdbusBookDirectProxy *proxy = E_GDBUS_BOOK_DIRECT_PROXY (object);
  GVariant *variant;
  const gchar *value = NULL;
  variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (proxy), "BackendName");
  if (variant != NULL)
    {
      value = g_variant_get_string (variant, NULL);
      g_variant_unref (variant);
    }
  return value;
}

static const gchar *
e_gdbus_book_direct_proxy_get_backend_config (EGdbusBookDirect *object)
{
  EGdbusBookDirectProxy *proxy = E_GDBUS_BOOK_DIRECT_PROXY (object);
  GVariant *variant;
  const gchar *value = NULL;
  variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (proxy), "BackendConfig");
  if (variant != NULL)
    {
      value = g_variant_get_string (variant, NULL);
      g_variant_unref (variant);
    }
  return value;
}

static void
e_gdbus_book_direct_proxy_init (EGdbusBookDirectProxy *proxy)
{
  proxy->priv = G_TYPE_INSTANCE_GET_PRIVATE (proxy, E_GDBUS_TYPE_BOOK_DIRECT_PROXY, EGdbusBookDirectProxyPrivate);
  g_dbus_proxy_set_interface_info (G_DBUS_PROXY (proxy), e_gdbus_book_direct_interface_info ());
}

static void
e_gdbus_book_direct_proxy_class_init (EGdbusBookDirectProxyClass *klass)
{
  GObjectClass *gobject_class;
  GDBusProxyClass *proxy_class;

  g_type_class_add_private (klass, sizeof (EGdbusBookDirectProxyPrivate));

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize     = e_gdbus_book_direct_proxy_finalize;
  gobject_class->get_property = e_gdbus_book_direct_proxy_get_property;
  gobject_class->set_property = e_gdbus_book_direct_proxy_set_property;

  proxy_class = G_DBUS_PROXY_CLASS (klass);
  proxy_class->g_signal = e_gdbus_book_direct_proxy_g_signal;
  proxy_class->g_properties_changed = e_gdbus_book_direct_proxy_g_properties_changed;


  e_gdbus_book_direct_override_properties (gobject_class, 1);
}

static void
e_gdbus_book_direct_proxy_iface_init (EGdbusBookDirectIface *iface)
{
  iface->get_backend_path = e_gdbus_book_direct_proxy_get_backend_path;
  iface->get_backend_name = e_gdbus_book_direct_proxy_get_backend_name;
  iface->get_backend_config = e_gdbus_book_direct_proxy_get_backend_config;
}

/**
 * e_gdbus_book_direct_proxy_new:
 * @connection: A #GDBusConnection.
 * @flags: Flags from the #GDBusProxyFlags enumeration.
 * @name: (allow-none): A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied.
 * @user_data: User data to pass to @callback.
 *
 * Asynchronously creates a proxy for the D-Bus interface <link linkend="gdbus-interface-org-gnome-evolution-dataserver-AddressBookDirect.top_of_page">org.gnome.evolution.dataserver.AddressBookDirect</link>. See g_dbus_proxy_new() for more details.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call e_gdbus_book_direct_proxy_new_finish() to get the result of the operation.
 *
 * See e_gdbus_book_direct_proxy_new_sync() for the synchronous, blocking version of this constructor.
 */
void
e_gdbus_book_direct_proxy_new (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data)
{
  g_async_initable_new_async (E_GDBUS_TYPE_BOOK_DIRECT_PROXY, G_PRIORITY_DEFAULT, cancellable, callback, user_data, "g-flags", flags, "g-name", name, "g-connection", connection, "g-object-path", object_path, "g-interface-name", "org.gnome.evolution.dataserver.AddressBookDirect", NULL);
}

/**
 * e_gdbus_book_direct_proxy_new_finish:
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to e_gdbus_book_direct_proxy_new().
 * @error: Return location for error or %NULL
 *
 * Finishes an operation started with e_gdbus_book_direct_proxy_new().
 *
 * Returns: (transfer full) (type EGdbusBookDirectProxy): The constructed proxy object or %NULL if @error is set.
 */
EGdbusBookDirect *
e_gdbus_book_direct_proxy_new_finish (
    GAsyncResult        *res,
    GError             **error)
{
  GObject *ret;
  GObject *source_object;
  source_object = g_async_result_get_source_object (res);
  ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
  g_object_unref (source_object);
  if (ret != NULL)
    return E_GDBUS_BOOK_DIRECT (ret);
  else
    return NULL;
}

/**
 * e_gdbus_book_direct_proxy_new_sync:
 * @connection: A #GDBusConnection.
 * @flags: Flags from the #GDBusProxyFlags enumeration.
 * @name: (allow-none): A bus name (well-known or unique) or %NULL if @connection is not a message bus connection.
 * @object_path: An object path.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL
 *
 * Synchronously creates a proxy for the D-Bus interface <link linkend="gdbus-interface-org-gnome-evolution-dataserver-AddressBookDirect.top_of_page">org.gnome.evolution.dataserver.AddressBookDirect</link>. See g_dbus_proxy_new_sync() for more details.
 *
 * The calling thread is blocked until a reply is received.
 *
 * See e_gdbus_book_direct_proxy_new() for the asynchronous version of this constructor.
 *
 * Returns: (transfer full) (type EGdbusBookDirectProxy): The constructed proxy object or %NULL if @error is set.
 */
EGdbusBookDirect *
e_gdbus_book_direct_proxy_new_sync (
    GDBusConnection     *connection,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error)
{
  GInitable *ret;
  ret = g_initable_new (E_GDBUS_TYPE_BOOK_DIRECT_PROXY, cancellable, error, "g-flags", flags, "g-name", name, "g-connection", connection, "g-object-path", object_path, "g-interface-name", "org.gnome.evolution.dataserver.AddressBookDirect", NULL);
  if (ret != NULL)
    return E_GDBUS_BOOK_DIRECT (ret);
  else
    return NULL;
}


/**
 * e_gdbus_book_direct_proxy_new_for_bus:
 * @bus_type: A #GBusType.
 * @flags: Flags from the #GDBusProxyFlags enumeration.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @callback: A #GAsyncReadyCallback to call when the request is satisfied.
 * @user_data: User data to pass to @callback.
 *
 * Like e_gdbus_book_direct_proxy_new() but takes a #GBusType instead of a #GDBusConnection.
 *
 * When the operation is finished, @callback will be invoked in the <link linkend="g-main-context-push-thread-default">thread-default main loop</link> of the thread you are calling this method from.
 * You can then call e_gdbus_book_direct_proxy_new_for_bus_finish() to get the result of the operation.
 *
 * See e_gdbus_book_direct_proxy_new_for_bus_sync() for the synchronous, blocking version of this constructor.
 */
void
e_gdbus_book_direct_proxy_new_for_bus (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GAsyncReadyCallback  callback,
    gpointer             user_data)
{
  g_async_initable_new_async (E_GDBUS_TYPE_BOOK_DIRECT_PROXY, G_PRIORITY_DEFAULT, cancellable, callback, user_data, "g-flags", flags, "g-name", name, "g-bus-type", bus_type, "g-object-path", object_path, "g-interface-name", "org.gnome.evolution.dataserver.AddressBookDirect", NULL);
}

/**
 * e_gdbus_book_direct_proxy_new_for_bus_finish:
 * @res: The #GAsyncResult obtained from the #GAsyncReadyCallback passed to e_gdbus_book_direct_proxy_new_for_bus().
 * @error: Return location for error or %NULL
 *
 * Finishes an operation started with e_gdbus_book_direct_proxy_new_for_bus().
 *
 * Returns: (transfer full) (type EGdbusBookDirectProxy): The constructed proxy object or %NULL if @error is set.
 */
EGdbusBookDirect *
e_gdbus_book_direct_proxy_new_for_bus_finish (
    GAsyncResult        *res,
    GError             **error)
{
  GObject *ret;
  GObject *source_object;
  source_object = g_async_result_get_source_object (res);
  ret = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), res, error);
  g_object_unref (source_object);
  if (ret != NULL)
    return E_GDBUS_BOOK_DIRECT (ret);
  else
    return NULL;
}

/**
 * e_gdbus_book_direct_proxy_new_for_bus_sync:
 * @bus_type: A #GBusType.
 * @flags: Flags from the #GDBusProxyFlags enumeration.
 * @name: A bus name (well-known or unique).
 * @object_path: An object path.
 * @cancellable: (allow-none): A #GCancellable or %NULL.
 * @error: Return location for error or %NULL
 *
 * Like e_gdbus_book_direct_proxy_new_sync() but takes a #GBusType instead of a #GDBusConnection.
 *
 * The calling thread is blocked until a reply is received.
 *
 * See e_gdbus_book_direct_proxy_new_for_bus() for the asynchronous version of this constructor.
 *
 * Returns: (transfer full) (type EGdbusBookDirectProxy): The constructed proxy object or %NULL if @error is set.
 */
EGdbusBookDirect *
e_gdbus_book_direct_proxy_new_for_bus_sync (
    GBusType             bus_type,
    GDBusProxyFlags      flags,
    const gchar         *name,
    const gchar         *object_path,
    GCancellable        *cancellable,
    GError             **error)
{
  GInitable *ret;
  ret = g_initable_new (E_GDBUS_TYPE_BOOK_DIRECT_PROXY, cancellable, error, "g-flags", flags, "g-name", name, "g-bus-type", bus_type, "g-object-path", object_path, "g-interface-name", "org.gnome.evolution.dataserver.AddressBookDirect", NULL);
  if (ret != NULL)
    return E_GDBUS_BOOK_DIRECT (ret);
  else
    return NULL;
}


/* ------------------------------------------------------------------------ */

/**
 * EGdbusBookDirectSkeleton:
 *
 * The #EGdbusBookDirectSkeleton structure contains only private data and should only be accessed using the provided API.
 */

/**
 * EGdbusBookDirectSkeletonClass:
 * @parent_class: The parent class.
 *
 * Class structure for #EGdbusBookDirectSkeleton.
 */

struct _EGdbusBookDirectSkeletonPrivate
{
  GValue *properties;
  GList *changed_properties;
  GSource *changed_properties_idle_source;
  GMainContext *context;
  GMutex lock;
};

static void
_e_gdbus_book_direct_skeleton_handle_method_call (
  GDBusConnection *connection,
  const gchar *sender,
  const gchar *object_path,
  const gchar *interface_name,
  const gchar *method_name,
  GVariant *parameters,
  GDBusMethodInvocation *invocation,
  gpointer user_data)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (user_data);
  _ExtendedGDBusMethodInfo *info;
  GVariantIter iter;
  GVariant *child;
  GValue *paramv;
  guint num_params;
  guint num_extra;
  guint n;
  guint signal_id;
  GValue return_value = G_VALUE_INIT;
  info = (_ExtendedGDBusMethodInfo *) g_dbus_method_invocation_get_method_info (invocation);
  g_assert (info != NULL);
  num_params = g_variant_n_children (parameters);
  num_extra = info->pass_fdlist ? 3 : 2;  paramv = g_new0 (GValue, num_params + num_extra);
  n = 0;
  g_value_init (&paramv[n], E_GDBUS_TYPE_BOOK_DIRECT);
  g_value_set_object (&paramv[n++], skeleton);
  g_value_init (&paramv[n], G_TYPE_DBUS_METHOD_INVOCATION);
  g_value_set_object (&paramv[n++], invocation);
  if (info->pass_fdlist)
    {
#ifdef G_OS_UNIX
      g_value_init (&paramv[n], G_TYPE_UNIX_FD_LIST);
      g_value_set_object (&paramv[n++], g_dbus_message_get_unix_fd_list (g_dbus_method_invocation_get_message (invocation)));
#else
      g_assert_not_reached ();
#endif
    }
  g_variant_iter_init (&iter, parameters);
  while ((child = g_variant_iter_next_value (&iter)) != NULL)
    {
      _ExtendedGDBusArgInfo *arg_info = (_ExtendedGDBusArgInfo *) info->parent_struct.in_args[n - num_extra];
      if (arg_info->use_gvariant)
        {
          g_value_init (&paramv[n], G_TYPE_VARIANT);
          g_value_set_variant (&paramv[n], child);
          n++;
        }
      else
        g_dbus_gvariant_to_gvalue (child, &paramv[n++]);
      g_variant_unref (child);
    }
  signal_id = g_signal_lookup (info->signal_name, E_GDBUS_TYPE_BOOK_DIRECT);
  g_value_init (&return_value, G_TYPE_BOOLEAN);
  g_signal_emitv (paramv, signal_id, 0, &return_value);
  if (!g_value_get_boolean (&return_value))
    g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "Method %s is not implemented on interface %s", method_name, interface_name);
  g_value_unset (&return_value);
  for (n = 0; n < num_params + num_extra; n++)
    g_value_unset (&paramv[n]);
  g_free (paramv);
}

static GVariant *
_e_gdbus_book_direct_skeleton_handle_get_property (
  GDBusConnection *connection,
  const gchar *sender,
  const gchar *object_path,
  const gchar *interface_name,
  const gchar *property_name,
  GError **error,
  gpointer user_data)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (user_data);
  GValue value = G_VALUE_INIT;
  GParamSpec *pspec;
  _ExtendedGDBusPropertyInfo *info;
  GVariant *ret;
  ret = NULL;
  info = (_ExtendedGDBusPropertyInfo *) g_dbus_interface_info_lookup_property ((GDBusInterfaceInfo *) &_e_gdbus_book_direct_interface_info.parent_struct, property_name);
  g_assert (info != NULL);
  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (skeleton), info->hyphen_name);
  if (pspec == NULL)
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No property with name %s", property_name);
    }
  else
    {
      g_value_init (&value, pspec->value_type);
      g_object_get_property (G_OBJECT (skeleton), info->hyphen_name, &value);
      ret = g_dbus_gvalue_to_gvariant (&value, G_VARIANT_TYPE (info->parent_struct.signature));
      g_value_unset (&value);
    }
  return ret;
}

static gboolean
_e_gdbus_book_direct_skeleton_handle_set_property (
  GDBusConnection *connection,
  const gchar *sender,
  const gchar *object_path,
  const gchar *interface_name,
  const gchar *property_name,
  GVariant *variant,
  GError **error,
  gpointer user_data)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (user_data);
  GValue value = G_VALUE_INIT;
  GParamSpec *pspec;
  _ExtendedGDBusPropertyInfo *info;
  gboolean ret;
  ret = FALSE;
  info = (_ExtendedGDBusPropertyInfo *) g_dbus_interface_info_lookup_property ((GDBusInterfaceInfo *) &_e_gdbus_book_direct_interface_info.parent_struct, property_name);
  g_assert (info != NULL);
  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (skeleton), info->hyphen_name);
  if (pspec == NULL)
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS, "No property with name %s", property_name);
    }
  else
    {
      if (info->use_gvariant)
        g_value_set_variant (&value, variant);
      else
        g_dbus_gvariant_to_gvalue (variant, &value);
      g_object_set_property (G_OBJECT (skeleton), info->hyphen_name, &value);
      g_value_unset (&value);
      ret = TRUE;
    }
  return ret;
}

static const GDBusInterfaceVTable _e_gdbus_book_direct_skeleton_vtable =
{
  _e_gdbus_book_direct_skeleton_handle_method_call,
  _e_gdbus_book_direct_skeleton_handle_get_property,
  _e_gdbus_book_direct_skeleton_handle_set_property
};

static GDBusInterfaceInfo *
e_gdbus_book_direct_skeleton_dbus_interface_get_info (GDBusInterfaceSkeleton *skeleton)
{
  return e_gdbus_book_direct_interface_info ();
}

static GDBusInterfaceVTable *
e_gdbus_book_direct_skeleton_dbus_interface_get_vtable (GDBusInterfaceSkeleton *skeleton)
{
  return (GDBusInterfaceVTable *) &_e_gdbus_book_direct_skeleton_vtable;
}

static GVariant *
e_gdbus_book_direct_skeleton_dbus_interface_get_properties (GDBusInterfaceSkeleton *_skeleton)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (_skeleton);

  GVariantBuilder builder;
  guint n;
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  if (_e_gdbus_book_direct_interface_info.parent_struct.properties == NULL)
    goto out;
  for (n = 0; _e_gdbus_book_direct_interface_info.parent_struct.properties[n] != NULL; n++)
    {
      GDBusPropertyInfo *info = _e_gdbus_book_direct_interface_info.parent_struct.properties[n];
      if (info->flags & G_DBUS_PROPERTY_INFO_FLAGS_READABLE)
        {
          GVariant *value;
          value = _e_gdbus_book_direct_skeleton_handle_get_property (g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (skeleton)), NULL, g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (skeleton)), "org.gnome.evolution.dataserver.AddressBookDirect", info->name, NULL, skeleton);
          if (value != NULL)
            {
              g_variant_take_ref (value);
              g_variant_builder_add (&builder, "{sv}", info->name, value);
              g_variant_unref (value);
            }
        }
    }
out:
  return g_variant_builder_end (&builder);
}

static gboolean _e_gdbus_book_direct_emit_changed (gpointer user_data);

static void
e_gdbus_book_direct_skeleton_dbus_interface_flush (GDBusInterfaceSkeleton *_skeleton)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (_skeleton);
  gboolean emit_changed = FALSE;

  g_mutex_lock (&skeleton->priv->lock);
  if (skeleton->priv->changed_properties_idle_source != NULL)
    {
      g_source_destroy (skeleton->priv->changed_properties_idle_source);
      skeleton->priv->changed_properties_idle_source = NULL;
      emit_changed = TRUE;
    }
  g_mutex_unlock (&skeleton->priv->lock);

  if (emit_changed)
    _e_gdbus_book_direct_emit_changed (skeleton);
}

static void e_gdbus_book_direct_skeleton_iface_init (EGdbusBookDirectIface *iface);
G_DEFINE_TYPE_WITH_CODE (EGdbusBookDirectSkeleton, e_gdbus_book_direct_skeleton, G_TYPE_DBUS_INTERFACE_SKELETON,
                         G_IMPLEMENT_INTERFACE (E_GDBUS_TYPE_BOOK_DIRECT, e_gdbus_book_direct_skeleton_iface_init));

static void
e_gdbus_book_direct_skeleton_finalize (GObject *object)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (object);
  guint n;
  for (n = 0; n < 3; n++)
    g_value_unset (&skeleton->priv->properties[n]);
  g_free (skeleton->priv->properties);
  g_list_free_full (skeleton->priv->changed_properties, (GDestroyNotify) _changed_property_free);
  if (skeleton->priv->changed_properties_idle_source != NULL)
    g_source_destroy (skeleton->priv->changed_properties_idle_source);
  g_main_context_unref (skeleton->priv->context);
  g_mutex_clear (&skeleton->priv->lock);
  G_OBJECT_CLASS (e_gdbus_book_direct_skeleton_parent_class)->finalize (object);
}

static void
e_gdbus_book_direct_skeleton_get_property (GObject      *object,
  guint         prop_id,
  GValue       *value,
  GParamSpec   *pspec)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (object);
  g_assert (prop_id != 0 && prop_id - 1 < 3);
  g_mutex_lock (&skeleton->priv->lock);
  g_value_copy (&skeleton->priv->properties[prop_id - 1], value);
  g_mutex_unlock (&skeleton->priv->lock);
}

static gboolean
_e_gdbus_book_direct_emit_changed (gpointer user_data)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (user_data);
  GList *l;
  GVariantBuilder builder;
  GVariantBuilder invalidated_builder;
  guint num_changes;

  g_mutex_lock (&skeleton->priv->lock);
  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_init (&invalidated_builder, G_VARIANT_TYPE ("as"));
  for (l = skeleton->priv->changed_properties, num_changes = 0; l != NULL; l = l->next)
    {
      ChangedProperty *cp = l->data;
      GVariant *variant;
      const GValue *cur_value;

      cur_value = &skeleton->priv->properties[cp->prop_id - 1];
      if (!_g_value_equal (cur_value, &cp->orig_value))
        {
          variant = g_dbus_gvalue_to_gvariant (cur_value, G_VARIANT_TYPE (cp->info->parent_struct.signature));
          g_variant_builder_add (&builder, "{sv}", cp->info->parent_struct.name, variant);
          g_variant_unref (variant);
          num_changes++;
        }
    }
  if (num_changes > 0)
    {
      GList *connections, *ll;
      GVariant *signal_variant;
      signal_variant = g_variant_ref_sink (g_variant_new ("(sa{sv}as)", "org.gnome.evolution.dataserver.AddressBookDirect",
                                           &builder, &invalidated_builder));
      connections = g_dbus_interface_skeleton_get_connections (G_DBUS_INTERFACE_SKELETON (skeleton));
      for (ll = connections; ll != NULL; ll = ll->next)
        {
          GDBusConnection *connection = ll->data;

          g_dbus_connection_emit_signal (connection,
                                         NULL, g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (skeleton)),
                                         "org.freedesktop.DBus.Properties",
                                         "PropertiesChanged",
                                         signal_variant,
                                         NULL);
        }
      g_variant_unref (signal_variant);
      g_list_free_full (connections, g_object_unref);
    }
  else
    {
      g_variant_builder_clear (&builder);
      g_variant_builder_clear (&invalidated_builder);
    }
  g_list_free_full (skeleton->priv->changed_properties, (GDestroyNotify) _changed_property_free);
  skeleton->priv->changed_properties = NULL;
  skeleton->priv->changed_properties_idle_source = NULL;
  g_mutex_unlock (&skeleton->priv->lock);
  return FALSE;
}

static void
_e_gdbus_book_direct_schedule_emit_changed (EGdbusBookDirectSkeleton *skeleton, const _ExtendedGDBusPropertyInfo *info, guint prop_id, const GValue *orig_value)
{
  ChangedProperty *cp;
  GList *l;
  cp = NULL;
  for (l = skeleton->priv->changed_properties; l != NULL; l = l->next)
    {
      ChangedProperty *i_cp = l->data;
      if (i_cp->info == info)
        {
          cp = i_cp;
          break;
        }
    }
  if (cp == NULL)
    {
      cp = g_new0 (ChangedProperty, 1);
      cp->prop_id = prop_id;
      cp->info = info;
      skeleton->priv->changed_properties = g_list_prepend (skeleton->priv->changed_properties, cp);
      g_value_init (&cp->orig_value, G_VALUE_TYPE (orig_value));
      g_value_copy (orig_value, &cp->orig_value);
    }
}

static void
e_gdbus_book_direct_skeleton_notify (GObject      *object,
  GParamSpec *pspec)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (object);
  g_mutex_lock (&skeleton->priv->lock);
  if (skeleton->priv->changed_properties != NULL &&
      skeleton->priv->changed_properties_idle_source == NULL)
    {
      skeleton->priv->changed_properties_idle_source = g_idle_source_new ();
      g_source_set_priority (skeleton->priv->changed_properties_idle_source, G_PRIORITY_DEFAULT);
      g_source_set_callback (skeleton->priv->changed_properties_idle_source, _e_gdbus_book_direct_emit_changed, g_object_ref (skeleton), (GDestroyNotify) g_object_unref);
      g_source_attach (skeleton->priv->changed_properties_idle_source, skeleton->priv->context);
      g_source_unref (skeleton->priv->changed_properties_idle_source);
    }
  g_mutex_unlock (&skeleton->priv->lock);
}

static void
e_gdbus_book_direct_skeleton_set_property (GObject      *object,
  guint         prop_id,
  const GValue *value,
  GParamSpec   *pspec)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (object);
  g_assert (prop_id != 0 && prop_id - 1 < 3);
  g_mutex_lock (&skeleton->priv->lock);
  g_object_freeze_notify (object);
  if (!_g_value_equal (value, &skeleton->priv->properties[prop_id - 1]))
    {
      if (g_dbus_interface_skeleton_get_connection (G_DBUS_INTERFACE_SKELETON (skeleton)) != NULL)
        _e_gdbus_book_direct_schedule_emit_changed (skeleton, _e_gdbus_book_direct_property_info_pointers[prop_id - 1], prop_id, &skeleton->priv->properties[prop_id - 1]);
      g_value_copy (value, &skeleton->priv->properties[prop_id - 1]);
      g_object_notify_by_pspec (object, pspec);
    }
  g_mutex_unlock (&skeleton->priv->lock);
  g_object_thaw_notify (object);
}

static void
e_gdbus_book_direct_skeleton_init (EGdbusBookDirectSkeleton *skeleton)
{
  skeleton->priv = G_TYPE_INSTANCE_GET_PRIVATE (skeleton, E_GDBUS_TYPE_BOOK_DIRECT_SKELETON, EGdbusBookDirectSkeletonPrivate);
  g_mutex_init (&skeleton->priv->lock);
  skeleton->priv->context = g_main_context_ref_thread_default ();
  skeleton->priv->properties = g_new0 (GValue, 3);
  g_value_init (&skeleton->priv->properties[0], G_TYPE_STRING);
  g_value_init (&skeleton->priv->properties[1], G_TYPE_STRING);
  g_value_init (&skeleton->priv->properties[2], G_TYPE_STRING);
}

static const gchar *
e_gdbus_book_direct_skeleton_get_backend_path (EGdbusBookDirect *object)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (object);
  const gchar *value;
  g_mutex_lock (&skeleton->priv->lock);
  value = g_value_get_string (&(skeleton->priv->properties[0]));
  g_mutex_unlock (&skeleton->priv->lock);
  return value;
}

static const gchar *
e_gdbus_book_direct_skeleton_get_backend_name (EGdbusBookDirect *object)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (object);
  const gchar *value;
  g_mutex_lock (&skeleton->priv->lock);
  value = g_value_get_string (&(skeleton->priv->properties[1]));
  g_mutex_unlock (&skeleton->priv->lock);
  return value;
}

static const gchar *
e_gdbus_book_direct_skeleton_get_backend_config (EGdbusBookDirect *object)
{
  EGdbusBookDirectSkeleton *skeleton = E_GDBUS_BOOK_DIRECT_SKELETON (object);
  const gchar *value;
  g_mutex_lock (&skeleton->priv->lock);
  value = g_value_get_string (&(skeleton->priv->properties[2]));
  g_mutex_unlock (&skeleton->priv->lock);
  return value;
}

static void
e_gdbus_book_direct_skeleton_class_init (EGdbusBookDirectSkeletonClass *klass)
{
  GObjectClass *gobject_class;
  GDBusInterfaceSkeletonClass *skeleton_class;

  g_type_class_add_private (klass, sizeof (EGdbusBookDirectSkeletonPrivate));

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = e_gdbus_book_direct_skeleton_finalize;
  gobject_class->get_property = e_gdbus_book_direct_skeleton_get_property;
  gobject_class->set_property = e_gdbus_book_direct_skeleton_set_property;
  gobject_class->notify       = e_gdbus_book_direct_skeleton_notify;


  e_gdbus_book_direct_override_properties (gobject_class, 1);

  skeleton_class = G_DBUS_INTERFACE_SKELETON_CLASS (klass);
  skeleton_class->get_info = e_gdbus_book_direct_skeleton_dbus_interface_get_info;
  skeleton_class->get_properties = e_gdbus_book_direct_skeleton_dbus_interface_get_properties;
  skeleton_class->flush = e_gdbus_book_direct_skeleton_dbus_interface_flush;
  skeleton_class->get_vtable = e_gdbus_book_direct_skeleton_dbus_interface_get_vtable;
}

static void
e_gdbus_book_direct_skeleton_iface_init (EGdbusBookDirectIface *iface)
{
  iface->get_backend_path = e_gdbus_book_direct_skeleton_get_backend_path;
  iface->get_backend_name = e_gdbus_book_direct_skeleton_get_backend_name;
  iface->get_backend_config = e_gdbus_book_direct_skeleton_get_backend_config;
}

/**
 * e_gdbus_book_direct_skeleton_new:
 *
 * Creates a skeleton object for the D-Bus interface <link linkend="gdbus-interface-org-gnome-evolution-dataserver-AddressBookDirect.top_of_page">org.gnome.evolution.dataserver.AddressBookDirect</link>.
 *
 * Returns: (transfer full) (type EGdbusBookDirectSkeleton): The skeleton object.
 */
EGdbusBookDirect *
e_gdbus_book_direct_skeleton_new (void)
{
  return E_GDBUS_BOOK_DIRECT (g_object_new (E_GDBUS_TYPE_BOOK_DIRECT_SKELETON, NULL));
}

