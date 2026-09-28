#!/usr/bin/env python3
#
# Copyright (C) 2026 Misha Nasledov
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""MlnPanes in a real window: the demo, driven the way a person drives it.

    widget.py DEMO

Every scenario starts the demo in a sealed shotbox session (a private X
display, nothing on the desktop) and reads back what it prints: the
layout it keeps, which panes are shown, and where every tab and leaf is,
so no position here depends on the fonts. Exits 77 (skipped) when shotbox
cannot be imported.
"""

import json
import os
import sys
import time

try:
    import shotbox
    from shotbox import xtest
except ImportError:
    print("skip: shotbox is not importable (PYTHONPATH)")
    sys.exit(77)

DEMO = os.path.abspath(sys.argv[1])

# The same scenarios on Wayland (a headless sway, shotbox --wayland) when
# MLN_WAYLAND is set; X11 on Xvfb otherwise.
WAYLAND = bool(os.environ.get("MLN_WAYLAND"))
OUT = os.path.join(os.environ.get("MESON_BUILD_ROOT", os.getcwd()), "widget-logs")
W = "mullion-gtk"
failures = 0


class Demo:
    def __init__(self, s, name):
        self.s = s
        self.log = os.path.join(OUT, name + ".log")
        if os.path.exists(self.log):
            os.remove(self.log)
        s.spawn([DEMO], log=self.log)
        s.wait_window(W)
        self.settle()

    def lines(self, prefix):
        with open(self.log) as f:
            return [l.rstrip("\n")[len(prefix):] for l in f if l.startswith(prefix)]

    def kept(self):
        k = self.lines("layout-kept main ")
        return json.loads(k[-1]) if k and k[-1] != "(forget)" else k[-1] if k else None

    def geometry(self):
        return json.loads(self.lines("geometry ")[-1])

    def shown(self):
        now = {}
        for l in self.lines("pane-shown "):
            id, on = l.split()
            now[id] = on == "on"
        return {k for k, v in now.items() if v}

    def settle(self):
        self.s.wait_stable(window=W)
        time.sleep(0.2)

    def centre(self, id, part="tab"):
        x, y, w, h = self.geometry()[id][part]
        return x + w / 2, y + h / 2

    def click(self, x, y):
        self.s.click(int(x), int(y), window=W)
        self.settle()

    def key(self, *chords):
        self.s.key(*chords)
        self.settle()

    def drag(self, x1, y1, to, escape=False):
        """Slowly, as XTEST drags have to be for GTK to see a drag. `to'
        is asked for the target once the drag has begun: the layout moves
        then, when the drawer's drop row comes up over it."""
        xt, (ox, oy) = self.s.xt, self.origin()
        x1, y1 = int(x1 + ox), int(y1 + oy)
        xt.move(x1 - 6, y1)
        time.sleep(0.1)
        xt.move(x1, y1)
        time.sleep(0.15)
        self.press(True)
        time.sleep(0.1)
        for i in range(1, 6):
            xt.move(x1 + 3 * i, y1 + 3 * i)
            time.sleep(0.03)
        time.sleep(0.4)
        x2, y2 = to(self.geometry()) if callable(to) else to
        x2, y2 = int(x2 + ox), int(y2 + oy)
        x0, y0 = x1 + 15, y1 + 15
        for i in range(1, 21):
            xt.move(x0 + (x2 - x0) * i // 20, y0 + (y2 - y0) * i // 20)
            time.sleep(0.03)
        time.sleep(0.3)
        if escape:
            self.s.key("Escape")
            time.sleep(0.2)
        self.press(False)
        time.sleep(0.4)
        self.settle()

    def press(self, down):
        if WAYLAND:
            self.s.xt._button(1, 1 if down else 0)
        else:
            self.s.xt._fake(xtest.BUTTON_PRESS if down else xtest.BUTTON_RELEASE, 1)
        self.s.xt.sync()

    def origin(self):
        _, _, _, _, x, y = self.s.wait_window(W)
        return x, y


def tabs(node):
    """Every leaf's tabs and front one, in order: what a check compares."""
    if "tabs" in node:
        return [(node["tabs"], node.get("active", 0))]
    return [t for k in node["kids"] for t in tabs(k)]


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        failures += 1


def scenario(fn):
    name = fn.__name__
    with shotbox.Session(size=(1100, 720), wayland=WAYLAND,
                         env={"GDK_BACKEND": "wayland" if WAYLAND else "x11"},
                         failed=os.path.join(OUT, name + "-failed.png")) as s:
        d = Demo(s, name)
        try:
            fn(d)
        except Exception as e:
            check(False, f"{name}: {e!r}")
        s.capture(os.path.join(OUT, name + ".png"), window=W, park=True)
        # Any popover on Xvfb (no compositor) gets this one from GDK; a
        # plain GtkPopoverMenu in a window of its own does too.
        log = "\n".join(l for l in open(d.log).read().splitlines()
                        if "gdk_frame_timings_submitted() called on submitted frame" not in l)
        check("CRITICAL" not in log and "WARNING" not in log,
              f"{name}: nothing warned")


# ---- scenarios ----

def starts(d):
    check(d.shown() == {"editor", "console", "inspector"},
          f"the front panes are shown, and only those: {sorted(d.shown())}")
    check("drawing" in d.geometry() and "tab" in d.geometry()["drawing"],
          "a pane behind a tab still has its tab")


def raises(d):
    d.click(*d.centre("drawing"))
    check(tabs(d.kept())[0] == (["editor", "drawing"], 1),
          f"a tab clicked is in front: {tabs(d.kept())}")
    check("drawing" in d.shown() and "editor" not in d.shown(),
          "and the pane behind it is told it is out of view")


def closes_and_reopens(d):
    x, y, w, h = d.geometry()["console"]["tab"]
    d.s.move(int(x + 10), int(y + h / 2), window=W)
    d.settle()
    d.click(x + w - 14, y + h / 2)      # its cross
    check(all("console" not in t for t, _ in tabs(d.kept())),
          f"its cross closes a pane: {tabs(d.kept())}")
    check("console" not in d.shown(), "and it is out of view")
    d.click(95, 14)                     # its button in the drawer
    check(tabs(d.kept()) == [(["editor", "drawing"], 0), (["console"], 0),
                             (["inspector"], 0)],
          f"and its button in the drawer puts it back where it was: {tabs(d.kept())}")


def leaf_at(id, fx, fy):
    def at(g):
        x, y, w, h = g[id]["leaf"]
        return x + w * fx, y + h * fy
    return at


def drags_beside(d):
    tx, ty = d.centre("inspector")
    d.drag(tx, ty, leaf_at("editor", 0.95, 0.5))
    k = d.kept()
    check(tabs(k)[:2] == [(["editor", "drawing"], 0), (["inspector"], 0)],
          f"a tab dropped on a leaf's right edge splits it: {tabs(k)}")
    check(k.get("dir") == "col" and k["kids"][0].get("dir") == "row",
          "the split is a row inside the column that is left")


def drags_onto_a_strip(d):
    tx, ty = d.centre("console")

    def before_editor(g):
        x, y, w, h = g["editor"]["tab"]
        return x + 4, y + h / 2

    d.drag(tx, ty, before_editor)
    check(tabs(d.kept())[0] == (["console", "editor", "drawing"], 0),
          f"a tab dropped on a strip goes before the tab under it: {tabs(d.kept())}")


def drags_into(d):
    tx, ty = d.centre("console")
    d.drag(tx, ty, leaf_at("inspector", 0.5, 0.5))
    check(tabs(d.kept()) == [(["editor", "drawing"], 0), (["inspector", "console"], 1)],
          f"a tab dropped in a leaf's middle joins it, in front: {tabs(d.kept())}")


def escape_cancels(d):
    before = d.kept()
    tx, ty = d.centre("console")
    d.drag(tx, ty, leaf_at("inspector", 0.5, 0.5), escape=True)
    check(d.kept() == before, "Escape during a drag leaves the layout as it was")


def chords(d):
    d.click(*d.centre("drawing"))       # the editor's leaf, Drawing in front
    d.key("alt+w")
    check(tabs(d.kept())[0] == (["editor"], 0), f"Alt W closes the pane in front: {tabs(d.kept())}")
    d.key("alt+Return")
    check(d.shown() == {"editor"}, f"Alt Enter fills the layout with one leaf: {sorted(d.shown())}")
    d.key("alt+Return")
    check(d.shown() == {"editor", "console", "inspector"}, "and again puts it back")
    d.key("alt+shift+Right")
    check(tabs(d.kept()) == [(["console"], 0), (["inspector", "editor"], 1)],
          f"Alt Shift Right moves the pane into the leaf that way: {tabs(d.kept())}")
    d.key("alt+0")
    check(d.lines("layout-kept main ")[-1] == "(forget)",
          "Alt 0 forgets what was kept")
    check(d.shown() == {"editor", "console", "inspector"}, "and the default is up again")


def tab_keys(d):
    d.click(*d.centre("editor"))
    d.key("Right")
    check("drawing" in d.shown() and "editor" not in d.shown(),
          "Right on a focused tab raises the next")
    d.key("Right")
    check("editor" in d.shown(), "and wraps round the strip")


def divider(d):
    ex, ey, ew, eh = d.geometry()["editor"]["leaf"]
    before = (d.kept() or {"size": [0.62]})["size"][0]     # the demo's default
    x, y = ex + ew + 3, ey + eh / 2
    d.drag(x, y, (x - 150, y + 15))
    after = d.kept()["size"][0]
    width = d.geometry()["editor"]["leaf"][2] + d.geometry()["inspector"]["leaf"][2]
    check(abs((before - after) * width - 150) < 4,
          f"dragging a divider moves it as far as the pointer: {before} -> {after}")
    x = d.geometry()["editor"]["leaf"][0] + d.geometry()["editor"]["leaf"][2] + 3
    d.s.click(int(x), int(y), window=W, double=True)
    d.settle()
    sizes = d.kept()["size"]
    check(abs(sizes[0] - sizes[1]) < 1e-9, f"a double click evens it: {sizes}")


def tab_menu(d):
    x, y = d.centre("drawing")
    d.s.click(int(x), int(y), window=W, button=3)
    time.sleep(0.5)
    d.s.capture(os.path.join(OUT, "tab_menu-open.png"))
    d.key("r")                          # Split _Right
    check(tabs(d.kept())[:2] == [(["editor"], 0), (["drawing"], 0)],
          f"a tab's menu splits its pane off to the right: {tabs(d.kept())}")
    x, y = d.centre("console")
    d.s.click(int(x), int(y), window=W, button=3)
    time.sleep(0.5)
    d.key("o")                          # Clear C_onsole, the app's
    check(d.lines("clear-console") != [], "an item of the app's own on a tab's menu runs")


os.makedirs(OUT, exist_ok=True)

for fn in (starts, raises, closes_and_reopens, drags_beside, drags_onto_a_strip,
           drags_into, escape_cancels, chords, tab_keys, divider, tab_menu):
    print(f"# {fn.__name__}")
    scenario(fn)

print(f"\n{failures} failed" if failures else "\nall passed")
sys.exit(1 if failures else 0)
