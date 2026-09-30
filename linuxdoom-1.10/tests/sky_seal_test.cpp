// sky_seal_test.cpp — DOOM-0322: the sky lid height and the seals under it.
//
// Why this exists: Solid/Ultra capped every sky sector at its OWN ceiling and
// walled every step between two sky ceilings, so a courtyard of sky sectors at
// different heights showed a visible sky wall where the original game shows one
// continuous sky (E3M1). The fix is one level-wide lid, seals only where sky
// meets a real wall or no wall, nothing between two sky ceilings, and a small
// offset so the seal never ties with a lintel on the back side (E3M9 line 154).
// Spec: docs/specs/DOOM-0322-sky-lid-height.md §4 and §8.
//
// Contract (the spec's INV numbers; this file covers the header-level halves):
//   INV-1  RB_SkyLidHeight returns the highest ceiling among SKY-ceiling
//          sectors only. It returns 0 and leaves *lid untouched when no sector
//          ceiling is sky. (The r_mesh.c cap-call scrape is a later step.)
//   INV-2  RB_SkySeal seals [frontCeil, lid] exactly when the front ceiling is
//          sky, the front ceiling is below the lid, and there is no back sector
//          or its ceiling is not sky. Sky-to-sky is never sealed.
//   INV-3  RB_UpperStepKind, per row of §4.3: both sky -> NONE; top texture ->
//          WALL; no texture + back sky -> SKY; no texture + back not sky ->
//          FLAT.
//   INV-4  (the rule only; the direction is checked by hand in Ultra at B3.)
//          The seal is offset exactly when a back sector exists with a ceiling
//          higher than the sky front's. RB_SKY_SEAL_OFFSET is 1/8 map unit.
//
// Heights are fixed_t passed as int, so the numbers below are map units << 16.
#include <cstdio>

#include "../sky_seal.h"
#include "check_util.h"

namespace {

int fx(int mapUnits) { return mapUnits << 16; }

// Seal result for one seg, so each case reads as one line.
struct Seal { int r, zb, zt, off; };

Seal seal(int fSky, int fCeil, int hasBack, int bSky, int bCeil, int lid)
{
    // Sentinels: a sealed result must overwrite them, and they make a wrongly
    // filled value on an unsealed seg visible in the message.
    Seal s = { -99, -7, -7, -7 };
    s.r = RB_SkySeal(fSky, fx(fCeil), hasBack, bSky, fx(bCeil), fx(lid),
                     &s.zb, &s.zt, &s.off);
    return s;
}

} // namespace

int main()
{
    char msg[256];

    // ---- INV-1: the lid -----------------------------------------------------
    {
        // E3M1's four courtyard sectors: 56, 192, 128, 128, all sky.
        int h[4] = { fx(56), fx(192), fx(128), fx(128) };
        unsigned char sky[4] = { 1, 1, 1, 1 };
        int lid = -12345;
        int r = RB_SkyLidHeight(h, sky, 4, &lid);
        check_eq_int(r, 1, "INV-1: E3M1 courtyard (56,192,128,128 all sky) has a lid");
        check_eq_int(lid, fx(192), "INV-1: E3M1 courtyard lid is the highest sky ceiling, 192");

        // No sky anywhere: returns 0, *lid untouched.
        int h2[3] = { fx(128), fx(72), fx(200) };
        unsigned char sky2[3] = { 0, 0, 0 };
        int lid2 = 424242;
        int r2 = RB_SkyLidHeight(h2, sky2, 3, &lid2);
        check_eq_int(r2, 0, "INV-1: a level with no sky ceiling has no lid (returns 0)");
        check_eq_int(lid2, 424242, "INV-1: no sky -> *lid is left untouched");

        // A non-sky sector taller than every sky one must be ignored.
        int h3[4] = { fx(56), fx(512), fx(128), fx(192) };
        unsigned char sky3[4] = { 1, 0, 1, 1 };
        int lid3 = -1;
        int r3 = RB_SkyLidHeight(h3, sky3, 4, &lid3);
        check_eq_int(r3, 1, "INV-1: sky sectors present alongside a taller non-sky one -> lid found");
        check_eq_int(lid3, fx(192),
                     "INV-1: a non-sky sector (512) taller than all sky ones is ignored; lid is 192");
    }

    // ---- INV-2: the seal ----------------------------------------------------
    {
        const int lid = 192;

        Seal a = seal(1, 128, 0, 0, 0, lid);
        check_eq_int(a.r, 1, "INV-2: front sky + no back sector -> sealed");
        check_eq_int(a.zb, fx(128), "INV-2: outer-wall seal starts at the front ceiling (zb)");
        check_eq_int(a.zt, fx(lid), "INV-2: outer-wall seal ends at the lid (zt)");

        Seal b = seal(1, 128, 1, 1, 64, lid);
        check_eq_int(b.r, 0, "INV-2: front sky + back sky -> NO seal (sky-to-sky line)");
        Seal b2 = seal(1, 128, 1, 1, 192, lid);
        check_eq_int(b2.r, 0, "INV-2: front sky + back sky (back higher) -> NO seal");

        Seal c = seal(1, 128, 1, 0, 64, lid);
        check_eq_int(c.r, 1, "INV-2: front sky + back not sky, lower ceiling -> sealed");
        check_eq_int(c.zb, fx(128), "INV-2: back-lower seal zb is the front ceiling");
        check_eq_int(c.zt, fx(lid), "INV-2: back-lower seal zt is the lid");

        Seal d = seal(1, 80, 1, 0, 128, lid);
        check_eq_int(d.r, 1, "INV-2: front sky + back not sky, higher ceiling -> sealed");
        check_eq_int(d.zb, fx(80), "INV-2: back-higher seal zb is the front ceiling");
        check_eq_int(d.zt, fx(lid), "INV-2: back-higher seal zt is the lid");

        Seal e = seal(0, 128, 0, 0, 0, lid);
        check_eq_int(e.r, 0, "INV-2: front NOT sky, no back -> no seal");
        Seal e2 = seal(0, 128, 1, 0, 64, lid);
        check_eq_int(e2.r, 0, "INV-2: front NOT sky, back not sky -> no seal");
        Seal e3 = seal(0, 128, 1, 1, 64, lid);
        check_eq_int(e3.r, 0, "INV-2: front NOT sky, back sky -> no seal");

        Seal f = seal(1, lid, 0, 0, 0, lid);
        check_eq_int(f.r, 0, "INV-2: front ceiling equal to the lid (zero height) -> no seal");
        Seal f2 = seal(1, lid, 1, 0, 64, lid);
        check_eq_int(f2.r, 0, "INV-2: zero-height seal is refused with a back sector too");
    }

    // ---- INV-3: the upper step ---------------------------------------------
    {
        check_eq_int(RB_UpperStepKind(1, 1, 0), RB_UPPER_NONE,
                     "INV-3: both ceilings sky, no top texture -> emits nothing");
        check_eq_int(RB_UpperStepKind(1, 1, 1), RB_UPPER_NONE,
                     "INV-3: both ceilings sky, with top texture -> still nothing");
        check_eq_int(RB_UpperStepKind(0, 0, 1), RB_UPPER_WALL,
                     "INV-3: a top texture (neither sky) -> the textured wall");
        check_eq_int(RB_UpperStepKind(0, 1, 1), RB_UPPER_WALL,
                     "INV-3: a top texture (back sky only) -> the textured wall");
        check_eq_int(RB_UpperStepKind(0, 1, 0), RB_UPPER_SKY,
                     "INV-3: no top texture, back ceiling sky -> a sky wall (DOOM-0142)");
        check_eq_int(RB_UpperStepKind(0, 0, 0), RB_UPPER_FLAT,
                     "INV-3: no top texture, back ceiling not sky -> the back ceiling's flat (DOOM-0142)");
    }

    // ---- INV-4: the offset rule --------------------------------------------
    {
        const int lid = 192;

        // E3M9 line 154: front sky ceiling 80, back non-sky ceiling 128.
        Seal a = seal(1, 80, 1, 0, 128, lid);
        check_eq_int(a.r, 1, "INV-4: E3M9 line 154 is sealed");
        check_eq_int(a.off, 1, "INV-4: E3M9 line 154 (front 80, back 128) is offset");

        Seal b = seal(1, 128, 0, 0, 0, lid);
        check_eq_int(b.r, 1, "INV-4: one-sided seg is sealed");
        check_eq_int(b.off, 0, "INV-4: a one-sided seg is never offset");

        // Without a back sector the backCeil argument is meaningless; make it
        // HIGHER than the front so a rule that forgets hasBack would offset.
        Seal b2 = seal(1, 80, 0, 0, 128, lid);
        check_eq_int(b2.r, 1, "INV-4: one-sided seg with a stray higher backCeil is sealed");
        check_eq_int(b2.off, 0, "INV-4: one-sided seg is never offset, whatever backCeil holds");

        Seal c = seal(1, 128, 1, 0, 64, lid);
        check_eq_int(c.r, 1, "INV-4: back-lower seg is sealed");
        check_eq_int(c.off, 0, "INV-4: back ceiling lower than the front's -> not offset");

        Seal d = seal(1, 128, 1, 0, 128, lid);
        check_eq_int(d.r, 1, "INV-4: equal-ceiling seg is sealed");
        check_eq_int(d.off, 0, "INV-4: back ceiling equal to the front's -> not offset");

        std::snprintf(msg, sizeof msg,
                      "INV-4: RB_SKY_SEAL_OFFSET is 1/8 map unit (got %f)",
                      (double)RB_SKY_SEAL_OFFSET);
        check(RB_SKY_SEAL_OFFSET == 0.125f, msg);
    }

    return check_summary("sky_seal_test");
}
