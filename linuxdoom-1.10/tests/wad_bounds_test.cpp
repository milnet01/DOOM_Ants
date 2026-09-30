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
//
// It also holds DOOM-0432 INV-4: WadLumpOfUser, which turns a zone block's `user`
// back into a lump index. Those labels begin "DOOM-0432 INV-4".
#include <cstdio>
#include <climits>
#include <cstdint>
#include <cstring>

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

    // --- DOOM-0432 INV-4: WadLumpOfUser(user, cache, numlumps, ptr). ---
    //
    // Why this exists: W_PatchOk finds a patch's lump through the zone block's
    // `user` field. A user outside lumpcache[] (the unowned-block sentinel 2, or
    // a pointer off a slot boundary) or a slot holding a different pointer must
    // not be mistaken for a lump, or a stale/foreign pointer gets a "good" verdict.
    //
    // backing[] is bigger than the cache: the cache is the interior [2..5], so
    // "below" and "one past the end" are addresses we own.
    {
        int   obj[6];
        void* backing[8];
        for (int i = 0; i < 6; i++) backing[1 + i] = &obj[i];
        backing[0] = &obj[5];
        backing[7] = &obj[5];
        // cache slots: backing[2..5] -> obj[1..4]; backing[1] -> obj[0]; backing[6] -> obj[5]
        void* const* cache = &backing[2];
        const int    n     = 4;
        void* const* const first = &backing[2];
        void* const* const last  = &backing[5];

        check_eq_int(WadLumpOfUser(first, cache, n, &obj[1]), 0,
                     "DOOM-0432 INV-4: the first slot with a matching pointer gives lump 0");
        check_eq_int(WadLumpOfUser(&backing[3], cache, n, &obj[2]), 1,
                     "DOOM-0432 INV-4: the second slot with a matching pointer gives lump 1");
        check_eq_int(WadLumpOfUser(last, cache, n, &obj[4]), 3,
                     "DOOM-0432 INV-4: the last slot with a matching pointer gives lump 3");

        check_eq_int(WadLumpOfUser(&backing[1], cache, n, &obj[0]), -1,
                     "DOOM-0432 INV-4: a user just below the array is refused");
        check_eq_int(WadLumpOfUser(&backing[6], cache, n, &obj[5]), -1,
                     "DOOM-0432 INV-4: a user one slot past the end is refused");
        check_eq_int(WadLumpOfUser((void* const*)((const char*)first + 1), cache, n, &obj[1]), -1,
                     "DOOM-0432 INV-4: a user one byte into a slot is refused");
        check_eq_int(WadLumpOfUser((void* const*)(uintptr_t)2, cache, n, &obj[1]), -1,
                     "DOOM-0432 INV-4: the unowned-block sentinel (void*)2 is refused");
        check_eq_int(WadLumpOfUser((void* const*)(uintptr_t)2, cache, n, (void*)(uintptr_t)2), -1,
                     "DOOM-0432 INV-4: the sentinel is refused even when ptr is the sentinel too");
        check_eq_int(WadLumpOfUser(nullptr, cache, n, &obj[1]), -1,
                     "DOOM-0432 INV-4: a NULL user is refused");
        check_eq_int(WadLumpOfUser(&backing[3], cache, n, &obj[1]), -1,
                     "DOOM-0432 INV-4: a real slot holding a different pointer is refused");
        check_eq_int(WadLumpOfUser(first, cache, n, nullptr), -1,
                     "DOOM-0432 INV-4: a NULL pointer against a non-NULL slot is refused");
        check_eq_int(WadLumpOfUser(first, cache, 0, &obj[1]), -1,
                     "DOOM-0432 INV-4: numlumps 0 refuses every user");
        check_eq_int(WadLumpOfUser(&backing[3], cache, 1, &obj[2]), -1,
                     "DOOM-0432 INV-4: a slot beyond a smaller numlumps is refused");
    }

    // A user one byte into a slot must be refused on ALIGNMENT alone. Both slots
    // hold a pointer whose bytes are all 0x41, so a misaligned read through the
    // user (last 7 bytes of slot 0 + first byte of slot 1) yields the same value
    // as ptr; only an alignment test can refuse it. The fake value is never
    // dereferenced.
    {
        void* v;
        std::memset(&v, 0x41, sizeof v);
        void* slots[3] = { v, v, v };
        void* const* cache = &slots[0];
        check_eq_int(WadLumpOfUser((void* const*)((const char*)&slots[0] + 1), cache, 2, v), -1,
                     "DOOM-0432 INV-4: a user one byte into a slot is refused by alignment even when the misaligned bytes equal ptr");
        check_eq_int(WadLumpOfUser(&slots[1], cache, 2, v), 1,
                     "DOOM-0432 INV-4: control: the aligned second slot holding the same value gives lump 1");
    }

    return check_summary("wad_bounds_test");
}
