// render_bounds.h — DOOM-0382: the two size decisions the software renderer's
// span and opening arrays rest on.
//
// Both are vanilla overflows that this fork made larger without noticing, so
// both are about a ratio the original chose for a 320-wide screen and nobody
// re-derived when the screen grew.
//
//   * openings[] holds the sprite-clip and masked-texture spans that
//     R_StoreWallRange (r_segs.c) claims. Vanilla sized it SCREENWIDTH*64 and
//     checked for the overflow only afterwards, in R_DrawPlanes -- by which
//     point the write has already gone past the array into floorclip and
//     ceilingclip, declared immediately after it. That ratio was never enough
//     even at 320 wide, and DOOM-0147 carried it forward against MAXWIDTH.
//
//   * R_MapPlane's span guard (DOOM-0055) admitted y == viewheight, which is
//     one row past the last valid one -- indexing cachedheight[] past its end
//     when the view is full height, and ylookup[] below the view otherwise.
//
// The decisions are factored out here, rather than left inline in r_plane.c, so
// tests/render_bounds_test.cpp can hold the boundary cases against them with no
// WAD, no window and no renderer state -- the same reason level_bounds.h,
// wad_bounds.h, save_bounds.h and net_bounds.h exist.
#ifndef RENDER_BOUNDS_H
#define RENDER_BOUNDS_H

// How many spans of up to viewwidth shorts one R_StoreWallRange call can claim
// from openings[]: the masked-texture column table, the sprite top clip and the
// sprite bottom clip. Each is written at most once per call.
#define RENDER_OPENINGS_SPANS_PER_SEG	3

// The exact worst case for openings[], in shorts.
//
// R_StoreWallRange is the only writer, and it returns early once ds_p reaches
// &drawsegs[MAXDRAWSEGS] -- before any of the three writes. So no frame can
// claim more than maxdrawsegs whole seg's worth, and sizing the array at this
// figure puts the overflow out of reach rather than merely reporting it.
//
// Vanilla's SCREENWIDTH*64 is what this replaces: at 320 wide that is 20,480
// shorts against a 245,760-short worst case, a factor of twelve short.
#define RENDER_OPENINGS_NEEDED(maxdrawsegs, maxwidth) \
	((maxdrawsegs) * RENDER_OPENINGS_SPANS_PER_SEG * (maxwidth))

// Is this a span R_MapPlane may draw?
//
// Valid columns are 0..viewwidth-1 and valid rows are 0..viewheight-1, so the
// row test is >=, not >. The guard exists because the visplane span open/close
// logic above it is a known open bug (DOOM-0055), which is exactly why it must
// not assume any of these values is unreachable by construction.
static int RenderSpanValid (int x1, int x2, int y, int viewwidth, int viewheight)
{
    if (x1 > x2)
	return 0;

    if (x1 < 0 || x2 >= viewwidth)
	return 0;

    if (y < 0 || y >= viewheight)
	return 0;

    return 1;
}

#endif // RENDER_BOUNDS_H
