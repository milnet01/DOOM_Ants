#!/usr/bin/env python3
"""Build a PWAD whose lump DIRECTORY is malformed, for security-guard testing.

Written for DOOM-0384. DOOM-0093 bounded the directory's extent against the real
file size; every filepos and size inside that directory was still stored raw, so
a crafted PWAD could declare a lump of any size or at a negative offset. This
generator produces the files that exercise the bound, and the valid mode is the
control that proves the pipeline itself produces a loadable WAD.

Sibling of make_map_fixture.py, which corrupts one MAP's content. This one is
for everything a WAD declares before any map loads: the lump directory, and the
whole-WAD lumps R_InitTextures parses at startup.

    mode          what it declares                     expected
    valid         nothing (control)                    loads
    hugesize      one lump declares ~112 MB            refused by name
    negpos        one lump declares a negative offset  refused by name
    pasteof       one lump ends one byte past EOF      refused by name
    shortheader   file truncated inside the header     refused by name
    sfxrate       a DSPISTOL declaring 1 Hz            rate falls back to SFXRATE
    texcount      TEXTURE1 declares 100000 textures    refused by name
    texpatches    one texture declares 5000 patches    refused by name
    texgap        a 64-wide texture with a 1-wide patch  counterfactual

The two texture modes carry lump BYTES rather than a bad directory, because
that is where the count lives: TEXTURE1 states how many textures follow it, and
each texture states how many patches follow it. DOOM-0254 bounded the identical
shape in PNAMES and DOOM-0402 bounded these two.

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

    if mode == "texgap":
        # A texture whose patches leave most of its columns uncovered. Legal to
        # state and reachable on real broken WADs -- id downgraded the engine's
        # response from I_Error to a printf deliberately. The patch is carried
        # here too, as the smallest well-formed one: 1x1, so columns 1..63 of
        # the texture have no patch at all.
        patch = (struct.pack("<hhhh", 1, 1, 0, 0)    # width height left top
                 + struct.pack("<i", 12)             # columnofs[0]
                 + bytes([0, 1, 0, 0x40, 0, 0xFF]))  # topdelta len pad px pad end
        tex = (b"FIXGAP\0\0"
               + struct.pack("<ihhi", 0, 64, 64, 0)
               + struct.pack("<h", 1)
               + struct.pack("<hhhhh", 0, 0, 0, 0, 0))
        texture1 = struct.pack("<ii", 1, 8) + tex
        pnames = struct.pack("<i", 1) + b"FIXPATCH"
        # An empty TEXTURE2 as well: this PWAD's PNAMES replaces the IWAD's,
        # so the IWAD's own TEXTURE2 would name patch indices that no longer
        # resolve and the run would stop there instead of reaching the texture
        # under test.
        lumps = [("FIXPATCH", patch), ("PNAMES", pnames),
                 ("TEXTURE1", texture1), ("TEXTURE2", struct.pack("<i", 0))]

    if mode in ("texcount", "texpatches"):
        # TEXTURE1 is: a 4-byte texture count, then that many 4-byte offsets
        # into the same lump, then the texture records. A record is name[8],
        # masked, width, height, columndirectory, patchcount, then patchcount
        # 10-byte patch entries -- 22 bytes of header on disk.
        tex = (b"FIXTURE\0"                      # name[8]
               + struct.pack("<ihhi", 0, 64, 64, 0)   # masked w h columndirectory
               + struct.pack("<h", 5000 if mode == "texpatches" else 1)
               + struct.pack("<hhhhh", 0, 0, 0, 0, 0))  # one real patch entry
        count = 100000 if mode == "texcount" else 1
        texture1 = struct.pack("<ii", count, 8) + tex
        # PNAMES too, so the run reaches the TEXTURE1 walk rather than stopping
        # on a patch name it cannot resolve.
        pnames = struct.pack("<i", 1) + b"FIXTURE\0"
        lumps = [("PNAMES", pnames), ("TEXTURE1", texture1),
                 ("TEXTURE2", struct.pack("<i", 0))]

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
    elif mode not in ("valid", "shortheader", "sfxrate",
                      "texcount", "texpatches", "texgap"):
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
                         "sfxrate|texcount|texpatches|texgap} <out.wad>" % argv[0])
    data = build(argv[1])
    open(argv[2], "wb").write(data)
    print("%s: %s (%d bytes)" % (argv[2], argv[1], len(data)))


if __name__ == "__main__":
    main(sys.argv)
