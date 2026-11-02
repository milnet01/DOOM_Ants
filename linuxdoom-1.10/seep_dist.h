// seep_dist.h — DOOM-0536: how far a seep cell is from an opening.
//
// Apart from RB_BuildSeepField() so a test can reach it without the engine.
//
// It measures to the nearest point on the opening, not its midpoint. Measured to the
// midpoint, air right beside a long roof edge read as deep indoors everywhere but the
// edge's middle, and the fog stopped at the roof line in a bright straight wall.
#ifndef SEEP_DIST_H
#define SEEP_DIST_H

#include <math.h>

static inline float RB_SeepPointSegDist(float px, float py,
                                        float ax, float ay, float bx, float by)
{
    const float abx = bx - ax, aby = by - ay;
    const float len2 = abx * abx + aby * aby;
    float t = 0.0f, dx, dy;
    if (len2 > 0.0f)        // a zero-length seg is its one point
    {
        t = ((px - ax) * abx + (py - ay) * aby) / len2;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
    }
    dx = ax + t * abx - px;
    dy = ay + t * aby - py;
    return sqrtf(dx * dx + dy * dy);
}

#endif
