#!/usr/bin/env python3
# Original DOOM-style menu-cursor skull for DOOM_Ants (our own art -> license-clean, CC0).
# Encodes THREE coverage masks in the RGB channels of one PNG so the engine can draw a
# multi-tone skull through its single-channel (R8) tinted-quad pipeline -- no new GPU path:
#   R = bone body      (drawn bone-grey)
#   G = dark recesses  (eye sockets, nasal, tooth gaps, temple hollows -> near-black)
#   B = eye glow       (red pinpoints in the sockets)
# Drawn 4x supersampled then downsampled for smooth anti-aliased edges.
from PIL import Image, ImageDraw
import sys

S = 4
W, H = 128*S, 160*S
def s(*v): return tuple(int(round(c*S)) for c in v)

def layer():
    im = Image.new("L", (W, H), 0)
    return im, ImageDraw.Draw(im)

body, bd   = layer()   # bone
dark, dd   = layer()   # recesses
eyes, ed   = layer()   # red glow
WH_ = 255

# --- bone body ------------------------------------------------------------
bd.ellipse([s(18, 4), s(110, 96)], fill=WH_)                 # cranium dome
bd.ellipse([s(22, 34), s(106, 116)], fill=WH_)               # brow / cheek mass
# Zygomatic (cheekbone) flare, then a jaw tapering to a rounded chin.
bd.polygon([s(24, 74), s(30, 104), s(42, 128), s(54, 140), s(64, 143),
            s(74, 140), s(86, 128), s(98, 104), s(104, 74)], fill=WH_)
bd.ellipse([s(48, 128), s(80, 150)], fill=WH_)               # chin

# --- dark recesses --------------------------------------------------------
# Eye sockets: large angled almonds (inner-lower tilt) for a DOOM glare. No brow
# bar between them -- that read as goggles. Slightly soft (fill 235) so they're
# deep charcoal, not flat black.
def socket(cx, cy, rx, ry, ang):
    e = Image.new("L", (W, H), 0)
    ImageDraw.Draw(e).ellipse([s(cx-rx, cy-ry), s(cx+rx, cy+ry)], fill=235)
    return e.rotate(ang, center=s(cx, cy), resample=Image.BICUBIC)
for sk in (socket(44, 58, 17, 21, -22), socket(84, 58, 17, 21, 22)):
    dark.paste(sk, (0, 0), sk)
# Nasal aperture — dark downward triangle just below the eyes.
dd.polygon([s(58, 76), s(70, 76), s(64, 94)], fill=WH_)
# Gritted teeth: a short dark mouth band with vertical bone teeth, sat high under
# the nose so the skull reads compact (not long-jawed).
dd.rectangle([s(46, 100), s(82, 120)], fill=WH_)            # mouth cavity (dark)
for tx in range(49, 80, 7):                                 # bone teeth punched back out
    ImageDraw.Draw(body).rectangle([s(tx, 101), s(tx+4, 119)], fill=WH_)
    dd.rectangle([s(tx, 101), s(tx+4, 119)], fill=0)

# --- red eye glow ---------------------------------------------------------
# Small bright glints low-inner in each socket.
ed.ellipse([s(44, 60), s(54, 70)], fill=WH_)
ed.ellipse([s(74, 60), s(84, 70)], fill=WH_)

# Pack the three masks into RGB (A = body, so a plain viewer still shows the shape).
out = Image.merge("RGBA", (body, dark, eyes, body)).resize((128, 160), Image.LANCZOS)
path = sys.argv[1] if len(sys.argv) > 1 else "skull_cursor.png"
out.save(path)

# Preview: composite the three tinted layers over a dark menu-grey, at big + cursor size.
def composite(scale_h):
    r, g, b, _ = out.split()
    w = int(out.width * scale_h / out.height)
    def lay(mask, rgb):
        m = mask.resize((w, scale_h), Image.LANCZOS)
        t = Image.new("RGBA", (w, scale_h), rgb + (0,)); t.putalpha(m); return t
    canvas = Image.new("RGBA", (w+20, scale_h+20), (24, 22, 26, 255))
    canvas.alpha_composite(lay(r, (201, 194, 174)), (10, 10))   # bone
    canvas.alpha_composite(lay(g, (18, 12, 10)),   (10, 10))    # recess
    canvas.alpha_composite(lay(b, (226, 46, 26)),  (10, 10))    # red eyes
    return canvas.convert("RGB")
composite(160).save("skull_preview.png")
composite(44).save("skull_prev_44.png")
print("wrote", path, out.size)
