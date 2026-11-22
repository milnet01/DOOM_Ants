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

// ProbesRO is the baked probe lattice (DOOM-0652 B3), laid out as light_grid.h says:
// header (origin xyz, cell, dz, nx, ny, nz), index (record number or -1 per lattice point),
// then 24-float records: pos[3], valid, channel-major radiance SH R[4] G[4] B[4], reach
// +x -x +y -y +z -z, pad. TriSs maps a hit triangle's primitive index to its subsector.
layout(buffer_reference, scalar) readonly buffer ProbesRO { float p[]; };
layout(buffer_reference, scalar) readonly buffer TriSs    { uint  s[]; };

// MUST equal light_grid.h's PROBE_HDR, PROBE_REC, RB_PROBE_NBIAS and RB_PROBE_SLACK
// (shader_mirror_test.cpp).
const uint  GI_PROBE_HDR   = 8u;
const uint  GI_PROBE_REC   = 24u;
const float GI_PROBE_NBIAS = 8.0;
const float GI_PROBE_SLACK = 8.0;

// The diffuse reflected-radiance factor (multiply by surface albedo) of the radiance SH
// sh (R[4] G[4] B[4]) along normal n. Convolving with the clamped-cosine kernel gives
// irradiance E(n) = A0*c0*Y0 + A1*(c1.Y1(n)), A0=PI, A1=2PI/3, and the Lambert BRDF
// divides by PI -- so the PIs fold to weight 1 on the DC term and 2/3 on the linear terms.
// Basis order matches the bake's projection (coeff 1<-n.y, 2<-n.z, 3<-n.x). SH-L1 can
// ring slightly negative, so clamp to >= 0.
vec3 giEvalSH(vec4 r, vec4 g, vec4 b, vec3 n)
{
    vec4 Y = vec4(0.282095, (2.0 / 3.0) * 0.488603 * vec3(n.y, n.z, n.x));
    return max(vec3(dot(r, Y), dot(g, Y), dot(b, Y)), vec3(0.0));
}

// The bounce light at world point pos on a surface facing n: the SH of the 8 lattice
// corners around pos + n * GI_PROBE_NBIAS, blended trilinearly and evaluated once (the
// evaluation is linear, so this equals blending the corners' light). A corner with no
// valid probe is dropped, and so is one whose reach falls short of the point; the weight
// left is renormalised. When every present corner falls short they blend unmasked, and
// with none present the answer is zero. light_grid.h's ProbeBlendSH is the C copy the
// tests read; change both together.
vec3 giIrradianceAt(ProbesRO pr, vec3 pos, vec3 n)
{
    vec3  x    = pos + n * GI_PROBE_NBIAS;
    vec3  o    = vec3(pr.p[0], pr.p[1], pr.p[2]);
    vec3  sp   = vec3(pr.p[3], pr.p[3], pr.p[4]);
    ivec3 dim  = ivec3(int(pr.p[5]), int(pr.p[6]), int(pr.p[7]));
    uint  rec0 = GI_PROBE_HDR + uint(dim.x * dim.y * dim.z);
    vec3  c    = (x - o) / sp;
    vec3  bf   = floor(c);
    vec3  f    = c - bf;
    ivec3 bi   = ivec3(bf);

    vec4 sR = vec4(0.0), sG = vec4(0.0), sB = vec4(0.0);
    vec4 aR = vec4(0.0), aG = vec4(0.0), aB = vec4(0.0);
    float wS = 0.0, wA = 0.0;
    for (int k = 0; k < 8; k++)
    {
        ivec3 off = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        ivec3 q   = bi + off;
        if (any(lessThan(q, ivec3(0))) || any(greaterThanEqual(q, dim)))
            continue;
        float idx = pr.p[GI_PROBE_HDR + uint((q.z * dim.y + q.y) * dim.x + q.x)];
        if (idx < 0.0)
            continue;
        uint b = rec0 + uint(idx) * GI_PROBE_REC;
        if (pr.p[b + 3u] == 0.0)
            continue;
        vec3  share = mix(1.0 - f, f, vec3(off));
        float w     = share.x * share.y * share.z;
        vec3  d     = x - vec3(pr.p[b], pr.p[b + 1u], pr.p[b + 2u]);
        vec3  reach = vec3(d.x >= 0.0 ? pr.p[b + 16u] : pr.p[b + 17u],
                           d.y >= 0.0 ? pr.p[b + 18u] : pr.p[b + 19u],
                           d.z >= 0.0 ? pr.p[b + 20u] : pr.p[b + 21u]);
        vec4 r = vec4(pr.p[b +  4u], pr.p[b +  5u], pr.p[b +  6u], pr.p[b +  7u]);
        vec4 g = vec4(pr.p[b +  8u], pr.p[b +  9u], pr.p[b + 10u], pr.p[b + 11u]);
        vec4 bl= vec4(pr.p[b + 12u], pr.p[b + 13u], pr.p[b + 14u], pr.p[b + 15u]);
        aR += w * r; aG += w * g; aB += w * bl; wA += w;
        if (any(greaterThan(abs(d), reach + GI_PROBE_SLACK)))
            continue;
        sR += w * r; sG += w * g; sB += w * bl; wS += w;
    }
    if (wS > 0.0) return giEvalSH(sR / wS, sG / wS, sB / wS, n);
    if (wA > 0.0) return giEvalSH(aR / wA, aG / wA, aB / wA, n);
    return vec3(0.0);
}

#endif // GI_PROBE_GLSL
