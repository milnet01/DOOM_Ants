// rb_bc4.h — DOOM-0632: pack one 4x4 block of a single-channel map as BC4, for the baker.
//
// A BC4 block is two endpoint bytes and sixteen 3-bit choices. When the first endpoint is the
// larger, the choices are the two endpoints and six values spaced evenly between them; otherwise
// they are the endpoints, four values between, and exact 0 and 255. Header-only so a test
// reaches it; the game never includes it (the GPU decodes BC4).
#ifndef RB_BC4_H
#define RB_BC4_H

#include <stdint.h>
#include <string.h>

// The eight values a block's endpoints select, as the GPU derives them.
static inline void RB_Bc4Palette(int e0, int e1, int pal[8])
{
    int i;
    pal[0] = e0;
    pal[1] = e1;
    if (e0 > e1)
        for (i = 1; i < 7; i++) pal[i + 1] = ((7 - i) * e0 + i * e1) / 7;
    else
    {
        for (i = 1; i < 5; i++) pal[i + 1] = ((5 - i) * e0 + i * e1) / 5;
        pal[6] = 0;
        pal[7] = 255;
    }
}

// Squared error of the best choice per pixel; fills sel[16] when given.
static inline long RB_Bc4Fit(const uint8_t px[16], int e0, int e1, uint8_t* sel)
{
    int  pal[8], p, k;
    long total = 0;
    RB_Bc4Palette(e0, e1, pal);
    for (p = 0; p < 16; p++)
    {
        int best = 0, bestErr = 1 << 30;
        for (k = 0; k < 8; k++)
        {
            const int d = (int)px[p] - pal[k];
            if (d * d < bestErr) { bestErr = d * d; best = k; }
        }
        total += bestErr;
        if (sel) sel[p] = (uint8_t)best;
    }
    return total;
}

// Encodes px[16] (row by row) into out[8]. Searches endpoints near the block's range in both
// forms and keeps the lowest error; the first found wins a tie, so the result is deterministic.
static inline void RB_Bc4Encode(const uint8_t px[16], uint8_t out[8])
{
    enum { R = 2 };
    int  lo = 255, hi = 0, lo2 = 255, hi2 = 0, p, a, b, e0 = 0, e1 = 0;
    long bestErr = -1;
    uint8_t  sel[16];
    uint64_t bits = 0;

    for (p = 0; p < 16; p++)
    {
        const int v = px[p];
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        if (v > 0 && v < lo2) lo2 = v;      // the range with exact 0 and 255 set aside,
        if (v < 255 && v > hi2) hi2 = v;    // which the second form supplies for free
    }
    // First form: e0 > e1.
    for (a = hi - R; a <= hi + R && bestErr != 0; a++)
        for (b = lo - R; b <= lo + R; b++)
        {
            long err;
            if (a > 255 || b < 0 || a <= b) continue;
            err = RB_Bc4Fit(px, a, b, NULL);
            if (bestErr < 0 || err < bestErr) { bestErr = err; e0 = a; e1 = b; }
        }
    // Second form: e0 <= e1. Worth trying only when a pixel is exactly 0 or 255, or the block
    // is flat (where the first form has no legal endpoints).
    if (lo == hi || lo == 0 || hi == 255)
    {
        if (lo2 > hi2) lo2 = hi2 = lo;      // every pixel is 0 or 255
        for (a = lo2 - R; a <= lo2 + R && bestErr != 0; a++)
            for (b = hi2 - R; b <= hi2 + R; b++)
            {
                long err;
                if (a < 0 || b > 255 || a > b) continue;
                err = RB_Bc4Fit(px, a, b, NULL);
                if (bestErr < 0 || err < bestErr) { bestErr = err; e0 = a; e1 = b; }
            }
    }
    RB_Bc4Fit(px, e0, e1, sel);
    for (p = 0; p < 16; p++) bits |= (uint64_t)sel[p] << (3 * p);
    out[0] = (uint8_t)e0;
    out[1] = (uint8_t)e1;
    for (p = 0; p < 6; p++) out[2 + p] = (uint8_t)(bits >> (8 * p));
}

// The values a block decodes to, for the test.
static inline void RB_Bc4Decode(const uint8_t in[8], uint8_t px[16])
{
    int      pal[8], p;
    uint64_t bits = 0;
    RB_Bc4Palette(in[0], in[1], pal);
    for (p = 0; p < 6; p++) bits |= (uint64_t)in[2 + p] << (8 * p);
    for (p = 0; p < 16; p++) px[p] = (uint8_t)pal[bits >> (3 * p) & 7];
}

#endif
