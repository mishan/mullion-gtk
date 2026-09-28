/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "mln-json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct _MlnJson
{
  MlnJsonType type;

  union
  {
    gboolean  b;
    double    d;
    char     *s;
    GPtrArray *items;           /* array: MlnJson*; object: key, value, ... */
  };
};

/* ---- reading ---- */

typedef struct
{
  const char *p;
  int depth;
} Reader;

static MlnJson *read_value (Reader *r);

static MlnJson *
new_value (MlnJsonType type)
{
  MlnJson *v = g_new0 (MlnJson, 1);

  v->type = type;

  return v;
}

static void
skip_space (Reader *r)
{
  /* JSON's white space, which is four characters and not isspace()'s. */
  while (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')
    r->p++;
}

static int
hex4 (const char *p)
{
  int v = 0;

  for (int i = 0; i < 4; i++)
    {
      int d = g_ascii_xdigit_value (p[i]);

      if (d < 0)
        return -1;

      v = v * 16 + d;
    }

  return v;
}

/* A string, with its escapes. A \u pair that makes a surrogate pair is
   one character; a lone surrogate, which JavaScript strings can hold and
   UTF-8 cannot, is refused. */
static char *
read_string (Reader *r)
{
  GString *s = g_string_new (NULL);

  r->p++;                       /* the opening quote */

  for (;;)
    {
      unsigned char c = (unsigned char) *r->p;

      if (c == '"')
        {
          r->p++;
          return g_string_free (s, FALSE);
        }

      if (c < 0x20)             /* an end of text, or a raw control */
        break;

      if (c != '\\')
        {
          g_string_append_c (s, (char) c);
          r->p++;
          continue;
        }

      r->p++;

      switch (*r->p)
        {
        case '"':  g_string_append_c (s, '"');  break;
        case '\\': g_string_append_c (s, '\\'); break;
        case '/':  g_string_append_c (s, '/');  break;
        case 'b':  g_string_append_c (s, '\b'); break;
        case 'f':  g_string_append_c (s, '\f'); break;
        case 'n':  g_string_append_c (s, '\n'); break;
        case 'r':  g_string_append_c (s, '\r'); break;
        case 't':  g_string_append_c (s, '\t'); break;
        case 'u':
          {
            int u = hex4 (r->p + 1);

            if (u < 0)
              goto bad;

            r->p += 4;

            if (u >= 0xD800 && u <= 0xDBFF)
              {
                int lo = r->p[1] == '\\' && r->p[2] == 'u' ? hex4 (r->p + 3) : -1;

                if (lo < 0xDC00 || lo > 0xDFFF)
                  goto bad;

                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                r->p += 6;
              }
            else if (u >= 0xDC00 && u <= 0xDFFF)
              goto bad;

            /* A NUL would end the string where JavaScript's goes on, and
               "a\u0000b" would be read as "a": refused instead. */
            if (u == 0)
              goto bad;

            g_string_append_unichar (s, (gunichar) u);
          }
          break;
        default:
          goto bad;
        }

      r->p++;
    }

bad:
  g_string_free (s, TRUE);
  return NULL;
}

/* A number in JSON's own grammar -- no leading +, no leading zeros, no
   bare point, no hex -- then read with g_ascii_strtod, which is strtod
   without the locale's decimal separator. */
static MlnJson *
read_number (Reader *r)
{
  const char *start = r->p;
  const char *p = r->p;
  MlnJson *v;

  if (*p == '-')
    p++;

  if (*p == '0')
    p++;
  else if (*p >= '1' && *p <= '9')
    while (g_ascii_isdigit (*p))
      p++;
  else
    return NULL;

  if (*p == '.')
    {
      p++;

      if (!g_ascii_isdigit (*p))
        return NULL;

      while (g_ascii_isdigit (*p))
        p++;
    }

  if (*p == 'e' || *p == 'E')
    {
      p++;

      if (*p == '+' || *p == '-')
        p++;

      if (!g_ascii_isdigit (*p))
        return NULL;

      while (g_ascii_isdigit (*p))
        p++;
    }

  v = new_value (MLN_JSON_NUMBER);

  {
    char *text = g_strndup (start, p - start);

    v->d = g_ascii_strtod (text, NULL);
    g_free (text);
  }

  r->p = p;

  return v;
}

static gboolean
word (Reader *r, const char *w)
{
  size_t n = strlen (w);

  if (strncmp (r->p, w, n) != 0)
    return FALSE;

  r->p += n;

  return TRUE;
}

static MlnJson *
read_container (Reader *r, gboolean object)
{
  MlnJson *v = new_value (object ? MLN_JSON_OBJECT : MLN_JSON_ARRAY);
  char close = object ? '}' : ']';

  v->items = g_ptr_array_new ();

  if (++r->depth > MLN_JSON_MAX_DEPTH)
    goto bad;

  r->p++;
  skip_space (r);

  if (*r->p == close)
    {
      r->p++;
      r->depth--;
      return v;
    }

  for (;;)
    {
      MlnJson *item;

      skip_space (r);

      if (object)
        {
          char *key;

          if (*r->p != '"' || (key = read_string (r)) == NULL)
            goto bad;

          g_ptr_array_add (v->items, key);
          skip_space (r);

          if (*r->p != ':')
            goto bad;

          r->p++;
        }

      if ((item = read_value (r)) == NULL)
        goto bad;

      g_ptr_array_add (v->items, item);
      skip_space (r);

      if (*r->p == ',')
        {
          r->p++;
          continue;
        }

      if (*r->p == close)
        {
          r->p++;
          r->depth--;
          return v;
        }

      goto bad;
    }

bad:
  mln_json_free (v);
  return NULL;
}

static MlnJson *
read_value (Reader *r)
{
  skip_space (r);

  switch (*r->p)
    {
    case '{':
      return read_container (r, TRUE);
    case '[':
      return read_container (r, FALSE);
    case '"':
      {
        char *s = read_string (r);
        MlnJson *v;

        if (s == NULL)
          return NULL;

        v = new_value (MLN_JSON_STRING);
        v->s = s;

        return v;
      }
    case 't':
    case 'f':
      {
        gboolean yes = *r->p == 't';
        MlnJson *v;

        if (!word (r, yes ? "true" : "false"))
          return NULL;

        v = new_value (MLN_JSON_BOOL);
        v->b = yes;

        return v;
      }
    case 'n':
      return word (r, "null") ? new_value (MLN_JSON_NULL) : NULL;
    default:
      return read_number (r);
    }
}

/* A whole text, or NULL for anything that is not one value of JSON with
   nothing but white space around it. */
MlnJson *
mln_json_parse (const char *text)
{
  Reader r = { text, 0 };
  MlnJson *v;

  if (text == NULL || !g_utf8_validate (text, -1, NULL))
    return NULL;

  v = read_value (&r);

  if (v == NULL)
    return NULL;

  skip_space (&r);

  if (*r.p != '\0')
    {
      mln_json_free (v);
      return NULL;
    }

  return v;
}

void
mln_json_free (MlnJson *v)
{
  if (v == NULL)
    return;

  if (v->type == MLN_JSON_STRING)
    g_free (v->s);
  else if (v->type == MLN_JSON_ARRAY || v->type == MLN_JSON_OBJECT)
    {
      for (guint i = 0; i < v->items->len; i++)
        {
          /* An object's items alternate key, value. */
          if (v->type == MLN_JSON_OBJECT && i % 2 == 0)
            g_free (v->items->pdata[i]);
          else
            mln_json_free (v->items->pdata[i]);
        }

      g_ptr_array_free (v->items, TRUE);
    }

  g_free (v);
}

/* ---- asking ---- */

MlnJsonType
mln_json_type (const MlnJson *v)
{
  return v->type;
}

gboolean
mln_json_bool (const MlnJson *v)
{
  return v->type == MLN_JSON_BOOL && v->b;
}

double
mln_json_number (const MlnJson *v)
{
  return v->type == MLN_JSON_NUMBER ? v->d : NAN;
}

const char *
mln_json_string (const MlnJson *v)
{
  return v->type == MLN_JSON_STRING ? v->s : NULL;
}

guint
mln_json_length (const MlnJson *v)
{
  if (v->type == MLN_JSON_ARRAY)
    return v->items->len;

  if (v->type == MLN_JSON_OBJECT)
    return v->items->len / 2;

  return 0;
}

MlnJson *
mln_json_index (const MlnJson *v, guint i)
{
  return v->type == MLN_JSON_ARRAY && i < v->items->len
    ? v->items->pdata[i] : NULL;
}

/* The last one of that name, as JSON.parse keeps a key written twice. */
MlnJson *
mln_json_member (const MlnJson *v, const char *key)
{
  if (v->type != MLN_JSON_OBJECT)
    return NULL;

  for (guint i = v->items->len; i >= 2; i -= 2)
    if (strcmp (v->items->pdata[i - 2], key) == 0)
      return v->items->pdata[i - 1];

  return NULL;
}

/* The same value, as a version compares: types first, then contents, and
   an object's members as a set of names. */
gboolean
mln_json_equal (const MlnJson *a, const MlnJson *b)
{
  if (a == NULL || b == NULL)
    return a == b;

  if (a->type != b->type)
    return FALSE;

  switch (a->type)
    {
    case MLN_JSON_NULL:
      return TRUE;
    case MLN_JSON_BOOL:
      return a->b == b->b;
    case MLN_JSON_NUMBER:
      return a->d == b->d;
    case MLN_JSON_STRING:
      return strcmp (a->s, b->s) == 0;
    case MLN_JSON_ARRAY:
      if (a->items->len != b->items->len)
        return FALSE;

      for (guint i = 0; i < a->items->len; i++)
        if (!mln_json_equal (a->items->pdata[i], b->items->pdata[i]))
          return FALSE;

      return TRUE;
    case MLN_JSON_OBJECT:
      if (mln_json_length (a) != mln_json_length (b))
        return FALSE;

      for (guint i = 0; i < a->items->len; i += 2)
        {
          MlnJson *other = mln_json_member (b, a->items->pdata[i]);

          if (other == NULL ||
              !mln_json_equal (mln_json_member (a, a->items->pdata[i]), other))
            return FALSE;
        }

      return TRUE;
    }

  return FALSE;
}

/* ---- writing ---- */

/* As JSON.stringify quotes a string: the two that must be escaped, the
   five controls with a short form, every other control as \u00XX. */
void
mln_json_write_string (GString *out, const char *s)
{
  g_string_append_c (out, '"');

  for (const unsigned char *p = (const unsigned char *) s; *p != '\0'; p++)
    switch (*p)
      {
      case '"':  g_string_append (out, "\\\""); break;
      case '\\': g_string_append (out, "\\\\"); break;
      case '\b': g_string_append (out, "\\b");  break;
      case '\f': g_string_append (out, "\\f");  break;
      case '\n': g_string_append (out, "\\n");  break;
      case '\r': g_string_append (out, "\\r");  break;
      case '\t': g_string_append (out, "\\t");  break;
      default:
        if (*p < 0x20)
          g_string_append_printf (out, "\\u%04x", *p);
        else
          g_string_append_c (out, (char) *p);
      }

  g_string_append_c (out, '"');
}

/*
 * As JavaScript writes a number (ECMA-262, Number::toString): the fewest
 * digits that read back as the same double, then placed by where the
 * point falls -- plain up to 21 digits before it and 6 zeros after it,
 * an exponent outside that.
 *
 * The digits come from printf, asking for one more each time until
 * strtod gives the same double back; printf rounds correctly, so the
 * first that does is the shortest and, of those, the nearest -- which is
 * what JavaScript picks too.
 */
void
mln_json_write_number (GString *out, double d)
{
  char buf[40];
  char digits[20];
  int k = 0, e = 0, n;

  if (!isfinite (d))
    {
      g_string_append (out, "null");    /* as JSON.stringify writes NaN */
      return;
    }

  if (d == 0)
    {
      g_string_append_c (out, '0');     /* and -0 too */
      return;
    }

  if (d < 0)
    {
      g_string_append_c (out, '-');
      d = -d;
    }

  for (int p = 0; p <= 17; p++)
    {
      g_snprintf (buf, sizeof buf, "%.*e", p, d);

      if (g_ascii_strtod (buf, NULL) == d)
        break;
    }

  /* buf is D.DDDDe±XX, or De±XX. */
  for (const char *c = buf; *c != 'e'; c++)
    if (g_ascii_isdigit (*c))
      digits[k++] = *c;

  e = atoi (strchr (buf, 'e') + 1);

  /* Trailing zeros are not digits JavaScript counts. */
  while (k > 1 && digits[k - 1] == '0')
    k--;

  digits[k] = '\0';
  n = e + 1;                    /* d = 0.digits x 10^n */

  if (k <= n && n <= 21)
    {
      g_string_append (out, digits);

      for (int i = 0; i < n - k; i++)
        g_string_append_c (out, '0');
    }
  else if (0 < n && n <= 21)
    {
      g_string_append_len (out, digits, n);
      g_string_append_c (out, '.');
      g_string_append (out, digits + n);
    }
  else if (-6 < n && n <= 0)
    {
      g_string_append (out, "0.");

      for (int i = 0; i < -n; i++)
        g_string_append_c (out, '0');

      g_string_append (out, digits);
    }
  else
    {
      g_string_append_c (out, digits[0]);

      if (k > 1)
        {
          g_string_append_c (out, '.');
          g_string_append (out, digits + 1);
        }

      g_string_append_printf (out, "e%c%d", n - 1 < 0 ? '-' : '+', abs (n - 1));
    }
}

void
mln_json_write (GString *out, const MlnJson *v)
{
  switch (v->type)
    {
    case MLN_JSON_NULL:
      g_string_append (out, "null");
      break;
    case MLN_JSON_BOOL:
      g_string_append (out, v->b ? "true" : "false");
      break;
    case MLN_JSON_NUMBER:
      mln_json_write_number (out, v->d);
      break;
    case MLN_JSON_STRING:
      mln_json_write_string (out, v->s);
      break;
    case MLN_JSON_ARRAY:
      g_string_append_c (out, '[');

      for (guint i = 0; i < v->items->len; i++)
        {
          if (i > 0)
            g_string_append_c (out, ',');

          mln_json_write (out, v->items->pdata[i]);
        }

      g_string_append_c (out, ']');
      break;
    case MLN_JSON_OBJECT:
      /* In the order written, once per name, as a parsed object keeps
         them: the first place a name appears, with the last value. */
      g_string_append_c (out, '{');

      {
        gboolean first = TRUE;

        for (guint i = 0; i < v->items->len; i += 2)
          {
            const char *key = v->items->pdata[i];
            gboolean seen = FALSE;

            for (guint j = 0; j < i; j += 2)
              if (strcmp (v->items->pdata[j], key) == 0)
                seen = TRUE;

            if (seen)
              continue;

            if (!first)
              g_string_append_c (out, ',');

            first = FALSE;
            mln_json_write_string (out, key);
            g_string_append_c (out, ':');
            mln_json_write (out, mln_json_member (v, key));
          }
      }

      g_string_append_c (out, '}');
      break;
    }
}
