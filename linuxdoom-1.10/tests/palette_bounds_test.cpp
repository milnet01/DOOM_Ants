// palette_bounds_test.cpp — DOOM-0404: a PLAYPAL lump must cover every palette
// the engine selects.
//
// I_SetPalette takes a bare `byte*` and reads 256 RGB triples from it; its
// declaration in i_video.h carries no length, and its callers hand it
// W_CacheLumpName("PLAYPAL") with no check. ST_doPaletteStuff goes further and
// offsets into the same lump by palette index, up to RADIATIONPAL. A PWAD
// carrying a short PLAYPAL therefore reads off the end of its zone block.
//
// d_main.c cannot be unit tested -- the gate wants a real WAD, the zone and the
// lump directory -- so the decision it makes lives in palette_bounds.h and is
// tested here, exactly as wad_bounds.h and level_bounds.h are.
#include <cstdio>

#include "../palette_bounds.h"
#include "check_util.h"

int main()
{
    // Both shipped IWADs carry 14 palettes: measured, doom.wad and doom2.wad
    // both hold a 10752-byte PLAYPAL.
    const int kReal = 10752;

    // --- The constants describe the real lump. ---
    check(PALETTE_BYTES == 768, "one palette is 256 RGB triples");
    check(PLAYPAL_MIN_BYTES == kReal,
	  "the required size matches the PLAYPAL both shipped IWADs carry");

    // --- Lumps that cover what the engine reads. ---
    check(PlayPalFits(kReal) != 0, "the shipped 14-palette PLAYPAL is accepted");
    check(PlayPalFits(kReal + 768) != 0,
	  "a PWAD carrying an EXTRA palette is accepted, not refused");
    check(PlayPalFits(kReal * 4) != 0, "a much longer PLAYPAL is accepted");

    // --- Lumps that do not. This is the over-read. ---
    check(PlayPalFits(kReal - 1) == 0,
	  "a PLAYPAL one byte short of the last palette is refused");
    check(PlayPalFits(768) == 0,
	  "a single-palette PLAYPAL is refused -- ST_doPaletteStuff indexes past it");
    check(PlayPalFits(0) == 0, "an empty PLAYPAL is refused");
    check(PlayPalFits(-1) == 0, "a negative length is refused");

    // --- Per-selection form: the indices ST_doPaletteStuff actually picks. ---
    // Palette 0 is the normal view; 1..8 are the pain reds; 9..12 the pickup
    // golds; 13 the radiation suit. All must be reachable in a real lump.
    check(PaletteIndexFits(0, kReal) != 0, "palette 0 (normal) is reachable");
    check(PaletteIndexFits(8, kReal) != 0, "the last pain red is reachable");
    check(PaletteIndexFits(12, kReal) != 0, "the last pickup gold is reachable");
    check(PaletteIndexFits(13, kReal) != 0, "RADIATIONPAL is reachable");

    // The index is bounded above independently of the lump, so a long lump does
    // not make an out-of-range index legal.
    check(PaletteIndexFits(PALETTE_COUNT, kReal * 4) == 0,
	  "an index past the last palette is refused even in an oversized lump");
    check(PaletteIndexFits(-1, kReal) == 0, "a negative palette index is refused");

    // And within range, the lump still has to be long enough to hold it. This is
    // the case the load-time gate exists to make impossible.
    check(PaletteIndexFits(13, 768) == 0,
	  "RADIATIONPAL is refused when the lump holds only palette 0");
    check(PaletteIndexFits(0, 768) != 0,
	  "palette 0 is still reachable in a single-palette lump");
    check(PaletteIndexFits(1, 1536) != 0,
	  "a palette ending exactly at the last byte is reachable");
    check(PaletteIndexFits(1, 1535) == 0,
	  "a palette one byte short of complete is refused");

    return check_summary("palette_bounds");
}
