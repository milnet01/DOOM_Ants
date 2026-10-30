// hd_surface.glsl — DOOM-0181 de-tiling and DOOM-0179/0181 filth, one copy for the traced
// view (pathtrace.comp) and the rasterised one (mesh.frag, DOOM-0182).
//
// The includer has hd_set.glsl, pcgHash and hdParallaxUV, and defines before the #include:
//   HD_TEX(i, uv)          sample hdTex[i] at a surface UV
//   HD_TEX_W(i, w, k, off) sample hdTex[i] at world coord w * k + off
//   HD_DETILE_DIAL         0 off, 1 2-tap, 2 4-tap
//   HD_GRIME_ID, HD_DIRT_ID the overlays' slots in hdTex[], 0xFFFFFFFF for none
//   HD_FILTH_ON            bool, the filth toggle
// Colours here are linear.

// DOOM-0179: world-space grime overlay tunables (the playtest knobs — rebuild to re-tune).
// One grime tile spans ~384 DOOM map units, so the pattern is LOW frequency vs the base
// texture (which repeats every 64-128 units): grime drifts across a whole wall and differs
// between adjacent walls at different world positions, breaking the visible tiling. Strength
// is the ± mottle depth of the centred blend (0 = off). The base heroes are pre-weathered,
// so keep this subtle — it ADDS variation, it isn't the primary dirt.
const float kGrimeWorldScale = 1.0 / 384.0;
const float kGrimeStrength    = 0.25;   // subtle large-scale brightness variation (grounding only)
// DOOM-0181 L4 "filth" rework: distinct, hard-edged, multi-coloured DIRT STAINS — not a soft wash.
// The old soft multiply could only fade, so it never read as dirt. Now a stain forms where the
// multi-scale grunge (grungeFbm) is darkest, and a NARROW smoothstep gives a defined edge. Each
// stain-region picks ONE colour from a small palette by a coarse world-cell hash, so a whole mark
// is one colour and neighbours differ: brown dirt / near-black soot on any surface; green goo only
// pools on floors. Applied to ALL world surfaces in the Ultra RT view (paletted included) so dirt
// shows everywhere — never on sprites. The crevice term still darkens genuine recesses (INV-7);
// darkening stays inside applyGrime's clamp(0.35,1.65) ceiling (INV-6).
const float kGrimeCrevice  = 0.75;      // extra darkening in AO-occluded crevices (grounding)
const float kStainThresh   = 0.44;      // grunge value below which a stain forms
const float kStainEdge     = 0.07;      // smoothstep band (small = hard, defined edge)
const float kStainOpacity  = 0.50;      // how opaque a stain sits over the albedo
// Dirt is NOT one flat colour: a stain blends across earthy tones (brown -> ochre -> soot) by a
// SMOOTH hue selector (a coarse overlay sample, not a cell grid — cells made straight seams), plus
// per-texel brightness mottle, so colour varies within and between marks. Green goo pools on
// floors, but never on an already-green (goo/slime) surface.
const vec3  kDirtBrown     = vec3(0.22, 0.14, 0.08);  // core dirt
const vec3  kDirtOchre     = vec3(0.30, 0.20, 0.09);  // lighter earthy streaks
const vec3  kDirtDark      = vec3(0.11, 0.07, 0.04);  // deep dark-brown patches
const vec3  kSootColour    = vec3(0.06, 0.05, 0.045); // dark grime
const vec3  kGooColour     = vec3(0.07, 0.20, 0.04);  // green goo (floors only)
const float kGooChance     = 0.15;      // goo share of floor stains
// Marks come in a RANGE of sizes by summing the overlay at three world scales (big stains ->
// medium -> fine speckle); one scale alone reads as a repeating stamp. Weights sum to 1 so the
// combined value stays ~[0,1]. Per-octave UV offsets stop the scales aligning into a grid.
const vec3  kGrimeOctaveScale  = vec3(0.35, 1.0, 2.6); // x kGrimeWorldScale: large / medium / fine
const vec3  kGrimeOctaveWeight = vec3(0.50, 0.32, 0.18);

// DOOM-0181: stochastic de-tiling (world-keyed offset + mirror + 4-corner blend). Breaks the
// within-surface repetition of an HD map by giving each world-space cell its own sub-tile UV
// offset and (maybe) a horizontal mirror, then blending neighbouring cells so no hard seam
// shows. World-keyed (INV-4): two same-texture walls at different world positions get different
// variation. Transform set is {offset, mirror} ONLY (INV-1) — DOOM walls are vertical, so no
// rotation / vertical-flip. Ultra only, gated on detileOn() + usePBR.
const float kDetileWorldCell  = 48.0;   // world units per variation cell (§10 Q2 tunable). Not 64, a
                                        // flat's period: there the blend seams sat on the texture's
                                        // own repeat and the two stacked into a visible floor grid
                                        // (DOOM-0271). Larger cells are worse (every tile copy in a
                                        // cell shares its offset), so 48: measured ~0 repeat left.
const float kDetileOffsetMag  = 0.65;   // sub-tile offset magnitude, in tiles (± ; 0.5->0.65 = more
                                        // decorrelation between neighbouring cells)
const float kDetileMirrorProb = 0.5;    // P(a cell is horizontally mirrored)

// vec3 hash of an integer cell, built on the existing pcgHash PRNG (no new algorithm). The two
// odd multipliers decorrelate x and y before hashing; the +1u/+2u re-hashes give 3 independent
// [0,1) channels (xy = offset, z = mirror decision).
vec3 hash3(ivec2 cell) {
    uint s = pcgHash(uint(cell.x) * 0x9E3779B1u ^ uint(cell.y) * 0x85EBCA77u);
    return vec3(pcgHash(s), pcgHash(s + 1u), pcgHash(s + 2u)) * (1.0 / 4294967296.0);
}

// Dominant-axis world projection of the hit point (matches applyGrime): floors/ceilings->xy,
// ±x walls->yz, ±y walls->xz. Returns the 2-D world coordinate the cell grid is quantised on.
vec2 detileWorldUV(vec3 hitP, vec3 n) {
    vec3 an = abs(n);
    return (an.z >= an.x && an.z >= an.y) ? hitP.xy
         : (an.x >= an.y)                 ? hitP.yz
                                          : hitP.xz;
}

// misc5.y quality dial: 0 = off, 1 = 2-tap, 2 = 4-tap; any other value = off.
bool detileOn()   { return HD_DETILE_DIAL == 1u || HD_DETILE_DIAL == 2u; }
int  detileTaps() { return HD_DETILE_DIAL == 2u ? 4 : (HD_DETILE_DIAL == 1u ? 2 : 0); }

// misc5.w: filth (dirt-stain) master toggle — 1 = on (shipped look), 0 = off (`[` key).
// DOOM-0187: lets applyGrime be A/B'd live to isolate the stain-fetch cost; off is also a
// standing perf/quality option. Applies to ALL non-sprite world surfaces (not HD-gated).
bool filthOn() { return HD_FILTH_ON; }

// Per-cell transform of the sampling coordinate sUV: signed sub-tile offset + optional horizontal
// mirror (flip U within the cell; INV-1). Returns the transformed UV for cell `c`.
// DOOM-0439: the hashed form, for a caller that also needs h.z for its own mirror test.
vec2 detileCellUVh(vec2 sUV, vec3 h) {
    vec2 off = (h.xy - 0.5) * 2.0 * kDetileOffsetMag;
    vec2 uv = sUV + off;
    if (h.z < kDetileMirrorProb) uv.x = -uv.x;   // horizontal mirror only
    return uv;
}
vec2 detileCellUV(vec2 sUV, ivec2 c) { return detileCellUVh(sUV, hash3(c)); }

// De-tiled sample of HD map `mapIdx` at surface UV `sUV`, world-cell coord `g = w/kDetileWorldCell`.
// Inigo-Quilez 4-corner blend: sample the neighbouring cells' transformed UVs and blend by the
// smooth fractional position. 2-tap mode blends the two nearest cells along X only.
vec3 detile(uint mapIdx, vec2 sUV, vec2 g) {
    ivec2 cell = ivec2(floor(g));
    vec2  f    = fract(g);
    vec2  b    = smoothstep(vec2(0.25), vec2(0.75), f);
    vec3 s00 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(0,0))).rgb;
    vec3 s10 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(1,0))).rgb;
    if (detileTaps() < 4) {                       // 2-tap: blend the two nearest cells on X only
        return mix(s00, s10, b.x);
    }
    vec3 s01 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(0,1))).rgb;
    vec3 s11 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(1,1))).rgb;
    return mix(mix(s00, s10, b.x), mix(s01, s11, b.x), b.y);
}

// DOOM-0181 L2: scalar (.r) de-tile — for the AO map. Same 4-corner blend as detile().
float detileR(uint mapIdx, vec2 sUV, vec2 g) {
    ivec2 cell = ivec2(floor(g)); vec2 f = fract(g);
    vec2 b = smoothstep(vec2(0.25), vec2(0.75), f);
    float s00 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(0,0))).r;
    float s10 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(1,0))).r;
    if (detileTaps() < 4) return mix(s00, s10, b.x);
    float s01 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(0,1))).r;
    float s11 = HD_TEX(mapIdx, detileCellUV(sUV, cell + ivec2(1,1))).r;
    return mix(mix(s00, s10, b.x), mix(s01, s11, b.x), b.y);
}

// DOOM-0181 L2: tangent-space normal de-tile. Unpack the normal PER cell and negate its X on a
// mirrored cell (INV-2 — a horizontal flip reverses tangent handedness, so relief points the
// right way), then blend. Returns the blended tangent-space normal (caller rotates by T,B,n).
vec3 detileNormalTS(uint mapIdx, vec2 sUV, vec2 g) {
    ivec2 cell = ivec2(floor(g)); vec2 f = fract(g);
    vec2 b = smoothstep(vec2(0.25), vec2(0.75), f);
    int taps = detileTaps();
    vec3 acc = vec3(0.0); float wsum = 0.0;
    for (int i = 0; i < (taps < 4 ? 2 : 4); i++) {
        ivec2 corner = (taps < 4) ? ivec2(i, 0) : ivec2(i & 1, i >> 1);
        ivec2 c = cell + corner;
        vec3  h  = hash3(c);                                  // once per cell (DOOM-0439)
        vec3  tn = HD_TEX(mapIdx, detileCellUVh(sUV, h)).xyz * 2.0 - 1.0;
        if (h.z < kDetileMirrorProb) tn.x = -tn.x;            // mirror flips tangent handedness
        float wx = (corner.x == 0) ? (1.0 - b.x) : b.x;
        float wy = (taps < 4) ? 1.0 : ((corner.y == 0) ? (1.0 - b.y) : b.y);
        acc += tn * (wx * wy); wsum += wx * wy;
    }
    return acc / max(wsum, 1e-4);
}

// DOOM-0181 L3: parallax-occlusion march in de-tiled space (§4.4). Compute the dominant cell's
// offset+mirror ONCE, march the height field in that single cell's frame (NOT per-step blended —
// that would cost taps × march-steps), then hand back a coordinate the §4.2 4-corner albedo/
// normal/AO wrappers can still de-tile in ONE step. Key subtlety: the wrappers re-apply the
// per-cell offset+mirror themselves, so we must NOT return the already-offset marched coordinate
// (that double-transforms). Instead transfer only the parallax DISPLACEMENT (marched - duv) onto
// the clean baseUV: then detileCellUV(result, dominantCell) lands exactly on the marched texel, so
// relief registers with colour, while neighbour cells in the blend cross-fade and hide the seam.
// INV-2: the march dir's tangent-X is negated on a mirrored cell (so relief doesn't invert), and
// the transferred displacement's U is negated too so it survives the wrapper's re-mirror.
vec2 detilePOM(MatCtrl mc, vec2 baseUV, vec3 T, vec3 B, vec3 n, vec3 dir, vec2 g) {
    if (mc.maps[6] < 0) return baseUV;                 // no height map -> hdParallaxUV is a no-op anyway
    ivec2 cell = ivec2(floor(g));
    vec2  f    = fract(g);
    // Border band (start 0.1): near a cell edge two adjacent pixels can pick different dominant
    // cells and the single-cell march would seam. Skip the march there and let the wrappers de-tile
    // the plain baseUV (§4.4 fallback; the 0.1 band width is the §10 Q1 tuning knob).
    if (min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)) < 0.1) return baseUV;
    vec3 hc   = hash3(cell);                           // once per cell (DOOM-0439)
    bool mir  = hc.z < kDetileMirrorProb;
    vec2 duv  = detileCellUVh(baseUV, hc);             // this cell's offset+mirror, applied once
    vec3 mdir = dir;
    if (mir) mdir -= 2.0 * dot(mdir, T) * T;           // mirror the march dir's tangent-X (INV-2)
    vec2 d = hdParallaxUV(mc, duv, T, B, n, mdir) - duv;   // parallax displacement in the cell frame
    return baseUV + vec2(mir ? -d.x : d.x, d.y);       // transfer to clean UV (cancels wrapper re-mirror)
}

// DOOM-0179: world-space grime overlay. The base HD texture tiles with the surface (every
// 64-128 units); this multiplies in a single low-frequency grunge map sampled by WORLD
// position instead, so identical tiles on different walls pick up different grime and stop
// reading as copies. Dominant-axis projection (DOOM geometry is axis-aligned, z up: floors/
// ceilings face ±z, walls face ±x or ±y — so a hard axis pick is exact, no triplanar blend).
// The blend is CENTRED on the map's 0.5 mean (m = 1 + (g-0.5)*2*strength): net-neutral
// exposure, dark specks darken and clean spots lighten. Applied to HD (usePBR) surfaces only
// — paletted/Classic art is left byte-identical. `gid` is the overlay's hdTex[] slot from the
// host (HD_GRIME_ID); 0xFFFFFFFF (the default all-paletted set, or grime disabled) is a no-op.
// Multi-scale ("randomly-sized") grunge: the overlay summed at three world scales + offsets so
// marks range from big stains to fine speckle instead of one repeating size.
float grungeFbm(uint gid, vec2 wUV)
{
    float s = kGrimeWorldScale;
    return kGrimeOctaveWeight.x * HD_TEX_W(gid, wUV, s*kGrimeOctaveScale.x, vec2(0.13, 0.71)).r
         + kGrimeOctaveWeight.y * HD_TEX_W(gid, wUV, s*kGrimeOctaveScale.y, vec2(0.51, 0.19)).r
         + kGrimeOctaveWeight.z * HD_TEX_W(gid, wUV, s*kGrimeOctaveScale.z, vec2(0.87, 0.43)).r;
}

// The coarse goo/tone selector. DOOM-0407: fetched ONCE per stained texel and passed
// down -- it was sampled up to three times with byte-identical arguments.
float gooSelector(uint gid, vec2 wUV)
{
    return HD_TEX_W(gid, wUV, kGrimeWorldScale * 0.12, vec2(3.1, 1.7)).r;
}

// Smooth, mottled, MULTI-HUE dirt colour — no cell grid, so no straight seams. `hue` (a coarse
// smooth overlay sample) picks the earthy tone; `bright` (the per-texel grunge) mottles it. Green
// goo pools on up-facing floors, but never on an already-green (goo/slime) surface.
// Dirt-stain colour. When a real dirt COLOUR texture is loaded (HD_DIRT_ID, DOOM-0181) it drives
// the colour — sampled at a macro world scale for organic tonal/hue variation, times a finer
// sample for close-up grain — so filth reads as a photographed dirt texture, not a flat tint.
// Falls back to a procedural earthy ramp (three grunge scales) when no dirt texture is present.
// Green goo pools on up-facing floors (coarse grunge selector) but never on already-green surfaces.
// `sel` is the coarse selector sample (gooSelector), already fetched by the caller on an
// up-facing floor and negative otherwise; the procedural `tone` is the same sample.
vec3 stainColour(uint gid, vec2 wUV, vec3 n, vec3 albedo, float sel)
{
    bool green = albedo.g > albedo.r * 1.15 && albedo.g > albedo.b * 1.15;   // already goo/slime
    if (n.z > 0.6 && !green) {
        if (sel < kGooChance)
            return kGooColour * (0.70 + 0.6 * HD_TEX_W(gid, wUV, kGrimeWorldScale * 3.3, vec2(0.0)).r);
    }
    uint did = HD_DIRT_ID;
    if (did != 0xFFFFFFFFu) {                                                 // real dirt texture
        vec3 macro = HD_TEX_W(did, wUV, kGrimeWorldScale * 0.5, vec2(0.0)).rgb;
        float fine = HD_TEX_W(did, wUV, kGrimeWorldScale * 2.0, vec2(0.37)).r;
        return macro * (0.72 + 0.55 * fine);                                 // macro colour x fine grain
    }
    float s     = kGrimeWorldScale;                                          // procedural fallback
    float tone  = sel >= 0.0 ? sel : gooSelector(gid, wUV);
    float blot  = HD_TEX_W(gid, wUV, s * 0.90, vec2(0.6, 4.2)).r;
    float grain = HD_TEX_W(gid, wUV, s * 3.30, vec2(2.8, 0.4)).r;
    vec3  dirt  = mix(kDirtDark, kDirtBrown, smoothstep(0.20, 0.55, tone));
    dirt        = mix(dirt, kDirtOchre, smoothstep(0.55, 0.85, tone));
    dirt       *= mix(0.60, 1.15, blot);
    dirt       *= (0.85 + 0.30 * grain);
    return mix(dirt, kSootColour, smoothstep(0.82, 0.97, blot) * 0.5);
}

vec3 applyGrime(vec3 albedo, vec3 hitP, vec3 n, float ao, out float gooWet)
{
    gooWet = 0.0;                                        // DOOM-0183 L5: green-goo puddle coverage
    // Liquids (nukage/slime — a saturated-green albedo) are not a paintable surface: no dirt, no
    // goo, no crevice grime lands on flowing liquid. Leave them untouched (user 2026-07-16).
    if (albedo.g > albedo.r * 1.15 && albedo.g > albedo.b * 1.15) return albedo;
    vec2 wUV = detileWorldUV(hitP, n);                    // dominant-axis world projection (shared)
    uint gid = HD_GRIME_ID;
    float m = 1.0;
    float stain = 0.0, g = 0.5;
    if (gid != 0xFFFFFFFFu) {                             // DOOM-0179 grunge overlay present
        g = grungeFbm(gid, wUV);                         // UNORM 0..1, mixed-size marks
        m = 1.0 + (g - 0.5) * 2.0 * kGrimeStrength;       // subtle large-scale grounding
        stain = smoothstep(kStainThresh, kStainThresh - kStainEdge, g);  // distinct hard-edged mark
    }
    m -= (1.0 - ao) * kGrimeCrevice;                     // dirt darkens genuine crevices (INV-7)
    vec3 c = albedo * clamp(m, 0.35, 1.65);              // INV-6 clamp preserved
    if (stain > 0.0 && gid != 0xFFFFFFFFu) {             // distinct mottled dirt on top
        float sel = (n.z > 0.6) ? gooSelector(gid, wUV) : -1.0;
        c = mix(c, stainColour(gid, wUV, n, albedo, sel), stain * kStainOpacity);
        // DOOM-0183 L5: report the GREEN-GOO puddle coverage using the SAME floor + goo-selector
        // test stainColour uses, so the mode block can wet those puddles (§4.6). Up-facing floor
        // goo only — brown dirt on a wall is not wet. (albedo is already non-green: early return.)
        if (n.z > 0.6 && sel < kGooChance) gooWet = stain;
    }
    return c;
}
