// seg_project.h — DOOM-0180: put a seg endpoint back on its linedef's exact line.
//
// DOOM stores every vertex as a 16-bit INTEGER, so when the node builder splits a
// linedef the split vertex is ROUNDED to whole units. On an axis-aligned linedef
// that rounds exactly; on a DIAGONAL one it lands off the true line — up to 1.02
// units on E1M1 linedef 193, the pit ledge in the roofed nukage room.
//
// That alone would be harmless. What is not harmless is that the two sides of a
// linedef are split independently, at different points, so the front segs and the
// back segs trace two DIFFERENT polylines between the same pair of endpoints.
// r_mesh.c builds a wall quad on one side's seg and clips the neighbouring floor
// and ceiling caps to the other's, so the two stop short of each other and leave a
// world-space slit — a genuine hole in the mesh, which the tracer sees straight
// through to the sky (bright) and the HITS debug view renders black. That is the
// reported seam, and it is diagonal because only diagonal linedefs can produce it.
//
// Projecting each endpoint onto the linedef's own line makes both sides build on
// one shared line, so the caps and the wall meet again. Factored out here rather
// than left inline in r_mesh.c so tests/seg_project_test.cpp can hold the real
// map numbers against it with no WAD, GPU or Vulkan link (mirrors nee_sampling.h).
#ifndef SEG_PROJECT_H
#define SEG_PROJECT_H

// Project (px,py) onto the infinite line through (ax,ay) along (dx,dy), writing
// the result to (*ox,*oy). Double in, float out: the mesh is float, but map
// coordinates reach ±32768 where a float mantissa is already coarser than the
// sub-unit error being corrected, so the arithmetic itself must not be the
// limit. A zero-length direction (a degenerate linedef) leaves the point alone
// rather than dividing by zero.
static void RB_ProjectOnLine(double ax, double ay, double dx, double dy,
                             double px, double py, float* ox, float* oy)
{
    double q = dx * dx + dy * dy;
    double t;
    if (q <= 0.0) { *ox = (float)px; *oy = (float)py; return; }
    t = ((px - ax) * dx + (py - ay) * dy) / q;
    *ox = (float)(ax + dx * t);
    *oy = (float)(ay + dy * t);
}

#endif
