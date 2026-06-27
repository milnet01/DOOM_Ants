// DOOM-0009 path tracer — shared surface shading (build step 4b-ii).
//
// One implementation of the linear-albedo decode + NEE direct lighting, included
// by BOTH the camera megakernel (pathtrace.comp) and the level-load GI bake
// (bake.comp). Sharing it is the point: the baked indirect irradiance and the
// directly-traced frame must agree on what a surface reflects, or the bounce
// solution wouldn't match the first hit. Mirrors the "one shared impl" posture of
// nee_sampling.h (the CPU/emitter-builder side of the same NEE math).
//
// The includer MUST, before #include-ing this, enable GL_EXT_ray_query /
// GL_EXT_buffer_reference / GL_EXT_scalar_block_layout / GL_EXT_nonuniform_qualifier
// and declare these globals with exactly these names + bindings:
//   layout(set = 0, binding = 0) uniform accelerationStructureEXT topAS;
//   layout(set = 1, binding = 0) uniform sampler2D paletteTex;     // PLAYPAL LUT
//   layout(set = 1, binding = 2) uniform sampler2D materialTex[];  // bindless mats
// The mesh/emitter/Le buffers are passed in by device-address handle (below) so
// each shader keeps its own push-constant layout.

const float PI        = 3.14159265358979323846;
const int   FLAG_FLAT = 0x1;     // matches RB_MESH_FLAT in r_mesh.h (flats vs walls)

// INV-7 backfill: provisional inline scene-light constants (user-approved
// 2026-06-27) pending the Vestige Formula Workbench export.
const float FIREFLY_MAX = 4.0;                       // clamp one NEE sample's
                                                     // reflected radiance (post-BRDF)
const vec3  SKY_COLOR    = vec3(0.20, 0.26, 0.40);   // bounded sky-light on a miss
                                                     // (linear radiance; the camera
                                                     // tonemaps it, the bake folds it
                                                     // into the probe as fill)

// The level mesh vertex buffer (rb_vertex_t: 18 floats/vertex — pos[0..2]
// normal[3..5] u/v[6..7] texnum[8] flags[9] sectorLight[10]) and the step-3b
// emitter list (14 floats/record: v0[3] v1[3] v2[3] Le[3] cdf pdf) and the
// per-material Le table (3 floats/material), all read by GPU address.
layout(buffer_reference, scalar) readonly buffer Verts    { float v[]; };
layout(buffer_reference, scalar) readonly buffer Emitters { float e[]; };
layout(buffer_reference, scalar) readonly buffer MatEmis  { float m[]; };

// GI probe cache (build step 4c), read side. ProbesRO is the baked per-subsector
// SH-L1 (16 floats/probe: pos[3] pad, then channel-major radiance SH R[4] G[4]
// B[4]); TriSs maps a hit triangle's primitive index -> its subsector (== probe)
// index. Both the camera frame and the multi-bounce bake read these.
layout(buffer_reference, scalar) readonly buffer ProbesRO { float p[]; };
layout(buffer_reference, scalar) readonly buffer TriSs    { uint  s[]; };

// Cheap integer hash (PCG) -> a float in [0,1), for stochastic sampling.
uint pcgHash(uint x)
{
    uint state = x * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float rnd(inout uint s) { s = pcgHash(s); return float(s) * (1.0 / 4294967296.0); }

// Linear surface albedo: fetch the paletted DOOM texel exactly as the raster
// mesh.frag does (R8 index -> PLAYPAL RGB), then de-gamma to linear. `id` is the
// unified bindless material id (walls direct, flats offset by numWall); `uv` is
// the raw texel coordinate (divided here by the material size).
vec3 decodeAlbedo(uint id, vec2 uv)
{
    vec2  sz  = vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
    float idx = texture(materialTex[nonuniformEXT(id)], uv / sz).r * 255.0;
    vec3  a   = texture(paletteTex, vec2((idx + 0.5) / 256.0, 0.5)).rgb;
    return pow(a, vec3(2.2));                 // palette is gamma-encoded -> linear
}

// One area-sampled direct-lighting estimate from emitter triangle k. Returns the
// incident contribution (Le * geometry / pdf), WITHOUT the surface BRDF (the
// caller folds in albedo/pi). The caller picks k by power (build step 3c-2) with
// selection probability pdfSel, and samples a uniform point on the triangle, so
// p(Q) = pdfSel * (1/area) and 1/p = area / pdfSel.
vec3 sampleEmitter(uint k, vec3 hitP, vec3 n, float pdfSel, Emitters emit, inout uint seed)
{
    uint b = k * 14u;
    vec3 v0 = vec3(emit.e[b+0u],  emit.e[b+1u],  emit.e[b+2u]);
    vec3 v1 = vec3(emit.e[b+3u],  emit.e[b+4u],  emit.e[b+5u]);
    vec3 v2 = vec3(emit.e[b+6u],  emit.e[b+7u],  emit.e[b+8u]);
    vec3 Le = vec3(emit.e[b+9u],  emit.e[b+10u], emit.e[b+11u]);

    vec3 e1 = v1 - v0, e2 = v2 - v0;
    vec3 ng = cross(e1, e2);
    float area2 = length(ng);                  // = 2 * triangle area
    if (area2 < 1e-8) return vec3(0.0);
    vec3  nL   = ng / area2;
    float area = 0.5 * area2;

    // Uniform point on the triangle (reflected barycentric).
    float r1 = rnd(seed), r2 = rnd(seed);
    if (r1 + r2 > 1.0) { r1 = 1.0 - r1; r2 = 1.0 - r2; }
    vec3 Q = v0 + e1 * r1 + e2 * r2;

    vec3  d     = Q - hitP;
    float dist2 = dot(d, d);
    if (dist2 < 1e-6) return vec3(0.0);
    float dist = sqrt(dist2);
    vec3  wi   = d / dist;

    float cosSurf = dot(n, wi);
    if (cosSurf <= 0.0) return vec3(0.0);      // emitter is behind the surface
    float cosL = abs(dot(nL, wi));             // two-sided (DOOM faces emit both ways)
    if (cosL <= 0.0) return vec3(0.0);

    // Occlusion: opaque any-hit, terminate on first hit. tMax just short of the
    // light so the emitter face itself doesn't self-occlude.
    rayQueryEXT sq;
    rayQueryInitializeEXT(sq, topAS,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFFu,
        hitP + n * 1e-3, 1e-3, wi, dist - 2e-3);
    while (rayQueryProceedEXT(sq)) {}
    if (rayQueryGetIntersectionTypeEXT(sq, true)
        != gl_RayQueryCommittedIntersectionNoneEXT)
        return vec3(0.0);                      // shadowed

    float G   = cosSurf * cosL / dist2;
    float inv = area / pdfSel;                  // 1 / p(Q), p = pdfSel * (1/area)
    return Le * G * inv;
}

// Reflected radiance leaving a hit surface toward the viewer/probe: optionally the
// surface's own emission, plus NEE direct light (power-importance emitter pick,
// hard ray-traced shadows, firefly-clamped per sample). Excludes any tonemapping
// (the integrator stays in linear radiance). `albedo` is decoded once by the
// caller and passed in; `nSamples` shadow rays are averaged. `addEmission` is true
// for the camera frame (a lamp you look at glows) and false for the GI bake (so the
// probes store INDIRECT-only radiance — the directly-visible emitter term is the
// frame's NEE job, double-counted if folded into the cache too).
vec3 shadeSurface(vec3 hitP, vec3 n, vec3 albedo, uint id, uint emitCount,
                  Emitters emit, MatEmis matEmis, uint nSamples, bool addEmission,
                  inout uint seed)
{
    // Self-emission (linear) from the per-material Le table.
    vec3 L = addEmission ? vec3(matEmis.m[id * 3u + 0u],
                                matEmis.m[id * 3u + 1u],
                                matEmis.m[id * 3u + 2u])
                         : vec3(0.0);

    if (emitCount > 0u && nSamples > 0u)
    {
        vec3 direct = vec3(0.0);
        for (uint si = 0u; si < nSamples; si++)
        {
            // Pick an emitter by power: binary-search the cdf (slot 12) for the
            // first k with cdf[k] >= u, then weight by its 1/pdf (slot 13).
            float u  = rnd(seed);
            uint  lo = 0u, hi = emitCount - 1u;
            while (lo < hi)
            {
                uint mid = (lo + hi) >> 1u;
                if (emit.e[mid * 14u + 12u] < u) lo = mid + 1u;
                else                             hi = mid;
            }
            float pdf = emit.e[lo * 14u + 13u];
            vec3  c   = sampleEmitter(lo, hitP, n, pdf, emit, seed);
            vec3  rad = albedo * (1.0 / PI) * c;     // reflected radiance (Lambert)
            direct += min(rad, vec3(FIREFLY_MAX));   // firefly clamp in luminance domain
        }
        L += direct / float(nSamples);
    }
    return L;
}

// Evaluate the baked SH-L1 GI cache for subsector `subId` along normal `n`, and
// return the diffuse reflected-radiance factor (multiply by surface albedo). The
// probe stores RADIANCE SH coefficients; convolving with the clamped-cosine kernel
// gives irradiance E(n) = A0*c0*Y0 + A1*(c1.Y1(n)), A0=PI, A1=2PI/3, and the
// Lambert BRDF divides by PI — so the PIs fold to weight 1 on the DC term and 2/3
// on the linear terms. Basis order matches the bake's projection (coeff 1<-n.y,
// 2<-n.z, 3<-n.x). SH-L1 can ring slightly negative, so clamp to >= 0.
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
