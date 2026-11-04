// specular.glsl — DOOM-0103: the specular lobe, one copy for every caller. The spec is
// docs/specs/DOOM-0103-specular-lobe.md (§4.1).
//
// The curves are the Workbench's (formulas.glsl: ggxDistributionParam,
// smithGgxVisibility, fresnelSchlickMetallic); this file only combines them and holds the
// look constants. tests/specular_test.cpp compiles this file as C++ and checks it
// against the published GGX, Smith and Schlick expressions.
#ifndef SPECULAR_GLSL
#define SPECULAR_GLSL

#include "formulas.glsl"

// Dielectric reflectance at normal incidence (specF0 at metal 0).
const float kSpecF0 = 0.04;
// Roughness floor: no tight, sparkly lobes on normals derived from luma, and a lobe wide
// enough for six samples and a short history to resolve (§4.1).
const float kSpecRoughMin = 0.35;
// MatCtrl.flags' liquid bits: RB_FLAG_LIQUID_NUKAGE | RB_FLAG_LIQUID_LAVA (rb_materials.h).
// A liquid keeps DOOM-0183's sheen instead of this lobe. tests/specular_test.cpp
// checks the value. Pass MatCtrl.flags, never the per-vertex matFlags, which carries no
// liquid bit (pathtrace.comp's FogHit note).
const uint kSpecLiquidFlags = 24u;

// False when the setting is 0 or the material is a liquid. Every caller of specBrdf
// checks it first.
bool specOn(uint setting, uint ctrlFlags)
{
    return setting != 0u && (ctrlFlags & kSpecLiquidFlags) == 0u;
}

// DOOM-0517: reflectance at normal incidence for the metallic workflow. A dielectric
// (metal 0) takes kSpecF0; a metal (metal 1) reflects in its own colour.
vec3 specF0(vec3 albedo, float metal)
{
    return mix(vec3(kSpecF0), albedo, metal);
}

// DOOM-0103 §4.2: one sprite light's resampling target in pt_common.glsl's RIS loop, from
// its clamped matte part refl and the stand-in for its highlight sguess (zero when specOn
// is false, so the pick is the matte one).
float risTarget(vec3 refl, vec3 sguess)
{
    vec3 s = refl + sguess;
    return max(s.x, max(s.y, s.z));
}

// DOOM-0103 §4.3: the rasterised view's Blinn-Phong exponent for a roughness, the usual
// match to GGX's width (2 / alpha^2 - 2, alpha as specBrdf's). Floored at 1, as 0 would
// light every angle equally and pow(0, 0) is undefined.
float specExponent(float roughness)
{
    float r = max(roughness, kSpecRoughMin);
    float a = r * r;
    return max(2.0 / (a * a) - 2.0, 1.0);
}

// Specular radiance factor for one light direction: D * V * F. Multiply by the light's
// radiance and NdotL. n, v and l are unit vectors; v points to the eye, l to the light.
// roughness is perceptual roughness; alpha = max(roughness, kSpecRoughMin)^2. f0 is
// specF0's reflectance at normal incidence.
// Returns 0 when v is zero (the bake) or either direction is below the surface.
vec3 specBrdf(vec3 n, vec3 v, vec3 l, float roughness, vec3 f0)
{
    float NdotV = dot(n, v);
    float NdotL = dot(n, l);
    if (NdotV <= 0.0 || NdotL <= 0.0)
        return vec3(0.0);
    vec3  h     = normalize(v + l);
    float NdotH = max(dot(n, h), 0.0);
    float VdotH = max(dot(v, h), 0.0);
    float r     = max(roughness, kSpecRoughMin);
    float alpha = r * r;
    return ggxDistributionParam(NdotH, alpha)
         * smithGgxVisibility(NdotV, NdotL, alpha)
         * fresnelSchlickMetallic(VdotH, f0);
}

#endif // SPECULAR_GLSL
