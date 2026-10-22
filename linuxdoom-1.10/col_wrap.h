// col_wrap.h — DOOM-0481: which texel row the classic column drawers read.
//
// id's R_DrawColumn read dc_source[(frac>>FRACBITS)&127]. The mask assumes
// every column is 128 rows tall, so a shorter wall column was read past its
// end: into the next composite column, or past the patch's post in the lump.
// A wall or sky column now repeats at its texture's real height, the way
// Solid and Ultra already tile it. For a power-of-two height this is exactly
// id's mask, so 128-row textures and masked posts draw as before.
//
// Factored out of r_draw.c so tests/col_wrap_test.cpp can hold the
// arithmetic with no WAD or video (mirrors sprite_cap.h).
#ifndef COL_WRAP_H
#define COL_WRAP_H

#include <stdint.h>

// f reduced into [0,hf). hf is the column height in 16.16 fixed point.
static inline unsigned R_WrapFrac(int64_t f, unsigned hf)
{
    f %= hf;
    return (unsigned)(f < 0 ? f + hf : f);
}

// One pixel down the column. frac and step are both already in [0,hf), so one
// subtraction keeps frac there; unsigned, because 2*hf can pass INT_MAX.
static inline unsigned R_StepFrac(unsigned frac, unsigned step, unsigned hf)
{
    frac += step;
    return frac >= hf ? frac - hf : frac;
}

#endif
