// seg_project_test.cpp — DOOM-0180: the bright diagonal seam in the 3D views.
//
// The numbers below are not invented. They are E1M1's own map data, read out of
// doom.wad: linedef 193 is the pit ledge in the roofed nukage room, the exact
// place the seam was photographed headlessly on 2026-08-04 (camera 3274,-3353 —
// 78 units away). Its two sides are split by the node builder at DIFFERENT
// points, and the back side's split vertex v442 is rounded a full 1.02 units off
// the linedef's true line. r_mesh.c builds the lower wall quad on the front
// side's line and clips the ledge's floor and ceiling caps to the back side's,
// so before the fix those two surfaces missed each other by that whole unit and
// left a hole the tracer saw sky through.
//
// So each case asserts BOTH halves: that the raw stored vertex really is off the
// line by the reported amount (the bug was real, and this test would notice if a
// future WAD-loading change silently moved the input), and that projecting it
// lands back on the line (the fix works). A test that only checked the second
// half would still pass if the first stopped being true and the fix became a
// no-op guarding nothing.
#include <cmath>
#include <cstdio>

#include "../seg_project.h"
#include "check_util.h"

namespace {

// Perpendicular distance from (px,py) to the infinite line through (ax,ay)
// along (dx,dy) — deliberately computed a different way from the projection
// under test, so a sign or algebra slip in one does not cancel in the other.
double dist_to_line(double ax, double ay, double dx, double dy,
                    double px, double py)
{
    double len = std::sqrt(dx * dx + dy * dy);
    return std::fabs(dx * (py - ay) - dy * (px - ax)) / len;
}

// One real linedef and one real seg endpoint on it.
struct Case {
    const char* what;
    double ax, ay, bx, by;   // the linedef's two (exact, integer) endpoints
    double px, py;           // a seg endpoint the node builder put on it
    double off;              // how far off the line it actually is, in map units
};

// E1M1. Every one of these is a DIAGONAL linedef — the 381 axis-aligned linedefs
// on this map split on exact integers and none of them is off by anything, which
// is why the reported seam was always diagonal.
const Case kCases[] = {
    { "E1M1 linedef 193 back-side split v442 (the photographed pit ledge)",
      3104, -3552, 3280, -3320,  3215, -3404,  1.0165 },
    { "E1M1 linedef 193 front-side split v435 (same linedef, other side)",
      3104, -3552, 3280, -3320,  3242, -3370,  0.0549 },
    { "E1M1 linedef 127 front-side split v431 (worst non-step offender)",
      64, -3648, -640, -3520,  -256, -3589,  0.8050 },
    { "E1M1 linedef 182 back-side split v443 (the room's second floor step)",
      2976, -3072, 2816, -3232,  2887, -3160,  0.7071 },
};

} // namespace

int main()
{
    for (const Case& c : kCases)
    {
        double dx = c.bx - c.ax, dy = c.by - c.ay;
        float  ox = 0.0f, oy = 0.0f;
        char   msg[256];

        // Half one: the stored vertex really is off its linedef's line.
        double before = dist_to_line(c.ax, c.ay, dx, dy, c.px, c.py);
        std::snprintf(msg, sizeof msg,
                      "%s sits %.4f units off the line (map data says %.4f)",
                      c.what, before, c.off);
        check(std::fabs(before - c.off) < 0.001, msg);

        // Half two: projecting puts it back on the line. The bar is 1/1000 of a
        // map unit — four orders below the ~1 unit hole, and far below one pixel
        // at any distance the seam was ever visible from.
        RB_ProjectOnLine(c.ax, c.ay, dx, dy, c.px, c.py, &ox, &oy);
        double after = dist_to_line(c.ax, c.ay, dx, dy, ox, oy);
        std::snprintf(msg, sizeof msg,
                      "%s projects back onto the line (%.6f units off)",
                      c.what, after);
        check(after < 0.001, msg);

        // ...without sliding ALONG the line, which would shorten the wall quad
        // and slide its texture. A projection moves a point by its perpendicular
        // offset and no more.
        double moved = std::sqrt((ox - c.px) * (ox - c.px) +
                                 (oy - c.py) * (oy - c.py));
        std::snprintf(msg, sizeof msg,
                      "%s moves only by its perpendicular offset (%.4f vs %.4f)",
                      c.what, moved, before);
        check(std::fabs(moved - before) < 0.001, msg);
    }

    // A degenerate linedef (zero length) must leave the point alone rather than
    // divide by zero and poison the whole mesh with NaN vertices.
    {
        float ox = -1.0f, oy = -1.0f;
        RB_ProjectOnLine(10.0, 20.0, 0.0, 0.0, 33.0, 44.0, &ox, &oy);
        check(ox == 33.0f && oy == 44.0f,
              "a zero-length linedef leaves the point where it was");
    }

    return check_summary("seg_project_test");
}
