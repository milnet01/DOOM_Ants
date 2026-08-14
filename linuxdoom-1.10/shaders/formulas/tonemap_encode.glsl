// DOOM-0345 R1 (spec 4.2) — the ray-traced chain's tone-map operator, in ONE place.
//
// This is svgf_composite.comp's old local toneEncode() with the exposure EV passed in
// instead of read from that shader's own push block. That parameterisation is the whole
// point: the function used to read pc.misc3.x, which is why a second shader could not
// call it, and lifting it whole is what DOOM-0345 3 decision 5 makes possible (the
// exposure now lands one pass LATER, downstream of the HDR store, so nothing has to be
// carved out of the operator itself).
//
// Two consumers: svgf_composite.comp (sky branch, either variant; surface store on the
// un-split variant) and rt_tonemap.comp (surface store on the split variant). Exactly one
// of them applies it to any given pixel -- INV-6.
//
// It lives in its own file rather than in pbr_neutral_tonemap.glsl because it calls
// exposureEv and linearToSrgb, which live in formulas.glsl: composite.frag includes only
// the tonemap header, so defining this there would give the raster composite a body
// calling undeclared functions -- a compile error whether or not it is ever called.
//
#ifndef TONEMAP_ENCODE_GLSL
#define TONEMAP_ENCODE_GLSL

// Neither prerequisite carries an include guard of its own, so a consumer must include
// THIS file alone and not also the two below it.
#include "formulas.glsl"
#include "pbr_neutral_tonemap.glsl"

// Linear scene radiance -> exposed -> PBR-Neutral compressed -> sRGB display encode.
// The max() travels with the operator because the compression assumes non-negative input.
vec3 toneExposeEncode(vec3 L, float ev)
{
    L = max(L, vec3(0.0)) * exposureEv(ev);
    L = pbrNeutralToneMapping(L);
    return vec3(linearToSrgb(L.r), linearToSrgb(L.g), linearToSrgb(L.b));
}

#endif
