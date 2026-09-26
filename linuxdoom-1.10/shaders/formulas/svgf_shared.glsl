// DOOM-0440 — the helpers the SVGF denoiser passes share, in ONE place.
//
// Hand-written, not exported from the Vestige Formula Workbench.
//
// svgfReprojectPrev was written out in svgf_temporal.comp (history lookup) and
// svgf_composite.comp (motion vectors for the upscaler). A sign flip in either copy shows
// only as ghosting IN MOTION, which static golden frames cannot catch, so a second copy
// is where that defect would have hidden. lum was defined byte-identically in
// svgf_temporal.comp and svgf_atrous.comp.
//
#ifndef SVGF_SHARED_GLSL
#define SVGF_SHARED_GLSL

// Rec.709 luminance of linear RGB.
float lum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Project world point `wpos` into the PREVIOUS frame's camera: the exact inverse of
// pathtrace.comp's pixel-centre primary-ray construction. prevRight.w and prevUp.w carry
// tan(hFov/2) and tan(vFov/2); screen-y is down, so world up maps to -ndcY.
// Returns false when the point is not in front of the previous camera; otherwise writes
// the depth along the previous view axis and prevScreen, the continuous screen position in
// pixels with pixel CENTRES at +0.5. So floor(prevScreen) is the pixel it lands in, and
// prevScreen - 0.5 is the coordinate with centres at integers.
//
// It returns the +0.5 form on purpose. The copies this replaced computed centre-at-integer
// fx, then floor(fx + 0.5); inlined, the optimiser cancelled the pair and ran floor(s).
// Across a function boundary it does not, and (s - 0.5) + 0.5 can differ from s in the
// last bit, which moves floor() at an exact pixel edge. Returning s keeps both callers
// executing exactly the arithmetic they did before.
bool svgfReprojectPrev(vec3 wpos, vec4 prevPos, vec4 prevDir, vec4 prevRight, vec4 prevUp,
                       int w, int h, out vec2 prevScreen, out float vz)
{
    vec3 rel = wpos - prevPos.xyz;
    vz = dot(rel, prevDir.xyz);
    if (!(vz > 1e-3))
        return false;
    float ndcX =  (dot(rel, prevRight.xyz) / vz) / prevRight.w;
    float ndcY = -(dot(rel, prevUp.xyz)    / vz) / prevUp.w;
    prevScreen = vec2((ndcX + 1.0) * 0.5 * float(w),
                      (ndcY + 1.0) * 0.5 * float(h));
    return true;
}

#endif // SVGF_SHARED_GLSL
