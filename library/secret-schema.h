/* libsecret - GLib wrapper for Secret Service
 *
 * Copyright 2011 Red Hat Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2 of the licence or (at
 * your option) any later version.
 *
 * See the included COPYING file for more information.
 */

#if !defined (__SECRET_INSIDE_HEADER__) && !defined (SECRET_COMPILATION)
#error "Only <secret/secret.h> can be included directly."
#endif

#ifndef __SECRET_SCHEMA_H__
#define __SECRET_SCHEMA_H__

#include <glib.h>

G_BEGIN_DECLS

#define SECRET_SCHEMA_IDENTIFIER_GENERIC     "org.freedesktop.Secret.Generic"
#define SECRET_SCHEMA_IDENTIFIER_NETWORK     "org.gnome.keyring.NetworkPassword"
#define SECRET_SCHEMA_IDENTIFIER_NOTE        "org.gnome.keyring.Note"

typedef enum {
	SECRET_SCHEMA_ATTRIBUTE_STRING = 0,
	SECRET_SCHEMA_ATTRIBUTE_INTEGER = 1,
	SECRET_SCHEMA_ATTRIBUTE_BOOLEAN = 2,
} SecretSchemaAttributeType;

typedef struct {
	const gchar* name;
	SecretSchemaAttributeType type;
} SecretSchemaAttribute;

typedef enum {
	SECRET_SCHEMA_NONE = 0,
	SECRET_SCHEMA_ALLOW_UNDEFINED = 1 << 0,
} SecretSchemaFlags;

typedef struct {
	const gchar *identifier;
	SecretSchemaFlags flags;
	SecretSchemaAttribute attributes[32];

	/* <private> */
	gint reserved;
	gpointer reserved1;
	gpointer reserved2;
	gpointer reserved3;
	gpointer reserved4;
	gpointer reserved5;
	gpointer reserved6;
	gpointer reserved7;
} SecretSchema;

extern const SecretSchema *  SECRET_SCHEMA_NETWORK;

extern const SecretSchema *  SECRET_SCHEMA_GENERIC;

extern const SecretSchema *  SECRET_SCHEMA_NOTE;

GType             secret_schema_get_type           (void) G_GNUC_CONST;

SecretSchema *    secret_schema_new                (const gchar *identifier,
                                                    SecretSchemaFlags flags,
                                                    GHashTable *attributes);

SecretSchema *    secret_schema_ref                (SecretSchema *schema);

void              secret_schema_unref              (SecretSchema *schema);

GType             secret_schema_attribute_get_type (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __SECRET_SCHEMA_H___ */
