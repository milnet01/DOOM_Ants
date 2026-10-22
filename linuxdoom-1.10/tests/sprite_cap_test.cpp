// sprite_cap_test.cpp — DOOM-0459: past the sprite cap, the nearest things are
// the ones drawn.
//
// Why this exists: RB_BuildSprites dropped the things that did not fit in the
// order it walked the sectors, so on a crowded map the traced view could leave
// out a monster beside the player and draw one across the map. Measured before
// the fix on E1M3 with the cap lowered to 8: a thing 101 units away was dropped
// while one 2752 units away was drawn.
//
// Contract:
//   1. After RB_SortSpritesNearest, the candidates run nearest first, so the
//      first `cap` of them are the `cap` nearest.
//   2. Equal distances keep the sector-walk order (`order`), so the kept set
//      is the same on every libc's qsort.
//   3. r_mesh.c calls RB_SortSpritesNearest (source scrape), so the order
//      tested here is the order the engine uses.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../sprite_cap.h"
#include "check_util.h"

namespace {

std::string slurp(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

int main()
{
    // ---- 1: nearest first ---------------------------------------------------
    {
        // Walk order puts the far things first, as a sector-index walk can.
        const float d[8] = { 2752.f, 1833.f, 900.f, 640.f, 400.f, 273.f, 101.f, 50.f };
        rb_sprite_cand_t c[8];
        for (int i = 0; i < 8; i++)
            c[i] = { d[i] * d[i], i, nullptr };
        RB_SortSpritesNearest(c, 8);
        bool ascending = true;
        for (int i = 1; i < 8; i++)
            if (c[i - 1].d2 > c[i].d2)
                ascending = false;
        check(ascending, "1: candidates run nearest first");
        check_eq_int(c[0].order, 7, "1: the nearest thing (50 units, walked last) comes first");
        check_eq_int(c[1].order, 6, "1: the 101-unit thing is kept under a cap of 2");
        check_eq_int(c[7].order, 0, "1: the farthest thing (2752 units, walked first) comes last");
    }

    // ---- 2: ties keep the walk order ----------------------------------------
    {
        rb_sprite_cand_t c[5] = {
            { 9.f, 0, nullptr }, { 4.f, 1, nullptr }, { 9.f, 2, nullptr },
            { 4.f, 3, nullptr }, { 9.f, 4, nullptr },
        };
        RB_SortSpritesNearest(c, 5);
        const int want[5] = { 1, 3, 0, 2, 4 };
        bool same = true;
        for (int i = 0; i < 5; i++)
            if (c[i].order != want[i])
                same = false;
        check(same, "2: equal distances keep the sector-walk order");
    }

    // ---- 3: the engine uses this order (source scrape) -----------------------
    {
        const std::string src = slurp(DOOM_TESTS_ROOT "/r_mesh.c");
        check(src.find("RB_SortSpritesNearest(") != std::string::npos,
              "3: r_mesh.c orders the sprite candidates nearest first");
    }

    return check_summary("sprite_cap_test");
}
