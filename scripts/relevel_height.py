#!/usr/bin/env python3
"""Re-level the height maps of materials.csv rows that carry depth=N.

The parallax march treats white as the wall's own plane and sinks everything
else behind it. A bake whose raised face is grey therefore draws the whole wall
some units behind its polygon, and at a corner its bands slide against the
next wall's (DOOM-0606). This stretches each height map so its raised face is
white and its deepest point black, and rewrites depth= to the relief that is
left, so the relief itself keeps its size.

The raised face is the 99th percentile, not the maximum: a rivet standing
proud of the face is flattened into it rather than holding the whole wall back.
Running it twice changes nothing.

    scripts/relevel_height.py [--dry] [NAME...]     default: every depth= row
"""
import os
import re
import sys

import numpy as np
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "ultra")
CSV = os.path.join(ROOT, "materials.csv")


def relevel(path, depth, dry):
    """Stretch one map; return the depth that keeps its relief the same size."""
    img = Image.open(path)
    a = np.asarray(img.convert("L"), dtype=np.float64) / 255.0
    lo, hi = float(a.min()), float(np.percentile(a, 99))
    if hi - lo < 1.0 / 255.0 or (lo == 0.0 and hi == 1.0):
        return depth
    if not dry:
        out = np.rint(np.clip((a - lo) / (hi - lo), 0.0, 1.0) * 255.0).astype(np.uint8)
        Image.fromarray(out, "L").convert(img.mode).save(path, optimize=True)
    return round(depth * (hi - lo), 1)


def main():
    args = sys.argv[1:]
    dry = "--dry" in args
    names = {a.upper() for a in args if not a.startswith("--")}
    lines = open(CSV).read().splitlines()
    for i, line in enumerate(lines):
        col = line.split(",")
        m = re.search(r"depth=([0-9.]+)", line)
        if line.startswith("#") or len(col) < 11 or not m or not col[8]:
            continue
        if names and col[0].upper() not in names:
            continue
        old = float(m.group(1))
        new = relevel(os.path.join(ROOT, col[8]), old, dry)
        if new != old:
            lines[i] = line[:m.start()] + ("depth=%g" % new) + line[m.end():]
            print("%-9s depth %g -> %g" % (col[0], old, new))
    if not dry:
        open(CSV, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
