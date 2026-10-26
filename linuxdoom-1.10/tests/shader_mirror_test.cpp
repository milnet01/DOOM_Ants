// shader_mirror_test.cpp — DOOM-0408. Shader constants that must equal a C++ twin.
//
// C++ cannot read a GLSL constant, so a few values live twice: once in mesh.frag,
// once in r_vulkan.cpp. Each pair was bound only by a comment saying "must match".
// All agree today. This test makes the next edit to one side fail until the other
// follows.
//
//   - the shadow map's size: mesh.frag's PCF texel step against kShadowDim, the
//     size the shadow image is created at. A mismatch smears or gaps the PCF taps.
//   - the torch's offset from the eye: FLASH_OFF_RIGHT/UP against kFlashOffRight/Up,
//     which place the shadow pass's viewpoint. A mismatch detaches every flashlight
//     shadow from the beam that casts it.
//   - the per-subsector light slot count: RASTER_MAX_LIGHTS against
//     RASTER_MAX_LIGHTS_PER_SUBSECTOR, the stride of the buffer the shader indexes.
//     A mismatch reads the next subsector's lights, or past the buffer.
//   - the floats per light record: RASTER_LIGHT_FLOATS on both sides (DOOM-0482).
//     A mismatch reads every light after the first from the wrong offset.
//   - the powerup mode codes, RB_FIXED_* in fixed_colormap.h and
//     formulas/fixed_colormap.glsl, and RB_INVERSECOLORMAP against p_user.c's
//     INVERSECOLORMAP (DOOM-0455 INV-2). A mismatch shows the wrong powerup, or
//     none, in the 3D views.
//   - DOOM-0273 INV-10: the members of the RT push block (pathtrace.comp's PC
//     against r_vulkan.cpp's RtPushConstants) end misc6, misc7 on both sides, and
//     ShadowUbo (mesh.frag) and ShadowUboData (r_vulkan.cpp) are lightVP, flags,
//     and the setting's on value, SURF_DETAIL_ON against kSurfaceDetailOn.
//     A field moved on one side alone shifts every lane after it.
//
// Source-scraping, like bloom_threshold_test.cpp: each value is found by its
// constant's name, never by line position.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU, no display.
#include "check_util.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef DOOM_TESTS_ROOT
#error "DOOM_TESTS_ROOT must be defined (the Makefile passes it)"
#endif

static std::string slurp(const char* path)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::printf("  FAIL: cannot open %s\n", path); return std::string(); }
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

static bool isIdent(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// The value assigned at the first place `name` appears as a whole token followed by
// a single `=`: a declaration, not a use or a comparison.
static bool assigned(const std::string& src, const char* name, double* out)
{
    const std::string key(name);
    for (size_t p = src.find(key); p != std::string::npos; p = src.find(key, p + 1))
    {
        if (p > 0 && isIdent(src[p - 1])) continue;
        size_t q = p + key.size();
        if (q < src.size() && isIdent(src[q])) continue;
        while (q < src.size() && (src[q] == ' ' || src[q] == '\t')) ++q;
        if (q >= src.size() || src[q] != '=' || (q + 1 < src.size() && src[q + 1] == '='))
            continue;
        char* end = nullptr;
        *out = std::strtod(src.c_str() + q + 1, &end);
        return end != src.c_str() + q + 1;
    }
    return false;
}

// The value of `#define <name> <value>`, which assigned() cannot read: a define
// has no `=`, so its value is the token after the name.
static bool defined(const std::string& src, const char* name, double* out)
{
    const std::string key(name);
    for (size_t p = src.find("#define"); p != std::string::npos; p = src.find("#define", p + 1))
    {
        size_t q = p + 7;
        while (q < src.size() && (src[q] == ' ' || src[q] == '\t')) ++q;
        if (src.compare(q, key.size(), key) != 0) continue;
        q += key.size();
        if (q < src.size() && isIdent(src[q])) continue;
        char* end = nullptr;
        *out = std::strtod(src.c_str() + q, &end);
        return end != src.c_str() + q;
    }
    return false;
}

static void same(const std::string& a, const char* aName,
                 const std::string& b, const char* bName)
{
    double va = 0.0, vb = 0.0;
    const bool ha = assigned(a, aName, &va);
    const bool hb = assigned(b, bName, &vb);
    char what[200];
    std::snprintf(what, sizeof what, "%s is still an assigned constant", aName);
    check(ha, what);
    std::snprintf(what, sizeof what, "%s is still an assigned constant", bName);
    check(hb, what);
    if (ha && hb)
    {
        std::snprintf(what, sizeof what, "%s equals %s (%g vs %g)", aName, bName, va, vb);
        check(va == vb, what);
    }
}

// The member names of the first block opened after `opener`, in order: each
// statement's last identifier, with an array's [N] dropped. Comments are skipped.
static std::vector<std::string> members(const std::string& src, const char* opener)
{
    std::vector<std::string> out;
    const size_t at = src.find(opener);
    if (at == std::string::npos) return out;
    size_t p = src.find('{', at);
    if (p == std::string::npos) return out;
    std::string stmt;
    for (++p; p < src.size() && src[p] != '}'; ++p)
    {
        if (src.compare(p, 2, "//") == 0) { p = src.find('\n', p); if (p == std::string::npos) break; continue; }
        if (src[p] != ';') { stmt += src[p]; continue; }
        const size_t br = stmt.find('[');
        if (br != std::string::npos) stmt.resize(br);
        size_t e = stmt.size();
        while (e > 0 && !isIdent(stmt[e - 1])) --e;
        size_t b = e;
        while (b > 0 && isIdent(stmt[b - 1])) --b;
        out.push_back(stmt.substr(b, e - b));
        stmt.clear();
    }
    return out;
}

static std::string tail(const std::vector<std::string>& m, size_t n)
{
    std::string s;
    for (size_t i = m.size() > n ? m.size() - n : 0; i < m.size(); ++i) s += m[i] + " ";
    return s;
}

int main()
{
    const std::string vk = slurp(DOOM_TESTS_ROOT "/r_vulkan.cpp");
    const std::string ms = slurp(DOOM_TESTS_ROOT "/shaders/mesh.frag");
    check(!vk.empty() && !ms.empty(), "both sources are readable");

    // The PCF step is written `texel = 1.0 / <size>`, so the size is the divisor.
    double shadowDim = 0.0;
    check(assigned(vk, "kShadowDim", &shadowDim), "kShadowDim is still an assigned constant");
    const size_t t = ms.find("texel = 1.0 /");
    check(t != std::string::npos, "mesh.frag still writes its PCF step as texel = 1.0 / <size>");
    if (t != std::string::npos && shadowDim > 0.0)
    {
        const double divisor = std::strtod(ms.c_str() + t + 13, nullptr);
        char what[160];
        std::snprintf(what, sizeof what,
                      "mesh.frag's PCF texel step matches kShadowDim (1/%g vs 1/%g)",
                      divisor, shadowDim);
        check(divisor == shadowDim, what);
    }

    same(ms, "FLASH_OFF_RIGHT", vk, "kFlashOffRight");
    same(ms, "FLASH_OFF_UP",    vk, "kFlashOffUp");
    same(ms, "RASTER_MAX_LIGHTS", vk, "RASTER_MAX_LIGHTS_PER_SUBSECTOR");
    same(ms, "RASTER_LIGHT_FLOATS", vk, "RASTER_LIGHT_FLOATS");

    // DOOM-0455 INV-2: the powerup mode codes, C header against GLSL include.
    const std::string fh = slurp(DOOM_TESTS_ROOT "/fixed_colormap.h");
    const std::string fg = slurp(DOOM_TESTS_ROOT "/shaders/formulas/fixed_colormap.glsl");
    const std::string pu = slurp(DOOM_TESTS_ROOT "/p_user.c");
    check(!fh.empty() && !fg.empty() && !pu.empty(), "the fixed-colormap sources are readable");
    same(fh, "RB_FIXED_NONE",   fg, "RB_FIXED_NONE");
    same(fh, "RB_FIXED_VISOR",  fg, "RB_FIXED_VISOR");
    same(fh, "RB_FIXED_INVERT", fg, "RB_FIXED_INVERT");
    double inv = 0.0, rbInv = 0.0;
    check(defined(pu, "INVERSECOLORMAP", &inv), "p_user.c still #defines INVERSECOLORMAP");
    check(assigned(fh, "RB_INVERSECOLORMAP", &rbInv), "RB_INVERSECOLORMAP is still an assigned constant");
    char what[160];
    std::snprintf(what, sizeof what, "RB_INVERSECOLORMAP equals p_user.c's INVERSECOLORMAP (%g vs %g)",
                  rbInv, inv);
    check(inv == rbInv && inv != 0.0, what);

    // DOOM-0273 INV-10: the misc7 lane and the ShadowUbo flags, by member order.
    const std::string pt = slurp(DOOM_TESTS_ROOT "/shaders/pathtrace.comp");
    check(!pt.empty(), "pathtrace.comp is readable");
    const auto glslPc = members(pt, "layout(push_constant) uniform PC");
    const auto cPc    = members(vk, "struct RtPushConstants");
    std::printf("  [INV-10] PC tail: glsl \"%s\" c \"%s\"\n",
                tail(glslPc, 2).c_str(), tail(cPc, 2).c_str());
    check(tail(glslPc, 2) == "misc6 misc7 ", "pathtrace.comp's PC block ends misc6, misc7");
    check(tail(cPc, 2) == "misc6 misc7 ", "RtPushConstants ends misc6, misc7");
    const auto glslUbo = members(ms, "uniform ShadowUbo");
    const auto cUbo    = members(vk, "struct ShadowUboData");
    check(tail(glslUbo, 9) == "lightVP flags ", "mesh.frag's ShadowUbo is lightVP, flags");
    check(tail(cUbo, 9) == "lightVP flags ", "r_vulkan.cpp's ShadowUboData is lightVP, flags");
    const std::string sd = slurp(DOOM_TESTS_ROOT "/shaders/formulas/surface_detail.glsl");
    check(!sd.empty(), "surface_detail.glsl is readable");
    same(sd, "SURF_DETAIL_ON", vk, "kSurfaceDetailOn");

    return check_summary("shader_mirror_test");
}
