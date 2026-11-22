// light_grid.h — DOOM-0652 B1: the rasterised view's shared light grid.
//
// Every raster light fades to exactly nothing at a finite range, and every cell of a
// 2D grid (the seep field's) lists every light whose range reaches it. mesh.frag finds
// a fragment's cell from its world position and sums that cell's list, so a light is
// either on both sides of any line in the world or contributes zero at it: no light
// stops at a BSP split (spec INV-1).
//
// Pure functions only, so tests/light_grid_test.cpp can check them without the engine.
// The falloff here mirrors mesh.frag's; shader_mirror_test.cpp holds the constants equal.
#ifndef LIGHT_GRID_H
#define LIGHT_GRID_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "seep_dist.h"   // RB_SeepPointSegDist (B2's wall range test)

namespace lgrid {

// The half-bright distance of a raster light (world units). MUST equal mesh.frag's
// RASTER_LIGHT_RADIUS (shader_mirror_test.cpp).
static const float RASTER_LIGHT_RADIUS = 256.0f;
// A light's range is where its peak channel times the falloff drops to this. One
// constant scene-wide, so two lights of equal strength reach equally far (spec 4.2).
static const float RASTER_LIGHT_CUTOFF = 0.02f;
// No light reaches further than this. The window is evaluated against the capped range,
// so a cap shortens the glow smoothly rather than cutting it.
static const float RASTER_LIGHT_MAXRANGE = 512.0f;
// Lights listed per cell. Sized from the worst cell over every IWAD map (spec 7 B1),
// with room above it for the moving lights a cell takes each frame. The load log prints
// each map's worst cell and any overflow.
static const uint32_t RASTER_CELL_LIGHTS = 128;

// The falloff mesh.frag applies before the window: 1 / (1 + d^2 / radius^2).
inline float RasterAtt(float d2)
{
    return 1.0f / (1.0f + d2 / (RASTER_LIGHT_RADIUS * RASTER_LIGHT_RADIUS));
}

// clamp(1 - (d/R)^4, 0, 1)^2: 1 at the light, and value and slope both 0 at R.
inline float RasterWindow(float d, float R)
{
    if (!(R > 0.0f)) return 0.0f;
    const float q  = d / R;
    const float q2 = q * q;
    const float t  = std::min(std::max(1.0f - q2 * q2, 0.0f), 1.0f);
    return t * t;
}

// The range of a light whose brightest channel is `peak`: where peak * RasterAtt(d)
// falls to the cutoff, capped. 0 means the light is too faint to list at all.
inline float RasterLightRange(float peak)
{
    if (!(peak > RASTER_LIGHT_CUTOFF)) return 0.0f;
    const float R = RASTER_LIGHT_RADIUS * std::sqrt(peak / RASTER_LIGHT_CUTOFF - 1.0f);
    return std::min(R, RASTER_LIGHT_MAXRANGE);
}

// mesh.frag's whole falloff for one light at distance d, before colour and facing: a
// copy of the shader's, for tests/light_grid_test.cpp. reach > 0 is a pickup, whose own
// roll-off already ends at its range; anything else is windowed to its range.
inline float RasterFalloff(float d, float range, float reach)
{
    float att = RasterAtt(d * d);
    if (reach > 0.0f)
    {
        const float w = std::min(std::max(1.0f - d / reach, 0.0f), 1.0f);
        return att * w * w;
    }
    return att * RasterWindow(d, range);
}

// The grid: cell (0,0)'s CENTRE at (ox, oy), as the seep field stores it.
struct Grid {
    float ox = 0.0f, oy = 0.0f, cell = 0.0f;
    int   w = 0, h = 0;
};

// The 2D distance from (x, y) to cell (ix, iy)'s box. A light reaches the cell when
// this is under its range; 2D is conservative, since a fragment's 3D distance is never
// shorter.
inline float BoxDist(const Grid& g, int ix, int iy, float x, float y)
{
    const float cx = g.ox + ix * g.cell, cy = g.oy + iy * g.cell, hc = 0.5f * g.cell;
    const float dx = std::max(std::fabs(x - cx) - hc, 0.0f);
    const float dy = std::max(std::fabs(y - cy) - hc, 0.0f);
    return std::sqrt(dx * dx + dy * dy);
}

struct Light2D { float x, y, range; };

struct ListStats {
    uint32_t worst    = 0;   // the longest list before the cap
    uint32_t overflow = 0;   // cells that held more than the cap
    size_t   worstCell = 0;  // where the longest list is (row-major cell index)
};

// Fill `cells` with two words per cell, cells in row-major order: the cell's first entry
// in `idx` and its count; `idx` holds the lists end to end. A cell lists every light whose range reaches its box, in light order. A cell over
// the cap keeps the cap lights whose window is largest at the cell's nearest point and
// is counted in the overflow (spec INV-3). keep(cell, light) may veto a light the cell's
// rooms provably cannot see (the REJECT cull); the shader would skip it anyway.
struct KeepAll { bool operator()(size_t, uint32_t) const { return true; } };

template <class Keep = KeepAll>
inline ListStats BuildCellLists(const Grid& g, const Light2D* L, int n, uint32_t cap,
                                std::vector<uint32_t>& cellsOut, std::vector<uint32_t>& idx,
                                const Keep& keep = Keep())
{
    ListStats st;
    const size_t cells  = (size_t)std::max(g.w, 0) * (size_t)std::max(g.h, 0);
    cellsOut.assign(cells * 2u, 0u);
    idx.clear();
    if (cells == 0 || !(g.cell > 0.0f))
        return st;

    std::vector<std::vector<uint32_t>> lists(cells);
    for (int i = 0; i < n; i++)
    {
        const float R = L[i].range;
        if (!(R > 0.0f)) continue;
        // The cells whose box can be within R: centres within R + half a cell.
        const float reach = R + 0.5f * g.cell;
        const int x0 = std::max(0,       (int)std::floor((L[i].x - reach - g.ox) / g.cell));
        const int x1 = std::min(g.w - 1, (int)std::ceil ((L[i].x + reach - g.ox) / g.cell));
        const int y0 = std::max(0,       (int)std::floor((L[i].y - reach - g.oy) / g.cell));
        const int y1 = std::min(g.h - 1, (int)std::ceil ((L[i].y + reach - g.oy) / g.cell));
        for (int iy = y0; iy <= y1; iy++)
        for (int ix = x0; ix <= x1; ix++)
            if (BoxDist(g, ix, iy, L[i].x, L[i].y) < R && keep((size_t)iy * g.w + ix, (uint32_t)i))
                lists[(size_t)iy * g.w + ix].push_back((uint32_t)i);
    }

    std::vector<std::pair<float, uint32_t>> rank;
    for (size_t c = 0; c < cells; c++)
    {
        std::vector<uint32_t>& l = lists[c];
        if (l.size() > st.worst) { st.worst = (uint32_t)l.size(); st.worstCell = c; }
        if (l.size() > cap)
        {
            st.overflow++;
            const int ix = (int)(c % (size_t)g.w), iy = (int)(c / (size_t)g.w);
            rank.clear();
            for (uint32_t i : l)
                rank.emplace_back(RasterWindow(BoxDist(g, ix, iy, L[i].x, L[i].y), L[i].range), i);
            std::stable_sort(rank.begin(), rank.end(),
                             [](const std::pair<float, uint32_t>& a,
                                const std::pair<float, uint32_t>& b) { return a.first > b.first; });
            rank.resize(cap);
            l.clear();
            for (const auto& r : rank) l.push_back(r.second);
            std::sort(l.begin(), l.end());     // light order, as an uncapped cell keeps
        }
        cellsOut[2 * c]     = (uint32_t)idx.size();
        cellsOut[2 * c + 1] = (uint32_t)l.size();
        idx.insert(idx.end(), l.begin(), l.end());
    }
    return st;
}

// A cell's static count, from BuildCellLists' `cells`.
inline uint32_t StaticCount(const std::vector<uint32_t>& cells, size_t cell)
{
    return 2 * cell + 1 < cells.size() ? cells[2 * cell + 1] : 0u;
}

// The moving lights, rebuilt every frame: `cells` gets two words per cell (first index
// into `idx`, count) and `idx` the lists end to end. A cell takes the lights that reach
// it into the room its static list left under the cap; past that it keeps the ones
// whose window is largest and counts an overflow. The touched cells are found per light,
// so the cost is the lights' footprint, not the grid's size; the caller clears `cells`.
template <class Keep = KeepAll>
inline ListStats BuildDynLists(const Grid& g, const Light2D* L, int n,
                               const std::vector<uint32_t>& staticCells, uint32_t cap,
                               std::vector<uint32_t>& cells, std::vector<uint32_t>& idx,
                               std::vector<uint32_t>& touched, const Keep& keep = Keep())
{
    ListStats st;
    idx.clear();
    touched.clear();
    const size_t ncell = (size_t)std::max(g.w, 0) * (size_t)std::max(g.h, 0);
    if (cells.size() != ncell * 2u)
        cells.assign(ncell * 2u, 0u);
    if (ncell == 0 || n <= 0 || !(g.cell > 0.0f))
        return st;

    // Pass 1: count per cell (in cells[2c+1]) and remember which cells were touched.
    auto forCells = [&](int i, auto&& fn) {
        const float R = L[i].range;
        if (!(R > 0.0f)) return;
        const float reach = R + 0.5f * g.cell;
        const int x0 = std::max(0,       (int)std::floor((L[i].x - reach - g.ox) / g.cell));
        const int x1 = std::min(g.w - 1, (int)std::ceil ((L[i].x + reach - g.ox) / g.cell));
        const int y0 = std::max(0,       (int)std::floor((L[i].y - reach - g.oy) / g.cell));
        const int y1 = std::min(g.h - 1, (int)std::ceil ((L[i].y + reach - g.oy) / g.cell));
        for (int iy = y0; iy <= y1; iy++)
        for (int ix = x0; ix <= x1; ix++)
            if (BoxDist(g, ix, iy, L[i].x, L[i].y) < R && keep((size_t)iy * g.w + ix, (uint32_t)i))
                fn((size_t)iy * g.w + ix, ix, iy);
    };
    for (int i = 0; i < n; i++)
        forCells(i, [&](size_t c, int, int) {
            if (cells[2 * c + 1]++ == 0) touched.push_back((uint32_t)c);
        });

    // Starts, with each cell's room under the cap.
    uint32_t total = 0;
    for (uint32_t c : touched)
    {
        const uint32_t want = cells[2 * c + 1];
        st.worst = std::max(st.worst, StaticCount(staticCells, c) + want);
        cells[2 * c] = total;
        total += want;            // every candidate gets a slot; the overflowing are cut below
    }
    idx.assign(total, 0u);
    for (uint32_t c : touched) cells[2 * c + 1] = 0;

    // Pass 2: fill.
    for (int i = 0; i < n; i++)
        forCells(i, [&](size_t c, int, int) { idx[cells[2 * c] + cells[2 * c + 1]++] = (uint32_t)i; });

    // Cut the cells over the cap, keeping the largest windows, in light order.
    std::vector<std::pair<float, uint32_t>> rank;
    for (uint32_t c : touched)
    {
        const uint32_t sc   = StaticCount(staticCells, c);
        const uint32_t room = sc < cap ? cap - sc : 0u;
        uint32_t& cnt = cells[2 * c + 1];
        if (cnt <= room) continue;
        st.overflow++;
        const int ix = (int)(c % (uint32_t)g.w), iy = (int)(c / (uint32_t)g.w);
        uint32_t* l = &idx[cells[2 * c]];
        rank.clear();
        for (uint32_t k = 0; k < cnt; k++)
            rank.emplace_back(RasterWindow(BoxDist(g, ix, iy, L[l[k]].x, L[l[k]].y), L[l[k]].range), l[k]);
        std::stable_sort(rank.begin(), rank.end(),
                         [](const std::pair<float, uint32_t>& a,
                            const std::pair<float, uint32_t>& b) { return a.first > b.first; });
        rank.resize(room);
        for (uint32_t k = 0; k < room; k++) l[k] = rank[k].second;
        std::sort(l, l + room);
        cnt = room;
    }
    return st;
}

// Clear the cells BuildDynLists touched, so the next frame starts from zero without
// clearing the whole grid.
inline void ClearDynLists(std::vector<uint32_t>& cells, const std::vector<uint32_t>& touched)
{
    for (uint32_t c : touched) { cells[2 * c] = 0; cells[2 * c + 1] = 0; }
}

// ---------------------------------------------------------------------------------------
// DOOM-0652 B2 (spec 4.4): the polar wall map. Each light keeps, for each of
// RASTER_POLAR_BINS directions round it, the 2D distance to the nearest wall that blocks
// it. A fragment is lit by the light when it is nearer than the wall in its direction.
// The direction is a "diamond angle" -- monotonic in the true angle and free of
// trigonometry -- scaled to bins. mesh.frag computes the same coordinate and samples the
// map the same way; shader_mirror_test.cpp holds the constants equal.
// ---------------------------------------------------------------------------------------

// Directions per light: a bin is about 12 units wide at the 512-unit range cap. MUST
// equal mesh.frag's RASTER_POLAR_BINS.
static const uint32_t RASTER_POLAR_BINS  = 256;
// A fragment is tested this far off its own surface along its geometric normal, so a wall
// facing the light is not shadowed by itself. A floor's normal has no 2D part, so floor
// light still stops at the wall's foot. MUST equal mesh.frag's.
static const float    RASTER_POLAR_NBIAS = 4.0f;
// The shadow fades over this many units past the wall. MUST equal mesh.frag's.
static const float    RASTER_POLAR_SOFT  = 4.0f;

// What a linedef does to light, in 2D. RB_LightWalls (r_lightwall.c) fills these.
enum : uint32_t {
    LW_OPEN     = 0,   // two-sided with an opening: light passes (spec 10)
    LW_ONESIDED = 1,   // one-sided: blocks light reaching its front, right of (x1,y1)->(x2,y2)
    LW_CLOSED   = 2,   // two-sided with no opening, as a shut door: blocks from both sides
};
struct Wall2D { float x1, y1, x2, y2; uint32_t kind; };

// The diamond angle of (dx, dy) in bins, [0, RASTER_POLAR_BINS).
inline float PolarCoord(float dx, float dy)
{
    const float s = std::fabs(dx) + std::fabs(dy);
    if (!(s > 0.0f)) return 0.0f;
    float p;
    if (dy >= 0.0f) p = dx >= 0.0f ? dy / s : 1.0f - dx / s;
    else            p = dx <  0.0f ? 2.0f - dy / s : 3.0f + dx / s;
    p *= (float)RASTER_POLAR_BINS * 0.25f;
    return p >= (float)RASTER_POLAR_BINS ? p - (float)RASTER_POLAR_BINS : p;
}

// The direction of polar coordinate p (bins), with |dx| + |dy| = 1.
inline void PolarDir(float p, float& dx, float& dy)
{
    float q = p * (4.0f / (float)RASTER_POLAR_BINS);
    q -= 4.0f * std::floor(q * 0.25f);
    const int   quad = std::min((int)q, 3);
    const float t    = q - (float)quad;
    switch (quad)
    {
        case 0:  dx = 1.0f - t; dy = t;        break;
        case 1:  dx = -t;       dy = 1.0f - t; break;
        case 2:  dx = t - 1.0f; dy = -t;       break;
        default: dx = t;        dy = t - 1.0f; break;
    }
}

// Fill out[RASTER_POLAR_BINS] for a light at (lx, ly) with range R, from walls[cand[k]]
// for k < n. A bin with no blocking wall in range holds R, so the distance a fragment
// interpolates never jumps to infinity at a wall's end. A bin holds the wall distance
// along its centre ray, with the ray clamped to the wall's own angular span, so a wall
// narrower than a bin still blocks the bin it falls in.
//
// A one-sided wall blocks only light that reaches its front. A light behind one is inside
// solid map (a cluster centre that fell inside a pillar), and lighting the room round the
// pillar beats lighting nothing. In a closed map, the first one-sided wall any ray from
// open air meets is met from the front, so this skips no real occluder.
inline void BuildPolarMap(float lx, float ly, float R, const Wall2D* walls,
                          const uint32_t* cand, int n, float* out)
{
    const int B = (int)RASTER_POLAR_BINS;
    for (int b = 0; b < B; b++) out[b] = R;
    for (int k = 0; k < n; k++)
    {
        const Wall2D& w = walls[cand[k]];
        if (w.kind == LW_OPEN) continue;
        const float ax = w.x1 - lx, ay = w.y1 - ly, bx = w.x2 - lx, by = w.y2 - ly;
        // cross(a, b) < 0: b is clockwise of a, which puts the light on the line's right,
        // its front.
        const float cr = ax * by - ay * bx;
        if (cr == 0.0f) continue;                    // a line through the light hides no angle
        if (w.kind == LW_ONESIDED && cr > 0.0f) continue;
        if (RB_SeepPointSegDist(lx, ly, w.x1, w.y1, w.x2, w.y2) >= R) continue;
        // Sweep counter-clockwise from s to e.
        const float sx = cr > 0.0f ? ax : bx, sy = cr > 0.0f ? ay : by;
        const float ex = cr > 0.0f ? bx : ax, ey = cr > 0.0f ? by : ay;
        const float ps = PolarCoord(sx, sy);
        float       pe = PolarCoord(ex, ey);
        if (pe < ps) pe += (float)B;
        const float wx = ex - sx, wy = ey - sy;
        for (int bb = (int)std::floor(ps); bb <= (int)std::floor(pe); bb++)
        {
            float dx, dy;
            PolarDir(std::min(std::max((float)bb + 0.5f, ps), pe), dx, dy);
            // The ray t * d meets s + u * w at t = cross(s, w) / cross(d, w).
            const float den = dx * wy - dy * wx;
            if (den == 0.0f) continue;
            const float t = (sx * wy - sy * wx) / den;
            if (!(t > 0.0f)) continue;
            float& o = out[bb % B];
            o = std::min(o, t * std::sqrt(dx * dx + dy * dy));
        }
    }
}

// How lit the point (dx, dy) from the light is, in [0, 1]: the wall distance linearly
// interpolated between the two nearest bin centres, then a step RASTER_POLAR_SOFT wide
// past it. The distance is interpolated rather than the test, so a lit wall stays lit:
// the distance to a straight wall is convex in the angle, and the chord never falls short
// of it. mesh.frag's copy is the one that runs; this one is what the tests read.
inline float PolarLit(const float* map, float dx, float dy)
{
    const int   B  = (int)RASTER_POLAR_BINS;
    const float p  = PolarCoord(dx, dy) - 0.5f;
    const float fl = std::floor(p);
    const int   i0 = ((int)fl + B) % B, i1 = (i0 + 1) % B;
    const float wall = map[i0] + (map[i1] - map[i0]) * (p - fl);
    const float d    = std::sqrt(dx * dx + dy * dy);
    return std::min(std::max((wall - d) / RASTER_POLAR_SOFT + 1.0f, 0.0f), 1.0f);
}

// Can any point of the box (cx +- hc, cy +- hc), grown by the normal offset, be lit by the
// light at (lx, ly)? Conservative: the most open bin any point of the box's angular span
// interpolates from, against the box's nearest point. The cell lists
// use it in place of the B1 REJECT cull, so a light shut behind a wall costs a cell nothing.
inline bool PolarBoxMaySee(const float* map, float lx, float ly, float cx, float cy, float hc)
{
    const float h  = hc + RASTER_POLAR_NBIAS + 1.0f;
    const float x0 = cx - h - lx, x1 = cx + h - lx, y0 = cy - h - ly, y1 = cy + h - ly;
    if (x0 <= 0.0f && x1 >= 0.0f && y0 <= 0.0f && y1 >= 0.0f)
        return true;                                 // the light is in the box
    const float nx = std::max(std::max(x0, -x1), 0.0f), ny = std::max(std::max(y0, -y1), 0.0f);
    const float near = std::sqrt(nx * nx + ny * ny);
    const int   B  = (int)RASTER_POLAR_BINS;
    // Seen from outside, a box spans under half a turn, so its corners' offsets from the
    // direction of its centre bound its span.
    const float pc = PolarCoord(0.5f * (x0 + x1), 0.5f * (y0 + y1));
    float lo = 0.0f, hi = 0.0f;
    const float xs[2] = { x0, x1 }, ys[2] = { y0, y1 };
    for (float x : xs) for (float y : ys)
    {
        float o = PolarCoord(x, y) - pc;
        if (o >  0.5f * B) o -= (float)B;
        if (o < -0.5f * B) o += (float)B;
        lo = std::min(lo, o); hi = std::max(hi, o);
    }
    // PolarLit at p reads bins floor(p - 0.5) and the one after, so these are every bin a
    // point of the span reads.
    const int b0 = (int)std::floor(pc + lo - 0.5f), b1 = (int)std::floor(pc + hi + 0.5f);
    for (int b = b0; b <= b1; b++)
        if (map[((b % B) + B) % B] + RASTER_POLAR_SOFT > near)
            return true;
    return false;
}

}  // namespace lgrid

#endif
