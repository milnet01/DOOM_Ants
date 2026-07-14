#!/usr/bin/env python3
"""DOOM-0042 offline PBR-map generator — fills E1M1's long tail (the `derive` rows in
assets/ultra/materials.csv) with per-texture PBR maps synthesized from the WAD art.

This is a STANDALONE offline tool, NOT built into the engine: it reads the IWAD with its
own pure-stdlib lump/PLAYPAL/texture parser (the engine's C W_CacheLumpName isn't reusable
offline) and writes assets/ultra/derived/<doom_name>_<suffix>.png for each derive row. The
derived/ output is gitignored (a derivative work of the WAD art), exactly like the WAD.

Usage:
  python3 scripts/pbr_derive.py --dump STARTAN3 --wad wads/doom.wad
  python3 scripts/pbr_derive.py --wad wads/doom.wad --csv assets/ultra/materials.csv \\
                                --out assets/ultra/derived

Stages (each runnable): (1) WAD reader, (2) map derivation from an albedo image, (3) CSV
driver. No external dependency — a minimal PNG writer + hand-rolled DOOM picture decoder.
"""
import struct, zlib, os, sys, math, argparse


# ---------------------------------------------------------------------------
# PNG writer — grayscale (1ch), RGB (3ch), or RGBA (4ch) from a flat byte buffer.
# (make_bringup_hero.py has a callback-style writer; this array-style one is the
# natural fit for derived maps. Two standalone tools, two ~15-line stdlib writers —
# below the Rule-of-Three extraction bar.)
# ---------------------------------------------------------------------------
def write_png(path, w, h, pixels, channels):
    color_type = {1: 0, 3: 2, 4: 6}[channels]

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    stride = w * channels
    raw = bytearray()
    for y in range(h):
        raw.append(0)                                  # filter type 0 (none) per scanline
        raw += bytes(pixels[y * stride:(y + 1) * stride])
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, color_type, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(png)


# ---------------------------------------------------------------------------
# Stage 1: WAD reader — lump directory, PLAYPAL, flats, composited wall textures.
# ---------------------------------------------------------------------------
class Wad:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        magic, numlumps, dirofs = struct.unpack_from("<4sii", self.data, 0)
        if magic not in (b"IWAD", b"PWAD"):
            raise ValueError("not a WAD (magic %r)" % magic)
        self.lumps = {}                                # NAME -> (filepos, size); last wins (PWAD override)
        self.flats = set()                             # names inside F_START..F_END (the flat namespace)
        in_flats = False
        off = dirofs
        for _ in range(numlumps):
            filepos, size = struct.unpack_from("<ii", self.data, off)
            name = self.data[off + 8:off + 16].split(b"\0")[0].decode("ascii", "replace").upper()
            off += 16
            # Flat namespace tracking: only lumps between the F_START/F_END (or PWAD FF_*)
            # markers are flats. The nested F1_/F2_/F3_ sub-markers sit inside that span and
            # are size 0, so the `size == 4096` guard skips them without extra bookkeeping.
            if name in ("F_START", "FF_START"):
                in_flats = True
            elif name in ("F_END", "FF_END"):
                in_flats = False
            elif in_flats and size == 4096:
                self.flats.add(name)
            self.lumps[name] = (filepos, size)
        pp = self._lump("PLAYPAL")
        self.palette = [(pp[i * 3], pp[i * 3 + 1], pp[i * 3 + 2]) for i in range(256)]
        self._load_pnames_textures()

    def _lump(self, name):
        p, s = self.lumps[name.upper()]
        return self.data[p:p + s]

    def has(self, name):
        return name.upper() in self.lumps

    # --- PNAMES + TEXTURE1/TEXTURE2: patch names + composited-texture directory ---
    def _load_pnames_textures(self):
        pn = self._lump("PNAMES")
        (npatches,) = struct.unpack_from("<i", pn, 0)
        self.pnames = []
        for i in range(npatches):
            nm = pn[4 + i * 8:4 + i * 8 + 8].split(b"\0")[0].decode("ascii", "replace").upper()
            self.pnames.append(nm)
        self.textures = {}                             # NAME -> maptexture dict
        for tlump in ("TEXTURE1", "TEXTURE2"):
            if not self.has(tlump):
                continue
            t = self._lump(tlump)
            (numtex,) = struct.unpack_from("<i", t, 0)
            offsets = struct.unpack_from("<%di" % numtex, t, 4)
            for o in offsets:
                name = t[o:o + 8].split(b"\0")[0].decode("ascii", "replace").upper()
                width, height = struct.unpack_from("<hh", t, o + 12)   # skip masked(int) at o+8
                (patchcount,) = struct.unpack_from("<h", t, o + 20)    # skip columndirectory(int) at o+16
                patches = []
                po = o + 22
                for _ in range(patchcount):
                    ox, oy, pidx = struct.unpack_from("<hhh", t, po)   # originx, originy, patch index
                    po += 10                                           # + stepdir, colormap (2 shorts)
                    patches.append((ox, oy, pidx))
                self.textures[name] = dict(w=width, h=height, patches=patches)

    # --- decode one DOOM picture (patch_t) to a (indices, mask, w, h) tuple ---
    def _decode_patch(self, lumpname):
        d = self._lump(lumpname)
        w, h, _lo, _to = struct.unpack_from("<hhhh", d, 0)
        colofs = struct.unpack_from("<%di" % w, d, 8)
        idx = bytearray(w * h)
        mask = bytearray(w * h)                        # 1 = opaque texel written
        for col in range(w):
            p = colofs[col]
            while True:
                topdelta = d[p]
                if topdelta == 0xFF:
                    break
                length = d[p + 1]
                p += 3                                 # topdelta, length, unused pre-pad byte
                for row in range(length):
                    y = topdelta + row
                    if 0 <= y < h:
                        idx[y * w + col] = d[p + row]
                        mask[y * w + col] = 1
                p += length + 1                        # + post's trailing unused byte
        return idx, mask, w, h

    # --- composited wall texture -> RGB bytes + (w,h) ---
    def texture_rgb(self, name):
        tex = self.textures[name.upper()]
        w, h = tex["w"], tex["h"]
        idx = bytearray(w * h)                         # transparent gaps stay index 0 (black)
        for ox, oy, pidx in tex["patches"]:
            pname = self.pnames[pidx]
            pidxbuf, pmask, pw, ph = self._decode_patch(pname)
            for py in range(ph):
                ty = oy + py
                if ty < 0 or ty >= h:
                    continue
                for px in range(pw):
                    if not pmask[py * pw + px]:
                        continue
                    tx = ox + px
                    if 0 <= tx < w:
                        idx[ty * w + tx] = pidxbuf[py * pw + px]
        return self._indices_to_rgb(idx, w, h), (w, h)

    # --- raw 64x64 flat -> RGB bytes + (w,h) ---
    def flat_rgb(self, name):
        nm = name.upper()
        if nm not in self.flats:
            raise KeyError("%s is not a flat (not inside F_START..F_END)" % nm)
        d = self._lump(nm)
        if len(d) < 64 * 64:
            raise ValueError("flat %s is %d bytes, expected 4096" % (nm, len(d)))
        return self._indices_to_rgb(d, 64, 64), (64, 64)

    # --- auto: composited wall texture, else a flat; unknown names raise (caller skips) ---
    def image_rgb(self, name):
        nm = name.upper()
        if nm in self.textures:
            return self.texture_rgb(nm)
        if nm in self.flats:
            return self.flat_rgb(nm)
        raise KeyError("%s: neither a composited texture nor a flat" % nm)

    def _indices_to_rgb(self, idx, w, h):
        out = bytearray(w * h * 3)
        pal = self.palette
        for i in range(w * h):
            r, g, b = pal[idx[i]]
            out[i * 3] = r
            out[i * 3 + 1] = g
            out[i * 3 + 2] = b
        return out


# ---------------------------------------------------------------------------
# Stage 2: map derivation from an RGB albedo image (flat RGB byte buffer, w, h).
# ---------------------------------------------------------------------------
# BT.601 luma weights for the HEIGHT field (the brief mandates exactly these). NOT the
# engine's Rec.709 emissive weights (0.2126/0.7152/0.0722) — emissive uses a separate
# max-channel value() gate below, mirroring emissive_derive.h. Keep these 601 weights.
_LUM = (0.299, 0.587, 0.114)


def _srgb2lin(c):                                      # c in [0,1]
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def _height_field(rgb, w, h):
    """Per-texel luminance in [0,1]; brighter = raised (matches the shader's white=raised)."""
    hf = [0.0] * (w * h)
    for i in range(w * h):
        r, g, b = rgb[i * 3] / 255.0, rgb[i * 3 + 1] / 255.0, rgb[i * 3 + 2] / 255.0
        hf[i] = _LUM[0] * r + _LUM[1] * g + _LUM[2] * b
    return hf


def _wrap(v, n):                                       # DOOM textures tile -> wrap sampling
    return v % n


def derive_height(rgb, w, h):
    hf = _height_field(rgb, w, h)
    return bytes(int(max(0.0, min(1.0, v)) * 255 + 0.5) for v in hf), 1


def derive_normal(rgb, w, h, strength=2.0):
    """Sobel gradient of the height field, encoded OpenGL Y+ (rgb = (-gx,-gy,1)/|.| *0.5+0.5).
    n.y is kept un-flipped to match the shader's Y+ tangent-space unpack. Texture wraps."""
    hf = _height_field(rgb, w, h)
    out = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            def hs(dx, dy):
                return hf[_wrap(y + dy, h) * w + _wrap(x + dx, w)]
            gx = (hs(1, -1) + 2 * hs(1, 0) + hs(1, 1)) - (hs(-1, -1) + 2 * hs(-1, 0) + hs(-1, 1))
            gy = (hs(-1, 1) + 2 * hs(0, 1) + hs(1, 1)) - (hs(-1, -1) + 2 * hs(0, -1) + hs(1, -1))
            nx, ny, nz = -gx * strength, -gy * strength, 1.0
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            nx, ny, nz = nx * inv, ny * inv, nz * inv
            o = (y * w + x) * 3
            out[o] = int((nx * 0.5 + 0.5) * 255 + 0.5)
            out[o + 1] = int((ny * 0.5 + 0.5) * 255 + 0.5)
            out[o + 2] = int((nz * 0.5 + 0.5) * 255 + 0.5)
    return bytes(out), 3


def derive_ao(rgb, w, h, radius=4):
    """Horizon-based local occlusion of the height field: for 8 compass directions, find the
    max elevation angle blocking the horizon within `radius` texels; AO = 1 - mean(sin angle).
    A texel sunk below its neighbours darkens; an open peak stays ~1. Texture wraps."""
    hf = _height_field(rgb, w, h)
    dirs = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]
    out = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            hc = hf[y * w + x]
            occ = 0.0
            for dx, dy in dirs:
                horizon = 0.0
                dlen = math.sqrt(dx * dx + dy * dy)
                for r in range(1, radius + 1):
                    dh = hf[_wrap(y + dy * r, h) * w + _wrap(x + dx * r, w)] - hc
                    if dh > 0.0:
                        horizon = max(horizon, dh / (r * dlen))   # tan(elevation)
                occ += horizon / math.sqrt(1.0 + horizon * horizon)  # sin(atan(horizon))
            ao = 1.0 - occ / len(dirs)
            out[y * w + x] = int(max(0.0, min(1.0, ao)) * 255 + 0.5)
    return bytes(out), 1


# Family-prefix roughness/metallic table (longest matching prefix wins). Uploaded to the
# rough/metal slots only once DOOM-0103 lands; baked here so the derive set is complete.
_FAMILY = [
    ("METAL TEK SILVER SHAWN SUPPORT", 0.35, 1.0),
    ("BROWN BRONZE COMP PIPE",         0.55, 1.0),
    ("BRICK STONE ROCK GRAY MARB",     0.85, 0.0),
    ("WOOD PANEL DOOR",                0.75, 0.0),
    ("FLOOR FLAT CEIL RROCK MFLR",     0.80, 0.0),
]


def _family_rough_metal(name):
    best = (0, 0.70, 0.0)                              # (matched-prefix-len, rough, metal) default
    for prefixes, rough, metal in _FAMILY:
        for pfx in prefixes.split():
            if name.upper().startswith(pfx) and len(pfx) > best[0]:
                best = (len(pfx), rough, metal)
    return best[1], best[2]


def derive_roughness(rgb, w, h, name):
    r, _ = _family_rough_metal(name)
    return bytes([int(r * 255 + 0.5)]) * (w * h), 1


def derive_metallic(rgb, w, h, name):
    _, m = _family_rough_metal(name)
    return bytes([int(m * 255 + 0.5)]) * (w * h), 1


def derive_emissive(rgb, w, h):
    """Black except at near-fullbright texels (value = max channel >= peak gate, mirroring
    emissive_derive.h): a bright texel keeps its sRGB albedo colour, all others go black. A
    texture with no fullbright region yields an all-black map (no glow) — the common case."""
    peak = 0.9                                         # emissive_derive.h kEmitterPeakLum
    out = bytearray(w * h * 3)
    for i in range(w * h):
        r, g, b = rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]
        val = max(_srgb2lin(r / 255.0), _srgb2lin(g / 255.0), _srgb2lin(b / 255.0))
        if val >= peak:
            out[i * 3], out[i * 3 + 1], out[i * 3 + 2] = r, g, b
    return bytes(out), 3


def derive_albedo(rgb, w, h, sprite=False):
    """Albedo = the WAD RGB verbatim (RGB; sprite rows would carry index-0 transparency into
    alpha, but v1 has no sprite derive rows, so this is the plain-RGB path)."""
    return rgb, 3


# suffix -> deriver. Signature (rgb, w, h) except roughness/metallic (need the name).
_DERIVERS = {
    "alb": lambda rgb, w, h, name: derive_albedo(rgb, w, h),
    "nrm": lambda rgb, w, h, name: derive_normal(rgb, w, h),
    "rgh": lambda rgb, w, h, name: derive_roughness(rgb, w, h, name),
    "met": lambda rgb, w, h, name: derive_metallic(rgb, w, h, name),
    "ao":  lambda rgb, w, h, name: derive_ao(rgb, w, h),
    "emis": lambda rgb, w, h, name: derive_emissive(rgb, w, h),
    "hgt": lambda rgb, w, h, name: derive_height(rgb, w, h),
}


# ---------------------------------------------------------------------------
# Stage 3: CSV driver — emit all seven maps for every `derive` row.
# ---------------------------------------------------------------------------
def parse_derive_rows(csv_path):
    names = []
    with open(csv_path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            cols = line.split(",")
            if len(cols) >= 2 and cols[1].strip() == "derive":
                names.append(cols[0].strip())
    return names


def emit_maps(wad, name, out_dir):
    rgb, (w, h) = wad.image_rgb(name)
    for suffix, fn in _DERIVERS.items():
        pixels, ch = fn(rgb, w, h, name)
        write_png(os.path.join(out_dir, "%s_%s.png" % (name, suffix)), w, h, pixels, ch)
    return w, h


def main():
    ap = argparse.ArgumentParser(description="DOOM-0042 offline PBR-map derive generator")
    ap.add_argument("--wad", required=True, help="path to the IWAD (e.g. wads/doom.wad)")
    ap.add_argument("--csv", help="materials.csv; derive its `derive` rows")
    ap.add_argument("--out", default="assets/ultra/derived", help="output dir for derived PNGs")
    ap.add_argument("--dump", help="write <NAME>_dump.png of one texture/flat and exit (Stage-1 check)")
    args = ap.parse_args()

    wad = Wad(args.wad)

    if args.dump:
        rgb, (w, h) = wad.image_rgb(args.dump)
        write_png("%s_dump.png" % args.dump, w, h, rgb, 3)
        print("wrote %s_dump.png (%dx%d)" % (args.dump, w, h))
        return

    if not args.csv:
        ap.error("need --csv (or --dump NAME) to know which materials to derive")
    names = parse_derive_rows(args.csv)
    if not names:
        print("no derive rows in", args.csv)
        return
    for name in names:
        try:
            w, h = emit_maps(wad, name, args.out)
            print("derived %-10s %dx%d -> 7 maps" % (name, w, h))
        except (KeyError, IndexError, ValueError, struct.error) as e:
            print("SKIP %s: %s" % (name, e), file=sys.stderr)   # bad/missing lump -> skip, keep going
    print("wrote derived maps to", args.out)


if __name__ == "__main__":
    main()
