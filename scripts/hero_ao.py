#!/usr/bin/env python3
"""Derive ambient-occlusion maps for the curated CC0 hero materials from their
authored height maps, reusing pbr_derive.py's exact horizon-based `derive_ao`.

Why this exists: the hero rows in materials.csv ship albedo/normal/height but no
AO, so DOOM-0181's L4 "filth" layer (crevice-pooled grime, which reads hdAO) stays
dormant on every hero. This bakes an AO map from each hero's own height relief so
the dirt actually pools in the recesses — and, as a bonus, deepens the ambient
contact-darkening in those same crevices.

AO is a low-frequency signal, so we downscale the 1024² height to 256² before the
(pure-Python, O(w*h*8*radius)) horizon walk — same radius the WAD-derived AO maps
use, whose look is already signed off. Output lands next to the hero maps (tracked,
unlike derived/). Prints the CSV ao-column value for each so materials.csv can be
wired by hand.

    python3 scripts/hero_ao.py            # generate for every hero missing AO
"""
import csv
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pbr_derive import derive_ao, write_png  # reuse the exact derived-material AO

ASSET_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "assets", "ultra")
# AO is a broad, low-frequency signal, so we work at 384² (below the 1024² source). The
# hero height maps are gentle bump maps, so radius+strength are pushed past pbr_derive's
# WAD defaults to make recesses pool visibly (min ~120-160, matching the signed-off
# TEKWALL1 depth) while flat areas stay clean. Tuned against a histogram sweep 2026-07-16.
AO_RES = 384
AO_RADIUS = 12
AO_STRENGTH = 5.0


def bake(height_rel):
    """height_rel -> (ao_rel, wrote_path). AO PNG sits beside the height map."""
    height_path = os.path.join(ASSET_ROOT, height_rel)
    im = Image.open(height_path).convert("L").resize((AO_RES, AO_RES), Image.LANCZOS)
    # derive_ao reads an RGB buffer and lumas it back to a height field; a grayscale
    # height triplicated to RGB round-trips to exactly the authored height value.
    lum = im.tobytes()
    rgb = bytes(b for v in lum for b in (v, v, v))
    ao_bytes, _ = derive_ao(rgb, AO_RES, AO_RES, radius=AO_RADIUS, strength=AO_STRENGTH)

    ao_rel = height_rel.replace("_hgt.png", "_ao.png")
    write_png(os.path.join(ASSET_ROOT, ao_rel), AO_RES, AO_RES, ao_bytes, 1)
    return ao_rel


def main():
    csv_path = os.path.join(ASSET_ROOT, "materials.csv")
    with open(csv_path) as fh:
        rows = [r for r in csv.reader(fh) if r and not r[0].startswith("#")]

    for r in rows:
        name, source, ao_col, height_col = r[0], r[1], r[6], r[8]
        if source != "hero" or not height_col or ao_col:
            continue
        ao_rel = bake(height_col)
        print(f"{name}: {ao_rel}")


if __name__ == "__main__":
    main()
