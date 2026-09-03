#!/usr/bin/env python3
"""Build a one-lump PWAD holding a crafted DEMO1, for security-guard testing.

Written for DOOM-0371, where the shipped IWAD demos could not exercise the fix:
they are recorded at demo version 109 and this engine is 110, so all six take
the version-mismatch path and never reach playback at all. A crafted lump is
the only way to run that code.

The PWAD replaces the IWAD's DEMO1, so `-file <out>.wad -timedemo demo1` plays
it. `-timedemo` reports "timed N gametics", and that count IS the observable:
it says how many tics the engine actually read. Measured on DOOM-0371:

    fixture          before the fix      after
    well-formed      30 gametics         30 gametics
    no DEMOMARKER    121 gametics        30 gametics
    consoleplayer    no timing line      30 gametics
      = 200            printed at all

Usage:  make_demo_fixture.py {valid|badplayer|noterm|walk} <out.wad>

Not a map fixture. Proving the DOOM-0369/0370/0372/0381 guards fire needs a
crafted MAP, which is a bigger job and is filed separately -- see the roadmap.
"""

import struct
import sys

DEMO_VERSION = 110      # doomdef.h: enum { VERSION = 110 }
DEMOMARKER = 0x80       # g_game.c


def pwad(path, lumps):
    """Write a PWAD: 12-byte header, lump payloads, then the directory."""
    data = b""
    dirents = []
    off = 12
    for name, payload in lumps:
        dirents.append((off, len(payload), name))
        data += payload
        off += len(payload)
    header = b"PWAD" + struct.pack("<ii", len(lumps), 12 + len(data))
    directory = b"".join(
        struct.pack("<ii8s", o, n, nm.encode().ljust(8, b"\0"))
        for o, n, nm in dirents
    )
    with open(path, "wb") as f:
        f.write(header + data + directory)


def demo(consoleplayer, tics, terminator, forwardmove=0):
    """A 13-byte demo header, `tics` ticcmds, and optionally its marker.

    A ticcmd is forwardmove, sidemove, angleturn, buttons. Empty ones are all
    DOOM-0371 needed; `forwardmove` is what makes the player actually cross a
    line, which is how a walkover special gets triggered with no display.
    """
    head = bytes([
        DEMO_VERSION,
        2,              # skill
        1, 1,           # episode, map
        0, 0, 0, 0,     # deathmatch, respawn, fast, nomonsters
        consoleplayer,  # the byte DOOM-0371 bounds; MAXPLAYERS is 4
        1, 0, 0, 0,     # playeringame[4]
    ])
    body = bytes([forwardmove & 0xFF, 0, 0, 0]) * tics
    return head + body + (bytes([DEMOMARKER]) if terminator else b"")


CASES = {
    # well-formed: the control arm. Must be unchanged by any guard.
    "valid":     lambda: demo(0, 30, True),
    # player slot far outside players[]/netcmds[] -- DOOM-0371's first half.
    "badplayer": lambda: demo(200, 30, True),
    # no end marker -- DOOM-0371's second half, the unbounded read.
    "noterm":    lambda: demo(0, 30, False),
    # Two seconds of running forward. Not a DOOM-0371 case: this one exists to
    # supply input to a crafted MAP, for the guards that only a line the player
    # steps across can reach. 50 is the run-speed forwardmove (g_game.c).
    "walk":      lambda: demo(0, 70, True, forwardmove=50),
}


def main(argv):
    if len(argv) != 3 or argv[1] not in CASES:
        sys.stderr.write(
            "usage: %s {%s} <out.wad>\n" % (argv[0], "|".join(CASES))
        )
        return 2
    pwad(argv[2], [("DEMO1", CASES[argv[1]]())])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
