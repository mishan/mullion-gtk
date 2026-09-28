/*
 * Copyright (C) 2026 Misha Nasledov
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/* A window of panes to try mullion-gtk on: an editor, a console, an
   inspector and a drawing, laid out as mullion's README lays them out. */

#include "mln-panes.h"

#include <stdio.h>

static const char *LAYOUT =
  "{\"dir\":\"row\",\"size\":[0.62,0.38],\"kids\":["
  "{\"dir\":\"col\",\"size\":[0.7,0.3],\"kids\":["
  "{\"tabs\":[\"editor\",\"drawing\"]},{\"tabs\":[\"console\"]}]},"
  "{\"tabs\":[\"inspector\"]}]}";

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

static const char *IDS[] = { "editor", "console", "inspector", "drawing" };

/* Where everything is, on one line, once the layout has been drawn: for a
   harness that clicks and drags, which cannot know the fonts. */
static char *last_geometry;

static gboolean
geometry (GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
  MlnPanes *panes = MLN_PANES (widget);
  GString *out = g_string_new ("geometry {");
  graphene_rect_t r;
  double x = 0, y = 0;
  GtkNative *native = gtk_widget_get_native (GTK_WIDGET (panes));
  graphene_point_t at;

  /* In the window's coordinates, which is what a harness clicks in. */
  if (native != NULL &&
      gtk_widget_compute_point (GTK_WIDGET (panes), GTK_WIDGET (native),
                                &GRAPHENE_POINT_INIT (0, 0), &at))
    {
      x = at.x;
      y = at.y;
    }

  for (guint i = 0; i < G_N_ELEMENTS (IDS); i++)
    {
      g_string_append_printf (out, "%s\"%s\":{", i ? "," : "", IDS[i]);

      if (mln_panes_get_tab_bounds (panes, IDS[i], &r))
        g_string_append_printf (out, "\"tab\":[%g,%g,%g,%g]", r.origin.x + x,
                                r.origin.y + y, r.size.width, r.size.height);

      if (mln_panes_get_leaf_bounds (panes, IDS[i], &r))
        g_string_append_printf (out, ",\"leaf\":[%g,%g,%g,%g]", r.origin.x + x,
                                r.origin.y + y, r.size.width, r.size.height);

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

static void
activate (GtkApplication *app)
{
  GtkWidget *win = gtk_application_window_new (app);
  GtkWidget *panes = mln_panes_new ();
  GtkWidget *drawing = gtk_drawing_area_new ();
  const char *start = g_getenv ("MLN_DEMO_LAYOUT");

  gtk_drawing_area_set_draw_func (GTK_DRAWING_AREA (drawing), draw, NULL, NULL);

  mln_panes_register (MLN_PANES (panes), "editor", "Editor",
                      text ("int\nmain (void)\n{\n  return 0;\n}\n"), 240);
  mln_panes_register (MLN_PANES (panes), "console", "Console",
                      text ("$ make\nok\n"), 200);
  mln_panes_register (MLN_PANES (panes), "inspector", "Inspector",
                      text ("width  240\nheight  64\n"), 160);
  mln_panes_register (MLN_PANES (panes), "drawing", "Drawing", drawing, 120);

  g_signal_connect (panes, "pane-shown", G_CALLBACK (shown), NULL);
  g_signal_connect (panes, "layout-kept", G_CALLBACK (kept), NULL);
  gtk_widget_add_tick_callback (panes, geometry, NULL, NULL);

  mln_panes_set_default (MLN_PANES (panes), "main", LAYOUT);
  mln_panes_set_mode (MLN_PANES (panes), "main");
  mln_panes_load (MLN_PANES (panes), start);

  gtk_window_set_title (GTK_WINDOW (win), "mullion-gtk");
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

  return g_application_run (G_APPLICATION (app), argc, argv);
}
