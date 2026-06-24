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
#include "m_fixed.h"
#include "r_defs.h"
#include "r_state.h"    // segs/subsectors/sectors + counts, firstflat, textureheight
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
    rb_vertex_t out;
    out.x = x;   out.y = y;   out.z = z;
    out.nx = nx; out.ny = ny; out.nz = nz;
    out.u = u;   out.v = v;
    out.texnum = texnum; out.flags = flags; out.light = light;
    return out;
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
}

// Copy one tile's palette indices into the atlas at (ox,oy).
static void blit_tile(unsigned char* dst, int dstw, int id, int ox, int oy,
                      int w, int h)
{
    int col, row;
    if (id < numtextures)
    {
        for (col = 0; col < w; col++)
        {
            const byte* src = R_GetColumn(id, col);   // contiguous top..bottom
            for (row = 0; row < h; row++)
                dst[(oy + row) * dstw + (ox + col)] = src[row];
        }
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
                    int y = column->topdelta + row;
                    if (y >= 0 && y < h)
                        dst[(oy + y) * dstw + (ox + col)] = src[row];
                }
                column = (const column_t*)
                    ((const byte*)column + column->length + 4);
            }
        }
    }
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

            // UVs inset half a texel so nearest sampling stays inside the rect.
            u0 = 0.5f;  u1 = wpx - 0.5f;
            v0 = 0.5f;  v1 = hpx - 0.5f;
            if (flip) { float t = u0; u0 = u1; u1 = t; }

            if (n + 6 > maxverts)
                return n;                       // buffer full; drop the rest

            // Corners: left-top, right-top, right-bottom, left-bottom.
            out[n++] = mkv(cx + rx*ld, cy + ry*ld, topz, nx, ny, 0.0f, u0, v0, lump, RB_MESH_SPRITE, light);
            out[n++] = mkv(cx + rx*rd, cy + ry*rd, topz, nx, ny, 0.0f, u1, v0, lump, RB_MESH_SPRITE, light);
            out[n++] = mkv(cx + rx*rd, cy + ry*rd, botz, nx, ny, 0.0f, u1, v1, lump, RB_MESH_SPRITE, light);
            out[n++] = mkv(cx + rx*ld, cy + ry*ld, topz, nx, ny, 0.0f, u0, v0, lump, RB_MESH_SPRITE, light);
            out[n++] = mkv(cx + rx*rd, cy + ry*rd, botz, nx, ny, 0.0f, u1, v1, lump, RB_MESH_SPRITE, light);
            out[n++] = mkv(cx + rx*ld, cy + ry*ld, botz, nx, ny, 0.0f, u0, v1, lump, RB_MESH_SPRITE, light);
        }
    }
    return n;
}


// Player weapon / muzzle-flash overlay. The psprites live in DOOM's 320x200 HUD
// space; mirror R_DrawPSprite's placement (with pspritescale = 1, i.e. straight
// into 320x200) and map that rect to Vulkan NDC over the whole frame. z=0 plants
// the quad at the near plane so the depth test (LESS) always lets it sit on top
// of the world. Screen-space, so no camera/view is needed.
int RB_BuildPSprites(rb_vertex_t* out, int maxverts)
{
    player_t* player;
    float     baselight;
    int       i, n = 0;
    int       fl = RB_MESH_SPRITE | RB_MESH_PSPRITE;

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
        nx0 = lx / 160.0f - 1.0f;
        nx1 = rx / 160.0f - 1.0f;
        ny0 = ty / 100.0f - 1.0f;
        ny1 = by / 100.0f - 1.0f;

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
