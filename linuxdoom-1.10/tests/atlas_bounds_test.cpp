// atlas_bounds_test.cpp — security review F-A: how large an atlas tile, and how
// tall an atlas, a WAD may ask for.
//
// Why this exists: r_mesh.c tile_size() clamped a tile's width to ATLAS_WIDTH
// but its height only to >= 1, so a texture or sprite header declaring a height
// of up to 32767 made an image taller than any device allows (an invalid
// vkCreateImage). RB_BuildAtlas then summed shelf heights in an int with no
// ceiling: about 33 tiles 2048 wide and 32767 tall made (oy + y) * dstw overflow
// int in blit_tile and write before the atlas buffer, after a multi-gigabyte
// calloc. A WAD is untrusted input (docs/standards/security.md).
//
// The decisions live in atlas_bounds.h so the boundary cases can be held with
// no WAD and no GPU, as with patch_bounds.h. That r_mesh.c actually calls them
// is held by tests/bounds_wiring_test.cpp.
#include <climits>
#include <cstdio>

#include "../atlas_bounds.h"
#include "check_util.h"

static void clamp_case(int w, int h, int wantw, int wanth, const char* what)
{
    int gw = w, gh = h;
    AtlasClampTile(&gw, &gh);
    if (gw != wantw || gh != wanth)
    {
        std::printf("  FAIL: %s (in %dx%d, got %dx%d, want %dx%d)\n",
                    what, w, h, gw, gh, wantw, wanth);
        g_failures++;
    }
}

int main()
{
    // ---- INV-1: AtlasClampTile leaves w in [1, RB_ATLAS_WIDTH] and h in
    // [1, RB_ATLAS_MAX_TILE_H], and changes nothing already inside.
    clamp_case(64, 64, 64, 64, "INV-1: a flat-sized tile is untouched");
    clamp_case(256, 128, 256, 128, "INV-1: a stock texture is untouched");
    clamp_case(RB_ATLAS_WIDTH, RB_ATLAS_MAX_TILE_H,
               RB_ATLAS_WIDTH, RB_ATLAS_MAX_TILE_H,
               "INV-1: the largest allowed tile is untouched");
    clamp_case(RB_ATLAS_WIDTH + 1, RB_ATLAS_MAX_TILE_H + 1,
               RB_ATLAS_WIDTH, RB_ATLAS_MAX_TILE_H,
               "INV-1: one past each limit is cut back");
    clamp_case(1, 1, 1, 1, "INV-1: the smallest tile is untouched");

    // The F-A case: a header can declare a height up to 32767.
    clamp_case(64, 32767, 64, RB_ATLAS_MAX_TILE_H,
               "INV-1: a 32767-tall tile is cropped");
    clamp_case(3000, 32767, RB_ATLAS_WIDTH, RB_ATLAS_MAX_TILE_H,
               "INV-1: a too-wide, too-tall tile is cropped on both axes");
    clamp_case(32767, 1, RB_ATLAS_WIDTH, 1, "INV-1: a 32767-wide tile is cropped");

    // Nonsense in, valid out.
    clamp_case(0, 0, 1, 1, "INV-1: zero size becomes 1x1");
    clamp_case(-5, -9, 1, 1, "INV-1: negative size becomes 1x1");
    clamp_case(INT_MIN, INT_MIN, 1, 1, "INV-1: INT_MIN becomes 1x1");
    clamp_case(INT_MAX, INT_MAX, RB_ATLAS_WIDTH, RB_ATLAS_MAX_TILE_H,
               "INV-1: INT_MAX is cropped");
    clamp_case(0, 100, 1, 100, "INV-1: axes are clamped independently");

    // ---- INV-2: AtlasRowsFit is true exactly for 0 <= rows <= RB_ATLAS_MAX_ROWS.
    check(AtlasRowsFit(0) == 1, "INV-2: zero rows fit");
    check(AtlasRowsFit(1) == 1, "INV-2: one row fits");
    check(AtlasRowsFit(RB_ATLAS_MAX_ROWS) == 1, "INV-2: exactly the limit fits");
    check(AtlasRowsFit(RB_ATLAS_MAX_ROWS + 1LL) == 0, "INV-2: one row past the limit is refused");
    check(AtlasRowsFit(-1) == 0, "INV-2: negative rows are refused");
    check(AtlasRowsFit((long long)INT_MAX) == 0, "INV-2: INT_MAX rows are refused");
    check(AtlasRowsFit((long long)INT_MAX + 1) == 0, "INV-2: rows past int range are refused");
    check(AtlasRowsFit(LLONG_MAX) == 0, "INV-2: LLONG_MAX rows are refused");
    check(AtlasRowsFit(LLONG_MIN) == 0, "INV-2: LLONG_MIN rows are refused");

    // ---- INV-3: the constants are consistent with each other and with int.
    // At RB_ATLAS_MAX_ROWS rows, (rows * RB_ATLAS_WIDTH) must fit a signed
    // 32-bit int with room for one more tile of the largest height. This is
    // the product blit_tile forms as (oy + y) * dstw. Derived from the
    // constants, not from remembered numbers.
    {
        const long long widest = RB_ATLAS_WIDTH;
        const long long atMax  = (long long)RB_ATLAS_MAX_ROWS * widest;
        const long long plusOne = ((long long)RB_ATLAS_MAX_ROWS + RB_ATLAS_MAX_TILE_H) * widest;
        if (plusOne > (long long)INT_MAX)
        {
            std::printf("  FAIL: INV-3: (MAX_ROWS + MAX_TILE_H) * WIDTH = %lld exceeds INT_MAX %d "
                        "(MAX_ROWS*WIDTH = %lld)\n", plusOne, INT_MAX, atMax);
            g_failures++;
        }
        check(RB_ATLAS_MAX_ROWS >= RB_ATLAS_MAX_TILE_H,
              "INV-3: the row ceiling holds at least one tile of the largest height");
        check(RB_ATLAS_MAX_TILE_H >= 1 && RB_ATLAS_WIDTH >= 1,
              "INV-3: the tile limits are positive");
    }

    // ---- INV-4: a run of maximum tiles is refused before the int product can
    // overflow. The F-A shape: ~33 tiles of 32767 rows each is 1.08M rows, and
    // 1.08M * 2048 overflows int. Simulate the packer's running total in the
    // wide type and require the guard to trip before that product passes INT_MAX.
    {
        long long rows = 0;
        int refusedAt = -1;
        for (int tile = 0; tile < 100000; tile++)
        {
            int w = RB_ATLAS_WIDTH, h = 32767;
            AtlasClampTile(&w, &h);
            rows += h;                       // one full-width tile per shelf
            if (!AtlasRowsFit(rows)) { refusedAt = tile; break; }
            if (rows * RB_ATLAS_WIDTH > (long long)INT_MAX)
            {
                std::printf("  FAIL: INV-4: after %d full-width tiles rows=%lld, "
                            "rows*width=%lld overflows int, yet AtlasRowsFit accepted it\n",
                            tile + 1, rows, rows * RB_ATLAS_WIDTH);
                g_failures++;
                break;
            }
        }
        check(refusedAt >= 0, "INV-4: a run of maximum-height tiles is eventually refused");
    }

    return check_summary("atlas_bounds");
}
