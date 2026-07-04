// emissive_derive_test.cpp — DOOM-0082 / DOOM-0009 build step 3b.
//
// Proves the per-material emitter derivation (emis::derive_material_le, the exact
// logic r_vulkan.cpp's ComputeMaterialEmissive runs at level load) has the
// STRUCTURAL properties the "switch/button glow" feature needs — without a GPU or a
// running game:
//
//   1. A switch: a small near-fullbright indicator on a big dark plate QUALIFIES as
//      an emitter, in the indicator's colour, with a FAINT radiance. (The reported
//      bug: the old area-average gate drowned the small bright region and the switch
//      cast no light.)
//   2. A uniformly TINTED wall (coloured, but no fullbright texel) is REJECTED, so it
//      cannot flood the room with colour. (The other half of the same root cause.)
//   3. A lamp (large fullbright region) QUALIFIES strongly — regression guard that the
//      new peak gate did not dim the lights that already worked.
//   4. A fully dark tile, and a lone bright speck below the region floor, are REJECTED.
//   5. DOOM-0157: with allowFaint (a sprite_glows collectible), a few-texel glow that
//      the strict gate rejects self-illuminates FAINTLY in its own colour — but a
//      genuinely dark tile still stays dark.
//
// Intensity/threshold *tuning* (how faint is "faint", the exact peak level) is an
// on-HW dial; this test locks the STRUCTURE (who qualifies, and in what colour) that
// tuning must not break.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD or GPU needed.
#include "../emissive_derive.h"

#include <cstdio>
#include <cstdint>
#include <vector>

static int g_failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); g_failures++; }
}

// A 256-entry linear-RGB palette with the handful of indices the tiles below use.
// (derive_material_le takes an already-decoded palette, so no sRGB step here.)
enum { PAL_BLACK = 0, PAL_RED = 1, PAL_GREEN = 2, PAL_WHITE = 3, PAL_MIDRED = 4 };
static void build_palette(float pal[256][3])
{
    for (int i = 0; i < 256; i++) { pal[i][0] = pal[i][1] = pal[i][2] = 0.0f; }
    pal[PAL_RED][0]   = 1.0f;                                   // fullbright red  (value 1.0)
    pal[PAL_GREEN][1] = 1.0f;                                   // fullbright green
    pal[PAL_WHITE][0] = pal[PAL_WHITE][1] = pal[PAL_WHITE][2] = 1.0f; // fullbright white
    pal[PAL_MIDRED][0] = 0.6f;                                  // mid red (value 0.6): bright-ish, NOT fullbright
}

// Build a contiguous w*h tile filled with `bg`, then paint a `count`-texel block of
// `fg` into the top-left. Returns the palette-index buffer.
static std::vector<uint8_t> make_tile(int w, int h, uint8_t bg, uint8_t fg, int count)
{
    std::vector<uint8_t> t((size_t)w * h, bg);
    for (int i = 0; i < count && i < w * h; i++) t[i] = fg;
    return t;
}

int main()
{
    std::printf("emissive_derive_test (DOOM-0082): peak-region emitter gate\n");

    float pal[256][3];
    build_palette(pal);

    // --- 1. Switch: 64x128 dark plate with a 40-texel fullbright-red indicator ------
    {
        const int w = 64, h = 128;
        auto tile = make_tile(w, h, PAL_BLACK, PAL_RED, 40);
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le);
        check(emitter, "switch: small fullbright indicator QUALIFIES as an emitter");
        check(le[0] > 0.0f && le[1] == 0.0f && le[2] == 0.0f,
              "switch: emitted colour is red (hue-correct, no green/blue leak)");
        // Faint: the indicator is 40/8192 of the plate, so Le is a soft glow, well
        // below a full lamp's radiance (checked against the lamp case below).
        check(le[0] < 1.0f, "switch: red radiance is faint (a soft glow, not a lamp)");
        std::printf("  [switch]  Le=(%.3f, %.3f, %.3f)\n", le[0], le[1], le[2]);
    }

    // --- 2. Tinted wall: 64x64 uniformly mid-red, NO fullbright texel ----------------
    {
        const int w = 64, h = 64;
        std::vector<uint8_t> tile((size_t)w * h, PAL_MIDRED);   // every texel mid-red
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le);
        check(!emitter, "tinted wall: no near-fullbright region -> REJECTED (no flood)");
        check(le[0] == 0.0f && le[1] == 0.0f && le[2] == 0.0f,
              "tinted wall: Le left at zero");
        std::printf("  [wall]    emitter=%d Le=(%.3f, %.3f, %.3f)\n",
                    (int)emitter, le[0], le[1], le[2]);
    }

    // --- 3. Lamp: 64x64 fully fullbright-white --------------------------------------
    float lampR = 0.0f;
    {
        const int w = 64, h = 64;
        std::vector<uint8_t> tile((size_t)w * h, PAL_WHITE);
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le);
        check(emitter, "lamp: large fullbright region QUALIFIES");
        check(le[0] > 1.0f && le[1] > 1.0f && le[2] > 1.0f,
              "lamp: emitted radiance is strong (regression guard: not dimmed)");
        lampR = le[0];
        std::printf("  [lamp]    Le=(%.3f, %.3f, %.3f)\n", le[0], le[1], le[2]);
    }

    // The switch must be strictly fainter than the lamp — the "faint glow" property.
    {
        const int w = 64, h = 128;
        auto tile = make_tile(w, h, PAL_BLACK, PAL_RED, 40);
        float le[3] = { 0, 0, 0 };
        emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le);
        check(le[0] < lampR, "switch glow is fainter than a full lamp");
    }

    // --- 4a. Fully dark tile is not a light -----------------------------------------
    {
        const int w = 32, h = 32;
        std::vector<uint8_t> tile((size_t)w * h, PAL_BLACK);
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le);
        check(!emitter, "dark tile: REJECTED");
    }

    // --- 4b. A lone speck (below the region floor) is not a light -------------------
    {
        const int w = 64, h = 64;
        auto tile = make_tile(w, h, PAL_BLACK, PAL_RED, 3);   // 3 < kEmitterMinBrightTexels(8)
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le);
        check(!emitter, "lone speck (< min bright texels): REJECTED, not an emitter");
    }

    // --- DOOM-0157: glowing collectible (skull eyes) with allowFaint --------------
    // A key skull's green eyes are a few bright texels on an otherwise dark sprite —
    // below the peak gate, so a plain derive yields Le=0 and it stays dark. With
    // allowFaint set (the sprite_glows allow-list), the same speck must self-illuminate
    // faintly, in its own colour, so it reads in a pitch-black room.
    {
        const int w = 40, h = 48;
        auto tile = make_tile(w, h, PAL_BLACK, PAL_GREEN, 4);   // 4 bright green "eye" texels
        float strict[3] = { -1, -1, -1 };
        bool e0 = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, strict);
        check(!e0 && strict[1] == 0.0f,
              "skull eyes (strict gate): REJECTED, Le=0 (would stay dark)");

        float faint[3] = { -1, -1, -1 };
        bool e1 = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, faint,
                                           /*allowFaint=*/true);
        check(e1, "skull eyes (allowFaint): QUALIFIES so the eyes self-illuminate");
        check(faint[1] > 0.0f && faint[0] == 0.0f && faint[2] == 0.0f,
              "skull eyes (allowFaint): glow is green (hue-correct)");
        check(faint[1] < lampR, "skull eyes (allowFaint): glow is faint, not a lamp");
        std::printf("  [skull]   Le=(%.3f, %.3f, %.3f)\n", faint[0], faint[1], faint[2]);
    }

    // allowFaint must NOT rescue a genuinely dark tile — no bright speck, no glow.
    {
        const int w = 40, h = 48;
        std::vector<uint8_t> tile((size_t)w * h, PAL_BLACK);
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le,
                                                /*allowFaint=*/true);
        check(!emitter && le[1] == 0.0f,
              "allowFaint + fully dark: still REJECTED (nothing bright to emit)");
    }

    // --- Green switch: colour derivation is hue-agnostic -----------------------------
    {
        const int w = 64, h = 128;
        auto tile = make_tile(w, h, PAL_BLACK, PAL_GREEN, 40);
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(tile.data(), w, 0, 0, w, h, pal, le);
        check(emitter && le[1] > 0.0f && le[0] == 0.0f && le[2] == 0.0f,
              "green switch: emits green (a green button glows green)");
    }

    // --- Sub-rect: derivation respects stride/origin (atlas tile is a sub-rect) ------
    {
        // A 128-wide atlas holding a 16x16 fullbright-white tile at origin (32,8);
        // everything else black. Deriving the sub-rect must see only the tile.
        const int stride = 128, ox = 32, oy = 8, tw = 16, th = 16;
        std::vector<uint8_t> atlas((size_t)stride * 64, PAL_BLACK);
        for (int r = 0; r < th; r++)
            for (int c = 0; c < tw; c++)
                atlas[(size_t)(oy + r) * stride + ox + c] = PAL_WHITE;
        float le[3] = { -1, -1, -1 };
        bool emitter = emis::derive_material_le(atlas.data(), stride, ox, oy, tw, th, pal, le);
        check(emitter && le[0] > 1.0f, "sub-rect tile: stride/origin honoured, tile qualifies");
    }

    if (g_failures == 0)
        std::printf("PASS: all checks green — peak-region emitter gate is structurally correct.\n");
    else
        std::printf("FAIL: %d check(s) failed.\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
