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

// DOOM-0170 L2a step 2: the world is drawn into the [0,uvScale] corner of a
// full-size scene target (render-scale sub-rectangle). Sample that corner and let
// the linear+clamp sampler upscale it to the full swapchain. uvScale = (1,1) at
// 100% render scale, so the picture is byte-identical to the full-res path.
layout(push_constant) uniform Push { vec2 uvScale; } pc;

// DOOM-0170 L2a step 3: the SAME Khronos PBR-Neutral tone operator the RT denoiser uses
// (svgf_composite.comp), so Solid and Ultra stay tone-matched. It is identity below its
// ~0.76 knee, so DOOM's palette and midtones are untouched — only true highlights (muzzle
// flash, explosions, stacked point lights) pull toward white. Unlike the RT path we do
// NOT sRGB-encode after: the raster scene is already display-referred paletted colour and
// the swapchain is a plain UNORM target, so the tone-mapped value is the final pixel.
#include "formulas/pbr_neutral_tonemap.glsl"

void main()
{
    vec2  uv      = vUV * pc.uvScale;
    vec3  ambient = texture(ambientTex, uv).rgb;
    vec3  direct  = texture(directTex,  uv).rgb;
    // L2b-2 will fold in the SSAO factor here (direct + ao*ambient); for now a plain sum,
    // so the recombined image is identical to the pre-split single target.
    vec3  hdr     = direct + ambient;
    outColor = vec4(pbrNeutralToneMapping(hdr), 1.0);
}
