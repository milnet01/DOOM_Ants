#!/usr/bin/env python3
"""One-off: synthesize a placeholder hero map set so the engine HD path can be
brought up before curated CC0 art is staged (Task 17 replaces these).

Uses only the Python stdlib (a minimal PNG writer) so no external asset or pip
install is needed. Normal is flat (+Z), height a horizontal ramp, ao mid-grey,
albedo a blue-grey tech checker."""
import struct
import zlib
import os
import math


def ao_bump(x, y, w, h):
    """AO correlated with the normal bumps: dark in the valleys (occluded), bright on the
    peaks (open), same 32px period as bump_normal so the ambient-occlusion shading lines up
    with the relief. Demonstrates Task 12 — the valleys darken under fill light, the flashlight
    still lights them fully."""
    a = 2.0 * math.pi * x / 32.0
    b = 2.0 * math.pi * y / 32.0
    hgt = math.cos(a) * math.cos(b)               # -1 valley .. +1 peak
    v = int((0.25 + 0.75 * (0.5 + 0.5 * hgt)) * 255)   # 0.25 valley .. 1.0 peak
    return (v, v, v)


def bump_height(x, y, w, h):
    """Height field for Task 14's parallax march — the SAME cos(a)cos(b) relief the normal and
    AO maps use (32px period), so POM's recesses line up with the normal/AO valleys. White =
    raised (peaks), black = sunk (valleys); the shader marches the view ray into the low bits."""
    a = 2.0 * math.pi * x / 32.0
    b = 2.0 * math.pi * y / 32.0
    v = int((0.5 + 0.5 * math.cos(a) * math.cos(b)) * 255)   # 0 valley .. 255 peak
    return (v, v, v)


def emissive_dots(x, y, w, h):
    """A warm glowing disc at the centre of each 32px tile, black elsewhere — a deliberately
    obvious emissive so Task 13's self-radiance is unmistakable in the RT direct view (mode 4):
    the dots glow regardless of scene light while the rest of the wall stays dark."""
    cx, cy = (x % 32) - 16.0, (y % 32) - 16.0
    return (255, 150, 60) if (cx * cx + cy * cy) <= 36.0 else (0, 0, 0)   # radius 6px disc


def bump_normal(x, y, w, h):
    """A tangent-space normal (OpenGL Y+) for a grid of BIG rounded bumps (~32px period,
    steep) so Task 11's normal mapping is unmistakable in the play view — a fine 8px albedo
    checker would clash with the half-res denoiser (which smooths high-frequency lighting)
    and wash out under soft ambient, so the bump is deliberately low-frequency + high-relief.
    Height field cos(a)cos(b); normal = norm(-dh/dx,-dh/dy,1)."""
    a = 2.0 * math.pi * x / 32.0
    b = 2.0 * math.pi * y / 32.0
    k = 2.0                                   # slope strength (~63deg max tilt — bold relief)
    nx, ny, nz = k * math.sin(a) * math.cos(b), k * math.cos(a) * math.sin(b), 1.0
    inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx * inv, ny * inv, nz * inv
    return (int((nx * 0.5 + 0.5) * 255), int((ny * 0.5 + 0.5) * 255), int((nz * 0.5 + 0.5) * 255))


def write_png(path, w, h, rgb_fn):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    raw = bytearray()
    for y in range(h):
        raw.append(0)  # filter type 0 (none) per scanline
        for x in range(w):
            r, g, b = rgb_fn(x, y, w, h)
            raw += bytes((r & 255, g & 255, b & 255))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))  # 8-bit RGB
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as fh:
        fh.write(png)


D = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "assets", "ultra", "heroes", "bringup")
write_png(os.path.join(D, "startan3_alb.png"), 64, 128,
          lambda x, y, w, h: (70, 90, 110) if (x // 8 + y // 8) % 2 else (60, 78, 96))
write_png(os.path.join(D, "startan3_nrm.png"), 64, 128, bump_normal)
write_png(os.path.join(D, "startan3_ao.png"), 64, 128, ao_bump)
# Dedicated stable fixture for tests/rb_image_test.cpp: a solid 180-grey field so a box
# downscale must stay ~180 everywhere. Kept separate from the visual maps above (which change
# per task) so tweaking the look never breaks the decode/downscale unit test. Not referenced
# by materials.csv, so the loader never touches it.
write_png(os.path.join(D, "_unittest_solid.png"), 64, 128, lambda x, y, w, h: (180, 180, 180))
write_png(os.path.join(D, "startan3_hgt.png"), 64, 128, bump_height)
write_png(os.path.join(D, "startan3_emis.png"), 64, 128, emissive_dots)
print("wrote bring-up hero set to", D)
