#!/usr/bin/env python3
"""Build a PWAD whose lump DIRECTORY is malformed, for security-guard testing.

Written for DOOM-0384. DOOM-0093 bounded the directory's extent against the real
file size; every filepos and size inside that directory was still stored raw, so
a crafted PWAD could declare a lump of any size or at a negative offset. This
generator produces the files that exercise the bound, and the valid mode is the
control that proves the pipeline itself produces a loadable WAD.

Sibling of make_map_fixture.py, which corrupts a map's CONTENT. This one leaves
every lump's bytes alone and corrupts the table of contents that describes them.

    mode          what it does to the directory        expected
    valid         nothing (control)                    loads
    hugesize      one lump declares ~112 MB            refused by name
    negpos        one lump declares a negative offset  refused by name
    pasteof       one lump ends one byte past EOF      refused by name
    shortheader   file truncated inside the header     refused by name
    sfxrate       a DSPISTOL declaring 1 Hz             rate falls back to SFXRATE

The sfxrate mode is about DOOM-0386 rather than the directory: a sound lump's
declared sample rate is attacker-controlled, and 1 Hz made SDL build a ~44100x
upsample whose conversion buffer came to about 11 GB for a 64 KB sound.

Usage:  make_wad_fixture.py <mode> <out.wad>

Needs no IWAD: the lumps are synthetic, because the directory is the subject and
the payload never has to mean anything.
"""

import struct
import sys

HEADER_SIZE = 12
DIRENT_SIZE = 16


def build(mode):
    """Return the PWAD bytes for `mode`."""
    # Two ordinary lumps and an empty marker, so the control is a WAD the engine
    # accepts and the malformed modes differ from it in one field only.
    lumps = [("FIXTURE1", b"\x01" * 64), ("MARKER", b""), ("FIXTURE2", b"\x02" * 32)]

    if mode == "sfxrate":
        # DMX sound: format, rate, sample count, then the 8-bit samples. The
        # engine reads the rate from bytes 2-3 and the count from 4-7, and
        # caches every sfx lump at startup, so this is reached without playing.
        samples = 64000
        lumps = [("DSPISTOL",
                  struct.pack("<HHI", 3, 1, samples) + b"\x80" * samples)]

    payload = b""
    dirents = []
    off = HEADER_SIZE
    for name, data in lumps:
        dirents.append([off, len(data), name])
        payload += data
        off += len(data)

    diroff = HEADER_SIZE + len(payload)
    total = diroff + DIRENT_SIZE * len(dirents)

    if mode == "hugesize":
        dirents[0][1] = 0x7000000          # ~112 MB claimed inside a tiny file
    elif mode == "negpos":
        dirents[0][0] = -1                 # lseek target the engine cannot reach
    elif mode == "pasteof":
        dirents[2][1] = total - dirents[2][0] + 1   # ends exactly one byte late
    elif mode not in ("valid", "shortheader", "sfxrate"):
        raise SystemExit("unknown mode: %s" % mode)

    header = b"PWAD" + struct.pack("<ii", len(dirents), diroff)
    directory = b"".join(
        struct.pack("<ii8s", pos, size, name.encode().ljust(8, b"\0"))
        for pos, size, name in dirents
    )
    wad = header + payload + directory

    if mode == "shortheader":
        # Shorter than a WAD header, so the read that fills it cannot complete.
        wad = wad[:8]
    return wad


def main(argv):
    if len(argv) != 3:
        raise SystemExit("usage: %s {valid|hugesize|negpos|pasteof|shortheader|"
                         "sfxrate} <out.wad>" % argv[0])
    data = build(argv[1])
    open(argv[2], "wb").write(data)
    print("%s: %s (%d bytes)" % (argv[2], argv[1], len(data)))


if __name__ == "__main__":
    main(sys.argv)
