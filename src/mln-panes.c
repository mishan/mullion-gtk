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
} Pane;

/* A strip drawn for a leaf, and the node it was drawn for. */
typedef struct
{
  MlnNode *leaf;
  GtkWidget *strip;
} Strip;

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
}

static void
on_tab_shut (GtkButton *button, gpointer data)
{
  MlnPanes *self = g_object_get_data (G_OBJECT (button), "mln-panes");
  Pane *p = data;
  MlnNode *leaf = mln_model_leaf_with (self->model, p->id);

  if (!mln_model_close (self->model, p->id))
    return;

  if (leaf != NULL && mln_model_holds (self->model, leaf))
    mln_model_set_focus (self->model, leaf);

  render (self);
}

static GtkWidget *
make_tab (MlnPanes *self, Pane *p)
{
  GtkWidget *tab = g_object_new (GTK_TYPE_BOX,
                                 "orientation", GTK_ORIENTATION_HORIZONTAL,
                                 "accessible-role", GTK_ACCESSIBLE_ROLE_TAB,
                                 "focusable", TRUE,
                                 "css-name", "tab",
                                 NULL);
  GtkGesture *click = gtk_gesture_click_new ();

  p->label = gtk_label_new (p->title);
  gtk_label_set_ellipsize (GTK_LABEL (p->label), PANGO_ELLIPSIZE_END);
  gtk_label_set_width_chars (GTK_LABEL (p->label), 3);
  gtk_box_append (GTK_BOX (tab), p->label);

  p->shut = gtk_button_new_from_icon_name ("window-close-symbolic");
  gtk_widget_add_css_class (p->shut, "flat");
  gtk_widget_add_css_class (p->shut, "shut");
  gtk_widget_set_focus_on_click (p->shut, FALSE);
  g_object_set_data (G_OBJECT (p->shut), "mln-panes", self);
  g_signal_connect (p->shut, "clicked", G_CALLBACK (on_tab_shut), p);
  gtk_box_append (GTK_BOX (tab), p->shut);

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
  g_signal_connect (click, "pressed", G_CALLBACK (on_tab_pressed), p);
  gtk_widget_add_controller (tab, GTK_EVENT_CONTROLLER (click));

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

static void
on_divider_begin (GtkGestureDrag *drag, double x, double y, gpointer data)
{
  Divider *d = data;

  d->moved = FALSE;
  d->start = g_array_index ((GArray *) g_object_get_data (G_OBJECT (d->bar), "mln-sides"),
                            double, 0);
}

static void
on_divider_update (GtkGestureDrag *drag, double dx, double dy, gpointer data)
{
  Divider *d = data;
  MlnPanes *self = g_object_get_data (G_OBJECT (d->bar), "mln-panes");
  GArray *sides = g_object_get_data (G_OBJECT (d->bar), "mln-sides");
  double delta = row_of (d->split) ? dx : dy;
  double both = g_array_index (sides, double, 1);

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

  switch (keyval)
    {
    case GDK_KEY_Left: case GDK_KEY_Up: step = -1; break;
    case GDK_KEY_Right: case GDK_KEY_Down: step = 1; break;
    default: return FALSE;
    }

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

  d->split = mln_node_ref (split);
  d->a = a;
  d->b = b;
  d->bar = g_object_new (GTK_TYPE_SEPARATOR,
                         "orientation", row ? GTK_ORIENTATION_VERTICAL
                                            : GTK_ORIENTATION_HORIZONTAL,
                         "accessible-role", GTK_ACCESSIBLE_ROLE_SEPARATOR,
                         "focusable", TRUE,
                         "cursor", gdk_cursor_new_from_name (row ? "col-resize" : "row-resize", NULL),
                         NULL);
  gtk_widget_add_css_class (d->bar, "panesplit");
  gtk_accessible_update_property (GTK_ACCESSIBLE (d->bar),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL,
                                  row ? "Resize columns" : "Resize rows", -1);

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

static void
fill_drawer (MlnPanes *self)
{
  GPtrArray *closed = mln_model_closed (self->model);
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (self->drawer)) != NULL)
    gtk_box_remove (GTK_BOX (self->drawer), child);

  if (closed->len > 0)
    gtk_box_append (GTK_BOX (self->drawer), gtk_label_new ("Closed:"));

  for (guint i = 0; i < closed->len; i++)
    {
      Pane *p = pane_of (self, closed->pdata[i]);
      GtkWidget *b = gtk_button_new_with_label (p->title);
      char *tip = g_strdup_printf ("Reopen %s", p->title);

      gtk_widget_set_tooltip_text (b, tip);
      gtk_widget_add_css_class (b, "paneclosed");
      g_object_set_data (G_OBJECT (b), "mln-panes", self);
      g_signal_connect_data (b, "clicked", G_CALLBACK (on_reopen), g_strdup (p->id),
                             free_id, 0);
      gtk_box_append (GTK_BOX (self->drawer), b);
      g_free (tip);
    }

  gtk_widget_set_child_visible (self->drawer, self->show_drawer && closed->len > 0);
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

  /* Which panes are in front of somebody, told only where it changed. */
  for (guint i = 0; i < self->order->len; i++)
    {
      Pane *p = self->order->pdata[i];
      gboolean now = g_hash_table_contains (front, p) &&
                     gtk_widget_get_mapped (GTK_WIDGET (self));

      if (now != p->shown)
        {
          p->shown = now;
          g_signal_emit (self, signals[PANE_SHOWN], 0, p->id, now);
        }
    }

  g_hash_table_unref (drawn);
  g_hash_table_unref (front);
  gtk_widget_queue_resize (GTK_WIDGET (self));
}

static void
render (MlnPanes *self)
{
  if (mln_model_loaded (self->model))
    sync_children (self);
}

/* ---- laying out ---- */

typedef struct { int x, y, w, h; } Box;

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

  at = row ? box.x : box.y;

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

            if (d != NULL)
              {
                place (d->bar, row ? (Box) { at, box.y, self->split, box.h }
                                   : (Box) { box.x, at, box.w, self->split });
              }

            at += self->split;
          }

        /* Whole pixels, the remainder carried so the children fill the
           box exactly. */
        carry += want[i];
        size = (int) (carry + 0.5);
        carry -= size;

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

        at += size;
        prev = (int) i;
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
  g_free (p->id);
  g_free (p->title);
  g_free (p);
}

static void
mln_panes_dispose (GObject *o)
{
  MlnPanes *self = MLN_PANES (o);

  g_clear_handle_id (&self->render_idle, g_source_remove);

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
  gtk_widget_set_parent (p->host, GTK_WIDGET (self));
  gtk_widget_set_child_visible (p->host, FALSE);
  p->tab = make_tab (self, p);

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

  mln_model_remove (self->model, id);

  if (p->shown)
    g_signal_emit (self, signals[PANE_SHOWN], 0, p->id, FALSE);

  if (gtk_widget_get_parent (p->tab) != NULL)
    gtk_box_remove (GTK_BOX (gtk_widget_get_parent (p->tab)), p->tab);

  host = MLN_HOST (p->host);
  content = g_object_ref (host->content);
  gtk_widget_unparent (host->content);
  host->content = NULL;
  gtk_widget_unparent (p->host);
  g_clear_object (&p->tab);

  g_hash_table_remove (self->panes, id);
  g_ptr_array_remove (self->order, p);
  render (self);

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
  render (self);
}

void
mln_panes_set_available (MlnPanes *self, const char *id, gboolean available)
{
  mln_model_set_available (self->model, id, available);
  render (self);
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
