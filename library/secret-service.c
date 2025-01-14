/* libsecret - GLib wrapper for Secret Service
 *
 * Copyright 2011 Collabora Ltd.
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
#include "secret-enum-types.h"
#include "secret-item.h"
#include "secret-private.h"
#include "secret-service.h"
#include "secret-types.h"
#include "secret-value.h"

#include "egg/egg-secure-memory.h"

/**
 * SECTION:secret-service
 * @title: SecretService
 * @short_description: the Secret Service
 *
 * A #SecretService object represents the Secret Service implementation which
 * runs as a DBus service.
 *
 * Normally a single #SecretService object can be shared between multiple
 * callers. The secret_service_get() method is used to access this #SecretService
 * object. If a new independent #SecretService object is required, use
 * secret_service_new().
 *
 * In order to securely transfer secrets to the Sercret Service, an session
 * is established. This session can be established while initializing a
 * #SecretService object by passing the %SECRET_SERVICE_OPEN_SESSION flag
 * to the secret_service_get() or secret_service_new() functions. In order to
 * establish a session on an already existing #SecretService, use the
 * secret_service_ensure_session() function.
 *
 * To search for items, use the secret_service_search() method.
 *
 * Multiple collections can exist in the Secret Service, each of which contains
 * secret items. In order to instantiate #SecretCollection objects which
 * represent those collections while initializing a #SecretService then pass
 * the %SECRET_SERVICE_LOAD_COLLECTIONS flag to the secret_service_get() or
 * secret_service_new() functions. In order to establish a session on an already
 * existing #SecretService, use the secret_service_ensure_collections() function.
 * To access the list of collections use secret_service_get_collections().
 *
 * Certain actions on the Secret Service require user prompting to complete,
 * such as creating a collection, or unlocking a collection. When such a prompt
 * is necessary, then a #SecretPrompt object is created by this library, and
 * passed to the secret_service_prompt() method. In this way it is handled
 * automatically.
 *
 * In order to customize prompt handling, override the <literal>prompt_async</literal>
 * and <literal>prompt_finish</literal> virtual methods of the #SecretService class.
 */

/**
 * SecretService:
 *
 * A proxy object representing the Secret Service.
 */

/**
 * SecretServiceClass:
 * @parent_class: the parent class
 * @collection_gtype: the #GType of the #SecretCollection objects instantiated
 *                    by the #SecretService proxy
 * @item_gtype: the #GType of the #SecretItem objects instantiated by the
 *              #SecretService proxy
 * @prompt_async: called to perform asynchronous prompting when necessary
 * @prompt_finish: called to complete an asynchronous prompt operation
 * @prompt_sync: called to perform synchronous prompting when necessary
 *
 * The class for #SecretService.
 */

/**
 * SecretServiceFlags:
 * @SECRET_SERVICE_NONE: no flags for initializing the #SecretService
 * @SECRET_SERVICE_OPEN_SESSION: establish a session for transfer of secrets
 *                               while initializing the #SecretService
 * @SECRET_SERVICE_LOAD_COLLECTIONS: load collections while initializing the
 *                                   #SecretService
 *
 * Flags which determine which parts of the #SecretService proxy are initialized
 * during a secret_service_get() or secret_service_new() operation.
 */

EGG_SECURE_GLIB_DEFINITIONS ();

static const gchar *default_bus_name = SECRET_SERVICE_BUS_NAME;

enum {
	PROP_0,
	PROP_FLAGS,
	PROP_COLLECTIONS
};

typedef struct _SecretServicePrivate {
	/* No change between construct and finalize */
	GCancellable *cancellable;
	SecretServiceFlags init_flags;

	/* Locked by mutex */
	GMutex mutex;
	gpointer session;
	GHashTable *collections;
} SecretServicePrivate;

G_LOCK_DEFINE (service_instance);
static gpointer service_instance = NULL;

static GInitableIface *secret_service_initable_parent_iface = NULL;

static GAsyncInitableIface *secret_service_async_initable_parent_iface = NULL;

static void   secret_service_initable_iface         (GInitableIface *iface);

static void   secret_service_async_initable_iface   (GAsyncInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (SecretService, secret_service, G_TYPE_DBUS_PROXY,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, secret_service_initable_iface);
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, secret_service_async_initable_iface);
);

static void
secret_service_init (SecretService *self)
{
	self->pv = G_TYPE_INSTANCE_GET_PRIVATE (self, SECRET_TYPE_SERVICE,
	                                        SecretServicePrivate);

	g_mutex_init (&self->pv->mutex);
	self->pv->cancellable = g_cancellable_new ();
}

static void
secret_service_get_property (GObject *obj,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	SecretService *self = SECRET_SERVICE (obj);

	switch (prop_id) {
	case PROP_FLAGS:
		g_value_set_flags (value, secret_service_get_flags (self));
		break;
	case PROP_COLLECTIONS:
		g_value_take_boxed (value, secret_service_get_collections (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
secret_service_set_property (GObject *obj,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	SecretService *self = SECRET_SERVICE (obj);

	switch (prop_id) {
	case PROP_FLAGS:
		self->pv->init_flags = g_value_get_flags (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
secret_service_dispose (GObject *obj)
{
	SecretService *self = SECRET_SERVICE (obj);

	g_cancellable_cancel (self->pv->cancellable);

	G_OBJECT_CLASS (secret_service_parent_class)->dispose (obj);
}

static void
secret_service_finalize (GObject *obj)
{
	SecretService *self = SECRET_SERVICE (obj);

	_secret_session_free (self->pv->session);
	if (self->pv->collections)
		g_hash_table_destroy (self->pv->collections);
	g_clear_object (&self->pv->cancellable);

	G_OBJECT_CLASS (secret_service_parent_class)->finalize (obj);
}

static gboolean
secret_service_real_prompt_sync (SecretService *self,
                                 SecretPrompt *prompt,
                                 GCancellable *cancellable,
                                 GError **error)
{
	return secret_prompt_perform_sync (prompt, 0, cancellable, error);
}

static void
on_real_prompt_completed (GObject *source,
                          GAsyncResult *result,
                          gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	GError *error = NULL;
	gboolean ret;

	ret = secret_prompt_perform_finish (SECRET_PROMPT (source), result, &error);
	g_simple_async_result_set_op_res_gboolean (res, ret);
	if (error != NULL)
		g_simple_async_result_take_error (res, error);
	g_simple_async_result_complete (res);

	g_object_unref (res);
}

static void
secret_service_real_prompt_async (SecretService *self,
                                  SecretPrompt *prompt,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GSimpleAsyncResult *res;

	res =  g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                  secret_service_real_prompt_async);

	secret_prompt_perform (prompt, 0, cancellable,
	                       on_real_prompt_completed,
	                       g_object_ref (res));

	g_object_unref (res);
}

static gboolean
secret_service_real_prompt_finish (SecretService *self,
                                   GAsyncResult *result,
                                   GError **error)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (result);

	if (g_simple_async_result_propagate_error (res, error))
		return FALSE;

	return g_simple_async_result_get_op_res_gboolean (res);
}

static void
handle_property_changed (SecretService *self,
                         const gchar *property_name,
                         GVariant *value)
{
	gboolean perform;

	if (g_str_equal (property_name, "Collections")) {

		g_mutex_lock (&self->pv->mutex);
		perform = self->pv->collections != NULL;
		g_mutex_unlock (&self->pv->mutex);

		if (perform)
			secret_service_ensure_collections (self, self->pv->cancellable, NULL, NULL);
	}
}

static void
secret_service_properties_changed (GDBusProxy *proxy,
                                   GVariant *changed_properties,
                                   const gchar* const *invalidated_properties)
{
	SecretService *self = SECRET_SERVICE (proxy);
	gchar *property_name;
	GVariantIter iter;
	GVariant *value;

	g_object_freeze_notify (G_OBJECT (self));

	g_variant_iter_init (&iter, changed_properties);
	while (g_variant_iter_loop (&iter, "{sv}", &property_name, &value))
		handle_property_changed (self, property_name, value);

	g_object_thaw_notify (G_OBJECT (self));
}

static void
secret_service_class_init (SecretServiceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GDBusProxyClass *proxy_class = G_DBUS_PROXY_CLASS (klass);

	object_class->get_property = secret_service_get_property;
	object_class->set_property = secret_service_set_property;
	object_class->dispose = secret_service_dispose;
	object_class->finalize = secret_service_finalize;

	proxy_class->g_properties_changed = secret_service_properties_changed;

	klass->prompt_sync = secret_service_real_prompt_sync;
	klass->prompt_async = secret_service_real_prompt_async;
	klass->prompt_finish = secret_service_real_prompt_finish;

	klass->item_gtype = SECRET_TYPE_ITEM;
	klass->collection_gtype = SECRET_TYPE_COLLECTION;

	/**
	 * SecretService:flags:
	 *
	 * A set of flags describing which parts of the secret service have
	 * been initialized.
	 */
	g_object_class_install_property (object_class, PROP_FLAGS,
	             g_param_spec_flags ("flags", "Flags", "Service flags",
	                                 secret_service_flags_get_type (), SECRET_SERVICE_NONE,
	                                 G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

	/**
	 * SecretService:collections:
	 *
	 * A list of #SecretCollection objects representing the collections in
	 * the Secret Service. This list may be %NULL if the collections have
	 * not been loaded.
	 *
	 * To load the collections, specify the %SECRET_SERVICE_LOAD_COLLECTIONS
	 * initialization flag when calling the secret_service_get() or
	 * secret_service_new() functions. Or call the secret_service_ensure_collections()
	 * method.
	 */
	g_object_class_install_property (object_class, PROP_COLLECTIONS,
	             g_param_spec_boxed ("collections", "Collections", "Secret Service Collections",
	                                 _secret_list_get_type (), G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	g_type_class_add_private (klass, sizeof (SecretServicePrivate));
}

typedef struct {
	GCancellable *cancellable;
	SecretServiceFlags flags;
} InitClosure;

static void
init_closure_free (gpointer data)
{
	InitClosure *closure = data;
	g_clear_object (&closure->cancellable);
	g_slice_free (InitClosure, closure);
}

static gboolean
service_ensure_for_flags_sync (SecretService *self,
                               SecretServiceFlags flags,
                               GCancellable *cancellable,
                               GError **error)
{
	if (flags & SECRET_SERVICE_OPEN_SESSION)
		if (!secret_service_ensure_session_sync (self, cancellable, error))
			return FALSE;

	if (flags & SECRET_SERVICE_LOAD_COLLECTIONS)
		if (!secret_service_ensure_collections_sync (self, cancellable, error))
			return FALSE;

	return TRUE;
}

static void
on_ensure_collections (GObject *source,
                       GAsyncResult *result,
                       gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretService *self = SECRET_SERVICE (source);
	GError *error = NULL;

	if (!secret_service_ensure_collections_finish (self, result, &error))
		g_simple_async_result_take_error (res, error);

	g_simple_async_result_complete (res);
	g_object_unref (res);
}

static void
on_ensure_session (GObject *source,
                   GAsyncResult *result,
                   gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	InitClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	SecretService *self = SECRET_SERVICE (source);
	GError *error = NULL;

	if (!secret_service_ensure_session_finish (self, result, &error)) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);

	} else if (closure->flags & SECRET_SERVICE_LOAD_COLLECTIONS) {
		secret_service_ensure_collections (self, closure->cancellable,
		                                   on_ensure_collections, g_object_ref (res));

	} else {
		g_simple_async_result_complete_in_idle (res);
	}

	g_object_unref (res);
}

static void
service_ensure_for_flags_async (SecretService *self,
                                SecretServiceFlags flags,
                                GSimpleAsyncResult *res)
{
	InitClosure *closure = g_simple_async_result_get_op_res_gpointer (res);

	closure->flags = flags;

	if (closure->flags & SECRET_SERVICE_OPEN_SESSION)
		secret_service_ensure_session (self, closure->cancellable,
		                               on_ensure_session, g_object_ref (res));

	else if (closure->flags & SECRET_SERVICE_LOAD_COLLECTIONS)
		secret_service_ensure_collections (self, closure->cancellable,
		                                   on_ensure_collections, g_object_ref (res));

	else
		g_simple_async_result_complete_in_idle (res);
}

static gboolean
secret_service_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error)
{
	SecretService *self;

	if (!secret_service_initable_parent_iface->init (initable, cancellable, error))
		return FALSE;

	self = SECRET_SERVICE (initable);
	return service_ensure_for_flags_sync (self, self->pv->init_flags, cancellable, error);
}

static void
secret_service_initable_iface (GInitableIface *iface)
{
	secret_service_initable_parent_iface = g_type_interface_peek_parent (iface);

	iface->init = secret_service_initable_init;
}

static void
on_init_base (GObject *source,
              GAsyncResult *result,
              gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretService *self = SECRET_SERVICE (source);
	GError *error = NULL;

	if (!secret_service_async_initable_parent_iface->init_finish (G_ASYNC_INITABLE (self),
	                                                              result, &error)) {
		g_simple_async_result_take_error (res, error);
		g_simple_async_result_complete (res);
	} else {

	}

	service_ensure_for_flags_async (self, self->pv->init_flags, res);
	g_object_unref (res);
}

static void
secret_service_async_initable_init_async (GAsyncInitable *initable,
                                          int io_priority,
                                          GCancellable *cancellable,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
	GSimpleAsyncResult *res;
	InitClosure *closure;

	res = g_simple_async_result_new (G_OBJECT (initable), callback, user_data,
	                                 secret_service_async_initable_init_async);
	closure = g_slice_new0 (InitClosure);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	g_simple_async_result_set_op_res_gpointer (res, closure, init_closure_free);

	secret_service_async_initable_parent_iface->init_async (initable, io_priority,
	                                                        cancellable,
	                                                        on_init_base,
	                                                        g_object_ref (res));

	g_object_unref (res);
}

static gboolean
secret_service_async_initable_init_finish (GAsyncInitable *initable,
                                           GAsyncResult *result,
                                           GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (initable),
	                      secret_service_async_initable_init_async), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

static void
secret_service_async_initable_iface (GAsyncInitableIface *iface)
{
	secret_service_async_initable_parent_iface = g_type_interface_peek_parent (iface);

	iface->init_async = secret_service_async_initable_init_async;
	iface->init_finish = secret_service_async_initable_init_finish;
}

void
_secret_service_set_default_bus_name (const gchar *bus_name)
{
	g_return_if_fail (bus_name != NULL);
	default_bus_name = bus_name;
}

static void
on_service_instance_gone (gpointer user_data,
                          GObject *where_the_object_was)
{
	G_LOCK (service_instance);

		g_assert (service_instance == where_the_object_was);
		service_instance = NULL;

	G_UNLOCK (service_instance);
}

/**
 * secret_service_get:
 * @flags: flags for which service functionality to ensure is initialized
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to be passed to the callback
 *
 * Get a #SecretService proxy for the Secret Service. If such a proxy object
 * already exists, then the same proxy is returned.
 *
 * If @flags contains any flags of which parts of the secret service to
 * ensure are initialized, then those will be initialized before completing.
 *
 * This method will return immediately and complete asynchronously.
 */
void
secret_service_get (SecretServiceFlags flags,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	SecretService *service = NULL;
	GSimpleAsyncResult *res;
	InitClosure *closure;

	G_LOCK (service_instance);
	if (service_instance != NULL)
		service = g_object_ref (service_instance);
	G_UNLOCK (service_instance);

	/* Create a whole new service */
	if (service == NULL) {
		g_async_initable_new_async (SECRET_TYPE_SERVICE, G_PRIORITY_DEFAULT,
		                            cancellable, callback, user_data,
		                            "g-flags", G_DBUS_PROXY_FLAGS_NONE,
		                            "g-interface-info", _secret_gen_service_interface_info (),
		                            "g-name", default_bus_name,
		                            "g-bus-type", G_BUS_TYPE_SESSION,
		                            "g-object-path", SECRET_SERVICE_PATH,
		                            "g-interface-name", SECRET_SERVICE_INTERFACE,
		                            "flags", flags,
		                            NULL);

	/* Just have to ensure that the service matches flags */
	} else {
		res = g_simple_async_result_new (G_OBJECT (service), callback,
		                                 user_data, secret_service_get);
		closure = g_slice_new0 (InitClosure);
		closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
		closure->flags = flags;
		g_simple_async_result_set_op_res_gpointer (res, closure, init_closure_free);

		service_ensure_for_flags_async (service, flags, res);

		g_object_unref (service);
		g_object_unref (res);
	}
}

/**
 * secret_service_get_finish:
 * @result: the asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Complete an asynchronous operation to get a #SecretService proxy for the
 * Secret Service.
 *
 * Returns: (transfer full): a new reference to a #SecretService proxy, which
 *          should be released with g_object_unref().
 */
SecretService *
secret_service_get_finish (GAsyncResult *result,
                           GError **error)
{
	GObject *service = NULL;
	GObject *source_object;

	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	source_object = g_async_result_get_source_object (result);

	/* Just ensuring that the service matches flags */
	if (g_simple_async_result_is_valid (result, source_object, secret_service_get)) {
		if (!g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
			service = g_object_ref (source_object);

	/* Creating a whole new service */
	} else {
		service = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object), result, error);

		if (service) {
			G_LOCK (service_instance);
			if (service_instance == NULL) {
				service_instance = service;
				g_object_weak_ref (G_OBJECT (service), on_service_instance_gone, NULL);
			}
			G_UNLOCK (service_instance);
		}
	}

	if (source_object)
		g_object_unref (source_object);

	if (service == NULL)
		return NULL;

	return SECRET_SERVICE (service);
}

/**
 * secret_service_get_sync:
 * @flags: flags for which service functionality to ensure is initialized
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Get a #SecretService proxy for the Secret Service. If such a proxy object
 * already exists, then the same proxy is returned.
 *
 * If @flags contains any flags of which parts of the secret service to
 * ensure are initialized, then those will be initialized before returning.
 *
 * This method may block indefinitely and should not be used in user interface
 * threads.
 *
 * Returns: (transfer full): a new reference to a #SecretService proxy, which
 *          should be released with g_object_unref().
 */
SecretService *
secret_service_get_sync (SecretServiceFlags flags,
                         GCancellable *cancellable,
                         GError **error)
{
	SecretService *service = NULL;

	G_LOCK (service_instance);
	if (service_instance != NULL)
		service = g_object_ref (service_instance);
	G_UNLOCK (service_instance);

	if (service == NULL) {
		service = g_initable_new (SECRET_TYPE_SERVICE, cancellable, error,
		                          "g-flags", G_DBUS_PROXY_FLAGS_NONE,
		                          "g-interface-info", _secret_gen_service_interface_info (),
		                          "g-name", default_bus_name,
		                          "g-bus-type", G_BUS_TYPE_SESSION,
		                          "g-object-path", SECRET_SERVICE_PATH,
		                          "g-interface-name", SECRET_SERVICE_INTERFACE,
		                          "flags", flags,
		                          NULL);

		if (service != NULL) {
			G_LOCK (service_instance);
			if (service_instance == NULL) {
				service_instance = service;
				g_object_weak_ref (G_OBJECT (service), on_service_instance_gone, NULL);
			}
			G_UNLOCK (service_instance);
		}

	} else {
		if (!service_ensure_for_flags_sync (service, flags, cancellable, error)) {
			g_object_unref (service);
			return NULL;
		}
	}

	return service;
}

/**
 * secret_service_new:
 * @service_bus_name: (allow-none): dbus service name of the secret service
 * @flags: flags for which service functionality to ensure is initialized
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to be passed to the callback
 *
 * Create a new #SecretService proxy for the Secret Service.
 *
 * This function is rarely used, see secret_service_get() instead.
 *
 * If @flags contains any flags of which parts of the secret service to
 * ensure are initialized, then those will be initialized before returning.
 *
 * If @service_bus_name is %NULL then the default is used.
 *
 * This method will return immediately and complete asynchronously.
 */
void
secret_service_new (const gchar *service_bus_name,
                    SecretServiceFlags flags,
                    GCancellable *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer user_data)
{
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	if (service_bus_name == NULL)
		service_bus_name = default_bus_name;

	g_async_initable_new_async (SECRET_TYPE_SERVICE, G_PRIORITY_DEFAULT,
	                            cancellable, callback, user_data,
	                            "g-flags", G_DBUS_PROXY_FLAGS_NONE,
	                            "g-interface-info", _secret_gen_service_interface_info (),
	                            "g-name", service_bus_name,
	                            "g-bus-type", G_BUS_TYPE_SESSION,
	                            "g-object-path", SECRET_SERVICE_PATH,
	                            "g-interface-name", SECRET_SERVICE_INTERFACE,
	                            "flags", flags,
	                            NULL);
}

/**
 * secret_service_new_finish:
 * @result: the asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Complete an asynchronous operation to create a new #SecretService proxy for
 * the Secret Service.
 *
 * Returns: (transfer full): a new reference to a #SecretService proxy, which
 *          should be released with g_object_unref().
 */
SecretService *
secret_service_new_finish (GAsyncResult *result,
                           GError **error)
{
	GObject *source_object;
	GObject *object;

	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	source_object = g_async_result_get_source_object (result);
	object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
	                                      result, error);
	g_object_unref (source_object);

	if (object == NULL)
		return NULL;

	return SECRET_SERVICE (object);
}

/**
 * secret_service_new_sync:
 * @service_bus_name: (allow-none): dbus service name of the secret service
 * @flags: flags for which service functionality to ensure is initialized
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Create a new #SecretService proxy for the Secret Service.
 *
 * This function is rarely used, see secret_service_get_sync() instead.
 *
 * If @flags contains any flags of which parts of the secret service to
 * ensure are initialized, then those will be initialized before returning.
 *
 * If @service_bus_name is %NULL then the default is used.
 *
 * This method may block indefinitely and should not be used in user interface
 * threads.
 *
 * Returns: (transfer full): a new reference to a #SecretService proxy, which
 *          should be released with g_object_unref().
 */
SecretService *
secret_service_new_sync (const gchar *service_bus_name,
                         SecretServiceFlags flags,
                         GCancellable *cancellable,
                         GError **error)
{
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);

	if (service_bus_name == NULL)
		service_bus_name = default_bus_name;

	return g_initable_new (SECRET_TYPE_SERVICE, cancellable, error,
	                       "g-flags", G_DBUS_PROXY_FLAGS_NONE,
	                       "g-interface-info", _secret_gen_service_interface_info (),
	                       "g-name", service_bus_name,
	                       "g-bus-type", G_BUS_TYPE_SESSION,
	                       "g-object-path", SECRET_SERVICE_PATH,
	                       "g-interface-name", SECRET_SERVICE_INTERFACE,
	                       "flags", flags,
	                       NULL);
}

/**
 * secret_service_get_flags:
 * @self: the secret service proxy
 *
 * Get the flags representing what features of the #SecretService proxy
 * have been initialized.
 *
 * Use secret_service_ensure_session() or secret_service_ensure_collections()
 * to initialize further features and change the flags.
 *
 * Returns: the flags for features initialized
 */
SecretServiceFlags
secret_service_get_flags (SecretService *self)
{
	SecretServiceFlags flags = 0;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), SECRET_SERVICE_NONE);

	g_mutex_lock (&self->pv->mutex);

	if (self->pv->session)
		flags |= SECRET_SERVICE_OPEN_SESSION;
	if (self->pv->collections)
		flags |= SECRET_SERVICE_LOAD_COLLECTIONS;

	g_mutex_unlock (&self->pv->mutex);

	return flags;
}

/**
 * secret_service_get_collections:
 * @self: the secret service proxy
 *
 * Get a list of #SecretCollection objects representing all the collections
 * in the secret service.
 *
 * If the %SECRET_SERVICE_LOAD_COLLECTIONS flag was not specified when
 * initializing #SecretService proxy object, then this method will return
 * %NULL. Use secret_service_ensure_collections() to load the collections.
 *
 * Returns: (transfer full) (element-type Secret.Collection) (allow-none): a
 *          list of the collections in the secret service
 */
GList *
secret_service_get_collections (SecretService *self)
{
	GList *l, *collections;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), NULL);

	g_mutex_lock (&self->pv->mutex);

	if (self->pv->collections == NULL) {
		collections = NULL;

	} else {
		collections = g_hash_table_get_values (self->pv->collections);
		for (l = collections; l != NULL; l = g_list_next (l))
			g_object_ref (l->data);
	}

	g_mutex_unlock (&self->pv->mutex);

	return collections;
}

SecretItem *
_secret_service_find_item_instance (SecretService *self,
                                    const gchar *item_path)
{
	SecretCollection *collection = NULL;
	gchar *collection_path;
	SecretItem *item;

	collection_path = _secret_util_parent_path (item_path);

	g_mutex_lock (&self->pv->mutex);
	if (self->pv->collections) {
		collection = g_hash_table_lookup (self->pv->collections, collection_path);
		if (collection != NULL)
			g_object_ref (collection);
	}
	g_mutex_unlock (&self->pv->mutex);

	g_free (collection_path);

	if (collection == NULL)
		return NULL;

	item = _secret_collection_find_item_instance (collection, item_path);
	g_object_unref (collection);

	return item;
}

SecretSession *
_secret_service_get_session (SecretService *self)
{
	SecretSession *session;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), NULL);

	g_mutex_lock (&self->pv->mutex);
	session = self->pv->session;
	g_mutex_unlock (&self->pv->mutex);

	return session;
}

void
_secret_service_take_session (SecretService *self,
                              SecretSession *session)
{
	g_return_if_fail (SECRET_IS_SERVICE (self));
	g_return_if_fail (session != NULL);

	g_mutex_lock (&self->pv->mutex);
	if (self->pv->session == NULL)
		self->pv->session = session;
	else
		_secret_session_free (session);
	g_mutex_unlock (&self->pv->mutex);
}

/**
 * secret_service_get_session_algorithms:
 * @self: the secret service proxy
 *
 * Get the set of algorithms being used to transfer secrets between this
 * secret service proxy and the Secret Service itself.
 *
 * This will be %NULL if no session has been established. Use
 * secret_service_ensure_session() to establish a session.
 *
 * Returns: (allow-none): a string representing the algorithms for transferring
 *          secrets
 */
const gchar *
secret_service_get_session_algorithms (SecretService *self)
{
	SecretSession *session;
	const gchar *algorithms;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), NULL);

	g_mutex_lock (&self->pv->mutex);
	session = self->pv->session;
	algorithms = session ? _secret_session_get_algorithms (session) : NULL;
	g_mutex_unlock (&self->pv->mutex);

	/* Session never changes once established, so can return const */
	return algorithms;
}

/**
 * secret_service_get_session_path:
 * @self: the secret service proxy
 *
 * Get the dbus object path of the session object being used to transfer
 * secrets between this secret service proxy and the Secret Service itself.
 *
 * This will be %NULL if no session has been established. Use
 * secret_service_ensure_session() to establish a session.
 *
 * Returns: (allow-none): a string representing the dbus object path of the
 *          session
 */
const gchar *
secret_service_get_session_path (SecretService *self)
{
	SecretSession *session;
	const gchar *path;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), NULL);

	g_mutex_lock (&self->pv->mutex);
	session = self->pv->session;
	path = session ? _secret_session_get_path (session) : NULL;
	g_mutex_unlock (&self->pv->mutex);

	/* Session never changes once established, so can return const */
	return path;
}

/**
 * secret_service_ensure_session:
 * @self: the secret service
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to be passed to the callback
 *
 * Ensure that the #SecretService proxy has established a session with the
 * Secret Service. This session is used to transfer secrets.
 *
 * It is not normally necessary to call this method, as the session is
 * established as necessary. You can also pass the %SECRET_SERVICE_OPEN_SESSION
 * to secret_service_get() in order to ensure that a session has been established
 * by the time you get the #SecretService proxy.
 *
 * This method will return immediately and complete asynchronously.
 */
void
secret_service_ensure_session (SecretService *self,
                               GCancellable *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer user_data)
{
	GSimpleAsyncResult *res;
	SecretSession *session;

	g_return_if_fail (SECRET_IS_SERVICE (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	g_mutex_lock (&self->pv->mutex);
	session = self->pv->session;
	g_mutex_unlock (&self->pv->mutex);

	if (session == NULL) {
		_secret_session_open (self, cancellable, callback, user_data);

	} else {
		res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
		                                 secret_service_ensure_session);
		g_simple_async_result_complete_in_idle (res);
		g_object_unref (res);
	}
}

/**
 * secret_service_ensure_session_finish:
 * @self: the secret service
 * @result: the asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Finish an asynchronous operation to ensure that the #SecretService proxy
 * has established a session with the Secret Service.
 *
 * Returns: the path of the established session
 */
const gchar *
secret_service_ensure_session_finish (SecretService *self,
                                      GAsyncResult *result,
                                      GError **error)
{
	g_return_val_if_fail (SECRET_IS_SERVICE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (!g_simple_async_result_is_valid (result, G_OBJECT (self),
	                                     secret_service_ensure_session)) {
		if (!_secret_session_open_finish (result, error))
			return NULL;
	}

	g_return_val_if_fail (self->pv->session != NULL, NULL);
	return secret_service_get_session_path (self);
}

/**
 * secret_service_ensure_session_sync:
 * @self: the secret service
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Ensure that the #SecretService proxy has established a session with the
 * Secret Service. This session is used to transfer secrets.
 *
 * It is not normally necessary to call this method, as the session is
 * established as necessary. You can also pass the %SECRET_SERVICE_OPEN_SESSION
 * to secret_service_get_sync() in order to ensure that a session has been
 * established by the time you get the #SecretService proxy.
 *
 * This method may block indefinitely and should not be used in user interface
 * threads.
 *
 * Returns: the path of the established session
 */
const gchar *
secret_service_ensure_session_sync (SecretService *self,
                                    GCancellable *cancellable,
                                    GError **error)
{
	SecretSync *sync;
	const gchar *path;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	sync = _secret_sync_new ();
	g_main_context_push_thread_default (sync->context);

	secret_service_ensure_session (self, cancellable,
	                               _secret_sync_on_result, sync);

	g_main_loop_run (sync->loop);

	path = secret_service_ensure_session_finish (self, sync->result, error);

	g_main_context_pop_thread_default (sync->context);
	_secret_sync_free (sync);

	return path;
}

static SecretCollection *
service_lookup_collection (SecretService *self,
                           const gchar *path)
{
	SecretCollection *collection = NULL;

	g_mutex_lock (&self->pv->mutex);

	if (self->pv->collections) {
		collection = g_hash_table_lookup (self->pv->collections, path);
		if (collection != NULL)
			g_object_ref (collection);
	}

	g_mutex_unlock (&self->pv->mutex);

	return collection;
}

static void
service_update_collections (SecretService *self,
                            GHashTable *collections)
{
	GHashTable *previous;

	g_hash_table_ref (collections);

	g_mutex_lock (&self->pv->mutex);

	previous = self->pv->collections;
	self->pv->collections = collections;

	g_mutex_unlock (&self->pv->mutex);

	if (previous != NULL)
		g_hash_table_unref (previous);
}

typedef struct {
	GCancellable *cancellable;
	GHashTable *collections;
	gint collections_loading;
} EnsureClosure;

static GHashTable *
collections_table_new (void)
{
	return g_hash_table_new_full (g_str_hash, g_str_equal,
	                              g_free, g_object_unref);
}

static void
ensure_closure_free (gpointer data)
{
	EnsureClosure *closure = data;
	g_clear_object (&closure->cancellable);
	g_hash_table_unref (closure->collections);
	g_slice_free (EnsureClosure, closure);
}

static void
on_ensure_collection (GObject *source,
                      GAsyncResult *result,
                      gpointer user_data)
{
	GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
	SecretService *self = SECRET_SERVICE (g_async_result_get_source_object (user_data));
	EnsureClosure *closure = g_simple_async_result_get_op_res_gpointer (res);
	SecretCollection *collection;
	const gchar *path;
	GError *error = NULL;

	closure->collections_loading--;

	collection = secret_collection_new_finish (result, &error);

	if (error != NULL)
		g_simple_async_result_take_error (res, error);

	if (collection != NULL) {
		path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (self));
		g_hash_table_insert (closure->collections, g_strdup (path), collection);
	}

	if (closure->collections_loading == 0) {
		service_update_collections (self, closure->collections);
		g_simple_async_result_complete (res);
	}

	g_object_unref (self);
	g_object_unref (res);
}

/**
 * secret_service_ensure_collections:
 * @self: the secret service
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to be passed to the callback
 *
 * Ensure that the #SecretService proxy has loaded all the collections present
 * in the Secret Service. This affects the result of
 * secret_service_get_collections().
 *
 * You can also pass the %SECRET_SERVICE_LOAD_COLLECTIONS to
 * secret_service_get_sync() in order to ensure that the collections have been
 * loaded by the time you get the #SecretService proxy.
 *
 * This method will return immediately and complete asynchronously.
 */
void
secret_service_ensure_collections (SecretService *self,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
	EnsureClosure *closure;
	SecretCollection *collection;
	GSimpleAsyncResult *res;
	const gchar *path;
	GVariant *paths;
	GVariantIter iter;

	g_return_if_fail (SECRET_IS_SERVICE (self));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	paths = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Collections");
	g_return_if_fail (paths != NULL);

	res = g_simple_async_result_new (G_OBJECT (self), callback, user_data,
	                                 secret_service_ensure_collections);
	closure = g_slice_new0 (EnsureClosure);
	closure->cancellable = cancellable ? g_object_ref (cancellable) : NULL;
	closure->collections = collections_table_new ();
	g_simple_async_result_set_op_res_gpointer (res, closure, ensure_closure_free);

	g_variant_iter_init (&iter, paths);
	while (g_variant_iter_loop (&iter, "&o", &path)) {
		collection = service_lookup_collection (self, path);

		/* No such collection yet create a new one */
		if (collection == NULL) {
			secret_collection_new (self, path, cancellable,
			                       on_ensure_collection, g_object_ref (res));
			closure->collections_loading++;
		} else {
			g_hash_table_insert (closure->collections, g_strdup (path), collection);
		}
	}

	if (closure->collections_loading == 0) {
		service_update_collections (self, closure->collections);
		g_simple_async_result_complete_in_idle (res);
	}

	g_variant_unref (paths);
	g_object_unref (res);
}

/**
 * secret_service_ensure_collections_finish:
 * @self: the secret service
 * @result: the asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Complete an asynchronous operation to ensure that the #SecretService proxy
 * has loaded all the collections present in the Secret Service.
 *
 * Returns: whether the load was successful or not
 */
gboolean
secret_service_ensure_collections_finish (SecretService *self,
                                          GAsyncResult *result,
                                          GError **error)
{
	g_return_val_if_fail (SECRET_IS_SERVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	g_return_val_if_fail (g_simple_async_result_is_valid (result, G_OBJECT (self),
	                      secret_service_ensure_collections), FALSE);

	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return FALSE;

	return TRUE;
}

/**
 * secret_service_ensure_collections_sync:
 * @self: the secret service
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Ensure that the #SecretService proxy has loaded all the collections present
 * in the Secret Service. This affects the result of
 * secret_service_get_collections().
 *
 * You can also pass the %SECRET_SERVICE_LOAD_COLLECTIONS to
 * secret_service_get_sync() in order to ensure that the collections have been
 * loaded by the time you get the #SecretService proxy.
 *
 * This method may block indefinitely and should not be used in user interface
 * threads.
 *
 * Returns: whether the load was successful or not
 */
gboolean
secret_service_ensure_collections_sync (SecretService *self,
                                        GCancellable *cancellable,
                                        GError **error)
{
	SecretCollection *collection;
	GHashTable *collections;
	GVariant *paths;
	GVariantIter iter;
	const gchar *path;
	gboolean ret = TRUE;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	paths = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (self), "Collections");
	g_return_val_if_fail (paths != NULL, FALSE);

	collections = collections_table_new ();

	g_variant_iter_init (&iter, paths);
	while (g_variant_iter_next (&iter, "&o", &path)) {
		collection = service_lookup_collection (self, path);

		/* No such collection yet create a new one */
		if (collection == NULL) {
			collection = secret_collection_new_sync (self, path, cancellable, error);
			if (collection == NULL) {
				ret = FALSE;
				break;
			}
		}

		g_hash_table_insert (collections, g_strdup (path), collection);
	}

	if (ret)
		service_update_collections (self, collections);

	g_hash_table_unref (collections);
	g_variant_unref (paths);
	return ret;
}

/**
 * secret_service_prompt_sync:
 * @self: the secret service
 * @prompt: the prompt
 * @cancellable: optional cancellation object
 * @error: location to place an error on failure
 *
 * Perform prompting for a #SecretPrompt.
 *
 * This function is called by other parts of this library to handle prompts
 * for the various actions that can require prompting.
 *
 * Override the #SecretServiceClass <literal>prompt_sync</literal> virtual method
 * to change the behavior of the propmting. The default behavior is to simply
 * run secret_prompt_perform_sync() on the prompt.
 *
 * Returns: %FALSE if the prompt was dismissed or an error occurred
 */
gboolean
secret_service_prompt_sync (SecretService *self,
                            SecretPrompt *prompt,
                            GCancellable *cancellable,
                            GError **error)
{
	SecretServiceClass *klass;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), FALSE);
	g_return_val_if_fail (SECRET_IS_PROMPT (prompt), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	klass = SECRET_SERVICE_GET_CLASS (self);
	g_return_val_if_fail (klass->prompt_sync != NULL, FALSE);

	return (klass->prompt_sync) (self, prompt, cancellable, error);
}

/**
 * secret_service_prompt:
 * @self: the secret service
 * @prompt: the prompt
 * @cancellable: optional cancellation object
 * @callback: called when the operation completes
 * @user_data: data to be passed to the callback
 *
 * Perform prompting for a #SecretPrompt.
 *
 * This function is called by other parts of this library to handle prompts
 * for the various actions that can require prompting.
 *
 * Override the #SecretServiceClass <literal>prompt_async</literal> virtual method
 * to change the behavior of the propmting. The default behavior is to simply
 * run secret_prompt_perform() on the prompt.
 */
void
secret_service_prompt (SecretService *self,
                       SecretPrompt *prompt,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	SecretServiceClass *klass;

	g_return_if_fail (SECRET_IS_SERVICE (self));
	g_return_if_fail (SECRET_IS_PROMPT (prompt));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	klass = SECRET_SERVICE_GET_CLASS (self);
	g_return_if_fail (klass->prompt_async != NULL);

	(klass->prompt_async) (self, prompt, cancellable, callback, user_data);
}

/**
 * secret_service_prompt_finish:
 * @self: the secret service
 * @result: the asynchronous result passed to the callback
 * @error: location to place an error on failure
 *
 * Complete asynchronous operation to perform prompting for a #SecretPrompt.
 *
 * Returns: %FALSE if the prompt was dismissed or an error occurred
 */
gboolean
secret_service_prompt_finish (SecretService *self,
                              GAsyncResult *result,
                              GError **error)
{
	SecretServiceClass *klass;

	g_return_val_if_fail (SECRET_IS_SERVICE (self), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	klass = SECRET_SERVICE_GET_CLASS (self);
	g_return_val_if_fail (klass->prompt_finish != NULL, FALSE);

	return (klass->prompt_finish) (self, result, error);
}
