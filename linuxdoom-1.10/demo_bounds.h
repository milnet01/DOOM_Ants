// demo_bounds.h — which demo headers G_DoPlayDemo may start playing.
//
// A demo lump is untrusted input (docs/standards/security.md names a WAD a trust
// boundary), and D_DoAdvanceDemo plays demo1..demo4 from whatever WAD is loaded
// with no user action. DOOM-0254 bounded the 13-byte header's LENGTH and
// DOOM-0371 clamped its console-player byte to MAXPLAYERS, but nothing asked
// whether that player is one the header puts in the game.
//
// What that allowed: a header whose playeringame byte for the console player is
// 0. P_SetupLevel spawns bodies only for players in the game, so the console
// player has none, and the first rendered frame dereferences
// players[consoleplayer].mo -- NULL -- in R_SetupFrame. Every valid demo records
// its own player as present, so refusing this changes nothing that plays.
//
// Factored out here, rather than left inline in g_game.c, so
// tests/demo_bounds_test.cpp can hold the boundary cases against it with no WAD
// and no level -- the same reason the other *_bounds.h headers exist.
#ifndef DEMO_BOUNDS_H
#define DEMO_BOUNDS_H

// The header G_BeginRecording writes: version, skill, episode, map, deathmatch,
// respawn, fast, nomonsters, consoleplayer, then one in-game byte per player.
#define DEMO_HEADER_CONSOLE	8
#define DEMO_HEADER_INGAME	9

// The slot G_DoPlayDemo plays as: the header's byte, or 0 when it is not a slot
// at all (DOOM-0371's clamp, kept so the attract loop survives such a demo).
static inline int DemoConsolePlayer (const unsigned char* header, int maxplayers)
{
    int	cp = header[DEMO_HEADER_CONSOLE];

    return cp >= maxplayers ? 0 : cp;
}

// May a demo lump of `length` bytes starting at `header` be played by an engine
// of `version` with `maxplayers` slots? It must hold the whole header, carry the
// engine's version, and put its console player in the game.
static inline int DemoHeaderPlayable (const unsigned char* header, int length,
				      int version, int maxplayers)
{
    if (!header || maxplayers < 1 || length < DEMO_HEADER_INGAME + maxplayers)
	return 0;

    if (header[0] != version)
	return 0;

    return header[DEMO_HEADER_INGAME + DemoConsolePlayer (header, maxplayers)]
	   != 0;
}

#endif // DEMO_BOUNDS_H
