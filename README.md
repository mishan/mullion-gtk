# mullion-gtk

Tiled panes for GTK 4 apps: a split tree of tabbed leaves, tabs dragged
to move or split, closed panes listed and reopened where they were, and
layouts kept and restored. It is the model of
[mullion](https://github.com/mishan/mullion), which does the same for a
web page, and it keeps layouts mullion can read and reads the ones
mullion keeps.

Work in progress. What is here so far:

- `src/mln-model.{h,c}`: the layout model, GLib only, ported one function
  to one function from mullion's `src/panes.js`.
- `src/mln-json.{h,c}`: the JSON a layout is kept as, read and written as
  `JSON.parse` and `JSON.stringify` do.
- `src/mln-panes.{h,c}`: `MlnPanes`, the widget: tab strips, dividers, the
  drawer, tab drags, mullion's keyboard chords, a tab menu.
- `demo/`: a window of four panes to try it on.
- `tests/`: the model's operations, the JSON, and mullion's layout cases
  (`tests/layouts.json`, copied from mullion's `test/layouts.json`; keep
  the two the same).

## What it adds to mullion's layouts

Two fields mullion does not write and reads past, for what a desktop app
needs that a page does not:

- `"slots"` on a leaf: names for where a pane goes when nothing remembers
  where it was (`mln_panes_set_placement`). A leaf that empties hands its
  slots to the leaf that takes its room.
- `"closed"` in the envelope: the panes a person closed that the app opens
  wherever a layout does not have them, so that a pane new in a release
  comes up in a layout kept by the one before, and one a person closed
  stays closed. It is written only when there are any, in the version's
  envelope where the app gives a version and in one of its own
  (`{"layout":…,"closed":[…]}`) where it does not. mullion reads that one
  as no layout.

A layout that uses neither is written exactly as mullion writes it. One
kept by mullion has neither: a layout that goes through the web and back
comes back without its slots.

## Building

With meson:

```sh
meson setup build -Dshotbox=/path/to/shotbox
meson test -C build
```

or with CMake:

```sh
cmake -S . -B build -G Ninja -DMLN_SHOTBOX=/path/to/shotbox
cmake --build build && ctest --test-dir build
```

Either builds a static `libmullion-gtk-0` by default, installs
`mullion-gtk-0.pc` and the headers under `include/mullion-gtk-0/`, and
runs the same tests. The widget tests drive the demo in a real window with
[shotbox](https://github.com/mishan/shotbox) and are skipped without it.
`-Db_sanitize=address,undefined` is how the tests are meant to be run.

## Using it

From another meson project, as a subproject or installed:
`dependency('mullion-gtk-0')`. From CMake, `add_subdirectory` or
FetchContent, then `target_link_libraries(app PRIVATE mullion-gtk::mullion-gtk)`;
or installed, through its pkg-config file. Include `mullion-gtk.h`.

## License

LGPL-2.1-or-later. See [COPYING](COPYING).
