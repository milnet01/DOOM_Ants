// iwad_detect.h — DOOM-0060 game-select: pure IWAD-family classification.
//
// No DOOM headers and no I/O, so it is shared verbatim by d_main.c (which does the
// directory scan + relaunch) and tests/game_select_test.cpp (which unit-tests the
// classifier). See docs/specs/DOOM-0060-game-select.md.
#ifndef IWAD_DETECT_H
#define IWAD_DETECT_H

enum { IWAD_NONE = 0, IWAD_DOOM1 = 1, IWAD_DOOM2 = 2 };

// Case-insensitive whole-string equality (ASCII).
static inline int iwad_streqi(const char* a, const char* b)
{
    for (; *a && *b; a++, b++)
    {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

// Classify an IWAD path or bare filename into DOOM 1 / DOOM 2 / none by exact
// (case-insensitive) basename match against the names IdentifyVersion recognises.
// DOOM 2 family: doom2, doom2f (French), plutonia, tnt. DOOM 1 family: doomu
// (retail/Ultimate), doom (registered), doom1 (shareware). Anything else -> none.
// The DOOM 2 names are tested first so a "doom2.wad" (which also contains "doom")
// is never mis-bucketed — the tests are exact-match, so ordering is not actually
// load-bearing, but it keeps the two families visually grouped.
static inline int D_IwadFamily(const char* path)
{
    const char* base;
    const char* p;

    if (!path)
        return IWAD_NONE;
    base = path;
    for (p = path; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;

    if (iwad_streqi(base, "doom2.wad") || iwad_streqi(base, "doom2f.wad")
        || iwad_streqi(base, "plutonia.wad") || iwad_streqi(base, "tnt.wad"))
        return IWAD_DOOM2;
    if (iwad_streqi(base, "doomu.wad") || iwad_streqi(base, "doom.wad")
        || iwad_streqi(base, "doom1.wad"))
        return IWAD_DOOM1;
    return IWAD_NONE;
}

// Preference-ordered IWAD candidate basenames — best/most-complete first within
// each family: DOOM 1 retail (doomu, episode 4) -> registered (doom) -> shareware
// (doom1); DOOM 2 English retail -> French -> Plutonia -> TNT. D_DetectIwads scans
// these in order and takes the first present per family, so this list IS the
// preference order; tests/game_select_test.cpp reuses it so the order is verified
// against the real data. NULL-terminated. (static const in a header -> one private
// copy per translation unit; the array is tiny.)
static const char* const IWAD_CANDIDATES[] =
{
    "doomu.wad", "doom.wad", "doom1.wad",
    "doom2.wad", "doom2f.wad", "plutonia.wad", "tnt.wad",
    (const char*)0
};

#endif // IWAD_DETECT_H
