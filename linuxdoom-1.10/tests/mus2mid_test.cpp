// Regression test for the DOOM-0093 mus2mid bounds fix (indie-review 2026-07-23),
// plus the happy-path conversion contract (test audit 2026-07-26).
//
// mus2mid() converts an in-WAD MUS lump to MIDI. It used to bound its reads on the
// MUS header's own declared score length, never the real lump size, so a crafted /
// truncated PWAD music lump that inflated scorestart+scorelength read far past the
// W_CacheLumpNum buffer. The fix threads the real lump length in and bounds every
// read by it (and requires the full 8-byte header be present).
//
// Part 1 asserts the SAFETY contract: a lump that is too short, or whose header
// over-declares its score, is rejected (non-zero return, *mid_out left NULL)
// instead of reading out of bounds.
//
// Part 2 asserts the CONVERSION contract on hand-built minimal scores, byte for
// byte. This was a gap the 2026-07-26 test audit found: every case here used to be
// a rejection case, and the file's comment claimed the happy path was "exercised by
// the headless game smoke-run on a real WAD" -- but that smoke run only proves the
// game boots. A wrong controller_map index, a broken percussion-channel skip, a
// regressed DMX 0x80 clamp or a bad track-length back-patch all leave the game
// booting happily while the music is wrong, so none of them would be caught.
//
// Single-TU include like the sibling tests; mus2mid.c only pulls <stdlib.h>/<string.h>.
#include "check_util.h"

#include <cstdio>
#include <cstdlib>
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

// Run one rejection case: the call must fail AND leave the out-params cleared.
// `midi` starts poisoned so "left untouched" cannot masquerade as "set to NULL".
static void reject_case(const unsigned char* lump, size_t len, const char* what)
{
    unsigned char* midi = (unsigned char*)0x1;
    int            midilen = -1;
    int            rc = mus2mid(lump, len, &midi, &midilen);
    check(rc != 0, what);
    check(midi == NULL && midilen == 0, "  ...and *mid_out/*mid_len are cleared (no OOB read)");
    if (rc == 0) free(midi);
}

// Convert `score` (appended to a generated header) and compare the produced MIDI
// with `want` byte for byte.
static void convert_case(const unsigned char* score, size_t scorelen,
                         const unsigned char* want, size_t wantlen, const char* what)
{
    unsigned char lump[128];
    if (8 + scorelen > sizeof lump) { check(false, "test bug: score too big for lump buffer"); return; }
    put_header(lump, (unsigned short)scorelen, 8);
    memcpy(lump + 8, score, scorelen);

    unsigned char* midi = (unsigned char*)0x1;
    int            midilen = -1;
    int            rc = mus2mid(lump, 8 + scorelen, &midi, &midilen);
    check(rc == 0, what);
    if (rc != 0) return;

    check_eq_int(midilen, (long)wantlen, "  ...converted MIDI has the expected length");
    if (midilen == (int)wantlen)
    {
        int firstBad = -1;
        for (size_t i = 0; i < wantlen; i++)
            if (midi[i] != want[i]) { firstBad = (int)i; break; }
        if (firstBad >= 0)
            std::printf("  FAIL: %s -- first differing byte at %d (got 0x%02X, want 0x%02X)\n",
                        what, firstBad, midi[firstBad], want[firstBad]);
        check(firstBad < 0, "  ...converted MIDI matches the expected bytes exactly");
    }
    free(midi);
}

// The 22-byte MIDI type-0 header mus2mid always emits. The last four bytes are the
// track length, back-patched once the track is complete -- so they double as the
// assertion that the back-patch happened.
#define MIDI_HEADER(tracklen) \
    'M','T','h','d', 0x00,0x00,0x00,0x06, 0x00,0x00, 0x00,0x01, 0x00,0x46, \
    'M','T','r','k', 0x00,0x00,0x00,(unsigned char)(tracklen)

int main()
{
    // ---- Part 1: the DOOM-0093 safety contract ---------------------------------

    // 1. A lump shorter than the 8-byte header is rejected without touching it.
    {
        unsigned char buf[4] = { 'M', 'U', 'S', 0x1a };
        reject_case(buf, sizeof buf, "lump shorter than the 8-byte header is rejected");
    }

    // 2. Header over-declares the score (scorelength = 0xFFFF) but the real lump is
    //    just the 8-byte header. Pre-fix this read ~64 KB past the buffer; now the
    //    score is clamped to the real length, the first event read fails.
    {
        unsigned char buf[8];
        put_header(buf, 0xFFFF, 8);
        reject_case(buf, sizeof buf, "header over-declaring scorelength is rejected");
    }

    // 3. scorestart points past the real lump end -> first read is already out of
    //    range -> rejected, no OOB.
    {
        unsigned char buf[16];
        memset(buf, 0, sizeof buf);
        put_header(buf, 100, 200);   // scorestart 200 >> real length 16
        reject_case(buf, sizeof buf, "scorestart past the real lump end is rejected");
    }

    // 4. A non-MUS lump of adequate length is still rejected on the magic check.
    {
        unsigned char buf[32];
        memset(buf, 0xAB, sizeof buf);
        reject_case(buf, sizeof buf, "lump failing the MUS magic check is rejected");
    }

    // ---- Part 2: the conversion contract ---------------------------------------

    // A. press key 60 on MUS channel 0, release it, end score. Each event carries
    //    the 0x80 "last of this group" bit and is followed by a zero time delay.
    //    Expected: note-on at default velocity 127, note-off, end-of-track -- each
    //    preceded by a zero VLQ delta, and a track length of 12 back-patched in.
    {
        const unsigned char score[] = {
            0x90, 60, 0x00,     // presskey ch0 key60 (no velocity byte) + delay 0
            0x80, 60, 0x00,     // releasekey ch0 key60               + delay 0
            0x60                // scoreend
        };
        const unsigned char want[] = {
            MIDI_HEADER(12),
            0x00, 0x90, 60, 127,    // delta 0, note-on  ch0 key60 vel127
            0x00, 0x80, 60, 0x00,   // delta 0, note-off ch0 key60
            0x00, 0xFF, 0x2F, 0x00  // delta 0, end of track
        };
        convert_case(score, sizeof score, want, sizeof want,
                     "press/release/end converts to the expected MIDI");
    }

    // B. MUS channel 15 is percussion and must map to MIDI channel 9 regardless of
    //    allocation order; controller 3 maps through controller_map to MIDI 0x07
    //    (volume); and a value with bit 7 set is clamped to 0x7F -- the DMX quirk
    //    vanilla DOOM had, deliberately preserved so converted music matches.
    {
        const unsigned char score[] = {
            0x4F, 0x03, 0xFF,   // changecontroller, MUS ch15, controller 3, value 255
            0x60                // scoreend
        };
        const unsigned char want[] = {
            MIDI_HEADER(8),
            0x00, 0xB9, 0x07, 0x7F, // delta 0, controller change ch9, ctrl 0x07, 255->0x7F
            0x00, 0xFF, 0x2F, 0x00
        };
        convert_case(score, sizeof score, want, sizeof want,
                     "percussion channel maps to MIDI 9 and the DMX 0x80 clamp holds");
    }

    // C. MIDI channel allocation skips channel 9 (reserved for percussion). Nine
    //    MUS channels take MIDI 0..8; the tenth must land on 10, not 9. Getting
    //    this wrong sends a melodic voice to the drum channel -- audible, but the
    //    game still boots, so only a unit test catches it.
    {
        unsigned char score[64];
        size_t p = 0;
        for (unsigned char ch = 0; ch <= 8; ch++)
        { score[p++] = (unsigned char)(0x90 | ch); score[p++] = 60; score[p++] = 0x00; }
        score[p++] = (unsigned char)(0x90 | 10); score[p++] = 60; score[p++] = 0x00;
        score[p++] = 0x60;

        unsigned char lump[128];
        put_header(lump, (unsigned short)p, 8);
        memcpy(lump + 8, score, p);

        unsigned char* midi = NULL;
        int            midilen = 0;
        int rc = mus2mid(lump, 8 + p, &midi, &midilen);
        check(rc == 0, "ten-channel score converts");
        if (rc == 0)
        {
            // Every event here is a zero delta plus a 3-byte note-on, so event i's
            // status byte sits at 22 + 4*i + 1.
            const int wantChan[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10 };
            bool ok = (midilen == 22 + 10 * 4 + 4);
            for (int i = 0; i < 10 && ok; i++)
                if (midi[22 + 4 * i + 1] != (unsigned char)(0x90 | wantChan[i])) ok = false;
            check(ok, "channel allocation fills MIDI 0..8 then skips 9 (percussion) to 10");
            free(midi);
        }
    }

    return check_summary("mus2mid");
}
