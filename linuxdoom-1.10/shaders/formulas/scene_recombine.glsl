// DOOM-0331 L2 (spec 5, "New shaders") — the raster chain's HDR recombination, in ONE
// place. This is the value composite.frag tone-maps, and it is also the value the bloom
// bright pass thresholds; if the two ever re-derive it separately, the thing that blooms
// is not the thing on screen and INV-2/INV-4 both come apart.
//
// It owns the WHOLE recombination, not just the final multiply: the aoEnable gate, the
// 4-tap bilinear box blur of the half-res AO, the AO_DIRECT_WEIGHT mix, and the rule that
// AO is sampled at plain frame UV while the scene targets are sampled at uv*uvScale (the
// world fills only the [0,uvScale] corner of a full-size target).
//
// The sample coordinate is a PARAMETER because the two consumers do not share one:
// composite.frag has a vUV varying, bloom_extract_raster.comp is compute and derives its
// own from gl_GlobalInvocationID.
//
// textureLod(..., 0.0), never texture(): one body has to compile in both a fragment and a
// COMPUTE stage, and compute has no derivatives, so implicit-LOD sampling is invalid
// there. Behaviourally identical here - all three targets are single-mip.
#ifndef SCENE_RECOMBINE_GLSL
#define SCENE_RECOMBINE_GLSL

// DOOM-0170 L2b — AO darkens AMBIENT fully; on DOOM's emitter-heavy floors the sector
// light is diluted by point-light DIRECT, so pure ambient-only AO is nearly invisible
// there. Apply a FRACTION of the occlusion to DIRECT too, so contact shadows read on lit
// floors - but keep it partial so the flashlight/lamp beams and the weapon/sprites (which
// are pure DIRECT and sit at AO~1 in their interiors) stay bright.
const float AO_DIRECT_WEIGHT = 0.5;

vec3 sceneRecombine(sampler2D amb, sampler2D dir, sampler2D ao,
                    vec2 uv, vec2 uvScale, float aoEnable)
{
    vec2  suv     = uv * uvScale;
    vec3  ambient = textureLod(amb, suv, 0.0).rgb;
    vec3  direct  = textureLod(dir, suv, 0.0).rgb;

    // A 4-tap bilinear box blur of the half-res AO removes the SSAO dither cheaply (no
    // separate blur pass). Skipped entirely when the rb_ssao toggle is off (aoEnable = 0
    // -> a = 1), which is also the case where the AO image holds its parked contents.
    float a = 1.0;
    if (aoEnable > 0.5)
    {
        vec2 t = 1.0 / vec2(textureSize(ao, 0));
        a = 0.25 * (textureLod(ao, uv + vec2(-0.5, -0.5) * t, 0.0).r
                  + textureLod(ao, uv + vec2( 0.5, -0.5) * t, 0.0).r
                  + textureLod(ao, uv + vec2(-0.5,  0.5) * t, 0.0).r
                  + textureLod(ao, uv + vec2( 0.5,  0.5) * t, 0.0).r);
    }
    float aoDirect = mix(1.0, a, AO_DIRECT_WEIGHT);
    return direct * aoDirect + ambient * a;
}
#endif
