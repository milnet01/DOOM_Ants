// gamma_curve_test.cpp — DOOM-0491: gamma in the 3D tiers follows Classic's table.
//
// Why this exists: the gamma levels changed only Classic's picture, because the 3D tiers decode
// through the raw palette. Their curve is a fit to gammatable, so nothing else holds the two
// together.
//
// Contract:
//   1. Level 0 and an out-of-range level ask for no pass.
//   2. Each other level's curve stays within a stated distance of its gammatable row, read from
//      v_video.c (source scrape). The fit is loosest at level 4.
//   3. Each level is brighter than the one below it.
//   4. RB_Vulkan_Present records the pass after both recording branches and before the
//      developer screenshot, so a shot shows what the player sees (source scrape).
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <vector>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../gamma_curve.h"
#include "check_util.h"

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

}  // namespace

int main()
{
    // 1.
    check(RB_GammaExponent(0) == 0.0f, "level 0 records no pass");
    check(RB_GammaExponent(-1) == 0.0f, "a negative level records no pass");
    check(RB_GammaExponent(RB_GAMMA_LEVELS) == 0.0f, "a level past the table records no pass");

    // 2.
    const std::string src = slurp(std::string(DOOM_TESTS_ROOT) + "/v_video.c");
    const size_t start = src.find("gammatable[5][256]");
    check(start != std::string::npos, "v_video.c holds gammatable");
    const size_t open = src.find('=', start), close = src.find("};", start);
    const std::string body = src.substr(open, close - open);
    std::vector<int> table;
    const std::regex number(R"(\b\d+\b)");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), number);
         it != std::sregex_iterator(); ++it)
        table.push_back(std::stoi(it->str()));
    check(table.size() == 5u * 256u, "gammatable reads as five rows of 256");

    if (table.size() == 5u * 256u)
    {
        const double kAllowed[RB_GAMMA_LEVELS] = { 0.0, 2.5, 4.5, 8.5, 16.5 };
        for (int level = 1; level < RB_GAMMA_LEVELS; level++)
        {
            double worst = 0.0;
            for (int i = 0; i < 256; i++)
            {
                const double got = 255.0 * std::pow(i / 255.0, (double)RB_GammaExponent(level));
                worst = std::max(worst, std::fabs(got - table[level * 256 + i]));
            }
            check(worst <= kAllowed[level], "the curve stays near its gammatable row");
        }
    }

    // 3.
    for (int level = 2; level < RB_GAMMA_LEVELS; level++)
        check(RB_GammaExponent(level) < RB_GammaExponent(level - 1), "each level is brighter");

    // 4.
    const std::string vk = slurp(std::string(DOOM_TESTS_ROOT) + "/r_vulkan.cpp");
    const size_t present = vk.find("extern \"C\" void RB_Vulkan_Present(void)");
    const size_t call = vk.find("RecordGamma(idx);", present);
    const size_t shot = vk.find("DOOM-0294 developer screenshot", present);
    const size_t branch = vk.find("end of the non-RT (raster) recording branch", present);
    check(present != std::string::npos && call != std::string::npos, "Present records the pass");
    check(branch < call, "the pass comes after both recording branches");
    check(shot != std::string::npos && call < shot, "the pass comes before the developer shot");

    return check_summary("gamma_curve_test");
}
