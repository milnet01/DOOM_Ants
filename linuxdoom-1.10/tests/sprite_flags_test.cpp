// sprite_flags_test.cpp — DOOM-0508: a partly invisible Thing is marked as one.
//
// Why this exists: sprite_quad() never read MF_SHADOW, so a Spectre's vertices were
// identical to a Pinky's, and no renderer could tell them apart. Both 3D views drew it
// solid, which makes it a different and easier enemy than the one the game intends.
//
// Contract:
//   1. An MF_SHADOW Thing's flags carry RB_MESH_SHADOW, whatever else it is.
//   2. Any other Thing's do not, and the shadow bit costs it none of its others.
//   3. RB_MESH_SHADOW shares no bit with another RB_MESH_ flag.
//   4. sprite_quad() takes its flags from RB_SpriteFlags, passing the real mobj bits,
//      and RB_BuildBlobs() skips an MF_SHADOW Thing (source scrape: both need the engine).
//   5. The shaders that draw a sprite agree on the bit's value (source scrape).
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../sprite_flags.h"
#include "check_util.h"

namespace {

const unsigned kSpecial = 0x1, kShadow = 0x40000, kSolid = 0x2;

std::string slurp(const std::string& path)
{
    std::ifstream f(DOOM_TESTS_ROOT + std::string("/") + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int flags_of(unsigned mobj_flags, int glows)
{
    return RB_SpriteFlags(mobj_flags, glows, kSpecial, kShadow);
}

}  // namespace

int main()
{
    // 1.
    check((flags_of(kShadow, 0) & RB_MESH_SHADOW) != 0, "a Spectre is marked");
    check((flags_of(kShadow | kSolid, 1) & RB_MESH_SHADOW) != 0, "a glowing one is too");

    // 2.
    check((flags_of(kSolid, 0) & RB_MESH_SHADOW) == 0, "a Pinky is not marked");
    check((flags_of(kSpecial, 1) & RB_MESH_SHADOW) == 0, "a pickup is not marked");
    check((flags_of(kShadow, 0) & RB_MESH_SPRITE) != 0, "a Spectre is still a sprite");
    check((flags_of(kShadow, 1) & RB_MESH_EMISSIVE) != 0, "and can still glow");
    check((flags_of(kSpecial | kShadow, 0) & RB_MESH_PICKUP) != 0, "and can still be a pickup");

    // 3.
    const int others[] = {RB_MESH_FLAT, RB_MESH_MASKED, RB_MESH_SPRITE, RB_MESH_PSPRITE,
                          RB_MESH_SKY, RB_MESH_EMISSIVE, RB_MESH_BLOB, RB_MESH_SKYDOME,
                          RB_MESH_PICKUP};
    for (int other : others)
        check((other & RB_MESH_SHADOW) == 0, "the bit is its own");

    // 4.
    const std::string mesh = slurp("r_mesh.c");
    check(!mesh.empty(), "r_mesh.c was read");
    check(std::regex_search(mesh, std::regex(
              "sflags *= *RB_SpriteFlags *\\(thing->flags,[^;]*MF_SPECIAL, *MF_SHADOW\\)")),
          "sprite_quad takes its flags from RB_SpriteFlags");
    check(std::regex_search(mesh, std::regex(
              "RB_BuildBlobs[\\s\\S]*?thing->flags *& *MF_SHADOW[\\s\\S]*?RB_MESH_BLOB")),
          "RB_BuildBlobs skips an MF_SHADOW Thing before it draws a blob");

    // 5.
    char want[32];
    snprintf(want, sizeof want, "FLAG_SHADOW *= *0x%x\\b", RB_MESH_SHADOW);
    for (const char* shader : {"shaders/mesh.frag", "shaders/shadow.frag", "shaders/pt_common.glsl"})
        check(std::regex_search(slurp(shader), std::regex(want)),
              (std::string("the shader agrees on the bit: ") + shader).c_str());

    return check_summary("sprite_flags_test");
}
