// DOOM-0440 — the baked GI probe cache, read side, in ONE place.
//
// Hand-written, not exported from the Vestige Formula Workbench (which has no SH model).
//
// Two consumers read the same baked probes: the path tracer (through pt_common.glsl, so
// pathtrace.comp and the bake) and the raster tier (mesh.frag). mesh.frag used to carry a
// verbatim copy of giIrradiance, and if either copy were re-derived, Solid and Ultra would
// shade the same probes differently -- a tier mismatch no test compares. It lives here, not
// in pt_common.glsl, because pt_common carries the path tracer's whole header contract,
// which a raster fragment shader cannot satisfy.
//
// The includer must enable GL_EXT_buffer_reference and GL_EXT_scalar_block_layout first.
//
#ifndef GI_PROBE_GLSL
#define GI_PROBE_GLSL

// ProbesRO is the baked per-subsector SH-L1 (16 floats/probe: pos[3] pad, then
// channel-major radiance SH R[4] G[4] B[4]); TriSs maps a hit triangle's primitive index
// -> its subsector (== probe) index.
layout(buffer_reference, scalar) readonly buffer ProbesRO { float p[]; };
layout(buffer_reference, scalar) readonly buffer TriSs    { uint  s[]; };

// Evaluate the baked SH-L1 GI cache for subsector `subId` along normal `n`, and return the
// diffuse reflected-radiance factor (multiply by surface albedo). The probe stores RADIANCE
// SH coefficients; convolving with the clamped-cosine kernel gives irradiance
// E(n) = A0*c0*Y0 + A1*(c1.Y1(n)), A0=PI, A1=2PI/3, and the Lambert BRDF divides by PI --
// so the PIs fold to weight 1 on the DC term and 2/3 on the linear terms. Basis order
// matches the bake's projection (coeff 1<-n.y, 2<-n.z, 3<-n.x). SH-L1 can ring slightly
// negative, so clamp to >= 0.
vec3 giIrradiance(ProbesRO pr, uint subId, vec3 n)
{
    uint  b  = subId * 16u + 4u;             // SH coeffs start at float 4
    float y0 = 0.282095;
    float y1 = 0.488603 * n.y;
    float y2 = 0.488603 * n.z;
    float y3 = 0.488603 * n.x;
    const float k = 2.0 / 3.0;
    vec3 gi;
    gi.r = pr.p[b+0u]*y0 + k*(pr.p[b+1u]*y1 + pr.p[b+2u]*y2 + pr.p[b+3u]*y3);
    gi.g = pr.p[b+4u]*y0 + k*(pr.p[b+5u]*y1 + pr.p[b+6u]*y2 + pr.p[b+7u]*y3);
    gi.b = pr.p[b+8u]*y0 + k*(pr.p[b+9u]*y1 + pr.p[b+10u]*y2 + pr.p[b+11u]*y3);
    return max(gi, vec3(0.0));
}

#endif // GI_PROBE_GLSL
