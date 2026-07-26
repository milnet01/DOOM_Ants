#!/usr/bin/env python3
"""DOOM-0042 T17: stage a curated CC0 PBR set as an Ultra "hero" material.

Extracts the PBR maps from a Poly-Haven-style `.blend.zip` (or a folder of loose
maps), downscales to <=1024, converts the EXR normal/height to 8-bit PNG WITHOUT a
gamma transform (a normal map's RGB are vector components, not colour), and writes
them into assets/ultra/heroes/<family>/<doom_name>_<suffix>.png — the paths a hero
row in materials.csv points at. Also prints the ready-to-paste CSV row + LICENSES
line so provenance is recorded (every committed hero PNG traces to a CC0 source).

v1 uploads albedo/normal/AO/height (roughness/metallic are baked by DOOM-0103, not
here). Map name matching (Poly Haven convention):
  diff|albedo|col -> alb (sRGB)   nor_gl|normal -> nrm (linear, no gamma)
  ao|arm.R        -> ao  (linear) disp|height|displacement -> hgt (linear)

Usage:
  python3 scripts/stage_hero.py --zip "<lib>/Metal/metal_plate_02_4k.blend.zip" \\
        --name STARTAN3 --family tech --url https://polyhaven.com/a/metal_plate_02
  python3 scripts/stage_hero.py --dir path/to/loose/maps --name FOO --family stone --url ...
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import zipfile
import glob
import importlib.util

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEROES = os.path.join(ROOT, "assets", "ultra", "heroes")
LICENSES = os.path.join(ROOT, "assets", "ultra", "LICENSES")
MAXPX = 1024

# suffix -> (regex over the source filename, is-sRGB, is-grayscale-single-channel)
MAP_RULES = [
    ("alb", re.compile(r"(diff|albedo|_col|basecolor|base_color)", re.I), True,  False),
    ("nrm", re.compile(r"(nor_gl|normal_gl|_nor|normal)",          re.I), False, False),
    ("ao",  re.compile(r"(_ao|ambient.?occl)",                     re.I), False, True),
    ("hgt", re.compile(r"(disp|height|displacement|_bump)",        re.I), False, True),
]


def magick(args):
    subprocess.run(["magick"] + args, check=True)


def tint_to_doom(alb_path, wad_path, doom_name, chroma_keep=0.12):
    """Palette-lock: recolor the HD albedo to the DOOM texture's average COLOUR while keeping the
    HD luminance detail. tinted = HD_luma * (doom_mean / doom_luma), so the result carries DOOM's
    exact hue (STARTAN tan, FLAT14 blue, BROWNGRN green) and can NEVER blow out — it's a grayscale
    times a bounded colour vector. A little of the HD map's own chroma is blended back so metals
    don't go dead-flat. Only the albedo is tinted; normal/height/AO are structure, left as-is."""
    import numpy as np
    from PIL import Image
    spec = importlib.util.spec_from_file_location("pbr", os.path.join(ROOT, "scripts", "pbr_derive.py"))
    pbr = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pbr)
    rgb, (w, h) = pbr.Wad(wad_path).image_rgb(doom_name)
    W = np.array([0.299, 0.587, 0.114], np.float32)
    ref = np.asarray(Image.frombytes("RGB", (w, h), bytes(rgb)), np.float32).reshape(-1, 3)
    doom_mean = ref.mean(0)                             # DOOM average colour (r,g,b)
    doom_lum = max(float(doom_mean @ W), 1e-3)
    factor = doom_mean / doom_lum                       # hue direction, luminance-normalised (~0.5..1.6)
    src = np.asarray(Image.open(alb_path).convert("RGB"), np.float32)
    gray = (src @ W)[..., None]                         # HD luminance detail (HxWx1)
    tinted = gray * factor                              # DOOM hue * HD detail
    out = (1.0 - chroma_keep) * tinted + chroma_keep * src * (doom_lum / max(float(src.reshape(-1,3).mean(0) @ W), 1e-3))
    Image.fromarray(np.clip(out, 0, 255).astype("uint8"), "RGB").save(alb_path)
    print("  tint <- DOOM %s palette (luma x mean-colour)" % doom_name)


def convert_map(src, dst, srgb, gray):
    """Downscale to <=MAXPX and write 8-bit PNG. Non-sRGB maps (normal/ao/height) are
    passed through with NO colorspace transform so their raw values survive quantization."""
    a = [src]
    if not srgb:
        a += ["-set", "colorspace", "RGB", "-colorspace", "RGB"]   # treat values as data, not colour
    a += ["-resize", "%dx%d>" % (MAXPX, MAXPX)]                    # '>' = only shrink, never enlarge
    if gray:
        a += ["-colorspace", "Gray"]
    a += ["-depth", "8", dst]
    magick(a)


def collect_sources(zip_path, dir_path):
    tmp = None
    if zip_path:
        tmp = tempfile.mkdtemp(prefix="hero_")
        with zipfile.ZipFile(zip_path) as z:
            for n in z.namelist():
                if n.lower().endswith((".png", ".jpg", ".jpeg", ".exr")) and not n.endswith("/"):
                    z.extract(n, tmp)
        files = glob.glob(os.path.join(tmp, "**", "*.*"), recursive=True)
    else:
        files = glob.glob(os.path.join(dir_path, "**", "*.*"), recursive=True)
    return [f for f in files if f.lower().endswith((".png", ".jpg", ".jpeg", ".exr"))], tmp


def main():
    ap = argparse.ArgumentParser(description="Stage a CC0 PBR set as a DOOM-0042 hero material")
    ap.add_argument("--zip", help="path to a .blend.zip (Poly Haven style)")
    ap.add_argument("--dir", help="folder of loose PBR maps (alternative to --zip)")
    ap.add_argument("--name", required=True, help="DOOM texture/flat name (e.g. STARTAN3)")
    ap.add_argument("--family", required=True, help="hero subfolder (e.g. tech, brownmetal, stone, floor)")
    ap.add_argument("--url", default="", help="CC0 source URL for the LICENSES line")
    ap.add_argument("--uvscale", default="1.0")
    ap.add_argument("--flags", default="pom", help="materials.csv flags (pom|noPom)")
    ap.add_argument("--tint-wad", help="WAD path; recolor the albedo to the DOOM texture's palette")
    args = ap.parse_args()
    if not args.zip and not args.dir:
        ap.error("need --zip or --dir")

    srcs, tmp = collect_sources(args.zip, args.dir)
    outdir = os.path.join(HEROES, args.family)
    os.makedirs(outdir, exist_ok=True)
    rel = {}                                          # suffix -> repo-relative hero path
    for suffix, rx, srgb, gray in MAP_RULES:
        match = next((s for s in srcs if rx.search(os.path.basename(s))), None)
        if not match:
            continue
        dst = os.path.join(outdir, "%s_%s.png" % (args.name, suffix))
        convert_map(match, dst, srgb, gray)
        rel[suffix] = os.path.relpath(dst, os.path.join(ROOT, "assets", "ultra"))
        print("  %-4s <- %s" % (suffix, os.path.basename(match)))

    if "alb" not in rel:
        print("ERROR: no albedo/diffuse map found in the source", file=sys.stderr)
        sys.exit(1)

    if args.tint_wad:
        tint_to_doom(os.path.join(ROOT, "assets", "ultra", rel["alb"]), args.tint_wad, args.name)

    # materials.csv column order: name,source,albedo,normal,roughness,metallic,ao,emissive,height,uv,flags
    row = [args.name, "hero", rel.get("alb", ""), rel.get("nrm", ""), "", "",
           rel.get("ao", ""), "", rel.get("hgt", ""), args.uvscale, args.flags]
    print("\nCSV row:\n" + ",".join(row))
    print("\nLICENSES lines:")
    for suffix in ("alb", "nrm", "ao", "hgt"):
        if suffix in rel:
            print("%s  %s  CC0" % (rel[suffix], args.url or "<source-url>"))
    if tmp:
        subprocess.run(["rm", "-rf", tmp])


if __name__ == "__main__":
    main()
