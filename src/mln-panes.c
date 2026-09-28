/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * The drawing half of mullion (src/panes.js from `render' down), as a GTK
 * widget.
 *
 * mullion lets the browser lay the tree out as nested flex boxes; here the
 * widget does it, with one pass over the tree in size_allocate. Every
 * pane's host, every tab strip and every divider is a direct child of
 * MlnPanes, so nothing a person does to the layout unparents a pane.
 * What changes from one layout to the next is which children are shown
 * and where they are put.
 *
 * `render' (sync_children) is what follows a change to the tree: strips
 * and dividers made for leaves and splits that have none, those for nodes
 * no longer drawn let go, each tab put in its strip in order, every child
 * shown or not, and the app told which panes came into view. Allocation
 * then only places what render decided to show.
 */

#include "mln-panes.h"
#include "mln-model.h"

#include <math.h>
#include <string.h>

/* ---- a pane's host: its region, with the app's widget in it ---- */

#define MLN_TYPE_HOST (mln_host_get_type ())
G_DECLARE_FINAL_TYPE (MlnHost, mln_host, MLN, HOST, GtkWidget)

struct _MlnHost
{
  GtkWidget parent;
  GtkWidget *content;
};

G_DEFINE_FINAL_TYPE (MlnHost, mln_host, GTK_TYPE_WIDGET)

static void
mln_host_measure (GtkWidget *w, GtkOrientation o, int for_size,
                  int *min, int *nat, int *min_base, int *nat_base)
{
  MlnHost *self = MLN_HOST (w);

  /* Nothing asked of the layout: a pane's width floor is the model's
     (the pane's min_width), and a content wider than the box it is
     given is clipped rather than widening the leaf. */
  *min = 0;
  *nat = 0;

  if (self->content != NULL)
    gtk_widget_measure (self->content, o, -1, NULL, nat, NULL, NULL);
}

static void
mln_host_size_allocate (GtkWidget *w, int width, int height, int baseline)
{
  MlnHost *self = MLN_HOST (w);
  int mw = 0, mh = 0;

  if (self->content == NULL)
    return;

  /* At least what it needs, and clipped by the host where that is more
     than the host has. */
  gtk_widget_measure (self->content, GTK_ORIENTATION_HORIZONTAL, -1, &mw, NULL, NULL, NULL);
  gtk_widget_measure (self->content, GTK_ORIENTATION_VERTICAL, MAX (width, mw),
                      &mh, NULL, NULL, NULL);
  gtk_widget_allocate (self->content, MAX (width, mw), MAX (height, mh), -1, NULL);
}

static void
mln_host_dispose (GObject *o)
{
  MlnHost *self = MLN_HOST (o);

  g_clear_pointer (&self->content, gtk_widget_unparent);
  G_OBJECT_CLASS (mln_host_parent_class)->dispose (o);
}

static void
mln_host_class_init (MlnHostClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = mln_host_dispose;
  wc->measure = mln_host_measure;
  wc->size_allocate = mln_host_size_allocate;
  gtk_widget_class_set_css_name (wc, "pane");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TAB_PANEL);
}

static void
mln_host_init (MlnHost *self)
{
  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
}

static GtkWidget *
mln_host_new (GtkWidget *content)
{
  MlnHost *self = g_object_new (MLN_TYPE_HOST, NULL);

  self->content = content;
  gtk_widget_set_parent (content, GTK_WIDGET (self));

  return GTK_WIDGET (self);
}

/* ---- a tab ---- */

/*
 * A tab is a widget that takes the focus itself, where a GtkBox only
 * passes it on to what is in it: the tab is what the arrow keys, Home,
 * End and the menu key are for. Its title and its cross are laid out as
 * a row. Only the front tab of a strip, and its cross, are in the Tab
 * chain (render says which), as a tablist's roving tab stop is.
 */

#define MLN_TYPE_TAB (mln_tab_get_type ())
G_DECLARE_FINAL_TYPE (MlnTab, mln_tab, MLN, TAB, GtkWidget)

struct _MlnTab
{
  GtkWidget parent;
};

G_DEFINE_FINAL_TYPE (MlnTab, mln_tab, GTK_TYPE_WIDGET)

static void
mln_tab_dispose (GObject *o)
{
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (o))) != NULL)
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (mln_tab_parent_class)->dispose (o);
}

static void
mln_tab_class_init (MlnTabClass *klass)
{
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);

  G_OBJECT_CLASS (klass)->dispose = mln_tab_dispose;
  gtk_widget_class_set_layout_manager_type (wc, GTK_TYPE_BOX_LAYOUT);
  gtk_widget_class_set_css_name (wc, "tab");
  gtk_widget_class_set_accessible_role (wc, GTK_ACCESSIBLE_ROLE_TAB);
}

static void
mln_tab_init (MlnTab *self)
{
}

/* ---- the look ---- */

/*
 * Enough to read as tabs and dividers under any theme: the colors are the
 * theme's own names, the sizes are small, and an app's own stylesheet wins
 * over all of it (this sits between the theme and the app). The node names
 * are mullion's classes, so a stylesheet for one reads like the other's.
 */
static const char *STYLE =
  "panes > tabs {"
  "  background-color: shade(@theme_bg_color, 0.96);"
  "  border-bottom: 1px solid alpha(@borders, 0.8);"
  "}"
  "panes > tabs > tab {"
  "  padding: 3px 4px 3px 10px;"
  "  border-bottom: 2px solid transparent;"
  "  color: alpha(currentColor, 0.72);"
  "}"
  "panes > tabs > tab.front {"
  "  color: @theme_fg_color;"
  "  border-bottom-color: @theme_selected_bg_color;"
  "}"
  "panes > tabs > tab:hover { background-color: alpha(currentColor, 0.06); }"
  "panes > tabs > tab:focus-visible {"
  "  outline: 2px solid alpha(@theme_selected_bg_color, 0.8);"
  "  outline-offset: -2px;"
  "}"
  "panes > tabs > tab > button.shut {"
  "  min-width: 18px; min-height: 18px; padding: 0; margin-left: 4px;"
  "  opacity: 0;"
  "}"
  "panes > tabs > tab:hover > button.shut,"
  "panes > tabs > tab.front > button.shut,"
  "panes > tabs > tab:focus-within > button.shut { opacity: 1; }"
  "panes > separator.panesplit {"
  "  background-color: shade(@theme_bg_color, 0.88);"
  "  min-width: 0; min-height: 0;"
  "}"
  "panes > separator.panesplit:focus-visible {"
  "  background-color: @theme_selected_bg_color;"
  "}"
  "panes > .panedrawer {"
  "  padding: 3px 6px;"
  "  border-bottom: 1px solid alpha(@borders, 0.8);"
  "}"
  "panes > .panedrawer > label { opacity: 0.7; }"
  "panes > tabs > tab.attention > label { font-weight: bold; }"
  "panes > tabs > tab.attention {"
  "  box-shadow: inset 0 2px alpha(@theme_selected_bg_color, 0.9);"
  "}"
  "panes > panedrop {"
  "  background-color: alpha(@theme_selected_bg_color, 0.18);"
  "  border: 2px solid alpha(@theme_selected_bg_color, 0.8);"
  "}"
  "panes > panedrop.slot {"
  "  background-color: @theme_selected_bg_color;"
  "  border: none;"
  "}"
  "panes > .panedrawer > button.paneclosed { padding: 1px 8px; min-height: 22px; }";

/* Between the theme (200) and the settings and the app (400, 600). */
#define STYLE_PRIORITY 250

/* A mistake in the stylesheet above, which is this file's and nobody
   else's to fix. */
static void
style_error (GtkCssProvider *css, GtkCssSection *section, GError *error,
             gpointer data)
{
  char *where = gtk_css_section_to_string (section);

  g_critical ("mullion-gtk stylesheet: %s: %s", where, error->message);
  g_free (where);
}

static void
add_style (GdkDisplay *display)
{
  GtkCssProvider *css;

  if (g_object_get_data (G_OBJECT (display), "mln-panes-style") != NULL)
    return;

  css = gtk_css_provider_new ();
  g_signal_connect (css, "parsing-error", G_CALLBACK (style_error), NULL);
  gtk_css_provider_load_from_string (css, STYLE);
  gtk_style_context_add_provider_for_display (display, GTK_STYLE_PROVIDER (css),
                                              STYLE_PRIORITY);
  g_object_set_data_full (G_OBJECT (display), "mln-panes-style", css, g_object_unref);
}

/* ---- the widget ---- */

typedef struct
{
  char *id;
  char *title;
  GtkWidget *host;
  GtkWidget *tab;               /* its tab, in whichever strip holds it */
  GtkWidget *label;
  GtkWidget *shut;              /* the cross on the tab */
  gboolean shown;               /* what pane-shown last said */
  GMenuModel *menu;             /* the app's items for its tab menu */
} Pane;

typedef struct { int x, y, w, h; } Box;

/* A strip drawn for a leaf, and the node it was drawn for, and where the
   leaf was last put: what a drop and a direction are measured against. */
typedef struct
{
  MlnNode *leaf;
  GtkWidget *strip;
  Box box;                      /* the whole leaf, strip and pane */
  int strip_h;
} Strip;

/* Where a dragged tab would land if it were let go (mullion's `under'). */
typedef enum
{
  DROP_NONE,
  DROP_TAB,                     /* onto a strip, before `before' */
  DROP_INTO,                    /* into a leaf, in front */
  DROP_BESIDE,                  /* off a leaf's edge, a split */
  DROP_DRAWER,                  /* closed */
} DropKind;

typedef struct
{
  DropKind kind;
  MlnNode *leaf;                /* a reference: a render mid-drag can free it */
  char *before;
  MlnDir dir;
  gboolean after;               /* the side in the tree */
  gboolean far;                 /* the side on the screen */
  Box hint;
} Drop;

/* A divider between two live children of a split. */
typedef struct
{
  MlnNode *split;
  guint a, b;                   /* the children either side, by index */
  GtkWidget *bar;
  double from;                  /* where a drag started, and ... */
  double start;                 /* ... the first side's extent then */
  gboolean moved;
} Divider;

struct _MlnPanes
{
  GtkWidget parent;

  MlnModel *model;
  GHashTable *panes;            /* id -> Pane* */
  GPtrArray *order;             /* Pane*, in the order taken on */
  GPtrArray *strips;            /* Strip* */
  GPtrArray *dividers;          /* Divider* */

  GtkWidget *drawer;            /* the row of closed panes */
  GtkWidget *blank;             /* what is shown when every pane is closed */

  int split;
  int least;
  double edge;
  gboolean show_drawer;

  MlnLaterFunc later;
  gpointer later_data;
  GDestroyNotify later_destroy;

  guint render_idle;

  /* A tab being dragged: which, whether it has passed the threshold,
     where it would land, and the widget that shows where. */
  char *drag_id;
  gboolean dragging;
  Drop drop;
  GtkWidget *hint;

  GtkWidget *menu;              /* the tab menu that is open, if one is */
  char *menu_id;                /* whose */
};

G_DEFINE_FINAL_TYPE (MlnPanes, mln_panes, GTK_TYPE_WIDGET)

enum
{
  PROP_0,
  PROP_SPLIT,
  PROP_LEAF_MIN_HEIGHT,
  PROP_EDGE,
  PROP_SHOW_DRAWER,
  N_PROPS
};

static GParamSpec *props[N_PROPS];

enum
{
  PANE_SHOWN,
  LAYOUT_CHANGED,
  LAYOUT_KEPT,
  PANE_DISCARD,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void render (MlnPanes *self);
static void on_tab_drag_begin (GtkGestureDrag *g, double x, double y, gpointer data);
static void on_tab_drag_update (GtkGestureDrag *g, double x, double y, gpointer data);
static void on_tab_drag_end (GtkGestureDrag *g, double x, double y, gpointer data);
static gboolean on_tab_key (GtkEventControllerKey *keys, guint keyval, guint code,
                            GdkModifierType state, gpointer data);
static void open_menu (MlnPanes *self, const char *id, double x, double y);

static Pane *
pane_of (MlnPanes *self, const char *id)
{
  return id == NULL ? NULL : g_hash_table_lookup (self->panes, id);
}

/* ---- the model's hooks ---- */

static gboolean
hook_later (const char *id, gpointer data)
{
  MlnPanes *self = data;

  return self->later != NULL && self->later (id, self->later_data);
}

static void
hook_discard (const char *id, gpointer data)
{
  g_signal_emit (data, signals[PANE_DISCARD], 0, id);
}

static void
hook_store (const char *mode, const char *text, gpointer data)
{
  g_signal_emit (data, signals[LAYOUT_KEPT], 0, mode, text);
}

static void
hook_changed (const char *mode, gpointer data)
{
  g_signal_emit (data, signals[LAYOUT_CHANGED], 0, mode);
}

/* ---- tabs ---- */

static void
on_tab_pressed (GtkGestureClick *click, int n, double x, double y, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (click), "mln-panes");
  Pane *p = data;
  MlnNode *leaf = mln_model_leaf_with (self->model, p->id);
  GPtrArray *live;

  if (leaf == NULL)
    return;

  live = mln_model_live_tabs (self->model, leaf);

  for (guint i = 0; i < live->len; i++)
    if (strcmp (live->pdata[i], p->id) == 0)
      {
        mln_model_set_focus (self->model, leaf);
        mln_model_raise (self->model, leaf, i);
      }

  g_ptr_array_unref (live);
  render (self);

  /* A tab clicked is a tab the person is on. */
  gtk_widget_grab_focus (p->tab);
}

static GtkWidget *reopen_button (MlnPanes *self, const char *id);
static void focus_front (MlnPanes *self, MlnNode *leaf);

static void
on_tab_shut (GtkButton *button, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (button), "mln-panes");
  Pane *p = data;
  MlnNode *leaf = mln_model_leaf_with (self->model, p->id);

  char *id = g_strdup (p->id);

  if (!mln_model_close (self->model, id))
    {
      g_free (id);
      return;
    }

  if (leaf != NULL && mln_model_holds (self->model, leaf))
    mln_model_set_focus (self->model, leaf);

  render (self);

  /* Onto its button in the drawer, which is where it went; or, with no
     drawer to reach, onto the front tab of what is left. */
  {
    GtkWidget *back = reopen_button (self, id);

    if (back != NULL)
      gtk_widget_grab_focus (back);
    else
      focus_front (self, leaf != NULL && mln_model_holds (self->model, leaf)
                         ? leaf : mln_model_first_leaf (self->model));
  }

  g_free (id);
}

static void
on_tab_menu_click (GtkGestureClick *click, int n, double x, double y, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (click), "mln-panes");
  Pane *p = data;
  graphene_point_t at;

  if (gtk_widget_compute_point (p->tab, GTK_WIDGET (self), &GRAPHENE_POINT_INIT (x, y), &at))
    open_menu (self, p->id, at.x, at.y);
}

static GtkWidget *
make_tab (MlnPanes *self, Pane *p)
{
  GtkWidget *tab = g_object_new (MLN_TYPE_TAB, NULL);
  GtkGesture *click = gtk_gesture_click_new ();

  p->label = gtk_label_new (p->title);
  gtk_label_set_ellipsize (GTK_LABEL (p->label), PANGO_ELLIPSIZE_END);
  gtk_label_set_width_chars (GTK_LABEL (p->label), 3);
  gtk_widget_set_parent (p->label, tab);

  p->shut = gtk_button_new_from_icon_name ("window-close-symbolic");
  gtk_widget_add_css_class (p->shut, "flat");
  gtk_widget_add_css_class (p->shut, "shut");
  gtk_widget_set_focus_on_click (p->shut, FALSE);
  g_object_set_data (G_OBJECT (p->shut), "mln-panes", self);
  g_signal_connect (p->shut, "clicked", G_CALLBACK (on_tab_shut), p);
  gtk_widget_set_parent (p->shut, tab);

  {
    char *name = g_strdup_printf ("Close %s", p->title);

    gtk_accessible_update_property (GTK_ACCESSIBLE (p->shut),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, name, -1);
    g_free (name);
  }

  gtk_accessible_update_relation (GTK_ACCESSIBLE (tab),
                                  GTK_ACCESSIBLE_RELATION_CONTROLS, p->host, NULL, -1);
  gtk_accessible_update_relation (GTK_ACCESSIBLE (p->host),
                                  GTK_ACCESSIBLE_RELATION_LABELLED_BY, p->label, NULL, -1);

  g_object_set_data (G_OBJECT (click), "mln-panes", self);
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), GDK_BUTTON_PRIMARY);
  /* Raised on release, and not after a drag: a click, not a press, as a
     tab that starts a drag has not been chosen. */
  g_signal_connect (click, "released", G_CALLBACK (on_tab_pressed), p);
  gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (click));

  {
    GtkGesture *second = gtk_gesture_click_new ();

    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (second), GDK_BUTTON_SECONDARY);
    g_object_set_data (G_OBJECT (second), "mln-panes", self);
    g_signal_connect (second, "pressed", G_CALLBACK (on_tab_menu_click), p);
    gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (second));
  }

  {
    GtkGesture *drag = gtk_gesture_drag_new ();
    GtkEventController *keys = gtk_event_controller_key_new ();

    g_object_set_data (G_OBJECT (drag), "mln-panes", self);
    gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (drag), GDK_BUTTON_PRIMARY);
    g_signal_connect (drag, "drag-begin", G_CALLBACK (on_tab_drag_begin), p);
    g_signal_connect (drag, "drag-update", G_CALLBACK (on_tab_drag_update), p);
    g_signal_connect (drag, "drag-end", G_CALLBACK (on_tab_drag_end), p);
    gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (drag));

    g_object_set_data (G_OBJECT (keys), "mln-panes", self);
    g_signal_connect (keys, "key-pressed", G_CALLBACK (on_tab_key), p);
    gtk_widget_add_controller (tab, keys);
  }

  return g_object_ref_sink (tab);
}

/* ---- strips ---- */

static Strip *
strip_for (MlnPanes *self, MlnNode *leaf)
{
  for (guint i = 0; i < self->strips->len; i++)
    {
      Strip *s = self->strips->pdata[i];

      if (s->leaf == leaf)
        return s;
    }

  return NULL;
}

static void
strip_free (Strip *s)
{
  /* Its tabs are the panes', and are taken out before it goes. */
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (s->strip)) != NULL)
    gtk_box_remove (GTK_BOX (s->strip), child);

  gtk_widget_unparent (s->strip);
  mln_node_unref (s->leaf);
  g_free (s);
}

static Strip *
make_strip (MlnPanes *self, MlnNode *leaf)
{
  Strip *s = g_new0 (Strip, 1);

  s->leaf = mln_node_ref (leaf);
  s->strip = g_object_new (GTK_TYPE_BOX,
                           "orientation", GTK_ORIENTATION_HORIZONTAL,
                           "accessible-role", GTK_ACCESSIBLE_ROLE_TAB_LIST,
                           "css-name", "tabs",
                           NULL);
  gtk_accessible_update_property (GTK_ACCESSIBLE (s->strip),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, "Panes", -1);
  gtk_widget_set_parent (s->strip, GTK_WIDGET (self));
  g_ptr_array_add (self->strips, s);

  return s;
}

/* ---- dividers ---- */

static Divider *divider_at (MlnPanes *self, MlnNode *split, guint a);

static gboolean
row_of (MlnNode *split)
{
  return mln_node_dir (split) == MLN_ROW;
}

/* Where the pointer is along the divider's axis, in MlnPanes' own
   coordinates: the divider moves under the pointer as it is dragged, and
   an offset measured from the divider shrinks by as much as it moves. */
static double
divider_axis (Divider *d, double x, double y)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (d->bar), "mln-panes");
  graphene_point_t at;

  if (!gtk_widget_compute_point (d->bar, GTK_WIDGET (self),
                                 &GRAPHENE_POINT_INIT (x, y), &at))
    return 0;

  return row_of (d->split) ? at.x : at.y;
}

static void
on_divider_begin (GtkGestureDrag *drag, double x, double y, gpointer data)
{
  Divider *d = data;

  d->moved = FALSE;
  d->from = divider_axis (d, x, y);
  d->start = g_array_index ((GArray *) g_object_get_data (G_OBJECT (d->bar), "mln-sides"),
                            double, 0);
}

static void
on_divider_update (GtkGestureDrag *drag, double dx, double dy, gpointer data)
{
  Divider *d = data;
  MlnPanes *self = g_object_get_data (G_OBJECT (d->bar), "mln-panes");
  GArray *sides = g_object_get_data (G_OBJECT (d->bar), "mln-sides");
  double both = g_array_index (sides, double, 1);
  double sx, sy, delta;

  gtk_gesture_drag_get_start_point (drag, &sx, &sy);
  delta = divider_axis (d, sx + dx, sy + dy) - d->from;

  if (row_of (d->split) && gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    delta = -delta;

  if (delta != 0)
    d->moved = TRUE;

  mln_model_move_divider (self->model, d->split, d->a, d->b, d->start + delta, both);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
on_divider_end (GtkGestureDrag *drag, double dx, double dy, gpointer data)
{
  Divider *d = data;
  MlnPanes *self = g_object_get_data (G_OBJECT (d->bar), "mln-panes");

  /* A press on a divider is not a new layout. */
  if (d->moved)
    mln_model_commit (self->model);
}

static void
on_divider_twice (GtkGestureClick *click, int n, double x, double y, gpointer data)
{
  Divider *d = data;
  MlnPanes *self = g_object_get_data (G_OBJECT (d->bar), "mln-panes");

  if (n != 2)
    return;

  mln_model_equalize (self->model, d->split);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static gboolean
on_divider_key (GtkEventControllerKey *keys, guint keyval, guint code,
                GdkModifierType state, gpointer data)
{
  Divider *d = data;
  MlnPanes *self = g_object_get_data (G_OBJECT (d->bar), "mln-panes");
  GArray *sides = g_object_get_data (G_OBJECT (d->bar), "mln-sides");
  int step = 0;

  if (state & (GDK_ALT_MASK | GDK_CONTROL_MASK | GDK_SHIFT_MASK |
               GDK_SUPER_MASK | GDK_META_MASK))
    return FALSE;

  /* Along the way it moves: left and right for a divider between
     columns, up and down for one between rows. */
  switch (keyval)
    {
    case GDK_KEY_Left:  step = row_of (d->split) ? -1 : 0; break;
    case GDK_KEY_Right: step = row_of (d->split) ? 1 : 0; break;
    case GDK_KEY_Up:    step = row_of (d->split) ? 0 : -1; break;
    case GDK_KEY_Down:  step = row_of (d->split) ? 0 : 1; break;
    default: return FALSE;
    }

  if (step == 0)
    return FALSE;

  if ((keyval == GDK_KEY_Left || keyval == GDK_KEY_Right) && row_of (d->split) &&
      gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL)
    step = -step;

  mln_model_move_divider (self->model, d->split, d->a, d->b,
                          g_array_index (sides, double, 0) + step * 16,
                          g_array_index (sides, double, 1));
  mln_model_commit (self->model);
  gtk_widget_queue_allocate (GTK_WIDGET (self));

  return TRUE;
}

static void
divider_free (Divider *d)
{
  gtk_widget_unparent (d->bar);
  mln_node_unref (d->split);
  g_free (d);
}

static Divider *
make_divider (MlnPanes *self, MlnNode *split, guint a, guint b)
{
  Divider *d = g_new0 (Divider, 1);
  GtkGesture *drag = gtk_gesture_drag_new ();
  GtkGesture *click = gtk_gesture_click_new ();
  GtkEventController *keys = gtk_event_controller_key_new ();
  gboolean row = row_of (split);
  GdkCursor *cursor = gdk_cursor_new_from_name (row ? "col-resize" : "row-resize", NULL);

  d->split = mln_node_ref (split);
  d->a = a;
  d->b = b;
  d->bar = g_object_new (GTK_TYPE_SEPARATOR,
                         "orientation", row ? GTK_ORIENTATION_VERTICAL
                                            : GTK_ORIENTATION_HORIZONTAL,
                         "accessible-role", GTK_ACCESSIBLE_ROLE_SEPARATOR,
                         "focusable", TRUE,
                         "cursor", cursor,
                         NULL);
  g_clear_object (&cursor);
  gtk_widget_add_css_class (d->bar, "panesplit");
  gtk_accessible_update_property (GTK_ACCESSIBLE (d->bar),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  row ? "Resize columns" : "Resize rows",
                                  GTK_ACCESSIBLE_PROPERTY_ORIENTATION,
                                  row ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL,
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, 0.0,
                                  GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, 100.0,
                                  -1);

  g_object_set_data (G_OBJECT (d->bar), "mln-panes", self);
  g_object_set_data_full (G_OBJECT (d->bar), "mln-sides",
                          g_array_sized_new (FALSE, TRUE, sizeof (double), 2),
                          (GDestroyNotify) g_array_unref);
  g_array_set_size (g_object_get_data (G_OBJECT (d->bar), "mln-sides"), 2);

  g_signal_connect (drag, "drag-begin", G_CALLBACK (on_divider_begin), d);
  g_signal_connect (drag, "drag-update", G_CALLBACK (on_divider_update), d);
  g_signal_connect (drag, "drag-end", G_CALLBACK (on_divider_end), d);
  gtk_widget_add_controller (d->bar, GTK_EVENT_CONTROLLER (drag));

  g_signal_connect (click, "pressed", G_CALLBACK (on_divider_twice), d);
  gtk_widget_add_controller (d->bar, GTK_EVENT_CONTROLLER (click));

  g_signal_connect (keys, "key-pressed", G_CALLBACK (on_divider_key), d);
  gtk_widget_add_controller (d->bar, keys);

  gtk_widget_set_parent (d->bar, GTK_WIDGET (self));
  g_ptr_array_add (self->dividers, d);

  return d;
}

static Divider *
divider_at (MlnPanes *self, MlnNode *split, guint a)
{
  for (guint i = 0; i < self->dividers->len; i++)
    {
      Divider *d = self->dividers->pdata[i];

      if (d->split == split && d->a == a)
        return d;
    }

  return NULL;
}

/* ---- the drawer ---- */

static void
on_reopen (GtkButton *button, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (button), "mln-panes");

  mln_panes_present (self, data, TRUE);
}

static void
free_id (gpointer data, GClosure *closure)
{
  g_free (data);
}

static GtkWidget *
reopen_button (MlnPanes *self, const char *id)
{
  for (GtkWidget *b = gtk_widget_get_first_child (self->drawer); b != NULL;
       b = gtk_widget_get_next_sibling (b))
    if (g_strcmp0 (g_object_get_data (G_OBJECT (b), "mln-id"), id) == 0 &&
        gtk_widget_get_child_visible (self->drawer))
      return b;

  return NULL;
}

static void
fill_drawer (MlnPanes *self)
{
  GPtrArray *closed = mln_model_closed (self->model);
  GtkWidget *child;
  char *had = NULL;

  /* The buttons are made again; the one with the focus is given it back. */
  for (child = gtk_widget_get_first_child (self->drawer); child != NULL;
       child = gtk_widget_get_next_sibling (child))
    if (gtk_widget_has_focus (child))
      had = g_strdup (g_object_get_data (G_OBJECT (child), "mln-id"));

  while ((child = gtk_widget_get_first_child (self->drawer)) != NULL)
    gtk_box_remove (GTK_BOX (self->drawer), child);

  if (closed->len > 0)
    gtk_box_append (GTK_BOX (self->drawer), gtk_label_new ("Closed:"));
  else if (self->dragging && mln_model_closable (self->model, self->drag_id))
    gtk_box_append (GTK_BOX (self->drawer), gtk_label_new ("Drop here to close"));

  for (guint i = 0; i < closed->len; i++)
    {
      Pane *p = pane_of (self, closed->pdata[i]);
      GtkWidget *b = gtk_button_new_with_label (p->title);
      char *tip = g_strdup_printf ("Reopen %s", p->title);

      gtk_widget_set_tooltip_text (b, tip);
      gtk_widget_add_css_class (b, "paneclosed");
      g_object_set_data (G_OBJECT (b), "mln-panes", self);
      g_object_set_data_full (G_OBJECT (b), "mln-id", g_strdup (p->id), g_free);
      g_signal_connect_data (b, "clicked", G_CALLBACK (on_reopen), g_strdup (p->id),
                             free_id, 0);
      gtk_box_append (GTK_BOX (self->drawer), b);
      g_free (tip);
    }

  gtk_widget_set_child_visible (self->drawer,
                                self->show_drawer &&
                                (closed->len > 0 ||
                                 (self->dragging &&
                                  mln_model_closable (self->model, self->drag_id))));

  if (had != NULL)
    {
      GtkWidget *again = reopen_button (self, had);

      if (again != NULL)
        gtk_widget_grab_focus (again);

      g_free (had);
    }
  g_ptr_array_unref (closed);
}

/* ---- render ---- */

static gboolean
is_visible_leaf (MlnPanes *self, MlnNode *leaf)
{
  MlnNode *zoom = mln_model_get_zoom (self->model);

  return zoom == NULL || zoom == leaf;
}

/* Strips and dividers for what the tree draws, the tabs in their strips,
   each host shown or not, and which panes are in front. */
static void
sync_node (MlnPanes *self, MlnNode *node, GHashTable *drawn, GHashTable *front)
{
  if (mln_node_is_leaf (node))
    {
      GPtrArray *live = mln_model_live_tabs (self->model, node);
      Strip *s = strip_for (self, node);
      GtkWidget *prev = NULL;
      guint active = mln_node_active (node);

      if (s == NULL)
        s = make_strip (self, node);

      g_hash_table_add (drawn, s);

      for (guint i = 0; i < live->len; i++)
        {
          Pane *p = pane_of (self, live->pdata[i]);
          gboolean on = i == active;

          if (gtk_widget_get_parent (p->tab) != s->strip)
            {
              if (gtk_widget_get_parent (p->tab) != NULL)
                gtk_box_remove (GTK_BOX (gtk_widget_get_parent (p->tab)), p->tab);

              gtk_box_insert_child_after (GTK_BOX (s->strip), p->tab, prev);
            }
          else
            gtk_box_reorder_child_after (GTK_BOX (s->strip), p->tab, prev);

          prev = p->tab;

          gtk_accessible_update_state (GTK_ACCESSIBLE (p->tab),
                                       GTK_ACCESSIBLE_STATE_SELECTED, on, -1);

          if (on)
            gtk_widget_add_css_class (p->tab, "front");
          else
            gtk_widget_remove_css_class (p->tab, "front");

          /* The front tab and its cross are the strip's Tab stops. */
          gtk_widget_set_focusable (p->tab, on);
          gtk_widget_set_focusable (p->shut, on);

          gtk_widget_set_visible (p->shut, mln_model_closable (self->model, p->id));

          if (on && is_visible_leaf (self, node))
            g_hash_table_add (front, p);
        }

      g_ptr_array_unref (live);
      return;
    }

  {
    int prev = -1;

    for (guint i = 0; i < mln_node_n_kids (node); i++)
      {
        MlnNode *kid = mln_node_kid (node, i);

        if (!mln_model_alive (self->model, kid))
          continue;

        /* A divider between each pair of live children, kept by the
           split and the first child's index. */
        if (prev >= 0)
          {
            Divider *d = divider_at (self, node, (guint) prev);

            if (d == NULL)
              d = make_divider (self, node, (guint) prev, i);

            d->b = i;
            g_hash_table_add (drawn, d);
          }

        sync_node (self, kid, drawn, front);
        prev = (int) i;
      }
  }
}

static void
sync_children (MlnPanes *self)
{
  GHashTable *drawn = g_hash_table_new (NULL, NULL);
  GHashTable *front = g_hash_table_new (NULL, NULL);
  MlnNode *tree = mln_model_tree (self->model);
  gboolean some;
  MlnNode *zoom;

  mln_model_settle (self->model);
  zoom = mln_model_get_zoom (self->model);
  some = tree != NULL && mln_model_alive (self->model, tree);

  if (some)
    sync_node (self, tree, drawn, front);

  /* Strips and dividers the tree no longer draws. */
  for (guint i = self->strips->len; i-- > 0; )
    if (!g_hash_table_contains (drawn, self->strips->pdata[i]))
      g_ptr_array_remove_index (self->strips, i);

  for (guint i = self->dividers->len; i-- > 0; )
    if (!g_hash_table_contains (drawn, self->dividers->pdata[i]))
      g_ptr_array_remove_index (self->dividers, i);

  /* A tab whose pane is not drawn goes nowhere. */
  for (guint i = 0; i < self->order->len; i++)
    {
      Pane *p = self->order->pdata[i];
      MlnNode *leaf = mln_model_leaf_with (self->model, p->id);
      gboolean drawn_tab = leaf != NULL && mln_model_playable (self->model, p->id) &&
                           strip_for (self, leaf) != NULL;

      if (!drawn_tab && gtk_widget_get_parent (p->tab) != NULL)
        gtk_box_remove (GTK_BOX (gtk_widget_get_parent (p->tab)), p->tab);

      gtk_widget_set_child_visible (p->host, g_hash_table_contains (front, p));
    }

  for (guint i = 0; i < self->strips->len; i++)
    {
      Strip *s = self->strips->pdata[i];

      gtk_widget_set_child_visible (s->strip, zoom == NULL || zoom == s->leaf);
    }

  for (guint i = 0; i < self->dividers->len; i++)
    gtk_widget_set_child_visible (((Divider *) self->dividers->pdata[i])->bar, zoom == NULL);

  gtk_widget_set_child_visible (self->blank, !some);
  fill_drawer (self);

  /* Which panes are in front of somebody, told only where it changed --
     and told once everything here is settled, from a list of its own:
     what the app does about it may remove a pane or render again. */
  {
    GPtrArray *on = g_ptr_array_new_with_free_func (g_free);
    GPtrArray *off = g_ptr_array_new_with_free_func (g_free);

    for (guint i = 0; i < self->order->len; i++)
      {
        Pane *p = self->order->pdata[i];
        gboolean now = g_hash_table_contains (front, p) &&
                       gtk_widget_get_mapped (GTK_WIDGET (self));

        if (now != p->shown)
          {
            p->shown = now;
            g_ptr_array_add (now ? on : off, g_strdup (p->id));
          }

        /* Looked at, which is what the mark asked for. */
        if (now)
          gtk_widget_remove_css_class (p->tab, "attention");
      }

    g_hash_table_unref (drawn);
    g_hash_table_unref (front);
    gtk_widget_queue_resize (GTK_WIDGET (self));

    g_object_ref (self);

    for (guint i = 0; i < off->len; i++)
      g_signal_emit (self, signals[PANE_SHOWN], 0, off->pdata[i], FALSE);

    for (guint i = 0; i < on->len; i++)
      g_signal_emit (self, signals[PANE_SHOWN], 0, on->pdata[i], TRUE);

    g_object_unref (self);
    g_ptr_array_unref (on);
    g_ptr_array_unref (off);
  }
}

static void
render (MlnPanes *self)
{
  if (mln_model_loaded (self->model))
    sync_children (self);
}


/* ---- dragging a tab ---- */

static gboolean
inside (Box b, double x, double y)
{
  return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}

static gboolean
rtl (MlnPanes *self)
{
  return gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;
}

static Box
bounds_in (MlnPanes *self, GtkWidget *w)
{
  graphene_rect_t r;

  if (!gtk_widget_compute_bounds (w, GTK_WIDGET (self), &r))
    return (Box) { 0, 0, 0, 0 };

  return (Box) { (int) r.origin.x, (int) r.origin.y,
                 (int) r.size.width, (int) r.size.height };
}

static Strip *
strip_at (MlnPanes *self, double x, double y)
{
  for (guint i = 0; i < self->strips->len; i++)
    {
      Strip *s = self->strips->pdata[i];

      if (gtk_widget_get_child_visible (s->strip) && inside (s->box, x, y))
        return s;
    }

  return NULL;
}

static void
drop_clear (Drop *d)
{
  g_clear_pointer (&d->before, g_free);
  g_clear_pointer (&d->leaf, mln_node_unref);
  d->kind = DROP_NONE;
}

/* Where a tab let go at (x, y) lands: the drawer, a place in a strip, a
   leaf's edge, or a leaf. */
static void
under (MlnPanes *self, double x, double y, Drop *d)
{
  const char *id = self->drag_id;
  Strip *s;
  Box box;
  double fx, fy;
  gboolean row, has_side = TRUE;

  drop_clear (d);

  if (gtk_widget_get_child_visible (self->drawer) &&
      inside (bounds_in (self, self->drawer), x, y))
    {
      if (mln_model_closable (self->model, id))
        {
          d->kind = DROP_DRAWER;
          d->hint = bounds_in (self, self->drawer);
        }

      return;
    }

  if ((s = strip_at (self, x, y)) == NULL)
    return;

  d->leaf = mln_node_ref (s->leaf);
  box = s->box;

  /* Over the strip, which is a row of places: before the first tab whose
     middle is past the pointer. The tab being dragged is not a place. */
  if (y < box.y + s->strip_h)
    {
      GtkWidget *last = NULL;
      int at = rtl (self) ? box.x + box.w : box.x;

      d->kind = DROP_TAB;

      for (GtkWidget *t = gtk_widget_get_first_child (s->strip); t != NULL;
           t = gtk_widget_get_next_sibling (t))
        {
          Pane *p = g_object_get_data (G_OBJECT (t), "mln-pane");
          Box tb = bounds_in (self, t);
          double mid = tb.x + tb.w / 2.0;

          if (p == NULL || strcmp (p->id, id) == 0)
            continue;

          if (rtl (self) ? mid < x : mid > x)
            {
              d->before = g_strdup (p->id);
              at = rtl (self) ? tb.x + tb.w : tb.x;
              break;
            }

          last = t;
        }

      if (d->before == NULL && last != NULL)
        {
          Box lb = bounds_in (self, last);

          at = rtl (self) ? lb.x : lb.x + lb.w;
        }

      d->hint = (Box) { at - 1, box.y, 3, s->strip_h };
      return;
    }

  fx = (x - box.x) / box.w;
  fy = (y - box.y) / box.h;

  /* The outer `edge' on each side; the left and right win in corners. */
  if (fx < self->edge)
    { row = TRUE; d->far = FALSE; }
  else if (fx > 1 - self->edge)
    { row = TRUE; d->far = TRUE; }
  else if (fy < self->edge)
    { row = FALSE; d->far = FALSE; }
  else if (fy > 1 - self->edge)
    { row = FALSE; d->far = TRUE; }
  else
    has_side = FALSE;

  if (!has_side ||
      !mln_model_splittable (self->model, s->leaf, id, row ? MLN_ROW : MLN_COL,
                             row ? box.w : box.h))
    {
      d->kind = DROP_INTO;
      d->hint = box;
      return;
    }

  d->kind = DROP_BESIDE;
  d->dir = row ? MLN_ROW : MLN_COL;

  /* `far' is the right or bottom half, which is what is drawn; `after' is
     which side of the leaf it goes in the tree, the other one for a row in
     a direction that reads leftward. */
  d->after = row && rtl (self) ? !d->far : d->far;
  d->hint = row ? (Box) { d->far ? box.x + box.w / 2 : box.x, box.y, box.w / 2, box.h }
                : (Box) { box.x, d->far ? box.y + box.h / 2 : box.y, box.w, box.h / 2 };
}

static void
show_hint (MlnPanes *self)
{
  const char *kind = NULL;

  switch (self->drop.kind)
    {
    case DROP_TAB: kind = "slot"; break;
    case DROP_INTO: kind = "into"; break;
    case DROP_BESIDE: kind = "beside"; break;
    case DROP_DRAWER: kind = "drawer"; break;
    case DROP_NONE: break;
    }

  gtk_widget_set_css_classes (self->hint, kind != NULL ? (const char *[]) { kind, NULL }
                                                      : (const char *[]) { NULL });
  gtk_widget_set_child_visible (self->hint, kind != NULL);
  gtk_widget_queue_allocate (GTK_WIDGET (self));
}

static void
end_drag (MlnPanes *self)
{
  gboolean was = self->dragging;

  self->dragging = FALSE;
  g_clear_pointer (&self->drag_id, g_free);
  drop_clear (&self->drop);
  gtk_widget_set_child_visible (self->hint, FALSE);
  gtk_widget_remove_css_class (GTK_WIDGET (self), "panedrag");

  if (was)
    fill_drawer (self);

  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
on_tab_drag_begin (GtkGestureDrag *g, double x, double y, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (g), "mln-panes");
  Pane *p = data;

  g_free (self->drag_id);
  self->drag_id = g_strdup (p->id);
  self->dragging = FALSE;
}

static void
on_tab_drag_update (GtkGestureDrag *g, double ox, double oy, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (g), "mln-panes");
  Pane *p = data;
  graphene_point_t at;
  double sx, sy;

  if (self->drag_id == NULL || strcmp (self->drag_id, p->id) != 0)
    return;

  /* Five pixels before it is a drag and not a press. */
  if (!self->dragging)
    {
      if (ox * ox + oy * oy < 25)
        return;

      self->dragging = TRUE;
      gtk_widget_add_css_class (GTK_WIDGET (self), "panedrag");

      /* Last, so it is drawn over everything. */
      gtk_widget_insert_before (self->hint, GTK_WIDGET (self), NULL);
      fill_drawer (self);
      gtk_widget_queue_resize (GTK_WIDGET (self));
    }

  gtk_gesture_drag_get_start_point (g, &sx, &sy);

  if (!gtk_widget_compute_point (p->tab, GTK_WIDGET (self),
                                 &GRAPHENE_POINT_INIT (sx + ox, sy + oy), &at))
    return;

  under (self, at.x, at.y, &self->drop);
  show_hint (self);
}

typedef struct
{
  MlnPanes *self;
  char *id;
  Drop drop;
} Landing;

/* The move, after the gesture that asked for it has finished: it can take
   the tab being dragged out of its strip, and a widget unparented under
   its own gesture's handler is a gesture left half done. */
static gboolean
land (gpointer data)
{
  Landing *l = data;
  MlnPanes *self = l->self;
  MlnNode *leaf = l->drop.leaf;

  if (mln_model_has (self->model, l->id) && (leaf == NULL || mln_model_holds (self->model, leaf)))
    {
      switch (l->drop.kind)
        {
        case DROP_DRAWER:
          mln_model_close (self->model, l->id);
          break;
        case DROP_TAB:
          mln_model_drop_tab (self->model, l->id, leaf, l->drop.before);
          break;
        case DROP_INTO:
          mln_model_drop_into (self->model, l->id, leaf);
          break;
        case DROP_BESIDE:
          mln_model_drop_beside (self->model, l->id, leaf, l->drop.dir, l->drop.after);
          break;
        case DROP_NONE:
          break;
        }

      leaf = mln_model_leaf_with (self->model, l->id);

      if (leaf != NULL)
        {
          MlnNode *zoom = mln_model_get_zoom (self->model);

          mln_model_set_focus (self->model, leaf);

          if (zoom != NULL && zoom != leaf)
            mln_model_set_zoom (self->model, NULL);
        }

      render (self);

      if (leaf != NULL)
        gtk_widget_grab_focus (pane_of (self, l->id)->tab);
    }

  drop_clear (&l->drop);
  g_free (l->id);
  g_object_unref (self);
  g_free (l);

  return G_SOURCE_REMOVE;
}

static void
on_tab_drag_end (GtkGestureDrag *g, double ox, double oy, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (g), "mln-panes");
  Landing *l;

  if (!self->dragging || self->drop.kind == DROP_NONE)
    {
      end_drag (self);
      return;
    }

  l = g_new0 (Landing, 1);
  l->self = g_object_ref (self);
  l->id = g_strdup (self->drag_id);
  l->drop = self->drop;
  l->drop.before = g_strdup (self->drop.before);
  mln_node_ref (l->drop.leaf);
  end_drag (self);

  /* A leaf that has left the tree since the pointer last moved is a drop
     on nothing (land asks the model). */
  g_idle_add (land, l);
}

/* ---- the keys ---- */

static void
focus_front (MlnPanes *self, MlnNode *leaf)
{
  GPtrArray *live;
  guint active;

  if (leaf == NULL || !mln_model_holds (self->model, leaf))
    return;

  live = mln_model_live_tabs (self->model, leaf);
  active = mln_node_active (leaf);

  if (active < live->len)
    gtk_widget_grab_focus (pane_of (self, live->pdata[active])->tab);

  g_ptr_array_unref (live);
}

/* Along a strip: moving the focus moves the tab, which is what a tablist
   does when what a tab shows costs nothing to show. */
static gboolean
on_tab_key (GtkEventControllerKey *keys, guint keyval, guint code,
            GdkModifierType state, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (keys), "mln-panes");
  Pane *p = data;
  MlnNode *leaf = mln_model_leaf_with (self->model, p->id);
  GPtrArray *live;
  int at = -1, to, n;

  if (leaf != NULL &&
      (keyval == GDK_KEY_Menu || (keyval == GDK_KEY_F10 && (state & GDK_SHIFT_MASK))))
    {
      graphene_rect_t r;

      if (gtk_widget_compute_bounds (p->tab, GTK_WIDGET (self), &r))
        open_menu (self, p->id, r.origin.x + r.size.width / 2,
                   r.origin.y + r.size.height);

      return TRUE;
    }

  if (leaf == NULL || (state & (GDK_ALT_MASK | GDK_CONTROL_MASK | GDK_SHIFT_MASK |
                                GDK_SUPER_MASK | GDK_META_MASK)))
    return FALSE;

  live = mln_model_live_tabs (self->model, leaf);
  n = (int) live->len;

  for (int i = 0; i < n; i++)
    if (strcmp (live->pdata[i], p->id) == 0)
      at = i;

  switch (keyval)
    {
    case GDK_KEY_Left:  to = rtl (self) ? at + 1 : at - 1; break;
    case GDK_KEY_Right: to = rtl (self) ? at - 1 : at + 1; break;
    case GDK_KEY_Home:  to = 0; break;
    case GDK_KEY_End:   to = n - 1; break;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: case GDK_KEY_space: to = at; break;
    default:
      g_ptr_array_unref (live);
      return FALSE;
    }

  to = (to % n + n) % n;
  mln_model_set_focus (self->model, leaf);
  mln_model_raise (self->model, leaf, (guint) to);
  render (self);
  gtk_widget_grab_focus (pane_of (self, live->pdata[to])->tab);
  g_ptr_array_unref (live);

  return TRUE;
}

/* The leaf a command is about: the one the focus is in, or the one last
   pressed in, or the first there is. */
static MlnNode *
current (MlnPanes *self)
{
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));
  GtkWidget *at = root != NULL ? gtk_root_get_focus (root) : NULL;
  MlnNode *focus = mln_model_get_focus (self->model);

  for (; at != NULL && at != GTK_WIDGET (self); at = gtk_widget_get_parent (at))
    {
      Pane *p = g_object_get_data (G_OBJECT (at), "mln-pane");

      if (p != NULL)
        return mln_model_leaf_with (self->model, p->id);

      for (guint i = 0; i < self->strips->len; i++)
        if (((Strip *) self->strips->pdata[i])->strip == at)
          return ((Strip *) self->strips->pdata[i])->leaf;
    }

  if (focus != NULL && strip_for (self, focus) != NULL)
    return focus;

  return mln_model_first_leaf (self->model);
}

/* The leaf that way: of the ones whose middle lies in the direction the
   arrow points, inside a 45 degree cone, the nearest. */
static MlnNode *
toward (MlnPanes *self, MlnNode *leaf, int dx, int dy)
{
  Strip *here = strip_for (self, leaf);
  MlnNode *best = NULL;
  double cx, cy, near = G_MAXDOUBLE;

  if (mln_model_get_zoom (self->model) != NULL || here == NULL)
    return NULL;

  cx = here->box.x + here->box.w / 2.0;
  cy = here->box.y + here->box.h / 2.0;

  for (guint i = 0; i < self->strips->len; i++)
    {
      Strip *s = self->strips->pdata[i];
      double x = s->box.x + s->box.w / 2.0 - cx;
      double y = s->box.y + s->box.h / 2.0 - cy;

      if (s->leaf == leaf)
        continue;

      if (dx != 0 && ((x > 0 ? 1 : x < 0 ? -1 : 0) != dx || fabs (x) < fabs (y)))
        continue;

      if (dy != 0 && ((y > 0 ? 1 : y < 0 ? -1 : 0) != dy || fabs (y) < fabs (x)))
        continue;

      if (hypot (x, y) < near)
        {
          near = hypot (x, y);
          best = s->leaf;
        }
    }

  return best;
}

static gboolean
editing (GtkWidget *w)
{
  for (; w != NULL; w = gtk_widget_get_parent (w))
    if (GTK_IS_EDITABLE (w) || GTK_IS_TEXT_VIEW (w))
      return TRUE;

  return FALSE;
}

/*
 * Whether the key pressed is `want', by where it is and not by what it
 * types, as mullion matches KeyboardEvent.code: Alt over a letter is a
 * different letter on half the layouts there are. The key's own keyval
 * first, then what the same key types in each group (layout) at its
 * first level -- so Alt W is found on a Cyrillic layout that has a Latin
 * group beside it.
 */
static gboolean
chord_is (GtkEventControllerKey *keys, guint keyval, guint code, guint want)
{
  GdkDisplay *display;
  GdkKeymapKey *mapped = NULL;
  guint *keyvals = NULL;
  int n = 0;
  gboolean found = FALSE;

  if (gdk_keyval_to_lower (keyval) == want)
    return TRUE;

  display = gtk_widget_get_display (gtk_event_controller_get_widget (GTK_EVENT_CONTROLLER (keys)));

  if (!gdk_display_map_keycode (display, code, &mapped, &keyvals, &n))
    return FALSE;

  for (int i = 0; i < n && !found; i++)
    if (mapped[i].level == 0 && gdk_keyval_to_lower (keyvals[i]) == want)
      found = TRUE;

  g_free (mapped);
  g_free (keyvals);

  return found;
}

static void
done (MlnPanes *self, MlnNode *leaf)
{
  mln_model_set_focus (self->model, leaf);
  render (self);
  focus_front (self, leaf);
}

/*
 * mullion's chords: Alt with an arrow moves the focus to the leaf that
 * way, Alt Shift with one moves the pane in front there (or off the edge
 * into a half of its own); Alt \ and Alt - split the pane in front off
 * to the right and below; Alt Enter fills the layout with one leaf; Alt
 * W closes; Alt 0 goes back to the default. Never while the focus is
 * somewhere text is typed.
 */
static gboolean
on_key (GtkEventControllerKey *keys, guint keyval, guint code,
        GdkModifierType state, gpointer data)
{
  MlnPanes *self = data;
  GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (self));
  MlnNode *leaf, *to;
  GPtrArray *live;
  const char *id;
  int dx = 0, dy = 0;
  gboolean shift = (state & GDK_SHIFT_MASK) != 0;
  gboolean used = TRUE;

  if (self->dragging && keyval == GDK_KEY_Escape)
    {
      end_drag (self);
      return TRUE;
    }

  if (!(state & GDK_ALT_MASK) ||
      (state & (GDK_CONTROL_MASK | GDK_SUPER_MASK | GDK_META_MASK)) ||
      !mln_model_loaded (self->model) ||
      (root != NULL && editing (gtk_root_get_focus (root))))
    return FALSE;

  leaf = current (self);

  if (leaf == NULL)
    return FALSE;

  live = mln_model_live_tabs (self->model, leaf);
  id = mln_node_active (leaf) < live->len ? live->pdata[mln_node_active (leaf)] : NULL;

  switch (keyval)
    {
    case GDK_KEY_Left:  dx = -1; break;
    case GDK_KEY_Right: dx = 1; break;
    case GDK_KEY_Up:    dy = -1; break;
    case GDK_KEY_Down:  dy = 1; break;
    default: break;
    }

  if ((dx || dy) && !shift)
    {
      if ((to = toward (self, leaf, dx, dy)) != NULL)
        done (self, to);
    }
  else if (dx || dy)
    {
      MlnDir dir = dx ? MLN_ROW : MLN_COL;
      Strip *s = strip_for (self, leaf);

      to = toward (self, leaf, dx, dy);

      if (id == NULL ||
          (to == NULL && (s == NULL ||
                          !mln_model_splittable (self->model, leaf, id, dir,
                                                 dx ? s->box.w : s->box.h))))
        used = FALSE;
      else
        {
          char *mine = g_strdup (id);
          MlnNode *zoom;

          if (to != NULL)
            mln_model_drop_into (self->model, mine, to);
          else
            mln_model_drop_beside (self->model, mine, leaf, dir,
                                   dy > 0 || (dx != 0 && (dx > 0) != rtl (self)));

          to = mln_model_leaf_with (self->model, mine);
          zoom = mln_model_get_zoom (self->model);

          if (zoom != NULL && zoom != to)
            mln_model_set_zoom (self->model, NULL);

          done (self, to);
          g_free (mine);
        }
    }
  else if (chord_is (keys, keyval, code, GDK_KEY_backslash) ||
           chord_is (keys, keyval, code, GDK_KEY_minus))
    {
      /* The pane in front, off into a half of its own; in a leaf with
         nothing else in it, the first pane in the drawer there instead. */
      MlnDir dir = chord_is (keys, keyval, code, GDK_KEY_backslash) ? MLN_ROW : MLN_COL;
      GPtrArray *closed = mln_model_closed (self->model);
      char *moving = g_strdup (live->len > 1 ? id
                               : closed->len > 0 ? closed->pdata[0] : NULL);
      Strip *s = strip_for (self, leaf);

      if (moving == NULL || s == NULL ||
          !mln_model_splittable (self->model, leaf, moving, dir,
                                 dir == MLN_ROW ? s->box.w : s->box.h))
        used = FALSE;
      else
        {
          mln_model_drop_beside (self->model, moving, leaf, dir, TRUE);
          done (self, leaf);
        }

      g_free (moving);
      g_ptr_array_unref (closed);
    }
  else if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)
    {
      mln_model_set_zoom (self->model, mln_model_get_zoom (self->model) == NULL ? leaf : NULL);
      render (self);
      focus_front (self, leaf);
    }
  else if (chord_is (keys, keyval, code, GDK_KEY_w) && id != NULL)
    {
      /* Onto a leaf still in the tree: this one if it kept anything. */
      if (mln_model_close (self->model, id))
        done (self, mln_model_holds (self->model, leaf) ? leaf
                                                       : mln_model_first_leaf (self->model));
    }
  else if (chord_is (keys, keyval, code, GDK_KEY_0))
    {
      mln_model_reset (self->model);
      render (self);
      focus_front (self, mln_model_first_leaf (self->model));
    }
  else
    used = FALSE;

  g_ptr_array_unref (live);

  return used;
}


/* ---- a tab's menu ---- */

/*
 * What a tab's menu offers: what the chords do, for the pane it is on,
 * and only what applies to it -- no Split in a leaf with nothing to split
 * off, no Close for a pane that cannot be -- then the app's items for the
 * pane, then Reset Layout. Built each time it opens, since what applies
 * changes with every move.
 */

static void
act_on_pane (GtkWidget *w, const char *name, GVariant *param)
{
  MlnPanes *self = MLN_PANES (w);
  const char *id = g_variant_get_string (param, NULL);
  char *mine = g_strdup (id);
  MlnNode *leaf = mln_model_leaf_with (self->model, mine);

  if (strcmp (name, "panes.close") == 0)
    {
      if (mln_model_close (self->model, mine))
        done (self, leaf != NULL && mln_model_holds (self->model, leaf)
                    ? leaf : mln_model_first_leaf (self->model));
    }
  else if (leaf != NULL && (strcmp (name, "panes.split-right") == 0 ||
                            strcmp (name, "panes.split-down") == 0))
    {
      mln_model_drop_beside (self->model, mine, leaf,
                             strcmp (name, "panes.split-right") == 0 ? MLN_ROW : MLN_COL,
                             TRUE);
      done (self, mln_model_leaf_with (self->model, mine));
    }
  else if (leaf != NULL && strcmp (name, "panes.zoom") == 0)
    {
      mln_model_set_zoom (self->model, mln_model_get_zoom (self->model) == leaf ? NULL : leaf);
      render (self);
      focus_front (self, leaf);
    }

  g_free (mine);
}

static void
act_reset (GtkWidget *w, const char *name, GVariant *param)
{
  MlnPanes *self = MLN_PANES (w);

  mln_model_reset (self->model);
  render (self);
}

static void
menu_closed (GtkPopover *popover, gpointer data)
{
  MlnPanes *self = data;

  g_clear_pointer (&self->menu_id, g_free);
}

static void
open_menu (MlnPanes *self, const char *id, double x, double y)
{
  Pane *p = pane_of (self, id);
  MlnNode *leaf = mln_model_leaf_with (self->model, id);
  GMenu *menu = g_menu_new (), *layout = g_menu_new (), *end = g_menu_new ();
  GPtrArray *live;
  Strip *s;
  char *target;

  if (p == NULL || leaf == NULL)
    return;

  live = mln_model_live_tabs (self->model, leaf);
  s = strip_for (self, leaf);
  target = g_strdup_printf ("::%s", id);

#define ITEM(label, action)                                             \
  G_STMT_START {                                                        \
    char *detailed = g_strconcat (action, target, NULL);                \
    g_menu_append (layout, label, detailed);                            \
    g_free (detailed);                                                  \
  } G_STMT_END

  /* A pane alone in its leaf has nothing to split off, and a split that
     would leave either half under its minimum is refused. */
  if (live->len > 1 && s != NULL)
    {
      if (mln_model_splittable (self->model, leaf, id, MLN_ROW, s->box.w))
        ITEM ("Split _Right", "panes.split-right");

      if (mln_model_splittable (self->model, leaf, id, MLN_COL, s->box.h))
        ITEM ("Split _Down", "panes.split-down");
    }

  ITEM (mln_model_get_zoom (self->model) == leaf ? "_Unzoom" : "_Zoom", "panes.zoom");

  if (mln_model_closable (self->model, id))
    ITEM ("_Close", "panes.close");

#undef ITEM

  g_menu_append_section (menu, NULL, G_MENU_MODEL (layout));

  if (p->menu != NULL)
    g_menu_append_section (menu, NULL, p->menu);

  g_menu_append (end, "Reset _Layout", "panes.reset");
  g_menu_append_section (menu, NULL, G_MENU_MODEL (end));

  /* One open at a time, parented to MlnPanes, which presents it again
     when it is allocated; the app's items find the app's actions from
     here as from the tab. */
  if (self->menu != NULL)
    gtk_popover_popdown (GTK_POPOVER (self->menu));

  g_clear_pointer (&self->menu, gtk_widget_unparent);
  self->menu = gtk_popover_menu_new_from_model (G_MENU_MODEL (menu));
  gtk_widget_set_parent (self->menu, GTK_WIDGET (self));
  gtk_popover_set_has_arrow (GTK_POPOVER (self->menu), FALSE);
  gtk_popover_set_pointing_to (GTK_POPOVER (self->menu),
                               &(GdkRectangle) { (int) x, (int) y, 1, 1 });

  g_free (self->menu_id);
  self->menu_id = g_strdup (id);
  g_signal_connect (self->menu, "closed", G_CALLBACK (menu_closed), self);
  gtk_popover_popup (GTK_POPOVER (self->menu));

  g_ptr_array_unref (live);
  g_free (target);
  g_object_unref (menu);
  g_object_unref (layout);
  g_object_unref (end);
}

/* ---- laying out ---- */

static int
strip_height (MlnPanes *self, Strip *s)
{
  int nat = 0;

  gtk_widget_measure (s->strip, GTK_ORIENTATION_VERTICAL, -1, NULL, &nat, NULL, NULL);

  return nat;
}

static void
place (GtkWidget *w, Box b)
{
  GskTransform *t = gsk_transform_translate (NULL, &GRAPHENE_POINT_INIT (b.x, b.y));

  gtk_widget_allocate (w, MAX (b.w, 0), MAX (b.h, 0), -1, t);
}

static void allocate_node (MlnPanes *self, MlnNode *node, Box box);

static void
allocate_leaf (MlnPanes *self, MlnNode *leaf, Box box)
{
  Strip *s = strip_for (self, leaf);
  GPtrArray *live = mln_model_live_tabs (self->model, leaf);
  guint active = mln_node_active (leaf);
  int sh = s != NULL ? strip_height (self, s) : 0;

  if (s != NULL)
    {
      int mw = 0;

      gtk_widget_measure (s->strip, GTK_ORIENTATION_HORIZONTAL, -1, &mw, NULL, NULL, NULL);
      place (s->strip, (Box) { box.x, box.y, MAX (box.w, mw), sh });
      s->box = box;
      s->strip_h = sh;
    }

  if (active < live->len)
    {
      Pane *p = pane_of (self, live->pdata[active]);

      place (p->host, (Box) { box.x, box.y + sh, box.w, box.h - sh });
    }

  g_ptr_array_unref (live);
}

/*
 * A split's children, by their shares of what the live ones have, then
 * held to their minimums: a child under its floor gets the floor and the
 * others give up what that costs, in proportion to what they have over
 * theirs. What the browser does with flex-grow and min-width, done here.
 */
static void
allocate_split (MlnPanes *self, MlnNode *split, Box box)
{
  gboolean row = row_of (split);
  guint n = mln_node_n_kids (split);
  double *want = g_newa (double, n);
  double *floor_ = g_newa (double, n);
  gboolean *live = g_newa (gboolean, n);
  double total = 0, room, over = 0, short_ = 0;
  guint count = 0;
  int at;

  for (guint i = 0; i < n; i++)
    {
      live[i] = mln_model_alive (self->model, mln_node_kid (split, i));

      if (live[i])
        {
          total += mln_node_size (split, i);
          count++;
        }
    }

  if (count == 0)
    return;

  room = (row ? box.w : box.h) - (double) (count - 1) * self->split;

  for (guint i = 0; i < n; i++)
    {
      if (!live[i])
        continue;

      want[i] = room * mln_node_size (split, i) / total;
      floor_[i] = MAX (0, mln_model_min_across (self->model, mln_node_kid (split, i), row));

      if (want[i] < floor_[i])
        short_ += floor_[i] - want[i];
      else
        over += want[i] - floor_[i];
    }

  if (short_ > 0 && over > 0)
    for (guint i = 0; i < n; i++)
      {
        if (!live[i])
          continue;

        if (want[i] < floor_[i])
          want[i] = floor_[i];
        else
          want[i] -= (want[i] - floor_[i]) / over * MIN (short_, over);
      }

  /* In a row that reads right to left the first child is on the right,
     as mullion's flex row puts it, and as the drops, the dividers and the
     chords all take it to be. */
  {
    gboolean mirrored = row && gtk_widget_get_direction (GTK_WIDGET (self)) == GTK_TEXT_DIR_RTL;

    at = row ? box.x : box.y;

    if (mirrored)
      at = box.x + box.w;

  {
    double carry = 0;
    int prev = -1;

    for (guint i = 0; i < n; i++)
      {
        int size;
        Box kid;

        if (!live[i])
          continue;

        if (prev >= 0)
          {
            Divider *d = divider_at (self, split, (guint) prev);

            if (mirrored)
              at -= self->split;

            if (d != NULL)
              {
                place (d->bar, row ? (Box) { at, box.y, self->split, box.h }
                                   : (Box) { box.x, at, box.w, self->split });
              }

            if (!mirrored)
              at += self->split;
          }

        /* Whole pixels, the remainder carried so the children fill the
           box exactly. */
        carry += want[i];
        size = (int) (carry + 0.5);
        carry -= size;

        if (mirrored)
          at -= size;

        kid = row ? (Box) { at, box.y, size, box.h } : (Box) { box.x, at, box.w, size };
        allocate_node (self, mln_node_kid (split, i), kid);

        /* What each divider's drag measures against: the child before it
           and the two together. */
        if (prev >= 0)
          {
            Divider *d = divider_at (self, split, (guint) prev);

            if (d != NULL)
              {
                GArray *sides = g_object_get_data (G_OBJECT (d->bar), "mln-sides");
                double before = g_array_index (sides, double, 0);

                g_array_index (sides, double, 1) = before + size;

                /* Where it is, as mullion's separator says: the first
                   side's share of the two, in percent. */
                if (before + size > 0)
                  gtk_accessible_update_property (GTK_ACCESSIBLE (d->bar),
                                                  GTK_ACCESSIBLE_PROPERTY_VALUE_NOW,
                                                  100.0 * before / (before + size), -1);
              }
          }

        {
          Divider *next = divider_at (self, split, i);

          if (next != NULL)
            {
              GArray *sides = g_object_get_data (G_OBJECT (next->bar), "mln-sides");

              g_array_index (sides, double, 0) = size;
            }
        }

        if (!mirrored)
          at += size;

        prev = (int) i;
      }
  }
  }
}

static void
allocate_node (MlnPanes *self, MlnNode *node, Box box)
{
  if (mln_node_is_leaf (node))
    allocate_leaf (self, node, box);
  else
    {
      /* A split with one live child is drawn as that child. */
      guint live = 0, only = 0;

      for (guint i = 0; i < mln_node_n_kids (node); i++)
        if (mln_model_alive (self->model, mln_node_kid (node, i)))
          {
            live++;
            only = i;
          }

      if (live == 1)
        allocate_node (self, mln_node_kid (node, only), box);
      else
        allocate_split (self, node, box);
    }
}

static int
drawer_height (MlnPanes *self)
{
  int nat = 0;

  if (!gtk_widget_get_child_visible (self->drawer))
    return 0;

  gtk_widget_measure (self->drawer, GTK_ORIENTATION_VERTICAL, -1, NULL, &nat, NULL, NULL);

  return nat;
}

static void
mln_panes_measure (GtkWidget *w, GtkOrientation o, int for_size,
                   int *min, int *nat, int *min_base, int *nat_base)
{
  MlnPanes *self = MLN_PANES (w);
  MlnNode *tree = mln_model_tree (self->model);
  gboolean row = o == GTK_ORIENTATION_HORIZONTAL;
  double least = 0;

  if (tree != NULL && mln_model_alive (self->model, tree))
    least = mln_model_min_across (self->model, tree, row);

  *min = (int) MAX (0, least);

  if (!row)
    *min += drawer_height (self);

  *nat = MAX (*min, row ? 640 : 400);
}

static void
mln_panes_size_allocate (GtkWidget *w, int width, int height, int baseline)
{
  MlnPanes *self = MLN_PANES (w);
  MlnNode *tree = mln_model_tree (self->model);
  MlnNode *zoom = mln_model_get_zoom (self->model);
  int top = drawer_height (self);
  Box box = { 0, top, width, height - top };

  if (gtk_widget_get_child_visible (self->drawer))
    place (self->drawer, (Box) { 0, 0, width, top });

  if (gtk_widget_get_child_visible (self->hint))
    place (self->hint, self->drop.hint);

  if (self->menu != NULL && gtk_widget_get_visible (self->menu))
    gtk_popover_present (GTK_POPOVER (self->menu));

  if (tree == NULL || !mln_model_alive (self->model, tree))
    {
      place (self->blank, box);
      return;
    }

  allocate_node (self, zoom != NULL ? zoom : tree, box);
}

/* ---- GObject ---- */

static void
pane_free (Pane *p)
{
  g_clear_object (&p->menu);
  g_free (p->id);
  g_free (p->title);
  g_free (p);
}

static void
mln_panes_dispose (GObject *o)
{
  MlnPanes *self = MLN_PANES (o);

  g_clear_handle_id (&self->render_idle, g_source_remove);

  /* The menu first: it is a child of this widget and can name a pane. */
  g_clear_pointer (&self->menu, gtk_widget_unparent);

  if (self->strips != NULL)
    {
      g_ptr_array_free (self->strips, TRUE);
      self->strips = NULL;
    }

  if (self->dividers != NULL)
    {
      g_ptr_array_free (self->dividers, TRUE);
      self->dividers = NULL;
    }

  if (self->order != NULL)
    {
      for (guint i = 0; i < self->order->len; i++)
        {
          Pane *p = self->order->pdata[i];

          g_clear_object (&p->tab);
          g_clear_pointer (&p->host, gtk_widget_unparent);
        }
    }

  g_clear_pointer (&self->drawer, gtk_widget_unparent);
  g_clear_pointer (&self->blank, gtk_widget_unparent);
  g_clear_pointer (&self->hint, gtk_widget_unparent);
  g_clear_pointer (&self->menu, gtk_widget_unparent);
  g_clear_pointer (&self->menu_id, g_free);
  g_clear_pointer (&self->drag_id, g_free);
  drop_clear (&self->drop);

  if (self->later_destroy != NULL)
    self->later_destroy (self->later_data);

  self->later_destroy = NULL;
  self->later = NULL;

  G_OBJECT_CLASS (mln_panes_parent_class)->dispose (o);
}

static void
mln_panes_finalize (GObject *o)
{
  MlnPanes *self = MLN_PANES (o);

  mln_model_free (self->model);
  g_hash_table_unref (self->panes);
  g_ptr_array_free (self->order, TRUE);

  G_OBJECT_CLASS (mln_panes_parent_class)->finalize (o);
}

static void
mln_panes_get_property (GObject *o, guint id, GValue *v, GParamSpec *spec)
{
  MlnPanes *self = MLN_PANES (o);

  switch (id)
    {
    case PROP_SPLIT: g_value_set_int (v, self->split); break;
    case PROP_LEAF_MIN_HEIGHT: g_value_set_int (v, self->least); break;
    case PROP_EDGE: g_value_set_double (v, self->edge); break;
    case PROP_SHOW_DRAWER: g_value_set_boolean (v, self->show_drawer); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, spec);
    }
}

static void
mln_panes_set_property (GObject *o, guint id, const GValue *v, GParamSpec *spec)
{
  MlnPanes *self = MLN_PANES (o);

  switch (id)
    {
    case PROP_SPLIT:
      self->split = g_value_get_int (v);
      mln_model_set_split (self->model, self->split);
      break;
    case PROP_LEAF_MIN_HEIGHT:
      self->least = g_value_get_int (v);
      mln_model_set_leaf (self->model, self->least);
      break;
    case PROP_EDGE:
      self->edge = g_value_get_double (v);
      break;
    case PROP_SHOW_DRAWER:
      self->show_drawer = g_value_get_boolean (v);
      render (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (o, id, spec);
      return;
    }

  g_object_notify_by_pspec (o, spec);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

/* A display other than the default one, which mln_panes_init styled. Too
   late for the styles already computed on the way to realizing, which is
   why the default display is done at construction. */
static void
mln_panes_realize (GtkWidget *w)
{
  add_style (gtk_widget_get_display (w));
  GTK_WIDGET_CLASS (mln_panes_parent_class)->realize (w);
}

static void
mln_panes_map (GtkWidget *w)
{
  GTK_WIDGET_CLASS (mln_panes_parent_class)->map (w);
  render (MLN_PANES (w));
}

static void
mln_panes_unmap (GtkWidget *w)
{
  MlnPanes *self = MLN_PANES (w);

  GTK_WIDGET_CLASS (mln_panes_parent_class)->unmap (w);

  /* Out of sight altogether: every pane that was shown is told. */
  for (guint i = 0; i < self->order->len; i++)
    {
      Pane *p = self->order->pdata[i];

      if (p->shown)
        {
          p->shown = FALSE;
          g_signal_emit (self, signals[PANE_SHOWN], 0, p->id, FALSE);
        }
    }
}

static void
mln_panes_class_init (MlnPanesClass *klass)
{
  GObjectClass *oc = G_OBJECT_CLASS (klass);
  GtkWidgetClass *wc = GTK_WIDGET_CLASS (klass);

  oc->dispose = mln_panes_dispose;
  oc->finalize = mln_panes_finalize;
  oc->get_property = mln_panes_get_property;
  oc->set_property = mln_panes_set_property;

  wc->measure = mln_panes_measure;
  wc->size_allocate = mln_panes_size_allocate;
  wc->realize = mln_panes_realize;
  wc->map = mln_panes_map;
  wc->unmap = mln_panes_unmap;

  props[PROP_SPLIT] =
    g_param_spec_int ("split", NULL, NULL, 1, 64, 6,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_LEAF_MIN_HEIGHT] =
    g_param_spec_int ("leaf-min-height", NULL, NULL, 0, 4096, 64,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_EDGE] =
    g_param_spec_double ("edge", NULL, NULL, 0, 0.5, 0.2,
                         G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  props[PROP_SHOW_DRAWER] =
    g_param_spec_boolean ("show-drawer", NULL, NULL, TRUE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_properties (oc, N_PROPS, props);

  signals[PANE_SHOWN] =
    g_signal_new ("pane-shown", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_BOOLEAN);
  signals[LAYOUT_CHANGED] =
    g_signal_new ("layout-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
  signals[LAYOUT_KEPT] =
    g_signal_new ("layout-kept", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
  signals[PANE_DISCARD] =
    g_signal_new ("pane-discard", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);

  gtk_widget_class_set_css_name (wc, "panes");

  /* What a tab's menu does, and what an app can do too, by name:
     gtk_widget_activate_action (panes, "panes.close", "s", id). */
  gtk_widget_class_install_action (wc, "panes.close", "s", act_on_pane);
  gtk_widget_class_install_action (wc, "panes.split-right", "s", act_on_pane);
  gtk_widget_class_install_action (wc, "panes.split-down", "s", act_on_pane);
  gtk_widget_class_install_action (wc, "panes.zoom", "s", act_on_pane);
  gtk_widget_class_install_action (wc, "panes.reset", NULL, act_reset);
}

static void
mln_panes_init (MlnPanes *self)
{
  MlnModelHooks hooks = { hook_later, hook_discard, hook_store, hook_changed };

  self->model = mln_model_new (&hooks, self);
  self->panes = g_hash_table_new (g_str_hash, g_str_equal);
  self->order = g_ptr_array_new_with_free_func ((GDestroyNotify) pane_free);
  self->strips = g_ptr_array_new_with_free_func ((GDestroyNotify) strip_free);
  self->dividers = g_ptr_array_new_with_free_func ((GDestroyNotify) divider_free);
  self->split = 6;
  self->least = 64;
  self->edge = 0.2;
  self->show_drawer = TRUE;

  mln_model_set_split (self->model, self->split);
  mln_model_set_leaf (self->model, self->least);

  if (gdk_display_get_default () != NULL)
    add_style (gdk_display_get_default ());

  self->drawer = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_add_css_class (self->drawer, "panedrawer");
  gtk_widget_set_parent (self->drawer, GTK_WIDGET (self));
  gtk_widget_set_child_visible (self->drawer, FALSE);

  self->hint = g_object_new (GTK_TYPE_BOX, "css-name", "panedrop", "can-target", FALSE, NULL);
  gtk_widget_set_parent (self->hint, GTK_WIDGET (self));
  gtk_widget_set_child_visible (self->hint, FALSE);

  {
    GtkEventController *keys = gtk_event_controller_key_new ();

    /* After the content, as mullion listens on the window after the
       page: a terminal that uses Alt with an arrow keeps it. */
    gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_BUBBLE);
    g_signal_connect (keys, "key-pressed", G_CALLBACK (on_key), self);
    gtk_widget_add_controller (GTK_WIDGET (self), keys);
  }

  self->blank = gtk_label_new ("Every pane is closed. Reopen one from the row above.");
  gtk_widget_add_css_class (self->blank, "dim-label");
  gtk_widget_set_parent (self->blank, GTK_WIDGET (self));
  gtk_widget_set_child_visible (self->blank, FALSE);

  gtk_widget_set_overflow (GTK_WIDGET (self), GTK_OVERFLOW_HIDDEN);
}

/* ---- the API ---- */

GtkWidget *
mln_panes_new (void)
{
  return g_object_new (MLN_TYPE_PANES, NULL);
}

static Pane *
take_on (MlnPanes *self, const char *id, const char *title, GtkWidget *content)
{
  Pane *p = g_new0 (Pane, 1);

  p->id = g_strdup (id);
  p->title = g_strdup (title != NULL ? title : id);
  p->host = mln_host_new (content);
  g_object_set_data (G_OBJECT (p->host), "mln-pane", p);
  gtk_widget_set_parent (p->host, GTK_WIDGET (self));
  gtk_widget_set_child_visible (p->host, FALSE);
  p->tab = make_tab (self, p);
  g_object_set_data (G_OBJECT (p->tab), "mln-pane", p);

  g_hash_table_insert (self->panes, p->id, p);
  g_ptr_array_add (self->order, p);

  return p;
}

gboolean
mln_panes_register (MlnPanes *self, const char *id, const char *title,
                    GtkWidget *content, int min_width)
{
  g_return_val_if_fail (MLN_IS_PANES (self), FALSE);
  g_return_val_if_fail (GTK_IS_WIDGET (content), FALSE);

  if (!mln_model_register (self->model, id, min_width))
    return FALSE;

  take_on (self, id, title, content);
  render (self);

  return TRUE;
}

gboolean
mln_panes_add (MlnPanes *self, const char *id, const char *title,
               GtkWidget *content, int min_width, MlnPaneFlags flags,
               const char *near)
{
  gboolean added = FALSE;

  g_return_val_if_fail (MLN_IS_PANES (self), FALSE);
  g_return_val_if_fail (GTK_IS_WIDGET (content), FALSE);

  if (pane_of (self, id) != NULL)
    return FALSE;

  /* Taken on before the model places it, so that the render that follows
     finds its widgets. */
  take_on (self, id, title, content);
  mln_model_add (self->model, id, min_width, !(flags & MLN_PANE_EPHEMERAL), near,
                 !(flags & MLN_PANE_QUIET), &added);
  render (self);

  return added;
}

GtkWidget *
mln_panes_remove (MlnPanes *self, const char *id)
{
  Pane *p = pane_of (self, id);
  MlnHost *host;
  GtkWidget *content;

  g_return_val_if_fail (MLN_IS_PANES (self), NULL);

  if (p == NULL)
    return NULL;

  /* Out of the tables first, so that nothing the app does when it is told
     below can find it again. */
  g_hash_table_steal (self->panes, p->id);

  {
    guint at;

    if (g_ptr_array_find (self->order, p, &at))
      g_ptr_array_steal_index (self->order, at);
  }

  mln_model_remove (self->model, p->id);

  if (gtk_widget_get_parent (p->tab) != NULL)
    gtk_box_remove (GTK_BOX (gtk_widget_get_parent (p->tab)), p->tab);

  /* Its menu, if it is open, is about a pane there no longer is. */
  if (self->menu != NULL && g_strcmp0 (self->menu_id, id) == 0)
    gtk_popover_popdown (GTK_POPOVER (self->menu));

  host = MLN_HOST (p->host);
  content = g_object_ref (host->content);
  gtk_widget_unparent (host->content);
  host->content = NULL;
  gtk_widget_unparent (p->host);
  g_clear_object (&p->tab);
  render (self);

  if (p->shown)
    g_signal_emit (self, signals[PANE_SHOWN], 0, p->id, FALSE);

  pane_free (p);

  return content;
}

GtkWidget *
mln_panes_get_content (MlnPanes *self, const char *id)
{
  Pane *p = pane_of (self, id);

  return p != NULL ? MLN_HOST (p->host)->content : NULL;
}

void
mln_panes_set_title (MlnPanes *self, const char *id, const char *title)
{
  Pane *p = pane_of (self, id);

  if (p == NULL)
    return;

  g_free (p->title);
  p->title = g_strdup (title);
  gtk_label_set_text (GTK_LABEL (p->label), title);

  {
    char *name = g_strdup_printf ("Close %s", title);

    gtk_accessible_update_property (GTK_ACCESSIBLE (p->shut),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, name, -1);
    g_free (name);
  }

  render (self);
}

void
mln_panes_set_available (MlnPanes *self, const char *id, gboolean available)
{
  mln_model_set_available (self->model, id, available);
  render (self);
}

void
mln_panes_set_attention (MlnPanes *self, const char *id, gboolean attention)
{
  Pane *p = pane_of (self, id);

  if (p == NULL)
    return;

  /* A pane in view is being looked at already. */
  if (attention && !p->shown)
    gtk_widget_add_css_class (p->tab, "attention");
  else
    gtk_widget_remove_css_class (p->tab, "attention");
}

void
mln_panes_set_pane_menu (MlnPanes *self, const char *id, GMenuModel *menu)
{
  Pane *p = pane_of (self, id);

  if (p == NULL)
    return;

  g_set_object (&p->menu, menu);
}

void
mln_panes_set_later_func (MlnPanes *self, MlnLaterFunc func, gpointer data,
                          GDestroyNotify destroy)
{
  if (self->later_destroy != NULL)
    self->later_destroy (self->later_data);

  self->later = func;
  self->later_data = data;
  self->later_destroy = destroy;
}

void
mln_panes_present (MlnPanes *self, const char *id, gboolean focus)
{
  Pane *p = pane_of (self, id);

  if (p == NULL || mln_model_present (self->model, id, NULL, NULL) == NULL)
    return;

  render (self);

  if (focus)
    gtk_widget_grab_focus (p->tab);
}

void
mln_panes_close (MlnPanes *self, const char *id)
{
  if (mln_model_close (self->model, id))
    render (self);
}

gboolean
mln_panes_is_visible (MlnPanes *self, const char *id)
{
  Pane *p = pane_of (self, id);

  return p != NULL && p->shown;
}

gboolean
mln_panes_get_tab_bounds (MlnPanes *self, const char *id, graphene_rect_t *bounds)
{
  Pane *p = pane_of (self, id);

  return p != NULL && gtk_widget_get_parent (p->tab) != NULL &&
         gtk_widget_get_child_visible (gtk_widget_get_parent (p->tab)) &&
         gtk_widget_compute_bounds (p->tab, GTK_WIDGET (self), bounds);
}

gboolean
mln_panes_get_leaf_bounds (MlnPanes *self, const char *id, graphene_rect_t *bounds)
{
  MlnNode *leaf = mln_model_leaf_with (self->model, id);
  Strip *s = leaf != NULL ? strip_for (self, leaf) : NULL;

  if (s == NULL || !gtk_widget_get_child_visible (s->strip))
    return FALSE;

  graphene_rect_init (bounds, s->box.x, s->box.y, s->box.w, s->box.h);

  return TRUE;
}

gboolean
mln_panes_get_closed_bounds (MlnPanes *self, const char *id, graphene_rect_t *bounds)
{
  GtkWidget *b = reopen_button (self, id);

  return b != NULL && gtk_widget_compute_bounds (b, GTK_WIDGET (self), bounds);
}

char **
mln_panes_get_closed (MlnPanes *self)
{
  GPtrArray *closed = mln_model_closed (self->model);
  GStrvBuilder *b = g_strv_builder_new ();
  char **out;

  for (guint i = 0; i < closed->len; i++)
    g_strv_builder_add (b, closed->pdata[i]);

  out = g_strv_builder_end (b);
  g_strv_builder_unref (b);
  g_ptr_array_unref (closed);

  return out;
}

gboolean
mln_panes_set_default (MlnPanes *self, const char *mode, const char *json)
{
  return mln_model_set_default (self->model, mode, json);
}

gboolean
mln_panes_set_version (MlnPanes *self, const char *json)
{
  return mln_model_set_version (self->model, json);
}

void
mln_panes_set_mode (MlnPanes *self, const char *mode)
{
  mln_model_set_mode (self->model, mode);
}

gboolean
mln_panes_load (MlnPanes *self, const char *kept)
{
  gboolean used = mln_model_load (self->model, kept);

  render (self);

  return used;
}

char *
mln_panes_save (MlnPanes *self)
{
  return mln_model_save (self->model);
}

char *
mln_panes_get_layout (MlnPanes *self)
{
  return mln_model_json (self->model);
}

gboolean
mln_panes_set_layout (MlnPanes *self, const char *json)
{
  if (!mln_model_set_layout (self->model, json))
    return FALSE;

  render (self);

  return TRUE;
}

void
mln_panes_reset (MlnPanes *self)
{
  mln_model_reset (self->model);
  render (self);
}
