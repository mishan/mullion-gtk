# mullion-gtk

Tiled panes for GTK 4 apps: a split tree of tabbed leaves, tabs dragged
to move or split, closed panes listed and reopened where they were, and
layouts kept and restored. It is the model of
[mullion](https://github.com/mishan/mullion), which does the same for a
web page, and it keeps layouts mullion can read and reads the ones
mullion keeps.

![Tabs dragged to split a leaf, to join one, out into a window of their own and back; a pane closed and put back from the drawer](demo/demo.gif)

What is here (see [CHANGELOG.md](CHANGELOG.md) for what each release
brought):

- `src/mln-model.{h,c}`: the layout model, GLib only, ported one function
  to one function from mullion's `src/panes.js`.
- `src/mln-json.{h,c}`: the JSON a layout is kept as, read and written as
  `JSON.parse` and `JSON.stringify` do.
- `src/mln-panes.{h,c}`: `MlnPanes`, the widget: tab strips, dividers, the
  drawer, tab drags, mullion's keyboard chords, a tab menu.
- `demo/`: a window of four panes to try it on, and `record.py`, which
  records it for the GIF above.
- `tests/`: the model's operations, the JSON, and mullion's layout cases
  (`tests/layouts.json`, copied from mullion's `test/layouts.json`; keep
  the two the same).

## Floating windows

A pane can go into a window of its own: its tab menu's Move to New
Window, `mln_panes_undock`, or a tab dragged out of the window and let go
over nothing. Tabs drag between windows as within one, splits and all,
and closing a floating window puts what was in it back where it was in
the main window (remembered for the session, not kept with the layout).

The app can make the windows (`mln_panes_set_window_func`), and should
where its panes' menus name window actions; otherwise each is transient
for the main window and of its application. A window the app destroys
is made again; closing one docks it. They are kept with the layout, with
their sizes. Positions are not: Wayland does not give them, and X11's
are the window manager's.

## Tabs in the corner

`mln_panes_set_header (panes, MLN_HEADER_CORNER)` (the `header`
property) takes the tab strip off every leaf and puts its tabs over the
leaf's top corner, in sight while the pointer or the focus is in the leaf
or a tab is being dragged: the panes' icons (`mln_panes_set_icon`; the
title for a pane without one), a grip for a pane alone in its leaf, the
front pane's cross, and a button for its tab menu. They raise, drag, drop
and take the keys as a strip's tabs do; the pane has the whole leaf. In a
strip, an icon sits before the title.

Laid over the pane, the corner covers whatever its first row has at that
end. `mln_panes_get_corner_width` says how much, and `::corner-changed`
when that may have changed, for a pane that makes room (a margin on its
toolbar, say). A pane that has made room can keep its corner in sight
while it is in front, where it covers nothing: `mln_panes_set_corner_pinned`.
mullion's `header: 'corner'` and `setCornerPinned` are the same, for a web
page.

## What it adds to mullion's layouts

Fields mullion does not write and reads past, for what a desktop app
needs that a page does not:

- `"slots"` on a leaf: names for where a pane goes when nothing remembers
  where it was (`mln_panes_set_placement`). A leaf that empties hands its
  slots to the leaf that takes its room.
- `"floating"` in the envelope: the floating windows, each
  `{"layout":…,"size":[w,h]}`. mullion has none, and a layout kept with
  one is read by mullion as the main window's alone -- or, with no
  version, as no layout (below).
- `"closed"` in the envelope: the panes a person closed that the app opens
  wherever a layout does not have them, so that a pane new in a release
  comes up in a layout kept by the one before, and one a person closed
  stays closed.

They are written only when there are any, in the version's envelope where
the app gives a version and in one of their own (`{"layout":…}` with
them) where it does not. mullion reads that one as no layout.

A layout that uses none of them is written exactly as mullion writes it. One
kept by mullion has neither: a layout that goes through the web and back
comes back without its slots.

## Building

With meson:

```sh
meson setup build
meson test -C build
```

or with CMake:

```sh
cmake -S . -B build -G Ninja
cmake --build build && ctest --test-dir build
```

Either builds a static `libmullion-gtk-0` by default, installs
`mullion-gtk-0.pc` and the headers under `include/mullion-gtk-0/`, and
runs the same tests. `-Db_sanitize=address,undefined` is how the tests are
meant to be run.

The widget tests drive the demo in a real window with
[shotbox](https://github.com/mishan/shotbox), and are skipped without it:

```sh
pipx install --system-site-packages shotbox
```

or `pip install shotbox`. To run them against a shotbox checkout instead,
give it with `-Dshotbox=/path/to/shotbox` or `-DMLN_SHOTBOX=/path/to/shotbox`.

## Using it

From another meson project, as a subproject or installed:
`dependency('mullion-gtk-0')`. From CMake, `add_subdirectory` or
FetchContent, then `target_link_libraries(app PRIVATE mullion-gtk::mullion-gtk)`;
or installed, through its pkg-config file. Include `mullion-gtk.h`.

## License

LGPL-2.1-or-later. See [COPYING](COPYING).
