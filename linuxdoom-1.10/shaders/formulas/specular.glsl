// specular.glsl — DOOM-0103: the specular lobe, one copy for every caller. The spec is
// docs/specs/DOOM-0103-specular-lobe.md (§4.1).
//
// The curves are the Workbench's (formulas.glsl: ggxDistributionParam,
// smithGgxVisibility, fresnelSchlickF0); this file only combines them and holds the
// look constants. tests/specular_test.cpp compiles this file as C++ and checks it
// against the published GGX, Smith and Schlick expressions.
#ifndef SPECULAR_GLSL
#define SPECULAR_GLSL

#include "formulas.glsl"

// Dielectric reflectance at normal incidence. Every surface takes it; metal is DOOM-0517.
const float kSpecF0 = 0.04;
// Roughness floor: no tight, sparkly lobes on normals derived from luma, and a lobe wide
// enough for six samples and a short history to resolve (§4.1).
const float kSpecRoughMin = 0.35;
// MatCtrl.flags' liquid bits: RB_FLAG_LIQUID_NUKAGE | RB_FLAG_LIQUID_LAVA (rb_materials.h).
// A liquid keeps DOOM-0183's sheen instead of this lobe. tests/shader_mirror_test.cpp
// checks the value. Pass MatCtrl.flags, never the per-vertex matFlags, which carries no
// liquid bit (pathtrace.comp's FogHit note).
const uint kSpecLiquidFlags = 24u;

// False when the setting is 0 or the material is a liquid. Every caller of specBrdf
// checks it first.
bool specOn(uint setting, uint ctrlFlags)
{
    return setting != 0u && (ctrlFlags & kSpecLiquidFlags) == 0u;
}

// Specular radiance factor for one light direction: D * V * F. Multiply by the light's
// radiance and NdotL. n, v and l are unit vectors; v points to the eye, l to the light.
// roughness is perceptual roughness; alpha = max(roughness, kSpecRoughMin)^2.
// Returns 0 when v is zero (the bake) or either direction is below the surface.
vec3 specBrdf(vec3 n, vec3 v, vec3 l, float roughness)
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
    return vec3(ggxDistributionParam(NdotH, alpha)
              * smithGgxVisibility(NdotV, NdotL, alpha)
              * fresnelSchlickF0(VdotH, kSpecF0));
}

#endif // SPECULAR_GLSL
