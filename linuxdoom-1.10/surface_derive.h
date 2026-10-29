// surface_derive.h — DOOM-0273 §4.1: one wall or flat's surface map (normal,
// height, roughness), derived at load from its own WAD pixels. Header-only, in
// the shape of emissive_derive.h, so tests/surface_derive_test.cpp runs it with
// no GPU. The maps live in memory only: they are derived from id's art, so they
// are never written to disk (INV-8).
//
// The height and normal port scripts/pbr_derive.py's _height_field and
// derive_normal exactly; the roughness ports its _FAMILY table. INV-1 and INV-2
// hold the two in step.
#ifndef SURFACE_DERIVE_H
#define SURFACE_DERIVE_H

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "rb_materials.h"   // RB_FLAG_LIQUID_*

namespace surf {
    // The liquid flats, by NAME. DOOM-0183 INV-2: only NUKAGE1-3 / LAVA1-4 -- water,
    // blood and SLIME* stay out (some SLIME* frames are dry rock). Liquids have their
    // own ripple and sheen, so they get a flat map here; r_vulkan.cpp's per-level flag
    // (FlagLiquidFlats) and forced emission (ForceLiquidEmissive) read this one list.
    struct LiquidFlat { const char* name; unsigned int bit; };
    constexpr LiquidFlat kLiquidFlats[] = {
        { "NUKAGE1", RB_FLAG_LIQUID_NUKAGE }, { "NUKAGE2", RB_FLAG_LIQUID_NUKAGE },
        { "NUKAGE3", RB_FLAG_LIQUID_NUKAGE },
        { "LAVA1", RB_FLAG_LIQUID_LAVA }, { "LAVA2", RB_FLAG_LIQUID_LAVA },
        { "LAVA3", RB_FLAG_LIQUID_LAVA }, { "LAVA4", RB_FLAG_LIQUID_LAVA },
    };

    // derive_normal's `strength`. pbr_derive.py defaults to 2.0; Solid uses 4.0, the
    // user's pick 2026-10-26 from a side-by-side at the E2M1 test scene (spec §10).
    constexpr float kNormalStrength = 4.0f;

    // pbr_derive.py's _FAMILY: a name starting with any prefix in a row gets that
    // row's roughness; the longest matching prefix wins; 0.70 when none matches.
    struct Family { const char* prefixes; float roughness; };
    constexpr Family kFamilies[] = {
        { "METAL TEK SILVER SHAWN SUPPORT", 0.35f },
        { "BROWN BRONZE COMP PIPE",         0.55f },
        { "BRICK STONE ROCK GRAY MARB",     0.85f },
        { "WOOD PANEL DOOR",                0.75f },
        { "FLOOR FLAT CEIL RROCK MFLR",     0.80f },
    };
    constexpr float kDefaultRoughness = 0.70f;

    // Length of pfx when `name` starts with it (case-blind, as WAD lookups are),
    // else 0.
    inline int prefix_len(const char* name, const char* pfx, int n)
    {
        for (int i = 0; i < n; i++)
            if (std::toupper((unsigned char)name[i]) != pfx[i])
                return 0;
        return n;
    }

    inline float family_roughness(const char* name)
    {
        int best = 0;
        float rough = kDefaultRoughness;
        for (const Family& f : kFamilies) {
            for (const char* p = f.prefixes; *p; ) {
                const int n = (int)std::strcspn(p, " ");
                const int got = prefix_len(name, p, n);
                if (got > best) { best = got; rough = f.roughness; }
                p += n + (p[n] == ' ');
            }
        }
        return rough;
    }

    // Case-blind, at most 8 characters: a WAD lump name.
    inline bool same_name(const char* a, const char* b)
    {
        for (int i = 0; i < 8; i++) {
            if (std::toupper((unsigned char)a[i]) != std::toupper((unsigned char)b[i]))
                return false;
            if (!a[i])
                return true;
        }
        return true;
    }

    // The light textures, by exact NAME (DOOM-0527; the user's list 2026-10-29). DOOM
    // paints its lights flat; derived from luma, a light sat raised above its dark
    // surround and slid against it as the player walked. Not a LITE prefix: LITEMET and
    // LITESTON are light-coloured metal and stone, and CRATELIT is plain crates; all
    // three keep their relief. The user picked the unclear ones by eye.
    constexpr const char* kLightTextures[] = {
        "TLITE6_1", "TLITE6_4", "TLITE6_5", "TLITE6_6", "GRNLITE1",
        "LITE2", "LITE3", "LITE4", "LITE5", "LITE96",
        "LITEBLU1", "LITEBLU2", "LITEBLU3", "LITEBLU4", "LITERED",
        "CEIL1_2", "CEIL1_3", "CEIL3_4", "CEIL3_6", "FLAT2", "FLAT17", "FLOOR1_7",
        "BRICKLIT", "TEKLITE", "TEKLITE2",
    };

    // Flat cases (§4.1): liquids, the sky flat, light textures, and anything with a
    // see-through texel, where parallax would slide the holes.
    inline bool wants_flat_map(const bool* transparent, int count, const char* name)
    {
        for (const LiquidFlat& l : kLiquidFlats)
            if (same_name(name, l.name))
                return true;
        for (const char* light : kLightTextures)
            if (same_name(name, light))
                return true;
        if (same_name(name, "F_SKY1"))
            return true;
        if (transparent)
            for (int i = 0; i < count; i++)
                if (transparent[i])
                    return true;
        return false;
    }

    inline uint8_t unorm8(float v)
    {
        v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        return (uint8_t)(v * 255.0f + 0.5f);
    }

    // One texture's composited RGB (w*h*3, from the atlas tile and PLAYPAL), its mask
    // (true where no patch covers the texel; nullptr when none is see-through, as for
    // every flat) and its name. Writes w*h RGBA8 texels to out:
    //   R, G = normal x, y encoded as 0.5 + 0.5 * n   (OpenGL Y+, z rebuilt)
    //   B    = height, 0 = low, 255 = high
    //   A    = roughness, 0..255 for 0..1
    inline void derive(const uint8_t* rgb, const bool* transparent, int w, int h,
                       const char* name, uint8_t* out)
    {
        const int count = w * h;
        const uint8_t rough = unorm8(family_roughness(name));

        // A flat map: normal (0, 0, 1), height 255 -- the surface itself, where
        // neither parallax helper moves the UV.
        if (wants_flat_map(transparent, count, name)) {
            for (int i = 0; i < count; i++) {
                out[i * 4 + 0] = 128;
                out[i * 4 + 1] = 128;
                out[i * 4 + 2] = 255;
                out[i * 4 + 3] = rough;
            }
            return;
        }

        // _height_field: BT.601 luma, brighter = raised. Once per texel: each is
        // read by nine Sobel taps.
        std::vector<float> hf((size_t)count);
        for (int i = 0; i < count; i++) {
            const uint8_t* c = rgb + (size_t)i * 3;
            hf[i] = 0.299f * (c[0] / 255.0f) + 0.587f * (c[1] / 255.0f)
                  + 0.114f * (c[2] / 255.0f);
        }

        for (int y = 0; y < h; y++) {
            // DOOM textures tile, so the Sobel taps wrap.
            const float* up = &hf[(size_t)((y + h - 1) % h) * w];
            const float* mid = &hf[(size_t)y * w];
            const float* dn = &hf[(size_t)((y + 1) % h) * w];
            for (int x = 0; x < w; x++) {
                const int l = (x + w - 1) % w, r = (x + 1) % w;
                const float gx = (up[r] + 2 * mid[r] + dn[r]) - (up[l] + 2 * mid[l] + dn[l]);
                const float gy = (dn[l] + 2 * dn[x] + dn[r]) - (up[l] + 2 * up[x] + up[r]);
                const float nx = -gx * kNormalStrength, ny = -gy * kNormalStrength;
                const float inv = 1.0f / std::sqrt(nx * nx + ny * ny + 1.0f);
                uint8_t* o = out + ((size_t)y * w + x) * 4;
                o[0] = unorm8(nx * inv * 0.5f + 0.5f);
                o[1] = unorm8(ny * inv * 0.5f + 0.5f);
                o[2] = unorm8(mid[x]);
                o[3] = rough;
            }
        }
    }
}

#endif // SURFACE_DERIVE_H
