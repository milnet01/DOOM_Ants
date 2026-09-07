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
    twosidedstub  ML_TWOSIDED line, no back side  DOOM-0372    counterfactual
    doorstub      manual door on a one-sided wall DOOM-0372    counterfactual
    badthingtype  THINGS entries of type 0 and -1  DOOM-0397    I_Error, by name
    nostart       player-1 start retyped to an Imp DOOM-0397   I_Error, by name
    lowerchange   every walkover lower-and-changes DOOM-0398    counterfactual
    segnoback     a two-sided line loses its back side DOOM-0399  I_Error, by name

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
SIDEDEF_SIZE = 30       # textureoffset rowoffset top[8] bottom[8] mid[8] sector
SECTOR_SIZE = 26        # floorheight ceilingheight floorpic[8] ceilingpic[8] ...
SECTOR_TAG_OFF = 24     # last field of the sector record
NO_SIDE = 0xFFFF        # stored as -1
ML_TWOSIDED = 4         # doomdata.h
# Linedef special 30: "W1 Raise Floor to Shortest Lower Texture", the walkover
# that reaches EV_DoFloor(raiseToTexture) -- one of twoSided()'s two call sites.
# The other, lowerAndChange, is no good here: its loop reassigns `sec` on the
# first two-sided line it accepts, so it stops at whatever the NEW sector's
# linecount is and never reaches a line appended at the end.
RAISE_TO_TEXTURE = 30
# Linedef special 1: "DR Door Open Wait Close", the manual door the player
# opens with the use key -- the one EV_VerticalDoor path a map can reach.
MANUAL_DOOR = 1
# Linedef special 37: "W1 Lower Floor to Lowest and Change", one of the two
# walkovers that reach EV_DoFloor(lowerAndChange). Special 84 is the other.
LOWER_AND_CHANGE = 37
SECTOR_SPECIAL_OFF = 22 # sector record: ... lightlevel special tag
THING_SIZE = 10         # x y angle type options
# THINGS options bits 1/2/4 are the three skill classes P_SpawnMapThing tests;
# bit 16 is multiplayer-only and would make the thing skip in a single-player
# boot, which is the boot this fixture is checked with.
ALL_SKILLS = 7


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


def mutate_twosidedstub(group):
    """Append an ML_TWOSIDED linedef whose back sidedef is absent, in a tagged
    sector, and make every walkover lower-and-change that sector's floor.

    This is the fixture for DOOM-0372's twoSided() guard, which is reached only
    from EV_DoFloor -- so unlike the other modes it needs player input as well
    as a map, and is played with the walk demo from make_demo_fixture.py.

    Three mutations, and each is needed:

      - The stub linedef is APPENDED, so no seg points at it. A seg on a
        two-sided line makes P_LoadSegs resolve sidenum[side^1], which refuses
        -1 before the level finishes loading. Its front sidedef is real, so
        P_GroupLines files it under that sidedef's sector and DOOM-0422 (which
        refuses a missing FRONT side) leaves it alone.
      - That sector gets a tag no sector already uses, so EV_DoFloor finds it
        and nothing else.
      - Every linedef gets the walkover special and that tag, so whichever line
        the player first steps across fires it. The map's own specials are lost,
        which does not matter for a fixture that runs for two seconds.

    Without the guard, twoSided() is a bare flag test, so the stub passes it and
    getSide(secnum, i, 1) reads sides[-1]. That is zone memory, so neither ASAN
    nor a segfault reports it: the observable is the guard's own decision on
    this line, which differs between a pre-fix and a post-fix build.
    """
    sidedefs = dict(group)["SIDEDEFS"]
    sectors = bytearray(dict(group)["SECTORS"])
    linedefs = bytearray(dict(group)["LINEDEFS"])

    # Any real sidedef will do; sidedef 0's sector becomes the target.
    stub_side = 0
    target = struct.unpack_from("<h", sidedefs, SIDEDEF_SIZE - 2)[0]

    nsectors = len(sectors) // SECTOR_SIZE
    if not 0 <= target < nsectors:
        raise SystemExit("sidedef 0 names sector %d, which does not exist" % target)
    tags = [struct.unpack_from("<h", sectors, i * SECTOR_SIZE + SECTOR_TAG_OFF)[0]
            for i in range(nsectors)]
    tag = max(tags) + 1
    if tag > 0x7FFF:
        raise SystemExit("no spare sector tag below the 16-bit limit")
    struct.pack_into("<h", sectors, target * SECTOR_SIZE + SECTOR_TAG_OFF, tag)

    for i in range(len(linedefs) // LINEDEF_SIZE):
        base = i * LINEDEF_SIZE
        struct.pack_into("<hh", linedefs, base + 6, RAISE_TO_TEXTURE, tag)

    stub = struct.pack("<5hHh", 0, 1, ML_TWOSIDED, 0, 0, stub_side, -1)
    group = replace(group, "SECTORS", bytes(sectors))
    return replace(group, "LINEDEFS", bytes(linedefs) + stub)


def mutate_doorstub(group):
    """Put a manual-door special on every one-sided linedef.

    This is the fixture for DOOM-0372's other reachable guard, in
    EV_VerticalDoor. That one reads sidenum[side^1] -- the BACK sidedef, since
    the player activates a door from its front -- and a one-sided wall has
    none. Without the guard, sides[-1].sector is stored in door->sector and
    written through on every tic the door runs, so it is a wild-pointer write
    rather than the stray read the twosidedstub mode produces.

    No linedef is appended here and none needs to be: P_UseLines finds lines
    through the BLOCKMAP, which an appended linedef is absent from, and the
    map's own walls are already one-sided. Every one of them is given the
    special so that whichever wall the player ends up facing will do, which is
    what lets the walkuse demo drive this with no display.
    """
    linedefs = bytearray(dict(group)["LINEDEFS"])
    made = 0
    for i in range(len(linedefs) // LINEDEF_SIZE):
        base = i * LINEDEF_SIZE
        if struct.unpack_from("<H", linedefs, base + 12)[0] != NO_SIDE:
            continue                    # two-sided: it has a back sidedef
        struct.pack_into("<h", linedefs, base + 6, MANUAL_DOOR)
        made += 1
    if not made:
        raise SystemExit("map has no one-sided linedef to put a door on")
    return replace(group, "LINEDEFS", bytes(linedefs))


def mutate_lowerchange(group):
    """Tag every sector and make every walkover lower-and-change it.

    The fixture for DOOM-0398's lowerAndChange defect. EV_DoFloor seeds
    floor->texture from the sector but seeded floor->newspecial only inside
    the branch that finds a neighbour sitting at the destination height --
    and P_FindLowestFloorSurrounding starts from the sector's OWN height and
    only ever lowers, so for a sector already the lowest around it the
    destination IS its own height, no neighbour matches, and the branch
    cannot run. T_MoveFloor then copies both into the sector, so the sector
    takes whatever Z_Malloc left in newspecial and P_PlayerInSpecialSector
    aborts the game on it.

    Every sector is tagged rather than one, because which sector the player
    occupies cannot be computed from the lump group without walking the BSP.
    Every linedef carries the special so whichever line the player first
    steps across fires it, the same blunt approach twosidedstub takes; the
    map's own specials are lost, which does not matter for a two-second
    fixture. Sector specials are cleared so the only special a player can be
    standing in is one this mutation produced.

    Played with the walk demo from make_demo_fixture.py.

    The observable is P_PlayerInSpecialSector's abort on an unknown special,
    which DOOM-0398 also REMOVED -- a sector special is WAD data and quitting
    the game on a bad one was its own defect. So the counterfactual only
    speaks against a build predating that removal. Against a newer build both
    sides run to the end and the mode proves nothing; it is kept because the
    map it produces is still the one that reaches the branch.
    """
    sectors = bytearray(dict(group)["SECTORS"])
    linedefs = bytearray(dict(group)["LINEDEFS"])
    nsectors = len(sectors) // SECTOR_SIZE

    tags = [struct.unpack_from("<h", sectors, i * SECTOR_SIZE + SECTOR_TAG_OFF)[0]
            for i in range(nsectors)]
    tag = max(tags) + 1
    if tag > 0x7FFF:
        raise SystemExit("no spare sector tag below the 16-bit limit")
    for i in range(nsectors):
        struct.pack_into("<h", sectors, i * SECTOR_SIZE + SECTOR_TAG_OFF, tag)
        struct.pack_into("<h", sectors, i * SECTOR_SIZE + SECTOR_SPECIAL_OFF, 0)

    for i in range(len(linedefs) // LINEDEF_SIZE):
        base = i * LINEDEF_SIZE
        struct.pack_into("<hh", linedefs, base + 6, LOWER_AND_CHANGE, tag)

    group = replace(group, "SECTORS", bytes(sectors))
    return replace(group, "LINEDEFS", bytes(linedefs))


def mutate_segnoback(group):
    """Clear the BACK sidedef of a two-sided linedef that carries segs.

    The fixture for DOOM-0399's compatibility call. ML_TWOSIDED with
    sidenum[1] == -1 is something the WAD format lets a map say, and vanilla
    read sides[-1] and carried on. P_WadIndex refuses -1, so the map stopped
    loading at all -- P_LoadSegs resolves sidenum[side^1] for any seg on a
    two-sided line.

    Unlike twosidedstub, the linedef is an EXISTING one rather than an
    appended one, precisely so its segs are present and P_LoadSegs has to
    decide. The ML_TWOSIDED flag is left set: the point is a line that
    CLAIMS two sides and supplies one.

    What this mode demonstrates is that the relaxation did not weaken the
    load-time refusal. Every two-sided linedef in MAP01 carries segs on BOTH
    sides, and a back-side seg's OWN sidedef is the one that has just been
    cleared -- so the map is still refused, by the seg-sidedef check rather
    than the seg-back-sidedef one:

        before   bad seg back sidedef index -1
        after    bad seg sidedef index -1

    It therefore does NOT exercise the case the relaxation rescues, which is
    a two-sided line with no back sidedef and no seg on its back side. No
    MAP01-derived fixture can: no such linedef exists there to mutate, and
    manufacturing one means editing SEGS, whose indices the subsector ranges
    depend on.
    """
    linedefs = bytearray(dict(group)["LINEDEFS"])
    for i in range(len(linedefs) // LINEDEF_SIZE):
        base = i * LINEDEF_SIZE
        flags = struct.unpack_from("<h", linedefs, base + 4)[0]
        back = struct.unpack_from("<H", linedefs, base + 12)[0]
        if (flags & ML_TWOSIDED) and back != NO_SIDE:
            struct.pack_into("<H", linedefs, base + 12, NO_SIDE)
            break
    else:
        raise SystemExit("map has no two-sided linedef with a back sidedef")
    return replace(group, "LINEDEFS", bytes(linedefs))


def mutate_badthingtype(group):
    """Append two THINGS whose type is not positive: 0 and -1.

    mapthing_t.type is a signed short read straight from the WAD.
    P_SpawnMapThing's player-start test bounds it at 1..4 and everything
    outside falls through to the doomednum walk, which is where the two
    non-positive cases diverge. Type -1 MATCHES mobjinfo[0] -- MT_PLAYER,
    whose doomednum is -1, one of 19 such entries -- and spawns a stray
    player-shaped mobj with a NULL player field. Type 0 matches nothing and
    reaches I_Error, aborting the level load.

    The type-0 thing is what makes the fixture loud: pre-fix the map is
    refused by name, and with the guard in place the map boots. Nothing is
    mutated, only appended, so the map is otherwise the IWAD's own MAP01.
    """
    things = dict(group)["THINGS"]
    # Sit them on the player-1 start so the coordinates are inside the map.
    x = y = 0
    for i in range(len(things) // THING_SIZE):
        tx, ty, _, ttype, _ = struct.unpack_from("<hhhhh", things, i * THING_SIZE)
        if ttype == 1:
            x, y = tx, ty
            break
    extra = (struct.pack("<hhhhh", x, y, 0, -1, ALL_SKILLS)
             + struct.pack("<hhhhh", x, y, 0, 0, ALL_SKILLS))
    return replace(group, "THINGS", things + extra)


def mutate_nostart(group):
    """Retype the player-1 start so the map has no start for player 1.

    Type 3001 is an Imp -- a thing that spawns normally, so the map is
    otherwise playable and the only thing missing is the start itself.
    Nothing else in the lump group references THINGS, so no index moves.
    """
    things = bytearray(dict(group)["THINGS"])
    for i in range(len(things) // THING_SIZE):
        base = i * THING_SIZE
        if struct.unpack_from("<h", things, base + 6)[0] == 1:
            struct.pack_into("<h", things, base + 6, 3001)
            break
    else:
        raise SystemExit("that map has no player-1 start to remove")
    return replace(group, "THINGS", bytes(things))


MODES = {
    "valid": lambda g: g,
    "orphanline": mutate_orphanline,
    "badblockmap": mutate_badblockmap,
    "badflat": mutate_badflat,
    "manylines": mutate_manylines,
    "onesided": mutate_onesided,
    "twosidedstub": mutate_twosidedstub,
    "doorstub": mutate_doorstub,
    "badthingtype": mutate_badthingtype,
    "nostart": mutate_nostart,
    "lowerchange": mutate_lowerchange,
    "segnoback": mutate_segnoback,
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
