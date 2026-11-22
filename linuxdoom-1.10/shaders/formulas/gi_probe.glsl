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
// +x -x +y -y +z -z, map start, pad, then the probes' polar wall maps (B5). TriSs maps a hit triangle's primitive index to its subsector.
layout(buffer_reference, scalar) readonly buffer ProbesRO { float p[]; };
layout(buffer_reference, scalar) readonly buffer TriSs    { uint  s[]; };

// MUST equal light_grid.h's PROBE_HDR, PROBE_REC, PROBE_MAP, RB_PROBE_POLAR_BINS,
// RB_PROBE_NBIAS and RB_PROBE_SLACK (shader_mirror_test.cpp).
const uint  GI_PROBE_HDR   = 8u;
const uint  GI_PROBE_REC   = 24u;
const uint  GI_PROBE_MAP   = 22u;
const uint  GI_PROBE_BINS  = 32u;
const float GI_PROBE_NBIAS = 8.0;
const float GI_PROBE_SLACK = 8.0;

// DOOM-0652 B5: does the probe whose polar map starts at m see the point d from it across?
// light_grid.h's PolarCoord (diamond angle, GI_PROBE_BINS bins) and ProbeSeesAcross: the
// wall distance interpolated between the two nearest bins. The map holds its own slack.
bool giProbeSees(ProbesRO pr, uint m, vec2 d)
{
    float s = abs(d.x) + abs(d.y);
    if (s <= 0.0) return true;
    float p = d.y >= 0.0 ? (d.x >= 0.0 ? d.y / s : 1.0 - d.x / s)
                         : (d.x <  0.0 ? 2.0 - d.y / s : 3.0 + d.x / s);
    p *= float(GI_PROBE_BINS) * 0.25;
    if (p >= float(GI_PROBE_BINS)) p -= float(GI_PROBE_BINS);
    p -= 0.5;
    float fl   = floor(p);
    uint  i0   = uint(int(fl) + int(GI_PROBE_BINS)) % GI_PROBE_BINS;
    uint  i1   = (i0 + 1u) % GI_PROBE_BINS;
    float wall = mix(pr.p[m + i0], pr.p[m + i1], p - fl);
    return length(d) <= wall;
}

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
// valid probe is dropped, and so is one with a wall between it and the point; the weight
// left is renormalised. When every present corner is blocked they blend unmasked, and
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

    // DOOM-0652 B4: two passes, so a corner's 12 SH floats are fetched only when the
    // blend uses it. The first tests whether each corner sees the point; the second blends
    // the corners that reach, or every present one when none does.
    uint  rb[8];
    float rw[8];
    uint  present = 0u, reaching = 0u;
    for (int k = 0; k < 8; k++)
    {
        ivec3 off = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);
        ivec3 q   = bi + off;
        rb[k] = 0u; rw[k] = 0.0;
        if (any(lessThan(q, ivec3(0))) || any(greaterThanEqual(q, dim)))
            continue;
        float idx = pr.p[GI_PROBE_HDR + uint((q.z * dim.y + q.y) * dim.x + q.x)];
        if (idx < 0.0)
            continue;
        uint b = rec0 + uint(idx) * GI_PROBE_REC;
        if (pr.p[b + 3u] == 0.0)
            continue;
        vec3  share = mix(1.0 - f, f, vec3(off));
        rb[k] = b;
        rw[k] = share.x * share.y * share.z;
        present |= 1u << k;
        // B5: across by the probe's polar map, up and down by its z reach (spec 4.5).
        vec3  d     = x - vec3(pr.p[b], pr.p[b + 1u], pr.p[b + 2u]);
        float reach = d.z >= 0.0 ? pr.p[b + 20u] : pr.p[b + 21u];
        bool  sees  = abs(d.z) <= reach + GI_PROBE_SLACK;
        float m     = pr.p[b + GI_PROBE_MAP];
        if (sees && m >= 0.0)
            sees = giProbeSees(pr, uint(m), d.xy);
        if (sees)
            reaching |= 1u << k;
    }
    float wR = 0.0;
    for (int k = 0; k < 8; k++)
        if ((reaching & (1u << k)) != 0u) wR += rw[k];
    // Every present corner falling short blends them all; a reaching set whose weights
    // sum to zero does too, as the single-pass form did.
    const uint use = (wR > 0.0) ? reaching : present;

    vec4 sR = vec4(0.0), sG = vec4(0.0), sB = vec4(0.0);
    float wS = 0.0;
    for (int k = 0; k < 8; k++)
    {
        if ((use & (1u << k)) == 0u)
            continue;
        uint  b = rb[k];
        float w = rw[k];
        sR += w * vec4(pr.p[b +  4u], pr.p[b +  5u], pr.p[b +  6u], pr.p[b +  7u]);
        sG += w * vec4(pr.p[b +  8u], pr.p[b +  9u], pr.p[b + 10u], pr.p[b + 11u]);
        sB += w * vec4(pr.p[b + 12u], pr.p[b + 13u], pr.p[b + 14u], pr.p[b + 15u]);
        wS += w;
    }
    if (wS > 0.0) return giEvalSH(sR / wS, sG / wS, sB / wS, n);
    return vec3(0.0);
}

#endif // GI_PROBE_GLSL
