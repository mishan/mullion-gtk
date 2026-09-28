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

/* Where a leaf's tabs are: a strip across its top, or tucked into its
   top corner over the pane, in sight while the pointer or the focus is in
   the leaf -- the panes' icons (the title for one without), a grip for a
   pane alone, the front pane's cross and a menu button. */
typedef enum
{
  MLN_HEADER_STRIP,
  MLN_HEADER_CORNER,
} MlnHeader;

#define MLN_TYPE_HEADER (mln_header_get_type ())

MLN_EXPORT GType mln_header_get_type (void);

/* A floating window for panes undocked from `panes', made by the app so
   that it can give it an application, a title, its keys. NULL: one is
   made, transient for the window `panes' is in and of its application. */
typedef GtkWindow *(*MlnWindowFunc) (MlnPanes *panes, gpointer data);

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

/* A picture of it for its tab: beside the title in a strip, in place of
   it in the corner. NULL for none. */
MLN_EXPORT void        mln_panes_set_icon      (MlnPanes *self, const char *id,
                                     GIcon *icon);

/* Whether a pane's corner stays in sight while the pane is in front of
   its leaf, rather than only while the pointer or the focus is there: for
   a pane whose first row makes room for it (mln_panes_get_corner_width),
   where it covers nothing. Off for a pane when it is registered or added,
   again too. */
MLN_EXPORT void        mln_panes_set_corner_pinned (MlnPanes *self, const char *id,
                                         gboolean pinned);

/* The "header" property (see MlnHeader). */
MLN_EXPORT void        mln_panes_set_header    (MlnPanes *self, MlnHeader header);
MLN_EXPORT MlnHeader   mln_panes_get_header    (MlnPanes *self);

/* How much of the top of a pane, at the end of the line, the corner's
   controls cover: for a pane whose first row makes room for them. 0 with
   the tabs in a strip. ::corner-changed says when it may have changed. */
MLN_EXPORT int         mln_panes_get_corner_width (MlnPanes *self, const char *id);

/* A pane that wants to be looked at, until it is: its tab is marked,
   and the mark goes when the pane comes into view. */
MLN_EXPORT void        mln_panes_set_attention (MlnPanes *self, const char *id,
                                     gboolean attention);

/* Items of the app's own for a pane's tab menu, between the layout's
   items and Reset Layout. Their actions are looked up from the tab up,
   so the app's window and application actions work. NULL for none. */
MLN_EXPORT void        mln_panes_set_pane_menu (MlnPanes *self, const char *id,
                                     GMenuModel *menu);
/* Where a pane goes when nothing remembers where it was: the leaf whose
   "slots" in the layout name `slot' (NULL: none), ahead of the leaf the
   person was working in. If `open', it is also put there whenever a
   layout is loaded or reset without it -- a pane new since the layout was
   kept comes up -- unless the kept layout lists it as closed: a person
   closed it, and it stays closed. Call it before mln_panes_load, with the
   panes: called after, it takes effect from the next load or reset. May
   be called before the pane is registered or added. */
MLN_EXPORT void        mln_panes_set_placement (MlnPanes *self, const char *id,
                                     const char *slot, gboolean open);
MLN_EXPORT void        mln_panes_set_later_func (MlnPanes *self, MlnLaterFunc func,
                                      gpointer data, GDestroyNotify destroy);

/* In front, out of the drawer if it was there. */
MLN_EXPORT void        mln_panes_present       (MlnPanes *self, const char *id,
                                     gboolean focus);
MLN_EXPORT void        mln_panes_close         (MlnPanes *self, const char *id);

/* Whether it is in front of somebody: what ::pane-shown last said. */
MLN_EXPORT gboolean    mln_panes_is_visible    (MlnPanes *self, const char *id);

/* Where a pane's tab, and the leaf holding it, are drawn, in the
   coordinates of the MlnPanes that draws them: this one, or the one in the
   floating window the pane is in (see mln_panes_get_window). For a
   popover anchored on a tab, and for tests. FALSE for a pane not drawn. */
MLN_EXPORT gboolean    mln_panes_get_tab_bounds  (MlnPanes *self, const char *id,
                                       graphene_rect_t *bounds);
MLN_EXPORT gboolean    mln_panes_get_leaf_bounds (MlnPanes *self, const char *id,
                                       graphene_rect_t *bounds);

/* Where a closed pane's button in the drawer is. FALSE for a pane that
   is not closed, or a drawer that is not shown. */
MLN_EXPORT gboolean    mln_panes_get_closed_bounds (MlnPanes *self, const char *id,
                                         graphene_rect_t *bounds);

/* ---- floating windows ---- */

/* How a floating window is made (see MlnWindowFunc). */
MLN_EXPORT void        mln_panes_set_window_func (MlnPanes *self, MlnWindowFunc func,
                                       gpointer data, GDestroyNotify destroy);

/* A pane into a window of its own, as its tab menu's Move to New Window
   does; and out of one, back where it was in the main window. Closing a
   floating window docks what was in it. Floating windows are kept with
   the layout, size and all, and come back when it is loaded. */
MLN_EXPORT void        mln_panes_undock        (MlnPanes *self, const char *id);
MLN_EXPORT void        mln_panes_dock          (MlnPanes *self, const char *id);

/* The floating window a pane is in, or NULL. */
MLN_EXPORT GtkWindow  *mln_panes_get_window    (MlnPanes *self, const char *id);

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
   "show-drawer", "header" (MlnHeader).

   Signals:
     pane-shown (id, visible)       a pane came into or went out of view
     layout-changed (mode)          by a person or the app
     layout-kept (mode, text)       keep this for the mode; NULL: forget it
     pane-discard (id)              a person closed an ephemeral pane
     corner-changed ()              the corner's width changed over a leaf */

G_END_DECLS
