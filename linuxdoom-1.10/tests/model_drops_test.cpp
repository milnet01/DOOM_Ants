// model_drops_test.cpp — DOOM-0626: r_mesh.c's model_drops table matches P_KillMobj's drops.
//
// Why this exists: a level loads only the models of sprites it can show (DOOM-0626), and a
// monster's drop is one it can show though no map thing places it. If p_inter.c gained a drop
// the table missed, that item would draw as a flat sprite on any level with no copy placed.
//
// Contract:
//   1. Every `case MT_X: ... item = MT_Y;` pair in P_KillMobj's drop switch is a row of
//      model_drops, and every row is such a pair.
//   2. The scrape found at least one pair, so contract 1 cannot pass on an empty match.
//
// A source scrape, because p_inter.c and r_mesh.c pull in most of the engine.
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <string>
#include <utility>

#include "check_util.h"

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

namespace {

std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

// The text between `from` and the next `to` after it, with // comments removed, so a row
// commented out does not count. "" when either marker is missing.
std::string between(const std::string& s, const std::string& from, const std::string& to)
{
    const size_t a = s.find(from);
    if (a == std::string::npos)
        return "";
    const size_t b = s.find(to, a);
    if (b == std::string::npos)
        return "";
    return std::regex_replace(s.substr(a, b - a), std::regex("//[^\n]*"), "");
}

}  // namespace

int main()
{
    const std::string root = DOOM_TESTS_ROOT;
    using Pair = std::pair<std::string, std::string>;

    // P_KillMobj: case labels collect until an `item = MT_Y;` closes the group.
    std::set<Pair> killed;
    {
        const std::string sw = between(slurp(root + "/p_inter.c"), "// Drop stuff.", "default:");
        check(!sw.empty(), "p_inter.c's drop switch is found");
        std::set<std::string> cases;
        const std::regex tok(R"(case\s+(MT_\w+)\s*:|item\s*=\s*(MT_\w+)\s*;)");
        for (std::sregex_iterator it(sw.begin(), sw.end(), tok), end; it != end; ++it)
        {
            if ((*it)[1].matched)
                cases.insert((*it)[1]);
            else
            {
                for (const std::string& c : cases)
                    killed.insert({c, (*it)[2]});
                cases.clear();
            }
        }
    }

    std::set<Pair> table;
    {
        const std::string t = between(slurp(root + "/r_mesh.c"), "model_drops[] = {", "};");
        check(!t.empty(), "r_mesh.c's model_drops table is found");
        const std::regex row(R"(\{\s*(MT_\w+)\s*,\s*(MT_\w+)\s*\})");
        for (std::sregex_iterator it(t.begin(), t.end(), row), end; it != end; ++it)
            table.insert({(*it)[1], (*it)[2]});
    }

    // 1.
    for (const Pair& p : killed)
        check(table.count(p) == 1, ("P_KillMobj's drop is in model_drops: " + p.first).c_str());
    for (const Pair& p : table)
        check(killed.count(p) == 1, ("model_drops' row is a P_KillMobj drop: " + p.first).c_str());
    // 2.
    check(!killed.empty(), "the scrape found P_KillMobj's drops");

    return check_summary("model_drops_test");
}
