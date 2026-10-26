// shown_emission.glsl — DOOM-0511: the self-emission SHOWN on a primary hit, as
// distinct from the Le the same surface casts on the room. One copy, included by the
// raw display path (pt_common.glsl's shadeSurface) and the denoised one
// (svgf_composite.comp), so the two views agree.
//
// The cast light is untouched: BuildStaticEmitterSet reads g.matEmissive unmodulated.
// What changes is only what the eye sees on the emitter itself. A wall light's derived
// Le is its bright texels x kEmissiveScale (40), which for REDWALL1 is about
// (19.9, 0.02, 0.02) linear: right for the red light it throws, but PBR Neutral blends a
// highlight that bright toward white, so the panel showed white with a red rim.
#ifndef SHOWN_EMISSION_GLSL
#define SHOWN_EMISSION_GLSL

// Above this peak, a surface shows its own albedo at this brightness instead of its Le,
// which keeps both the hue and the texture. Tuned at E2M1's cross against the user's
// reference, rt_brightness 13 (x1.40): 0.6 shows the panels at a mean of (188,8,8)
// against the reference's (211,1,1). Higher washes toward pink: 1.0 gave (205,32,32).
const float kShownGlowMax = 0.6;

// emisWeight is the signed weight from emisWeight() in pathtrace.comp: its magnitude
// scales the glow, and a negative sign marks a liquid. A liquid always shows its forced
// Le flat, because DOOM-0302 found that any glow tracking a pool's mottled texture
// makes it glow in patches. Below the cap Le is shown as before.
vec3 shownEmission(vec3 Le, vec3 albedo, float emisWeight)
{
    float w = abs(emisWeight);
    if (emisWeight < 0.0 || max(Le.r, max(Le.g, Le.b)) <= kShownGlowMax)
        return Le * w;
    return albedo * (kShownGlowMax * w);
}

#endif // SHOWN_EMISSION_GLSL
