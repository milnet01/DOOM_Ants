// emissive_derive.h — DOOM-0009 build step 3b (§4.2): per-material emitter
// derivation, factored out of r_vulkan.cpp so it is unit-testable with no GPU /
// Vulkan link (mirrors nee_sampling.h). Pure CPU: palette-index texels + a
// pre-decoded linear-RGB palette in, emitted radiance Le out. Both the back-end
// (r_vulkan.cpp ComputeMaterialEmissive) and tests/emissive_derive_test.cpp use
// this one implementation, so the on-HW behaviour and the test agree by
// construction.
//
// INV-7 backfill: the thresholds below are provisional closed-form tunables
// authored inline (user-approved 2026-06-27); they migrate to a Vestige Formula
// Workbench `shaders/formulas/*.glsl` artifact when 3c's curves are formalised.
#ifndef EMISSIVE_DERIVE_H
#define EMISSIVE_DERIVE_H

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace emis {
    // A palette texel CONTRIBUTES to a tile's emitted colour when its linear VALUE
    // clears this. ~0.5 = brighter than mid-grey: lamp/screen/switch fullbrights and
    // the brightest slime greens pass; ordinary wall/floor art does not. This governs
    // the emitted *colour + intensity*, and is unchanged from the original derivation
    // (so lamps that already read as lights keep their brightness).
    constexpr float kBrightLum = 0.5f;

    // DOOM-0082: a tile QUALIFIES as an NEE emitter when it contains a genuine
    // near-fullbright region (a switch's lit indicator, a lamp core, a screen) — NOT
    // when its area-AVERAGED emitted luminance clears a floor. The old area-average
    // gate had two failure modes the peak gate fixes together:
    //   * a small bright switch indicator on a big dark plate averaged down below the
    //     floor and never cast light (the reported bug — switch lights up but the room
    //     stays dark);
    //   * a uniformly TINTED wall with no fullbright texel averaged UP over the floor
    //     and flooded the room with colour.
    // Gating on "has a near-fullbright peak region" admits the switch and rejects the
    // tinted wall. kEmitterPeakLum is the on-HW tunable (raise toward 1.0 to be
    // stricter about what counts as a light).
    constexpr float kEmitterPeakLum = 0.9f;
    // ...and it must be a real region, not a stray speck: at least this fraction of
    // the tile's texels (with an absolute floor for tiny tiles) must clear the peak.
    constexpr float kEmitterMinBrightFrac  = 0.0015f;   // 0.15% of the tile
    constexpr int   kEmitterMinBrightTexels = 8;        // absolute floor for small tiles

    // Global emissive intensity (radiance) scale. INV-7 tunable. DOOM's emissive
    // textures are small, so at palette brightness (1.0) they subtend too little solid
    // angle to light a room — they need a high radiance to read as lights. 40 makes
    // lamps/screens visibly pool light + cast shadows; the shader's per-sample radiance
    // clamp bounds the near-field. A small bright region (switch) is naturally FAINT
    // here: its Le is the bright texels averaged over the whole tile, so a tiny
    // indicator yields a soft glow rather than a full lamp.
    constexpr float kEmissiveScale = 40.0f;

    // Rec.709 luminance weights (linear RGB).
    constexpr float kLumR = 0.2126f, kLumG = 0.7152f, kLumB = 0.0722f;

    inline float srgb2lin(float c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    inline float luminance(float r, float gg, float b) {
        return kLumR * r + kLumG * gg + kLumB * b;
    }
    // Brightness as VALUE (max channel), not luminance — luminance underweights
    // saturated colours (pure red scores only 0.21), so a red ceiling light would miss
    // the "bright"/emitter thresholds and never glow. Value is hue-agnostic, so a
    // saturated red/blue/green light is rated by its intensity and emits in colour.
    inline float value(float r, float gg, float b) {
        return std::max(r, std::max(gg, b));
    }

    // Derive one tile's emitted radiance Le (linear RGB) from its palette-index texels.
    //   pix     : palette indices, row-major, row stride = `stride`.
    //   ox, oy  : the tile's top-left origin within `pix` (0,0 for a contiguous tile).
    //   w, h    : the tile's dimensions.
    //   palLin  : the 256 palette entries pre-decoded to linear RGB (palLin[i][c]).
    // Writes Le to out[3] and returns true iff the tile qualifies as an NEE emitter;
    // a non-emitter leaves out at {0,0,0} (matching the downstream `sum <= 0` skip).
    inline bool derive_material_le(const uint8_t* pix, int stride, int ox, int oy,
                                   int w, int h, const float palLin[256][3],
                                   float out[3])
    {
        out[0] = out[1] = out[2] = 0.0f;
        const int total = w * h;
        if (total <= 0 || !pix)
            return false;

        double sr = 0.0, sg = 0.0, sb = 0.0;   // sum of bright texels' linear RGB
        int peakCount = 0;                     // texels at/over the near-fullbright peak
        for (int row = 0; row < h; row++)
        {
            const uint8_t* line = pix + (size_t)(oy + row) * stride + ox;
            for (int col = 0; col < w; col++)
            {
                const float* c = palLin[line[col]];
                const float v = value(c[0], c[1], c[2]);
                if (v > kBrightLum) { sr += c[0]; sg += c[1]; sb += c[2]; }
                if (v >= kEmitterPeakLum) peakCount++;
            }
        }

        const int minBright = std::max(kEmitterMinBrightTexels,
                                       (int)(kEmitterMinBrightFrac * (float)total));
        if (peakCount < minBright)
            return false;   // no genuine near-fullbright region — not a light source

        out[0] = (float)(sr / total) * kEmissiveScale;
        out[1] = (float)(sg / total) * kEmissiveScale;
        out[2] = (float)(sb / total) * kEmissiveScale;
        return true;
    }
}

#endif // EMISSIVE_DERIVE_H
