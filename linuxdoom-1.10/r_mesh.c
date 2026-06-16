// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2026 DOOM_Ants contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//    DOOM-0008 Stage 1 geometry conversion (see r_mesh.h). Walls come from
//    segs, floor/ceiling caps from subsectors. Sky-flat surfaces are left out
//    (a ray miss into the sky environment, not solid geometry).
//
//-----------------------------------------------------------------------------

#include <stdlib.h>
#include <math.h>

#include "doomtype.h"
#include "m_fixed.h"
#include "r_defs.h"
#include "r_state.h"    // segs/subsectors/sectors + counts
#include "doomstat.h"   // skyflatnum
#include "i_system.h"   // I_Error
#include "r_mesh.h"

//
// Growable vertex array.
//
typedef struct
{
    rb_vertex_t* v;
    int          count;
    int          cap;
} builder_t;

static void push_vert(builder_t* b, rb_vertex_t vert)
{
    if (b->count == b->cap)
    {
        b->cap = b->cap ? b->cap * 2 : 8192;
        b->v = realloc(b->v, b->cap * sizeof(rb_vertex_t));
        if (!b->v)
            I_Error("RB_BuildLevelMesh: out of memory at %d vertices", b->count);
    }
    b->v[b->count++] = vert;
}

static rb_vertex_t mkv(float x, float y, float z,
                       float nx, float ny, float nz,
                       float u, float v, int texnum, int flags, float light)
{
    rb_vertex_t r;
    r.x = x;   r.y = y;   r.z = z;
    r.nx = nx; r.ny = ny; r.nz = nz;
    r.u = u;   r.v = v;
    r.texnum = texnum; r.flags = flags; r.light = light;
    return r;
}

static void push_tri(builder_t* b, rb_vertex_t a, rb_vertex_t c, rb_vertex_t d)
{
    push_vert(b, a); push_vert(b, c); push_vert(b, d);
}

// Quad corners given counter-clockwise; split into two triangles.
static void push_quad(builder_t* b, rb_vertex_t a, rb_vertex_t c,
                      rb_vertex_t d, rb_vertex_t e)
{
    push_tri(b, a, c, d);
    push_tri(b, a, d, e);
}

//
// One vertical wall quad between bottomz and topz along the seg.
//
static void emit_wall(builder_t* bld, seg_t* seg, fixed_t bottomz, fixed_t topz,
                      int texnum, int flags)
{
    vertex_t* v1;
    vertex_t* v2;
    float x1, y1, x2, y2, zb, zt;
    float dx, dy, len, nx, ny;
    float u0, u1, vtop, vbot, light;
    rb_vertex_t bl, br, tr, tl;

    if (topz <= bottomz)   return;   // no vertical span
    if (texnum <= 0)       return;   // "-" / no texture: nothing to draw here

    v1 = seg->v1;
    v2 = seg->v2;
    x1 = v1->x / (float)FRACUNIT; y1 = v1->y / (float)FRACUNIT;
    x2 = v2->x / (float)FRACUNIT; y2 = v2->y / (float)FRACUNIT;
    zb = bottomz / (float)FRACUNIT; zt = topz / (float)FRACUNIT;

    // Front-facing normal: the seg's front sector is on the right-hand side
    // walking v1->v2, i.e. the direction rotated -90deg => (dy, -dx).
    dx = x2 - x1; dy = y2 - y1;
    len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;         // degenerate seg
    nx = dy / len; ny = -dx / len;

    // UVs in DOOM texels (shader divides by texture size). U runs along the
    // wall from the seg + sidedef offset; V runs down from the quad top.
    u0   = (seg->offset + seg->sidedef->textureoffset) / (float)FRACUNIT;
    u1   = u0 + len;
    vtop = seg->sidedef->rowoffset / (float)FRACUNIT;
    vbot = vtop + (zt - zb);
    light = seg->frontsector->lightlevel / 255.0f;

    bl = mkv(x1, y1, zb, nx, ny, 0.0f, u0, vbot, texnum, flags, light);
    br = mkv(x2, y2, zb, nx, ny, 0.0f, u1, vbot, texnum, flags, light);
    tr = mkv(x2, y2, zt, nx, ny, 0.0f, u1, vtop, texnum, flags, light);
    tl = mkv(x1, y1, zt, nx, ny, 0.0f, u0, vtop, texnum, flags, light);
    push_quad(bld, bl, br, tr, tl);
}

//
// Floor or ceiling cap for one subsector: fan its segs' v1 points.
//  up != 0  -> floor (normal +z, CCW from above);  else ceiling (normal -z).
//
static void emit_cap(builder_t* bld, seg_t* segp, int n, fixed_t height,
                     int up, int flatnum, float light)
{
    float z  = height / (float)FRACUNIT;
    float nz = up ? 1.0f : -1.0f;
    vertex_t* p0;
    float px0, py0;
    rb_vertex_t pivot;
    int k;

    p0  = segp[0].v1;
    px0 = p0->x / (float)FRACUNIT; py0 = p0->y / (float)FRACUNIT;
    // Flats tile on the fixed 64x64 world grid -> world-xy texel coords.
    pivot = mkv(px0, py0, z, 0.0f, 0.0f, nz, px0, py0, flatnum, RB_MESH_FLAT, light);

    for (k = 1; k < n - 1; k++)
    {
        vertex_t* pa = segp[k].v1;
        vertex_t* pb = segp[k + 1].v1;
        float ax = pa->x / (float)FRACUNIT, ay = pa->y / (float)FRACUNIT;
        float bx = pb->x / (float)FRACUNIT, by = pb->y / (float)FRACUNIT;
        rb_vertex_t va = mkv(ax, ay, z, 0.0f, 0.0f, nz, ax, ay, flatnum, RB_MESH_FLAT, light);
        rb_vertex_t vb = mkv(bx, by, z, 0.0f, 0.0f, nz, bx, by, flatnum, RB_MESH_FLAT, light);
        if (up)
            push_tri(bld, pivot, va, vb);
        else
            push_tri(bld, pivot, vb, va);   // flip winding so -z faces down
    }
}

rb_mesh_t* RB_BuildLevelMesh(void)
{
    builder_t bld = { 0 };
    rb_mesh_t* mesh;
    int i;

    // Walls.
    for (i = 0; i < numsegs; i++)
    {
        seg_t*    seg   = &segs[i];
        side_t*   side  = seg->sidedef;
        sector_t* front = seg->frontsector;
        sector_t* back  = seg->backsector;

        if (!side || !front)
            continue;

        if (!back)
        {
            // One-sided solid wall: full floor..ceiling, mid texture.
            emit_wall(&bld, seg, front->floorheight,
                      front->ceilingheight, side->midtexture, 0);
            continue;
        }

        // Upper step (front ceiling higher than back). Skipped when the front
        // ceiling is sky: that region renders as sky, not the top texture.
        if (front->ceilingheight > back->ceilingheight
            && front->ceilingpic != skyflatnum)
            emit_wall(&bld, seg, back->ceilingheight, front->ceilingheight,
                      side->toptexture, 0);

        // Lower step (front floor lower than back).
        if (front->floorheight < back->floorheight)
            emit_wall(&bld, seg, front->floorheight, back->floorheight,
                      side->bottomtexture, 0);

        // Middle (rails/grates): alpha-tested, spans the shared opening.
        if (side->midtexture)
        {
            fixed_t zb = front->floorheight > back->floorheight
                       ? front->floorheight : back->floorheight;
            fixed_t zt = front->ceilingheight < back->ceilingheight
                       ? front->ceilingheight : back->ceilingheight;
            emit_wall(&bld, seg, zb, zt, side->midtexture, RB_MESH_MASKED);
        }
    }

    // Floor/ceiling caps.
    for (i = 0; i < numsubsectors; i++)
    {
        subsector_t* ss  = &subsectors[i];
        sector_t*    sec = ss->sector;
        seg_t*       segp;
        float        light;

        if (!sec || ss->numlines < 3)
            continue;

        segp  = &segs[ss->firstline];
        light = sec->lightlevel / 255.0f;

        if (sec->floorpic != skyflatnum)
            emit_cap(&bld, segp, ss->numlines, sec->floorheight, 1,
                     sec->floorpic, light);
        if (sec->ceilingpic != skyflatnum)
            emit_cap(&bld, segp, ss->numlines, sec->ceilingheight, 0,
                     sec->ceilingpic, light);
    }

    mesh = malloc(sizeof(rb_mesh_t));
    if (!mesh)
        I_Error("RB_BuildLevelMesh: out of memory allocating mesh handle");
    mesh->verts    = bld.v;
    mesh->numverts = bld.count;
    mesh->numtris  = bld.count / 3;
    return mesh;
}

void RB_FreeMesh(rb_mesh_t* mesh)
{
    if (!mesh)
        return;
    free(mesh->verts);
    free(mesh);
}
