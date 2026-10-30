// sprite_flags.h — DOOM-0508: which rb_vertex_t flags a sprite Thing's quad carries.
//
// Apart from sprite_quad() so a test can reach it without the engine. The mobj bits
// come in as arguments because p_mobj.h is not C++-clean; r_mesh.c passes the real ones.
#ifndef SPRITE_FLAGS_H
#define SPRITE_FLAGS_H

#include "r_mesh.h"

static inline int RB_SpriteFlags(unsigned mobj_flags, int glows,
                                 unsigned mf_special, unsigned mf_shadow)
{
    int flags = RB_MESH_SPRITE;
    if (glows)
        flags |= RB_MESH_EMISSIVE;
    if (mobj_flags & mf_special)
        flags |= RB_MESH_PICKUP;     // DOOM-0482
    if (mobj_flags & mf_shadow)
        flags |= RB_MESH_SHADOW;
    return flags;
}

#endif
