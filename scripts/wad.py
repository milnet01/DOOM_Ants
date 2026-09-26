"""WAD directory reader and PWAD writer shared by the offline scripts.

One parser for the on-disk format instead of one per script (DOOM-0452).
Pure stdlib, so pbr_derive.py stays a standalone offline tool (DOOM-0042).

make_wad_fixture.py keeps its own writer on purpose: its whole job is to
emit a MALFORMED directory, which this module refuses to read.
"""
import struct


def read_directory(path):
    """Return (data, [(name, offset, size)]) in directory order.

    A name is the bytes up to the first NUL, decoded as latin-1 with its
    case kept; callers that want the engine's case-insensitive lookup
    upper-case it themselves. Raises ValueError on a file that is not a
    WAD or whose directory or lumps fall outside the file.
    """
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12:
        raise ValueError("%s: too short to be a WAD" % path)
    magic, count, diroff = struct.unpack_from("<4sii", data, 0)
    if magic not in (b"IWAD", b"PWAD"):
        raise ValueError("%s is not a WAD (magic %r)" % (path, magic))
    if count < 0 or diroff < 12 or diroff + 16 * count > len(data):
        raise ValueError("%s: lump directory falls outside the file" % path)
    directory = []
    for i in range(count):
        off, size, raw = struct.unpack_from("<ii8s", data, diroff + 16 * i)
        name = raw.split(b"\0")[0].decode("latin-1")
        if size < 0 or off < 0 or off + size > len(data):
            raise ValueError("%s: lump %s falls outside the file" % (path, name))
        directory.append((name, off, size))
    return data, directory


def write_pwad(path, lumps):
    """Write a PWAD: 12-byte header, lump payloads, then the directory.

    `lumps` is [(name, payload_bytes)] in the order they should appear.
    """
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
    with open(path, "wb") as f:
        f.write(header + payloads + directory)
