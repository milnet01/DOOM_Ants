// sky_spot_test.cpp — DOOM-0658: stronger sky fill at a hand-picked spot of one map.
//
// Why this exists: the spot is meant for id's E2M1 only. A spot that matched by name alone
// would brighten whatever Freedoom or a mod built at the same coordinates.
//
// Contract:
//   1. id's E2M1 (92 sectors, 415 lines) gets its spot, with a gain above 1.
//   2. A map of the same name with other counts gets none.
//   3. DOOM 2's MAP21 (episode 1 there) does not take E2M1's spot, nor does E1M1.
//   4. The engine passes the spot to the bake through RB_SkySpotFor (source scrape: it
//      needs the engine).
#include <fstream>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../sky_spot.h"
#include "check_util.h"

int main()
{
    // 1.
    const rb_sky_spot_t* s = RB_SkySpotFor(0, 2, 1, 92, 415);
    check(s != nullptr, "id's E2M1 has its sky spot");
    check(s != nullptr && s->gain > 1.0f, "the spot strengthens the sky fill");
    check(s != nullptr && s->lo[0] < s->hi[0] && s->lo[1] < s->hi[1] && s->lo[2] < s->hi[2],
          "the spot's box is not empty");

    // 2.
    check(RB_SkySpotFor(0, 2, 1, 93, 415) == nullptr, "another E2M1's sector count misses");
    check(RB_SkySpotFor(0, 2, 1, 92, 400) == nullptr, "another E2M1's line count misses");

    // 3.
    check(RB_SkySpotFor(1, 2, 1, 92, 415) == nullptr, "a DOOM 2 map never takes it");
    check(RB_SkySpotFor(0, 1, 1, 92, 415) == nullptr, "E1M1 does not take E2M1's spot");

    // 4.
    std::ifstream in(std::string(DOOM_TESTS_ROOT) + "/r_vulkan.cpp");
    std::stringstream src;
    src << in.rdbuf();
    check(src.str().find("RB_SkySpotFor(") != std::string::npos,
          "the engine passes the spot to the bake through RB_SkySpotFor");

    return check_summary("sky_spot_test");
}
