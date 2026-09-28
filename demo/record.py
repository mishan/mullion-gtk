#!/usr/bin/env python3
#
# Copyright (C) 2026 Misha Nasledov
#
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The README's demo.gif: the demo, driven in a sealed shotbox session
under metacity, recorded with ffmpeg.

    PYTHONPATH=path/to/shotbox record.py DEMO OUT.gif

Needs metacity, xdotool, xsetroot and ffmpeg besides what shotbox does.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

import shotbox
from shotbox import xtest

DEMO, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
W = "mullion-gtk"
SCREEN = (1440, 780)
CROP = "1284:634:0:0"          # the main window, and the floating one beside it


class Demo:
    def __init__(self, s, log):
        self.s, self.log = s, log
        self.proc = s.spawn([DEMO], log=log)
        s.wait_window(W)
        # Smaller than its own default, for the floating window to fit
        # beside it.
        s.run(["xdotool", "search", "--name", f"^{W}$",
               "windowsize", "%@", "820", "540", "windowmove", "%@", "24", "24"])
        time.sleep(0.8)
        self.settle()
        self.pos = (SCREEN[0] - 60, SCREEN[1] - 40)
        s.xt.move(*self.pos)

    def settle(self):
        self.s.wait_stable(window=W)
        time.sleep(0.2)

    def at(self, id, part="tab", fx=0.5, fy=0.5, win=W):
        """A point in pane `id`'s tab (or leaf, or drawer button), on the
        screen, from where the demo says everything is."""
        with open(self.log) as f:
            g = [l for l in f if l.startswith("geometry ")][-1]
        x, y, w, h = json.loads(g[len("geometry "):])[id][part]
        _, _, _, _, ox, oy = self.s.wait_window(win)
        return int(ox + x + w * fx), int(oy + y + h * fy)

    def glide(self, x, y, steps=25, dt=0.025):
        x0, y0 = self.pos
        for i in range(1, steps + 1):
            self.s.xt.move(x0 + (x - x0) * i // steps, y0 + (y - y0) * i // steps)
            time.sleep(dt)
        self.pos = (x, y)

    def press(self, down):
        self.s.xt._fake(xtest.BUTTON_PRESS if down else xtest.BUTTON_RELEASE, 1)
        self.s.xt.sync()

    def click(self, x, y, wait=0.8):
        self.glide(x, y)
        time.sleep(0.15)
        self.press(True)
        time.sleep(0.08)
        self.press(False)
        time.sleep(wait)

    def drag(self, x, y, to, wait=0.8):
        """Slowly, as XTEST drags have to be for GTK to see one; `to` is
        asked for once the drag has begun, when the drawer has come up."""
        self.glide(x, y)
        time.sleep(0.2)
        self.press(True)
        time.sleep(0.1)
        for i in range(1, 6):
            self.s.xt.move(x + 3 * i, y + 3 * i)
            time.sleep(0.03)
        self.pos = (x + 15, y + 15)
        time.sleep(0.35)
        self.glide(*(to() if callable(to) else to), 40, 0.03)
        time.sleep(0.6)
        self.press(False)
        time.sleep(0.6 + wait)


def story(d):
    d.click(*d.at("drawing"))
    # On the drawing's right edge: side by side.
    d.drag(*d.at("console"), lambda: d.at("drawing", "leaf", 0.93, 0.5))
    # In the console's middle: behind a tab.
    d.drag(*d.at("inspector"), lambda: d.at("console", "leaf", 0.5, 0.55))
    # Out of the window: a window of its own.
    _, _, w, _, x, y = d.s.wait_window(W)
    d.drag(*d.at("console"), (x + w + 60, y + 140), 0)
    d.s.wait_window("Console")
    d.settle()
    time.sleep(1.0)
    # And back, under the drawing.
    d.drag(*d.at("console", win="Console"), lambda: d.at("drawing", "leaf", 0.5, 0.93))
    # Closed by its cross, and put back from the drawer.
    x, y = d.at("inspector", fx=1)
    d.glide(x - 30, y)
    time.sleep(0.3)
    d.click(x - 14, y, 1.3)
    d.glide(*d.at("inspector", "closed"), 30)
    time.sleep(0.4)
    d.click(*d.at("inspector", "closed"), 1.2)


def main():
    work = tempfile.mkdtemp(prefix="mullion-gif-")
    raw = os.path.join(work, "raw.mkv")
    with shotbox.Session(size=SCREEN, env={"GDK_BACKEND": "x11", "GSK_RENDERER": "cairo",
                                           "MLN_DEMO_HEADERBAR": "1"}) as s:
        s.run(["xsetroot", "-solid", "#5a6270"])
        s.spawn(["metacity", "--replace", "--compositor=none"],
                log=os.path.join(work, "wm.log"))
        time.sleep(1.5)
        d = Demo(s, os.path.join(work, "demo.log"))
        rec = subprocess.Popen(["ffmpeg", "-y", "-loglevel", "error", "-f", "x11grab",
                                "-draw_mouse", "1", "-framerate", "20",
                                "-video_size", "%dx%d" % SCREEN, "-i", s.env["DISPLAY"],
                                "-c:v", "libx264rgb", "-crf", "0", "-preset", "ultrafast", raw],
                               env={**os.environ, **s.env}, stdin=subprocess.PIPE)
        time.sleep(1.2)
        story(d)
        time.sleep(1.5)
        rec.communicate(b"q")
        d.proc.terminate()
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", raw, "-vf",
                    f"crop={CROP},fps=15,scale=960:-1:flags=lanczos,split[a][b];"
                    "[a]palettegen=max_colors=64:stats_mode=diff[p];"
                    "[b][p]paletteuse=dither=none:diff_mode=rectangle", OUT], check=True)
    shutil.rmtree(work)
    print(OUT)


main()
