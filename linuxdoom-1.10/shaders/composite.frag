#version 450
//
// DOOM-0170 L2a — composite/tone-map pass fragment stage. Samples the off-screen HDR
// scene target (16-bit float, kSceneFormat) and writes the tone-mapped result to the
// 8-bit swapchain. This is the seam where the raster path's highlights roll off softly
// instead of clipping to flat white at the store.
//
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

// DOOM-0170 L2b — the scene is now TWO HDR targets: AMBIENT (sector light + baked bounce)
// and DIRECT (flashlight + point lights + sprite/sky colour). The composite recombines them
// as DIRECT + AMBIENT. Keeping them separate is what lets L2b-2 darken only AMBIENT with the
// SSAO factor (DIRECT + AO×AMBIENT) so contact shadows never dim directly-lit surfaces.
layout(set = 0, binding = 0) uniform sampler2D ambientTex;
layout(set = 0, binding = 1) uniform sampler2D directTex;
// DOOM-0170 L2b — the half-res SSAO factor (§4.3). 1 = open, ->0 in corners/contacts. It maps
// [0,1] over the WHOLE frame (unlike the scene targets, which fill only the render-scaled
// corner), so it is sampled at vUV directly, not vUV*uvScale.
layout(set = 0, binding = 2) uniform sampler2D aoTex;

// DOOM-0170 L2a step 2: the world is drawn into the [0,uvScale] corner of a
// full-size scene target (render-scale sub-rectangle). Sample that corner and let
// the linear+clamp sampler upscale it to the full swapchain. uvScale = (1,1) at
// 100% render scale, so the picture is byte-identical to the full-res path.
// DOOM-0170 L2b: aoEnable gates the SSAO multiply (0 -> ambient un-occluded) so the rb_ssao
// toggle needs no separate pipeline; the SSAO pass is simply skipped and AO reads as 1.
layout(push_constant) uniform Push { vec2 uvScale; float aoEnable; float pad; } pc;

// DOOM-0170 L2a step 3: the SAME Khronos PBR-Neutral tone operator the RT denoiser uses
// (svgf_composite.comp), so Solid and Ultra stay tone-matched. It is identity below its
// ~0.76 knee, so DOOM's palette and midtones are untouched — only true highlights (muzzle
// flash, explosions, stacked point lights) pull toward white. Unlike the RT path we do
// NOT sRGB-encode after: the raster scene is already display-referred paletted colour and
// the swapchain is a plain UNORM target, so the tone-mapped value is the final pixel.
#include "formulas/pbr_neutral_tonemap.glsl"

// DOOM-0331 L2 — the recombination (AMBIENT/DIRECT/AO -> the pre-tone-map HDR value) moved
// wholesale into this include, because the bloom bright pass has to threshold exactly the
// value this shader tone-maps. Nothing of it is computed here any more: a second copy of
// the AO blur or the AO_DIRECT_WEIGHT mix is how the two drift apart (spec 5).
#include "formulas/scene_recombine.glsl"

void main()
{
    vec3 hdr = sceneRecombine(ambientTex, directTex, aoTex, vUV, pc.uvScale, pc.aoEnable);
    outColor = vec4(pbrNeutralToneMapping(hdr), 1.0);
}
