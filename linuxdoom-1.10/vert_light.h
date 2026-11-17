// vert_light.h — DOOM-0629: which sector's light a level-mesh vertex shows.
//
// RB_BuildLevelMesh copies a sector's lightlevel into each vertex. DOOM's light
// thinkers (p_lights.c: flicker, flash, strobe, glow) and light switches change
// lightlevel every tic, so RB_UpdateMeshHeights reads it again each tic. A wall,
// a relief wall or a filled step is lit by the sector on its own sidedef
// (p_setup.c sets seg->frontsector from seg->sidedef); a floor or ceiling by
// its vsector.
//
// Factored out of r_mesh.c so tests/vert_light_test.cpp can hold the rule with
// no WAD, GPU or Vulkan link (mirrors static_runs.h).
#ifndef VERT_LIGHT_H
#define VERT_LIGHT_H

#include "r_mesh.h"

// A sector lightlevel as the 0..1 vertex light. lightlevel is a raw WAD short
// that a crafted map can set to anything (DOOM-0406), and the traced view reads
// the vertex light with no clamp of its own.
static float RB_LevelLight(int lightlevel)
{
    float light = lightlevel / 255.0f;
    if (light < 0.0f) light = 0.0f;
    if (light > 1.0f) light = 1.0f;
    return light;
}

#define RB_LIGHT_NONE        0   // nothing relights this vertex
#define RB_LIGHT_FROM_SIDE   1   // *index is a sidedef; its sector lights the vertex
#define RB_LIGHT_FROM_SECTOR 2   // *index is the sector that lights the vertex

// Where v's light comes from. A filled step is RB_MESH_FLAT, but its vsector
// names the sector its EDGE follows, not the one that lights it, so the sidedef
// test comes first.
static int RB_VertLightSource(const rb_vertex_t* v, int* index)
{
    if (v->vtexside >= 0)
    {
        *index = v->vtexside;
        return RB_LIGHT_FROM_SIDE;
    }
    if (v->flags & RB_MESH_FLAT)
    {
        *index = v->vsector;
        return RB_LIGHT_FROM_SECTOR;
    }
    return RB_LIGHT_NONE;
}

#endif
