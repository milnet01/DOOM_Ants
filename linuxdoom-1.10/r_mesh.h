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
    int   vsector;      // owning sector index, for the dynamic-height lookup below
    int   vplane;       // RB_PLANE_* : which sector plane this vertex's z tracks
    // Texture-pegging anchor (walls only). DOOM pegs a wall's texture to one
    // edge; on a moving sector (door/lift) that edge slides, and the texture must
    // slide with it at 1 texel per world unit -- not stretch. So the texture row
    // 0 sits at world height (sectors[vtexsec].<vtexplane> + vtexoff), and
    // RB_UpdateMeshHeights re-derives v = that - z each frame. vtexplane==
    // RB_PLANE_NONE means "static texture" (flats; never re-pegged). DOOM-0067.
    int   vtexsec;      // sector whose plane the texture is pegged to
    int   vtexplane;    // RB_PLANE_* of that anchor plane (NONE = don't re-peg)
    float vtexoff;      // texels from the anchor plane to texture row 0
    // Live-texture source (walls only). A switch swaps the sidedef's texture
    // (P_ChangeSwitchTexture) and animated walls/flats cycle a translation table
    // each tic, neither of which the once-baked texnum reflects. So a wall vertex
    // records the sidedef + which slot it drew from; RB_UpdateMeshHeights re-reads
    // texturetranslation[sides[vtexside].<slot>] each frame. vtexside < 0 means a
    // flat (refreshed via vsector/vplane + flattranslation) or static. DOOM-0066.
    int   vtexside;     // sidedef index, or -1 (flat / no re-texture)
    int   vtexslot;     // 0 = top, 1 = mid, 2 = bottom texture
} rb_vertex_t;

// Which moving sector plane a mesh vertex's z follows. The mesh is built once
// with heights baked into z, but doors/lifts change sector floor/ceiling
// heights every tic; tagging each vertex lets RB_UpdateMeshHeights re-height the
// moving ones per frame with no mesh rebuild (DOOM-0049). vsector/vplane are
// not GPU vertex attributes (the pipeline reads only position/normal/uv/...),
// so they ride along free in the vertex stride.
#define RB_PLANE_NONE   0   // static: z never changes
#define RB_PLANE_FLOOR  1   // z follows sectors[vsector].floorheight
#define RB_PLANE_CEIL   2   // z follows sectors[vsector].ceilingheight

// rb_vertex_t.flags bits.
#define RB_MESH_FLAT    0x1   // texnum indexes flats[], not textures[]
#define RB_MESH_MASKED  0x2   // two-sided middle texture (alpha-tested)
#define RB_MESH_SPRITE  0x4   // billboard sprite (texnum indexes sprite lumps;
                              // palette index 0 is transparent / alpha-tested)
#define RB_MESH_PSPRITE 0x8   // player weapon overlay: position is already in
                              // clip space (NDC), so the vertex shader skips the
                              // view-projection. Carries RB_MESH_SPRITE too.
#define RB_MESH_SKY     0x10  // full-screen sky backdrop: NDC like a psprite, but
                              // the fragment shader samples the sky texture by
                              // view yaw + screen position (cylindrical), fullbright.
#define RB_MESH_EMISSIVE 0x20 // DOOM-0084: this sprite Thing is a light source (its
                              // current frame is FF_FULLBRIGHT — lamps/torches/barrels,
                              // not ammo pickups). Only these self-glow + cast NEE light
                              // in the path tracer; the per-texel value mask then
                              // localises the glow to the sprite's bright pixels.

// Transparent-key palette index for the 2D HUD/menu compositor (DOOM-0008). In
// the 3D back-ends the engine still draws every 2D element (status bar, menu,
// messages) into the paletted screens[0]; the 3D view's footprint there is
// cleared to this index each frame (r_backend.c) and the overlay shader
// (overlay.frag) discards it, so the rendered 3D scene shows through. Index 251
// is pure magenta (255,0,255), unused by any DOOM HUD/menu/font art.
#define RB_OVERLAY_KEY  251

typedef struct
{
    rb_vertex_t* verts;     // numverts entries, owned by this struct
    int          numverts;  // always a multiple of 3
    int          numtris;   // numverts / 3
    int*         tri_ss;    // per-triangle subsector index (numtris entries), so a
                            // ray hit can find its room's GI probe (DOOM-0009 step
                            // 4c). -1 if untagged. Owned by this struct.
    // DOOM-0141: sky backdrop geometry (sky ceilings + sky-border walls), kept
    // SEPARATE from the world mesh above. Classic DOOM draws a solid sky backdrop
    // that occludes everything behind it; the world mesh omits sky surfaces, so in
    // the ray-traced view rays escaped through the gap (distant geometry "floated").
    // These tris ride their own acceleration structure on a ray mask only PRIMARY
    // rays see, so they occlude the view without casting shadows or perturbing the
    // GI bake, and the raster path (its own RB_BuildSky quad) never draws them.
    // texnum on each vertex is the sky wall-texture (skytexture); flags = RB_MESH_SKY.
    rb_vertex_t* sky;       // numsky entries (multiple of 3), or NULL. Owned here.
    int          numsky;    // sky vertex count (always a multiple of 3)
} rb_mesh_t;

// Build the current level's mesh from the globals p_setup.c populated
// (segs/subsectors/sectors). Returns a heap-owned mesh; call RB_FreeMesh on it.
// Must be called after P_SetupLevel has loaded the map.
rb_mesh_t* RB_BuildLevelMesh(void);

void RB_FreeMesh(rb_mesh_t* mesh);

// Re-height a built mesh from the current sector floor/ceiling heights, writing
// into dst[] (which must hold mesh->numverts rb_vertex_t -- e.g. the mapped GPU
// vertex buffer). Only vertices tagged RB_PLANE_FLOOR/CEIL have their z rewritten
// (to the live sector plane height); static vertices are untouched. Call once
// per frame so doors and lifts animate without rebuilding the mesh (DOOM-0049).
// Returns non-zero if any vertex z actually changed this frame (a door/lift is
// mid-move) so the RT back-end can refit its acceleration structure only when the
// geometry really moved (DOOM-0009 build step 5).
int RB_UpdateMeshHeights(const rb_mesh_t* mesh, rb_vertex_t* dst);

// DOOM-0009 build step 4 (static GI bake): one irradiance probe per subsector.
// The position is the subsector's convex BSP-leaf centroid (mean of its seg
// endpoints) at mid-height ((floor+ceiling)/2), in world units (fixed/FRACUNIT,
// matching the mesh). Subsectors are convex and wall-bounded, so a centroid probe
// sits inside the cell and the GI bake it anchors cannot leak light through a wall
// (the per-subsector scheme chosen by the step-4a measurement). The bake fills a
// directional-irradiance payload per probe on the GPU; this only places them.
typedef struct { float x, y, z; } rb_probe_t;

// Write one probe per subsector into out[] (in subsector order, so out[i] is
// subsectors[i]); returns the count written (min(numsubsectors, maxprobes)).
int RB_BuildProbes(rb_probe_t* out, int maxprobes);

// Subsector count of the loaded level = the probe count. Lets the C++ back-end
// size the probe array without reaching past the mesh seam into engine globals.
int RB_NumSubsectors(void);

// DOOM-0119 (REJECT-lump light culling). The path tracer skips emitter sectors the
// WAD's REJECT visibility lump marks invisible from a shading point's sector, before
// casting a shadow ray. These hand the sector mapping + the matrix across the C/C++
// seam (the back-end has no DOOM headers). All valid after P_SetupLevel.

// Sector count of the loaded level (the REJECT matrix is numsectors x numsectors bits).
int RB_NumSectors(void);

// The packed REJECT bitmatrix and its byte length (set *sizeBytes). Bit
// (s1*numsectors + s2) set => sector s2 is provably never visible from s1 (DOOM's
// P_CheckSight convention, row-major LSB-first). Returns NULL / *sizeBytes 0 if the
// level has no REJECT lump (then the caller skips the cull).
const unsigned char* RB_RejectMatrix(int* sizeBytes);

// Sector index containing world point (x, y) in float world units (the mesh's
// units), via the BSP. -1 if degenerate. Used to tag a sprite emitter (a glowing
// Thing) with its room for the REJECT cull.
int RB_SectorAtPoint(float x, float y);

// Owning sector index of each subsector, written into out[] in subsector order
// (matching RB_BuildProbes / the mesh tri_ss map), up to n entries; returns the
// count. -1 for a degenerate (line-less) leaf. Lets a ray hit -> subsector ->
// sector without crossing the seam to the DOOM globals.
int RB_BuildSubsectorSectors(int* out, int n);

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

// Total material count: wall textures + flats + sprite lumps, the size of the
// bindless texture array (DOOM-0009 build step 1). WAD-global and known right
// after R_Init, so the C++ back-end can size the variable-count descriptor array
// at device-init time — before the atlas itself is built — without crossing the
// seam to the DOOM texture globals. Matches the id ordering RB_BuildAtlas packs:
// walls [0,numwall), flats next, sprites last.
int RB_MaterialCount(void);

// WAD-global PLAYPAL (palette 0): 256 straight-RGB triples, pointing at the
// cached lump. The back-end builds its colour LUT from this at init so the 2D
// HUD/menu overlay can composite from the very first frame -- the title/demo
// screen, before any level or texture atlas exists (DOOM-0045).
const unsigned char* RB_PlayPal(void);

// POD camera handed across the seam each frame (RB_Vulkan_RenderView). The C
// side (r_backend.c) reads the player's view globals and converts to float
// world units / radians so the C++ back-end needs no DOOM headers. Axes match
// the mesh: x east, y north, z up; angle is yaw (0 = +x east, CCW positive).
typedef struct
{
    float x, y, z;       // eye position, world units
    float angle;         // yaw, radians
    float extralight;    // muzzle-flash view brighten, [0,1] added to every shade
                         // (classic DOOM's player->extralight, see r_backend.c)
    int   skytexnum;     // sky wall-texture index (DOOM's skytexture global); the
                         // sky backdrop samples this atlas tile, see RB_BuildSky
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
// Vulkan NDC with z=0 so the weapon draws on top of the world; flags carry
// RB_MESH_SPRITE|RB_MESH_PSPRITE. The 320x200 HUD space maps to the frame with
// x scaled by (4:3)/aspect about screen centre, so the gun keeps DOOM's pixel
// aspect instead of stretching on widescreen. Returns the vertex count (a
// multiple of 6). Rebuilt every frame.
int RB_BuildPSprites(rb_vertex_t* out, int maxverts, float aspect);

// Build the sky backdrop as one full-screen quad in Vulkan NDC (z=0), flagged
// RB_MESH_SKY with texnum = view->skytexnum. Drawn behind the world (depth test
// off) so it shows through sky-flat openings; the fragment shader maps each
// screen pixel to a sky-texture texel by view yaw (cylindrical panorama, 90 deg
// of yaw per texture width) so it pans as the player turns. Returns the vertex
// count written (6, or 0 if out has no room).
int RB_BuildSky(const rb_view_t* view, rb_vertex_t* out, int maxverts);

#ifdef __cplusplus
}
#endif

#endif
