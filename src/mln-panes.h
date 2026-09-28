/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * MlnPanes -- an app's panes, tiled.
 *
 * The app hands it widgets by id; it lays them out as a tree of splits and
 * tabbed leaves (mln-model), and a person rearranges them: a tab dragged
 * onto another strip or another leaf's edge, a divider dragged, a pane
 * closed to the drawer and brought back where it was.
 *
 * Every pane is a direct child of this widget, for its whole life. A move
 * is a new allocation and a pane behind another tab is unmapped, never
 * unparented: unparenting unrealizes, and a GL area would lose its
 * context every time somebody moved a tab.
 *
 * The layout is data (docs/layout.md in mullion), and keeping it is the
 * app's: ::layout-kept says what to keep, and mln_panes_load reads it
 * back.
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

/* What a shared build exports: this header's functions, and nothing of
   the model underneath, which is not API. Nothing to do for a static
   build, which is the default. */
#if defined(_WIN32) && defined(MLN_BUILDING_SHARED)
#  define MLN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__)
#  define MLN_EXPORT __attribute__ ((visibility ("default")))
#else
#  define MLN_EXPORT
#endif

#define MLN_TYPE_PANES (mln_panes_get_type ())

MLN_EXPORT GType mln_panes_get_type (void);

G_DECLARE_FINAL_TYPE (MlnPanes, mln_panes, MLN, PANES, GtkWidget)

typedef enum
{
  MLN_PANE_LASTING   = 0,       /* closed to the drawer */
  MLN_PANE_EPHEMERAL = 1 << 0,  /* closing asks the app (::pane-discard) */
  MLN_PANE_QUIET     = 1 << 1,  /* added behind what is in front */
} MlnPaneFlags;

/* Whether the app will add a pane later, so a kept layout keeps its
   place. */
typedef gboolean (*MlnLaterFunc) (const char *id, gpointer data);

MLN_EXPORT GtkWidget  *mln_panes_new           (void);

/* ---- panes ---- */

/* A pane the app has from the start: before mln_panes_load. */
MLN_EXPORT gboolean    mln_panes_register      (MlnPanes *self, const char *id,
                                     const char *title, GtkWidget *content,
                                     int min_width);

/* A pane added once the layout is up, beside `near' when the layout has
   no place for it. */
MLN_EXPORT gboolean    mln_panes_add           (MlnPanes *self, const char *id,
                                     const char *title, GtkWidget *content,
                                     int min_width, MlnPaneFlags flags,
                                     const char *near);

/* No longer a pane. The content, which the app now owns (a new
   reference), or NULL. */
MLN_EXPORT GtkWidget  *mln_panes_remove        (MlnPanes *self, const char *id);

MLN_EXPORT GtkWidget  *mln_panes_get_content   (MlnPanes *self, const char *id);
MLN_EXPORT void        mln_panes_set_title     (MlnPanes *self, const char *id,
                                     const char *title);
MLN_EXPORT void        mln_panes_set_available (MlnPanes *self, const char *id,
                                     gboolean available);

/* A pane that wants to be looked at, until it is: its tab is marked,
   and the mark goes when the pane comes into view. */
MLN_EXPORT void        mln_panes_set_attention (MlnPanes *self, const char *id,
                                     gboolean attention);

/* Items of the app's own for a pane's tab menu, between the layout's
   items and Reset Layout. Their actions are looked up from the tab up,
   so the app's window and application actions work. NULL for none. */
MLN_EXPORT void        mln_panes_set_pane_menu (MlnPanes *self, const char *id,
                                     GMenuModel *menu);
MLN_EXPORT void        mln_panes_set_later_func (MlnPanes *self, MlnLaterFunc func,
                                      gpointer data, GDestroyNotify destroy);

/* In front, out of the drawer if it was there. */
MLN_EXPORT void        mln_panes_present       (MlnPanes *self, const char *id,
                                     gboolean focus);
MLN_EXPORT void        mln_panes_close         (MlnPanes *self, const char *id);

/* Whether it is in front of somebody: what ::pane-shown last said. */
MLN_EXPORT gboolean    mln_panes_is_visible    (MlnPanes *self, const char *id);

/* Where a pane's tab, and the leaf holding it, are drawn, in this
   widget's coordinates: for a popover anchored on a tab, and for tests.
   FALSE for a pane not drawn. */
MLN_EXPORT gboolean    mln_panes_get_tab_bounds  (MlnPanes *self, const char *id,
                                       graphene_rect_t *bounds);
MLN_EXPORT gboolean    mln_panes_get_leaf_bounds (MlnPanes *self, const char *id,
                                       graphene_rect_t *bounds);

/* Where a closed pane's button in the drawer is. FALSE for a pane that
   is not closed, or a drawer that is not shown. */
MLN_EXPORT gboolean    mln_panes_get_closed_bounds (MlnPanes *self, const char *id,
                                         graphene_rect_t *bounds);

/* The drawer: panes in play that the layout does not hold. */
MLN_EXPORT char      **mln_panes_get_closed    (MlnPanes *self);

/* ---- the layout ---- */

MLN_EXPORT gboolean    mln_panes_set_default   (MlnPanes *self, const char *mode,
                                     const char *json);
MLN_EXPORT gboolean    mln_panes_set_version   (MlnPanes *self, const char *json);
MLN_EXPORT void        mln_panes_set_mode      (MlnPanes *self, const char *mode);

/* A kept layout for the mode, or NULL for the default. TRUE if the kept
   one was used. */
MLN_EXPORT gboolean    mln_panes_load          (MlnPanes *self, const char *kept);
MLN_EXPORT char       *mln_panes_save          (MlnPanes *self);
MLN_EXPORT char       *mln_panes_get_layout    (MlnPanes *self);
MLN_EXPORT gboolean    mln_panes_set_layout    (MlnPanes *self, const char *json);
MLN_EXPORT void        mln_panes_reset         (MlnPanes *self);

/* Properties: "split" (divider thickness, px), "leaf-min-height" (px),
   "edge" (the part of a leaf's box that splits it, 0..0.5),
   "show-drawer".

   Signals:
     pane-shown (id, visible)       a pane came into or went out of view
     layout-changed (mode)          by a person or the app
     layout-kept (mode, text)       keep this for the mode; NULL: forget it
     pane-discard (id)              a person closed an ephemeral pane */

G_END_DECLS
