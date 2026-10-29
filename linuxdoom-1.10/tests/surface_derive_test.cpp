// surface_derive_test.cpp — DOOM-0273 B1: the load-time surface-map derive.
//
// Locks surf::derive and surf::family_roughness (surface_derive.h) with no WAD or
// GPU:
//
//   INV-1  The height and normal equal scripts/pbr_derive.py's for the same texture,
//          within one step of 8-bit rounding.
//   INV-2  family_roughness matches pbr_derive.py's _FAMILY: longest prefix wins,
//          0.70 when nothing matches, and a prefix matches only at the start.
//   INV-3  Liquids, the sky flat, light textures (by exact name) and a texture with
//          a transparent texel get a flat map: every texel normal (0, 0, 1) and
//          height 255.
//   INV-4  (traced half) In pathtrace.comp's surface-map branch the paletted colour
//          is read at the UV surfParallaxUV returned, not at the raw hit UV.
//   INV-6  Every surf helper call in pathtrace.comp sits where usePBR is 0, so an HD
//          material never gets the derived map on top of its own.
//   INV-4  (raster half) mesh.frag reads the paletted colour at surfOffsetUV's UV.
//   INV-7  In mesh.frag the derived normal (shadeN) is read only by the flashlight's
//          facing term and the point lights' ndl term, and every surf call sits inside
//          #ifndef SINGLE_TARGET, so the weapon overlay build has none.
// INV-4 and INV-6 scrape the shader source by name, never by line position.
//
// Build/run: `make test` (from linuxdoom-1.10/).
#include "../surface_derive.h"
#include "check_util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

#ifndef DOOM_TESTS_ROOT
#error "DOOM_TESTS_ROOT must be defined (the Makefile passes it)"
#endif

static const int W = 8, H = 8;

// The synthetic texture INV-1's reference values were made from. Keep it in step
// with the generator command below.
static std::vector<uint8_t> synthetic_rgb()
{
    std::vector<uint8_t> rgb;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            rgb.push_back((uint8_t)((x * 37 + y * 91) % 256));
            rgb.push_back((uint8_t)((x * x * 13 + y * 29) % 256));
            rgb.push_back((uint8_t)((x * y * 53 + 17) % 256));
        }
    return rgb;
}

// Generated once by pbr_derive.py at surf::kNormalStrength (4.0), from scripts/:
//   python3 -I -c 'import sys; sys.path.insert(0,"."); import pbr_derive as p
//   rgb=bytearray()
//   for y in range(8):
//     for x in range(8): rgb += bytes([(x*37+y*91)%256,(x*x*13+y*29)%256,(x*y*53+17)%256])
//   hb,_=p.derive_height(rgb,8,8); nb,_=p.derive_normal(rgb,8,8,strength=4.0)
//   print(list(hb)); print([nb[i*3] for i in range(64)]); print([nb[i*3+1] for i in range(64)])'
static const int kRefHeight[64] = {
    2, 21, 55, 104, 168, 98, 193, 76, 46, 71, 111, 166, 237, 66, 168, 134,
    90, 121, 91, 123, 49, 112, 69, 191, 58, 95, 118, 185, 88, 157, 120, 143,
    102, 145, 174, 218, 51, 126, 65, 200, 147, 166, 125, 25, 90, 171, 117, 78,
    114, 140, 181, 87, 129, 216, 91, 59, 158, 190, 237, 120, 121, 35, 143, 87 };
static const int kRefNx[64] = {
    156, 69, 64, 11, 195, 127, 68, 254, 196, 66, 25, 4, 218, 177, 23, 242,
    248, 25, 11, 114, 205, 125, 5, 243, 246, 29, 44, 206, 226, 24, 15, 215,
    170, 52, 39, 216, 178, 28, 73, 67, 22, 22, 240, 228, 9, 115, 252, 73,
    4, 36, 213, 204, 3, 143, 250, 34, 54, 64, 165, 242, 201, 96, 195, 242 };
static const int kRefNy[64] = {
    249, 240, 236, 88, 22, 6, 27, 126, 21, 18, 56, 145, 213, 240, 193, 74,
    100, 73, 93, 235, 224, 157, 125, 142, 97, 54, 34, 30, 57, 99, 112, 92,
    14, 28, 198, 217, 234, 97, 204, 191, 191, 118, 180, 201, 86, 4, 127, 239,
    108, 44, 35, 27, 119, 249, 153, 70, 230, 237, 248, 164, 164, 231, 36, 107 };

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

// The source with every // comment blanked out (same length, so offsets hold).
static std::string strip_comments(std::string s)
{
    for (size_t p = s.find("//"); p != std::string::npos; p = s.find("//", p))
        while (p < s.size() && s[p] != '\n') s[p++] = ' ';
    return s;
}

// The text of the block that opens at the first '{' at or after `from`, braces included.
static std::string block_at(const std::string& s, size_t from)
{
    const size_t open = s.find('{', from);
    if (open == std::string::npos) return std::string();
    int depth = 0;
    for (size_t p = open; p < s.size(); ++p) {
        if (s[p] == '{') depth++;
        else if (s[p] == '}' && --depth == 0) return s.substr(open, p - open + 1);
    }
    return std::string();
}

// The condition guarding offset `at`: the text from the statement start before the
// '{' that encloses `at`, up to that '{'. Also the call's own line, for a call that
// IS the condition (`else if (... && surfHas(id)) {`).
static std::string guard_of(const std::string& s, size_t at)
{
    int depth = 0;
    size_t p = at;
    while (p > 0) {
        --p;
        if (s[p] == '}') depth++;
        else if (s[p] == '{' && depth-- == 0) break;
    }
    size_t start = s.find_last_of(";}", p - 1);
    start = (start == std::string::npos) ? 0 : start + 1;
    const size_t ls = s.rfind('\n', at), le = s.find('\n', at);
    return s.substr(start, p - start) + " | " + s.substr(ls + 1, le - ls - 1);
}

static bool is_flat_map(const std::vector<uint8_t>& out)
{
    for (size_t i = 0; i < out.size(); i += 4)
        if (out[i] != 128 || out[i + 1] != 128 || out[i + 2] != 255)
            return false;
    return true;
}

static std::vector<uint8_t> derive(const char* name, const bool* mask)
{
    const std::vector<uint8_t> rgb = synthetic_rgb();
    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    surf::derive(rgb.data(), mask, W, H, name, out.data());
    return out;
}

int main()
{
    std::printf("surface_derive_test (DOOM-0273): load-time surface maps\n");

    // --- INV-1: height and normal match pbr_derive.py ----------------------------
    {
        const std::vector<uint8_t> out = derive("STARTAN3", nullptr);
        int worst = 0;
        for (int i = 0; i < W * H; i++) {
            worst = std::max(worst, std::abs(out[i * 4 + 0] - kRefNx[i]));
            worst = std::max(worst, std::abs(out[i * 4 + 1] - kRefNy[i]));
            worst = std::max(worst, std::abs(out[i * 4 + 2] - kRefHeight[i]));
        }
        std::printf("  [INV-1] worst channel difference from pbr_derive.py: %d\n", worst);
        check(worst <= 1, "INV-1: normal x, y and height equal pbr_derive.py's within one 8-bit step");
        check(out[3] == 179, "INV-1: alpha carries STARTAN3's roughness, 0.70 -> 179");
    }

    // --- INV-2: family roughness -------------------------------------------------
    {
        struct { const char* name; float want; } cases[] = {
            { "STONE2", 0.85f }, { "FLOOR5_2", 0.80f }, { "METAL1", 0.35f },
            { "STARTAN3", 0.70f }, { "XMETAL", 0.70f },
        };
        for (const auto& c : cases) {
            const float got = surf::family_roughness(c.name);
            std::printf("  [INV-2] %-8s %.2f\n", c.name, got);
            char what[96];
            std::snprintf(what, sizeof what, "INV-2: %s gets roughness %.2f", c.name, c.want);
            check(std::fabs(got - c.want) < 1e-6f, what);
        }
    }

    // --- INV-3: flat maps --------------------------------------------------------
    {
        check(!is_flat_map(derive("STARTAN3", nullptr)),
              "INV-3 control: an opaque wall with relief does NOT get a flat map");
        check(is_flat_map(derive("NUKAGE1", nullptr)), "INV-3: NUKAGE1 (a liquid) gets a flat map");
        check(is_flat_map(derive("F_SKY1", nullptr)), "INV-3: the sky flat F_SKY1 gets a flat map");
        // DOOM-0527: light textures are flat, by exact name; a LITE prefix is not enough.
        for (const char* n : { "TLITE6_5", "LITE3", "FLAT2", "FLOOR1_7", "CEIL3_6", "TEKLITE" }) {
            char what[80];
            std::snprintf(what, sizeof what, "INV-3: the light %s gets a flat map", n);
            check(is_flat_map(derive(n, nullptr)), what);
        }
        for (const char* n : { "LITEMET", "LITESTON", "CRATELIT" }) {
            char what[80];
            std::snprintf(what, sizeof what, "INV-3 control: %s (not a light) keeps relief", n);
            check(!is_flat_map(derive(n, nullptr)), what);
        }

        bool mask[W * H] = {};
        check(!is_flat_map(derive("STARTAN3", mask)),
              "INV-3 control: an all-opaque mask does NOT give a flat map");
        mask[W * 3 + 5] = true;
        check(is_flat_map(derive("STARTAN3", mask)),
              "INV-3: a texture with one transparent texel gets a flat map");
    }

    // --- INV-4 / INV-6: pathtrace.comp, scraped ----------------------------------
    {
        const std::string pt = strip_comments(slurp(DOOM_TESTS_ROOT "/shaders/pathtrace.comp"));
        check(!pt.empty(), "pathtrace.comp is readable");

        // INV-4: the albedo in the surf branch of primaryHitSurface is read at h.sUV,
        // and h.sUV there comes from surfParallaxUV.
        const size_t fn = pt.find("PrimaryHit primaryHitSurface(");
        const std::string body = block_at(pt, fn);
        check(fn != std::string::npos && !body.empty(), "INV-4: primaryHitSurface is found");
        check(body.find("h.sUV = surfParallaxUV(") != std::string::npos,
              "INV-4: primaryHitSurface sets h.sUV from surfParallaxUV");
        const size_t br = body.find("else if (h.mc.usePBR == 0u && h.surf)");
        const std::string albedo = br == std::string::npos ? std::string() : block_at(body, br);
        check(albedo.find("decodeAlbedo(h.id, h.sUV") != std::string::npos,
              "INV-4: the surf branch reads the paletted colour at the marched h.sUV");
        check(albedo.find("hitUV") == std::string::npos,
              "INV-4: the surf branch's colour read does not use the raw hitUV");

        // INV-6: every surf helper call is guarded by usePBR == 0u.
        int calls = 0, unguarded = 0;
        for (size_t p = pt.find("surf"); p != std::string::npos; p = pt.find("surf", p + 1)) {
            if (p > 0 && (std::isalnum((unsigned char)pt[p - 1]) || pt[p - 1] == '_')) continue;
            size_t q = p + 4;
            if (q >= pt.size() || !std::isupper((unsigned char)pt[q])) continue;
            while (q < pt.size() && (std::isalnum((unsigned char)pt[q]) || pt[q] == '_')) ++q;
            if (q >= pt.size() || pt[q] != '(') continue;
            calls++;
            if (guard_of(pt, p).find("usePBR == 0u") == std::string::npos) {
                unguarded++;
                std::printf("  [INV-6] unguarded: %s\n", pt.substr(p, q - p).c_str());
            }
        }
        std::printf("  [INV-6] %d surf helper call(s), %d unguarded\n", calls, unguarded);
        check(calls > 0, "INV-6: pathtrace.comp calls the surf helpers at all");
        check(unguarded == 0, "INV-6: every surf helper call sits where usePBR is 0");
    }

    // --- INV-4 (raster) / INV-7: mesh.frag, scraped ---------------------------------
    {
        const std::string mf = strip_comments(slurp(DOOM_TESTS_ROOT "/shaders/mesh.frag"));
        check(!mf.empty(), "mesh.frag is readable");

        check(mf.find("uv01 = surfOffsetUV(") != std::string::npos,
              "INV-4: mesh.frag sets uv01 from surfOffsetUV");
        check(mf.find("float index   = texture(materialTex[nonuniformEXT(id)], uv01)")
                  != std::string::npos,
              "INV-4: mesh.frag's paletted colour read uses uv01");

        // The raster offset must not divide by viewTS.z: at grazing views that grew the
        // shift to tiles and the texture slid as the player walked (DOOM-0527).
        const std::string sd =
            strip_comments(slurp(DOOM_TESTS_ROOT "/shaders/formulas/surface_detail.glsl"));
        const size_t fo = sd.find("vec2 surfOffsetUV(");
        const size_t fe = fo == std::string::npos ? fo : sd.find("\n}", fo);
        check(fe != std::string::npos, "surface_detail.glsl defines surfOffsetUV");
        if (fe != std::string::npos)
            check(sd.substr(fo, fe - fo).find("viewTS.z") == std::string::npos,
                  "DOOM-0527: surfOffsetUV's shift is bounded (no divide by viewTS.z)");
        // The traced march had the same runaway: maxOff divided by the clamped view z.
        const size_t po = sd.find("vec2 surfParallaxUV(");
        const size_t pe = po == std::string::npos ? po : sd.find("\n}", po);
        check(pe != std::string::npos, "surface_detail.glsl defines surfParallaxUV");
        if (pe != std::string::npos)
            check(sd.substr(po, pe - po).find("viewTS.xy / nv") == std::string::npos,
                  "DOOM-0527: surfParallaxUV's march is bounded (no divide by nv)");

        // Walk the lines, tracking whether SINGLE_TARGET is known to be undefined.
        std::vector<char> stack;               // 'N' #ifndef ST, 'D' #ifdef ST, 'O' other
        int surfCalls = 0, outside = 0, shadeUses = 0, strayUses = 0;
        size_t ls = 0;
        while (ls < mf.size()) {
            size_t le = mf.find('\n', ls);
            if (le == std::string::npos) le = mf.size();
            const std::string line = mf.substr(ls, le - ls);
            ls = le + 1;
            const size_t h = line.find_first_not_of(" \t");
            if (h != std::string::npos && line[h] == '#') {
                if (line.find("#ifndef SINGLE_TARGET") != std::string::npos) stack.push_back('N');
                else if (line.find("#ifdef SINGLE_TARGET") != std::string::npos) stack.push_back('D');
                else if (line.compare(h, 3, "#if") == 0) stack.push_back('O');
                else if (line.compare(h, 5, "#else") == 0 && !stack.empty())
                    stack.back() = stack.back() == 'N' ? 'D' : stack.back() == 'D' ? 'N' : 'O';
                else if (line.compare(h, 6, "#endif") == 0 && !stack.empty()) stack.pop_back();
                if (line.find("surface_detail.glsl") == std::string::npos) continue;
            }
            bool inN = false;
            for (char c : stack) inN = inN || c == 'N';
            for (size_t p = line.find("surf"); p != std::string::npos; p = line.find("surf", p + 1)) {
                const size_t q = p + 4;
                const bool call = q < line.size() && std::isupper((unsigned char)line[q])
                               && line.find('(', q) != std::string::npos;
                const bool inc = line.find("surface_detail.glsl") != std::string::npos;
                if (!call && !inc) continue;
                surfCalls++;
                if (!inN) { outside++; std::printf("  [INV-7] outside #ifndef SINGLE_TARGET: %s\n", line.c_str()); }
                break;
            }
            if (line.find("shadeN") != std::string::npos) {
                const bool def = line.find("shadeN  =") != std::string::npos
                              || line.find("vec3  shadeN") != std::string::npos;
                if (def) continue;
                shadeUses++;
                if (line.find("facing") == std::string::npos && line.find("ndl") == std::string::npos) {
                    strayUses++;
                    std::printf("  [INV-7] shadeN read outside facing/ndl: %s\n", line.c_str());
                }
            }
        }
        std::printf("  [INV-7] %d surf line(s), %d outside; shadeN read %d time(s), %d stray\n",
                    surfCalls, outside, shadeUses, strayUses);
        check(surfCalls > 0 && outside == 0, "INV-7: every surf call sits inside #ifndef SINGLE_TARGET");
        check(shadeUses == 2 && strayUses == 0,
              "INV-7: the derived normal is read only by the facing and ndl terms");
    }

    return check_summary("surface_derive_test");
}
