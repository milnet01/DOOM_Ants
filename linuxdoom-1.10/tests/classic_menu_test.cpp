// classic_menu_test.cpp — DOOM-0211 INV-6: Classic's Video menu (RendererDef)
// lists exactly Renderer, Widescreen, Fill Screen, FPS Counter and Back, and the
// Render Effects submenu (EffectsDef) no longer exists.
//
// Why this exists: Classic reads none of the 3D tiers' settings, so a row for
// one of them on RendererDef is a control that does nothing. The rows are an
// enum in m_menu.c, which cannot be linked into a test (it wants the zone, the
// WAD and the video layer), so this scrapes the source, on the model of
// bounds_wiring_test.cpp.
//
// Method: strip comments and string literals first -- a comment above
// crispMenus[] names EffectsDef -- then read the members of the enum that ends
// in `renderer_e`, in order, and look for EffectsDef as a whole word.
//
// The enum is the row list: RendererDef.numitems is rm_end, so an entry in
// RendererMenu[] past it is never shown.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU.
#include "check_util.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

static std::string slurp(const std::string& name)
{
    const std::string path = std::string(DOOM_TESTS_ROOT) + "/" + name;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::printf("  FAIL: cannot open %s\n", path.c_str()); g_failures++; return std::string(); }
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

// Blank out comments, string and character literals (keeping newlines and
// length), so a name in a comment or message cannot satisfy or defeat a check.
// Same as bounds_wiring_test.cpp's strip.
static std::string strip(const std::string& s)
{
    std::string o = s;
    size_t i = 0;
    while (i < s.size())
    {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/')
            while (i < s.size() && s[i] != '\n') o[i++] = ' ';
        else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*')
        {
            o[i] = o[i + 1] = ' '; i += 2;
            while (i < s.size() && !(s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/'))
            { if (s[i] != '\n') o[i] = ' '; i++; }
            if (i < s.size()) { o[i] = ' '; if (i + 1 < s.size()) o[i + 1] = ' '; i += 2; }
        }
        else if (s[i] == '"' || s[i] == '\'')
        {
            const char q = s[i++];
            while (i < s.size() && s[i] != q)
            {
                if (s[i] == '\\' && i + 1 < s.size()) { o[i++] = ' '; }
                if (s[i] != '\n') o[i] = ' ';
                i++;
            }
            if (i < s.size()) i++;
        }
        else i++;
    }
    return o;
}

static bool isIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// True when `word` occurs in `s` with no identifier character either side.
static bool has_word(const std::string& s, const std::string& word)
{
    size_t at = 0;
    while ((at = s.find(word, at)) != std::string::npos)
    {
        const bool l = at == 0 || !isIdent(s[at - 1]);
        const bool r = at + word.size() >= s.size() || !isIdent(s[at + word.size()]);
        if (l && r) return true;
        at += word.size();
    }
    return false;
}

// Members, in order, of the enum whose closing brace is followed by `tag`.
static std::vector<std::string> enum_members(const std::string& src, const std::string& tag)
{
    std::vector<std::string> out;
    const size_t end = src.find("} " + tag + ";");
    if (end == std::string::npos) return out;
    const size_t open = src.rfind('{', end);
    if (open == std::string::npos) return out;
    std::string cur;
    for (size_t i = open + 1; i <= end; i++)
    {
        if (i < end && isIdent(src[i])) { cur += src[i]; continue; }
        if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    return out;
}

int main()
{
    const std::string menu = strip(slurp("m_menu.c"));

    const std::vector<std::string> want =
        { "rm_renderer", "rm_widescreen", "rm_fillstretch", "rm_fps", "rm_back", "rm_end" };
    const std::vector<std::string> got = enum_members(menu, "renderer_e");

    check(!got.empty(), "the renderer_e enum is found in m_menu.c");
    check(got == want, "renderer_e lists rm_renderer, rm_widescreen, rm_fillstretch, rm_fps, rm_back, rm_end in that order");
    if (got != want)
    {
        std::printf("  got:");
        for (const std::string& m : got) std::printf(" %s", m.c_str());
        std::printf("\n");
    }

    check(!has_word(menu, "EffectsDef"), "EffectsDef does not appear in m_menu.c outside comments");

    return check_summary("classic_menu_test");
}
