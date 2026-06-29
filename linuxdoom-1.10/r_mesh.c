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
#include <string.h>
#include <math.h>

#include "doomtype.h"
#include "doomdef.h"    // SCREENHEIGHT (view-window -> NDC for the weapon)
#include "m_fixed.h"
#include "doomdata.h"   // NF_SUBSECTOR (BSP child = leaf flag)
#include "r_defs.h"
#include "r_state.h"    // segs/subsectors/sectors/nodes + counts, firstflat, textureheight
#include "r_data.h"     // R_GetColumn
#include "r_main.h"     // R_PointToAngle2 (sprite rotation pick)
#include "doomstat.h"   // skyflatnum, players, consoleplayer
#include "p_mobj.h"     // mobj_t (the things billboarded as sprites)
#include "p_pspr.h"     // FF_FRAMEMASK / FF_FULLBRIGHT
#include "tables.h"     // ANG45
#include "m_swap.h"     // SHORT / LONG (sprite patch headers)
#include "i_system.h"   // I_Error
#include "w_wad.h"      // W_CacheLumpNum/Name
#include "z_zone.h"     // PU_CACHE
#include "r_mesh.h"

// Texture geometry tables from r_data.c (no public header declares them).
// texturewidthmask+1 is the wall's tiling width (matches how R_GetColumn masks
// column lookups — non-power-of-two textures tile at this width, exactly as the
// software renderer shows them); textureheight is the wall's pixel height.
extern int*     texturewidthmask;
extern int      numtextures;
extern int      numflats;

//
// Growable vertex array.
//
typedef struct
{
    rb_vertex_t* v;
    int          count;
    int          cap;
    // Per-triangle subsector id (DOOM-0009 step 4c). push_tri stamps the current
    // subsector (cur_ss) onto each triangle so a ray hit can locate its room's GI
    // probe. Parallel to the triangle list (one entry per 3 verts).
    int*         ssid;
    int          ssidcount;
    int          ssidcap;
    int          cur_ss;
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

static void push_ssid(builder_t* b, int ss)
{
    if (b->ssidcount == b->ssidcap)
    {
        b->ssidcap = b->ssidcap ? b->ssidcap * 2 : 4096;
        b->ssid = realloc(b->ssid, b->ssidcap * sizeof(int));
        if (!b->ssid)
            I_Error("RB_BuildLevelMesh: out of memory at %d tri-ids", b->ssidcount);
    }
    b->ssid[b->ssidcount++] = ss;
}

static rb_vertex_t mkv(float x, float y, float z,
                       float nx, float ny, float nz,
                       float u, float v, int texnum, int flags, float light)
{
    rb_vertex_t out;
    out.x = x;   out.y = y;   out.z = z;
    out.nx = nx; out.ny = ny; out.nz = nz;
    out.u = u;   out.v = v;
    out.texnum = texnum; out.flags = flags; out.light = light;
    out.vsector = 0; out.vplane = RB_PLANE_NONE;   // static unless tagged below
    out.vtexsec = 0; out.vtexplane = RB_PLANE_NONE; out.vtexoff = 0.0f;
    out.vtexside = -1; out.vtexslot = 0;           // flat/static unless emit_wall tags it
    return out;
}

static void push_tri(builder_t* b, rb_vertex_t a, rb_vertex_t c, rb_vertex_t d)
{
    push_vert(b, a); push_vert(b, c); push_vert(b, d);
    push_ssid(b, b->cur_ss);   // tag this triangle with the subsector being emitted
}

// Quad corners given counter-clockwise; split into two triangles.
static void push_quad(builder_t* b, rb_vertex_t a, rb_vertex_t c,
                      rb_vertex_t d, rb_vertex_t e)
{
    push_tri(b, a, c, d);
    push_tri(b, a, d, e);
}

// Wall pegging kind: selects which DOOM vertical-alignment rule emit_wall
// applies (faithful to r_segs.c). One-sided mids and two-sided masked rails
// share a rule; uppers and lowers each have their own.
enum { PEG_ONESIDED, PEG_UPPER, PEG_LOWER, PEG_MID };

//
// One vertical wall quad between bottomz and topz along the seg.
//
// botsec/botplane and topsec/topplane tag which moving sector plane the quad's
// bottom and top edges follow, so doors/lifts can be re-heighted per frame
// (DOOM-0049). The bottom edge sits at bottomz (== that sector plane's height
// at build time); the top edge at topz.
static void emit_wall(builder_t* bld, seg_t* seg, fixed_t bottomz, fixed_t topz,
                      int texnum, int flags, int pegkind,
                      int botsec, int botplane, int topsec, int topplane)
{
    vertex_t* v1;
    vertex_t* v2;
    float x1, y1, x2, y2, zb, zt;
    float dx, dy, len, nx, ny;
    float u0, u1, vtop, vbot, light, vtexoff;
    int   anchorsec, anchorplane;
    rb_vertex_t bl, br, tr, tl;

    // Keep zero-height walls (topz == bottomz): a closed door/lift sector
    // collapses its own walls to zero, but those walls must exist in the mesh so
    // the DOOM-0049 per-frame re-height can grow them as the sector opens --
    // otherwise the doorway side (the jamb/track) is a void hole (DOOM-0052).
    // A zero-height quad is degenerate (the GPU draws nothing) until it grows.
    // Still drop inverted spans (topz < bottomz) and untextured walls.
    if (topz < bottomz)    return;   // inverted: skip
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

    // U (horizontal) in DOOM texels: along the wall from the seg + sidedef
    // offset. The shader divides by texture size; REPEAT wraps.
    u0 = (seg->offset + seg->sidedef->textureoffset) / (float)FRACUNIT;
    u1 = u0 + len;

    // V (vertical) origin: faithfully port DOOM's wall pegging (r_segs.c). The
    // texture's top row sits at a world height that depends on the wall kind and
    // the linedef's ML_DONTPEG* flags. Without this every default upper texture
    // (DOOM bottom-pegs them) and every DONTPEGBOTTOM switch/step face samples
    // the wrong rows, sliding its art out of the visible band -- e.g. a wall
    // switch whose button graphic scrolls off. vtop is the texel V at the quad's
    // top edge; 1 world unit == 1 texel, and the shader REPEAT-wraps the rest.
    {
        float texH = textureheight[texnum] / (float)FRACUNIT;
        float h    = zt - zb;
        int   lf   = seg->linedef->flags;
        // vtop = texel V at the top edge; anchorsec/anchorplane = the sector
        // plane the texture is pegged to (the top edge, the bottom edge, or --
        // for a DONTPEGBOTTOM lower -- the front ceiling). On a door/lift that
        // anchor plane slides and the texture rides with it (DOOM-0067).
        switch (pegkind)
        {
          case PEG_UPPER:   // default: texture bottom at the lower (back) ceiling
            if (lf & ML_DONTPEGTOP) { vtop = 0.0f;     anchorsec = topsec; anchorplane = topplane; }
            else                    { vtop = texH - h; anchorsec = botsec; anchorplane = botplane; }
            break;
          case PEG_LOWER:   // default: texture top at the step top (back floor)
            if (lf & ML_DONTPEGBOTTOM)
            {
                vtop = seg->frontsector->ceilingheight / (float)FRACUNIT - zt;
                anchorsec = botsec; anchorplane = RB_PLANE_CEIL;   // front ceiling
            }
            else { vtop = 0.0f; anchorsec = topsec; anchorplane = topplane; }
            break;
          default:          // PEG_ONESIDED / PEG_MID: default texture top at top
            if (lf & ML_DONTPEGBOTTOM) { vtop = texH - h; anchorsec = botsec; anchorplane = botplane; }
            else                       { vtop = 0.0f;     anchorsec = topsec; anchorplane = topplane; }
            break;
        }
        vtop += seg->sidedef->rowoffset / (float)FRACUNIT;
        // Texture row 0 sits at world height (vtop + zt) in texels(==world
        // units); store it relative to the anchor plane's build height so
        // RB_UpdateMeshHeights can re-derive v from the live anchor height.
        {
            float anchorz = (anchorplane == RB_PLANE_CEIL
                             ? sectors[anchorsec].ceilingheight
                             : sectors[anchorsec].floorheight) / (float)FRACUNIT;
            vtexoff = (vtop + zt) - anchorz;
        }
    }
    vbot = vtop + (zt - zb);
    light = seg->frontsector->lightlevel / 255.0f;

    bl = mkv(x1, y1, zb, nx, ny, 0.0f, u0, vbot, texnum, flags, light);
    br = mkv(x2, y2, zb, nx, ny, 0.0f, u1, vbot, texnum, flags, light);
    tr = mkv(x2, y2, zt, nx, ny, 0.0f, u1, vtop, texnum, flags, light);
    tl = mkv(x1, y1, zt, nx, ny, 0.0f, u0, vtop, texnum, flags, light);
    // Bottom edge follows botsec/botplane, top edge follows topsec/topplane.
    bl.vsector = br.vsector = botsec; bl.vplane = br.vplane = botplane;
    tr.vsector = tl.vsector = topsec; tr.vplane = tl.vplane = topplane;
    bl.vtexsec   = br.vtexsec   = tr.vtexsec   = tl.vtexsec   = anchorsec;
    bl.vtexplane = br.vtexplane = tr.vtexplane = tl.vtexplane = anchorplane;
    bl.vtexoff   = br.vtexoff   = tr.vtexoff   = tl.vtexoff   = vtexoff;
    // Tag the live-texture source so switches/animations refresh per frame
    // (DOOM-0066). Slot follows the pegkind: upper=top, lower=bottom, one-sided
    // and two-sided rail both draw the midtexture.
    {
        int side = (int)(seg->sidedef - sides);
        int slot = pegkind == PEG_UPPER ? 0 : pegkind == PEG_LOWER ? 2 : 1;
        bl.vtexside = br.vtexside = tr.vtexside = tl.vtexside = side;
        bl.vtexslot = br.vtexslot = tr.vtexslot = tl.vtexslot = slot;
    }
    push_quad(bld, bl, br, tr, tl);
}

//
// Floor/ceiling caps via BSP carve.
//
// A subsector is a convex BSP leaf, but only PART of its boundary is made of
// segs -- the remaining edges are invisible BSP partition lines that carry no
// seg, and some subsectors have fewer than three segs at all. Fanning seg
// endpoints (or their convex hull) therefore leaves the partition edges open,
// showing the sky backdrop through the floor/ceiling, and drops the seg-poor
// subsectors entirely. The exact convex cell of every leaf is instead recovered
// the canonical way (as GZDoom/Eternity build flat polygons): start from a
// map-sized quad and clip it by each ancestor partition line on the way down
// the node tree. Every subsector seg lies on one of those ancestor partitions,
// so the carved polygon reproduces the full floor/ceiling outline with no gaps.
//

enum { POLYMAX = 64 };
typedef struct { int n; float x[POLYMAX], y[POLYMAX]; } poly_t;

// Clip `in` to one side of the line through (ox,oy) with direction (dx,dy).
// keepRight != 0 keeps the front/right half-plane (DOOM's side 0, where a seg's
// front sector sits); else the back/left (side 1). Sutherland-Hodgman; boundary
// points are kept on both sides so the two children share the cut edge exactly.
static poly_t clip_poly(const poly_t* in, float ox, float oy,
                        float dx, float dy, int keepRight)
{
    poly_t out;
    float  sgn = keepRight ? 1.0f : -1.0f;
    int    i;
    out.n = 0;
    for (i = 0; i < in->n; i++)
    {
        int   j  = (i + 1 == in->n) ? 0 : i + 1;
        float fi = sgn * (dx * (in->y[i] - oy) - dy * (in->x[i] - ox));
        float fj = sgn * (dx * (in->y[j] - oy) - dy * (in->x[j] - ox));
        int   ini = fi <= 0.0f, inj = fj <= 0.0f;   // inside == front half-plane
        if (ini && out.n < POLYMAX)
        { out.x[out.n] = in->x[i]; out.y[out.n] = in->y[i]; out.n++; }
        if (ini != inj && out.n < POLYMAX)          // edge crosses: add the hit
        {
            float t = fi / (fi - fj);
            out.x[out.n] = in->x[i] + t * (in->x[j] - in->x[i]);
            out.y[out.n] = in->y[i] + t * (in->y[j] - in->y[i]);
            out.n++;
        }
    }
    return out;
}

// Triangulate a carved cell as a fan at `height`, flats tiling on the fixed
// 64x64 world grid (world-xy texel coords). up != 0 -> floor (normal +z, CCW
// from above); else ceiling (normal -z, reversed winding).
static void emit_cap_poly(builder_t* bld, const poly_t* p, fixed_t height,
                          int up, int flatnum, float light,
                          int secidx, int plane)
{
    float z  = height / (float)FRACUNIT;
    float nz = up ? 1.0f : -1.0f;
    rb_vertex_t pivot;
    int k;
    if (p->n < 3) return;
    pivot = mkv(p->x[0], p->y[0], z, 0.0f, 0.0f, nz, p->x[0], p->y[0], flatnum, RB_MESH_FLAT, light);
    pivot.vsector = secidx; pivot.vplane = plane;
    for (k = 1; k < p->n - 1; k++)
    {
        rb_vertex_t va = mkv(p->x[k],   p->y[k],   z, 0.0f, 0.0f, nz, p->x[k],   p->y[k],   flatnum, RB_MESH_FLAT, light);
        rb_vertex_t vb = mkv(p->x[k+1], p->y[k+1], z, 0.0f, 0.0f, nz, p->x[k+1], p->y[k+1], flatnum, RB_MESH_FLAT, light);
        va.vsector = vb.vsector = secidx; va.vplane = vb.vplane = plane;
        if (up) push_tri(bld, pivot, va, vb);
        else    push_tri(bld, pivot, vb, va);   // flip winding so -z faces down
    }
}

// Emit a subsector's floor and ceiling from its carved convex cell (sky flats
// are skipped: those openings render as the sky backdrop, not a flat).
static void emit_subsector_caps(builder_t* bld, int ssnum, const poly_t* cell)
{
    subsector_t* ss  = &subsectors[ssnum];
    sector_t*    sec = ss->sector;
    float        light;
    int          secidx, i;
    poly_t       clipped;
    if (!sec || cell->n < 3) return;

    bld->cur_ss = ssnum;   // tag this subsector's floor/ceiling caps (step 4c)

    // The partition-only carve reproduces the convex BSP leaf, but a seg is not a
    // BSP partition: where a wall bounds the cell nothing trims it, so the cell
    // overshoots past the wall -- out to the map-box edge at a one-sided wall, or
    // into the neighbour past a two-sided one. Classic never overshoots (its
    // visplane spans are clipped to each seg's on-screen extent), so the stray cap
    // shows only in 3D, floating past walls in open views (DOOM-0065). Trim the
    // cell to every seg's front (right) half-plane: each seg is an edge of the
    // convex leaf, so the sector lies entirely on its front and clipping removes
    // only the overshoot, never valid interior. Two-sided neighbours clip to the
    // same shared line from opposite sides, so their caps still meet exactly (any
    // height step between them is covered by the wall quad on that seg).
    clipped = *cell;
    for (i = 0; i < ss->numlines; i++)
    {
        seg_t* sg = &segs[ss->firstline + i];
        float  ox = sg->v1->x / (float)FRACUNIT, oy = sg->v1->y / (float)FRACUNIT;
        float  dx = (sg->v2->x - sg->v1->x) / (float)FRACUNIT;
        float  dy = (sg->v2->y - sg->v1->y) / (float)FRACUNIT;
        clipped = clip_poly(&clipped, ox, oy, dx, dy, 1);   // keep front (sector) side
        if (clipped.n < 3) return;                          // trimmed to nothing
    }

    light  = sec->lightlevel / 255.0f;
    secidx = (int)(sec - sectors);   // for the per-frame dynamic-height update
    if (sec->floorpic != skyflatnum)
        emit_cap_poly(bld, &clipped, sec->floorheight, 1, sec->floorpic, light,
                      secidx, RB_PLANE_FLOOR);
    if (sec->ceilingpic != skyflatnum)
        emit_cap_poly(bld, &clipped, sec->ceilingheight, 0, sec->ceilingpic, light,
                      secidx, RB_PLANE_CEIL);
}

// Walk the BSP, carrying the convex cell clipped by every ancestor partition.
// children[0] is the front/right half-space, children[1] the back/left (DOOM's
// R_PointOnSide convention); at a leaf the carried cell is the subsector's
// exact floor/ceiling outline.
static void carve_caps(builder_t* bld, int nodenum, poly_t poly)
{
    node_t* nd;
    float   ox, oy, dx, dy;
    if (poly.n < 3)
        return;
    if (nodenum & NF_SUBSECTOR)
    {
        emit_subsector_caps(bld, nodenum & ~NF_SUBSECTOR, &poly);
        return;
    }
    nd = &nodes[nodenum];
    ox = nd->x  / (float)FRACUNIT; oy = nd->y  / (float)FRACUNIT;
    dx = nd->dx / (float)FRACUNIT; dy = nd->dy / (float)FRACUNIT;
    carve_caps(bld, nd->children[0], clip_poly(&poly, ox, oy, dx, dy, 1));
    carve_caps(bld, nd->children[1], clip_poly(&poly, ox, oy, dx, dy, 0));
}

rb_mesh_t* RB_BuildLevelMesh(void)
{
    builder_t bld = { 0 };
    rb_mesh_t* mesh;
    int* seg2ss;
    int i, s, k;

    bld.cur_ss = -1;   // untagged until a wall/cap sets it (step 4c)

    // Reverse map seg -> subsector for the per-triangle GI tag: subsectors own
    // contiguous seg ranges (firstline..firstline+numlines), so invert that once.
    // A wall's lit face is in its seg's subsector, so that's the probe to sample.
    seg2ss = malloc(numsegs * sizeof(int));
    if (!seg2ss)
        I_Error("RB_BuildLevelMesh: out of memory for seg->subsector map");
    for (i = 0; i < numsegs; i++) seg2ss[i] = -1;
    for (s = 0; s < numsubsectors; s++)
        for (k = 0; k < subsectors[s].numlines; k++)
            seg2ss[subsectors[s].firstline + k] = s;

    // Walls.
    for (i = 0; i < numsegs; i++)
    {
        seg_t*    seg   = &segs[i];
        side_t*   side  = seg->sidedef;
        sector_t* front = seg->frontsector;
        sector_t* back  = seg->backsector;

        if (!side || !front)
            continue;

        bld.cur_ss = seg2ss[i];   // tag this seg's wall quads (step 4c)

        // Sector indices for the per-frame dynamic-height update (DOOM-0049):
        // each wall edge is tagged with the sector + plane whose height set it.
        int fi = (int)(front - sectors);

        if (!back)
        {
            // One-sided solid wall: front floor..ceiling, mid texture.
            emit_wall(&bld, seg, front->floorheight,
                      front->ceilingheight, side->midtexture, 0, PEG_ONESIDED,
                      fi, RB_PLANE_FLOOR, fi, RB_PLANE_CEIL);
            continue;
        }

        int bi = (int)(back - sectors);

        // Upper step (front ceiling higher than back). Skipped only when BOTH
        // ceilings are sky -- DOOM's outdoor height-change hack (r_segs.c:530),
        // which hides the seam between two open-sky areas of differing heights.
        // When only the front ceiling is sky (e.g. a doorway cut into an outdoor
        // wall), the top texture above the opening IS drawn, with the sky
        // backdrop showing above it; guarding on the front ceiling alone left a
        // see-through hole there (the missing lintel above E1M1's exit door).
        // Bottom edge = back ceiling (the door face on a door sector), top edge
        // = front ceiling -- so a rising door ceiling shrinks this wall.
        if (front->ceilingheight > back->ceilingheight
            && !(front->ceilingpic == skyflatnum
                 && back->ceilingpic == skyflatnum))
            emit_wall(&bld, seg, back->ceilingheight, front->ceilingheight,
                      side->toptexture, 0, PEG_UPPER,
                      bi, RB_PLANE_CEIL, fi, RB_PLANE_CEIL);

        // Lower step (front floor lower than/equal to back): bottom = front
        // floor, top = back floor (a lift floor raising/lowering changes the
        // relevant edge). The <= (not <) emits a flush line's lower wall as a
        // zero-height quad so a lift built level with its neighbour still has a
        // shaft wall to GROW into as it travels -- without it that wall was
        // missing in 3D until the lift moved, where Classic re-derives it each
        // frame (DOOM-0068). emit_wall drops the "-"/untextured side, so only the
        // textured shaft face (which grows valid, never inverts) is added.
        if (front->floorheight <= back->floorheight)
            emit_wall(&bld, seg, front->floorheight, back->floorheight,
                      side->bottomtexture, 0, PEG_LOWER,
                      fi, RB_PLANE_FLOOR, bi, RB_PLANE_FLOOR);

        // Middle (rails/grates): alpha-tested, spans the shared opening. The
        // bottom edge follows the higher floor, the top edge the lower ceiling.
        if (side->midtexture)
        {
            int     bsec = front->floorheight > back->floorheight ? fi : bi;
            int     tsec = front->ceilingheight < back->ceilingheight ? fi : bi;
            fixed_t zb = front->floorheight > back->floorheight
                       ? front->floorheight : back->floorheight;
            fixed_t zt = front->ceilingheight < back->ceilingheight
                       ? front->ceilingheight : back->ceilingheight;
            emit_wall(&bld, seg, zb, zt, side->midtexture, RB_MESH_MASKED, PEG_MID,
                      bsec, RB_PLANE_FLOOR, tsec, RB_PLANE_CEIL);
        }
    }

    // Floor/ceiling caps: carve each subsector's exact convex cell out of the
    // BSP, clipping a map-sized quad down through the node tree. The root is the
    // last node (or subsector 0 for a node-less single-sector map). B spans
    // DOOM's signed-16-bit coordinate range, so the box contains the whole map.
    {
        const float B = 32768.0f;
        poly_t box;
        int    root;
        box.n = 4;                       // CCW with y up
        box.x[0] = -B; box.y[0] = -B;
        box.x[1] =  B; box.y[1] = -B;
        box.x[2] =  B; box.y[2] =  B;
        box.x[3] = -B; box.y[3] =  B;
        root = (numnodes > 0) ? (numnodes - 1) : NF_SUBSECTOR;
        carve_caps(&bld, root, box);
    }

    free(seg2ss);

    mesh = malloc(sizeof(rb_mesh_t));
    if (!mesh)
        I_Error("RB_BuildLevelMesh: out of memory allocating mesh handle");
    mesh->verts    = bld.v;
    mesh->numverts = bld.count;
    mesh->numtris  = bld.count / 3;
    mesh->tri_ss   = bld.ssid;   // one subsector id per triangle (step 4c)
    return mesh;
}

void RB_FreeMesh(rb_mesh_t* mesh)
{
    if (!mesh)
        return;
    free(mesh->verts);
    free(mesh->tri_ss);
    free(mesh);
}

// DOOM-0009 build step 4: place one GI-bake irradiance probe per subsector. The
// position is the mean of the subsector's seg endpoints (a point inside the
// convex BSP leaf) at the owning sector's mid-height. The sector is taken from
// the first seg's frontsector (subsectors[].sector is not populated at load in
// this DOOM build -- it is filled lazily by R_Subsector at render time), so this
// stays correct even before the first frame is drawn.
int RB_BuildProbes(rb_probe_t* out, int maxprobes)
{
    int n = (numsubsectors < maxprobes) ? numsubsectors : maxprobes;
    int i;
    for (i = 0; i < n; i++)
    {
        subsector_t* ss = &subsectors[i];
        double cx = 0.0, cy = 0.0;
        int    j;

        if (ss->numlines <= 0)        // degenerate leaf: park at origin, never hit
        {
            out[i].x = out[i].y = out[i].z = 0.0f;
            continue;
        }

        for (j = 0; j < ss->numlines; j++)
        {
            seg_t* seg = &segs[ss->firstline + j];
            cx += (seg->v1->x + seg->v2->x) / (double)FRACUNIT;
            cy += (seg->v1->y + seg->v2->y) / (double)FRACUNIT;
        }
        cx /= (double)(ss->numlines * 2);
        cy /= (double)(ss->numlines * 2);

        {
            sector_t* sec = segs[ss->firstline].frontsector;
            out[i].x = (float)cx;
            out[i].y = (float)cy;
            out[i].z = 0.5f * (sec->floorheight + sec->ceilingheight) / (float)FRACUNIT;
        }
    }
    return n;
}

int RB_NumSubsectors(void)
{
    return numsubsectors;
}

int RB_UpdateMeshHeights(const rb_mesh_t* mesh, rb_vertex_t* dst)
{
    int i;
    int moved = 0;          // any z actually changed this frame -> RT refit (step 5)
    if (!mesh || !dst)
        return 0;
    // Doors/lifts move sector floor/ceiling heights every tic; rewrite only the
    // z of vertices tagged to a moving plane, straight to the GPU buffer (the
    // mesh stays otherwise static). mesh->verts holds the build-time tags +
    // base geometry; dst[i] is the live GPU vertex (z gets the current height).
    for (i = 0; i < mesh->numverts; i++)
    {
        const rb_vertex_t* v = &mesh->verts[i];
        float newz;
        // Live texture id (DOOM-0066). A pressed switch swaps the sidedef's
        // texture and animated walls/flats cycle the translation table each tic;
        // the baked texnum reflects neither. Re-derive exactly what the software
        // renderer samples: texturetranslation[sidedef tex] for walls,
        // flattranslation[sector pic] for flats. Cheap: one array read per vertex.
        if (v->flags & RB_MESH_FLAT)
        {
            int pic = (v->vplane == RB_PLANE_CEIL)
                      ? sectors[v->vsector].ceilingpic
                      : sectors[v->vsector].floorpic;
            if (pic != skyflatnum)
                dst[i].texnum = flattranslation[pic];
        }
        else if (v->vtexside >= 0)
        {
            const side_t* sd = &sides[v->vtexside];
            int base = v->vtexslot == 0 ? sd->toptexture
                     : v->vtexslot == 2 ? sd->bottomtexture
                     : sd->midtexture;
            if (base > 0)
                dst[i].texnum = texturetranslation[base];
        }
        if (v->vplane == RB_PLANE_FLOOR)
            newz = sectors[v->vsector].floorheight / (float)FRACUNIT;
        else if (v->vplane == RB_PLANE_CEIL)
            newz = sectors[v->vsector].ceilingheight / (float)FRACUNIT;
        else
            continue;
        if (dst[i].z != newz)
            moved = 1;          // geometry shifted -> the BLAS is now stale
        dst[i].z = newz;
        // Walls: re-peg the texture to its (possibly moving) anchor plane so it
        // slides WITH a door/lift at 1 texel per world unit instead of stretching
        // or staying fixed in space. Texture row 0 sits at anchor_height +
        // vtexoff; v counts down from there to this vertex's z. Flats project on
        // world XY (vtexplane == NONE) and are left alone (DOOM-0067).
        if (v->vtexplane == RB_PLANE_FLOOR || v->vtexplane == RB_PLANE_CEIL)
        {
            float az = (v->vtexplane == RB_PLANE_CEIL
                        ? sectors[v->vtexsec].ceilingheight
                        : sectors[v->vtexsec].floorheight) / (float)FRACUNIT;
            dst[i].v = (az + v->vtexoff) - newz;
        }
    }
    return moved;
}

//
// Atlas packing. Each tile is a wall texture (id = texnum) or a flat
// (id = numtextures + flatnum). Tiles are placed by a simple shelf packer into
// a fixed-width atlas that grows in height; a tile never bleeds into its
// neighbour because the shader wraps UVs strictly within [origin, origin+size)
// and samples nearest, so no inter-tile padding is needed.
//
#define ATLAS_WIDTH 2048

static void tile_size(int id, int* w, int* h)
{
    if (id < numtextures)
    {
        *w = texturewidthmask[id] + 1;            // wall tiling width
        *h = textureheight[id] >> FRACBITS;       // wall pixel height
    }
    else if (id < numtextures + numflats)
    {
        *w = *h = 64;                             // flats are always 64x64
    }
    else
    {
        // sprite lump: full patch bounding box (the gaps become transparent)
        const patch_t* p = W_CacheLumpNum(
            firstspritelump + (id - numtextures - numflats), PU_CACHE);
        *w = SHORT(p->width);
        *h = SHORT(p->height);
    }
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
    // A tile wider than the atlas would overrun the shelf row in blit_tile
    // (the packer resets x to 0 but keeps the width). Stock DOOM textures are
    // <=256 wide so this never fires, but a crafted WAD could define a wider
    // texture -- clamp to crop it rather than corrupt the heap.
    if (*w > ATLAS_WIDTH) *w = ATLAS_WIDTH;
}

// Palette index of the darkest non-black colour. Index 0 doubles as the
// sprite/masked transparent key, so a genuinely-black (index 0) texel inside a
// post would be discarded and punch a see-through hole in the sprite (visible
// on the player weapon/hand). Remap such opaque texels to this near-black index
// instead: visually identical, but not keyed out. Scanned once from PLAYPAL.
static int sprite_opaque_black(void)
{
    static int idx = -1;
    if (idx < 0)
    {
        const byte* pal = W_CacheLumpName("PLAYPAL", PU_CACHE);
        int i, best = 1, bestlum = 1 << 30;
        for (i = 1; i < 256; i++)
        {
            int lum = pal[i*3] + pal[i*3 + 1] + pal[i*3 + 2];
            if (lum < bestlum) { bestlum = lum; best = i; }
        }
        idx = best;
    }
    return idx;
}

// Copy one tile's palette indices into the atlas at (ox,oy).
static void blit_tile(unsigned char* dst, int dstw, int id, int ox, int oy,
                      int w, int h)
{
    int col, row;
    if (id < numtextures)
    {
        // Composite the wall texture's patches with their real posts: opaque
        // textures fill the slot, masked ones (grates/fences) leave gaps at the
        // pre-zeroed index 0 so the shader's alpha test reads them transparent.
        R_RenderTextureToAtlas(id, dst, dstw, ox, oy, w, h);
    }
    else if (id < numtextures + numflats)
    {
        const byte* flat = W_CacheLumpNum(firstflat + (id - numtextures), PU_CACHE);
        for (row = 0; row < h; row++)
            for (col = 0; col < w; col++)
                dst[(oy + row) * dstw + (ox + col)] = flat[row * 64 + col];
    }
    else
    {
        // Sprite lump: a posted (masked) patch. The atlas slot is pre-zeroed
        // (calloc), so untouched texels stay palette index 0 = transparent; we
        // write only the opaque pixels each post lists. Same column walk the
        // software masked-column drawer uses: data at post+3, next post at
        // +length+4.
        const patch_t* patch = W_CacheLumpNum(
            firstspritelump + (id - numtextures - numflats), PU_CACHE);
        for (col = 0; col < w; col++)
        {
            const column_t* column = (const column_t*)
                ((const byte*)patch + LONG(patch->columnofs[col]));
            while (column->topdelta != 0xff)
            {
                const byte* src = (const byte*)column + 3;
                for (row = 0; row < column->length; row++)
                {
                    int  y     = column->topdelta + row;
                    byte texel = src[row];
                    // Opaque black -> near-black, so it survives the index-0
                    // transparency test instead of becoming a hole.
                    if (texel == 0)
                        texel = (byte)sprite_opaque_black();
                    if (y >= 0 && y < h)
                        dst[(oy + y) * dstw + (ox + col)] = texel;
                }
                column = (const column_t*)
                    ((const byte*)column + column->length + 4);
            }
        }
    }
}

const unsigned char* RB_PlayPal(void)
{
    // Palette 0 of the WAD-global PLAYPAL lump (256 RGB triples). Cached, so the
    // back-end can read it at init -- before any level -- for the overlay LUT.
    return (const unsigned char*)W_CacheLumpName("PLAYPAL", PU_CACHE);
}

rb_atlas_t* RB_BuildAtlas(void)
{
    int total = numtextures + numflats + numspritelumps;
    rb_atlas_t* atlas = malloc(sizeof(rb_atlas_t));
    int x = 0, y = 0, shelf = 0, id;

    if (!atlas)
        I_Error("RB_BuildAtlas: out of memory allocating atlas handle");

    atlas->numwall   = numtextures;
    atlas->numflat   = numflats;
    atlas->numsprite = numspritelumps;
    atlas->rects     = malloc((size_t)total * sizeof(rb_rect_t));
    if (!atlas->rects)
        I_Error("RB_BuildAtlas: out of memory allocating %d rects", total);

    // Pass 1: shelf-pack to assign each tile an origin and measure the height.
    // Packed in id order (not height-sorted) so the rect array stays indexable
    // by the shader's unified id; a level's texture set wastes only a little
    // atlas area this way, paid once per session.
    for (id = 0; id < total; id++)
    {
        int w, h;
        tile_size(id, &w, &h);
        if (x + w > ATLAS_WIDTH)        // next shelf
        {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        atlas->rects[id].ox = (float)x;
        atlas->rects[id].oy = (float)y;
        atlas->rects[id].w  = (float)w;
        atlas->rects[id].h  = (float)h;
        x += w;
        if (h > shelf) shelf = h;
    }

    atlas->atlasw = ATLAS_WIDTH;
    atlas->atlash = y + shelf;
    atlas->pixels = calloc((size_t)atlas->atlasw * atlas->atlash, 1);
    if (!atlas->pixels)
        I_Error("RB_BuildAtlas: out of memory for a %dx%d atlas",
                atlas->atlasw, atlas->atlash);

    // Pass 2: blit every tile's palette indices into the packed slot.
    for (id = 0; id < total; id++)
        blit_tile(atlas->pixels, atlas->atlasw, id,
                  (int)atlas->rects[id].ox, (int)atlas->rects[id].oy,
                  (int)atlas->rects[id].w, (int)atlas->rects[id].h);

    memcpy(atlas->playpal, W_CacheLumpName("PLAYPAL", PU_CACHE), sizeof(atlas->playpal));
    return atlas;
}

int RB_MaterialCount(void)
{
    // Same set RB_BuildAtlas packs, in the same id order (walls, flats, sprites).
    return numtextures + numflats + numspritelumps;
}

void RB_FreeAtlas(rb_atlas_t* atlas)
{
    if (!atlas)
        return;
    free(atlas->pixels);
    free(atlas->rects);
    free(atlas);
}

//
// DOOM-0008 sprites. Each visible thing becomes one camera-facing billboard
// quad: a cylindrical billboard whose horizontal axis follows the camera's
// right vector and whose vertical axis is world +z (sprites stand upright). The
// quad is sized in map units from the sprite patch, positioned at the thing's
// feet via spritetopoffset, and the 8-way rotation / horizontal flip are chosen
// exactly as R_ProjectSprite does. Transparency is the shader's job (it drops
// palette index 0), so no per-sprite sorting is needed — the depth buffer plus
// alpha-test resolves occlusion. Rebuilt every frame because things move.
//

// Sprite-lump pixel heights, cached on first use. Widths/offsets already live in
// r_data.c's spritewidth/spriteoffset/spritetopoffset (fixed-point); only the
// height has no table, so we read it from each patch header once.
static short* sprite_h = NULL;

static void ensure_sprite_heights(void)
{
    int i;
    if (sprite_h)
        return;
    sprite_h = malloc((size_t)numspritelumps * sizeof(short));
    if (!sprite_h)
        I_Error("RB_BuildSprites: out of memory for %d sprite heights",
                numspritelumps);
    for (i = 0; i < numspritelumps; i++)
    {
        const patch_t* p = W_CacheLumpNum(firstspritelump + i, PU_CACHE);
        sprite_h[i] = SHORT(p->height);
    }
}

// DOOM-0112: glowing collectibles that should emit coloured light even though their
// frames aren't FF_FULLBRIGHT — the blue/green bonus potions, the powerup spheres,
// the coloured keys/skulls, and the health pickups. Plain ammo is deliberately
// absent (it stays a dark billboard). Their emission colour comes from the sprite's
// own bright texels (the per-material Le), so a blue potion glows blue.
static boolean sprite_glows(spritenum_t s)
{
    switch (s)
    {
        case SPR_BON1: case SPR_BON2:                         // health / armor bonus
        case SPR_SOUL: case SPR_MEGA: case SPR_PINV:          // soul / mega / invuln
        case SPR_PINS: case SPR_PSTR:                         // blur sphere / berserk
        case SPR_BKEY: case SPR_RKEY: case SPR_YKEY:          // keycards
        case SPR_BSKU: case SPR_RSKU: case SPR_YSKU:          // skull keys
        case SPR_STIM: case SPR_MEDI:                         // stimpack / medikit
            return true;
        default:
            return false;
    }
}

int RB_BuildSprites(const rb_view_t* view, rb_vertex_t* out, int maxverts)
{
    // Camera right vector in world space, matching Mat4LookAt with up = +z:
    // right = normalize(fwd x up) = (sin a, -cos a, 0). Billboard up is +z, and
    // the normal faces the camera (placeholder shading only).
    float   a    = view->angle;
    float   rx   =  sinf(a), ry = -cosf(a);    // screen-right in world
    float   nx   = -cosf(a), ny = -sinf(a);    // billboard normal (toward eye)
    fixed_t camx = (fixed_t)(view->x * FRACUNIT);
    fixed_t camy = (fixed_t)(view->y * FRACUNIT);
    int     s, n = 0;

    if (numspritelumps <= 0)
        return 0;
    ensure_sprite_heights();

    for (s = 0; s < numsectors; s++)
    {
        mobj_t* thing;
        for (thing = sectors[s].thinglist; thing; thing = thing->snext)
        {
            spritedef_t*   sprdef;
            spriteframe_t* sprframe;
            int            lump;
            int            sflags;
            boolean        flip;
            float          wpx, hpx, loff, toff, cx, cy, topz, botz, ld, rd, light;
            float          u0, u1, v0, v1;

            if (thing == players[consoleplayer].mo)
                continue;                       // our own body, first person
            if ((unsigned)thing->sprite >= (unsigned)numsprites)
                continue;
            sprdef = &sprites[thing->sprite];
            if ((thing->frame & FF_FRAMEMASK) >= sprdef->numframes)
                continue;
            sprframe = &sprdef->spriteframes[thing->frame & FF_FRAMEMASK];

            if (sprframe->rotate)
            {
                angle_t  ang = R_PointToAngle2(camx, camy, thing->x, thing->y);
                unsigned rot = (ang - thing->angle + (unsigned)(ANG45/2)*9) >> 29;
                lump = sprframe->lump[rot];
                flip = (boolean)sprframe->flip[rot];
            }
            else
            {
                lump = sprframe->lump[0];
                flip = (boolean)sprframe->flip[0];
            }

            wpx  = spritewidth[lump]     / (float)FRACUNIT;
            hpx  = (float)sprite_h[lump];
            loff = spriteoffset[lump]    / (float)FRACUNIT;
            toff = spritetopoffset[lump] / (float)FRACUNIT;

            cx   = thing->x / (float)FRACUNIT;
            cy   = thing->y / (float)FRACUNIT;
            topz = thing->z / (float)FRACUNIT + toff;
            botz = topz - hpx;
            ld   = -loff;                       // left edge along right vector
            rd   = -loff + wpx;                 // right edge

            light = thing->subsector->sector->lightlevel / 255.0f;
            if (thing->frame & FF_FULLBRIGHT)
                light = 1.0f;
            if (light < 0.0f) light = 0.0f;
            if (light > 1.0f) light = 1.0f;

            // DOOM-0084: a fullbright frame marks an actual light object (lamp,
            // torch, candle, burning barrel, projectile) — NOT ammo/pickups. Only
            // these self-glow + cast NEE light in the path tracer; everything else
            // is just a depth-occluding billboard (DOOM-0100). DOOM-0112 also lights
            // the glowing collectibles (bonus potions, spheres, keys, health) which
            // aren't FF_FULLBRIGHT but should still emit their colour.
            sflags = RB_MESH_SPRITE;
            if ((thing->frame & FF_FULLBRIGHT) || sprite_glows(thing->sprite))
                sflags |= RB_MESH_EMISSIVE;

            // UVs inset half a texel so nearest sampling stays inside the rect.
            u0 = 0.5f;  u1 = wpx - 0.5f;
            v0 = 0.5f;  v1 = hpx - 0.5f;
            if (flip) { float t = u0; u0 = u1; u1 = t; }

            if (n + 6 > maxverts)
                return n;                       // buffer full; drop the rest

            // Corners: left-top, right-top, right-bottom, left-bottom.
            out[n++] = mkv(cx + rx*ld, cy + ry*ld, topz, nx, ny, 0.0f, u0, v0, lump, sflags, light);
            out[n++] = mkv(cx + rx*rd, cy + ry*rd, topz, nx, ny, 0.0f, u1, v0, lump, sflags, light);
            out[n++] = mkv(cx + rx*rd, cy + ry*rd, botz, nx, ny, 0.0f, u1, v1, lump, sflags, light);
            out[n++] = mkv(cx + rx*ld, cy + ry*ld, topz, nx, ny, 0.0f, u0, v0, lump, sflags, light);
            out[n++] = mkv(cx + rx*rd, cy + ry*rd, botz, nx, ny, 0.0f, u1, v1, lump, sflags, light);
            out[n++] = mkv(cx + rx*ld, cy + ry*ld, botz, nx, ny, 0.0f, u0, v1, lump, sflags, light);
        }
    }
    return n;
}


// Player weapon / muzzle-flash overlay. The psprites live in DOOM's 320x200 HUD
// space; mirror R_DrawPSprite's placement (with pspritescale = 1, i.e. straight
// into 320x200) and map that rect to Vulkan NDC over the whole frame. z=0 plants
// the quad at the near plane so the depth test (LESS) always lets it sit on top
// of the world. Screen-space, so no camera/view is needed.
int RB_BuildPSprites(rb_vertex_t* out, int maxverts, float aspect)
{
    player_t* player;
    float     baselight;
    int       i, n = 0;
    int       fl = RB_MESH_SPRITE | RB_MESH_PSPRITE;
    // DOOM's weapon lives in 320x200 (displayed 4:3). Mapping it to the full
    // widescreen NDC stretched it ~33% wide on 16:9; scale x by (4:3)/aspect
    // about screen centre so the gun keeps DOOM's 1.2 pixel aspect (y stays
    // full-height). 1.0 at 4:3, ~0.75 at 16:9.
    float     xscale = (4.0f / 3.0f) / aspect;
    // The weapon belongs to the active view window, not the whole frame. Map the
    // 200-tall HUD canvas onto the view's vertical span [viewwindowy,
    // viewwindowy+viewheight] (physical pixels) so the lower part -- the hand --
    // sits at the view bottom and clears the status bar when one is shown (screen
    // size <= 10), exactly as the classic renderer anchors the weapon to the
    // view. At full-screen (size 11) this is the identity y/100 - 1.
    float     halfH  = SCREENHEIGHT * 0.5f;
    float     vy0    = viewwindowy / halfH - 1.0f;             // view top, NDC
    float     vyscl  = (viewheight / 200.0f) / halfH;          // canvas px -> NDC

    if (numspritelumps <= 0)
        return 0;
    ensure_sprite_heights();

    player = &players[consoleplayer];
    if (!player->mo)
        return 0;
    baselight = player->mo->subsector->sector->lightlevel / 255.0f;

    for (i = 0; i < NUMPSPRITES; i++)
    {
        pspdef_t*      psp = &player->psprites[i];
        spritedef_t*   sprdef;
        spriteframe_t* sprframe;
        int            lump;
        boolean        flip;
        float          wpx, hpx, loff, toff, light;
        float          lx, rx, ty, by;          // rect in 320x200 HUD space
        float          nx0, nx1, ny0, ny1;      // same rect in NDC
        float          u0, u1, v0, v1;

        if (!psp->state)
            continue;
        if ((unsigned)psp->state->sprite >= (unsigned)numsprites)
            continue;
        sprdef = &sprites[psp->state->sprite];
        if ((psp->state->frame & FF_FRAMEMASK) >= sprdef->numframes)
            continue;
        sprframe = &sprdef->spriteframes[psp->state->frame & FF_FRAMEMASK];
        lump = sprframe->lump[0];               // psprites are never rotated
        flip = (boolean)sprframe->flip[0];

        wpx  = spritewidth[lump]     / (float)FRACUNIT;
        hpx  = (float)sprite_h[lump];
        loff = spriteoffset[lump]    / (float)FRACUNIT;
        toff = spritetopoffset[lump] / (float)FRACUNIT;

        // psp->sx / psp->sy carry the weapon bob; offsets re-centre the patch.
        lx = psp->sx / (float)FRACUNIT - loff;
        ty = psp->sy / (float)FRACUNIT - toff;
        rx = lx + wpx;
        by = ty + hpx;

        // 320 wide, 200 tall -> NDC [-1,1]; Vulkan y points down (y=0 is top).
        // x is scaled about screen centre to undo the widescreen stretch.
        nx0 = (lx / 160.0f - 1.0f) * xscale;
        nx1 = (rx / 160.0f - 1.0f) * xscale;
        ny0 = vy0 + ty * vyscl;
        ny1 = vy0 + by * vyscl;

        light = (psp->state->frame & FF_FULLBRIGHT) ? 1.0f : baselight;
        if (light < 0.0f) light = 0.0f;
        if (light > 1.0f) light = 1.0f;

        u0 = 0.5f;  u1 = wpx - 0.5f;
        v0 = 0.5f;  v1 = hpx - 0.5f;
        if (flip) { float t = u0; u0 = u1; u1 = t; }

        if (n + 6 > maxverts)
            return n;

        // Normal toward +z keeps the placeholder Lambert term constant for the
        // weapon. left-top, right-top, right-bottom, then left-top, rb, left-bot.
        out[n++] = mkv(nx0, ny0, 0.0f, 0.0f, 0.0f, 1.0f, u0, v0, lump, fl, light);
        out[n++] = mkv(nx1, ny0, 0.0f, 0.0f, 0.0f, 1.0f, u1, v0, lump, fl, light);
        out[n++] = mkv(nx1, ny1, 0.0f, 0.0f, 0.0f, 1.0f, u1, v1, lump, fl, light);
        out[n++] = mkv(nx0, ny0, 0.0f, 0.0f, 0.0f, 1.0f, u0, v0, lump, fl, light);
        out[n++] = mkv(nx1, ny1, 0.0f, 0.0f, 0.0f, 1.0f, u1, v1, lump, fl, light);
        out[n++] = mkv(nx0, ny1, 0.0f, 0.0f, 0.0f, 1.0f, u0, v1, lump, fl, light);
    }
    return n;
}

int RB_BuildSky(const rb_view_t* view, rb_vertex_t* out, int maxverts)
{
    int     fl = RB_MESH_SKY;
    int     sky = view->skytexnum;
    float   l = 1.0f;   // unused: the sky fragment path is fullbright

    if (maxverts < 6)
        return 0;

    // One full-screen quad in Vulkan NDC (z=0). UVs are unused here: the vertex
    // shader derives the screen position from the NDC corner, and the fragment
    // shader turns that plus the view yaw into a sky-texture texel. Corners:
    // top-left, top-right, bottom-right, then top-left, bottom-right, bottom-left.
    out[0] = mkv(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sky, fl, l);
    out[1] = mkv( 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sky, fl, l);
    out[2] = mkv( 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sky, fl, l);
    out[3] = mkv(-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sky, fl, l);
    out[4] = mkv( 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sky, fl, l);
    out[5] = mkv(-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, sky, fl, l);
    return 6;
}
