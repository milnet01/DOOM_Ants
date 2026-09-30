// render_bounds_test.cpp — DOOM-0382: the software renderer's two size
// decisions, held against the boundary cases that were wrong.
//
// Both defects were off-by-a-ratio rather than off-by-a-line, which is why
// neither showed up in review for twenty-eight years: the code reads correctly
// and only the numbers are wrong. So the cases below are mostly about the exact
// boundary, and about the historical value being insufficient.
//
// r_plane.c cannot be unit tested -- R_MapPlane wants a view, a visplane and a
// loaded map -- so the decisions it makes live in render_bounds.h and are
// tested here, exactly as level_bounds.h, wad_bounds.h, save_bounds.h and
// net_bounds.h are.
//
// It also holds DOOM-0364's RenderEyeZ cases (labels begin "DOOM-0364").
#include <cstdio>
#include <climits>

#include "../render_bounds.h"
#include "check_util.h"

int main()
{
    // ---- R_MapPlane's span guard ----
    //
    // The bug: the row test was `y > viewheight`, so y == viewheight passed.
    // Valid rows are 0..viewheight-1, so that is one row past the end --
    // indexing cachedheight[] (sized SCREENHEIGHT) out of bounds when the view
    // is full height, and ylookup[] below the view otherwise.
    const int kW = 320;
    const int kH = 200;

    check(RenderSpanValid(0, kW - 1, 0, kW, kH) != 0,
          "the full top row is a valid span");
    check(RenderSpanValid(0, kW - 1, kH - 1, kW, kH) != 0,
          "the full bottom row is a valid span");
    check(RenderSpanValid(7, 7, 5, kW, kH) != 0,
          "a single-column span is valid");

    // The regression this fix is for. It must stay red-adjacent: if the guard
    // is ever loosened back to `>`, this is the case that catches it.
    check(RenderSpanValid(0, kW - 1, kH, kW, kH) == 0,
          "a row exactly at viewheight is refused, not admitted");
    check(RenderSpanValid(0, kW - 1, kH + 1, kW, kH) == 0,
          "a row past viewheight is refused");
    check(RenderSpanValid(0, kW - 1, -1, kW, kH) == 0,
          "a negative row is refused");

    // The column half, which was already correct -- kept so a later edit to the
    // shared helper cannot quietly drop it.
    check(RenderSpanValid(0, kW, 5, kW, kH) == 0,
          "a column exactly at viewwidth is refused");
    check(RenderSpanValid(-1, 10, 5, kW, kH) == 0,
          "a negative start column is refused");
    check(RenderSpanValid(10, 9, 5, kW, kH) == 0,
          "an inverted span is refused");

    // A zero-size view has no valid row or column at all. This is the degenerate
    // case a `>` test gets wrong in the other direction: y == 0 == viewheight.
    check(RenderSpanValid(0, 0, 0, 0, 0) == 0,
          "no span is valid in a zero-size view");

    // ---- openings[] sizing ----
    //
    // The worst case is exact: R_StoreWallRange is the only writer, claims at
    // most RENDER_OPENINGS_SPANS_PER_SEG spans of up to viewwidth shorts, and
    // returns early once ds_p reaches &drawsegs[MAXDRAWSEGS].
    check(RENDER_OPENINGS_SPANS_PER_SEG == 3,
          "three spans per seg: masked-texture column, sprite top, sprite bottom");

    check_eq_int(RENDER_OPENINGS_NEEDED(256, 320), 245760,
                 "vanilla's own 320-wide worst case");
    check_eq_int(RENDER_OPENINGS_NEEDED(256, 1280), 983040,
                 "this fork's 1280-wide worst case");

    // The historical defect, stated as a test so the reasoning cannot be lost:
    // vanilla sized openings[] at SCREENWIDTH*64, which is twelve times too
    // small for its own worst case -- and DOOM-0147 carried that ratio forward
    // against the enlarged MAXWIDTH rather than re-deriving it.
    check(320 * 64 < RENDER_OPENINGS_NEEDED(256, 320),
          "vanilla's 64x ratio was already too small at 320 wide");
    check(1280 * 64 < RENDER_OPENINGS_NEEDED(256, 1280),
          "the same ratio is still too small at 1280 wide");

    // The sizing must scale with both inputs, or a future change to either
    // constant reopens the overflow silently -- which is exactly how this one
    // survived DOOM-0147.
    check(RENDER_OPENINGS_NEEDED(256, 1280) > RENDER_OPENINGS_NEEDED(256, 640),
          "a wider view needs more openings");
    check(RENDER_OPENINGS_NEEDED(512, 1280) > RENDER_OPENINGS_NEEDED(256, 1280),
          "more drawsegs need more openings");

    // ---- DOOM-0364: RenderEyeZ(viewz, ceilingz) ----
    //
    // Why this exists: P_CalcHeight's airborne branch clamps viewz below the
    // ceiling and then overwrites the result (id's 1997 code, left alone so Classic
    // stays identical), so the camera could pass up through a low ceiling. The
    // Solid/Ultra camera takes min(viewz, ceilingz - 4 map units) instead.
    // Heights are fixed point, FRACUNIT = 65536.
    {
        const int F = 65536;
        check_eq_int(RenderEyeZ(41 * F, 128 * F), 41 * F,
                     "DOOM-0364: an eye well below the ceiling is unchanged");
        check_eq_int(RenderEyeZ(124 * F, 128 * F), 124 * F,
                     "DOOM-0364: an eye exactly 4 units below the ceiling is unchanged");
        check_eq_int(RenderEyeZ(125 * F, 128 * F), 124 * F,
                     "DOOM-0364: an eye 3 units below the ceiling is clamped to 4 below");
        check_eq_int(RenderEyeZ(128 * F, 128 * F), 124 * F,
                     "DOOM-0364: an eye at the ceiling is clamped to 4 below");
        check_eq_int(RenderEyeZ(130 * F, 128 * F), 124 * F,
                     "DOOM-0364: an eye above the ceiling is clamped to 4 below");
        check_eq_int(RenderEyeZ(-40 * F, -48 * F), -52 * F,
                     "DOOM-0364: negative heights clamp too (ceiling -48, eye -40 gives -52)");
        check_eq_int(RenderEyeZ(-60 * F, -48 * F), -60 * F,
                     "DOOM-0364: a negative eye already 12 below a negative ceiling is unchanged");

        // The subtraction must not overflow. A ceiling of exactly INT_MIN + 4 units
        // is the lowest one the subtraction can take; anything lower is outside
        // DOOM's map range and is deliberately not tested.
        const int lowCeil = INT_MIN + 4 * F;
        check_eq_int(RenderEyeZ(0, lowCeil), INT_MIN,
                     "DOOM-0364: a ceiling at INT_MIN + 4 units clamps to INT_MIN without overflow");
        check_eq_int(RenderEyeZ(INT_MAX, lowCeil), INT_MIN,
                     "DOOM-0364: INT_MAX eye under that ceiling clamps to INT_MIN without overflow");
    }

    return check_summary("render_bounds");
}
