// Khronos PBR Neutral tone mapper — hand-written GLSL (DOOM-0009 build step 4b-iii).
//
// NOT a Vestige Formula Workbench export: the Workbench fits *scalar* closed-form
// curves, but PBR Neutral is a piecewise, cross-channel vec3 operator (it keys
// the compression on the per-pixel max channel and desaturates only the highlight
// toward white). That is the same "copy as hand-written GLSL" category the
// Workbench README carves out for non-scalar routines (e.g. the VNDF basis
// transform). The Workbench still owns the scalar pieces of the tone pipeline —
// the exposure multiplier (exposureEv) and the sRGB display encode (linearToSrgb)
// — which pathtrace.comp pulls from the generated formulas.glsl.
//
// Why PBR Neutral over ACES/Reinhard (spec DOOM-0009 §5): ACES and Reinhard both
// desaturate and hue-shift saturated colours as they brighten, which washes out
// DOOM's vivid palette (the red of a switch, the green of slime). PBR Neutral is
// designed to leave in-gamut colour essentially untouched up to the compression
// knee and only pull true highlights toward white, so the look stays DOOM.
//
// Reference: Khronos PBR Neutral Tone Mapper, KhronosGroup/ToneMapping
// (Apache-2.0). Input is linear HDR radiance; output is linear [0,1] (feed the
// sRGB encode after). Verbatim constants from the reference implementation.

vec3 pbrNeutralToneMapping(vec3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation     = 0.15;

    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) return color;

    float d       = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3(1.0), g);
}
