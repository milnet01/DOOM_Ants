// palette_tint.h — DOOM-0379: Classic's palette flashes, as a colour blend.
//
// Classic shows damage red, the pickup flash and the radiation suit by swapping
// the whole palette for one of PLAYPAL's later palettes (ST_doPaletteStuff ->
// I_SetPalette). The 3D tiers draw in full colour, so a palette swap reaches
// nothing there. But every one of those palettes is palette 0 scaled and shifted
// per channel: measured against doom.wad's PLAYPAL, a straight-line fit per
// channel reproduces palettes 1-13 to within one colour step. So the same flash
// is one blend over the finished frame:  out = scale * frame + bias.
//
// The fit is least squares over the 256 entries, per channel. Plain C, no DOOM
// headers: r_backend.c computes it and tests/palette_tint_test.cpp checks it.
#ifndef PALETTE_TINT_H
#define PALETTE_TINT_H

#include <string.h>

typedef struct
{
    int   active;     // 0: pal is palette 0, draw nothing
    float scale[3];   // per channel, multiplies the frame
    float bias[3];    // per channel, in [0,1] display units, added after
} rb_tint_t;

// base and pal are 256 RGB triples (768 bytes): PLAYPAL palette 0 and the
// palette the engine just set.
static inline void RB_FitPaletteTint(const unsigned char* base,
                                     const unsigned char* pal, rb_tint_t* out)
{
    int c, i;

    out->active = memcmp(base, pal, 768) != 0;
    for (c = 0; c < 3; c++)
    {
        // Integer sums, so an unchanged channel fits exactly 1 and 0.
        long long sx = 0, sy = 0, sxx = 0, sxy = 0;
        double den, s, b;
        for (i = 0; i < 256; i++)
        {
            long long x = base[3 * i + c], y = pal[3 * i + c];
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        den = 256.0 * (double)sxx - (double)sx * (double)sx;
        s   = den != 0.0 ? (256.0 * (double)sxy - (double)sx * (double)sy) / den : 1.0;
        b   = ((double)sy - s * (double)sx) / 256.0 / 255.0;
        if (s < 0.0) s = 0.0;
        if (b < 0.0) b = 0.0;
        if (b > 1.0) b = 1.0;
        out->scale[c] = (float)s;
        out->bias[c]  = (float)b;
    }
}

#endif
