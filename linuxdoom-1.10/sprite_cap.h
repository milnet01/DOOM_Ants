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

#endif
