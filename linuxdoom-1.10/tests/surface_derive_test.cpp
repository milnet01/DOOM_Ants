// surface_derive_test.cpp — DOOM-0273 B1: the load-time surface-map derive.
//
// Locks surf::derive and surf::family_roughness (surface_derive.h) with no WAD or
// GPU:
//
//   INV-1  The height and normal equal scripts/pbr_derive.py's for the same texture,
//          within one step of 8-bit rounding.
//   INV-2  family_roughness matches pbr_derive.py's _FAMILY: longest prefix wins,
//          0.70 when nothing matches, and a prefix matches only at the start.
//   INV-3  Liquids, the sky flat and a texture with a transparent texel get a flat
//          map: every texel normal (0, 0, 1) and height 255.
//
// Build/run: `make test` (from linuxdoom-1.10/).
#include "../surface_derive.h"
#include "check_util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

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

// Generated once by pbr_derive.py, from scripts/:
//   python3 -I -c 'import sys; sys.path.insert(0,"."); import pbr_derive as p
//   rgb=bytearray()
//   for y in range(8):
//     for x in range(8): rgb += bytes([(x*37+y*91)%256,(x*x*13+y*29)%256,(x*y*53+17)%256])
//   hb,_=p.derive_height(rgb,8,8); nb,_=p.derive_normal(rgb,8,8)
//   print(list(hb)); print([nb[i*3] for i in range(64)]); print([nb[i*3+1] for i in range(64)])'
static const int kRefHeight[64] = {
    2, 21, 55, 104, 168, 98, 193, 76, 46, 71, 111, 166, 237, 66, 168, 134,
    90, 121, 91, 123, 49, 112, 69, 191, 58, 95, 118, 185, 88, 157, 120, 143,
    102, 145, 174, 218, 51, 126, 65, 200, 147, 166, 125, 25, 90, 171, 117, 78,
    114, 140, 181, 87, 129, 216, 91, 59, 158, 190, 237, 120, 121, 35, 143, 87 };
static const int kRefNx[64] = {
    155, 70, 66, 20, 192, 127, 78, 251, 194, 69, 30, 10, 213, 172, 33, 239,
    239, 44, 25, 117, 198, 126, 18, 221, 235, 37, 47, 203, 215, 51, 39, 184,
    165, 56, 59, 214, 170, 57, 92, 90, 31, 51, 233, 222, 15, 116, 243, 77,
    9, 43, 211, 201, 9, 142, 245, 58, 57, 65, 163, 226, 172, 104, 181, 220 };
static const int kRefNy[64] = {
    242, 238, 232, 91, 26, 19, 44, 126, 24, 22, 59, 144, 208, 230, 186, 75,
    102, 83, 97, 208, 215, 142, 125, 139, 100, 60, 37, 34, 66, 107, 116, 104,
    27, 34, 182, 214, 217, 106, 178, 167, 185, 121, 177, 197, 88, 12, 127, 231,
    109, 51, 37, 30, 120, 237, 152, 85, 226, 235, 243, 159, 150, 203, 55, 111 };

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

        bool mask[W * H] = {};
        check(!is_flat_map(derive("STARTAN3", mask)),
              "INV-3 control: an all-opaque mask does NOT give a flat map");
        mask[W * 3 + 5] = true;
        check(is_flat_map(derive("STARTAN3", mask)),
              "INV-3: a texture with one transparent texel gets a flat map");
    }

    return check_summary("surface_derive_test");
}
