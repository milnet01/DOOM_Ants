// static_runs.h — DOOM-0612: the mesh vertices the per-tic scan can check once
// per wall instead of once each.
//
// RB_UpdateMeshHeights visits every vertex each game tic. A wall vertex whose z
// follows no plane (vplane NONE) needs only its live texture id, and every
// vertex of one wall face reads that id from the same sidedef slot. DOOM-0610's
// relief walls are made of such vertices, many to a wall, so the scan checks a
// run of them once and writes the result to all of them. A run is consecutive
// vertices with the same sidedef, slot and baked texture, so they hold one id at
// build time; only the scan writes texnum afterwards, and it writes a run whole,
// so they keep holding one.
//
// Factored out of r_mesh.c so tests/static_runs_test.cpp can hold the rule with
// no WAD, GPU or Vulkan link (mirrors step_wall.h). User ruling 2026-11-14 lifts
// DOOM-0569's "do not narrow the scan" for this case.
#ifndef STATIC_RUNS_H
#define STATIC_RUNS_H

#include "r_mesh.h"

// A wall vertex the scan touches only for its texture id.
static int RB_IsStaticWallVert(const rb_vertex_t* v)
{
    return !(v->flags & RB_MESH_FLAT) && v->vtexside >= 0 && v->vplane == RB_PLANE_NONE;
}

// Fills out[] (when not NULL) with each run of at least minlen static wall
// vertices in v[0..n), in order, and returns how many there are. Call once with
// out NULL to size the array.
static int RB_FindStaticRuns(const rb_vertex_t* v, int n, int minlen, rb_vrun_t* out)
{
    int count = 0;
    int i = 0;
    while (i < n)
    {
        int j = i + 1;
        if (RB_IsStaticWallVert(&v[i]))
            while (j < n && RB_IsStaticWallVert(&v[j]) && v[j].vtexside == v[i].vtexside
                   && v[j].vtexslot == v[i].vtexslot && v[j].texnum == v[i].texnum)
                j++;
        if (RB_IsStaticWallVert(&v[i]) && j - i >= minlen)
        {
            if (out)
            {
                out[count].first = i;
                out[count].count = j - i;
            }
            count++;
        }
        i = j;
    }
    return count;
}

#endif
