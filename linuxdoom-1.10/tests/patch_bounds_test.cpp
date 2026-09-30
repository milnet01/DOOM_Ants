// patch_bounds_test.cpp — DOOM-0228: how much of a graphic lump the atlas
// builder may read.
//
// It also holds DOOM-0432's PatchLumpValid cases (INV-1): the whole-lump rule
// every patch reader asks before it follows a column offset. Those labels begin
// "DOOM-0432 INV-1".
//
// The cases that matter are the ones a stock WAD never produces, because
// blit_tile has always been correct for stock data and that is why the missing
// checks went unnoticed. Each boundary is tested on both sides.
#include <cstdio>
#include <climits>
#include <vector>

#include "../patch_bounds.h"
#include "check_util.h"

// ---- DOOM-0432 INV-1 fixtures: patches built byte by byte, little-endian. ----
typedef std::vector<unsigned char> Bytes;

static void put16(Bytes& b, int v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff); }
static void put32(Bytes& b, unsigned v)
{
    for (int i = 0; i < 4; i++) b.push_back((v >> (8 * i)) & 0xff);
}
static void set32(Bytes& b, size_t at, unsigned v)
{
    for (int i = 0; i < 4; i++) b[at + i] = (v >> (8 * i)) & 0xff;
}
// Header plus a zeroed column table of `ncols` entries; width is declared as such.
static Bytes header(int width, int height, int ncols)
{
    Bytes b;
    put16(b, width); put16(b, height); put16(b, 0); put16(b, 0);
    for (int i = 0; i < ncols; i++) put32(b, 0);
    return b;
}
// Point column `col` at the current end of the array (where its posts go next).
static void begin_col(Bytes& b, int col) { set32(b, 8 + 4 * col, (unsigned)b.size()); }
// topdelta, length, pad, `len` texels, pad.
static void post(Bytes& b, int len)
{
    b.push_back(0); b.push_back(len); b.push_back(0);
    for (int i = 0; i < len; i++) b.push_back(0x11);
    b.push_back(0);
}
static void endcol(Bytes& b) { b.push_back(0xff); }
static int valid(const Bytes& b) { return PatchLumpValid(b.data(), (int)b.size()); }

int main()
{
    // ---- FlatFits ----
    //
    // A flat is read as h rows of 64, taking w from each, so the last byte
    // touched is (h-1)*64 + (w-1) and a full tile needs exactly 4096.
    check(FlatFits(4096, 64, 64) != 0, "a stock 64x64 flat fits");
    check(FlatFits(8192, 64, 64) != 0, "an oversized lump fits");
    check(FlatFits(4095, 64, 64) == 0,
          "one byte short of a full flat is refused");
    check(FlatFits(0, 64, 64) == 0, "an empty lump is refused");

    // The read is strided by 64 whatever w is, so a part-width tile still
    // reaches into the last row.
    check(FlatFits(4032 + 1, 1, 64) != 0,
          "a one-column read needs only up to the first byte of the last row");
    check(FlatFits(4032, 1, 64) == 0,
          "one byte less than that is refused");

    // Nonsense dimensions must not be talked into a pass.
    check(FlatFits(4096, 0, 64) == 0, "a zero width is refused");
    check(FlatFits(4096, 64, 0) == 0, "a zero height is refused");
    check(FlatFits(4096, -1, 64) == 0, "a negative width is refused");
    check(FlatFits(1 << 20, 65, 64) == 0,
          "a width past the 64 stride is refused however big the lump");
    check(FlatFits(1 << 20, 64, 65) == 0, "a height past 64 is refused");

    // ---- PatchHeaderFits ----
    //
    // Four shorts, then one 32-bit column offset per column.
    check(PatchHeaderFits(8 + 4 * 16, 16) != 0,
          "a header with room for its column offsets fits");
    check(PatchHeaderFits(8 + 4 * 16 - 1, 16) == 0,
          "one byte short of the offset table is refused");
    check(PatchHeaderFits(7, 1) == 0, "a lump too short for the header is refused");
    check(PatchHeaderFits(8, 0) == 0, "a zero width is refused");

    // The case that motivates the subtraction: a width read from the lump can
    // be large enough that width*4 would overflow if computed first.
    check(PatchHeaderFits(100, 32767) == 0,
          "a huge declared width is refused without overflowing the multiply");

    // ---- PatchColumnFits ----
    //
    // A column offset must leave room for the two-byte post header.
    check(PatchColumnFits(0, 100) != 0, "offset zero fits");
    check(PatchColumnFits(98, 100) != 0, "the last usable offset fits");
    check(PatchColumnFits(99, 100) == 0,
          "an offset with only one byte left is refused");
    check(PatchColumnFits(100, 100) == 0, "an offset at the end is refused");
    check(PatchColumnFits(-1, 100) == 0, "a negative offset is refused");
    check(PatchColumnFits(1 << 30, 100) == 0, "a wild offset is refused");

    // ---- PatchPostFits ----
    //
    // Texels start three bytes into the post, so the last byte read is
    // pos + 2 + length.
    check(PatchPostFits(0, 97, 100) != 0, "a post ending exactly at the end fits");
    check(PatchPostFits(0, 98, 100) == 0, "one texel past the end is refused");
    check(PatchPostFits(0, 0, 100) != 0, "an empty post fits");
    check(PatchPostFits(97, 0, 100) != 0, "an empty post at the last header fits");
    check(PatchPostFits(98, 0, 100) == 0,
          "a header with no room for its texel start is refused");
    check(PatchPostFits(-1, 0, 100) == 0, "a negative position is refused");
    check(PatchPostFits(0, -1, 100) == 0, "a negative length is refused");
    check(PatchPostFits(0, 0, 2) == 0, "a lump too short for any post is refused");

    // A length byte is 0..255. The last texel sits at pos+2+length, so a
    // full-length post at offset 0 needs 258 bytes -- not 257, which is the
    // off-by-one this assertion was written wrong the first time.
    check(PatchPostFits(0, 255, 258) != 0, "the largest post fits a lump sized for it");
    check(PatchPostFits(0, 255, 257) == 0, "the same post is refused one byte short");

    // ---- PatchHasHeader ----
    //
    // Why this exists: r_mesh.c tile_size(), r_mesh.c ensure_sprite_heights()
    // and r_data.c R_InitSpriteLumps() read a sprite patch's four-short header
    // with no check the lump held 8 bytes (security review, F-B). A truncated or
    // empty sprite lump was read past its end.
    check(PatchHasHeader(8) != 0, "exactly the 8 header bytes is enough");
    check(PatchHasHeader(7) == 0, "one byte short of the header is refused");
    check(PatchHasHeader(0) == 0, "an empty lump has no header");
    check(PatchHasHeader(1) == 0, "a one-byte lump has no header");
    check(PatchHasHeader(-1) == 0, "a negative length has no header");
    check(PatchHasHeader(INT_MIN) == 0, "INT_MIN has no header");
    check(PatchHasHeader(INT_MAX) != 0, "a huge lump has a header");

    // Agrees with PatchHeaderFits: whatever fits a column table has a header.
    for (int len = -2; len <= 40; len++)
        if (PatchHeaderFits(len, 1) != 0)
            check(PatchHasHeader(len) != 0,
                  "PatchHeaderFits implies PatchHasHeader");

    // ---- DOOM-0432 INV-1: PatchLumpValid ----
    //
    // Why this exists: every patch reader followed columnofs[] and each post's
    // length with no idea how long the lump was, so a crafted lump read anywhere
    // in memory. This is the one rule they now share.

    // One column, one post of 2 texels: table ends at 12, post 12..17, the
    // terminator is the FINAL byte (19 bytes). Stock patches end this way.
    Bytes one = header(1, 1, 1);
    begin_col(one, 0); post(one, 2); endcol(one);
    check(valid(one), "DOOM-0432 INV-1: a patch whose terminator is its last byte is accepted");
    check(PatchLumpValid(one.data(), (int)one.size() - 1) == 0,
          "DOOM-0432 INV-1: the same patch one byte shorter (terminator missing) is refused");

    // A post whose length field puts the next post's first byte one past the end.
    {
        Bytes b = one;
        b[13] = 3;
        check(!valid(b),
              "DOOM-0432 INV-1: a post whose successor byte is one past the end is refused");
        b[13] = 2;
        check(valid(b),
              "DOOM-0432 INV-1: the same post one byte shorter (exactly fitting) is accepted");
        b[13] = 255;
        check(!valid(b), "DOOM-0432 INV-1: a 255-texel post in a 19-byte lump is refused");
    }

    // Column offsets.
    {
        Bytes b = one;
        set32(b, 8, (unsigned)b.size());
        check(!valid(b), "DOOM-0432 INV-1: a column offset equal to len is refused");
        set32(b, 8, (unsigned)b.size() - 1);
        check(valid(b), "DOOM-0432 INV-1: a column offset at the last byte (the terminator) is accepted");
        set32(b, 8, 0xffffffffu);
        check(!valid(b), "DOOM-0432 INV-1: a negative column offset (-1) is refused");
        set32(b, 8, 0x80000000u);
        check(!valid(b), "DOOM-0432 INV-1: INT_MIN as a column offset is refused");
        set32(b, 8, 0x7fffffffu);
        check(!valid(b), "DOOM-0432 INV-1: a wild positive column offset is refused");
    }

    // A column offset equal to len, with a 0xff guard byte sitting at index len in
    // a LARGER buffer: the walk must refuse because the byte is outside the lump,
    // not because of what happens to lie there. (Kills an "offset == len is
    // accepted" off-by-one that a zero byte past the array would hide.)
    {
        Bytes b = one;
        const int len = (int)b.size();
        set32(b, 8, (unsigned)len);
        b.push_back(0xff);                       // guard terminator at index len
        b.push_back(0xff); b.push_back(0xff);
        check(PatchLumpValid(b.data(), len) == 0,
              "DOOM-0432 INV-1: a column offset equal to len is refused even when a 0xff sits at index len");
    }

    // Width one more than the table holds, where the extra table entry lies wholly
    // past len yet reads as a VALID offset. len 12 = header + one entry; the offset
    // 7 points at data[7] = 0xff (topoffset's high byte), a terminator inside the
    // lump. Bytes 12..15 are guards: a second entry of 7.
    {
        unsigned char raw[16] = { 1,0, 1,0, 0,0, 0xff,0xff,  7,0,0,0,  7,0,0,0 };
        check(PatchLumpValid(raw, 12) != 0,
              "DOOM-0432 INV-1: width 1 in a 12-byte lump (header + one entry, offset into the header) is accepted");
        raw[0] = 2;
        check(PatchLumpValid(raw, 12) == 0,
              "DOOM-0432 INV-1: width 2 in that 12-byte lump is refused although the second entry beyond len is a valid offset");
    }

    // Header fields and length.
    {
        Bytes b = one;
        b[0] = 0; b[1] = 0;
        check(!valid(b), "DOOM-0432 INV-1: width 0 is refused");
        b = one;
        b[2] = 0; b[3] = 0;
        check(!valid(b), "DOOM-0432 INV-1: height 0 is refused");
        check(PatchLumpValid(one.data(), 7) == 0, "DOOM-0432 INV-1: len 7 is refused");
        check(PatchLumpValid(one.data(), 0) == 0, "DOOM-0432 INV-1: len 0 is refused");
        check(PatchLumpValid(one.data(), -1) == 0, "DOOM-0432 INV-1: a negative len is refused");
    }

    // Width against the column table: two columns sharing one bare terminator at
    // byte 16. len 17 leaves (17-8)/4 = 2 table entries.
    {
        Bytes b = header(2, 1, 2);
        set32(b, 8, (unsigned)b.size());
        set32(b, 12, (unsigned)b.size());
        endcol(b);
        check(valid(b), "DOOM-0432 INV-1: width equal to the entries the table holds is accepted");
        b[0] = 3;
        check(!valid(b), "DOOM-0432 INV-1: width one more than the table holds is refused");
    }

    // A column of two posts.
    {
        Bytes b = header(1, 1, 1);
        begin_col(b, 0); post(b, 2); post(b, 3); endcol(b);
        check(valid(b), "DOOM-0432 INV-1: a valid two-post column is accepted");
        check(PatchLumpValid(b.data(), (int)b.size() - 1) == 0,
              "DOOM-0432 INV-1: the two-post column one byte short is refused");
        b[19] = 4;   // second post's length: pushes its successor past the end
        check(!valid(b), "DOOM-0432 INV-1: a bad SECOND post in a column is refused");
    }

    // Two columns, only the second bad: the first must not excuse it.
    {
        Bytes b = header(2, 1, 2);
        begin_col(b, 0); post(b, 1); endcol(b);
        begin_col(b, 1); post(b, 1); endcol(b);
        check(valid(b), "DOOM-0432 INV-1: a valid two-column patch is accepted");
        set32(b, 12, (unsigned)b.size());
        check(!valid(b), "DOOM-0432 INV-1: a patch whose only bad column is the second is refused");
    }

    return check_summary("patch_bounds");
}
