#!/usr/bin/env python3
"""Build a PWAD holding a deliberately malformed MAP01, for security-guard testing.

Written for DOOM-0420. The CRITICAL untrusted-input guards shipped 2026-09-02
were verified not to MISFIRE -- every map in both IWADs boots -- but with one
exception none had been observed actually stopping bad input. Proving that needs
a map the shipped IWADs cannot supply.

The map is not authored from scratch: a hand-built map needs a valid BSP, and a
BSP builder is a bigger job than the thing being tested. Instead the generator
copies MAP01's whole lump group out of an IWAD and mutates ONE lump, so every
fixture differs from a known-good map in exactly the way its name says.

Some guards are loud and are observed directly, by their own I_Error text. Others
are silent skips, so the observable is the counterfactual the DOOM-0371 demo
fixture used: the pre-fix binary misbehaves on the fixture and the patched one
does not.

The onesided mode is kept although it does not reach the guard it was written
for: SEGS carry a sidedef index, so removing a linedef's front sidedef makes the
seg check refuse the map before P_SpawnSpecials runs. That is worth recording
rather than deleting, and orphanline is the mode that gets past it.

    mode          mutation                        guard        observable
    valid         none (control)                  --           boots
    badblockmap   BLOCKMAP truncated              DOOM-0370    I_Error, by name
    badflat       a sector's floorpic -> PLAYPAL  DOOM-0381    I_Error, by name
    manylines     >MAXLINEANIMS scrolling lines   DOOM-0369    counterfactual
    onesided      a scrolling line, no front side (see below)  I_Error, by name
    orphanline    ditto, on a line carrying no seg DOOM-0372    counterfactual

Usage:  make_map_fixture.py <mode> <iwad> <out.wad>

The IWAD is read, never written. doom2.wad supplies MAP01; a doom.wad map group
is named E1M1 and this script does not look for it.
"""

import struct
import sys

# p_spec.c. Exceeding it is what DOOM-0369's guard refuses.
MAXLINEANIMS = 64
# Linedef special 48: "EFFECT FIRSTCOL SCROLL+", the one P_SpawnSpecials collects.
SCROLL_SPECIAL = 48
# The lump group P_SetupLevel loads, in the order a map marker is followed by.
MAP_LUMPS = ["THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
             "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"]
LINEDEF_SIZE = 14       # v1 v2 flags special tag sidenum[0] sidenum[1]
SECTOR_SIZE = 26        # floorheight ceilingheight floorpic[8] ceilingpic[8] ...
NO_SIDE = 0xFFFF        # stored as -1


def read_wad(path):
    """Return the raw bytes and the directory as [(name, offset, size)]."""
    data = open(path, "rb").read()
    magic = data[:4]
    if magic not in (b"IWAD", b"PWAD"):
        raise SystemExit("%s is not a WAD (magic %r)" % (path, magic))
    count, diroff = struct.unpack_from("<ii", data, 4)
    directory = []
    for i in range(count):
        off, size, name = struct.unpack_from("<ii8s", data, diroff + 16 * i)
        directory.append((name.rstrip(b"\0").decode("latin1"), off, size))
    return data, directory


def map_group(data, directory, marker):
    """The marker lump plus the map lumps that follow it, as [(name, payload)]."""
    try:
        start = next(i for i, (n, _, _) in enumerate(directory) if n == marker)
    except StopIteration:
        raise SystemExit("no %s lump in that WAD" % marker)
    group = [(marker, b"")]
    for name, off, size in directory[start + 1:start + 1 + len(MAP_LUMPS)]:
        if name not in MAP_LUMPS:
            break
        group.append((name, data[off:off + size]))
    missing = [n for n in MAP_LUMPS if n not in dict(group)]
    if missing:
        raise SystemExit("%s is missing %s" % (marker, ", ".join(missing)))
    return group


def replace(group, name, payload):
    return [(n, payload if n == name else p) for n, p in group]


def mutate_badblockmap(group):
    """Cut the BLOCKMAP so its offset table is shorter than bmapwidth*bmapheight.

    The header stays intact, so the engine reads the real dimensions and then
    finds the table it promised is not there -- which is the case that used to
    walk the block iterator off the end of the lump.
    """
    blockmap = dict(group)["BLOCKMAP"]
    w, h = struct.unpack_from("<hh", blockmap, 4)
    keep = 2 * (4 + w * h - 1)          # one short short of the offset table
    return replace(group, "BLOCKMAP", blockmap[:keep])


def mutate_badflat(group):
    """Point sector 0's floorpic at PLAYPAL -- a real lump, but not a flat.

    W_CheckNumForName searches the whole WAD, so this used to return an index far
    outside the flat range and be fed straight into floorpic.
    """
    sectors = bytearray(dict(group)["SECTORS"])
    sectors[4:12] = b"PLAYPAL\0"
    return replace(group, "SECTORS", bytes(sectors))


def mutate_manylines(group):
    """Give more than MAXLINEANIMS linedefs the scrolling special.

    Only lines that already have a front sidedef are used, so this fixture
    exercises the count limit alone and not the sidedef check.
    """
    linedefs = bytearray(dict(group)["LINEDEFS"])
    n = len(linedefs) // LINEDEF_SIZE
    want = MAXLINEANIMS + 8
    made = 0
    for i in range(n):
        base = i * LINEDEF_SIZE
        if struct.unpack_from("<H", linedefs, base + 10)[0] == NO_SIDE:
            continue
        struct.pack_into("<h", linedefs, base + 6, SCROLL_SPECIAL)
        made += 1
        if made == want:
            break
    if made < want:
        raise SystemExit("map has only %d two-sided linedefs, need %d" % (made, want))
    return replace(group, "LINEDEFS", bytes(linedefs))


def mutate_onesided(group):
    """Give linedef 0 the scrolling special and take its front sidedef away.

    The per-tic scroll writes through sides[sidenum[0]], so without the guard
    this is a write to sides[-1] every tic the level runs.
    """
    linedefs = bytearray(dict(group)["LINEDEFS"])
    struct.pack_into("<h", linedefs, 6, SCROLL_SPECIAL)
    struct.pack_into("<H", linedefs, 10, NO_SIDE)
    return replace(group, "LINEDEFS", bytes(linedefs))


def mutate_orphanline(group):
    """Append a scrolling linedef with no sidedefs at all, referenced by no seg.

    The plain onesided mutation never reaches P_SpawnSpecials: SEGS carry the
    linedef's sidedef index, so the seg check refuses the map first. A linedef
    appended past the last one the BSP knows about has no seg pointing at it, so
    it survives to the point the DOOM-0372 guard is written for.
    """
    linedefs = dict(group)["LINEDEFS"]
    orphan = struct.pack("<7h", 0, 1, 0, SCROLL_SPECIAL, 0, -1, -1)
    return replace(group, "LINEDEFS", linedefs + orphan)


MODES = {
    "valid": lambda g: g,
    "orphanline": mutate_orphanline,
    "badblockmap": mutate_badblockmap,
    "badflat": mutate_badflat,
    "manylines": mutate_manylines,
    "onesided": mutate_onesided,
}


def write_pwad(path, lumps):
    """Write a PWAD: 12-byte header, lump payloads, then the directory."""
    payloads = b""
    dirents = []
    off = 12
    for name, payload in lumps:
        dirents.append((off, len(payload), name))
        payloads += payload
        off += len(payload)
    header = b"PWAD" + struct.pack("<ii", len(lumps), 12 + len(payloads))
    directory = b"".join(struct.pack("<ii8s", o, n, nm.encode().ljust(8, b"\0"))
                         for o, n, nm in dirents)
    open(path, "wb").write(header + payloads + directory)


def main(argv):
    if len(argv) != 4 or argv[1] not in MODES:
        raise SystemExit("usage: %s {%s} <iwad> <out.wad>"
                         % (argv[0], "|".join(MODES)))
    mode, iwad, out = argv[1], argv[2], argv[3]
    data, directory = read_wad(iwad)
    group = MODES[mode](map_group(data, directory, "MAP01"))
    write_pwad(out, group)
    print("%s: %s (%d lumps)" % (out, mode, len(group)))


if __name__ == "__main__":
    main(sys.argv)
