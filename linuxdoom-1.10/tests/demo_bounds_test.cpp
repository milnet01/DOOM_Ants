// demo_bounds_test.cpp — a demo header must put its own player in the game.
//
// D_DoAdvanceDemo plays demo1..demo4 from whatever WAD is loaded, with no user
// action. A header whose console player's in-game byte is 0 gave that player no
// body, and the first rendered frame dereferenced the NULL mobj. G_DoPlayDemo
// wants a WAD, the zone and a level, so the decision lives in demo_bounds.h and
// is tested here, exactly as the other *_bounds.h headers are.
#include <cstring>

#include "../demo_bounds.h"
#include "check_util.h"

// MAXPLAYERS and VERSION, named here rather than included, so a change to
// either is a deliberate edit to this test.
static const int kPlayers = 4;
static const int kVersion = 110;

// A header as G_BeginRecording writes it: single player, slot 0, in game.
static void header(unsigned char h[13])
{
    std::memset(h, 0, 13);
    h[0] = kVersion;
    h[1] = 2;		// skill
    h[2] = 1;		// episode
    h[3] = 1;		// map
    h[9] = 1;		// playeringame[0]
}

int main()
{
    unsigned char h[13];

    // --- What every recorder writes plays. ---
    header(h);
    check(DemoHeaderPlayable(h, 13, kVersion, kPlayers) != 0,
	  "a single-player demo recorded as player 1 plays");
    h[DEMO_HEADER_CONSOLE] = 2;
    h[9] = h[10] = h[11] = 1;
    check(DemoHeaderPlayable(h, 13, kVersion, kPlayers) != 0,
	  "a netdemo recorded as player 3, with player 3 in the game, plays");
    check(DemoConsolePlayer(h, kPlayers) == 2,
	  "an in-range console byte is played as that slot");

    // --- The crash: the console player is not in the game. ---
    header(h);
    h[9] = 0;
    check(DemoHeaderPlayable(h, 13, kVersion, kPlayers) == 0,
	  "a demo putting nobody in the game is refused, not given a NULL body");
    header(h);
    h[DEMO_HEADER_CONSOLE] = 1;		// plays as player 2, but only 1 is in
    check(DemoHeaderPlayable(h, 13, kVersion, kPlayers) == 0,
	  "a demo whose console player is absent is refused");

    // DOOM-0371's clamp: an out-of-range byte plays as slot 0, so slot 0 is
    // the one that must be present.
    header(h);
    h[DEMO_HEADER_CONSOLE] = 200;
    check(DemoConsolePlayer(h, kPlayers) == 0,
	  "a console byte past MAXPLAYERS is clamped to slot 0");
    check(DemoHeaderPlayable(h, 13, kVersion, kPlayers) != 0,
	  "a clamped console player with slot 0 in game still plays");
    h[9] = 0;
    h[10] = 1;
    check(DemoHeaderPlayable(h, 13, kVersion, kPlayers) == 0,
	  "a clamped console player is refused when slot 0 is absent");

    // --- The checks that were already there keep their meaning. ---
    header(h);
    check(DemoHeaderPlayable(h, 12, kVersion, kPlayers) == 0,
	  "a header one byte short is refused before its in-game bytes are read");
    check(DemoHeaderPlayable(h, 0, kVersion, kPlayers) == 0,
	  "an empty demo lump is refused");
    h[0] = 109;
    check(DemoHeaderPlayable(h, 13, kVersion, kPlayers) == 0,
	  "a demo from DOOM 1.9 is refused by version");

    return check_summary("demo_bounds");
}
