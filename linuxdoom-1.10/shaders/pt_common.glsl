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

// DOOM-0011: volumetric fog (single-scatter view-ray march). All tune-on-hardware.
const int   kFogSteps        = 24;               // fixed sample count (coherent, cheap)
                                 // DOOM-0011 L1c raised this 24 -> 40 on the hypothesis that the
                                 // wisps put a HIGH-FREQUENCY signal along the ray (a billow is
                                 // ~200 world units across, the march spans 2048) which 24 samples
                                 // would alias into bands. FALSIFIED and reverted 2026-07-30, on
                                 // captured frames rather than argument: 40 vs 24 at the E1M1 spawn
                                 // scores mean-abs-error 0.153/255 at Low fog and 2.86 at High --
                                 // and at High the SAME build scores 2.41 against its own second
                                 // run, so the difference is at or under the engine's own
                                 // run-to-run noise. The residual also sits on the LIGHT PANELS,
                                 // not in the open air: banding would read as arcs through the fog
                                 // volume, and the volume is identical. Q26's quadratic march is
                                 // what actually resolves the near-camera layer (see the loop in
                                 // marchFog), and it does so at 24.
                                 // This matters beyond one constant: L2 fires a sun ray PER SAMPLE,
                                 // so every step here is multiplied by a ray query. Banking 16
                                 // steps is a 40 % cut to L2's cost before L2 is written.
const float kFogMaxDist      = 2048.0;           // clamp tHit so a long corridor can't blow budget
const float kFogSkyDist      = 2048.0;           // DOOM-0011 Q24a: the fog layer's finite
                                 // HORIZONTAL extent, used by skyFogOpticalDepth. Since
                                 // 2026-07-27 the sky's haze is geometric -- H/rd.z, the exact
                                 // path through an exponential layer -- so this is no longer
                                 // "the sky's distance"; it is what a perfectly horizontal ray
                                 // gets instead of an infinite path. Applied as a SOFT
                                 // saturation (reciprocals added), never a min(): a hard clamp
                                 // gave every pixel within ~3 degrees of the horizon the same
                                 // value, i.e. a flat band with a visible top edge. Set to
                                 // kFogMaxDist so the skyline is never charged more air than the
                                 // furthest wall the march covers (Q24a's point). Lower it to
                                 // pull the horizon back out of the white.
const float kFogBaseDensity  = 0.0033;           // extinction AT FLOOR LEVEL at "High". tune-on-hw.
                                 // DOOM-0011 L1c TRIED 0.0033 -> 0.0066 (the "roughly twice as
                                 // thick" half of the 2026-07-25 reference) and REVERTED it, on
                                 // a captured frame: at High the doubled layer saturates -- E1M1's
                                 // start room goes to flat white, every sight line past ~3 optical
                                 // depths -- and a saturated medium cannot show billows at all.
                                 // The wisps are a +/-60 % swing in density, which is only VISIBLE
                                 // where transmittance still varies with density, i.e. tau of
                                 // roughly 0.5..2. Thicker fog and structured fog pull against
                                 // each other; the user's 2026-07-30 call was billows over bulk.
                                 // If the bulk is wanted back, raise kFogPoolHeight (a deeper
                                 // bank, same peak density) before touching this.
                                 // 2026-07-27: the pair below was re-balanced so the cloud reads as
                                 // a bank lying ON the ground. The eye-height product is HELD FIXED
                                 // -- kFogBaseDensity * exp(-kEyeAboveFloor / kFogPoolHeight) was
                                 // 0.000340 (0.0008 / 48) and is now 0.000338 (0.0033 / 18) -- so
                                 // walls, the sky backdrop and the mountains are unchanged to
                                 // within a rounding error, and ONLY the near-ground air thickened.
                                 // Measured at High: ground at 512 u 24 % -> 49 % hazed, at 1024 u
                                 // 42 % -> 74 %; a wall at eye height is 16 % / 29 % either way.
const float kFogPoolHeight   = 112.0;            // OUTDOOR e-fold height (DOOM units) above the
                                 // level's fog-layer altitude. 2026-07-27: 18 -> 112 on user
                                 // request ("outside I want the fog much, much thicker and
                                 // higher"). At 18 (~knee height) the bank was below every sight
                                 // line; at 112 it reaches well up a courtyard wall, so you are
                                 // inside it rather than looking over it. Raise for a deeper bank.
const float kFogIndoorPool   = 18.0;             // INDOOR e-fold height. Roofed air keeps the
                                 // shallow knee-height bank the outdoor fog had before the raise —
                                 // the look the user asked to keep for interiors. Paired with
                                 // kIndoorFogScale, which is what still holds sealed rooms clear
                                 // until the L1d seep can tell "room with a window" from "room
                                 // buried three doors deep".
const float kEyeAboveFloor   = 41.0;             // DOOM's VIEWHEIGHT: how far the eye rides above
                                 // its own floor. Used as the height at which a SKY ray crosses
                                 // the ground cloud, and as the floor fallback when the primary
                                 // hit is not an up-facing surface.
const float kFogAnisotropy   = 0.40;             // Henyey-Greenstein g (mild forward bias); 0 = isotropic
const vec3  kSunDir          = normalize(vec3(0.30, 0.30, 1.0)); // world; +z is up (floor = hitP.z). L2.
const vec3  kGooTint         = vec3(0.35, 0.85, 0.30); // sickly green (L4)
const vec3  kHellTint        = vec3(0.90, 0.35, 0.30); // faint red   (L4)
const float kSkyShaftStrength   = 0.85;          // sky in-scatter gain (L1/L2). Brightness of the
                                 // fog ONLY -- extinction is kFogBaseDensity's job -- so this is
                                 // the "the fog is a bit bright outside" dial (user, 2026-07-26)
                                 // and it does not change how much the fog hides. 1.0 -> 0.85.
                                 // It must be applied by every in-scatter site, the closed-form
                                 // sky branches included, or the seam where sky meets wall shows
                                 // two different fog brightnesses.
const float kTorchShaftStrength = 1.0;           // static-emitter in-scatter gain (L3)
const float kIndoorFogScale = 0.05;   // DOOM-0011 §4.3a: fog density multiplier under a solid
                                       // roof (open sky = 1.0). 0.0 = interiors totally clear;
                                       // ~0.05 keeps a faint indoor haze. Tune on hardware (Q12).

// DOOM-0011 L1c (spec §4.3b): the Silent Hill 2 look. Two halves -- a near-white,
// colourless base tone in place of the blue-grey sky fill, and billows of visibly
// varying thickness drifting slowly past. The billows are a multiplier on density
// (mean 1), never an addend: at kWispAmp = 0 the whole feature is the identity and
// must render byte-identical to L1b (INV-11).
const vec3  kFogColor    = vec3(0.55, 0.56, 0.56); // near-white, LINEAR radiance (Q9). Replaces
                                 // SKY_COLOR at every fog in-scatter site -- foreground march and
                                 // both sky closed forms -- or the sky/wall seam shows two tones.
const float kWispAmp     = 1.0;    // density swings 0x..2x -- billows, not grain
const float kWispWeight2 = 0.7;    // octave-2 weight: CHOSEN, not SH2-derived
// Feature size is the whole game. The spec's 512 was tried first and read as NO structure
// at all: a value-noise blob is about one lattice cell across, DOOM's rooms run 256..1024
// units, so one cell spanned the entire view and the march integrated it to a flat wash.
// 192 puts several blobs across a room and a dozen along a sight line, which is what reads
// as billows. Lower still and it becomes grain along the ray -- which the step count, not
// the eye, would have to pay for.
const float kWispFreq1   = 1.0/192.0;          // one texel spans 192 world units
const float kWispFreq2   = 2.5 * kWispFreq1;   // finer octave
const vec3  kWispVel1    = vec3( 8.0, 3.0, 1.0);  // world units/sec, INSIDE the freq scale
const vec3  kWispVel2    = vec3(-3.0, 4.0, 0.3);  // deliberately SLOWER than kWispVel1
const vec3  kWispOffset2 = vec3(17.3, 5.1, 23.7); // decorrelates the octaves at t=0 and p=0
// Vertical squash. A ray integrates the noise along its whole length, which averages
// isotropic blobs back toward their mean -- the reason the first two tunings read as a
// flat wash. Squashing the lattice in z makes the billows wide and SHALLOW, like real
// banks of mist, so a sight line crosses few of them horizontally but the eye still sees
// the layering stacked up a wall. It is also the cheapest place to buy contrast: no extra
// tap, no extra step.
const float kWispSquashZ = 2.5;
const float kWispTexels  = 64.0;               // the noise volume's edge, in texels

// DOOM-0011 L1d §4.3a: the outdoor-proximity SEEP. kIndoorFogScale above is a flat
// floor for every roofed sample, which cuts the fog off hard at a roofline: step
// through a doorway and the mist stops dead at the threshold. The seep replaces that
// with a grade -- strong just inside an opening, falling away as you walk deeper --
// keyed on a load-time field holding the distance to outdoor air THROUGH OPEN SPACE
// (r_mesh.c's RB_BuildSeepField; a straight line cannot tell a room with a window
// from a sealed room sharing that wall). kIndoorFogScale stays the floor it decays
// to, and must stay > 0: L3's torch shafts need a medium in roofed air to light.
// DOOM-0281 re-tune (2026-07-27), after the re-flood shipped and the fog still did not
// read as coming in. The mechanism was firing -- the play log shows the field going
// 835 -> 715 sealed cells as walls opened -- so the fault was entirely in these two
// numbers, and both were wrong in the same direction:
//
//   kSeepMax 0.5 put a 2x density STEP at every threshold. Air standing in a doorway
//   is outdoor air; capping it at half the outdoor density means fog visibly halves
//   the instant it crosses the opening, which is the opposite of seeping through it.
//   0.9 leaves a slight lip (a room is still not a courtyard) without the cliff.
//
//   kSeepFalloff 192 killed the grade within two door-widths: 21% of outdoor density
//   at 192 units in, 11% at 384, indistinguishable from a sealed room by ~600. A
//   player standing back in a room -- which is where players stand -- saw nothing.
//
// INV-12 is untouched, and by construction rather than by luck: dMax below is defined
// as 8 x kSeepFalloff, so the sealed/unreachable sentinel scales with the falloff and
// a sealed room stays exactly 8 e-folds out, i.e. at the kIndoorFogScale floor,
// whatever this number becomes.
const float kSeepMax     = 0.9;               // density multiplier right at an opening
const float kSeepFalloff = 384.0;             // world units; e-fold inward from it
const float dMax         = 8.0 * kSeepFalloff;// the field's finite unreachable/void
                                 // sentinel, mirrored by RB_SEEP_DMAX in r_mesh.h.
                                 // FINITE deliberately: an infinity meeting a zero
                                 // bilinear weight at the grid edge yields a NaN,
                                 // and that propagates through the whole march.

// DOOM-0011 L1e / DOOM-0272 §4.3c: the SECOND fog layer. The aerial layer above is a real
// participating medium, so its opacity only ever GROWS with distance -- which means the density
// that makes the air at your feet misty is the same density that turns the far end of a courtyard
// into a white sheet. The two wants fight inside one term, and no amount of tuning separates them
// (every re-balance on 2026-07-27 traded one for the other). This layer breaks the conflict by
// NOT being a medium: it also falls off with distance FROM THE CAMERA, so it can be thick
// underfoot and gone by the far wall. Deliberately non-physical; it is the standard game trick
// that makes ground mist readable. Free to evaluate -- marchFog already has `t`.
const float kFloorFogDensity = 0.010;  // extinction AT THE FLOOR (3x kFogBaseDensity there)
const float kFloorFogPool    = 24.0;   // e-fold HEIGHT: knee-deep, so you wade through it. At
                                       // eye height (41) it is already down to 18%.
const float kFloorFogRange   = 256.0;  // e-fold DISTANCE FROM THE CAMERA -- the whole trick.
                                       // A horizontal eye-level ray therefore collects at most
                                       // 0.010*exp(-41/24)*256 = 0.46 optical depth (~37% haze),
                                       // against the aerial layer's 16% at 512 units.
                                       // Also Q26's middle column: the warped march resolves a
                                       // 256-unit range to 0.09%, so it cannot band.

// Henyey-Greenstein phase (forward/back scatter weight); cosTheta = dot(viewDir, lightDir).
float fogPhaseHG(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(denom, 1e-4), 1.5));
}

// L1: base density only (height pooling + profiles arrive at L3/L4).
// DOOM-0011 L3 (pulled forward 2026-07-27 on user feedback): density falls off with
// height above the floor, so the fog reads as a CLOUD LYING ON THE GROUND rather than a
// uniform tint painted over everything by distance. Until now this returned a constant,
// which is why the user described it as "a fog look applied to geometry instead of an
// actual cloud near the ground" -- with uniform density every surface just gets greyed in
// proportion to how far away it is, and nothing ever looks like it is standing IN
// something. kFogPoolHeight is the e-fold height: at floorZ + kFogPoolHeight the air is
// 1/e as thick, so the cloud has a soft top edge instead of a hard line.
// `baseZ` is the altitude the cloud lies on and `poolH` its e-fold height. BOTH must
// depend only on where `p` is -- never on what the ray that produced `p` went on to hit.
// Breaking that is what produced the 2026-07-27 defect the user described as "we are
// simulating the look of a cloud but only on some surfaces": the floor pixel and the wall
// pixel beside it were shading the SAME air against two different ground heights, so the
// wall hazed and the floor in front of it did not.
float fogDensity(vec3 p, float baseZ, float poolH) {
    return kFogBaseDensity * exp(-max(0.0, p.z - baseZ) / poolH);
}

// DOOM-0011 L1e: the floor layer (§4.3c). Unlike fogDensity() this takes `t`, the distance
// along the view ray -- and that is the one deliberate relaxation of the contract above. The
// height factor still depends on `p` alone (so the invariant that matters, "the air at a point
// cannot depend on what the ray eventually hits", holds); the range factor depends on the ray's
// own parameter, which is a property of the VIEW, not of the world. That is what buys mist at
// your feet without a white wall at distance, and it is why this cannot be folded into
// fogDensity(): the two layers must keep separate e-fold heights.
float floorFogDensity(vec3 p, float baseZ, float t) {
    return kFloorFogDensity * exp(-max(0.0, p.z - baseZ) / kFloorFogPool)
                            * exp(-t / kFloorFogRange);
}

// rb_fog strength level (pc.misc6.z: 1=Low 2=Med 3=High; 0 is gated out by the caller)
// -> density multiplier, so the `;` dial genuinely thins/thickens the air. tune-on-hardware.
float fogStrengthScale(uint level) {
    return (level <= 1u) ? 0.35 : (level == 2u) ? 0.65 : 1.0;
}

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

// One power-importance NEE sample over the FULL emitter set [0, emitCount), unclamped:
// a single cdf binary-search pick weighted by 1/pdf. This is INV-6's unbiased-selection
// REFERENCE, not a literal mirror of shadeSurface's production pick -- after the
// DOOM-0084 static/omni split shadeSurface selects static emitters by cdf but samples
// the omni sprites in a separate RIS loop (DOOM-0120). omniStart only sets how
// sampleEmitter interprets a picked emitter (oriented < omniStart, omni >=); the real
// omniStart (pc.misc4.y) is now passed so the omni emitters are covered, and
// directAllLights passes the same omniStart, so the importance identity holds (INV-6).
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
