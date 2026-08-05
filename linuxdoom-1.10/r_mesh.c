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
#include "r_main.h"     // R_PointInSubsector (RB_SectorAtPoint)
#include "doomstat.h"   // skyflatnum, players, consoleplayer
#include "p_local.h"    // P_LineOpening + openrange (DOOM-0011 L1d's seep fill)
#include "p_mobj.h"     // mobj_t (the things billboarded as sprites)
#include "p_pspr.h"     // FF_FRAMEMASK / FF_FULLBRIGHT
#include "tables.h"     // ANG45
#include "m_swap.h"     // SHORT / LONG (sprite patch headers)
#include "i_system.h"   // I_Error
#include "w_wad.h"      // W_CacheLumpNum/Name
#include "z_zone.h"     // PU_CACHE
#include "seg_project.h" // DOOM-0180 RB_ProjectOnLine (seg -> its linedef's line)
#include "r_mesh.h"

// Texture geometry tables from r_data.c (no public header declares them).
// texturewidthmask+1 is the wall's tiling width (matches how R_GetColumn masks
// column lookups — non-power-of-two textures tile at this width, exactly as the
// software renderer shows them); textureheight is the wall's pixel height.
extern int*     texturewidthmask;
extern int      numtextures;
extern int      numflats;

// DOOM-0141: the level's sky wall-texture index (r_sky.c). Sky ceilings/borders are
// emitted into the RT-only sky list below with this texnum so the path tracer samples
// the same sky panorama the raster path's RB_BuildSky uses.
extern int      skytexture;

// DOOM-0119: the WAD REJECT sector->sector visibility lump, cached by P_SetupLevel
// (declared in p_local.h, not pulled in here). numsectors/sectors/subsectors/segs
// come from r_state.h.
extern byte*    rejectmatrix;

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
    // DOOM-0141: sky backdrop tris (RT-only, separate acceleration structure). No
    // ssid/probe tag — sky is fullbright and never reflects, so it carries no GI.
    rb_vertex_t* sky;
    int          skycount;
    int          skycap;
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

// DOOM-0141: push one sky backdrop triangle (separate list, no ssid tag).
static void sky_push_tri(builder_t* b, rb_vertex_t a, rb_vertex_t c, rb_vertex_t d)
{
    if (b->skycount + 3 > b->skycap)
    {
        b->skycap = b->skycap ? b->skycap * 2 : 1024;
        b->sky = realloc(b->sky, b->skycap * sizeof(rb_vertex_t));
        if (!b->sky)
            I_Error("RB_BuildLevelMesh: out of memory at %d sky verts", b->skycount);
    }
    b->sky[b->skycount++] = a;
    b->sky[b->skycount++] = c;
    b->sky[b->skycount++] = d;
}

// DOOM-0180: a seg's endpoints, snapped back onto its linedef's exact line.
//
// Use this for EVERY piece of mesh geometry built along a seg -- walls and the
// half-planes the floor/ceiling caps are clipped to alike. The stored endpoints
// are integers, so a node-builder split of a diagonal linedef rounds off the true
// line, and because the two sides are split independently they trace different
// polylines; a wall on one and the neighbouring caps on the other then miss each
// other by up to a unit and leave a hole. seg_project.h has the full account.
static void seg_line_xy(const seg_t* seg, float* x1, float* y1,
                        float* x2, float* y2)
{
    const line_t* ld = seg->linedef;
    double ax = ld->v1->x / (double)FRACUNIT, ay = ld->v1->y / (double)FRACUNIT;
    double dx = ld->dx    / (double)FRACUNIT, dy = ld->dy    / (double)FRACUNIT;
    RB_ProjectOnLine(ax, ay, dx, dy, seg->v1->x / (double)FRACUNIT,
                     seg->v1->y / (double)FRACUNIT, x1, y1);
    RB_ProjectOnLine(ax, ay, dx, dy, seg->v2->x / (double)FRACUNIT,
                     seg->v2->y / (double)FRACUNIT, x2, y2);
}

// DOOM-0141: emit a sky-border wall (the vertical gap between two open-sky sectors of
// different ceiling height, where classic DOOM shows sky in place of an upper
// texture) as sky backdrop geometry. Fills the [zb, zt] span across the seg so the
// view can't see through to geometry beyond; flagged RB_MESH_SKYDOME (world-space
// sky, UVs unused) so both the tracer and the raster occluder pass paint it as sky.
static void emit_sky_wall(builder_t* bld, const seg_t* seg, fixed_t zb, fixed_t zt)
{
    float x1, y1, x2, y2;
    float zbf = zb / (float)FRACUNIT, ztf = zt / (float)FRACUNIT;
    float dx, dy, len, nx, ny;
    rb_vertex_t a, b, c, d;
    if (ztf <= zbf) return;
    seg_line_xy(seg, &x1, &y1, &x2, &y2);
    dx = x2 - x1; dy = y2 - y1;
    len = sqrtf(dx * dx + dy * dy);
    nx = len > 0.0f ? -dy / len : 0.0f;
    ny = len > 0.0f ?  dx / len : 0.0f;
    a = mkv(x1, y1, zbf, nx, ny, 0.0f, 0.0f, 0.0f, skytexture, RB_MESH_SKYDOME, 1.0f);
    b = mkv(x2, y2, zbf, nx, ny, 0.0f, 0.0f, 0.0f, skytexture, RB_MESH_SKYDOME, 1.0f);
    c = mkv(x2, y2, ztf, nx, ny, 0.0f, 0.0f, 0.0f, skytexture, RB_MESH_SKYDOME, 1.0f);
    d = mkv(x1, y1, ztf, nx, ny, 0.0f, 0.0f, 0.0f, skytexture, RB_MESH_SKYDOME, 1.0f);
    sky_push_tri(bld, a, b, c);
    sky_push_tri(bld, a, c, d);
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

    seg_line_xy(seg, &x1, &y1, &x2, &y2);
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
    int    i, trunc = 0;
    out.n = 0;
    for (i = 0; i < in->n; i++)
    {
        int   j  = (i + 1 == in->n) ? 0 : i + 1;
        float fi = sgn * (dx * (in->y[i] - oy) - dy * (in->x[i] - ox));
        float fj = sgn * (dx * (in->y[j] - oy) - dy * (in->x[j] - ox));
        int   ini = fi <= 0.0f, inj = fj <= 0.0f;   // inside == front half-plane
        if (ini)
        {
            if (out.n < POLYMAX)
            { out.x[out.n] = in->x[i]; out.y[out.n] = in->y[i]; out.n++; }
            else trunc = 1;
        }
        if (ini != inj)                             // edge crosses: add the hit
        {
            if (out.n < POLYMAX)
            {
                float t = fi / (fi - fj);
                out.x[out.n] = in->x[i] + t * (in->x[j] - in->x[i]);
                out.y[out.n] = in->y[i] + t * (in->y[j] - in->y[i]);
                out.n++;
            }
            else trunc = 1;
        }
    }
    // DOOM-0073: a half-plane clip of a convex poly adds at most one vertex, so a
    // carved cell stays well under POLYMAX on stock maps (<=8 verts on E1M1). A
    // pathological custom sector could overflow the fixed buffer; rather than
    // silently return a truncated (malformed) polygon that emit_cap_poly would fan
    // into garbage triangles, drop the cell (n=0) so every caller skips it.
    if (trunc)
        out.n = 0;
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

// DOOM-0141: emit a sky-flat cap (a sector floor/ceiling textured F_SKY1) as sky
// backdrop geometry — same convex cell as a normal cap, but flagged RB_MESH_SKYDOME with
// the sky wall-texture so the tracer shades it as the panorama (fullbright; UVs
// unused, the panorama is keyed on screen/ray yaw). `up` picks the normal direction
// (a sky ceiling faces down). Mirrors emit_cap_poly's fan/winding into the sky list.
static void emit_sky_cap(builder_t* bld, const poly_t* p, fixed_t height, int up)
{
    float z  = height / (float)FRACUNIT;
    float nz = up ? 1.0f : -1.0f;
    int   k;
    rb_vertex_t pivot;
    if (p->n < 3) return;
    pivot = mkv(p->x[0], p->y[0], z, 0.0f, 0.0f, nz, 0.0f, 0.0f, skytexture, RB_MESH_SKYDOME, 1.0f);
    for (k = 1; k < p->n - 1; k++)
    {
        rb_vertex_t va = mkv(p->x[k],   p->y[k],   z, 0.0f, 0.0f, nz, 0.0f, 0.0f, skytexture, RB_MESH_SKYDOME, 1.0f);
        rb_vertex_t vb = mkv(p->x[k+1], p->y[k+1], z, 0.0f, 0.0f, nz, 0.0f, 0.0f, skytexture, RB_MESH_SKYDOME, 1.0f);
        if (up) sky_push_tri(bld, pivot, va, vb);
        else    sky_push_tri(bld, pivot, vb, va);   // flip winding so -z faces down
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
        float  ox, oy, ex, ey;
        // DOOM-0180: the seg's linedef line, NOT its stored (rounded) endpoints --
        // the neighbouring sector's cap clips to this same line from the other
        // side, and they only meet exactly if both use the linedef's own.
        seg_line_xy(sg, &ox, &oy, &ex, &ey);
        clipped = clip_poly(&clipped, ox, oy, ex - ox, ey - oy, 1);  // keep front side
        if (clipped.n < 3) return;                          // trimmed to nothing
    }

    light  = sec->lightlevel / 255.0f;
    secidx = (int)(sec - sectors);   // for the per-frame dynamic-height update
    // DOOM-0141: a sky-flat cap becomes RT-only sky backdrop geometry (occludes the
    // view like classic's sky) instead of being omitted; a normal flat caps as before.
    if (sec->floorpic != skyflatnum)
        emit_cap_poly(bld, &clipped, sec->floorheight, 1, sec->floorpic, light,
                      secidx, RB_PLANE_FLOOR);
    else
        emit_sky_cap(bld, &clipped, sec->floorheight, 1);
    if (sec->ceilingpic != skyflatnum)
        emit_cap_poly(bld, &clipped, sec->ceilingheight, 0, sec->ceilingpic, light,
                      secidx, RB_PLANE_CEIL);
    else
        emit_sky_cap(bld, &clipped, sec->ceilingheight, 0);
}

// Walk the BSP, carrying the convex cell clipped by every ancestor partition.
// children[0] is the front/right half-space, children[1] the back/left (DOOM's
// R_PointOnSide convention); at a leaf the carried cell is the subsector's
// exact floor/ceiling outline.
static void carve_caps(builder_t* bld, int nodenum, const poly_t* poly)
{
    node_t* nd;
    float   ox, oy, dx, dy;
    poly_t  child;
    if (poly->n < 3)
        return;
    if (nodenum & NF_SUBSECTOR)
    {
        emit_subsector_caps(bld, nodenum & ~NF_SUBSECTOR, poly);
        return;
    }
    nd = &nodes[nodenum];
    ox = nd->x  / (float)FRACUNIT; oy = nd->y  / (float)FRACUNIT;
    dx = nd->dx / (float)FRACUNIT; dy = nd->dy / (float)FRACUNIT;
    // DOOM-0073: recurse with the carved child cell passed by pointer. poly_t is
    // ~520 bytes; the old by-value parameter copied the whole cell into every BSP
    // level (bounding a degenerate-tree stack blow-up). One reused buffer suffices:
    // the front subtree is fully carved before `child` is overwritten for the back,
    // and clip_poly only reads `poly` (the parent), so the two descents never alias.
    child = clip_poly(poly, ox, oy, dx, dy, 1);
    carve_caps(bld, nd->children[0], &child);
    child = clip_poly(poly, ox, oy, dx, dy, 0);
    carve_caps(bld, nd->children[1], &child);
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
        if (front->ceilingheight > back->ceilingheight)
        {
            if (front->ceilingpic == skyflatnum && back->ceilingpic == skyflatnum)
                // DOOM-0141: both ceilings sky -> classic shows sky in the height
                // gap (no upper texture). Emit it as RT-only sky backdrop so the
                // tracer occludes geometry beyond instead of seeing through the gap.
                emit_sky_wall(&bld, seg, back->ceilingheight, front->ceilingheight);
            else
                emit_wall(&bld, seg, back->ceilingheight, front->ceilingheight,
                          side->toptexture, 0, PEG_UPPER,
                          bi, RB_PLANE_CEIL, fi, RB_PLANE_CEIL);
        }

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
        carve_caps(&bld, root, &box);
    }

    free(seg2ss);

    mesh = malloc(sizeof(rb_mesh_t));
    if (!mesh)
        I_Error("RB_BuildLevelMesh: out of memory allocating mesh handle");
    mesh->verts    = bld.v;
    mesh->numverts = bld.count;
    mesh->numtris  = bld.count / 3;
    mesh->tri_ss   = bld.ssid;   // one subsector id per triangle (step 4c)
    mesh->sky      = bld.sky;     // DOOM-0141: RT-only sky backdrop tris
    mesh->numsky   = bld.skycount;

    // DOOM-0011: the outdoor fog layer's base altitude — the lowest floor under open
    // sky. See rb_mesh_t's field comment for why this must be one level-wide number.
    {
        fixed_t lowest = 0;
        int     found  = 0;
        for (i = 0; i < numsectors; i++)
        {
            if (sectors[i].ceilingpic != skyflatnum)
                continue;
            if (!found || sectors[i].floorheight < lowest)
                lowest = sectors[i].floorheight;
            found = 1;
        }
        mesh->fogFloorZ = found ? lowest / (float)FRACUNIT : 0.0f;
    }

    // DOOM-0300: this level's wisp heading. A pure function of the map's own identity, so
    // the same map always draws the same angle and different maps differ -- see the field
    // comment in r_mesh.h for why this must not be a real roll. Two rounds of an integer
    // avalanche mix, because episode and map are small adjacent integers and the low bits
    // of a plain multiply would walk the angle in step with the map number.
    {
        unsigned int h = (unsigned int)gameepisode * 2654435761u
                       + (unsigned int)gamemap     * 2246822519u;
        h ^= h >> 15;  h *= 2654435761u;  h ^= h >> 13;
        mesh->wispAngle = (float)(h & 0xFFFFu) * (6.28318530718f / 65536.0f);
    }
    return mesh;
}

void RB_FreeMesh(rb_mesh_t* mesh)
{
    if (!mesh)
        return;
    free(mesh->verts);
    free(mesh->tri_ss);
    free(mesh->sky);
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

// DOOM-0119: REJECT-lump visibility, used by the path tracer to cull lights a
// shading point can't see.
int RB_NumSectors(void)
{
    return numsectors;
}

const unsigned char* RB_RejectMatrix(int* sizeBytes)
{
    // DOOM packs the matrix as numsectors*numsectors bits, row-major, LSB-first
    // (see P_CheckSight): bit (s1*numsectors+s2) set => s2 invisible from s1.
    if (sizeBytes)
        *sizeBytes = (numsectors * numsectors + 7) / 8;
    return (const unsigned char*)rejectmatrix;
}

int RB_SectorAtPoint(float x, float y)
{
    subsector_t* ss = R_PointInSubsector((fixed_t)(x * FRACUNIT), (fixed_t)(y * FRACUNIT));
    if (!ss || ss->numlines <= 0)
        return -1;
    // subsectors[].sector is filled lazily at render time; derive the sector the same
    // way RB_BuildProbes does (the first seg's frontsector) so this is valid even
    // before the first frame is drawn.
    return (int)(segs[ss->firstline].frontsector - sectors);
}

// ---------------------------------------------------------------------------
// DOOM-0011 L1d: the outdoor-proximity seep field (spec §4.3a amendment).
//
// The question this answers is "how far is this patch of floor from outdoor air,
// walking?" -- so that fog can drift in through a doorway and thin as you go
// deeper, while a sealed room that merely shares a WALL with the courtyard stays
// clear. Straight-line distance cannot tell those two apart; only connectivity can.
//
// Nodes are PORTALS (open two-sided segs), not sectors and not subsectors. A
// sector-indexed search settles one distance per sector, which is the flat-per-room
// answer this feature exists to avoid. A subsector graph breaks differently:
// vanilla DOOM has no minisegs (P_LoadSegs gives every seg a linedef), so two BSP
// leaves of the same room share no seg and every multi-leaf hall comes out
// disconnected. Distance is then resolved per GRID CELL, not per node, so it varies
// smoothly across a room instead of stepping at its boundary.
// ---------------------------------------------------------------------------

#define RB_SEEP_MAXDIM   256      // texture side cap; the cell size doubles until it fits
#define RB_SEEP_CELL0    64.0f    // starting cell size (a DOOM flat's own 64x64 period)

typedef struct
{
    float x, y;       // world midpoint of the opening
    int   secA, secB; // the two sectors it joins
    float d;          // distance to outdoor air, filled by the search
} rb_portal_t;

// Lazy-deletion binary min-heap over portal indices. Lazy because a portal can be
// pushed more than once as shorter routes to it turn up; the pop checks the entry
// against the node's settled distance and drops it if it has been beaten.
typedef struct { float d; int node; } rb_heapent_t;

// DOOM-0289: what the sun-clearance march needs to know about a cell, cached in the
// pass that already descends the BSP. The march itself then reads plain arrays -- the
// whole ~3.5k-cell rasterise costs 0.6 ms on E1M1, so a march re-querying the BSP per
// step would cost tens of milliseconds and land on DOOM-0281's door-open frame as a
// visible hitch.
typedef struct
{
    float         fz, cz;      // this cell's floor / ceiling, world units
    int           sec;         // owning sector index, or -1 for the void ring
    unsigned char sky;         // sky ceiling?
    unsigned char isvoid;      // centre lies inside a wall / out past the map
    unsigned char solid;       // no air here: isvoid, or a shut door / pillar (cz <= fz)
} rb_cellgeom_t;

// DOOM-0289. The sun's fixed horizontal heading (out) and its slope (returned), derived
// from r_mesh.h's mirror of kSunDir. Both are invariant under a uniform scale of the
// triple, which is why the mirror need not be normalised.
static float RB_SunHeading(float* ux, float* uy)
{
    float ulen = sqrtf(RB_SUN_DIR_X * RB_SUN_DIR_X + RB_SUN_DIR_Y * RB_SUN_DIR_Y);
    // A sun with no horizontal component (a hypothetical "straight overhead" tune) has
    // no heading to march along and would divide by zero here. Not a state the shipped
    // constant can reach; the guard is one line and a NaN would propagate through the
    // whole field and then through sigma. A degenerate heading has no march to run:
    // every OPEN-SKY cell escapes at its own ceiling and every roofed cell falls to the
    // never-sentinel (m = 1e30 drives any ceiling bound to -inf, so `lo > hi` fires at
    // k = 0).
    if (ulen < 1e-6f) { *ux = 1.0f; *uy = 0.0f; return 1e30f; }
    *ux = RB_SUN_DIR_X / ulen;
    *uy = RB_SUN_DIR_Y / ulen;
    return RB_SUN_DIR_Z / ulen;                           // 2.357 at the shipped kSunDir
}

// DOOM-0289. Returns EXACTLY the sector index RB_SectorAtPoint would, and additionally
// fills the geometry the clearance march needs -- from the same single subsector lookup,
// so the BSP is descended once per cell rather than twice.
//
// ONE lookup, TWO independent verdicts, and they must not contaminate each other. The
// sector index is what the seep has always used and it comes out unchanged; the `solid`
// flag is new and is the clearance march's alone. Returning -1 for a void cell would be
// the natural-looking shortcut and it is a LOOK REGRESSION: the rasterise loop gates on
// `sec >= 0`, so a wall-interior cell would flip from a real seep distance to
// RB_SEEP_DMAX and from its BSP leaf's sky mask to 0 -- silently changing a shipped,
// user-signed-off feature.
static int RB_CellGeomAtPoint(float x, float y, fixed_t nudgeX, fixed_t nudgeY,
                              rb_cellgeom_t* out)
{
    fixed_t      fx = (fixed_t)(x * FRACUNIT), fy = (fixed_t)(y * FRACUNIT);
    subsector_t* ss = R_PointInSubsector(fx, fy);
    sector_t*    sc;
    int          i;

    out->fz = 0.0f; out->cz = 0.0f; out->sec = -1;
    out->sky = 0; out->isvoid = 1; out->solid = 1;
    if (!ss || ss->numlines <= 0)
        return -1;                          // same answer RB_SectorAtPoint gives

    sc = segs[ss->firstline].frontsector;    // same derivation RB_SectorAtPoint uses
    out->sec    = (int)(sc - sectors);
    out->fz     = sc->floorheight   / (float)FRACUNIT;
    out->cz     = sc->ceilingheight / (float)FRACUNIT;
    out->sky    = (unsigned char)(sc->ceilingpic == skyflatnum);
    out->isvoid = 0;
    out->solid  = (unsigned char)(out->cz <= out->fz);   // a shut door, a solid pillar

    // VOID TEST -- sets `isvoid`/`solid`, and NOTHING ELSE. DOOM's BSP partitions the
    // whole plane, so a point inside a wall or out past the map still lands in SOME leaf
    // and R_PointInSubsector hands back a NEIGHBOURING room's sector. The seep can live
    // with that -- it decided connectivity on the portal graph before rasterising -- but
    // the clearance march reads these heights directly, and without this test a solid
    // building in a courtyard casts no shadow, which is exactly the shaft the feature
    // exists to draw.
    //
    // The descent already guarantees the point is on the correct side of every node
    // split, so the segs are the only remaining half of the leaf's boundary, and a convex
    // leaf's segs face inward: behind any one of them means outside the leaf.
    for (i = 0; i < ss->numlines; i++)
    {
        seg_t* sg = &segs[ss->firstline + i];
        int    side;
        // The flood loop guards `!ld` before touching a linedef and so must this:
        // "vanilla has no minisegs" is a statement about WAD content, not an engine
        // guarantee, and a PWAD built by a modern node builder can carry them.
        if (!sg->linedef)
            continue;
        // A self-referencing sector (deep water / fake wall) names the SAME sector on
        // both sides, so `side` below cannot distinguish them and would come out 0 for
        // every seg -- misjudging about half of them as walls and stamping a spurious
        // shadow across the room. The point is in that sector's air either way, so the
        // seg simply carries no information here. RB_BuildSeepField excludes the same
        // case from the portal graph, for a different reason.
        if (sg->linedef->frontsector == sg->linedef->backsector)
            continue;
        side = (sg->linedef->frontsector == sg->frontsector) ? 0 : 1;
        // ON-THE-LINE IS NOT A RARE CASE HERE, and P_PointOnLineSide resolves it to 1
        // (back) -- which would mark a doorway-threshold cell solid and delete the very
        // shaft this feature exists to draw. The caller nudges the sample off the line
        // along +u by RB_SUN_NUDGE.
        //
        // The nudge is ONE WORLD UNIT, and the size is load-bearing rather than a taste
        // call: `ss` was resolved at the UN-nudged point, so this test is only meaningful
        // while the nudged point is still inside that leaf. Leaves are much smaller than
        // the 64-unit cell, so a quarter-cell nudge walks out of them routinely and the
        // "behind a seg" verdict then means "in the next room", not "in the void".
        if (P_PointOnLineSide(fx + nudgeX, fy + nudgeY, sg->linedef) != side)
        {
            out->isvoid = 1;
            out->solid  = 1;                // in the void -- but `sc` is still returned
            break;
        }
    }
    return (int)(sc - sectors);
}

// DOOM-0289 (spec §4.4's 2026-07-30 amendment). A ray leaving height z is at
// h(s) = z + m*s. Inside a crossed cell it is LOWEST at the entry and HIGHEST at the
// exit, so each cell contributes exactly one bound at exactly one end:
//   floor          -> z >  floor - m*sIn    (lower bound; lo is a running MAX)
//   ceiling, solid -> z <  ceil  - m*sOut   (upper bound; hi is a running MIN)
//   ceiling, sky   -> z >= ceil  - m*sOut   ESCAPES
// The march must NOT stop at the first sky cell: an open courtyard's first cell escapes
// only above head height, and it is the second and third that bring the answer down to
// the floor. Store the convex hull of the NON-EMPTY escape windows -- the `wLo <= hi`
// guard below is load-bearing, because a later sky sector with a higher ceiling raises
// zEsc, so the first sky cell reached is not necessarily the first that escapes, and
// taking its `hi` would report air as lit that a ceiling in between blocks. The hull's
// remaining error is one-sided (over-lights a gap between two separated openings, never
// leaves a hole), which is the right direction for a beam.
static void RB_SunClearance(const rb_cellgeom_t* geom, int gw, int gh,
                            int ix, int iy, float cell,
                            float ux, float uy, float m,
                            float* outLo, float* outHi)
{
    const rb_cellgeom_t* own = &geom[iy * gw + ix];
    float lo = -1e30f, hi = 1e30f;        // running bounds
    float zLo = 1e30f, zHi = -1e30f;      // the hull, empty
    float s = 0.0f;                       // entry distance of the current cell, world units
    float tMaxX, tMaxY, tDeltaX, tDeltaY;
    int   cx = ix, cy = iy, stepX, stepY, k;

    // Distances are carried in CELLS and scaled by `cell` on use; u is a unit vector, so
    // one unit of t is one cell of world travel. An axis-aligned sun (a component of 0)
    // never crosses that axis' boundaries -- 1e30 is the "never" that expresses it.
    //
    // A cell is entered every cell/(|ux|+|uy|) world units on average -- 45.3 at the
    // shipped 45-degree heading, NOT 64 -- so the rise per cell entered is m*45.3 = 107.
    // At exactly 45 degrees tMaxX and tMaxY tie at every corner and the `else` branch
    // takes it, so the walk staircases one axis at a time and sOut repeats in pairs
    // (45.3, 45.3, 135.8, 135.8, ...). That is correct, not a degeneracy: both cells of
    // each pair are genuinely crossed and both must contribute their bounds.
    stepX   = (ux > 0.0f) ? 1 : -1;
    stepY   = (uy > 0.0f) ? 1 : -1;
    tDeltaX = (ux != 0.0f) ? (1.0f / fabsf(ux)) : 1e30f;
    tDeltaY = (uy != 0.0f) ? (1.0f / fabsf(uy)) : 1e30f;
    tMaxX   = 0.5f * tDeltaX;             // centre -> first boundary is half a cell
    tMaxY   = 0.5f * tDeltaY;

    for (k = 0; k < RB_SUN_MARCH_MAX; k++)
    {
        const rb_cellgeom_t* g = &geom[cy * gw + cx];
        float sOut;

        if (g->solid)
            break;                        // wall, void or a shut door: blocked here

        if (g->fz - m * s > lo)
            lo = g->fz - m * s;
        if (lo > hi)
            break;

        if (tMaxX < tMaxY) { sOut = tMaxX * cell; tMaxX += tDeltaX; cx += stepX; }
        else               { sOut = tMaxY * cell; tMaxY += tDeltaY; cy += stepY; }

        if (g->sky)
        {
            float zEsc = g->cz - m * sOut;
            float wLo  = (zEsc > lo) ? zEsc : lo;
            if (wLo <= hi)                // a non-empty escape window
            {
                if (wLo < zLo) zLo = wLo; // hull: the lowest escape wins
                if (hi  > zHi) zHi = hi;  // hull: the first (and highest) hi wins
            }
        }
        else
        {
            if (g->cz - m * sOut < hi)
                hi = g->cz - m * sOut;
            if (lo > hi)
                break;
        }

        // SATURATION EXIT, and it is not an optimisation. Once the march leaves roofed
        // air, `hi` stops falling (a sky cell contributes no ceiling bound) and `lo`
        // stops rising, so the `lo > hi` exits above can never fire -- without this test
        // every cell whose sun line reaches sky marches to RB_SUN_MARCH_MAX, the doorway
        // case included.
        //
        // The condition is "a window was captured AND zLo has bottomed out", NOT
        // "zHi >= own->cz": zHi is pinned below the cell's own ceiling for any ROOFED
        // cell, so that form can never fire where it is needed most. This form is exact
        // -- `hi` is non-increasing, so zHi (the FIRST non-empty window's hi) is already
        // final once one window exists, and zLo is at the clamp floor.
        if (zHi > -1e29f && zLo <= own->fz)
            break;

        if (cx < 0 || cy < 0 || cx >= gw || cy >= gh)
            break;                        // walked off the padded grid
        s = sOut;
    }

    // Clamp to the cell's OWN air column. A march sample always lies between the camera
    // and a real geometry hit, so the interval is never consulted outside it -- and the
    // clamp is what bounds the dynamic range RB_SUN_NEVER is sized against (INV-13).
    // The shader carries the one correction this needs: an OPEN-SKY cell's "ceiling" is a
    // sky plane rather than a barrier, so it tests `p.z <= zHi || openSky`.
    if (zLo <= zHi)
    {
        float a = (zLo > own->fz) ? zLo : own->fz;
        float b = (zHi < own->cz) ? zHi : own->cz;
        if (a <= b) { *outLo = a; *outHi = b; return; }
    }
    // Never: an interval that is empty by construction, expressed relative to this cell's
    // own column so it blends the same way in a corridor and in a hall. FINITE, for the
    // reason dMax is (§4.3a): a half-float inf under a zero bilinear weight is a NaN, and
    // a NaN in sigma blows the whole march.
    *outLo = own->cz + RB_SUN_NEVER;
    *outHi = own->fz - RB_SUN_NEVER;
}

// DOOM-0289: run the clearance march over every cell of an already-rasterised field.
// Factored out because DOOM-0281's clearance-only rebuild needs exactly this and nothing
// else -- no portal graph, no Dijkstra.
static void RB_SunClearanceMarchAll(rb_seep_t* field)
{
    const rb_cellgeom_t* geom = (const rb_cellgeom_t*)field->geom;
    float ux, uy, m = RB_SunHeading(&ux, &uy);
    int   ix, iy;

    for (iy = 0; iy < field->h; iy++)
        for (ix = 0; ix < field->w; ix++)
        {
            // The ring is written explicitly by the rasterise loop -- open sky in the
            // clearance's reading of it -- and marching from it would only walk further
            // out of the map.
            if (ix == 0 || iy == 0 || ix == field->w - 1 || iy == field->h - 1)
                continue;
            RB_SunClearance(geom, field->w, field->h, ix, iy, field->cell, ux, uy, m,
                            &field->zLo[iy * field->w + ix],
                            &field->zHi[iy * field->w + ix]);
        }
}

int RB_RefreshSunClearance(rb_seep_t* field)
{
    rb_cellgeom_t* geom;
    float*         snap;
    size_t         n;
    int            i, moved;

    if (!field || !field->geom || !field->zLo || !field->zHi)
        return 0;
    geom = (rb_cellgeom_t*)field->geom;
    n    = (size_t)field->w * (size_t)field->h;

    // Refresh the cached HEIGHTS from the live sectors. The void verdict is geometry,
    // not height -- walls do not move in DOOM -- so it is kept from the build rather than
    // re-derived, which is the whole reason the cache is retained: re-deriving it means
    // re-descending the BSP once per cell.
    for (i = 0; i < (int)n; i++)
    {
        sector_t* sc;
        if (geom[i].sec < 0)
            continue;                        // the void ring: written once, never moves
        sc = &sectors[geom[i].sec];
        geom[i].fz    = sc->floorheight   / (float)FRACUNIT;
        geom[i].cz    = sc->ceilingheight / (float)FRACUNIT;
        geom[i].sky   = (unsigned char)(sc->ceilingpic == skyflatnum);
        geom[i].solid = (unsigned char)(geom[i].isvoid || geom[i].cz <= geom[i].fz);
    }

    snap = (float*)malloc(n * 2 * sizeof(float));
    if (!snap)
        I_Error("RB_RefreshSunClearance: out of memory for %d cells", (int)n);
    memcpy(snap,     field->zLo, n * sizeof(float));
    memcpy(snap + n, field->zHi, n * sizeof(float));

    RB_SunClearanceMarchAll(field);

    moved = (memcmp(snap,     field->zLo, n * sizeof(float)) != 0) ||
            (memcmp(snap + n, field->zHi, n * sizeof(float)) != 0);
    free(snap);
    return moved;
}

// DOOM-0281, defined with the rest of the change detector below the flood.
static void RB_SeepSeedOpenings(void);

static void RB_HeapPush(rb_heapent_t* h, int* n, float d, int node)
{
    int i = (*n)++;
    h[i].d = d; h[i].node = node;
    while (i > 0)
    {
        int parent = (i - 1) / 2;
        rb_heapent_t t;
        if (h[parent].d <= h[i].d)
            break;
        t = h[parent]; h[parent] = h[i]; h[i] = t;
        i = parent;
    }
}

static rb_heapent_t RB_HeapPop(rb_heapent_t* h, int* n)
{
    rb_heapent_t top = h[0];
    int i = 0;
    h[0] = h[--(*n)];
    for (;;)
    {
        int l = 2 * i + 1, r = l + 1, small = i;
        rb_heapent_t t;
        if (l < *n && h[l].d < h[small].d) small = l;
        if (r < *n && h[r].d < h[small].d) small = r;
        if (small == i)
            break;
        t = h[small]; h[small] = h[i]; h[i] = t;
        i = small;
    }
    return top;
}

rb_seep_t* RB_BuildSeepField(void)
{
    rb_seep_t*    field;
    rb_portal_t*  portals = NULL;
    rb_heapent_t* heap    = NULL;
    int*          secFirst = NULL;   // per-sector index into secList (numsectors + 1 entries)
    int*          secList  = NULL;   // portal indices grouped by sector
    int           np = 0, heapN = 0, i, j, ix, iy;
    float         minX, minY, maxX, maxY, cell;
    int           gw, gh;
    rb_cellgeom_t* geom = NULL;                    // DOOM-0289, retained on the field
    float          sunUx, sunUy;
    fixed_t        nudgeX, nudgeY;

    // --- 1. One node per OPEN two-sided seg --------------------------------
    portals = (rb_portal_t*)malloc((size_t)(numsegs > 0 ? numsegs : 1) * sizeof(rb_portal_t));
    if (!portals)
        I_Error("RB_BuildSeepField: out of memory for %d portals", numsegs);

    for (i = 0; i < numsegs; i++)
    {
        seg_t*  sg = &segs[i];
        line_t* ld = sg->linedef;
        if (!ld || !sg->backsector || !(ld->flags & ML_TWOSIDED))
            continue;
        // A self-referencing sector (the vanilla deep-water / fake-wall trick) is
        // two-sided with a full-height opening but is DRAWN solid. Without this the
        // flood walks straight through what the player sees as a wall.
        if (ld->frontsector == ld->backsector)
            continue;
        // A shut DOOM door is still a two-sided linedef, so one-sidedness is the
        // wrong test -- the opening is. P_LineOpening returns void and writes the
        // file-scope globals, which are not re-entrant, so this stays single-threaded.
        P_LineOpening(ld);
        if (openrange <= 0)
            continue;

        portals[np].x    = 0.5f * (sg->v1->x / (float)FRACUNIT + sg->v2->x / (float)FRACUNIT);
        portals[np].y    = 0.5f * (sg->v1->y / (float)FRACUNIT + sg->v2->y / (float)FRACUNIT);
        portals[np].secA = (int)(sg->frontsector - sectors);
        portals[np].secB = (int)(sg->backsector  - sectors);
        // Seeded at zero if EITHER side is open to the sky: standing in that opening
        // is standing in outdoor air.
        portals[np].d    = (sectors[portals[np].secA].ceilingpic == skyflatnum ||
                            sectors[portals[np].secB].ceilingpic == skyflatnum)
                         ? 0.0f : RB_SEEP_DMAX;
        np++;
    }

    // --- 2. Group portals by sector, so "shares a sector" is an O(1) lookup --
    secFirst = (int*)calloc((size_t)numsectors + 1, sizeof(int));
    secList  = (int*)malloc((size_t)(np * 2 > 0 ? np * 2 : 1) * sizeof(int));
    if (!secFirst || !secList)
        I_Error("RB_BuildSeepField: out of memory for the sector index");
    for (i = 0; i < np; i++)
    {
        secFirst[portals[i].secA]++;
        secFirst[portals[i].secB]++;
    }
    for (i = 0, j = 0; i < numsectors; i++)   // counts -> start offsets
    {
        int c = secFirst[i];
        secFirst[i] = j;
        j += c;
    }
    secFirst[numsectors] = j;
    {
        int* fill = (int*)malloc((size_t)(numsectors > 0 ? numsectors : 1) * sizeof(int));
        if (!fill)
            I_Error("RB_BuildSeepField: out of memory for the sector index");
        memcpy(fill, secFirst, (size_t)numsectors * sizeof(int));
        for (i = 0; i < np; i++)
        {
            secList[fill[portals[i].secA]++] = i;
            secList[fill[portals[i].secB]++] = i;
        }
        free(fill);
    }

    // --- 3. Dijkstra from the whole seed set at once ------------------------
    // Weights are portal-to-portal distances (non-negative) and the graph is finite,
    // so this terminates. An empty seed set -- a level with no open sky at all --
    // simply settles nothing and every cell ends at RB_SEEP_DMAX.
    heap = (rb_heapent_t*)malloc((size_t)(np * 4 + 1) * sizeof(rb_heapent_t));
    if (!heap)
        I_Error("RB_BuildSeepField: out of memory for the search heap");
    {
        int cap = np * 4 + 1;
        for (i = 0; i < np; i++)
            if (portals[i].d == 0.0f)
                RB_HeapPush(heap, &heapN, 0.0f, i);

        while (heapN > 0)
        {
            rb_heapent_t e = RB_HeapPop(heap, &heapN);
            int s;
            if (e.d > portals[e.node].d)
                continue;                       // beaten by a shorter route already
            for (s = 0; s < 2; s++)
            {
                int sec = (s == 0) ? portals[e.node].secA : portals[e.node].secB;
                int k;
                for (k = secFirst[sec]; k < secFirst[sec + 1]; k++)
                {
                    int   v  = secList[k];
                    float dx = portals[v].x - portals[e.node].x;
                    float dy = portals[v].y - portals[e.node].y;
                    float nd = e.d + sqrtf(dx * dx + dy * dy);
                    if (nd >= portals[v].d || nd >= RB_SEEP_DMAX)
                        continue;
                    portals[v].d = nd;
                    if (heapN < cap)
                        RB_HeapPush(heap, &heapN, nd, v);
                }
            }
        }
    }
    free(heap);

    // --- 4. Rasterise: resolve d PER CELL, not per node ---------------------
    minX = minY =  1e30f;
    maxX = maxY = -1e30f;
    for (i = 0; i < numvertexes; i++)
    {
        float vx = vertexes[i].x / (float)FRACUNIT;
        float vy = vertexes[i].y / (float)FRACUNIT;
        if (vx < minX) minX = vx;
        if (vx > maxX) maxX = vx;
        if (vy < minY) minY = vy;
        if (vy > maxY) maxY = vy;
    }
    if (numvertexes <= 0) { minX = minY = 0.0f; maxX = maxY = 1.0f; }

    // Coarser cells only blur the seep's edge; they cannot break the sealed-room
    // guarantee, because connectivity was decided on the portal graph above --
    // before anything was rasterised.
    cell = RB_SEEP_CELL0;
    for (;;)
    {
        // CEILING, not truncation (DOOM-0276). The ring is only "outside the map" if
        // the interior cells reach PAST maxX/maxY: cell (gw-2)'s centre sits at
        // minX + ceil(dx/cell)*cell >= maxX, so no in-map sample can ever weight the
        // ring in a bilinear tap. Truncating left the last interior centre SHORT of
        // maxX by up to a cell, which handed the void's sentinel a real weight on real
        // air along the +X/+Y edges -- a thin strip of wrong seep, and, once the
        // open-sky mask rides the same texture, a strip of wrong fog.
        gw = (int)ceilf((maxX - minX) / cell) + 3;   // +1 for the partial cell, +2 for the ring
        gh = (int)ceilf((maxY - minY) / cell) + 3;
        if (gw <= RB_SEEP_MAXDIM && gh <= RB_SEEP_MAXDIM)
            break;
        cell *= 2.0f;
    }

    field = (rb_seep_t*)malloc(sizeof(rb_seep_t));
    if (!field)
        I_Error("RB_BuildSeepField: out of memory for the field");
    field->w       = gw;
    field->h       = gh;
    field->cell    = cell;
    field->originX = minX - cell;   // cell (0,0)'s CENTRE, one full cell outside the map
    field->originY = minY - cell;
    field->d       = (float*)malloc((size_t)gw * (size_t)gh * sizeof(float));
    field->sky     = (float*)malloc((size_t)gw * (size_t)gh * sizeof(float));
    field->zLo     = (float*)malloc((size_t)gw * (size_t)gh * sizeof(float));
    field->zHi     = (float*)malloc((size_t)gw * (size_t)gh * sizeof(float));
    field->geom    = malloc((size_t)gw * (size_t)gh * sizeof(rb_cellgeom_t));
    if (!field->d || !field->sky || !field->zLo || !field->zHi || !field->geom)
        I_Error("RB_BuildSeepField: out of memory for %dx%d cells", gw, gh);
    geom = (rb_cellgeom_t*)field->geom;
    // DOOM-0289: the tie-break nudge that keeps a doorway-threshold cell centre off the
    // linedef it sits exactly on, biased toward the air the sun ray is about to enter.
    // Hoisted out of both loops -- the heading is fixed. See RB_SUN_NUDGE on why it is one
    // unit and not a quarter cell.
    (void)RB_SunHeading(&sunUx, &sunUy);
    nudgeX = (fixed_t)(RB_SUN_NUDGE * sunUx * FRACUNIT);
    nudgeY = (fixed_t)(RB_SUN_NUDGE * sunUy * FRACUNIT);

    for (iy = 0; iy < gh; iy++)
    {
        for (ix = 0; ix < gw; ix++)
        {
            float cx = field->originX + ix * cell;
            float cy = field->originY + iy * cell;
            float best = RB_SEEP_DMAX;
            float sky  = 0.0f;
            int   sec, k;

            // The void ring is unreachable by construction. It has to be written
            // explicitly rather than looked up: DOOM's BSP partitions the whole
            // plane, so a point outside the map still lands in some leaf, and that
            // leaf's sector could be an outdoor one -- which would wrap outdoor air
            // onto the map edge under CLAMP_TO_EDGE.
            //
            // DOOM-0289: the ring plays OPPOSITE roles in the two halves of the field,
            // which is why it is spelled out rather than left to a default. For the SEEP
            // it is a blocker (INV-12), unchanged. For the CLEARANCE it is open sky: past
            // the map's bounding box there is no geometry at all, so a march that reaches
            // the ring has ESCAPED. Marking it solid -- which `fz = cz` of any form would,
            // via `solid = (cz <= fz)` -- would block every march leaving the map along
            // +u, and since the shipped kSunDir points at +X/+Y that carves a
            // systematically unlit band two or three cells deep along exactly those edges
            // of every outdoor area. The zLo/zHi writes are not optional bookkeeping
            // either: this branch `continue`s, so they would otherwise reach FloatToHalf
            // as uninitialised malloc bytes.
            if (ix == 0 || iy == 0 || ix == gw - 1 || iy == gh - 1)
            {
                field->d[iy * gw + ix]   = RB_SEEP_DMAX;
                field->sky[iy * gw + ix] = 0.0f;
                field->zLo[iy * gw + ix] = -RB_SUN_NEVER;   // maximally WIDE, not empty
                field->zHi[iy * gw + ix] =  RB_SUN_NEVER;
                geom[iy * gw + ix].fz     = 0.0f;
                geom[iy * gw + ix].cz     = 1e30f;          // a sky ceiling above anything
                geom[iy * gw + ix].sec    = -1;
                geom[iy * gw + ix].sky    = 1;
                geom[iy * gw + ix].isvoid = 0;
                geom[iy * gw + ix].solid  = 0;
                continue;
            }

            sec = RB_CellGeomAtPoint(cx, cy, nudgeX, nudgeY, &geom[iy * gw + ix]);
            if (sec >= 0)
            {
                if (sectors[sec].ceilingpic == skyflatnum)
                {
                    best = 0.0f;                       // this cell IS outdoor air
                    sky  = 1.0f;                       // ...and has no roof over it
                }
                else
                {
                    for (k = secFirst[sec]; k < secFirst[sec + 1]; k++)
                    {
                        rb_portal_t* pt = &portals[secList[k]];
                        float dx = pt->x - cx, dy = pt->y - cy;
                        float nd = pt->d + sqrtf(dx * dx + dy * dy);
                        if (nd < best)
                            best = nd;
                    }
                }
            }
            field->d[iy * gw + ix]   = (best < RB_SEEP_DMAX) ? best : RB_SEEP_DMAX;
            field->sky[iy * gw + ix] = sky;
        }
    }

    // DOOM-0289: the clearance interval, from the geometry cache the loop above just
    // filled. A SECOND double loop, run after the rasterise has finished -- not fused
    // into it. That is not a style preference: the shipped kSunDir is (+0.30, +0.30, ...),
    // so the DDA always walks toward INCREASING ix/iy, which under a single fused loop is
    // exactly the set of cells not yet written. The march would read uninitialised malloc
    // bytes every time, not intermittently, and the resulting field would look plausible
    // rather than obviously broken.
    RB_SunClearanceMarchAll(field);

    free(portals);
    free(secFirst);
    free(secList);
    // DOOM-0281: the openings this field was flooded from are now the baseline that
    // RB_SeepOpeningsChanged diffs against. Re-seeding here (rather than at level
    // load) is what makes the answer mean "stale relative to the LIVE field" on a
    // mid-play re-flood as well as on the first build.
    RB_SeepSeedOpenings();
    return field;
}

// ---------------------------------------------------------------------------
// DOOM-0281: connectivity-change detection for the seep field.
//
// One byte per linedef: 1 if it currently has a vertical opening. A shut DOOM door
// is a two-sided linedef with openrange 0, so this bit -- not one-sidedness -- is
// what the flood treats as a wall, and a flip in it is exactly the event that
// invalidates the field. Sector movement that does NOT cross zero (a lift running
// between two open heights, a crusher not yet at the floor) flips nothing and
// costs nothing beyond the scan.
// ---------------------------------------------------------------------------

static unsigned char* rb_seepOpen  = NULL;   // one byte per linedef
static int            rb_seepOpenN = 0;      // numlines rb_seepOpen is sized for

static int RB_SeepLineOpen(line_t* ld)
{
    // Same three rejections the flood applies to a seg, so the bit tracks what the
    // flood would actually do with this line. P_LineOpening writes file-scope globals
    // and is not re-entrant; this runs on the render thread between tics, where the
    // playsim is not mid-P_TryMove -- the same window RB_BuildSeepField uses.
    if (!(ld->flags & ML_TWOSIDED) || !ld->backsector)
        return 0;
    if (ld->frontsector == ld->backsector)   // self-referencing fake wall
        return 0;
    P_LineOpening(ld);
    return openrange > 0;
}

// Fill the cache from the live map, discarding whatever was there.
static void RB_SeepSeedOpenings(void)
{
    int i;
    if (rb_seepOpenN != numlines)
    {
        free(rb_seepOpen);
        rb_seepOpen = (unsigned char*)malloc((size_t)(numlines > 0 ? numlines : 1));
        if (!rb_seepOpen)
            I_Error("RB_SeepSeedOpenings: out of memory for %d lines", numlines);
        rb_seepOpenN = numlines;
    }
    for (i = 0; i < numlines; i++)
        rb_seepOpen[i] = (unsigned char)RB_SeepLineOpen(&lines[i]);
}

int RB_SeepOpeningsChanged(void)
{
    int i, changed = 0;
    // No baseline yet means no field has been flooded yet, so nothing can be stale.
    if (rb_seepOpenN != numlines)
        return 0;
    for (i = 0; i < numlines; i++)
    {
        unsigned char open = (unsigned char)RB_SeepLineOpen(&lines[i]);
        if (open != rb_seepOpen[i])
        {
            rb_seepOpen[i] = open;
            changed = 1;
        }
    }
    return changed;
}

// DOOM-0011 L3: the air column of one cell, for the fog-light bake. rb_cellgeom_t is
// private to this file -- the bake lives in r_vulkan.cpp because that is where the
// emitter set is, and it needs exactly two numbers per cell: is there air here, and
// between which heights. Everything else about the geometry cache stays behind this
// accessor rather than leaking the struct into a second translation unit.
int RB_SeepCellAir(const rb_seep_t* f, int ix, int iy, float* fz, float* cz)
{
    const rb_cellgeom_t* c;
    if (!f || !f->geom || ix < 0 || iy < 0 || ix >= f->w || iy >= f->h)
        return 0;
    c = &((const rb_cellgeom_t*)f->geom)[iy * f->w + ix];
    if (c->solid)
        return 0;                       // wall, void, or a shut door: no air to light
    if (fz) *fz = c->fz;
    if (cz) *cz = c->cz;
    return 1;
}

void RB_FreeSeepField(rb_seep_t* f)
{
    if (!f)
        return;
    free(f->d);
    free(f->sky);
    free(f->zLo);       // DOOM-0289
    free(f->zHi);
    free(f->geom);      // the retained per-cell geometry cache
    free(f);
}

int RB_BuildSubsectorSectors(int* out, int n)
{
    int count = (numsubsectors < n) ? numsubsectors : n;
    int i;
    for (i = 0; i < count; i++)
    {
        subsector_t* ss = &subsectors[i];
        out[i] = (ss->numlines > 0)
               ? (int)(segs[ss->firstline].frontsector - sectors)
               : -1;
    }
    return count;
}

int RB_UpdateMeshHeights(const rb_mesh_t* mesh, rb_vertex_t* dst)
{
    int i;
    int moved = 0;          // any z actually changed this frame -> RT refit (step 5)
    int retex = 0;          // any live texture id changed -> NEE emitter rebuild (DOOM-0082)
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
        int newtex = dst[i].texnum;
        if (v->flags & RB_MESH_FLAT)
        {
            int pic = (v->vplane == RB_PLANE_CEIL)
                      ? sectors[v->vsector].ceilingpic
                      : sectors[v->vsector].floorpic;
            // DOOM-0073: pic/base are WAD-loaded shorts re-read every frame; a
            // corrupt map could push them out of range. flattranslation and
            // texturetranslation are sized numflats/numtextures, so bound the
            // lookups to those (skip the update, keeping the baked id) rather than
            // reading past the array each frame.
            if (pic != skyflatnum && pic >= 0 && pic < numflats)
                newtex = flattranslation[pic];
        }
        else if (v->vtexside >= 0)
        {
            const side_t* sd = &sides[v->vtexside];
            int base = v->vtexslot == 0 ? sd->toptexture
                     : v->vtexslot == 2 ? sd->bottomtexture
                     : sd->midtexture;
            if (base > 0 && base < numtextures)
                newtex = texturetranslation[base];
        }
        // A pressed/reverted switch or an animated texture changes the live id;
        // flag it so the RT back-end refreshes the NEE emitter set (DOOM-0082).
        if (newtex != dst[i].texnum)
        {
            dst[i].texnum = newtex;
            retex = 1;
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
    return (moved ? RB_UPD_MOVED : 0) | (retex ? RB_UPD_RETEX : 0);
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

// DOOM-0157: sprite_glows() is keyed by spritenum, but the emissive back-end derives
// per-material Le keyed by ATLAS LUMP (one tile per sprite-lump). This maps each
// sprite lump back to "belongs to a glowing collectible", so ComputeMaterialEmissive
// can grant those lumps a guaranteed faint Le — their glowing bit (skull eyes, armour
// gleam) is only a few texels, below the room-lighting emitter gate, so without this
// they derive Le=0 and stay dark even in an unlit room. Built once, cached.
static boolean* sprite_lump_glow;

static void ensure_sprite_glow_map(void)
{
    int sp, fr, rot, l;
    if (sprite_lump_glow || numspritelumps <= 0)
        return;
    sprite_lump_glow = calloc((size_t)numspritelumps, sizeof(boolean));
    if (!sprite_lump_glow)
        I_Error("ensure_sprite_glow_map: out of memory for %d sprite lumps",
                numspritelumps);
    for (sp = 0; sp < numsprites; sp++)
    {
        if (!sprite_glows((spritenum_t)sp))
            continue;
        for (fr = 0; fr < sprites[sp].numframes; fr++)
        {
            spriteframe_t* f = &sprites[sp].spriteframes[fr];
            for (rot = 0; rot < 8; rot++)
            {
                l = f->lump[rot];               // -1 for an unused rotation slot
                if (l >= 0 && l < numspritelumps)
                    sprite_lump_glow[l] = true;
            }
        }
    }
}

// True when sprite-atlas lump `lump` (0-based, as carried on a billboard vertex's
// texnum) belongs to a glowing collectible. The C++ Vulkan back-end calls this across
// the seam, so it returns int (r_mesh.h stays DOOM-type-free).
int RB_SpriteLumpGlows(int lump)
{
    ensure_sprite_glow_map();
    if (!sprite_lump_glow || lump < 0 || lump >= numspritelumps)
        return 0;
    return sprite_lump_glow[lump] ? 1 : 0;
}

// DOOM-0307: which WALL textures are light sources.
//
// The emissive back-end derives Le from texels alone, and texels cannot answer this.
// DOOM's palette ramps nearly all top out at 255 in some channel, and the gate rates
// brightness by VALUE (max channel), so the palest cement highlight scores exactly as
// pure fire does. FIREWALL and AASHITTY clear the gate on the SAME two palette entries
// (#176, #175); SW1CMT clears it on the cement wall BEHIND the switch, which is why its
// Le (12.86) sat beside CEMENT1's (15.32). Measured across both IWADs 2026-08-03: no
// texel statistic separates the two classes — peak fraction, saturation, hue, tile and
// peak luminance, global contrast, local ring contrast, blob count and blob size all
// have the light/not-light ranges fully overlapping, and the best two-feature threshold
// rule still rejects FIREWALL. So the question is answered by NAME, judged from the art.
//
// This list REPLACES the peak gate for walls rather than narrowing it. The gate's job
// was to guess this and it guessed wrong in both directions: 82 of doom2's 428 walls
// emitted (nine CEMENTs, every ZZWOLF, the skin walls), while DOOM's own light panels —
// LITE3, LITE5, LITEBLU*, COMPSTA* — peak at only ~0.53 linear, never cleared it, and
// were dark. On the list, a tile emits whatever its bright texels come to; off it, a
// tile is not a light however bright its art. Flats and sprites are untouched (liquids
// are forced by name in ForceLiquidEmissive, sprites have RB_SpriteLumpGlows).
//
// Names absent from the loaded IWAD are skipped, so one table serves DOOM and DOOM II.
static char wall_light_tex[][9] = {
    // Fire, lava and hell glow
    "FIREWALL", "FIREWALA", "FIREWALB", "FIREBLU1", "FIREBLU2",
    "FIRELAVA", "FIRELAV2", "FIRELAV3", "FIREMAG1", "FIREMAG2", "FIREMAG3",
    "ROCKRED1", "ROCKRED2", "ROCKRED3", "CRACKLE2", "CRACKLE4", "REDWALL1",
    "DBRAIN1",  "DBRAIN2",  "DBRAIN3",  "DBRAIN4",
    // Light panels and wall fixtures
    "LITE3",    "LITE5",    "LITEBLU1", "LITEBLU2", "LITEBLU3", "LITEBLU4",
    "LITERED",  "LITESTON", "TEKLITE",  "TEKLITE2", "BRICKLIT",
    // Ordinary walls carrying a lit strip or slot (METAL6/7 are the same strip at
    // the top and at the bottom — hence their identical texel counts)
    "METAL6",   "METAL7",   "BIGBRIK3", "BRONZE4",  "TEKGREN5", "SPCDOOR1",
    "SPCDOOR2", "TEKBRON2", "BSTONE3",  "SILVER2",  "EXITDOOR",
    // Computer screens and readouts
    "COMPSTA1", "COMPSTA2", "COMPUTE1", "COMPUTE2", "COMPUTE3", "PLANET1",
    "TEKWALL1", "TEKWALL2", "TEKWALL3", "TEKWALL4", "TEKWALL5", "TEKWALL6",
    // Switches whose face carries a LIT indicator (DOOM-0082). The SW1/SW2 skull and
    // gargoyle switches are bronze relief, not lit, and are deliberately absent.
    "SW2COMP",  "SW2EXIT",  "SW2GRAY",  "SW2STON6",
};

static byte* wall_tex_light;   // per-texture "is a light source" flag. Built once, cached.

static void ensure_wall_light_map(void)
{
    int i, t;
    if (wall_tex_light || numtextures <= 0)
        return;
    wall_tex_light = calloc((size_t)numtextures, sizeof(byte));
    if (!wall_tex_light)
        I_Error("ensure_wall_light_map: out of memory for %d wall textures",
                numtextures);
    for (i = 0; i < (int)(sizeof(wall_light_tex) / sizeof(wall_light_tex[0])); i++)
    {
        t = R_CheckTextureNumForName(wall_light_tex[i]);
        if (t >= 0 && t < numtextures)
            wall_tex_light[t] = 1;
    }
}

// True when wall texture `texnum` (a unified atlas id below numwall) is a light
// source. The C++ Vulkan back-end calls this across the seam, so it returns int
// (r_mesh.h stays DOOM-type-free), mirroring RB_SpriteLumpGlows.
int RB_WallTexEmits(int texnum)
{
    ensure_wall_light_map();
    if (!wall_tex_light || texnum < 0 || texnum >= numtextures)
        return 0;
    return wall_tex_light[texnum] ? 1 : 0;
}

int RB_BuildSprites(const rb_view_t* view, rb_vertex_t* out, int maxverts)
{
    // Camera right vector in world space, matching Mat4LookAt with up = +z:
    // right = normalize(fwd x up) = (sin a, -cos a, 0). Billboard up is +z, and
    // the normal faces the camera (placeholder shading only).
    float   a    = view->angle;
    float   rx   =  sinf(a), ry = -cosf(a);    // screen-right in world
    float   nx   = -cosf(a), ny = -sinf(a);    // billboard normal (toward eye)
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
                // DOOM-0073: derive the view->thing angle from float deltas. The old
                // (fixed_t)(view->x*FRACUNIT) overflowed int32 at the +/-32768 map
                // edge, giving a wrong sprite rotation for that frame. angle_t is a
                // 32-bit BAM (2^32 == 360deg); map atan2's [-pi,pi] onto it via an
                // int64 cast (negative wraps to the correct unsigned angle).
                float    dx  = thing->x / (float)FRACUNIT - view->x;
                float    dy  = thing->y / (float)FRACUNIT - view->y;
                angle_t  ang = (angle_t)(long long)
                    (atan2f(dy, dx) * (4294967296.0 / (2.0 * 3.14159265358979)));
                unsigned rot = (ang - thing->angle + (unsigned)(ANG45/2)*9) >> 29;
                lump = sprframe->lump[rot];
                flip = (boolean)sprframe->flip[rot];
            }
            else
            {
                lump = sprframe->lump[0];
                flip = (boolean)sprframe->flip[0];
            }
            // DOOM-0073: an unassigned rotation is lump == -1 (R_InstallSpriteLump
            // validates at load, so this is unreachable with stock WADs); guard it
            // rather than index spritewidth[]/the atlas with a negative lump.
            if (lump < 0)
                continue;

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

// DOOM-0170 L2d — blob shadows. One horizontal quad on the floor beneath each solid
// or pickup Thing (monsters, barrels, keys/items), so things read as grounded even in
// a room with no dominant light (§4.5). The quad's four corners carry [-1,1] radial UV;
// blob.frag turns |uv| into a soft dark oval and fades it by the sector light. Skips the
// player's own body, projectiles/puffs (MF_MISSILE), and non-solid non-pickup decor
// (corpses, blood), which have no grounding to cue. Mirrors RB_BuildSprites' thing walk.
int RB_BuildBlobs(const rb_view_t* view, rb_vertex_t* out, int maxverts)
{
    int s, n = 0;
    (void)view;                                 // world-space; no view-facing needed
    for (s = 0; s < numsectors; s++)
    {
        mobj_t* thing;
        for (thing = sectors[s].thinglist; thing; thing = thing->snext)
        {
            float cx, cy, fz, rad, light;

            if (thing == players[consoleplayer].mo)
                continue;                       // no shadow under the first-person body
            if (!(thing->flags & (MF_SOLID | MF_SPECIAL)))
                continue;                       // corpses/blood/decals: nothing to ground
            if (thing->flags & MF_MISSILE)
                continue;                       // projectiles/puffs don't sit on the floor

            cx    = thing->x / (float)FRACUNIT;
            cy    = thing->y / (float)FRACUNIT;
            fz    = thing->floorz / (float)FRACUNIT + 1.0f;   // 1 unit up: hug floor, no z-fight
            rad   = (thing->radius / (float)FRACUNIT) * 1.35f; // a touch wider than the hitbox
            light = thing->subsector->sector->lightlevel / 255.0f;
            if (light < 0.0f) light = 0.0f;
            if (light > 1.0f) light = 1.0f;

            if (n + 6 > maxverts)
                return n;                       // buffer full; drop the rest

            // Horizontal quad (normal +z), UV = radial corner coords for blob.frag.
            {
                rb_vertex_t a = mkv(cx - rad, cy - rad, fz, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0, RB_MESH_BLOB, light);
                rb_vertex_t b = mkv(cx + rad, cy - rad, fz, 0.0f, 0.0f, 1.0f,  1.0f, -1.0f, 0, RB_MESH_BLOB, light);
                rb_vertex_t c = mkv(cx + rad, cy + rad, fz, 0.0f, 0.0f, 1.0f,  1.0f,  1.0f, 0, RB_MESH_BLOB, light);
                rb_vertex_t d = mkv(cx - rad, cy + rad, fz, 0.0f, 0.0f, 1.0f, -1.0f,  1.0f, 0, RB_MESH_BLOB, light);
                out[n++] = a; out[n++] = b; out[n++] = c;
                out[n++] = a; out[n++] = c; out[n++] = d;
            }
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
        if (lump < 0)                           // like RB_BuildSprites: a missing
            continue;                           // rotation lump is -1 (OOB guard)

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
