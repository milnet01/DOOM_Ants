// lastgame_policy_test.cpp — DOOM-0483: test runs leave the remembered game alone.
//
// Why this exists: D_DoomMain wrote $HOME/.doom_ants_lastgame on every launch
// that found a game, test runs included. Measured before the fix: one
// `-iwad doom2.wad -bootsmoke 35` run with an empty scratch HOME left the file
// naming doom2.wad.
//
// Contract:
//   1. Each test-run flag, in any letter case, marks the launch as a test run.
//   2. A player's launch is not a test run: no arguments, -iwad, -warp, -skill.
//   3. argv[0] is never read as a flag.
//   4. Every flag in D_TEST_RUN_FLAGS is one the engine reads with M_CheckParm
//      (source scrape), so the list holds no dead names.
//   5. d_main.c writes the remembered game only when D_IsTestRun is false
//      (source scrape), so the function tested here is the one that decides.
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../lastgame_policy.h"
#include "check_util.h"

namespace {

std::string slurp(const char* path)
{
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int is_test(std::initializer_list<const char*> args)
{
    char* argv[16];
    int argc = 0;
    for (const char* a : args)
        argv[argc++] = const_cast<char*>(a);
    return D_IsTestRun(argc, argv);
}

}  // namespace

int main()
{
    // 1. Every listed flag, as written and in capitals.
    for (const char* flag : D_TEST_RUN_FLAGS)
    {
        std::string upper(flag);
        for (char& c : upper)
            c = (char)toupper((unsigned char)c);
        check(is_test({"doom", "-iwad", "doom2.wad", flag}) == 1,
              (std::string("a test flag marks a test run: ") + flag).c_str());
        check(is_test({"doom", upper.c_str()}) == 1,
              (std::string("a test flag in capitals marks a test run: ") + upper).c_str());
    }
    check(is_test({"doom", "-timedemo", "demo1", "-warp", "1", "1"}) == 1,
          "a test flag before other arguments still counts");

    // 2. A player's launches.
    check(is_test({"doom"}) == 0, "a plain launch is not a test run");
    check(is_test({"doom", "-iwad", "/wads/doom.wad"}) == 0,
          "a hand-typed -iwad is not a test run");
    check(is_test({"doom", "-iwad", "doom2.wad", "-warp", "7", "-skill", "4"}) == 0,
          "-warp and -skill are not test flags");
    check(is_test({"doom", "-bootsmok"}) == 0, "a flag is matched whole, not by prefix");

    // 3. argv[0].
    check(is_test({"-bootsmoke"}) == 0, "argv[0] is the program, never a flag");

    // 4. No dead names.
    std::string engine;
    for (const char* f : {"/d_main.c", "/d_ledger.c", "/bot_play.c", "/g_game.c", "/i_video.c"})
        engine += slurp((std::string(DOOM_TESTS_ROOT) + f).c_str());
    check(!engine.empty(), "the engine sources were read");
    for (const char* flag : D_TEST_RUN_FLAGS)
    {
        std::regex read("M_CheckParm *\\(\"" + std::string(flag) + "\"\\)");
        check(std::regex_search(engine, read),
              (std::string("the engine reads the listed flag ") + flag).c_str());
    }

    // 5. The write is guarded.
    const std::string dmain = slurp(DOOM_TESTS_ROOT "/d_main.c");
    check(std::regex_search(dmain, std::regex(
              "!D_IsTestRun *\\(myargc, *myargv\\)\\)\\s*D_WriteLastGame *\\(")),
          "d_main.c skips D_WriteLastGame on a test run");

    return check_summary("lastgame_policy_test");
}
