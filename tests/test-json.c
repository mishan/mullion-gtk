/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* The JSON reader and writer, against what JSON.parse and JSON.stringify
   do. The expected numbers were written by Node. */

#include "mln-json.h"

#include <string.h>

static char *
written (const char *text)
{
  MlnJson *v = mln_json_parse (text);
  GString *out = g_string_new (NULL);

  g_assert_nonnull (v);
  mln_json_write (out, v);
  mln_json_free (v);

  return g_string_free (out, FALSE);
}

static void
numbers (void)
{
  /* The double (as %.17g-ish text, which reads back exactly) and what
     JSON.stringify writes for it. */
  static const char *cases[][2] = {
    { "0", "0" }, { "-0", "0" }, { "1", "1" }, { "-1", "-1" },
    { "0.5", "0.5" }, { "0.10000000000000001", "0.1" },
    { "0.30000000000000004", "0.30000000000000004" },
    { "0.33333333333333331", "0.3333333333333333" },
    { "0.66666666666666663", "0.6666666666666666" },
    { "100", "100" }, { "1e+21", "1e+21" }, { "1e+20", "100000000000000000000" },
    { "1.2345678901234568e+20", "123456789012345680000" },
    { "9.9999999999999995e-7", "0.000001" }, { "9.9999999999999995e-8", "1e-7" },
    { "1.4999999999999999e-7", "1.5e-7" }, { "12345.678", "12345.678" },
    { "0.001", "0.001" }, { "4.9406564584124654e-324", "5e-324" },
    { "1.7976931348623157e+308", "1.7976931348623157e+308" },
    { "-2.5000000000000002e-10", "-2.5e-10" }, { "1e+100", "1e+100" },
    { "1.23e-18", "1.23e-18" }, { "0.000001234", "0.000001234" },
    { "4.3499999999999996", "4.35" }, { "1.0049999999999999", "1.005" },
    { "9007199254740992", "9007199254740992" },
  };

  for (guint i = 0; i < G_N_ELEMENTS (cases); i++)
    {
      GString *out = g_string_new (NULL);

      mln_json_write_number (out, g_ascii_strtod (cases[i][0], NULL));
      g_assert_cmpstr (out->str, ==, cases[i][1]);
      g_string_free (out, TRUE);
    }
}

static void
strings (void)
{
  char *s = written ("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\\u0001\\u00e9\\ud83d\\ude00\"");

  /* JSON.stringify("a\"b\\c/d\b\f\n\r\t\u0001é😀") */
  g_assert_cmpstr (s, ==, "\"a\\\"b\\\\c/d\\b\\f\\n\\r\\t\\u0001\xc3\xa9\xf0\x9f\x98\x80\"");
  g_free (s);
}

static void
refused (void)
{
  static const char *bad[] = {
    "", " ", "{", "[1,]", "{\"a\":1,}", "01", "1.", ".5", "+1", "0x10",
    "tru", "nul", "\"abc", "\"\\x\"", "\"\\ud800\"", "\"\\udc00\"",
    "\"a\nb\"", "{\"a\" 1}", "[1 2]", "1 2", "NaN", "Infinity", "'a'",
    "{a:1}",
  };

  for (guint i = 0; i < G_N_ELEMENTS (bad); i++)
    {
      MlnJson *v = mln_json_parse (bad[i]);

      if (v != NULL)
        g_error ("parsed %s", bad[i]);
    }
}

static void
accepted (void)
{
  static const char *good[][2] = {
    { " {\"a\" : [ 1 , 2.5e3 , -0.25 , true , false , null ] } ",
      "{\"a\":[1,2500,-0.25,true,false,null]}" },
    { "{\"a\":1,\"b\":2,\"a\":3}", "{\"a\":3,\"b\":2}" },
    { "[]", "[]" }, { "{}", "{}" }, { "\"\"", "\"\"" },
    { "1E2", "100" }, { "1e999", "null" },
  };

  for (guint i = 0; i < G_N_ELEMENTS (good); i++)
    {
      char *s = written (good[i][0]);

      g_assert_cmpstr (s, ==, good[i][1]);
      g_free (s);
    }
}

static void
depth (void)
{
  GString *deep = g_string_new (NULL);
  MlnJson *v;

  for (int i = 0; i < MLN_JSON_MAX_DEPTH; i++)
    g_string_append_c (deep, '[');
  for (int i = 0; i < MLN_JSON_MAX_DEPTH; i++)
    g_string_append_c (deep, ']');

  v = mln_json_parse (deep->str);
  g_assert_nonnull (v);
  mln_json_free (v);

  g_string_prepend_c (deep, '[');
  g_string_append_c (deep, ']');
  g_assert_null (mln_json_parse (deep->str));
  g_string_free (deep, TRUE);
}

static void
members (void)
{
  MlnJson *v = mln_json_parse ("{\"n\":2,\"s\":\"2\",\"o\":{\"x\":[1]}}");
  MlnJson *w = mln_json_parse ("{\"o\":{\"x\":[1]},\"s\":\"2\",\"n\":2}");

  g_assert_cmpfloat (mln_json_number (mln_json_member (v, "n")), ==, 2);
  g_assert_cmpstr (mln_json_string (mln_json_member (v, "s")), ==, "2");
  g_assert_null (mln_json_member (v, "missing"));
  g_assert_false (mln_json_equal (mln_json_member (v, "n"), mln_json_member (v, "s")));
  g_assert_true (mln_json_equal (v, w));
  mln_json_free (v);
  mln_json_free (w);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/json/numbers", numbers);
  g_test_add_func ("/json/strings", strings);
  g_test_add_func ("/json/refused", refused);
  g_test_add_func ("/json/accepted", accepted);
  g_test_add_func ("/json/depth", depth);
  g_test_add_func ("/json/members", members);

  return g_test_run ();
}
