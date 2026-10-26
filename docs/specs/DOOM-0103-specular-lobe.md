<!-- ants-spec-format: 1 -->
# DOOM-0103 — Let light gleam off surfaces: a specular lobe in both of Solid's views

**Status:** spec draft (2026-10-26).
**Kind:** feature.
**Source:** ROADMAP DOOM-0103 (user request 2026-06-29; joint design with
DOOM-0273 chosen by the user 2026-10-26, after the DOOM-0511 play-test).
**Blocked by:** DOOM-0273 (`docs/specs/DOOM-0273-solid-surface-detail.md`),
for roughness, the surface normal and the reserved setting lanes.
**Pairs with:** DOOM-0273.

**Layman:** Today light in the ray-traced view spreads over every surface like
chalk and never shines. After this, a glowing wall or a lamp leaves a soft
gleam on the floor and on the edges of stone, which moves as you walk, so a
red light makes a dark room read red.

## Contents

1. Goal
2. Where this sits
3. Scope decisions
4. Design
5. Data & resources
6. Performance budget
7. Build order
8. Invariants
9. Alternatives considered (and rejected)
10. Open questions
11. What checks this
12. Cross-doc impact
13. Cold-eyes loop log

## 1. Goal

Surfaces reflect direct light as a glossy highlight as well as a matte wash.
In the ray-traced view, the static light surfaces, the sprite lights, the
flashlight and the muzzle flash each leave a GGX highlight, shadowed by the
rays the view already casts. In the rasterised view, the point lights and the
flashlight leave a cheap highlight. Roughness comes from DOOM-0273's surface
map. A highlight keeps its light's colour. One setting turns it off, and then
every frame is as it was.

## 2. Where this sits

1. **Light is matte everywhere.**
   - The static loop in `shadeSurface` (`pt_common.glsl`) adds
     `albedo * (1.0 / PI) * c` per sample, clamped by `FIREFLY_MAX`.
   - `flashlightDelta` and `muzzleFlashDelta` (`pathtrace.comp`) are
     Lambert too.
   - The GI probes (`giIrradiance`) carry irradiance only.
   - `mesh.frag` has no specular term.
2. **The only gleam is the liquid sheen.** `wetSheen` (`pathtrace.comp`,
   DOOM-0183) is a Blinn-Phong term, `kWetSheenPower` 48, for the flashlight
   and muzzle flash only, on flagged liquids and goo puddles. DOOM-0183 INV-3
   says "no GGX/VNDF/MIS lobe (that is DOOM-0103)".
3. **The denoiser assumes matte light.** `gillum` holds illumination divided
   by albedo, and `svgf_composite.comp` rebuilds `albedo * illum` plus the
   emission. `svgfReprojectPrev` (`formulas/svgf_shared.glsl`) reprojects
   by surface position, which is wrong for a highlight that moves with the
   view. The à-trous passes blur `gillum` spatially. `primaryHitAddTerms`
   records the same problem for the sheen: in mode 6 the composite tints it
   by albedo, and "a neutral wet surface would need a separate full-res
   specular channel (a DOOM-0103 follow-up)".
4. **The static NEE samples half the pixels.** Mode 6 traces NEE on one
   pixel of each 2x2 block, `NEE_SAMPLES` per pixel, each with one shadow
   ray from `sampleEmitter`. A specular term evaluated on those samples needs
   no extra ray.
5. **The formulas exist with fixed parameters.** `formulas/formulas.glsl`,
   the Vestige Formula Workbench export, has `ggxDistribution`,
   `schlickGeometry`, `fresnelSchlick` and `ggxVndfPdf`. Each has its
   roughness or F0 baked in: alpha 0.25, k 0.125, F0 0.02. DOOM-0009 §5
   (INV-7) requires every shading curve to come from that export.
   `bloomKneeQuadratic` shows the export can take parameters.
6. **The rasterised view's lights include the light walls.**
   `RebuildStaticPointLightCache` (`r_vulkan.cpp`) packs the static emitters,
   REDWALL1 among them, into each subsector's nearest-16 list (centroid, Le,
   reach). `mesh.frag` loops over that list with Lambert `ndl`.

**Files this touches:** `shaders/formulas/formulas.glsl` (regenerated),
`shaders/formulas/specular.glsl` (new), `shaders/pt_common.glsl`,
`shaders/pathtrace.comp`, `shaders/bake.comp`, `shaders/svgf_temporal.comp`,
`shaders/svgf_composite.comp`, `shaders/mesh.frag`, `r_vulkan.cpp`,
`m_misc.c`, `m_menu.c`, `shaders/formulas/README.md`,
`tests/specular_test.cpp` (new), `tests/shader_mirror_test.cpp`, `Makefile`.
All under `linuxdoom-1.10/`.

## 3. Scope decisions

- **The tiers, in the user's words on 2026-10-26.**
  - Solid's ray-traced view "must be rt but can use some cheaper methods to
    help keep performance up. But rt is preferred."
  - The rasterised views get "nice bells and whistles but done in a cheap /
    fake way".
  - In Ultra's ray-traced view a fake is fine where "a person [cannot tell]
    the difference between rt and the cheap / fake way".

  So the traced highlight is ray traced, but it reuses the shadow rays the
  view already casts and adds none. The rasterised highlight is Blinn-Phong
  with no shadow.
- **Direct light only.** Reflections of the room are DOOM-0104. The GI
  probes stay matte.
- **Dielectric only.** Every surface takes F0 0.04. Metal needs a metallic
  map, and no texture has one in Solid. Deferred, not queued.
- **Liquids keep their sheen.** Where `MatCtrl.flags` marks a liquid, the
  lobe is skipped, so DOOM-0183's sheen is not doubled and its INV-3 holds.
- **The highlight keeps the light's colour.** It must not repeat
  DOOM-0511's white-out (INV-5).
- **Out of scope:** the GI bake, the sky, Classic, metal, reflections.

## 4. Design

### 4.1 The formulas

The Vestige session regenerates `formulas.glsl` with parameterised versions,
because DOOM-0009 INV-7 forbids hand-written curves:

```glsl
float ggxDistributionA(float NdotH, float alpha);
float smithGgxCorrelated(float NdotV, float NdotL, float alpha);  // visibility V
float fresnelSchlickF0(float cosTheta, float f0);
```

The names are proposals; the Workbench picks the final ones. The existing
fixed versions stay, because something may call them.

`formulas/specular.glsl` (new, hand-written, as `shown_emission.glsl` is)
combines them:

```glsl
// Specular radiance factor for one light direction. Multiply by the light's
// radiance and NdotL. Returns 0 when v is zero (the bake).
// roughness is DOOM-0273's perceptual roughness.
vec3 specBrdf(vec3 n, vec3 v, vec3 l, float roughness);
// False when the setting is off or the material is flagged a liquid.
// Every caller of specBrdf checks it first.
bool specOn(uint matFlags);
const float kSpecF0       = 0.04;
const float kSpecRoughMin = 0.35;   // floor: no tight, sparkly lobes on derived normals
```

`alpha = max(roughness, kSpecRoughMin)^2`. The floor keeps the lobe wide
enough that six samples and a short history resolve it, and stops normals
made from luma from sparkling.

### 4.2 The traced view

**Where it is evaluated.**

- **Static NEE (`shadeSurface`).** It gains the view direction and the
  roughness. Each sample adds `specBrdf(n, v, wi, r) * c` to a separate
  specular sum, alongside today's matte sum. It uses the same `wi` and the
  same shadow ray. Each sum is clamped per channel by `FIREFLY_MAX`
  separately.
- **Sprite lights (RIS).** The target stays the matte one, so which light is
  chosen does not change. The survivor adds its specular term with the same
  weight.
- **Flashlight and muzzle flash.** `flashlightDelta` and `muzzleFlashDelta`
  each return a specular part beside today's matte part, using the same
  shadow ray.
- **The bake.** `bake.comp` calls `shadeSurface` with a zero view
  direction, and `specBrdf` returns 0 for it. The bake stays view-independent
  (DOOM-0183 INV-8).
- **The verify path (mode 5).** `directNEEVerify` and `directAllLights` add
  the same `specBrdf` term, so `-rtverify` (DOOM-0009 INV-6) covers the lobe.
  An estimator that disagreed with its brute-force reference would fail it.

**The specular channel.**

- `pathtrace.comp` writes the specular sum to a new G-buffer image, `gspec`
  (rgba16f, set 2), at the pixels that trace NEE. It is **not** divided by
  albedo: a dielectric highlight is the light's colour, not the surface's.
- `svgf_temporal.comp` fills the other three pixels of each 2x2 block with
  the same joint-bilateral upsample it uses for `gillum`.
- It then blends `gspec` with its own history, at a faster rate than
  `gillum` (`kSpecHistoryAlpha`, starting at 0.2, set from the Workbench's
  `temporalAlpha` family). Disocclusion and the anti-ghost gradient test
  reject history exactly as they do for `gillum`.
- `gspec` does not go through the à-trous passes.
- `svgf_composite.comp` adds it: `L = albedo * illum + spec + shownEmission(…)`.
- Mode 4 (no denoiser) adds the specular sum straight to the pixel, in the
  same order. Modes 4 and 6 keep the agreement DOOM-0439 set.

### 4.3 The rasterised view (`mesh.frag`)

Inside the existing point-light loop and the flashlight block:

```glsl
float sp = pow(max(dot(N, normalize(L + V)), 0.0), specExponent(r));
direct += sp * kRasterSpecStrength * lightColour * att;   // same att and reach as ndl
```

- `N` is DOOM-0273's surface normal, and `r` its roughness.
- `specExponent` maps roughness to a Blinn-Phong exponent, so a rough
  floor's highlight is broad, the same way GGX makes it broad. It lives in
  `formulas/specular.glsl`.
- The term goes into `direct`, never `ambient`, so SSAO does not darken it
  and the raster bloom treats it as direct light (DOOM-0331 INV-4).
- The term is gated on the setting alone. The rasterised view has no liquid
  sheen (DOOM-0183 INV-5) and no `MatCtrl`, so liquids get the highlight
  there too.
- There is no shadow on the raster highlight, as the point lights have none.
  The flashlight's highlight takes its existing shadow factor.
- The `SINGLE_TARGET` weapon overlay is unchanged.

### 4.4 The setting

- **Config key `specular`**, 0 = off, 1 = on, default 1. It sits beside
  `surface_detail`, with a menu row in the same list.
- **Traced view:** `misc7.y`, which DOOM-0273 reserves.
- **Rasterised view:** `ShadowUbo.flags.y`, which DOOM-0273 reserves.

### 4.5 Failure modes

- **A highlight that lags the view.** History is reprojected by surface
  position, so a moving highlight trails. The faster blend and the anti-ghost
  test limit the trail. If the user can see it, the next step is a
  reflected-direction reprojection, a later item, not queued.
- **Noise.** Six samples on a wide lobe are noisy at half resolution. The
  roughness floor and the temporal blend are the defence, and the user
  judges the result at B4. Raising the floor is the first remedy.
- **White-out.** A bright light's highlight goes through the same tone map
  that turned DOOM-0511's panels white. The specular sum is clamped per
  channel, so a red light's highlight stays red. INV-5 checks it on the test
  scene.
- **Bloom.** A floor highlight above the bloom threshold blooms. That is
  expected of direct light (DOOM-0331). No bloom constant changes.
- **Liquids in Solid.** `FlagLiquidFlats` runs only for Ultra (DOOM-0183
  INV-5), so in Solid a nukage flat is not flagged and gets the lobe instead
  of the sheen. Accepted: it then gleams in both tiers, through different
  terms.

## 5. Data & resources

- **One G-buffer image,** `gspec`, rgba16f at the traced render size. Its
  history is a second image of the same size, for the temporal blend.
- **Config:** one new key, `specular` (§4.4).
- **Push constants and `ShadowUbo`:** no growth. The lanes DOOM-0273 adds
  are used.
- **Formulas:** a regenerated `formulas.glsl` (§4.1).
- No new file format, save-game field, command-line flag or dependency.

## 6. Performance budget

Measured with the `\` profiler at the same render scale, on the RX 6600,
before and after, with `specular` 1 and `surface_detail` 1:

- **Traced view, E1M1 start, 50% scale:** the megakernel grows by at most
  0.8 ms and the temporal pass by at most 0.3 ms. That is this feature's
  share under DOOM-0279.
- **Rasterised view, DOOM-0170 reference (1920x1080, 100%, E1M1 start):**
  the scene pass (`profMs[1]`) grows by at most 0.3 ms, and the frame rate
  stays at or above 60 (DOOM-0170 INV-2).

## 7. Build order

Each step ends with `make DEV=1` and `make test` green. DOOM-0273's B1 to B4
come first.

- **B1 — the formulas.** The Vestige session adds the parameterised exports;
  `formulas.glsl` is regenerated; `specular.glsl` is written.
  *Verify:* INV-1 passes, and fails against a stand-in that ignores alpha.
- **B2 — the setting and the channel.** §4.4, `gspec`, its history and the
  composite add, with the specular sum still zero. *Verify:* INV-4's captures
  match.
- **B3 — the traced lobe.** §4.2's evaluation sites. *Verify:* `-rtverify`
  passes; INV-2 and INV-3's scrapes pass and fail against mutated shaders.
- **B4 — the look.** The user plays the test scene and E1M1 with the setting
  on and off. *Verify:* INV-5's measurement; the user's verdict on noise and
  trailing (§4.5).
- **B5 — the rasterised view.** §4.3. *Verify:* INV-6's scrape; §6's raster
  measurement.
- **B6 — closing checks.** INV-4's captures on the finished build, §6's
  traced measurement, the §12 edits.

**Test scene:** E2M1, `-warp 2 1 -skill 3 -warpto 424 382 352`, the corridor
of STONE2 walls and FLOOR5_2 floor leading to the REDWALL1 cross
(`dev-shots/shot-0026.png` is the before).

## 8. Invariants

- **INV-1** — `specBrdf` follows the GGX model with the given roughness, and
  is 0 when the view direction is zero. `specOn` is false when the setting is
  off or the material is flagged a liquid, and every call of `specBrdf` sits
  behind it.
  *Test:* `tests/specular_test.cpp` evaluates a C++ mirror of `specBrdf` at
  fixed angles and roughnesses against values computed from the published
  GGX, Smith and Schlick expressions, and checks the zero-view case and both
  `specOn` cases. It also scrapes `pt_common.glsl` and `pathtrace.comp`:
  every `specBrdf(` call is inside a block guarded by `specOn(`.
  `tests/shader_mirror_test.cpp` checks the mirror's constants and the
  liquid flag bits against `specular.glsl` and `rb_materials.h`.
  *Breaks when:* roughness is ignored (the old fixed alpha 0.25), the floor
  is dropped, a zero case returns light, or a call site skips `specOn` and a
  liquid gets both lobe and sheen.

- **INV-2** — The bake never adds specular light.
  *Test:* `tests/specular_test.cpp` scrapes `bake.comp`: every
  `shadeSurface` call passes `vec3(0.0)` as the view direction, and
  `specBrdf(` does not appear.
  *Breaks when:* the bake passes a real view direction, which bakes one
  camera's highlights into the probes.

- **INV-3** — The specular channel is never divided by albedo, and it skips
  the à-trous passes.
  *Test:* `tests/specular_test.cpp` scrapes the shaders: `gspec` is written
  in `pathtrace.comp` with no division by `albedo`, is read in
  `svgf_composite.comp` without multiplying by `albedo`, and appears in no
  `svgf_atrous.comp` binding.
  *Breaks when:* the highlight takes the surface's colour, so a red light on
  a brown floor gleams brown, or the à-trous passes blur it away.

- **INV-4** — With `specular` 0, every frame in both views matches the build
  before this change, with `surface_detail` set either way.
  *Test:* `scripts/ab_capture.sh`, old build against new, at E1M1's start and
  the test scene, in each view, both `surface_detail` values. Each pair
  differs by no more than a same-build control pair.
  *Breaks when:* a site adds the specular term without checking the setting,
  or the composite adds an uninitialised `gspec`.

- **INV-5** — A red light's highlight stays red. On the test scene, in
  Solid's traced view, the mean colour of the floor region in front of the
  cross has a red channel at least four times its green and blue, with
  `specular` on.
  *Test:* manual. Capture with `scratch/doom0511/cap.sh` (Solid,
  `-rtview 6`) and measure the region's mean, as DOOM-0511's notes do.
  *Breaks when:* the highlight bypasses the per-channel clamp and the tone map
  turns it white.

- **INV-6** — In the rasterised view the highlight goes into `direct`, uses
  the surface roughness, and is absent from the `SINGLE_TARGET` build.
  *Test:* `tests/specular_test.cpp` scrapes `mesh.frag`: the Blinn-Phong term
  is added to `direct`, `ambient` is not assigned in that block, its exponent
  comes from `specExponent(`, and the `SINGLE_TARGET` build has no such term.
  *Breaks when:* the term goes into `ambient`, or the weapon overlay gleams.

- **INV-7** — Each formula has one copy: `specBrdf` and `specExponent` are
  defined only in `shaders/formulas/specular.glsl`.
  *Test:* `grep -rlE "^(vec3 specBrdf|float specExponent)\(" linuxdoom-1.10/shaders`
  → `linuxdoom-1.10/shaders/formulas/specular.glsl`, alone.
  *Breaks when:* a shader pastes its own copy.

## 9. Alternatives considered (and rejected)

- **Extra rays sampled from the GGX lobe, with MIS.** The full method from
  DOOM-0009 §4.4. It costs more rays per pixel, and the user asked Solid's
  traced view to use cheaper methods where it can (§3). Sampling the lights
  and reusing their shadow rays is ray traced and adds none.
- **Fold specular into `gillum`.** It would have to be divided by albedo,
  which fails on dark texels, and the à-trous passes would blur it into a
  smear.
- **An analytic area-light highlight with no shadow (representative point or
  LTC).** Free of noise, but a highlight would show through walls. That is
  the kind of difference a player can see.
- **Replace `wetSheen` with the lobe on liquids.** It changes DOOM-0183's
  INV-3, and the liquid look was signed off. Revisit when the lobe has been
  played.
- **Hand-write the GGX terms.** DOOM-0009 INV-7 requires Workbench exports.

## 10. Open questions

- **The constants** `kSpecRoughMin`, `kSpecHistoryAlpha` and
  `kRasterSpecStrength`, and the roughness-to-exponent map, are look calls.
  They are tuned with the user at B4 and B5.
- **The Workbench names and signatures** (§4.1) are the Vestige session's to
  settle. The request is sent before B1.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/specular_test.cpp`, `tests/shader_mirror_test.cpp` |
| INV-2 | `tests/specular_test.cpp` (scrape) |
| INV-3 | `tests/specular_test.cpp` (scrape) |
| INV-4 | **manual** — the §8 capture recipe, run at B2 and B6 |
| INV-5 | **manual** — the §8 region measurement, run at B4 |
| INV-6 | `tests/specular_test.cpp` (scrape) |
| INV-7 | the §8 grep, run at B6; **nothing** stops a later copy |
| DOOM-0009 INV-6 | `-rtverify`, at B3 |
| §6 budgets | **manual** — the profiler, at B5 and B6 |

Each scrape is run once against a mutated shader to see it fail. Rebuild with
`make test` before trusting either result: `make` does not rebuild tests.

## 12. Cross-doc impact

- `docs/specs/DOOM-0273-solid-surface-detail.md` — supplies the roughness,
  normal and lanes. No clause changes.
- `docs/specs/DOOM-0183-glowing-wet-liquid.md` — no clause changes: liquids
  skip the lobe, so INV-3 holds. Its INV-8 holds through INV-2 here.
- `docs/specs/DOOM-0009-path-tracer.md` — no clause changes. §4.4's "GGX/VNDF
  specular gated to measured need" is met by direct-light evaluation without
  lobe sampling, so "NEE — no MIS" still holds.
- `shaders/formulas/README.md` — the new include and the new exports.
- `CHANGELOG.md` — an Added entry when this ships.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0103-specular-lobe-loop-log.md`.
