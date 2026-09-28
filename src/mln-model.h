/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * mln-model -- mullion's layout model, with no widgets in it.
 *
 * The tree of splits and tabbed leaves, the panes a page has and which
 * of them are in play, the drawer, and every operation a person or the
 * app performs on them: moving a pane into a leaf or beside one, closing
 * it and bringing it back where it was, reading a kept layout and
 * writing one. It is mullion's model (src/panes.js there, the part
 * above the drawing), ported one function to one function, so that the
 * two behave alike and keep layouts the other can read (docs/layout.md
 * and test/layouts.json in mullion).
 *
 * What needs pixels -- whether a split fits, which leaf is under the
 * pointer, which one is to the left -- is the widget's, and it asks
 * here with the sizes it has.
 *
 * Nodes are reference counted. A leaf is remembered after it leaves the
 * tree: a pane closed out of it goes back into it if it comes back into
 * the tree, and to the neighbor it had if not. mullion has a garbage
 * collector for that; here the model holds a reference while it
 * remembers one.
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum
{
  MLN_ROW,                      /* children side by side */
  MLN_COL,                      /* children stacked */
} MlnDir;

typedef struct _MlnNode  MlnNode;
typedef struct _MlnModel MlnModel;

/* Where a pane is, for mln_model_where. */
typedef enum
{
  MLN_WHERE_FRONT,              /* in front in its leaf */
  MLN_WHERE_BEHIND,             /* in a leaf, behind another */
  MLN_WHERE_DRAWER,             /* closed */
  MLN_WHERE_OFF,                /* its mode is not up */
  MLN_WHERE_NONE,               /* not a pane, or no layout yet */
} MlnWhere;

typedef struct
{
  /* Whether the app will add this pane, so a kept layout keeps its
     place (mullion's `later'). May be NULL: never. */
  gboolean (*later)    (const char *id, gpointer data);

  /* A person closed an ephemeral pane: the app ends it with
     mln_model_remove, or keeps it (mullion's `onDiscard'). NULL makes
     ephemeral panes impossible to close from the layout. */
  void     (*discard)  (const char *id, gpointer data);

  /* The layout for `mode' is to be kept as `text', or forgotten when
     `text' is NULL (after a reset). */
  void     (*store)    (const char *mode, const char *text, gpointer data);

  /* The layout changed, by a person or by the app (mullion's
     `onLayout'). mln_model_json has it. */
  void     (*changed)  (const char *mode, gpointer data);
} MlnModelHooks;

/* ---- the model ---- */

MlnModel   *mln_model_new           (const MlnModelHooks *hooks, gpointer data);
void        mln_model_free          (MlnModel *m);

/* A divider's thickness and a leaf's height floor, in pixels. */
void        mln_model_set_split     (MlnModel *m, double px);
void        mln_model_set_leaf      (MlnModel *m, double px);
double      mln_model_get_split     (MlnModel *m);

/* The version kept layouts are kept under: a JSON value (`2',
   `"desk-3"'), or NULL for none. FALSE if it is not JSON. */
gboolean    mln_model_set_version   (MlnModel *m, const char *json);

/* A mode's default layout, as JSON. NULL forgets it; a mode without one
   has every pane in one leaf. FALSE if it is not a layout. */
gboolean    mln_model_set_default   (MlnModel *m, const char *mode,
                                     const char *json);

/* Which mode is up. The tree is gone until mln_model_load. */
void        mln_model_set_mode      (MlnModel *m, const char *mode);
const char *mln_model_get_mode      (MlnModel *m);

/* ---- panes ---- */

/* A pane the app has from the start (mullion's catalog): lasting. */
gboolean    mln_model_register      (MlnModel *m, const char *id, double min);

/* A pane the app adds once it is up (mullion's `add'): ephemeral unless
   `keep', placed beside `near' when the layout has no place for it, in
   front unless `take' is FALSE and the layout kept a place for it. The
   leaf it went into, or NULL when there is no layout or its mode is
   down. `*added' says whether it was taken on at all. */
MlnNode    *mln_model_add           (MlnModel *m, const char *id, double min,
                                     gboolean keep, const char *near,
                                     gboolean take, gboolean *added);

/* Where a pane goes when nothing remembers where it was: the leaf with
   `slot' in its "slots" (NULL: none, or none in the tree), before the
   leaf last focused. And, if `open', it is put there when a layout is
   loaded or reset without it, unless the kept layout lists it as closed:
   a pane new since the layout was kept comes up, and one a person closed
   stays closed. Set it before mln_model_load, as the app registers its
   panes: set after, it takes effect from the next load or reset. May be
   set before the pane is taken on. */
void        mln_model_set_placement (MlnModel *m, const char *id,
                                     const char *slot, gboolean open);

/* No longer a pane (mullion's `remove'). TRUE if it was one. */
gboolean    mln_model_remove        (MlnModel *m, const char *id);

gboolean    mln_model_has           (MlnModel *m, const char *id);
gboolean    mln_model_ephemeral     (MlnModel *m, const char *id);
double      mln_model_min           (MlnModel *m, const char *id);

/* Whether a pane's mode is up (mullion's `available'). */
void        mln_model_set_available (MlnModel *m, const char *id, gboolean ok);

/* The page has it and its mode is up. */
gboolean    mln_model_playable      (MlnModel *m, const char *id);

/* Every pane, in the order taken on. */
GPtrArray  *mln_model_panes         (MlnModel *m);

MlnWhere    mln_model_where         (MlnModel *m, const char *id);

/* The drawer: panes in play that no leaf holds, not ephemeral, in the
   order taken on. Free with g_ptr_array_unref; the strings are the
   model's. */
GPtrArray  *mln_model_closed        (MlnModel *m);

/* ---- the layout ---- */

/* A kept layout for the mode that is up, or NULL: it comes up if it is
   one, and the mode's default if not. TRUE if the kept one was used. */
gboolean    mln_model_load          (MlnModel *m, const char *kept);
gboolean    mln_model_loaded        (MlnModel *m);

/* What is kept for the layout that is up, byte for byte as mullion keeps
   it. Free with g_free; NULL with no layout. */
char       *mln_model_save          (MlnModel *m);

/* The tree as it stands, as JSON (not trimmed, no envelope). */
char       *mln_model_json          (MlnModel *m);

/* A layout put up by the app (mullion's `setLayout'). FALSE, and nothing
   changed, if it is not one. It is the whole layout: floating windows
   close, and what was in them is where it puts it, or closed. */
gboolean    mln_model_set_layout    (MlnModel *m, const char *json);

/* Back to the mode's default, forgetting what was kept. */
void        mln_model_reset         (MlnModel *m);

/* What a render does to the tree before drawing it: a zoom or a focus on
   a leaf no longer there, or with nothing in play, is dropped, and each
   leaf's front tab is clamped to the tabs in play. */
void        mln_model_settle        (MlnModel *m);

/* ---- moving panes, as a person does ---- */

/* Each is one change, kept and told of if it changed anything. */

/* Onto a leaf's strip, before `before', or at the end for NULL. */
void        mln_model_drop_tab      (MlnModel *m, const char *id,
                                     MlnNode *leaf, const char *before);

/* Into a leaf, in front. */
void        mln_model_drop_into     (MlnModel *m, const char *id, MlnNode *leaf);

/* Beside a leaf, in a split of two made for it; `after' is the right or
   the bottom. */
void        mln_model_drop_beside   (MlnModel *m, const char *id, MlnNode *leaf,
                                     MlnDir dir, gboolean after);

/* A person's close: to the drawer, or, for an ephemeral pane, asked of
   the app. TRUE if it closed here and now. */
gboolean    mln_model_close         (MlnModel *m, const char *id);

/* Whether a person's close can do anything to it. */
gboolean    mln_model_closable      (MlnModel *m, const char *id);

/* The i-th tab in play of a leaf, in front. */
void        mln_model_raise         (MlnModel *m, MlnNode *leaf, guint i);

/* Reopened by a person from the drawer: where it was, or into the leaf
   last focused. A change, and the focus is left alone. The leaf, or NULL
   if it was not closed. */
MlnNode    *mln_model_reopen        (MlnModel *m, const char *id);

/* Raised by the app (mullion's `present'): into the leaf it was in or
   beside its old neighbor if it is closed, else `fallback'; never into
   `avoid'. The leaf it is in; NULL if it is not a pane in play. */
MlnNode    *mln_model_present       (MlnModel *m, const char *id,
                                     MlnNode *fallback, MlnNode *avoid);

/* Divider moves, as the drawing measured them: the shares of children
   `a' and `b' of `split' so that `a' is `now' of the `both' pixels the
   two have. No change is kept until mln_model_commit. */
void        mln_model_move_divider  (MlnModel *m, MlnNode *split,
                                     guint a, guint b, double now, double both);

/* Every live child of `split', an equal share. */
void        mln_model_equalize      (MlnModel *m, MlnNode *split);

/* A change made in steps (a divider dragged) is one change: kept and
   told of if the tree is not what it was. */
void        mln_model_commit        (MlnModel *m);

/* ---- floating windows ---- */

/* Each floating window is a tree of its own, kept with the main one (as
   "floating" in the envelope) and known by an id that is never reused.
   The operations above work on a leaf in any of the trees; zoom is the
   main tree's only. */

/* The ids of the floating windows, in the order made. Free with
   g_array_unref. */
GArray     *mln_model_floats        (MlnModel *m);
MlnNode    *mln_model_float_root    (MlnModel *m, guint id);

/* The floating window holding a node, or 0 for the main tree or none. */
guint       mln_model_float_of      (MlnModel *m, MlnNode *node);

/* The size a floating window was last given, 0 by 0 for none yet; set
   by the widget as it is resized, and kept with the layout, not told of
   as a change. */
gboolean    mln_model_float_size    (MlnModel *m, guint id, int *w, int *h);
void        mln_model_set_float_size (MlnModel *m, guint id, int w, int h);

/* A pane into a floating window of its own, `w' by `h'. The window's id,
   or 0 if it is not a pane in play or there is no layout. One change. */
guint       mln_model_undock        (MlnModel *m, const char *id, int w, int h);

/* A floating window closed: its panes back into the main tree, each
   where it was before it left, or at its slot, or in the leaf last
   focused. One change. */
void        mln_model_dock          (MlnModel *m, guint id);

/* One pane out of a floating window, back into the main tree the same
   way, and its leaf the focus. One change. */
void        mln_model_dock_pane     (MlnModel *m, const char *id);

/* ---- asking about the tree ---- */

MlnNode    *mln_model_tree          (MlnModel *m);
MlnNode    *mln_model_leaf_with     (MlnModel *m, const char *id);
MlnNode    *mln_model_parent_of     (MlnModel *m, MlnNode *node);
MlnNode    *mln_model_first_leaf    (MlnModel *m);
gboolean    mln_model_holds         (MlnModel *m, MlnNode *node);

/* The leaf a pane out of the drawer goes into, and the one the keyboard
   is in; the leaf filling the layout. Either may be NULL. */
MlnNode    *mln_model_get_focus     (MlnModel *m);
void        mln_model_set_focus     (MlnModel *m, MlnNode *leaf);
MlnNode    *mln_model_get_zoom      (MlnModel *m);
void        mln_model_set_zoom      (MlnModel *m, MlnNode *leaf);

/* The tabs of a leaf in play (not off, and the page has them). Free
   with g_ptr_array_unref; the strings are the leaf's. */
GPtrArray  *mln_model_live_tabs     (MlnModel *m, MlnNode *leaf);

/* A leaf with a tab in play, or a split with such a leaf under it. */
gboolean    mln_model_alive         (MlnModel *m, MlnNode *node);

/* How narrow (`row') or short a node may be, in pixels. */
double      mln_model_min_across    (MlnModel *m, MlnNode *node, gboolean row);

/* Whether `leaf', `extent' pixels along `dir', has room to be split
   with `id' beside what is in it. */
gboolean    mln_model_splittable    (MlnModel *m, MlnNode *leaf,
                                     const char *id, MlnDir dir,
                                     double extent);

/* ---- a node ---- */

MlnNode    *mln_node_ref            (MlnNode *node);
void        mln_node_unref          (MlnNode *node);

gboolean    mln_node_is_leaf        (const MlnNode *node);

/* A leaf's tabs, all of them, and its front tab (an index into the tabs
   in play). */
guint       mln_node_n_tabs         (const MlnNode *node);
const char *mln_node_tab            (const MlnNode *node, guint i);
guint       mln_node_active         (const MlnNode *node);

/* A split's direction, children and shares. */
MlnDir      mln_node_dir            (const MlnNode *node);
guint       mln_node_n_kids         (const MlnNode *node);
MlnNode    *mln_node_kid            (const MlnNode *node, guint i);
double      mln_node_size           (const MlnNode *node, guint i);

G_END_DECLS
