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

// Reflected radiance leaving a hit surface toward the viewer/probe: the surface's
// own emission + NEE direct light (power-importance emitter pick, hard ray-traced
// shadows, firefly-clamped per sample). EXCLUDES the temporary flat ambient fill
// (camera-only, retired once the probes feed the frame in step 4c) and any
// tonemapping (the integrator stays in linear radiance). `albedo` is decoded once
// by the caller and passed in; `nSamples` shadow rays are averaged.
vec3 shadeSurface(vec3 hitP, vec3 n, vec3 albedo, uint id, uint emitCount,
                  Emitters emit, MatEmis matEmis, uint nSamples, inout uint seed)
{
    // Self-emission (linear) from the per-material Le table.
    vec3 L = vec3(matEmis.m[id * 3u + 0u],
                  matEmis.m[id * 3u + 1u],
                  matEmis.m[id * 3u + 2u]);

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
