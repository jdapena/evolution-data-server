/*
 * e-source-camel-provider.c
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

/**
 * SECTION: e-source-camel
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for #CamelSettings
 *
 * #ESourceCamel itself is abstract.  Its sole function is to
 * bridge #GObject properties from the #CamelSettings framework to the
 * #ESource framework.  It does this by procedurally registering an
 * #ESourceCamel subtype for each available #CamelService subtype,
 * and then registering #GObject properties to proxy the properties in the
 * corresponding #CamelSettings subtype.  The #ESourceCamel owns an
 * instance of the appropriate #CamelSettings subtype, and redirects all
 * get/set operations on its own #GObject properties to its #CamelSettings
 * instance.  The #CamelSettings instance, now fully initialized from a key
 * file, can then be inserted into a new #CamelService instance using
 * camel_service_set_settings().
 *
 * Ultimately, this is all just implementation detail for glueing two
 * unrelated class hierarchies together.  If you need to access provider
 * specific settings, use the #CamelSettings API, not this.
 **/

#include "e-source-camel.h"

#include <string.h>
#include <glib/gprintf.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-source-authentication.h>
#include <libedataserver/e-source-offline.h>
#include <libedataserver/e-source-security.h>

#define E_SOURCE_CAMEL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_CAMEL, ESourceCamelPrivate))

struct _ESourceCamelPrivate {
	CamelSettings *settings;
	GArray *value_array;
};

enum {
	PROP_0,
	PROP_SETTINGS
};

typedef struct {
	const gchar *extension_name;
	const gchar *extension_property_name;
	const gchar *settings_property_name;
	GBindingTransformFunc extension_to_settings;
	GBindingTransformFunc settings_to_extension;
} BindingData;

static gboolean
transform_none_to_null (GBinding *binding,
                        const GValue *source_value,
                        GValue *target_value,
                        gpointer not_used)
{
	const gchar *v_string;

	/* XXX Camel doesn't understand ESource's convention of using
	 *     "none" to represent no value, instead of NULL or empty
	 *     strings.  So convert "none" to NULL for Camel. */

	v_string = g_value_get_string (source_value);

	if (g_strcmp0 (v_string, "none") == 0)
		v_string = NULL;

	g_value_set_string (target_value, v_string);

	return TRUE;
}

static BindingData bindings[] = {

	{ E_SOURCE_EXTENSION_AUTHENTICATION,
	  "host", "host" },

	{ E_SOURCE_EXTENSION_AUTHENTICATION,
	  "method", "auth-mechanism",
	  transform_none_to_null,
	  NULL },

	{ E_SOURCE_EXTENSION_AUTHENTICATION,
	  "port", "port" },

	{ E_SOURCE_EXTENSION_AUTHENTICATION,
	  "user", "user" },

	{ E_SOURCE_EXTENSION_OFFLINE,
	  "stay-synchronized", "stay-synchronized" },

	{ E_SOURCE_EXTENSION_SECURITY,
	  "method", "security-method",
	  e_binding_transform_enum_nick_to_value,
	  e_binding_transform_enum_value_to_nick }
};

G_DEFINE_ABSTRACT_TYPE (
	ESourceCamel,
	e_source_camel,
	E_TYPE_SOURCE_EXTENSION)

/* XXX A function like this belongs in GObject.  I may yet propose it,
 *     GParamSpecClass still has some reserved slots.  This fiddles with
 *     GParamSpec fields that are supposed to be private to GObject, but
 *     I have no other choice.
 *
 * XXX Historical note, originally I tried (ab)using override properties
 *     in ESourceCamel, which redirected to the equivalent CamelSettings
 *     property.  Seemed to work at first, and I was proud of my clever
 *     hack, but it turns out g_object_class_list_properties() excludes
 *     override properties.  So the ESourceCamel properties were being
 *     skipped in source_load_from_key_file() (e-source.c). */
static GParamSpec *
param_spec_clone (GParamSpec *pspec)
{
	GParamSpec *clone;
	GTypeQuery query;

	/* Query the instance size. */
	g_type_query (G_PARAM_SPEC_TYPE (pspec), &query);

	/* Start with a memcpy()'d buffer. */
	clone = g_slice_alloc0 (query.instance_size);
	memcpy (clone, pspec, query.instance_size);

	/* This sort of mimics g_param_spec_init(). */

#define PARAM_FLOATING_FLAG 0x2  /* from gparam.c */
	g_datalist_set_flags (&clone->qdata, PARAM_FLOATING_FLAG);
	clone->ref_count = 1;

	/* Clear the owner_type. */
	clone->owner_type = G_TYPE_INVALID;

	/* Clear the param_id. */
	clone->param_id = 0;

	/* This sort of mimics g_param_spec_internal(). */

	/* Param name should already be canonicalized and interned. */

	/* Always copy the nickname. */
	clone->flags &= ~G_PARAM_STATIC_NICK;
	clone->_nick = g_strdup (g_param_spec_get_nick (pspec));

	/* Always copy the blurb. */
	clone->flags &= ~G_PARAM_STATIC_BLURB;
	clone->_blurb = g_strdup (g_param_spec_get_blurb (pspec));

	/* Handle special cases. */

	if (G_IS_PARAM_SPEC_STRING (clone)) {
		GParamSpecString *clone_s;

		clone_s = (GParamSpecString *) clone;
		clone_s->default_value = g_strdup (clone_s->default_value);
	}

	/* Some types we don't handle but shouldn't need to. */
	g_warn_if_fail (!G_IS_PARAM_SPEC_VALUE_ARRAY (clone));
	g_warn_if_fail (!G_IS_PARAM_SPEC_OVERRIDE (clone));
	g_warn_if_fail (!G_IS_PARAM_SPEC_VARIANT (clone));

	return clone;
}

static gint
subclass_get_binding_index (GParamSpec *settings_property)
{
	gint ii;

	/* Return the index in the bindings list for the given
	 * CamelSettings property specification, or else -1. */

	for (ii = 0; ii < G_N_ELEMENTS (bindings); ii++) {
		const gchar *property_name;

		property_name = bindings[ii].settings_property_name;
		if (g_strcmp0 (settings_property->name, property_name) == 0)
			return ii;
	}

	return -1;
}

static void
subclass_set_property (GObject *object,
                       guint property_id,
                       const GValue *src_value,
                       GParamSpec *pspec)
{
	ESourceCamel *extension;
	GArray *value_array;
	GValue *dst_value;

	extension = E_SOURCE_CAMEL (object);
	value_array = extension->priv->value_array;

	dst_value = &g_array_index (value_array, GValue, property_id - 1);
	g_value_copy (src_value, dst_value);
}

static void
subclass_get_property (GObject *object,
                       guint property_id,
                       GValue *dst_value,
                       GParamSpec *pspec)
{
	ESourceCamel *extension;
	GArray *value_array;
	GValue *src_value;

	extension = E_SOURCE_CAMEL (object);
	value_array = extension->priv->value_array;

	src_value = &g_array_index (value_array, GValue, property_id - 1);
	g_value_copy (src_value, dst_value);
}

static void
subclass_class_init (gpointer g_class,
                     gpointer class_data)
{
	GObjectClass *object_class;

	/* e_source_camel_generate_subtype() does all the
	 * dynamic class initialization.  We just do what static
	 * initialization we can here. */

	object_class = G_OBJECT_CLASS (g_class);
	object_class->set_property = subclass_set_property;
	object_class->get_property = subclass_get_property;
}

static void
subclass_instance_init (GTypeInstance *instance,
                        gpointer g_class)
{
	/* Nothing to do here, just makes a handy breakpoint. */
}

static void
source_camel_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SETTINGS:
			g_value_set_object (
				value,
				e_source_camel_get_settings (
				E_SOURCE_CAMEL (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_camel_dispose (GObject *object)
{
	ESourceCamelPrivate *priv;

	priv = E_SOURCE_CAMEL_GET_PRIVATE (object);

	if (priv->settings != NULL) {
		g_object_unref (priv->settings);
		priv->settings = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_source_camel_parent_class)->dispose (object);
}

static void
source_camel_finalize (GObject *object)
{
	ESourceCamelPrivate *priv;
	guint ii;

	priv = E_SOURCE_CAMEL_GET_PRIVATE (object);

	for (ii = 0; ii < priv->value_array->len; ii++)
		g_value_unset (&g_array_index (priv->value_array, GValue, ii));

	g_array_free (priv->value_array, TRUE);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_camel_parent_class)->finalize (object);
}

static void
source_camel_constructed (GObject *object)
{
	ESource *source;
	ESourceCamelClass *class;
	ESourceCamelPrivate *priv;
	GObjectClass *settings_class;
	GParamSpec **properties;
	guint ii, n_properties;
	guint array_index = 0;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_source_camel_parent_class)->
		constructed (object);

	class = E_SOURCE_CAMEL_GET_CLASS (object);
	priv = E_SOURCE_CAMEL_GET_PRIVATE (object);

	source = e_source_extension_ref_source (E_SOURCE_EXTENSION (object));

	priv->settings = g_object_new (class->settings_type, NULL);

	/* Here we bind all the GObject properties in the newly-created
	 * CamelSettings instance to either our own identical properties
	 * or properties in another ESourceExtensions.  The bindings list
	 * at the top of the file maps out bindings to other extensions. */

	settings_class = G_OBJECT_GET_CLASS (priv->settings);

	properties = g_object_class_list_properties (
		settings_class, &n_properties);

	/* Allocate more elements than we need, since some CamelSettings
	 * properties get bound to properties of other ESourceExtensions.
	 * We'll trim off the extra elements later. */
	g_array_set_size (priv->value_array, n_properties);

	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec = properties[ii];
		GBindingTransformFunc transform_to = NULL;
		GBindingTransformFunc transform_from = NULL;
		ESourceExtension *extension;
		const gchar *source_property;
		const gchar *target_property;
		gint binding_index;

		binding_index = subclass_get_binding_index (pspec);

		/* Bind the CamelSettings property to
		 * one in a different ESourceExtension. */
		if (binding_index >= 0) {
			BindingData *binding;

			binding = &bindings[binding_index];

			extension = e_source_get_extension (
				source, binding->extension_name);

			source_property = binding->extension_property_name;
			target_property = binding->settings_property_name;

			transform_to = binding->extension_to_settings;
			transform_from = binding->settings_to_extension;

		/* Bind the CamelSettings property to our own
		 * equivalent E_SOURCE_PARAM_SETTING property. */
		} else {
			GValue *value;

			extension = E_SOURCE_EXTENSION (object);

			source_property = pspec->name;
			target_property = pspec->name;

			/* Initialize the array element to
			 * hold the GParamSpec's value type. */
			value = &g_array_index (
				priv->value_array, GValue, array_index++);
			g_value_init (value, G_PARAM_SPEC_VALUE_TYPE (pspec));

			/* Set the array element to the GParamSpec's default
			 * value.  This allows us to avoid declaring our own
			 * properties with a G_PARAM_CONSTRUCT flag. */
			g_param_value_set_default (pspec, value);
		}

		g_object_bind_property_full (
			extension, source_property,
			priv->settings, target_property,
			G_BINDING_BIDIRECTIONAL |
			G_BINDING_SYNC_CREATE,
			transform_to, transform_from,
			NULL, (GDestroyNotify) NULL);
	}

	/* Trim off any extra array elements. */
	g_array_set_size (priv->value_array, array_index);

	g_free (properties);

	g_object_unref (source);
}

static void
e_source_camel_class_init (ESourceCamelClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (ESourceCamelPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->get_property = source_camel_get_property;
	object_class->dispose = source_camel_dispose;
	object_class->finalize = source_camel_finalize;
	object_class->constructed = source_camel_constructed;

	/* CamelSettings itself has no properties. */
	class->settings_type = CAMEL_TYPE_SETTINGS;

	/* XXX This kind of stomps on CamelSettings' namespace, but it's
	 *     unlikely a CamelSettings subclass would define a property
	 *     named "settings". */
	g_object_class_install_property (
		object_class,
		PROP_SETTINGS,
		g_param_spec_object (
			"settings",
			"Settings",
			"The CamelSettings instance being proxied",
			CAMEL_TYPE_SETTINGS,
			G_PARAM_READABLE |
			G_PARAM_STATIC_STRINGS));
}

static void
e_source_camel_init (ESourceCamel *extension)
{
	GArray *value_array;

	/* Zero-fill array elements when they are allocated. */
	value_array = g_array_new (FALSE, TRUE, sizeof (GValue));

	extension->priv = E_SOURCE_CAMEL_GET_PRIVATE (extension);
	extension->priv->value_array = value_array;
}

/* Helper for e_source_camel_register_types() */
static gpointer
source_camel_register_types_once (gpointer unused)
{
	GList *list, *link;

	/* This implicitly takes care of provider initialization. */
	list = camel_provider_list (TRUE);

	for (link = list; link != NULL; link = g_list_next (link)) {
		CamelProvider *provider;
		gint ii;

		provider = (CamelProvider *) link->data;

		/* This is the novel part: generate and register
		 * a new ESourceCamel subclass on-the-fly for each
		 * object type listed in the provider. */
		for (ii = 0; ii < CAMEL_NUM_PROVIDER_TYPES; ii++) {
			CamelServiceClass *service_class = NULL;
			GType service_type;

			service_type = provider->object_types[ii];

			if (g_type_is_a (service_type, CAMEL_TYPE_SERVICE))
				service_class = g_type_class_ref (service_type);

			if (service_class != NULL) {
				e_source_camel_generate_subtype (
					provider->protocol,
					service_class->settings_type);
				g_type_class_unref (service_class);
			}
		}
	}

	g_list_free (list);

	return NULL;
}

/**
 * e_source_camel_register_types:
 *
 * Creates and registers subclasses of #ESourceCamel for each available
 * #CamelProvider.  This function should be called once during application
 * or library initialization.
 *
 * Since: 3.6
 **/
void
e_source_camel_register_types (void)
{
	static GOnce register_types_once = G_ONCE_INIT;

	g_once (&register_types_once, source_camel_register_types_once, NULL);
}

/**
 * e_source_camel_generate_subtype:
 * @protocol: a #CamelProvider protocol
 * @settings_type: a subtype of #CamelSettings
 *
 * Generates a custom #ESourceCamel subtype for @protocol.  Instances of the
 * new subtype will contain a #CamelSettings instance of type @settings_type.
 *
 * This function is called as part of e_source_camel_register_types() and
 * should not be called explicitly, except by some groupware packages that
 * need to share package-specific settings across their mail, calendar and
 * address book components.  In that case the groupware package may choose
 * to subclass #CamelSettings rather than #ESourceExtension since libcamel
 * is the lowest common denominator across all components.  This function
 * provides a way for the calendar and address book components of such a
 * package to generate an #ESourceCamel subtype for its #CamelSettings
 * subtype without having to load all available #CamelProvider modules.
 *
 * Returns: the #GType of the generated #ESourceCamel subtype
 *
 * Since: 3.6
 **/
GType
e_source_camel_generate_subtype (const gchar *protocol,
                                 GType settings_type)
{
	ESourceCamelClass *class;
	GObjectClass *settings_class;
	GParamSpec **properties;
	guint ii, n_properties;
	guint prop_id = 1;
	GTypeInfo type_info;
	GType parent_type;
	GType type;
	const gchar *type_name;
	const gchar *extension_name;

	g_return_val_if_fail (protocol != NULL, G_TYPE_INVALID);

	type_name = e_source_camel_get_type_name (protocol);
	extension_name = e_source_camel_get_extension_name (protocol);

	/* Check if the type name is already registered. */
	type = g_type_from_name (type_name);
	if (type != G_TYPE_INVALID)
		return type;

	/* The settings type must be derived from CAMEL_TYPE_SETTINGS. */
	if (!g_type_is_a (settings_type, CAMEL_TYPE_SETTINGS)) {
		g_warning (
			"%s: Invalid settings type '%s' for protocol '%s'",
			G_STRFUNC, g_type_name (settings_type), protocol);
		return G_TYPE_INVALID;
	}

	memset (&type_info, 0, sizeof (GTypeInfo));
	type_info.class_size = sizeof (ESourceCamelClass);
	type_info.class_init = subclass_class_init;
	type_info.instance_size = sizeof (ESourceCamel);
	type_info.instance_init = subclass_instance_init;

	parent_type = E_TYPE_SOURCE_CAMEL;
	type = g_type_register_static (parent_type, type_name, &type_info, 0);

	/* Since we have first access to the newly registered GType, and
	 * because initializing its class structure requires some of the
	 * arguments we were passed, we'll complete class initialization
	 * here rather than trying to do it all in subclass_init(). */

	class = g_type_class_ref (type);
	settings_class = g_type_class_ref (settings_type);

	/* Initialize more class members. */
	class->settings_type = G_OBJECT_CLASS_TYPE (settings_class);
	class->parent_class.name = g_intern_string (extension_name);

	/* For each property in the CamelSettings class, register
	 * an equivalent GObject property in this class and add an
	 * E_SOURCE_PARAM_SETTING flag so the value gets written to
	 * the ESource's key file. */

	properties = g_object_class_list_properties (
		settings_class, &n_properties);

	for (ii = 0; ii < n_properties; ii++) {
		GParamSpec *pspec;

		/* Some properties in CamelSettings may be covered
		 * by other ESourceExtensions.  Skip them here. */
		if (subclass_get_binding_index (properties[ii]) >= 0)
			continue;

		pspec = param_spec_clone (properties[ii]);
		pspec->flags |= E_SOURCE_PARAM_SETTING;

		/* Clear the G_PARAM_CONSTRUCT flag.  We apply default
		 * property values to our GValue array during instance
		 * initialization. */
		pspec->flags &= ~G_PARAM_CONSTRUCT;

		g_object_class_install_property (
			G_OBJECT_CLASS (class), prop_id++, pspec);
	}

	g_free (properties);

	g_type_class_unref (class);
	g_type_class_unref (settings_class);

	return type;
}

/**
 * e_source_camel_get_settings:
 * @extension: an #ESourceCamel
 *
 * Returns @extension's #ESourceCamel:settings instance, pre-configured
 * from the #ESource to which @extension belongs.  Changes to the #ESource
 * will automatically propagate to the #ESourceCamel:settings instance and
 * vice versa.
 *
 * This is essentially the glue that binds #ESource to #CamelService.
 * See e_source_camel_configure_service().
 *
 * Returns: a configured #CamelSettings instance
 *
 * Since: 3.6
 **/
CamelSettings *
e_source_camel_get_settings (ESourceCamel *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_CAMEL (extension), NULL);

	return extension->priv->settings;
}

/**
 * e_source_camel_get_type_name:
 * @protocol: a #CamelProvider protocol
 *
 * Returns the #GType name of the registered #ESourceCamel subtype for
 * @protocol.
 *
 * For example, given a protocol named "imap" the function would return
 * "ESourceCamelImap".
 *
 * Returns: the #ESourceCamel type name for @protocol
 *
 * Since: 3.6
 **/
const gchar *
e_source_camel_get_type_name (const gchar *protocol)
{
	gchar *buffer;

	g_return_val_if_fail (protocol != NULL, NULL);

	buffer = g_alloca (strlen (protocol) + 16);
	g_sprintf (buffer, "ESourceCamel%s", protocol);
	buffer[12] = g_ascii_toupper (buffer[12]);

	return g_intern_string (buffer);
}

/**
 * e_source_camel_get_extension_name:
 * @protocol: a #CamelProvider protocol
 *
 * Returns the extension name for the #ESourceCamel subtype for @protocol.
 * The extension name can then be passed to e_source_get_extension() to
 * obtain an instance of the #ESourceCamel subtype.
 *
 * For example, given a protocol named "imap" the function would return
 * "Imap Backend".
 *
 * Returns: the #ESourceCamel extension name for @protocol
 *
 * Since: 3.6
 **/
const gchar *
e_source_camel_get_extension_name (const gchar *protocol)
{
	gchar *buffer;

	g_return_val_if_fail (protocol != NULL, NULL);

	/* Use the term "backend" for consistency with other
	 * calendar and address book backend extension names. */
	buffer = g_alloca (strlen (protocol) + 16);
	g_sprintf (buffer, "%s Backend", protocol);
	buffer[0] = g_ascii_toupper (buffer[0]);

	return g_intern_string (buffer);
}

/**
 * e_source_camel_configure_service:
 * @source: an #ESource
 * @service: a #CamelService
 *
 * This function essentially glues together @source and @serivce so their
 * configuration settings stay synchronized.  The glue itself is a shared
 * #CamelSettings instance.
 *
 * Call this function immediately after creating a new #CamelService with
 * camel_session_add_service().
 *
 * Since: 3.6
 **/
void
e_source_camel_configure_service (ESource *source,
                                  CamelService *service)
{
	ESourceCamel *extension;
	CamelProvider *provider;
	CamelSettings *settings;
	const gchar *extension_name;

	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (CAMEL_IS_SERVICE (service));

	provider = camel_service_get_provider (service);
	g_return_if_fail (provider != NULL);

	extension_name =
		e_source_camel_get_extension_name (provider->protocol);
	extension = e_source_get_extension (source, extension_name);

	settings = e_source_camel_get_settings (extension);
	camel_service_set_settings (service, settings);
}

