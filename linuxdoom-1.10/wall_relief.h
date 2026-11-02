// wall_relief.h - DOOM-0541: real depth in a wall, built from a list of sunk rectangles.
//
// A wall's art has rectangles sunk behind its plane. This turns them into faces: a face looking
// out of the wall for each stretch at one depth, and a side wall wherever two depths meet. Nothing
// stands in front of the wall's plane, so no part can get in the player's way.
//
// Header-only and free of engine types, so a test reaches it without the engine. Coordinates are
// the wall's own: u along it, v down it (both in texels, as a wall's UVs are), d out of it, so d is
// never above 0. u, up and out form a right-handed frame, and a quad's corners run counter-clockwise
// seen from the side its normal faces.
#ifndef WALL_RELIEF_H
#define WALL_RELIEF_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RB_RELIEF_MAX_RECTS 256
#define RB_RELIEF_MAX_DEPTH 32.0f   // map units; a wall's far side is rarely closer
#define RB_RELIEF_MAX_CELLS 4000000 // a wall that needs more stays flat

typedef struct
{
    float x, y, w, h;   // texels within one tile; a later rectangle covers an earlier one
    float depth;        // map units behind the wall's plane
} rb_relief_rect_t;

typedef struct
{
    float u, v, d;
    float nu, nv, nd;   // nv follows v, so a face looking up has nv -1
    float tu, tv;       // where the wall's art is sampled
} rb_relief_vert_t;

typedef void (*rb_relief_emit_fn)(const rb_relief_vert_t q[4], void* user);

// One row of relief.csv: name,x,y,w,h,depth. Returns 1 and fills name (9 bytes) and rect, or 0 for
// a comment, a blank line or a bad row. The file is read from DOOMASSETDIR, which a user can point
// anywhere, so every number is range-checked before it is trusted.
static inline int RB_ReliefParseLine(const char* line, char name[9], rb_relief_rect_t* r)
{
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\r' || line[0] == '\0')
        return 0;
    if (sscanf(line, "%8[^,],%f,%f,%f,%f,%f", name, &r->x, &r->y, &r->w, &r->h, &r->depth) != 6)
        return 0;
    // Written so a NaN fails each test and is refused with the rest.
    if (!(r->x >= 0.0f && r->x < 4096.0f && r->y >= 0.0f && r->y < 4096.0f))
        return 0;
    if (!(r->w > 0.0f && r->w <= 4096.0f && r->h > 0.0f && r->h <= 4096.0f))
        return 0;
    return r->depth >= 0.0f && r->depth <= RB_RELIEF_MAX_DEPTH;
}

// Whether a linedef's special can move a sector's floor or ceiling, so a wall on that sector must
// stay a plain quad that follows it. Relief vertices stand still. Anything not known to be harmless
// counts as moving: the cost of a wrong yes is a flat wall.
static inline int RB_ReliefSpecialMoves(int special)
{
    static const short harmless[] = {
        0,                                    // no special
        11, 51, 52, 124,                      // exits
        12, 13, 17, 35, 79, 80, 81, 104, 138, 139,   // light changes
        39, 97, 125, 126,                     // teleports
        48                                    // the scrolling wall
    };
    size_t i;
    for (i = 0; i < sizeof harmless / sizeof harmless[0]; i++)
        if (harmless[i] == special)
            return 0;
    return 1;
}

static inline int rb_relief__cmpf(const void* a, const void* b)
{
    const float x = *(const float*)a, y = *(const float*)b;
    return x < y ? -1 : x > y;
}

// Sorted, without repeats. Returns the count.
static inline int rb_relief__uniq(float* a, int n)
{
    int i, o = 0;
    qsort(a, (size_t)n, sizeof *a, rb_relief__cmpf);
    for (i = 0; i < n; i++)
        if (!o || a[i] - a[o - 1] > 1e-3f)
            a[o++] = a[i];
    return o;
}

// Every place a rectangle's edge falls strictly inside [lo, hi], across the tiles it covers.
static inline int rb_relief__cuts(float* out, const rb_relief_rect_t* r, int n, int vertical,
                                  float tile, float lo, float hi)
{
    int   c = 0, i;
    long  k;
    out[c++] = lo;
    out[c++] = hi;
    for (k = (long)floorf(lo / tile); (float)k * tile < hi; k++)
    {
        const float base = (float)k * tile;
        if (base > lo)
            out[c++] = base;        // a tile's edge: the art's tiling meets here
        for (i = 0; i < n; i++)
        {
            const float e0 = base + (vertical ? r[i].x : r[i].y);
            const float e1 = e0 + (vertical ? r[i].w : r[i].h);
            if (e0 > lo && e0 < hi) out[c++] = e0;
            if (e1 > lo && e1 < hi) out[c++] = e1;
        }
    }
    return rb_relief__uniq(out, c);
}

static inline float rb_relief__depth_at(const rb_relief_rect_t* r, int n, float tw, float th,
                                        float u, float v)
{
    const float tu = u - floorf(u / tw) * tw;
    const float tv = v - floorf(v / th) * th;
    int         i;
    for (i = n - 1; i >= 0; i--)
        if (tu >= r[i].x && tu < r[i].x + r[i].w && tv >= r[i].y && tv < r[i].y + r[i].h)
            return r[i].depth;
    return 0.0f;
}

static inline rb_relief_vert_t rb_relief__v(float u, float v, float d, float nu, float nv, float nd,
                                            float tu, float tv)
{
    rb_relief_vert_t o;
    o.u = u; o.v = v; o.d = d; o.nu = nu; o.nv = nv; o.nd = nd; o.tu = tu; o.tv = tv;
    return o;
}

// A side wall's art: one texel of the shallower side, stretched down the side. Sampling the edge
// alone would give every vertex one texture coordinate across the depth, and the shaders' tangent
// frames need the two axes to differ.
#define RB_RELIEF_SIDE_TEXELS 1.0f

// Builds the wall [u0,u1] x [v0,v1] and passes each quad to emit. capLeft and capRight close the
// wall's ends; leave one off where the next seg carries on along the same line, or a side wall
// stands across the recess. Returns 0 with nothing emitted when the wall is too large or memory ran
// out, and the caller draws it flat.
static inline int RB_ReliefBuild(const rb_relief_rect_t* r, int n, float tw, float th,
                                 float u0, float u1, float v0, float v1,
                                 int capLeft, int capRight, rb_relief_emit_fn emit, void* user)
{
    const long tilesU = (long)ceilf((u1 - u0) / tw) + 2;
    const long tilesV = (long)ceilf((v1 - v0) / th) + 2;
    const long maxU   = 3 + tilesU * (2L * n + 1);
    const long maxV   = 3 + tilesV * (2L * n + 1);
    float*         xs, *ys, *depth;
    unsigned char* done;
    int            nx, ny, i, j;

    if (!(u1 > u0 && v1 > v0 && tw > 0.0f && th > 0.0f) || n < 0 || n > RB_RELIEF_MAX_RECTS
        || maxU > RB_RELIEF_MAX_CELLS / maxV)
        return 0;

    xs = (float*)malloc((size_t)maxU * sizeof *xs);
    ys = (float*)malloc((size_t)maxV * sizeof *ys);
    if (!xs || !ys)
    {
        free(xs);
        free(ys);
        return 0;
    }
    nx = rb_relief__cuts(xs, r, n, 1, tw, u0, u1) - 1;
    ny = rb_relief__cuts(ys, r, n, 0, th, v0, v1) - 1;

    depth = (float*)malloc((size_t)nx * (size_t)ny * sizeof *depth);
    done  = (unsigned char*)calloc((size_t)nx * (size_t)ny, 1);
    if (!depth || !done)
    {
        free(xs); free(ys); free(depth); free(done);
        return 0;
    }
#define RB_RELIEF_D(a, b) depth[(size_t)(b) * (size_t)nx + (size_t)(a)]
    for (j = 0; j < ny; j++)
        for (i = 0; i < nx; i++)
            RB_RELIEF_D(i, j) = rb_relief__depth_at(r, n, tw, th, 0.5f * (xs[i] + xs[i + 1]),
                                                    0.5f * (ys[j] + ys[j + 1]));

    // Faces looking out of the wall, merged into the largest rectangles a greedy pass finds.
    for (j = 0; j < ny; j++)
    {
        for (i = 0; i < nx; i++)
        {
            const float dep = RB_RELIEF_D(i, j);
            int         ie = i + 1, je = j + 1, a, b, same = 1;
            rb_relief_vert_t q[4];
            if (done[(size_t)j * (size_t)nx + (size_t)i])
                continue;
            while (ie < nx && !done[(size_t)j * (size_t)nx + (size_t)ie] && RB_RELIEF_D(ie, j) == dep)
                ie++;
            while (je < ny && same)
            {
                for (a = i; a < ie; a++)
                    if (done[(size_t)je * (size_t)nx + (size_t)a] || RB_RELIEF_D(a, je) != dep)
                        same = 0;
                if (same)
                    je++;
            }
            for (b = j; b < je; b++)
                for (a = i; a < ie; a++)
                    done[(size_t)b * (size_t)nx + (size_t)a] = 1;
            q[0] = rb_relief__v(xs[i],  ys[je], -dep, 0, 0, 1, xs[i],  ys[je]);
            q[1] = rb_relief__v(xs[ie], ys[je], -dep, 0, 0, 1, xs[ie], ys[je]);
            q[2] = rb_relief__v(xs[ie], ys[j],  -dep, 0, 0, 1, xs[ie], ys[j]);
            q[3] = rb_relief__v(xs[i],  ys[j],  -dep, 0, 0, 1, xs[i],  ys[j]);
            emit(q, user);
        }
    }

    // Side walls along each vertical line, joined where a run keeps the same two depths. Beyond a
    // capped end the wall counts as depth 0, so the recess is closed there.
    for (i = 0; i <= nx; i++)
    {
        if ((i == 0 && !capLeft) || (i == nx && !capRight))
            continue;
        for (j = 0; j < ny; )
        {
            const float dl = i > 0  ? RB_RELIEF_D(i - 1, j) : 0.0f;
            const float dr = i < nx ? RB_RELIEF_D(i, j)     : 0.0f;
            int je = j + 1;
            while (je < ny && (i > 0  ? RB_RELIEF_D(i - 1, je) : 0.0f) == dl
                           && (i < nx ? RB_RELIEF_D(i, je)     : 0.0f) == dr)
                je++;
            if (dl != dr)
            {
                const float X = xs[i], deep = -(dl > dr ? dl : dr), shal = -(dl > dr ? dr : dl);
                const float nu = dl > dr ? -1.0f : 1.0f;      // it faces the deeper side
                const float tuS = X - nu * 0.25f, tuD = tuS - nu * RB_RELIEF_SIDE_TEXELS;
                rb_relief_vert_t q[4];
                if (nu > 0.0f)  // up, then out
                {
                    q[0] = rb_relief__v(X, ys[je], deep, nu, 0, 0, tuD, ys[je]);
                    q[1] = rb_relief__v(X, ys[j],  deep, nu, 0, 0, tuD, ys[j]);
                    q[2] = rb_relief__v(X, ys[j],  shal, nu, 0, 0, tuS, ys[j]);
                    q[3] = rb_relief__v(X, ys[je], shal, nu, 0, 0, tuS, ys[je]);
                }
                else            // out, then up
                {
                    q[0] = rb_relief__v(X, ys[je], deep, nu, 0, 0, tuD, ys[je]);
                    q[1] = rb_relief__v(X, ys[je], shal, nu, 0, 0, tuS, ys[je]);
                    q[2] = rb_relief__v(X, ys[j],  shal, nu, 0, 0, tuS, ys[j]);
                    q[3] = rb_relief__v(X, ys[j],  deep, nu, 0, 0, tuD, ys[j]);
                }
                emit(q, user);
            }
            j = je;
        }
    }

    // The same along each horizontal line. The wall's top and bottom are always closed.
    for (j = 0; j <= ny; j++)
    {
        for (i = 0; i < nx; )
        {
            const float da = j > 0  ? RB_RELIEF_D(i, j - 1) : 0.0f;   // the side with the lower v
            const float db = j < ny ? RB_RELIEF_D(i, j)     : 0.0f;
            int ie = i + 1;
            while (ie < nx && (j > 0  ? RB_RELIEF_D(ie, j - 1) : 0.0f) == da
                           && (j < ny ? RB_RELIEF_D(ie, j)     : 0.0f) == db)
                ie++;
            if (da != db)
            {
                const float Y = ys[j], deep = -(da > db ? da : db), shal = -(da > db ? db : da);
                const float nv = da > db ? -1.0f : 1.0f;      // it faces the deeper side
                const float tvS = Y - nv * 0.25f, tvD = tvS - nv * RB_RELIEF_SIDE_TEXELS;
                rb_relief_vert_t q[4];
                if (nv < 0.0f)  // faces up: out, then along
                {
                    q[0] = rb_relief__v(xs[i],  Y, deep, 0, nv, 0, xs[i],  tvD);
                    q[1] = rb_relief__v(xs[i],  Y, shal, 0, nv, 0, xs[i],  tvS);
                    q[2] = rb_relief__v(xs[ie], Y, shal, 0, nv, 0, xs[ie], tvS);
                    q[3] = rb_relief__v(xs[ie], Y, deep, 0, nv, 0, xs[ie], tvD);
                }
                else            // faces down: along, then out
                {
                    q[0] = rb_relief__v(xs[i],  Y, deep, 0, nv, 0, xs[i],  tvD);
                    q[1] = rb_relief__v(xs[ie], Y, deep, 0, nv, 0, xs[ie], tvD);
                    q[2] = rb_relief__v(xs[ie], Y, shal, 0, nv, 0, xs[ie], tvS);
                    q[3] = rb_relief__v(xs[i],  Y, shal, 0, nv, 0, xs[i],  tvS);
                }
                emit(q, user);
            }
            i = ie;
        }
    }
#undef RB_RELIEF_D
    free(xs); free(ys); free(depth); free(done);
    return 1;
}

#endif // WALL_RELIEF_H
