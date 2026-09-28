/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * mln-json -- the JSON a layout is kept as, read and written.
 *
 * Small on purpose: a layout is six keys deep in nothing, and a JSON
 * library is one more thing for every app bundle to carry. What is here
 * is what JSON.parse and JSON.stringify do for the values a layout
 * holds, because a layout kept by mullion in a browser has to be read
 * here, and one kept here read there, byte for byte (docs/layout.md in
 * mullion).
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  MLN_JSON_NULL,
  MLN_JSON_BOOL,
  MLN_JSON_NUMBER,
  MLN_JSON_STRING,
  MLN_JSON_ARRAY,
  MLN_JSON_OBJECT,
} MlnJsonType;

typedef struct _MlnJson MlnJson;

/* Nesting deeper than this is refused rather than recursed into: a kept
   layout is text anybody's settings file can hold, and a stack is not a
   thing to hand it. */
#define MLN_JSON_MAX_DEPTH 256

MlnJson     *mln_json_parse        (const char *text);
void         mln_json_free         (MlnJson *value);

MlnJsonType  mln_json_type         (const MlnJson *value);
gboolean     mln_json_bool         (const MlnJson *value);
double       mln_json_number       (const MlnJson *value);
const char  *mln_json_string       (const MlnJson *value);
guint        mln_json_length       (const MlnJson *value);
MlnJson     *mln_json_index        (const MlnJson *value, guint i);
MlnJson     *mln_json_member       (const MlnJson *value, const char *key);

gboolean     mln_json_equal        (const MlnJson *a, const MlnJson *b);

/* Appended as JSON.stringify writes them. */
void         mln_json_write        (GString *out, const MlnJson *value);
void         mln_json_write_string (GString *out, const char *s);
void         mln_json_write_number (GString *out, double d);

G_END_DECLS
