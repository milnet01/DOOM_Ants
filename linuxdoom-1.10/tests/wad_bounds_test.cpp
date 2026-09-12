// wad_bounds_test.cpp — DOOM-0384: a lump may not claim to reach outside its WAD.
//
// DOOM-0093 bounded the lump directory's extent against the real file size; every
// filepos and size INSIDE that directory was still stored raw. A crafted PWAD
// could therefore declare a lump of any size (W_CacheLumpNum's Z_Malloc aborts
// the game) or at a negative offset (W_ReadLump's lseek fails silently and read()
// takes bytes from wherever the descriptor already was).
//
// w_wad.c cannot be unit tested — W_AddFile wants a file descriptor, the zone and
// the real lumpinfo table — so the decision it makes per lump lives in
// wad_bounds.h and is tested here, exactly as save_bounds.h is.
#include <cstdio>
#include <climits>

#include "../wad_bounds.h"
#include "check_util.h"

int main()
{
    const long kFile = 1024;   // a 1 KiB WAD to measure claims against

    // --- Lumps that genuinely fit. ---
    check(WadLumpFits(0, kFile, kFile) != 0, "a lump spanning the whole file fits");
    check(WadLumpFits(0, 0, kFile) != 0, "a marker lump at the start fits");
    check(WadLumpFits(kFile, 0, kFile) != 0, "a marker lump at the very end fits");
    check(WadLumpFits(1000, 24, kFile) != 0, "a lump ending exactly at the last byte fits");
    check(WadLumpFits(512, 100, kFile) != 0, "an ordinary interior lump fits");

    // Marker lumps are empty by design -- MAP01, S_START, F_END -- so rejecting a
    // zero size would refuse every real WAD. This is the case a stricter check
    // gets wrong.
    check(WadLumpFits(300, 0, kFile) != 0, "a zero-size marker lump is legal");

    // --- Lumps that do not. ---
    check(WadLumpFits(1000, 25, kFile) == 0, "a lump ending one byte past the file is refused");
    check(WadLumpFits(0, kFile + 1, kFile) == 0, "a lump longer than the whole file is refused");
    check(WadLumpFits(kFile + 1, 0, kFile) == 0, "a lump starting past the end is refused");
    check(WadLumpFits(-1, 16, kFile) == 0, "a negative offset is refused");
    check(WadLumpFits(0, -1, kFile) == 0, "a negative size is refused");
    check(WadLumpFits(-4096, 8192, kFile) == 0,
          "a negative offset is refused even where offset + size would land inside");

    // The DoS the roadmap named: a lump declaring ~112 MB inside a small WAD,
    // which reached Z_Malloc and aborted the game.
    check(WadLumpFits(12, 0x7000000, kFile) == 0, "a lump declaring a huge size is refused");

    // --- The overflow a naive `pos + size <= filelen` check gets wrong. ---
    // Both operands are positive and their sum wraps negative, so the naive form
    // reports this as fitting. Subtracting cannot wrap.
    check(WadLumpFits(LONG_MAX, LONG_MAX, kFile) == 0,
          "a pair whose sum would wrap is refused, not admitted");
    check(WadLumpFits(1, LONG_MAX, kFile) == 0, "a size near LONG_MAX is refused");

    // --- Degenerate files. ---
    check(WadLumpFits(0, 0, 0) != 0, "an empty lump at 0 fits an empty file");
    check(WadLumpFits(0, 1, 0) == 0, "no non-empty lump fits an empty file");
    check(WadLumpFits(0, 0, -1) == 0, "a negative file length admits nothing");

    // --- DOOM-0402: a lump's own declared entry count. ---
    // The two real shapes. PNAMES: a 4-byte count then 8-byte names. TEXTURE1:
    // a 4-byte count then 4-byte directory offsets. Named here rather than
    // included, so a change to either is a deliberate edit to this test.
    const int kPnamesHdr = 4, kPnamesEntry = 8;
    const int kTexHdr = 4, kTexEntry = 4;

    check(WadCountFitsLump(0, 4, kPnamesHdr, kPnamesEntry) != 0,
          "a lump holding only its count may declare no entries");
    check(WadCountFitsLump(1, 12, kPnamesHdr, kPnamesEntry) != 0,
          "a lump with room for one name may declare one");
    check(WadCountFitsLump(2, 20, kPnamesHdr, kPnamesEntry) != 0,
          "a lump may declare exactly what it holds");
    check(WadCountFitsLump(2, 25, kPnamesHdr, kPnamesEntry) != 0,
          "trailing bytes past the last entry do not invalidate the lump");

    check(WadCountFitsLump(3, 20, kPnamesHdr, kPnamesEntry) == 0,
          "a lump claiming one more name than it holds is refused");
    check(WadCountFitsLump(1, 11, kPnamesHdr, kPnamesEntry) == 0,
          "an entry one byte short of complete is refused");
    check(WadCountFitsLump(0, 3, kPnamesHdr, kPnamesEntry) == 0,
          "a lump too short to hold its own count is refused");
    check(WadCountFitsLump(-1, 100, kPnamesHdr, kPnamesEntry) == 0,
          "a negative count is refused");

    // The fixture's case: TEXTURE1 declaring 100000 textures in a lump with
    // room for nine offsets. The directory walk reads each entry before any
    // per-entry check can judge it.
    check(WadCountFitsLump(100000, 40, kTexHdr, kTexEntry) == 0,
          "a texture count far past the lump is refused");
    check(WadCountFitsLump(9, 40, kTexHdr, kTexEntry) != 0,
          "the count the same lump does hold is accepted");

    // The overflow a naive `hdr + count*entry <= lumplen` check gets wrong:
    // multiplying a WAD-supplied count wraps, and the wrapped product compares
    // as small. Dividing what is left after the header cannot.
    check(WadCountFitsLump(INT_MAX, 40, kTexHdr, kTexEntry) == 0,
          "a count near INT_MAX is refused, not wrapped into acceptance");
    check(WadCountFitsLump(0x20000001, 40, kTexHdr, kTexEntry) == 0,
          "a count whose byte total overflows int is refused");

    // --- Degenerate shapes. ---
    check(WadCountFitsLump(1, 100, kTexHdr, 0) == 0, "a zero entry size is refused");
    check(WadCountFitsLump(1, 100, -1, kTexEntry) == 0,
          "a negative header size is refused");

    return check_summary("wad_bounds_test");
}
