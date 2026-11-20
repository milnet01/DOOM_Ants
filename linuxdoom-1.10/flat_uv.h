// flat_uv.h — DOOM-0634: which texel of a flat lands on a point of floor or ceiling.
//
// Classic's R_MapPlane starts a span at ds_xfrac = viewx + ..., ds_yfrac =
// -viewy - ..., and R_DrawSpan reads row (yfrac >> 16) & 63, column
// (xfrac >> 16) & 63. So a flat's columns run east with map x and its rows run
// SOUTH: row 0 sits on the north edge of each 64-unit tile.
//
// The 3D views sample a flat at uv / its size with a repeating sampler, row 0 at
// v = 0. Feeding v = +y ran the rows north, which drew every floor and ceiling
// picture mirrored north to south, the HD art included, since it is read at the
// same coordinates.
//
// Factored out of r_mesh.c so tests/flat_uv_test.cpp can hold the rule with no
// WAD, GPU or Vulkan link (mirrors step_wall.h).
#ifndef FLAT_UV_H
#define FLAT_UV_H

// The texture coordinate, in texels, of map point (x, y) on a floor or ceiling.
static void RB_FlatUV(float x, float y, float* u, float* v)
{
    *u = x;
    *v = -y;
}

#endif
