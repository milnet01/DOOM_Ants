// game_select_test.cpp — DOOM-0060 game-select.
//
// Proves the pure IWAD detection logic (iwad_detect.h) that the chooser depends
// on: (a) D_IwadFamily classifies every recognised IWAD filename into the right
// family and rejects everything else; (b) scanning the shared preference-ordered
// IWAD_CANDIDATES list and taking the first present per family yields the expected
// representative (retail beats registered beats shareware; doom2 beats the
// variants); (c) "both present" is true only when a DOOM 1 and a DOOM 2 rep were
// both found. No WAD, GPU, or engine link — the classifier is header-only.
//
// This is the automatable slice of DOOM-0060's verification; the boot/menu/relaunch
// behaviour is manual (see docs/specs/DOOM-0060-game-select.md).
//
// Build/run: `make test` (from linuxdoom-1.10/).
#include "../iwad_detect.h"

#include <cstdio>

static int g_failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); g_failures++; }
}

// Is `name` in the NULL-terminated present-set (case-insensitive)?
static bool in_set(const char* name, const char* const* set)
{
    for (int i = 0; set[i]; i++)
        if (iwad_streqi(name, set[i]))
            return true;
    return false;
}

// Mirror of D_DetectIwads' pure selection loop (d_main.c): over the shared
// preference-ordered IWAD_CANDIDATES, take the first present name per family.
// Reuses the REAL D_IwadFamily + IWAD_CANDIDATES, so the preference order is
// checked against production data, not a copy.
static void select_reps(const char* const* present,
                        const char** doom1, const char** doom2)
{
    *doom1 = *doom2 = 0;
    for (int i = 0; IWAD_CANDIDATES[i]; i++)
    {
        int fam = D_IwadFamily(IWAD_CANDIDATES[i]);
        const char** slot = (fam == IWAD_DOOM2) ? doom2
                          : (fam == IWAD_DOOM1) ? doom1 : 0;
        if (!slot || *slot)
            continue;
        if (in_set(IWAD_CANDIDATES[i], present))
            *slot = IWAD_CANDIDATES[i];
    }
}

int main()
{
    std::printf("game_select_test (DOOM-0060): IWAD family classification + preference\n");

    // --- (a) classification --------------------------------------------------------
    check(D_IwadFamily("doom2.wad")    == IWAD_DOOM2, "doom2.wad -> DOOM 2");
    check(D_IwadFamily("doom2f.wad")   == IWAD_DOOM2, "doom2f.wad (French) -> DOOM 2");
    check(D_IwadFamily("plutonia.wad") == IWAD_DOOM2, "plutonia.wad -> DOOM 2");
    check(D_IwadFamily("tnt.wad")      == IWAD_DOOM2, "tnt.wad -> DOOM 2");
    check(D_IwadFamily("doomu.wad")    == IWAD_DOOM1, "doomu.wad (retail) -> DOOM 1");
    check(D_IwadFamily("doom.wad")     == IWAD_DOOM1, "doom.wad (registered) -> DOOM 1");
    check(D_IwadFamily("doom1.wad")    == IWAD_DOOM1, "doom1.wad (shareware) -> DOOM 1");

    // basename extraction (paths, both separators) + case-insensitivity
    check(D_IwadFamily("/usr/share/games/doom/doom2.wad") == IWAD_DOOM2, "unix path -> basename");
    check(D_IwadFamily("C:\\Games\\DOOM.WAD") == IWAD_DOOM1, "windows path + uppercase -> DOOM 1");
    check(D_IwadFamily("Doom2.Wad") == IWAD_DOOM2, "mixed case -> DOOM 2");

    // negatives: exact-match only, so near-misses and non-IWADs are NONE
    check(D_IwadFamily("doom3.wad")  == IWAD_NONE, "doom3.wad -> none");
    check(D_IwadFamily("mydoom.wad") == IWAD_NONE, "mydoom.wad (substring, not exact) -> none");
    check(D_IwadFamily("doom2x.wad") == IWAD_NONE, "doom2x.wad -> none");
    check(D_IwadFamily("ammo.wad")   == IWAD_NONE, "a PWAD -> none");
    check(D_IwadFamily("")           == IWAD_NONE, "empty string -> none");
    check(D_IwadFamily(0)            == IWAD_NONE, "NULL -> none");

    // --- (b) preference order + (c) bothPresent -----------------------------------
    const char* d1;
    const char* d2;

    {   // both families, non-preferred members present -> preferred rep per family
        const char* present[] = { "doom.wad", "doom1.wad", "doom2.wad", "tnt.wad", 0 };
        select_reps(present, &d1, &d2);
        check(d1 && iwad_streqi(d1, "doom.wad"), "DOOM 1 rep = doom.wad (beats doom1.wad)");
        check(d2 && iwad_streqi(d2, "doom2.wad"), "DOOM 2 rep = doom2.wad (beats tnt.wad)");
        check(d1 && d2, "both present -> true");
    }
    {   // retail beats registered
        const char* present[] = { "doomu.wad", "doom.wad", 0 };
        select_reps(present, &d1, &d2);
        check(d1 && iwad_streqi(d1, "doomu.wad"), "DOOM 1 rep = doomu.wad (retail beats registered)");
        check(d2 == 0, "no DOOM 2 present -> DOOM 2 rep NULL");
        check(!(d1 && d2), "only one family -> not both present");
    }
    {   // plutonia beats tnt; doom2 beats doom2f
        const char* present[] = { "tnt.wad", "plutonia.wad", "doom2f.wad", "doom2.wad", 0 };
        select_reps(present, &d1, &d2);
        check(d2 && iwad_streqi(d2, "doom2.wad"), "DOOM 2 rep = doom2.wad (beats doom2f/plutonia/tnt)");
        check(d1 == 0, "no DOOM 1 present -> DOOM 1 rep NULL");
    }
    {   // one from each of the lesser members -> both present, those reps
        const char* present[] = { "doom1.wad", "tnt.wad", 0 };
        select_reps(present, &d1, &d2);
        check(d1 && iwad_streqi(d1, "doom1.wad"), "DOOM 1 rep = doom1.wad (only one present)");
        check(d2 && iwad_streqi(d2, "tnt.wad"), "DOOM 2 rep = tnt.wad (only one present)");
        check(d1 && d2, "both present -> true");
    }
    {   // nothing present
        const char* present[] = { 0 };
        select_reps(present, &d1, &d2);
        check(d1 == 0 && d2 == 0, "no IWADs -> both reps NULL, not both present");
    }

    if (g_failures == 0)
        std::printf("PASS: all checks green — IWAD detection is correct.\n");
    else
        std::printf("FAIL: %d check(s) failed.\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
