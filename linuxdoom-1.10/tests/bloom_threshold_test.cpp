// bloom_threshold_test.cpp — DOOM-0331 §10 Q2. The floor INV-4 and INV-9 both rest on.
//
// Both invariants break on the same condition, and they say so in the same words:
// "a preset's ramp start (threshold − knee) drops below 1.0". Below paper-white,
// ordinary lit art starts extracting — INV-4's "only genuinely over-white light
// blooms" fails on a plain wall, and INV-9's sky begins GENERATING its own halo
// instead of merely receiving a neighbour's.
//
// INV-4's own written test surface checks that floor by reading kBloomPresets. That
// check was true and blind on 2026-08-20, and this test exists because of the gap:
// DOOM-0345 added kBloomRasterScale, a sixth push constant that multiplied the raster
// chain's peak BEFORE the threshold comparison. Multiplying the peak by 1.5 is
// arithmetically a DIVISION of every ramp start by 1.5, so Medium's 1.15 became 0.767
// on that chain while the preset table it is read from never moved. Measured
// 2026-08-21: a plain STARTAN3 wall at sector light 255 with no emitter in frame and
// the flashlight off bloomed across 15.9 % of pixels on the raster chain, while both
// traced arms read 0.0 %. Nothing in tests/ could see it.
//
// So this test asks the question in the units the breach actually occurred in — what
// is the ramp start AFTER every per-chain factor the threshold test applies — rather
// than reading the table and stopping.
//
// §4.2 is what makes the DIRECT/AMBIENT split the right repair rather than a smaller
// constant: the ramp start bounds AMBIENT, which is sector light plus a GI bounce and
// tops out near paper-white, while DIRECT is unbounded BY DESIGN — INV-4 states
// outright that a heavily point-lit wall is expected to bloom. A per-chain scale
// therefore belongs on the term whose units genuinely differ between the chains, and
// on that term alone.
//
// Source-scraping, deliberately: the arithmetic under test is GLSL and no C++ test can
// execute it. Every claim below is anchored on a token that carries meaning (a
// constant's name, an operand pairing), never on a line's position or its whitespace,
// which is normalised away first — so reformatting the shader cannot redden this and
// changing what multiplies what cannot leave it green.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU, no display.
#include "check_util.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

// The floor both invariants name. A ramp start at exactly 1.0 is legal — High sits
// there on purpose ("on the floor", kBloomPresets) — so the comparison is >=.
static const float kRampFloor = 1.0f;

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

// Collapse every run of whitespace to one space, so an assertion about WHAT multiplies
// WHAT survives a reflow of the line it lives on.
static std::string squeeze(const std::string& in)
{
    std::string out;
    bool ws = false;
    for (char c : in)
    {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ws = true; continue; }
        if (ws && !out.empty()) out.push_back(' ');
        ws = false;
        out.push_back(c);
    }
    return out;
}

static bool has(const std::string& hay, const char* needle)
{
    return hay.find(needle) != std::string::npos;
}

// Pull the float initialiser of `static const float <name> = <v>f;`.
static bool scalar(const std::string& src, const char* name, float* out)
{
    std::string key = std::string("float ") + name;
    size_t p = src.find(key);
    if (p == std::string::npos) return false;
    p = src.find('=', p);
    if (p == std::string::npos) return false;
    *out = std::strtof(src.c_str() + p + 1, nullptr);
    return true;
}

int main()
{
    const std::string vk  = slurp(DOOM_TESTS_ROOT "/r_vulkan.cpp");
    const std::string ex  = slurp(DOOM_TESTS_ROOT "/shaders/bloom_extract_raster.comp");
    check(!vk.empty(), "r_vulkan.cpp is readable");
    check(!ex.empty(), "bloom_extract_raster.comp is readable");
    if (vk.empty() || ex.empty()) return check_summary("bloom_threshold_test");

    // ---- 1. Every preset's ramp start clears the floor (INV-4's own clause). ----
    size_t tp = vk.find("kBloomPresets[4]");
    check(tp != std::string::npos, "the kBloomPresets[4] table is still declared");
    std::vector<float> starts;
    if (tp != std::string::npos)
    {
        size_t end = vk.find("};", tp);
        size_t p   = vk.find('{', tp);          // the initialiser's own brace
        while (p != std::string::npos && end != std::string::npos)
        {
            p = vk.find('{', p + 1);
            if (p == std::string::npos || p > end) break;
            float thr = std::strtof(vk.c_str() + p + 1, nullptr);
            size_t c1 = vk.find(',', p);
            float knee = (c1 == std::string::npos) ? 0.0f : std::strtof(vk.c_str() + c1 + 1, nullptr);
            starts.push_back(thr - knee);
        }
        check_eq_int((long)starts.size(), 4, "the preset table still has four rows");
    }
    static const char* kName[4] = { "Off", "Low", "Medium", "High" };
    for (size_t i = 0; i < starts.size() && i < 4; ++i)
    {
        char what[160];
        std::snprintf(what, sizeof what,
                      "preset %s starts extracting at or above paper-white "
                      "(threshold - knee = %.3f, floor %.2f)",
                      kName[i], (double)starts[i], (double)kRampFloor);
        check(starts[i] >= kRampFloor, what);
    }

    // ---- 2. The raster chain's per-chain scale does not divide that floor. ----
    // kBloomRasterScale is a unit conversion for the threshold test. Applied to the
    // recombined colour it divides every ramp start above; applied to DIRECT alone it
    // leaves AMBIENT — the term §4.2's floor actually bounds — measured against the
    // table's own numbers.
    float chainScale = 0.0f;
    const bool haveScale = scalar(vk, "kBloomRasterScale", &chainScale);
    check(haveScale, "kBloomRasterScale is still a named constant in r_vulkan.cpp");
    check(chainScale > 0.0f, "kBloomRasterScale is positive");

    const std::string exq = squeeze(ex);

    // The operand pairing IS the invariant: chainScale multiplies the DIRECT term.
    check(has(exq, "sp.direct * pc.chainScale"),
          "the raster extract scales the DIRECT term by chainScale");

    // ...and nothing scales the recombined colour or the combined peak by it. Those
    // are the two shapes that put the division back; `c` is the extracted colour and
    // `peak` the value compared against the threshold.
    check(!has(exq, "c.r, max(c.g, c.b)) * pc.chainScale"),
          "the raster extract does not scale the recombined peak by chainScale");
    check(!has(exq, "c * pc.chainScale") && !has(exq, "pc.chainScale * c"),
          "the raster extract does not scale the recombined colour by chainScale");
    check(!has(exq, "sp.ambient * pc.chainScale") && !has(exq, "pc.chainScale * sp.ambient"),
          "the raster extract does not scale the AMBIENT term by chainScale");

    // Report the counterfactual, so a failure above explains itself rather than
    // merely firing: this is the number the 2026-08-21 breach actually ran at.
    if (haveScale && starts.size() > 2)
        std::printf("  note: were chainScale (%.2f) applied to the sum, Medium's ramp "
                    "start would be %.3f (floor %.2f)\n",
                    (double)chainScale, (double)(starts[2] / chainScale), (double)kRampFloor);

    // ---- 2b. The non-finite guard covers every term the threshold reads. ----
    // §4.4 puts the NaN/Inf guard in this pass because one bad texel becomes a
    // ±16-pixel hole. It used to zero the recombined colour alone, which was
    // sufficient while the threshold was built from that colour. It no longer is:
    // peak now comes from sp.direct and sp.ambient, and `c * w` is NaN even with c
    // zeroed if either term is left non-finite.
    check(has(exq, "c = vec3(0.0); sp.direct = vec3(0.0); sp.ambient = vec3(0.0);"),
          "the non-finite guard zeroes both recombination terms, not just their sum");

    // ---- 3. The sky never GENERATES bloom on this chain (INV-9). ----
    // mesh.frag writes the sky as outDirect = vec4(skyOut, 100000.0); ssao.frag reads
    // the same far depth at >= 50000.0 to skip it. Before this guard INV-9 held only
    // because a sky texel's DIRECT could not reach a ramp start of 1.15 — which a
    // per-chain scale on that term reaches at 1.5, so the invariant rode on whatever
    // chainScale happened to be. Generation only: INV-9 keeps the COMBINE free of any
    // sky test, so a lamp beside a sky edge still bleeds its halo onto the backdrop.
    check(has(exq, "sp.viewZ >= 50000.0"),
          "the raster extract tests the sky's far depth before generating");
    check(has(exq, "sp.viewZ >= 50000.0) w = 0.0;"),
          "a sky texel contributes exactly zero weight to the extract");

    // ---- 4. The AMBIENT ceiling's sector half is not a stale copy (§10 Q5). ----
    // RunGiBake prints the map's AMBIENT bound as kAmbientSectorMax + max giIrradiance,
    // and INV-4's floor is read against that number. The first half MIRRORS mesh.frag's
    // BASE_SECTOR_DIM, because C++ cannot read a GLSL constant. Q2's breach was a factor
    // applied in one place invalidating a check that read another, so the mirror gets a
    // check of its own rather than a comment asking the next reader to keep them in step.
    const std::string ms = slurp(DOOM_TESTS_ROOT "/shaders/mesh.frag");
    check(!ms.empty(), "mesh.frag is readable");
    float sectorMax = 0.0f, baseDim = 0.0f;
    const bool haveMirror = scalar(vk, "kAmbientSectorMax", &sectorMax);
    const bool haveBase   = !ms.empty() && scalar(ms, "BASE_SECTOR_DIM", &baseDim);
    check(haveMirror, "kAmbientSectorMax is still a named constant in r_vulkan.cpp");
    check(haveBase,   "BASE_SECTOR_DIM is still a named constant in mesh.frag");
    if (haveMirror && haveBase)
    {
        char what[160];
        std::snprintf(what, sizeof what,
                      "the AMBIENT ceiling's sector half still equals mesh.frag's "
                      "BASE_SECTOR_DIM (%.3f vs %.3f)", (double)sectorMax, (double)baseDim);
        check(sectorMax == baseDim, what);
    }

    return check_summary("bloom_threshold_test");
}
