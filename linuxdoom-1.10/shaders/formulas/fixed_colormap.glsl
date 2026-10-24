// fixed_colormap.glsl — DOOM-0455: invulnerability and the light-amp visor in
// the 3D views. The one copy of both formulas; every site includes this file.
//
// The host hands the mode across as a push constant (rb_view_t.fixedMode, from
// fixed_colormap.h). The codes must equal that header's; shader_mirror_test
// holds the pair. Spec: docs/specs/DOOM-0455-fixed-colormap-3d.md.
#ifndef FIXED_COLORMAP_GLSL
#define FIXED_COLORMAP_GLSL

const uint RB_FIXED_NONE = 0u, RB_FIXED_VISOR = 1u, RB_FIXED_INVERT = 2u;

// Invulnerability: the greyscale negative of a DISPLAY-ENCODED colour.
// Rec.601 weights, measured against COLORMAP row 32 (spec §4.6). Encoded,
// because Classic inverts a gamma-encoded palette entry; inverting linear light
// crushes midtones toward white.
vec3 fixedInvert(vec3 encoded)
{
    return vec3(1.0 - dot(clamp(encoded, 0.0, 1.0), vec3(0.299, 0.587, 0.114)));
}

// Visor: how strongly the camera-attached lamp lights a surface, 0..1.
// Wrapped Lambert on |N.V| (a surface the camera sees faces it, so the
// stored normal's sign carries nothing), times a falloff that
// VISOR_FLATNESS blends away. The values are look calls (spec §10); the
// user chose a lamp that fades with distance (2026-10-24), so the far end
// of a dark room stays dim rather than lit flat.
const vec3  VISOR_TINT     = vec3(1.0);
const float VISOR_WRAP     = 0.5;     // 0 = plain Lambert, 1 = no normal term
const float VISOR_FLATNESS = 0.15;    // 1 = no distance falloff, 0 = full falloff
const float VISOR_REACH    = 1000.0;  // world units; the falloff's zero point
const float VISOR_GAIN_RT  = 1.0;     // traced view only: radiance scale
float visorShade(vec3 N, vec3 toEye, float dist)
{
    float facing = (abs(dot(N, toEye)) + VISOR_WRAP) / (1.0 + VISOR_WRAP);
    float fall   = mix(clamp(1.0 - dist / VISOR_REACH, 0.0, 1.0), 1.0, VISOR_FLATNESS);
    return facing * fall;
}

// Visor: how much of the lamp a surface takes given the light it already has,
// 1 in the dark down to 0 at `full`. The user chose a visor that is weaker in
// bright rooms (2026-10-24), so a lit room keeps its own shading (spec §4.5).
// Each site passes its own measure of "fully lit": the raster views their
// sector-light ceiling, the traced view VISOR_LIT_FULL_RT of illumination.
const float VISOR_LIT_FULL_RT = 1.0;  // traced view: demodulated illum luminance
float visorLitFade(float lit, float full)
{
    return 1.0 - smoothstep(0.0, full, lit);
}

#endif
