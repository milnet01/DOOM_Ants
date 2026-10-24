// fixed_colormap_test.cpp — DOOM-0455: invulnerability and the light-amp
// visor in the 3D views.
//
// Why this exists: the 3D views ignored player->fixedcolormap, so picking up
// either powerup changed nothing on screen in Solid or Ultra. The spec is
// docs/specs/DOOM-0455-fixed-colormap-3d.md.
//
// Contract:
//   INV-1  RB_FixedMode maps 0 to none, INVERSECOLORMAP (32) to invert, and
//          every other row in 1..NUMCOLORMAPS to the visor.
#include "../fixed_colormap.h"
#include "check_util.h"

int main()
{
    // ---- INV-1: the three classes ---------------------------------------
    check_eq_int(RB_FixedMode(0), RB_FIXED_NONE,
                 "INV-1: fixedcolormap 0 (no powerup) -> none");
    check_eq_int(RB_FixedMode(32), RB_FIXED_INVERT,
                 "INV-1: fixedcolormap 32 (invulnerability) -> invert");
    check_eq_int(RB_FixedMode(1), RB_FIXED_VISOR,
                 "INV-1: fixedcolormap 1 (light-amp visor) -> visor");
    // Every other row Classic could be handed is shaded as the visor. 32 is
    // NUMCOLORMAPS (r_main.h) and the invert row, so the sweep stops below it.
    for (int row = 1; row < 32; row++)
        check(RB_FixedMode(row) == RB_FIXED_VISOR,
              "INV-1: a non-zero row other than 32 -> visor");

    return check_summary("fixed_colormap_test");
}
