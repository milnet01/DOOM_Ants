// light_grid_test.cpp — DOOM-0652: the rasterised view's shared light grid.
//
// Why this exists: the raster view kept each subsector's nearest 16 lights and a falloff
// that never reached zero, so a light on one subsector's list and not its neighbour's
// stopped in a straight line across the floor. The grid replaces both.
//
// Contract (spec docs/specs/DOOM-0652-shared-light-grid.md):
//   INV-2  Every light kind's falloff is zero at and beyond its range, with zero slope.
//   INV-3  A cell lists every light whose range reaches it, unless it overflowed, and
//          each overflow is counted. Holds for the static lists and for the moving
//          lights added on top of them each frame.
// INV-4 and INV-6 join this file with B2 and B3.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU, no display.
#include <cmath>
#include <cstdio>
#include <vector>

#include "../light_grid.h"
#include "check_util.h"

using namespace lgrid;

namespace {

// Every light whose range reaches cell c's box, in light order: the brute-force answer.
std::vector<uint32_t> brute(const Grid& g, const std::vector<Light2D>& L, size_t c)
{
    std::vector<uint32_t> out;
    const int ix = (int)(c % (size_t)g.w), iy = (int)(c / (size_t)g.w);
    for (size_t i = 0; i < L.size(); i++)
        if (L[i].range > 0.0f && BoxDist(g, ix, iy, L[i].x, L[i].y) < L[i].range)
            out.push_back((uint32_t)i);
    return out;
}

std::vector<uint32_t> list(const std::vector<uint32_t>& cells, const std::vector<uint32_t>& idx, size_t c)
{
    return std::vector<uint32_t>(idx.begin() + cells[2 * c], idx.begin() + cells[2 * c] + cells[2 * c + 1]);
}

}  // namespace

int main()
{
    // INV-2, per light kind: a wall or flat light (reach 0), a lamp or fireball (reach
    // below zero: a room light with a pool), and a pickup (its reach is its range).
    struct Kind { const char* name; float peak; float reach; };
    const Kind kinds[] = {
        { "wall or flat light", 1.0f,    0.0f },
        { "bright panel",       9.0f,    0.0f },   // range capped at RASTER_LIGHT_MAXRANGE
        { "lamp",               1.0f, -160.0f },
        { "fireball",           0.4f, -160.0f },
        { "pickup",             1.0f,   96.0f },
    };
    for (const Kind& k : kinds)
    {
        const float R = k.reach > 0.0f ? k.reach : RasterLightRange(k.peak);
        char what[160];
        std::snprintf(what, sizeof what, "%s: has a range (%.1f)", k.name, R);
        check(R > 0.0f && R <= RASTER_LIGHT_MAXRANGE, what);
        std::snprintf(what, sizeof what, "%s: zero at its range", k.name);
        check(RasterFalloff(R, R, k.reach) == 0.0f, what);
        std::snprintf(what, sizeof what, "%s: zero one unit past its range", k.name);
        check(RasterFalloff(R + 1.0f, R, k.reach) == 0.0f, what);
        // Zero slope: the step across R +- 0.5 is second order, against a first-order
        // step of peak * slope that a hard cut-off or a linear fade would show.
        const float step = std::fabs(RasterFalloff(R - 0.5f, R, k.reach) - RasterFalloff(R + 0.5f, R, k.reach));
        std::snprintf(what, sizeof what, "%s: no step across its range (%.2g)", k.name, step);
        check(step < 1e-4f, what);
        std::snprintf(what, sizeof what, "%s: still lit at half its range", k.name);
        check(RasterFalloff(0.5f * R, R, k.reach) > 0.0f, what);
    }
    check(RasterLightRange(RASTER_LIGHT_CUTOFF) == 0.0f, "a light at the cutoff is not listed");
    check(RasterLightRange(100.0f) == RASTER_LIGHT_MAXRANGE, "a bright light's range is capped");

    // INV-3, static lists. A 12 x 10 grid of 64-unit cells; lights of mixed range, then
    // cap + 1 short lights on one cell's centre so that cell alone overflows.
    Grid g;
    g.ox = 32.0f; g.oy = 32.0f; g.cell = 64.0f; g.w = 12; g.h = 10;
    const uint32_t cap = 6;
    std::vector<Light2D> L = {
        { 100.0f, 100.0f, 150.0f }, { 500.0f, 300.0f, 90.0f }, { 700.0f, 600.0f, 300.0f },
        { 64.0f,  600.0f, 40.0f },  { 300.0f, 300.0f, 0.0f },   // range 0: never listed
        { 400.0f, 100.0f, 70.0f },
    };
    const float hx = g.ox + 9 * g.cell, hy = g.oy + 2 * g.cell;   // cell (9, 2)'s centre
    for (uint32_t i = 0; i <= cap; i++)
        L.push_back(Light2D{ hx + (float)i, hy, 20.0f + (float)i });   // all inside the cell
    std::vector<uint32_t> rows, sidx;
    const ListStats st = BuildCellLists(g, L.data(), (int)L.size(), cap, rows, sidx);
    const size_t hot = 2 * (size_t)g.w + 9;
    bool allMatch = true;
    for (size_t c = 0; c < (size_t)g.w * g.h; c++)
        if (c != hot && list(rows, sidx, c) != brute(g, L, c))
        {
            std::printf("  cell %zu differs from the brute-force set\n", c);
            allMatch = false;
        }
    check(allMatch, "every cell but the full one lists exactly the lights that reach it");
    check(st.overflow == 1, "one cell overflowed, and it is counted");
    check(list(rows, sidx, hot).size() == cap, "the full cell keeps cap lights");
    check(st.worst == cap + 1, "the worst list is reported before the cap");
    // The full cell drops the light whose window is smallest at the cell: all seven sit
    // inside it (distance 0, window 1), so the tie keeps light order and drops the last.
    {
        std::vector<uint32_t> want = brute(g, L, hot);
        want.pop_back();
        check(list(rows, sidx, hot) == want, "the full cell drops the lowest-ranked light");
    }
    check(list(rows, sidx, 0).size() == 1 && list(rows, sidx, 0)[0] == 0,
          "a corner cell lists the one light that reaches it");

    // INV-3, the moving lights added each frame. They fill each cell's room under the cap.
    std::vector<Light2D> D = {
        { 100.0f, 100.0f, 50.0f },   // over cell (1,1), which has room
        { hx, hy, 10.0f },           // over the full cell: no room
    };
    std::vector<uint32_t> cells, idx, touched;
    const ListStats ds = BuildDynLists(g, D.data(), (int)D.size(), rows, cap, cells, idx, touched);
    // A veto from keep() removes a light from a cell it reaches.
    {
        std::vector<uint32_t> vc, vi;
        BuildCellLists(g, L.data(), (int)L.size(), cap, vc, vi,
                       [](size_t c, uint32_t i) { return !(c == 0 && i == 0); });
        check(list(vc, vi, 0).empty(), "a vetoed light is left out of that cell");
        check(list(vc, vi, 1) == list(rows, sidx, 1), "and stays in the cells not vetoed");
    }
    bool dynMatch = true;
    for (size_t c = 0; c < (size_t)g.w * g.h; c++)
    {
        const std::vector<uint32_t> got(idx.begin() + cells[2 * c], idx.begin() + cells[2 * c] + cells[2 * c + 1]);
        const std::vector<uint32_t> want = c == hot ? std::vector<uint32_t>() : brute(g, D, c);
        if (got != want)
        {
            std::printf("  moving lights: cell %zu differs\n", c);
            dynMatch = false;
        }
    }
    check(dynMatch, "every cell lists the moving lights that reach it, the full cell none");
    check(ds.overflow == 1, "the full cell's refused moving light is counted");
    ClearDynLists(cells, touched);
    bool clear = true;
    for (uint32_t v : cells) clear = clear && v == 0;
    check(clear, "clearing the touched cells leaves the moving-light table zero");

    return check_summary("light_grid_test");
}
