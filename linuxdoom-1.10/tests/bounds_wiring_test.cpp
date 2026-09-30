// bounds_wiring_test.cpp — security review F-A..F-D: the engine must actually
// CALL the bounds decisions the pure headers make.
//
// Why this exists: atlas_bounds.h, PatchHasHeader, LevelBspIsTree and
// nee_omni_start are each unit-tested on their own, and each unit test passes
// while the engine ignores the function -- the exact failure a bounds header
// invites. This scrapes the call sites, on the model of
// gamemode_predicate_test.cpp and shader_mirror_test.cpp. C sources cannot be
// linked into a test (they want the zone, the WAD and Vulkan), so a source
// scrape is the only route.
//
// Method: strip comments and string literals, cut out the named function's
// body by brace matching, then require the call to be in that body and, where
// order matters, BEFORE the read it guards. Anchors are function names and the
// bounds functions' own names, never line numbers.
//
// Not caught: a call that exists in the right place but whose result is
// discarded (INV-3 checks only that an I_Error follows LevelBspIsTree, and INV-1
// that AtlasRowsFit precedes the calloc). The unit tests hold the decision;
// this holds that the decision is consulted.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU.
#include "check_util.h"

#include <cctype>
#include <cstdio>
#include <string>

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

static size_t skipws(const std::string& s, size_t i)
{
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    return i;
}

// Body (between the braces) of the DEFINITION of `name`: the occurrence whose
// next token is '(' and whose parameter list is followed by '{'. A call or a
// prototype does not match.
static bool body_of(const std::string& src, const char* name, std::string* body)
{
    const std::string key(name);
    size_t at = 0;
    while ((at = src.find(key, at)) != std::string::npos)
    {
        const size_t end = at + key.size();
        const bool whole = (at == 0 || !isIdent(src[at - 1])) &&
                           (end >= src.size() || !isIdent(src[end]));
        at = end;
        if (!whole) continue;
        size_t i = skipws(src, end);
        if (i >= src.size() || src[i] != '(') continue;
        int depth = 0;
        for (; i < src.size(); i++)
        {
            if (src[i] == '(') depth++;
            else if (src[i] == ')' && --depth == 0) { i++; break; }
        }
        i = skipws(src, i);
        if (i >= src.size() || src[i] != '{') continue;
        const size_t open = i;
        depth = 0;
        for (; i < src.size(); i++)
        {
            if (src[i] == '{') depth++;
            else if (src[i] == '}' && --depth == 0)
            { *body = src.substr(open + 1, i - open - 1); return true; }
        }
    }
    return false;
}

// Position of `name` followed by '(' as a whole token, or npos.
static size_t call_pos(const std::string& s, const char* name)
{
    const std::string key(name);
    size_t at = 0;
    while ((at = s.find(key, at)) != std::string::npos)
    {
        const size_t end = at + key.size();
        const bool whole = (at == 0 || !isIdent(s[at - 1])) && (end >= s.size() || !isIdent(s[end]));
        if (whole) { size_t i = skipws(s, end); if (i < s.size() && s[i] == '(') return at; }
        at = end;
    }
    return std::string::npos;
}

static size_t tok_pos(const std::string& s, const char* tok)
{
    return s.find(tok);
}

static bool has_include(const std::string& raw, const char* header)
{
    return raw.find(std::string("#include \"") + header + "\"") != std::string::npos;
}

static bool get_body(const std::string& file, const std::string& stripped,
                     const char* fn, std::string* body)
{
    if (!body_of(stripped, fn, body))
    {
        std::printf("  FAIL: cannot find the definition of %s in %s\n", fn, file.c_str());
        g_failures++;
        return false;
    }
    return true;
}

// The call must exist in `body`, and precede the first occurrence of `guarded`
// (a read it is meant to guard) when one is given.
static void require_call_before(const std::string& body, const char* fn, const char* call,
                                const char* guarded, const char* what)
{
    const size_t c = call_pos(body, call);
    if (c == std::string::npos)
    {
        std::printf("  FAIL: %s: %s() does not call %s\n", what, fn, call);
        g_failures++;
        return;
    }
    if (guarded)
    {
        const size_t r = tok_pos(body, guarded);
        if (r != std::string::npos && r < c)
        {
            std::printf("  FAIL: %s: %s() reads \"%s\" before it calls %s\n",
                        what, fn, guarded, call);
            g_failures++;
        }
    }
}

int main()
{
    const std::string meshRaw = slurp("r_mesh.c");
    const std::string dataRaw = slurp("r_data.c");
    const std::string setupRaw = slurp("p_setup.c");
    const std::string vkRaw = slurp("r_vulkan.cpp");
    const std::string mesh = strip(meshRaw), data = strip(dataRaw),
                      setup = strip(setupRaw), vk = strip(vkRaw);
    std::string b;

    // ---- INV-1 (F-A): the atlas builder consults atlas_bounds.h.
    check(has_include(meshRaw, "atlas_bounds.h"), "INV-1: r_mesh.c includes atlas_bounds.h");
    if (get_body("r_mesh.c", mesh, "tile_size", &b))
        require_call_before(b, "tile_size", "AtlasClampTile", nullptr,
                            "INV-1: tile height is clamped by the shared decision");
    if (get_body("r_mesh.c", mesh, "RB_BuildAtlas", &b))
    {
        require_call_before(b, "RB_BuildAtlas", "AtlasRowsFit", "calloc",
                            "INV-1: the running row total is checked before the atlas is allocated");
    }

    // ---- INV-2 (F-B): every sprite header read is preceded by PatchHasHeader.
    check(has_include(meshRaw, "patch_bounds.h"), "INV-2: r_mesh.c includes patch_bounds.h");
    check(has_include(dataRaw, "patch_bounds.h"), "INV-2: r_data.c includes patch_bounds.h");
    if (get_body("r_mesh.c", mesh, "tile_size", &b))
        require_call_before(b, "tile_size", "PatchHasHeader", "->width",
                            "INV-2: the sprite branch checks the lump holds a header");
    if (get_body("r_mesh.c", mesh, "ensure_sprite_heights", &b))
        require_call_before(b, "ensure_sprite_heights", "PatchHasHeader", "->height",
                            "INV-2: the sprite-height cache checks the lump holds a header");
    if (get_body("r_data.c", data, "R_InitSpriteLumps", &b))
        require_call_before(b, "R_InitSpriteLumps", "PatchHasHeader", "->width",
                            "INV-2: R_InitSpriteLumps checks the lump holds a header");

    // ---- INV-3 (F-C): P_LoadNodes verifies the nodes form a tree, and reacts.
    if (get_body("p_setup.c", setup, "P_LoadNodes", &b))
    {
        require_call_before(b, "P_LoadNodes", "LevelBspIsTree", nullptr,
                            "INV-3: the node list is checked to be a tree");
        const size_t c = call_pos(b, "LevelBspIsTree");
        check(c != std::string::npos && b.find("I_Error", c) != std::string::npos,
              "INV-3: a failed LevelBspIsTree reaches I_Error");
    }

    // ---- INV-4 (F-D): r_vulkan.cpp derives every omniStart push constant
    // (misc4[1]) through nee_omni_start and never from the static count alone.
    check(has_include(vkRaw, "nee_sampling.h"), "INV-4: r_vulkan.cpp includes nee_sampling.h");
    {
        int sites = 0;
        size_t at = 0;
        const std::string key = "misc4[1]";
        while ((at = vk.find(key, at)) != std::string::npos)
        {
            size_t i = skipws(vk, at + key.size());
            at += key.size();
            if (i >= vk.size() || vk[i] != '=' || (i + 1 < vk.size() && vk[i + 1] == '='))
                continue;                                   // a read or comparison
            const size_t semi = vk.find(';', i);
            const std::string rhs = vk.substr(i, semi == std::string::npos ? 0 : semi - i);
            sites++;
            if (call_pos(rhs, "nee_omni_start") == std::string::npos)
            {
                std::printf("  FAIL: INV-4: an omniStart push constant is assigned without "
                            "nee_omni_start: pc.misc4[1] =%s\n", rhs.c_str());
                g_failures++;
            }
        }
        check(sites >= 2, "INV-4: found both omniStart assignments (display path and rtverify path)");
    }

    return check_summary("bounds_wiring");
}
