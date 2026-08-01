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
#define RB_MESH_BLOB    0x80  // DOOM-0170 L2d: blob-shadow decal — a horizontal quad on
                              // the floor under a Thing, drawn by blob.frag as a soft
                              // dark radial oval (u,v carry [-1,1] radial coords). Uses
                              // mesh.vert's MVP path; a separate alpha-blend pipeline.
#define RB_MESH_SKYDOME 0x40  // DOOM-0162: world-space sky occluder (emit_sky_wall/
                              // emit_sky_cap). Painted as the sky panorama like
                              // RB_MESH_SKY, but its verts are WORLD-space, so the
                              // vertex shader runs the view-projection on them (unlike
                              // the NDC backdrop quad) and the depth test lets them
                              // occlude distant geometry in the raster view.

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
    // GI bake. DOOM-0162: the raster path now ALSO draws these (depth-tested) so
    // distant geometry is occluded by sky there too, alongside its own full-screen
    // RB_BuildSky backdrop quad.
    // texnum on each vertex is the sky wall-texture (skytexture); flags = RB_MESH_SKYDOME.
    rb_vertex_t* sky;       // numsky entries (multiple of 3), or NULL. Owned here.
    int          numsky;    // sky vertex count (always a multiple of 3)
    // DOOM-0011: the altitude the outdoor fog layer sits on, in world units — the
    // LOWEST floor among this level's open-sky sectors (0 if the level has none, in
    // which case nothing reads it: the fog's own up-ray never reports open sky).
    // The fog needs ONE ground height for the whole view. Deriving it per pixel from
    // whatever the ray happened to hit made a floor pixel and the wall pixel beside it
    // disagree about where the cloud was, which is visible as fog on the walls and none
    // on the ground in front of them. Density must be a function of a point in space
    // and nothing else. Known limit: one deep outdoor pit drags the layer down under
    // the rest of the map, thinning it everywhere; E1M1's open-sky floors span 80 units
    // against a 112-unit falloff, so it does not bite there.
    float        fogFloorZ;
} rb_mesh_t;

// Build the current level's mesh from the globals p_setup.c populated
// (segs/subsectors/sectors). Returns a heap-owned mesh; call RB_FreeMesh on it.
// Must be called after P_SetupLevel has loaded the map.
rb_mesh_t* RB_BuildLevelMesh(void);

// DOOM-0011 L1d: the outdoor-proximity seep field (spec §4.3a amendment).
// A coarse 2-D grid over the map holding, per cell, the distance to outdoor air
// THROUGH OPEN SPACE -- i.e. the walk through doorways and windows, never the
// straight line through a wall. The fog uses it to fade indoor haze in at a
// threshold and out as you go deeper, instead of the flat indoor floor L1b ships
// (which cuts off hard at a roofline). Built once per level; the runtime cost is
// one texture tap per fog sample.
//
// Distances are in world units and saturate at RB_SEEP_DMAX, which doubles as the
// "unreachable" sentinel for a sealed room and for the void ring around the map.
// It is FINITE deliberately: an infinity here would meet a zero bilinear weight at
// the grid edge and produce a NaN, which propagates through the whole fog march.
#define RB_SEEP_FALLOFF  384.0f                    // must match kSeepFalloff (pt_common.glsl)
#define RB_SEEP_DMAX     (8.0f * RB_SEEP_FALLOFF)  // must match dMax        (pt_common.glsl)

// DOOM-0289. MIRROR of pt_common.glsl's kSunDir -- THE SHADER IS AUTHORITATIVE.
// Same relationship dMax/RB_SEEP_DMAX already has, but a drift here is worse than that
// one: the beam and its shadows would point in different directions, and nothing fails
// to build. The triple need NOT be normalised -- both values derived from it (the
// horizontal heading and the slope) are invariant under a uniform scale.
#define RB_SUN_DIR_X       0.30f
#define RB_SUN_DIR_Y       0.30f
#define RB_SUN_DIR_Z       1.00f
#define RB_SUN_NEVER       128.0f   // sentinel offset past a cell's own air column (Q28)
// Tie-break offset for the void test, WORLD UNITS -- and small on purpose. DOOM geometry
// is 64-unit aligned and the seep cell is 64 units, so a cell centre landing exactly on a
// linedef is systematic rather than incidental, and P_PointOnLineSide resolves exactly-on
// to "back" -- which would mark a doorway-threshold cell solid and delete the very shaft
// this feature draws. So the sample is nudged off the line before testing.
//
// It must stay ~1 unit. The subsector is looked up at the UN-nudged point, so the nudge
// has to be small enough that it cannot leave that leaf: BSP leaves are far smaller than
// a 64-unit cell, and a quarter-cell (16-unit) nudge -- which is what DOOM-0289's plan
// originally specified -- pushed any cell centre within 16 units of one of its own segs
// outside its leaf and reported it as VOID. Measured on E1M1: 588 of 920 open-sky cells
// falsely solid, unmoved by sun elevation, i.e. most of the courtyard's shafts deleted.
#define RB_SUN_NUDGE       1.0f
#define RB_SUN_MARCH_MAX   32       // cells; a backstop -- geometry terminates far sooner

typedef struct
{
    float* d;                 // w*h cells, row-major, world units (<= RB_SEEP_DMAX)
    // DOOM-0276: the open-sky mask, 1.0 where this cell's sector has
    // ceilingpic == skyflatnum and 0.0 otherwise. Same grid, same tap -- the fog march
    // reads BOTH from one texture, which is what let the per-sample up-ray go.
    //
    // It has to be its OWN channel and cannot be an epsilon on `d`: an outdoor cell is
    // d == 0, but a ROOFED cell one step inside a doorway is also only a few units from
    // the portal it walks to, so `d < eps` would report the first room behind every door
    // as open sky and put the full outdoor fog bank inside it.
    float* sky;               // w*h cells, row-major, 1.0 = open sky, 0.0 = roofed
    // DOOM-0289: the sun-clearance INTERVAL, per cell. Not a minimum height: in ROOFED
    // air rising clears the wall ahead of you but runs you into the ceiling above you,
    // so the set of heights that see the sun has BOTH ends (spec §4.4's 2026-07-30
    // amendment). A cell the sun never reaches stores an interval that is empty by
    // construction (zLo = cz + RB_SUN_NEVER, zHi = fz - RB_SUN_NEVER) rather than an
    // infinity, for the reason RB_SEEP_DMAX is finite: a half-float inf meeting a zero
    // bilinear weight is a NaN, and a NaN in sigma blows the whole fog march.
    float* zLo;               // lowest world z in this cell that still reaches the sun
    float* zHi;               // ...and the highest, before a solid ceiling stops it
    // DOOM-0289: the rb_cellgeom_t grid, RETAINED for the life of the field. Not a build
    // temporary like `portals`/`secList`: a plane that moves without flipping an opening
    // needs the clearance march re-run, and re-deriving this grid means re-descending the
    // BSP once per cell (~3.5k times on E1M1) to learn heights the sectors already know.
    void*  geom;
    int    w, h;              // cell counts, including the one-cell void ring
    float  originX, originY;  // world XY of cell (0,0)'s CENTRE -- inside the ring,
                              // i.e. one full cell outside the map's bounding box
    float  cell;              // world units per cell
} rb_seep_t;

// Flood-fill the seep field for the current level. Returns a heap-owned field;
// call RB_FreeSeepField on it. Never returns NULL -- a level with no open sky at
// all (most hell maps) yields an all-RB_SEEP_DMAX field, which is exactly the
// pre-seep look rather than a failure. Must run after P_SetupLevel.
rb_seep_t* RB_BuildSeepField(void);
void       RB_FreeSeepField(rb_seep_t* field);

// DOOM-0011 L3: one cell's air column, in world units. Returns 0 (leaving *fz/*cz
// untouched) when the cell holds no air at all -- a wall, the void ring, or a shut door.
// The fog-light bake uses it to place its sight-test sample inside real air.
int RB_SeepCellAir(const rb_seep_t* field, int ix, int iy, float* fz, float* cz);

// DOOM-0289. Re-run ONLY the sun-clearance march over an existing field: refresh the
// retained geometry cache from the live sector heights, then re-derive zLo/zHi. Skips the
// portal graph and the Dijkstra entirely -- a plane that moved without flipping an opening
// provably cannot have changed a single seep distance. Returns 1 if any cell's interval
// moved, so the caller knows whether an upload is owed.
int RB_RefreshSunClearance(rb_seep_t* field);

// DOOM-0281: has any linedef's OPENING appeared or vanished since the field was
// last flooded? The flood skips segs with openrange <= 0 -- which is what makes a
// shut door seal a room (INV-12) -- so a door or lift that opens in play leaves the
// field describing a map that no longer exists, and the room behind it keeps the
// sealed sentinel. Returns 1 when a re-flood is owed.
//
// Cheap, but not free: one P_LineOpening per linedef. Call it only on frames where
// RB_UpdateMeshHeights reported RB_UPD_MOVED -- a connectivity change needs a plane
// to have moved, so a still map never pays for this at all. RB_BuildSeepField
// re-seeds the cache, so the answer is always relative to the LIVE field.
int RB_SeepOpeningsChanged(void);

void RB_FreeMesh(rb_mesh_t* mesh);

// Re-height a built mesh from the current sector floor/ceiling heights, writing
// into dst[] (which must hold mesh->numverts rb_vertex_t -- e.g. the mapped GPU
// vertex buffer). Only vertices tagged RB_PLANE_FLOOR/CEIL have their z rewritten
// (to the live sector plane height); static vertices are untouched. Call once
// per frame so doors and lifts animate without rebuilding the mesh (DOOM-0049).
// Returns an OR of the RB_UPD_* bits below: RB_UPD_MOVED when any vertex z changed
// this frame (a door/lift mid-move) so the RT back-end refits its acceleration
// structure only when geometry really moved (DOOM-0009 build step 5); RB_UPD_RETEX
// when any wall/flat live texture id changed (a switch pressed or reverted, or an
// animated texture cycled) so the back-end can rebuild the NEE emitter set so a
// now-lit switch face pools light and a reverted one stops (DOOM-0082).
#define RB_UPD_MOVED    0x1
#define RB_UPD_RETEX    0x2
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

// DOOM-0157: does sprite-atlas lump `lump` (0-based; the sprite id is numwall +
// numflat + lump) belong to a glowing collectible (a key skull, armour/health bonus,
// powerup sphere)? The emissive back-end grants these a guaranteed faint per-material
// Le so their few-texel glow (skull eyes, armour gleam) self-illuminates in a dark
// room, where the generic near-fullbright emitter gate would derive Le=0. Returns
// 1/0; -1-out-of-range and pre-R_Init lumps return 0.
int RB_SpriteLumpGlows(int lump);

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

// DOOM-0170 L2d: one soft dark blob-shadow quad on the floor under each solid/pickup
// Thing (monsters, barrels, items), so nothing floats even where the key light is off.
// Same rb_vertex_t stream as the billboards; drawn with the alpha-blend blob pipeline.
int RB_BuildBlobs(const rb_view_t* view, rb_vertex_t* out, int maxverts);

#ifdef __cplusplus
}
#endif

#endif
