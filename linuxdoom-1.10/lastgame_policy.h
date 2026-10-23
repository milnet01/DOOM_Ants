// lastgame_policy.h — DOOM-0483: which launches may change the remembered game.
//
// DOOM-0060 remembers the IWAD each launch loaded, so the next plain launch
// opens the same game. Test and measurement runs also name their IWAD, and
// used to overwrite that memory: after a demo-fixture run on doom2.wad, the
// player's next double-click opened DOOM 2.
//
// A run carrying any flag below exists to check or measure the engine, not to
// play, so it does not write the remembered game. A player's own choice still
// does, including one typed with -iwad. A test flag added later belongs here;
// tests/lastgame_policy_test.cpp checks that every name below is one the
// engine reads, but cannot see a flag missing from the list.
//
// No DOOM headers and no I/O, so tests/lastgame_policy_test.cpp includes it as is.
#ifndef LASTGAME_POLICY_H
#define LASTGAME_POLICY_H

#include "iwad_detect.h"	// iwad_streqi: M_CheckParm matches flags case-insensitively too

static const char* const D_TEST_RUN_FLAGS[] = {
    "-bootsmoke", "-timedemo", "-playdemo", "-devshot", "-shotverify",
    "-shotcompare", "-noinput", "-nodraw", "-inspect", "-freeze",
};

// 1 when argv marks this launch as a test or measurement run. argv[0] is the
// program and is never a flag.
static inline int D_IsTestRun(int argc, char** argv)
{
    int i;
    unsigned f;

    for (i = 1; i < argc; i++)
	for (f = 0; f < sizeof(D_TEST_RUN_FLAGS) / sizeof(D_TEST_RUN_FLAGS[0]); f++)
	    if (iwad_streqi(argv[i], D_TEST_RUN_FLAGS[f]))
		return 1;
    return 0;
}

#endif
