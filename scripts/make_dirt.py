#!/usr/bin/env python3
"""Generate a rich, tileable DIRT colour texture for the DOOM-0181 grime layer.

The in-shader procedural dirt read as uniform because it derived from one noise
pattern tinted brown. This bakes a real dirt IMAGE with organic multi-scale
structure + tonal and hue variation, so sampling it in world space gives the
mottled, non-uniform look of a photographed dirt texture (the "dirty brown
texture" reference the user pointed at). First-party, CC0.

Method: FFT-filtered fractal noise (naturally seamless/tileable) at several
spectral slopes — broad tonal regions, mid blotches, fine grain, plus a faint
anisotropic streak field — composited and mapped through a warm-brown ramp with
occasional reddish/soot shifts. Deterministic (fixed seed).

    python3 scripts/make_dirt.py            # writes assets/ultra/overlays/dirt.png
"""
import os
import numpy as np
from PIL import Image

N = 1024
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "ultra")
OUT = os.path.join(ROOT, "overlays", "dirt.png")

rng = np.random.default_rng(20260716)


def fractal(beta, ax=1.0, ay=1.0):
    """Seamless fractal field in [0,1]: white noise shaped by 1/f**beta in the
    frequency domain (ax/ay bias the slope per-axis for streaks). FFT => periodic."""
    white = rng.standard_normal((N, N))
    F = np.fft.fft2(white)
    fx = np.fft.fftfreq(N)[:, None]
    fy = np.fft.fftfreq(N)[None, :]
    f = np.sqrt((ax * fx) ** 2 + (ay * fy) ** 2)
    f[0, 0] = 1.0
    out = np.real(np.fft.ifft2(F / (f ** beta)))
    out -= out.min()
    return out / (out.max() + 1e-9)


tone   = fractal(2.7)                 # large soft tonal regions
blot   = fractal(2.0)                 # mid blotches
grain  = fractal(0.9)                 # fine grain
streak = fractal(2.4, ax=0.35, ay=2.2)  # faint vertical weathering streaks
redf   = fractal(2.3)                 # where rusty/reddish patches sit

# Composite dirt density (broad tone dominates, blotches + streaks break it up)
d = 0.55 * tone + 0.28 * blot + 0.10 * streak + 0.07 * grain
d = (d - d.min()) / (d.max() - d.min())
d = np.clip(d ** 1.15, 0, 1)          # slight contrast so patches read

# Warm-brown ramp (dark earth -> brown -> ochre), linear-ish sRGB values
xp = [0.0, 0.30, 0.60, 0.85, 1.0]
r = np.interp(d, xp, [0.09, 0.24, 0.40, 0.52, 0.60])
g = np.interp(d, xp, [0.05, 0.14, 0.26, 0.36, 0.44])
b = np.interp(d, xp, [0.03, 0.08, 0.14, 0.20, 0.27])

# Rusty/reddish patches (push red, drop green) where redf is high and it's mid-tone
rust = np.clip((redf - 0.6) * 2.5, 0, 1) * np.clip(1.0 - abs(d - 0.5) * 2, 0, 1)
r += 0.10 * rust
g -= 0.04 * rust

# Occasional dark soot cores from the blotch extremes
soot = np.clip((0.22 - blot) * 4.0, 0, 1)
for ch in (r, g, b):
    ch *= (1.0 - 0.55 * soot)

# Fine grain brightness + a touch of desaturated cool grit in the very darkest
grainm = 0.88 + 0.24 * grain
r *= grainm; g *= grainm; b *= grainm

rgb = np.clip(np.stack([r, g, b], axis=-1), 0, 1)
img = (rgb * 255 + 0.5).astype(np.uint8)
os.makedirs(os.path.dirname(OUT), exist_ok=True)
Image.fromarray(img, "RGB").save(OUT)
print(f"wrote {OUT}  {N}x{N}  mean_rgb={img.reshape(-1,3).mean(0).round(1)}")
