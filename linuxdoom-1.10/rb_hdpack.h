// rb_hdpack.h — DOOM-0632: read and write ultra.hdpack, Ultra's HD art packed for the GPU
// (docs/specs/DOOM-0632-packed-hd-art.md § 4.1).
//
// Header-only so the game, the baker and a test share one reader. The pack comes from a folder
// the user can set (DOOMASSETDIR), so it is untrusted: every count is capped before it sizes
// anything, every offset is checked against the real file length, and a level's unpacked size
// is computed from its format and dimensions, never read from the file (INV-2).
//
// Fields are little-endian and read with memcpy, as rb_model.h reads a .dmdl.
//
// A level may be stored LZ4-compressed (spec § 4.5), so every user links the vendored lz4.c.
#ifndef RB_HDPACK_H
#define RB_HDPACK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz4.h"

#define RB_HDPACK_NAME        "ultra.hdpack"
#define RB_HDPACK_VERSION     1u
#define RB_HDPACK_HEADER_SIZE 32u
#define RB_HDPACK_ENTRY_SIZE  32u
#define RB_HDPACK_MIP_SIZE    16u
#define RB_HDPACK_MAX_EDGE    4096u   // r_vulkan.cpp's kHdMaxEdge; a larger level 0 is refused
#define RB_HDPACK_MAX_ENTRIES 65536u
#define RB_HDPACK_MAX_NAME    191u    // the loader's path buffers hold 192
// Offsets stay within a long on every target (it is 32 bits on Windows), so plain fseek serves.
#define RB_HDPACK_MAX_FILE    0x7fffffffu

enum
{
    RB_HDPACK_BC7_SRGB = 1,
    RB_HDPACK_BC7_UNORM,
    RB_HDPACK_BC4_UNORM,
    RB_HDPACK_R8_UNORM,
    RB_HDPACK_RGBA8_SRGB,
    RB_HDPACK_RGBA8_UNORM,
    RB_HDPACK_FORMAT_END
};

enum { RB_HDPACK_STORED = 0, RB_HDPACK_LZ4 = 1, RB_HDPACK_ZSTD = 2, RB_HDPACK_CODEC_END };
// The codecs RB_HdPackReadLevel unpacks, as RB_HdPackUsable's codecMask. Zstandard has a
// number and no reader: LZ4 won the measurement (spec § 10).
#define RB_HDPACK_CODECS ((1u << RB_HDPACK_STORED) | (1u << RB_HDPACK_LZ4))

typedef struct
{
    uint32_t nameOffset;
    uint16_t nameLen;
    uint8_t  format;
    uint8_t  mipLevels;
    uint16_t width, height;
    uint32_t firstMip;
    uint64_t srcBytes;
    int64_t  srcMtime;
} rb_hdpack_entry_t;

typedef struct
{
    uint64_t offset;
    uint32_t storedBytes;
    uint8_t  codec;
} rb_hdpack_mip_t;

// A parsed, validated index. Entries are sorted by name. Free with RB_HdPackFree.
typedef struct
{
    uint32_t           entryCount, mipCount, nameBytes;
    uint64_t           fileBytes;
    rb_hdpack_entry_t* entries;
    rb_hdpack_mip_t*   mips;
    char*              names;   // nameBytes long, no terminators
} rb_hdpack_t;

static inline int RB_HdPackFormatSrgb(unsigned format)
{
    return format == RB_HDPACK_BC7_SRGB || format == RB_HDPACK_RGBA8_SRGB;
}

// Block-compressed: sampling it needs the device's textureCompressionBC.
static inline int RB_HdPackFormatBlock(unsigned format)
{
    return format >= RB_HDPACK_BC7_SRGB && format <= RB_HDPACK_BC4_UNORM;
}

// Levels in a full mip chain, as r_vulkan.cpp's UploadImageBatch counts them.
static inline uint32_t RB_HdPackLevels(uint32_t w, uint32_t h)
{
    uint32_t levels = 1, edge = w > h ? w : h;
    for (; edge > 1; edge >>= 1) levels++;
    return levels;
}

// Unpacked bytes of one level; 0 for an unknown format.
static inline uint32_t RB_HdPackLevelBytes(unsigned format, uint32_t w, uint32_t h, uint32_t level)
{
    uint32_t lw = w >> level, lh = h >> level;
    if (lw == 0) lw = 1;
    if (lh == 0) lh = 1;
    switch (format)
    {
        case RB_HDPACK_BC7_SRGB:
        case RB_HDPACK_BC7_UNORM:   return ((lw + 3) / 4) * ((lh + 3) / 4) * 16u;
        case RB_HDPACK_BC4_UNORM:   return ((lw + 3) / 4) * ((lh + 3) / 4) * 8u;
        case RB_HDPACK_R8_UNORM:    return lw * lh;
        case RB_HDPACK_RGBA8_SRGB:
        case RB_HDPACK_RGBA8_UNORM: return lw * lh * 4u;
        default:                    return 0;
    }
}

// Where the data begins: the end of the header, both tables and the name block.
static inline uint64_t RB_HdPackDataOffset(uint32_t entries, uint32_t mips, uint32_t nameBytes)
{
    return (uint64_t)RB_HDPACK_HEADER_SIZE + (uint64_t)entries * RB_HDPACK_ENTRY_SIZE
         + (uint64_t)mips * RB_HDPACK_MIP_SIZE + nameBytes;
}

static inline void RB_HdPackFree(rb_hdpack_t* p)
{
    free(p->entries);
    free(p->mips);
    free(p->names);
    memset(p, 0, sizeof *p);
}

static inline const char* RB_HdPackEntryName(const rb_hdpack_t* p, const rb_hdpack_entry_t* e)
{
    return p->names + e->nameOffset;
}

// Byte order, then the shorter name first.
static inline int RB_HdPackNameCmp(const char* a, size_t alen, const char* b, size_t blen)
{
    const int c = memcmp(a, b, alen < blen ? alen : blen);
    return c != 0 ? c : (alen > blen) - (alen < blen);
}

// Header counts only, each capped. Returns the data offset, or 0 if the header is refused.
// `fileBytes` is the file's real length, not the header's claim.
static inline uint64_t RB_HdPackHeader(const unsigned char* b, size_t avail, uint64_t fileBytes,
                                       uint32_t* entries, uint32_t* mips, uint32_t* nameBytes)
{
    uint32_t version;
    uint64_t claimed, dataOffset;

    if (!b || avail < RB_HDPACK_HEADER_SIZE || memcmp(b, "DHDP", 4) != 0)
        return 0;
    memcpy(&version, b + 4, 4);
    memcpy(entries, b + 8, 4);
    memcpy(mips, b + 12, 4);
    memcpy(nameBytes, b + 16, 4);
    memcpy(&claimed, b + 24, 8);
    if (version != RB_HDPACK_VERSION || claimed != fileBytes || fileBytes > RB_HDPACK_MAX_FILE)
        return 0;
    if (*entries > RB_HDPACK_MAX_ENTRIES)   // bounds the index allocation; the rest is bounded
        return 0;                           // by the tables having to fit the real file
    dataOffset = RB_HdPackDataOffset(*entries, *mips, *nameBytes);
    return dataOffset <= fileBytes ? dataOffset : 0;
}

// Parses the header, both tables and the name block from the first `avail` bytes of a file
// whose real length is `fileBytes`. Fills *out and returns 1, or returns 0 and leaves it empty.
static inline int RB_HdPackParse(const void* data, size_t avail, uint64_t fileBytes, rb_hdpack_t* out)
{
    const unsigned char* b = (const unsigned char*)data;
    const unsigned char* et;
    const unsigned char* mt;
    uint32_t ne, nm, nb, i, l;
    uint64_t dataOffset;

    memset(out, 0, sizeof *out);
    dataOffset = RB_HdPackHeader(b, avail, fileBytes, &ne, &nm, &nb);
    if (dataOffset == 0 || avail < dataOffset)
        return 0;

    out->entries = (rb_hdpack_entry_t*)calloc(ne ? ne : 1, sizeof *out->entries);
    out->mips    = (rb_hdpack_mip_t*)calloc(nm ? nm : 1, sizeof *out->mips);
    out->names   = (char*)malloc(nb ? nb : 1);
    if (!out->entries || !out->mips || !out->names)
        goto refuse;
    out->entryCount = ne;
    out->mipCount   = nm;
    out->nameBytes  = nb;
    out->fileBytes  = fileBytes;

    et = b + RB_HDPACK_HEADER_SIZE;
    mt = et + (size_t)ne * RB_HDPACK_ENTRY_SIZE;
    memcpy(out->names, mt + (size_t)nm * RB_HDPACK_MIP_SIZE, nb);

    for (i = 0; i < nm; i++)
    {
        rb_hdpack_mip_t* m = &out->mips[i];
        memcpy(&m->offset, mt + (size_t)i * RB_HDPACK_MIP_SIZE, 8);
        memcpy(&m->storedBytes, mt + (size_t)i * RB_HDPACK_MIP_SIZE + 8, 4);
        m->codec = mt[(size_t)i * RB_HDPACK_MIP_SIZE + 12];
        // Inside the data: past the tables, and wholly before the end of the file.
        if (m->codec >= RB_HDPACK_CODEC_END || m->storedBytes == 0
            || m->offset < dataOffset || m->offset > fileBytes
            || m->storedBytes > fileBytes - m->offset)
            goto refuse;
    }

    for (i = 0; i < ne; i++)
    {
        rb_hdpack_entry_t*   e = &out->entries[i];
        const unsigned char* r = et + (size_t)i * RB_HDPACK_ENTRY_SIZE;
        memcpy(&e->nameOffset, r, 4);
        memcpy(&e->nameLen, r + 4, 2);
        e->format    = r[6];
        e->mipLevels = r[7];
        memcpy(&e->width, r + 8, 2);
        memcpy(&e->height, r + 10, 2);
        memcpy(&e->firstMip, r + 12, 4);
        memcpy(&e->srcBytes, r + 16, 8);
        memcpy(&e->srcMtime, r + 24, 8);

        if (e->width == 0 || e->height == 0
            || e->width > RB_HDPACK_MAX_EDGE || e->height > RB_HDPACK_MAX_EDGE
            || e->format == 0 || e->format >= RB_HDPACK_FORMAT_END
            || e->mipLevels != RB_HdPackLevels(e->width, e->height)
            || e->firstMip > nm || e->mipLevels > nm - e->firstMip
            || e->nameLen == 0 || e->nameLen > RB_HDPACK_MAX_NAME
            || e->nameOffset > nb || e->nameLen > nb - e->nameOffset)
            goto refuse;
        if (i > 0)
        {
            const rb_hdpack_entry_t* p = &out->entries[i - 1];
            if (RB_HdPackNameCmp(out->names + p->nameOffset, p->nameLen,
                                 out->names + e->nameOffset, e->nameLen) >= 0)
                goto refuse;   // out of order, or the same name twice
        }
        for (l = 0; l < e->mipLevels; l++)
        {
            const rb_hdpack_mip_t* m = &out->mips[e->firstMip + l];
            if (m->codec == RB_HDPACK_STORED
                && m->storedBytes != RB_HdPackLevelBytes(e->format, e->width, e->height, l))
                goto refuse;
        }
    }
    return 1;

refuse:
    RB_HdPackFree(out);
    return 0;
}

// Reads and validates an open pack's index. The file stays the caller's, for RB_HdPackReadLevel.
static inline int RB_HdPackOpen(FILE* f, rb_hdpack_t* out)
{
    unsigned char  head[RB_HDPACK_HEADER_SIZE];
    unsigned char* buf;
    uint32_t ne, nm, nb;
    uint64_t dataOffset;
    long     len;
    int      ok;

    memset(out, 0, sizeof *out);
    if (!f || fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0)
        return 0;
    if (fread(head, 1, sizeof head, f) != sizeof head)
        return 0;
    dataOffset = RB_HdPackHeader(head, sizeof head, (uint64_t)len, &ne, &nm, &nb);
    if (dataOffset == 0)
        return 0;
    buf = (unsigned char*)malloc((size_t)dataOffset);
    if (!buf)
        return 0;
    memcpy(buf, head, sizeof head);
    ok = fread(buf + sizeof head, 1, (size_t)dataOffset - sizeof head, f)
             == (size_t)dataOffset - sizeof head
         && RB_HdPackParse(buf, (size_t)dataOffset, (uint64_t)len, out);
    free(buf);
    return ok;
}

// The entry named `name`, or NULL.
static inline const rb_hdpack_entry_t* RB_HdPackFind(const rb_hdpack_t* p, const char* name)
{
    const size_t len = strlen(name);
    uint32_t lo = 0, hi = p->entryCount;
    while (lo < hi)
    {
        const uint32_t mid = lo + (hi - lo) / 2;
        const rb_hdpack_entry_t* e = &p->entries[mid];
        const int c = RB_HdPackNameCmp(p->names + e->nameOffset, e->nameLen, name, len);
        if (c == 0) return e;
        if (c < 0) lo = mid + 1; else hi = mid;
    }
    return NULL;
}

// Whether the loader may take this picture from the pack (spec § 4.4 Probe; INV-4, INV-5).
// It may not when the format's sRGB-ness is not the caller's, when the device cannot sample
// a block format, when a level uses a codec outside `codecMask` (bit n = codec n), or when
// the loose PNG exists and its length or modification time differs from the baked ones: an
// edited PNG wins.
static inline int RB_HdPackUsable(const rb_hdpack_t* p, const rb_hdpack_entry_t* e,
                                  int wantSrgb, int bcSupported, unsigned codecMask,
                                  int pngPresent, uint64_t pngBytes, int64_t pngMtime)
{
    uint32_t l;
    if (RB_HdPackFormatSrgb(e->format) != (wantSrgb != 0))
        return 0;
    if (RB_HdPackFormatBlock(e->format) && !bcSupported)
        return 0;
    if (pngPresent && (pngBytes != e->srcBytes || pngMtime != e->srcMtime))
        return 0;
    for (l = 0; l < e->mipLevels; l++)
        if (!(codecMask >> p->mips[e->firstMip + l].codec & 1u))
            return 0;
    return 1;
}

// Reads one level into dst, which must hold RB_HdPackLevelBytes of it. A stored level is read
// straight into dst. An LZ4 level is unpacked in ordinary memory and then copied: the unpacker
// reads back what it has written, and dst may be write-combined memory. The unpacker is given
// room for exactly the computed size and must fill it (INV-3). Returns 1, or 0 on a short
// read, a level that unpacks to another length, no memory, or a codec outside
// RB_HDPACK_CODECS.
static inline int RB_HdPackReadLevel(FILE* f, const rb_hdpack_t* p, const rb_hdpack_entry_t* e,
                                     uint32_t level, unsigned char* dst)
{
    const rb_hdpack_mip_t* m;
    unsigned char* packed;
    unsigned char* plain;
    uint32_t want;
    int ok;

    if (level >= e->mipLevels)
        return 0;
    m = &p->mips[e->firstMip + level];
    if (fseek(f, (long)m->offset, SEEK_SET) != 0)
        return 0;
    if (m->codec == RB_HDPACK_STORED)   // RB_HdPackParse checked storedBytes is the level's size
        return fread(dst, 1, m->storedBytes, f) == m->storedBytes;
    if (m->codec != RB_HDPACK_LZ4)
        return 0;

    want   = RB_HdPackLevelBytes(e->format, e->width, e->height, level);
    packed = (unsigned char*)malloc(m->storedBytes);
    plain  = (unsigned char*)malloc(want);
    ok = packed && plain && fread(packed, 1, m->storedBytes, f) == m->storedBytes
         && LZ4_decompress_safe((const char*)packed, (char*)plain, (int)m->storedBytes, (int)want)
                == (int)want;
    if (ok)
        memcpy(dst, plain, want);
    free(packed);
    free(plain);
    return ok;
}

// ---- Writing (the baker and the tests) ----

// One picture to write. levels[l] and stored[l] give level l's bytes as they go on disk;
// codec[l] says how they were packed.
typedef struct
{
    const char* name;
    uint8_t     format;
    uint16_t    width, height;
    uint64_t    srcBytes;
    int64_t     srcMtime;
    const unsigned char* const* levels;
    const uint32_t*      stored;
    const uint8_t*       codec;
} rb_hdpack_src_t;

// Writes a whole pack. `srcs` must be sorted by name (RB_HdPackNameCmp) with no repeats; each
// must carry its full mip chain. Returns 1, or 0 on a bad argument or a failed write. The same
// arguments give the same bytes.
static inline int RB_HdPackWrite(FILE* f, const rb_hdpack_src_t* srcs, uint32_t n)
{
    unsigned char rec[RB_HDPACK_ENTRY_SIZE];
    uint32_t i, l, nm = 0, nb = 0, version = RB_HDPACK_VERSION, zero = 0, mipAt = 0, nameAt = 0;
    uint64_t at, fileBytes;

    if (n > RB_HDPACK_MAX_ENTRIES)
        return 0;
    for (i = 0; i < n; i++)
    {
        const size_t len = strlen(srcs[i].name);
        if (len == 0 || len > RB_HDPACK_MAX_NAME
            || (i > 0 && RB_HdPackNameCmp(srcs[i - 1].name, strlen(srcs[i - 1].name),
                                          srcs[i].name, len) >= 0))
            return 0;
        nm += RB_HdPackLevels(srcs[i].width, srcs[i].height);
        nb += (uint32_t)len;
    }
    at = fileBytes = RB_HdPackDataOffset(n, nm, nb);
    for (i = 0; i < n; i++)
        for (l = 0; l < RB_HdPackLevels(srcs[i].width, srcs[i].height); l++)
            fileBytes += srcs[i].stored[l];
    if (fileBytes > RB_HDPACK_MAX_FILE)
        return 0;

    memset(rec, 0, sizeof rec);
    memcpy(rec, "DHDP", 4);
    memcpy(rec + 4, &version, 4);
    memcpy(rec + 8, &n, 4);
    memcpy(rec + 12, &nm, 4);
    memcpy(rec + 16, &nb, 4);
    memcpy(rec + 20, &zero, 4);
    memcpy(rec + 24, &fileBytes, 8);
    if (fwrite(rec, 1, RB_HDPACK_HEADER_SIZE, f) != RB_HDPACK_HEADER_SIZE)
        return 0;

    for (i = 0; i < n; i++)
    {
        const uint16_t len = (uint16_t)strlen(srcs[i].name);
        memset(rec, 0, sizeof rec);
        memcpy(rec, &nameAt, 4);
        memcpy(rec + 4, &len, 2);
        rec[6] = srcs[i].format;
        rec[7] = (unsigned char)RB_HdPackLevels(srcs[i].width, srcs[i].height);
        memcpy(rec + 8, &srcs[i].width, 2);
        memcpy(rec + 10, &srcs[i].height, 2);
        memcpy(rec + 12, &mipAt, 4);
        memcpy(rec + 16, &srcs[i].srcBytes, 8);
        memcpy(rec + 24, &srcs[i].srcMtime, 8);
        if (fwrite(rec, 1, RB_HDPACK_ENTRY_SIZE, f) != RB_HDPACK_ENTRY_SIZE)
            return 0;
        nameAt += len;
        mipAt  += rec[7];
    }
    for (i = 0; i < n; i++)
        for (l = 0; l < RB_HdPackLevels(srcs[i].width, srcs[i].height); l++)
        {
            memset(rec, 0, sizeof rec);
            memcpy(rec, &at, 8);
            memcpy(rec + 8, &srcs[i].stored[l], 4);
            rec[12] = srcs[i].codec[l];
            if (fwrite(rec, 1, RB_HDPACK_MIP_SIZE, f) != RB_HDPACK_MIP_SIZE)
                return 0;
            at += srcs[i].stored[l];
        }
    for (i = 0; i < n; i++)
        if (fwrite(srcs[i].name, 1, strlen(srcs[i].name), f) != strlen(srcs[i].name))
            return 0;
    for (i = 0; i < n; i++)
        for (l = 0; l < RB_HdPackLevels(srcs[i].width, srcs[i].height); l++)
            if (fwrite(srcs[i].levels[l], 1, srcs[i].stored[l], f) != srcs[i].stored[l])
                return 0;
    return 1;
}

#endif
