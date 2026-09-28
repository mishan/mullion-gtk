/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * mullion's layout cases (test/layouts.json there, copied here as
 * tests/layouts.json): each case's kept text read back, and what comes up
 * kept again, byte for byte against what mullion does in a browser.
 */

#include "mln-json.h"
#include "mln-model.h"

typedef struct
{
  const MlnJson *later;
  char *stored;
} Hooked;

static gboolean
later (const char *id, gpointer data)
{
  Hooked *h = data;

  if (h->later == NULL)
    return FALSE;

  for (guint i = 0; i < mln_json_length (h->later); i++)
    if (g_strcmp0 (mln_json_string (mln_json_index (h->later, i)), id) == 0)
      return TRUE;

  return FALSE;
}

static void
store (const char *mode, const char *text, gpointer data)
{
  Hooked *h = data;

  g_free (h->stored);
  h->stored = g_strdup (text);
}

static char *
text_of (const MlnJson *v)
{
  GString *out = g_string_new (NULL);

  mln_json_write (out, v);

  return g_string_free (out, FALSE);
}

static void
run_case (gconstpointer data)
{
  const MlnJson *c = data;
  const MlnJson *panes = mln_json_member (c, "panes");
  const MlnJson *version = mln_json_member (c, "version");
  const MlnJson *stored = mln_json_member (c, "stored");
  MlnModelHooks hooks = { .later = later, .store = store };
  Hooked h = { mln_json_member (c, "later"), NULL };
  MlnModel *m = mln_model_new (&hooks, &h);
  char *expect = text_of (mln_json_member (c, "expect"));
  char *got, *other;

  for (guint i = 0; i < mln_json_length (panes); i++)
    mln_model_register (m, mln_json_string (mln_json_index (panes, i)), 0);

  if (version != NULL)
    {
      char *v = text_of (version);

      g_assert_true (mln_model_set_version (m, v));
      g_free (v);
    }

  mln_model_set_mode (m, "m");
  g_assert_cmpint (mln_model_load (m, mln_json_string (stored)), ==,
                   !mln_json_bool (mln_json_member (c, "refused")));

  /* Drawn, which clamps each front tab to the tabs in play. */
  mln_model_settle (m);

  got = mln_model_json (m);
  g_assert_cmpstr (got, ==, expect);
  g_free (got);

  /* Kept: put up by the app after another, so that it is a change. */
  {
    GString *o = g_string_new ("{\"dir\":\"row\",\"size\":[0.3,0.7],\"kids\":[{\"tabs\":[");

    mln_json_write_string (o, mln_json_string (mln_json_index (panes, 0)));
    g_string_append (o, "]},{\"tabs\":[");
    mln_json_write_string (o, mln_json_string (mln_json_index (panes, 1)));
    g_string_append (o, "]}]}");
    other = g_string_free (o, FALSE);
  }
  g_assert_true (mln_model_set_layout (m, other));
  g_assert_true (mln_model_set_layout (m, expect));
  g_assert_cmpstr (h.stored, ==,
                   mln_json_string (mln_json_member (c, "written")));

  g_free (other);
  g_free (expect);
  g_free (h.stored);
  mln_model_free (m);
}

int
main (int argc, char **argv)
{
  char *path, *text;
  MlnJson *corpus;
  const MlnJson *cases;

  g_test_init (&argc, &argv, NULL);

  path = g_build_filename (g_getenv ("MLN_FIXTURES"), "layouts.json", NULL);

  if (!g_file_get_contents (path, &text, NULL, NULL))
    g_error ("no %s", path);

  corpus = mln_json_parse (text);
  g_assert_nonnull (corpus);
  cases = mln_json_member (corpus, "cases");

  for (guint i = 0; i < mln_json_length (cases); i++)
    {
      const MlnJson *c = mln_json_index (cases, i);
      char *name = g_strdup_printf ("/layouts/%02u %s", i,
                                    mln_json_string (mln_json_member (c, "name")));

      g_test_add_data_func (name, c, run_case);
      g_free (name);
    }

  {
    int status = g_test_run ();

    mln_json_free (corpus);
    g_free (text);
    g_free (path);

    return status;
  }
}
