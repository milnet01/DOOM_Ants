// wall_relief_test.cpp - DOOM-0541: locks RB_ReliefBuild's geometry and relief.csv's parser.
//
// The closure checks use the divergence theorem: a closed surface's faces, weighted by area, sum to
// zero along any axis. So a hole, a missing side wall or a face turned the wrong way moves a sum.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../wall_relief.h"

static int failures = 0;

static void check(bool ok, const char* what)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

static bool near(float a, float b) { return std::fabs(a - b) < 1e-2f; }

struct Sums
{
    std::vector<rb_relief_vert_t> v;
};

static void collect(const rb_relief_vert_t q[4], void* user)
{
    Sums* s = (Sums*)user;
    for (int i = 0; i < 4; i++)
        s->v.push_back(q[i]);
}

struct Point { float x, y, z; };

// The wall's right-handed frame: u, up (minus v), out.
static Point pos(const rb_relief_vert_t& v) { return { v.u, -v.v, v.d }; }

static Point cross(Point a, Point b)
{
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

static Point sub(Point a, Point b) { return { a.x-b.x, a.y-b.y, a.z-b.z }; }

// A quad's area vector, from its corners alone, so the winding is what is measured.
static Point area(const rb_relief_vert_t* q)
{
    Point a = cross(sub(pos(q[1]), pos(q[0])), sub(pos(q[2]), pos(q[0])));
    Point b = cross(sub(pos(q[2]), pos(q[0])), sub(pos(q[3]), pos(q[0])));
    return { 0.5f*(a.x+b.x), 0.5f*(a.y+b.y), 0.5f*(a.z+b.z) };
}

struct Report
{
    float sumU = 0, sumUp = 0, outward = 0, volume = 0;
    float minD = 0, maxD = -1e9f, minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
    bool  windingOk = true, uvOk = true;
};

static Report measure(const Sums& s)
{
    Report r;
    for (size_t i = 0; i + 3 < s.v.size(); i += 4)
    {
        const rb_relief_vert_t* q = &s.v[i];
        const Point a = area(q);
        r.sumU  += a.x;
        r.sumUp += a.y;
        r.outward += a.z;
        if (q[0].nd > 0.5f)
            r.volume += a.z * -q[0].d;
        // The stated normal must agree with the winding.
        const float dot = a.x*q[0].nu + a.y*-q[0].nv + a.z*q[0].nd;
        const float len = std::sqrt(a.x*a.x + a.y*a.y + a.z*a.z);
        if (!(dot > 0.99f * len))
            r.windingOk = false;
        // A tangent frame needs the art's coordinates to span both of the quad's axes.
        const float d1u = q[1].tu - q[0].tu, d1v = q[1].tv - q[0].tv;
        const float d2u = q[2].tu - q[0].tu, d2v = q[2].tv - q[0].tv;
        if (std::fabs(d1u*d2v - d1v*d2u) < 1e-6f)
            r.uvOk = false;
        for (int k = 0; k < 4; k++)
        {
            r.minD = std::min(r.minD, q[k].d); r.maxD = std::max(r.maxD, q[k].d);
            r.minU = std::min(r.minU, q[k].u); r.maxU = std::max(r.maxU, q[k].u);
            r.minV = std::min(r.minV, q[k].v); r.maxV = std::max(r.maxV, q[k].v);
        }
    }
    return r;
}

static const rb_relief_rect_t kPattern[] = {
    { 0, 0, 128, 128, 12 },      // the gaps between the plates
    { 1, 1, 126, 62, 0 },        // the upper plate
    { 1, 65, 126, 62, 0 },       // the lower plate
    { 32, 0, 64, 128, 12 },      // the deep middle
    { 21, 14, 11, 7, 12 },       // a niche
    { 22, 15, 9, 1.25f, 4 },     // the bar across it
};
static const int kPatternN = sizeof kPattern / sizeof kPattern[0];

// The depth at a point, the slow way, to check the volume against.
static float expected_volume(float u0, float u1, float v0, float v1)
{
    double sum = 0.0;
    for (float u = u0 + 0.0625f; u < u1; u += 0.125f)
        for (float v = v0 + 0.0625f; v < v1; v += 0.125f)
            sum += rb_relief__depth_at(kPattern, kPatternN, 128, 128, u, v) * 0.125 * 0.125;
    return (float)sum;
}

static void test_pattern(float u0, float u1, float v0, float v1, const char* name)
{
    Sums s;
    char what[160];
    check(RB_ReliefBuild(kPattern, kPatternN, 128, 128, u0, u1, v0, v1, 1, 1, collect, &s) == 1, name);
    const Report r = measure(s);
    std::snprintf(what, sizeof what, "%s: closed along the wall (%.3f)", name, r.sumU);
    check(near(r.sumU, 0), what);
    std::snprintf(what, sizeof what, "%s: closed up and down (%.3f)", name, r.sumUp);
    check(near(r.sumUp, 0), what);
    std::snprintf(what, sizeof what, "%s: out-facing area is the wall's (%.3f)", name, r.outward);
    check(near(r.outward, (u1-u0)*(v1-v0)), what);
    std::snprintf(what, sizeof what, "%s: depth sums to the pattern's (%.1f)", name, r.volume);
    check(std::fabs(r.volume - expected_volume(u0, u1, v0, v1)) < 0.01f * (u1-u0)*(v1-v0), what);
    std::snprintf(what, sizeof what, "%s: nothing stands proud of the wall", name);
    check(r.maxD <= 0.0f, what);
    std::snprintf(what, sizeof what, "%s: nothing passes the deepest rectangle", name);
    check(r.minD >= -12.0f, what);
    std::snprintf(what, sizeof what, "%s: stays inside the wall", name);
    check(r.minU >= u0 && r.maxU <= u1 && r.minV >= v0 && r.maxV <= v1, what);
    std::snprintf(what, sizeof what, "%s: winding matches each normal", name);
    check(r.windingOk, what);
    std::snprintf(what, sizeof what, "%s: every quad has a usable tangent frame", name);
    check(r.uvOk, what);
}

int main()
{
    // A whole tile, then walls cut anywhere, across tiles and at negative offsets.
    test_pattern(0, 128, 0, 128, "one tile");
    test_pattern(10, 390, 20, 300, "several tiles");
    test_pattern(-200, -33, -64, 77, "negative offsets");

    // No rectangles: one flat quad.
    {
        Sums s;
        check(RB_ReliefBuild(kPattern, 0, 128, 128, 0, 64, 0, 64, 1, 1, collect, &s) == 1, "empty list builds");
        check(s.v.size() == 4, "an empty list gives one quad");
    }

    // An uncapped end leaves its side open by exactly the recess it cuts through, so segs along one
    // line do not stand a wall across their recess.
    {
        const rb_relief_rect_t pit = { 0, 0, 128, 128, 10 };
        Sums s;
        RB_ReliefBuild(&pit, 1, 128, 128, 0, 50, 0, 100, 0, 1, collect, &s);
        const Report r = measure(s);
        check(near(r.sumU, 1000.0f) || near(r.sumU, -1000.0f), "an uncapped end leaves its side open");
        Sums t;
        RB_ReliefBuild(&pit, 1, 128, 128, 0, 50, 0, 100, 1, 1, collect, &t);
        check(near(measure(t).sumU, 0), "capping both ends closes it");
    }

    // A side wall faces into its recess, the only way the player can see it.
    {
        const rb_relief_rect_t pit = { 10, 10, 10, 10, 8 };
        Sums s;
        RB_ReliefBuild(&pit, 1, 128, 128, 0, 30, 0, 30, 1, 1, collect, &s);
        int sides = 0;
        for (size_t i = 0; i + 3 < s.v.size(); i += 4)
        {
            const rb_relief_vert_t& v = s.v[i];
            if (v.nd > 0.5f)
                continue;
            sides++;
            if (v.nu != 0.0f)
                check((v.u < 15.0f) == (v.nu > 0.0f), "a side wall along v faces the recess");
            else
                check((v.v < 15.0f) == (v.nv > 0.0f), "a side wall along u faces the recess");
        }
        check(sides == 4, "a pit has four side walls");
    }

    // DOOM-0607: the map may split a wall exactly where its art steps back. The two pieces are
    // open at the join, and together they must still be closed: one of them owns the step's face.
    {
        const rb_relief_rect_t band = { 32, 0, 64, 128, 12 };   // sunk from 32 to 96
        Sums s;
        RB_ReliefBuild(&band, 1, 128, 128, 0, 32, 0, 64, 1, 0, collect, &s);     // up to the step
        RB_ReliefBuild(&band, 1, 128, 128, 32, 128, 0, 64, 0, 1, collect, &s);   // from the step on
        Report r = measure(s);
        check(near(r.sumU, 0) && near(r.sumUp, 0), "a wall split on a step is closed");
        Sums whole;
        RB_ReliefBuild(&band, 1, 128, 128, 0, 128, 0, 64, 1, 1, collect, &whole);
        check(s.v.size() == whole.v.size(), "and has the unsplit wall's faces, no more");
    }

    // A wall too large for the grid, and nonsense bounds, stay flat and emit nothing.
    {
        Sums s;
        check(RB_ReliefBuild(kPattern, kPatternN, 0.01f, 0.01f, 0, 30000, 0, 30000, 1, 1, collect, &s) == 0,
              "an absurd wall is refused");
        check(RB_ReliefBuild(kPattern, kPatternN, 128, 128, 5, 5, 0, 10, 1, 1, collect, &s) == 0,
              "an empty wall is refused");
        check(s.v.empty(), "a refused wall emits nothing");
    }

    // The parser.
    {
        char             name[9];
        rb_relief_rect_t r;
        check(RB_ReliefParseLine("COMPTILE,21,14,11,7,12\n", name, &r) == 1, "a good row parses");
        check(!strcmp(name, "COMPTILE") && r.x == 21 && r.depth == 12, "and reads its fields");
        check(!RB_ReliefParseLine("# a comment\n", name, &r), "a comment is skipped");
        check(!RB_ReliefParseLine("\n", name, &r), "a blank line is skipped");
        check(!RB_ReliefParseLine("COMPTILE,21,14,11\n", name, &r), "a short row is refused");
        check(!RB_ReliefParseLine("COMPTILE,nan,14,11,7,12\n", name, &r), "NaN is refused");
        check(!RB_ReliefParseLine("COMPTILE,21,14,inf,7,12\n", name, &r), "infinity is refused");
        check(!RB_ReliefParseLine("COMPTILE,21,14,-1,7,12\n", name, &r), "a negative width is refused");
        check(!RB_ReliefParseLine("COMPTILE,21,14,11,7,-3\n", name, &r), "a negative depth is refused");
        check(!RB_ReliefParseLine("COMPTILE,21,14,11,7,9999\n", name, &r), "too deep is refused");
    }

    // Movement: unknown specials count as moving, a door moves, a light does not.
    check(RB_ReliefSpecialMoves(1), "a door moves its sector");
    check(RB_ReliefSpecialMoves(9999), "an unknown special counts as moving");
    check(!RB_ReliefSpecialMoves(0), "no special moves nothing");
    check(!RB_ReliefSpecialMoves(12), "a light change moves nothing");

    if (failures)
    {
        std::printf("wall_relief_test: %d failed\n", failures);
        return 1;
    }
    std::printf("wall_relief_test: all passed\n");
    return 0;
}
