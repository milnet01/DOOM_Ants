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
#include <cstring>
#include <utility>
#include <vector>

#include "nee_sampling.h"   // DOOM-0653: the lists' layout and nee_build_cdf
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

// Directions per light: at the 512-unit range cap a bin is about 8 units wide on the axes
// and 16 on the diagonals (PolarCoord is a diamond angle, not an even one). MUST
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

// The diamond angle of (dx, dy) in bins, [0, bins). A probe's map (B5) uses fewer bins
// than a light's.
inline float PolarCoord(float dx, float dy, int bins = (int)RASTER_POLAR_BINS)
{
    const float s = std::fabs(dx) + std::fabs(dy);
    if (!(s > 0.0f)) return 0.0f;
    float p;
    if (dy >= 0.0f) p = dx >= 0.0f ? dy / s : 1.0f - dx / s;
    else            p = dx <  0.0f ? 2.0f - dy / s : 3.0f + dx / s;
    p *= (float)bins * 0.25f;
    return p >= (float)bins ? p - (float)bins : p;
}

// The direction of polar coordinate p (bins), with |dx| + |dy| = 1.
inline void PolarDir(float p, float& dx, float& dy, int bins = (int)RASTER_POLAR_BINS)
{
    float q = p * (4.0f / (float)bins);
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

// Fill out[bins] for a light at (lx, ly) with range R, from walls[cand[k]]
// for k < n. A bin with no blocking wall in range holds R, so the distance a fragment
// interpolates never jumps to infinity at a wall's end. A bin holds the wall distance
// along its centre ray, with the ray clamped to the wall's own angular span, so a wall
// narrower than a bin still blocks the bin it falls in.
//
// With centresOnly, a wall writes only the bins whose centre ray it crosses, with no clamp.
// A probe's map (B5) needs this: its bins are wide, and seen edge-on from a probe near it,
// every linedef of a long wall is narrower than a bin, so each clamped ray meets a joint far
// nearer than the wall along the bin's centre, and a point in front of the wall reads
// blocked. A pillar thinner than a bin can then go unseen, which errs toward light.
//
// behind moves each written distance that far past the wall, measured square to the wall,
// so a reader compares against the map with no slack of its own. Along a slanting ray the
// same depth is longer, by 1 / sin of the angle between ray and wall, capped at 4x.
//
// A one-sided wall blocks only light that reaches its front. A light behind one is inside
// solid map (a cluster centre that fell inside a pillar), and lighting the room round the
// pillar beats lighting nothing. In a closed map, the first one-sided wall any ray from
// open air meets is met from the front, so this skips no real occluder.
inline void BuildPolarMap(float lx, float ly, float R, const Wall2D* walls,
                          const uint32_t* cand, int n, float* out,
                          int bins = (int)RASTER_POLAR_BINS, bool centresOnly = false,
                          float behind = 0.0f)
{
    const int B = bins;
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
        const float ps = PolarCoord(sx, sy, B);
        float       pe = PolarCoord(ex, ey, B);
        if (pe < ps) pe += (float)B;
        const float wx = ex - sx, wy = ey - sy;
        for (int bb = (int)std::floor(ps); bb <= (int)std::floor(pe); bb++)
        {
            if (centresOnly && ((float)bb + 0.5f < ps || (float)bb + 0.5f > pe))
                continue;                            // centres sit at bb + 0.5
            float dx, dy;
            PolarDir(std::min(std::max((float)bb + 0.5f, ps), pe), dx, dy, B);
            // The ray t * d meets s + u * w at t = cross(s, w) / cross(d, w).
            const float den = dx * wy - dy * wx;
            if (den == 0.0f) continue;
            const float t = (sx * wy - sy * wx) / den;
            if (!(t > 0.0f)) continue;
            const float dl = std::sqrt(dx * dx + dy * dy);
            float past = 0.0f;
            if (behind > 0.0f)
                past = std::min(behind * dl * std::sqrt(wx * wx + wy * wy) / std::fabs(den),
                                4.0f * behind);
            float& o = out[bb % B];
            o = std::min(o, t * dl + past);
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

// ---------------------------------------------------------------------------------------
// B3 (spec 4.5): the bounce light's probes on a 3D lattice, blended trilinearly.
//
// The probe buffer, which the bake writes and gi_probe.glsl's giIrradianceAt reads, is
// floats end to end:
//   header  PROBE_HDR floats: origin x, y, z, cell (xy spacing), dz, nx, ny, nz
//   index   nx * ny * nz floats, x fastest: the point's record number, or -1 if none
//   records PROBE_REC floats each: pos[3], valid, SH-L1 radiance R[4] G[4] B[4],
//           reach +x -x +y -y +z -z, map start, one pad
//   maps    RB_PROBE_POLAR_BINS floats each (B5), for the probes whose map start is >= 0
// The index holds float values, not bit patterns, so no read can mistake one for a NaN.
// "valid" is the bake's verdict: 0 when the point turned out to sit inside solid map.

// Layer spacing (world units), doubled until the layers fit RB_PROBE_MAXLAYERS.
static const float RB_PROBE_DZ        = 128.0f;
static const int   RB_PROBE_MAXLAYERS = 8;
// A point is looked up this far off its surface along its normal, so a floor reads the air
// above it. MUST equal gi_probe.glsl's GI_PROBE_NBIAS (shader_mirror_test.cpp).
static const float RB_PROBE_NBIAS     = 8.0f;
// A corner's reach may fall this short of the point and still count (UT_Ants'
// PROBE_SIGHT_SLACK). MUST equal gi_probe.glsl's GI_PROBE_SLACK.
static const float RB_PROBE_SLACK     = 8.0f;
// How far inside a column's air a moved point sits (spec 4.5).
static const float RB_PROBE_MARGIN    = 8.0f;
static const uint32_t PROBE_HDR = 8;    // MUST equal gi_probe.glsl's GI_PROBE_HDR
static const uint32_t PROBE_REC = 24;   // MUST equal gi_probe.glsl's GI_PROBE_REC
// Record float 22: the buffer index of the probe's polar map, or -1 for a probe with no wall
// in range (B5). A float index, so the whole buffer must stay under 2^24 floats.
static const uint32_t PROBE_MAP   = 22;
static const size_t   PROBE_FLOAT_LIMIT = (size_t)1 << 24;
// B5 (spec 4.5): each probe's polar wall map. Bins round the probe, few because a probe only
// answers for the cube of lattice points around it. MUST equal gi_probe.glsl's
// GI_PROBE_BINS.
static const int      RB_PROBE_POLAR_BINS  = 32;
// How far behind a wall, square to it, a probe still sees a point. Ultra's relief panels
// (relief.csv) sink a lit surface up to 12 units behind its linedef; the normal offset
// does not lift a recess's side faces out of it.
static const float    RB_PROBE_WALL_SLACK  = 14.0f;
// The map's range in lattice cells. A point a probe answers for lies within sqrt(2) cells
// of its lattice point plus the normal offset, and a moved probe sits up to half a cell's
// diagonal further off: about 2.2 cells. A bin with no wall in range holds the range, and
// the read interpolates toward it, so the range sits well past that.
static const float    RB_PROBE_POLAR_REACH = 4.0f;

struct ProbeLattice { float ox, oy, oz, cell, dz; int nx, ny, nz; };

// The lattice over the seep field's cells (centres at ox + i * cell) and the z span
// [zmin, zmax]: layers dz apart from zmin + dz / 2, dz doubled until they fit.
inline ProbeLattice MakeProbeLattice(float ox, float oy, float cell, int nx, int ny,
                                     float zmin, float zmax)
{
    ProbeLattice L = { ox, oy, 0.0f, cell, RB_PROBE_DZ, nx, ny, 1 };
    const float span = std::max(zmax - zmin, 1.0f);
    while ((int)std::ceil(span / L.dz) > RB_PROBE_MAXLAYERS)
        L.dz *= 2.0f;
    L.nz = std::max((int)std::ceil(span / L.dz), 1);
    L.oz = zmin + 0.5f * L.dz;
    return L;
}

inline size_t ProbeIndexCount(const ProbeLattice& L) { return (size_t)L.nx * L.ny * L.nz; }
inline size_t ProbeRecBase(const ProbeLattice& L)    { return PROBE_HDR + ProbeIndexCount(L); }

// Where layer z's probe stands in a column whose air is [f, c], or NAN if it has none.
// A layer inside the air stays put; one less than a layer outside it moves in, so a room
// lower than dz still gets probes at its floor and ceiling (spec 4.5).
inline float ProbeLayerZ(float z, float dz, float f, float c)
{
    if (!(c > f))
        return NAN;
    const float m  = std::min(RB_PROBE_MARGIN, 0.5f * (c - f));
    const float lo = f + m, hi = c - m;
    if (z < lo - dz || z > hi + dz)
        return NAN;
    return std::min(std::max(z, lo), hi);
}

// B5: fill out[RB_PROBE_POLAR_BINS] for a probe at (px, py, pz) from walls[cand[k]], k < n.
// gapLo and gapHi hold each wall's opening (the higher floor and the lower ceiling of its
// two sides). An open two-sided line whose opening does not hold pz blocks the probe as a
// shut one would, so a probe under a ledge does not see the ledge's top. Returns whether any
// bin holds a wall nearer than R; a map with none need not be stored.
inline bool BuildProbePolarMap(float px, float py, float pz, float R, const Wall2D* walls,
                               const float* gapLo, const float* gapHi,
                               const uint32_t* cand, int n, float* out)
{
    std::vector<Wall2D>   w((size_t)std::max(n, 0));
    std::vector<uint32_t> ids((size_t)std::max(n, 0));
    for (int k = 0; k < n; k++)
    {
        const uint32_t id = cand[k];
        w[(size_t)k]   = walls[id];
        ids[(size_t)k] = (uint32_t)k;
        if (w[(size_t)k].kind == LW_OPEN && (pz < gapLo[id] || pz > gapHi[id]))
            w[(size_t)k].kind = LW_CLOSED;
    }
    BuildPolarMap(px, py, R, w.data(), ids.data(), n, out, RB_PROBE_POLAR_BINS, true,
                  RB_PROBE_WALL_SLACK);
    for (int b = 0; b < RB_PROBE_POLAR_BINS; b++)
        if (out[b] < R) return true;
    return false;
}

// B5: does a probe see the point (dx, dy) from it across its map? The wall distance is
// interpolated between the two nearest bins, as PolarLit does; the map already holds
// RB_PROBE_WALL_SLACK past each wall. gi_probe.glsl's giProbeSees is the copy that runs.
inline bool ProbeSeesAcross(const float* map, float dx, float dy)
{
    const int   B  = RB_PROBE_POLAR_BINS;
    const float p  = PolarCoord(dx, dy, B) - 0.5f;
    const float fl = std::floor(p);
    const int   i0 = ((int)fl + B) % B, i1 = (i0 + 1) % B;
    const float wall = map[i0] + (map[i1] - map[i0]) * (p - fl);
    return std::sqrt(dx * dx + dy * dy) <= wall;
}

// The 12 SH-L1 coefficients blended at world point p (already offset along its normal)
// from the 8 corners around it: a corner without a valid probe is dropped, and so is one
// with a wall between it and p (B5: its polar map across, its z reach up and down); the
// weight left is renormalised. If every present corner is blocked, they are blended unmasked rather than giving black (UT_Ants' indirectAt).
// Returns false with sh zeroed when no corner has a probe. gi_probe.glsl's
// giIrradianceAt is the copy that runs; this one is what the tests read.
inline bool ProbeBlendSH(const float* buf, float px, float py, float pz, float sh[12])
{
    for (int k = 0; k < 12; k++) sh[k] = 0.0f;
    const ProbeLattice L = { buf[0], buf[1], buf[2], buf[3], buf[4],
                             (int)buf[5], (int)buf[6], (int)buf[7] };
    const size_t rec0 = ProbeRecBase(L);
    const float cx = (px - L.ox) / L.cell, cy = (py - L.oy) / L.cell, cz = (pz - L.oz) / L.dz;
    const float bx = std::floor(cx), by = std::floor(cy), bz = std::floor(cz);
    const float f[3] = { cx - bx, cy - by, cz - bz };
    float sum[12] = {}, all[12] = {};
    float wSum = 0.0f, wAll = 0.0f;
    for (int corner = 0; corner < 8; corner++)
    {
        const int o[3] = { corner & 1, (corner >> 1) & 1, (corner >> 2) & 1 };
        const int ix = (int)bx + o[0], iy = (int)by + o[1], iz = (int)bz + o[2];
        if (ix < 0 || iy < 0 || iz < 0 || ix >= L.nx || iy >= L.ny || iz >= L.nz)
            continue;
        const float idx = buf[PROBE_HDR + ((size_t)iz * L.ny + iy) * L.nx + ix];
        if (idx < 0.0f)
            continue;
        const float* r = buf + rec0 + (size_t)idx * PROBE_REC;
        if (r[3] == 0.0f)
            continue;
        float w = 1.0f;
        for (int a = 0; a < 3; a++) w *= o[a] ? f[a] : 1.0f - f[a];
        // B5: across by the probe's polar map, up and down by its z reach (spec 4.5).
        const float d[3] = { px - r[0], py - r[1], pz - r[2] };
        bool reaches = std::fabs(d[2]) <= (d[2] >= 0.0f ? r[20] : r[21]) + RB_PROBE_SLACK;
        if (reaches && r[PROBE_MAP] >= 0.0f)
            reaches = ProbeSeesAcross(buf + (size_t)r[PROBE_MAP], d[0], d[1]);
        for (int k = 0; k < 12; k++) all[k] += w * r[4 + k];
        wAll += w;
        if (!reaches)
            continue;
        for (int k = 0; k < 12; k++) sum[k] += w * r[4 + k];
        wSum += w;
    }
    if (wSum > 0.0f)      for (int k = 0; k < 12; k++) sh[k] = sum[k] / wSum;
    else if (wAll > 0.0f) for (int k = 0; k < 12; k++) sh[k] = all[k] / wAll;
    return wAll > 0.0f;
}

// ---------------------------------------------------------------------------------------
// DOOM-0653 (spec 4.1): the traced view's per-cell light lists. Each cell lists the static
// light clusters that reach it, ranked by their estimate there, and the megakernel draws
// most of its static-light samples from that list (nee_sampling.h, the mixture pick).
// ---------------------------------------------------------------------------------------

// Clusters listed per cell. Sized from the worst cell over every IWAD map (spec 7 B1).
static const uint32_t NEE_CELL_LIGHTS = 32;
// The estimate's distance floor, so a cluster at the cell does not take its whole list.
static const float    NEE_NEAR        = 16.0f;
// No cluster further than this from a cell's box is listed in it.
static const float    NEE_LIST_RANGE  = 2048.0f;
// Cells ranked together by BuildNeeCellLists, per side (a speed setting: the lists do not
// depend on it).
static const int      NEE_BLOCK       = 8;

inline uint32_t NeeBits(float f) { uint32_t u; std::memcpy(&u, &f, sizeof u); return u; }

// Fill `out` with the lists' buffer (nee_sampling.h's layout) for grid g. Cluster ci's
// centre is (cx[ci], cy[ci]); static emitter e belongs to cluster emitCluster[e] (-1 for
// none) and has power emitW[e], the weight the global cdf uses. A cluster's power is the
// sum of its emitters'. Its estimate at a cell is power / max(d^2, NEE_NEAR^2), d the 2D
// distance from its centre to the cell's box, and see(cell, ci) false makes it 0 (the
// raster polar map's cull). A cell keeps its `cap` largest estimates above 0, ties to the
// lower cluster id, sorted by cluster id. The stats are upper bounds, from the clusters in
// range of each cell's NEE_BLOCK square before the cull and the cap: the largest such
// count, and the cells whose count is over the cap.
template <class See = KeepAll>
inline ListStats BuildNeeCellLists(const Grid& g, const float* cx, const float* cy, int nc,
                                   const int32_t* emitCluster, const float* emitW, int ne,
                                   uint32_t cap, std::vector<uint32_t>& out,
                                   const See& see = See())
{
    ListStats st;
    const size_t cells = (g.cell > 0.0f) ? (size_t)std::max(g.w, 0) * (size_t)std::max(g.h, 0) : 0;
    nc = std::max(nc, 0);
    ne = std::max(ne, 0);

    // Clusters' members, grouped by cluster in emitter order, with their in-cluster cdf.
    std::vector<uint32_t> first((size_t)nc + 1, 0u);
    std::vector<double>   power((size_t)nc, 0.0);
    for (int e = 0; e < ne; e++)
    {
        const int32_t c = emitCluster[e];
        if (c < 0 || c >= nc) continue;
        first[(size_t)c + 1]++;
        power[(size_t)c] += (double)std::max(emitW[e], 0.0f);
    }
    for (int c = 0; c < nc; c++) first[(size_t)c + 1] += first[(size_t)c];
    std::vector<uint32_t> member(first[(size_t)nc]);
    {
        std::vector<uint32_t> fill(first.begin(), first.end() - 1);
        for (int e = 0; e < ne; e++)
        {
            const int32_t c = emitCluster[e];
            if (c >= 0 && c < nc) member[fill[(size_t)c]++] = (uint32_t)e;
        }
    }
    std::vector<float> triCdf(member.size()), inPdf((size_t)ne, 0.0f), w, cdf, pdf;
    for (int c = 0; c < nc; c++)
    {
        const uint32_t a = first[(size_t)c], n = first[(size_t)c + 1] - a;
        if (n == 0) continue;
        w.resize(n); cdf.resize(n); pdf.resize(n);
        for (uint32_t k = 0; k < n; k++) w[k] = std::max(emitW[member[a + k]], 0.0f);
        nee_build_cdf(w.data(), (int)n, cdf.data(), pdf.data());
        for (uint32_t k = 0; k < n; k++)
        {
            triCdf[a + k]            = cdf[k];
            inPdf[member[a + k]]     = pdf[k];
        }
    }

    // Lay the buffer out. A heap keeps each cell's cap best estimates so far, its worst at
    // the front.
    out.assign(NEE_HDR_WORDS, 0u);
    out[NEE_HDR_CELLS]    = (uint32_t)cells;
    out[NEE_HDR_EMITTERS] = (uint32_t)ne;
    out[NEE_HDR_CLUSTERS] = (uint32_t)nc;
    out[NEE_HDR_CELL_AT]  = (uint32_t)out.size();
    out.resize(out.size() + 2 * cells, 0u);
    std::vector<uint32_t> entries;
    std::vector<int> live;                       // clusters with power
    for (int c = 0; c < nc; c++) if (power[(size_t)c] > 0.0) live.push_back(c);
    entries.reserve(cells * std::min<size_t>(cap, live.size()) * NEE_ENTRY_WORDS);
    const float near2 = NEE_NEAR * NEE_NEAR, range2 = NEE_LIST_RANGE * NEE_LIST_RANGE;
    // a before b: the larger estimate, ties to the lower id. As a heap order it puts the
    // worst kept entry at the front.
    auto better = [](const std::pair<float, uint32_t>& a, const std::pair<float, uint32_t>& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    };
    // Cells are ranked a block at a time. A block sorts its clusters by the largest
    // estimate any of its cells could get, and each cell stops walking that order once no
    // cluster left can beat its list's worst: the lists come out as a full scan would
    // make them, at a fraction of the scan.
    const int B = NEE_BLOCK;
    std::vector<std::pair<float, uint32_t>> l, order;
    for (int by0 = 0; cells && by0 < g.h; by0 += B)
    for (int bx0 = 0; bx0 < g.w; bx0 += B)
    {
        const int bx1 = std::min(bx0 + B, g.w) - 1, by1 = std::min(by0 + B, g.h) - 1;
        const float hc = 0.5f * g.cell;
        const float lx = g.ox + bx0 * g.cell - hc, hx = g.ox + bx1 * g.cell + hc;
        const float ly = g.oy + by0 * g.cell - hc, hy = g.oy + by1 * g.cell + hc;
        order.clear();
        for (int c : live)
        {
            const float dx = std::max(std::max(lx - cx[c], cx[c] - hx), 0.0f);
            const float dy = std::max(std::max(ly - cy[c], cy[c] - hy), 0.0f);
            const float d2 = dx * dx + dy * dy;
            if (!(d2 < range2)) continue;
            order.emplace_back((float)(power[(size_t)c] / std::max(d2, near2)), (uint32_t)c);
        }
        std::sort(order.begin(), order.end(), better);
        // The stats: clusters in range of the block, an upper bound for each of its cells.
        const uint32_t inBlock = (uint32_t)order.size();
        for (int iy = by0; iy <= by1; iy++)
        for (int ix = bx0; ix <= bx1; ix++)
        {
            const size_t cell = (size_t)iy * g.w + ix;
            if (inBlock > st.worst) { st.worst = inBlock; st.worstCell = cell; }
            if (inBlock > cap) st.overflow++;
            const float bx = g.ox + ix * g.cell, byc = g.oy + iy * g.cell;
            l.clear();
            for (const auto& o : order)
            {
                if (cap == 0) break;
                // o.first bounds every estimate after it: once the list is full and its
                // worst beats that bound, nothing later gets in.
                if (l.size() >= cap && o.first < l.front().first) break;
                const uint32_t c = o.second;
                const float dx = std::max(std::fabs(cx[c] - bx) - hc, 0.0f);
                const float dy = std::max(std::fabs(cy[c] - byc) - hc, 0.0f);
                const float d2 = dx * dx + dy * dy;
                if (!(d2 < range2)) continue;
                const std::pair<float, uint32_t> e((float)(power[c] / std::max(d2, near2)), c);
                if (!(e.first > 0.0f)) continue;
                const bool full = l.size() >= cap;
                if (full && !better(e, l.front())) continue;
                if (!see(cell, c)) continue;     // the wall cull, only for a keeper
                if (!full)
                {
                    l.push_back(e);
                    std::push_heap(l.begin(), l.end(), better);
                }
                else
                {
                    std::pop_heap(l.begin(), l.end(), better);
                    l.back() = e;
                    std::push_heap(l.begin(), l.end(), better);
                }
            }
            std::sort(l.begin(), l.end(), [](const std::pair<float, uint32_t>& a,
                                             const std::pair<float, uint32_t>& b) {
                return a.second < b.second;
            });
            if (l.empty()) continue;
            w.resize(l.size()); cdf.resize(l.size()); pdf.resize(l.size());
            for (size_t k = 0; k < l.size(); k++) w[k] = l[k].first;
            nee_build_cdf(w.data(), (int)l.size(), cdf.data(), pdf.data());
            // Cells are filled a block at a time, so the entries are not in cell order:
            // each cell's two words say where its own run starts.
            out[NEE_HDR_WORDS + 2 * cell]     = (uint32_t)(entries.size() / NEE_ENTRY_WORDS);
            out[NEE_HDR_WORDS + 2 * cell + 1] = (uint32_t)l.size();
            for (size_t k = 0; k < l.size(); k++)
            {
                entries.push_back(l[k].second);
                entries.push_back(NeeBits(cdf[k]));
                entries.push_back(NeeBits(pdf[k]));
            }
        }
    }
    out[NEE_HDR_ENTRY_AT] = (uint32_t)out.size();
    out.insert(out.end(), entries.begin(), entries.end());
    out[NEE_HDR_CLUSTER_AT] = (uint32_t)out.size();
    for (int c = 0; c < nc; c++)
    {
        out.push_back(first[(size_t)c]);
        out.push_back(first[(size_t)c + 1] - first[(size_t)c]);
    }
    out[NEE_HDR_TRI_AT] = (uint32_t)out.size();
    for (size_t k = 0; k < member.size(); k++)
    {
        out.push_back(member[k]);
        out.push_back(NeeBits(triCdf[k]));
    }
    out[NEE_HDR_EMIT_AT] = (uint32_t)out.size();
    for (int e = 0; e < ne; e++)
    {
        const int32_t c = emitCluster[e];
        out.push_back(c >= 0 && c < nc ? (uint32_t)c : NEE_NO_CLUSTER);
        out.push_back(NeeBits(inPdf[(size_t)e]));
    }
    return st;
}

}  // namespace lgrid

#endif
