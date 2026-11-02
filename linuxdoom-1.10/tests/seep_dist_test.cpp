// seep_dist_test.cpp — DOOM-0536: a seep cell's distance to an opening.
//
// Why this exists: RB_BuildSeepField measured a cell's distance to the MIDDLE of each
// opening, so air beside a long roof edge read as far from outdoors everywhere but the
// edge's middle. At E1M4 a cell one column from open sky read 205 units, and the fog
// stopped at the roof line in a bright straight wall.
//
// Contract:
//   1. Beside a long opening, the distance is the gap to it, wherever along it you are.
//   2. Past an end, the distance is to that end.
//   3. A zero-length opening is one point, and divides by nothing.
//   4. RB_BuildSeepField rasterises through this function (source scrape: it needs the
//      engine).
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../seep_dist.h"
#include "check_util.h"

namespace {

bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

}  // namespace

int main()
{
    // An opening 512 units long, on the y axis.
    const float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 512.0f;

    // 1.
    check(near(RB_SeepPointSegDist(64.0f, 256.0f, ax, ay, bx, by), 64.0f),
          "beside the opening's middle, the distance is the gap");
    check(near(RB_SeepPointSegDist(64.0f, 16.0f, ax, ay, bx, by), 64.0f),
          "beside the opening near one end, the distance is still the gap");
    check(near(RB_SeepPointSegDist(64.0f, 496.0f, bx, by, ax, ay), 64.0f),
          "the opening's direction does not change the answer");

    // 2.
    check(near(RB_SeepPointSegDist(30.0f, -40.0f, ax, ay, bx, by), 50.0f),
          "before the first end, the distance is to that end");
    check(near(RB_SeepPointSegDist(30.0f, 552.0f, ax, ay, bx, by), 50.0f),
          "past the last end, the distance is to that end");

    // 3.
    check(near(RB_SeepPointSegDist(3.0f, 4.0f, 0.0f, 0.0f, 0.0f, 0.0f), 5.0f),
          "a zero-length opening is one point");

    // 4.
    std::ifstream in(std::string(DOOM_TESTS_ROOT) + "/r_mesh.c");
    std::stringstream src;
    src << in.rdbuf();
    check(src.str().find("RB_SeepPointSegDist(cx, cy,") != std::string::npos,
          "RB_BuildSeepField measures each cell through RB_SeepPointSegDist");

    return check_summary("seep_dist_test");
}
