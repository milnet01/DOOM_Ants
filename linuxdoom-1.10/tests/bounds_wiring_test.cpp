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
// INV-2's R_InitSpriteLumps clause is superseded by DOOM-0432 INV-3: that reader
// now asks W_PatchLumpOk (whose rule refuses a lump under 8 bytes) ahead of its
// first ->width, instead of calling PatchHasHeader. tile_size and
// ensure_sprite_heights still call PatchHasHeader.
//
// DOOM-0479 (labels begin "DOOM-0479"): pathtrace.comp's primary ray loop caps its
// candidates with kMaxPrimaryCandidates and ends traversal with rayQueryTerminateEXT.
//
// Not caught: a call that exists in the right place but whose result is
// discarded (INV-3 checks only that an I_Error follows LevelBspIsTree, and INV-1
// that AtlasRowsFit precedes the calloc). The unit tests hold the decision;
// this holds that the decision is consulted.
//
// It also holds DOOM-0432's wiring clauses (INV-3, INV-4's wiring half, INV-6
// to INV-10): every patch reader asks W_PatchOk / W_PatchLumpOk before it
// follows columnofs. Their labels begin "DOOM-0432 INV-n".
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU.
#include "check_util.h"

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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

// Bounds of the DEFINITION of `name` (same match rule as body_of): [*lo, *hi) is
// the text between its braces.
static bool body_span(const std::string& src, const char* name, size_t* lo, size_t* hi)
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
            else if (src[i] == '}' && --depth == 0) { *lo = open + 1; *hi = i; return true; }
        }
    }
    return false;
}

// `name` as a whole token anywhere (call or not), or npos.
static size_t whole_tok(const std::string& s, const char* name)
{
    const std::string key(name);
    size_t at = 0;
    while ((at = s.find(key, at)) != std::string::npos)
    {
        const size_t end = at + key.size();
        if ((at == 0 || !isIdent(s[at - 1])) && (end >= s.size() || !isIdent(s[end]))) return at;
        at = end;
    }
    return std::string::npos;
}

// DOOM-0432: `fn` in `src` must call `c1` (or `c2`, when given) before the first
// `guard` in its body. A missing body, a missing call, a missing guard or a call
// after the guard each FAIL, so an unwritten function reports rather than passes.
static void reader_asks(const char* file, const std::string& src, const char* fn,
                        const char* c1, const char* c2, const char* guard, const char* what)
{
    std::string b;
    if (!get_body(file, src, fn, &b)) return;
    size_t c = call_pos(b, c1);
    if (c2) c = std::min(c, call_pos(b, c2));
    if (c == std::string::npos)
    {
        std::printf("  FAIL: %s: %s() never calls %s%s%s\n", what, fn, c1, c2 ? " or " : "", c2 ? c2 : "");
        g_failures++;
        return;
    }
    const size_t r = tok_pos(b, guard);
    if (r == std::string::npos)
    {
        std::printf("  FAIL: %s: %s() has no \"%s\" to guard (test out of date?)\n", what, fn, guard);
        g_failures++;
    }
    else if (r < c)
    {
        std::printf("  FAIL: %s: %s() reads \"%s\" before it asks %s\n", what, fn, guard, c1);
        g_failures++;
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
    // R_InitSpriteLumps's header-length test moved into W_PatchLumpOk (DOOM-0432
    // spec 4.4); its ordering clause is DOOM-0432 INV-3 (header reader) below.

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

    // =====================================================================
    // DOOM-0432: a patch lump is validated once; every reader asks.
    // Why this exists: readers followed a crafted lump's columnofs[] and post
    // lengths with no idea of the lump's length. The pure rule is unit-tested
    // (patch_bounds_test, wad_bounds_test); these clauses hold that each reader
    // consults it BEFORE its first read. The implementation names are pinned by
    // the spec: W_PatchLumpOk, W_PatchOk, R_TexPatchOk, R_GetPostColumn,
    // Z_BlockUser, lumppatchok.
    // =====================================================================
    {
        const std::string wad    = strip(slurp("w_wad.c"));
        const std::string video  = strip(slurp("v_video.c"));
        const std::string finale = strip(slurp("f_finale.c"));
        const std::string things = strip(slurp("r_things.c"));
        const std::string menu   = strip(slurp("m_menu.c"));
        const std::string segs   = strip(slurp("r_segs.c"));
        const char* const kCol = "->columnofs[";

        // ---- INV-3: each reader asks ahead of its first ->columnofs[ .
        reader_asks("v_video.c", video, "V_BlitPatch", "W_PatchOk", "W_PatchLumpOk", kCol,
                    "DOOM-0432 INV-3");
        reader_asks("f_finale.c", finale, "F_DrawPatchCol", "W_PatchOk", "W_PatchLumpOk", kCol,
                    "DOOM-0432 INV-3");
        reader_asks("r_things.c", things, "R_DrawVisSprite", "W_PatchOk", "W_PatchLumpOk", kCol,
                    "DOOM-0432 INV-3");
        reader_asks("r_mesh.c", mesh, "blit_tile", "W_PatchOk", "W_PatchLumpOk", kCol,
                    "DOOM-0432 INV-3");
        reader_asks("m_menu.c", menu, "M_DecodePatchRGBA", "W_PatchLumpOk", nullptr, kCol,
                    "DOOM-0432 INV-3");
        reader_asks("r_data.c", data, "R_GenerateLookup", "R_TexPatchOk", nullptr, kCol,
                    "DOOM-0432 INV-3 / INV-6");
        reader_asks("r_data.c", data, "R_GenerateComposite", "R_TexPatchOk", nullptr, kCol,
                    "DOOM-0432 INV-3 / INV-6");
        reader_asks("r_data.c", data, "R_RenderTextureToAtlas", "R_TexPatchOk", nullptr, kCol,
                    "DOOM-0432 INV-3 / INV-6");
        reader_asks("r_data.c", data, "R_InitSpriteLumps", "W_PatchLumpOk", nullptr, "->width",
                    "DOOM-0432 INV-3 (header reader)");
        if (get_body("r_data.c", data, "R_TexPatchOk", &b))
            check(call_pos(b, "W_PatchLumpOk") != std::string::npos,
                  "DOOM-0432 INV-3: R_TexPatchOk asks W_PatchLumpOk");

        // ---- INV-3, second half: every ->columnofs[ in the engine sources sits in
        // one of the eight readers. The directory is LISTED, so a new file is seen.
        {
            static const char* const kReaders[] = {
                "V_BlitPatch", "F_DrawPatchCol", "M_DecodePatchRGBA", "R_GenerateLookup",
                "R_GenerateComposite", "R_RenderTextureToAtlas", "R_DrawVisSprite", "blit_tile" };
            std::vector<std::string> files;
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(DOOM_TESTS_ROOT, ec))
            {
                const std::string ext = e.path().extension().string();
                if (e.is_regular_file() && (ext == ".c" || ext == ".cpp" || ext == ".h"))
                    files.push_back(e.path().filename().string());
            }
            check(!ec && !files.empty(),
                  "DOOM-0432 INV-3: the engine directory can be listed for source files");
            std::sort(files.begin(), files.end());
            int sites = 0;
            for (const std::string& f : files)
            {
                const std::string src = strip(slurp(f));
                std::vector<std::pair<size_t, size_t>> spans;
                for (const char* r : kReaders)
                {
                    size_t lo, hi;
                    if (body_span(src, r, &lo, &hi)) spans.push_back({lo, hi});
                }
                size_t at = 0;
                while ((at = src.find(kCol, at)) != std::string::npos)
                {
                    sites++;
                    bool in = false;
                    for (const auto& sp : spans) if (at >= sp.first && at < sp.second) in = true;
                    if (!in)
                    {
                        const int line = 1 + (int)std::count(src.begin(), src.begin() + at, '\n');
                        std::printf("  FAIL: DOOM-0432 INV-3: %s:%d reads %s outside the eight "
                                    "validated readers\n", f.c_str(), line, kCol);
                        g_failures++;
                    }
                    at += 1;
                }
            }
            check(sites >= 8,
                  "DOOM-0432 INV-3: the scan found the readers' columnofs reads (else it is blind)");
        }

        // ---- INV-4 (wiring half).
        if (get_body("w_wad.c", wad, "W_PatchOk", &b))
        {
            check(b.find("W_CacheLump") == std::string::npos,
                  "DOOM-0432 INV-4: W_PatchOk does not go through W_CacheLump* (it would re-tag a block)");
            check(whole_tok(b, "Z_ChangeTag") == std::string::npos,
                  "DOOM-0432 INV-4: W_PatchOk does not call Z_ChangeTag");
            check(call_pos(b, "WadLumpOfUser") != std::string::npos,
                  "DOOM-0432 INV-4: W_PatchOk finds the lump with WadLumpOfUser");
            check(call_pos(b, "Z_BlockUser") != std::string::npos,
                  "DOOM-0432 INV-4: W_PatchOk reads the block's user with Z_BlockUser");
        }
        if (get_body("w_wad.c", wad, "W_PatchLumpOk", &b))
        {
            const size_t t = tok_pos(b, "lumpcache[");
            const size_t c = call_pos(b, "W_CacheLumpNum");
            check(t != std::string::npos && (c == std::string::npos || t < c),
                  "DOOM-0432 INV-4: W_PatchLumpOk tests lumpcache[ ahead of any W_CacheLumpNum call");
        }
        {
            std::string zb;
            check(get_body("z_zone.c", strip(slurp("z_zone.c")), "Z_BlockUser", &zb),
                  "DOOM-0432 INV-4: z_zone.c defines Z_BlockUser");
        }

        // ---- INV-6: the three texture readers use one helper and no words of their own.
        for (const char* fn : { "R_GenerateLookup", "R_GenerateComposite", "R_RenderTextureToAtlas" })
        {
            std::string fb;
            const bool ok = body_of(data, fn, &fb);
            check(ok && call_pos(fb, "R_TexPatchOk") != std::string::npos,
                  (std::string("DOOM-0432 INV-6: ") + fn + " asks through R_TexPatchOk").c_str());
            check(ok && whole_tok(fb, "W_PatchLumpOk") == std::string::npos &&
                  whole_tok(fb, "W_PatchOk") == std::string::npos,
                  (std::string("DOOM-0432 INV-6: ") + fn + " does not ask the verdict in its own words").c_str());
        }

        // ---- INV-7: W_Reload resets the verdict.
        if (get_body("w_wad.c", wad, "W_Reload", &b))
            check(b.find("lumppatchok[") != std::string::npos,
                  "DOOM-0432 INV-7: W_Reload resets lumppatchok[ for a reloaded lump");

        // ---- INV-8: F_DrawPatchCol compares col with the width ahead of the read.
        if (get_body("f_finale.c", finale, "F_DrawPatchCol", &b))
        {
            const size_t rd = tok_pos(b, kCol);
            const std::string pre = rd == std::string::npos ? b : b.substr(0, rd);
            check(rd != std::string::npos && pre.find("->width") != std::string::npos,
                  "DOOM-0432 INV-8: F_DrawPatchCol reads the patch width ahead of ->columnofs[");
            bool cmp = false;
            for (size_t at = 0; (at = pre.find("col", at)) != std::string::npos; at += 3)
            {
                if ((at > 0 && isIdent(pre[at - 1])) || isIdent(pre[at + 3])) continue;
                const size_t after = skipws(pre, at + 3);
                size_t before = at;
                while (before > 0 && std::isspace((unsigned char)pre[before - 1])) before--;
                const char a = after < pre.size() ? pre[after] : 0;
                const char p = before > 0 ? pre[before - 1] : 0;
                if (a == '<' || a == '>' || p == '<' || p == '>') { cmp = true; break; }
            }
            check(cmp, "DOOM-0432 INV-8: F_DrawPatchCol compares col (< or >) ahead of ->columnofs[");
        }

        // ---- INV-9: the 16-bit offset table is widened, and its allocation sized.
        {
            int decls = 0;
            bool tableDecl = false;
            size_t at = 0;
            while ((at = data.find("colofs", at)) != std::string::npos)
            {
                const size_t end = at + 6;
                const bool whole = (at == 0 || !isIdent(data[at - 1])) && !isIdent(data[end]);
                const size_t nx = skipws(data, end);
                if (whole && nx < data.size() && data[nx] == ';')
                {
                    decls++;
                    const size_t ls = data.rfind('\n', at) + 1;
                    check(data.substr(ls, at - ls).find("short") == std::string::npos,
                          "DOOM-0432 INV-9: a colofs local is not a short");
                }
                at = end;
            }
            check(decls >= 2, "DOOM-0432 INV-9: found both colofs local declarations");
            for (at = 0; (at = data.find("texturecolumnofs", at)) != std::string::npos; at += 16)
            {
                const size_t end = at + 16;
                if (isIdent(data[end]) || (at > 0 && isIdent(data[at - 1]))) continue;
                const size_t nx = skipws(data, end);
                if (nx < data.size() && data[nx] == ';')
                {
                    tableDecl = true;
                    const size_t ls = data.rfind('\n', at) + 1;
                    check(data.substr(ls, at - ls).find("short") == std::string::npos,
                          "DOOM-0432 INV-9: texturecolumnofs is not declared with short entries");
                }
            }
            check(tableDecl, "DOOM-0432 INV-9: found the texturecolumnofs declaration");
            bool alloc = false;
            for (at = 0; (at = data.find("texturecolumnofs[i]", at)) != std::string::npos; at += 19)
            {
                const size_t nx = skipws(data, at + 19);
                if (nx + 1 < data.size() && data[nx] == '=' && data[nx + 1] != '=')
                {
                    alloc = true;
                    const size_t semi = data.find(';', nx);
                    check(data.substr(nx, semi - nx).find("sizeof") != std::string::npos,
                          "DOOM-0432 INV-9: the texturecolumnofs[i] allocation multiplies by a sizeof");
                }
            }
            check(alloc, "DOOM-0432 INV-9: found the texturecolumnofs[i] allocation");
        }

        // ---- INV-10: the see-through wall walks posts only through R_GetPostColumn.
        if (get_body("r_segs.c", segs, "R_RenderMaskedSegRange", &b))
        {
            check(whole_tok(b, "R_GetPostColumn") != std::string::npos,
                  "DOOM-0432 INV-10: R_RenderMaskedSegRange names R_GetPostColumn");
            check(whole_tok(b, "R_GetColumn") == std::string::npos,
                  "DOOM-0432 INV-10: R_RenderMaskedSegRange does not name R_GetColumn");
        }
        if (get_body("r_data.c", data, "R_GetPostColumn", &b))
        {
            const size_t t = whole_tok(b, "texturecolumnlump");
            const size_t n = whole_tok(b, "NULL");
            check(t != std::string::npos && n != std::string::npos && t < n,
                  "DOOM-0432 INV-10: R_GetPostColumn tests texturecolumnlump ahead of returning NULL");
        }
    }

    // =====================================================================
    // DOOM-0479: the ray-traced view's primary ray loop caps its candidates.
    // Why this exists: `while (rayQueryProceedEXT(rq))` in pathtrace.comp had
    // nothing bounding the candidates per ray, so thousands of stacked
    // see-through sprites or walls on one sight line made a frame cost seconds
    // (4000 trees = 365 ms) and, long enough, a device timeout.
    // =====================================================================
    {
        const std::string pt = strip(slurp("shaders/pathtrace.comp"));

        // ---- Clause 1: `const uint kMaxPrimaryCandidates = N`, 64 <= N <= 4096.
        {
            const char* const nm = "kMaxPrimaryCandidates";
            bool declared = false;
            unsigned long val = 0;
            for (size_t at = 0; (at = pt.find(nm, at)) != std::string::npos; at += 1)
            {
                const size_t end = at + std::string(nm).size();
                if ((at > 0 && isIdent(pt[at - 1])) || (end < pt.size() && isIdent(pt[end]))) continue;
                size_t ls = pt.find_last_of(";{}\n", at);
                ls = (ls == std::string::npos) ? 0 : ls + 1;
                const std::string lead = pt.substr(ls, at - ls);
                const size_t eq = skipws(pt, end);
                if (whole_tok(lead, "const") == std::string::npos ||
                    whole_tok(lead, "uint") == std::string::npos ||
                    eq >= pt.size() || pt[eq] != '=' || (eq + 1 < pt.size() && pt[eq + 1] == '='))
                    continue;
                const size_t num = skipws(pt, eq + 1);
                if (num < pt.size() && std::isdigit((unsigned char)pt[num]))
                {
                    declared = true;
                    val = std::strtoul(pt.c_str() + num, nullptr, 0);
                    break;
                }
            }
            check(declared,
                  "DOOM-0479: pathtrace.comp declares `const uint kMaxPrimaryCandidates = <literal>`");
            check(declared && val >= 64 && val <= 4096,
                  "DOOM-0479: kMaxPrimaryCandidates is within [64, 4096] (clear of real content, still a cap)");
            if (declared && (val < 64 || val > 4096))
                std::printf("    expected 64..4096, actual %lu\n", val);
        }

        // ---- Clauses 2 and 3: the loop on `rq` (not the empty shadow loops on `sq`).
        std::string lb;
        {
            const std::string key = "rayQueryProceedEXT(rq)";
            const size_t at = pt.find(key);
            bool found = false;
            if (at != std::string::npos)
            {
                size_t i = skipws(pt, at + key.size());
                if (i < pt.size() && pt[i] == ')')      // the `while (` closing paren
                {
                    i = skipws(pt, i + 1);
                    if (i < pt.size() && pt[i] == '{')
                    {
                        const size_t open = i;
                        int depth = 0;
                        for (; i < pt.size(); i++)
                        {
                            if (pt[i] == '{') depth++;
                            else if (pt[i] == '}' && --depth == 0)
                            { lb = pt.substr(open + 1, i - open - 1); found = true; break; }
                        }
                    }
                }
            }
            if (!found)
            {
                std::printf("  FAIL: DOOM-0479: cannot find the body of `while (rayQueryProceedEXT(rq))` in pathtrace.comp\n");
                g_failures++;
            }
        }
        const size_t cap = whole_tok(lb, "kMaxPrimaryCandidates");
        check(cap != std::string::npos,
              "DOOM-0479: the primary candidate loop names kMaxPrimaryCandidates");
        check(call_pos(lb, "rayQueryTerminateEXT") != std::string::npos &&
              lb.find("rayQueryTerminateEXT(rq)") != std::string::npos,
              "DOOM-0479: the primary candidate loop calls rayQueryTerminateEXT(rq)");

        bool cmp = false;
        if (cap != std::string::npos)
        {
            const size_t after = skipws(lb, cap + 21);
            size_t before = cap;
            while (before > 0 && std::isspace((unsigned char)lb[before - 1])) before--;
            const char a = after < lb.size() ? lb[after] : 0;
            const char p = before > 0 ? lb[before - 1] : 0;
            cmp = a == '<' || a == '>' || a == '=' || a == '!' || p == '<' || p == '>' || p == '=';
        }
        size_t tex = std::min(whole_tok(lb, "spriteCandidateOpaque"), whole_tok(lb, "worldCandidateOpaque"));
        check(cap != std::string::npos && cmp && tex != std::string::npos && cap < tex,
              "DOOM-0479: the kMaxPrimaryCandidates comparison comes before the first spriteCandidateOpaque/worldCandidateOpaque call in the loop");
    }

    // =====================================================================
    // Small wiring clauses (source scrape; each fix is unit-untestable).
    // =====================================================================
    {
        // The statement (up to ';') that starts at the first `lhs` token followed by
        // a single '='. Returns false if there is none.
        auto assign_stmt = [](const std::string& src, const char* lhs, std::string* out) {
            const std::string key(lhs);
            for (size_t at = 0; (at = src.find(key, at)) != std::string::npos; at += 1)
            {
                if (at > 0 && (isIdent(src[at - 1]) || src[at - 1] == '.')) continue;
                const size_t eq = skipws(src, at + key.size());
                if (eq >= src.size() || src[eq] != '=' || (eq + 1 < src.size() && src[eq + 1] == '='))
                    continue;
                const size_t semi = src.find(';', eq);
                *out = src.substr(eq + 1, semi == std::string::npos ? std::string::npos : semi - eq - 1);
                return true;
            }
            return false;
        };
        std::string st;

        // ---- DOOM-0225: the device pick goes through the shared rule.
        check(has_include(vkRaw, "device_pick.h"), "DOOM-0225: r_vulkan.cpp includes device_pick.h");
        if (get_body("r_vulkan.cpp", vk, "PickPhysicalAndDevice", &b))
            check(call_pos(b, "RB_PickDevice") != std::string::npos,
                  "DOOM-0225: PickPhysicalAndDevice calls RB_PickDevice");

        // ---- DOOM-0338: the fog strength pushed to the shader is clamped.
        if (!assign_stmt(vk, "pc.misc6[2]", &st))
        {
            std::printf("  FAIL: DOOM-0338: no assignment to pc.misc6[2] found in r_vulkan.cpp\n");
            g_failures++;
        }
        else
            check(call_pos(st, "FogStrength") != std::string::npos &&
                  whole_tok(st, "rb_fog") == std::string::npos,
                  "DOOM-0338: pc.misc6[2] is assigned from FogStrength(), not a bare rb_fog");
        if (get_body("r_vulkan.cpp", vk, "FogStrength", &b))
            check(whole_tok(b, "rb_fog") != std::string::npos && b.find('3') != std::string::npos,
                  "DOOM-0338: FogStrength clamps rb_fog (names rb_fog and the limit 3)");

        // ---- DOOM-0232: FindResponseFile's file buffer has room for a terminator.
        {
            const std::string dmain = strip(slurp("d_main.c"));
            if (get_body("d_main.c", dmain, "FindResponseFile", &b))
            {
                bool seen = false, ok = false;
                for (size_t at = 0; (at = b.find("malloc", at)) != std::string::npos; at += 6)
                {
                    size_t ls = b.find_last_of(";{}", at);
                    ls = ls == std::string::npos ? 0 : ls + 1;
                    const std::string lead = b.substr(ls, at - ls);
                    if (whole_tok(lead, "file") == std::string::npos) continue;
                    seen = true;
                    const size_t semi = b.find(';', at);
                    const std::string args = b.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
                    if (args.find("size + 1") != std::string::npos || args.find("size+1") != std::string::npos)
                        ok = true;
                }
                check(seen, "DOOM-0232: found the malloc that assigns FindResponseFile's `file`");
                check(ok, "DOOM-0232: that malloc allocates size + 1 (room for the terminator)");
            }
        }

        // ---- DOOM-0229: the box-filter accumulator is wider than 32 bits.
        {
            const std::string img = strip(slurp("rb_image.c"));
            const size_t at = img.find("acc[4]");
            check(at != std::string::npos, "DOOM-0229: rb_image.c declares acc[4]");
            if (at != std::string::npos)
            {
                size_t ls = img.find_last_of(";{}", at);
                ls = ls == std::string::npos ? 0 : ls + 1;
                const size_t semi = img.find(';', at);
                const std::string decl = img.substr(ls, semi == std::string::npos ? std::string::npos : semi - ls);
                check(decl.find("uint64_t") != std::string::npos ||
                      decl.find("unsigned long long") != std::string::npos,
                      "DOOM-0229: the acc[4] accumulator is 64-bit (uint64_t or unsigned long long)");
            }
        }
    }

    return check_summary("bounds_wiring");
}
