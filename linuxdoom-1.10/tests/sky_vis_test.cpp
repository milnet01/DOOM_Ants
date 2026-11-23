// sky_vis_test.cpp — DOOM-0651: how much outdoor air one cell of fog can see.
//
// Why this exists: the traced fog lit roofed air by how near an opening it was along
// open space, so a corridor that bends away from a door glowed grey with every light
// off (E2M1's start, view mean 45 of 255 against 0.7 with the fog off).
//
// Contract:
//   1. A room with no outdoor air reads 0.
//   2. A cell in a doorway onto a courtyard reads near 1: half its directions face out.
//   3. Air that reaches outdoors only round a corner reads 0.
//   4. Deeper into a room with one door, the reading falls, and stays above 0 while
//      the door is in sight.
//   5. Each direction asks the sight test at most once.
//   6. The engine bakes the fog's visibility through RB_SkyVisCell (source scrape: it
//      needs the engine).
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../sky_vis.h"
#include "check_util.h"

namespace {

// A floor plan, one character per 64-unit cell: 'O' open-sky air, '.' roofed air,
// '#' wall. Row 0 is y = 0.
struct Plan {
    std::vector<std::string> rows;
    int sightCalls = 0;
    int w() const { return (int)rows[0].size(); }
    int h() const { return (int)rows.size(); }
    char at(int x, int y) const { return rows[(size_t)y][(size_t)x]; }
};

const float kCell = 64.0f;

int Outdoor(void* ctx, int ix, int iy)
{
    return static_cast<Plan*>(ctx)->at(ix, iy) == 'O';
}

// Blocked when the straight line passes through any wall cell.
int Sight(void* ctx, float x, float y, int ix, int iy)
{
    Plan* p = static_cast<Plan*>(ctx);
    p->sightCalls++;
    const float tx = ix * kCell, ty = iy * kCell;
    const int   n  = 256;
    for (int i = 1; i < n; i++)
    {
        const float s  = (float)i / n;
        const int   cx = (int)std::floor((x + (tx - x) * s) / kCell + 0.5f);
        const int   cy = (int)std::floor((y + (ty - y) * s) / kCell + 0.5f);
        if (p->at(cx, cy) == '#')
            return 0;
    }
    return 1;
}

float Vis(Plan& p, int ix, int iy)
{
    return RB_SkyVisCell(ix, iy, p.w(), p.h(), kCell, 0.0f, 0.0f, Outdoor, Sight, &p);
}

}  // namespace

int main()
{
    // 1.
    Plan sealed{{
        "#######",
        "#.....#",
        "#.....#",
        "#.....#",
        "#######",
        "OOOOOOO",
    }};
    check(Vis(sealed, 3, 2) == 0.0f, "a room that shares only a wall with outdoors reads 0");

    // 2 and 4. A room (x 1..8) with a door three cells wide at x = 9, onto a courtyard.
    // One cell is too narrow to test at this range: at seven cells it spans about 8
    // degrees, under the 11.25 between two directions.
    Plan room{{
        "##########OOOOOOOO",
        "#........#OOOOOOOO",
        "#........#OOOOOOOO",
        "#.........OOOOOOOO",
        "#.........OOOOOOOO",
        "#.........OOOOOOOO",
        "#........#OOOOOOOO",
        "#........#OOOOOOOO",
        "##########OOOOOOOO",
    }};
    check(Vis(room, 9, 4) >= 0.75f, "a cell in the doorway reads near 1");
    const float near = Vis(room, 7, 4), far = Vis(room, 2, 4);
    check(near > far, "deeper into the room the reading falls");
    check(far > 0.0f, "a cell that still sees the door reads above 0");

    // 3. A corridor from the courtyard turns a corner into a dead end.
    Plan corner{{
        "###########",
        "#....######",
        "#.##.######",
        "#.##.......",
        "#.#########",
        "#.#########",
        "###########",
    }};
    for (auto& r : corner.rows)
        for (auto& c : r)
            if (c == '.' && &r == &corner.rows[3] && (&c - r.data()) >= 6)
                c = 'O';                       // the corridor's far end opens to the sky
    check(Vis(corner, 4, 3) > 0.0f, "the corridor's mouth sees the outdoors");
    check(Vis(corner, 1, 5) == 0.0f, "air round the corner from the outdoors reads 0");

    // 5.
    room.sightCalls = 0;
    (void)Vis(room, 2, 4);
    check(room.sightCalls <= RB_SKYVIS_DIRS, "each direction asks the sight test at most once");

    // 6.
    std::ifstream in(std::string(DOOM_TESTS_ROOT) + "/r_vulkan.cpp");
    std::stringstream src;
    src << in.rdbuf();
    check(src.str().find("RB_SkyVisCell(") != std::string::npos,
          "the engine bakes the fog's visibility through RB_SkyVisCell");

    return check_summary("sky_vis_test");
}
