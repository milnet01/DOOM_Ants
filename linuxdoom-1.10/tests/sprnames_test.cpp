// sprnames_test.cpp — DOOM-0423: the sprite-name table must stay NULL-terminated.
//
// R_InitSpriteDefs' own comment asks for "a null terminated list of sprite
// names", and it finds the count by walking the list until it sees a NULL.
// sprnames was sized exactly NUMSPRITES with no terminator, so that walk read
// the global that happened to follow it -- an ASAN build reported
// "global-buffer-overflow ... 0 bytes after global variable 'sprnames'" on an
// ordinary launch with no PWAD involved. It only behaved because the neighbour
// read as zero, which is a property of the link order and not of this table.
//
// The realistic regression is someone adding a sprite: drop the NULL and the
// overflow is back, add the enumerator without the name (or the reverse) and
// numsprites stops matching NUMSPRITES. Both are pinned here.
//
// This is a source scrape rather than a link-time test because info.c cannot be
// compiled standalone -- it pulls in p_mobj.h and most of the playsim with it.
#include <cstdio>
#include <string>

#include "check_util.h"

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

static std::string slurp(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("  FAIL: cannot open %s\n", path); return std::string(); }
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// The sprite enum runs from SPR_TROO to the NUMSPRITES that closes it. Counting
// its members is what gives the name table something to be measured against.
static int count_sprite_enumerators(const std::string& h)
{
    size_t first = h.find("SPR_TROO");
    size_t last  = h.find("NUMSPRITES", first);
    if (first == std::string::npos || last == std::string::npos) return -1;
    int n = 0;
    for (size_t p = h.find("SPR_", first); p != std::string::npos && p < last;
         p = h.find("SPR_", p + 4))
        n++;
    return n;
}

int main()
{
    const std::string h = slurp(DOOM_TESTS_ROOT "/info.h");
    const std::string c = slurp(DOOM_TESTS_ROOT "/info.c");
    check(!h.empty(), "info.h is readable");
    check(!c.empty(), "info.c is readable");
    if (h.empty() || c.empty()) return check_summary("sprnames_test");

    // --- 1. Both declarations carry the room the terminator needs. ---
    check(h.find("extern char *sprnames[NUMSPRITES + 1]") != std::string::npos,
          "info.h declares sprnames with room for the terminator");

    const size_t decl = c.find("char *sprnames[NUMSPRITES + 1] = {");
    check(decl != std::string::npos,
          "info.c defines sprnames with room for the terminator");
    if (decl == std::string::npos) return check_summary("sprnames_test");

    // --- 2. The initialiser ends with the NULL the scan is looking for. ---
    // Sliced from the declaration so the comment above it, which quotes
    // R_InitSpriteDefs, cannot be mistaken for table content.
    const size_t open  = c.find('{', decl);
    const size_t close = c.find("};", open);
    check(close != std::string::npos, "the sprnames initialiser is closed");
    if (close == std::string::npos) return check_summary("sprnames_test");
    const std::string body = c.substr(open + 1, close - open - 1);

    size_t tail = body.find_last_not_of(" \t\r\n");
    const bool ends_null = tail != std::string::npos && tail >= 3
        && body.compare(tail - 3, 4, "NULL") == 0;
    check(ends_null, "the sprnames initialiser's last element is NULL");

    // --- 3. One name per sprite, so numsprites still lands on NUMSPRITES. ---
    int quotes = 0;
    for (char ch : body) if (ch == '"') quotes++;
    const int names = quotes / 2;
    check(quotes % 2 == 0, "every sprite name in the table is a closed string");
    check_eq_int(names, count_sprite_enumerators(h),
                 "the table holds exactly one name per sprite enumerator");

    return check_summary("sprnames_test");
}
