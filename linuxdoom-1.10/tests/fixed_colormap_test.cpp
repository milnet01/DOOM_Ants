// fixed_colormap_test.cpp — DOOM-0455: invulnerability and the light-amp
// visor in the 3D views.
//
// Why this exists: the 3D views ignored player->fixedcolormap, so picking up
// either powerup changed nothing on screen in Solid or Ultra. The spec is
// docs/specs/DOOM-0455-fixed-colormap-3d.md.
//
// Contract:
//   INV-1  RB_FixedMode maps 0 to none, INVERSECOLORMAP (32) to invert, and
//          every other row in 1..NUMCOLORMAPS to the visor.
//   INV-5  At the three world sites, fixedInvert is applied to the tone
//          operator's result in a later statement: after the last tone call in
//          main, and after the bloom add where the site has one. Inverting
//          before the bloom add turns every halo into a dark smudge.
//          (Source scrape: the order is the contract, and it needs a GPU to see.)
//   INV-7  In mesh.frag the visor goes into DIRECT, never AMBIENT, bounded by
//          the headroom sector light and the flashlight leave (1.0 - sect -
//          flash). In AMBIENT, SSAO would darken a lamp at the camera at full
//          strength; without the cap, lit walls bloom under the visor.
#include <cctype>
#include <cstdio>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "../fixed_colormap.h"
#include "check_util.h"

namespace {

// Whole file as text. A missing file is a failure.
std::string slurp(const char* path)
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

// The text of main() onward, with // comments blanked, so a name in prose is
// never read as a call.
std::string mainCode(const std::string& src)
{
    const size_t m = src.find("void main()");
    if (m == std::string::npos) return std::string();
    std::string s = src.substr(m);
    for (size_t p = s.find("//"); p != std::string::npos; p = s.find("//", p))
    {
        const size_t e = s.find('\n', p);
        s.replace(p, (e == std::string::npos ? s.size() : e) - p, "");
    }
    return s;
}

bool isIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// INV-5 at one site. `tone` is the tone operator's name; `bloomAdd` is the text
// of the bloom add, or null where the site has none.
void inv5(const char* file, const char* tone, const char* bloomAdd)
{
    char what[200];
    const std::string body = mainCode(slurp((std::string(DOOM_TESTS_ROOT "/shaders/") + file).c_str()));
    std::snprintf(what, sizeof what, "INV-5: %s has a main()", file);
    check(!body.empty(), what);
    if (body.empty()) return;

    // The last tone call, and the variable its result is assigned to.
    const size_t call = body.rfind(std::string(tone) + "(");
    std::snprintf(what, sizeof what, "INV-5: %s calls %s in main", file, tone);
    check(call != std::string::npos, what);
    if (call == std::string::npos) return;
    size_t q = call;
    while (q > 0 && (body[q - 1] == ' ' || body[q - 1] == '\t')) --q;
    std::string var;
    if (q > 0 && body[q - 1] == '=')
    {
        size_t e = q - 1;
        while (e > 0 && (body[e - 1] == ' ' || body[e - 1] == '\t')) --e;
        size_t b = e;
        while (b > 0 && isIdent(body[b - 1])) --b;
        var = body.substr(b, e - b);
    }
    std::snprintf(what, sizeof what, "INV-5: %s assigns the last %s result to a variable", file, tone);
    check(!var.empty(), what);
    if (var.empty()) return;

    // The invert of that variable, in a later statement.
    const size_t inv = body.find(var + " = fixedInvert(" + var + ")", call);
    std::snprintf(what, sizeof what, "INV-5: %s applies fixedInvert to %s after the last %s",
                  file, var.c_str(), tone);
    check(inv != std::string::npos, what);
    std::snprintf(what, sizeof what, "INV-5: %s calls fixedInvert nowhere before its tone operator", file);
    check(body.find("fixedInvert(") > call, what);

    if (bloomAdd)
    {
        const size_t add = body.rfind(bloomAdd);
        std::snprintf(what, sizeof what, "INV-5: %s still adds bloom as `%s`", file, bloomAdd);
        check(add != std::string::npos, what);
        std::snprintf(what, sizeof what, "INV-5: %s inverts after the bloom add", file);
        check(add != std::string::npos && inv != std::string::npos && inv > add
                  && body.find("fixedInvert(") > add, what);
    }
}

// The body of the first `if (<cond>)` block in `code`, braces matched; empty
// when the block is missing.
std::string ifBlock(const std::string& code, const std::string& cond)
{
    const size_t at = code.find("if (" + cond + ")");
    if (at == std::string::npos) return std::string();
    const size_t open = code.find('{', at);
    if (open == std::string::npos) return std::string();
    int depth = 0;
    for (size_t i = open; i < code.size(); i++)
    {
        if (code[i] == '{') depth++;
        else if (code[i] == '}' && --depth == 0)
        {
            // Runs of whitespace become one space, so alignment never breaks a match.
            std::string out;
            for (size_t k = open; k <= i; k++)
            {
                const bool ws = std::isspace((unsigned char)code[k]);
                if (!ws) out += code[k];
                else if (out.empty() || out.back() != ' ') out += ' ';
            }
            return out;
        }
    }
    return std::string();
}

} // namespace

int main()
{
    // ---- INV-1: the three classes ---------------------------------------
    check_eq_int(RB_FixedMode(0), RB_FIXED_NONE,
                 "INV-1: fixedcolormap 0 (no powerup) -> none");
    check_eq_int(RB_FixedMode(32), RB_FIXED_INVERT,
                 "INV-1: fixedcolormap 32 (invulnerability) -> invert");
    check_eq_int(RB_FixedMode(1), RB_FIXED_VISOR,
                 "INV-1: fixedcolormap 1 (light-amp visor) -> visor");
    // Every other row Classic could be handed is shaded as the visor. 32 is
    // NUMCOLORMAPS (r_main.h) and the invert row, so the sweep stops below it.
    for (int row = 1; row < 32; row++)
        check(RB_FixedMode(row) == RB_FIXED_VISOR,
              "INV-1: a non-zero row other than 32 -> visor");

    // ---- INV-5: invert after the tone operator and the bloom add ---------
    inv5("composite.frag",      "pbrNeutralToneMapping", "* pc.bloomIntensity");
    inv5("rt_tonemap.comp",     "toneExposeEncode",      "* pc.bloomIntensity");
    // The un-split build has no bloom add; its store is the file's last tone call.
    inv5("svgf_composite.comp", "toneExposeEncode",      nullptr);

    // ---- INV-7: the raster visor is DIRECT, capped by the headroom ------
    {
        const std::string block = ifBlock(mainCode(slurp(DOOM_TESTS_ROOT "/shaders/mesh.frag")),
                                          "pc.fixedMode == RB_FIXED_VISOR");
        check(!block.empty(), "INV-7: mesh.frag has a visor block in main");
        check(block.find("direct += VISOR_TINT * albedo * min(v, room)") != std::string::npos,
              "INV-7: the visor term is added to direct, scaled by min(shade, headroom)");
        check(block.find("room = max(0.0, 1.0 - sect - flash)") != std::string::npos,
              "INV-7: the headroom is 1.0 - sect - flash");
        check(block.find("ambient") == std::string::npos,
              "INV-7: the visor block never touches ambient");
    }

    return check_summary("fixed_colormap_test");
}
