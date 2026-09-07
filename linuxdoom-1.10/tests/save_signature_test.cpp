// save_signature_test.cpp — DOOM-0426: a savegame names the layout it was
// written in, so a build that lays those structs out differently refuses it
// instead of reading the bytes as its own.
//
// NOTE for anyone extending this: do NOT assert the engine's real sizeof()s
// here. This file compiles as C++, and doomtype.h makes `boolean` a 1-byte
// `bool` under C++ where the engine's C build gets a 4-byte enum -- measured
// 2026-09-07, sizeof(player_t) is 320 in the C build and 272 here. That
// divergence is harmless (the signature is computed inside the C engine on both
// the save and the load path, so it is self-consistent) but it means a C++ test
// cannot speak for the engine's layout. Everything below tests the two
// functions as pure arithmetic, which is all they are.
#include <cstdio>
#include <cstring>

#include "../save_signature.h"
#include "check_util.h"

int main()
{
    // The layout this build actually had when the item was fixed, kept as a
    // worked example rather than as a requirement -- it is here so the shape of
    // a real call is visible.
    const unsigned kMobj = 224, kPlayer = 320, kSector = 128, kPtr = 8;

    // ---- SaveLayoutId ----
    check(SaveLayoutId(kMobj, kPlayer, kSector, kPtr)
              == SaveLayoutId(kMobj, kPlayer, kSector, kPtr),
          "the same layout always gives the same id");

    // Each input must matter on its own, or a struct could change size without
    // the signature moving -- which is the whole failure being fixed.
    check(SaveLayoutId(kMobj + 1, kPlayer, kSector, kPtr)
              != SaveLayoutId(kMobj, kPlayer, kSector, kPtr),
          "a change in mobj_t's size changes the id");
    check(SaveLayoutId(kMobj, kPlayer + 1, kSector, kPtr)
              != SaveLayoutId(kMobj, kPlayer, kSector, kPtr),
          "a change in player_t's size changes the id");
    check(SaveLayoutId(kMobj, kPlayer, kSector + 1, kPtr)
              != SaveLayoutId(kMobj, kPlayer, kSector, kPtr),
          "a change in sector_t's size changes the id");

    // The case the item names explicitly: a 32-bit build against a 64-bit one.
    check(SaveLayoutId(kMobj, kPlayer, kSector, 4)
              != SaveLayoutId(kMobj, kPlayer, kSector, 8),
          "a 32-bit build and a 64-bit build get different ids");

    // No cancellation. A byte gained by one struct and lost by another must not
    // produce the same id -- a plain sum or xor would let exactly that through,
    // and it is a realistic shape for a field moving between two structs.
    check(SaveLayoutId(kMobj + 1, kPlayer - 1, kSector, kPtr)
              != SaveLayoutId(kMobj, kPlayer, kSector, kPtr),
          "a byte moved from player_t to mobj_t still changes the id");

    // Position matters: two structs swapping sizes is a different layout.
    check(SaveLayoutId(kPlayer, kMobj, kSector, kPtr)
              != SaveLayoutId(kMobj, kPlayer, kSector, kPtr),
          "swapping two struct sizes changes the id");

    // The C-versus-C++ divergence found while fixing this is itself a worked
    // example of the thing being detected, so it is pinned as one.
    check(SaveLayoutId(224, 320, 128, 8) != SaveLayoutId(224, 272, 128, 8),
          "the C and C++ views of player_t give different ids");

    // ---- SaveFormatSignature ----
    {
        char sig[SAVE_SIGNATURE_SIZE];
        char again[SAVE_SIGNATURE_SIZE];
        char other[SAVE_SIGNATURE_SIZE];
        int  i;

        std::memset(sig, 0x7f, sizeof(sig));
        SaveFormatSignature(sig, kMobj, kPlayer, kSector, kPtr);

        check(std::strncmp(sig, "layout ", 7) == 0,
              "the field is tagged, so a human reading a .dsg can see what it is");

        // It must fit the fixed-width field with room for its terminator, or it
        // would run into the byte after it in the header.
        check(std::strlen(sig) == SAVE_SIGNATURE_SIZE - 1,
              "the field fills exactly its 16 bytes including the terminator");

        // The tail must be written, not left as whatever was on the stack --
        // this field goes into a file that gets shared.
        for (i = (int) std::strlen(sig); i < SAVE_SIGNATURE_SIZE; i++)
            check(sig[i] == 0, "every byte past the text is zeroed");

        SaveFormatSignature(again, kMobj, kPlayer, kSector, kPtr);
        check(std::memcmp(sig, again, SAVE_SIGNATURE_SIZE) == 0,
              "the same layout formats to the same bytes, so a save round-trips");

        SaveFormatSignature(other, kMobj, kPlayer, kSector, 4);
        check(std::memcmp(sig, other, SAVE_SIGNATURE_SIZE) != 0,
              "a different layout formats to different bytes, so it is refused");
    }

    // A save written before this field existed has skill/episode/map bytes at
    // this offset instead. Those are small integers and cannot spell "layout ",
    // so the same comparison refuses them -- which is what makes the old files
    // fail cleanly rather than needing a separate check.
    {
        char sig[SAVE_SIGNATURE_SIZE];
        const char legacy[SAVE_SIGNATURE_SIZE] =
            { 2, 1, 1, 1, 0, 0, 0, 0, 0, '/', 0, 0, 'H', 0x7f, 0x1f, 5 };

        SaveFormatSignature(sig, kMobj, kPlayer, kSector, kPtr);
        check(std::memcmp(sig, legacy, SAVE_SIGNATURE_SIZE) != 0,
              "a pre-signature save's header bytes cannot be mistaken for one");
    }

    return check_summary("save_signature");
}
