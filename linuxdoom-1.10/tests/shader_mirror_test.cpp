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

    return check_summary("shader_mirror_test");
}
