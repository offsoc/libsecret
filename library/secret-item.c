/* libsecret - GLib wrapper for Secret Service
 *
 * Copyright 2012 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 */

#include "config.h"

#include "secret-collection.h"
#include "secret-dbus-generated.h"
#include "secret-item.h"
#include "secret-private.h"
#include "secret-service.h"
#include "secret-types.h"
#include "secret-value.h"

#include <glib/gi18n-lib.h>

/**
 * SECTION:secret-item
 * @title: SecretItem
 * @short_description: A secret item
 *
 * #SecretItem represents a secret item stored in the Secret Service.
 *
 * Each item has a value, represented by a #SecretValue, which can be
 * retrieved by secret_item_get_secret() or set by secret_item_set_secret().
 * The item is only available when the item is not locked.
 *
 * Items can be locked or unlocked using the secret_service_lock() or
 * secret_service_unlock() functions. The Secret Service may not be able to
 * unlock individual items, and may unlock an entire collection when a single
 * item is unlocked.
 *
 * Each item has a set of attributes, which are used to locate the item later.
 * These are not stored or transferred in a secure manner. Each attribute has
 * a string name and a string value. Use secret_service_search() to search for
 * items based on their attributes, and secret_item_set_attributes to change
 * the attributes associated with an item.
 *
 * Items can be created with secret_item_create() or secret_service_store().
 */

/**
 * SecretItem:
 *
 * A proxy object representing a secret item in the Secret Service.
 */

/**
 * SecretItemClass:
 * @parent_class: the parent class
 *
 * The class for #SecretItem.
 */

enum {
	PROP_0,
	PROP_SERVICE,
	PROP_ATTRIBUTES,
	PROP_LABEL,
	PROP_SCHEMA,
	PROP_LOCKED,
	PROP_CREATED,
	PROP_MODIFIED
};

/* Thread safe: no changes between construct and finalize */
typedef struct _SecretItemPrivate {
	SecretService *service;
	GCancellable *cancellable;
} SecretItemPrivate;

static GInitableIface *secret_item_initable_parent_iface = NULL;

static GAsyncInitableIface *secret_item_async_initable_parent_iface = NULL;

static void   secret_item_initable_iface         (GInitableIface *iface);

static void   secret_item_async_initable_iface   (GAsyncInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (SecretItem, secret_item, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, secret_item_initable_iface);
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, secret_item_async_initable_iface);
);

static void
secret_item_init (SecretItem *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, SECRET_TYPE_ITEM, SecretItemPrivate);
	self->pv->cancellable = g_cancellable_new ();
}

static void
on_set_attributes (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	SecretItem *self = SECRET_ITEM (user_data);
	GError *error = NULL;

	secret_item_set_attributes_finish (self, result, &error);
	if (error != NULL) {
		g_warning ("couldn't set SecretItem Attributes: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (self);
}

static void
on_set_label (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	SecretItem *self = SECRET_ITEM (user_data);
	GError *error = NULL;

	secret_item_set_label_finish (self, result, &error);
	if (error != NULL) {
		g_warning ("couldn't set SecretItem Label: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (self);
}

static void
secret_item_set_property (GObject *obj,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
	SecretItem *self = SECRET_ITEM (obj);

	switch (prop_id) {
	case PROP_SERVICE:
		g_return_if_fail (self->pv->service == NULL);
		self->pv->service = g_value_get_object (value);
		if (self->pv->service)
			g_object_add_weak_pointer (G_OBJECT (self->pv->service),
			                           (gpointer *)&self->pv->service);
		break;
	case PROP_ATTRIBUTES:
		secret_item_set_attributes (self, g_value_get_boxed (value),
		                            self->pv->cancellable, on_set_attributes,
		                            g_object_ref (self));
		break;
	case PROP_LABEL:
		secret_item_set_label (self, g_value_get_string (value),
		                       self->pv->cancellable, on_set_label,
		                       g_object_ref (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
secret_item_get_property (GObject *obj,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
	SecretItem *self = SECRET_ITEM (obj);

	switch (prop_id) {
	case PROP_SERVICE:
		g_value_set_object (value, self->pv->service);
		break;
	case PROP_ATTRIBUTES:
		g_value_take_boxed (value, secret_item_get_attributes (self));
		break;
	case PROP_LABEL:
		g_value_take_string (value, secret_item_get_label (self));
		break;
	case PROP_SCHEMA:
		g_value_take_string (value, secret_item_get_schema (self));
		break;
	case PROP_LOCKED:
		g_value_set_boolean (value, secret_item_get_locked (self));
		break;
	case PROP_CREATED:
		g_value_set_uint64 (value, secret_item_get_created (self));
		break;
	case PROP_MODIFIED:
		g_value_set_uint64 (value, secret_item_get_modified (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
secret_item_dispose (GObject *obj)
{
	SecretItem *self = SECRET_ITEM (obj);

	g_cancellable_cancel (self->pv->cancellable);

	G_OBJECT_CLASS (secret_item_parent_class)->dispose (obj);
}

static void
secret_item_finalize (GObject *obj)
{
	SecretItem *self = SECRET_ITEM (obj);

	if (self->pv->service)
		g_object_remove_weak_pointer (G_OBJECT (self->pv->service),
		                              (gpointer *)&self->pv->service);

	g_object_unref (self->pv->cancellable);

	G_OBJECT_CLASS (secret_item_parent_class)->finalize (obj);
}

static void
handle_property_changed (GObject *object,
                         const gchar *property_name)
{
	if (g_str_equal (property_name, "Attributes"))
		g_object_notify (object, "attributes");

	else if (g_str_equal (property_name, "Label"))
		g_object_notify (object, "label");

	else if (g_str_equal (property_name, "Type"))
		g_object_notify (object, "schema");

	else if (g_str_equal (property_name, "Locked"))
		g_object_notify (object, "locked");

	else if (g_str_equal (property_name, "Created"))
		g_object_notify (object, "created");

	else if (g_str_equal (property_name, "Modified"))
		g_object_notify (object, "modified");
}

static void
secret_item_properties_changed (GDBusProxy *proxy,
                                GVariant *changed_properties,
                                const gchar* const *invalidated_properties)
{
	GObject *obj = G_OBJECT (proxy);
	gchar *property_name;
	GVariantIter iter;
	GVariant *value;

	g_object_freeze_notify (obj);

	g_variant_iter_init (&iter, changed_properties);
	while (g_variant_iter_loop (&iter, "{sv}", &property_name, &value))
		handle_property_changed (obj, property_name);

	g_object_thaw_notify (obj);
}

static void
secret_item_class_init (SecretItemClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GDBusProxyClass *proxy_class = G_DBUS_PROXY_CLASS (klass);

	gobject_class->get_property = secret_item_get_property;
	gobject_class->set_property = secret_item_set_property;
	gobject_class->dispose = secret_item_dispose;
	gobject_class->finalize = secret_item_finalize;

	proxy_class->g_properties_changed = secret_item_properties_changed;

	/**
	 * SecretItem:service:
	 *
	 * The #SecretService object that this item is associated with and
	 * uses to interact with the actual DBus Secret Service.
	 */
	g_object_class_install_property (gobject_class, PROP_SERVICE,
	            g_param_spec_object ("service", "Service", "Secret Service",
	                                 SECRET_TYPE_SERVICE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	/**
	 * SecretItem:attributes:
	 *
	 * The attributes set on this item. Attributes are used to locate an
	 * item. They are not guaranteed to be stored or transferred securely.
	 *
	 * The schema describes which attributes should be present and their types.
	 */
	g_object_class_install_property (gobject_class, PROP_ATTRIBUTES,
	             g_param_spec_boxed ("attributes", "Attributes", "Item attributes",
	                                 G_TYPE_HASH_TABLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * SecretItem:label:
	 *
	 * The human readable label for the item.
	 *
	 * Setting this property will result in the label of the item being
	 * set asynchronously. To properly track the changing of the label use the
	 * secret_item_set_label() function.
	 */
	g_object_class_install_property (gobject_class, PROP_LABEL,
	            g_param_spec_string ("label", "Label", "Item label",
	                                 NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * SecretItem:schema:
	 *
	 * The schema for this item. This is a dotted string that describes
	 * which attributes should be present and the types of values on
	 * those attributes.
	 */
	g_object_class_install_property (gobject_class, PROP_SCHEMA,
	            g_param_spec_string ("schema", "Schema", "Item schema",
	                                 NULL, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	/**
	 * SecretItem:locked:
	 *
	 * Whether the item is locked or not. An item may not be independently
	 * lockable separate from other items in its collection.
	 *
	 * To lock or unlock a item use the secret_service_lock() or
	 * secret_service_unlock() functions.
	 */
	g_object_class_install_property (gobject_class, PROP_LOCKED,
	           g_param_spec_boolean ("locked", "Locked", "Item locked",
	                                 TRUE, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	/**
	 * SecretItem:created:
	 *
	 * The date and time (in seconds since the UNIX epoch) that this
	 * item was created.
	 */
	g_object_class_install_property (gobject_class, PROP_CREATED,
	            g_param_spec_uint64 ("created", "Created", "Item creation date",
	                                 0UL, G_MAXUINT64, 0UL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * SecretItem:modified:
	 *
	 * The date and time (in seconds since the UNIX epoch) that this
	 * item was last modified.
	 */
	g_object_class_install_property (gobject_class, PROP_MODIFIED,
	            g_param_spec_uint64 ("modified", "Modified", "Item modified date",
	                                 0UL, G_MAXUINT64, 0UL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	g_type_class_add_private (gobject_class, sizeof (SecretItemPrivate));
}

static gboolean
secret_item_initable_init (GInitable *initable,
                           GCancellable *cancellable,
                           GError **error)
{
	GDBusProxy *proxy;

	if (!secret_item_initable_parent_iface->init (initable, cancellable, error))
		return FALSE;

	proxy = G_DBUS_PROXY (initable);

	if (!_secret_util_have_cached_properties (proxy)) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
		             "No such secret item at path: %s",
		             g_dbus_proxy_get_object_path (proxy));
		return FALSE;
	}

	return TRUE;
}

static void
secret_item_initable_iface (GInitableIface *iface)
{
	secret_item_initable_parent_iface = g_type_interface_peek_parent (iface);

	iface->init = secret_item_initable_init;
}

static void
on_init_base (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretItem *self = SECRET_ITEM (source);
	GDBusProxy *proxy = G_DBUS_PROXY (self);
	GError *error = NULL;

	if (!secret_item_async_initable_parent_iface->init_finish (G_ASYNC_INITABLE (self),
	                                                           result, &error)) {
		g_simple_async_result_take_error (res, error);

	} else if (!_secret_util_have_cached_properties (proxy)) {
		g_simple_async_result_set_error (res, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
		                                 "No such secret item at path: %s",
		                                 g_dbus_proxy_get_object_path (proxy));
	}

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
secret_item_async_initable_init_async (GAsyncInitable *initable,
                                       int io_priority,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	GSimpleAsyncResult *res;

	res = g_simple_async_result_new (G_OBJECT (initable), callback, user_data,
	                                 secret_item_async_initable_init_async);

	secret_item_async_initable_parent_iface->init_async (initable, io_priority,
	                                                     cancellable,
	                                                     on_init_base,
	                                                     g_object_ref (res));

	g_object_unref (res);
}

static gboolean
secret_item_async_initable_init_finish (GAsyncInitable *initable,
                                        GAsyncResult *result,
                                        GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (initable),
	                      secret_item_async_initable_init_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
secret_item_async_initable_iface (GAsyncInitableIface *iface)
{
	secret_item_async_initable_parent_iface = g_type_interface_peek_parent (iface);

	iface->init_async = secret_item_async_initable_init_async;
	iface->init_finish = secret_item_async_initable_init_finish;
}

/**
 * secret_item_new:
 * @service: a secret service object
 * @item_path: the dbus path of the collection
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to be passed to the callback
 *
 * Get a new item proxy for a secret item in the secret service.
 *
 * This method will return immediately and complete asynchronously.
 */
void
secret_item_new (SecretService *service,
                 const gchar *item_path,
                 GCancellable *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer user_data)
{
	GDBusProxy *proxy;

	g_return_if_fail (SECRET_IS_SERVICE (service));
	g_return_if_fail (item_path != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	proxy = G_DBUS_PROXY (service);

	g_async_initable_new_async (SECRET_SERVICE_GET_CLASS (service)->item_gtype,
	                            G_PRIORITY_DEFAULT, cancellable, callback, user_data,
	                            "g-flags", G_DBUS_CALL_FLAGS_NONE,
	                            "g-interface-info", _secret_gen_item_interface_info (),
	                            "g-name", g_dbus_proxy_get_name (proxy),
	                            "g-connection", g_dbus_proxy_get_connection (proxy),
	                            "g-object-path", item_path,
	                            "g-interface-name", SECRET_ITEM_INTERFACE,
	                            "service", service,
	                            NULL);
}

/**
 * secret_item_new_finish:
 * @result: the asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Finish asynchronous operation to get a new item proxy for an secret
 * item in the secret service.
 *
 * Returns: (transfer full): the new item, which should be unreferenced
 *          with g_object_unref()
 */
SecretItem *
secret_item_new_finish (GAsyncResult *result,
                        GError **error)
{
	GObject *object;
	GObject *source_object;

	source_object = g_async_result_get_source_object (result);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
	                                      result, error);
	g_object_unref (source_object);

	if (object == NULL)
		return NULL;

	return SECRET_ITEM (object);
}

/**
 * secret_item_new_sync:
 * @service: a secret service object
 * @item_path: the dbus path of the item
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Get a new item proxy for a secret item in the secret service.
 *
 * This method may block indefinitely and should not be used in user interface
 * threads.
 *
 * Returns: (transfer full): the new item, which should be unreferenced
 *          with g_object_unref()
 */
SecretItem *
secret_item_new_sync (SecretService *service,
                      const gchar *item_path,
                      GCancellable *cancellable,
                      GError **error)
{
	GDBusProxy *proxy;

	g_return_val_if_fail (SECRET_IS_SERVICE (service), NULL);
	g_return_val_if_fail (item_path != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	proxy = G_DBUS_PROXY (service);

	return g_initable_new (SECRET_SERVICE_GET_CLASS (service)->item_gtype,
	                       cancellable, error,
	                       "g-flags", G_DBUS_CALL_FLAGS_NONE,
	                       "g-interface-info", _secret_gen_item_interface_info (),
	                       "g-name", g_dbus_proxy_get_name (proxy),
	                       "g-connection", g_dbus_proxy_get_connection (proxy),
	                       "g-object-path", item_path,
	                       "g-interface-name", SECRET_ITEM_INTERFACE,
	                       "service", service,
	                       NULL);
}

/**
 * secret_item_refresh:
 * @self: the collection
 *
 * Refresh the properties on this item. This fires off a request to
 * refresh, and the properties will be updated later.
 *
 * Calling this method is not normally necessary, as the secret service
 * will notify the client when properties change.
 */
void
secret_item_refresh (SecretItem *self)
{
	g_return_if_fail (SECRET_IS_ITEM (self));

	_secret_util_get_properties (G_DBUS_PROXY (self),
	                             secret_item_refresh,
	                             NULL, NULL, NULL);
}


typedef struct {
	GCancellable *cancellable;
	SecretItem *item;
} CreateClosure;

static void
create_closure_free (gpointer data)
{
	CreateClosure *closure = data;
	g_clear_object (&closure->cancellable);
	g_clear_object (&closure->item);
	g_slice_free (CreateClosure, closure);
}

static void
on_create_item (GObject *source,
                GAsyncResult *result,
                gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	CreateClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	GError *error = NULL;

	closure->item = secret_item_new_finish (result, &error);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_create_path (GObject *source,
                GAsyncResult *result,
                gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	CreateClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	SecretService *service = SECRET_SERVICE (source);
	GError *error = NULL;
	gchar *path;

	path = secret_service_create_item_path_finish (service, result, &error);
	if (error == NULL) {
		secret_item_new (service, path, closure->cancellable,
		                 on_create_item, g_object_ref (res));
	} else {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	}

	g_object_unref (res);
}

static GHashTable *
item_properties_new (const gchar *schema_name,
                     const gchar *label,
                     GHashTable *attributes)
{
	GHashTable *properties;
	GVariant *value;

	properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
	                                    (GDestroyNotify)g_variant_unref);

	value = g_variant_new_string (label);
	g_hash_table_insert (properties,
	                     SECRET_ITEM_INTERFACE ".Label",
	                     g_variant_ref_sink (value));

	value = g_variant_new_string (schema_name);
	g_hash_table_insert (properties,
	                     SECRET_ITEM_INTERFACE ".Schema",
	                     g_variant_ref_sink (value));

	value = _secret_util_variant_for_attributes (attributes);
	g_hash_table_insert (properties,
	                     SECRET_ITEM_INTERFACE ".Attributes",
	                     g_variant_ref_sink (value));

	return properties;
}

/**
 * secret_item_create:
 * @collection: a secret collection to create this item in
 * @schema_name: schema name for the new item
 * @label: label for the new item
 * @attributes: (element-type utf8 utf8): attributes for the new item
 * @value: secret value for the new item
 * @replace: whether to replace an existing item with the same attributes
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to pass to the callback
 *
 * Create a new item in the secret service.
 *
 * If the @replace is set to %TRUE, then the secret service will search for
 * an item matching the @attributes, and update that item instead of creating
 * a new one.
 *
 * This method may block indefinitely and should not be used in user interface
 * threads. The secret service may prompt the user. secret_service_prompt()
 * will be used to handle any prompts that are required.
 */
void
secret_item_create (SecretCollection *collection,
                    const gchar *schema_name,
                    const gchar *label,
                    GHashTable *attributes,
                    SecretValue *value,
                    gboolean replace,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	SecretService *service = NULL;
	const gchar *collection_path;
	GSimpleAsyncResult *res;
	CreateClosure *closure;
	GHashTable *properties;

	g_return_if_fail (SECRET_IS_COLLECTION (collection));
	g_return_if_fail (label != NULL);
	g_return_if_fail (attributes != NULL);
	g_return_if_fail (value != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (NULL, callback, user_data,
	                                 secret_item_create);
	closure = g_slice_new0 (CreateClosure);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	g_simple_async_result_set_op_res_gpointer (res, closure, create_closure_free);

	properties = item_properties_new (schema_name, label, attributes);
	g_object_get (collection, "service", &service, NULL);

	collection_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (collection));

	secret_service_create_item_path (service, collection_path, properties,
	                                 value, replace, cancellable,
	                                 on_create_path, g_object_ref (res));

	g_hash_table_unref (properties);
	g_object_unref (service);
	g_object_unref (res);
}

/**
 * secret_item_create_finish:
 * @result: the asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Finish operation to create a new item in the secret service.
 *
 * Returns: (transfer full): the new item, which should be unreferenced
 *          with g_object_unref()
 */
SecretItem *
secret_item_create_finish (GAsyncResult *result,
                           GError **error)
{
	GSimpleAsyncResult *res;
	CreateClosure *closure;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL,
	                      secret_item_create), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return NULL;

	closure = g_simple_async_result_get_op_res_gpointer (res);
	if (closure->item == NULL)
		return NULL;

	return g_object_ref (closure->item);
}

/**
 * secret_item_create_sync:
 * @collection: a secret collection to create this item in
 * @schema_name: schema name for the new item
 * @label: label for the new item
 * @attributes: (element-type utf8 utf8): attributes for the new item
 * @value: secret value for the new item
 * @replace: whether to replace an existing item with the same attributes
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Create a new item in the secret service.
 *
 * If the @replace is set to %TRUE, then the secret service will search for
 * an item matching the @attributes, and update that item instead of creating
 * a new one.
 *
 * This method may block indefinitely and should not be used in user interface
 * threads. The secret service may prompt the user. secret_service_prompt()
 * will be used to handle any prompts that are required.
 *
 * Returns: (transfer full): the new item, which should be unreferenced
 *          with g_object_unref()
 */
SecretItem *
secret_item_create_sync (SecretCollection *collection,
                         const gchar *schema_name,
                         const gchar *label,
                         GHashTable *attributes,
                         SecretValue *value,
                         gboolean replace,
                         GCancellable *cancellable,
                         GError **error)
{
	SecretService *service = NULL;
	const gchar *collection_path;
	SecretItem *item = NULL;
	GHashTable *properties;
	gchar *path;

	g_return_val_if_fail (SECRET_IS_COLLECTION (collection), NULL);
	g_return_val_if_fail (label != NULL, NULL);
	g_return_val_if_fail (attributes != NULL, NULL);
	g_return_val_if_fail (value != NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	properties = item_properties_new (schema_name, label, attributes);
	g_object_get (collection, "service", &service, NULL);

	collection_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (collection));

	path = secret_service_create_item_path_sync (service, collection_path, properties,
	                                             value, replace, cancellable, error);

	if (path != NULL) {
		item = secret_item_new_sync (service, path, cancellable, error);
		g_free (path);
	}

	g_hash_table_unref (properties);
	g_object_unref (service);

	return item;
}

static void
on_item_deleted (GObject *source,
                 GAsyncResult *result,
                 gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretItem *self = SECRET_ITEM (g_async_result_get_source_object (user_data));
	GError *error = NULL;

	if (secret_service_delete_path_finish (SECRET_SERVICE (source), result, &error)) {
		g_simple_async_result_set_op_res_gboolean (res, TRUE);
		g_object_run_dispose (G_OBJECT (self));
	}

	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (self);
	g_object_unref (res);
}

/**
 * secret_collection_delete:
 * @self: a collection
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to pass to the callback
 *
 * Delete this collection.
 *
 * This method returns immediately and completes asynchronously. The secret
 * service may prompt the user. secret_service_prompt() will be used to handle
 * any prompts that show up.
 */
void
secret_item_delete (SecretItem *self,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	GSimpleAsyncResult *res;
	const gchar *object_path;

	g_return_if_fail (SECRET_IS_ITEM (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	object_path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (self));
	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 secret_item_delete);

	_secret_service_delete_path (self->pv->service, object_path, TRUE,
	                             cancellable, on_item_deleted, g_object_ref (res));

	g_object_unref (res);
}

/**
 * secret_item_delete_finish:
 * @self: an item
 * @result: asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Complete asynchronous operation to delete the secret item.
 *
 * Returns: whether the item was successfully deleted or not
 */
gboolean
secret_item_delete_finish (SecretItem *self,
                           GAsyncResult *result,
                           GError **error)
{
	GSimpleAsyncResult *res;

	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      secret_item_delete), FALSE);

	res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (res);
}

/**
 * secret_item_delete_sync:
 * @self: an item
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Delete this secret item.
 *
 * This method may block indefinitely and should not be used in user
 * interface threads. The secret service may prompt the user.
 * secret_service_prompt() will be used to handle any prompts that show up.
 *
 * Returns: whether the item was successfully deleted or not
 */
gboolean
secret_item_delete_sync (SecretItem *self,
                         GCancellable *cancellable,
                         GError **error)
{
	SecretSync *sync;
	gboolean ret;

	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	sync = _secret_sync_new ();
	g_main_context_push_thread_default (sync->context);

	secret_item_delete (self, cancellable, _secret_sync_on_result, sync);

	g_main_loop_run (sync->loop);

	ret = secret_item_delete_finish (self, sync->result, error);

	g_main_context_pop_thread_default (sync->context);
	_secret_sync_free (sync);

	return ret;
}

typedef struct {
	GCancellable *cancellable;
	SecretValue *value;
} GetClosure;

static void
get_closure_free (gpointer data)
{
	GetClosure *closure = data;
	g_clear_object (&closure->cancellable);
	secret_value_unref (closure->value);
	g_slice_free (GetClosure, closure);
}

static void
on_item_get_secret (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretItem *self = SECRET_ITEM (g_async_result_get_source_object (user_data));
	GetClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	SecretSession *session;
	GError *error = NULL;
	GVariant *retval;
	GVariant *child;

	retval = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);
	if (error == NULL) {
		child = g_variant_get_child_value (retval, 0);
		g_variant_unref (retval);

		session = _secret_service_get_session (self->pv->service);
		closure->value = _secret_session_decode_secret (session, child);
		g_variant_unref (child);

		if (closure->value == NULL)
			g_set_error (&error, SECRET_ERROR, SECRET_ERROR_PROTOCOL,
			             _("Received invalid secret from the secret storage"));
	}

	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_get_ensure_session (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretItem *self = SECRET_ITEM (g_async_result_get_source_object (user_data));
	GetClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	const gchar *session_path;
	GError *error = NULL;

	session_path = secret_service_ensure_session_finish (self->pv->service, result, &error);
	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	} else {
		g_assert (session_path != NULL && session_path[0] != '\0');
		g_dbus_proxy_call (G_DBUS_PROXY (self), "GetSecret",
		                   g_variant_new ("(o)", session_path),
		                   G_DBUS_CALL_FLAGS_NONE, -1, closure->cancellable,
		                   on_item_get_secret, g_object_ref (res));
	}

	g_object_unref (self);
	g_object_unref (res);
}

/**
 * secret_item_get_secret:
 * @self: an item
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to pass to the callback
 *
 * Get the secret value of this item.
 *
 * Each item has a single secret which might be a password or some
 * other secret binary value.
 *
 * This function returns immediately and completes asynchronously.
 */
void
secret_item_get_secret (SecretItem *self,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *res;
	GetClosure *closure;

	g_return_if_fail (SECRET_IS_ITEM (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (self), callback,
	                                 user_data, secret_item_get_secret);
	closure = g_slice_new0 (GetClosure);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	g_simple_async_result_set_op_res_gpointer (res, closure, get_closure_free);

	secret_service_ensure_session (self->pv->service, cancellable,
	                               on_get_ensure_session,
	                               g_object_ref (res));

	g_object_unref (res);
}

/**
 * secret_item_get_secret_finish:
 * @self: an item
 * @result: asynchronous result passed to callback
 * @error: location to place error on failure
 *
 * Get the secret value of this item.
 *
 * Complete asynchronous operation to get the secret value of this item.
 *
 * Returns: (transfer full): the newly allocated secret value in this
 *          item, which should be released with secret_value_unref()
 */
SecretValue *
secret_item_get_secret_finish (SecretItem *self,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *res;
	GetClosure *closure;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      secret_item_get_secret), NULL);

	res = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (res, error))
		return NULL;

	closure = g_simple_async_result_get_op_res_gpointer (res);
	return closure->value ? secret_value_ref (closure->value) : NULL;
}

/**
 * secret_item_get_secret_sync:
 * @self: an item
 * @cancellable: optional cancellation object
 * @error: location to place error on failure
 *
 * Get the secret value of this item.
 *
 * Each item has a single secret which might be a password or some
 * other secret binary value.
 *
 * This function may block indefinetely. Use the asynchronous version
 * in user interface threads.
 *
 * Returns: (transfer full): the newly allocated secret value in this
 *          item, which should be released with secret_value_unref()
 */
SecretValue *
secret_item_get_secret_sync (SecretItem *self,
                             GCancellable *cancellable,
                             GError **error)
{
	SecretSync *sync;
	SecretValue *value;

	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	sync = _secret_sync_new ();
	g_main_context_push_thread_default (sync->context);

	secret_item_get_secret (self, cancellable, _secret_sync_on_result, sync);

	g_main_loop_run (sync->loop);

	value = secret_item_get_secret_finish (self, sync->result, error);

	g_main_context_pop_thread_default (sync->context);
	_secret_sync_free (sync);

	return value;
}

typedef struct {
	GCancellable *cancellable;
	SecretValue *value;
} SetClosure;

static void
set_closure_free (gpointer data)
{
	GetClosure *closure = data;
	g_clear_object (&closure->cancellable);
	secret_value_unref (closure->value);
	g_slice_free (GetClosure, closure);
}

static void
on_item_set_secret (GObject *source,
                    GAsyncResult *result,
                    gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	GVariant *retval;

	retval = g_dbus_proxy_call_finish (G_DBUS_PROXY (source), result, &error);

	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	if (retval != NULL)
		g_variant_unref (retval);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_set_ensure_session (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretItem *self = SECRET_ITEM (g_async_result_get_source_object (user_data));
	SetClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	SecretSession *session;
	GVariant *encoded;
	GError *error = NULL;

	secret_service_ensure_session_finish (self->pv->service, result, &error);
	if (error != NULL) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	} else {
		session = _secret_service_get_session (self->pv->service);
		encoded = _secret_session_encode_secret (session, closure->value);
		g_dbus_proxy_call (G_DBUS_PROXY (self), "SetSecret",
		                   g_variant_new ("(@(oayays))", encoded),
		                   G_DBUS_CALL_FLAGS_NO_AUTO_START, -1, closure->cancellable,
		                   on_item_set_secret, g_object_ref (res));
	}

	g_object_unref (self);
	g_object_unref (res);
}

/**
 * secret_item_set_secret:
 * @self: an item
 * @value: a new secret value
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to pass to the callback
 *
 * Set the secret value of this item.
 *
 * Each item has a single secret which might be a password or some
 * other secret binary value.
 *
 * This function returns immediately and completes asynchronously.
 */
void
secret_item_set_secret (SecretItem *self,
                        SecretValue *value,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *res;
	SetClosure *closure;

	g_return_if_fail (SECRET_IS_ITEM (self));
	g_return_if_fail (value != NULL);
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	res = g_simple_async_result_new (G_OBJECT (self), callback,
	                                 user_data, secret_item_set_secret);
	closure = g_slice_new0 (SetClosure);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	closure->value = secret_value_ref (value);
	g_simple_async_result_set_op_res_gpointer (res, closure, set_closure_free);

	secret_service_ensure_session (self->pv->service, cancellable,
	                               on_set_ensure_session,
	                               g_object_ref (res));

	g_object_unref (res);
}

/**
 * secret_item_set_secret_finish:
 * @self: an item
 * @result: asynchronous result passed to callback
 * @error: location to place error on failure
 *
 * Complete asynchronous operation to set the secret value of this item.
 *
 * Returns: whether the change was successful or not
 */
gboolean
secret_item_set_secret_finish (SecretItem *self,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *res;

	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      secret_item_set_secret), FALSE);

	res = G_SIMPLE_ASYNC_RESULT (result);
	if (g_simple_async_result_propagate_error (res, error))
		return FALSE;

	return TRUE;
}

/**
 * secret_item_set_secret_sync:
 * @self: an item
 * @value: a new secret value
 * @cancellable: optional cancellation object
 * @error: location to place error on failure
 *
 * Set the secret value of this item.
 *
 * Each item has a single secret which might be a password or some
 * other secret binary value.
 *
 * This function may block indefinetely. Use the asynchronous version
 * in user interface threads.
 *
 * Returns: whether the change was successful or not
 */
gboolean
secret_item_set_secret_sync (SecretItem *self,
                             SecretValue *value,
                             GCancellable *cancellable,
                             GError **error)
{
	SecretSync *sync;
	gboolean ret;

	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	sync = _secret_sync_new ();
	g_main_context_push_thread_default (sync->context);

	secret_item_set_secret (self, value, cancellable, _secret_sync_on_result, sync);

	g_main_loop_run (sync->loop);

	ret = secret_item_set_secret_finish (self, sync->result, error);

	g_main_context_pop_thread_default (sync->context);
	_secret_sync_free (sync);

	return ret;
}

/**
 * secret_item_get_attributes:
 * @self: an item
 *
 * Set the attributes of this item.
 *
 * The @attributes are a mapping of string keys to string values.
 * Attributes are used to search for items. Attributes are not stored
 * or transferred securely by the secret service.
 *
 * Do not modify the attributes returned by this method. Use
 * secret_item_set_attributes() instead.
 *
 * Returns: (transfer full): a new reference to the attributes, which should
 *          not be modified, and released with g_hash_table_unref()
 */
GHashTable *
secret_item_get_attributes (SecretItem *self)
{
	GHashTable *attributes;
	GVariant *variant;

	g_return_val_if_fail (SECRET_IS_ITEM (self), NULL);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Attributes");
	g_return_val_if_fail (variant != NULL, NULL);

	attributes = _secret_util_attributes_for_variant (variant);
	g_variant_unref (variant);

	return attributes;
}

/**
 * secret_item_set_attributes:
 * @self: an item
 * @attributes: (element-type utf8 utf8): a new set of attributes
 * @cancellable: optional cancellation object
 * @callback: called when the asynchronous operation completes
 * @user_data: data to pass to the callback
 *
 * Set the attributes of this item.
 *
 * The @attributes are a mapping of string keys to string values.
 * Attributes are used to search for items. Attributes are not stored
 * or transferred securely by the secret service.
 *
 * This function returns immediately and completes asynchronously.
 */
void
secret_item_set_attributes (SecretItem *self,
                            GHashTable *attributes,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	g_return_if_fail (SECRET_IS_ITEM (self));
	g_return_if_fail (attributes != NULL);

	_secret_util_set_property (G_DBUS_PROXY (self), "Attributes",
	                           _secret_util_variant_for_attributes (attributes),
	                           secret_item_set_attributes, cancellable,
	                           callback, user_data);
}

/**
 * secret_item_set_attributes_finish:
 * @self: an item
 * @result: asynchronous result passed to the callback
 * @error: location to place error on failure
 *
 * Complete operation to set the attributes of this item.
 *
 * Returns: whether the change was successful or not
 */
gboolean
secret_item_set_attributes_finish (SecretItem *self,
                                   GAsyncResult *result,
                                   GError **error)
{
	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);

	return _secret_util_set_property_finish (G_DBUS_PROXY (self),
	                                         secret_item_set_attributes,
	                                         result, error);
}

/**
 * secret_item_set_attributes_sync:
 * @self: an item
 * @attributes: (element-type utf8 utf8): a new set of attributes
 * @cancellable: optional cancellation object
 * @error: location to place error on failure
 *
 * Set the attributes of this item.
 *
 * The @attributes are a mapping of string keys to string values.
 * Attributes are used to search for items. Attributes are not stored
 * or transferred securely by the secret service.
 *
 * This function may block indefinetely. Use the asynchronous version
 * in user interface threads.
 *
 * Returns: whether the change was successful or not
 */
gboolean
secret_item_set_attributes_sync (SecretItem *self,
                                 GHashTable *attributes,
                                 GCancellable *cancellable,
                                 GError **error)
{
	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (attributes != NULL, FALSE);

	return _secret_util_set_property_sync (G_DBUS_PROXY (self), "Attributes",
	                                       _secret_util_variant_for_attributes (attributes),
	                                       cancellable, error);
}

/**
 * secret_item_get_schema:
 * @self: an item
 *
 * Get the schema of this item.
 *
 * The schema is a dotted string like <literal>org.freedesktop.Secret.Generic</literal>.
 * A schema describes the set of attributes that should be set on this item.
 *
 * Returns: (transfer full): the schema, which should be freed with g_free()
 */
gchar *
secret_item_get_schema (SecretItem *self)
{
	GVariant *variant;
	gchar *label;

	g_return_val_if_fail (SECRET_IS_ITEM (self), NULL);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Type");
	if (variant == NULL)
		return NULL;

	label = g_variant_dup_string (variant, NULL);
	g_variant_unref (variant);

	return label;
}

/**
 * secret_item_get_label:
 * @self: an item
 *
 * Get the label of this item.
 *
 * Returns: (transfer full): the label, which should be freed with g_free()
 */
gchar *
secret_item_get_label (SecretItem *self)
{
	GVariant *variant;
	gchar *label;

	g_return_val_if_fail (SECRET_IS_ITEM (self), NULL);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Label");
	g_return_val_if_fail (variant != NULL, NULL);

	label = g_variant_dup_string (variant, NULL);
	g_variant_unref (variant);

	return label;
}

/**
 * secret_item_set_label:
 * @self: an item
 * @label: a new label
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to pass to the callback
 *
 * Set the label of this item.
 *
 * This function returns immediately and completes asynchronously.
 */
void
secret_item_set_label (SecretItem *self,
                       const gchar *label,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	g_return_if_fail (SECRET_IS_ITEM (self));
	g_return_if_fail (label != NULL);

	_secret_util_set_property (G_DBUS_PROXY (self), "Label",
	                           g_variant_new_string (label),
	                           secret_item_set_label,
	                           cancellable, callback, user_data);
}

/**
 * secret_item_set_label_finish:
 * @self: an item
 * @result: asynchronous result passed to callback
 * @error: location to place error on failure
 *
 * Complete asynchronous operation to set the label of this collection.
 *
 * Returns: whether the change was successful or not
 */
gboolean
secret_item_set_label_finish (SecretItem *self,
                              GAsyncResult *result,
                              GError **error)
{
	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);

	return _secret_util_set_property_finish (G_DBUS_PROXY (self),
	                                         secret_item_set_label,
	                                         result, error);
}

/**
 * secret_item_set_label_sync:
 * @self: an item
 * @label: a new label
 * @cancellable: optional cancellation object
 * @error: location to place error on failure
 *
 * Set the label of this item.
 *
 * This function may block indefinetely. Use the asynchronous version
 * in user interface threads.
 *
 * Returns: whether the change was successful or not
 */
gboolean
secret_item_set_label_sync (SecretItem *self,
                            const gchar *label,
                            GCancellable *cancellable,
                            GError **error)
{
	g_return_val_if_fail (SECRET_IS_ITEM (self), FALSE);
	g_return_val_if_fail (label != NULL, FALSE);

	return _secret_util_set_property_sync (G_DBUS_PROXY (self), "Label",
	                                       g_variant_new_string (label),
	                                       cancellable, error);
}

/**
 * secret_item_get_locked:
 * @self: an item
 *
 * Get whether the item is locked or not.
 *
 * Depending on the secret service an item may not be able to be locked
 * independently from the collection that it is in.
 *
 * Returns: whether the item is locked or not
 */
gboolean
secret_item_get_locked (SecretItem *self)
{
	GVariant *variant;
	gboolean locked;

	g_return_val_if_fail (SECRET_IS_ITEM (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Locked");
	g_return_val_if_fail (variant != NULL, TRUE);

	locked = g_variant_get_boolean (variant);
	g_variant_unref (variant);

	return locked;
}

/**
 * secret_item_get_created:
 * @self: an item
 *
 * Get the created date and time of the item. The return value is
 * the number of seconds since the unix epoch, January 1st 1970.
 *
 * Returns: the created date and time
 */
guint64
secret_item_get_created (SecretItem *self)
{
	GVariant *variant;
	guint64 created;

	g_return_val_if_fail (SECRET_IS_ITEM (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Created");
	g_return_val_if_fail (variant != NULL, 0);

	created = g_variant_get_uint64 (variant);
	g_variant_unref (variant);

	return created;
}

/**
 * secret_item_get_modified:
 * @self: an item
 *
 * Get the modified date and time of the item. The return value is
 * the number of seconds since the unix epoch, January 1st 1970.
 *
 * Returns: the modified date and time
 */
guint64
secret_item_get_modified (SecretItem *self)
{
	GVariant *variant;
	guint64 modified;

	g_return_val_if_fail (SECRET_IS_ITEM (self), TRUE);

	variant = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Modified");
	g_return_val_if_fail (variant != NULL, 0);

	modified = g_variant_get_uint64 (variant);
	g_variant_unref (variant);

	return modified;
}
