/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * The model, one function to one function with mullion's panes.js: the
 * names are its names, so that a change there can be found here. Where
 * this has to differ -- reference counts where JavaScript collects
 * garbage, strings where it has undefined -- it says so.
 */

#include "mln-model.h"
#include "mln-json.h"

#include <math.h>
#include <string.h>

struct _MlnNode
{
  gint ref;
  gboolean leaf;

  /* A leaf. */
  GPtrArray *tabs;              /* char*, owned */
  int active;

  /* A split. */
  MlnDir dir;
  GArray *size;                 /* double */
  GPtrArray *kids;              /* MlnNode*, a reference each */
};

typedef struct
{
  char *id;
  double min;
  gboolean off;                 /* its mode is not up */
  gboolean added;               /* by mln_model_add, once up */
  gboolean ephemeral;
  char *near;
} Pane;

/* Where a pane's leaf was, for a pane that was the last one in it. */
typedef struct
{
  MlnNode *next;                /* the node it sat beside, a reference */
  gboolean after;
  MlnDir dir;
  double share;
  gboolean has_via;             /* the split it was in collapsed ... */
  MlnDir via;                   /* ... and ran this way */
} Spot;

struct _MlnModel
{
  MlnModelHooks hooks;
  gpointer data;

  GPtrArray *panes;             /* Pane*, in the order taken on */
  GHashTable *by_id;            /* id -> Pane* */

  GHashTable *home;             /* id -> MlnNode*, a reference */
  GHashTable *spot;             /* id -> Spot* */
  GHashTable *dismissed;        /* id set */
  GHashTable *was_front;        /* id set */

  GHashTable *defaults;         /* mode -> MlnJson* */
  MlnJson *version;             /* NULL: none */

  MlnNode *tree;
  MlnNode *focus;
  MlnNode *zoom;
  char *where;
  char *last;                   /* the tree as last told of */

  double split;
  double least;
};

/* ---- nodes ---- */

static MlnNode *
new_leaf (void)
{
  MlnNode *n = g_new0 (MlnNode, 1);

  n->ref = 1;
  n->leaf = TRUE;
  n->tabs = g_ptr_array_new_with_free_func (g_free);

  return n;
}

static MlnNode *
new_split (MlnDir dir)
{
  MlnNode *n = g_new0 (MlnNode, 1);

  n->ref = 1;
  n->dir = dir;
  n->size = g_array_new (FALSE, FALSE, sizeof (double));
  n->kids = g_ptr_array_new_with_free_func ((GDestroyNotify) mln_node_unref);

  return n;
}

MlnNode *
mln_node_ref (MlnNode *n)
{
  if (n != NULL)
    n->ref++;

  return n;
}

void
mln_node_unref (MlnNode *n)
{
  if (n == NULL || --n->ref > 0)
    return;

  if (n->leaf)
    g_ptr_array_free (n->tabs, TRUE);
  else
    {
      g_array_free (n->size, TRUE);
      g_ptr_array_free (n->kids, TRUE);
    }

  g_free (n);
}

/* A node in a slot that held a reference: the new one referenced, the old
   one let go, in that order, since they can be the same. */
static void
set_node (MlnNode **slot, MlnNode *n)
{
  MlnNode *old = *slot;

  *slot = mln_node_ref (n);
  mln_node_unref (old);
}

static double
size_at (const MlnNode *n, guint i)
{
  return g_array_index (n->size, double, i);
}

static MlnNode *
kid_at (const MlnNode *n, guint i)
{
  return n->kids->pdata[i];
}

static int
kid_index (const MlnNode *up, const MlnNode *kid)
{
  for (guint i = 0; i < up->kids->len; i++)
    if (kid_at (up, i) == kid)
      return (int) i;

  return -1;
}

static int
tab_index (const MlnNode *leaf, const char *id)
{
  for (guint i = 0; i < leaf->tabs->len; i++)
    if (strcmp (leaf->tabs->pdata[i], id) == 0)
      return (int) i;

  return -1;
}

/* A child replaced where it is, keeping its share. */
static void
replace_kid (MlnNode *up, MlnNode *old, MlnNode *made)
{
  int i = kid_index (up, old);

  set_node ((MlnNode **) &up->kids->pdata[i], made);
}

static void
insert_kid (MlnNode *up, guint i, MlnNode *kid, double share)
{
  g_ptr_array_insert (up->kids, (int) i, mln_node_ref (kid));
  g_array_insert_val (up->size, i, share);
}

/* ---- panes ---- */

static void
pane_free (Pane *p)
{
  g_free (p->id);
  g_free (p->near);
  g_free (p);
}

static void
spot_free (Spot *s)
{
  mln_node_unref (s->next);
  g_free (s);
}

static Pane *
pane (MlnModel *m, const char *id)
{
  return id == NULL ? NULL : g_hash_table_lookup (m->by_id, id);
}

/* Whether a pane is in play at all: the page has it and the mode it
   belongs to is up. */
static gboolean
playable (MlnModel *m, const char *id)
{
  Pane *p = pane (m, id);

  return p != NULL && !p->off;
}

static gboolean
later (MlnModel *m, const char *id)
{
  return m->hooks.later != NULL && m->hooks.later (id, m->data);
}

/* ---- the layout ---- */

static GPtrArray *
live_tabs (MlnModel *m, MlnNode *leaf)
{
  GPtrArray *ids = g_ptr_array_new ();

  for (guint i = 0; i < leaf->tabs->len; i++)
    if (playable (m, leaf->tabs->pdata[i]))
      g_ptr_array_add (ids, leaf->tabs->pdata[i]);

  return ids;
}

static int
live_index (MlnModel *m, MlnNode *leaf, const char *id)
{
  int at = -1, i = 0;

  for (guint k = 0; k < leaf->tabs->len; k++)
    if (playable (m, leaf->tabs->pdata[k]))
      {
        if (strcmp (leaf->tabs->pdata[k], id) == 0)
          at = i;

        i++;
      }

  return at;
}

static guint
live_count (MlnModel *m, MlnNode *leaf)
{
  guint n = 0;

  for (guint i = 0; i < leaf->tabs->len; i++)
    if (playable (m, leaf->tabs->pdata[i]))
      n++;

  return n;
}

/* The i-th tab in play, or NULL. */
static const char *
live_tab (MlnModel *m, MlnNode *leaf, int i)
{
  int k = 0;

  if (i < 0)
    return NULL;

  for (guint j = 0; j < leaf->tabs->len; j++)
    if (playable (m, leaf->tabs->pdata[j]) && k++ == i)
      return leaf->tabs->pdata[j];

  return NULL;
}

static gboolean
alive (MlnModel *m, MlnNode *n)
{
  if (n->leaf)
    return live_count (m, n) > 0;

  for (guint i = 0; i < n->kids->len; i++)
    if (alive (m, kid_at (n, i)))
      return TRUE;

  return FALSE;
}

/* How narrow a node may be made: a pane's own minimum, a row's the sum
   of its children's with the dividers between them, a column's the
   widest of them. And the other way up, what a leaf asks for is the
   leaf floor. */
static double
min_across (MlnModel *m, MlnNode *n, gboolean row)
{
  if (n->leaf)
    {
      double most = -INFINITY;

      if (!row)
        return m->least;

      /* Math.max over nothing is -Infinity in mullion, and so here. */
      for (guint i = 0; i < n->tabs->len; i++)
        if (playable (m, n->tabs->pdata[i]))
          most = MAX (most, pane (m, n->tabs->pdata[i])->min);

      return most;
    }

  {
    double sum = 0, most = -INFINITY;
    guint live = 0;

    for (guint i = 0; i < n->kids->len; i++)
      if (alive (m, kid_at (n, i)))
        {
          double k = min_across (m, kid_at (n, i), row);

          sum += k;
          most = MAX (most, k);
          live++;
        }

    return ((n->dir == MLN_ROW) == row)
      ? sum + ((double) live - 1) * m->split
      : most;
  }
}

/* ---- JSON, both ways ---- */

static void
write_node (GString *out, const MlnNode *n)
{
  if (n->leaf)
    {
      g_string_append (out, "{\"tabs\":[");

      for (guint i = 0; i < n->tabs->len; i++)
        {
          if (i > 0)
            g_string_append_c (out, ',');

          mln_json_write_string (out, n->tabs->pdata[i]);
        }

      g_string_append (out, "],\"active\":");
      g_string_append_printf (out, "%d", n->active);
      g_string_append_c (out, '}');
      return;
    }

  g_string_append (out, n->dir == MLN_ROW ? "{\"dir\":\"row\",\"size\":["
                                          : "{\"dir\":\"col\",\"size\":[");

  for (guint i = 0; i < n->size->len; i++)
    {
      if (i > 0)
        g_string_append_c (out, ',');

      mln_json_write_number (out, size_at (n, i));
    }

  g_string_append (out, "],\"kids\":[");

  for (guint i = 0; i < n->kids->len; i++)
    {
      if (i > 0)
        g_string_append_c (out, ',');

      write_node (out, kid_at (n, i));
    }

  g_string_append (out, "]}");
}

static char *
snapshot (MlnModel *m)
{
  GString *out;

  if (m->tree == NULL)
    return g_strdup ("null");

  out = g_string_new (NULL);
  write_node (out, m->tree);

  return g_string_free (out, FALSE);
}

static gboolean
is_whole (double d)
{
  return isfinite (d) && floor (d) == d;
}

/* A saved layout, read back: anything that is not a tree of the shape
   this writes is not one. */
static gboolean
sane (const MlnJson *n)
{
  const MlnJson *tabs, *active, *dir, *kids, *size;

  if (n == NULL || mln_json_type (n) != MLN_JSON_OBJECT)
    return FALSE;

  tabs = mln_json_member (n, "tabs");

  if (tabs != NULL && mln_json_type (tabs) == MLN_JSON_ARRAY)
    {
      for (guint i = 0; i < mln_json_length (tabs); i++)
        if (mln_json_type (mln_json_index (tabs, i)) != MLN_JSON_STRING)
          return FALSE;

      active = mln_json_member (n, "active");

      return active == NULL ||
             (mln_json_type (active) == MLN_JSON_NUMBER &&
              is_whole (mln_json_number (active)) &&
              mln_json_number (active) >= 0);
    }

  dir = mln_json_member (n, "dir");
  kids = mln_json_member (n, "kids");
  size = mln_json_member (n, "size");

  if (dir == NULL || mln_json_string (dir) == NULL ||
      (strcmp (mln_json_string (dir), "row") != 0 &&
       strcmp (mln_json_string (dir), "col") != 0))
    return FALSE;

  if (kids == NULL || mln_json_type (kids) != MLN_JSON_ARRAY ||
      mln_json_length (kids) <= 1)
    return FALSE;

  if (size == NULL || mln_json_type (size) != MLN_JSON_ARRAY ||
      mln_json_length (size) != mln_json_length (kids))
    return FALSE;

  for (guint i = 0; i < mln_json_length (size); i++)
    {
      const MlnJson *f = mln_json_index (size, i);

      /* Finite, as in mullion: 1e999 reads as Infinity, and an Infinity
         is written back as null, which does not read back. */
      if (mln_json_type (f) != MLN_JSON_NUMBER || !isfinite (mln_json_number (f)) ||
          !(mln_json_number (f) > 0))
        return FALSE;
    }

  for (guint i = 0; i < mln_json_length (kids); i++)
    if (!sane (mln_json_index (kids, i)))
      return FALSE;

  return TRUE;
}

/* A sane tree with the panes this page has never heard of dropped, and
   a pane named twice kept where it is named first. NULL for nothing
   left. */
static MlnNode *
known (MlnModel *m, const MlnJson *n, GHashTable *taken)
{
  const MlnJson *tabs;

  /* A sane tree is what this is written for; a default the app wrote is
     read through it too (docs/layout.md), and has been through nothing,
     so whatever is not the shape of a node here is nothing. */
  if (n == NULL || mln_json_type (n) != MLN_JSON_OBJECT)
    return NULL;

  tabs = mln_json_member (n, "tabs");

  if (tabs != NULL && mln_json_type (tabs) == MLN_JSON_ARRAY)
    {
      MlnNode *leaf = new_leaf ();
      const MlnJson *active = mln_json_member (n, "active");
      double want = active != NULL && mln_json_type (active) == MLN_JSON_NUMBER &&
                    mln_json_number (active) > 0 ? mln_json_number (active) : 0;

      for (guint i = 0; i < mln_json_length (tabs); i++)
        {
          const char *id = mln_json_string (mln_json_index (tabs, i));

          if (id != NULL && (pane (m, id) != NULL || later (m, id)) &&
              !g_hash_table_contains (taken, id))
            {
              g_hash_table_add (taken, g_strdup (id));
              g_ptr_array_add (leaf->tabs, g_strdup (id));
            }
        }

      if (leaf->tabs->len == 0)
        {
          mln_node_unref (leaf);
          return NULL;
        }

      leaf->active = want >= (double) leaf->tabs->len - 1
        ? (int) leaf->tabs->len - 1 : (int) want;

      return leaf;
    }

  {
    const MlnJson *kids = mln_json_member (n, "kids");
    const MlnJson *size = mln_json_member (n, "size");
    const char *dir = kids != NULL ? mln_json_string (mln_json_member (n, "dir")) : NULL;
    MlnNode *split, *only;

    if (dir == NULL || (strcmp (dir, "row") != 0 && strcmp (dir, "col") != 0) ||
        mln_json_type (kids) != MLN_JSON_ARRAY)
      return NULL;

    split = new_split (strcmp (dir, "row") == 0 ? MLN_ROW : MLN_COL);

    for (guint i = 0; i < mln_json_length (kids); i++)
      {
        MlnNode *kept = known (m, mln_json_index (kids, i), taken);

        if (kept != NULL)
          {
            /* A share that is not a number is an even one. */
            const MlnJson *f = size != NULL ? mln_json_index (size, i) : NULL;
            double share = f != NULL && mln_json_type (f) == MLN_JSON_NUMBER &&
                           isfinite (mln_json_number (f)) ? mln_json_number (f) : 1;

            g_ptr_array_add (split->kids, kept);
            g_array_append_val (split->size, share);
          }
      }

    if (split->kids->len > 1)
      return split;

    only = split->kids->len == 1 ? mln_node_ref (kid_at (split, 0)) : NULL;
    mln_node_unref (split);

    return only;
  }
}

static MlnNode *
known_of (MlnModel *m, const MlnJson *n)
{
  GHashTable *taken = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  MlnNode *made = known (m, n, taken);

  g_hash_table_unref (taken);

  return made;
}

/* Whether a tree names any pane this page has now, whatever mode it is
   in: one that holds only places for panes to come is no layout. */
static gboolean
holds_any (MlnModel *m, MlnNode *n)
{
  if (n->leaf)
    {
      for (guint i = 0; i < n->tabs->len; i++)
        if (pane (m, n->tabs->pdata[i]) != NULL)
          return TRUE;

      return FALSE;
    }

  for (guint i = 0; i < n->kids->len; i++)
    if (holds_any (m, kid_at (n, i)))
      return TRUE;

  return FALSE;
}

/* A version compares as JavaScript's === does: by value for a number, a
   string, a boolean or null, and never for an object or an array. */
static gboolean
same_version (const MlnJson *a, const MlnJson *b)
{
  if (a == NULL || b == NULL)
    return FALSE;

  if (mln_json_type (a) == MLN_JSON_ARRAY || mln_json_type (a) == MLN_JSON_OBJECT)
    return FALSE;

  return mln_json_equal (a, b);
}

/* A saved layout, as the page's own tree: or NULL, for one that is not
   there, does not parse, was kept under another version, or keeps
   nothing this page has. */
static MlnNode *
read_kept (MlnModel *m, const char *text)
{
  MlnJson *got = mln_json_parse (text);
  const MlnJson *saved = got;
  MlnNode *tree = NULL;

  if (got == NULL)
    return NULL;

  if (m->version != NULL)
    {
      const MlnJson *v = mln_json_type (got) == MLN_JSON_OBJECT
        ? mln_json_member (got, "version") : NULL;

      saved = same_version (v, m->version) ? mln_json_member (got, "layout") : NULL;
    }

  if (saved != NULL && sane (saved))
    tree = known_of (m, saved);

  if (tree != NULL && !holds_any (m, tree))
    {
      mln_node_unref (tree);
      tree = NULL;
    }

  mln_json_free (got);

  return tree;
}

/* The page's own layout for the mode that is up, or every pane in one
   leaf. */
static MlnNode *
fresh (MlnModel *m)
{
  const MlnJson *def = m->where == NULL ? NULL
    : g_hash_table_lookup (m->defaults, m->where);
  MlnNode *made = NULL;

  if (def != NULL)
    made = known_of (m, def);
  else
    {
      GString *all = g_string_new ("{\"tabs\":[");
      MlnJson *j;

      for (guint i = 0; i < m->panes->len; i++)
        {
          if (i > 0)
            g_string_append_c (all, ',');

          mln_json_write_string (all, ((Pane *) m->panes->pdata[i])->id);
        }

      g_string_append (all, "]}");
      j = mln_json_parse (all->str);
      made = known_of (m, j);
      mln_json_free (j);
      g_string_free (all, TRUE);
    }

  if (made == NULL)
    {
      made = new_leaf ();

      for (guint i = 0; i < m->panes->len; i++)
        g_ptr_array_add (made->tabs, g_strdup (((Pane *) m->panes->pdata[i])->id));
    }

  return made;
}

/* What is kept: the tree trimmed of places for panes that `later' no
   longer promises, in the version's envelope where there is one. */
static char *
kept (MlnModel *m)
{
  char *now = snapshot (m);
  MlnJson *j = mln_json_parse (now);
  MlnNode *trimmed = j != NULL ? known_of (m, j) : NULL;
  GString *out = g_string_new (NULL);

  if (m->version != NULL)
    {
      g_string_append (out, "{\"version\":");
      mln_json_write (out, m->version);
      g_string_append (out, ",\"layout\":");
    }

  /* mullion writes an emptied tree as `{ tabs: [] }', with no front tab,
     which reads back as no layout. */
  if (trimmed != NULL)
    write_node (out, trimmed);
  else
    g_string_append (out, "{\"tabs\":[]}");

  if (m->version != NULL)
    g_string_append_c (out, '}');

  mln_node_unref (trimmed);
  mln_json_free (j);
  g_free (now);

  return g_string_free (out, FALSE);
}

static void
save (MlnModel *m)
{
  char *text;

  if (m->tree == NULL || m->hooks.store == NULL)
    return;

  text = kept (m);
  m->hooks.store (m->where, text, m->data);
  g_free (text);
}

/* Kept and told of, if it is anything new. TRUE if it was. */
static gboolean
told (MlnModel *m)
{
  char *now = snapshot (m);

  if (m->last != NULL && strcmp (now, m->last) == 0)
    {
      g_free (now);
      return FALSE;
    }

  g_free (m->last);
  m->last = now;
  save (m);

  if (m->hooks.changed != NULL)
    m->hooks.changed (m->where, m->data);

  return TRUE;
}

/* Somebody changed the layout. A change that changed nothing is none. */
static void
changed (MlnModel *m)
{
  char *now = snapshot (m);
  gboolean same = m->last != NULL && strcmp (now, m->last) == 0;

  g_free (now);

  if (same)
    return;

  g_hash_table_remove_all (m->was_front);
  told (m);
}

/* ---- moving a pane about ---- */

static MlnNode *
leaf_with (MlnNode *n, const char *id)
{
  if (n == NULL)
    return NULL;

  if (n->leaf)
    return tab_index (n, id) >= 0 ? n : NULL;

  for (guint i = 0; i < n->kids->len; i++)
    {
      MlnNode *f = leaf_with (kid_at (n, i), id);

      if (f != NULL)
        return f;
    }

  return NULL;
}

static MlnNode *
parent_of (MlnNode *n, MlnNode *target)
{
  if (n == NULL || n->leaf)
    return NULL;

  if (kid_index (n, target) >= 0)
    return n;

  for (guint i = 0; i < n->kids->len; i++)
    {
      MlnNode *f = parent_of (kid_at (n, i), target);

      if (f != NULL)
        return f;
    }

  return NULL;
}

static MlnNode *
first_leaf (MlnNode *n)
{
  return n->leaf ? n : first_leaf (kid_at (n, 0));
}

static gboolean
holds (MlnNode *n, MlnNode *target)
{
  if (n == NULL || target == NULL)
    return FALSE;

  if (n == target)
    return TRUE;

  if (!n->leaf)
    for (guint i = 0; i < n->kids->len; i++)
      if (holds (kid_at (n, i), target))
        return TRUE;

  return FALSE;
}

static void
set_tree (MlnModel *m, MlnNode *n)
{
  set_node (&m->tree, n);
}

/* A leaf with nothing left in it, taken out of the tree, and the split
   above it collapsed if that leaves it with one child. */
static void
empty (MlnModel *m, MlnNode *leaf)
{
  MlnNode *up = parent_of (m->tree, leaf);
  MlnNode *only, *over;
  GHashTableIter it;
  Spot *s;
  int i;

  if (up == NULL)
    {
      MlnNode *blank = new_leaf ();

      set_tree (m, blank);
      mln_node_unref (blank);
      return;
    }

  i = kid_index (up, leaf);
  g_ptr_array_remove_index (up->kids, i);
  g_array_remove_index (up->size, i);

  if (up->kids->len > 1)
    return;

  /* Held across the swap: `up' holds it, and `up' may go. */
  only = mln_node_ref (kid_at (up, 0));
  mln_node_ref (up);
  over = parent_of (m->tree, up);

  if (over == NULL)
    set_tree (m, only);
  else
    replace_kid (over, up, only);

  /* A pane closed beside this split, and waiting to come back beside it,
     comes back beside what is left of it. */
  g_hash_table_iter_init (&it, m->spot);

  while (g_hash_table_iter_next (&it, NULL, (gpointer *) &s))
    if (s->next == up)
      {
        set_node (&s->next, only);
        s->has_via = TRUE;
        s->via = up->dir;
      }

  mln_node_unref (up);
  mln_node_unref (only);
}

/* Out of the layout: into the drawer. */
static void
drawer (MlnModel *m, const char *id)
{
  MlnNode *leaf = leaf_with (m->tree, id);
  MlnNode *up;
  int at, was, left;
  char *key;

  if (leaf == NULL)
    return;

  /* What was in front stays in front, counted over the tabs in play. */
  at = live_index (m, leaf, id);
  was = leaf->active;

  key = g_strdup (id);
  g_hash_table_replace (m->home, key, mln_node_ref (leaf));
  g_ptr_array_remove_index (leaf->tabs, tab_index (leaf, id));

  left = (int) live_count (m, leaf) - 1;
  leaf->active = MAX (0, MIN (at != -1 && at < was ? was - 1 : was, left));

  up = leaf->tabs->len == 0 ? parent_of (m->tree, leaf) : NULL;

  if (up == NULL)
    g_hash_table_remove (m->spot, id);
  else
    {
      Spot *s = g_new0 (Spot, 1);
      int i = kid_index (up, leaf);
      double total = 0;

      for (guint k = 0; k < up->size->len; k++)
        total += size_at (up, k);

      s->next = mln_node_ref (kid_at (up, i > 0 ? i - 1 : i + 1));
      s->after = i > 0;
      s->dir = up->dir;
      s->share = size_at (up, i) / total;
      g_hash_table_replace (m->spot, g_strdup (id), s);
    }

  if (leaf->tabs->len == 0)
    empty (m, leaf);
}

gboolean
mln_model_closable (MlnModel *m, const char *id)
{
  Pane *p = pane (m, id);

  return p != NULL && (!p->ephemeral || m->hooks.discard != NULL);
}

static void
put_away (MlnModel *m, const char *id)
{
  drawer (m, id);
  g_hash_table_add (m->dismissed, g_strdup (id));
}

/* What a person's close does to a pane, by its kind. TRUE where a close
   happened here and now. */
static gboolean
dismiss (MlnModel *m, const char *id)
{
  Pane *p = pane (m, id);

  if (p == NULL)
    return FALSE;

  if (!p->ephemeral)
    {
      put_away (m, id);
      return TRUE;
    }

  if (mln_model_closable (m, id))
    m->hooks.discard (id, m->data);

  return FALSE;
}

/* Where a tab goes in a strip: mullion's `before', which is undefined
   (nowhere in particular), null (the end), or a pane. */
typedef enum { ANYWHERE, AT_END, BEFORE } Place;

/* Into a leaf, as the tab in front of it. */
static void
into (MlnModel *m, const char *id, MlnNode *leaf, Place place, const char *before)
{
  char *mine = g_strdup (id);   /* `id' may be a tab this frees */
  int at;

  if (leaf_with (m->tree, mine) == leaf &&
      (place == ANYWHERE || live_count (m, leaf) == 1))
    {
      leaf->active = live_index (m, leaf, mine);
      g_free (mine);
      return;
    }

  drawer (m, mine);
  g_hash_table_remove (m->dismissed, mine);

  at = place == BEFORE && before != NULL ? tab_index (leaf, before) : -1;
  g_ptr_array_insert (leaf->tabs, at == -1 ? -1 : at, mine);

  leaf->active = live_index (m, leaf, mine);
}

/* And beside one, which is what splitting is. */
static void
beside (MlnModel *m, const char *id, MlnNode *leaf, MlnDir dir, gboolean after)
{
  MlnNode *from = leaf_with (m->tree, id);
  MlnNode *made, *pair, *up;
  char *mine;

  if (from == leaf && live_count (m, leaf) == 1)
    return;

  mine = g_strdup (id);
  drawer (m, mine);
  g_hash_table_remove (m->dismissed, mine);

  made = new_leaf ();
  g_ptr_array_add (made->tabs, mine);

  pair = new_split (dir);
  insert_kid (pair, 0, after ? leaf : made, 0.5);
  insert_kid (pair, 1, after ? made : leaf, 0.5);

  up = parent_of (m->tree, leaf);

  if (up == NULL)
    set_tree (m, pair);
  else
    replace_kid (up, leaf, pair);

  mln_node_unref (made);
  mln_node_unref (pair);
}

/*
 * Back out of the drawer, to where it was: the leaf it was last in, if
 * the tree still has it; or a leaf of its own beside the neighbor it
 * had, on the same side and with the same share. Where neither is left,
 * into `fallback'. `avoid' is a leaf it must not be put in front of.
 */
static MlnNode *
reopen (MlnModel *m, const char *id, MlnNode *fallback, MlnNode *avoid)
{
  MlnNode *back = g_hash_table_lookup (m->home, id);
  Spot *was;
  MlnNode *made, *up;

  if (back != NULL && back != avoid && holds (m->tree, back))
    {
      into (m, id, back, ANYWHERE, NULL);
      return back;
    }

  was = g_hash_table_lookup (m->spot, id);

  if (was == NULL || !holds (m->tree, was->next))
    {
      into (m, id, fallback, ANYWHERE, NULL);
      return fallback;
    }

  made = back != NULL && back->tabs->len == 0 ? mln_node_ref (back) : new_leaf ();
  up = parent_of (m->tree, was->next);

  if (up != NULL && up->dir == was->dir)
    {
      double total = 0;
      guint i = (guint) kid_index (up, was->next) + (was->after ? 1 : 0);

      for (guint k = 0; k < up->size->len; k++)
        total += size_at (up, k);

      for (guint k = 0; k < up->size->len; k++)
        g_array_index (up->size, double, k) =
          size_at (up, k) / total * (1 - was->share);

      insert_kid (up, i, made, was->share);
    }
  else
    {
      MlnNode *pair = new_split (was->dir);
      MlnNode *next = mln_node_ref (was->next);
      GHashTableIter it;
      Spot *s;

      insert_kid (pair, 0, was->after ? next : made,
                  was->after ? 1 - was->share : was->share);
      insert_kid (pair, 1, was->after ? made : next,
                  was->after ? was->share : 1 - was->share);

      if (up == NULL)
        set_tree (m, pair);
      else
        replace_kid (up, next, pair);

      /* The split this was closed out of, made again: a pane closed
         beside it before it collapsed goes beside it again. */
      g_hash_table_iter_init (&it, m->spot);

      while (g_hash_table_iter_next (&it, NULL, (gpointer *) &s))
        if (s->next == next && s->has_via && s->via == was->dir)
          {
            set_node (&s->next, pair);
            s->has_via = FALSE;
          }

      mln_node_unref (next);
      mln_node_unref (pair);
    }

  into (m, id, made, ANYWHERE, NULL);
  mln_node_unref (made);

  return made;
}

/* Where a pane the page added goes when the layout has no place for it:
   the leaf of the pane it was added beside, or the one last used, or the
   first there is. */
static MlnNode *
target (MlnModel *m, Pane *p)
{
  MlnNode *by = p->near == NULL ? NULL : leaf_with (m->tree, p->near);

  if (by != NULL && playable (m, p->near))
    return by;

  if (m->focus != NULL && holds (m->tree, m->focus))
    return m->focus;

  return first_leaf (m->tree);
}

/* The panes the page added that a layout just put up does not have, and
   that nobody put away, each put where `target' says. */
static gboolean
stray (MlnModel *m)
{
  gboolean any = FALSE;

  for (guint i = 0; i < m->panes->len; i++)
    {
      Pane *p = m->panes->pdata[i];

      if (p->added && !g_hash_table_contains (m->dismissed, p->id) &&
          !p->off && leaf_with (m->tree, p->id) == NULL)
        {
          into (m, p->id, target (m, p), ANYWHERE, NULL);
          any = TRUE;
        }
    }

  return any;
}

static void
unzoom_for (MlnModel *m, MlnNode *leaf)
{
  if (m->zoom != NULL && m->zoom != leaf)
    set_node (&m->zoom, NULL);
}

/* ---- the model ---- */

MlnModel *
mln_model_new (const MlnModelHooks *hooks, gpointer data)
{
  MlnModel *m = g_new0 (MlnModel, 1);

  if (hooks != NULL)
    m->hooks = *hooks;

  m->data = data;
  m->panes = g_ptr_array_new_with_free_func ((GDestroyNotify) pane_free);
  m->by_id = g_hash_table_new (g_str_hash, g_str_equal);
  m->home = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) mln_node_unref);
  m->spot = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                   (GDestroyNotify) spot_free);
  m->dismissed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  m->was_front = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  m->defaults = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
                                       (GDestroyNotify) mln_json_free);
  m->split = 6;
  m->least = 64;

  return m;
}

void
mln_model_free (MlnModel *m)
{
  if (m == NULL)
    return;

  mln_node_unref (m->tree);
  mln_node_unref (m->focus);
  mln_node_unref (m->zoom);
  g_hash_table_unref (m->home);
  g_hash_table_unref (m->spot);
  g_hash_table_unref (m->dismissed);
  g_hash_table_unref (m->was_front);
  g_hash_table_unref (m->defaults);
  g_hash_table_unref (m->by_id);
  g_ptr_array_free (m->panes, TRUE);
  mln_json_free (m->version);
  g_free (m->where);
  g_free (m->last);
  g_free (m);
}

void
mln_model_set_split (MlnModel *m, double px)
{
  m->split = px;
}

void
mln_model_set_leaf (MlnModel *m, double px)
{
  m->least = px;
}

double
mln_model_get_split (MlnModel *m)
{
  return m->split;
}

gboolean
mln_model_set_version (MlnModel *m, const char *json)
{
  MlnJson *v = NULL;

  if (json != NULL && (v = mln_json_parse (json)) == NULL)
    return FALSE;

  mln_json_free (m->version);
  m->version = v;

  return TRUE;
}

gboolean
mln_model_set_default (MlnModel *m, const char *mode, const char *json)
{
  MlnJson *j;

  if (json == NULL)
    {
      g_hash_table_remove (m->defaults, mode);
      return TRUE;
    }

  /* Read as mullion reads a default: through `known' and nothing else,
     so a split of one is that one and a share of nothing is kept. What is
     refused is text that is not a JSON object at all. */
  j = mln_json_parse (json);

  if (j == NULL || mln_json_type (j) != MLN_JSON_OBJECT)
    {
      mln_json_free (j);
      return FALSE;
    }

  g_hash_table_replace (m->defaults, g_strdup (mode), j);

  return TRUE;
}

void
mln_model_set_mode (MlnModel *m, const char *mode)
{
  if (g_strcmp0 (mode, m->where) == 0 && m->tree != NULL)
    return;

  g_free (m->where);
  m->where = g_strdup (mode);
  set_node (&m->tree, NULL);
  set_node (&m->zoom, NULL);
  set_node (&m->focus, NULL);
}

const char *
mln_model_get_mode (MlnModel *m)
{
  return m->where;
}

/* ---- panes ---- */

static Pane *
enlist (MlnModel *m, const char *id, double min)
{
  Pane *p = g_new0 (Pane, 1);

  p->id = g_strdup (id);
  p->min = min >= 0 ? min : 240;
  g_ptr_array_add (m->panes, p);
  g_hash_table_insert (m->by_id, p->id, p);

  return p;
}

gboolean
mln_model_register (MlnModel *m, const char *id, double min)
{
  if (id == NULL || pane (m, id) != NULL)
    return FALSE;

  enlist (m, id, min);

  return TRUE;
}

MlnNode *
mln_model_add (MlnModel *m, const char *id, double min, gboolean keep,
               const char *near, gboolean take, gboolean *added)
{
  MlnNode *dormant, *leaf;
  const char *front = NULL;
  char *front_copy = NULL;
  Pane *p;

  if (added != NULL)
    *added = FALSE;

  if (id == NULL || pane (m, id) != NULL)
    return NULL;

  /* A place kept for it, and what is in front there now. */
  dormant = leaf_with (m->tree, id);

  if (dormant != NULL)
    {
      front = g_hash_table_contains (m->was_front, id)
        ? id : live_tab (m, dormant, dormant->active);
      front_copy = g_strdup (front);
    }

  g_hash_table_remove (m->was_front, id);

  p = enlist (m, id, min);
  p->added = TRUE;
  p->near = g_strdup (near);
  p->ephemeral = !keep;

  if (added != NULL)
    *added = TRUE;

  if (m->tree == NULL || !playable (m, id))
    {
      g_free (front_copy);
      return NULL;
    }

  leaf = leaf_with (m->tree, id);

  if (leaf == NULL)
    {
      leaf = target (m, p);
      into (m, id, leaf, ANYWHERE, NULL);
    }
  else
    leaf->active = live_index (m, leaf,
                               take || front_copy == NULL ? id : front_copy);

  g_free (front_copy);

  /* The layout's own state first, and then told: what the app does when
     it is told can change the tree, and this leaf with it. */
  set_node (&m->focus, leaf);
  unzoom_for (m, leaf);
  told (m);

  return m->focus;
}

gboolean
mln_model_remove (MlnModel *m, const char *id)
{
  Pane *p = pane (m, id);
  MlnNode *leaf;
  gboolean held, coming;
  char *front = NULL;
  char *mine;

  if (p == NULL)
    return FALSE;

  mine = g_strdup (id);
  leaf = leaf_with (m->tree, mine);
  held = leaf != NULL;
  coming = held && later (m, mine);

  if (coming)
    front = g_strdup (live_tab (m, leaf, leaf->active));

  if (held && !coming)
    drawer (m, mine);

  g_hash_table_remove (m->home, mine);
  g_hash_table_remove (m->spot, mine);
  g_hash_table_remove (m->dismissed, mine);
  g_hash_table_remove (m->by_id, mine);
  g_ptr_array_remove (m->panes, p);

  if (coming && front != NULL && strcmp (front, mine) != 0)
    leaf->active = live_index (m, leaf, front);

  if (coming && front != NULL && strcmp (front, mine) == 0)
    g_hash_table_add (m->was_front, g_strdup (mine));
  else
    g_hash_table_remove (m->was_front, mine);

  if (held)
    told (m);

  g_free (front);
  g_free (mine);

  return TRUE;
}

gboolean
mln_model_has (MlnModel *m, const char *id)
{
  return pane (m, id) != NULL;
}

gboolean
mln_model_ephemeral (MlnModel *m, const char *id)
{
  Pane *p = pane (m, id);

  return p != NULL && p->ephemeral;
}

double
mln_model_min (MlnModel *m, const char *id)
{
  Pane *p = pane (m, id);

  return p != NULL ? p->min : 0;
}

void
mln_model_set_available (MlnModel *m, const char *id, gboolean ok)
{
  Pane *p = pane (m, id);
  MlnNode *leaf;
  char *front = NULL;

  if (p == NULL || p->off == !ok)
    return;

  /* What is in front of its leaf stays in front. */
  leaf = leaf_with (m->tree, id);

  if (leaf != NULL)
    front = g_strdup (live_tab (m, leaf, leaf->active));

  p->off = !ok;

  if (front != NULL && strcmp (front, id) != 0)
    leaf->active = live_index (m, leaf, front);

  g_free (front);

  /* A pane the page added while its mode was down had nowhere to go then,
     and has now. */
  if (ok && m->tree != NULL && stray (m))
    told (m);
}

gboolean
mln_model_playable (MlnModel *m, const char *id)
{
  return playable (m, id);
}

GPtrArray *
mln_model_panes (MlnModel *m)
{
  GPtrArray *ids = g_ptr_array_new ();

  for (guint i = 0; i < m->panes->len; i++)
    g_ptr_array_add (ids, ((Pane *) m->panes->pdata[i])->id);

  return ids;
}

MlnWhere
mln_model_where (MlnModel *m, const char *id)
{
  Pane *p = pane (m, id);
  MlnNode *leaf;

  if (p == NULL)
    return MLN_WHERE_NONE;

  if (p->off)
    return MLN_WHERE_OFF;

  if (m->tree == NULL)
    return MLN_WHERE_NONE;

  leaf = leaf_with (m->tree, id);

  if (leaf == NULL)
    return MLN_WHERE_DRAWER;

  return g_strcmp0 (live_tab (m, leaf, leaf->active), id) == 0
    ? MLN_WHERE_FRONT : MLN_WHERE_BEHIND;
}

GPtrArray *
mln_model_closed (MlnModel *m)
{
  GPtrArray *ids = g_ptr_array_new ();

  for (guint i = 0; i < m->panes->len; i++)
    {
      Pane *p = m->panes->pdata[i];

      if (!p->off && !p->ephemeral && leaf_with (m->tree, p->id) == NULL)
        g_ptr_array_add (ids, p->id);
    }

  return ids;
}

/* ---- the layout ---- */

gboolean
mln_model_load (MlnModel *m, const char *text)
{
  MlnNode *saved = text != NULL ? read_kept (m, text) : NULL;
  gboolean used = saved != NULL;

  if (saved == NULL)
    saved = fresh (m);

  set_tree (m, saved);
  mln_node_unref (saved);
  set_node (&m->zoom, NULL);

  g_free (m->last);
  m->last = snapshot (m);
  stray (m);
  told (m);

  return used;
}

gboolean
mln_model_loaded (MlnModel *m)
{
  return m->tree != NULL;
}

char *
mln_model_save (MlnModel *m)
{
  return m->tree == NULL ? NULL : kept (m);
}

char *
mln_model_json (MlnModel *m)
{
  return m->tree == NULL ? NULL : snapshot (m);
}

gboolean
mln_model_set_layout (MlnModel *m, const char *json)
{
  MlnJson *j = json != NULL ? mln_json_parse (json) : NULL;
  MlnNode *made = sane (j) ? known_of (m, j) : NULL;

  mln_json_free (j);

  /* One that names only panes still to come is no layout yet. */
  if (made == NULL || !holds_any (m, made))
    {
      mln_node_unref (made);
      return FALSE;
    }

  set_node (&m->zoom, NULL);
  set_node (&m->focus, NULL);
  set_tree (m, made);
  mln_node_unref (made);
  stray (m);
  changed (m);

  return TRUE;
}

void
mln_model_reset (MlnModel *m)
{
  MlnNode *made;

  /* Nothing to forget is the same as forgetting; and not written over
     with the default, which may change. */
  if (m->hooks.store != NULL)
    m->hooks.store (m->where, NULL, m->data);

  set_node (&m->zoom, NULL);
  made = fresh (m);
  set_tree (m, made);
  mln_node_unref (made);
  stray (m);
  set_node (&m->focus, NULL);
  mln_model_settle (m);

  g_free (m->last);
  m->last = snapshot (m);

  if (m->hooks.changed != NULL)
    m->hooks.changed (m->where, m->data);
}

static void
settle_node (MlnModel *m, MlnNode *n)
{
  if (n->leaf)
    {
      int live = (int) live_count (m, n);

      if (live > 0)
        n->active = MIN (MAX (n->active, 0), live - 1);

      return;
    }

  for (guint i = 0; i < n->kids->len; i++)
    if (alive (m, kid_at (n, i)))
      settle_node (m, kid_at (n, i));
}

void
mln_model_settle (MlnModel *m)
{
  if (m->tree == NULL)
    return;

  if (m->zoom != NULL && (!holds (m->tree, m->zoom) || !alive (m, m->zoom)))
    set_node (&m->zoom, NULL);

  /* A leaf that went away takes the focus with it: a split that collapsed
     is not a place to put the next pane into. */
  if (m->focus != NULL && (!holds (m->tree, m->focus) || !alive (m, m->focus)))
    set_node (&m->focus, NULL);

  if (alive (m, m->tree))
    settle_node (m, m->tree);
}

/* ---- moving panes, as a person does ---- */

/* One change: kept and told of if it made the tree anything else. */
static void
after_move (MlnModel *m, char *was)
{
  char *now = snapshot (m);

  if (strcmp (now, was) != 0)
    changed (m);

  g_free (now);
  g_free (was);
}

void
mln_model_drop_tab (MlnModel *m, const char *id, MlnNode *leaf, const char *before)
{
  char *was;

  if (!playable (m, id) || !holds (m->tree, leaf))
    return;

  {
    char *mine = g_strdup (id);
    char *ahead = g_strdup (before);

    was = snapshot (m);
    into (m, mine, leaf, ahead == NULL ? AT_END : BEFORE, ahead);
    after_move (m, was);
    g_free (ahead);
    g_free (mine);
  }
}

void
mln_model_drop_into (MlnModel *m, const char *id, MlnNode *leaf)
{
  char *was;

  if (!playable (m, id) || !holds (m->tree, leaf))
    return;

  {
    char *mine = g_strdup (id);

    was = snapshot (m);
    into (m, mine, leaf, ANYWHERE, NULL);
    after_move (m, was);
    g_free (mine);
  }
}

void
mln_model_drop_beside (MlnModel *m, const char *id, MlnNode *leaf,
                       MlnDir dir, gboolean after)
{
  char *was;

  if (!playable (m, id) || !holds (m->tree, leaf))
    return;

  {
    char *mine = g_strdup (id);

    was = snapshot (m);
    beside (m, mine, leaf, dir, after);
    after_move (m, was);
    g_free (mine);
  }
}

/* The public entry points take an id that may be a leaf's own copy of
   it -- what mln_model_live_tabs hands out -- and moving the pane frees
   that copy. So each works on its own. */

gboolean
mln_model_close (MlnModel *m, const char *id)
{
  char *mine = g_strdup (id);
  gboolean closed = m->tree != NULL && dismiss (m, mine);

  if (closed)
    changed (m);

  g_free (mine);

  return closed;
}

void
mln_model_raise (MlnModel *m, MlnNode *leaf, guint i)
{
  if (leaf->active == (int) i)
    return;

  leaf->active = (int) i;
  changed (m);
}

/* A person's reopen from the drawer (mullion's drawer button): where it
   was, or into the leaf last focused, or the first; a person's change, so
   `changed' and not `told', and the focus left where it was. */
MlnNode *
mln_model_reopen (MlnModel *m, const char *id)
{
  char *mine = g_strdup (id);
  MlnNode *to;

  if (m->tree == NULL || !playable (m, mine) || leaf_with (m->tree, mine) != NULL)
    {
      g_free (mine);
      return NULL;
    }

  to = reopen (m, mine, m->focus != NULL && holds (m->tree, m->focus)
                          ? m->focus : first_leaf (m->tree), NULL);
  unzoom_for (m, to);
  changed (m);
  g_free (mine);

  return holds (m->tree, to) ? to : NULL;
}

static MlnNode *present (MlnModel *m, const char *id, MlnNode *fallback, MlnNode *avoid);

MlnNode *
mln_model_present (MlnModel *m, const char *id, MlnNode *fallback, MlnNode *avoid)
{
  char *mine = g_strdup (id);
  MlnNode *leaf = present (m, mine, fallback, avoid);

  g_free (mine);

  return leaf;
}

static MlnNode *
present (MlnModel *m, const char *id, MlnNode *fallback, MlnNode *avoid)
{
  MlnNode *leaf;

  if (m->tree == NULL || !playable (m, id))
    return NULL;

  leaf = leaf_with (m->tree, id);

  if (leaf == NULL)
    {
      /* The leaf last focused, which mln_model_settle lets go of once it
         is not drawn; asked again here for an app that has not settled
         since, since a pane put into a leaf that is gone is lost. */
      if (fallback == NULL || !holds (m->tree, fallback))
        fallback = m->focus != NULL && holds (m->tree, m->focus)
          ? m->focus : first_leaf (m->tree);

      leaf = reopen (m, id, fallback, avoid);
    }
  else
    leaf->active = live_index (m, leaf, id);

  set_node (&m->focus, leaf);
  unzoom_for (m, leaf);
  told (m);

  return leaf;
}

void
mln_model_move_divider (MlnModel *m, MlnNode *split, guint a, guint b,
                        double now, double both)
{
  double floor_, ceiling, sum;
  gboolean row;

  if (split->leaf || a >= split->kids->len || b >= split->kids->len)
    return;

  row = split->dir == MLN_ROW;
  floor_ = min_across (m, kid_at (split, a), row);
  ceiling = both - min_across (m, kid_at (split, b), row);
  sum = size_at (split, a) + size_at (split, b);

  /* Two panes that cannot both have what they asked for: there is no
     share that makes it better. */
  if (ceiling < floor_ || both <= 0)
    return;

  now = MAX (floor_, MIN (ceiling, now));
  g_array_index (split->size, double, a) = sum * (now / both);
  g_array_index (split->size, double, b) = sum - size_at (split, a);
}

void
mln_model_equalize (MlnModel *m, MlnNode *split)
{
  double total = 0;
  guint live = 0;

  if (split->leaf)
    return;

  for (guint i = 0; i < split->kids->len; i++)
    if (alive (m, kid_at (split, i)))
      {
        total += size_at (split, i);
        live++;
      }

  for (guint i = 0; i < split->kids->len && live > 0; i++)
    if (alive (m, kid_at (split, i)))
      g_array_index (split->size, double, i) = total / live;

  changed (m);
}

void
mln_model_commit (MlnModel *m)
{
  changed (m);
}

/* ---- asking about the tree ---- */

MlnNode *
mln_model_tree (MlnModel *m)
{
  return m->tree;
}

MlnNode *
mln_model_leaf_with (MlnModel *m, const char *id)
{
  return leaf_with (m->tree, id);
}

MlnNode *
mln_model_parent_of (MlnModel *m, MlnNode *node)
{
  return parent_of (m->tree, node);
}

MlnNode *
mln_model_first_leaf (MlnModel *m)
{
  return m->tree == NULL ? NULL : first_leaf (m->tree);
}

gboolean
mln_model_holds (MlnModel *m, MlnNode *node)
{
  return holds (m->tree, node);
}

MlnNode *
mln_model_get_focus (MlnModel *m)
{
  return m->focus;
}

void
mln_model_set_focus (MlnModel *m, MlnNode *leaf)
{
  set_node (&m->focus, leaf);
}

MlnNode *
mln_model_get_zoom (MlnModel *m)
{
  return m->zoom;
}

void
mln_model_set_zoom (MlnModel *m, MlnNode *leaf)
{
  set_node (&m->zoom, leaf);
}

GPtrArray *
mln_model_live_tabs (MlnModel *m, MlnNode *leaf)
{
  return live_tabs (m, leaf);
}

gboolean
mln_model_alive (MlnModel *m, MlnNode *node)
{
  return alive (m, node);
}

double
mln_model_min_across (MlnModel *m, MlnNode *node, gboolean row)
{
  return min_across (m, node, row);
}

gboolean
mln_model_splittable (MlnModel *m, MlnNode *leaf, const char *id, MlnDir dir,
                      double extent)
{
  gboolean row = dir == MLN_ROW;
  double want;

  if (pane (m, id) == NULL)
    return FALSE;

  want = min_across (m, leaf, row) + (row ? pane (m, id)->min : m->least) + m->split;

  return extent >= want;
}

/* ---- a node ---- */

gboolean
mln_node_is_leaf (const MlnNode *n)
{
  return n->leaf;
}

guint
mln_node_n_tabs (const MlnNode *n)
{
  return n->leaf ? n->tabs->len : 0;
}

const char *
mln_node_tab (const MlnNode *n, guint i)
{
  return n->leaf && i < n->tabs->len ? n->tabs->pdata[i] : NULL;
}

guint
mln_node_active (const MlnNode *n)
{
  return n->leaf ? (guint) MAX (n->active, 0) : 0;
}

MlnDir
mln_node_dir (const MlnNode *n)
{
  return n->dir;
}

guint
mln_node_n_kids (const MlnNode *n)
{
  return n->leaf ? 0 : n->kids->len;
}

MlnNode *
mln_node_kid (const MlnNode *n, guint i)
{
  return !n->leaf && i < n->kids->len ? kid_at (n, i) : NULL;
}

double
mln_node_size (const MlnNode *n, guint i)
{
  return !n->leaf && i < n->size->len ? size_at (n, i) : 0;
}
