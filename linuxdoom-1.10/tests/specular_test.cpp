// specular_test.cpp — DOOM-0103: the specular lobe (docs/specs/DOOM-0103-specular-lobe.md).
//
//   INV-1  specBrdf follows GGX with the given roughness (floored at kSpecRoughMin), and
//          is 0 when the view direction is zero or a direction is below the surface.
//          specOn is false when the setting is off or the material is a liquid, and
//          every specBrdf call in pt_common.glsl and pathtrace.comp sits behind specOn.
//   INV-2  The bake passes a zero view direction to shadeSurface and calls no specBrdf.
//   INV-3  gspec is stored with no albedo or division; the composite adds hspec without
//          albedo; svgf_atrous.comp touches neither.
//   INV-6  In mesh.frag the highlight goes into direct, never ambient, its exponent comes
//          from specExponent, and both blocks holding it skip the weapon.
//   INV-7  specBrdf and specExponent are defined only in shaders/formulas/specular.glsl.
//
// INV-1 does not keep a C++ copy of specBrdf. It compiles the shader's own
// specular.glsl, and the Workbench's formulas.glsl it includes, as C++ through the
// small GLSL shim below, so an edit to the shader is what this test runs. The reference
// it is checked against is GGX and height-correlated Smith in their tangent (Lambda)
// forms, as Walter et al. 2007 and Heitz 2014 publish them, not the cosine forms the
// Workbench exports, so a wrong rearrangement cannot agree with itself.
//
// The liquid bits are checked against rb_materials.h here rather than in
// shader_mirror_test, because the GLSL constant is compiled in and needs no scrape.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD, no GPU, no display.
#include "../rb_materials.h"
#include "check_util.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#ifndef DOOM_TESTS_ROOT
#error "DOOM_TESTS_ROOT must be defined (the Makefile passes it)"
#endif

// --- The GLSL shim: just enough of GLSL for formulas.glsl and specular.glsl. ---
namespace glsl {
typedef unsigned int uint;
struct vec3 {
    float x, y, z;
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
};
static vec3 operator+(vec3 a, vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
static vec3 operator-(float s, vec3 a) { return vec3(s - a.x, s - a.y, s - a.z); }
static vec3 operator*(vec3 a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
static vec3 operator*(float s, vec3 a) { return a * s; }
static vec3 mix(vec3 a, vec3 b, float t)
{
    return vec3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
static float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static vec3 normalize(vec3 a) { return a * (1.0f / std::sqrt(dot(a, a))); }
static float max(float a, float b) { return a > b ? a : b; }
static float min(float a, float b) { return a < b ? a : b; }
static float clamp(float x, float lo, float hi) { return min(max(x, lo), hi); }
[[maybe_unused]] static float mix(float a, float b, float t) { return a + (b - a) * t; }
[[maybe_unused]] static float smoothstep(float e0, float e1, float x)
{
    float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static float pow(float b, float e) { return std::pow(b, e); }
static float sqrt(float x) { return std::sqrt(x); }
static float abs(float x) { return std::fabs(x); }
static float floor(float x) { return std::floor(x); }
static float log(float x) { return std::log(x); }
static float exp(float x) { return std::exp(x); }
static float sin(float x) { return std::sin(x); }
static float cos(float x) { return std::cos(x); }
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "../shaders/formulas/specular.glsl"
#pragma GCC diagnostic pop
} // namespace glsl

// --- The published reference, in double. ---
struct D3 { double x, y, z; };
static double dot3(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static D3 unit(D3 a) { double s = 1.0 / std::sqrt(dot3(a, a)); return {a.x * s, a.y * s, a.z * s}; }
static double tan2(double c) { return (1.0 - c * c) / (c * c); }

// GGX NDF, Walter et al. 2007 eq. 33: 1 / (pi a^2 cos^4 (1 + tan^2 / a^2)^2), a = rough^2.
static double refD(double NdotH, double a)
{
    double c4 = NdotH * NdotH * NdotH * NdotH;
    double q  = 1.0 + tan2(NdotH) / (a * a);
    return 1.0 / (M_PI * a * a * c4 * q * q);
}
// Smith GGX Lambda, Heitz 2014 eq. 72: (-1 + sqrt(1 + a^2 tan^2)) / 2.
static double lambda(double c, double a) { return 0.5 * (-1.0 + std::sqrt(1.0 + a * a * tan2(c))); }
// Height-correlated G2 (Heitz eq. 99) over the 4 NdotV NdotL denominator.
static double refV(double NdotV, double NdotL, double a)
{
    double g2 = 1.0 / (1.0 + lambda(NdotV, a) + lambda(NdotL, a));
    return g2 / (4.0 * NdotV * NdotL);
}
// Schlick 1994.
static double refF(double VdotH, double f0) { return f0 + (1.0 - f0) * std::pow(1.0 - VdotH, 5.0); }

static double refSpec(D3 n, D3 v, D3 l, double rough, double f0 = 0.04)
{
    double NdotV = dot3(n, v), NdotL = dot3(n, l);
    if (NdotV <= 0.0 || NdotL <= 0.0) return 0.0;
    D3 h = unit({v.x + l.x, v.y + l.y, v.z + l.z});
    double r = rough > 0.35 ? rough : 0.35;   // the spec's floor, written out on purpose
    double a = r * r;
    return refD(dot3(n, h), a) * refV(NdotV, NdotL, a) * refF(dot3(v, h), f0);
}

// The shader's lobe; f0 defaults to the dielectric 0.04 written out, not kSpecF0.
static glsl::vec3 shader3(D3 n, D3 v, D3 l, float rough, glsl::vec3 f0)
{
    return glsl::specBrdf(glsl::vec3(float(n.x), float(n.y), float(n.z)),
                          glsl::vec3(float(v.x), float(v.y), float(v.z)),
                          glsl::vec3(float(l.x), float(l.y), float(l.z)), rough, f0);
}
static float shader(D3 n, D3 v, D3 l, float rough)
{
    return shader3(n, v, l, rough, glsl::vec3(0.04f)).x;
}

static std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// GLSL source with // and /* */ comments blanked, so a ; or brace in a comment cannot
// move a statement boundary and a name in a comment is not a use.
static std::string code(const std::string& src)
{
    std::string out = src;
    for (size_t i = 0; i + 1 < out.size(); ++i) {
        if (out[i] == '/' && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') out[i++] = ' ';
        } else if (out[i] == '/' && out[i + 1] == '*') {
            size_t e = out.find("*/", i + 2);
            e = (e == std::string::npos) ? out.size() : e + 2;
            for (; i < e; ++i) if (out[i] != '\n') out[i] = ' ';
        }
    }
    return out;
}

// The top-level, whitespace-trimmed arguments of the call whose '(' is at open.
static std::vector<std::string> callArgs(const std::string& src, size_t open)
{
    std::vector<std::string> args;
    std::string cur;
    int depth = 0;
    for (size_t i = open + 1; i < src.size(); ++i) {
        char c = src[i];
        if (c == '(') ++depth;
        if (c == ')' && depth-- == 0) { args.push_back(cur); break; }
        if (c == ',' && depth == 0) { args.push_back(cur); cur.clear(); continue; }
        if (!std::isspace((unsigned char)c)) cur += c;
    }
    return args;
}

// Whether the code at `at` sits behind `guard`: the statement holding it, or the header
// of a block enclosing it, contains that text.
static bool guardedBy(const std::string& src, size_t at, const std::string& guard)
{
    // The statement: back to the previous ; { or }.
    size_t s = src.find_last_of(";{}", at - 1);
    if (src.substr(s + 1, at - s - 1).find(guard) != std::string::npos) return true;
    // Each enclosing block's header: walk back over balanced braces.
    int depth = 0;
    for (size_t i = at; i-- > 0;) {
        if (src[i] == '}') ++depth;
        else if (src[i] == '{') {
            if (depth > 0) { --depth; continue; }
            size_t h = src.find_last_of(";{}", i - 1);
            h = (h == std::string::npos) ? 0 : h + 1;
            if (src.substr(h, i - h).find(guard) != std::string::npos) return true;
        }
    }
    return false;
}

// INV-1's guard rule: every specBrdf( call sits behind specOn(. Returns the number of
// unguarded calls; *calls counts every call (definitions excluded).
static int unguardedCalls(const std::string& src, int* calls)
{
    int bad = 0;
    *calls = 0;
    const std::string tok = "specBrdf(";
    for (size_t at = src.find(tok); at != std::string::npos; at = src.find(tok, at + 1)) {
        if (at > 0 && (std::isalnum((unsigned char)src[at - 1]) || src[at - 1] == '_')) continue;
        if (at >= 5 && src.compare(at - 5, 5, "vec3 ") == 0) continue;   // the definition
        ++*calls;
        if (!guardedBy(src, at, "specOn(")) ++bad;
    }
    return bad;
}

int main()
{
    std::printf("specular_test\n");
    char what[200];

    // INV-1: the shader's lobe against the published model.
    const D3 n = {0, 0, 1};
    const D3 dirs[][2] = {
        {{0, 0, 1}, {0, 0, 1}},                         // head-on
        {{0.6, 0, 0.8}, {-0.6, 0, 0.8}},                // mirror pair
        {{0.6, 0, 0.8}, {0.3, 0.4, 0.866}},             // off the mirror
        {{0.9, 0.1, 0.42}, {-0.2, -0.7, 0.68}},         // grazing view
        {{0.1, 0.2, 0.97}, {-0.95, 0.05, 0.3}},         // grazing light
    };
    const float roughs[] = {0.0f, 0.2f, 0.35f, 0.5f, 0.8f, 1.0f};
    double worst = 0.0;
    for (const auto& d : dirs) {
        D3 v = unit(d[0]), l = unit(d[1]);
        for (float r : roughs) {
            double want = refSpec(n, v, l, r);
            double got  = shader(n, v, l, r);
            double err  = std::fabs(got - want) / std::fmax(want, 1e-6);
            if (err > worst) worst = err;
        }
    }
    std::printf("  [INV-1] worst relative error vs Walter/Heitz/Schlick: %.2e\n", worst);
    check(worst < 1e-4, "specBrdf matches published GGX x height-correlated Smith x Schlick");

    const D3 v = unit({0.6, 0, 0.8}), l = unit({-0.3, 0.2, 0.9});
    check(shader(n, v, l, 0.5f) != shader(n, v, l, 0.8f), "specBrdf reads its roughness");
    check(shader(n, v, l, 0.1f) == shader(n, v, l, glsl::kSpecRoughMin),
          "roughness below kSpecRoughMin is floored to it");
    check(shader(n, {0, 0, 0}, l, 0.5f) == 0.0f, "a zero view direction (the bake) returns 0");
    check(shader(n, unit({0.6, 0, -0.8}), l, 0.5f) == 0.0f, "a view below the surface returns 0");
    check(shader(n, v, unit({0.3, 0, -0.95}), 0.5f) == 0.0f, "a light below the surface returns 0");

    // DOOM-0517: specF0 is 0.04 for a dielectric and the albedo for a metal, and the lobe
    // reflects each channel with its own f0.
    const glsl::vec3 grey(0.55f, 0.6f, 0.65f);
    glsl::vec3 d0 = glsl::specF0(grey, 0.0f), m1 = glsl::specF0(grey, 1.0f), mh = glsl::specF0(grey, 0.5f);
    check(d0.x == 0.04f && d0.y == 0.04f && d0.z == 0.04f, "specF0 at metal 0 is 0.04 in every channel");
    check(m1.x == grey.x && m1.y == grey.y && m1.z == grey.z, "specF0 at metal 1 is the albedo");
    check(std::fabs(mh.y - 0.32f) < 1e-6f, "specF0 at metal 0.5 is halfway (0.04 to 0.6 gives 0.32)");
    glsl::vec3 lobe = shader3(n, v, l, 0.9f, m1);
    double we[3] = {grey.x, grey.y, grey.z}, gotc[3] = {lobe.x, lobe.y, lobe.z}, mworst = 0.0;
    for (int c = 0; c < 3; ++c) {
        double want = refSpec(n, v, l, 0.9, we[c]);
        mworst = std::fmax(mworst, std::fabs(gotc[c] - want) / want);
    }
    std::printf("  [0517] metal lobe worst relative error per channel: %.2e\n", mworst);
    check(mworst < 1e-4, "a metal's lobe matches Schlick with its albedo as f0, channel by channel");

    // §4.3: specExponent is 2/alpha^2 - 2 with specBrdf's alpha, floored at 1, so it narrows
    // as the surface gets smoother.
    const double a35 = 0.35 * 0.35;
    check(std::fabs(glsl::specExponent(0.35f) - (2.0 / (a35 * a35) - 2.0)) < 1e-2,
          "specExponent at the roughness floor is 2/alpha^2 - 2");
    check(glsl::specExponent(0.1f) == glsl::specExponent(glsl::kSpecRoughMin),
          "specExponent floors roughness like specBrdf");
    check(glsl::specExponent(1.0f) == 1.0f, "specExponent is floored at 1 for the roughest surface");
    check(glsl::specExponent(0.5f) > glsl::specExponent(0.8f), "a smoother surface gets a tighter highlight");

    check(!glsl::specOn(0u, 0u), "specOn is false with the setting off");
    check(glsl::specOn(1u, 0u), "specOn is true with the setting on and no flags");
    check(glsl::specOn(1u, RB_FLAG_SPRITE | RB_FLAG_NO_DETILE), "specOn ignores flags that mark no liquid");
    check(!glsl::specOn(1u, RB_FLAG_LIQUID_NUKAGE), "specOn is false on nukage");
    check(!glsl::specOn(1u, RB_FLAG_LIQUID_LAVA), "specOn is false on lava");
    check(glsl::kSpecLiquidFlags == (RB_FLAG_LIQUID_NUKAGE | RB_FLAG_LIQUID_LAVA),
          "kSpecLiquidFlags equals rb_materials.h's two liquid bits");

    // INV-1: the guard rule fires on a bad call, so a pass below means something.
    int calls = 0;
    check(unguardedCalls("void f(){ s += specBrdf(n, v, l, r); }", &calls) == 1,
          "the guard scrape flags an unguarded call");
    check(unguardedCalls("void f(){ if (specOn(a, b)) { x = 1; s += specBrdf(n, v, l, r); } }", &calls) == 0,
          "the guard scrape accepts a call inside a specOn block");
    check(unguardedCalls("void f(){ if (specOn(a, b)) s += specBrdf(n, v, l, r); }", &calls) == 0,
          "the guard scrape accepts a call in a specOn statement");
    check(unguardedCalls("void f(){ if (specOn(a, b)) { } s += specBrdf(n, v, l, r); }", &calls) == 1,
          "the guard scrape flags a call after a closed specOn block");

    for (const char* rel : {"/shaders/pt_common.glsl", "/shaders/pathtrace.comp"}) {
        std::string src = code(slurp(std::string(DOOM_TESTS_ROOT) + rel));
        std::snprintf(what, sizeof what, "%s is readable", rel);
        check(!src.empty(), what);
        int bad = unguardedCalls(src, &calls);
        std::printf("  [INV-1] %s: %d specBrdf call(s), %d unguarded\n", rel, calls, bad);
        // B3 put calls in both files; none found means the scrape is looking at nothing.
        std::snprintf(what, sizeof what, "%s calls specBrdf, so the guard scrape has work", rel);
        check(calls > 0, what);
        std::snprintf(what, sizeof what, "every specBrdf call in %s sits behind specOn", rel);
        check(bad == 0, what);
    }

    // INV-2: the bake passes a zero view direction to every shadeSurface call and never
    // calls specBrdf itself. Where v sits is read from shadeSurface's own definition.
    {
        const std::string ptc = code(slurp(std::string(DOOM_TESTS_ROOT) + "/shaders/pt_common.glsl"));
        const std::string sig = "vec3 shadeSurface(";
        size_t d = ptc.find(sig);
        check(d != std::string::npos, "pt_common.glsl defines shadeSurface");
        std::vector<std::string> params;
        if (d != std::string::npos) params = callArgs(ptc, d + sig.size() - 1);
        size_t vAt = params.size();
        for (size_t i = 0; i < params.size(); ++i) if (params[i] == "vec3v") vAt = i;
        check(vAt < params.size(), "shadeSurface takes a view direction `vec3 v`");

        const std::string bake = code(slurp(std::string(DOOM_TESTS_ROOT) + "/shaders/bake.comp"));
        check(!bake.empty(), "bake.comp is readable");
        int n = 0, bad = 0;
        for (size_t at = bake.find("shadeSurface("); at != std::string::npos;
             at = bake.find("shadeSurface(", at + 1)) {
            auto a = callArgs(bake, at + std::string("shadeSurface").size());
            ++n;
            if (a.size() != params.size() || vAt >= a.size() || a[vAt] != "vec3(0.0)") ++bad;
        }
        std::printf("  [INV-2] bake.comp: %d shadeSurface call(s), %d without a zero view\n", n, bad);
        check(n > 0, "bake.comp calls shadeSurface");
        check(bad == 0, "every bake shadeSurface call passes vec3(0.0) as the view direction");
        check(bake.find("specBrdf(") == std::string::npos, "bake.comp never calls specBrdf");
    }

    // INV-3: gspec is stored undivided, the composite adds hspec without albedo, and the
    // a-trous passes never touch either.
    {
        const std::string pt = code(slurp(std::string(DOOM_TESTS_ROOT) + "/shaders/pathtrace.comp"));
        size_t at = pt.find("imageStore(gspec");
        check(at != std::string::npos, "pathtrace.comp stores gspec");
        std::string store = (at == std::string::npos) ? "" : pt.substr(at, pt.find(';', at) - at);
        check(store.find("albedo") == std::string::npos && store.find('/') == std::string::npos,
              "the gspec store holds no albedo and no division");

        const std::string cs = code(slurp(std::string(DOOM_TESTS_ROOT) + "/shaders/svgf_composite.comp"));
        size_t ld = cs.find("imageLoad(hspec");
        check(ld != std::string::npos, "svgf_composite.comp reads hspec");
        size_t s0 = (ld == std::string::npos) ? 0 : cs.find_last_of(";{}", ld) + 1;
        std::string load = (ld == std::string::npos) ? "" : cs.substr(s0, cs.find(';', ld) - s0);
        check(load.find("albedo") == std::string::npos, "the hspec read is not multiplied by albedo");
        const std::regex mul("albedo\\s*\\*\\s*spec\\b|\\bspec\\s*\\*\\s*albedo");
        check(!std::regex_search(cs, mul), "the composite never multiplies spec by albedo");

        const std::string atrous = code(slurp(std::string(DOOM_TESTS_ROOT) + "/shaders/svgf_atrous.comp"));
        check(!atrous.empty(), "svgf_atrous.comp is readable");
        check(atrous.find("gspec") == std::string::npos && atrous.find("hspec") == std::string::npos,
              "the a-trous passes bind neither gspec nor hspec");
    }

    // INV-6: in mesh.frag the highlight goes into direct, never ambient, takes its exponent
    // from specExponent, and each block holding it skips the weapon (FLAG_PSPRITE).
    {
        const std::string mf = code(slurp(std::string(DOOM_TESTS_ROOT) + "/shaders/mesh.frag"));
        check(!mf.empty(), "mesh.frag is readable");
        int adds = 0, badAdds = 0, terms = 0, unskipped = 0, ambientMix = 0;
        std::istringstream lines(mf);
        for (std::string line; std::getline(lines, line);) {
            size_t f = line.find_first_not_of(" \t");
            std::string t = (f == std::string::npos) ? "" : line.substr(f);
            if (t.find("kRasterSpecStrength *") != std::string::npos) {
                ++adds;
                if (t.rfind("direct +=", 0) != 0) ++badAdds;
            }
            if (t.find("ambient") != std::string::npos &&
                (t.find("spec") != std::string::npos || t.find("ndh") != std::string::npos))
                ++ambientMix;
        }
        for (size_t at = mf.find("pow(ndh, specN)"); at != std::string::npos;
             at = mf.find("pow(ndh, specN)", at + 1)) {
            ++terms;
            if (!guardedBy(mf, at, "FLAG_PSPRITE")) ++unskipped;
        }
        std::printf("  [INV-6] mesh.frag: %d highlight term(s), %d add(s) to direct, %d elsewhere\n",
                    terms, adds, badAdds);
        check(terms == 2 && adds == 2 && badAdds == 0,
              "the flashlight's and the point lights' highlights are each added to direct");
        check(ambientMix == 0, "no ambient line touches the highlight");
        check(mf.find("specN    = specExponent(specR)") != std::string::npos,
              "the highlight's exponent comes from specExponent on the surface roughness");
        check(unskipped == 0, "both blocks holding the highlight skip the weapon (FLAG_PSPRITE)");
    }

    // INV-7: one definition, in specular.glsl.
    const std::regex def("^(vec3 specBrdf|float specExponent)\\(");
    std::vector<std::string> homes;
    for (const auto& e : std::filesystem::recursive_directory_iterator(
             std::string(DOOM_TESTS_ROOT) + "/shaders")) {
        if (!e.is_regular_file()) continue;
        std::ifstream f(e.path());
        for (std::string line; std::getline(f, line);)
            if (std::regex_search(line, def)) { homes.push_back(e.path().filename().string()); break; }
    }
    std::printf("  [INV-7] files defining specBrdf/specExponent: %zu\n", homes.size());
    check(homes.size() == 1 && homes[0] == "specular.glsl",
          "specBrdf and specExponent are defined only in formulas/specular.glsl");

    return check_summary("specular_test");
}
