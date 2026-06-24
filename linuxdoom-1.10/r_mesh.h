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
//    DOOM-0008 Stage 1 geometry: convert the loaded 2.5D level (segs +
//    subsectors) into a flat list of 3D world-space triangles for the Vulkan
//    back-end to upload. Pure C, POD output — no Vulkan or C++ here, so the
//    C++ back-end can consume it across the extern "C" seam without pulling in
//    the (not C++-clean) DOOM engine headers.
//
//    Non-indexed: every 3 vertices form one triangle. Indexing/dedup is a
//    later size optimisation (BLAS footprint), not needed to get an image.
//
//-----------------------------------------------------------------------------

#ifndef __R_MESH__
#define __R_MESH__

// POD-only and DOOM-header-free, so the C++ Vulkan back-end can include this
// directly across the seam (the functions keep C linkage).
#ifdef __cplusplus
extern "C" {
#endif

// Per-vertex attributes. Positions/normals are float world units
// (fixed / FRACUNIT); UVs are raw DOOM texel coords (the shader divides by the
// texture/flat dimensions). Axes: x east, y north, z up.
typedef struct
{
    float x, y, z;      // world position
    float nx, ny, nz;   // surface normal (unit)
    float u, v;         // texel coordinates (pre-wrap; the shader tiles by the
                        // texture's atlas-rect size)
    int   texnum;       // wall texture index, or flat index when RB_MESH_FLAT
    int   flags;        // RB_MESH_* bits
    float light;        // owning sector lightlevel, 0..1
} rb_vertex_t;

// rb_vertex_t.flags bits.
#define RB_MESH_FLAT    0x1   // texnum indexes flats[], not textures[]
#define RB_MESH_MASKED  0x2   // two-sided middle texture (alpha-tested)
#define RB_MESH_SPRITE  0x4   // billboard sprite (texnum indexes sprite lumps;
                              // palette index 0 is transparent / alpha-tested)
#define RB_MESH_PSPRITE 0x8   // player weapon overlay: position is already in
                              // clip space (NDC), so the vertex shader skips the
                              // view-projection. Carries RB_MESH_SPRITE too.

typedef struct
{
    rb_vertex_t* verts;     // numverts entries, owned by this struct
    int          numverts;  // always a multiple of 3
    int          numtris;   // numverts / 3
} rb_mesh_t;

// Build the current level's mesh from the globals p_setup.c populated
// (segs/subsectors/sectors). Returns a heap-owned mesh; call RB_FreeMesh on it.
// Must be called after P_SetupLevel has loaded the map.
rb_mesh_t* RB_BuildLevelMesh(void);

void RB_FreeMesh(rb_mesh_t* mesh);

//
// Texture atlas for per-texel sampling (DOOM-0008 materials slice). Every wall
// texture and flat is packed, as raw 8-bit palette indices, into one R8 atlas
// image; the GPU shader looks each surface up by a unified id and decodes the
// index through the PLAYPAL colour table. Keeping the art paletted (index +
// palette LUT, not pre-decoded RGB) preserves DOOM's exact colours and leaves
// the door open for palette-flash effects as a single LUT-row swap later.
//

// One packed tile, addressed by id. ox/oy = origin in the atlas (texels);
// w/h = the tile's size in texels (the shader tiles UVs by this). Floats so the
// array uploads straight into a std430 storage buffer (vec4 per rect).
typedef struct
{
    float ox, oy, w, h;
} rb_rect_t;

typedef struct
{
    unsigned char* pixels;        // atlasw*atlash palette indices, heap-owned
    int            atlasw, atlash;
    rb_rect_t*     rects;         // numwall+numflat+numsprite entries, in that
                                  // order: flat id = numwall + flatnum, sprite
                                  // id = numwall + numflat + spritelump
    int            numwall;       // wall-texture rects (== numtextures)
    int            numflat;       // flat rects (== numflats)
    int            numsprite;     // sprite-lump rects (== numspritelumps)
    unsigned char  playpal[256 * 3];  // palette 0, straight RGB
} rb_atlas_t;

// Pack every wall texture, flat, and sprite lump into one paletted atlas.
// WAD-global and constant for the session, so the back-end builds it once and
// reuses it across levels. Heap-owned; release with RB_FreeAtlas.
rb_atlas_t* RB_BuildAtlas(void);

void RB_FreeAtlas(rb_atlas_t* atlas);

// POD camera handed across the seam each frame (RB_Vulkan_RenderView). The C
// side (r_backend.c) reads the player's view globals and converts to float
// world units / radians so the C++ back-end needs no DOOM headers. Axes match
// the mesh: x east, y north, z up; angle is yaw (0 = +x east, CCW positive).
typedef struct
{
    float x, y, z;   // eye position, world units
    float angle;     // yaw, radians
} rb_view_t;

// Build this frame's billboard sprites (things + items) as camera-facing quads,
// writing rb_vertex_t triangles into out[] (up to maxverts). Returns the vertex
// count written (a multiple of 6). The view supplies the camera so each quad
// faces the view plane, and picks the 8-way sprite rotation, exactly as the
// software renderer does. texnum carries the sprite-lump index (atlas id =
// numwall + numflat + texnum); flags carry RB_MESH_SPRITE. Rebuilt every frame
// because things move; must be called after a level is loaded.
int RB_BuildSprites(const rb_view_t* view, rb_vertex_t* out, int maxverts);

// Build the player's weapon/flash psprites as a screen-space overlay, writing
// rb_vertex_t triangles into out[] (up to maxverts). Positions are emitted in
// Vulkan NDC (the 320x200 HUD space mapped to the full frame) with z=0 so the
// weapon draws on top of the world; flags carry RB_MESH_SPRITE|RB_MESH_PSPRITE.
// Returns the vertex count (a multiple of 6). Rebuilt every frame.
int RB_BuildPSprites(rb_vertex_t* out, int maxverts);

#ifdef __cplusplus
}
#endif

#endif
