/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* A window of panes to try mullion-gtk on: an editor, a console, an
   inspector and a drawing, laid out as mullion's README lays them out. */

#include "mullion-gtk.h"

#include <stdio.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#include <signal.h>
#endif

static const char *LAYOUT =
  "{\"dir\":\"row\",\"size\":[0.62,0.38],\"kids\":["
  "{\"dir\":\"col\",\"size\":[0.7,0.3],\"kids\":["
  "{\"tabs\":[\"editor\",\"drawing\"]},{\"tabs\":[\"console\"]}]},"
  "{\"tabs\":[\"inspector\"],\"slots\":[\"side\"]}]}";

static GtkWidget *
text (const char *what)
{
  GtkWidget *view = gtk_text_view_new ();
  GtkWidget *scroll = gtk_scrolled_window_new ();

  gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)), what, -1);
  gtk_text_view_set_monospace (GTK_TEXT_VIEW (view), TRUE);
  gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scroll), view);

  return scroll;
}

static void
draw (GtkDrawingArea *area, cairo_t *cr, int w, int h, gpointer data)
{
  cairo_set_source_rgb (cr, 0.20, 0.40, 0.70);
  cairo_arc (cr, w / 2.0, h / 2.0, MIN (w, h) / 3.0, 0, 6.2832);
  cairo_fill (cr);
}

static void
shown (MlnPanes *panes, const char *id, gboolean on, gpointer data)
{
  g_print ("pane-shown %s %s\n", id, on ? "on" : "off");
}

/* notes only with MLN_DEMO_PLACEMENT; without it, it has no parts. */
static const char *IDS[] = { "editor", "console", "inspector", "drawing", "notes" };

/* Where everything is, on one line, once the layout has been drawn: for a
   harness that clicks and drags, which cannot know the fonts. */
static char *last_geometry;

static gboolean
geometry (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
  MlnPanes *panes = MLN_PANES (widget);
  GString *out = g_string_new ("geometry {");
  graphene_rect_t r;

  for (guint i = 0; i < G_N_ELEMENTS (IDS); i++)
    {
      const char *sep = "";
      GtkWindow *win = mln_panes_get_window (panes, IDS[i]);
      GtkWidget *drawn = win != NULL ? gtk_window_get_child (win) : widget;
      GtkNative *native = gtk_widget_get_native (drawn);
      graphene_point_t at;
      double x = 0, y = 0;

      /* In the coordinates of the window it is in, which is what a
         harness clicks in; and which window that is, by title. */
      if (native != NULL &&
          gtk_widget_compute_point (drawn, GTK_WIDGET (native),
                                    &GRAPHENE_POINT_INIT (0, 0), &at))
        {
          x = at.x;
          y = at.y;
        }

      g_string_append_printf (out, "%s\"%s\":{", i ? "," : "", IDS[i]);

      if (win != NULL)
        {
          g_string_append_printf (out, "\"window\":\"%s\"", gtk_window_get_title (win));
          sep = ",";
        }

#define PART(name, get)                                                    \
      if (get (panes, IDS[i], &r))                                          \
        {                                                                   \
          g_string_append_printf (out, "%s\"" name "\":[%g,%g,%g,%g]", sep, \
                                  r.origin.x + x, r.origin.y + y,           \
                                  r.size.width, r.size.height);             \
          sep = ",";                                                        \
        }

      PART ("tab", mln_panes_get_tab_bounds)
      PART ("leaf", mln_panes_get_leaf_bounds)
      PART ("closed", mln_panes_get_closed_bounds)
#undef PART

      g_string_append (out, "}");
    }

  g_string_append (out, "}");

  if (g_strcmp0 (out->str, last_geometry) != 0)
    {
      g_print ("%s\n", out->str);
      g_free (last_geometry);
      last_geometry = g_strdup (out->str);
    }

  g_string_free (out, TRUE);

  return G_SOURCE_CONTINUE;
}

static void
kept (MlnPanes *panes, const char *mode, const char *layout, gpointer data)
{
  g_print ("layout-kept %s %s\n", mode, layout != NULL ? layout : "(forget)");
}

G_GNUC_BEGIN_IGNORE_DEPRECATIONS
static gboolean
dump (gpointer data)
{
  g_print ("%s\n", gtk_style_context_to_string (gtk_widget_get_style_context (data),
                                                GTK_STYLE_CONTEXT_PRINT_RECURSE |
                                                GTK_STYLE_CONTEXT_PRINT_SHOW_STYLE));
  return G_SOURCE_REMOVE;
}
G_GNUC_END_IGNORE_DEPRECATIONS

/* Which widget has the keyboard, for a harness that cannot see it: a tab
   by its title, anything else by its type. */
static void
focus_moved (GtkWindow *win, GParamSpec *spec, gpointer data)
{
  GtkWidget *w = gtk_window_get_focus (win);

  if (w == NULL)
    g_print ("focus none\n");
  else if (g_strcmp0 (gtk_widget_get_css_name (w), "tab") == 0)
    g_print ("focus tab %s\n",
             gtk_label_get_text (GTK_LABEL (gtk_widget_get_first_child (w))));
  else
    g_print ("focus %s\n", G_OBJECT_TYPE_NAME (w));
}

static void
quit (GSimpleAction *action, GVariant *param, gpointer app)
{
  g_application_quit (G_APPLICATION (app));
}

#ifdef G_OS_UNIX
static gboolean
term (gpointer app)
{
  g_print ("quit-on-term\n");
  g_application_quit (G_APPLICATION (app));

  return G_SOURCE_REMOVE;
}
#endif

static void
clear_console (GSimpleAction *action, GVariant *param, gpointer data)
{
  g_print ("clear-console\n");
}

static void
activate (GtkApplication *app)
{
  GtkWidget *win, *panes, *drawing;
  const char *start = g_getenv ("MLN_DEMO_LAYOUT");

  /* After GTK has started, which sets the direction from the locale, and
     before anything is made. */
  if (g_getenv ("MLN_DEMO_RTL") != NULL)
    gtk_widget_set_default_direction (GTK_TEXT_DIR_RTL);

  win = gtk_application_window_new (app);
  panes = mln_panes_new ();
  drawing = gtk_drawing_area_new ();

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (drawing), draw, NULL, NULL);

  mln_panes_register (MLN_PANES (panes), "editor", "Editor",
                      text ("int\nmain (void)\n{\n  return 0;\n}\n"), 240);
  mln_panes_register (MLN_PANES (panes), "console", "Console",
                      text ("$ make\nok\n"), 200);
  mln_panes_register (MLN_PANES (panes), "inspector", "Inspector",
                      text ("width  240\nheight  64\n"), 160);
  mln_panes_register (MLN_PANES (panes), "drawing", "Drawing", drawing, 120);

  /* A pane the layout above does not have, opened in the side slot
     wherever a layout does not have it: what a pane new in a release
     does to a layout kept by the one before. */
  if (g_getenv ("MLN_DEMO_PLACEMENT") != NULL)
    {
      mln_panes_register (MLN_PANES (panes), "notes", "Notes",
                          text ("remember the milk\n"), 120);
      mln_panes_set_placement (MLN_PANES (panes), "notes", "side", TRUE);
    }

  g_signal_connect (panes, "pane-shown", G_CALLBACK (shown), NULL);
  g_signal_connect (panes, "layout-kept", G_CALLBACK (kept), NULL);
  gtk_widget_add_tick_callback (panes, geometry, NULL, NULL);

  /* An item of the app's own on the console's tab menu, and a way for a
     harness to ask for the attention mark. */
  {
    GMenu *items = g_menu_new ();
    GSimpleAction *clear = g_simple_action_new ("clear-console", NULL);

    g_signal_connect (clear, "activate", G_CALLBACK (clear_console), NULL);
    g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (clear));
    g_menu_append (items, "Clear C_onsole", "app.clear-console");
    mln_panes_set_pane_menu (MLN_PANES (panes), "console", G_MENU_MODEL (items));
    g_object_unref (items);
    g_object_unref (clear);
  }

  if (g_getenv ("MLN_DEMO_ATTENTION") != NULL)
    mln_panes_set_attention (MLN_PANES (panes), g_getenv ("MLN_DEMO_ATTENTION"), TRUE);

  mln_panes_set_default (MLN_PANES (panes), "main", LAYOUT);
  mln_panes_set_mode (MLN_PANES (panes), "main");
  mln_panes_load (MLN_PANES (panes), start);

  gtk_window_set_title (GTK_WINDOW (win), "mullion-gtk");
  g_signal_connect (win, "notify::focus-widget", G_CALLBACK (focus_moved), NULL);

  {
    GSimpleAction *q = g_simple_action_new ("quit", NULL);

    g_signal_connect (q, "activate", G_CALLBACK (quit), app);
    g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (q));
    gtk_application_set_accels_for_action (app, "app.quit",
                                           (const char *[]) { "<Control>q", NULL });
    g_object_unref (q);
  }
  gtk_window_set_default_size (GTK_WINDOW (win), 1000, 640);
  gtk_window_set_child (GTK_WINDOW (win), panes);
  gtk_window_present (GTK_WINDOW (win));

  if (g_getenv ("MLN_DEMO_DUMP") != NULL)
    g_timeout_add (500, dump, panes);
}

int
main (int argc, char **argv)
{
  GtkApplication *app;

  /* A line at a time, for a harness reading it as it comes. */
  setvbuf (stdout, NULL, _IOLBF, 0);

  app = gtk_application_new ("org.example.MullionDemo",
                                             G_APPLICATION_DEFAULT_FLAGS);

  g_signal_connect (app, "activate", G_CALLBACK (activate), NULL);

#ifdef G_OS_UNIX
  /* A harness's way out when the keys do not reach the window -- X11 with
     no window manager leaves the focus nowhere once the window that had
     it is gone -- by the same quit Ctrl Q is. */
  g_unix_signal_add (SIGTERM, (GSourceFunc) term, app);
#endif

  return g_application_run (G_APPLICATION (app), argc, argv);
}
