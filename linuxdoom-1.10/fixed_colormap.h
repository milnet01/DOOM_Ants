// fixed_colormap.h — DOOM-0455: what the player's fixedcolormap asks the 3D
// views to do.
//
// Classic honours invulnerability and the light-amp visor through one global:
// P_PlayerThink (p_user.c) sets player->fixedcolormap to INVERSECOLORMAP for
// invulnerability and to 1 for the visor, and the software renderer draws with
// that COLORMAP row in place of the light tables. The 3D views shade in colour,
// not palette indices, so they take a mode instead: invert, or a lamp at the
// camera. r_backend.c hands it across the seam in rb_view_t.fixedMode, and
// shaders/formulas/fixed_colormap.glsl holds the same codes and the formulas.
//
// Factored out so tests/fixed_colormap_test.cpp can hold the mapping with no
// engine (mirrors step_wall.h).
#ifndef FIXED_COLORMAP_H
#define FIXED_COLORMAP_H

enum { RB_FIXED_NONE = 0, RB_FIXED_VISOR = 1, RB_FIXED_INVERT = 2 };
enum { RB_INVERSECOLORMAP = 32 };   // p_user.c's INVERSECOLORMAP (INV-2)

// player->fixedcolormap -> mode. Classic sets only 0, 1 (visor) and
// INVERSECOLORMAP; any other non-zero row is shaded as the visor.
static inline int RB_FixedMode(int fixedcolormap)
{
    if (fixedcolormap == 0)                  return RB_FIXED_NONE;
    if (fixedcolormap == RB_INVERSECOLORMAP) return RB_FIXED_INVERT;
    return RB_FIXED_VISOR;
}

#endif
