// pickup_models_test.cpp — DOOM-0539 INV-2 and INV-3: the pickup list, and each model's size.
//
// Why this exists: a model that outgrows its sprite shows the player something standing where
// nothing stood. A new MF_SPECIAL entry with no row here would keep drawing as a flat sprite,
// with nothing to say so.
//
// Contract:
//   1. The table below names exactly the sprites of info.c's MF_SPECIAL entries (source scrape).
//   2. Every .dmdl in assets/ultra/models names a sprite in the table and loads.
//   3. Its footprint is within the larger of the Thing's radius and half the sprite's width.
//   4. Its top is within 1.25 times the sprite's top offset, and it does not sink below the floor.
//   5. Each light sits inside those bounds.
//
// Not yet: "every sprite has a model", which holds once DOOM-0539 B6 is done. Until then this
// reads the models that exist.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../rb_model.h"
#include "check_util.h"

namespace {

struct Sprite { const char* name; int width; int top; };

// Copied from doom.wad and doom2.wad, because a test takes no WAD (docs/standards/testing.md):
// the widest frame's width and the highest frame's top offset, over every frame in both IWADs.
const Sprite kSprites[] = {
    {"AMMO", 28, 16}, {"ARM1", 31, 17}, {"ARM2", 31, 17}, {"BFUG", 61, 38}, {"BKEY", 14, 19},
    {"BON1", 14, 14}, {"BON2", 16, 13}, {"BPAK", 22, 25}, {"BROK", 54, 20}, {"BSKU", 13, 18},
    {"CELL", 17, 12}, {"CELP", 32, 20}, {"CLIP",  9, 11}, {"CSAW", 62, 23}, {"LAUN", 62, 18},
    {"MEDI", 28, 19}, {"MEGA", 25, 32}, {"MGUN", 54, 18}, {"PINS", 25, 39}, {"PINV", 25, 23},
    {"PLAS", 54, 19}, {"PMAP", 28, 23}, {"PSTR", 28, 15}, {"PVIS", 28,  9}, {"RKEY", 14, 19},
    {"ROCK", 12, 27}, {"RSKU", 13, 18}, {"SBOX", 32, 12}, {"SGN2", 54, 15}, {"SHEL", 15,  7},
    {"SHOT", 63, 17}, {"SOUL", 25, 39}, {"STIM", 14, 15}, {"SUIT", 24, 51}, {"YKEY", 14, 19},
    {"YSKU", 13, 18},
};

const float kThingRadius = 20.0f;   // every MF_SPECIAL entry in info.c; contract 1 checks it
const float kTopSlack    = 1.25f;

std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

const Sprite* find_sprite(const std::string& name)
{
    for (const Sprite& s : kSprites)
        if (name == s.name)
            return &s;
    return nullptr;
}

void check_inside(float x, float y, float z, float reach2d, float top, const std::string& what)
{
    const float eps = 0.001f;
    check(std::hypot(x, y) <= reach2d + eps, (what + ": footprint within its limit").c_str());
    check(z <= top + eps, (what + ": top within its limit").c_str());
    check(z >= -eps, (what + ": not below the floor").c_str());
}

}  // namespace

int main()
{
    // 1.
    {
        const std::string info = slurp(std::string(DOOM_TESTS_ROOT) + "/info.c");
        check(!info.empty(), "info.c is readable");

        std::map<std::string, std::string> stateSprite;
        const std::regex stateLine(R"(\{SPR_(\w+),.*//\s*(S_\w+))");
        const std::regex spawnLine(R"((S_\w+),\s*//\s*spawnstate)");
        const std::regex radiusLine(R"((\d+)\*FRACUNIT,\s*//\s*radius)");
        std::set<std::string> fromInfo;
        std::string line, spawn;
        int radius = 0;
        bool radiiAgree = true;
        std::istringstream in(info);
        while (std::getline(in, line))
        {
            std::smatch m;
            if (std::regex_search(line, m, stateLine))
                stateSprite[m[2]] = m[1];
            else if (std::regex_search(line, m, spawnLine))
                spawn = m[1];
            else if (std::regex_search(line, m, radiusLine))
                radius = std::stoi(m[1]);
            else if (line.find("// flags") != std::string::npos
                     && line.find("MF_SPECIAL") != std::string::npos)
            {
                fromInfo.insert(stateSprite[spawn]);
                radiiAgree = radiiAgree && radius == (int)kThingRadius;
            }
        }

        std::set<std::string> fromTable;
        for (const Sprite& s : kSprites)
            fromTable.insert(s.name);
        for (const std::string& s : fromInfo)
            check(fromTable.count(s) == 1, ("MF_SPECIAL sprite has a table row: " + s).c_str());
        for (const std::string& s : fromTable)
            check(fromInfo.count(s) == 1, ("table row is an MF_SPECIAL sprite: " + s).c_str());
        check(radiiAgree, "every MF_SPECIAL entry has the radius this test assumes");
    }

    // 2 to 5.
    namespace fs = std::filesystem;
    const fs::path dir = fs::path(DOOM_TESTS_ROOT) / ".." / "assets" / "ultra" / "models";
    int models = 0;
    for (const fs::directory_entry& e : fs::directory_iterator(dir))
    {
        if (e.path().extension() != ".dmdl")
            continue;
        models++;
        const std::string name = e.path().stem().string();
        const Sprite* s = find_sprite(name);
        check(s != nullptr, (name + ".dmdl names a pickup sprite").c_str());
        const std::string bytes = slurp(e.path().string());
        rb_model_t m;
        check(RB_ModelParse(bytes.data(), bytes.size(), &m) == 1, (name + ".dmdl loads").c_str());
        if (!s || !m.floats)
            continue;

        const float reach = std::max(kThingRadius, s->width / 2.0f);
        const float top = kTopSlack * s->top;
        for (uint32_t i = 0; i < m.numVerts; i++)
        {
            const float* v = RB_ModelVert(&m, i);
            check_inside(v[0], v[1], v[2], reach, top, name + " vertex");
        }
        for (uint32_t i = 0; i < m.numLights; i++)
        {
            const float* l = RB_ModelLight(&m, i);
            check_inside(l[0], l[1], l[2], reach, top, name + " light");
        }
        RB_ModelFree(&m);
    }
    check(models >= 1, "at least one model was read, so the size checks ran");

    return check_summary("pickup_models_test");
}
