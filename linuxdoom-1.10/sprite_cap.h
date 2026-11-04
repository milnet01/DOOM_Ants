// sprite_cap.h — DOOM-0459: which things the 3D views draw when there are more
// than the sprite buffer holds.
//
// RB_BuildSprites writes one billboard per thing into a buffer of fixed size
// (about 4096 things in the traced view). It walked the sectors in index order
// and dropped whatever did not fit, so on a crowded map a monster in front of
// the player could be the one left out while one far behind was drawn. Past
// the cap it now orders the candidates nearest first and keeps that many.
//
// Factored out of r_mesh.c so tests/sprite_cap_test.cpp can hold the ordering
// with no WAD, GPU or Vulkan link (mirrors sky_seal.h).
#ifndef SPRITE_CAP_H
#define SPRITE_CAP_H

#include <stdlib.h>

// One thing that wants a billboard this frame. `order` is its place in the
// sector walk, so equal distances keep the walk's order on every libc.
typedef struct
{
    float d2;       // squared distance from the eye in the map plane
    int   order;
    void* thing;    // the mobj_t; opaque here so this header needs no DOOM types
} rb_sprite_cand_t;

static int RB_SpriteCandCmp(const void* pa, const void* pb)
{
    const rb_sprite_cand_t* a = (const rb_sprite_cand_t*)pa;
    const rb_sprite_cand_t* b = (const rb_sprite_cand_t*)pb;
    if (a->d2 != b->d2)
        return a->d2 < b->d2 ? -1 : 1;
    return (a->order > b->order) - (a->order < b->order);
}

// Order `n` candidates nearest first.
static void RB_SortSpritesNearest(rb_sprite_cand_t* c, int n)
{
    qsort(c, (size_t)n, sizeof *c, RB_SpriteCandCmp);
}

// DOOM-0539 §4.5: a pickup Thing whose model could draw this frame. `order` is its place
// in the sector walk, as above.
typedef struct
{
    float d2;       // squared distance from the eye in the map plane
    int   order;
    int   tris;     // the model's triangles, its lights not counted
} rb_model_cand_t;

static int RB_ModelCandCmp(const void* pa, const void* pb)
{
    const rb_model_cand_t* a = (const rb_model_cand_t*)pa;
    const rb_model_cand_t* b = (const rb_model_cand_t*)pb;
    if (a->d2 != b->d2)
        return a->d2 < b->d2 ? -1 : 1;
    return (a->order > b->order) - (a->order < b->order);
}

// The squared distance a Thing must be nearer than for its model to draw: models are
// taken nearest first while their triangles fit `budget`, and none at or past maxD2.
// Returns maxD2 when every candidate nearer than it fits. Things at the same distance as
// the first that does not fit all draw as sprites, so the budget is never passed.
// Sorts c in place.
static float RB_ModelCutoff(rb_model_cand_t* c, int n, int budget, float maxD2)
{
    int i, sum = 0;
    qsort(c, (size_t)n, sizeof *c, RB_ModelCandCmp);
    for (i = 0; i < n && c[i].d2 < maxD2; i++)
    {
        if (sum + c[i].tris > budget)
            return c[i].d2;
        sum += c[i].tris;
    }
    return maxD2;
}

#endif
