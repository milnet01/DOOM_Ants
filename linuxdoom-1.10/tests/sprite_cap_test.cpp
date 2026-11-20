// sprite_cap_test.cpp — DOOM-0459: past the sprite cap, the nearest things are
// the ones drawn.
//
// Why this exists: RB_BuildSprites dropped the things that did not fit in the
// order it walked the sectors, so on a crowded map the traced view could leave
// out a monster beside the player and draw one across the map. Measured before
// the fix on E1M3 with the cap lowered to 8: a thing 101 units away was dropped
// while one 2752 units away was drawn.
//
// Contract:
//   1. After RB_SortSpritesNearest, the candidates run nearest first, so the
//      first `cap` of them are the `cap` nearest.
//   2. Equal distances keep the sector-walk order (`order`), so the kept set
//      is the same on every libc's qsort.
//   3. r_mesh.c calls RB_SortSpritesNearest (source scrape), so the order
//      tested here is the order the engine uses; RB_BuildBlobs uses the same
//      nearest-first candidates when its blobs overflow (DOOM-0557).
//   4. DOOM-0539 §4.5: RB_ModelCutoff takes models nearest first while their
//      triangles fit the budget, none at or past the distance cap, and things at
//      the distance of the first that does not fit draw as sprites too, so the
//      budget is never passed. r_mesh.c gates a model on it (source scrape).
//   5. DOOM-0631: RB_ModelFadeLevel fades a model into its sprite over the last
//      stretch before that cutoff: the model alone nearer than the band, the
//      sprite alone at or past the cutoff, both between, and never a half-faded
//      model close to the eye when the budget pulls the cutoff in. The flag bits
//      that carry the level read the same in r_mesh.h and the shaders (scrape).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../sprite_cap.h"
#include "check_util.h"

namespace {

std::string slurp(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

int main()
{
    // ---- 1: nearest first ---------------------------------------------------
    {
        // Walk order puts the far things first, as a sector-index walk can.
        const float d[8] = { 2752.f, 1833.f, 900.f, 640.f, 400.f, 273.f, 101.f, 50.f };
        rb_sprite_cand_t c[8];
        for (int i = 0; i < 8; i++)
            c[i] = { d[i] * d[i], i, nullptr };
        RB_SortSpritesNearest(c, 8);
        bool ascending = true;
        for (int i = 1; i < 8; i++)
            if (c[i - 1].d2 > c[i].d2)
                ascending = false;
        check(ascending, "1: candidates run nearest first");
        check_eq_int(c[0].order, 7, "1: the nearest thing (50 units, walked last) comes first");
        check_eq_int(c[1].order, 6, "1: the 101-unit thing is kept under a cap of 2");
        check_eq_int(c[7].order, 0, "1: the farthest thing (2752 units, walked first) comes last");
    }

    // ---- 2: ties keep the walk order ----------------------------------------
    {
        rb_sprite_cand_t c[5] = {
            { 9.f, 0, nullptr }, { 4.f, 1, nullptr }, { 9.f, 2, nullptr },
            { 4.f, 3, nullptr }, { 9.f, 4, nullptr },
        };
        RB_SortSpritesNearest(c, 5);
        const int want[5] = { 1, 3, 0, 2, 4 };
        bool same = true;
        for (int i = 0; i < 5; i++)
            if (c[i].order != want[i])
                same = false;
        check(same, "2: equal distances keep the sector-walk order");
    }

    // ---- 3: the engine uses this order (source scrape) -----------------------
    {
        const std::string src = slurp(DOOM_TESTS_ROOT "/r_mesh.c");
        check(src.find("RB_SortSpritesNearest(") != std::string::npos,
              "3: r_mesh.c orders the sprite candidates nearest first");
        // DOOM-0557: blobs take the same nearest-first path when they overflow.
        // In order inside the body: the overflow test, the sorted walk, and each blob
        // built from that sorted list. A call whose result went unused fails the third.
        const size_t blobs = src.find("int RB_BuildBlobs(");
        const size_t end   = src.find("\n}\n", blobs);
        const size_t full  = src.find("if (!full)", blobs);
        const size_t walk  = src.find("nearest_things(", blobs);
        const size_t use   = src.find("blob_verts((const mobj_t*)cand[i].thing", blobs);
        check(blobs != std::string::npos && full < walk && walk < use && use < end,
              "3: RB_BuildBlobs takes its overflow nearest first");
    }

    // ---- 4: the model budget and distance cap (DOOM-0539 §4.5) --------------
    {
        const float maxD2 = 1024.0f * 1024.0f;
        // Walk order is not distance order: the cutoff must sort.
        rb_model_cand_t c[] = { {900.0f * 900.0f, 0, 5000}, {100.0f * 100.0f, 1, 5000},
                                {500.0f * 500.0f, 2, 5000}, {2000.0f * 2000.0f, 3, 10} };
        float cut = RB_ModelCutoff(c, 4, 12000, maxD2);
        check(cut == 900.0f * 900.0f,
              "4: the two nearest 5000-triangle models fit 12000; the third (900 units) does not");
        rb_model_cand_t few[] = { {300.0f * 300.0f, 0, 252}, {1500.0f * 1500.0f, 1, 252} };
        check(RB_ModelCutoff(few, 2, 12000, maxD2) == maxD2,
              "4: when every model nearer than the cap fits, the cut is the distance cap");
        rb_model_cand_t tie[] = { {400.0f * 400.0f, 0, 7000}, {400.0f * 400.0f, 1, 7000} };
        float tcut = RB_ModelCutoff(tie, 2, 12000, maxD2);
        check(!(400.0f * 400.0f < tcut),
              "4: two models at one distance that do not both fit both draw as sprites");
        check(RB_ModelCutoff(nullptr, 0, 12000, maxD2) == maxD2, "4: no models leaves the cap");
        const std::string src = slurp(DOOM_TESTS_ROOT "/r_mesh.c");
        check(src.find("RB_ModelFadeLevel(thing_d2(view, thing), model_cut_d2") != std::string::npos,
              "4: r_mesh.c draws a model only nearer than the cutoff");
    }

    // ---- 5: the model fades into its sprite before the cutoff (DOOM-0631) ----
    {
        const float cut = 1024.0f, band = 160.0f;
        auto level = [&](float d, float c) { return RB_ModelFadeLevel(d * d, c * c, band); };
        check(level(1024.0f, cut) == 0 && level(1100.0f, cut) == 0,
              "5: at or past the cutoff the sprite draws alone");
        check(level(864.0f, cut) == 16 && level(100.0f, cut) == 16,
              "5: nearer than the band the model draws alone");
        check(level(944.0f, cut) == 8, "5: half way through the band the two share the pixels evenly");
        check(level(1023.9f, cut) == 1, "5: just inside the cutoff the model still has a share");
        check(level(865.0f, cut) == 15, "5: just inside the band the sprite already has a share");
        bool falls = true;
        for (float d = 800.0f; d < 1040.0f; d += 0.5f)
            falls = falls && level(d + 0.5f, cut) <= level(d, cut);
        check(falls, "5: the model's share never grows with distance");
        check(level(140.0f, 200.0f) == 16 && level(190.0f, 200.0f) < 16,
              "5: a cutoff pulled in to 200 units fades over its last quarter, not over 160 units");
        const std::string hdr  = slurp(DOOM_TESTS_ROOT "/r_mesh.h");
        const std::string glsl = slurp(DOOM_TESTS_ROOT "/shaders/formulas/model_fade.glsl");
        const std::string shd  = slurp(DOOM_TESTS_ROOT "/shaders/shadow.frag");
        check(hdr.find("#define RB_MESH_FADE_SHIFT 12") != std::string::npos
              && hdr.find("#define RB_MESH_FADE_MASK  0xF000") != std::string::npos
              && hdr.find("#define RB_MESH_FADE_INV   0x10000") != std::string::npos,
              "5: r_mesh.h holds the fade bits");
        check(glsl.find("FLAG_FADE_SHIFT = 12;") != std::string::npos
              && glsl.find("FLAG_FADE_MASK  = 0xF000;") != std::string::npos
              && glsl.find("FLAG_FADE_INV   = 0x10000;") != std::string::npos
              && shd.find("FLAG_FADE_INV = 0x10000;") != std::string::npos,
              "5: the shaders read the same fade bits");
    }

    return check_summary("sprite_cap_test");
}
