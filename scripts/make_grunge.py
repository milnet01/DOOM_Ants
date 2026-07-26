#!/usr/bin/env python3
"""DOOM-0179: generate the world-space grime overlay for the Ultra RT view.

A single low-frequency, seamlessly-TILEABLE grayscale map. The path tracer samples
it by WORLD position (not tile UV), so two walls that share the same HD texture pick
up grime from different world coordinates and stop looking identical — killing the
visible repetition the base tiling produces.

First-party asset: authored here (procedural), committed CC0 — no third-party
redistribution question in the public repo. Mean is normalised to ~0.5 so the shader's
centred blend (m = 1 + (g-0.5)*2*strength) neither brightens nor darkens the scene on
average — it only mottles it. Output: assets/ultra/overlays/grunge.png (8-bit L, 1024).

Tileability: the fractal base is built in the frequency domain (irfft2 of a 1/f^beta
spectrum), which is periodic by construction; the specks are stamped with wrap-around
indexing. So world-position REPEAT sampling has no seam at the 384-unit tile edge.

Usage:  python3 scripts/make_grunge.py            # regenerate the committed overlay
        python3 scripts/make_grunge.py --size 512 # smaller (faster) variant
"""
import argparse
import os
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT  = os.path.join(ROOT, "assets", "ultra", "overlays", "grunge.png")


def fractal_field(n, beta, rng):
    """Periodic fractal noise: random-phase spectrum with a 1/f^beta amplitude falloff,
    inverse-FFT'd to a real NxN field. Periodic (seamless) because the DFT basis is."""
    fy = np.fft.fftfreq(n)[:, None]
    fx = np.fft.rfftfreq(n)[None, :]
    f  = np.sqrt(fy * fy + fx * fx)
    f[0, 0] = 1.0                                   # avoid div-by-zero at DC
    amp = 1.0 / np.power(f, beta)
    amp[0, 0] = 0.0                                 # drop DC (we set the mean ourselves)
    phase = rng.uniform(0.0, 2.0 * np.pi, size=amp.shape)
    spec  = amp * (np.cos(phase) + 1j * np.sin(phase))
    field = np.fft.irfft2(spec, s=(n, n))
    field -= field.min()
    field /= max(field.max(), 1e-6)
    return field                                    # 0..1, periodic


def stamp_specks(field, count, radius, strength, rng):
    """Darken with `count` soft round specks (grime spots / small marks), wrapped at the
    edges so the tile stays seamless. Each speck subtracts a Gaussian dab."""
    n = field.shape[0]
    yy, xx = np.mgrid[-radius:radius + 1, -radius:radius + 1]
    kernel = np.exp(-(xx * xx + yy * yy) / (2.0 * (radius * 0.5) ** 2))
    for _ in range(count):
        cy, cx = rng.integers(0, n), rng.integers(0, n)
        ys = (np.arange(-radius, radius + 1) + cy) % n
        xs = (np.arange(-radius, radius + 1) + cx) % n
        field[np.ix_(ys, xs)] -= kernel * strength * rng.uniform(0.5, 1.0)
    return np.clip(field, 0.0, 1.0)


def main():
    ap = argparse.ArgumentParser(description="Generate the DOOM-0179 world-space grime overlay")
    ap.add_argument("--size", type=int, default=1024)
    ap.add_argument("--seed", type=int, default=1997)          # DOOM's release year, for luck
    ap.add_argument("--out",  default=OUT)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    n   = args.size

    # Large soft blotches (dirt drift) over finer grain, then sparse specks (marks).
    base  = 0.65 * fractal_field(n, 2.6, rng) + 0.35 * fractal_field(n, 1.7, rng)
    base  = (base - base.min()) / max(base.max() - base.min(), 1e-6)
    base  = np.power(base, 1.25)                               # bias toward clean, so grime reads as marks
    grime = stamp_specks(base, count=max(24, n // 20), radius=max(6, n // 90),
                         strength=0.55, rng=rng)

    # Normalise the MEAN to 0.5 (centred blend => no net brightness change), keep contrast sane.
    grime = 0.5 + (grime - grime.mean()) * 0.9
    grime = np.clip(grime, 0.04, 0.96)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    Image.fromarray((grime * 255.0 + 0.5).astype("uint8"), "L").save(args.out)
    print("wrote %s  (%dx%d, mean %.3f, min %.3f, max %.3f)"
          % (os.path.relpath(args.out, ROOT), n, n, grime.mean(), grime.min(), grime.max()))


if __name__ == "__main__":
    main()
