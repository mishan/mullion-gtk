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
- `tests/`: the model's operations, the JSON, and mullion's layout cases
  (`tests/layouts.json`, copied from mullion's `test/layouts.json`; keep
  the two the same).

## Building

```sh
meson setup build
meson test -C build
```

`-Db_sanitize=address,undefined` is how the tests are meant to be run.

## License

LGPL-2.1-or-later. See [COPYING](COPYING).
