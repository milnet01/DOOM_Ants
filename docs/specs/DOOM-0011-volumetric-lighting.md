# DOOM-0011 — Volumetric lighting (god-rays + fog) in the ray-traced view

**Status:** **L1 + L1b implemented and user-play-tested** (uniform-haze skeleton
e7753b3; fog-placement standard + sky-backdrop aerial fog 1345c92 — user 2026-07-25:
"looking fantastic… covers the mountains… outside and not inside"). **A 2026-07-25
amendment retargets the look at Silent Hill 2 (§4.3b, wisps) and softens the indoor
cutoff into an outdoor-proximity seep (§4.3a amendment); the perf gate rises to
≤ 15 % (§6). Awaiting `/cold-eyes`; L1c + L1d are the next work.** Original design
approved by the user
2026-07-21
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
backdrop); build order revised in §7 (new **L1b**); INV-9/INV-10 added.

**2026-07-25 amendment (user play-test of L1b + a named art target).** L1b shipped and
reads right — the user confirmed the mountains are covered and the fog is "outside and
not inside". They then asked for two wrinkles, and named a reference: **Silent Hill 2**
(original PS2, 2001), supplying screenshots.
1. **Make the fog like SH2.** The L1b haze is *uniform*; SH2's defining quality is
   **near-white colourless fog full of billowing wisps of varying thickness, drifting
   slowly past**. Researched how SH2 actually builds it (§4.3b, sources cited there):
   distance fog **plus two animated layers at different scroll speeds and alphas**. The
   volumetric translation is **two octaves of drifting 3-D noise modulating density** —
   which, because we march a real volume, gives wisps genuine **depth** (they pass in
   front of and behind geometry) that SH2's 2-D planes could not. New **§4.3b**.
2. **Let a little fog seep into areas open to outside.** §4.3a's indoor value stops
   being a flat constant and becomes a **graded fade from the opening inward**, driven
   by a load-time flood-filled "distance to outdoor air" field (the user's pick from
   three offered options). Amendment appended to **§4.3a**.
The user also **raised the perf gate from ≤ 5 % to ≤ 15 %** of present-total (§6),
reasoning that a PS2 ran this look — a caveat on how far that comparison carries is
recorded in §6. Build order gains **L1c** + **L1d** (§7); **INV-9 amended**, **INV-11 /
INV-12** added; **Q16–Q19** added. SH2's player-reactive swirl is **deliberately not
taken** (DOOM is first-person — no body to swirl around); split out as **DOOM-0239**.

**Cold-eyes log — 2026-07-24 amendment** (rule 14 — looped until convergence; 2 lanes
= amendment-accuracy + whole-doc-coherence, each loop cold):
- **Loop 1** — CRITICAL 0 · HIGH 3 · MEDIUM 3 · LOW 5 · INFO 3 (9 fixed, 2 dismissed).
  The up-ray + L2 sun ray "reaches custom-index-2 sky" was wrong (shadow mask `0x01`
  can't hit the mask-`0x04` sky instance → detect open sky via the **miss**); the mode-6
  sky-distance fog can't run in `svgf_composite.comp` (no `pt_common` consts) → the
  megakernel writes `fogImg`, the existing fold reads it; plus a sweep of post-L1
  citation drift in the pre-existing body (`main()` 762→798, push-constant asserts,
  etc.); profiler-pool "8/8 full" corrected.
- **Loop 2** — CRITICAL 1 · HIGH 6 · MEDIUM 2 · LOW 3 · INFO 2 (10 fixed, 2 dismissed).
  INV-9 still said "custom-index 2" (reconciled to the mask/miss mechanism); "up-ray
  roughly doubles the ray count" was wrong — the shipped march does **zero** rays/sample,
  so it is the *first* ray; the L1b 60 FPS spot-check collided with the goo room's
  pre-existing ~40 FPS (pinned to a non-goo scene + added-Δ); more "Depends on"/INV-7
  citation drift; the profiler pool is in fact **all 8 slots used** (loop-1 trusted a
  stale code comment).
- **Loop 3** — CRITICAL 0 · HIGH 1 · MEDIUM 1 · LOW 6 · INFO 2 (8 fixed, 1 dismissed).
  Only citation-precision + wording left: sky-branch range `:93-104`→`:93-107`; INV-8
  pin `:8177`→`:8207`; three menu draw citations; a note that L1's composite-side gate
  rides a separate `SvgfPC.misc3.y` lane; `skyExposure` is binary per-sample. An
  independent cold audit verified ~45 other citations byte-exact. **Converged**
  (polish) — no design/structural/mechanism finding remains.

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
  after the primary hit is resolved (`tHit`/`hitP` are passed into the `marchFog` calls,
  `pathtrace.comp:1060-1064` mode 4 / `:1189-1195` mode 6). Unlike those features there
  is **no existing shading point to extend** — the march is genuinely new code between
  the primary hit and the final composite.
- **DOOM-0009 / DOOM-0084** (emitter set + static slice; **DOOM-0119** cull) — the fog's light sources are the
  **existing** static emitters: the `Emitters` device-address buffer
  (14 floats/record, `pt_common.glsl:84-87`), sliced `[0, omniStart)` = oriented
  static wall/flat lights via `omniStart = pc.misc4.y` (`r_vulkan.cpp:7426`). Fog
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
  fog-placement standard** (+ the outdoor-proximity seep) · **4.3b the Silent Hill 2
  look — drifting two-octave wisps** · 4.4 light sources & shafts · 4.5 area profiles ·
  4.6 half-res, denoise, composite · **4.6a fogging the sky backdrop (aerial
  perspective)**) —
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

Today the path tracer is a **single-primary-ray megakernel** (`main()` at
`pathtrace.comp:798`). It shades the **first hit** and composites; the air between
camera and surface contributes **nothing**. Three concrete gaps:

1. **There is no air-march and no shading point to extend.** DOOM-0181/0183 hooked
   an *existing* primary-hit block; volumetrics instead needs **new** code that
   marches the segment `t ∈ [0, tHit]` (camera `pc.camPos.xyz`, primary ray built at
   `pathtrace.comp:811-812`; `tHit`/`hitP` are resolved at the primary hit and passed
   into the `marchFog` calls — the mode-4 call at `:1060-1064`, mode-6 at `:1189-1195`,
   §4.6a) *before* the final colour is written.
2. **There is no directional sky light.** The sky is a positional backdrop
   (TLAS custom-index 2, the `isSky` test at `pathtrace.comp:852-853`) sampled by
   `skyPanorama()` (`:735`) plus a constant ambient `SKY_COLOR = vec3(0.20,0.26,0.40)`
   (`pt_common.glsl:31`). A project-wide search finds **no** sun/light-direction
   vector anywhere. Sky *shafts* need a direction (which way the light slants), so
   this spec **adds** one (§4.4).
3. **Fog cannot ride the denoised lighting channel.** The SVGF composite
   re-multiplies the denoised illumination by surface albedo —
   `L = albedo * illum + emis * emisMask * ga.a` (`svgf_composite.comp:123`). A
   view-ray fog term has **no** surface albedo; multiplying it by the wall's albedo
   would be wrong. Fog must be a **separate channel composited *after* that
   re-modulation** (§4.6), and the sky-passthrough path (`svgf_composite.comp:93-107`)
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

- **Step count** `kFogSteps` (a compile-time `const`, start ~24; **raised to ~40 at
  L1c** — structured wisp density bands at 24 where flat haze did not, §4.3b) — fixed,
  not
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
(a small `const`, `0`..~`0.1`, a look-tune — Q12) under a roof. **As shipped in L1b the
value is binary per sample** — exactly `1` (up-ray missed) or exactly `kIndoorFogScale`
(up-ray hit a ceiling); the interval names the tunable endpoints, not a graded
per-sample value. The *smooth* indoor↔outdoor gradient in the final image emerges from
many binary samples averaged across the march + the half-res denoise (§4.6).
**Superseded on the indoor side by the 2026-07-25 amendment at the end of this
section** — the indoor branch becomes a position-dependent seep; the open-sky branch
(`= 1`) is unchanged.

**How `skyExposure` is measured — per march sample ("true volumetric", user's
explicit pick 2026-07-24 over the cheaper per-surface flag).** At each sample point
`p` the march casts **one shadow ray straight up** (world `+Z`) with the standard
shadow-ray cull mask **`0x01`**. That mask sees only solid world geometry — the sky
backdrop is a separate TLAS instance on mask `0x04` (`r_vulkan.cpp:2015`) that
"primary rays only" see (`:1918`), so a `0x01` up-ray **cannot hit the sky instance**.
Therefore: **the up-ray MISSING all geometry means the pocket is under open sky** (the
solid ceiling that would block it isn't there) → `skyExposure = 1`; a **hit on solid
geometry** (a real ceiling) → `skyExposure = kIndoorFogScale`. (Detecting open sky via
the *miss* needs no mask change; the alternative — widening the up-ray to `0x01|0x04`
to hit the sky dome directly — is unnecessary here.) This is what makes the haze
genuinely **fill the outdoor volume and cut off at a doorway threshold** — a sample
just inside the roofline hits the ceiling above it and reads clear, so you get a wall
of mist at the door, not fog leaking indoors. It reuses the existing ray-query
machinery and is the **same order of ray as the L2 sky-visibility shaft test**
(§4.4(a)) — no new buffers.

**Cost + the built-in cheap fallback (perf lever, §6).** The up-ray adds **one ray per
march sample** — at `kFogSteps`×half-res it is the dominant new cost, so L1b (§7)
measures it on hardware. If it misses the 60 FPS floor, the layer falls back **in the
same build** to a **per-surface openness bit** with near-zero cost:
- A free flag bit **`RB_MESH_OUTDOOR = 0x100`** on the per-vertex geometry `flags`
  word (bits `0x1..0x80` used, storage is a 32-bit int → `0x100` is free with no
  format change, `r_mesh.h:82-101`).
- Set at mesh-build time from `frontsector/sector->ceilingpic == skyflatnum`, OR-ed
  into the `flags` word each surface already carries: **walls** — `emit_wall` receives
  `seg_t* seg` and already dereferences `seg->frontsector` for lightlevel
  (`r_mesh.c:275`), so derive the bit from `seg->frontsector->ceilingpic == skyflatnum`
  there (or OR it into the `flags` argument at the four `emit_wall` call sites in
  `RB_BuildLevelMesh`); **flats** — `emit_subsector_caps` already branches on
  `sec->ceilingpic == skyflatnum` (`r_mesh.c:452`, inside the `:443-456` cap block).
- Already carried to the march in `FogHit.matFlags` with **no** new plumbing (the raw
  flags word is passed at both call sites, `pathtrace.comp:1061` / `:1190`), so
  `marchFog` just tests `(h.matFlags & uint(FLAG_OUTDOOR)) != 0u`.

The fallback gates fog by whether the **surface you look at** is open-sky (whole-view
granularity, no doorway cutoff) instead of per-pocket — cheaper, coarser. The primary
path is the per-sample up-ray; the flag is the escape hatch and can also back a
future cheaper "Low" quality tier. The **sky backdrop** (the mountains) is open-sky by
definition → always `skyExposure = 1` (§4.6a); no up-ray is needed for a sky pixel.

**2026-07-25 amendment — the indoor value becomes a graded seep.** The binary gate
shipped in L1b and reads correctly (user play-test 2026-07-25: fog is "outside and not
inside"). The user asked for one softening: *"have a little bit of the fog come in by
open areas exposed to outside."* So the **indoor** value is no longer the flat
`kIndoorFogScale`; it fades from a fraction of full strength at the opening down to
`kIndoorFogScale` deep inside:

```
skyExposure = openSky ? 1.0
                      : mix(kIndoorFogScale, kSeepMax, exp(-d / kSeepFalloff))
```

- **`d`** = distance from the sample to the nearest open-sky air, measured **through
  open space**, not straight-line — so fog cannot bleed through a solid wall into a
  sealed room next door.
- **`kSeepMax` ≈ `0.5`** — even standing in the doorway, indoor air reaches at most
  half outdoor strength. This is the "a little bit" the user asked for; it must not
  become a second outdoors.
- **`kSeepFalloff` ≈ `192`** DOOM units — roughly a doorway-to-back-wall depth (Q16).
- **The up-ray still decides `openSky` unchanged.** The seep replaces only the *indoor*
  constant, so the outdoor look L1b shipped is preserved **exactly** (`openSky` → `1.0`
  on both sides of the amendment). Per sample the value is still one of two branches;
  what changed is that the indoor branch now varies with position instead of being
  constant.

**Where `d` comes from — a load-time distance field (user's pick 2026-07-25, chosen
from three offered options).** At level load, lay a coarse **2-D grid** over the map's
XY extent (start `64`-unit cells, matching DOOM's flat grid), seed every cell in an
open-sky sector (`ceilingpic == skyflatnum`) at `d = 0`, then **flood outward through
connected open space only** — a step to a neighbouring cell is allowed only where no
impassable (one-sided) linedef separates them, so the fill travels through doorways and
archways but never through a wall. Upload as a small single-channel 2-D texture (§5);
the march takes **one bilinear tap** at `p.xy` per sample. Effectively free at runtime,
noise-free, and it adds **no rays**.

**Why a 2-D field is exact here, not an approximation.** Vanilla DOOM has **no
room-over-room** — any XY column belongs to exactly one sector — so flattening loses
nothing. (Source-port-style 3-D floors would break this; DOOM_Ants renders vanilla
geometry, so it is out of scope.)

**Rejected alternatives** (all three were put to the user): **(B) extra sky-visibility
rays per sample** — no load-time work and correct in full 3-D, but it multiplies the
march's ray cost and adds sparkle at half-res; **(C) the per-room `RB_MESH_OUTDOOR` flag
as the indoor grade** — free, but fog would step abruptly at the room boundary rather
than drifting in, which is precisely the behaviour the user asked to soften. Note (C)
here is a **different role** from the same flag's use above as the up-ray's *perf
fallback*; the two are independent.

### 4.3b The Silent Hill 2 look — drifting two-octave wisps

**The target (user 2026-07-25, with reference screenshots).** Silent Hill 2 (original
PS2, 2001): **near-white, colourless** fog, thick enough that the world fades toward
flat grey at middling range, and — the quality L1b's uniform haze misses — full of
**billowing wisps of visibly varying thickness that drift slowly past**.

**What SH2 actually does.** Researched 2026-07-25; it is three stacked things, not one:
1. Hardware **distance fog** — the base grey-out. This is what L1/L1b already are.
2. **Two animated fog layers** at different scroll speeds and transparencies. The
   Silent Hill 2 Enhancements project's reverse-engineered parameters: layer 1 scrolls
   `0.125` on X and Y; layer 2 carries a density multiplier of `1.4` plus an offset;
   the two layers' alphas are `128` and `90` out of 255. **Two layers at differing
   rates and weights is what produces wisps of varying thickness** — a single layer
   reads as dirty glass.
3. Local swirls (moving cylinders with a rotating texture) and a **player-reactive**
   term (an "influence" of `200.0` around James, dropped to `10.0` in the Forest).

The community's "Fog Speed Fix" exists because the PC port scrolled the fog *too fast*
and lost the dread — **slow drift is part of the look**, not an accident.

*Sources:* [gamedev.net thread 362970](https://www.gamedev.net/forums/topic/362970-fog-effect-in-silent-hill-2/);
[elishacloud/Silent-Hill-2-Enhancements #246](https://github.com/elishacloud/Silent-Hill-2-Enhancements/issues/246)
and its [README](https://github.com/elishacloud/Silent-Hill-2-Enhancements/blob/master/README.md)
(`FogFix` / `FogSpeedFix`).

**What we take, and what we deliberately do not.** SH2 pasted 2-D planes over the
screen because a PS2 could not march a volume. We already march one, so the faithful
translation is to modulate `σ(p)` with **drifting 3-D noise** — which buys what the
original could not have: the wisps have **depth**, passing in front of *and* behind
pillars and monsters and parallaxing correctly as the camera turns. Item (3)'s
player-reactive swirl is **not** taken: DOOM is **first-person**, so there is no
on-screen body for fog to curl around and the player would never see it. Split out as
**DOOM-0239** (💭) rather than built.

**The density modulation.** §4.3a's product gains one more mean-1 multiplier:

`σ_final = kFogBaseDensity · heightPool · areaMult · skyExposure · wisp(p, t)`

where `wisp` is two octaves of value noise read from a single **3-D noise volume**
(§5), each drifting on its own slow velocity:

- `wisp(p,t) = 1 + kWispAmp · (A + kWispWeight2 · B) / (1 + kWispWeight2)`, with
  `A = 2·noise(p·f₁ + v₁·t) − 1` and `B = 2·noise(p·f₂ + v₂·t) − 1`.
- **Mean 1 by construction** (`A`, `B` are zero-mean), so `kWispAmp = 0` restores the
  L1b uniform haze **exactly** — the wisps are a pure addition, and the fog-off path is
  untouched either way (INV-8, INV-11).
- **Amplitude is large, not a wobble.** `kWispAmp` starts ~`0.6`, swinging density
  roughly `0.4×`..`1.8×` of base. That swing *is* the "various thickness" the user
  asked for; a ±15 % grain would read as noise, not billows.
- **Octave 2 is finer and fainter:** `f₂ ≈ 2.5·f₁`, `kWispWeight2 ≈ 0.7` — SH2's
  `90/128` alpha ratio, rounded.
- **Drift is slow and mostly horizontal**, with `v₁` and `v₂` differing in **direction**
  as well as speed so the octaves never lock into a repeating pattern. Start ~`8` DOOM
  units/s for octave 1. Erring **slow** is the SH2-authentic direction.
- **Time comes from `misc6.x`** — DOOM-0183's ripple-time lane (float seconds from a
  `steady_clock` zeroed at first use, `r_vulkan.cpp:7446-7452`), already
  frame-rate-independent and already in the push block. **No new push lane** (INV-5).

**Colour and thickness.** In-scatter tone moves from `SKY_COLOR`
(`vec3(0.20, 0.26, 0.40)`, cool blue, `pt_common.glsl:31`) to a near-white desaturated
`kFogColor` — start ~`vec3(0.55, 0.56, 0.56)` in linear radiance: **brighter *and*
colourless**, so distance reads as *pale* rather than merely dim. `kFogBaseDensity`
rises from `0.0008` toward ~`0.0016` (≈2×) — deliberately **not** the ~3× a wisp-free
haze would need, because structure sells the look at lower average density, which also
keeps enemies readable. Both tune on hardware, and the `;` strength dial still scales
the whole thing.

**Keep it from pooling.** SH2 fog is vertically uniform. L3's height pooling (§4.3)
must stay **gentle** or it will undo this; `kFogPoolHeight` is a look-tune to be judged
**with** the wisps present, not before (Q17).

**How the near-white base coexists with coloured fog (user wrinkle, 2026-07-25:
*"the fog must be lit with any relevant colours but only where it makes sense — like in
Hell for example"*).** `kFogColor` is **not** a global override; it is the **clear
profile's** base tone, and §4.5's area profiles still multiply it. The composition rule
of §4.3 is unchanged and is what makes this work:

`fog colour = (light colour: sky `kFogColor` / emitter `Le`) × mediumTint(§4.5)`

- **Earth-side maps** (E1 Knee-Deep, most of DOOM II's city run) sit in the **clear**
  profile → `mediumTint` is neutral → the fog reads **SH2 near-white**. This is the
  default and the majority of play.
- **Hell levels** take `kHellTint` → the same wisps, same drift, but lit **red/ember**.
  The SH2 grey is deliberately *departed from* there, which is the point of the user's
  "only where it makes sense".
- **Goo/nukage rooms** take `kGooTint` → sickly green pooling, per §4.5.
- **Emitter-lit fog is already coloured** by construction (§4.4b): a torch shaft
  inherits its emitter's `Le`, so fog near a flame goes warm without any new mechanism —
  a warm core against the near-white surround, which is exactly the SH2 street-lamp
  look.

So the SH2 amendment changes **what "neutral" means** — from cool blue to near-white —
and leaves the colouring machinery of §4.4/§4.5 untouched. The judgement of *where*
colour makes sense stays §4.5's profile selection (level flag + primary-hit liquid
flag), tuned at **L4**; nothing here pre-empts it. The one new caution: `kHellTint` and
`kGooTint` were picked against a **blue-grey** base and must be **re-judged against the
near-white base** at L4, or they will read washed-out (Q20).

### 4.4 Light sources & shafts

Fog scatters light from **two** sources only — sky and big static emitters
(user 2026-07-21). Muzzle/flashlight/sprite lights are excluded by construction
(they are push-constant deltas / the `[omniStart, emitCount)` dynamic slice, never
iterated here — INV-3).

**(a) Sky shafts — needs a new direction.** Add a **sun direction** `kSunDir`
(world-space, a compile-time `const` default for v1 — a plausible steep slant; per
-level control deferred, Q1). At a march sample, cast **one** shadow ray toward
`kSunDir` with the standard shadow cull mask `0x01`. Because that mask excludes the
sky-backdrop instance (mask `0x04`, `r_vulkan.cpp:2015` — same as the open-sky up-ray
of §4.3a), the ray **reaching the sky = MISSING all solid geometry** toward the sun
(nothing blocks the path). A clear (miss) ray → the sample is **sky-lit**, add
`skyRadiance · phase · mediumTint`; a ray blocked by solid geometry → dark. The
bright/dark boundary *is* the shaft (a beam through a doorway/sky-hole). One ray per
sample keeps it affordable at half-res (§4.6). **Sky shafts require an open sightline
to the sky:** on a fully enclosed level with no sky (sky tex id
`misc4.w == 0xFFFFFFFF` / no sky mesh, `pathtrace.comp:735-737`) a solid ceiling blocks
every sun ray, so sky shafts vanish — only torch shafts (b) + the base/haze fog
remain. Expected, not a bug.

**(b) Torch shafts — the existing static emitters.** Iterate the static slice
`k ∈ [0, omniStart)` (`omniStart = pc.misc4.y`, written at `r_vulkan.cpp:7426`; record
layout `pt_common.glsl:84-87`). For cost control, **do not** shadow-test every emitter at
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

**2026-07-25 — the user's steer on colour, and what it does not change.** *"The fog must
be lit with any relevant colours but only where it makes sense. Like in Hell for
example."* That is what this section already specifies, and the 2026-07-25 SH2 amendment
does **not** weaken it: §4.3b's near-white `kFogColor` is the **clear profile's** base
tone, which `mediumTint` still multiplies (the reconciliation is set out at the end of
§4.3b). Two consequences to carry into **L4**:
- **Selective, not global.** Colour is applied by profile — hell red, goo green,
  emitter-warm near flames — and **withheld** elsewhere, so Earth-side maps stay
  near-white. Blanket-tinting every level would be the failure mode.
- **Re-judge the tints against the new base.** `kHellTint` / `kGooTint` were chosen
  against a cool blue-grey fog; against a near-white base the same values will read
  differently (likely washed-out). Re-tune them on hardware at L4 **after** L1c lands,
  not before (Q20).

### 4.6 Half-res, denoise, composite

Fog is low-frequency, so compute it **cheaply and smooth it**:

- **Half-res march.** Mirror mode 6's existing even/even 2×2 half-res gate
  (`pathtrace.comp:1181`): march fog on one pixel in four, into a **new half-res fog
  target** (`inscatter.rgb` + scalar `transmittance` packed into one `RGBA16F`
  image). Mode 4 (NEE display) has **no** even/even gate and **no** SVGF upsample of
  its own, so a half-res mode-4 march would need its **own** dither + in-megakernel
  upsample; the simpler first cut is **full-res in mode 4**, half-res only in mode 6
  (Q4).
- **Denoise / upsample.** Fog **cannot** ride the SVGF illumination channel
  (albedo re-multiply, §3 gap 3). Two candidate paths (Q6): (a) a **bilateral upsample**
  of the half-res fog target guided by depth, cheapest and self-contained; (b) run
  the existing edge-aware **a-trous** passes (`r_vulkan.cpp:7564`) on the fog channel
  too. Start with (a); escalate to (b) only if the fog crawls/flickers. **At sky /
  far-depth pixels** (the `gp.w < 0.0` sentinel of the sky-passthrough branch,
  `svgf_composite.comp:93`) a depth-guided weight has no valid neighbour depth right at
  the sky/wall seam where shafts read — so there the upsample **falls back to a plain
  bilinear fetch** of the fog target (no depth guide), keeping the shaft-against-sky
  reconstruction smooth.
- **Composite — computed once, applied per-mode, always in linear radiance.**
  `marchFog` *computes* `inscatter`/`transmittance` in the megakernel for **both**
  modes; where they are *applied* differs by mode:
  - **Mode 4 (NEE display, no denoiser):** fold into `L` **in the megakernel**, before
    the mode-4 tonemap — `L = L * transmittance + inscatter`, then `colour =
    toneEncode(L)` (`pathtrace.comp:1063-1065`).
  - **Mode 6 (denoised play):** the megakernel writes fog to the half-res fog target;
    it is applied in `svgf_composite.comp` **after** the albedo re-multiply
    `L = albedo * illum + emis * … ` (`:123`, still linear) — `L = L * transmittance +
    inscatter`, then `toneEncode(L)` (`:133`).
  - **Both fold fog in linear radiance before the tonemap.** The mode-6
    **sky-passthrough** branch (`svgf_composite.comp:93-107`) is the trap: it stores a
    **display-encoded, fullbright** sky (`clamp(sky, 0, 1)`, deliberately *not*
    tonemapped, to match the raster sky), so fog must be folded in **in the same
    linear space** — treat the sky as linear, apply `sky * transmittance + inscatter`,
    then re-clamp/encode — so it matches at the sky/wall seam where shafts read.
    Compositing the two branches (or the two modes) in *different* colour spaces is the
    failure mode to avoid (Q9).

### 4.6a Fogging the sky backdrop (aerial perspective)

**Why the mountains stay crisp today.** `marchFog` runs **only** inside the world-hit
branch (`if (committed && !isSky)`, `pathtrace.comp:849-856`); it is called at
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
- **Mode 6:** `svgf_composite.comp` **cannot** compute the sky fog itself — it
  `#include`s only `formulas.glsl` / `pbr_neutral_tonemap.glsl`, not `pt_common.glsl`,
  so it has no access to `kFogMaxDist` / `kFogBaseDensity` / the tints. So do it the
  cheap, plumbing-free way: have the **megakernel's mode-6 sky branch write the
  closed-form aerial fog into the half-res `fogImg`** for sky pixels (it already holds
  the `pt_common` consts) — the sky branch currently writes **no** fog there — and the
  composite's **existing** `fetchFogBilinear` fold on the sky-passthrough branch
  (`svgf_composite.comp:100-103`) then picks it up **unchanged**. No new composite-shader
  code, no duplicated consts (INV-5-consistent).
- **Mode 4:** in the megakernel sky branch, after `colour = skyPanorama(...)`
  (`pathtrace.comp:1295`), fold the closed-form fog in before the write.

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
  `RtPushConstants` to **240 B** (`static_assert(sizeof==240)`, `r_vulkan.cpp:7400`;
  `pcr.size = 240`, `:2363`) and consumed `misc6.x` (ripple time), `misc6.y` (wet
  toggle). The **only** free components were **`misc6.z` and `misc6.w`**. This feature
  uses **exactly those two**, needing **no** struct growth (L1 already wired `misc6.z`,
  `:7454`; `misc6.w` stays `0` until L4, `:7455`):
  - **`misc6.z` = `rb_fog` strength** — a small **uint** (0..3; `0` = off, which also
    *is* the on/off state), written/read exactly like `misc6.y = rb_wet`
    (`pc.misc6[2] = (uint32_t)rb_fog`, `:7454`; the shader reads a `uint`).
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
    (`static_assert(sizeof(RtPC)==184)`, `r_vulkan.cpp:6854`, which stops before
    `misc6`), so **`-rtverify` is unaffected** (INV-7).
- **New `rb_view_t` field for the hell flag.** `rb_view_t` (`r_mesh.h:265-273`)
  currently carries only `x,y,z,angle,extralight,skytexnum`. Add one field
  (e.g. `float hazeDensity`), computed beside `view.skytexnum = skytexture`
  (`r_backend.c:181`) from `gameepisode`/`gamemap` + the sky, and written to
  `misc6.w`. (`r_backend.c` does not reference `gameepisode`/`gamemap` today; the
  compute brings them into scope.)
- **New runtime dial `rb_fog`** — `rb_detile`-style 0..3 value, `rb_wet`-style wiring:
  `extern "C" { int rb_fog = 1; }` in `r_vulkan.cpp` (beside `rb_wet`, `:1009`) — a
  **subtle "Low" on by default**, matching the on-by-default effect siblings
  (`rb_wet=1`, `rb_filth=1`, `rb_detile=2`) so atmosphere is present out of the box
  (perf-gated at L6; flip to `0` for off-by-default if review prefers — Q10); a config
  row `{"rt_fog", &rb_fog, 1}` in `m_misc.c` defaults (`:272`, beside `rt_wet` `:271`);
  the value written to `pc.misc6[2]` (`:7454`, beside `misc6[1] = rb_wet` `:7453`).
  `rb_fog` is a
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
     (`m_menu.c:1464`), not the boolean `"Wet liquid:"` row.
  4. **Crisp label table:** a `videoLabels[]` entry for `vid_fog` (`m_menu.c:1494`).
  5. **Crisp value switch:** a `case vid_fog:` in `M_VideoCrispValue` returning
     `fogNames[rb_fog]`, mirroring `case vid_detile:` (`m_menu.c:1557`), not the
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
  lanes *in `RtPushConstants`*. (Note the composite-side fog gate is a **separate**
  pre-existing lane, not touched here: `svgf_composite.comp` has its own 120-byte
  `SvgfPC` struct — `static_assert(sizeof(SvgfPC)==120)`, `r_vulkan.cpp:7524` — and L1
  mirrors `rb_fog` into its free `misc3.y` since `SvgfPC` has no `misc6`, written at
  `r_vulkan.cpp:7582`. That lane is out of scope for this "no new lane" claim, which is
  about the megakernel push block.) The **only** optional data addition is the fallback
  bit **`RB_MESH_OUTDOOR =
  0x100`** on the existing per-vertex `flags` int (`r_mesh.h:82-101`, no format change),
  set at mesh-build (walls via `emit_wall`'s `seg->frontsector`, `r_mesh.c:275`; flats
  `r_mesh.c:452`) and mirrored as a shader `const int FLAG_OUTDOOR = 0x100;` beside
  `FLAG_FLAT`/`FLAG_EMISSIVE` (`pt_common.glsl:20-22`, all `const int`) — added **only
  if** L1b's perf spot-check forces the cheap path (§4.3a, §6).

- **2026-07-25 amendment — two new sampled images; still no new push lane.**
  - **A 3-D noise volume** for the wisps (§4.3b) — `R8`, start `64³` (~256 KB),
    **generated on the CPU at startup**, not shipped as an asset (no file, no licence
    question — cf. `docs/standards/assets.md`). Trilinear filtering, `REPEAT` wrap on
    all three axes. The two octaves are two taps at different scales, so **one** volume
    serves both; it is level-independent and built once.
  - **A 2-D outdoor-distance field** for the seep (§4.3a amendment) — single channel
    (`R16F`, or `R8` with `d` normalised against `kSeepFalloff`), covering the map's XY
    extent at `64`-unit cells (a large vanilla map stays well under `256×256`, i.e.
    ≤ 128 KB). **Rebuilt per level**, beside the existing mesh build.
  - **Descriptor placement is pinned in the plan, not here.** Both are sampled images.
    `set 1` holds the RT pipeline's sampled textures (`binding 0` = `paletteTex`,
    `binding 2` = `materialTex[]`, `pathtrace.comp:69-70`; `binding 1` is unused by
    `pathtrace.comp` today) — **but `set 1` is shared with `bake.comp`**
    (`bake.comp:30-31`), so extending it touches both layouts; `set 3` (`MatCtrlBuf` +
    `hdTex[]`, `pathtrace.comp:83-84`) is the pathtrace-only alternative. L1c/L1d
    confirm which is the smaller change before wiring. The bake itself stays
    functionally untouched either way (INV-6).
  - **No new push-constant lane.** Wisp drift reuses **`misc6.x`** (DOOM-0183 ripple
    time, `r_vulkan.cpp:7446-7452`); everything else new (`kFogColor`, `kWispAmp`,
    `kWispWeight2`, the octave frequencies/velocities, `kSeepMax`, `kSeepFalloff`) is a
    compile-time `const` per house convention. **INV-5 holds unchanged** — still 240 B,
    with `misc6.z/.w` still the only fog runtime lanes.

## 6. Performance budget

- **Baseline & method:** the DOOM-0181/0183 §6 protocol — average the `` ` ``
  profiler present-total (ms, not FPS) over a fixed ~10 s walk of the **E1M1
  green-goo room** (a sky-hole/doorway scene too, for shafts), RT-on, 50 % render
  scale, with `rb_fog` **off** then **on** (same-walk A/B, the DOOM-0187 lesson).
- **Cost shape (measure, don't assert):** the march is `kFogSteps` samples/pixel,
  each with **one** sky shadow ray + a few emitter evaluations, at **half-res**
  (¼ the pixels) + denoise. The shadow rays are the pole; half-res + few steps +
  dither + denoise is what makes it affordable.
- **A dedicated GPU-timer slot needs the pool grown.** The RT profiler pool is sized
  `queryCount = 8` (`r_vulkan.cpp:1518`) and the RT path **already writes all 8 indices
  0–7** (`vkCmdWriteTimestamp` at `:7327`, `:7354`, `:7495`, `:7554`, `:7574`, `:7587`,
  `:7634-7636`, `:7638`, `:7768` — note the code comment at `:1504` still says "5 used"
  but is itself stale). So a fog-pass timer must **bump `queryCount` past 8** and widen
  the resets + the readback — a small, contained change made **with** the perf layer
  (L6), not silently skipped.
- **Levers held ready** (measure before cutting): the `rb_fog` **strength** dial is
  the standing perf option (though a lower strength is not automatically cheaper), plus
  reduce `kFogSteps`; drop the emitter occlusion ray (§4.4b); distance-gate the march
  (`kFogMaxDist`); make mode 4 half-res too; and — the biggest new lever —
  **swap the per-sample open-sky up-ray (§4.3a) for the near-free per-surface
  `RB_MESH_OUTDOOR` flag**, trading the doorway cutoff for whole-view granularity.
- **2026-07-24 amendment — the up-ray is the march's FIRST ray, and L1b spot-checks it.**
  The shipped L1 `marchFog` (`pathtrace.comp:774-796`) does **zero** ray-queries per
  sample — the loop is just `density × strength × flat-sky-ambient`. The open-sky up-ray
  (§4.3a) adds **the first** ray-query, one per march sample: a `0 → 1` ray/sample jump
  ×`kFogSteps`×half-res, so expect a **large, not incremental** cost step (a ray-query is
  typically the priciest op in a march loop). (Mode 4's full-res march pays
  proportionally more, but mode 4 isn't the FPS-gated play path; L2 later adds a *second*
  ray, the sun shaft.) Because it lands in **L1b** (§7), that layer carries its **own
  hardware perf spot-check** on the RX 6600, using the §6 A/B method: measure the up-ray's
  **added present-total** (fog-off vs fog-on, same walk) — the goo room's ~40 FPS baseline
  is a pre-existing megakernel/denoiser cost, so the check is the *added* Δ, plus a
  confirmation that a **typical non-goo corridor scene** (where Ultra sits above 60 FPS
  today) still holds 60 FPS with the up-ray on. If the added cost blows the budget, L1b
  ships the `RB_MESH_OUTDOOR` fallback instead (built in the same layer). The **formal
  ≤ 5 % present-total gate stays L6**; L1b's check is a go/no-go on which exposure method
  ships.
- **2026-07-25 amendment — the gate rises from ≤ 5 % to ≤ 15 % present-total (user
  decision 2026-07-25).** The user's reasoning: SH2 ran this look on a PS2, so a modern
  PC should afford it. Right in spirit, but it does **not** transfer literally — SH2's
  fog was **flat 2-D planes composited over the frame**, while ours is a **true 3-D
  march inside a path tracer that is already GPU-bound** (~45 FPS Ultra RT). The
  headroom is whatever the tracer leaves, not a PS2-sized gulf. The user was told this
  and chose ~15 % anyway. What the extra budget buys, **in priority order**:
  1. `kFogSteps` `24 → ~40` (§4.2/§4.3b) — the one that matters; structured wisps band
     at 24 where flat haze did not.
  2. **Promoting mode-6 fog from half-res to full-res** *if* the wisps read soft or
     blocky. This **dissolves** the L5 upsample problem rather than solving it (no
     upsample, no depth guide, no sky-seam bilinear fallback). **Measure half-res
     first** — if half-res with wisps looks right, keep the cheaper path and bank the
     budget (Q18).
  3. A third noise octave, only if two read too regular.
- **The ≤ 5 % figure is superseded** wherever it appears in this section and in §7's L6
  row. The **method is unchanged** — same-walk A/B on present-total via the `` ` ``
  profiler, fixed E1M1 goo-room walk, 50 % render scale, `rb_fog` off then on. Only the
  threshold moves.
- **Conflict between the two gates, surfaced and resolved.** "≤ 15 % of frame time" and
  "a 60 FPS scene still holds 60 FPS" **cannot both bind**: 15 % of a 16.7 ms frame is
  2.5 ms, landing that scene near 52 FPS. The user was shown the concrete trade
  (`~45 → ~39 FPS` in Ultra RT) when choosing the budget and accepted it, so **for
  RT-engaged scenes the percentage governs and the 60 FPS floor is relaxed
  accordingly**. The floor still binds where it always did: **Classic and the raster
  path are untouched and must not move at all** (INV-7).
- **Gate (L6, the pass/fail):** with `rb_fog` at its shipped default, the march adds
  **≤ 15 % to present-total** (ms; was ≤ 5 % before the 2026-07-25 amendment) vs the
  `rb_fog`-off RT-on baseline on the fixed
  goo-room walk — the same measurable bar DOOM-0181 held, at the raised threshold.
  ("Within a handful of FPS" was the user's informal phrasing on 2026-07-21; the
  **present-total percentage** is the actual test, because FPS is non-linear — a few
  FPS at 160 is trivial, at 40 it is > 10 %.) The goo room's existing ~40 FPS remains a
  pre-existing megakernel/denoiser cost (per DOOM-0183 framing), not fog's; the gate
  measures fog's **added Δ**. Per the 2026-07-25 amendment above, the 60 FPS floor no
  longer binds RT-engaged scenes — the ≤ 15 % share does.
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
| **L1** | The march skeleton: `marchFog` over `[0,tHit]`, constant base density, **isotropic single scatter from the sky only** (no direction yet — flat sky ambient), composited via a new half-res fog target + bilateral upsample + the per-mode apply (§4.6: in-megakernel `toneEncode` for mode 4, `svgf_composite.comp:123`+sky-passthrough for mode 6). Full RGB, no colour profiles. | Air picks up a faint uniform glow; surfaces behind thick fog fade; sky still visible through fog; no NaNs; modes 4 & 6 match | no |
| **L1b** | **The fog-placement standard + the mountains** (2026-07-24 amendment, the immediate next work). Two parts: **(i) sky-backdrop aerial fog** (§4.6a) — fog sky pixels over `[0,kFogMaxDist]`, folded on the sky-passthrough branch + mode-4 sky branch, reconciling the old `SKY_FOG_COL` band (Q14); **(ii) open-sky exposure gate** (§4.3a) — per-sample up-ray sky-visibility → `skyExposure` multiplier on density, with the `RB_MESH_OUTDOOR` flag path built in as the perf fallback. | Open/sky-exposed rooms stay hazy; step under a roof and the air **clears with a mist wall at the threshold**; distant **mountains fade into haze**, not crisp; sky still recognizable; fog-off byte-identical (INV-7/8). **Plus a hardware perf spot-check** (§6): if the up-ray misses 60 FPS, ship the `RB_MESH_OUTDOOR` fallback. | spot-check |
| **L1c** | **The Silent Hill 2 look** (§4.3b, 2026-07-25 amendment): near-white `kFogColor`, base density ≈2×, `kFogSteps` 24→~40, and the **two-octave drifting wisps** off a CPU-generated 3-D noise volume (new sampled image, §5), drift time reusing `misc6.x`. | Fog reads **near-white and colourless**, not blue; **billows of visibly differing thickness drift slowly past**, and they sit correctly **in depth** — passing in front of *and* behind pillars/monsters as the camera turns; no banding at wisp boundaries; no crawl or strobe in a slow pan; `kWispAmp = 0` reproduces the L1b haze exactly; fog-off byte-identical (INV-8). Half-res measured **first**; promote mode-6 fog to full-res only if wisps read soft (§6, Q18) | spot-check |
| **L1d** | **Outdoor-proximity seep** (§4.3a amendment, 2026-07-25): the load-time flood-filled distance field (new per-level 2-D texture, §5) + the graded indoor `skyExposure`. | Standing in a doorway onto a courtyard, **a little fog drifts in and thins as you walk deeper**; a **sealed** room that merely shares a wall with outdoors stays clear (proves the fill is through-open-space, not straight-line); the outdoor look is **unchanged** from L1b; level load time not visibly longer | no |
| **L2** | **Sky shafts:** add `kSunDir` + the one-ray sky-visibility test per sample + HG phase (builds on L1b's up-ray machinery). | A doorway/sky-hole open to sky throws a visible slanted beam; closed rooms stay clear; the beam moves correctly as the camera orbits | no |
| **L3** | **Height pooling + torch shafts:** height-based density (`hitP.z` floor ref); iterate static emitters `k<omniStart` (nearest-few, no occlusion first). | Fog settles low into a floor layer; a torch in a dark room glows its surrounding air; dynamic/muzzle/flashlight do **not** scatter | no |
| **L4** | **Area profiles + colour:** goo tint via the primary-hit `RB_FLAG_LIQUID_NUKAGE`; hell haze via the new `rb_view_t` field → `misc6.w`; `mediumTint` colouring (light×medium). | Goo rooms fill green and pool low; hell levels gain a faint red haze; a torch shaft reads warm-through-green in goo; clear levels stay neutral | no |
| **L5** | **Denoise/quality pass:** dither tuning; escalate upsample→a-trous if it crawls (§4.6 Q6); phase/anisotropy tune. **May be largely dissolved** if L1c promotes mode-6 fog to full-res (§6 item 2) — with no upsample there is no upsample to harden; the dither/phase tuning still applies. | Fog is smooth, not grainy or crawling, in a slow pan; shafts hold their shape | no |
| **L6** | **Runtime dial + menu + key + perf:** `rb_fog` (`rt_fog` config), both menu rows, the `;` key, the fog-pass profiler-slot wiring (claim a free slot; grow the pool only if full — §6), the DOOM-0208 canonical-config pin (§8 INV-8), and the perf pass. | Toggle/strength flip cleanly off→low→high; adds **≤ 15 % present-total** vs off (§6, raised from ≤ 5 % by the 2026-07-25 amendment); `-rtverify` **green**; if fog ships on-by-default (Q10) the `-shotcompare` golden is re-blessed with subtle fog, else fog-off stays byte-identical (INV-8); Classic + the raster path unmoved (INV-7) — the 60 FPS floor no longer binds RT-engaged scenes (§6, 2026-07-25) | **yes** |

**Footnote — the L1b "spot-check" FPS-gate:** *not* the formal perf gate (that stays
L6-only, §6). It is an internal **go/no-go on which exposure method ships** — the
per-sample up-ray if it holds the budget, the `RB_MESH_OUTDOOR` fallback if it doesn't.

**Interim state (expected, not a regression):** L1 (shipped, e7753b3) was a flat
**uniform** sky-ambient glow — the user play-test flagged it as too-uniform and
mountain-less, which **L1b** fixed (open-sky gating + sky-backdrop fog, 1345c92;
user-confirmed 2026-07-25). Post-L1b the haze is still **cool-blue and uniform**
(**L1c** makes it near-white and wispy) and cuts off **hard** at a roofline (**L1d**
adds the seep); it has **no** shafts (the directional term arrives at L2) and **no**
colour profiles (L4) — mirroring DOOM-0183's "sheen-before-ripple" staged interim.

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
  re-multiply** (`svgf_composite.comp:123`) — it never rides `gillum`/`illum` (which
  is multiplied by surface albedo). `inscatter`/`transmittance` are **linear
  radiance**, folded in **before** the tonemap on **both** the surface path and the
  sky-passthrough branch (`:93-107`) in the *same* colour space, so the sky/wall seam
  matches (§4.6).
- **INV-5:** The two runtime values ride **`misc6.z` (fog strength) + `misc6.w`
  (haze density)** — the **last two free components** of the 240-byte
  `RtPushConstants`. This feature adds **no** struct growth and does **not** append
  `misc7`; the C++ struct, its `static_assert` (`r_vulkan.cpp:7400`), `pcr.size`
  (`:2363`), and the GLSL push block stay at 240 B.
- **INV-6:** The GI bake (`bake.comp`) is **untouched** — fog is a view-ray term and
  never enters the bake (which computes surface irradiance). No double-count.
- **INV-7:** Ultra **and** Solid, **RT engaged only** (`rb_rtdebug` ∈ {4, 6}).
  Classic and the raster path (RT off) are **byte-identical by construction** — fog
  lives only in the RT megakernel, so no raster/Classic code is touched. (There is no
  golden test for *this* claim: `-shotcompare` renders the Ultra-RT view only and
  cannot exercise the raster path — that gate is INV-8's, below.) The fog lanes sit
  beyond the 184-byte `-rtverify` prefix (`r_vulkan.cpp:6854`), so **`-rtverify` is
  unaffected**; the headless verify mode (5), the debug views (1–3), and RT-off (0)
  are untouched.
- **INV-8:** Every fog cost is **`rb_fog`-gated** — `rb_fog == 0` skips the march
  entirely (the branch is not taken), so the RT path with fog off is byte-identical to
  today **by construction** (like INV-7 — no golden needed). Two *distinct*
  `-shotcompare` roles, not to be conflated: **(a)** the DOOM-0208 canonical config
  pins effect toggles to their **shipped defaults** (`rb_detile=2, rb_filth=1,
  rb_wet=1`, `r_vulkan.cpp:8207`), so when fog ships it pins `rb_fog` to *its*
  shipped default (§5) and the golden is **re-blessed *with* subtle fog** — the gate
  then guards the fog *look* (exactly how DOOM-0183 re-blessed for wet). **(b)** The
  fog-*off* byte-identity is structural; if an empirical check is wanted, a temporary
  `-config` forcing `rb_fog=0` vs the pre-feature golden proves it — that is *not* the
  canonical run.

- **INV-9 (open-sky standard, 2026-07-24):** fog density is gated by **open-sky
  exposure** — `σ_final = base · heightPool · areaMult · skyExposure`, with
  `skyExposure = 1` under open sky and `kIndoorFogScale` (`const`) under a solid roof.
  v1 measures exposure **per march sample** via one up-ray — **MISS = open sky, solid-
  geometry hit = indoor** (the mask mechanism is derived once in §4.3a). It is the user's
  "true volumetric" pick; the per-surface `RB_MESH_OUTDOOR` flag is the cheap fallback,
  selected only if L1b's perf spot-check demands it. "Open sky" = `ceilingpic ==
  skyflatnum`, the engine's own open-air signal. **Amended 2026-07-25:** the open-sky
  branch is still exactly `1`, but the **indoor** branch is no longer the flat
  `kIndoorFogScale` — it is `mix(kIndoorFogScale, kSeepMax, exp(-d/kSeepFalloff))`,
  where `d` is the **through-open-space** distance to outdoor air (§4.3a amendment).
- **INV-10 (sky-backdrop fog, 2026-07-24):** sky pixels receive **aerial-perspective
  fog** (`skyExposure = 1`) over `[0, kFogMaxDist]`, folded as `sky · transmittance +
  inscatter` on the mode-6 sky-passthrough branch (`svgf_composite.comp:93-107`) and the
  mode-4 sky branch (`pathtrace.comp:1295`), in the **same linear space** as every other
  fog fold (INV-4). Fog-off (`rb_fog == 0`) → `transmittance = 1, inscatter = 0`, so the
  sky is **byte-identical** to today (INV-7/INV-8). No up-ray and no new resource
  (INV-5) — the sky is outdoors by definition.
- **INV-11 (wisps, 2026-07-25):** density is modulated by **two octaves of drifting 3-D
  value noise** read from a single CPU-generated noise volume —
  `σ_final = … · skyExposure · wisp(p,t)`. `wisp` is **mean-1 by construction**, so
  `kWispAmp = 0` reproduces the L1b uniform haze **exactly**; the wisps are a pure
  addition and cannot shift the un-wisped look. Drift time is **`misc6.x`** (DOOM-0183's
  ripple lane), so **no new push lane** is added (INV-5 holds). The noise volume is
  **generated at startup, never shipped as an asset**.
- **INV-12 (seep field, 2026-07-25):** the outdoor-distance field is flood-filled
  **through connected open space only**, never straight-line — so fog can **never**
  appear in a sealed room that merely shares a wall with an outdoor area. It is rebuilt
  **per level**, read with a **single bilinear tap**, and adds **no rays** to the march.
  It is exact rather than approximate because vanilla DOOM has no room-over-room
  (§4.3a).

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
  re-multiplies by albedo (`svgf_composite.comp:123`), which a view-ray term must not
  receive (INV-4).
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
- **Q4 (mode-4 resolution):** **decided (§4.6):** mode 4 (display) marches **full-res**,
  mode 6 (play) marches **half-res** — matching the shipped code (mode 6 has the
  even/even gate, mode 4 does not). Revisit only if the mode-4 display looks soft or its
  full-res march proves too costly.
- **Q5 (phase):** Henyey–Greenstein forward-bias vs isotropic — a shaft-shape tune
  (L2/L5).
- **Q6 (denoise path):** depth-guided bilateral upsample (cheap, self-contained) vs
  routing the fog channel through the a-trous passes (`r_vulkan.cpp:7564`). Start
  bilateral;
  escalate if the fog crawls (L5).
- **Q7 (hell-haze tuning):** the v1 hell rule (§4.5: Inferno E≥3 / DOOM-II map≥20 /
  fire-sky) is a concrete default so L4 is testable; the exact map thresholds, haze
  density, and `kHellTint` are tuned on hardware at L4.
- **Q8 (tonemap headroom):** bright sky shafts must read strong without clipping to
  a flat white slab under the PBR-Neutral tonemap — verify at L2/L5 (same caution as
  DOOM-0183 Q7).
- **Q9 (sky fog encode point):** the sky-passthrough branch stores a display-encoded
  fullbright sky (`svgf_composite.comp:93-107`); folding fog in linear (§4.6) means
  treating it as linear, compositing, then re-clamp/encode. **L1 (shipped, e7753b3)**
  wired this fold with `rb_fog`-gated `fetchFogBilinear`; the round-trip is a **no-op by
  construction** for an un-fogged pixel (`rb_fog==0` → `fog.a=1, fog.rgb=0` →
  `sky·1+0`), so the fog-off sky stays byte-identical (INV-7/INV-8). L1b re-verifies this
  holds once the sky-distance fog (§4.6a) writes real values for sky pixels.
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
- **Q16 (seep reach, 2026-07-25):** `kSeepMax ≈ 0.5` and `kSeepFalloff ≈ 192` units are
  first guesses at "a little bit of fog comes in". Too far/strong and the interior
  becomes a second outdoors (defeating the L1b standard the user liked); too short and
  the seep is invisible. Look-tune at **L1d**.
- **Q17 (pooling vs wisps, 2026-07-25):** SH2 fog is vertically uniform, but L3 adds
  height pooling. Judge `kFogPoolHeight` **with** the wisps present — a strong pool plus
  strong wisps may read as soup. **L3**, after L1c.
- **Q18 (fog resolution, 2026-07-25):** keep mode-6 fog **half-res** with the wisps
  (cheaper, and L5's depth-guided upsample must then hold up against structured
  density), or promote to **full-res** (dissolves the upsample problem entirely,
  affordable under the raised ≤ 15 % budget)? **Measure half-res first at L1c** (§6).
- **Q19 (distance-field cell size, 2026-07-25):** `64`-unit cells match DOOM's flat grid
  and keep the texture tiny, but a doorway is often only ~64–128 units wide, so a coarse
  cell may smear the opening's edge. Finer cells cost load time + memory. Judge at
  **L1d**; bilinear filtering may make 64 sufficient.
- **Q20 (tints against a near-white base, 2026-07-25):** `kHellTint` / `kGooTint` were
  picked against a cool blue-grey fog (§4.5). Against §4.3b's near-white base they will
  read differently — likely washed-out. Re-tune at **L4**, after L1c ships, per the
  user's "colour only where it makes sense" steer.
