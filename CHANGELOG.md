# Changelog

## 0.3.0 — 2026-10-12

- **F6 and Shift F6** step the focus to the next leaf and the one before,
  in the order the leaves are laid out and round from the last to the
  first, as F6 moves between panes in most desktop apps. Unlike the Alt chords they work from a text field, so a
  keyboard user can leave one without tabbing through it.
- **Screen readers**: what is out of sight is out of their reach too
  (panes behind a tab, a zoom's hidden leaves, an empty drawer); a
  divider is a range with its value in percent, which a screen reader
  can set; each tab strip is named for the pane in front; a marked tab
  says it needs attention; the drawer is a group named by its label, and
  its buttons say what they reopen.
- **Dividers** take Home and End, as far as they go either way, and
  Enter, which evens the split as a double click does.
- **High contrast** (GTK's interface contrast from GTK 4.20, or a high
  contrast theme) takes the dimming off inactive tabs, the drawer and
  the dividers. In the corner, a marked tab has a dot as well as a color.
- **Announcements** (GTK 4.14 and later): a screen reader is told of a
  move the focus does not tell of -- a pane moved, split off, zoomed or
  unzoomed, closed, reopened, moved to a window of its own or back to the
  main one -- and of an attention mark on a pane out of sight.
- **Ctrl Shift Page Up and Page Down** move the pane in front along its
  strip, as they move a tab in a browser, and a tab's menu has Move Left
  and Move Right, with the chords shown.
- **Translatable**: every string the widget shows or says is in the
  gettext domain `mullion-gtk-0`, looked up under the localedir the
  library is built for; `po/` has the template's sources (no
  translations yet). Builds need libintl, which GLib does already.
- **Closing a floating window** puts the focus on what came back into
  the main window, rather than nowhere.

## 0.2.1 — 2026-09-28

- **Fixed**: closing the last pane of a floating window by its tab's cross,
  Alt W or its tab menu crashed: the window closed with it, and the click
  went on with the MlnPanes that had drawn it.

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
