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
        self.proc = s.spawn([DEMO], log=self.log)
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

    def focus(self):
        f = self.lines("focus ")
        return f[-1] if f else None

    def quit(self):
        """Ctrl Q, and wait for it to go: dispose runs, under ASan. Where
        the key reaches no window, the same quit by SIGTERM (see the
        demo's main)."""
        self.s.key("ctrl+q")
        for tries in range(160):
            if self.proc.poll() is not None:
                return self.proc.returncode
            if tries == 60:
                self.proc.terminate()
            time.sleep(0.1)
        return None

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
        self.during = self.geometry()
        x2, y2 = to(self.during) if callable(to) else to
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


def scenario(fn, env=None):
    name = fn.__name__
    with shotbox.Session(size=(1100, 720), wayland=WAYLAND,
                         env={"GDK_BACKEND": "wayland" if WAYLAND else "x11",
                              # The leaks GTK leaves at exit are GTK's.
                              "ASAN_OPTIONS": "detect_leaks=0",
                              **(env or {})},
                         failed=os.path.join(OUT, name + "-failed.png")) as s:
        d = Demo(s, name)
        try:
            fn(d)
        except Exception as e:
            check(False, f"{name}: {e!r}")
        s.capture(os.path.join(OUT, name + ".png"), window=W, park=True)
        code = d.quit()
        check(code == 0, f"{name}: the window closes cleanly ({code})")
        # Two that GDK says about the display rather than about this: any
        # popover on Xvfb (no compositor) gets the first, a plain
        # GtkPopoverMenu included; a sway without xdg_popup.reposition
        # (Ubuntu 24.04's) gets the second, and GTK remaps the popup.
        benign = ("gdk_frame_timings_submitted() called on submitted frame",
                  "Compositor doesn't support moving popups, relying on remapping")
        log = "\n".join(l for l in open(d.log).read().splitlines()
                        if not any(b in l for b in benign))
        check("CRITICAL" not in log and "WARNING" not in log,
              f"{name}: nothing warned")
        # And a sanitizer's report, which a demo stopped by the session
        # would otherwise take with it unread.
        check("AddressSanitizer" not in log and "runtime error:" not in log,
              f"{name}: no sanitizer report")


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
    d.click(*d.centre("console", "closed"))     # its button in the drawer
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
    check(d.during["inspector"]["leaf"][1] > d.geometry()["inspector"]["leaf"][1],
          "(the drag was on: the drawer's row came up over the layout)")
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


def drops_on_the_drawer(d):
    tx, ty = d.centre("console")

    def drawer(g):
        x, y, w, h = g["editor"]["leaf"]
        return x + 60, y - 10                     # the row over the layout

    d.drag(tx, ty, drawer)
    check(all("console" not in t for t, _ in tabs(d.kept())),
          f"a tab dropped on the drawer closes its pane: {tabs(d.kept())}")
    check("closed" in d.geometry()["console"], "and it is in the drawer")


def split_chords(d):
    d.click(*d.centre("drawing"))
    d.key("alt+\\")
    check(tabs(d.kept())[:2] == [(["editor"], 0), (["drawing"], 0)],
          f"Alt \\ splits the pane in front off to the right: {tabs(d.kept())}")
    d.click(*d.centre("inspector"))
    d.key("alt+minus")
    check(tabs(d.kept())[-1] == (["inspector"], 0),
          f"Alt - in a leaf of one pane does nothing when the drawer is empty: {tabs(d.kept())}")


def tab_order(d):
    d.click(*d.centre("inspector", "leaf"))      # into the inspector's text
    seen = []
    for _ in range(12):
        d.key("ctrl+Tab")               # Tab alone is a tab in a text view
        seen.append(d.focus())
    tabs_seen = [f for f in seen if f.startswith("tab ")]
    check(tabs_seen != [], f"the Tab key reaches the tabs: {seen}")
    check("tab Drawing" not in seen, "but not a tab behind another")
    closes = [f for f in seen if f == "GtkButton"]
    check(len(set(i for i, f in enumerate(seen) if f == "GtkButton")) <= 3 * 2,
          f"and only the front tabs' crosses: {seen}")


def placed(d):
    k = d.kept()
    check(tabs(k)[-1] == (["inspector", "notes"], 0),
          f"a pane opened by placement comes up in its slot, behind: {tabs(k)}")
    d.click(*d.centre("notes"))
    x, y, w, h = d.geometry()["notes"]["tab"]
    d.click(x + w - 14, y + h / 2)      # its cross
    k = d.kept()
    check(k.get("closed") == ["notes"] and "layout" in k and
          all("notes" not in t for t, _ in tabs(k["layout"])),
          f"closed, it is kept as closed: {k}")


def floats(d):
    x, y = d.centre("console")
    d.s.click(int(x), int(y), window=W, button=3)
    time.sleep(0.5)
    d.key("n")                          # Move to _New Window
    d.s.wait_window("Console")
    d.settle()
    g = d.geometry()
    k = d.kept()
    check(g["console"].get("window") == "Console" and "tab" in g["console"],
          f"a tab's menu moves its pane into a window of its own: {g['console']}")
    check("floating" in k and tabs(k["floating"][0]["layout"]) == [(["console"], 0)] and
          all("console" not in t for t, _ in tabs(k["layout"])),
          f"which is kept with the layout: {k}")
    check("console" in d.shown(), "and the pane in it is in view")

    # Its own tab menu, in its own window: back where it came from.
    x, y = d.centre("console")
    d.s.click(int(x), int(y), window="Console", button=3)
    time.sleep(0.5)
    d.s.key("m")                        # Move to _Main Window
    d.settle()
    k = d.kept()
    check("floating" not in k and tabs(k)[1] == (["console"], 0),
          f"and its menu moves it back, where it was: {k}")
    check("window" not in d.geometry()["console"], "the window is gone")


def drag_across(d, src, x1, y1, dst, to):
    """A tab dragged from window `src' to a point `to' in window `dst' (or,
    for dst None, on the screen), as slowly as a drag in one window."""
    xt = d.s.xt
    _, _, _, _, sx, sy = d.s.wait_window(src)
    x1, y1 = int(x1 + sx), int(y1 + sy)
    xt.move(x1 - 6, y1)
    time.sleep(0.1)
    xt.move(x1, y1)
    time.sleep(0.15)
    d.press(True)
    time.sleep(0.1)
    for i in range(1, 6):
        xt.move(x1 + 3 * i, y1 + 3 * i)
        time.sleep(0.03)
    time.sleep(0.3)
    if dst is not None:
        _, _, _, _, dx, dy = d.s.wait_window(dst)
        x2, y2 = int(to[0] + dx), int(to[1] + dy)
    else:
        x2, y2 = to
    x0, y0 = x1 + 15, y1 + 15
    for i in range(1, 31):
        xt.move(x0 + (x2 - x0) * i // 30, y0 + (y2 - y0) * i // 30)
        time.sleep(0.04)
    time.sleep(0.5)
    d.press(False)
    time.sleep(0.8)
    d.settle()


def drags_out_and_in(d):
    _, _, w, h, _, _ = d.s.wait_window(W)
    x, y = d.centre("console")
    drag_across(d, W, x, y, None, (w + 60, 40))        # off the window's right
    d.s.wait_window("Console")
    d.settle()
    g = d.geometry()
    check(g["console"].get("window") == "Console",
          f"a tab dragged out of the window and let go over nothing floats: {g['console']}")

    # From its window into the middle of the inspector's leaf, in the main
    # one -- the middle, which the drawer's drop row coming up during the
    # drag does not move off.
    x, y = d.centre("console")
    drag_across(d, "Console", x, y, W, d.centre("inspector", "leaf"))
    k = d.kept()
    check("floating" not in k and tabs(k)[-1] == (["inspector", "console"], 1),
          f"and dragged into another window's leaf, it lands there: {k}")


def floats_kept(d):
    g = d.geometry()
    check(g["inspector"].get("window") == "Inspector",
          f"a kept layout's floating window comes up with it: {g['inspector']}")
    check("inspector" in d.shown(), "with its pane in view")


def right_to_left(d):
    ex, ey, ew, eh = d.geometry()["editor"]["leaf"]
    ix = d.geometry()["inspector"]["leaf"][0]
    check(ix < ex, f"a row reads right to left: the first child is on the right ({ix} < {ex})")
    tx, ty = d.centre("inspector")
    d.drag(tx, ty, leaf_at("editor", 0.95, 0.5))       # its right edge, on the screen
    k = d.kept()
    row = k["kids"][0]
    check(row.get("dir") == "row" and tabs(row)[0] == (["inspector"], 0),
          f"a tab dropped on a leaf's right edge goes before it, which is right: {tabs(k)}")


os.makedirs(OUT, exist_ok=True)

for fn in (starts, raises, closes_and_reopens, drags_beside, drags_onto_a_strip,
           drags_into, escape_cancels, chords, tab_keys, divider, tab_menu,
           drops_on_the_drawer, split_chords, tab_order):
    print(f"# {fn.__name__}")
    scenario(fn)

# On X11 with no window manager, GTK puts a right-to-left window at
# x = 1 - width, off the screen; sway puts it where it goes.
print("# floats")
scenario(floats)

print("# drags_out_and_in")
scenario(drags_out_and_in)

print("# floats_kept")
scenario(floats_kept, env={"MLN_DEMO_LAYOUT": json.dumps({
    "layout": {"dir": "col", "size": [0.7, 0.3], "kids": [
        {"tabs": ["editor", "drawing"]}, {"tabs": ["console"]}]},
    "floating": [{"layout": {"tabs": ["inspector"]}, "size": [300, 200]}]})})

print("# placed")
scenario(placed, env={"MLN_DEMO_PLACEMENT": "1"})

print("# right_to_left")
if WAYLAND:
    scenario(right_to_left, env={"MLN_DEMO_RTL": "1"})
else:
    print("skip  right to left: Wayland only (MLN_WAYLAND=1); unmanaged X11 "
          "puts the window off the screen")

print(f"\n{failures} failed" if failures else "\nall passed")
sys.exit(1 if failures else 0)
