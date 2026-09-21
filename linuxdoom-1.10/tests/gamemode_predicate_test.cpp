// gamemode_predicate_test.cpp — DOOM-0391: a GameMode_t enumerator is a value,
// never a condition.
//
// WI_drawAnimatedBack opened with `if (commercial) return;`. `commercial` is an
// enumerator of GameMode_t (doomdef.h), the third one, so it is the constant 2
// and the test is always true. The function returned on the first line of its
// body for every game, and DOOM 1's animated intermission map — the burning
// cities behind the level scores — has never drawn in this fork. Its siblings
// WI_initAnimatedBack and WI_updateAnimatedBack both say `gamemode ==
// commercial` and are correct, which is what makes the single odd one out read
// as a typo rather than a decision.
//
// The compiler cannot help here. Every enumerator is a valid int, so the bare
// form is well-formed C and warns under nothing the build enables.
//
// This pins the class rather than the one line. Any of the five enumerators
// used as a bare condition is the same defect, and the next one will be written
// the same way: by someone reaching for `gamemode == X` and dropping the left
// half. A source scrape is the only route — the enumerators are compile-time
// constants, so there is no runtime state a linked test could inspect.
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>

#include "check_util.h"

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

// The GameMode_t enumerators, from doomdef.h. `indetermined` is included even
// though it is the falsy one: `if (indetermined)` is dead rather than always
// live, which is the same defect pointing the other way and just as silent.
static const char* const kGameModes[] = {
    "shareware", "registered", "commercial", "retail", "indetermined",
};

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

static void skip_spaces(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
}

// True where `line` tests one of the enumerators directly: `if (commercial)`,
// `if (!retail)`, `while (shareware)`. A comparison — `gamemode == commercial`
// — puts the enumerator on the right of an operator and is not matched, which
// is the whole point: the correct form must not trip this.
static bool tests_enumerator_directly(const std::string& line, std::string& which)
{
    static const char* const kKeywords[] = { "if", "while" };

    for (const char* kw : kKeywords)
    {
        const size_t klen = std::char_traits<char>::length(kw);
        size_t at = 0;
        while ((at = line.find(kw, at)) != std::string::npos)
        {
            const size_t start = at;
            at += klen;

            // A keyword, not the tail of an identifier such as `elif` or a
            // struct member named `while_`.
            const bool left_clean = start == 0
                || (!std::isalnum((unsigned char)line[start - 1])
                    && line[start - 1] != '_');
            if (!left_clean) continue;

            size_t i = at;
            skip_spaces(line, i);
            if (i >= line.size() || line[i] != '(') continue;
            i++;
            skip_spaces(line, i);
            while (i < line.size() && line[i] == '!') { i++; skip_spaces(line, i); }

            for (const char* mode : kGameModes)
            {
                const size_t mlen = std::char_traits<char>::length(mode);
                if (line.compare(i, mlen, mode) != 0) continue;

                size_t j = i + mlen;
                skip_spaces(line, j);
                // The condition must END here. Anything else — an operator, a
                // field access — means the enumerator is an operand, not the
                // whole test.
                if (j < line.size() && line[j] == ')')
                {
                    which = mode;
                    return true;
                }
            }
        }
    }
    return false;
}

// Strip a whole-line comment and a trailing one. Enough for this scrape: the
// engine's block comments sit on their own lines, and a false negative inside
// one costs nothing because code there does not run.
static std::string decomment(const std::string& line)
{
    size_t i = 0;
    skip_spaces(line, i);
    if (line.compare(i, 2, "//") == 0) return std::string();
    if (line.compare(i, 2, "/*") == 0) return std::string();
    if (line.compare(i, 1, "*") == 0) return std::string();
    const size_t slashes = line.find("//");
    return slashes == std::string::npos ? line : line.substr(0, slashes);
}

int main()
{
    namespace fs = std::filesystem;

    std::vector<fs::path> sources;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(DOOM_TESTS_ROOT, ec))
    {
        if (!e.is_regular_file()) continue;
        const fs::path p = e.path();
        if (p.extension() == ".c" || p.extension() == ".h") sources.push_back(p);
    }
    check(!ec, "the engine source directory can be listed");

    // A scrape that reads nothing passes by default, which is the failure this
    // whole family of tests is most prone to. Make the population visible.
    check(sources.size() > 20, "the scrape found the engine's source files");

    int offences = 0;
    for (const fs::path& p : sources)
    {
        const std::string text = slurp(p.string().c_str());
        size_t at = 0, lineno = 0;
        while (at <= text.size())
        {
            const size_t nl = text.find('\n', at);
            const std::string line = text.substr(
                at, nl == std::string::npos ? std::string::npos : nl - at);
            lineno++;

            std::string which;
            if (tests_enumerator_directly(decomment(line), which))
            {
                std::printf("  FAIL: %s:%zu tests the enumerator `%s` directly;"
                            " it is a constant, so the branch never varies"
                            " (use `gamemode == %s`)\n",
                            p.filename().string().c_str(), lineno,
                            which.c_str(), which.c_str());
                offences++;
                g_failures++;
            }

            if (nl == std::string::npos) break;
            at = nl + 1;
        }
    }

    // The matcher has to be able to say no as well as yes, or a scrape that
    // silently matched nothing at all would read exactly like a clean tree.
    std::string ignored;
    check(tests_enumerator_directly("    if (commercial)", ignored),
          "the scrape recognises a bare enumerator test");
    check(tests_enumerator_directly("    if (!retail)", ignored),
          "the scrape recognises a negated bare enumerator test");
    check(!tests_enumerator_directly("    if (gamemode == commercial)", ignored),
          "the scrape passes the correct comparison form");
    check(!tests_enumerator_directly("    if (gamemode != retail)", ignored),
          "the scrape passes the correct negated comparison form");

    if (offences == 0)
        std::printf("gamemode_predicate_test: scanned %zu source files\n",
                    sources.size());

    return check_summary("gamemode_predicate_test");
}
