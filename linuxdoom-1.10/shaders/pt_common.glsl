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

const float PI            = 3.14159265358979323846;
const int   FLAG_FLAT     = 0x1;     // matches RB_MESH_FLAT in r_mesh.h (flats vs walls)
const int   FLAG_MASKED   = 0x2;     // matches RB_MESH_MASKED — two-sided grate/fence mid-wall
const int   FLAG_EMISSIVE = 0x20;    // matches RB_MESH_EMISSIVE — a fullbright light Thing

// INV-7 backfill: provisional inline scene-light constants (user-approved
// 2026-06-27) pending the Vestige Formula Workbench export.
const float FIREFLY_MAX = 4.0;                       // clamp one NEE sample's
                                                     // reflected radiance (post-BRDF)
// (DOOM-0090's OMNI_CULL_VALUE dim-sprite cull was removed by DOOM-0120: RIS keeps
// every surviving sprite as a weighted candidate — a dim one simply almost never wins
// the reservoir draw — so the cliff is gone without the cull's one-sided bias.)
const vec3  SKY_COLOR    = vec3(0.20, 0.26, 0.40);   // bounded sky-light on a miss
                                                     // (linear radiance; the camera
                                                     // tonemaps it, the bake folds it
                                                     // into the probe as fill)
// DOOM-0084: self-emission is LOCALISED to a surface's bright texels — a lamp glows
// from its lit top, a computer from its screen, not the whole sprite/face evenly.
// The per-material Le (a tile-averaged value) is scaled by how bright THIS hit texel
// is, so dark texels (a lamp's metal stand) stop glowing while bright ones keep the
// tuned Le. Mirrors the same scale in svgf_composite.comp so the raw + denoised views
// agree. INV-7 backfill thresholds (linear luminance), pending a Workbench export.
const float EMIS_MASK_LO = 0.30;   // texel below this VALUE (max channel): no self-glow
const float EMIS_MASK_HI = 0.60;   // at/above this: the full material Le
// Brightness as VALUE (max channel), not luminance — so a saturated red/blue light
// glows by its intensity instead of being suppressed by luma weighting (matches
// ComputeMaterialEmissive's emis::value on the C++ side).
float emissiveMask(vec3 albedoLinear) {
    return smoothstep(EMIS_MASK_LO, EMIS_MASK_HI,
                      max(albedoLinear.r, max(albedoLinear.g, albedoLinear.b)));
}

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

// DOOM-0119 REJECT-lump light cull (read side). SubSec maps a subsector index to its
// owning sector; EmitSec maps an emitter index to its sector (0xFFFFFFFF = no cull);
// Reject is the packed sector x sector visibility bitmatrix as uint words (byte b =
// (w[b>>2] >> (8*(b&3))) & 0xFF; bit (s1*numSec+s2) set => s2 not visible from s1).
layout(buffer_reference, scalar) readonly buffer SubSec  { uint s[]; };
layout(buffer_reference, scalar) readonly buffer EmitSec { uint s[]; };
layout(buffer_reference, scalar) readonly buffer Reject  { uint w[]; };

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

// One area-sampled direct-lighting estimate from emitter triangle k, WITHOUT the
// shadow ray. Returns the incident contribution (Le * geometry / pdf), WITHOUT the
// surface BRDF (the caller folds in albedo/pi), and outputs the sampled direction +
// distance so a later occlusion test can reuse the exact point. The caller picks k by
// power (build step 3c-2) with selection probability pdfSel, and samples a uniform
// point on the triangle, so p(Q) = pdfSel * (1/area) and 1/p = area / pdfSel.
// `omniStart`: emitters with index >= omniStart are OMNIDIRECTIONAL (DOOM-0084
// sprite lights — lamps/torches/barrels). A camera-facing billboard quad has a
// normal pointing at the viewer, so as an oriented area light its cosL would be ~0
// for a floor point right below it (it would light nothing). Sprite lights are
// glowing objects that emit in all directions, so they skip the emitter-orientation
// cosine (cosL = 1). Static wall/flat emitters (index < omniStart) stay oriented.
// DOOM-0120: split out of sampleEmitter so RIS can resample candidates by their
// unshadowed weight BEFORE spending one shadow ray on the survivor. vec3(0) on a
// degenerate/back-facing emitter (wiOut/distOut then left zeroed — don't shadow them).
vec3 emitterContribUnshadowed(uint k, vec3 hitP, vec3 n, float pdfSel, uint omniStart,
                              Emitters emit, out vec3 wiOut, out float distOut, inout uint seed)
{
    wiOut = vec3(0.0); distOut = 0.0;

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
    // Oriented wall/flat emitter: two-sided cosine (DOOM faces emit both ways).
    // Omnidirectional sprite light (k >= omniStart): emits in all directions.
    float cosL = (k >= omniStart) ? 1.0 : abs(dot(nL, wi));
    if (cosL <= 0.0) return vec3(0.0);

    float G   = cosSurf * cosL / dist2;
    float inv = area / pdfSel;                  // 1 / p(Q), p = pdfSel * (1/area)
    wiOut = wi; distOut = dist;
    return Le * G * inv;                        // unshadowed estimate (pre-BRDF)
}

// Shadow ray toward a sampled emitter point: true if a world-BLAS surface blocks it.
// Opaque any-hit, terminate on first hit. tMax just short of the light so the emitter
// face itself doesn't self-occlude. Cull mask 0x01 = the world BLAS only (DOOM-0100):
// sprite billboards (mask 0x02) are alpha-cut-outs and do not occlude light in this
// increment, so shadow rays skip them entirely. (DOOM-0120: split out so the RIS omni
// path and the direct sampleEmitter path cast the byte-identical occlusion ray.)
bool occluded(vec3 hitP, vec3 n, vec3 wi, float dist)
{
    rayQueryEXT sq;
    rayQueryInitializeEXT(sq, topAS,
        gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0x01u,
        hitP + n * 1e-3, 1e-3, wi, dist - 2e-3);
    while (rayQueryProceedEXT(sq)) {}
    return rayQueryGetIntersectionTypeEXT(sq, true)
           != gl_RayQueryCommittedIntersectionNoneEXT;
}

// Full single-emitter direct estimate: unshadowed contribution gated by one shadow
// ray (the static-NEE and INV-6 verify paths' workhorse). Behaviour and seed usage
// are identical to the pre-DOOM-0120 monolithic version — it now just composes the
// two helpers above.
vec3 sampleEmitter(uint k, vec3 hitP, vec3 n, float pdfSel, uint omniStart, Emitters emit,
                   float minContribValue, inout uint seed)
{
    vec3  wi; float dist;
    vec3  contrib = emitterContribUnshadowed(k, hitP, n, pdfSel, omniStart, emit, wi, dist, seed);
    if (contrib == vec3(0.0)) return vec3(0.0); // degenerate / back-facing

    // DOOM-0090 contribution cull (opt-in: minContribValue > 0, omni path only).
    // The shadow ray is the expensive part; skip it when even a FULLY-lit result
    // would be invisible. Cheap to evaluate (no traversal), and unbiased for the
    // static/verify paths which pass 0 here.
    if (minContribValue > 0.0 &&
        max(contrib.r, max(contrib.g, contrib.b)) < minContribValue)
        return vec3(0.0);

    if (occluded(hitP, n, wi, dist)) return vec3(0.0); // shadowed
    return contrib;
}

// Reflected radiance leaving a hit surface toward the viewer/probe: optionally the
// surface's own emission, plus NEE direct light (power-importance emitter pick,
// hard ray-traced shadows, firefly-clamped per sample). Excludes any tonemapping
// (the integrator stays in linear radiance). `albedo` is decoded once by the
// caller and passed in; `nSamples` shadow rays are averaged. `addEmission` is true
// for the camera frame (a lamp you look at glows) and false for the GI bake (so the
// probes store INDIRECT-only radiance — the directly-visible emitter term is the
// frame's NEE job, double-counted if folded into the cache too).
// DOOM-0119: hitSec is the owning sector of the shading point (0xFFFFFFFF = unknown
// -> no cull); numSectors is the REJECT matrix dimension (0 = cull off). emitSec +
// reject are the cull buffers. They gate ONLY the omnidirectional sprite loop below;
// the GI bake passes numSectors 0 (its omni loop is empty, omniStart == emitCount).
vec3 shadeSurface(vec3 hitP, vec3 n, vec3 albedo, uint id, uint emitCount,
                  uint omniStart, Emitters emit, MatEmis matEmis, uint nSamples,
                  bool addEmission, uint hitSec, uint numSectors,
                  EmitSec emitSec, Reject reject, inout uint seed)
{
    // Self-emission (linear) from the per-material Le table, LOCALISED to this hit
    // texel's brightness (DOOM-0084) so a lamp glows from its lit top, not its dark
    // stand. The GI bake passes addEmission=false (the emitter term is the frame's
    // NEE job), so this only shapes the directly-visible glow.
    vec3 L = addEmission ? vec3(matEmis.m[id * 3u + 0u],
                                matEmis.m[id * 3u + 1u],
                                matEmis.m[id * 3u + 2u]) * emissiveMask(albedo)
                         : vec3(0.0);

    // Direct lighting splits by emitter kind (DOOM-0084). The MANY static wall/flat
    // emitters [0, omniStart) are power-importance-sampled through the CDF. The FEW
    // omnidirectional sprite lights (lamps/torches/pickups, [omniStart, emitCount))
    // carry tiny area-weight and would almost never win the CDF draw, so they are
    // sampled DIRECTLY below — one guaranteed shadow ray each, every frame. Without
    // the split a lamp self-glows but never casts: its CDF slice rounds to zero next
    // to the big walls, and the one-in-a-thousand hit is then eaten by the firefly
    // clamp. Splitting also keeps the static sampler unbiased (sprites are out of its
    // draw, so no double-count with the explicit loop).
    if (nSamples > 0u)
    {
        if (omniStart > 0u)              // power-importance-sample the static set
        {
            vec3 direct = vec3(0.0);
            for (uint si = 0u; si < nSamples; si++)
            {
                // Binary-search the cdf (slot 12) over [0, omniStart) for the first
                // k with cdf[k] >= u, then weight by its 1/pdf (slot 13).
                float u  = rnd(seed);
                uint  lo = 0u, hi = omniStart - 1u;
                while (lo < hi)
                {
                    uint mid = (lo + hi) >> 1u;
                    if (emit.e[mid * 14u + 12u] < u) lo = mid + 1u;
                    else                             hi = mid;
                }
                float pdf = emit.e[lo * 14u + 13u];
                vec3  c   = sampleEmitter(lo, hitP, n, pdf, omniStart, emit, 0.0, seed);
                vec3  rad = albedo * (1.0 / PI) * c; // reflected radiance (Lambert)
                direct += min(rad, vec3(FIREFLY_MAX)); // firefly clamp (luminance)
            }
            L += direct / float(nSamples);
        }

        // DOOM-0120 RIS: resample the omnidirectional sprite lights down to ONE shadow
        // ray. The old path cast a shadow ray for EVERY surviving sprite (O(sprites)
        // /pixel — the megakernel's dominant cost in a prop-lit room); RIS draws each
        // candidate's UNSHADOWED reflected radiance, keeps one by weighted-reservoir,
        // and spends a single occlusion ray on the survivor — O(1) rays + O(M) cheap
        // weight evals (cheap-ladder step 2, DOOM-0092 §1.4). No reservoir is kept
        // across frames or neighbours (the part that hurts RDNA2 registers); TAAU +
        // SVGF absorb the single ray's per-pixel visibility noise over time.
        vec3  selRefl = vec3(0.0);     // survivor's clamped UNSHADOWED reflected radiance
        vec3  selWi   = vec3(0.0);
        float selDist = 0.0, selTgt = 0.0, wsum = 0.0;
        for (uint k = omniStart; k < emitCount; k++)
        {
            // DOOM-0119 REJECT-lump cull: drop a sprite whose sector is provably
            // invisible from this hit's sector before it ever enters the candidate pool
            // — exact + free. Bypassed when either sector is unknown (a static centroid
            // on a linedef, or a sprite hit) or the level carries no REJECT data.
            if (numSectors > 0u && hitSec < numSectors)
            {
                uint es = emitSec.s[k];
                if (es < numSectors)
                {
                    uint pnum    = hitSec * numSectors + es;
                    uint bytenum = pnum >> 3u;
                    uint byteval = (reject.w[bytenum >> 2u] >> ((bytenum & 3u) * 8u)) & 0xFFu;
                    if ((byteval & (1u << (pnum & 7u))) != 0u)
                        continue;                 // not visible -> not a candidate
                }
            }
            vec3  wi; float dist;
            vec3  c = emitterContribUnshadowed(k, hitP, n, 1.0, omniStart, emit, wi, dist, seed);
            // Per-light firefly clamp BEFORE resampling — mirrors the old per-sprite
            // min(rad, FIREFLY_MAX). The RIS sum of pre-clamped lights then needs no
            // second clamp, so a torch-lit room stays as bright as the old loop while a
            // degenerate near-light can't blow up the reservoir weight.
            vec3  refl = min(albedo * (1.0 / PI) * c, vec3(FIREFLY_MAX));
            float tgt  = max(refl.r, max(refl.g, refl.b)); // VALUE target (max channel,
                                                           // matching the brightness
                                                           // convention) — a saturated
                                                           // light can't over-weight.
            if (tgt <= 0.0) continue;
            wsum += tgt;
            if (rnd(seed) * wsum < tgt)           // weighted reservoir: keep w.p. tgt/wsum
            {
                selRefl = refl; selWi = wi; selDist = dist; selTgt = tgt;
            }
        }
        // One shadow ray to the survivor; reweight by wsum/selTgt so E[estimate] equals
        // the old per-light-clamped sum  Σ_k min(rad_k, FIREFLY_MAX)·V_k  exactly.
        if (selTgt > 0.0 && !occluded(hitP, n, selWi, selDist))
            L += selRefl * (wsum / selTgt);
    }
    return L;
}

// Cosine-weighted hemisphere direction around normal n (build step 4d). The
// returned direction's pdf is cos(theta)/PI, so a Lambert brdf*cos/pdf collapses to
// the surface albedo — used by both the brute-force estimator and the white-furnace
// math check below.
vec3 cosineHemisphere(vec3 n, inout uint seed)
{
    float u1 = rnd(seed), u2 = rnd(seed);
    vec3  t  = normalize(cross(abs(n.z) < 0.999 ? vec3(0,0,1) : vec3(1,0,0), n));
    vec3  b  = cross(n, t);
    float r  = sqrt(u1), phi = 2.0 * PI * u2;
    return t * (r * cos(phi)) + b * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1));
}

// --- INV-6 verify estimators (build step 4d) -------------------------------------
// Two independent UNCLAMPED estimators of the same direct-light integral, compared
// for convergence. They must be unclamped because INV-6 proves the integrator
// unbiased "up to the firefly clamp" — the clamp is a separate, acknowledged bias,
// so folding it into either estimator would make a matched image fail for a reason
// the invariant explicitly excludes. The display/bake path (shadeSurface) keeps its
// clamp; only this verify path drops it.

// One power-importance NEE sample (the shipping estimator's selection), unclamped.
// Mirrors shadeSurface's emitter pick (cdf binary search, weight by 1/pdf) but
// without the firefly clamp.
vec3 directNEEVerify(vec3 hitP, vec3 n, vec3 albedo, uint emitCount,
                     uint omniStart, Emitters emit, inout uint seed)
{
    if (emitCount == 0u) return vec3(0.0);
    float u  = rnd(seed);
    uint  lo = 0u, hi = emitCount - 1u;
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1u;
        if (emit.e[mid * 14u + 12u] < u) lo = mid + 1u;
        else                             hi = mid;
    }
    float pdf = emit.e[lo * 14u + 13u];
    return albedo * (1.0 / PI) * sampleEmitter(lo, hitP, n, pdf, omniStart, emit, 0.0, seed);
}

// Brute-force reference: evaluate EVERY emitter each sample (no importance
// selection, pdfSel = 1 so sampleEmitter returns each light's full Le·G·area
// estimate), unclamped. This shares NO selection machinery with the NEE estimator,
// so a converged image matching power-NEE proves the power-importance weighting is
// unbiased (INV-6). Low variance (the selection noise is gone — only the per-light
// area sample remains), so it converges in far fewer samples than a unidirectional
// estimator would on DOOM's sparse emitters.
vec3 directAllLights(vec3 hitP, vec3 n, vec3 albedo, uint emitCount,
                     uint omniStart, Emitters emit, inout uint seed)
{
    vec3 sum = vec3(0.0);
    for (uint k = 0u; k < emitCount; k++)
        sum += sampleEmitter(k, hitP, n, 1.0, omniStart, emit, 0.0, seed);   // pdfSel = 1 -> full light
    return albedo * (1.0 / PI) * sum;
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
