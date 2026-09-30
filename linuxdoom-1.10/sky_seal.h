// sky_seal.h — DOOM-0322: where the 3D mesh puts the sky, so a taller outdoor
// area shows over a lower one as it does in Classic.
//
// DOOM has no sky surface. Where two sky-ceilinged sectors meet, the software
// renderer's sky hack (R_StoreWallRange: worldtop = worldhigh) draws no upper
// wall and lets the taller sector's walls show above the lower one's ceiling.
// The 3D mesh made sky solid (DOOM-0141): each sky ceiling became a sky "lid"
// at that sector's own height. From a low courtyard, that lid hid a building
// just past the courtyard wall — E3M1's opening view.
//
// So every sky lid sits at ONE level-wide height, the highest sky ceiling, and
// each sky sector's edges toward a non-sky neighbour (or a solid wall) are
// sealed with sky from its own ceiling up to that height. Two sky sectors
// sharing a line share their air above the lower ceiling, which is the hack.
//
// Factored out of r_mesh.c so tests/sky_seal_test.cpp can hold the decisions
// with no WAD, GPU or Vulkan link (mirrors seg_project.h). Heights are fixed_t
// values passed as int. docs/specs/DOOM-0322-sky-lid-height.md is the contract.
#ifndef SKY_SEAL_H
#define SKY_SEAL_H

// How far an offset seal sits off its line, in map units, into its front
// sector (spec §4.4). A seal whose line carries a taller roofed neighbour would
// otherwise share a plane with that neighbour's upper wall, and the tracer
// keeps either of two hits at one distance.
#define RB_SKY_SEAL_OFFSET 0.125f

// What the upper step emits when the front ceiling is higher than the back's.
enum { RB_UPPER_NONE, RB_UPPER_WALL, RB_UPPER_SKY, RB_UPPER_FLAT };

// The level's sky lid: the highest ceiling among sectors whose ceiling is sky.
// Returns 0 and leaves *lid alone when no ceiling is sky.
static int RB_SkyLidHeight(const int* ceilingheight,
                           const unsigned char* ceilingIsSky,
                           int numsectors, int* lid)
{
    int i, found = 0, best = 0;
    for (i = 0; i < numsectors; i++)
    {
        if (!ceilingIsSky[i])
            continue;
        if (!found || ceilingheight[i] > best)
            best = ceilingheight[i];
        found = 1;
    }
    if (found)
        *lid = best;
    return found;
}

// The seal on one seg: a sky wall from a sky front ceiling up to the lid,
// wherever the back is not another sky sector (a missing back is a solid
// wall). *offset is 1 where the back sector's ceiling is higher, so a wall
// can stand on the back side of the same span. Returns 1 when a seal is
// emitted; a zero-height one is not.
static int RB_SkySeal(int frontCeilIsSky, int frontCeil,
                      int hasBack, int backCeilIsSky, int backCeil,
                      int lid, int* zb, int* zt, int* offset)
{
    if (!frontCeilIsSky || (hasBack && backCeilIsSky) || lid <= frontCeil)
        return 0;
    *zb = frontCeil;
    *zt = lid;
    *offset = hasBack && backCeil > frontCeil;
    return 1;
}

// The upper step, front ceiling higher than back. Two sky ceilings emit
// nothing: that gap is the sky hack's, and the lid covers it (spec §4.3).
// The rest is unchanged: the top texture, else DOOM-0142's fill.
static int RB_UpperStepKind(int frontCeilIsSky, int backCeilIsSky,
                            int hasTopTexture)
{
    if (frontCeilIsSky && backCeilIsSky) return RB_UPPER_NONE;
    if (hasTopTexture)                   return RB_UPPER_WALL;
    if (backCeilIsSky)                   return RB_UPPER_SKY;
    return RB_UPPER_FLAT;
}

#endif
