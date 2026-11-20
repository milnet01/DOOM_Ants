// flat_uv_test.cpp — DOOM-0634: floors and ceilings mirrored north to south in the 3D views.
//
// Photographed 2026-11-20 at E3M5's skull pit (sector 158, FLAT5_6), camera
// 1568,600 facing north: Classic drew each skull's crown to the north, Solid drew
// it to the south.
//
// Contract: for any map point, the texel the 3D views read through RB_FlatUV is
// the texel Classic reads.
//
// The two sides are computed differently on purpose. Classic's side is its own
// fixed-point arithmetic, copied from R_MapPlane (the sign of ds_yfrac) and
// R_DrawSpan (the spot index). The 3D side is what a repeating sampler does with
// uv / 64: the fraction of the coordinate, floored to a texel.
#include <cmath>
#include <cstdio>

#include "../flat_uv.h"
#include "check_util.h"

namespace {

// Classic: index into the 64x64 flat for map point (x, y), given in 16.16 fixed
// point as R_MapPlane holds it.
int classic_spot(double x, double y)
{
    const int xfrac = (int)std::floor(x * 65536.0);
    const int yfrac = (int)std::floor(-y * 65536.0);   // ds_yfrac = -viewy - ...
    return ((yfrac >> (16 - 6)) & (63 * 64)) + ((xfrac >> 16) & 63);
}

// The 3D views: a repeating sampler over a 64x64 image, row 0 at v = 0.
int mesh_spot(double x, double y)
{
    float u, v;
    RB_FlatUV((float)x, (float)y, &u, &v);
    const double fu = u / 64.0 - std::floor(u / 64.0);
    const double fv = v / 64.0 - std::floor(v / 64.0);
    return (int)std::floor(fv * 64.0) * 64 + (int)std::floor(fu * 64.0);
}

struct Point { double x, y; const char* what; };

// Texel centres, so neither side sits on a rounding edge.
const Point kPoints[] = {
    { 1568.5,   800.5, "E3M5 skull pit, sector 158 (the photographed floor)" },
    { 1472.5,   895.5, "the pit's north-west corner: the tile's first row" },
    { 1472.5,   704.5, "the pit's south-west corner: the tile's last row" },
    { 1056.5, -3615.5, "E1M1, negative y" },
    {  -63.5,    10.5, "negative x" },
    {  -10.5,   -10.5, "both negative" },
    {    0.5,     0.5, "the origin tile" },
};

} // namespace

int main()
{
    for (const Point& p : kPoints)
    {
        char what[160];
        std::snprintf(what, sizeof what, "the 3D views read Classic's texel at %s", p.what);
        check_eq_int(mesh_spot(p.x, p.y), classic_spot(p.x, p.y), what);
    }

    // The rule in words, so a failure above reads plainly: one unit further north
    // is one row EARLIER in the flat.
    check_eq_int(classic_spot(0.5, 1.5) / 64, 62, "Classic: y = 1.5 reads row 62");
    check_eq_int(classic_spot(0.5, 0.5) / 64, 63, "Classic: y = 0.5 reads row 63");

    return check_summary("flat_uv_test");
}
