#!/usr/bin/env python3
"""Find holes in a map's geometry from the WAD alone -- no engine, no render.

This is the tool that found DOOM-0180's bright diagonal seam, and it found it
before a single line of engine code was changed or a single frame captured. Kept
rather than rebuilt, for the same reason ab_capture.sh was: the next "there is a
crack in the world" report should start here, because a map-data answer costs
seconds and a render-side investigation costs a session.

WHAT IT LOOKS FOR

DOOM stores every vertex as a 16-bit INTEGER. When the node builder splits a
linedef, the split vertex is ROUNDED to whole units -- exact on an axis-aligned
linedef, off the true line on a DIAGONAL one. And the two sides of a linedef are
split independently, at different points, so the front segs and the back segs can
trace two DIFFERENT polylines between the same pair of endpoints.

That matters because r_mesh.c builds a wall quad along one side's segs and clips
the neighbouring floor/ceiling caps to the other's. Where the two polylines
disagree, the wall and the caps miss each other and leave a real world-space slit
-- which the path tracer sees straight through to the sky (bright) and the HITS
debug view renders black. Fixed in DOOM-0180 by projecting every seg endpoint
onto its linedef's exact line (linuxdoom-1.10/seg_project.h).

So a non-zero `gap` column here is a hole the renderer has to be defending
against. After the DOOM-0180 fix it is defended, and the numbers below are a
description of the INPUT data, not of a live bug -- they stay non-zero forever
because the WAD does. Use them to locate a suspect surface, then A/B it with
scripts/ab_capture.sh.

USAGE

    scripts/wad_seg_probe.py wads/doom.wad E1M1              # rank the offenders
    scripts/wad_seg_probe.py wads/doom.wad E1M1 193          # dump one linedef
    scripts/wad_seg_probe.py wads/doom.wad E1M1 -- 3274 -3353   # sort by distance

On E1M1 this reports 6 of 180 two-sided linedefs mismatched, worst 0.97 units at
linedef 193 -- the pit ledge in the roofed nukage room -- and ALL SIX diagonal
against 381 clean axis-aligned linedefs. That last ratio is what explained why
the seam in the original report was always a diagonal line.
"""
import struct
import sys
from collections import defaultdict

# The ten lumps that follow a map marker, in order (we only need four of them).
MAP_LUMPS = 10


def read_map(path, mapname):
    data = open(path, "rb").read()
    _, nlumps, dirofs = struct.unpack_from("<4sii", data, 0)
    entries = [struct.unpack_from("<ii8s", data, dirofs + 16 * i)
               for i in range(nlumps)]
    names = [nm.rstrip(b"\0").decode() for _, _, nm in entries]
    try:
        start = names.index(mapname)
    except ValueError:
        sys.exit(f"{path}: no map named {mapname}")
    out = {}
    for fp, sz, nm in entries[start + 1:start + 1 + MAP_LUMPS]:
        out[nm.rstrip(b"\0").decode()] = data[fp:fp + sz]
    return out


def unpack_all(blob, fmt):
    sz = struct.calcsize(fmt)
    return [struct.unpack_from(fmt, blob, sz * i) for i in range(len(blob) // sz)]


class Map:
    def __init__(self, path, mapname):
        L = read_map(path, mapname)
        self.name = mapname
        self.verts = unpack_all(L["VERTEXES"], "<hh")
        self.lines = unpack_all(L["LINEDEFS"], "<HHHHHHH")
        self.segs = unpack_all(L["SEGS"], "<HHhHhh")
        self.sides = unpack_all(L["SIDEDEFS"], "<hh8s8s8sh")
        self.sectors = unpack_all(L["SECTORS"], "<hh8s8shhh")
        # segs grouped by linedef, then by side (0 = front/right, 1 = back).
        self.by_line = defaultdict(lambda: defaultdict(list))
        for i, (v1, v2, _ang, ld, side, _ofs) in enumerate(self.segs):
            self.by_line[ld][side].append((i, v1, v2))

    def line_frame(self, ld):
        """(origin, direction, length) of a linedef's exact line."""
        ax, ay = self.verts[self.lines[ld][0]]
        bx, by = self.verts[self.lines[ld][1]]
        dx, dy = bx - ax, by - ay
        return (ax, ay), (dx, dy), (dx * dx + dy * dy) ** 0.5

    def project(self, ld, vi):
        """A vertex as (t along the linedef, signed perpendicular offset)."""
        (ax, ay), (dx, dy), ln = self.line_frame(ld)
        px, py = self.verts[vi]
        t = ((px - ax) * dx + (py - ay) * dy) / (ln * ln)
        perp = (dx * (py - ay) - dy * (px - ax)) / ln     # + is LEFT of v1->v2
        return t, perp

    def sector_of(self, ld, side):
        sidenum = self.lines[ld][5 + side]
        return self.sectors[self.sides[sidenum][5]]


def polyline(m, ld, side):
    """One side's segs as a sorted [(t, perpendicular offset)] polyline."""
    pts = {}
    for _si, v1, v2 in m.by_line[ld][side]:
        for v in (v1, v2):
            t, perp = m.project(ld, v)
            pts[round(t, 6)] = perp
    return sorted(pts.items())


def perp_at(poly, t):
    """Linearly interpolate a polyline's perpendicular offset at parameter t."""
    if t <= poly[0][0]:
        return poly[0][1]
    if t >= poly[-1][0]:
        return poly[-1][1]
    for (t0, d0), (t1, d1) in zip(poly, poly[1:]):
        if t0 <= t <= t1:
            return d0 if t1 == t0 else d0 + (d1 - d0) * (t - t0) / (t1 - t0)
    return poly[-1][1]


def survey(m, cam=None):
    """Every two-sided linedef whose two sides trace different polylines."""
    rows = []
    for ld, sides in m.by_line.items():
        if len(sides) != 2:
            continue
        (ax, ay), (dx, dy), ln = m.line_frame(ld)
        if ln == 0:
            continue
        p0, p1 = polyline(m, ld, 0), polyline(m, ld, 1)
        gap, gt = 0.0, 0.0
        for t in sorted({t for p in (p0, p1) for t, _ in p}):
            g = abs(perp_at(p0, t) - perp_at(p1, t))
            if g > gap:
                gap, gt = g, t
        if gap < 1e-6:
            continue
        s0, s1 = m.sector_of(ld, 0), m.sector_of(ld, 1)
        gx, gy = ax + dx * gt, ay + dy * gt
        dist = ((gx - cam[0]) ** 2 + (gy - cam[1]) ** 2) ** 0.5 if cam else None
        rows.append(dict(gap=gap, ld=ld, x=gx, y=gy, dist=dist,
                         f0=s0[0], c0=s0[1], f1=s1[0], c1=s1[1],
                         diagonal=dx != 0 and dy != 0))
    rows.sort(key=lambda r: (r["dist"] if cam else -r["gap"]))
    return rows


def dump_line(m, ld):
    (ax, ay), (dx, dy), ln = m.line_frame(ld)
    v1, v2 = m.lines[ld][0], m.lines[ld][1]
    kind = "diagonal" if dx and dy else "AXIS-ALIGNED"
    print(f"linedef {ld}: v{v1}{m.verts[v1]} -> v{v2}{m.verts[v2]}  "
          f"len {ln:.2f}  {kind}")
    print("  perp is signed: + is LEFT of v1->v2. A split vertex with a non-zero")
    print("  perp is one the node builder rounded OFF the linedef's true line.")
    for side in sorted(m.by_line[ld]):
        for si, sv1, sv2 in m.by_line[ld][side]:
            a, b = m.project(ld, sv1), m.project(ld, sv2)
            print(f"  seg {si:5d} side {side}  "
                  f"v{sv1}{m.verts[sv1]} t={a[0]:.4f} perp={a[1]:+.4f}  ->  "
                  f"v{sv2}{m.verts[sv2]} t={b[0]:.4f} perp={b[1]:+.4f}")


def main():
    argv = sys.argv[1:]
    cam = None
    if "--" in argv:
        i = argv.index("--")
        cam = (float(argv[i + 1]), float(argv[i + 2]))
        argv = argv[:i]
    wad = argv[0] if argv else "wads/doom.wad"
    mapname = argv[1] if len(argv) > 1 else "E1M1"
    m = Map(wad, mapname)

    if len(argv) > 2:
        dump_line(m, int(argv[2]))
        return

    n_axis = sum(1 for l in m.lines
                 if m.verts[l[0]][0] == m.verts[l[1]][0]
                 or m.verts[l[0]][1] == m.verts[l[1]][1])
    n_two = sum(1 for s in m.by_line.values() if len(s) == 2)
    rows = survey(m, cam)
    print(f"{mapname}: {len(m.verts)} vertexes, {len(m.lines)} linedefs "
          f"({n_axis} axis-aligned, {len(m.lines) - n_axis} diagonal), "
          f"{len(m.segs)} segs")
    print(f"two-sided linedefs whose sides trace DIFFERENT polylines: "
          f"{len(rows)} of {n_two}"
          f"  ({sum(1 for r in rows if r['diagonal'])} diagonal)\n")
    print("  gap   line      worst at (x,y)      front f/c     back f/c"
          + ("     dist" if cam else ""))
    for r in rows:
        step = "  <-- FLOOR STEP" if r["f0"] != r["f1"] else ""
        d = f"  {r['dist']:8.1f}" if cam else ""
        print(f" {r['gap']:5.2f}  {r['ld']:5d}  ({r['x']:7.1f},{r['y']:8.1f})  "
              f"{r['f0']:5d}/{r['c0']:5d}  {r['f1']:5d}/{r['c1']:5d}{d}{step}")
    if rows:
        print(f"\ndump one with:  {sys.argv[0]} {wad} {mapname} {rows[0]['ld']}")


if __name__ == "__main__":
    main()
