# DOOM-0181 — De-tiled, grimy Ultra surfaces Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Kill the within-wall/floor texture repetition on HD surfaces in Ultra's ray-traced view, and add a filthy "monster-overrun" grime layer — by wrapping each HD map fetch in a world-keyed stochastic de-tiling blend and enriching `applyGrime`.

**Architecture:** Five layers built and play-tested in order, all inside `pathtrace.comp` (modes 4 + 6) with a small host change in `r_vulkan.cpp`. **(L1)** a `detile()` blend on albedo — world-space cell hash → per-cell UV offset + horizontal mirror → Inigo-Quilez 4-corner blend — plus hoisting `hitP` and a `misc5.y` on/off dial. **(L2)** the same transform on normal + AO. **(L3)** height + POM run in the de-tiled coordinate. **(L4)** the filth layer (crevice-pooled, tinted, darkened grime). **(L5)** the `misc5.y` quality dial (off/2-tap/4-tap) + a measured ≤5% perf gate.

**Tech Stack:** GLSL (compute path tracer, `glslc` → SPIR-V → embedded header via `xxd`), C++23 (Vulkan back-end `r_vulkan.cpp`), Vulkan 1.2 + `VK_KHR_ray_query`. Build: `make -C linuxdoom-1.10`. Regression: `make -C linuxdoom-1.10 test`. RT math: `linuxxdoom -rtverify`.

## Global Constraints

*(Copied from `docs/specs/DOOM-0181-detile-grime.md`. Every task's requirements implicitly include these.)*

- **Ultra RT view only** — `pathtrace.comp` modes 4 (NEE display) + 6 (denoised play). Classic, the raster stack (Solid, **and Ultra with RT off**), and all paletted (non-`usePBR`) surfaces MUST stay **byte-identical** (INV-5, INV-8). Verified by `make test` (rb_image/rb_materials + regression) staying green and paletted surfaces unchanged in play-test.
- **HD (`usePBR`) surfaces only** — everything is gated on `mc.usePBR != 0u`. Sprites never carry `usePBR` materials, so the same gate excludes them.
- **Transform set = {sub-tile offset, horizontal mirror} only** — NO rotation, NO vertical flip (INV-1). DOOM wall textures are vertically oriented.
- **World-keyed** (INV-4) — the per-cell hash is seeded by the dominant-axis world projection of `hitP`, so identical texture-space cells on different walls get different seeds.
- **Map registration** (INV-3, final/L3+ state) — albedo, normal, AO, height share ONE per-cell transform. On a horizontal mirror, the sampled normal's tangent-space X **and** the POM march's tangent-space X are negated in lockstep (INV-2).
- **`applyGrime` stays clamped** (INV-6) — filth darkens/tints within the existing `clamp(m, 0.35, 1.65)` (`pathtrace.comp:445`); never brightens beyond that ceiling.
- **`-rtverify` unaffected** (INV-9) — no push-constant layout change; `misc5` already exists as padding on the mode-5 verify struct.
- **Perf gate (L5 only)** — 4-tap must add ≤ 5% to frame time vs the measured RT-on baseline (avg `[cpu_profile]` present-total, ms, over a ~10s green-goo-room walk, same path). L1–L4 are visual play-test only. The 60 FPS floor (DOOM-0012, 💭) is advisory, not this feature's gate.

## Verification model (read once)

This is a shader feature — there is no GLSL unit test. Each task's "verify" steps are:
1. **Build:** `make -C linuxdoom-1.10 -j"$(nproc)"` — compiles shaders (`glslc`) and the engine. Must succeed with no new warnings.
2. **Regression:** `make -C linuxdoom-1.10 test` — must stay green (proves Classic/Solid/paletted math is untouched; INV-5/INV-8).
3. **RT math (L1 + L5):** `linuxxdoom -rtverify` headless must stay green (INV-9). (Not in `make test`.)
4. **Play-test:** the user runs the game in Ultra + RT-on (`~` key toggles RT) on E1M1 and confirms the layer's visual acceptance criterion. **STOP and get user acceptance before proceeding to the next task.**

Shader-source constants live near the existing grime consts (`pathtrace.comp:107-108`, `kGrimeWorldScale`/`kGrimeStrength`).

---

### Task 1 (L1): De-tile albedo — the `detile()` blend, `hitP` hoist, `misc5.y` on/off dial

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` — add consts + `hash3`/`detile` helpers; hoist `hitP` and wrap the albedo fetch in modes 4 (~L637-649) and 6 (~L762-784).
- Modify: `linuxdoom-1.10/r_vulkan.cpp` — populate `pc.misc5[1]` (the `misc5.y` de-tile dial) next to the existing `pc.misc5[0]` grime-id line (~L6398).

**Interfaces:**
- Consumes: existing `hdTex[]`, `MatCtrl mc`, `hdBaseUV(id, hitUV, uvScale)`, `pcgHash(uint)`/`rnd(inout uint)` from `pt_common.glsl`, the dominant-axis projection idiom from `applyGrime` (`pathtrace.comp:440-442`).
- Produces: `vec3 hash3(ivec2 cell)`, `vec2 detileWorldUV(vec3 hitP, vec3 n)`, `vec3 detile(uint mapIdx, vec2 sUV, vec2 g)` (where `g = w/kDetileWorldCell`), and `bool detileOn()` — used by L2/L3.

- [ ] **Step 1: Add constants + hash + world-projection helpers.** In `pathtrace.comp`, after the grime consts (`kGrimeStrength`, ~L108) add:

```glsl
// DOOM-0181: stochastic de-tiling (world-keyed offset + mirror + 4-corner blend).
const float kDetileWorldCell = 96.0;   // world units per variation cell (§10 Q2 tunable)
const float kDetileOffsetMag = 0.5;    // sub-tile offset magnitude, in tiles (±)
const float kDetileMirrorProb = 0.5;   // P(a cell is horizontally mirrored)

// vec3 hash of an integer cell, built on the existing pcgHash PRNG (do not add a new algorithm).
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
```

- [ ] **Step 2: Add the `detile()` 4-corner blend + the dial reader.** Add after `hash3`/`detileWorldUV`:

```glsl
// misc5.y: 0 = off, 1 = 2-tap, 2 = 4-tap; any other value = off.
bool detileOn()  { return pc.misc5.y == 1u || pc.misc5.y == 2u; }
int  detileTaps(){ return pc.misc5.y == 2u ? 4 : (pc.misc5.y == 1u ? 2 : 0); }

// Per-cell transform applied to the sampling coordinate sUV: signed sub-tile offset + optional
// horizontal mirror (flip U within the cell). Returns the transformed UV for cell `c`.
vec2 detileCellUV(vec2 sUV, ivec2 c) {
    vec3 h = hash3(c);
    vec2 off = (h.xy - 0.5) * 2.0 * kDetileOffsetMag;
    vec2 uv = sUV + off;
    if (h.z < kDetileMirrorProb) uv.x = -uv.x;   // horizontal mirror only (INV-1)
    return uv;
}

// De-tiled sample of HD map `mapIdx` at surface UV `sUV`, world-cell coord `g = w/kDetileWorldCell`.
// Inigo-Quilez 4-corner blend: sample the (up to 4) neighbouring cells' transformed UVs and blend
// by the smooth fractional position. 2-tap mode blends the two nearest cells along the larger axis.
vec3 detile(uint mapIdx, vec2 sUV, vec2 g) {
    ivec2 cell = ivec2(floor(g));
    vec2  f    = fract(g);
    vec2  b    = smoothstep(vec2(0.25), vec2(0.75), f);
    vec3 s00 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(0,0))).rgb;
    vec3 s10 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(1,0))).rgb;
    if (detileTaps() < 4) {                       // 2-tap: blend the two nearest cells on X only
        return mix(s00, s10, b.x);
    }
    vec3 s01 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(0,1))).rgb;
    vec3 s11 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(1,1))).rgb;
    return mix(mix(s00, s10, b.x), mix(s01, s11, b.x), b.y);
}
```

- [ ] **Step 3: Hoist `hitP` above the HD block and wrap the albedo fetch — mode 4.** In `pathtrace.comp` mode 4 (~L637-649), move the `tHit`/`hitP` computation to *before* `hdBaseUV`, and replace the albedo fetch. The block becomes:

```glsl
            MatCtrl mc  = ctrl[id];
            vec2    sUV = hitUV;
            float   tHit = rayQueryGetIntersectionTEXT(rq, true);   // HOISTED (was after hdAlbedo)
            vec3    hitP = origin + dir * tHit;                     // HOISTED
            if (mc.usePBR != 0u) {
                vec2 baseUV = hdBaseUV(id, hitUV, mc.uvScale);
                hdTangentFrame(n, p0, p1, p2, uv0, uv1, uv2, T, B);
                sUV = hdParallaxUV(mc, baseUV, T, B, n, dir);
            }
            vec3 albedo;
            if (mc.usePBR != 0u && mc.maps[0] >= 0 && detileOn()) {
                vec2 g = detileWorldUV(hitP, n) / kDetileWorldCell;
                albedo = detile(uint(mc.maps[0]), sUV, g);          // DOOM-0181 L1: de-tiled albedo
            } else {
                albedo = hdAlbedo(mc, id, hitUV, sUV);              // unchanged path
            }
            if (mc.usePBR != 0u) albedo = applyGrime(albedo, hitP, n);
```

> Note: `hdAlbedo` returns the sRGB→linear-decoded albedo; `detile` samples the same `hdTex[]` (sRGB view) so it is also linear — consistent. The `mc.maps[0] >= 0` guard keeps the paletted/no-albedo case on the original path.

- [ ] **Step 4: Same hoist + wrap — mode 6.** In mode 6 (~L762-784) apply the identical change: hoist `tHit`/`hitP` above `hdBaseUV`, and wrap the albedo fetch with the same `detileOn()` branch. (Mode 6's surrounding variables are the same names; copy Step 3's albedo block verbatim.)

- [ ] **Step 5: Host — populate the `misc5.y` dial.** In `r_vulkan.cpp`, right after the existing `pc.misc5[0] = ...` grime line (~L6398), add:

```cpp
    // DOOM-0181: de-tile quality dial. 0=off,1=2-tap,2=4-tap. Default 2 (4-tap) on the HD set,
    // 0 when no HD materials are loaded (nothing to de-tile). Runtime-tunable later via a cvar.
    pc.misc5[1] = (g.hdBuilt && g.matNumWall + g.matNumFlat > 0) ? 2u : 0u;
```

> `g.hdBuilt` and `g.matNumWall`/`g.matNumFlat` already exist (used by the HD material path). If a name differs, grep `hdBuilt`/`matNumWall` in `r_vulkan.cpp` and use the real field.

- [ ] **Step 6: Build.** Run: `make -C linuxdoom-1.10 -j"$(nproc)"`
  Expected: clean build, no new warnings. If `glslc` errors, fix the GLSL (common: missing `nonuniformEXT`, or `pc.misc5.y` not present — confirm the `misc5` field is in the `PC` block at ~L233).

- [ ] **Step 7: Regression + RT math.** Run: `make -C linuxdoom-1.10 test` (expect all green — Classic/Solid/paletted untouched), then `cd linuxdoom-1.10 && ./linux/linuxxdoom -rtverify` headless if available (expect the mode-5 verify to pass; INV-9).

- [ ] **Step 8: Commit.**
```bash
git add linuxdoom-1.10/shaders/pathtrace.comp linuxdoom-1.10/r_vulkan.cpp
git commit -m "DOOM-0181: L1 de-tile albedo (world-keyed offset+mirror+4-corner blend)"
```

- [ ] **Step 9: PLAY-TEST (user).** User launches Ultra + RT-on on E1M1. **Acceptance:** the green-goo room wall and the start-area perimeter wall no longer read as the same tile repeated; two different same-texture walls look different. `~` toggling RT off (raster) must look unchanged. **Stop for user sign-off before Task 2.**

---

### Task 2 (L2): De-tile normal + AO (same transform, INV-2 normal-X negate)

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` — a `detileVec` variant that also reports the mirror flag per cell (for the normal-X negate), and wrap the normal + AO fetches in both modes.

**Interfaces:**
- Consumes: `detile`/`detileCellUV`/`hash3` (Task 1).
- Produces: `vec3 detileNormalTS(uint mapIdx, vec2 sUV, vec2 g)` (returns the blended *tangent-space* normal with per-cell X-negate applied), `float detileR(uint mapIdx, vec2 sUV, vec2 g)` (single-channel, for AO).

- [ ] **Step 1: Add the normal + scalar de-tile helpers.** In `pathtrace.comp` after `detile()`:

```glsl
// Scalar (.r) de-tile — for AO. Same blend as detile(), single channel.
float detileR(uint mapIdx, vec2 sUV, vec2 g) {
    ivec2 cell = ivec2(floor(g)); vec2 f = fract(g);
    vec2 b = smoothstep(vec2(0.25), vec2(0.75), f);
    float s00 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(0,0))).r;
    float s10 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(1,0))).r;
    if (detileTaps() < 4) return mix(s00, s10, b.x);
    float s01 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(0,1))).r;
    float s11 = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, cell + ivec2(1,1))).r;
    return mix(mix(s00, s10, b.x), mix(s01, s11, b.x), b.y);
}

// Tangent-space normal de-tile: unpack per cell, negate X on a mirrored cell (INV-2), then blend.
vec3 detileNormalTS(uint mapIdx, vec2 sUV, vec2 g) {
    ivec2 cell = ivec2(floor(g)); vec2 f = fract(g);
    vec2 b = smoothstep(vec2(0.25), vec2(0.75), f);
    int taps = detileTaps();
    vec3 acc = vec3(0.0); float wsum = 0.0;
    for (int i = 0; i < (taps < 4 ? 2 : 4); i++) {
        ivec2 corner = (taps < 4) ? ivec2(i, 0) : ivec2(i & 1, i >> 1);
        ivec2 c = cell + corner;
        vec3  tn = texture(hdTex[nonuniformEXT(mapIdx)], detileCellUV(sUV, c)).xyz * 2.0 - 1.0;
        if (hash3(c).z < kDetileMirrorProb) tn.x = -tn.x;   // mirror flips tangent handedness
        float wx = (corner.x == 0) ? (1.0 - b.x) : b.x;
        float wy = (taps < 4) ? 1.0 : ((corner.y == 0) ? (1.0 - b.y) : b.y);
        acc += tn * (wx * wy); wsum += wx * wy;
    }
    return acc / max(wsum, 1e-4);
}
```

- [ ] **Step 2: Wire the normal into `hdShadingNormal`, AO into `hdAO` — both modes.** Where the shading normal is computed (mode 4 uses `hdShadingNormal(mc, id, sUV, n, T, B)`), replace the tangent-space unpack with the de-tiled one when `detileOn()`. Concretely, change the shading-normal call site to:

```glsl
            vec3 shN;
            if (mc.usePBR != 0u && mc.maps[1] >= 0 && detileOn()) {
                vec2 g  = detileWorldUV(hitP, n) / kDetileWorldCell;
                vec3 tn = detileNormalTS(uint(mc.maps[1]), sUV, g);
                shN = normalize(tn.x * T + tn.y * B + tn.z * n);
            } else {
                shN = hdShadingNormal(mc, id, sUV, n, T, B);
            }
```

Replace subsequent uses of the shading normal with `shN`. For AO, at the ambient-term `hdAO(mc, id, sUV)` call (mode 4 ~L690, mode 6 ~L839), swap to:

```glsl
            float ao = (mc.usePBR != 0u && mc.maps[4] >= 0 && detileOn())
                     ? detileR(uint(mc.maps[4]), sUV, detileWorldUV(hitP, n) / kDetileWorldCell)
                     : hdAO(mc, id, sUV);
```

and use `ao` in the ambient multiply.

- [ ] **Step 3: Build + regression.** `make -C linuxdoom-1.10 -j"$(nproc)"` then `make -C linuxdoom-1.10 test` (green).

- [ ] **Step 4: Commit.**
```bash
git add linuxdoom-1.10/shaders/pathtrace.comp
git commit -m "DOOM-0181: L2 de-tile normal + AO (INV-2 normal-X negate on mirror)"
```

- [ ] **Step 5: PLAY-TEST (user).** **Acceptance:** relief (bumps) and lighting stay registered with the de-tiled albedo — a mirrored tile's bumps read correctly (not inverted), no colour-vs-relief mismatch. **Stop for user sign-off before Task 3.**

---

### Task 3 (L3): Height + POM in de-tiled space

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` — apply the per-cell transform to `baseUV` *before* the POM march, negate the march's tangent-space X on a mirrored cell, and add the border-band fallback.

**Interfaces:**
- Consumes: `detileWorldUV`, `hash3`, `detileCellUV`, `hdParallaxUV`.
- Produces: `vec2 detilePOM(MatCtrl mc, vec2 baseUV, vec3 T, vec3 B, vec3 n, vec3 dir, vec2 g)`.

- [ ] **Step 1: Add the de-tiled POM wrapper.** The march uses the pixel's single dominant cell (no per-step blend — INV/§4.4). Add:

```glsl
// De-tiled POM: transform baseUV by the pixel's dominant cell, march in that space. On a mirrored
// cell, flip the view direction's tangent-space X so relief does not invert (INV-2). Border-band
// fallback: near a cell edge, skip the march and return the de-tiled baseUV (avoids a relief seam).
vec2 detilePOM(MatCtrl mc, vec2 baseUV, vec3 T, vec3 B, vec3 n, vec3 dir, vec2 g) {
    if (mc.maps[6] < 0) return baseUV;                 // no height map: POM is a no-op anyway
    ivec2 cell = ivec2(floor(g));
    vec2  f    = fract(g);
    vec2  duv  = detileCellUV(baseUV, cell);           // single-cell de-tiled coordinate
    bool  mir  = hash3(cell).z < kDetileMirrorProb;
    // border band (start 0.1): too close to a cell edge -> skip the march, sample de-tiled baseUV
    if (min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)) < 0.1) return duv;
    vec3 mdir = dir;
    if (mir) { mdir -= 2.0 * dot(mdir, T) * T; }       // negate march tangent-X (mirror lockstep)
    return hdParallaxUV(mc, duv, T, B, n, mdir);
}
```

- [ ] **Step 2: Use `detilePOM` when de-tiling is on.** In both modes, where `sUV = hdParallaxUV(mc, baseUV, T, B, n, dir)` is computed inside the `usePBR` block, branch:

```glsl
                sUV = detileOn()
                    ? detilePOM(mc, baseUV, T, B, n, dir, detileWorldUV(hitP, n) / kDetileWorldCell)
                    : hdParallaxUV(mc, baseUV, T, B, n, dir);
```

> Because albedo/normal/AO (Tasks 1-2) already sample at `sUV` and add their own per-cell offset in `detileCellUV`, and `detilePOM` produces a `sUV` already in de-tiled space, keep the flat-map de-tile keyed on the *same* cell — they use `detileWorldUV(hitP,n)`, identical here, so registration holds (INV-3).

- [ ] **Step 3: Build + regression.** `make -C linuxdoom-1.10 -j"$(nproc)"` then `make -C linuxdoom-1.10 test` (green).

- [ ] **Step 4: Commit.**
```bash
git add linuxdoom-1.10/shaders/pathtrace.comp
git commit -m "DOOM-0181: L3 POM + height in de-tiled space (border-band fallback)"
```

- [ ] **Step 5: PLAY-TEST (user).** **Acceptance:** parallax depth agrees with the de-tiled albedo/normal; no visible relief seams at cell boundaries (if seams show, the border-band `0.1` is the tuning knob — §10 Q1). **Stop for user sign-off before Task 4.**

---

### Task 4 (L4): Filth — enrich `applyGrime` (crevice pooling + tint + darken/desaturate)

**Files:**
- Modify: `linuxdoom-1.10/shaders/pathtrace.comp` — grow `applyGrime` to take AO, add filth consts, keep crevice/tint when the overlay is absent; hoist the AO sample before the `applyGrime` call in both modes.

**Interfaces:**
- Consumes: `ao` (Task 2's de-tiled AO), the existing `applyGrime` + `misc5.x` grunge overlay.
- Produces: `vec3 applyGrime(vec3 albedo, vec3 hitP, vec3 n, float ao)` (new 4-arg signature).

- [ ] **Step 1: Add filth consts.** Near the grime consts:

```glsl
const float kGrimeCrevice = 0.6;                 // extra darkening in AO-occluded crevices
const vec3  kGrimeTint     = vec3(0.85, 0.80, 0.72);  // muted grease/rust/mould
const float kGrimeTintW    = 0.15;               // tint weight
```

- [ ] **Step 2: Rewrite `applyGrime` to the 4-arg filth form.** Replace the body of `applyGrime` (~L435-446). Keep the existing centred grunge multiply + clamp (INV-6); add crevice darkening (survives a missing overlay) + tint:

```glsl
vec3 applyGrime(vec3 albedo, vec3 hitP, vec3 n, float ao) {
    uint gid = pc.misc5.x;
    float m = 1.0;
    if (gid != 0xFFFFFFFFu) {                      // grunge overlay present: centred multiply
        vec2 an = abs(n).xy;                       // (dominant-axis projection, as before)
        vec2 wUV = detileWorldUV(hitP, n);
        float grunge = texture(hdTex[nonuniformEXT(gid)], wUV * kGrimeWorldScale).r;
        m = 1.0 + (grunge - 0.5) * 2.0 * kGrimeStrength;
    }
    // Crevice pooling — dirt collects where AO is dark. Independent of the overlay (INV-7).
    m -= (1.0 - ao) * kGrimeCrevice;
    vec3 c = albedo * clamp(m, 0.35, 1.65);        // INV-6 clamp preserved
    // Faint grimy tint, weighted by how grimy this spot is (darker m => more tint).
    float tintAmt = kGrimeTintW * clamp(1.0 - m, 0.0, 1.0);
    return mix(c, c * kGrimeTint, tintAmt);
}
```

> `detileWorldUV` (Task 1) replaces the inline dominant-axis ternary the old `applyGrime` had — same projection, DRY.

- [ ] **Step 3: Hoist AO and pass it to `applyGrime` — both modes.** The `ao` value (Task 2) is computed at the ambient term (after `applyGrime` today). Move the `ao` computation up to right after `sUV`/`hdAlbedo`, before the `applyGrime(...)` call, and change the call to the 4-arg form:

```glsl
            float ao = (mc.usePBR != 0u && mc.maps[4] >= 0 && detileOn())
                     ? detileR(uint(mc.maps[4]), sUV, detileWorldUV(hitP, n) / kDetileWorldCell)
                     : hdAO(mc, id, sUV);
            if (mc.usePBR != 0u) albedo = applyGrime(albedo, hitP, n, ao);
```

Then in the ambient term below, reuse `ao` instead of calling `hdAO` again (single fetch — §4.3).

- [ ] **Step 4: Build + regression.** `make -C linuxdoom-1.10 -j"$(nproc)"` then `make -C linuxdoom-1.10 test` (green).

- [ ] **Step 5: Commit.**
```bash
git add linuxdoom-1.10/shaders/pathtrace.comp
git commit -m "DOOM-0181: L4 filth (crevice-pooled + tinted grime), applyGrime gains AO"
```

- [ ] **Step 6: PLAY-TEST (user).** **Acceptance:** E1M1 reads as a filthy, neglected base — dirt pools in corners/recesses, surfaces are grimier and less uniform-clean, but not so dark it's muddy. Dials (`kGrimeCrevice`, `kGrimeTintW`, `kGrimeStrength`) tune lived-in↔abandoned (§10 Q3). **Stop for user sign-off before Task 5.**

---

### Task 5 (L5): Runtime dial + perf pass

**Files:**
- Modify: `linuxdoom-1.10/r_vulkan.cpp` — expose the `misc5.y` de-tile quality as a runtime toggle (cvar or existing debug-key), so off/2-tap/4-tap can be A/B'd live for the perf measurement.

**Interfaces:**
- Consumes: `pc.misc5[1]` (Task 1), the `\`-key `rb_profile` profiler.
- Produces: (perf report only — no new interface.)

- [ ] **Step 1: Make `misc5.y` runtime-switchable.** Add a small integer state `g.rb_detile` (default 2) toggled by an unused debug key, and set `pc.misc5[1] = g.hdBuilt && (g.matNumWall + g.matNumFlat > 0) ? (uint32_t)g.rb_detile : 0u;`. Follow the pattern of the existing `rb_rtdebug`/`rb_profile` toggles (grep `rb_profile` in `r_vulkan.cpp`/`i_video.c` for the key-handling idiom). Cycle 0→1→2→0.

- [ ] **Step 2: Build.** `make -C linuxdoom-1.10 -j"$(nproc)"` (clean).

- [ ] **Step 3: RT math + regression.** `make -C linuxdoom-1.10 test` (green) and `./linux/linuxxdoom -rtverify` (green — INV-9).

- [ ] **Step 4: PERF MEASURE (user + agent).** User walks a fixed ~10s path through the green-goo room in Ultra RT-on at 50% render scale with flashlight, with the `\` profiler on, twice: de-tile **off** (baseline) then **4-tap**. Record avg `[cpu_profile]` present-total (ms) each. **Gate:** 4-tap adds ≤ 5% (INV/§6). If over, the shipped default drops to 2-tap (`g.rb_detile = 1`), or a compile-time cheaper split; re-measure the fallback.

- [ ] **Step 5: Commit.**
```bash
git add linuxdoom-1.10/r_vulkan.cpp
git commit -m "DOOM-0181: L5 runtime de-tile quality dial + perf pass (<=5% at 4-tap)"
```

- [ ] **Step 6: PLAY-TEST + close-out (user).** **Acceptance:** the dial visibly steps off→2-tap→4-tap; the shipped default holds the perf gate; `-rtverify` green. On user sign-off: flip **DOOM-0181** and **DOOM-0179** to ✅ in `ROADMAP.md`, add a `CHANGELOG.md [Unreleased]` entry (de-tiled + grimy Ultra surfaces), and run `/debt-sweep`.

---

## Self-review notes

- **Spec coverage:** L1-L5 map 1:1 to spec §7; INV-1 (Task 1 mirror-only), INV-2 (Tasks 2-3 normal/march X-negate), INV-3 (Task 3 same-cell key), INV-4 (Task 1 world hash), INV-5/INV-8 (`usePBR`+`detileOn` gates, `make test`), INV-6 (Task 4 clamp kept), INV-7 (Task 4 crevice + single AO), INV-9 (`-rtverify` steps). Perf gate = Task 5. Filth missing-overlay case = Task 4 Step 2.
- **Registration caveat (INV-3):** interim L1/L2 knowingly de-tile a subset (albedo, then +normal/AO) while POM/height still samples the plain coordinate until L3 — expected per spec, called out in each play-test criterion.
- **Field-name risk:** `g.hdBuilt`, `g.matNumWall`, `g.matNumFlat`, and the `misc5[1]` populate site are cited by grep-anchor, not verified line — the implementer confirms exact names in `r_vulkan.cpp` at Task 1 Step 5 / Task 5 Step 1.
- **`texture()` in compute:** the codebase already uses `texture(hdTex[...], sUV)` in `pathtrace.comp` (LOD 0 in compute); `detile` follows that exact idiom — no derivative/mip changes.
