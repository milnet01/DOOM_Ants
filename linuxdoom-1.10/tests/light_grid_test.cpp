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
//   INV-4  A wall blocks a lamp's light along the wall's own line (the polar wall map),
//          and the cell cull built on the map never drops a light that reaches the cell.
//   INV-6  A probe on the far side of a wall adds nothing to a point (the lattice blend).
//   INV-9  The same for a wall at any angle (B5's per-probe polar map), and a pillar beside
//          the sight line hides nothing.
//   INV-10 A two-sided line blocks a probe whose height its opening does not hold.
//
// DOOM-0653 INV-6 (docs/specs/DOOM-0653-cell-light-pick.md): the traced view's NEE cell
// lists hold at most the cap, each entry above 0 and sorted by cluster id, and a cell over
// the cap keeps its largest estimates.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU, no display.
#include <algorithm>
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

    // INV-4: a wall blocks a light along the wall's own line. A light at the origin, a
    // one-sided wall at x = 100 from y = 50 down to y = -50, whose front (its right) faces
    // the light.
    {
        const std::vector<Wall2D> W = { { 100.0f, 50.0f, 100.0f, -50.0f, LW_ONESIDED } };
        const uint32_t all[1] = { 0 };
        std::vector<float> m(RASTER_POLAR_BINS);
        BuildPolarMap(0.0f, 0.0f, 400.0f, W.data(), all, 1, m.data());
        check(PolarLit(m.data(), 150.0f, 0.0f) == 0.0f, "a point behind the wall reads blocked");
        check(PolarLit(m.data(), 150.0f, 40.0f) == 0.0f, "so does one behind it near its end");
        check(PolarLit(m.data(), 90.0f, 120.0f) == 1.0f,
              "a point as far away, beside the wall's end, reads open");
        check(PolarLit(m.data(), 60.0f, 0.0f) == 1.0f, "a point before the wall reads open");
        // The wall's own lit face, tested RASTER_POLAR_NBIAS off it as mesh.frag does.
        bool faceLit = true;
        for (float y = -45.0f; y <= 45.0f; y += 1.0f)
            faceLit = faceLit && PolarLit(m.data(), 100.0f - RASTER_POLAR_NBIAS, y) == 1.0f;
        check(faceLit, "the wall's own face is not shadowed by the wall");

        // Seen from behind, a one-sided wall is the inside of solid map and blocks nothing.
        BuildPolarMap(200.0f, 0.0f, 400.0f, W.data(), all, 1, m.data());
        check(PolarLit(m.data(), -150.0f, 0.0f) == 1.0f, "a one-sided wall does not block its back");

        // A shut door blocks from both sides; an open one from neither.
        std::vector<Wall2D> D2 = W;
        D2[0].kind = LW_CLOSED;
        BuildPolarMap(200.0f, 0.0f, 400.0f, D2.data(), all, 1, m.data());
        check(PolarLit(m.data(), -150.0f, 0.0f) == 0.0f, "a shut door blocks from its back");
        D2[0].kind = LW_OPEN;
        BuildPolarMap(200.0f, 0.0f, 400.0f, D2.data(), all, 1, m.data());
        check(PolarLit(m.data(), -150.0f, 0.0f) == 1.0f, "an open door blocks nothing");
    }

    // The polar coordinate and its inverse agree all the way round.
    {
        bool round = true;
        for (int i = 0; i < 4096; i++)
        {
            const float p = (float)RASTER_POLAR_BINS * (float)i / 4096.0f;
            float dx, dy;
            PolarDir(p, dx, dy);
            round = round && std::fabs(PolarCoord(dx, dy) - p) < 1e-3f;
        }
        check(round, "PolarCoord inverts PolarDir");
    }

    // The cell cull never drops a light that reaches a point of the cell. A room of
    // random walls round a light; every point PolarLit lights must sit in a box
    // PolarBoxMaySee keeps. Deterministic LCG, so a failure repeats.
    {
        uint32_t seed = 12345u;
        auto rnd = [&seed](float lo, float hi) {
            seed = seed * 1664525u + 1013904223u;
            return lo + (hi - lo) * (float)(seed >> 8) / 16777216.0f;
        };
        bool kept = true, culledSome = false;
        for (int trial = 0; trial < 200 && kept; trial++)
        {
            std::vector<Wall2D> W;
            for (int k = 0; k < 12; k++)
            {
                const float x = rnd(-400.0f, 400.0f), y = rnd(-400.0f, 400.0f);
                W.push_back({ x, y, x + rnd(-150.0f, 150.0f), y + rnd(-150.0f, 150.0f),
                              k % 3 == 0 ? LW_CLOSED : LW_ONESIDED });
            }
            // A picket fence of shut doors with gaps narrower than a bin, so light reaches
            // a culled-looking cell only through the edge of its span.
            const float fx = rnd(100.0f, 250.0f), gap = rnd(1.0f, 6.0f);
            for (float y = -400.0f; y < 400.0f; y += 24.0f + gap)
                W.push_back({ fx, y, fx, y + 24.0f, LW_CLOSED });
            std::vector<uint32_t> ids(W.size());
            for (size_t k = 0; k < ids.size(); k++) ids[k] = (uint32_t)k;
            const float lx = rnd(-50.0f, 50.0f), ly = rnd(-50.0f, 50.0f), R = 450.0f;
            std::vector<float> m(RASTER_POLAR_BINS);
            BuildPolarMap(lx, ly, R, W.data(), ids.data(), (int)ids.size(), m.data());
            const float hc = 32.0f;
            for (float cy = -480.0f; cy <= 480.0f && kept; cy += 64.0f)
            for (float cx = -480.0f; cx <= 480.0f && kept; cx += 64.0f)
            {
                const bool may = PolarBoxMaySee(m.data(), lx, ly, cx, cy, hc);
                culledSome = culledSome || !may;
                if (may) continue;
                // mesh.frag tests a fragment up to RASTER_POLAR_NBIAS off its cell.
                const float ph = hc + RASTER_POLAR_NBIAS;
                for (float py = cy - ph; py <= cy + ph && kept; py += 1.0f)
                for (float px = cx - ph; px <= cx + ph && kept; px += 1.0f)
                    if (PolarLit(m.data(), px - lx, py - ly) > 0.0f)
                    {
                        std::printf("  trial %d: (%.0f, %.0f) is lit in a culled cell\n", trial, px, py);
                        kept = false;
                    }
            }
        }
        check(kept, "a culled cell holds no point the light reaches");
        check(culledSome, "and the cull does cull");
    }

    // INV-6 and INV-9: a probe with a wall between it and a point adds nothing to it. A
    // 2x2x2 lattice 64 units apart; corners the `bright` test picks hold bright SH, the
    // rest dim SH of their own. Each probe's polar map is drawn from `walls`, as BuildProbes
    // draws it, so these exercise the same map the engine stores.
    {
        const ProbeLattice L = { 0.0f, 0.0f, 0.0f, 64.0f, 64.0f, 2, 2, 2 };
        auto lattice = [&](const std::vector<Wall2D>& W, auto present, auto bright) {
            std::vector<float> buf(ProbeRecBase(L) + 8 * PROBE_REC, 0.0f);
            const float hdr[PROBE_HDR] = { L.ox, L.oy, L.oz, L.cell, L.dz, 2, 2, 2 };
            std::copy(hdr, hdr + PROBE_HDR, buf.begin());
            std::vector<uint32_t> ids(W.size());
            for (size_t i = 0; i < W.size(); i++) ids[i] = (uint32_t)i;
            const std::vector<float> lo(W.size(), 0.0f), hi(W.size(), 1000.0f);
            for (int k = 0; k < 8; k++)
            {
                const int ix = k & 1, iy = (k >> 1) & 1, iz = (k >> 2) & 1;
                buf[PROBE_HDR + k] = present(ix, iy) ? (float)k : -1.0f;
                const size_t r = ProbeRecBase(L) + (size_t)k * PROBE_REC;
                buf[r + 0] = ix * 64.0f; buf[r + 1] = iy * 64.0f; buf[r + 2] = iz * 64.0f;
                buf[r + 3] = 1.0f;
                for (int c = 0; c < 12; c++)
                    buf[r + 4 + c] = bright(ix, iy) ? 50.0f : 1.0f + 0.1f * (float)(k + c);
                for (int a = 0; a < 6; a++) buf[r + 16 + a] = 1000.0f;
                std::vector<float> m(RB_PROBE_POLAR_BINS);
                buf[r + PROBE_MAP] = -1.0f;
                if (BuildProbePolarMap(buf[r], buf[r + 1], buf[r + 2], RB_PROBE_POLAR_REACH * L.cell,
                                       W.data(), lo.data(), hi.data(), ids.data(), (int)W.size(), m.data()))
                {
                    buf[r + PROBE_MAP] = (float)buf.size();
                    buf.insert(buf.end(), m.begin(), m.end());
                }
            }
            return buf;
        };
        auto all   = [](int, int) { return true; };
        auto nearX = [](int ix, int) { return ix == 0; };
        auto farX  = [](int ix, int) { return ix == 1; };
        auto same  = [](const float* a, const float* b) {
            bool s = true;
            for (int c = 0; c < 12; c++) s = s && std::fabs(a[c] - b[c]) < 1e-5f;
            return s;
        };
        const float px = 40.0f, py = 20.0f, pz = 30.0f;
        const std::vector<Wall2D> none;
        // A shut line at x = 56 stands 8 units before the x = 64 corners.
        const std::vector<Wall2D> wallX = { { 56.0f, -200.0f, 56.0f, 300.0f, LW_CLOSED } };
        float shWall[12], shNear[12], shOpen[12];
        const bool any1 = ProbeBlendSH(lattice(wallX, all, farX).data(), px, py, pz, shWall);
        const bool any2 = ProbeBlendSH(lattice(none, nearX, farX).data(), px, py, pz, shNear);
        ProbeBlendSH(lattice(none, all, farX).data(), px, py, pz, shOpen);
        check(any1 && any2 && same(shWall, shNear),
              "INV-6: the blend beside a wall equals the near corners' blend alone");
        check(!same(shOpen, shNear), "INV-6: and with no wall the far corners do change it");

        // Every corner blocked (a second shut line at x = 20): blend them unmasked rather
        // than go black.
        std::vector<Wall2D> both = wallX;
        both.push_back({ 20.0f, -200.0f, 20.0f, 300.0f, LW_CLOSED });
        float shShort[12];
        const bool any3 = ProbeBlendSH(lattice(both, all, farX).data(), px, py, pz, shShort);
        check(any3 && shShort[0] > 0.0f, "INV-6: a point every corner is blocked from is not black");
        float shNone[12];
        check(!ProbeBlendSH(lattice(none, all, farX).data(), -500.0f, py, pz, shNone) && shNone[0] == 0.0f,
              "INV-6: a point with no corner present reads zero");

        // INV-9: a shut line along x + y = 120 hides the (64, 64) corners from the point,
        // though their axis reach to it is clear. A corner short of a 45-degree wall is the
        // case B3's six reach rays missed.
        auto diagBright = [](int ix, int iy) { return ix == 1 && iy == 1; };
        auto diagGone   = [](int ix, int iy) { return !(ix == 1 && iy == 1); };
        const std::vector<Wall2D> diag = { { 200.0f, -80.0f, -80.0f, 200.0f, LW_CLOSED } };
        float shDiag[12], shDiagNear[12], shDiagOpen[12];
        ProbeBlendSH(lattice(diag, all, diagBright).data(), px, py, pz, shDiag);
        ProbeBlendSH(lattice(none, diagGone, diagBright).data(), px, py, pz, shDiagNear);
        ProbeBlendSH(lattice(none, all, diagBright).data(), px, py, pz, shDiagOpen);
        check(same(shDiag, shDiagNear), "INV-9: a 45-degree wall hides the corner behind it");
        check(!same(shDiagOpen, shDiagNear), "INV-9: and with no wall that corner does change it");

        // INV-9: a small shut pillar 5 units beside the (64, 64) corner's sight line to the
        // point does not hide it (the diagonal reach rays tried at B4 did).
        const std::vector<Wall2D> pillar = {
            { 56.0f, 36.0f, 62.0f, 36.0f, LW_CLOSED }, { 62.0f, 36.0f, 62.0f, 42.0f, LW_CLOSED },
            { 62.0f, 42.0f, 56.0f, 42.0f, LW_CLOSED }, { 56.0f, 42.0f, 56.0f, 36.0f, LW_CLOSED } };
        float shPillar[12];
        ProbeBlendSH(lattice(pillar, all, diagBright).data(), px, py, pz, shPillar);
        check(same(shPillar, shDiagOpen), "INV-9: a pillar beside the sight line hides nothing");
    }

    // INV-9: a long straight wall split into short linedefs hides nothing in front of it. A
    // probe 20 units off a wall at y = 20 cut every 24 units sees every point 8 units in
    // front of the wall, out to the 2.5 cells (64 units each) a probe answers for: a joint
    // inside a bin must not stand in for the wall.
    {
        std::vector<Wall2D> W;
        for (float x = 200.0f; x > -200.0f; x -= 24.0f)
            W.push_back({ x - 24.0f, 20.0f, x, 20.0f, LW_ONESIDED });   // facing the probe
        std::vector<uint32_t> ids(W.size());
        for (size_t i = 0; i < W.size(); i++) ids[i] = (uint32_t)i;
        const std::vector<float> lo(W.size(), 0.0f), hi(W.size(), 0.0f);
        float m[RB_PROBE_POLAR_BINS];
        BuildProbePolarMap(0, 0, 0, RB_PROBE_POLAR_REACH * 64.0f, W.data(), lo.data(), hi.data(),
                           ids.data(), (int)ids.size(), m);
        bool seen = true;
        for (float x = -160.0f; x <= 160.0f; x += 1.0f)
            if (x * x + 144.0f <= 160.0f * 160.0f)
                seen = seen && ProbeSeesAcross(m, x, 12.0f);
        check(seen, "INV-9: a wall cut into short lines hides no point in front of it");
        check(!ProbeSeesAcross(m, 0.0f, 40.0f), "INV-9: and still hides the point behind it");
        // A relief recess (Ultra's sunk wall panels, up to 12 units deep) puts a lit point
        // behind the line. Seen at a slant its distance past the wall along the ray is many
        // times its depth; it must still be seen.
        check(ProbeSeesAcross(m, 60.0f, 30.0f), "INV-9: a point 10 units into a recess is seen at a slant");
        check(!ProbeSeesAcross(m, 60.0f, 44.0f), "INV-9: one 24 units behind the wall is not");
    }

    // INV-10: a two-sided line blocks a probe whose height its opening does not hold. A
    // ledge line at x = 50, open from z 64 to 200: a probe at z 32 sees nothing past it,
    // one at z 100 sees past it.
    {
        const std::vector<Wall2D> ledge = { { 50.0f, -200.0f, 50.0f, 200.0f, LW_OPEN } };
        const float lo[1] = { 64.0f }, hi[1] = { 200.0f };
        const uint32_t id[1] = { 0 };
        float below[RB_PROBE_POLAR_BINS], within[RB_PROBE_POLAR_BINS];
        const bool b1 = BuildProbePolarMap(0, 0, 32.0f, 192.0f, ledge.data(), lo, hi, id, 1, below);
        const bool b2 = BuildProbePolarMap(0, 0, 100.0f, 192.0f, ledge.data(), lo, hi, id, 1, within);
        check(b1 && !ProbeSeesAcross(below, 80.0f, 0.0f), "INV-10: a probe below a ledge does not see its top");
        check(ProbeSeesAcross(below, 30.0f, 0.0f), "INV-10: it still sees the floor before the ledge");
        check(!b2 && ProbeSeesAcross(within, 80.0f, 0.0f), "INV-10: a probe within the opening sees past it");
    }

    // Spec 4.5: placement. A layer inside a column's air stays; one less than a layer
    // outside moves in, so a room lower than dz still gets probes; a far one gets none.
    {
        check(ProbeLayerZ(32.0f, 64.0f, 0.0f, 128.0f) == 32.0f, "a layer inside the air stays put");
        check(ProbeLayerZ(-20.0f, 64.0f, 0.0f, 40.0f) == RB_PROBE_MARGIN, "a layer below a low room moves to its floor");
        check(ProbeLayerZ(90.0f, 64.0f, 0.0f, 40.0f) == 40.0f - RB_PROBE_MARGIN, "a layer above it moves to its ceiling");
        check(std::isnan(ProbeLayerZ(200.0f, 64.0f, 0.0f, 40.0f)), "a layer more than dz away gets none");
        check(std::isnan(ProbeLayerZ(0.0f, 64.0f, 72.0f, 72.0f)), "a shut door has no air");
        const ProbeLattice big = MakeProbeLattice(0, 0, 64, 4, 4, -1000.0f, 3000.0f);
        check(big.nz <= RB_PROBE_MAXLAYERS && big.oz + (big.nz - 0.5f) * big.dz >= 3000.0f,
              "the layers fit the cap and still span the map's height");
    }

    // DOOM-0653 INV-6: twelve clusters round a 4x4 grid of 64-unit cells, a cap of 5 so
    // most cells overflow, and a veto on one pair. Each cell is checked against a brute-
    // force ranking of the same estimates.
    {
        Grid g; g.ox = 0.0f; g.oy = 0.0f; g.cell = 64.0f; g.w = 4; g.h = 4;
        const int nc = 12;
        float cx[nc], cy[nc];
        std::vector<int32_t> ec;
        std::vector<float>   ew;
        for (int c = 0; c < nc; c++)
        {
            cx[c] = -100.0f + 37.0f * (float)c;
            cy[c] = 250.0f - 41.0f * (float)((c * 5) % nc);
            for (int k = 0; k <= c % 3; k++) { ec.push_back(c); ew.push_back(0.5f + (float)((c * 7 + k) % 5)); }
        }
        ec.push_back(-1); ew.push_back(9.0f);           // an emitter with no cluster
        ec.push_back(3);  ew.push_back(0.0f);           // a powerless member
        const uint32_t cap = 5;
        auto see = [](size_t cell, uint32_t c) { return !(cell == 5 && c == 4); };
        std::vector<uint32_t> nee;
        const ListStats st = BuildNeeCellLists(g, cx, cy, nc, ec.data(), ew.data(), (int)ec.size(),
                                               cap, nee, see);
        std::vector<double> P(nc, 0.0);
        for (size_t e = 0; e < ec.size(); e++) if (ec[e] >= 0) P[(size_t)ec[e]] += ew[e];

        bool allOk = true;
        uint32_t over = 0, worst = 0;
        for (int iy = 0; iy < g.h; iy++)
        for (int ix = 0; ix < g.w; ix++)
        {
            const size_t cell = (size_t)iy * g.w + ix;
            std::vector<std::pair<float, uint32_t>> want;
            uint32_t inRange = 0;              // before the cull, as the stats count
            for (int c = 0; c < nc; c++)
            {
                const float d = BoxDist(g, ix, iy, cx[c], cy[c]);
                const float E = (float)(P[(size_t)c] / std::max(d * d, NEE_NEAR * NEE_NEAR));
                if (!(d < NEE_LIST_RANGE && E > 0.0f)) continue;
                inRange++;
                if (see(cell, (uint32_t)c)) want.emplace_back(E, (uint32_t)c);
            }
            worst = std::max(worst, inRange);
            if (inRange > cap) over++;
            std::sort(want.begin(), want.end(), [](const std::pair<float, uint32_t>& a,
                                                   const std::pair<float, uint32_t>& b) {
                return a.first != b.first ? a.first > b.first : a.second < b.second;
            });
            if (want.size() > cap) want.resize(cap);
            std::vector<uint32_t> ids;
            for (const auto& wv : want) ids.push_back(wv.second);
            std::sort(ids.begin(), ids.end());

            uint32_t first;
            const uint32_t count = nee_cell_list(nee.data(), (int)cell, (int)ec.size(), &first);
            std::vector<uint32_t> got;
            const uint32_t* en = nee.data() + nee[NEE_HDR_ENTRY_AT];
            bool sorted = true, positive = true;
            for (uint32_t k = 0; k < count; k++)
            {
                got.push_back(en[(first + k) * NEE_ENTRY_WORDS]);
                if (k > 0 && got[k] <= got[k - 1]) sorted = false;
                if (!(nee_word_float(en[(first + k) * NEE_ENTRY_WORDS + 2]) > 0.0f)) positive = false;
            }
            if (got != ids || !sorted || !positive || count > cap) allOk = false;
        }
        check(allOk, "DOOM-0653 INV-6: every cell holds the brute-force top estimates, sorted, each above 0");
        // The stats are upper bounds taken per block of cells, so they may only exceed the
        // per-cell counts.
        check(over > 0 && st.overflow >= over, "DOOM-0653 INV-6: every cell with more in range than the cap is counted");
        check(st.worst >= worst, "DOOM-0653 INV-6: the reported worst bounds the most clusters in range of one cell");

        // A cluster's triangles and the per-emitter table agree, and in-cluster pdfs sum to 1.
        const uint32_t* cl = nee.data() + nee[NEE_HDR_CLUSTER_AT];
        const uint32_t* tr = nee.data() + nee[NEE_HDR_TRI_AT];
        const uint32_t* em = nee.data() + nee[NEE_HDR_EMIT_AT];
        bool tables = true;
        for (int c = 0; c < nc; c++)
        {
            double s = 0.0;
            for (uint32_t k = 0; k < cl[2 * c + 1]; k++)
            {
                const uint32_t e = tr[2 * (cl[2 * c] + k)];
                if (em[2 * e] != (uint32_t)c) tables = false;
                s += nee_word_float(em[2 * e + 1]);
            }
            if (std::fabs(s - 1.0) > 1e-5) tables = false;
        }
        if (em[2 * (ec.size() - 2)] != NEE_NO_CLUSTER) tables = false;   // the one with no cluster
        check(tables, "DOOM-0653: cluster triangle lists, emitter clusters and in-cluster pdfs agree");
    }

    return check_summary("light_grid_test");
}
