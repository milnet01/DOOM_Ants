// hu_bounds_test.cpp — DOOM-0250 B1: the chat-macro copy in HU_Responder.
//
// Why this exists: HU_Responder copied chat_macros[c] into
// `static char lastmessage[HU_MAXLINELENGTH+1]` (81 bytes) with strcpy. A
// macro comes from ~/.doomrc, where M_LoadDefaults reads up to 97 characters
// (`%99[^\n]` less the two quotes), so a hand-edited macro overran the buffer.
// (The index half, B2, was checked and found not live: `c` is an unsigned
// char, so a key below '0' wraps above 9 and the existing check refuses it.)
//
// Contract (the decision lives in hu_bounds.h so this test needs no engine,
// no WAD and no globals, as with the other *_bounds.h headers):
//
//   INV-1  HU_CopyMessage(dst, cap, src) never writes at or past dst[cap-1]
//          for any src length, and never writes anything for cap == 0.
//   INV-2  HU_CopyMessage always NUL-terminates dst when cap > 0, so the
//          result is a valid C string whatever src was.
//   INV-3  A src that fits (length < cap) is copied whole and unchanged; a
//          src that does not fit is truncated to cap-1 characters, keeping
//          the leading characters.
//   INV-4  A NULL src yields an empty string, not a crash.
//   INV-5  hu_stuff.c includes hu_bounds.h, has no unbounded strcpy/sprintf
//          into lastmessage, and makes both of its copies into lastmessage
//          through HU_CopyMessage(lastmessage, sizeof lastmessage, ...) —
//          the buffer's own size, not a literal (the wiring scrape; without it INV-1..3
//          could pass while the engine ignores them).
//
// The interface the fix must provide (does not exist yet, so this test will
// not compile until linuxdoom-1.10/hu_bounds.h does):
//   static void HU_CopyMessage(char* dst, size_t cap, const char* src);
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "../hu_bounds.h"
#include "check_util.h"

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

// hu_lib.h: HU_MAXLINELENGTH 80, so lastmessage is 81 bytes.
static const size_t kLastMessageCap = 81;
static const unsigned char kCanary = 0xA5;
static const size_t kGuard = 32;

static std::string slurp(const char* path)
{
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("  FAIL: cannot open %s\n", path); g_failures++; return std::string(); }
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// Count occurrences of `word` followed by optional spaces and `(lastmessage`.
static int count_calls_on_lastmessage(const std::string& src, const char* word)
{
    const size_t wlen = std::strlen(word);
    int n = 0;
    size_t at = 0;
    while ((at = src.find(word, at)) != std::string::npos)
    {
        size_t i = at + wlen;
        at = i;
        while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) i++;
        if (i < src.size() && src[i] == '('
            && src.compare(i + 1, 11, "lastmessage") == 0)
            n++;
    }
    return n;
}

static void ws(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
}

static bool eat(const std::string& s, size_t& i, const char* tok)
{
    const size_t n = std::strlen(tok);
    if (s.compare(i, n, tok) != 0) return false;
    i += n;
    return true;
}

// Count `HU_CopyMessage(lastmessage, sizeof lastmessage,` — the size argument
// must be the buffer's own size (`sizeof lastmessage` or `sizeof(lastmessage)`),
// whitespace tolerated. A literal such as 200 does not count.
static int count_sized_copies(const std::string& src)
{
    int n = 0;
    size_t at = 0;
    while ((at = src.find("HU_CopyMessage", at)) != std::string::npos)
    {
        size_t i = at + std::strlen("HU_CopyMessage");
        at = i;
        ws(src, i);
        if (!eat(src, i, "(")) continue;
        ws(src, i);
        if (!eat(src, i, "lastmessage")) continue;
        ws(src, i);
        if (!eat(src, i, ",")) continue;
        ws(src, i);
        if (!eat(src, i, "sizeof")) continue;
        ws(src, i);
        if (eat(src, i, "("))
        {
            ws(src, i);
            if (!eat(src, i, "lastmessage")) continue;
            ws(src, i);
            if (!eat(src, i, ")")) continue;
        }
        else if (!eat(src, i, "lastmessage"))
            continue;
        ws(src, i);
        if (!eat(src, i, ",")) continue;
        n++;
    }
    return n;
}

// Copy a src of `len` 'x' characters into a cap-sized window surrounded by
// canary bytes, and report whether anything outside the window changed.
static bool copy_stays_inside(size_t cap, size_t len, std::string& out)
{
    std::string src(len, 'x');
    unsigned char mem[kGuard + 128 + kGuard];
    std::memset(mem, kCanary, sizeof mem);
    char* dst = reinterpret_cast<char*>(mem + kGuard);

    HU_CopyMessage(dst, cap, src.c_str());

    bool clean = true;
    for (size_t i = 0; i < kGuard; i++)
        if (mem[i] != kCanary) clean = false;
    for (size_t i = kGuard + cap; i < sizeof mem; i++)
        if (mem[i] != kCanary) clean = false;
    out.assign(dst, cap);
    return clean;
}

int main()
{
    // ---- INV-1 / INV-2 / INV-3: the bounded copy ----
    // Lengths straddle the buffer: empty, short, exactly-fits (cap-1), one
    // over, and the 97-character macro the config file can produce.
    const size_t lens[] = { 0, 1, 40, kLastMessageCap - 1, kLastMessageCap,
                            kLastMessageCap + 1, 97, 100 };
    for (size_t len : lens)
    {
        std::string got;
        const bool clean = copy_stays_inside(kLastMessageCap, len, got);
        char what[160];

        std::snprintf(what, sizeof what,
                      "INV-1: a %zu-character source writes nothing outside an"
                      " %zu-byte destination", len, kLastMessageCap);
        check(clean, what);

        const size_t nul = got.find('\0');
        std::snprintf(what, sizeof what,
                      "INV-2: a %zu-character source leaves a NUL-terminated"
                      " destination", len);
        check(nul != std::string::npos, what);

        const size_t want = len < kLastMessageCap ? len : kLastMessageCap - 1;
        std::snprintf(what, sizeof what,
                      "INV-3: a %zu-character source copies %zu characters"
                      " (got %zu)", len, want,
                      nul == std::string::npos ? got.size() : nul);
        check(nul == want && got.compare(0, want, std::string(want, 'x')) == 0,
              what);
    }

    // A cap too small for anything but the terminator.
    {
        std::string got;
        const bool clean = copy_stays_inside(1, 10, got);
        check(clean, "INV-1: cap 1 writes only the terminator");
        check(got[0] == '\0', "INV-2: cap 1 leaves an empty string");
    }
    // cap 0: nothing may be written at all.
    {
        unsigned char mem[16];
        std::memset(mem, kCanary, sizeof mem);
        HU_CopyMessage(reinterpret_cast<char*>(mem + 8), 0, "abc");
        bool clean = true;
        for (unsigned char b : mem) if (b != kCanary) clean = false;
        check(clean, "INV-1: cap 0 writes nothing");
    }

    // ---- INV-4: a NULL source ----
    {
        char dst[8];
        std::memset(dst, 'z', sizeof dst);
        HU_CopyMessage(dst, sizeof dst, nullptr);
        check(dst[0] == '\0', "INV-4: a NULL source yields an empty string");
    }

    // ---- INV-5: the engine actually uses it ----
    {
        const std::string src = slurp(DOOM_TESTS_ROOT "/hu_stuff.c");
        check(src.find("hu_bounds.h") != std::string::npos,
              "INV-5: hu_stuff.c includes hu_bounds.h");
        check(count_calls_on_lastmessage(src, "strcpy") == 0
              && count_calls_on_lastmessage(src, "sprintf") == 0,
              "INV-5: hu_stuff.c has an unbounded strcpy/sprintf into lastmessage");
        const int n = count_sized_copies(src);
        if (n < 2)
            std::printf("  (HU_CopyMessage(lastmessage, sizeof lastmessage, ...)"
                        " found %d time(s), want 2: the macro copy and the"
                        " typed-line copy)\n", n);
        check(n >= 2,
              "INV-5: both copies into lastmessage go through HU_CopyMessage"
              " sized by sizeof lastmessage");
    }

    return check_summary("hu_bounds_test");
}
