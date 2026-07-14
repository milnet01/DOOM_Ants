#!/usr/bin/env python3
"""One-off: synthesize a placeholder hero map set so the engine HD path can be
brought up before curated CC0 art is staged (Task 17 replaces these).

Uses only the Python stdlib (a minimal PNG writer) so no external asset or pip
install is needed. Normal is flat (+Z), height a horizontal ramp, ao mid-grey,
albedo a blue-grey tech checker."""
import struct, zlib, os, math


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
write_png(os.path.join(D, "startan3_ao.png"), 64, 128,
          lambda x, y, w, h: (180, 180, 180))
write_png(os.path.join(D, "startan3_hgt.png"), 64, 128,
          lambda x, y, w, h: (int(255 * (x / w)),) * 3)
print("wrote bring-up hero set to", D)
