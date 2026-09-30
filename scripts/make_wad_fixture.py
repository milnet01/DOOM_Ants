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
    noflats       F_END placed directly after F_START   refused by name
    textall       the IWAD's TEXTURE1 plus one 32767-tall texture  loads, tile cropped
    badpatch-ui      STBAR with its first column offset past the lump  refused, not drawn
    badpatch-sprite  the pistol's first frame, likewise               refused, not drawn
    badpatch-wall    the first patch PNAMES lists, likewise           refused, skipped

The two texture modes carry lump BYTES rather than a bad directory, because
that is where the count lives: TEXTURE1 states how many textures follow it, and
each texture states how many patches follow it. DOOM-0254 bounded the identical
shape in PNAMES and DOOM-0402 bounded these two.

The sfxrate mode is about DOOM-0386 rather than the directory: a sound lump's
declared sample rate is attacker-controlled, and 1 Hz made SDL build a ~44100x
upsample whose conversion buffer came to about 11 GB for a 64 KB sound.

The textall mode is about DOOM-0221: a texture's declared height sizes an atlas
tile and then a GPU image, and 32767 is what the format lets it say. It is the
one mode that needs an IWAD, because a map will not load against a TEXTURE1 that
lacks the textures its sidedefs name -- so the fixture is the IWAD's own lump
with one texture appended, built from the IWAD's first patch.

The three badpatch modes are about DOOM-0432: a patch's column offsets are the
lump's own, and every reader followed them. Each copies one real patch out of
the IWAD and rewrites its first column offset to land far outside the lump;
the header and the offset table stay intact, so nothing but a bound on that
offset can refuse it. A sprite is only read from between S_START and S_END, and
a PWAD's markers replace the IWAD's, so that mode carries every sprite.

Usage:  make_wad_fixture.py <mode> <out.wad> [<iwad>]

textall and the badpatch modes need the IWAD; for every other mode the lumps are
synthetic, because the directory is the subject and the payload never has to
mean anything.
"""

import struct
import sys

HEADER_SIZE = 12
DIRENT_SIZE = 16


def build(mode, iwad=None):
    """Return the PWAD bytes for `mode`."""
    # Two ordinary lumps and an empty marker, so the control is a WAD the engine
    # accepts and the malformed modes differ from it in one field only.
    lumps = [("FIXTURE1", b"\x01" * 64), ("MARKER", b""), ("FIXTURE2", b"\x02" * 32)]

    if mode == "noflats":
        # Two adjacent markers and nothing between them, so firstflat ends up
        # one PAST lastflat and the count goes negative. W_GetNumForName takes
        # the LAST lump of a name, so a PWAD's markers win over the IWAD's.
        lumps = [("F_START", b""), ("F_END", b"")]

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

    if mode == "textall":
        if not iwad:
            raise SystemExit("textall needs the IWAD whose TEXTURE1 it extends")
        from wad import read_directory
        data, directory = read_directory(iwad)
        found = [(o, n) for name, o, n in directory if name == "TEXTURE1"]
        if not found:
            raise SystemExit("no TEXTURE1 lump in that WAD")
        off, size = found[-1]
        tex1 = data[off:off + size]
        count = struct.unpack_from("<i", tex1, 0)[0]
        offsets = struct.unpack_from("<%di" % count, tex1, 4)
        records = tex1[4 + 4 * count:]
        # The texture is exactly as wide as the patch it is built from, so every
        # column has one patch and needs no composite. A column with none or
        # several is composited, and R_GenerateLookup refuses a composite past
        # 64 KB by name -- which would stop the run before the atlas is built.
        names = [(o, n) for name, o, n in directory if name == "PNAMES"]
        if not names:
            raise SystemExit("no PNAMES lump in that WAD")
        first = data[names[-1][0] + 4:names[-1][0] + 12].split(b"\0")[0].decode("latin-1")
        patch = [(o, n) for name, o, n in directory if name.upper() == first.upper()]
        if not patch:
            raise SystemExit("PNAMES names %s, which that WAD does not hold" % first)
        width = struct.unpack_from("<h", data, patch[-1][0])[0]
        # One more offset in the table moves every record 4 bytes down the lump.
        tall = (b"FIXTALL\0"
                + struct.pack("<ihhi", 0, width, 32767, 0)  # masked w h columndirectory
                + struct.pack("<h", 1)
                + struct.pack("<hhhhh", 0, 0, 0, 0, 0))   # the IWAD's patch 0
        count += 1
        offsets = [o + 4 for o in offsets] + [4 + 4 * count + len(records)]
        lumps = [("TEXTURE1", struct.pack("<i", count)
                  + struct.pack("<%di" % count, *offsets) + records + tall)]

    if mode.startswith("badpatch-"):
        if not iwad:
            raise SystemExit("%s needs the IWAD it takes the patch from" % mode)
        from wad import read_directory
        data, directory = read_directory(iwad)
        names = [n.upper() for n, _, _ in directory]

        def broken(index):
            # Column offset 0 rewritten to a value no lump reaches. The field is
            # a signed 32-bit offset from the start of the patch.
            name, off, size = directory[index]
            patch = bytearray(data[off:off + size])
            struct.pack_into("<i", patch, 8, 0x7FFFFF00)
            return name, bytes(patch)

        if mode == "badpatch-ui":
            lumps = [broken(names.index("STBAR"))]
        elif mode == "badpatch-wall":
            at = names.index("PNAMES")
            first = data[directory[at][1] + 4:directory[at][1] + 12]
            first = first.split(b"\0")[0].decode("latin-1").upper()
            lumps = [broken(names.index(first))]
        elif mode == "badpatch-sprite":
            start, end = names.index("S_START"), names.index("S_END")
            target = names.index("PISGA0")
            lumps = [("S_START", b"")]
            for i in range(start + 1, end):
                name, off, size = directory[i]
                lumps.append(broken(i) if i == target else (name, data[off:off + size]))
            lumps.append(("S_END", b""))
        else:
            raise SystemExit("unknown mode: %s" % mode)

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
    elif mode not in ("valid", "shortheader", "sfxrate", "texcount",
                      "texpatches", "texgap", "noflats", "textall",
                      "badpatch-ui", "badpatch-sprite", "badpatch-wall"):
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
    if len(argv) not in (3, 4):
        raise SystemExit("usage: %s {valid|hugesize|negpos|pasteof|shortheader|"
                         "sfxrate|texcount|texpatches|texgap|noflats|textall|"
                         "badpatch-ui|badpatch-sprite|badpatch-wall} "
                         "<out.wad> [<iwad>]" % argv[0])
    data = build(argv[1], argv[3] if len(argv) == 4 else None)
    open(argv[2], "wb").write(data)
    print("%s: %s (%d bytes)" % (argv[2], argv[1], len(data)))


if __name__ == "__main__":
    main(sys.argv)
