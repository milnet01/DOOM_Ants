#!/usr/bin/env python3
"""Compare two ab_capture.sh frames against a same-build control.

    ab_diff.py <on.png> <off.png> <control.png> [x0,y0,x1,y1,label ...]

SIGNAL is |on - off|; NOISE is |on - control|. Quote the two together and never
the first alone — a signal only means something relative to what the harness
moves on its own. With -inspect -freeze the noise floor lands at 0.01/255.

The block map exists so "where did it move" is READ rather than guessed; pick
the named regions from it afterwards, not from memory of the screenshot. The
per-channel means are what catch a coloured cast that a scalar delta hides
(DOOM-0330: a ceiling moved 4.53 overall but its GREEN channel rose 77%).

Built for DOOM-0330; nothing in it is specific to that.
"""
import sys
import numpy as np
from PIL import Image

STATUS_BAR = 0.805      # crop it: the HUD is not part of the look


def load(path):
    a = np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)
    return a[:int(a.shape[0] * STATUS_BAR)]


on, off, ctrl = (load(p) for p in sys.argv[1:4])
sig = np.abs(on - off).mean(axis=2)
noise = np.abs(on - ctrl).mean(axis=2)
H, W = sig.shape

print(f"frame {W}x{H} (status bar cropped)")
for name, d in (("SIGNAL on-vs-off ", sig), ("NOISE  on-vs-ctrl", noise)):
    print(f"{name}: mean {d.mean():6.2f}/255  max {d.max():6.1f}  "
          f"pixels>2: {100 * (d > 2).mean():5.1f}%")

bh, bw = H // 8, W // 12
for name, d in (("SIGNAL", sig), ("NOISE", noise)):
    print(f"\n{name} block map (mean delta/255, top row first):")
    for r in range(8):
        print("  " + " ".join(f"{d[r * bh:(r + 1) * bh, c * bw:(c + 1) * bw].mean():5.1f}"
                              for c in range(12)))

if len(sys.argv) > 4:
    print("\nregion                          signal  noise   meanRGB(on)         meanRGB(off)")
    for spec in sys.argv[4:]:
        x0, y0, x1, y1, label = spec.split(",", 4)
        x0, y0, x1, y1 = (int(v) for v in (x0, y0, x1, y1))
        a = on[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)
        b = off[y0:y1, x0:x1].reshape(-1, 3).mean(axis=0)
        print(f"{label:30s} {sig[y0:y1, x0:x1].mean():6.2f} {noise[y0:y1, x0:x1].mean():6.2f}"
              f"  ({a[0]:5.1f},{a[1]:5.1f},{a[2]:5.1f})   ({b[0]:5.1f},{b[1]:5.1f},{b[2]:5.1f})")
