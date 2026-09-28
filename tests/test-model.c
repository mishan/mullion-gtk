/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * What a person and an app do to a layout, and what the tree is after:
 * closing a pane and bringing it back where it was, splits collapsing
 * and being made again, drops, modes, panes added and removed.
 *
 * Trees are compared as JSON, as mln_model_json writes them.
 */

#include "mln-model.h"

#include <string.h>

typedef struct
{
  int changed;
  int stored;
  gboolean forgot;
  char *kept;
  GPtrArray *discarded;
  GPtrArray *later;
} Heard;

static gboolean
later_cb (const char *id, gpointer data)
{
  Heard *h = data;

  for (guint i = 0; h->later != NULL && i < h->later->len; i++)
    if (strcmp (h->later->pdata[i], id) == 0)
      return TRUE;

  return FALSE;
}

static void
discard_cb (const char *id, gpointer data)
{
  Heard *h = data;

  g_ptr_array_add (h->discarded, g_strdup (id));
}

static void
store_cb (const char *mode, const char *text, gpointer data)
{
  Heard *h = data;

  h->stored++;
  h->forgot = text == NULL;
  g_free (h->kept);
  h->kept = g_strdup (text);
}

static void
changed_cb (const char *mode, gpointer data)
{
  Heard *h = data;

  h->changed++;
}

typedef struct
{
  MlnModel *m;
  Heard h;
} Fixture;

/* A model with these panes (each 100px wide), laid out as `layout'. */
static void
setup (Fixture *f, const char *ids, const char *layout, gboolean discard)
{
  MlnModelHooks hooks = { later_cb, discard ? discard_cb : NULL, store_cb, changed_cb };
  char **each = g_strsplit (ids, " ", -1);

  memset (&f->h, 0, sizeof f->h);
  f->h.discarded = g_ptr_array_new_with_free_func (g_free);
  f->h.later = g_ptr_array_new_with_free_func (g_free);
  f->m = mln_model_new (&hooks, &f->h);

  for (char **id = each; *id != NULL; id++)
    mln_model_register (f->m, *id, 100);

  g_strfreev (each);

  if (layout != NULL)
    g_assert_true (mln_model_set_default (f->m, "m", layout));

  mln_model_set_mode (f->m, "m");
  mln_model_load (f->m, NULL);
  mln_model_settle (f->m);
  f->h.changed = 0;
  f->h.stored = 0;
}

static void
teardown (Fixture *f)
{
  mln_model_free (f->m);
  g_free (f->h.kept);
  g_ptr_array_free (f->h.discarded, TRUE);
  g_ptr_array_free (f->h.later, TRUE);
}

#define assert_tree(f, want)                                            \
  G_STMT_START {                                                        \
    char *got_ = mln_model_json ((f)->m);                               \
    g_assert_cmpstr (got_, ==, want);                                   \
    g_free (got_);                                                      \
  } G_STMT_END

#define L(...) "{\"tabs\":[" __VA_ARGS__ "]"

/* ---- closing, and coming back ---- */

static void
close_and_present_beside (void)
{
  Fixture f;

  setup (&f, "a b",
         "{\"dir\":\"row\",\"size\":[1,3],\"kids\":[{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"]}]}",
         FALSE);

  g_assert_true (mln_model_close (f.m, "b"));
  assert_tree (&f, "{\"tabs\":[\"a\"],\"active\":0}");
  g_assert_cmpint (mln_model_where (f.m, "b"), ==, MLN_WHERE_DRAWER);
  g_assert_cmpint (f.h.changed, ==, 1);
  g_assert_cmpint (f.h.stored, ==, 1);

  {
    GPtrArray *closed = mln_model_closed (f.m);

    g_assert_cmpuint (closed->len, ==, 1);
    g_assert_cmpstr (closed->pdata[0], ==, "b");
    g_ptr_array_unref (closed);
  }

  /* Back beside the neighbor it had, on the same side, with its share. */
  mln_model_present (f.m, "b", NULL, NULL);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[0.25,0.75],\"kids\":"
                   "[{\"tabs\":[\"a\"],\"active\":0},{\"tabs\":[\"b\"],\"active\":0}]}");
  teardown (&f);
}

static void
close_from_a_stack (void)
{
  Fixture f;

  setup (&f, "a b c", "{\"tabs\":[\"a\",\"b\",\"c\"],\"active\":2}", FALSE);

  /* What was in front stays in front, though its index moves. */
  mln_model_close (f.m, "a");
  assert_tree (&f, "{\"tabs\":[\"b\",\"c\"],\"active\":1}");

  /* And back into the leaf it was in, in front. */
  mln_model_present (f.m, "a", NULL, NULL);
  assert_tree (&f, "{\"tabs\":[\"b\",\"c\",\"a\"],\"active\":2}");
  teardown (&f);
}

static void
collapse_and_make_again (void)
{
  Fixture f;
  const char *three =
    "{\"dir\":\"row\",\"size\":[0.5,0.5],\"kids\":[{\"tabs\":[\"a\"],\"active\":0},"
    "{\"dir\":\"col\",\"size\":[0.5,0.5],\"kids\":[{\"tabs\":[\"b\"],\"active\":0},"
    "{\"tabs\":[\"c\"],\"active\":0}]}]}";

  setup (&f, "a b c", three, FALSE);

  /* b out: the column is left with c alone, and collapses into it. */
  mln_model_close (f.m, "b");
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[0.5,0.5],\"kids\":"
                   "[{\"tabs\":[\"a\"],\"active\":0},{\"tabs\":[\"c\"],\"active\":0}]}");

  /* c out too: the row is left with a alone. */
  mln_model_close (f.m, "c");
  assert_tree (&f, "{\"tabs\":[\"a\"],\"active\":0}");

  /* c back, beside a; then b back beside c, in a column made again --
     which it can only do because c came back in the very leaf it left,
     the one b remembers as its neighbor. */
  mln_model_present (f.m, "c", NULL, NULL);
  mln_model_present (f.m, "b", NULL, NULL);
  assert_tree (&f, three);
  teardown (&f);
}

static void
come_back_beside_what_is_left (void)
{
  Fixture f;

  /* a | (b / c): close c, whose neighbor b is then all that is left of the
     column, and close b's column away by closing b... then bring c back:
     beside a, where the column was, as a split made for it. */
  setup (&f, "a b c",
         "{\"dir\":\"row\",\"size\":[0.4,0.6],\"kids\":[{\"tabs\":[\"a\"]},"
         "{\"dir\":\"col\",\"size\":[0.5,0.5],\"kids\":[{\"tabs\":[\"b\"]},{\"tabs\":[\"c\"]}]}]}",
         FALSE);

  mln_model_close (f.m, "c");       /* the column collapses into b */
  mln_model_drop_into (f.m, "b", mln_model_leaf_with (f.m, "a"));
  assert_tree (&f, "{\"tabs\":[\"a\",\"b\"],\"active\":1}");

  mln_model_present (f.m, "c", NULL, NULL);
  assert_tree (&f, "{\"tabs\":[\"a\",\"b\",\"c\"],\"active\":2}");
  teardown (&f);
}

static void
present_avoids_a_leaf (void)
{
  Fixture f;
  MlnNode *busy;

  setup (&f, "a b c",
         "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\",\"c\"]},{\"tabs\":[\"b\"]}]}",
         FALSE);

  mln_model_close (f.m, "c");
  busy = mln_model_leaf_with (f.m, "a");

  /* Raised at somebody working in a's leaf: not in front of them, even
     though that is where c was. */
  mln_model_present (f.m, "c", mln_model_leaf_with (f.m, "b"), busy);
  g_assert_true (mln_model_leaf_with (f.m, "c") == mln_model_leaf_with (f.m, "b"));
  teardown (&f);
}

/* ---- drops ---- */

static void
drops (void)
{
  Fixture f;
  MlnNode *leaf;

  setup (&f, "a b c", "{\"tabs\":[\"a\",\"b\",\"c\"]}", FALSE);
  leaf = mln_model_tree (f.m);

  /* Onto the strip, before a. */
  mln_model_drop_tab (f.m, "c", leaf, "a");
  assert_tree (&f, "{\"tabs\":[\"c\",\"a\",\"b\"],\"active\":0}");

  /* Into the leaf it is in, nowhere in particular: in front, not moved,
     and no change. */
  f.h.changed = 0;
  mln_model_drop_into (f.m, "c", leaf);
  g_assert_cmpint (f.h.changed, ==, 0);

  /* Beside, below. */
  mln_model_drop_beside (f.m, "b", leaf, MLN_COL, TRUE);
  assert_tree (&f, "{\"dir\":\"col\",\"size\":[0.5,0.5],\"kids\":"
                   "[{\"tabs\":[\"c\",\"a\"],\"active\":0},{\"tabs\":[\"b\"],\"active\":0}]}");

  /* A pane alone in its leaf cannot be split off it. */
  f.h.changed = 0;
  mln_model_drop_beside (f.m, "b", mln_model_leaf_with (f.m, "b"), MLN_ROW, FALSE);
  g_assert_cmpint (f.h.changed, ==, 0);

  /* Dropped at the end of the other strip: its leaf empties and the
     split goes. */
  mln_model_drop_tab (f.m, "b", mln_model_leaf_with (f.m, "a"), NULL);
  assert_tree (&f, "{\"tabs\":[\"c\",\"a\",\"b\"],\"active\":2}");
  teardown (&f);
}

/* ---- modes, and panes in and out of play ---- */

static void
availability_keeps_the_front_tab (void)
{
  Fixture f;

  setup (&f, "a b c", "{\"tabs\":[\"a\",\"b\",\"c\"],\"active\":1}", FALSE);

  mln_model_set_available (f.m, "a", FALSE);
  assert_tree (&f, "{\"tabs\":[\"a\",\"b\",\"c\"],\"active\":0}");
  g_assert_cmpint (mln_model_where (f.m, "b"), ==, MLN_WHERE_FRONT);
  g_assert_cmpint (mln_model_where (f.m, "a"), ==, MLN_WHERE_OFF);

  mln_model_set_available (f.m, "a", TRUE);
  assert_tree (&f, "{\"tabs\":[\"a\",\"b\",\"c\"],\"active\":1}");
  teardown (&f);
}

static void
modes_have_layouts_of_their_own (void)
{
  Fixture f;

  setup (&f, "a b", "{\"tabs\":[\"a\",\"b\"]}", FALSE);
  g_assert_true (mln_model_set_default (f.m, "other", "{\"tabs\":[\"b\"]}"));

  mln_model_set_mode (f.m, "other");
  g_assert_false (mln_model_loaded (f.m));
  mln_model_load (f.m, NULL);
  assert_tree (&f, "{\"tabs\":[\"b\"],\"active\":0}");

  /* A kept layout wins over the default. */
  mln_model_set_mode (f.m, "m");
  g_assert_true (mln_model_load (f.m, "{\"tabs\":[\"b\",\"a\"],\"active\":1}"));
  assert_tree (&f, "{\"tabs\":[\"b\",\"a\"],\"active\":1}");
  teardown (&f);
}

/* ---- panes the app adds and removes ---- */

static void
added_panes (void)
{
  Fixture f;
  MlnNode *leaf;
  gboolean added;

  setup (&f, "a b",
         "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"]}]}",
         TRUE);

  /* Beside the pane it was added near. */
  leaf = mln_model_add (f.m, "f1", 100, FALSE, "b", TRUE, &added);
  g_assert_true (added);
  g_assert_true (leaf == mln_model_leaf_with (f.m, "b"));
  g_assert_cmpint (mln_model_where (f.m, "f1"), ==, MLN_WHERE_FRONT);
  g_assert_true (mln_model_ephemeral (f.m, "f1"));

  /* Twice is refused. */
  g_assert_null (mln_model_add (f.m, "f1", 100, FALSE, NULL, TRUE, &added));
  g_assert_false (added);

  /* An ephemeral pane's close asks the app, and moves nothing. */
  g_assert_false (mln_model_close (f.m, "f1"));
  g_assert_cmpuint (f.h.discarded->len, ==, 1);
  g_assert_cmpstr (f.h.discarded->pdata[0], ==, "f1");
  g_assert_nonnull (mln_model_leaf_with (f.m, "f1"));

  /* And the app ends it. */
  g_assert_true (mln_model_remove (f.m, "f1"));
  g_assert_null (mln_model_leaf_with (f.m, "f1"));
  g_assert_false (mln_model_has (f.m, "f1"));

  /* Kept: to the drawer when closed, like any pane from the start. */
  mln_model_add (f.m, "f2", 100, TRUE, NULL, TRUE, NULL);
  g_assert_true (mln_model_close (f.m, "f2"));
  g_assert_cmpint (mln_model_where (f.m, "f2"), ==, MLN_WHERE_DRAWER);
  teardown (&f);
}

static void
ephemeral_without_discard_cannot_close (void)
{
  Fixture f;

  setup (&f, "a", NULL, FALSE);
  mln_model_add (f.m, "e", 100, FALSE, NULL, TRUE, NULL);
  g_assert_false (mln_model_closable (f.m, "e"));
  g_assert_false (mln_model_close (f.m, "e"));
  g_assert_nonnull (mln_model_leaf_with (f.m, "e"));
  teardown (&f);
}

static void
places_kept_for_panes_to_come (void)
{
  Fixture f;

  setup (&f, "a", NULL, FALSE);
  g_ptr_array_add (f.h.later, g_strdup ("f"));

  /* A layout that kept a place for f, read before f is back. */
  g_assert_true (mln_model_load (f.m, "{\"tabs\":[\"a\",\"f\"],\"active\":0}"));
  mln_model_settle (f.m);

  /* Added quietly: into its place, behind what is in front. */
  mln_model_add (f.m, "f", 100, FALSE, NULL, FALSE, NULL);
  assert_tree (&f, "{\"tabs\":[\"a\",\"f\"],\"active\":0}");

  /* In front, then removed while `later' says it will come back: its
     place is kept, and it is in front again when it does. */
  mln_model_present (f.m, "f", NULL, NULL);
  mln_model_remove (f.m, "f");
  assert_tree (&f, "{\"tabs\":[\"a\",\"f\"],\"active\":1}");
  mln_model_add (f.m, "f", 100, FALSE, NULL, FALSE, NULL);
  g_assert_cmpint (mln_model_where (f.m, "f"), ==, MLN_WHERE_FRONT);

  /* What is kept drops the place once `later' stops promising it. */
  mln_model_remove (f.m, "f");
  g_ptr_array_set_size (f.h.later, 0);

  {
    char *kept = mln_model_save (f.m);

    g_assert_cmpstr (kept, ==, "{\"tabs\":[\"a\"],\"active\":0}");
    g_free (kept);
  }

  teardown (&f);
}

static void
stray_panes_are_put_somewhere (void)
{
  Fixture f;

  setup (&f, "a b",
         "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"]}]}",
         TRUE);

  /* Added beside b, and kept. */
  {
    gboolean added;

    g_assert_nonnull (mln_model_add (f.m, "x", 100, TRUE, "b", TRUE, &added));
  }

  /* A layout put up without it: put beside b again. */
  g_assert_true (mln_model_set_layout (f.m, "{\"tabs\":[\"a\",\"b\"]}"));
  g_assert_true (mln_model_leaf_with (f.m, "x") == mln_model_leaf_with (f.m, "b"));

  /* Put away by somebody: stays away when a layout comes up. */
  mln_model_close (f.m, "x");
  g_assert_true (mln_model_set_layout (f.m, "{\"tabs\":[\"b\",\"a\"]}"));
  g_assert_null (mln_model_leaf_with (f.m, "x"));
  teardown (&f);
}

/* ---- the layout as a whole ---- */

static void
set_layout_and_reset (void)
{
  Fixture f;

  setup (&f, "a b", "{\"tabs\":[\"a\",\"b\"]}", FALSE);

  /* Refused, and nothing changed. */
  g_assert_false (mln_model_set_layout (f.m, "{\"dir\":\"row\",\"size\":[1],\"kids\":[]}"));
  g_assert_false (mln_model_set_layout (f.m, "{\"tabs\":[\"nowhere\"]}"));
  g_assert_false (mln_model_set_layout (f.m, "not json"));
  g_assert_cmpint (f.h.changed, ==, 0);

  g_assert_true (mln_model_set_layout (f.m, "{\"tabs\":[\"b\",\"zz\",\"a\"]}"));
  assert_tree (&f, "{\"tabs\":[\"b\",\"a\"],\"active\":0}");
  g_assert_cmpint (f.h.changed, ==, 1);
  g_assert_cmpstr (f.h.kept, ==, "{\"tabs\":[\"b\",\"a\"],\"active\":0}");

  /* The same again is no change. */
  g_assert_true (mln_model_set_layout (f.m, "{\"tabs\":[\"b\",\"a\"]}"));
  g_assert_cmpint (f.h.changed, ==, 1);

  /* Reset: what was kept is forgotten, not written over. */
  mln_model_reset (f.m);
  assert_tree (&f, "{\"tabs\":[\"a\",\"b\"],\"active\":0}");
  g_assert_true (f.h.forgot);
  g_assert_cmpint (f.h.changed, ==, 2);
  teardown (&f);
}

static void
every_pane_closed (void)
{
  Fixture f;

  setup (&f, "a", NULL, FALSE);
  mln_model_close (f.m, "a");
  assert_tree (&f, "{\"tabs\":[],\"active\":0}");

  /* Kept as nothing, which reads back as no layout. */
  g_assert_cmpstr (f.h.kept, ==, "{\"tabs\":[]}");
  g_assert_false (mln_model_load (f.m, f.h.kept));

  /* Back into the one empty leaf there is. */
  mln_model_close (f.m, "a");
  mln_model_present (f.m, "a", NULL, NULL);
  assert_tree (&f, "{\"tabs\":[\"a\"],\"active\":0}");
  teardown (&f);
}

static void
versions (void)
{
  Fixture f;

  setup (&f, "a b", NULL, FALSE);
  g_assert_true (mln_model_set_version (f.m, "3"));
  g_assert_false (mln_model_set_version (f.m, "{"));

  mln_model_close (f.m, "a");
  g_assert_cmpstr (f.h.kept, ==, "{\"version\":3,\"layout\":{\"tabs\":[\"b\"],\"active\":0}}");
  g_assert_true (mln_model_load (f.m, f.h.kept));
  g_assert_false (mln_model_load (f.m, "{\"tabs\":[\"b\"]}"));
  teardown (&f);
}

/* ---- sizes ---- */

static void
sizes (void)
{
  Fixture f;
  MlnNode *row;

  setup (&f, "a b c",
         "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\"]},"
         "{\"dir\":\"col\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"b\"]},{\"tabs\":[\"c\"]}]}]}",
         FALSE);
  mln_model_set_split (f.m, 6);
  mln_model_set_leaf (f.m, 64);
  row = mln_model_tree (f.m);

  /* Across a row the panes' widths and the divider; down a column the
     leaf floors and theirs. */
  g_assert_cmpfloat (mln_model_min_across (f.m, row, TRUE), ==, 206);
  g_assert_cmpfloat (mln_model_min_across (f.m, row, FALSE), ==, 134);

  /* A leaf 205 wide cannot take a pane 100 wide beside its own 100. */
  g_assert_false (mln_model_splittable (f.m, mln_model_leaf_with (f.m, "a"), "b",
                                        MLN_ROW, 205));
  g_assert_true (mln_model_splittable (f.m, mln_model_leaf_with (f.m, "a"), "b",
                                       MLN_ROW, 206));

  /* A divider pulled past a minimum stops at it. */
  f.h.changed = 0;
  mln_model_move_divider (f.m, row, 0, 1, 10, 406);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[0.49261083743842365,1.5073891625615763],"
                   "\"kids\":[{\"tabs\":[\"a\"],\"active\":0},{\"dir\":\"col\",\"size\":[1,1],"
                   "\"kids\":[{\"tabs\":[\"b\"],\"active\":0},{\"tabs\":[\"c\"],\"active\":0}]}]}");
  g_assert_cmpint (f.h.changed, ==, 0);
  mln_model_commit (f.m);
  g_assert_cmpint (f.h.changed, ==, 1);

  /* And even again. */
  mln_model_equalize (f.m, row);
  g_assert_cmpfloat (mln_node_size (row, 0), ==, mln_node_size (row, 1));
  g_assert_cmpint (f.h.changed, ==, 2);
  teardown (&f);
}

static void
zoom (void)
{
  Fixture f;
  MlnNode *a;

  setup (&f, "a b",
         "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"]}]}",
         FALSE);
  a = mln_model_leaf_with (f.m, "a");
  mln_model_set_zoom (f.m, a);

  /* Presenting a pane in another leaf ends it. */
  mln_model_present (f.m, "b", NULL, NULL);
  g_assert_null (mln_model_get_zoom (f.m));

  /* So does its leaf leaving the tree. */
  mln_model_set_zoom (f.m, mln_model_leaf_with (f.m, "b"));
  mln_model_close (f.m, "b");
  mln_model_settle (f.m);
  g_assert_null (mln_model_get_zoom (f.m));
  teardown (&f);
}

static void
stale_focus (void)
{
  Fixture f;

  /* The leaf last focused goes away; a pane with no place of its own to
     come back to goes into what there is, as in mullion. */
  setup (&f, "a b c",
         "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\",\"c\"]},{\"tabs\":[\"b\"]}]}",
         FALSE);
  mln_model_present (f.m, "c", NULL, NULL);
  mln_model_close (f.m, "c");
  mln_model_close (f.m, "a");
  mln_model_settle (f.m);
  g_assert_null (mln_model_get_focus (f.m));
  mln_model_present (f.m, "c", NULL, NULL);
  assert_tree (&f, "{\"tabs\":[\"b\",\"c\"],\"active\":1}");
  teardown (&f);
}

static void
ids_from_the_tree (void)
{
  Fixture f;
  GPtrArray *live;

  /* The ids mln_model_live_tabs hands out are the leaf's own, and closing
     or moving the pane frees them: every entry point has to cope. */
  setup (&f, "a b c", "{\"tabs\":[\"a\",\"b\",\"c\"]}", FALSE);

  live = mln_model_live_tabs (f.m, mln_model_tree (f.m));
  g_assert_true (mln_model_close (f.m, live->pdata[0]));
  g_ptr_array_unref (live);

  live = mln_model_live_tabs (f.m, mln_model_tree (f.m));
  mln_model_drop_beside (f.m, live->pdata[0], mln_model_tree (f.m), MLN_ROW, TRUE);
  g_ptr_array_unref (live);

  live = mln_model_live_tabs (f.m, mln_model_leaf_with (f.m, "c"));
  mln_model_drop_tab (f.m, live->pdata[0], mln_model_leaf_with (f.m, "b"),
                      live->len > 1 ? live->pdata[1] : NULL);
  g_ptr_array_unref (live);

  assert_tree (&f, "{\"tabs\":[\"b\",\"c\"],\"active\":1}");
  teardown (&f);
}

static void
lenient_defaults (void)
{
  Fixture f;

  /* Read through `known' only, as mullion reads a default: a split of one
     is that one, where a kept layout like it would be refused. */
  setup (&f, "a b", "{\"dir\":\"row\",\"size\":[1],\"kids\":[{\"tabs\":[\"b\"]}]}", FALSE);
  assert_tree (&f, "{\"tabs\":[\"b\"],\"active\":0}");
  g_assert_false (mln_model_set_default (f.m, "x", "not json"));
  g_assert_false (mln_model_set_default (f.m, "x", "[1]"));
  g_assert_true (mln_model_set_default (f.m, "x", "{\"dir\":\"up\"}"));
  teardown (&f);
}

static void
reopen_is_a_change (void)
{
  Fixture f;
  MlnNode *b;

  setup (&f, "a b c",
         "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\",\"c\"]},{\"tabs\":[\"b\"]}]}",
         FALSE);
  b = mln_model_leaf_with (f.m, "b");
  mln_model_set_focus (f.m, b);
  mln_model_close (f.m, "c");
  f.h.changed = 0;

  /* Back where it was; a change, told once; the focus not moved. */
  g_assert_true (mln_model_reopen (f.m, "c") == mln_model_leaf_with (f.m, "a"));
  g_assert_cmpint (f.h.changed, ==, 1);
  g_assert_true (mln_model_get_focus (f.m) == b);

  /* Not closed: nothing to reopen. */
  g_assert_null (mln_model_reopen (f.m, "c"));
  teardown (&f);
}

static void
reopen_clears_the_front_kept_for_a_pane_to_come (void)
{
  Fixture f;

  /* An ephemeral pane `later' promises, in front, removed by the app: its
     place and its front are kept. A person then reopens a pane into that
     leaf, which is theirs to have put in front: the pane added back
     quietly goes behind it (the review's differential case). */
  setup (&f, "a b", "{\"tabs\":[\"a\",\"b\"]}", TRUE);
  g_ptr_array_add (f.h.later, g_strdup ("e"));
  mln_model_add (f.m, "e", 100, FALSE, NULL, TRUE, NULL);
  mln_model_close (f.m, "b");
  mln_model_present (f.m, "e", NULL, NULL);
  mln_model_remove (f.m, "e");
  mln_model_reopen (f.m, "b");
  mln_model_add (f.m, "e", 100, FALSE, NULL, FALSE, NULL);
  g_assert_cmpint (mln_model_where (f.m, "b"), ==, MLN_WHERE_FRONT);
  teardown (&f);
}

static MlnModel *reentered;

static void
replace_layout_cb (const char *mode, gpointer data)
{
  if (reentered != NULL)
    {
      MlnModel *m = reentered;

      reentered = NULL;
      mln_model_set_layout (m, "{\"tabs\":[\"a\"]}");
    }
}

static void
hook_reenters_add (void)
{
  MlnModelHooks hooks = { NULL, NULL, NULL, replace_layout_cb };
  MlnModel *m = mln_model_new (&hooks, NULL);

  /* A changed hook that puts up another layout while `add' is telling it:
     the leaf `add' was working with is gone by the time it returns. */
  mln_model_register (m, "a", 100);
  mln_model_register (m, "b", 100);
  mln_model_set_mode (m, "m");
  mln_model_load (m, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"]}]}");
  reentered = m;
  mln_model_add (m, "x", 100, TRUE, "b", TRUE, NULL);
  mln_model_free (m);
}


/* ---- slots and placement ---- */

#define SLOTTED \
  "{\"dir\":\"row\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\"],\"slots\":[\"start\"]}," \
  "{\"tabs\":[\"b\"],\"slots\":[\"end\"]}]}"

static void
slots_are_kept (void)
{
  Fixture f;
  char *kept;

  setup (&f, "a b", SLOTTED, FALSE);

  /* Written after the front tab, and only where there are any. */
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0,\"slots\":[\"start\"]},"
                   "{\"tabs\":[\"b\"],\"active\":0,\"slots\":[\"end\"]}]}");

  kept = mln_model_save (f.m);
  g_assert_true (mln_model_load (f.m, kept));
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0,\"slots\":[\"start\"]},"
                   "{\"tabs\":[\"b\"],\"active\":0,\"slots\":[\"end\"]}]}");

  /* Names only: anything else in the list is not a slot. */
  g_assert_true (mln_model_load (f.m, "{\"tabs\":[\"a\",\"b\"],\"slots\":[\"x\",3,null,\"x\"]}"));
  assert_tree (&f, "{\"tabs\":[\"a\",\"b\"],\"active\":0,\"slots\":[\"x\"]}");

  g_free (kept);
  teardown (&f);
}

static void
slots_move_with_the_room (void)
{
  Fixture f;

  setup (&f, "a b c", SLOTTED, FALSE);
  mln_model_set_placement (f.m, "c", "end", FALSE);

  /* b's leaf empties, and a's takes its room and its slot. */
  mln_model_close (f.m, "b");
  assert_tree (&f, "{\"tabs\":[\"a\"],\"active\":0,\"slots\":[\"start\",\"end\"]}");

  /* So a pane for the end slot goes there. */
  mln_model_present (f.m, "c", NULL, NULL);
  assert_tree (&f, "{\"tabs\":[\"a\",\"c\"],\"active\":1,\"slots\":[\"start\",\"end\"]}");
  teardown (&f);

  /* A leaf dropped on reading, for holding nothing this page has, hands
     its slots on the same way: here to the leaf after it. */
  setup (&f, "a b",
         "{\"dir\":\"row\",\"size\":[1,1,1],\"kids\":[{\"tabs\":[\"x\"],\"slots\":[\"start\"]},"
         "{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"],\"slots\":[\"end\"]}]}", FALSE);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0,\"slots\":[\"start\"]},"
                   "{\"tabs\":[\"b\"],\"active\":0,\"slots\":[\"end\"]}]}");
  teardown (&f);
}

static void
a_slot_before_the_focus (void)
{
  Fixture f;

  setup (&f, "a b c", SLOTTED, FALSE);
  mln_model_set_placement (f.m, "c", "end", FALSE);

  /* c was never in the layout: the drawer, as mullion has it, and not
     put up, since it is not open by placement. */
  g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_DRAWER);

  /* Presented with a's leaf as the fallback, it goes to its slot. */
  mln_model_present (f.m, "c", mln_model_leaf_with (f.m, "a"), NULL);
  g_assert_true (mln_model_leaf_with (f.m, "c") == mln_model_leaf_with (f.m, "b"));

  /* Once it has been somewhere, that is where it goes back to. */
  mln_model_drop_into (f.m, "c", mln_model_leaf_with (f.m, "a"));
  mln_model_close (f.m, "c");
  mln_model_present (f.m, "c", NULL, NULL);
  g_assert_true (mln_model_leaf_with (f.m, "c") == mln_model_leaf_with (f.m, "a"));

  /* A slot that is the leaf to avoid is not used. */
  mln_model_close (f.m, "c");
  mln_model_set_placement (f.m, "c", "start", FALSE);
  mln_model_remove (f.m, "c");
  mln_model_register (f.m, "c", 100);
  mln_model_present (f.m, "c", mln_model_leaf_with (f.m, "b"),
                     mln_model_leaf_with (f.m, "a"));
  g_assert_true (mln_model_leaf_with (f.m, "c") == mln_model_leaf_with (f.m, "b"));
  teardown (&f);
}

static void
open_where_missing (void)
{
  Fixture f;

  setup (&f, "a b c", SLOTTED, FALSE);
  mln_model_set_placement (f.m, "c", "end", TRUE);

  /* A kept layout from before c: c comes up, at the end of its slot's
     strip, behind what is in front there. */
  g_assert_true (mln_model_load (f.m, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":"
                                      "[{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"],\"slots\":[\"end\"]}]}"));
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0},"
                   "{\"tabs\":[\"b\",\"c\"],\"active\":0,\"slots\":[\"end\"]}]}");

  /* Nothing to say it was put away, so no envelope. */
  g_assert_cmpstr (f.h.kept, ==, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0},"
                   "{\"tabs\":[\"b\",\"c\"],\"active\":0,\"slots\":[\"end\"]}]}");

  /* Closed by a person: kept as closed, and so it stays closed. */
  mln_model_close (f.m, "c");
  g_assert_cmpstr (f.h.kept, ==, "{\"layout\":{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0},"
                   "{\"tabs\":[\"b\"],\"active\":0,\"slots\":[\"end\"]}]},\"closed\":[\"c\"]}");

  {
    char *kept = g_strdup (f.h.kept);

    g_assert_true (mln_model_load (f.m, kept));
    g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_DRAWER);
    g_free (kept);
  }

  /* Reset forgets that: the default, and c open in it. */
  mln_model_reset (f.m);
  g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_BEHIND);
  g_assert_true (mln_model_leaf_with (f.m, "c") == mln_model_leaf_with (f.m, "b"));

  /* No slot in the tree: the first leaf. */
  mln_model_set_placement (f.m, "c", "nowhere", TRUE);
  g_assert_true (mln_model_load (f.m, "{\"tabs\":[\"a\",\"b\"],\"active\":1}"));
  assert_tree (&f, "{\"tabs\":[\"a\",\"b\",\"c\"],\"active\":1}");
  teardown (&f);
}

static void
closed_with_a_version (void)
{
  Fixture f;

  setup (&f, "a b c", NULL, FALSE);
  g_assert_true (mln_model_set_version (f.m, "2"));
  mln_model_set_placement (f.m, "c", NULL, TRUE);
  mln_model_close (f.m, "c");
  g_assert_cmpstr (f.h.kept, ==, "{\"version\":2,\"layout\":{\"tabs\":[\"a\",\"b\"],"
                                 "\"active\":0},\"closed\":[\"c\"]}");

  /* An envelope with a version is no layout for a page without one, and
     a closed list does not make it one. */
  g_assert_true (mln_model_set_version (f.m, NULL));
  g_assert_false (mln_model_load (f.m, "{\"version\":2,\"layout\":{\"tabs\":[\"a\"]},"
                                        "\"closed\":[\"c\"]}"));
  teardown (&f);
}

static void
closed_panes_to_come (void)
{
  Fixture f;

  setup (&f, "a", NULL, FALSE);
  g_ptr_array_add (f.h.later, g_strdup ("y"));
  g_ptr_array_add (f.h.later, g_strdup ("x"));

  /* Put away by a person before the app added them this time: kept that
     way, in an order of its own, while the page still promises them. */
  g_assert_true (mln_model_load (f.m, "{\"layout\":{\"tabs\":[\"a\"]},\"closed\":[\"y\",\"x\",\"gone\"]}"));
  mln_model_close (f.m, "a");
  mln_model_present (f.m, "a", NULL, NULL);
  g_assert_cmpstr (f.h.kept, ==, "{\"layout\":{\"tabs\":[\"a\"],\"active\":0},"
                                 "\"closed\":[\"x\",\"y\"]}");
  teardown (&f);
}


static void
closed_only_by_a_person (void)
{
  Fixture f;

  /* Placed open after the layout is up, and never put up: nobody closed
     it, so it is not kept as closed (and comes up next time). */
  setup (&f, "a b c", "{\"tabs\":[\"a\",\"b\"]}", FALSE);
  mln_model_set_placement (f.m, "c", NULL, TRUE);
  mln_model_raise (f.m, mln_model_leaf_with (f.m, "a"), 1);
  g_assert_cmpstr (f.h.kept, ==, "{\"tabs\":[\"a\",\"b\"],\"active\":1}");

  /* Nor when a layout the app puts up leaves it out. */
  mln_model_reset (f.m);
  g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_BEHIND);
  g_assert_true (mln_model_set_layout (f.m, "{\"tabs\":[\"b\",\"a\"]}"));
  g_assert_cmpstr (f.h.kept, ==, "{\"tabs\":[\"b\",\"a\"],\"active\":0}");

  /* Closed by a person, it is; and no longer opened, it is not. */
  mln_model_present (f.m, "c", NULL, NULL);
  mln_model_close (f.m, "c");
  g_assert_cmpstr (f.h.kept, ==, "{\"layout\":{\"tabs\":[\"b\",\"a\"],\"active\":1},\"closed\":[\"c\"]}");
  mln_model_set_placement (f.m, "c", NULL, FALSE);
  g_assert_cmpstr (f.h.kept, ==, "{\"tabs\":[\"b\",\"a\"],\"active\":1}");
  teardown (&f);
}

static void
closed_read_back_and_modes (void)
{
  Fixture f;

  setup (&f, "a b c", NULL, FALSE);
  g_assert_true (mln_model_set_version (f.m, "2"));
  mln_model_set_placement (f.m, "c", NULL, TRUE);

  /* Read back under its version: kept closed, and still listed. */
  g_assert_true (mln_model_load (f.m, "{\"version\":2,\"layout\":{\"tabs\":[\"a\",\"b\"]},\"closed\":[\"c\"]}"));
  g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_DRAWER);
  mln_model_raise (f.m, mln_model_leaf_with (f.m, "a"), 1);
  g_assert_cmpstr (f.h.kept, ==, "{\"version\":2,\"layout\":{\"tabs\":[\"a\",\"b\"],\"active\":1},"
                                 "\"closed\":[\"c\"]}");

  /* A layout that is refused says nothing about what is closed. */
  g_ptr_array_add (f.h.later, g_strdup ("y"));
  g_assert_false (mln_model_load (f.m, "{\"version\":2,\"layout\":{\"tabs\":[\"y\"]},\"closed\":[\"c\"]}"));
  g_assert_cmpint (mln_model_where (f.m, "c"), !=, MLN_WHERE_DRAWER);

  /* And what one mode's layout said is not another's. */
  g_assert_true (mln_model_load (f.m, "{\"version\":2,\"layout\":{\"tabs\":[\"a\"]},\"closed\":[\"y\"]}"));
  mln_model_set_mode (f.m, "other");
  mln_model_load (f.m, NULL);
  mln_model_raise (f.m, mln_model_leaf_with (f.m, "a"), 1);
  g_assert_null (strstr (f.h.kept, "closed"));
  teardown (&f);
}

static void
added_panes_are_not_listed (void)
{
  Fixture f;
  gboolean added;

  setup (&f, "a", NULL, TRUE);
  mln_model_set_placement (f.m, "c", NULL, TRUE);
  mln_model_add (f.m, "c", 100, TRUE, NULL, TRUE, &added);
  mln_model_close (f.m, "c");
  g_assert_null (strstr (f.h.kept, "closed"));
  teardown (&f);
}

static void
a_slot_comes_back_with_its_room (void)
{
  Fixture f;

  setup (&f, "a b c", SLOTTED, FALSE);
  mln_model_set_placement (f.m, "c", "end", FALSE);

  /* b closed, the end slot lent to a; b back where it was, and the slot
     with it. */
  mln_model_close (f.m, "b");
  mln_model_present (f.m, "b", NULL, NULL);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[0.5,0.5],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0,\"slots\":[\"start\"]},"
                   "{\"tabs\":[\"b\"],\"active\":0,\"slots\":[\"end\"]}]}");

  mln_model_present (f.m, "c", NULL, NULL);
  g_assert_true (mln_model_leaf_with (f.m, "c") == mln_model_leaf_with (f.m, "b"));
  teardown (&f);
}

static void
dropped_slots_go_as_emptied_ones_do (void)
{
  Fixture f;

  /* A leaf in the middle, dropped on reading: to the leaf before it, as
     closing its last pane would. */
  setup (&f, "a b",
         "{\"dir\":\"row\",\"size\":[1,1,1],\"kids\":[{\"tabs\":[\"a\"]},"
         "{\"tabs\":[\"x\"],\"slots\":[\"s\"]},{\"tabs\":[\"b\"]}]}", FALSE);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0,\"slots\":[\"s\"]},"
                   "{\"tabs\":[\"b\"],\"active\":0}]}");
  teardown (&f);

  /* And a slot two leaves name is the first one's. */
  setup (&f, "a b",
         "{\"dir\":\"row\",\"size\":[1,1,1],\"kids\":[{\"tabs\":[\"x\"],\"slots\":[\"s\"]},"
         "{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"],\"slots\":[\"s\"]}]}", FALSE);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,1],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0,\"slots\":[\"s\"]},"
                   "{\"tabs\":[\"b\"],\"active\":0}]}");
  teardown (&f);
}

/* ---- floating windows ---- */

static guint
only_float (Fixture *f)
{
  GArray *ids = mln_model_floats (f->m);
  guint id = ids->len == 1 ? g_array_index (ids, guint, 0) : 0;

  g_array_unref (ids);

  return id;
}

static void
undock_and_dock (void)
{
  Fixture f;
  const char *two =
    "{\"dir\":\"row\",\"size\":[1,3],\"kids\":[{\"tabs\":[\"a\"],\"active\":0},"
    "{\"tabs\":[\"b\",\"c\"],\"active\":1}]}";
  guint id;

  setup (&f, "a b c", two, FALSE);

  /* c into a window of its own: out of its leaf, into the window's. */
  id = mln_model_undock (f.m, "c", 400, 300);
  g_assert_cmpuint (id, !=, 0);
  g_assert_cmpuint (only_float (&f), ==, id);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,3],\"kids\":[{\"tabs\":[\"a\"],\"active\":0},"
                   "{\"tabs\":[\"b\"],\"active\":0}]}");
  g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_FRONT);
  g_assert_cmpuint (mln_model_float_of (f.m, mln_model_leaf_with (f.m, "c")), ==, id);
  g_assert_cmpint (f.h.changed, ==, 1);

  /* It is not closed, and the window is kept, in an envelope. */
  {
    GPtrArray *closed = mln_model_closed (f.m);

    g_assert_cmpuint (closed->len, ==, 0);
    g_ptr_array_unref (closed);
  }

  g_assert_cmpstr (f.h.kept, ==, "{\"layout\":{\"dir\":\"row\",\"size\":[1,3],\"kids\":["
                   "{\"tabs\":[\"a\"],\"active\":0},{\"tabs\":[\"b\"],\"active\":0}]},"
                   "\"floating\":[{\"layout\":{\"tabs\":[\"c\"],\"active\":0},\"size\":[400,300]}]}");

  /* Undocking it again is that window. */
  g_assert_cmpuint (mln_model_undock (f.m, "c", 1, 1), ==, id);

  /* Closed, the window docks c where it was: behind b, as a tab. */
  mln_model_dock (f.m, id);
  g_assert_cmpuint (only_float (&f), ==, 0);
  assert_tree (&f, "{\"dir\":\"row\",\"size\":[1,3],\"kids\":[{\"tabs\":[\"a\"],\"active\":0},"
                   "{\"tabs\":[\"b\",\"c\"],\"active\":1}]}");
  g_assert_cmpstr (f.h.kept, ==, "{\"dir\":\"row\",\"size\":[1,3],\"kids\":[{\"tabs\":[\"a\"],"
                   "\"active\":0},{\"tabs\":[\"b\",\"c\"],\"active\":1}]}");
  teardown (&f);
}

static void
a_window_of_its_own_tree (void)
{
  Fixture f;
  guint id;
  MlnNode *leaf;

  setup (&f, "a b c", "{\"tabs\":[\"a\",\"b\",\"c\"]}", FALSE);
  id = mln_model_undock (f.m, "b", 0, 0);
  leaf = mln_model_leaf_with (f.m, "b");

  /* Dropped into the window's leaf and beside it: the window's tree. */
  mln_model_drop_into (f.m, "c", leaf);
  g_assert_true (mln_model_leaf_with (f.m, "c") == leaf);
  mln_model_drop_beside (f.m, "a", leaf, MLN_ROW, TRUE);

  {
    MlnNode *root = mln_model_float_root (f.m, id);

    g_assert_false (mln_node_is_leaf (root));
    g_assert_cmpuint (mln_node_n_kids (root), ==, 2);
  }

  /* Every pane is in the window, and the main tree has none in play: it
     is kept all the same, as an emptied one is. */
  assert_tree (&f, "{\"tabs\":[],\"active\":0}");

  /* Closing a pane out of the window closes it; the split in the window
     collapses as one in the main tree does. */
  mln_model_close (f.m, "a");
  g_assert_true (mln_node_is_leaf (mln_model_float_root (f.m, id)));
  g_assert_cmpint (mln_model_where (f.m, "a"), ==, MLN_WHERE_DRAWER);

  /* The last one out of the window: the window goes. */
  mln_model_close (f.m, "b");
  mln_model_close (f.m, "c");
  g_assert_cmpuint (only_float (&f), ==, 0);
  teardown (&f);
}

static void
dock_one_pane (void)
{
  Fixture f;
  guint id;

  setup (&f, "a b c",
         "{\"dir\":\"col\",\"size\":[1,1],\"kids\":[{\"tabs\":[\"a\"]},{\"tabs\":[\"b\"]}]}",
         FALSE);

  /* b goes, and its leaf with it; a pane moved about in the window does
     not forget where it was in the main tree. */
  id = mln_model_undock (f.m, "b", 0, 0);
  mln_model_drop_into (f.m, "c", mln_model_leaf_with (f.m, "b"));
  mln_model_drop_tab (f.m, "b", mln_model_leaf_with (f.m, "c"), NULL);
  assert_tree (&f, "{\"tabs\":[\"a\"],\"active\":0}");

  mln_model_dock_pane (f.m, "b");
  assert_tree (&f, "{\"dir\":\"col\",\"size\":[0.5,0.5],\"kids\":[{\"tabs\":[\"a\"],\"active\":0},"
                   "{\"tabs\":[\"b\"],\"active\":0}]}");
  g_assert_cmpuint (only_float (&f), ==, id);

  /* c never was in the main tree: docked, it goes where the focus is,
     which b's docking left on b's leaf. */
  mln_model_dock (f.m, id);
  g_assert_true (mln_model_leaf_with (f.m, "c") == mln_model_leaf_with (f.m, "b"));
  teardown (&f);
}

static void
floating_windows_are_kept (void)
{
  Fixture f;
  char *kept;

  setup (&f, "a b c", "{\"tabs\":[\"a\",\"b\",\"c\"]}", FALSE);
  g_assert_true (mln_model_set_version (f.m, "1"));
  mln_model_undock (f.m, "c", 500, 250);
  mln_model_set_float_size (f.m, only_float (&f), 510, 260);
  g_assert_cmpstr (f.h.kept, ==, "{\"version\":1,\"layout\":{\"tabs\":[\"a\",\"b\"],\"active\":0},"
                   "\"floating\":[{\"layout\":{\"tabs\":[\"c\"],\"active\":0},\"size\":[510,260]}]}");

  /* Read back: the window with it, under a new id. */
  kept = g_strdup (f.h.kept);
  g_assert_true (mln_model_load (f.m, kept));
  g_assert_cmpuint (only_float (&f), !=, 0);
  g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_FRONT);

  {
    int w = 0, h = 0;

    g_assert_true (mln_model_float_size (f.m, only_float (&f), &w, &h));
    g_assert_cmpint (w, ==, 510);
    g_assert_cmpint (h, ==, 260);
  }

  /* A window naming a pane the main tree has is kept without it, and one
     left with nothing is not kept; a size that is not two numbers is
     none. */
  g_assert_true (mln_model_load (f.m, "{\"version\":1,\"layout\":{\"tabs\":[\"a\",\"b\"]},"
                                      "\"floating\":[{\"layout\":{\"tabs\":[\"b\"]}},"
                                      "{\"layout\":{\"tabs\":[\"b\",\"c\"]},\"size\":\"big\"},7]}"));
  g_assert_true (mln_model_leaf_with (f.m, "b") == mln_model_leaf_with (f.m, "a"));
  g_assert_cmpuint (mln_model_float_of (f.m, mln_model_leaf_with (f.m, "c")), !=, 0);

  {
    int w = 1, h = 1;

    mln_model_float_size (f.m, only_float (&f), &w, &h);
    g_assert_cmpint (w, ==, 0);
    g_assert_cmpint (h, ==, 0);
  }

  /* Reset: no windows. And a layout the app puts up is the whole one. */
  mln_model_reset (f.m);
  g_assert_cmpuint (only_float (&f), ==, 0);
  mln_model_undock (f.m, "c", 0, 0);
  g_assert_true (mln_model_set_layout (f.m, "{\"tabs\":[\"a\"]}"));
  g_assert_cmpuint (only_float (&f), ==, 0);
  g_assert_cmpint (mln_model_where (f.m, "c"), ==, MLN_WHERE_DRAWER);

  g_free (kept);
  teardown (&f);

  /* With no version, the windows are kept in an envelope of their own. */
  setup (&f, "a b", NULL, FALSE);
  mln_model_undock (f.m, "b", 0, 0);
  g_assert_cmpstr (f.h.kept, ==, "{\"layout\":{\"tabs\":[\"a\"],\"active\":0},"
                   "\"floating\":[{\"layout\":{\"tabs\":[\"b\"],\"active\":0}}]}");
  kept = g_strdup (f.h.kept);
  g_assert_true (mln_model_load (f.m, kept));
  g_assert_cmpuint (only_float (&f), !=, 0);
  g_free (kept);
  teardown (&f);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/model/close-and-present-beside", close_and_present_beside);
  g_test_add_func ("/model/close-from-a-stack", close_from_a_stack);
  g_test_add_func ("/model/collapse-and-make-again", collapse_and_make_again);
  g_test_add_func ("/model/come-back-beside-what-is-left", come_back_beside_what_is_left);
  g_test_add_func ("/model/present-avoids-a-leaf", present_avoids_a_leaf);
  g_test_add_func ("/model/drops", drops);
  g_test_add_func ("/model/availability", availability_keeps_the_front_tab);
  g_test_add_func ("/model/modes", modes_have_layouts_of_their_own);
  g_test_add_func ("/model/added", added_panes);
  g_test_add_func ("/model/ephemeral-without-discard", ephemeral_without_discard_cannot_close);
  g_test_add_func ("/model/later", places_kept_for_panes_to_come);
  g_test_add_func ("/model/stray", stray_panes_are_put_somewhere);
  g_test_add_func ("/model/set-layout-and-reset", set_layout_and_reset);
  g_test_add_func ("/model/every-pane-closed", every_pane_closed);
  g_test_add_func ("/model/versions", versions);
  g_test_add_func ("/model/sizes", sizes);
  g_test_add_func ("/model/zoom", zoom);
  g_test_add_func ("/model/stale-focus", stale_focus);
  g_test_add_func ("/model/ids-from-the-tree", ids_from_the_tree);
  g_test_add_func ("/model/lenient-defaults", lenient_defaults);
  g_test_add_func ("/model/reopen-is-a-change", reopen_is_a_change);
  g_test_add_func ("/model/reopen-clears-was-front", reopen_clears_the_front_kept_for_a_pane_to_come);
  g_test_add_func ("/model/hook-reenters-add", hook_reenters_add);
  g_test_add_func ("/model/slots-are-kept", slots_are_kept);
  g_test_add_func ("/model/slots-move-with-the-room", slots_move_with_the_room);
  g_test_add_func ("/model/a-slot-before-the-focus", a_slot_before_the_focus);
  g_test_add_func ("/model/open-where-missing", open_where_missing);
  g_test_add_func ("/model/closed-with-a-version", closed_with_a_version);
  g_test_add_func ("/model/closed-panes-to-come", closed_panes_to_come);
  g_test_add_func ("/model/closed-only-by-a-person", closed_only_by_a_person);
  g_test_add_func ("/model/closed-read-back-and-modes", closed_read_back_and_modes);
  g_test_add_func ("/model/added-panes-are-not-listed", added_panes_are_not_listed);
  g_test_add_func ("/model/a-slot-comes-back-with-its-room", a_slot_comes_back_with_its_room);
  g_test_add_func ("/model/dropped-slots-go-as-emptied-ones-do", dropped_slots_go_as_emptied_ones_do);
  g_test_add_func ("/model/undock-and-dock", undock_and_dock);
  g_test_add_func ("/model/a-window-of-its-own-tree", a_window_of_its_own_tree);
  g_test_add_func ("/model/dock-one-pane", dock_one_pane);
  g_test_add_func ("/model/floating-windows-are-kept", floating_windows_are_kept);

  return g_test_run ();
}
