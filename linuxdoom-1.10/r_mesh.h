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
    float u, v;         // texel coordinates (pre-division)
    int   texnum;       // wall texture index, or flat index when RB_MESH_FLAT
    int   flags;        // RB_MESH_* bits
    float light;        // owning sector lightlevel, 0..1
} rb_vertex_t;

// rb_vertex_t.flags bits.
#define RB_MESH_FLAT    0x1   // texnum indexes flats[], not textures[]
#define RB_MESH_MASKED  0x2   // two-sided middle texture (alpha-tested)

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

#ifdef __cplusplus
}
#endif

#endif
