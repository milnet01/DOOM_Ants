// vert_light_test.cpp — DOOM-0629: walls and floors follow their sector's live
// light in Solid and Ultra.
//
// Why this exists: the level mesh took each sector's lightlevel once, at build,
// and RB_UpdateMeshHeights refreshed heights and textures but never light. So
// flickering, flashing, strobing and glowing sectors, and light switches, did
// nothing to walls and floors in the 3D views, while sprites (built each frame)
// still flickered.
//
// Contract:
//   1  RB_LevelLight is lightlevel / 255, clamped to 0..1.
//   2  A wall, relief wall or filled step is lit through its sidedef; a floor or
//      ceiling cap through its vsector; any other vertex is not relit. A filled
//      step's vsector names its edge's sector, so it must not be used for light.
//   3  RB_UpdateMeshHeights refreshes light on both of its paths: the per-vertex
//      walk and the static wall runs (source scrape).
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "../vert_light.h"
#include "check_util.h"

namespace {

std::string slurp(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

rb_vertex_t vert(int flags, int side, int sector)
{
    rb_vertex_t v = {};
    v.flags    = flags;
    v.vtexside = side;
    v.vsector  = sector;
    return v;
}

} // namespace

int main()
{
    // ---- 1: the clamp ------------------------------------------------------
    check(RB_LevelLight(0) == 0.0f, "1: lightlevel 0 is black");
    check(RB_LevelLight(255) == 1.0f, "1: lightlevel 255 is full");
    check(RB_LevelLight(160) == 160 / 255.0f, "1: lightlevel 160 is 160/255");
    check(RB_LevelLight(-40) == 0.0f, "1: a negative WAD lightlevel clamps to 0");
    check(RB_LevelLight(4000) == 1.0f, "1: a lightlevel over 255 clamps to 1");

    // ---- 2: which sector lights a vertex -----------------------------------
    {
        int idx = -1;
        rb_vertex_t wall = vert(0, 7, 3);
        check_eq_int(RB_VertLightSource(&wall, &idx), RB_LIGHT_FROM_SIDE, "2: a wall is lit through its sidedef");
        check_eq_int(idx, 7, "2: the wall's own sidedef");

        idx = -1;
        rb_vertex_t step = vert(RB_MESH_FLAT, 5, 9);
        check_eq_int(RB_VertLightSource(&step, &idx), RB_LIGHT_FROM_SIDE,
                     "2: a filled step is lit through its sidedef, not its edge sector");
        check_eq_int(idx, 5, "2: the step's own sidedef");

        idx = -1;
        rb_vertex_t cap = vert(RB_MESH_FLAT, -1, 4);
        check_eq_int(RB_VertLightSource(&cap, &idx), RB_LIGHT_FROM_SECTOR, "2: a floor cap is lit by its sector");
        check_eq_int(idx, 4, "2: the cap's own sector");

        rb_vertex_t other = vert(0, -1, 2);
        check_eq_int(RB_VertLightSource(&other, &idx), RB_LIGHT_NONE, "2: an untagged vertex is not relit");
    }

    // ---- 3: the per-tic update relights both paths (source scrape) ----------
    {
        const std::string src = slurp(DOOM_TESTS_ROOT "/r_mesh.c");
        const size_t fn  = src.find("int RB_UpdateMeshHeights(");
        const size_t end = src.find("\n}\n", fn);
        int calls = 0;
        for (size_t at = src.find("vert_live_light(", fn); at < end; at = src.find("vert_live_light(", at + 1))
            calls++;
        check(fn != std::string::npos && calls >= 2,
              "3: RB_UpdateMeshHeights refreshes light for the static wall runs and the per-vertex walk");
    }

    return check_summary("vert_light_test");
}
