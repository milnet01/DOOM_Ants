// step_wall_test.cpp — DOOM-0500: step walls follow a plane that moves past
// its neighbour's.
//
// Why this exists: the 3D mesh built a lower step only on the side whose floor
// was lower when the level loaded, and an upper step only on the side whose
// ceiling was higher. When the barons die on E1M8, sector 30 (floor 208) sinks
// to -136, below its neighbours at -96, 0 and 8. Its own steps were never built,
// so Solid showed sector 31's red floor from underneath and the sky beyond it.
//
// Contract:
//   1  RB_LowerStepTop is the other side's floor, never below this side's.
//      RB_UpperStepBottom is the other side's ceiling, never above this side's.
//   2  r_mesh.c builds both steps on every two-sided seg with the far edge on
//      the clamped plane tags, and RB_UpdateMeshHeights moves those tags with
//      the two helpers above. (Source scrape: the moving case needs a level.)
//
// Heights are fixed_t passed as int, so the numbers below are map units << 16.
#include <cstdio>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../step_wall.h"
#include "check_util.h"

namespace {

int fx(int mapUnits) { return mapUnits << 16; }

// Whole file as text, for the r_mesh.c scrape. A missing file is a failure.
std::string slurp(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("  FAIL: cannot open %s\n", path); g_failures++; return std::string(); }
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

} // namespace

int main()
{
    // ---- 1: the clamp, on E1M8's sector 30 against sector 31 ---------------
    {
        // Load: sector 30 stands at 208, sector 31's floor is 0. Seen from 30's
        // side the lower step is hidden, so it has zero height.
        check_eq_int(RB_LowerStepTop(fx(208), fx(0)), fx(208),
                     "1: own floor above the other's -> the step's top is the own floor (zero height)");
        // After the barons die: 30 is at -136, so 30's step rises to 31's floor.
        check_eq_int(RB_LowerStepTop(fx(-136), fx(0)), fx(0),
                     "1: own floor below the other's -> the step's top is the other floor");
        check_eq_int(RB_LowerStepTop(fx(8), fx(8)), fx(8),
                     "1: equal floors -> zero height at that floor");

        check_eq_int(RB_UpperStepBottom(fx(208), fx(176)), fx(176),
                     "1: own ceiling above the other's -> the step's bottom is the other ceiling");
        check_eq_int(RB_UpperStepBottom(fx(0), fx(176)), fx(0),
                     "1: own ceiling below the other's -> the step's bottom is the own ceiling (zero height)");
    }

    // ---- 2: r_mesh.c builds and moves both sides (source scrape) -----------
    {
        const std::string src = slurp(DOOM_TESTS_ROOT "/r_mesh.c");
        check(src.find("bi, RB_PLANE_FLOOR_STEP") != std::string::npos,
              "2: r_mesh.c tags a lower step's top edge with RB_PLANE_FLOOR_STEP on the back sector");
        check(src.find("bi, RB_PLANE_CEIL_STEP") != std::string::npos,
              "2: r_mesh.c tags an upper step's bottom edge with RB_PLANE_CEIL_STEP on the back sector");
        check(src.find("RB_LowerStepTop(") != std::string::npos,
              "2: r_mesh.c places a lower step's top edge with RB_LowerStepTop");
        check(src.find("RB_UpperStepBottom(") != std::string::npos,
              "2: r_mesh.c places an upper step's bottom edge with RB_UpperStepBottom");
    }

    return check_summary("step_wall_test");
}
