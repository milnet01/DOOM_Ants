// sky_vis.h — DOOM-0651: how much outdoor air one cell of fog can see.
//
// The traced fog lights roofed air with the sky's share, graded by the seep distance:
// how far the air is from an opening along open space. That says nothing about sight. A
// corridor that bends away from a door is near the door by path, sees none of it, and its
// fog glowed grey with every light off.
//
// So each roofed cell also gets a visibility. Cast RB_SKYVIS_DIRS directions across the
// floor plan, walk each to the first cell of outdoor air within RB_SKYVIS_RANGE, and ask
// the caller's sight test whether that cell can be seen. A spot in a doorway or under a
// roof edge faces outdoors in at most half its directions, so the fraction is doubled and
// capped at 1: an opening keeps its full grade, and air with no line of sight to outdoor
// air reads 0.
//
// The walk does not stop at a wall cell. A cell's centre can land in a wall beside a
// doorway that is open, and stopping there would hide the door; the sight test decides.
//
// Apart from the engine so a test can reach it; the engine passes the seep grid's
// outdoor-air lookup and P_CheckSightTrace.
#ifndef SKY_VIS_H
#define SKY_VIS_H

#include <math.h>

#define RB_SKYVIS_DIRS   32         // 11.25 degrees apart
#define RB_SKYVIS_RANGE  1536.0f    // 4 seep falloffs: past it the seep grade is under 2 %

// 1 when cell (ix, iy) is open-sky air a sight line can end in.
typedef int (*rb_skyvis_outdoor_fn)(void* ctx, int ix, int iy);
// 1 when the point (x, y), at the asking cell's test height, can see cell (ix, iy)'s air.
typedef int (*rb_skyvis_sight_fn)(void* ctx, float x, float y, int ix, int iy);

static inline float RB_SkyVisCell(int ix, int iy, int w, int h, float cell,
                                  float originX, float originY,
                                  rb_skyvis_outdoor_fn outdoor,
                                  rb_skyvis_sight_fn sight, void* ctx)
{
    const float x    = originX + ix * cell;
    const float y    = originY + iy * cell;
    const float step = 0.5f * cell;           // half a cell: a diagonal cannot skip one
    int seen = 0, k;
    float v;

    for (k = 0; k < RB_SKYVIS_DIRS; k++)
    {
        const float a  = (k + 0.5f) * (6.2831853f / RB_SKYVIS_DIRS);
        const float dx = cosf(a), dy = sinf(a);
        int   lastX = ix, lastY = iy;
        float t;
        for (t = step; t <= RB_SKYVIS_RANGE; t += step)
        {
            const int cx = (int)floorf((x + dx * t - originX) / cell + 0.5f);
            const int cy = (int)floorf((y + dy * t - originY) / cell + 0.5f);
            if (cx < 0 || cy < 0 || cx >= w || cy >= h)
                break;                        // off the grid: no outdoor air this way
            if (cx == lastX && cy == lastY)
                continue;
            lastX = cx;
            lastY = cy;
            if (!outdoor(ctx, cx, cy))
                continue;
            // Only the FIRST outdoor cell is tested, so a direction costs one sight test
            // at most.
            if (sight(ctx, x, y, cx, cy))
                seen++;
            break;
        }
    }
    v = 2.0f * (float)seen / (float)RB_SKYVIS_DIRS;
    return v < 1.0f ? v : 1.0f;
}

#endif
