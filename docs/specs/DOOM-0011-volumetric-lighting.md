# DOOM-0011 — Volumetric lighting (god-rays + fog) in the ray-traced view

**Status:** **L1 implemented (uniform-haze skeleton, e7753b3); 2026-07-24 amendment
adds the fog-placement standard (§4.3a + §4.6a) — pending its own `/cold-eyes` pass
before the amended work is built.** Original design approved by the user 2026-07-21
(brainstorm), cold-eyes-converged 2026-07-23 (4 loops, below), scope-widened
2026-07-23: the volumetrics run whenever the **ray-traced path is engaged**, so they
cover **both Solid-RT and Ultra-RT**, not Ultra alone. The **rasterised** "Original"
view (Solid-raster / Ultra-raster) gets a **separate, faked** screen-space treatment
tracked as its own item (**DOOM-0238**, "match the RT look as closely as raster
allows", user 2026-07-23) — **out of scope here**. This spec is the RT technique only.

**2026-07-24 amendment (user play-test feedback on L1):** the L1 uniform haze read
wrong two ways — (1) the distant sky **mountains stayed crisp** (the march skips sky
pixels), and (2) the haze was **equally thick indoors and out**. The user asked for a
**standard** for where fog is thick vs thin. Approved direction (user 2026-07-24):
**fog follows open sky** — air that can see the sky is hazed, air under a roof is
clear — measured **per march sample** ("true volumetric", user's explicit pick over the
cheaper per-surface flag), plus **aerial-perspective fog on the sky backdrop** so the
mountains fade into haze. New design in **§4.3a** (the standard) and **§4.6a** (the sky
backdrop); build order revised in §7 (new **L1b**); INV-9/INV-10 added. This amendment
awaits its own cold-eyes convergence before L1b is built.

**Cold-eyes log** (rule 14 — looped until convergence, 2026-07-23):
- **Loop 1** (2 lanes) — CRITICAL 0 · HIGH 1 · MEDIUM 5 · LOW 4 · INFO 3, all verified
  & fixed. Headline: the fog composite mixed colour spaces at the sky/wall seam (pinned
  linear-radiance on both branches); the menu plumbing listed 2 of 6 sites; "DOOM-0042
  (emitter set)" was the wrong ID (→ DOOM-0009 buffer / DOOM-0084 static slice); the
  perf gate read in FPS not ≤ 5 % present-total; INV-7/8 named no falsifier
  (→ `-shotcompare` / by-construction).
- **Loop 2** — CRITICAL 0 · HIGH 0 · MEDIUM 3 · LOW 4 · INFO 2, all verified & fixed.
  The mode-4 composite hook was unspecified (added `pathtrace.comp:1024`); the INV-7
  `-shotcompare` falsifier was wrong — it renders RT-only, so reserved for INV-8;
  "mirror `rb_wet` exactly" was the wrong menu template for a 0..3 dial (→ `rb_detile`).
- **Loop 3** — CRITICAL 0 · HIGH 1 · MEDIUM 2 · LOW 4 · INFO 1, all verified & fixed.
  `rb_fog`'s shipped default conflicted with DOOM-0208's canonical-config pin
  (reconciled: default `=1`, golden re-blessed with fog, fog-off identity
  by-construction); "no new bindings" vs the new fog image (reworded); transmittance
  "RGB or scalar" vs the `RGBA16F` packing (pinned scalar; coloured absorption → Q11).
- **Loop 4** — CRITICAL 0 · HIGH 0 · MEDIUM 1 · LOW 3, all verified & fixed. One
  completeness gap — the half-res fog upsample at sky/far-depth pixels (added a
  plain-bilinear fallback at the sky sentinel); the rest polish. Reviewer verdict:
  "genuinely tight." **Converged** — no substantive finding remains.

---

This adds **single-scattering participating media** to the path tracer: light
visibly travelling *through the air* — dramatic **shafts** where a strong light
(sky through a doorway/hole, a torch in a dark room) meets darkness, **coloured fog
pooling low** in toxic/goo rooms, and a **thin global haze** on hell levels so fires
glow and soften. It reuses the existing emitter set and the SVGF half-res +
denoise machinery; it adds **one** new thing the engine does not have today — a
**directional sky term** (a "sun" vector) so sky light can form slanted shafts.

Keep the DOOM feel: stylised and a little exaggerated (the RT-DOOM look the user is
after, cf. DOOM-0193), not photoreal — and **cheap & smooth**: half-res, dithered,
denoised, staying within a handful of FPS of today (Ultra ~45 FPS, goo room ~40),
tuned on the user's RX 6600 together. Start subtle.

**Depends on:**
- **DOOM-0009** (path tracer) — the RT back-end. This adds a **view-ray march**
  over `t ∈ [0, tHit]` in `shaders/pathtrace.comp` **mode 4** (NEE display) and
  **mode 6** (denoised play), the same two display modes DOOM-0181/0183 hook, right
  after the primary hit is resolved (`tHit`/`hitP` at `pathtrace.comp:915-916`
  mode 4, `:1093-1094` mode 6). Unlike those features there is **no existing
  shading point to extend** — the march is genuinely new code between the primary
  hit and the final composite.
- **DOOM-0009 / DOOM-0084** (emitter set + static slice; **DOOM-0119** cull) — the fog's light sources are the
  **existing** static emitters: the `Emitters` device-address buffer
  (14 floats/record, `pt_common.glsl:52-56`), sliced `[0, omniStart)` = oriented
  static wall/flat lights via `omniStart = pc.misc4.y` (`r_vulkan.cpp:7400`). Fog
  scatters **only** sky + those big static lights (§4.4) — no new light system.

**Delivers / relates:**
- The **sky "sun" direction** added here (§4.4) is the first directional light in
  the engine; a future real-sky-shadows or god-rays-from-windows feature can reuse
  it. No existing item owns it.
- Complements **DOOM-0183** (glowing liquid): a goo pool now both *glows wet*
  (0183) **and** *fills its room with green fog* (this). They share the liquid
  flag (`RB_FLAG_LIQUID_NUKAGE`, `rb_materials.h:17`) but are independent layers.

**Defers (explicitly NOT in this build):**
- **The rasterised fake** — screen-space shafts + colour fog for the Solid/Ultra
  *Original* (non-RT) view. Its own item **DOOM-0238** (user 2026-07-23), because
  the technique (screen-space post-process) shares almost nothing with the traced
  march. Sequenced after this ships.
- **Classic** (1997 software renderer) — no volumetrics; the software rasteriser
  can't and the user did not ask for it.
- **Multiple scattering / true GI through the medium** — v1 is single scattering
  only (one in-scatter event per march sample). Dense-media multi-bounce is far
  future.
- **Dynamic lights in fog** — fireball/sprite lights (`[omniStart, emitCount)`),
  muzzle flash (`misc2.z`), and the flashlight (`misc2.w`) do **not** scatter
  (perf + avoids combat-strobe noise in the air). User decision 2026-07-21.
- **Per-sector authored fog volumes** — v1 derives density from the level
  (hell flag) + the primary hit (liquid flag), not hand-placed volumes (§4.5, Q3).

**Scope:** the RT path only — `pathtrace.comp` modes 4 + 6, i.e. **RT engaged**
(`rb_rtdebug` ∈ {4, 6}) in **either** the Solid or the Ultra tier. Classic, and the
raster path (Solid/Ultra with RT off), stay **byte-identical**. The offline GI bake
(`bake.comp`) is untouched — fog is a **view-ray** term and never enters the bake
(INV-6).

---

## Contents

- §1 Goal — §2 Where this sits — §3 The problem, precisely — §4 Design
  (4.1 hook · 4.2 the march · 4.3 density & colour · **4.3a open-sky exposure — the
  fog-placement standard** · 4.4 light sources & shafts · 4.5 area profiles ·
  4.6 half-res, denoise, composite · **4.6a fogging the sky backdrop**) —
  §5 Data & resources — §6 Performance budget — §7 Build order — §8 Invariants —
  §9 Alternatives considered — §10 Open questions

---

## 1. Goal

In the ray-traced view, make light **visible in the air**:

- **Shafts (§4.4)** — where sky light reaches the fog through a hole/doorway, or a
  torch lights the fog in a dark room, a visible beam forms (bright lit fog beside
  shadowed fog; the eye integrates the difference into a shaft).
- **Coloured fog by area (§4.5)** — default clear air with subtle shafts; **thick
  low green fog** in nukage/goo rooms; **thin red-tinted haze** across hell levels.
- **Pooling (§4.3)** — fog is denser near the floor, so it settles into a layer
  rather than filling rooms uniformly.
- **Colour is the effect** — the in-scattered light is tinted by both the light
  making it (warm torch, sky-toned sky) and the medium it is in (sickly green in
  goo, ember/red in hell).

Cheap and smooth, subtle by default, tuned on hardware.

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0011? |
|-----------------|----------|-----------------------|
| Classic | paletted software | No |
| Solid or Ultra, RT **off** ("Original") | raster stack | No — faked separately in **DOOM-0238** |
| Solid or Ultra, RT **on** (`rb_rtdebug` 4/6) | path tracer | **Yes** — the view-ray march |

The gate is **RT engaged** (`rb_rtdebug` ∈ {4, 6}), **not** the tier label — so
toggling RT on in Solid gets the same volumetrics as Ultra. Modes 4 (NEE display)
and 6 (denoised play) get identical treatment (kept in lockstep, as with
DOOM-0181/0183). The headless verify mode (5), the debug views (1–3), and RT-off (0)
are untouched (§8 INV-7).

## 3. The problem, precisely

Today the path tracer is a **single-primary-ray megakernel** (`pathtrace.comp:762`,
8×8 workgroup `:40`, dispatched at `r_vulkan.cpp:7468`). It shades the **first hit**
and composites; the air between camera and surface contributes **nothing**. Three
concrete gaps:

1. **There is no air-march and no shading point to extend.** DOOM-0181/0183 hooked
   an *existing* primary-hit block; volumetrics instead needs **new** code that
   marches the segment `t ∈ [0, tHit]` (camera `pc.camPos.xyz`, ray built at
   `pathtrace.comp:775-780`; `tHit`/`hitP` at `:915-916` / `:1093-1094`) *before*
   the final colour is written.
2. **There is no directional sky light.** The sky is a positional backdrop
   (TLAS custom-index 2, `pathtrace.comp:816-817`) sampled by `skyPanorama()`
   (`:731`) plus a constant ambient `SKY_COLOR = vec3(0.20,0.26,0.40)`
   (`pt_common.glsl:31`). A project-wide search finds **no** sun/light-direction
   vector anywhere. Sky *shafts* need a direction (which way the light slants), so
   this spec **adds** one (§4.4).
3. **Fog cannot ride the denoised lighting channel.** The SVGF composite
   re-multiplies the denoised illumination by surface albedo —
   `L = albedo * illum + emis * emisMask * ga.a` (`svgf_composite.comp:88`). A
   view-ray fog term has **no** surface albedo; multiplying it by the wall's albedo
   would be wrong. Fog must be a **separate channel composited *after* that
   re-modulation** (§4.6), and the sky-passthrough path (`svgf_composite.comp:66-71`)
   must also receive the fog term (fog in front of visible sky).

## 4. Design

### 4.1 Where it hooks

A new function `marchFog(ray origin, ray dir, float tHit, hitInfo)` runs in **both**
mode 4 and mode 6, **after** the primary hit and its surface colour are resolved but
**before** the value is written to the output image. `hitInfo` carries the three
primary-hit fields the march reads: **`hitP`** (world hit, for the floor reference
`hitP.z`, §4.3), the **geometric normal** (the up-facing floor test, §4.3), and the
primary-hit **material id / liquid flag** (the goo profile, §4.5). It returns two
quantities:

- **`inscatter`** (RGB) — light scattered *toward the eye* along the segment.
- **`transmittance`** (**scalar** for v1) — how much of the surface behind the fog
  survives to the eye (`exp(-∫σ dt)`). Scalar so `inscatter.rgb + transmittance` packs
  into the one `RGBA16F` fog target (§4.6); the coloured look comes from the **RGB
  `inscatter`**, not tinted absorption. Per-channel (coloured) transmittance is
  deferred — it needs a wider target (Q11).

The **compute** runs in the megakernel for both modes; the **apply**
(`outColor = surfaceColor · transmittance + inscatter`) happens **in-megakernel** for
mode 4 and in **`svgf_composite.comp`** for mode 6 — the exact sites and colour space
are pinned in §4.6. Non-RT paths never march (it lives only in the modes 4/6
megakernel). The bake never calls it (INV-6).

### 4.2 The march — single scattering, dithered, few steps

March `N` steps from the camera to the primary hit over `t ∈ [0, tHit]`:

- **Step count** `kFogSteps` (a compile-time `const`, start ~24) — fixed, not
  adaptive, for coherence and simplicity. `tHit` is clamped to a `kFogMaxDist` so a
  long sightline down a corridor does not blow the step budget (steps past
  `kFogMaxDist` contribute negligibly for the target densities).
- **Dither the start offset** per pixel (interleaved-gradient / blue-noise, reusing
  the frame counter `pc.misc3.x` on the mode-6 path) so the fixed step count does
  not band; the denoise (§4.6) then cleans the dither noise. This is the standard
  cheap-volumetrics recipe.
- At each sample point `p = origin + t·dir`:
  1. Evaluate **density** `σ(p)` (§4.3).
  2. Evaluate **in-scattered light** `Ls(p)` from the fog's sources (§4.4), each
     with a **phase function** `phase(cosθ)` weighting forward/back scatter.
  3. Accumulate `inscatter += transmittance · σ(p) · Ls(p) · dt` and then
     `transmittance *= exp(-σ(p) · dt)`.
- **Early-out** when `transmittance` falls below a small epsilon (thick fog occludes
  the rest of the march cheaply).

Phase: a **Henyey–Greenstein** phase with a mild forward bias `kFogAnisotropy`
(a `const`) — light scatters a little more toward its travel direction, which makes
sky/torch shafts read as beams rather than a flat glow. Isotropic (g=0) is the
fallback if HG reads busy (Q5).

### 4.3 Density & colour

- **Base density** `kFogBaseDensity` — a small always-on `const` so "clear air"
  still shows faint shafts (pure zero = no shafts at all). This is the "clear"
  profile.
- **Height pooling** — density scales up toward the floor:
  `σ_height = exp(-max(0, p.z − floorZ) / kFogPoolHeight)`. The floor reference
  `floorZ` for v1 is the **primary hit's** `hitP.z` when the hit faces up (a floor);
  otherwise a level-min fallback. This makes fog **settle low** without new geometry
  data (uses only `hitP`, already in hand). Its coarseness (one floor reference per
  pixel) is an accepted v1 approximation (Q3).
- **Area multiplier & tint** (§4.5) — the profile scales `σ` and sets the medium's
  **scattering tint** `mediumTint` (green in goo, red in hell, neutral in clear).
- **Colour of a shaft = light colour × medium tint.** Sky shafts inherit the sky
  tone (`SKY_COLOR` / `skyPanorama`), torch shafts inherit the emitter's `Le`
  colour; both are then multiplied by `mediumTint`. So a torch shaft in a goo room
  is warm-through-green; a sky shaft in hell is sky-through-red. Tint colours and
  strengths are compile-time `const`s (`kGooTint`, `kHellTint`, …), tuned on
  hardware toward the DOOM-0193 exaggerated look. Start subtle.

### 4.3a Open-sky exposure — the fog-placement standard

**The standard (user 2026-07-24):** *fog lives under open sky.* A pocket of air that
can see the sky carries **full** density; a pocket under a solid roof carries **little
to none**. This is the single rule for where fog is thick vs thin, and it is
DOOM-native: an open-air area is exactly a sector whose ceiling is the sky flat
(`ceilingpic == skyflatnum`) — the same signal the engine already uses to draw open-air
(`r_mesh.c:452`). Formally, density gets one more multiplier:

`σ_final = kFogBaseDensity · heightPool(§4.3) · areaMult(§4.5) · skyExposure`

where **`skyExposure ∈ [kIndoorFogScale, 1]`**: `1` under open sky, `kIndoorFogScale`
(a small `const`, `0`..~`0.1`, a look-tune — Q12) under a roof.

**How `skyExposure` is measured — per march sample ("true volumetric", user's
explicit pick 2026-07-24 over the cheaper per-surface flag).** At each sample point
`p` the march casts **one shadow ray straight up** (world `+Z`). If that ray **reaches
the sky** (TLAS custom-index 2 — the same sky test as the primary hit,
`pathtrace.comp:852-853`) **or misses all geometry**, the pocket is under open sky →
`skyExposure = 1`. If it **hits real geometry** (a ceiling) → `skyExposure =
kIndoorFogScale`. This is what makes the haze genuinely **fill the outdoor volume and
cut off at a doorway threshold** — a sample just inside the roofline sees the ceiling
above it and reads clear, so you get a wall of mist at the door, not fog leaking
indoors. It reuses the existing ray-query machinery and is the **same order of ray as
the L2 sky-visibility shaft test** (§4.4(a)) — no new buffers.

**Cost + the built-in cheap fallback (perf lever, §6).** The up-ray adds **one ray per
march sample** — at `kFogSteps`×half-res it is the dominant new cost, so L1b (§7)
measures it on hardware. If it misses the 60 FPS floor, the layer falls back **in the
same build** to a **per-surface openness bit** with near-zero cost:
- A free flag bit **`RB_MESH_OUTDOOR = 0x100`** on the per-vertex geometry `flags`
  word (bits `0x1..0x80` used, storage is a 32-bit int → `0x100` is free with no
  format change, `r_mesh.h:82-101`).
- Set at mesh-build time from `ceilingpic == skyflatnum`, beside the checks already
  there — walls in `emit_wall` (`r_mesh.c:275`), flats in `emit_subsector_caps`
  (`r_mesh.c:443-456`).
- Already carried to the march in `FogHit.matFlags` with **no** new plumbing (the raw
  flags word is passed at both call sites, `pathtrace.comp:1061` / `:1190`), so
  `marchFog` just tests `(h.matFlags & FLAG_OUTDOOR) != 0u`.

The fallback gates fog by whether the **surface you look at** is open-sky (whole-view
granularity, no doorway cutoff) instead of per-pocket — cheaper, coarser. The primary
path is the per-sample up-ray; the flag is the escape hatch and can also back a
future cheaper "Low" quality tier. The **sky backdrop** (the mountains) is open-sky by
definition → always `skyExposure = 1` (§4.6a); no up-ray is needed for a sky pixel.

### 4.4 Light sources & shafts

Fog scatters light from **two** sources only — sky and big static emitters
(user 2026-07-21). Muzzle/flashlight/sprite lights are excluded by construction
(they are push-constant deltas / the `[omniStart, emitCount)` dynamic slice, never
iterated here — INV-3).

**(a) Sky shafts — needs a new direction.** Add a **sun direction** `kSunDir`
(world-space, a compile-time `const` default for v1 — a plausible steep slant; per
-level control deferred, Q1). At a march sample, cast **one** shadow ray toward
`kSunDir`; if it reaches the sky (TLAS custom-index 2, the same test as
`pathtrace.comp:816-817`) the sample is **sky-lit** — add
`skyRadiance · phase · mediumTint`. Samples whose sun ray is blocked by geometry are
dark. The bright/dark boundary *is* the shaft (a beam through a doorway/sky-hole).
One ray per sample keeps it affordable at half-res (§4.6). **Sky shafts require sky
geometry:** on a fully enclosed level with no sky (sky tex id `misc4.w == 0xFFFFFFFF`
/ no sky mesh, `pathtrace.comp:731-733`) no sun ray can reach a sky instance, so sky
shafts vanish — only torch shafts (b) + the base/haze fog remain. Expected, not a bug.

**(b) Torch shafts — the existing static emitters.** Iterate the static slice
`k ∈ [0, omniStart)` (`omniStart = pc.misc4.y`, `r_vulkan.cpp:7400`; record layout
`pt_common.glsl:52-56`). For cost control, **do not** shadow-test every emitter at
every sample — that is `steps × emitters` rays. Instead (Q2, start cheap):
- pick the **nearest few** static emitters to the sample (distance from the record's
  centroid), and
- add each as `Le · falloff(dist) · phase · mediumTint`, with an **optional single**
  occlusion ray to the chosen emitter (start *without* occlusion — a torch glows its
  air even through a thin wall, usually acceptable and much cheaper; add occlusion
  only if light-through-wall reads wrong, Q2).

Both sources feed the same `Ls(p)` accumulation (§4.2). The sky path is the primary
shaft mechanism; torch shafts are the secondary "dark room glows around the flame"
effect.

### 4.5 Area profiles — clear / goo / hell

Three profiles select the density multiplier + `mediumTint`:

- **Clear (default).** Base density only; neutral tint. Subtle shafts, no colour.
- **Goo / toxic.** Where the **primary hit is flagged liquid nukage**
  (`RB_FLAG_LIQUID_NUKAGE = 8u`, `rb_materials.h:17`, set on the flat at
  `FlagLiquidFlats`, `r_vulkan.cpp:5910`), thicken density and set `mediumTint =
  kGooTint` (green). This is **primary-hit-keyed**: the room reads goo-foggy when
  you are looking at/across the goo. It does **not** know about goo behind you or
  around a corner (no per-sector volume in v1) — an accepted approximation (Q3);
  the honest alternative (a per-sector fog buffer) is deferred.
- **Hell.** A per-**level** flag: a **thin global haze** everywhere with
  `mediumTint = kHellTint` (faint red). Detected CPU-side and crossed to the shader
  via a **new `rb_view_t` field** (§5) → the `misc6.w` lane. **v1 default rule
  (concrete, so L4 is testable):** a level is "hell" when the DOOM-1 episode is
  Inferno (`gamemode` registered/retail **and** `gameepisode >= 3`), **or** the
  DOOM-II map is in the hell run (`gamemode` commercial **and** `gamemap >= 20`),
  **or** the level uses a fire/hell sky. So L4 is checkable — E3M1 shows haze, E1M1
  does not. Exact thresholds, density, and `kHellTint` are tuned on hardware (Q7).

Profiles compose: a goo room *on* a hell level gets both (green pool + red haze).

### 4.6 Half-res, denoise, composite

Fog is low-frequency, so compute it **cheaply and smooth it**:

- **Half-res march.** Mirror mode 6's existing even/even 2×2 half-res gate
  (`pathtrace.comp:1141`): march fog on one pixel in four, into a **new half-res fog
  target** (`inscatter.rgb` + scalar `transmittance` packed into one `RGBA16F`
  image). Mode 4 (NEE display) has **no** even/even gate and **no** SVGF upsample of
  its own, so a half-res mode-4 march would need its **own** dither + in-megakernel
  upsample; the simpler first cut is **full-res in mode 4**, half-res only in mode 6
  (Q4).
- **Denoise / upsample.** Fog **cannot** ride the SVGF illumination channel
  (albedo re-multiply, §3 gap 3). Two candidate paths (Q6): (a) a **bilateral upsample**
  of the half-res fog target guided by depth, cheapest and self-contained; (b) run
  the existing edge-aware **a-trous** passes (`r_vulkan.cpp:7545`) on the fog channel
  too. Start with (a); escalate to (b) only if the fog crawls/flickers. **At sky /
  far-depth pixels** (the `gp.w < 0.0` sentinel of the sky-passthrough branch,
  `svgf_composite.comp:66`) a depth-guided weight has no valid neighbour depth right at
  the sky/wall seam where shafts read — so there the upsample **falls back to a plain
  bilinear fetch** of the fog target (no depth guide), keeping the shaft-against-sky
  reconstruction smooth.
- **Composite — computed once, applied per-mode, always in linear radiance.**
  `marchFog` *computes* `inscatter`/`transmittance` in the megakernel for **both**
  modes; where they are *applied* differs by mode:
  - **Mode 4 (NEE display, no denoiser):** fold into `L` **in the megakernel**, before
    the mode-4 tonemap — `L = L * transmittance + inscatter`, then `colour =
    toneEncode(L)` (`pathtrace.comp:1024`).
  - **Mode 6 (denoised play):** the megakernel writes fog to the half-res fog target;
    it is applied in `svgf_composite.comp` **after** the albedo re-multiply
    `L = albedo * illum + emis * … ` (`:88`, still linear) — `L = L * transmittance +
    inscatter`, then `toneEncode(L)` (`:91`).
  - **Both fold fog in linear radiance before the tonemap.** The mode-6
    **sky-passthrough** branch (`svgf_composite.comp:66-71`) is the trap: it stores a
    **display-encoded, fullbright** sky (`clamp(sky, 0, 1)`, deliberately *not*
    tonemapped, to match the raster sky), so fog must be folded in **in the same
    linear space** — treat the sky as linear, apply `sky * transmittance + inscatter`,
    then re-clamp/encode — so it matches at the sky/wall seam where shafts read.
    Compositing the two branches (or the two modes) in *different* colour spaces is the
    failure mode to avoid (Q9).

### 4.6a Fogging the sky backdrop (aerial perspective)

**Why the mountains stay crisp today.** `marchFog` runs **only** inside the world-hit
branch (`if (committed && !isSky)`, `pathtrace.comp:850-856`); it is called at
`pathtrace.comp:1060-1064` (mode 4) / `:1189-1195` (mode 6). A primary ray that hits
the sky — a true miss, or a committed hit on the sky-backdrop instance (custom-index 2)
— funnels to the sky `else` branch (`:1274-1297`) and **never marches**: no `tHit` is
computed, and in mode 6 the half-res fog target is **not written** for that pixel (it
keeps its one-time neutral clear, `r_vulkan.cpp:2864-2876`). So the distant mountains
receive **no distance-fog** — only `skyPanorama()`'s own screen-space horizon band
(`SKY_FOG_COL` mixed by `smoothstep(0.50,0.63,suv.y)`, `pathtrace.comp:761-763`) and a
one-texel bilinear leak at the sky/wall seam. That is why the mountains read sharp.

**The fix — aerial perspective on the sky.** A sky pixel is open-sky by definition
(§4.3a), so give it the **full** fog over a fixed distance. A sky ray has no finite
`tHit`, so march (or analytically integrate — a sky ray sees constant outdoor density,
so a closed-form `inscatter`/`transmittance` over `[0, kFogMaxDist]` is exact and
avoids a second loop) toward the backdrop with `skyExposure = 1`, then fold the same
`sky = sky · transmittance + inscatter` used everywhere else:
- **Mode 6:** on the sky-passthrough branch of `svgf_composite.comp:93-104`, replacing
  the current `fetchFogBilinear` no-op fold (`:100-103`) with the sky-distance fog.
- **Mode 4:** in the megakernel sky branch, after `colour = skyPanorama(...)`
  (`pathtrace.comp:1295`), before the write.

Distant peaks fade into the haze colour; the near horizon matches the wall-line fog
because it is the **same medium**. Because the sky is always outdoors, this term needs
**no up-ray** and is cheap (a per-pixel closed form, no shadow ray). Fog-off
(`rb_fog == 0`) leaves `transmittance = 1, inscatter = 0` → `sky · 1 + 0`, so the sky
stays **byte-identical** (INV-7/INV-8). The existing `skyPanorama` `SKY_FOG_COL`
screen-space band (`:761-763`) now overlaps the real distance-fog and must be
reconciled (dial down or remove) so the horizon is not **double-hazed** — a look-tune
at L1b (Q14).

## 5. Data & resources

- **One new image — a half-res fog target** (`RGBA16F`, §4.6) — **plus its descriptor
  bindings**: a megakernel **write** target and a `svgf_composite.comp` **read** input
  (mode 6). No new SSBOs, light/emitter buffers, or vertex data — fog reuses the
  existing `Emitters` buffer + sky.
- **Push constants — the two genuinely-free lanes.** DOOM-0183 grew
  `RtPushConstants` to **240 B** (`static_assert(sizeof==240)`, `r_vulkan.cpp:7374`;
  `pcr.size = 240`, `:2355`) and consumed `misc6.x` (ripple time), `misc6.y` (wet
  toggle). The **only** free components today are **`misc6.z` and `misc6.w`**
  (`r_vulkan.cpp:7428-7429`, currently written 0). This feature uses **exactly those
  two**, needing **no** struct growth:
  - **`misc6.z` = `rb_fog` strength** — a small **uint** (0..3; `0` = off, which also
    *is* the on/off state), written/read exactly like `misc6.y = rb_wet`
    (`pc.misc6[2] = (uint)rb_fog`; the shader reads a `uint`).
  - **`misc6.w` = global haze density** — a **bit-cast float** (like `misc6.x` ripple
    time: bit-cast in on the C++ side, `uintBitsToFloat` in the shader); the
    hell-level haze from `rb_view_t` (§4.5), `0.0` on non-hell levels.
  - Everything else is a **compile-time `const`** per house convention
    (DOOM-0181/0183 §5): `kSunDir`, `kFogSteps`, `kFogBaseDensity`, `kFogMaxDist`,
    `kFogPoolHeight`, `kFogAnisotropy`, `kGooTint`, `kHellTint`, the per-source
    strengths. Only the runtime **strength** and the **per-level haze** vary at
    runtime, so only they take lanes.
  - **Budget note (INV-5):** this consumes the **last two free components** of the
    shared RT push block. Any *further* RT push value must append the final
    `misc7 uvec4` (240 → 256 B, the documented device limit; after that the block is
    full). This spec deliberately stays within the free lanes and does **not** add
    `misc7`, leaving that headroom for the future.
  - The fog lanes sit **beyond the 184-byte `-rtverify` prefix**
    (`static_assert(sizeof(RtPC)==184)`, `r_vulkan.cpp:6828`, which stops before
    `misc6`), so **`-rtverify` is unaffected** (INV-7).
- **New `rb_view_t` field for the hell flag.** `rb_view_t` (`r_mesh.h:265-273`)
  currently carries only `x,y,z,angle,extralight,skytexnum`. Add one field
  (e.g. `float hazeDensity`), computed beside `view.skytexnum = skytexture`
  (`r_backend.c:181`) from `gameepisode`/`gamemap` + the sky, and written to
  `misc6.w`. (`r_backend.c` does not reference `gameepisode`/`gamemap` today; the
  compute brings them into scope.)
- **New runtime dial `rb_fog`** — `rb_detile`-style 0..3 value, `rb_wet`-style wiring:
  `extern "C" { int rb_fog = 1; }` in `r_vulkan.cpp` (beside `rb_wet` `:1001`) — a
  **subtle "Low" on by default**, matching the on-by-default effect siblings
  (`rb_wet=1`, `rb_filth=1`, `rb_detile=2`) so atmosphere is present out of the box
  (perf-gated at L6; flip to `0` for off-by-default if review prefers — Q10); a config
  row `{"rt_fog", &rb_fog, 1}` in `m_misc.c` defaults (beside `rt_wet` `:270`); the
  value written to `pc.misc6[2]` (beside `misc6[1] = rb_wet` `:7427`). `rb_fog` is a
  small **strength** integer (0..3, `0` = off), so the menu "Strength" row and the
  on/off state share it.
- **Menu rows — place like `rb_wet` (both menus, DOOM-0206 doubled them), behave like
  `rb_detile` (a 0..3 cycle with a name table, NOT a boolean).** `rb_wet` shows as a
  row in the legacy Effects menu **and** the crisp Video menu, so the fog row goes to
  the same two menus; but a multi-value strength dial is the `rb_detile` pattern
  (`M_ChangeDetile` cycles `0..2`, drawn via `detileNames[]`), not the boolean
  `rb_wet`/`M_ChangeWet` On-Off. That is **six** edits + one string table, not two —
  adding only the menuitem arrays ships a blank/mislabelled row:
  1. **Enums:** `ef_fog` in `effects_e` and `vid_fog` in `videoitem_e`
     (`m_menu.c:501-510` / `543-565`).
  2. **Menuitem arrays:** the row in `EffectsMenu[]` (`:512-520`) and `VideoMenu[]`
     (`:567-588`), both bound to `M_ChangeFog`.
  3. **Legacy inline draw:** a label + value pair in `M_DrawEffectsMenu` keyed on
     `ef_fog`, mirroring the `"De-tile:"` + `detileNames[rb_detile]` row
     (`m_menu.c:1465`), not the boolean `"Wet liquid:"` row.
  4. **Crisp label table:** a `videoLabels[]` entry for `vid_fog` (`m_menu.c:1491`).
  5. **Crisp value switch:** a `case vid_fog:` in `M_VideoCrispValue` returning
     `fogNames[rb_fog]`, mirroring `case vid_detile:` (`m_menu.c:1558`), not the
     `"On"/"Off"` `case vid_wet:`.
  6. **Shared handler + name table:** `M_ChangeFog` mirroring `M_ChangeDetile`
     (`rb_fog = (rb_fog + 1) % 4`), used by both menus, plus a new
     `fogNames[] = {"Off","Low","Med","High"}` table (like `detileNames[]`).
  So the "Strength" presentation Off/Low/Med/High maps to `rb_fog` 0..3. Placement =
  wherever `rb_wet` sits; mechanism = the `rb_detile` multi-value pattern.
- **New hotkey.** A free key in the `i_video.c:441-475` toggle block — `;`
  (`SDLK_SEMICOLON`) is unused (`]`=de-tile, `[`=filth, `'`=wet, `~`=view cycle,
  `` ` ``=profiler are taken). Cycles `rb_fog` and prints `Volumetric fog: <level>`.

- **2026-07-24 amendment — no new runtime resource.** The open-sky standard (§4.3a)
  and the sky-backdrop fog (§4.6a) add **no push-constant lane, no SSBO, no new image**:
  `skyExposure` is measured per-sample by a ray-query up-ray (compute only), the sky
  fog is a per-pixel closed form, and `kIndoorFogScale` / `kFogMaxDist` are compile-time
  `const`s. So **INV-5 holds unchanged** — still 240 B, `misc6.z/.w` the only runtime
  lanes. The **only** optional data addition is the fallback bit **`RB_MESH_OUTDOOR =
  0x100`** on the existing per-vertex `flags` int (`r_mesh.h:82-101`, no format change),
  set at mesh-build (`r_mesh.c:275`, `:443-456`) and mirrored as a shader `const`
  `FLAG_OUTDOOR = 0x100u` beside `FLAG_FLAT`/`FLAG_EMISSIVE` (`pt_common.glsl:20-22`) —
  added **only if** L1b's perf spot-check forces the cheap path (§4.3a, §6).

## 6. Performance budget

- **Baseline & method:** the DOOM-0181/0183 §6 protocol — average the `` ` ``
  profiler present-total (ms, not FPS) over a fixed ~10 s walk of the **E1M1
  green-goo room** (a sky-hole/doorway scene too, for shafts), RT-on, 50 % render
  scale, with `rb_fog` **off** then **on** (same-walk A/B, the DOOM-0187 lesson).
- **Cost shape (measure, don't assert):** the march is `kFogSteps` samples/pixel,
  each with **one** sky shadow ray + a few emitter evaluations, at **half-res**
  (¼ the pixels) + denoise. The shadow rays are the pole; half-res + few steps +
  dither + denoise is what makes it affordable.
- **A dedicated GPU-timer slot needs pool growth.** The RT profiler pool is **full
  (8/8 slots used)** (`queryCount = 8`, `r_vulkan.cpp:1510`). A fog-pass timer
  requires bumping `queryCount` and widening the two resets (`:7300`, `:8285`) and
  the readback (`:8076-8086`) — a small, contained change made **with** the perf
  layer (L6), not silently skipped.
- **Levers held ready** (measure before cutting): the `rb_fog` **strength** dial is
  the standing perf option (though a lower strength is not automatically cheaper), plus
  reduce `kFogSteps`; drop the emitter occlusion ray (§4.4b); distance-gate the march
  (`kFogMaxDist`); make mode 4 half-res too; and — the biggest new lever —
  **swap the per-sample open-sky up-ray (§4.3a) for the near-free per-surface
  `RB_MESH_OUTDOOR` flag**, trading the doorway cutoff for whole-view granularity.
- **2026-07-24 amendment — the up-ray is a new pole; L1b spot-checks it.** The open-sky
  up-ray (§4.3a) adds **one ray per march sample** — the same order as the L2 sun ray,
  so at `kFogSteps`×half-res it roughly *doubles* the march's ray count. Because it
  lands in **L1b** (§7), that layer carries its **own hardware perf spot-check** on the
  RX 6600: if the per-sample up-ray drops Ultra below the 60 FPS floor, L1b ships the
  `RB_MESH_OUTDOOR` fallback instead (built in the same layer). The **formal ≤ 5 %
  present-total gate stays L6**; L1b's check is a go/no-go on which exposure method
  ships.
- **Gate (L6, the pass/fail):** with `rb_fog` at its shipped default, the march adds
  **≤ 5 % to present-total** (ms) vs the `rb_fog`-off RT-on baseline on the fixed
  goo-room walk — the same measurable bar DOOM-0181 held. ("Within a handful of FPS"
  is the user's informal phrasing; the **≤ 5 % present-total** figure is the actual
  test, because FPS is non-linear — a few FPS at 160 is trivial, at 40 it is > 10 %.)
  Ultra must also stay above the 60 FPS floor where it is today; the goo room's
  existing ~40 FPS is the megakernel/denoiser (per DOOM-0183 framing), and fog must
  not make it materially worse.
- **Gate reliability:** the `-shotcompare` / `-rtverify` gates leaned on here were
  made config-independent and deterministic by DOOM-0208 (2026-07-23) — the historical
  staleness/instability recorded in its ROADMAP body is *resolved*, so they are
  reliable pass/fail gates now.

## 7. Build order

Each layer is independently play-testable (renderer look is a play-test call, per
DOOM-0181/0183). L1–L5 acceptance is **human play-test**; only **L6**'s perf + verify
is objective.

| Layer | Scope | Verify | FPS-gate? |
|-------|-------|--------|-----------|
| **L1** | The march skeleton: `marchFog` over `[0,tHit]`, constant base density, **isotropic single scatter from the sky only** (no direction yet — flat sky ambient), composited via a new half-res fog target + bilateral upsample + the per-mode apply (§4.6: in-megakernel `toneEncode` for mode 4, `svgf_composite.comp:88`+sky-passthrough for mode 6). Full RGB, no colour profiles. | Air picks up a faint uniform glow; surfaces behind thick fog fade; sky still visible through fog; no NaNs; modes 4 & 6 match | no |
| **L1b** | **The fog-placement standard + the mountains** (2026-07-24 amendment, the immediate next work). Two parts: **(i) sky-backdrop aerial fog** (§4.6a) — fog sky pixels over `[0,kFogMaxDist]`, folded on the sky-passthrough branch + mode-4 sky branch, reconciling the old `SKY_FOG_COL` band (Q14); **(ii) open-sky exposure gate** (§4.3a) — per-sample up-ray sky-visibility → `skyExposure` multiplier on density, with the `RB_MESH_OUTDOOR` flag path built in as the perf fallback. | Open/sky-exposed rooms stay hazy; step under a roof and the air **clears with a mist wall at the threshold**; distant **mountains fade into haze**, not crisp; sky still recognizable; fog-off byte-identical (INV-7/8). **Plus a hardware perf spot-check** (§6): if the up-ray misses 60 FPS, ship the `RB_MESH_OUTDOOR` fallback. | spot-check |
| **L2** | **Sky shafts:** add `kSunDir` + the one-ray sky-visibility test per sample + HG phase (builds on L1b's up-ray machinery). | A doorway/sky-hole open to sky throws a visible slanted beam; closed rooms stay clear; the beam moves correctly as the camera orbits | no |
| **L3** | **Height pooling + torch shafts:** height-based density (`hitP.z` floor ref); iterate static emitters `k<omniStart` (nearest-few, no occlusion first). | Fog settles low into a floor layer; a torch in a dark room glows its surrounding air; dynamic/muzzle/flashlight do **not** scatter | no |
| **L4** | **Area profiles + colour:** goo tint via the primary-hit `RB_FLAG_LIQUID_NUKAGE`; hell haze via the new `rb_view_t` field → `misc6.w`; `mediumTint` colouring (light×medium). | Goo rooms fill green and pool low; hell levels gain a faint red haze; a torch shaft reads warm-through-green in goo; clear levels stay neutral | no |
| **L5** | **Denoise/quality pass:** dither tuning; escalate upsample→a-trous if it crawls (§4.6 Q6); phase/anisotropy tune. | Fog is smooth, not grainy or crawling, in a slow pan; shafts hold their shape | no |
| **L6** | **Runtime dial + menu + key + perf:** `rb_fog` (`rt_fog` config), both menu rows, the `;` key, the profiler-slot growth, the DOOM-0208 canonical-config pin (§8 INV-8), and the perf pass. | Toggle/strength flip cleanly off→low→high; adds **≤ 5 % present-total** vs off (§6); `-rtverify` **green**; if fog ships on-by-default (Q10) the `-shotcompare` golden is re-blessed with subtle fog, else fog-off stays byte-identical (INV-8); 60 FPS floor held | **yes** |

**Interim state (expected, not a regression):** L1 (shipped, e7753b3) is a flat
**uniform** sky-ambient glow — the user play-test flagged it as too-uniform and
mountain-less, which **L1b** fixes (open-sky gating + sky-backdrop fog). Post-L1b the
haze still has **no** shafts (the directional term arrives at L2) and **no** colour
(profiles arrive at L4) — mirroring DOOM-0183's "sheen-before-ripple" staged interim.

## 8. Invariants

- **INV-1:** Fog is **single-scattering** along the **primary view ray** only —
  one in-scatter event per march sample, no secondary bounces, no path extension.
  It is composited as `surface·transmittance + inscatter` after the primary hit.
- **INV-2:** Fog scatters light from **sky + static emitters `[0, omniStart)`
  only** (`omniStart = pc.misc4.y`). Dynamic sprite lights `[omniStart, emitCount)`,
  the muzzle flash (`misc2.z`), and the flashlight (`misc2.w`) **never** scatter.
- **INV-3:** The sky **sun direction** (`kSunDir`) is **new** — no directional light
  existed before (only positional sky + constant `SKY_COLOR`). v1 is a compile-time
  `const`; per-level control is deferred (Q1).
- **INV-4:** Fog is a **separate channel composited *after* the SVGF albedo
  re-multiply** (`svgf_composite.comp:88`) — it never rides `gillum`/`illum` (which
  is multiplied by surface albedo). `inscatter`/`transmittance` are **linear
  radiance**, folded in **before** the tonemap on **both** the surface path and the
  sky-passthrough branch (`:66-71`) in the *same* colour space, so the sky/wall seam
  matches (§4.6).
- **INV-5:** The two runtime values ride **`misc6.z` (fog strength) + `misc6.w`
  (haze density)** — the **last two free components** of the 240-byte
  `RtPushConstants`. This feature adds **no** struct growth and does **not** append
  `misc7`; the C++ struct, its `static_assert` (`r_vulkan.cpp:7374`), `pcr.size`
  (`:2355`), and the GLSL push block stay at 240 B.
- **INV-6:** The GI bake (`bake.comp`) is **untouched** — fog is a view-ray term and
  never enters the bake (which computes surface irradiance). No double-count.
- **INV-7:** Ultra **and** Solid, **RT engaged only** (`rb_rtdebug` ∈ {4, 6}).
  Classic and the raster path (RT off) are **byte-identical by construction** — fog
  lives only in the RT megakernel, so no raster/Classic code is touched. (There is no
  golden test for *this* claim: `-shotcompare` renders the Ultra-RT view only and
  cannot exercise the raster path — that gate is INV-8's, below.) The fog lanes sit
  beyond the 184-byte `-rtverify` prefix (`r_vulkan.cpp:6828`), so **`-rtverify` is
  unaffected**; the headless verify mode (5), the debug views (1–3), and RT-off (0)
  are untouched.
- **INV-8:** Every fog cost is **`rb_fog`-gated** — `rb_fog == 0` skips the march
  entirely (the branch is not taken), so the RT path with fog off is byte-identical to
  today **by construction** (like INV-7 — no golden needed). Two *distinct*
  `-shotcompare` roles, not to be conflated: **(a)** the DOOM-0208 canonical config
  pins effect toggles to their **shipped defaults** (`rb_detile=2, rb_filth=1,
  rb_wet=1`, `r_vulkan.cpp:8177`), so when fog ships it pins `rb_fog` to *its*
  shipped default (§5) and the golden is **re-blessed *with* subtle fog** — the gate
  then guards the fog *look* (exactly how DOOM-0183 re-blessed for wet). **(b)** The
  fog-*off* byte-identity is structural; if an empirical check is wanted, a temporary
  `-config` forcing `rb_fog=0` vs the pre-feature golden proves it — that is *not* the
  canonical run.

- **INV-9 (open-sky standard, 2026-07-24):** fog density is gated by **open-sky
  exposure** — `σ_final = base · heightPool · areaMult · skyExposure`, with
  `skyExposure = 1` under open sky and `kIndoorFogScale` (`const`) under a solid roof
  (§4.3a). v1 measures exposure **per march sample** via one up-ray to the sky
  (custom-index 2 / miss), the user's "true volumetric" pick; the per-surface
  `RB_MESH_OUTDOOR` flag is the cheap fallback, selected only if L1b's perf spot-check
  demands it. "Open sky" = `ceilingpic == skyflatnum`, the engine's own open-air signal.
- **INV-10 (sky-backdrop fog, 2026-07-24):** sky pixels receive **aerial-perspective
  fog** (`skyExposure = 1`) over `[0, kFogMaxDist]`, folded as `sky · transmittance +
  inscatter` on the mode-6 sky-passthrough branch (`svgf_composite.comp:93-104`) and the
  mode-4 sky branch (`pathtrace.comp:1295`), in the **same linear space** as every other
  fog fold (INV-4). Fog-off (`rb_fog == 0`) → `transmittance = 1, inscatter = 0`, so the
  sky is **byte-identical** to today (INV-7/INV-8). No up-ray and no new resource
  (INV-5) — the sky is outdoors by definition.

## 9. Alternatives considered

- **Screen-space / post-process god-rays (radial blur from a bright source).**
  Rejected for the RT path: it is the *raster fake* (now **DOOM-0238**). In the
  traced view a real march gives correct off-screen-source shafts and volumetric
  occlusion that a screen-space blur cannot.
- **Froxel (volumetric texture) fog, à la Frostbite.** Rejected for v1: a 3D froxel
  grid + its own populate/scatter passes is a much larger system. A per-pixel march
  reuses the existing megakernel + SVGF and is enough for DOOM's room scales;
  revisit only if the per-pixel march proves too noisy/expensive (Q6).
- **Fog inside the SVGF illumination channel.** Rejected — the composite
  re-multiplies by albedo (`:88`), which a view-ray term must not receive (INV-4).
- **Per-sector authored fog volumes.** Deferred: needs a per-sector attribute buffer
  the engine does not have (the seam carries only `skytexnum`). v1 derives density
  from the level (hell) + primary hit (liquid) instead (§4.5, Q3).
- **Growing the push block now (add `misc7`) for richer runtime control (sun dir,
  per-area colours as uniforms).** Rejected: v1 needs only two runtime scalars;
  colours/direction are `const`s. Keeping to `misc6.z/.w` preserves the last
  `misc7` uvec4 of headroom (INV-5).
- **Scattering dynamic + flashlight lights too.** Rejected by the user (2026-07-21):
  perf, plus fog strobing with the muzzle/flashlight would be combat noise.

## 10. Open questions

- **Q1 (sun direction):** a single world-space `const` for all levels (v1), or
  per-level/per-sky tuning? Start `const`; the shaft angle is a look-tune (L2).
- **Q2 (torch-shaft cost):** nearest-few emitters with **no** occlusion ray (cheap,
  may glow through thin walls) vs one occlusion ray each (correct, costlier). Start
  no-occlusion; add if light-through-wall reads wrong (L3).
- **Q3 (density source):** primary-hit-keyed goo density (v1, cheap, blind to
  goo behind/around corners) vs a per-sector fog buffer (correct, new plumbing).
  v1 takes the primary-hit key; revisit if the room-fill reads wrong (L4).
- **Q4 (mode-4 resolution):** march mode 4 (display) at full-res or half-res? Start
  half-res both for one path; raise mode 4 only if the display view looks soft.
- **Q5 (phase):** Henyey–Greenstein forward-bias vs isotropic — a shaft-shape tune
  (L2/L5).
- **Q6 (denoise path):** depth-guided bilateral upsample (cheap, self-contained) vs
  routing the fog channel through the a-trous passes (`:7545`). Start bilateral;
  escalate if the fog crawls (L5).
- **Q7 (hell-haze tuning):** the v1 hell rule (§4.5: Inferno E≥3 / DOOM-II map≥20 /
  fire-sky) is a concrete default so L4 is testable; the exact map thresholds, haze
  density, and `kHellTint` are tuned on hardware at L4.
- **Q8 (tonemap headroom):** bright sky shafts must read strong without clipping to
  a flat white slab under the PBR-Neutral tonemap — verify at L2/L5 (same caution as
  DOOM-0183 Q7).
- **Q9 (sky fog encode point):** the sky-passthrough branch stores a display-encoded
  fullbright sky (`svgf_composite.comp:66-71`); folding fog in linear (§4.6) means
  treating it as linear, compositing, then re-clamp/encode. Confirm that round-trip is
  a no-op for an un-fogged pixel, so a fog-off sky stays byte-identical (INV-7/INV-8)
  — a small implementation check at L1.
- **Q10 (fog on/off by default):** ship `rb_fog=1` (subtle "Low" on, matching the
  on-by-default effect siblings — the spec's current pick) vs `rb_fog=0` (off, user
  opts in). On-by-default means the DOOM-0208 golden is re-blessed *with* fog
  (§8 INV-8) — a review decision (§5).
- **Q11 (coloured absorption):** v1 uses **scalar** transmittance (§4.1) so the fog
  target stays one `RGBA16F`; per-channel transmittance (green goo darkening the
  red/blue *behind* it, not just adding green inscatter) needs a wider target —
  deferred; revisit if neutral dimming reads wrong.
- **Q12 (indoor floor, 2026-07-24):** `kIndoorFogScale = 0` (roofed air totally clear)
  vs a small nonzero (a faint indoor haze so interiors aren't dead-flat) — a look-tune
  at L1b (§4.3a).
- **Q13 (exposure method, 2026-07-24):** per-sample up-ray (true volumetric, the user's
  pick, fills volume + doorway cutoff) vs per-surface `RB_MESH_OUTDOOR` flag (near-free,
  whole-view granularity). Decided by L1b's hardware perf spot-check (§6) — up-ray if it
  holds 60 FPS, flag otherwise.
- **Q14 (double-haze, 2026-07-24):** the new sky-distance fog (§4.6a) overlaps
  `skyPanorama`'s existing screen-space `SKY_FOG_COL` band (`pathtrace.comp:761-763`).
  Reconcile at L1b — dial the old band down or remove it so the horizon isn't hazed
  twice.
- **Q15 (up-ray direction, 2026-07-24):** straight world `+Z` up (simplest, chosen) vs
  a small cone / toward `kSunDir`. Straight up can misclassify a roofed room with a
  tiny sky-hole directly overhead as "outdoors"; revisit only if that reads wrong at L1b.
