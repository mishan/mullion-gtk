# Changelog

## 0.2.0 — 2026-09-28

- **A pinned corner**: `mln_panes_set_corner_pinned` keeps a pane's corner
  in sight while it is in front of its leaf, for a pane whose first row
  makes room for it.

## 0.1.0 — 2026-09-28

The first release: mullion's tiled panes for a GTK 4 app, with the
layouts mullion keeps.

- **`MlnPanes`**: a split tree of tabbed leaves. Tabs drag to move, stack
  or split; dividers drag and take the arrow keys; closed panes go to a
  drawer and come back where they were; mullion's Alt chords; a tab menu
  with an app's own items; zoom; attention marks; panes the app adds and
  removes while it is up.
- **Layouts** kept and read as mullion keeps and reads them, per mode,
  with a version, and checked against mullion's layout cases.
- **Slots and placement**: names on a leaf for where a pane goes when
  nothing remembers where it was, so a pane new in a release comes up in
  a layout kept by the one before, and one a person closed stays closed.
- **Floating windows**: a pane moved into a window of its own by its tab
  menu or by dragging its tab out of the window; tabs drag between
  windows; the windows are kept with the layout, sizes and all.
- **Tabs in the corner** (`header`): a leaf's tabs as icons over its top
  corner instead of a strip across it, and icons for tabs in a strip.
- Builds with meson or CMake, as a static or shared library, against
  GTK 4.10 and GLib 2.74 or later.
