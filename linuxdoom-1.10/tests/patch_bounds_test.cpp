// patch_bounds_test.cpp — DOOM-0228: how much of a graphic lump the atlas
// builder may read.
//
// The cases that matter are the ones a stock WAD never produces, because
// blit_tile has always been correct for stock data and that is why the missing
// checks went unnoticed. Each boundary is tested on both sides.
#include <cstdio>

#include "../patch_bounds.h"
#include "check_util.h"

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

    return check_summary("patch_bounds");
}
