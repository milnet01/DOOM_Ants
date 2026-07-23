// Regression test for the DOOM-0093 mus2mid bounds fix (indie-review 2026-07-23).
//
// mus2mid() converts an in-WAD MUS lump to MIDI. It used to bound its reads on the
// MUS header's own declared score length, never the real lump size, so a crafted /
// truncated PWAD music lump that inflated scorestart+scorelength read far past the
// W_CacheLumpNum buffer. The fix threads the real lump length in and bounds every
// read by it (and requires the full 8-byte header be present).
//
// These cases assert the SAFETY contract: a lump that is too short, or whose header
// over-declares its score, is rejected (non-zero return, *mid_out left NULL) instead
// of reading out of bounds. The happy path (real MUS -> MIDI) is exercised by the
// headless game smoke-run on a real WAD.
//
// Single-TU include like the sibling tests; mus2mid.c only pulls <stdlib.h>/<string.h>.
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../mus2mid.c"

// Build an 8-byte MUS header: magic, scorelength, scorestart (the only header bytes
// mus2mid reads before jumping to the score).
static void put_header(unsigned char* b, unsigned short scorelength, unsigned short scorestart)
{
    b[0] = 'M'; b[1] = 'U'; b[2] = 'S'; b[3] = 0x1a;
    b[4] = (unsigned char)(scorelength & 0xff);
    b[5] = (unsigned char)(scorelength >> 8);
    b[6] = (unsigned char)(scorestart & 0xff);
    b[7] = (unsigned char)(scorestart >> 8);
}

int main()
{
    unsigned char* midi = (unsigned char*)0x1;   // poison: must be reset to NULL on failure
    int            midilen = -1;

    // 1. A lump shorter than the 8-byte header is rejected without touching it.
    {
        unsigned char buf[4] = { 'M', 'U', 'S', 0x1a };
        assert(mus2mid(buf, 4, &midi, &midilen) != 0);
        assert(midi == NULL && midilen == 0);
    }

    // 2. Header over-declares the score (scorelength = 0xFFFF) but the real lump is
    //    just the 8-byte header. Pre-fix this read ~64 KB past the buffer; now the
    //    score is clamped to the real length, the first event read fails, return != 0.
    {
        unsigned char buf[8];
        put_header(buf, 0xFFFF, 8);
        midi = (unsigned char*)0x1; midilen = -1;
        assert(mus2mid(buf, sizeof buf, &midi, &midilen) != 0);
        assert(midi == NULL && midilen == 0);
    }

    // 3. scorestart points past the real lump end -> first read is already out of
    //    range -> rejected, no OOB.
    {
        unsigned char buf[16];
        memset(buf, 0, sizeof buf);
        put_header(buf, 100, 200);   // scorestart 200 >> real length 16
        midi = (unsigned char*)0x1; midilen = -1;
        assert(mus2mid(buf, sizeof buf, &midi, &midilen) != 0);
        assert(midi == NULL && midilen == 0);
    }

    // 4. A non-MUS lump of adequate length is still rejected on the magic check.
    {
        unsigned char buf[32];
        memset(buf, 0xAB, sizeof buf);
        midi = (unsigned char*)0x1; midilen = -1;
        assert(mus2mid(buf, sizeof buf, &midi, &midilen) != 0);
        assert(midi == NULL && midilen == 0);
    }

    printf("mus2mid: all passed\n");
    return 0;
}
