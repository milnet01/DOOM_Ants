# DOOM-0011 — Volumetric lighting (god-rays + fog) in the ray-traced view

**Status:** **L1 + L1b implemented and user-play-tested** (uniform-haze skeleton 84e8b35, base-density
tune e7753b3; fog-placement standard + sky-backdrop aerial fog 1345c92 — user 2026-07-25:
"looking fantastic… covers the mountains… outside and not inside"). **A 2026-07-25
amendment retargets the look at Silent Hill 2 (§4.3b, wisps) and softens the indoor
cutoff into an outdoor-proximity seep (§4.3a amendment); the perf gate rises to
≤ 15 % (§6). `/cold-eyes` has run **13 loops and CONVERGED** (2026-07-26) — loop 13 returned
**zero findings**, after two clean-of-CRITICAL-and-HIGH passes before it. Every code block in the
plan has been through `glslangValidator`. **The documents are ready to build from.** One item is
still genuinely blocking and cannot be settled at a desk: **Δ(L1b) is unmeasured**, and L1c's perf
gate is `8 % − Δ(L1b)`. Every code block in the plan has been
through `glslangValidator`. Loops 4–11 each had their worst finding inside the *previous* loop's
own fixes, which is why the fix ledger's pre-flight exists. **The `--max-loops` cap
of 5 is passed, so each further loop is an explicit user call.** The plan's **L1c and L1d tasks
were written on 2026-07-26** and first reviewed at loop 7. Original design
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
INV-12** added; **Q16–Q22** added. SH2's player-reactive swirl is **deliberately not
taken** (DOOM is first-person — no body to swirl around); split out as **DOOM-0239**.

**2026-07-30 amendment (DOOM-0289 — L2's sun ray is deleted and baked into the field).**
L2 shipped correct and **19× over its share**: one shadow ray per march sample, measured
at **13.6 ms** against the entire rest of the fog's 0.7 ms, for **−15 fps at the shipped
default strength** — and `master` carries that regression today. This is DOOM-0276
repeating with the march's *other* ray, and it takes the same answer, because DOOM is
flat-mapped and `kSunDir` is a compile-time `const`: the visibility question is decided
at level load on the 2-D grid the seep field already owns. **The correction that makes it
non-obvious** is that sun visibility is an **interval** `[zLo, zHi]` per cell, not a
minimum height — in roofed air, rising both clears the wall ahead *and* meets the ceiling
above, and roofed air just inside a doorway is precisely where the good shafts are. The
field widens `RG16F → RGBA16F`; the shader's existing bilinear tap answers it with **no
new image, sampler, descriptor or second lookup**. New design at the end of **§4.4**;
build order gains **L2b** (§7); **INV-3 and INV-12 amended**, **INV-13** added;
**Q27–Q30** added, with **Q27 (a moving sun) closed by the user** the day it was asked —
DOOM 1 and 2 have no day/night cycle, so the fixed sun this scheme requires is settled
rather than deferred.

**Cold-eyes log (rule 14).** Three review campaigns have run over this document. The
**full loop-by-loop record** — every severity tally, every headline finding, and what each
would have cost if it had reached the implementer — lives in
`docs/specs/DOOM-0011-fix-ledger.md`, alongside the per-fix ripple tables.

| Campaign | Loops | Outcome |
|---|---|---|
| Original spec (2026-07-23) | 4 | **Converged** — polish only by loop 4 |
| 2026-07-24 amendment (fog follows open sky) | 3 | **Converged** — polish only by loop 3 |
| 2026-07-25 amendment (SH2 look + seep) | 13 | **Converged** — zero findings at loop 13 |
| 2026-07-30 amendment (DOOM-0289 sun-clearance field) | 3 (cap) | **Converged-by-cap, zero deferred.** loop 1 **C 3 · H 5 · M 7 · L 5** (20 verified / 2 not); loop 2 **C 2 · H 5 · M 10 · L 9** (26); loop 3 **C 4 · H 4 · M 10 · L 8** (26). Every verified finding at every severity was fixed in its own loop. **The trend is the finding:** draft defects fell 20 → 11 → 4 while *collateral from the previous loop's own fixes* rose 0 → 15 → 22 — loops 2 and 3 were mostly repairing loop 1's and loop 2's repairs, which is the ratio trigger for "sweep harder, do not loop again". The document is 2.7 k lines, well past this gate's one-to-three-loop design point; **if it is amended again, split it first** |

One lesson is repeated here because it shaped both documents: **for five loops running, the
worst findings were defects in the previous loop's own fixes** — shader code written into
the docs using symbols that were never declared anywhere. Code inside a doc has to be read
as code, not as prose.

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
denoised — informally "within a handful of FPS of today" (Ultra ~45 FPS, goo room ~40),
now formalised as the ≤ 15 % present-total gate of §6 —
tuned on the user's RX 6600 together. Start subtle.

**Depends on:**
- **DOOM-0009** (path tracer) — the RT back-end. This adds a **view-ray march**
  over `t ∈ [0, tHit]` in `shaders/pathtrace.comp` **mode 4** (NEE display) and
  **mode 6** (denoised play), the same two display modes DOOM-0181/0183 hook, right
  after the primary hit is resolved (`tHit`/`hitP` are passed into the `marchFog` calls,
  `pathtrace.comp:1080` mode 4 / `:1209` mode 6). Unlike those features there
  is **no existing shading point to extend** — the march is genuinely new code between
  the primary hit and the final composite.
- **DOOM-0009 / DOOM-0084** (emitter set + static slice; **DOOM-0119** cull) — the fog's light sources are the
  **existing** static emitters: the `Emitters` device-address buffer
  (14 floats/record, `pt_common.glsl:84-87`), sliced `[0, omniStart)` = oriented
  static wall/flat lights via `omniStart = pc.misc4.y` (`r_vulkan.cpp:7431`). Fog
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

**Citation convention — read this before following any `file:line` in this document.**
This is the project rule (`docs/standards/documentation.md` § *Citing code from docs*),
restated here because this document predates it and still carries many bare numbers.
The **symbol name or quoted code is authoritative; the line number is only a hint.**
Locate the site by the quoted token (`static_assert(sizeof(RtPushConstants) == 240)`,
`FlagLiquidFlats`, `if (committed && !isSky)`), then confirm the line — never edit at a raw
line number. This is not pedantry: `r_vulkan.cpp` is ~9000 lines and `pathtrace.comp` ~1340,
and both move under unrelated work, so every number here rots. Cold-eyes loops 2 and 3 both
re-anchored these citations; DOOM-0254/0263 then shifted `r_vulkan.cpp` by +4..+6 and
`pathtrace.comp` by +2, and loop 4 had to correct **50** of them again — three of which had
come to point at entirely unrelated constructs. **All line numbers in this document were
verified against commit `d925a29` (2026-07-26).** If HEAD has moved since, treat every
number as advisory, and record any drift you fix in `DOOM-0011-fix-ledger.md`.

## Contents

- §1 Goal — §2 Where this sits — §3 The problem, precisely — §4 Design
  (4.1 hook · 4.2 the march · 4.3 density & colour · **4.3a open-sky exposure — the
  fog-placement standard** (+ the outdoor-proximity seep) · **4.3b the Silent Hill 2
  look — drifting two-octave wisps** · **4.3c two layers: the aerial layer and the
  floor fog** · 4.4 light sources & shafts (+ **the sun-clearance field**, DOOM-0289) ·
  4.5 area profiles ·
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
- **Placement follows open sky (§4.3a)** — air that can see the sky is hazed; roofed air
  keeps only a low floor, with **a little seeping in through openings**. This is the single rule
  for where fog is thick vs thin.
- **The default look is Silent Hill 2 (§4.3b)** — **near-white, colourless**, filled
  with slowly drifting **billows of varying thickness**. Not a cool-blue distance haze.
- **Coloured fog by area (§4.5)** — that near-white base is the *clear* profile; **thick
  low green fog** in nukage/goo rooms; **thin red-tinted haze** across hell levels.
- **Pooling (§4.3)** — fog is denser near the floor, so it settles into a layer
  rather than filling rooms uniformly. Kept **gentle**, since the SH2 look is vertically
  uniform (Q17).
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

*(This section diagnoses the engine **as it stood before L1/L1b**, which have since
shipped — `marchFog` exists and sky pixels are fogged. It is retained as the derivation
of why the design looks the way it does; do not read it as current state.)*

Before L1, the path tracer was a **single-primary-ray megakernel** (`main()` at
`pathtrace.comp:816`). It shades the **first hit** and composites; the air between
camera and surface contributes **nothing**. Three concrete gaps:

1. **There is no air-march and no shading point to extend.** DOOM-0181/0183 hooked
   an *existing* primary-hit block; volumetrics instead needs **new** code that
   marches the segment `t ∈ [0, tHit]` (camera `pc.camPos.xyz`, primary ray built at
   `pathtrace.comp:831`; `tHit`/`hitP` are resolved at the primary hit and passed
   into the `marchFog` calls — the mode-4 call at `:1080`, mode-6 at `:1209`,
   §4.6a) *before* the final colour is written.
2. **There is no directional sky light.** The sky is a positional backdrop
   (TLAS custom-index 2, the `isSky` test at `pathtrace.comp:870-871`) sampled by
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
primary-hit fields the march reads: **`hitP`** (world hit — used for the *liquid/profile* lookup, **not** as the floor
reference: density must be a function of the sample point alone, so the floor comes from
`pc.fogFloorZ` outdoors and the camera's floor indoors, §4.3 / Q3), the **geometric normal** (the up-facing floor test, §4.3), and the
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

- **Step count** `kFogSteps` (a compile-time `const`, **24 — and it stays 24**: L1c raised
  it to 40 on the banding hypothesis of §4.3b's "Step count and the wisps" note, and that
  raise was **measured, falsified and reverted on 2026-07-30** — the 40-vs-24 difference at
  the E1M1 spawn sat at or under the engine's own run-to-run noise) — fixed, not
  adaptive, for coherence and simplicity. `tHit` is clamped to `kFogMaxDist` (shipped value **2048** DOOM units,
  `pt_common.glsl:38` — load-bearing, since it is also the mountains' fade distance in
  §4.6a; tuned at L1c) so a
  long sightline down a corridor does not blow the step budget (steps past
  `kFogMaxDist` contribute negligibly for the target densities).
- **The samples are warped, not evenly spaced** (Q26, shipped 2026-07-27). Sample `i` sits at
  `t = tMax · s²` for `s = (i + jitter)/N`, and carries the substitution's Jacobian
  `dt = 2 · tMax · s / N`. Quadrature error scales as *sample spacing ÷ the density's e-fold
  range*, so an evenly-spaced march over 2048 units cannot resolve a layer that lives in the
  first few hundred — and buying accuracy with steps is a losing trade (64 uniform steps still
  band). §4.3c owns the measurement and the check that this does not starve the general fog.
  **The Jacobian is load-bearing**: drop it and every sample is weighted as if the march were
  still uniform, which silently rescales the whole fog instead of erroring.
- **Dither** per pixel (interleaved-gradient / blue-noise, reusing
  the frame counter `pc.misc3.x` on the mode-6 path) so the fixed step count does
  not band; the denoise (§4.6) then cleans the dither noise. This is the standard
  cheap-volumetrics recipe. Under the warp the jitter is applied to `s`, the position
  *within the warp*, not as an offset in `t` — jittering `t` directly would fight the
  redistribution the warp exists to do.
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

> **L3's height pooling was PULLED FORWARD and shipped 2026-07-27, ahead of L2.** User verdict on
> the uniform-density build: *"a fog look is applied to geometry instead of an actual cloud near
> the ground. If we can emulate a cloud near the ground, that should resolve everything."* That is
> the correct diagnosis — with `fogDensity()` returning a constant, every surface is simply greyed
> in proportion to its distance and nothing ever reads as standing *in* anything. Pooling is what
> turns the tint into a medium, so it stopped being an L3 nicety and became the point.
>
> It then took **three** passes on the same day to get right, and the two failures are worth
> keeping because both were invisible to review and obvious in a screenshot.
>
> **Pass 1 — pooling, at `kFogPoolHeight = 48`.** Too shallow a gradient: the floor was only 2.4×
> thicker than eye height, which nobody can see. User: *"it is applied to the walls but not the
> floor."*
>
> **Pass 2 — re-balance to `0.0033 / 18`,** holding the eye-height product fixed
> (`0.0008 × exp(−41/48) = 0.000340` vs `0.0033 × exp(−41/18) = 0.000338`) so only the near-ground
> air moved. The ground did thicken. It also exposed the real bug, which pass 1 had been hiding.
>
> **Pass 3 — THE DEFECT: density depended on what the ray hit.** `floorZ` came from the primary
> hit — `hitP.z` when it faced up, else the camera's floor. Standing on a ledge above a courtyard,
> that gives **two clouds at two heights in one view**: every wall pixel referenced the ledge, so
> its cloud sat high and hazed the wall; every floor pixel referenced the courtyard, so its cloud
> sat low and the eye looked down over the top of it and collected almost nothing. The user
> photographed it and named it exactly: *"we are not actually rendering a cloud, we are simulating
> the look of a cloud but only on some surfaces, not all."*
>
> **The invariant that fixes it, and that nothing may break again: the density at a point in
> space must be a function of that point and nothing else.** In particular it must not depend on
> what the ray carrying the sample eventually hits. The reference is now chosen per sample
> position:
>
> | sample is | fog sits on | e-fold height |
> |---|---|---|
> | under open sky | `pc.fogFloorZ` — one altitude for the whole level | `kFogPoolHeight` = 112 |
> | under a roof | the floor under the camera, `ro.z − kEyeAboveFloor` | `kFogIndoorPool` = 18 |
>
> `pc.fogFloorZ` is the lowest floor among the level's open-sky sectors, computed once in
> `RB_BuildLevelMesh` (`rb_mesh_t::fogFloorZ`) and pushed as a bit-cast float in the pad word
> `misc6`'s alignment already required — so the push range is unchanged at 240 bytes. A real fog
> bank has an altitude, not a per-pixel one; stand on a high ledge and you now correctly look down
> onto its top. Indoors keeps a camera-relative reference because a single global Z **is** wrong
> there — interior rooms sit hundreds of units above and below the outdoor ground — and it is
> still a per-frame constant, so the invariant holds.
>
> `kFogFloorFallback` was never added and is **not** in the tree; §5's inventory reflects that.
>
> **Outdoor thickness, same pass.** User: *"outside I want the fog much, much thicker and higher."*
> Delivered by raising the outdoor e-fold height 18 → **112**, with `kFogBaseDensity` left alone.
> The bank now reaches well up a courtyard wall instead of sitting below every sight line. At High,
> standing on E1M1's courtyard (65 units above the fog altitude):
>
> | | 256 u | 512 u | 1024 u | 2048 u |
> |---|---|---|---|---|
> | ground | 44 % | 68 % | 90 % | 99 % |
> | wall at eye height | 38 % | 61 % | 85 % | 98 % |
>
> **Ground and wall now agree to within a few points at every distance** — that agreement *is* the
> fix, and the small residual is correct (a ground ray dips into thicker air). A far wall at 1024
> reads 90 % at its base, 73 % at +128 and 58 % at +256: a vertical gradient, which is what a bank
> looks like. Standing on the ledge instead (121 units up) everything drops by roughly a third,
> because you are higher in the layer.
>
> **`kFogPoolHeight` is the outdoor dial** — raise for a deeper bank. It is no longer paired with
> `kFogBaseDensity` the way pass 2 described: the sky term now derives its own path length from it
> geometrically (§4.6a), so raising it thickens the horizon too.

- **Base density** `kFogBaseDensity` — a small always-on `const` so "clear air"
  still shows faint shafts (pure zero = no shafts at all). This is the "clear"
  profile.
- **Height pooling** — density scales up toward the floor:
  `heightPool = exp(-max(0, p.z − baseZ) / poolH)` (called `heightPool`
  throughout; earlier drafts named it `σ_height`). `baseZ` and `poolH` come from the
  open-sky test on the sample's own position, per the table above — **never** from the
  primary hit, which is the pass-3 defect. This makes fog **settle low** without new
  geometry data. Its coarseness (one indoor floor reference per
  pixel) is an accepted v1 approximation (Q3).
- **Area multiplier & tint** (§4.5) — the profile scales `σ` and sets the medium's
  **scattering tint** `mediumTint` (green in goo, red in hell, neutral in clear).
- **Colour of a shaft = light colour × medium tint.** Sky shafts inherit the fog's sky
  tone — **`kFogColor` (§4.3b), which SHIPS at `(0.55, 0.56, 0.56)` and is what `marchFog`
  in-scatters (`pathtrace.comp`, the `Ls = kFogColor * kSkyShaftStrength` line). This
  passage previously said it was unshipped and that the tree still used `SKY_COLOR`;
  that was stale, corrected 2026-08-03** —
  torch shafts inherit the emitter's `Le`
  colour; both are then multiplied by `mediumTint`. So a torch shaft in a goo room
  is warm-through-green; a sky shaft in hell is sky-through-red. Tint colours and
  strengths are compile-time `const`s (`kGooTint`, `kHellTint`, …), tuned on
  hardware toward the DOOM-0193 exaggerated look. Start subtle.

### 4.3a Open-sky exposure — the fog-placement standard

**The standard (user 2026-07-24):** *fog lives under open sky.* A pocket of air that
can see the sky carries **full** density; a pocket under a solid roof carries **little
to none**. This is the single rule for where the **sky-sourced haze** is thick vs thin (area
profiles, §4.5, add their own density on top), and it is
DOOM-native: an open-air area is exactly a sector whose ceiling is the sky flat
(`ceilingpic == skyflatnum`) — the same signal the engine already uses to draw open-air
(`r_mesh.c:452`). Formally, density gets one more multiplier:

`σ_final` is spelled out **once**, in §4.3b — it is the single authority. In outline:
the sky-sourced haze `kFogBaseDensity · skyExposure` plus the area profile's own density,
all scaled by height pooling, the wisp term and the `rb_fog` strength dial.

**`skyExposure` gates the SKY-SOURCED haze only — never the area profiles.** This is
load-bearing and was got wrong in the first draft of this section. Goo rooms, hell
interiors and torch-lit dark rooms are all **roofed**, so a formula that multiplied the
*whole* product by `skyExposure` would drive their density to `kIndoorFogScale`
(0–10 %; Q12 originally offered exactly `0`, since struck) — silently cancelling §4.5's green goo pool and
§4.5's red hell haze, and making L4's own falsifier ("E3M1 shows haze") fail by
construction.

**What the split does NOT rescue: the plain roofed room.** A dark, dry, non-hell
interior is the **clear** profile, which contributes no `areaMult` density — so its air
is `kFogBaseDensity · kIndoorFogScale`, exactly the whole-product behaviour. L3's torch
shafts need *something* in that air to light, so **`kIndoorFogScale` must be > 0**: Q12's
`= 0` option is therefore **struck** (it would make L3's own falsifier, "a torch in a
dark room glows its surrounding air", unachievable). The shipped value is `0.05`
(`pt_common.glsl:47`). The two terms have different *sources* and so take
different gates: outdoor haze comes **from the sky** and must vanish under a roof;
goo outgassing and hell's haze are properties of **the room** and must not.

where **`skyExposure ∈ [kIndoorFogScale, 1]`**: `1` under open sky, `kIndoorFogScale`
(a small `const`, ~`0.02`..~`0.1` — `0` is struck, see below; Q12) under a roof. **As shipped in L1b the
value is binary per sample** — exactly `1` (the open-sky test passed) or exactly `kIndoorFogScale`
(it failed); the interval names the tunable endpoints, not a graded
per-sample value. The *smooth* indoor↔outdoor gradient in the final image emerges from
many binary samples averaged across the march + the half-res denoise (§4.6).
**Superseded on the indoor side by the 2026-07-25 amendment at the end of this
section** — the indoor branch becomes a position-dependent seep; the open-sky branch
(`= 1`) is unchanged. **The *test* that picks the branch is superseded in turn by the
2026-07-27 amendment (DOOM-0276)** — it is a field lookup, not a ray. The two values
either branch yields are untouched by that change.

**How `skyExposure` is measured — per march sample ("true volumetric", user's
explicit pick 2026-07-24 over the cheaper per-surface flag). The MECHANISM below —
one ray per sample — is what L1b shipped and what the 2026-07-27 amendment replaces;
the *granularity* it argues for (per sample, not per surface) survives that change.**
At each sample point
`p` the march casts **one shadow ray straight up** (world `+Z`) with the standard
shadow-ray cull mask **`0x01`**, over a finite length `kFogMaxDist` (so a ceiling more
than `kFogMaxDist` = **2048** units overhead reads as open sky — harmless in vanilla geometry, but state it
rather than implying an unbounded ray). That mask sees only solid world geometry — the sky
backdrop is a separate TLAS instance on mask `0x04` (`r_vulkan.cpp:2020`) that
"primary rays only" see (`:1923`), so a `0x01` up-ray **cannot hit the sky instance**.
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
measured it on hardware, and **the up-ray shipped** (1345c92). The fallback below was
not needed and is retained as a **standing perf lever** — a **per-surface openness bit**
with near-zero cost:
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
  flags word is passed at both call sites, `pathtrace.comp:1079` / `:1208`), so
  `marchFog` just tests `(h.matFlags & uint(RB_MESH_OUTDOOR)) != 0u` (the shader-side
  mirror carries the same name as the C-side bit, so there is one name, not two).

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
> **User target for the seep, given 2026-07-27 while looking at the shipped outdoor layer:**
> *"that is how I want the fog indoors in rooms that have a window / door to the outside."*
> "That" is the shallow knee-height bank — `kFogIndoorPool = 18` at roughly the pre-112 outdoor
> thickness. So the seep's job is to raise `skyExposure` from the `kIndoorFogScale` floor toward
> **≈ 1.0** near an opening, not toward the outdoor bank's full strength (which pairs with the
> 112-unit pool, not the 18-unit one). `kSeepMax ≈ 0.5` below is therefore probably **too low** —
> re-judge it against the shipped indoor pool when L1d lands. Until then interiors stay near-clear
> at the flat `kIndoorFogScale = 0.05`, which is deliberate: nothing today can tell "room with a
> window" from "room buried three doors deep", and the user was explicit at L1b that fog belongs
> *"outside and not inside."*

- **`kSeepMax` ≈ `0.5`** — even standing in the doorway, indoor air reaches at most
  half outdoor strength. This is the "a little bit" the user asked for; it must not
  become a second outdoors.
- **`kSeepFalloff` ≈ `192`** DOOM units — roughly a doorway-to-back-wall depth (Q16).
- **The open-sky test still decides `openSky`, and the seep does not touch it.** (Which
  test that is changed on 2026-07-27 — see the DOOM-0276 amendment; at the time this
  paragraph was written it was the up-ray.) The seep replaces only the *indoor*
  constant, so the outdoor look L1b shipped is preserved **exactly** (`openSky` → `1.0`
  on both sides of the amendment). Per sample the value is still one of two branches;
  what changed is that the indoor branch now varies with position instead of being
  constant.

**Where `d` comes from — a load-time distance field (user's pick 2026-07-25, chosen
from three offered options).** At level load, lay a coarse **2-D grid** over the map's
XY extent (start `64`-unit cells, matching DOOM's flat grid), seed every cell in an
open-sky sector (`ceilingpic == skyflatnum`) at `d = 0`, then **flood outward through
connected open space only**, so the fill travels through doorways and archways but never
through a wall.

**The connectivity test is an OPENING test, not a one-sidedness test.** A step to a
neighbouring cell is allowed only where the linedef between them is **two-sided *and* the
two sectors' openings overlap** — `min(front.ceilingheight, back.ceilingheight) >
max(front.floorheight, back.floorheight)`. Testing merely "not one-sided" is **wrong**
and would break INV-12: in vanilla DOOM a **closed door is a two-sided linedef** whose
sector has `ceilingheight == floorheight`, as are windows, ledges and raised lifts. Under
a one-sidedness test the flood would pour straight through every shut door, seeping fog
into a door-sealed closet beside a courtyard. This is exactly `P_LineOpening`'s `openrange > 0`
(`p_maputl.c:300-331`), so reuse it rather than re-deriving it. Doors are evaluated at
their state **at flood time**, and since **DOOM-0281** (2026-07-27) the field is re-flooded
whenever one of those `openrange > 0` answers actually flips — so a wall that opens in play
does let the fog in, and INV-12 still holds because connectivity is re-decided from real
openings rather than assumed (Q22).

**Flood over SEGS, then rasterise — not over grid cells.** A cell-to-cell test cannot work,
for two reasons. First, two adjacent 64-unit cells have no single "linedef between them" to
test. Second, comparing the two cells' *sector heights* says **nothing about whether a wall
stands between them** — a courtyard and a sealed closet with matching floor and ceiling
heights pass any height comparison trivially. Either way the flood walks straight through
the wall and breaks INV-12 in the common case. One-sidedness lives on the **linedef**, so
the traversal must follow linedefs:

**Adjacency comes from SECTORS, not subsectors — but the search's nodes are the PORTALS between
them (step 1).** This matters and is easy to get wrong.
Vanilla DOOM has **no minisegs**: `P_LoadSegs` gives every `seg_t` a `linedef`
(`p_setup.c:196-198`), because the SEGS lump only ever contains linedef-backed segs. So
two BSP leaves of the *same room*, split by a partition line, share **no seg at all** —
a subsector graph built from segs would leave every multi-leaf hall or courtyard
disconnected, and `d` could not propagate inward from a doorway. A **sector** is the
room, and segs give sector-to-sector adjacency through exactly the linedefs whose
openings we want to test, so the partition problem disappears rather than being worked
around. What the search *settles* is nevertheless one value per **portal**, never one per sector —
step 1 gives the reason.

1. **Nodes = portals, not sectors** — the search state has to be the *opening*, because
   step 3 needs two portals of the **same** sector to carry **different** distances, and a
   sector-indexed Dijkstra can only ever settle one value per sector (which is exactly the
   flat-per-room result step 3 forbids). A **portal** is one surviving `seg_t`, sited at
   its midpoint. A seg survives iff it has a `backsector`, its `linedef` is two-sided **and**
   `P_LineOpening(seg->linedef)` leaves the file-scope global `openrange > 0`
   (`p_maputl.c:300-331` — it returns **`void`** and writes `opentop`/`openbottom`/
   `openrange`/`lowfloor`; call it and read the global, don't re-derive it, and keep the
   flood **single-threaded** since those globals are not re-entrant). A one-sided seg is a
   wall and contributes no edge. **Also require `linedef->frontsector !=
   linedef->backsector`:** a self-referencing sector (the vanilla deep-water / fake-wall
   trick) is two-sided with a full-height opening but is *drawn* as a solid wall, so
   without this test the flood walks straight through it — the INV-12 leak in another
   costume. A one-sided seg is a wall and yields no portal.
2. **Edges join two portals that share a sector**, weighted by the distance between their
   midpoints — that is the path fog would actually travel across that room. **Seed** every
   portal at `d = 0` if *either* of its sectors has `ceilingpic == skyflatnum`, then run
   Dijkstra from the whole seed set at once. Weights are non-negative and the graph is
   finite, so it terminates. The result is `d(portal)` for every opening in the map: how
   far that doorway is, through open space, from outdoor air.
3. **Resolve `d` per GRID CELL, not per node.** For each 64-unit cell, take
   `d(cell) = min over the portals of the cell's own sector of ( d(portal) + |cell centre
   − portal| )`, clamped to `dMax`; an outdoor cell is `0`. **A per-node value would
   defeat the whole feature:** `d` would be constant across an entire room, so
   `exp(-d/kSeepFalloff)` would step abruptly at the room boundary and hold flat inside —
   which is precisely the behaviour rejected as option (C) below and precisely what the
   user asked to soften. The gradient must come from the cell's distance to its own
   doorway. Cells that resolve into **void space** (outside any sector) take `dMax`.

`R_PointInSubsector` maps a cell centre to its leaf and thence to `->sector`; `r_mesh.c`
already walks the BSP and already calls it, so the machinery is in hand.

Upload as a small single-channel 2-D texture (§5); the march takes **one bilinear tap**
at `p.xy` per sample. No rays, no noise, and its load-time cost is budgeted at **≤ 20 ms
on E1M1** (§7, L1d).

**The three degenerate cases, pinned so the implementer does not have to guess:**
- **A level with no open sky at all** (most hell maps: no sector has `ceilingpic ==
  skyflatnum`). The seed set is empty, so **every cell gets `dMax`** and the seep term
  collapses to exactly `kIndoorFogScale` — i.e. the shipped L1b look, unchanged. This is
  the correct behaviour, not a failure: with no outdoors there is nothing to seep in.
- **Unreachable cells** (a sealed room, and every cell in void space outside any sector)
  take the **finite** sentinel `dMax = 8 · kSeepFalloff`. It must be finite: a half-float
  `+inf` multiplied by a zero bilinear weight yields `NaN`, which would propagate into
  `σ` and blow the whole march.
- **A map whose XY extent exceeds the field budget** (§5 sizes it for ≤ `256×256` cells):
  **double the cell size and rebuild**, repeating until it fits. Coarser cells only blur
  the seep's edge (Q19); they cannot break INV-12, because connectivity was decided on
  the seg graph before rasterisation.

**Sampler state is part of the contract:** `CLAMP_TO_EDGE` on both axes. A march sample
can legitimately land outside the map's XY box (the `tHit` clamp lets `p` run past the
geometry toward the sky backdrop), and under `REPEAT` an outdoor `d = 0` at one map edge
would wrap onto indoor air at the opposite edge. For that to be true rather than merely asserted, **pad the grid by one cell beyond the
map's XY bounding box**: the padding ring lies outside every sector, so it takes the void
value `dMax` by the rule above, and `CLAMP_TO_EDGE` then extends *that* outward. Without
the padding a level whose outdoor sector runs flush to its bounding box would clamp an
outdoor `d = 0` out past the map edge — the opposite of the guarantee.

**Why a 2-D field is sufficient here.** Vanilla DOOM has **no room-over-room** — any XY
column belongs to exactly one sector, and `r_mesh.c` emits exactly one floor and one
ceiling cap per subsector — so projecting to XY loses no *topology*.
(Source-port-style 3-D floors would break this; DOOM_Ants renders vanilla geometry, so
it is out of scope.) It is deliberately **not** claimed to be *exact*: `d` is a
grid-quantised, 64-unit-cell connected distance, not a true geodesic (Q19), and it is
**height-invariant**, so air near the ceiling of a tall hall reads the same `d` as air at
the floor. Sufficient for a soft seep; not a distance oracle. (Until 2026-07-27 there
was a contrast to draw here: `openSky` was fully 3-D while `d` was not. The DOOM-0276
amendment moves `openSky` onto this same grid, so both now share its quantisation —
which is the whole of what that amendment costs.)

**Rejected alternatives** (all three were put to the user): **(B) extra sky-visibility
rays per sample** — no load-time work and correct in full 3-D, but it multiplies the
march's ray cost and adds sparkle at half-res; **(C) the per-room `RB_MESH_OUTDOOR` flag
as the indoor grade** — free, but fog would step abruptly at the room boundary rather
than drifting in, which is precisely the behaviour the user asked to soften. Note (C)
here is a **different role** from the same flag's use above as the up-ray's *perf
fallback*; the two are independent.

**2026-07-27 amendment (DOOM-0276) — the up-ray becomes a field lookup.** The A/B
measurement in §6 put the whole fog at **+8.38 ms / +34.7 % present-total**, of which
**7.93 ms is inside the megakernel** — over the ≤ 15 % gate, and the up-ray is the pole:
one ray query per sample × `kFogSteps` = 24 × every fog pixel.

**It does not need a ray, and the reason is the same one §4.3a's own "why a 2-D field is
sufficient" paragraph already gives.** Vanilla DOOM is flat-mapped — `R_PointInSubsector`
takes `(x, y)` only and returns exactly one sector, hence exactly one `ceilingpic`. "Is
there sky above this point" is therefore a **pure function of XY**, and the up-ray was
doing 3-D work on a 2-D question. The field L1d builds is indexed by exactly that key.

**Mechanism: a second channel, not a threshold on the first.** *(Format superseded
2026-07-30: DOOM-0289 widened this image again, to `RGBA16F`, adding the sun-clearance
interval on `.b`/`.a` — §4.4's amendment. Nothing about the `.r`/`.g` channels described
here changed, but a snippet below that reads `.rg` into a `vec2` now reads a `vec4`.)*
The seep field becomes
`R16G16_SFLOAT` — `R` = the through-open-space distance `d` exactly as L1d shipped it,
`G` = an **open-sky mask**, `1.0` where the cell centre's sector has `ceilingpic ==
skyflatnum`, `0.0` otherwise (and `0.0` on the void ring, which must read roofed for the
same reason it reads `dMax`). The march's existing single tap answers both questions:

```glsl
vec2  seep    = texture(uSeepField, worldToSeepUV(p.xy)).rg;
bool  openSky = seep.g > 0.5;
float seepD   = seep.r;
```

**Why the mask cannot be `d < ε`.** `d = 0` does mean "outdoor cell", but a *roofed* cell
one step inside a doorway carries a `d` of only a few units — the portal it walks to is
seeded at zero. The two are not separable by any epsilon, and an epsilon that tried would
put the full outdoor bank inside the first room behind every door. The mask is a distinct
fact and gets a distinct channel.

**What the change costs, stated so it can be judged on screen.** Three differences from
the ray, none of them free:
1. **The roofline moves onto the grid.** Bilinear + a `0.5` threshold puts the boundary
   midway between differing cell centres, so it is accurate to **half a cell (32 units)**
   and follows the grid rather than the wall. The mist wall at a doorway threshold softens
   and may sit up to 32 units either side of the door. This is the one visible cost.
2. **Height-invariance.** The ray was 3-D; the field is not. Air under a roof more than
   `kFogMaxDist` = 2048 units up used to read *open sky* (the ray ran out) and now reads
   roofed — the field is the more correct of the two here.
3. **The void ring reads roofed** where an unbounded ray would have missed. This one is
   free, but only after a **latent bug in L1d had to be fixed to make it so** (found by
   the review of this amendment). Two facts have to hold together:
   - *No march sample leaves the map's XY bounding box.* `marchFog` is called only on the
     surface-hit branch (`pathtrace.comp:1255`, `:1384`) — sky pixels take §4.6a's closed
     form and never march — so every sample sits on the segment from the camera to a real
     geometry hit. Both endpoints are inside the box, so the whole segment is.
   - *No in-box sample can weight a ring cell either.* That is what the ring being **one
     full cell outside** buys — but it only holds if the interior cells reach **past**
     `maxX`/`maxY`, and L1d sized the grid with a **truncating** divide
     (`(int)((maxX-minX)/cell) + 3`), which left the last interior centre short of `maxX`
     by up to a cell. Real air along the `+X`/`+Y` edges therefore got a bilinear weight on
     the void's sentinel. Fixed to `ceilf` in the same change; `centre(gw-2) = minX +
     ceil(Δ/cell)·cell ≥ maxX` is what makes the ring provably unreachable. E1M1's grid
     goes 74×47 → 75×47, so this was live, not theoretical.

**What it does not change.** Both branch *values* (`1.0` and the seeped indoor grade), the
`σ` composition (INV-9), the sky-backdrop closed form (§4.6a — it never cast an up-ray),
the field's build, its sampler, its transform, and the tap count in `marchFog`. The only
new bytes are the field's second channel — it doubles a small texture: `≤ 256 × 256 × 4` =
256 KB at the worst-case grid, and E1M1's actual 75 × 47 field is 14 KB.

**Not to be confused with the `RB_MESH_OUTDOOR` fallback** named above and in §6. That
lever traded the doorway cutoff for **whole-view** granularity and would have coarsened
the seep with it; this keeps per-sample granularity and coarsens only to the cell.

### 4.3b The Silent Hill 2 look — drifting two-octave wisps

> **User play-test of the SHIPPED fog, 2026-07-26 — read before tuning L1c.** Verdict on L1/L1b at
> High: *"I really like the fog, it can be slightly darker though. It is quite bright when
> outside."*
>
> **This is in tension with L1c as specified, and the tension must be resolved deliberately.**
> The outdoor brightness comes from the sky in-scatter term, gated to full strength outdoors by
> L1b's `skyExposure`: `skyAmbient = SKY_COLOR * kSkyShaftStrength`. **Answered 2026-07-27:**
> `kSkyShaftStrength` went `1.0` → **`0.85`**, and the same gain was added to both sky closed
> forms, which had omitted it — a no-op only while the gain was 1.0, and a visible seam the moment
> it moved. This constant is the right knob because it scales in-scatter *brightness* alone; how
> much the fog hides is `kFogBaseDensity`'s job.
>
> **Settled by what L1c actually shipped (2026-07-30).** L1c planned two brightness-raising
> moves and made only one. The near-white tone landed — `kFogColor` ships at
> `(0.55, 0.56, 0.56)`, neutral by construction. The **≈2× `kFogBaseDensity` raise was tried
> and reverted**; density still ships at `0.0033`. So `kSkyShaftStrength` = 0.85 was re-checked
> against a smaller change than this passage anticipated, and it held.
>
> **What L1c must do about it:** treat "slightly darker outside" as an acceptance criterion, not a
> later polish pass. The cheapest lever is `kSkyShaftStrength` (a single `const`, no new plumbing);
> the near-white `kFogColor` can also be taken down in value without losing its colourlessness —
> near-white is about *saturation*, not brightness, and a light grey satisfies the Silent Hill 2
> reference as well as a near-white does. Do **not** reach for `kFogBaseDensity` to fix
> brightness: the wisps are a multiplier on density (§4.3b), so lowering it flattens the
> billows along with the brightness.
> This is the same trap Q24 guards on the sky side.
>
> **The user likes the effect** — this is a tuning note, not a rejection. Judge it at High
> strength outdoors, which is where they saw it.

**The target (user 2026-07-25, with reference screenshots).** Silent Hill 2 (original
PS2, 2001): **near-white, colourless** fog, thick enough that the world fades toward
flat grey at middling range, and — the quality L1b's uniform haze misses — full of
**billowing wisps of visibly varying thickness that drift slowly past**.

**What SH2 actually does.** Researched 2026-07-25; it is three stacked things, not one.
**One caveat on the evidence up front:** the `[FOG]` values below are reverse-engineered
from the **PC port** by an enhancement project, so its "original" column means *the PC
port's* originals — PS2 parity is assumed, not verified. Another reason the two-octave
design is an analogy rather than a derivation.
1. Hardware **distance fog** — the base grey-out. This is what L1/L1b already are.
2. **Two fog layers combined at different densities and alphas**, at least one of them
   scrolling. The Silent Hill 2 Enhancements project's reverse-engineered `[FOG]` block
   gives as **original** values: both layer alphas `128`, layer-1 scroll `0.125`,
   layer-2 density multiplier `1.0` with offset `0.0`. Its **modified**
   (Enhanced-Edition) values are layer-1 X scroll `0.250`, layer-2 alpha `90`, density
   multiplier `1.4`, offset `100.0`.
   **Read that evidence carefully — it is thinner than it first looks.** No layer-2
   scroll rate appears anywhere in the block, so "two layers scrolling at *different
   speeds*" is **not** established by it. What *is* established: two layers, combined at
   differing density and alpha, with motion on at least one. That is still enough to
   motivate more-than-one-octave — a single layer reads as dirty glass — but the
   two-octave design below is an **analogy**, not a derivation from these numbers.
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

```
σ_final = ( kFogBaseDensity · skyExposure                 // sky-sourced haze (§4.3a)
          + Σ_profiles areaDensity(profile) · areaMult(profile) )   // §4.5
          · heightPool                                     // §4.3
          · wisp(p, t)                                     // §4.3b
          · fogStrengthScale(rb_fog)                       // the `;` dial
```

**This is the single authoritative statement** — §4.3a and INV-9 point here rather than
restating it. Three things the form has to carry: `skyExposure` gates **only** the
sky-sourced term (§4.3a); the profile term is a **sum over profiles**, each with its own
`areaDensity`, because goo takes a `const` while hell takes the per-level `misc6.w` and
§4.5 requires a goo room *on* a hell level to get both; and `wisp` multiplies the whole
medium, so goo and hell billow too.

where `wisp` is two octaves of value noise read from a single **3-D noise volume**
(§5), each drifting on its own slow velocity:

- `wisp(p,t) = 1 + kWispAmp · (A + kWispWeight2 · B) / (1 + kWispWeight2)`, with
  `A = 2·noise(f₁·(p + v₁·t)) − 1` and
  `B = 2·noise(f₂·(p + v₂·t) + kWispOffset2) − 1`.
- **The maths symbols above have C identifiers** — every other constant here is a `kXxx`,
  so name these too rather than leaving the implementer to invent them: `f₁` =
  **`kWispFreq1`**, `f₂` = **`kWispFreq2`** (= `2.5 · kWispFreq1`), `v₁` = **`kWispVel1`**,
  `v₂` = **`kWispVel2`**, plus **`kWispOffset2`** — a constant lattice offset (start
  `vec3(17.3, 5.1, 23.7)`) that **decorrelates the two octaves at `t = 0`**. Both taps read
  the *same* volume, so without an offset they are phase-locked at the world origin and the
  finer octave contributes nothing there.
- **Sampling convention — state it before reading any number below.** `noise(u)` is a
  trilinear fetch from the `N³` volume at **texture coordinate `u / N`**, wrapped `REPEAT`,
  where `N` is the volume's edge in texels (**64** today, §5). So `u` is in *lattice*
  units: one texel spans `1/f₁` world units and the volume repeats every `N/f₁` world
  units. Every number below is derived from `N = 64` and scales if the volume is resized. The velocity sits **inside** the `f₁` scale,
  so `v` is a plain world-units/second velocity (mirrors the house precedent
  `kGrimeWorldScale`, `pathtrace.comp:111`). Writing `noise(p·f₁ + v·t)` instead —
  velocity *outside* the scale — would drift the field `1/f₁` = 512× too fast, crossing a
  whole map every half-second.
- **`kWispAmp = 0` is an exact no-op** — it follows from the *multiplicative* form
  (`1 + 0·x ≡ 1`), so the wisp term can be switched off without touching anything else.
  **But mean-1 does NOT mean "same look" at non-zero amplitude.** Transmittance is
  `exp(−∫σ dt)` and in-scatter is weighted by it — both curves, not straight lines — and
  averaging *through* a curve is not the same as taking the curve of the average. So at
  `kWispAmp = 0.6` the fog reads measurably **thinner** on average than un-wisped fog of the
  same base density. That is ordinary maths, not a bug. It interacted directly with the ≈2×
  density raise below — **which was tried and reverted 2026-07-30** — and the standing rule
  survives the revert: **base density is re-tuned with wisps on**, never inferred from
  the un-wisped value.
- **Amplitude is large, not a wobble.** `kWispAmp` starts ~`0.6`, bounding density to
  `1 ± kWispAmp` = **`0.4×`..`1.6×`** of base (and value noise rarely reaches its
  extremes, so the practical swing is narrower). That swing *is* the "various thickness"
  the user asked for; a ±15 % grain would read as noise, not billows.
- **Octave 2 is finer and fainter:** `f₂ ≈ 2.5·f₁`, `kWispWeight2 ≈ 0.7` — a **chosen
  starting value, not an SH2-derived one**. (An earlier draft justified `0.7` as SH2's
  `90/128` alpha ratio. That is wrong twice: `90` is the Enhanced Edition's *modified*
  alpha — the original is `128`, a ratio of `1.0` — and a 2-D compositing alpha is not
  an octave weight in a volumetric march.)
- **Octave 1 scale `f₁ = 1/512`** — under the convention above, one **texel** spans
  `1/f₁ = 512` DOOM units, a touch wider than a large room, so billows read at room scale
  rather than as fog-coloured static; and octave 1 repeats every `N/f₁ = 32768`
  units — but **the binding period is the finer octave's**: `N/f₂ = 64/(2.5/512) = 13107`
  units is what tiles first, still comfortably longer than any vanilla map's longest
  sightline, which is the claim that actually matters (Q21). Both
  numbers depend on the convention being implemented as stated — get it wrong and you get
  512-unit tiling with 8-unit features, i.e. exactly the two failure modes this avoids.
- **Drift is slow and mostly horizontal**, with `v₁` and `v₂` differing in **direction**
  as well as speed so the octaves never lock into a repeating pattern. Start
  `v₁ ≈ (8, 3, 1)` (‖v₁‖ ≈ 8.6) and `v₂ ≈ (−3, 4, 0.3)` (‖v₂‖ ≈ 5.0) DOOM units/s — the finer
  octave drifts **slower**, not merely elsewhere: at equal speeds the two octaves read as one
  field being advected rather than as two layers at different depths. Erring **slow** is the
  SH2-authentic direction.
- **The tap lives in `marchFog` (`pathtrace.comp`), NOT in `fogDensity`.** `fogDensity`
  sits in `pt_common.glsl`, which `bake.comp` `#include`s verbatim — putting a sampler
  there would force the bake to declare and bind the noise volume, contradicting §5's
  "the bake stays functionally untouched" and INV-6.
- **Time comes from `misc6.x`** — DOOM-0183's ripple-time lane (float seconds from a
  `steady_clock` zeroed at first use, `r_vulkan.cpp:7455-7457`), already
  frame-rate-independent and already in the push block. **No new push lane** (INV-5).

**Step count and the wisps — the hypothesis, and its falsification.** L1c raised `kFogSteps`
**24 → 40**, and this section owned that decision because it is the wisps that were thought to
force it. **It was reverted on 2026-07-30**: measured on captured frames, 40 vs 24 scored MAE
0.153/255 at Low and 2.86 at High, while the *same build* scored 2.41 against its own second
run — i.e. at or under the engine's own noise. Q26's quadratic march is what actually resolves
the near-camera layer, and it does so at 24. **`kFogSteps` ships at 24.** The reasoning below
is kept because it is the argument the measurement answered: a *flat* haze integrates smoothly at
24 steps, but a **structured** density field is a high-frequency signal along the ray, and
undersampling it bands — the same reason a froxel fog needs more slices than a constant
one. That is a **hypothesis to confirm at L1c by looking**, not an estimate: if 24 steps
read clean with wisps on, bank the budget and leave it. The cost is **not independent of
the resolution question** — ×1.67 on steps and ×4 on pixels both multiply the per-sample
up-ray that §4.3a calls the march's dominant cost (§6 item 2). Together with the ≈2×
density raise below (**proposed, then reverted 2026-07-30**), this is what L1c's ≤ 8 % cumulative spot-check (§7) measures.

**Colour and thickness.** In-scatter tone moves from `SKY_COLOR`
(`vec3(0.20, 0.26, 0.40)`, cool blue, `pt_common.glsl:31`) to a near-white desaturated
`kFogColor` — start ~`vec3(0.55, 0.56, 0.56)` in linear radiance: **brighter *and*
colourless**, so distance reads as *pale* rather than merely dim. **The value is defined in
linear**, but the sky branch it also feeds writes a *display-encoded* colour (`skyPanorama`'s
output is deliberately not tonemapped, `pathtrace.comp:1293-1299`), so whether the same
numeric triple is correct on both branches is **Q9's encode question** — resolve it there,
do not assume the number transfers unchanged. `kFogBaseDensity`
was to rise ≈2× (from `0.0033` toward ~`0.0066`) — **proposed, then reverted on 2026-07-30;
`kFogBaseDensity` ships at `0.0033`.** The reasoning is kept for any future density move. It was
deliberately **not** the ~3× a wisp-free
haze would be *estimated* to need (an estimate, not a measurement — no density-to-look
tuning has been done yet), because structure sells the look at lower average density, which also
keeps enemies readable. Both tune on hardware, and the `;` strength dial still scales
the whole thing.

**The sky backdrop shares `kFogBaseDensity` — doubling it would cost the mountains most.**
*(Historical: the doubling below was proposed for L1c, tried, and reverted on 2026-07-30 —
density ships at `0.0033`. The coupling it describes is real and still governs any future
density move, which is why the reasoning is kept in the conditional.)*
§4.6a's aerial-perspective term is a closed form using the *same* density constant, so doubling
density **squares** the transmittance everywhere — which costs far more where optical depth is
already high, i.e. on the sky.

**Rewritten 2026-07-27 — the sky's haze is now GEOMETRIC, so there is no single number to
quote.** §4.6a's closed form takes the exact slant path through the layer, `H / rd.z`, clamped
at `kFogSkyDist`. Haze therefore varies with how high up the sky pixel is, which is the whole
point — the mountains rise out of the mist instead of sitting behind a uniform wash. At High,
standing on E1M1's courtyard (65 units above the fog altitude), by elevation above the horizon:

| 1° | 5° | 10° | 20° | 30° |
|---|---|---|---|---|
| 98 % | 91 % | 70 % | 45 % | 34 % |

Doubling `kFogBaseDensity` would raise every one of those. There is no longer a "halve
`kFogSkyDist` to cancel it" trick, because the clamp only bites within a couple of degrees of
the horizon — above that the path length comes from `kFogPoolHeight / rd.z` and is untouched by
it. **So a density raise genuinely moves the mountains** — which is part of why L1c's was
reverted — and the honest levers are
`kFogPoolHeight` (a shallower layer clears the sky faster with elevation) or a sky-only density.
Take the decision with the screenshot in hand: "distant sky still readable at High" is an
explicit L1c acceptance item (§7).

**What the sky does about wisps.** §4.6a's sky term is a *closed form* precisely because
a sky ray sees constant density — which the wisp modulation breaks. v1 resolves this the
cheap way: **the sky backdrop keeps the un-wisped closed form** (`wisp ≡ 1` for sky
pixels). The mountains are far enough that billow structure on them would be
sub-pixel anyway. The risk this creates is at the **sky/wall seam**, where wisped
foreground meets un-wisped sky — the exact seam §4.6/Q9 already protects — so "no visible
discontinuity at the sky/wall seam with wisps on" is an L1c acceptance item too. INV-10
is amended to record that the closed form is *deliberately* wisp-free rather than
silently inconsistent.

**Keep it from pooling.** SH2 fog is vertically uniform. L3's height pooling (§4.3)
must stay **gentle** or it will undo this; `kFogPoolHeight` is a look-tune to be judged
**with** the wisps present, not before (Q17).

**How the near-white base coexists with coloured fog (user wrinkle, 2026-07-25:
*"the fog must be lit with any relevant colours but only where it makes sense — like in
Hell for example"*).** `kFogColor` is **not** a global override; it is the **clear
profile's** base tone, and §4.5's area profiles still multiply it. The composition rule
of §4.3 is unchanged and is what makes this work:

```
fog colour = (light colour: sky kFogColor / emitter Le) × mediumTint    (§4.5)
```

- **Earth-side maps** (E1 Knee-Deep, most of DOOM II's city run) sit in the **clear**
  profile → `mediumTint` is neutral → the fog reads **SH2 near-white**. This is the
  default and the majority of play.
- **Hell levels** take `kHellTint` → the same wisps, same drift, but lit **red/ember**.
  The SH2 grey is deliberately *departed from* there, which is the point of the user's
  "only where it makes sense".
- **Goo/nukage rooms** take `kGooTint` → sickly green pooling, per §4.5.
- **Emitter-lit fog is already coloured** by construction (§4.4(b)): a torch shaft
  inherits its emitter's `Le`, so fog near a flame goes warm without any new mechanism —
  a warm core against the near-white surround, which is exactly the SH2 street-lamp
  look.

So the SH2 amendment changes **what "neutral" means** — from cool blue to near-white —
and leaves the colouring machinery of §4.4/§4.5 untouched. The judgement of *where*
colour makes sense stays §4.5's profile selection (level flag + primary-hit liquid
flag), tuned at **L4**; nothing here pre-empts it. The one new caution: `kHellTint` and
`kGooTint` were picked against a **blue-grey** base and must be **re-judged against the
near-white base** at L4, or they will read washed-out (Q20).

### 4.3c Two layers: the aerial layer and the floor fog (2026-07-27 amendment)

> **SIGNED OFF — the single-layer fog, 2026-07-27.** Everything §4.3, §4.6a and INV-10 describe
> above is user-accepted on hardware: *"The fog / mist looks significantly better now. Great work
> and the horizon fix is much, much better… happy to sign it off now."* One residual noted and
> **accepted, not fixed**: *"You can still see the cut off but I am happy with it."* Do not
> re-open that without a new report — it survived two rounds of correction and the remaining
> visibility is the sky texture's own 128-row limit, not the fog.

> **The request that follows it**, in the same breath: *"I want fog thickest on
> the floor. So, for those indoor rooms that are affected, they should have fog closest to the
> floor with a much smaller distance to the camera setting than the general fog. In fact, the same
> effect should apply outside as well… for outside you will have the general fog and the floor
> fog. Outside, the floor fog can probably be thicker."*

**Why one layer cannot deliver this, and no amount of tuning will change that.** Today's fog is a
pure participating medium: opacity along a ray only ever *grows* with distance. So the density
that makes the air at your feet visibly misty is the same density that, integrated over a thousand
units, turns the far end of the courtyard into a white sheet. The two wants are in direct conflict
inside one term. Every re-tune on 2026-07-27 traded one against the other.

**The second term breaks the conflict by not being a medium.** The floor layer's density falls off
with **distance from the camera**:

```
σ_floor(p, t) = kFloorFogDensity
              · exp(−max(0, p.z − baseZ) / kFloorFogPool)   // hugs the ground
              · exp(−t / kFloorFogRange)                    // and only NEAR you

// the two layers simply ADD, and the whole sum carries the existing two
// multipliers — the `;` dial and the open-sky gate — exactly as today:
σ(p, t) = (σ_general(p) + σ_floor(p, t)) · fogStrengthScale · skyExposure
```

with `kFloorFogPool ≪ kFogPoolHeight` (112) and `kFloorFogRange ≪ kFogMaxDist` (2048).
The multipliers are written into the block deliberately: the shipped `marchFog` line is
`fogDensity(p, baseZ, poolH) * strength * skyExposure`, and a block that shows only the bare
`σ_floor` is the one an implementer copies.

**First guesses** (tune on hardware; the shipped aerial pair is `kFogBaseDensity` = 0.0033,
`kFogPoolHeight` = 112):

| constant | first guess | what it means |
|---|---|---|
| `kFloorFogDensity` | **0.010** | 3× the aerial layer's density *at the floor itself* |
| `kFloorFogPool` | **24.0** | e-fold height — knee-deep, so it reads as mist you wade through |
| `kFloorFogRange` | **256.0** | e-fold distance from the camera |

The arithmetic that picks them, so a re-tune knows what it is trading: the eye rides
`kEyeAboveFloor` = 41 units up, so at eye level `σ_floor = 0.010 · exp(−41/24) = 0.0018` and a
horizontal eye-level ray accumulates at most `σ · kFloorFogRange ≈ 0.46` optical depth — **≈37 %
haze**, against the aerial layer's 16 % at 512 units. At the floor it is the full 0.010, 3× the
aerial density there. Indoors both layers are additionally scaled by `kIndoorFogScale` = 0.05.
`kFloorFogRange` = 256 is also the second column of Q26's error table (0.09 % with the shipped
warp), so the march resolves it without banding.

**This is deliberately not physical, and that is the point.** Real fog has no idea where the
camera is. But a medium whose *visible contribution* is bounded in range is exactly what lets mist
pool around the player's feet without accumulating into an opaque wall at distance — the trick
that makes ground mist readable in games that ship it. It costs nothing to evaluate: `marchFog`
already has `t`, the distance along the ray, as its loop variable.

**Placement follows §4.3a's existing gate, with its own strengths.** Both layers are scaled by the
same `skyExposure`, so the floor fog inherits the open-sky test and the L1d seep for free — no
second placement mechanism, no second up-ray. Only the density constants differ:

| | outdoors | roofed air |
|---|---|---|
| aerial layer | `kFogBaseDensity`, `kFogPoolHeight` = 112 | × `kIndoorFogScale`, `kFogIndoorPool` = 18 |
| floor fog | `kFloorFogDensity` | `kFloorFogDensity` × `kIndoorFogScale` |

The user asked for the outdoor floor fog to be **thicker**; that falls out of `skyExposure`
already, without a fourth constant.

**The sky backdrop needs the floor term too — this is not negligible.** Sky pixels never enter
`marchFog`; they take the closed form `skyFogOpticalDepth()` (§4.6a, INV-10). Leave the floor layer
out of it and a horizon-grazing sky pixel disagrees with the wall pixel directly beneath it by
`σ_floor(eye) · kFloorFogRange` = 0.0018 × 256 ≈ **0.46 optical depth, ~37 % haze** — a hard line
along the skyline, which is the exact defect §4.6a was written to remove. So the closed form gains
a second addend, and it has an exact answer for the same reason the aerial one does:

```
σ_f0 = kFloorFogDensity · exp(−h₀ / kFloorFogPool)        // density where the eye is
a    = |rd.z| / kFloorFogPool − 1 / kFloorFogRange
t₁   = h₀ / |rd.z|                                        // where a falling ray reaches the base

rd.z ≥ 0 (ascending):  τ_floor = σ_f0 / (rd.z / kFloorFogPool + 1 / kFloorFogRange)
rd.z < 0 (descending): τ_floor = kFloorFogDensity · [ (e^(−t₁/R) − e^(−h₀/P)) / a
                                                    + kFloorFogRange · e^(−t₁/R) ]
```

(`P` = `kFloorFogPool`, `R` = `kFloorFogRange`, `h₀ = max(0, ro.z − pc.fogFloorZ)` — the same `h₀`
the aerial branch uses.) Three properties that make it safe, each checkable by substitution:

- **The branches meet exactly at `rd.z = 0`,** both tending to `σ_f0 · kFloorFogRange`. That is the
  test to run before believing an implementation: a seam at the horizon is what the 2026-07-27
  plateau fixes were about.
- **`a` passes through zero** when `|rd.z| = kFloorFogPool / kFloorFogRange` (0.094 with the first
  guesses — well inside the visible range, not a corner case). Expand the first term as
  `σ_f0 · t₁ · (1 + a·t₁/2)` near there; the naive quotient loses its precision to cancellation
  exactly where the two must agree, the same trap `(e^x − 1)/x` already carries in the aerial branch.
- **No `kFogSkyDist` analogue is needed.** The aerial layer needs a horizontal extent because an
  exponential layer grazed at the horizon integrates to infinity; the floor layer's own
  `exp(−t/kFloorFogRange)` already bounds it. Do **not** add a `min()` here (§4.6a).

**The indoor half depends on L1d and the outdoor half does not.** Nothing in the tree can yet
distinguish "room with a window onto the courtyard" from "room buried three doors deep", which is
what the outdoor-proximity seep (§4.3a, task L1d) exists to compute. The user was explicit at L1b
that sealed interiors stay clear, and that has not changed. **So the outdoor floor fog can land
first, and the indoor half arrives with L1d** — at which point §4.3a's `kSeepMax` note applies to
both layers at once.

**Open, and deliberately not guessed:**

- **Q25 — does the floor fog need its own up-ray, or does sharing `skyExposure` suffice?** Sharing
  is free and is the assumption above. The risk is that the floor fog is densest exactly where the
  aerial layer is thinnest (at your feet, under an overhang), so a misclassification that is
  invisible today could become obvious. Judge on hardware, not here.
- **Q26 — CLOSED 2026-07-27, before any shader was written. The march must WARP its samples
  toward the camera; raising the step count does not work.** Measured: the sampled floor term
  against its exact integral `R(1 − e^(−tMax/R))`, over 400 jitters.

  | | range 128 | range 256 | range 512 |
  |---|---|---|---|
  | 24 steps, uniform | 17 % (max 37 %) | 8 % (max 18 %) | 4 % (max 9 %) |
  | 64 steps, uniform | 6 % (max 13 %) | 3 % (max 6 %) | 1.6 % (max 3 %) |
  | **24 steps, `t = tMax·s²`** | **0.19 %** | **0.09 %** | **0.17 %** |

  Error scales as `dt/R`, so buying accuracy with steps is a losing trade — **64 steps still
  bands at a 128-unit range**, at 2.7× the cost. Redistributing the same 24 samples fixes it
  outright. Substitute `t = tMax · s²` for `s = (i + jitter)/kFogSteps` and carry the warp's
  Jacobian `dt = tMax · 2s · (1/kFogSteps)`; forgetting the Jacobian silently rescales the whole
  fog, which will read as a tuning problem rather than a bug.

  **Checked against the layer it is not for** — the general fog is roughly uniform out to 2048, so
  a warp could have starved its far field. It does not: at the shipped High density the warp is
  *better* (10 % error vs 15 %), and at the thinnest it costs ~0.5 %. Note what that table also
  says — **the shipped uniform march already under-integrates long rays by 8–15 % at High.** It is
  a constant bias, so it was absorbed into tuning rather than seen; expect the fog to read slightly
  *thicker* after the warp lands, at unchanged constants.

  Exponent 3 is better still for the floor term but measurably worse for the general one — **use 2.**

**Ripples if this ships.** The sample warp (Q26) changes `marchFog`'s loop for **both** layers and
every future one, so it is a §4.2 change, not a §4.3c one — and it **landed first, on its own**
(2026-07-27), because it alters the shipped look independently of any new layer (see the 8–15 %
bias above); shipping it inside the floor fog would have left two causes for one screenshot.
`fogDensity()` currently
returns the whole density; L4's split-sigma
form (`skySigma · skyExposure + areaSigma`, INV-9) must gain a third addend rather than folding the
floor term into either existing one — the floor term sits on the **gated** side, with `skySigma`,
because its placement question is the same one (`skyExposure` still never touches `areaMult`).
`fogHeightPool()` — which L4 still has to extract — is reused
by both layers with different `poolH`, so it must keep taking the height as a parameter.
**INV-10 moves too:** the sky closed form gains the second addend above, and inherits the aerial
branch's exclusions unchanged — no `wisp`, `skyExposure = 1` by definition, `fogStrengthScale`
applied once to the sum.

### 4.4 Light sources & shafts

Fog scatters light from **two** sources only — sky and big static emitters
(user 2026-07-21). Muzzle/flashlight/sprite lights are excluded by construction
(they are push-constant deltas / the `[omniStart, emitCount)` dynamic slice, never
iterated here — INV-2).

**(a) Sky shafts — needs a direction.** ⚠ **This part is superseded twice, and the second
one is not just a mechanism change — read both before building from it.**
**(i)** The per-sample ray is replaced by a bilinear tap on the seep field
(**DOOM-0289**, the 2026-07-30 amendment at the end of this section): it shipped, cost
13.6 ms, and only the *visibility mechanism* moves.
**(ii)** The "REPLACES the flat ambient" paragraph below is **wrong against what shipped**
and is corrected in place — L2 ships an ambient/directional **split**, not a replacement.
`kSunDir` is **already declared but unread**
(`pt_common.glsl:42`, `normalize(vec3(0.30, 0.30, 1.0))`, commented "L2"); L2 wires it
into the march. It stays a compile-time `const` for v1 — a plausible steep slant;
per-level control deferred, Q1. At a march sample, cast **one** shadow ray toward
`kSunDir` with the standard shadow cull mask `0x01`. Because that mask excludes the
sky-backdrop instance (mask `0x04`, `r_vulkan.cpp:2020` — same as the open-sky up-ray
of §4.3a), the ray **reaching the sky = MISSING all solid geometry** toward the sun
(nothing blocks the path). A clear (miss) ray → the sample is **sky-lit**, contributing
`kFogColor · kSkyShaftStrength · phase · mediumTint` (the fog tone of §4.3b, so shaft and
surround match); a ray blocked by solid geometry → dark.

**L2's visibility-gated sky term must not be *added on top of* L1's flat sky ambient**
(and it is not a straight replacement either — see the correction below).
L1 shipped an un-shadowed `skyAmbient = SKY_COLOR · kSkyShaftStrength` applied at
every sample (`marchFog`, `pathtrace.comp:782-814`, whose code comment reads "L2 adds
directional sky + torches"). If L2 *added* its term on top, open-air samples would
in-scatter sky light **twice** — the double-count class this spec has already shipped
once. So L2 rewrites that line — but **not** as the outright replacement this paragraph
originally specified.

> ⚠ **CORRECTED 2026-07-30 against what shipped, and the original was a look defect, not
> a wording slip.** This paragraph used to specify `vis · kFogColor · kSkyShaftStrength`
> with `vis ∈ {0,1}`, and concluded "**roofed air in-scatters no sky light at all from L2
> onward**". Built literally, that is **measured at the E1M1 spawn as a 3.4× darkening**
> (mean frame brightness 60.6 → 17.9) — the fog simply disappears, taking the Silent Hill
> haze of §4.3b with it. The reasoning was sound about *energy* and wrong about *the sky*:
> it models the sky as a sun **disc**, while the art direction (§1, §4.3b) is an
> **overcast dome** that lights fog from every direction at once.
>
> **What ships is a split of the same energy, not a replacement:**
> ```glsl
> Ls = kFogColor * kSkyShaftStrength * (kSkyAmbientFrac + (1.0 - kSkyAmbientFrac) * sunLit)
> ```
> with `kSkyAmbientFrac = 0.65` (`pt_common.glsl`, which carries the measurement). The
> double-count this paragraph was protecting against is still avoided — the two shares sum
> to 1 — while **shadowed air stays milky** and the beam reads as **contrast** rather than
> as black-against-white. So roofed air keeps 65 % of the sky term, `kIndoorFogScale > 0`
> keeps a medium there for L3's torches (§4.3a), and there is **no dark interim waiting on
> L3**. DOOM-0289 changes only where `sunLit` comes from; the split is untouched (INV-13).

The bright/dark boundary *is* the shaft (a beam through a doorway/sky-hole). One ray per
sample keeps it affordable at half-res (§4.6). **Sky shafts require an open sightline
to the sky:** on a fully enclosed level with no sky (sky tex id
`misc4.w == 0xFFFFFFFF` / no sky mesh, `pathtrace.comp:739`) a solid ceiling blocks
every sun ray, so sky shafts vanish — only torch shafts (b) + the base/haze fog
remain. Expected, not a bug.

**(b) Torch shafts — the existing static emitters.**

**The selection is not per sample — it happens once, at level load.** That is the whole
design, and it is the same substitution DOOM-0276 made for the up-ray and DOOM-0289 for
the sun ray: static geometry plus static emitters means "which torches reach this air?" is
decidable before the first frame, so the march reads a table instead of searching one.
`marchFog` therefore does **no** emitter scan at all.

**What the bake produces** (`BuildFogLightGrid`, `r_vulkan.cpp`). Emitter triangles are
clustered to lights; each light gets `lum` = its max channel and a
`reach = sqrt(lum / RB_FOG_LIGHT_CUTOFF)` (cutoff `0.04`), capped at
`RB_FOG_LIGHT_MAXREACH` = 512 units. Then, for each cell of the **seep grid** (§4.3a's
field, reused whole — same dims, same 64-unit cell, so no transform of its own):

- rank every light whose reach covers the cell by its **unoccluded contribution at this
  cell** — `lum · win² / (d² + kTorchSoftR2)`, deliberately the *same* windowed curve
  `torchInscatter` evaluates, so the two agree about which light matters. **Not by raw
  brightness**: a dim near lamp outranks a bright far one, and the cell-boundary
  continuity below depends on the ranking matching the march's falloff;
- **sight-test the best `RB_FOG_LIGHT_PROBES` (= 4) only** — ranking before testing is what
  bounds the bake, so a map with hundreds of emitters pays the same handful of BSP walks
  per cell as one with a dozen;
- the test is a real `P_CheckSightTrace` from a 2×2 sub-lattice inside the cell, and the
  **fraction that passes is kept as a soft visibility `vis`** — a sample landing inside a
  wall simply fails, which is what grades the cell at a wall's edge instead of stepping;
- keep the first `kFogLightsPerCell` (= 2) that anything sees, as two `vec4`s:
  `(pos.xyz, reach)` and `(Le.rgb, vis)`.

**What the march does** (`torchInscatter`, `pathtrace.comp`). One indexed read —
`fogLightCell(p.xy)` — then per kept light a windowed inverse square
`w = clamp(1 − (d²/reach²)², 0, 1)`, `fall = w² / (d² + kTorchSoftR2)`, times
`4π · fogPhaseHG(cos θ, kFogAnisotropy)`, times `Le · vis`. The window reaches exactly zero
at the light's own baked reach, which is what lets the list be finite without the cut
showing as a ring in the air. The sum is scaled by `kTorchShaftStrength` = **0.047** — the
emitter-side gain, twin to the sky's `kSkyShaftStrength` = 0.85; its derivation lives in
`pt_common.glsl`'s comment above the declaration, not here, and it pins a median torch's
peak at roughly 4× the indoor sky in-scatter floor.

**Occlusion is settled, and not the way the design posed it.** It is not an "optional extra
ray" — the bake's sight test *is* the occlusion, paid once at load instead of per sample, so
a torch does **not** glow through a wall (Q2, closed).

> ⚠ **The medium tint on this term is an OPEN DECISION, not a settled one — do not build
> either way from this section alone.** As shipped today the torch term carries no tint,
> but that is only because **L4 has not shipped**: `mediumTint` does not exist in the
> shaders, and `kGooTint` / `kHellTint` are declared and unread (`pt_common.glsl`, both
> marked `(L4)`). So today's untinted term is L4's absence, not a decision against tinting.
> Meanwhile §4.3, §4.5 and §7's L4 acceptance row all specify that L4 **will** multiply this
> term by `mediumTint` ("a torch shaft in a goo room is warm-through-green"). The question
> L4 must answer before it is built: does a torch shaft take the medium's colour, or keep
> the emitter's own `Le` so a warm flame still reads warm through green air? Both are
> defensible and the spec currently implies the first while the code implies the second.

Two amendments below modify this, and neither changes the scheme above: DOOM-0295
integrates the term at **half the march's rate**, and DOOM-0296 **re-bakes the grid when a
plane moves**, because a load-time answer goes stale the moment a door opens.

Both sources feed the same `Ls(p)` accumulation (§4.2). The sky path is the primary
shaft mechanism; torch shafts are the secondary "dark room glows around the flame"
effect.

**2026-08-02 amendment (DOOM-0295) — the torch term is integrated at HALF the march's
rate.** The change is one of *rate only*: `torchInscatter` is called once per **pair**
of march samples instead of once per sample. Selection, occlusion and tinting are exactly
as §4.4(b) above describes them and are untouched here. Implemented in `marchFog`
(`pathtrace.comp`, the `if ((i & 1) == 0)` block inside the `kFogSteps` loop).

The evaluation point is the pair's midpoint **in index space** (`i + 0.5`, with
`jitter` inside it, so pair boundaries move per pixel exactly as the sample positions
do), and the result is spent on both samples of the pair — except where the
`trans < 0.003` early-out ends the loop on an even `i`, which spends that midpoint
once. That case is worth ~0.3% of the ray by construction. Density, the seep grade and
the sky term are untouched and still get every sample.

**`kFogSteps` parity is NOT a precondition — the code removes the need for one.** On an
odd count the final even index has no partner, and offsetting it by half a step can
sample past `tMax` (whenever `jitter > 0.5`); the shipped expression drops the offset
for that index instead. Stated because §4.2 once scheduled `kFogSteps` 24 → **~40** at L1c
(since reverted) and "~40" invites an odd value, so the natural reading of a pairing scheme is that it
constrains the retune. It does not.

Why this term and not the others: it is the smooth one. A windowed inverse square whose
peak is capped by `kTorchSoftR2` (a 32-unit softening radius, stored squared) has no
feature finer than that radius, while
the density it multiplies carries every sharp thing in the model — the wisps, the floor
layer's knee, the seep grade at a threshold. Two things in the torch term are **not**
smooth, and are tolerated rather than overlooked: the cell's light **list** changes
discontinuously at a `fogLightCell` boundary, and the phase argument swings fast along a
ray that passes close to a lamp, which can be well inside 32 units. Both are jittered
per pixel and land in the denoiser's input, which is where the measured error stayed.

Midpoint rather than the pair's leading sample, because holding the leading value would
shift every shaft **half a sample step** toward the camera; the midpoint rule is
second-order, the same error order as interpolating both ends, for one evaluation
instead of two. Second-order **in `s`**, and only where a pair's two `dt` weights are
close: `dt = 2·tMax·s/N` grows with `s`, so the first pair or two are weighted forward
of their midpoint. That is the near-camera band — and near-camera and near-floor are the
same pixels from this viewpoint, which is exactly where the measured difference landed.
The claim is second-order, not exact.

Measured on an RX 6600 (Mesa 26.1.5), E1M1 nukage courtyard (`-warpto 1866 -3221 45`),
Ultra RT with HD art, 50% scale, `-noinput`, three runs of 21 samples each per build,
medians, idle machine. The floor build sets `kTorchShaftStrength = 0.0`, which makes the
whole torch loop dead code and is therefore a true feature-off floor, not a gain of zero
— **confirmed rather than assumed**, since this feature already has one falsified compiler
assumption on its record: `RADV_DEBUG=shaderstats` shows the floor build 640 bytes and 124
instructions smaller (28300 → 27660, VMEM 159 → 155), which is the loop leaving.

| `rt_fog` | before | after | floor | L3 before → after |
|---|---|---|---|---|
| `3` High | 15.29 ms | 14.97 ms | 14.14 ms | **1.15 → 0.83 ms** |
| `1` Low — **the shipped default** | 15.31 ms | 14.96 ms | — | **0.35 ms** recovered |

All figures are the megakernel pass alone, not whole-frame. Whole-frame went 38 → 39 fps
at High, which is a rounding step on a 0.32 ms saving and is quoted only so the order of
magnitude is visible. The High "after" re-measured at **14.96 ms** once the `kFogSteps`
parity guard landed — inside the 0.02 ms run-to-run agreement, i.e. the guard is free —
and 14.96 is the shipped build.

The saving holds at the shipped default and is marginally *larger* there, which is the
opposite of the intuition and worth recording: thinner fog never trips the
`trans < 0.003` early-out, so more samples run and there are more evaluations to halve.

**Halving the evaluations recovered 28%, not 50%, and the gap is the point.** L3's
1.15 ms is not purely per-evaluation cost: the floor build deletes the whole term, while
a rate halving keeps the `Ls += torchPair` addend on *every* sample, keeps the carried
register, and still makes half the buffer reads. Anyone applying this trick to another
term should predict ~28%, not ~50%, or they will over-promise by 1.8×.

**Which of 1.05 / 1.55 / 1.15 ms is L3's cost?** All three, in different contexts, and
the spec should not be the one record that omits the reconciliation. `1.05 ms` is
DOOM-0295's original headline, taken at a lighter viewpoint; `1.55 ms` is the same term
at this courtyard *before* the phase-function rewrite landed; `1.15 ms` is here and now,
after it. The ROADMAP bullet and `torchInscatter`'s comment both carry this; now so does
this spec.

Look A/B at 3840×2160, ripple clock pinned with `-rippletime 8`: MAE **0.055/255**,
worst pixel **10/255**, measured over the 99.6% of the frame a same-build control pair
holds stable (the excluded remainder is sprite animation on the game clock, plus the fps
readout). Control MAE is 0.0041/255, so the change is 13× the noise floor — and **55×
under `-shotcompare`'s `kGoldenMAE` bar of 3.0** (`r_vulkan.cpp:1126`), which is the
gate that decides whether a look change is acceptable. It is confined to near-floor fog,
mottled rather than banded. `-rtverify` INV-6 is identical either side (0.1091%, bar
0.50%), as it must be: this touches no direct-lighting path.

> **What §4.4(b) used to say, and why the record is kept (`DOOM-0304`, closed
> 2026-08-03).** Until this date the spec specified torch selection as: iterate
> `k ∈ [0, omniStart)`, pick the **nearest few** by centroid distance, multiply by
> `mediumTint`, and treat occlusion as an optional extra ray. L3 shipped none of that —
> it took a third form neither option contemplated, the load-time per-cell bake now
> written up above — and the stale text survived in **four** places (§4.4(b), the §7 layer
> table's L3 row, Q2 and Q23) through two later amendments that each cited it. All four are
> corrected as of 2026-08-03. **Five, in fact** — §6's "drop the emitter occlusion ray" lever
> and INV-2's `k < omniStart` falsifier were both missed by the count and corrected in the same
> pass, which is the honest measure of how far a superseded mechanism spreads.
>
> Kept as a note rather than deleted because the *reasoning* that killed the runtime scan
> is still the reason the current design is the right one: a per-sample scan is
> `steps × emitters` distance tests per pixel, and a per-ray scan trades that for a stale
> pick on long rays. The bake beats both by answering the question zero times per frame.

**2026-07-30 amendment (DOOM-0289) — the sun ray becomes two more channels on the seep
field.** Everything above about *what the shaft is* stands. What changes is **how the
sample learns whether the sun reaches it**: not by tracing, but by one bilinear tap on a
load-time field. The whole of the rest of this amendment is that one substitution and
what it costs.

**The measurement that forces it.** L2 shipped correct (`544ae84`) and far over budget.
E1M1 courtyard, default `rt_fog = 1` (Low), 50 % render scale:

| | present-total | GPU megakernel | the fog's own share |
|---|---|---|---|
| pre-L2, fog **off** | 43 fps | 12.7 ms | — |
| pre-L2, fog **Low** | 40 fps | 13.4 ms | **0.7 ms** |
| L2, fog **Low** | **25 fps** | **27.0 ms** | **14.3 ms** — of which the sun ray is 13.6 |

The ray alone is **19× the entire rest of the fog** and costs **15 fps**, against a
≤ 15 % gate the feature had been sitting comfortably inside at +4.2 %. At `rt_fog = 3`
it is 26.5 ms of megakernel against 15.4 with fog off. **`master` ships that regression
today**, and `rb_fog` defaults to Low, so it is live for anyone playing Ultra RT — not a
branch problem.

**This is DOOM-0276 repeating, and it admits the same answer.** That item deleted this
same feature's *other* per-sample ray — §4.3a's straight-up open-sky test, then 7.9 ms
of an 8.4 ms feature — by reading a load-time 2-D field instead, and took the fog from
+35 % of frame time to +4 %. The sun ray is the same **shape** of question for the same
two reasons: DOOM is **flat-mapped** (no room-over-room, so an XY grid can carry the
answer), and **`kSunDir` is a compile-time `const`** (INV-3), so "can the sun be seen
from here?" is decidable before the first frame.

**Confirmed with the user, 2026-07-30, because this makes INV-3 structural rather than
merely current:** DOOM 1 and 2 have no day/night cycle and none is wanted, so a fixed
sun is settled, not deferred. See Q27.

#### The correction that matters: sun visibility is an INTERVAL, not a threshold

The obvious form of this field — *store the minimum `z` at which the sun clears every
obstruction, and test `p.z >= that`* — is **only valid in open-sky air**, where rising
can only ever help. In **roofed air it is false in both directions**: rising clears the
wall in front of you but also runs you into the ceiling above you. And roofed air just
inside a doorway is *exactly* where the best shafts are, so this cannot be waved through
as an edge case — it is the main case.

Stated properly. March the cell's 2-D projection along the sun's fixed horizontal
heading `u = normalize(kSunDir.xy)`, with slope

```
m = kSunDir.z / |kSunDir.xy| = 1.0 / 0.4243 = 2.357        (~67° elevation at the shipped kSunDir)
```

A ray leaving height `z` is at `h(s) = z + m·s` after horizontal distance `s`. Let cell
`k` be crossed over `s ∈ [sIn(k), sOut(k)]`. Because `h` is increasing in `s`, the ray is
lowest inside cell `k` at `sIn(k)` and highest at `sOut(k)`, so **each constraint is
evaluated at exactly one end** — a roofed cell yields **both** a lower and an upper bound,
a sky cell yields a lower bound and an escape:

- **floor** (lowest point of the crossing): need `z > floor(k) − m·sIn(k)` — a **lower** bound
- **ceiling, non-sky** (highest point): need `z < ceil(k) − m·sOut(k)` — an **upper** bound
- **ceiling, sky**: `z ≥ ceil(k) − m·sOut(k)` **escapes** — the ray leaves the map, the
  sun is reached, stop tightening

So the admissible `z` is an **intersection of bounds terminated by an escape** — an
interval. Running the march with `lo` = max of the lower bounds so far and `hi` = min of
the upper bounds so far, and `zEsc(k) = ceil(k) − m·sOut(k)` at each sky cell:

```
window(k)  = [ max(lo_k, zEsc(k)) , hi_k ]        at each SKY cell k
zLo, zHi   = the convex hull of every non-empty window(k)
```

Both endpoints are tracked in the loop with no storage, because `lo` is a running **max**
and `hi` a running **min** — so `hi` never rises and `zHi` is the `hi` of the **first
non-empty** window, while `zLo` is the running minimum of `max(lo_k, zEsc(k))` over the
non-empty ones.

⚠ **"Non-empty" is load-bearing in both, and dropping it is the second bug this section
exists to prevent.** `zEsc(k)` is *not* monotone — a later sky sector with a higher
ceiling raises it — so the first sky cell reached is not necessarily the first that
escapes. If its window is empty and a later one is not, `hi` may have fallen in between,
and taking the first sky cell's `hi` reports air as sun-lit that a solid ceiling between
them blocks. Guard **both** updates on `max(lo_k, zEsc(k)) ≤ hi_k`; the build sketch in
the plan's Task L2b Step 3 is the normative form.

**Worked, because the courtyard case is the one that looks wrong on first reading.**
Open cell, floor 0, sky ceiling 256. First, the step size, since it is not the cell size:
a DDA crosses a boundary every `cell / (|u.x| + |u.y|)` world units on average, which at
the shipped 45° heading is `64 / 1.414 = 45.3` — so a cell entered costs `m·45.3 ≈ 107`
units of rise, not `m·64`. At exactly 45° the DDA also ties at every corner and steps
one axis at a time, so `sOut` repeats in pairs: `45.3, 45.3, 135.8, 135.8, …`

Cell 0 therefore escapes only above `256 − m·45.3 = 149`; that alone would say the
courtyard is unlit below head height, which is plainly false. But the march does not stop
there: cell 1 repeats `149`, cell 2 gives `256 − m·135.8 = −64`, and the hull closes to
`[0, ceil]` — the whole air column, which is correct. **Stopping at the first sky cell is
a bug**, and it is the one an implementer will write.

#### What is stored, and where

**No new image, no new sampler, no new descriptor, no second tap.** §4.3a's seep field
is `RG16F` today (`.r` = seep distance, `.g` = open-sky mask) and the march **already
taps it every sample**. Widen that one image to **`RGBA16F`**:

| channel | meaning | owner |
|---|---|---|
| `.r` | distance to outdoor air through open space | L1d |
| `.g` | open-sky mask (0/1) | DOOM-0276 |
| `.b` | **`zLo`** — lowest world `z` in this cell that still reaches the sun | **DOOM-0289** |
| `.a` | **`zHi`** — highest world `z` before a solid ceiling stops it | **DOOM-0289** |

Memory: `256×256` worst case × 4 channels × 2 B = **512 KB** (E1M1's 75×47 grid is
28 KB). Half-float resolves ~1 unit at `|z| = 1024`, far finer than the 64-unit cell.

**A cell that the sun does reach stores its interval clamped to its own air column**,
`zLo ≥ floor(own)` and `zHi ≤ ceil(own)` — where in practice only the `zHi` half binds, since `lo` is seeded at
`floor(own)` at `s = 0` and only rises, so every window's lower end already clears it. The
clamp buys a **bounded dynamic range** —
which is what makes the sentinel behave the same way in a 96-unit corridor and a
1024-unit hall. (The never-sentinel below is deliberately **outside** that column — it has
to be, to express an empty interval — so it is written instead of the clamp, never
through it.)

⚠ **The upper clamp is wrong on its own for an OPEN-SKY cell, and the shader carries the
correction.** A march sample lies between the camera and a real geometry hit, but that
does **not** keep it under the cell's ceiling when the "ceiling" is a sky plane: a ray
aimed at a distant wall passes hundreds of units above a courtyard's sky flat, and
clamping `zHi` to it would fail every such sample and paint a **horizontal seam across
outdoor fog at the sky-plane height** — in exactly the shafts L2b must leave unchanged.
The fix is free, because the mask is already tapped: an open-sky cell has no solid
ceiling *of its own*, so any `z` above its sky plane is outside the map and lit by
definition. The test therefore reads

```
sunSeen = (p.z >= zLo) && (p.z <= zHi || openSky)
```

and the clamp stays, keeping the range bounded. (Above a sky plane the answer rests on
approximation 3 below — a taller neighbour is not consulted — which is the same
one-sided over-lighting already accepted there, not a new one.)

**The "no sun here" sentinel is finite and local**, for the reason `dMax` is finite in
§4.3a: a half-float `±inf` meeting a zero bilinear weight yields `NaN`, and a `NaN` in
`σ` blows the whole march. A cell the sun never reaches stores

```
zLo = ceil(own) + RB_SUN_NEVER    zHi = floor(own) − RB_SUN_NEVER    (RB_SUN_NEVER = 128 world units)
```

i.e. an interval that is empty by construction, expressed **relative to the cell's own
air column** so it blends consistently everywhere. **The name has no `k` prefix on
purpose** — this constant lives C-side in `r_mesh.h` with the rest of the build, not in
`pt_common.glsl`; the shader never reads it.

The value sets **where the shaft edge falls between two cell centres**. Blending a
never-cell of column height `H` (bilinear weight `w`) against a lit neighbour whose window
spans `W`, the interval closes once

```
w · (H + 2·RB_SUN_NEVER)  >  (1 − w) · W
```

— the `H` term is part of it, because the sentinel is expressed relative to the cell's
own column rather than as a bare ±constant. At `H = W = 256` and `RB_SUN_NEVER = 128`
that is `w > 1/3`.

**Read `w` carefully, because the direction is the opposite of what the shape suggests.**
`w` is the **never-cell's** weight: `0` at the lit cell's centre, `0.5` at the boundary
between them, `1` at the shadowed centre. Closure at `w > 1/3` therefore puts the edge
*before* the boundary — the shaft **stops about a sixth of a cell short of it**, pinching
slightly rather than bleeding across. That is the conservative direction and it is within
the half-cell error §4.3a already accepts, but it is worth stating plainly: raising
`RB_SUN_NEVER` pinches the shaft further, lowering it lets the shaft spill past the
boundary into geometry that should shadow it. **Tuning is Q28.**

#### The shader change is one test

```glsl
// before (L2, 544ae84) -- 13.6 ms
float sunLit = (skyExists && kSkyAmbientFrac < 1.0 && sunRayMissesGeometry(p))
             ? sunGain : 0.0;

// after (DOOM-0289) -- the tap is ALREADY in the loop; the local `seep` widens .rg -> vec4
vec4  fld     = texture(uSeepField, worldToSeepUV(p.xy));
bool  openSky = fld.g > 0.5;                                  // unchanged, DOOM-0276
bool  sunSeen = (p.z >= fld.b) && (p.z <= fld.a || openSky);  // DOOM-0289, replaces the ray
float sunLit  = (skyExists && kSkyAmbientFrac < 1.0 && sunSeen) ? sunGain : 0.0;
```

**Renaming the local is part of the edit, not cosmetic.** The shipped line declares
`vec2 seep`, which *shadows* the `SeepXform` block instance also named `seep`; widening
it to a `vec4` named `fld` means the downstream `exp(-seep.r / kSeepFalloff)` in the
`skyExposure` line must become `fld.r` in the same edit, or it resolves to the `SeepXform`
block instance, which has no `.r` — a hard compile error, which is the good outcome here.

`sunRayMissesGeometry()` and its `rayQueryEXT` go with it — **deleting it is part of the
change, not tidying afterwards**, because a live definition invites the ray back. Note
what is *not* touched in **meaning**: `kSkyAmbientFrac`'s ambient/directional split,
`sunGain` and the HG phase, the `skyExists` guard, and what the `.r`/`.g` channels say.
`vis` stays binary, exactly as the ray gave it, so the shaft's character does not change
in kind. (The bounds are derived as strict inequalities above and tested inclusively
here; at the field's 64-unit cell and after bilinear filtering the difference is far
below one texel and is not worth carrying through the arithmetic.)

#### The build, and where it lives

`RB_BuildSeepField` (`r_mesh.c`) is the precedent and the host — it already walks the
grid and already resolves a sector per cell centre. Four additions, in order:

1. **Cache what the march needs, in the pass that already exists.** The rasterise loop
   (`--- 4. Rasterise`) calls `RB_SectorAtPoint(cx, cy)` once per cell. Record
   `floor`, `ceil`, `isSky` and `isSolid` there. **Marching would otherwise re-descend
   the BSP per step** — the seep's whole ~3.5 k-cell pass costs 0.6 ms, so a march of up
   to `RB_SUN_MARCH_MAX` steps re-querying it would cost tens of milliseconds and land on
   DOOM-0281's door-open frame as a visible hitch. Cached, the march reads plain arrays.
   (`RB_SUN_MARCH_MAX` is the DDA's own bound and has nothing to do with `kFogSteps`, the
   shader's per-pixel fog march — they are different marches in different processes.)
2. **Detect solid cells, which the seep field never had to.** DOOM's BSP partitions the
   whole plane, so `R_PointInSubsector` returns a leaf for a point inside a wall or out
   in the void, and its sector is some *neighbouring* room's. The seep tolerates that
   (it decides connectivity on the portal graph before rasterising); **this field cannot**
   — a solid building in a courtyard would cast no shadow, which is precisely the shaft
   the feature exists to draw. The test is cheap and exact enough: BSP descent already
   guarantees the point is on the right side of every node split, so the point is in the
   void **iff it lies on the back side of at least one seg of its own subsector** (leaves
   are convex and their segs face inward), which the engine's own `P_PointOnLineSide`
   answers. Three clauses come with it:
   - **A void cell, and a cell whose sector has `ceil ≤ floor`** (a shut door, a solid
     pillar sector), **block: the march ends there contributing no further window — the
     hull collected from nearer sky cells still stands.** Not "the result is empty": a
     wall further along the sun line cannot un-light a beam that already escaped through
     a nearer opening, and reading it that way deletes real shafts, the opposite of
     approximation 2's stated one-sidedness.
   - **Skip a self-referencing seg** (`linedef->frontsector == backsector` — the
     deep-water / fake-wall trick §4.3a already excludes from the portal graph). Both its
     sides name the same sector, so the point is in that sector's air whichever side it
     falls on; testing it anyway misclassifies about half such segs as walls and stamps a
     spurious shadow across every deep-water room.
   - ⚠ **The padded ring is the one place "outside the map" must NOT mean "solid".** §4.3a
     pads the grid by a cell of void so `CLAMP_TO_EDGE` extends `dMax`/roofed outward
     rather than outdoor air; for the seep that ring is a blocker, and rightly so. For the
     clearance it is the opposite: beyond the map's bounding box there is no geometry at
     all, so a ray that reaches the ring has **escaped**. Marking it solid would block
     every march that leaves the map along `+u` and carve a systematically unlit band —
     roughly the two or three cells a courtyard needs to saturate — along the `+X`/`+Y`
     edges of every outdoor area. That is the same class of edge artefact §4.3a's `ceilf`
     fix was written for, in the one feature L2b must leave look-identical.
   - ⚠ **The void verdict feeds the clearance march ONLY.** The seep's `.r`/`.g` keep
     taking their sector from the existing `RB_SectorAtPoint` derivation, unchanged. The
     rasterise loop gates on `sec >= 0`, so handing it "void" for a wall-interior cell
     would flip that cell's distance to `RB_SEEP_DMAX` and its sky mask to 0 — silently
     altering a shipped, user-signed-off look and breaking L2b's own look-identity gate.
     **One lookup, two verdicts: a sector index for the seep, a solid/air flag for the
     clearance.**
3. **March each cell with a 2-D DDA** over the cached grid along `u`, exactly as derived
   above. **The march is short, and that is a property of the sun rather than an
   assumption** — a cell entered costs `m · cell/(|u.x|+|u.y|) ≈ 107` units of rise at the
   shipped heading, so a ray from any floor meets the ceiling of a 128-unit room within
   two cells. It ends on **four** conditions — a solid cell, walking off the padded grid,
   and the two below; the second of these two is the one most easily left out:
   - **Blocked** — `lo > hi`. In *roofed* air this arrives geometrically, because `hi`
     falls by the per-cell rise while `lo` never falls.
   - **Saturated** — a window has been captured **and** `zLo` has reached `floor(own)`, so
     nothing later can widen the clamped answer: `hi` is non-increasing (so `zHi`, the
     first non-empty window's `hi`, is already final) and `zLo` is at the clamp floor.
     **This is the exit that matters wherever the sun line reaches sky**: a sky cell
     contributes no ceiling bound, so `hi` stops falling and `lo` stops rising the moment
     the march leaves roofed air — the blocked exit can then never fire, and **without
     this test every cell with a sky line marches to the cap**, the doorway case included.
   `RB_SUN_MARCH_MAX = 32` is then a genuine backstop rather than the common path. **What
   happens if it does bind is worth stating**, because it is not a graceful degradation:
   a cell that reaches the cap without ever capturing a window falls through to the
   never-sentinel and loses its shaft outright. That is the conservative direction
   (under-lighting, unlike the other two approximations), and at 32 cells it takes a sun
   line that stays in air for `32 · cell/(|u.x|+|u.y|)` — about 1.4 k units of travel and
   3.4 k of rise at 64-unit cells — which no vanilla room provides.
   Cost either way is ~3.5 k cells × a handful of steps, worst case × 32 —
   a few hundred thousand iterations of float arithmetic with no BSP in them, well inside
   the seep's own 0.6 ms. **Measure it rather than trusting this paragraph** (L2b Step 8).
4. **`kSunDir` must be mirrored C-side**, as `dMax`/`RB_SEEP_DMAX` already are —
   `RB_SUN_DIR_X/Y/Z` in `r_mesh.h`, with `pt_common.glsl`'s `kSunDir` named as
   authoritative in a comment on **both** sides. A mismatch here is worse than the
   `dMax` one it copies: the beam and the field would disagree about where the sun is,
   and the symptom is shafts landing in the wrong place rather than a build error.
   L2b's play-test check — *the beam is where it was before* — is what catches it.

#### The re-flood, and the upload gate that is currently wrong

DOOM-0281's hook (`RecordSeepRefresh`, `r_vulkan.cpp`) must carry the new channels or an
opening door leaves **stale shafts** — worse than stale fog, because a beam through a
wall reads as a bug rather than as thin air. Four specifics:

- The re-flood already recomputes the whole field, so `zLo`/`zHi` come along for free
  once they are in `rb_seep_t`; add `g.seepZLo` / `g.seepZHi` beside `g.seepSky` and pack
  them in `PackSeepTexels`.
- ⚠ **DOOM-0281's trigger is necessary but NOT sufficient, and this is the easiest thing
  in the amendment to miss.** `RB_SeepOpeningsChanged` detects an `openrange` **flip** —
  by its own comment, "sector movement that does NOT cross zero (a lift running between
  two open heights, a crusher not yet at the floor) flips nothing". That is exactly right
  for the seep, whose distances are pure connectivity. It is **wrong for the clearance**,
  which is baked from `floorheight` and `ceilingheight` and therefore moves whenever a
  plane moves at all. A lift rising in a courtyard changes what it shadows without
  flipping anything, and the beam beside it would freeze. So the dirty condition widens
  to **"an opening flipped, or any sector plane moved"** — the second half is already
  free, since `RB_UpdateMeshHeights` reports `RB_UPD_MOVED` and that is the gate the
  existing scan sits behind.
- **Split the refresh in two, so the widened trigger stays cheap.** An opening flip needs
  the **full** re-flood (portal graph + Dijkstra + rasterise + march). A plane that moved
  without flipping needs **only** the geometry cache refreshed and the march re-run — the
  seep's distances provably did not change, so its expensive half is skipped. Rate-limit
  the clearance-only rebuild (a moving door runs for ~30 tics and does not need 30
  rebuilds); **the cadence is Q30, to be set from Step 8's measurement rather than
  guessed** — which is the DOOM-0281 lesson (Q22) applied before it has to be relearned.
  ⚠ **Whoever closes Q30 must read INV-14's constraint first:** DOOM-0296's fog-light
  re-bake reads the cache this block refreshes, so a cadence longer than `kFogLightSettle`
  (0.15 s) would let a settled bake run against a cache refreshed before the plane's final
  position. Either keep the cadence at or under that, or have the fog-light fire block force
  a refresh of its own.
- **The clearance channels are snapped, not eased.** The seep distance eases over
  `kSeepEaseTau` because mist genuinely rolls in; light does not. A door opening admits
  its beam in the same frame, which is both correct and simpler. **"Snapped" and
  "rate-limited" are not in tension**, and the difference is worth being precise about:
  a *rebuild* may be deferred by up to Q30's cadence, but when one runs its result is
  applied whole rather than faded in. So the beam appears within the cadence, not over a
  time constant — and the cadence is what §7's "in the same frame" is bounded by.
- ⚠ **`RecordSeepRefresh`'s early-out is wrong for a snapped channel, and this is a real
  defect the change must fix, not inherit.** It sets
  `g.seepEasing = (g.seepTarget != g.seepCur)` and then `if (!g.seepEasing) return;` —
  so the pack-and-copy is reached **only when a seep distance changed**. A door whose
  opening changes what the sun can reach without changing any distance-to-outdoor-air
  (entirely plausible: a second route of the same length) would recompute the clearance
  and **never upload it**. The gate must become "ease *or* upload pending", with the
  clearance change raising the upload half on its own.

#### The approximations, each bounded rather than implied

**First, a scaling note that all three rest on: every figure in this section is quoted at
`cell = 64`.** `RB_BuildSeepField` **doubles** the cell until the grid fits
`RB_SEEP_MAXDIM` (256), so a map beyond ~16 k units on a side gets 128-unit cells — and
then the rise per cell entered is ~214 rather than 107, the quantisation is ±64 rather
than ±32, and `RB_SUN_NEVER`'s 128 is weighed against a coarser neighbourhood. **Build
from the forms, not the numbers:** rise per cell entered is `m · cell/(|u.x| + |u.y|)`,
quantisation is `±cell/2`, and Q28's closure condition is already written in terms of the
columns it blends rather than in cells. Vanilla DOOM 1 and 2 do not reach the doubling
threshold — E1M1 builds 75×47 at 64 — so this is about correctness under a large PWAD,
not a caveat on the shipped numbers.

1. **Grid quantisation, ±half a cell** — the same trade DOOM-0276 took, for the same
   reason. The shaft edge follows the cell grid rather than the exact wall.
   Bilinear interpolation of `zLo`/`zHi` softens it, and a softer beam edge is arguably
   a **feature** here rather than a cost — a hard-edged 67° shaft was never the DOOM
   look this spec is after (§1). Q19 owns the cell size and is unchanged by this.
2. **Two separated openings on one heading collapse to one interval.** Two windows at
   different heights on the same sun line produce two disjoint escape windows; storing
   the **convex hull** reports the *gap between them* as lit. The error is therefore
   **one-sided — it over-lights, never leaving a hole**, and that is the right direction
   for a beam effect (a missing shaft reads as a bug; a slightly tall one does not).
   It is also **rare by the same arithmetic that governs the march**: a second window
   would have to sit on exactly the sun's heading, within the handful of cells the ray
   crosses before it is blocked, and at least `m · cell/(|u.x|+|u.y|)` — about **107**
   units at 64-unit cells — above the first. The alternative — taking the
   *widest* window instead of the hull — is exact for the gap and drops the other
   window entirely, i.e. it errs toward missing shafts. Recorded so the choice is
   visible, not so it is re-litigated.
3. **An escaped `z` is never re-shadowed.** Once a starting height has cleared a sky
   ceiling it is counted as sun-lit for good, so a structure *taller* than the sky sector
   it borders does not shadow across it. **This is about a height, not about the loop:
   the cell march itself continues** — stopping it at the first sky cell is the bug named
   two sections above, and this approximation must not be read as licence for it. Also
   one-sided toward over-lighting. It is the one of the three where a real ray and the
   field can visibly disagree, so it is the thing to look at first if a play-test finds
   light where it should not be.

**What this does not change.** `kSunDir`, `kSkyShaftStrength`, `kSkyAmbientFrac`,
`kFogAnisotropy`, the phase, the ambient/directional split, and every other constant keep
their values — the *look* is meant to survive this, and L2b's acceptance is that it does.
Fog **off** stays byte-identical (INV-8): the new channels are written unconditionally at
level load but read only inside the `rb_fog`-gated march.

#### The fog-light grid goes stale the same way (2026-08-02 amendment, DOOM-0296)

L3's torch grid has exactly the defect the two sections above fix for the seep and the
clearance, and it shipped with it: `BuildFogLightGrid` (`r_vulkan.cpp`) runs **once**, from
the level-load path beside `BuildEmitterList`, so which cell of air can see which torch is
answered from **spawn-state** door heights and never revisited. Open a door mid-play and
the room behind it gets its torch's *light* (the megakernel traces that per frame) but not
its torch's *fog*.

**For doors the error is one-directional, and that is why it shipped rather than being
caught.** Doors are shut when a level loads, so a door can only ever produce a torch that
does **not** light air it could — never one that lights air through a wall. A miss reads as
nothing happening; a leak reads as the brightest thing in a dark room. ⚠ **Two exceptions, and stating the asymmetry without them is how the trigger below gets
chosen wrongly.** It is false for a plane that **rises** after load — a lift or a raising
floor between a torch and a room leaves the shipped grid lighting air *through* it, which
is D1's motivating case. And it is false for a door that starts **open**: sector special
10 spawns `P_SpawnDoorCloseIn30` (`p_spec.c`), so the load bake sees the opening and the
stale grid keeps lighting through it once it shuts. Both leak today.

**D1 — the trigger is any plane that moved, not an opening that flipped.** DOOM-0281's
`RB_SeepOpeningsChanged` detects `openrange` crossing zero, which is right for the seep
(pure connectivity) and wrong here for the same reason DOOM-0289 gives for the clearance:
the bake's question is answered by `P_CheckSightTrace`, whose `P_CrossSubsector`
(`p_sight.c`) narrows `topslope`/`bottomslope` from `opentop`/`openbottom` — so the answer
moves **continuously** with plane height, not only at the crossing. A lift rising in front
of a torch changes what a cell can see while flipping nothing. So the dirty condition is
`RB_UPD_MOVED`, which `BuildFrameReheight` already computes and already gates
`g.clearanceDirty` on; this rides that signal rather than adding one.

**D2 — it fires when the planes settle, and the reason is throughput, not the crack.**
`RB_UPD_MOVED` is a per-frame level signal, not an edge, so the naive form re-bakes on
**every frame a plane is moving**: a 128-unit door at `VDOORSPEED = FRACUNIT*2`
(`p_spec.h`) is 2 map units per tic, so 64 tics = **1.83 s**. At the ~45 FPS DOOM-0197
recorded for Ultra RT that is of order **eighty** traced frames, and at the measured 3.6 ms
the naive form spends ~**284 ms** of CPU building seventy-nine grids nobody sees. The
settle timer buys all of it back for one bake. (The frame rate is the only soft number
here: halve it and the waste halves, and the argument is unchanged either way.)

That is the reason; the crack is a second, independent one against the *edge* form. An
edge trigger fires the frame the door cracks — `openrange` is a few units — when nearly
every sight trace through it still fails, so it would record "still dark" and, having no
second edge, record it permanently. Neither form is chosen. The timer is:

| Constant | Value | What it is |
|---|---|---|
| `kFogLightSettle` | **0.15 s** | no plane has moved for this long → bake |
| `kFogLightMaxWait` | **4.0 s** | armed this long without settling → bake anyway, stay armed |

**4.0 s is argued** — against `VDOORSPEED`, below. **0.15 s is not**: it is a first guess,
picked to be a few traced frames (long enough that a door's last tic and its stop are not
two separate bakes, short enough to read as immediate) and *not* derived from anything.
Neither is measurable, because what they trade is a wait against a hitch and no profiler
produces that. They are look-and-feel dials for the play-test to move.

⚠ **The timer is map-GLOBAL.** `RB_UPD_MOVED` reports that *some* plane moved, not which,
so a lift cycling in an unvisited corner defers every door's bake to the cap for as long
as it runs. Accepted rather than fixed — a per-sector timer needs a per-sector dirty set
the engine does not keep, and the cap bounds the damage — but it is the reason the cap is
not the rare path it looks like.

⚠ **`kFogLightMaxWait` is for a mover that never stops, and `perpetualRaise` is not one —
a crusher is.** A perpetual platform waits `35 · PLATWAIT` = 105 tics = **3 s** at each end
(`p_plats.c`, `p_spec.h`), twenty times `kFogLightSettle`, so it settles twice per cycle
and never reaches the cap. ⚠ **That is not the reassuring reading it looks like**: settling
twice per cycle means **two bakes and two fog snaps per cycle, indefinitely**, on a mover
far commoner than a crusher. It is the same repeating-event question the cap raises,
arriving by the settle path instead, and §7's L3b row carries both fixtures for that
reason. `crushAndRaise` (`p_ceilng.c`) is the real case: at `pastdest`
it flips `direction` and moves off with no wait state. Without the cap the feature would be
**silently dead** on such a map — not merely unchanged, which is the trap, because "no
worse than today" is true of a still map and false of this one. **With the cap it is a
repeating event, and that is the part to judge on screen:** the fog re-snaps every 4 s from
whatever height the crusher happens to be at, unsynchronised with anything the player can
see, plus a 3.6 ms hitch on the same cadence. §7's L3b row carries it as an acceptance
item; if the snap reads badly the answer is to suppress re-bakes while a cap-path mover is
running, not to shorten the cap.

⚠ **A cap firing mid-travel is wasted, not wrong, and it self-corrects — the arm is not
cleared on that path.** 4.0 s exceeds the 1.8 s computed above for a standard door, but a
tall enough opening (over ~280 units of travel) still passes it. Such a bake records the
half-open state; then, the planes still moving, the arm survives and the settle path bakes
again with the door finished. Stated because the obvious implementation — clearing the arm
on both paths — turns that into a permanent half-open answer, which is the edge form's own
failure wearing the escape hatch's clothes.

**D3 — the result is snapped, not eased.** §4.4's clearance section above draws this line
and it is the same line: the seep is connectivity-keyed and **eases** because mist
genuinely rolls in; light is height-keyed and **snaps**. (The line is that section's, not
INV-13's — INV-13 states where sun visibility is read from and carries no ease/snap
clause.) Two further reasons specific to this grid, either sufficient alone. First, D2
means the bake lands *after* the door has stopped, so there is no partial state for an ease
to fade through. Second, a slot in this grid holds a light's **identity** (position, reach,
colour) beside its visibility weight, and two different lights in one slot cannot be
interpolated; an ease would need either a second grid or a crossfade through zero — real
machinery bought for an effect D2 has already removed the need for.

**Three claims in the DOOM-0296 roadmap bullet are superseded here**, all by precedent it
was itself citing: "ease rather than snap" (D3), keying the re-bake off
`RB_SeepOpeningsChanged` (D1), and re-baking "only the cells within reach of a light whose
visibility could have changed" (D4). Its cost figure is superseded too — see the box below.

**D4 — the whole grid re-bakes, not a scoped subset.** Measured, not budgeted (the Q22
lesson): the full bake is **3.6 ms / 6320 sight tests on E1M1** and **3.4 ms / 5228 on
MAP01**. A scope keyed to the changed linedef would be wrong — an opened door reveals a
torch to every cell within that torch's `reach`, not to cells near the door — so a correct
scope is a reach-radius sweep per changed opening, which is not obviously cheaper than
3.6 ms and is considerably easier to get wrong. Re-bake everything.

> **Command behind those two figures**, machine idle (checked with `pgrep -x linuxxdoom`),
> config `renderer 1` (Ultra) / `rt_fog 2` / `render_scale 50`:
> `./linux/linuxxdoom -iwad wads/doom.wad  -warp 1 1 -noinput`
> `./linux/linuxxdoom -iwad wads/doom2.wad -warp 1   -noinput`
>
> reading the `RB_Vulkan: DOOM-0011 L3 fog lights` line the bake already prints. (Both runs
> were actually issued as `-warp 1 1`; on `doom2.wad` the second `1` is ignored, because
> commercial mode reads only `myargv[p+1]` — `d_main.c` — so `-warp 1` above is the correct
> form and reaches the same MAP01.) E1M1 reports `449 lit of 3525 cells`; MAP01 `158 lit of 3360`.
>
> ✅ **CLOSED 2026-08-02 by implementation — the figure above is now bake+upload.** It was
> the bake alone when this box was first written: the timer stopped before
> `UploadFogLightGrid`'s `memset` + ~225 KB `memcpy` into host-coherent (write-combined)
> memory and its `vkUpdateDescriptorSets`. At level load that distinction never mattered;
> per settled door it is part of the hitch a player feels, so the upload moved **inside**
> the measurement rather than the number being annotated. **Measured: 4.1 ms on E1M1,
> 2.9 ms on MAP01**, against a gate of **≤ 6 ms** — a little over a third of the ~15 ms
> megakernel frame §6 works against, i.e. a visibly long frame but a single one, on an event
> the player caused. Both pass.
>
> ⚠ **The upload is not separable from run-to-run variance and should not be quoted as a
> delta.** Bake-alone samples on E1M1 spanned **3.6–4.7 ms** across seven runs of the same
> build, which is wider than the upload itself; the honest statement is that the event costs
> ~4 ms and the upload is inside the noise. Had the figure exceeded 6 ms the answer would
> not have been a faster bake but a different shape — slice it across frames, or scope it —
> because a hitch that large on every door is a worse trade than the stale grid it replaces.
>
> The roadmap bullet's `7788 sight tests / 4.4 ms` predates **DOOM-0302** (`63d5a2d`),
> which re-tuned `kNukageLe` `0.35/1.30/0.15 → 0.05/0.19/0.02` and `kLavaLe`
> `2.20/0.75/0.12 → 0.55/0.19/0.03`. A light's `reach` is
> `sqrtf(L.lum / RB_FOG_LIGHT_CUTOFF)` from its own intensity (clamped to
> `RB_FOG_LIGHT_MAXREACH`), so dimming the liquids shrank their reach and dropped the number of cells with
> any candidate at all — an input change, not measurement drift.

**Where it hooks, and why the placement is not free.** `RecordSeepRefresh` is the host: it
owns the `dt` clock the settle timer needs, and it is entered after `vkWaitForFences` and
inside the frame's command buffer. These constraints come with that placement:

- ⚠ **`RecordSeepRefresh` is entered once per frame but does NOT run to its end once per
  frame.** It early-outs at `if (!needUpload) return;`, and the frames the settle timer
  exists for are exactly the frames that early-out: the planes have stopped, so nothing is
  dirty, nothing is easing, and `needUpload` is false. A timer block placed after that
  return never accumulates and the bake never fires at all. It goes **after the clearance
  block and before the `!needUpload` early-out** — the same placement the clearance block
  itself had to argue for, whose code comment already says "This block MUST sit before the
  `if (!needUpload) return;` below". There is an earlier return too
  (`if (!g.seepStaging || g.seepCur.empty()) return;`). ⚠ **It does NOT gate on a seep field
  existing**, which is easy to assume and wrong: the placeholder path
  (`UploadSeepField(nullptr)`) fills `g.seepCur` with a single cell rather than clearing it,
  so the guard passes with `g.seepField == nullptr`. What makes the placeholder harmless is
  upstream — nothing arms without `RB_UPD_MOVED`, and no plane moves before a level exists —
  and downstream, since `BuildFogLightGrid` returns early on `!f` and uploads an empty grid.
  Worth stating because one consequence survives, and the plan's Step 1 carries it: a
  zero-cell buffer sized on that path would be grown and re-created by the first real bake.
- **After the clearance block — a constraint that bites on the cap path, not the settle
  path.** `RB_SeepCellAir` reads the per-cell geometry cache on `g.seepField`. A flip
  refreshes it by **swapping** the pointer for a fresh flood; a no-flip move — D1's lift —
  is refreshed in place by `RB_RefreshSunClearance`, which rewrites each cell's `fz`, `cz`,
  `sky` and `solid` from the live sectors inside the clearance block. The two are mutually
  exclusive per frame (`if (g.clearanceDirty && !didReflood && g.seepField)`), so a bake
  reads **whichever path ran this frame**. On a settle frame neither runs — nothing has
  been dirty for 0.15 s — and the cache is already current from the last moving frame. The
  ordering therefore matters only when the bake fires on a frame that is still dirty, which
  is the cap path. Correct on both, and cheap; stated so it is not "simplified" away.
- ⚠ **This assumes the clearance refresh is not deferred, and §4.4's Q30 says it may be.**
  That section calls for rate-limiting the clearance-only rebuild at a cadence Q30 leaves
  open. **No rate limit is implemented today** — the shipped `RecordSeepRefresh` refreshes
  on every dirty frame — so the conflict is latent, not live. It becomes live the moment
  Q30 lands: if its cadence exceeds `kFogLightSettle`, a settle-frame bake can read a cache
  last refreshed before the plane's final position. **Whoever closes Q30 owns this:** its
  cadence must be ≤ `kFogLightSettle`, or the fire block must force a refresh before baking.
- **The emitter set must be current, and it already is.** `BuildFogLightGrid` clusters
  `g.staticWgt`, which `BuildStaticEmitterSet` rebuilds when `g.worldEmitDirty` is raised by
  an `RB_UPD_RETEX` — a switch changing texture. That rebuild runs in `BuildFrameInputs`,
  which the present path calls **before** `RecordRtTrace` (the function holding this call
  site), so the flag is already
  cleared by the time this block is reached. Recorded because the level-load call site
  states the precondition explicitly ("must come after **both** the emitter list and the
  seep field") and a mid-play caller could quietly lose it.
- **The descriptor write must precede the dispatch's bind, and `P_CheckSightTrace` must
  run between tics.** `UploadFogLightGrid` ends in `vkUpdateDescriptorSets` on `g.rtDs`
  binding 6; `RecordSeepRefresh` is called ahead of the megakernel's
  `vkCmdBindDescriptorSets`, which is what makes that legal — the fence alone would not.
  And `P_CheckSightTrace` is playsim code on file-scope globals (it also bumps
  `validcount`), safe here for the reason `RB_SeepLineOpen` already gives for
  `P_LineOpening`: this runs on the render thread between tics, the same window
  `RB_BuildSeepField` uses, and this block sits inside the same function.

**Traced frames only, and that is correct.** `RecordSeepRefresh` has one call site, inside
the ray-traced record path, so the settle timer ticks only on traced frames. Only the
megakernel reads this grid, so a raster interlude has nothing to keep current — and the arm
is a latch (like `blasDirty`), so a door opened while rasterising is not lost. It is not
baked on the *first* traced frame either: `g.fogLightStill` only accumulates inside
`RecordSeepRefresh`, which raster frames never reach, so the bake lands `kFogLightSettle`
of **traced** frames after tracing resumes. `kFogLightSettle` is therefore a bound at
traced-frame granularity, not a wall-clock guarantee.

**What this does not change.** No constant of the look moves, no push-constant lane is
added, and no new resource appears in §5 — the grid, its stride and
`RB_FOG_LIGHTS_PER_CELL` (`kFogLightsPerCell` is its GLSL mirror) are exactly as L3 shipped
them. **With `rb_fog` off the pixels are unchanged**, because only the `rb_fog`-gated march
reads the grid (INV-8) — but the CPU bake is **not** gated on `rb_fog` and is paid whenever
RT is on and either a plane settles or `kFogLightMaxWait` elapses. That is deliberate: `rb_fog` is a
runtime toggle (the `;` key), so a grid left un-baked while fog was off would be stale the
moment it was switched back on, and the alternative — re-baking on the toggle — puts the
hitch on a keypress instead of on a door.

### 4.5 Area profiles — clear / goo / hell

Three profiles select a density multiplier `areaMult` + a `mediumTint`. The profile
density enters **§4.3b's `σ_final`** (the single authoritative form) as the sum term
`Σ areaDensity(profile) · areaMult(profile)`, where `areaDensity` is **`kAreaDensity`**
for goo — a compile-time `const`, start `0.0020`, §5 — setting how thick a *fully*
profiled medium is, and `areaMult` is the per-profile weight below. (Hell is the exception: its density
is a **runtime** value on `misc6.w`, not `kAreaDensity`, because it varies per level.)

| Profile | `areaMult` | `mediumTint` | Source of the density |
|---|---|---|---|
| Clear (default) | `0` | neutral | none — its *profile* contribution is zero, so clear air is `kFogBaseDensity · skyExposure`; the shared `heightPool · wisp · fogStrengthScale` factors of §4.3b still apply |
| Goo / nukage | `1.0` | `kGooTint` | `kAreaDensity` (`const`) |
| Hell | `1.0` | `kHellTint` | `misc6.w` (per-level, §5) |

All three numbers are first guesses, tuned at L4 (Q7/Q20).


- **Clear (default).** Base density only; neutral tint. Subtle shafts, no colour.
- **Goo / toxic.** Where the **primary hit is flagged liquid nukage**
  (`RB_FLAG_LIQUID_NUKAGE = 8u`, `rb_materials.h:17`, set on the flat at
  `FlagLiquidFlats`, `r_vulkan.cpp:5940`), thicken density and set `mediumTint =
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

Profiles compose: a goo room *on* a hell level gets both (green pool + red haze). **Densities add**
(§4.3b's `Σ` term) but **tints multiply** — that room's `mediumTint` is `kGooTint · kHellTint`,
applied once to every `Ls` contribution (colour = light × medium). Multiplication is the deliberate
pick over a density-weighted blend: it keeps one `vec3` on the hot path and needs no per-profile
weight, at the cost of reading *darker* than either tint alone. If goo-in-hell reads muddy on
hardware that is an L4 tuning dial (Q7/Q20) — raise `kHellTint` toward white; do **not** switch the
composition rule.

**2026-07-25 — the user's steer on colour.** *"The fog must be lit with any relevant
colours but only where it makes sense. Like in Hell for example."* That is what this
section already specifies; the reconciliation with §4.3b's near-white base is set out
once, at the end of §4.3b — **not restated here**. Two consequences bind **L4**:
colour is applied **by profile and withheld elsewhere** (blanket-tinting every level is
the failure mode), and the profile densities reach roofed rooms because §4.3a's
`skyExposure` gates only the **sky-sourced** haze, never `areaMult` — without that, hell
and goo interiors would have no fog to tint. Tint re-tuning is Q20's.

### 4.6 Half-res, denoise, composite

Fog is low-frequency, so compute it **cheaply and smooth it**:

- **Half-res march.** Mirror mode 6's existing even/even 2×2 half-res gate
  (`pathtrace.comp:1199`): march fog on one pixel in four, into a **new half-res fog
  target** (`inscatter.rgb` + scalar `transmittance` packed into one `RGBA16F`
  image). Mode 4 (NEE display) has **no** even/even gate and **no** SVGF upsample of
  its own, so a half-res mode-4 march would need its **own** dither + in-megakernel
  upsample; the simpler first cut is **full-res in mode 4**, half-res only in mode 6
  (Q4). **Mode 6's half-res is re-opened at L1c** — see §6 item 2 and Q18.
- **Denoise / upsample.** Fog **cannot** ride the SVGF illumination channel
  (albedo re-multiply, §3 gap 3). **What L1 actually shipped is a plain, un-guided bilinear** (`fetchFogBilinearPlain`,
  `svgf_composite.comp:53`, called at `:101` and `:129` — its own comment reads "plain
  bilinear (L1)"). The guided variant below is **L5 work, not shipped**. Two
  candidate paths (Q6): (a) a **bilateral upsample** of the half-res fog target guided by
  the gbuffer's **world position**, cheapest and self-contained; (b) run the existing
  edge-aware **a-trous** passes (`r_vulkan.cpp:7564`) on the fog channel too. Start with
  (a); escalate to (b) only if the fog crawls/flickers. **There is no depth buffer to guide
  with** — `gpos` carries the primary hit's world position in `gp.xyz` and its *material
  id* in `gp.w`, so the guide compares hit **positions**, not depths. **At sky pixels** (the
  `gp.w < 0.0` sentinel of the sky-passthrough branch, `svgf_composite.comp:93`) there is no
  hit point to compare right at the sky/wall seam where shafts read — so there L5's upsample
  must **fall back to the plain bilinear fetch** already shipped (unguided), keeping
  the shaft-against-sky reconstruction smooth. (Until L5 lands, that "fallback" is simply
  what both branches already do.)
- **Composite — computed once, applied per-mode, always in linear radiance.**
  `marchFog` *computes* `inscatter`/`transmittance` in the megakernel for **both**
  modes; where they are *applied* differs by mode:
  - **Mode 4 (NEE display, no denoiser):** fold into `L` **in the megakernel**, before
    the mode-4 tonemap — `L = L * transmittance + inscatter`, then `colour =
    toneEncode(L)` (`pathtrace.comp:1080-1083`).
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

**Why the mountains stayed crisp before L1b (diagnosis; since fixed by 1345c92).** `marchFog` runs **only** inside the world-hit
branch (`if (committed && !isSky)`, `pathtrace.comp:874`; the terms are
defined at `:868-871`); it is called at
`pathtrace.comp:1080` (mode 4) / `:1209` (mode 6). A primary ray that hits
the sky — a true miss, or a committed hit on the sky-backdrop instance (custom-index 2)
— funnels to the sky `else` branch (`:1292-1339`; mode-6 sub-branch `:1301-1324`,
mode-4 `:1326-1337`) and **never marches**: no `tHit` is
computed, and in mode 6 the half-res fog target is **not written** for that pixel (it
keeps its one-time neutral clear, `r_vulkan.cpp:2871-2881`). So the distant mountains
receive **no distance-fog** — only `skyPanorama()`'s own screen-space horizon band
(`SKY_FOG_COL` mixed by `smoothstep(0.50,0.63,suv.y)`, `pathtrace.comp:763-764`) and a
one-texel bilinear leak at the sky/wall seam. That is why the mountains read sharp.

**The fix — aerial perspective on the sky (shipped in L1b, 1345c92; described here as the design).** A sky pixel is open-sky by definition
(§4.3a), so give it the **full** fog. A sky ray has no finite `tHit`, so integrate
analytically rather than marching. **Amended 2026-07-27 — the integral is now geometric,
not a fixed distance.** For an exponential layer the integral along a straight ray to
infinity has an exact closed form:

`∫₀^∞ D·exp(−(z₀ + rd.z·t − base)/H) dt  =  D·exp(−h₀/H) · H/rd.z`

— "density where you are, times `H/rd.z`". That one term is what makes the sky behave like a
real bank: look steeply up and the ray leaves the layer almost at once, so the peaks stay
clear; look along the horizon and it grazes for miles, so the skyline goes white. The old
form gave **every** sky pixel the same haze over a fixed `kFogSkyDist`, which can never let a
mountain rise out of the mist. `kFogSkyDist` survives as the layer's finite **horizontal
extent** — what a perfectly horizontal ray gets instead of an infinite path (2048 =
`kFogMaxDist`, so the skyline is never charged more air than the furthest wall the march
covers — Q24a's point, preserved).

**It must saturate SOFTLY.** The first cut wrote `min(H/rd.z, kFogSkyDist)`, which handed
every pixel within ~3° of the horizon the identical clamped value: a flat band of uniform
haze with a definite top edge, reported as *"the fog has a hard cut off line"*. Add the
reciprocals instead —

`1/path = rd.z / kFogPoolHeight + 1 / kFogSkyDist`

— and the path approaches `kFogSkyDist` at the horizon and `H/rd.z` looking up with no kink
anywhere between. `rd.z ≤ 0` (a below-horizon sky fill) falls out of the same expression.
**Any future clamp on a smoothly-varying visual quantity should be read as this defect until
proven otherwise:** a `min()` does not cap a gradient, it replaces part of it with a plateau,
and a plateau in a gradient has an edge. The helper is
`skyFogOpticalDepth(ro, rd, strength)` in `pathtrace.comp` — **not** `pt_common.glsl`, because
it reads `pc` and the GI bake includes that header (INV-6). Fold with the same
`sky = sky · transmittance + inscatter` used everywhere else:
- **Mode 6:** `svgf_composite.comp` **cannot** compute the sky fog itself — it
  `#include`s only `formulas.glsl` / `pbr_neutral_tonemap.glsl`, not `pt_common.glsl`,
  so it has no access to `kFogMaxDist` / `kFogBaseDensity` / the tints. So do it the
  cheap, plumbing-free way: have the **megakernel's mode-6 sky branch write the
  closed-form aerial fog into the half-res `fogImg`** for sky pixels (it already holds
  the `pt_common` consts) — the sky branch wrote **no** fog there before L1b — and the
  composite's **existing** fog fold on the sky-passthrough branch
  (`svgf_composite.comp:100-103`) then picks it up **unchanged**. No new composite-shader
  code, no duplicated consts (INV-5-consistent).
- **Mode 4:** in the megakernel sky branch, after `colour = skyPanorama(...)`
  (`pathtrace.comp:1328`), fold the closed-form fog in before the write.

Distant peaks fade into the haze colour; the near horizon matches the wall-line fog
because it is the **same medium**. Because the sky is always outdoors, this term needs
**no up-ray** and is cheap (a per-pixel closed form, no shadow ray). Fog-off
(`rb_fog == 0`) leaves `transmittance = 1, inscatter = 0` → `sky · 1 + 0`, so the sky
stays **byte-identical** (INV-7/INV-8). The existing `skyPanorama` `SKY_FOG_COL`
screen-space band (`:763-764`, mixed at `:771`) overlaps the real distance-fog and must be
reconciled so the horizon is not **double-hazed**; L1b halved it, and L1c re-judges it
against the near-white base (Q14).

**The sky's in-scatter tone follows `kFogColor` too (2026-07-25; SHIPPED).** Both closed
forms now in-scatter `kFogColor` (`pathtrace.comp`, the two `kFogColor * kSkyShaftStrength
* (1.0 - trans)` lines, each marked "L1c tone") — this passage said the swap was still owed
until 2026-08-03, when it had in fact landed with L1c. It had to move
alongside the foreground — otherwise the mountains haze **cool blue** while
everything in front of them goes near-white, producing precisely the sky/wall seam L1c's
own acceptance criterion is there to catch. Wisp-free (INV-10), but **not**
colour-frozen.

## 5. Data & resources

- **L1: one new image — a half-res fog target** (`RGBA16F`, §4.6) — **plus its descriptor
  bindings**: a megakernel **write** target and a `svgf_composite.comp` **read** input
  (mode 6). No new SSBOs, light/emitter buffers, or vertex data — fog reuses the
  existing `Emitters` buffer + sky.
- **Push constants — the two genuinely-free lanes.** DOOM-0183 grew
  `RtPushConstants` to **240 B** (`static_assert(sizeof==240)`, `r_vulkan.cpp:7405`;
  `pcr.size = 240`, `:2368`) and consumed `misc6.x` (ripple time), `misc6.y` (wet
  toggle). The **only** free components were **`misc6.z` and `misc6.w`**. This feature
  uses **exactly those two**, needing **no** struct growth (L1 already wired `misc6.z`,
  `:7459`; `misc6.w` stays `0` until L4, `:7460`):
  - **`misc6.z` = `rb_fog` strength** — a small **uint** (0..3; `0` = off, which also
    *is* the on/off state), written/read exactly like `misc6.y = rb_wet`
    (`pc.misc6[2] = (uint32_t)rb_fog`, `:7459`; the shader reads a `uint`).
  - **`misc6.w` = global haze density** — a **bit-cast float** (like `misc6.x` ripple
    time: bit-cast in on the C++ side, `uintBitsToFloat` in the shader); the
    hell-level haze from `rb_view_t` (§4.5), `0.0` on non-hell levels.
  - **`pc.fogFloorZ` = the outdoor fog layer's altitude** (added 2026-07-27) — a **bit-cast
    float** in world units, from `rb_mesh_t::fogFloorZ`, which `RB_BuildLevelMesh` sets to the
    lowest floor among the level's open-sky sectors (`0` if it has none, in which case nothing
    reads it: the seep field's open-sky mask is `0` everywhere, so no sample takes the
    outdoor branch — the up-ray this clause used to name was deleted by DOOM-0276). It
    costs **no push-constant budget** — it
    occupies the first of the two pad words `misc6`'s 16-byte alignment already forced, so the
    range stays 240 bytes and `-rtverify`'s 184-byte prefix is untouched. Per level, not per
    frame, but it rides the same push as everything else rather than earning a UBO.
  - Everything else is a **compile-time `const`** per house convention
    (DOOM-0181/0183 §5), each with a starting value so L2–L4 are buildable without a
    round-trip: `kSunDir` = `normalize(0.30, 0.30, 1.0)`, `kFogSteps` = 24
    (**the L1c raise to ~40 was measured, falsified and reverted 2026-07-30** — not open),
    `kFogBaseDensity` = 0.0033 (**the L1c ≈2× raise was tried and REVERTED 2026-07-30 —
    this is the shipped value, not an interim one**), `kFogMaxDist` = 2048,
    **`kFogSkyDist` = 2048** (the sky's GRAZE CLAMP since 2026-07-27, no longer "the sky's
    distance" — §4.6a), `kFogPoolHeight` = 112 (**outdoor** e-fold height),
    **`kFogIndoorPool` = 18** (roofed air keeps the shallower bank),
    `kFogAnisotropy` = 0.40, `kGooTint` = `(0.35, 0.85, 0.30)`,
    `kHellTint` = `(0.90, 0.35, 0.30)`, `kIndoorFogScale` = 0.05, the per-source
    strengths (**shipped 2026-08-02: `kSkyShaftStrength` = 0.85, `kTorchShaftStrength`
    = 0.047 — both were first guessed at 1.0 and this list said so until corrected**),
    **`kAreaDensity` = 0.0020** (§4.5's profile density), **`kFogFloorFallback`**
    and **`kTorchFalloff`** (§4.3/§4.4). **`kFogFloorFallback` was never needed** and is not in the
    tree — the outdoor reference is the per-level `pc.fogFloorZ` and the indoor one is
    camera-relative (§4.3).  **`kTorchFalloff` is discharged, corrected 2026-08-02:** it
    shipped as **`kTorchSoftR2`** (= `32.0 * 32.0`, `pt_common.glsl`), the softening radius
    SQUARED in the falloff denominator, so no constant of that name is owed. **`kFogDepthSigma`** (L5's bilateral guide, §4.6 — a
    distance in **world units** between two hit positions, not a depth ratio) is the **one
    exception: it is declared in `svgf_composite.comp` itself, NOT in `pt_common.glsl`**, because
    that shader includes only `formulas.glsl` and `pbr_neutral_tonemap.glsl` — the same limitation
    §4.6a leans on to justify computing the sky fog in the megakernel. **Shipped today** (`pt_common.glsl`, verifiable by grep): `kFogSteps`,
    **`kSkyAmbientFrac`** (= 0.65 — L2's ambient/directional split, §4.4(a)'s correction box;
    the most load-bearing constant in L2 and previously missing from this list),
    **`kWispSquashZ`**, **`kWispTexels`**,
    `kFogMaxDist`, **`kFogSkyDist`**, `kFogBaseDensity`, `kSunDir`, `kSkyShaftStrength`,
    `kTorchShaftStrength` (= 0.047), **`kTorchSoftR2`** (= `32.0 * 32.0`),
    `kIndoorFogScale`, `kFogPoolHeight`, **`kFogIndoorPool`**,
    `kEyeAboveFloor`, `kFogAnisotropy`, `kGooTint`, `kHellTint`, **`kFogLightsPerCell`**
    (= 2 — L3's per-cell light budget, and the constant that bounds its per-sample cost),
    **`kIndoorSkyLight`** (= 0.45). **Not yet in the tree** — `kAreaDensity` only
    (corrected 2026-08-02: this line used to add "and every 2026-07-25 constant below",
    which the paragraph below already contradicts — they shipped at L1c/L1d).
    **The 2026-07-25 constants belong to the same inventory:**
    `kFogColor` = `(0.55, 0.56, 0.56)`, `kWispAmp` = 0.6, `kWispWeight2` = 0.7,
    `kWispFreq1` = 1/512, `kWispFreq2` = 2.5·`kWispFreq1`, `kWispVel1` = `(8, 3, 1)`,
    `kWispVel2` = `(−3, 4, 0.3)` units/s (deliberately **slower** than `kWispVel1`, §4.3b), `kWispOffset2` = `(17.3, 5.1, 23.7)`,
    **`kFloorFogDensity`** = 0.010, **`kFloorFogPool`** = 24, **`kFloorFogRange`** = 256
    (§4.3c's second layer — `kFloorFogPool` ≪ `kFogPoolHeight`, `kFloorFogRange` ≪ `kFogMaxDist`;
    **none of the three is in the tree yet**; the arithmetic behind all three is in §4.3c, and
    Q25 owns them on hardware),
    `kSeepMax` = 0.5, `kSeepFalloff` = 192, `dMax` = `8 · kSeepFalloff` (the seep field's
    finite unreachable/void sentinel — §4.3a) (values derived in §4.3a/§4.3b; `kWispAmp`
    and `kWispWeight2` are owned by Q21 alongside `kWispFreq1`).
    **Four of the figures in the paragraph above are the values first written down, not
    the values that shipped** (corrected 2026-07-30 — this bullet is the implementer's
    lookup table, so a stale number here is read as authority): `kSeepMax` ships **0.9**
    and `kSeepFalloff` **384** (the DOOM-0281 re-tune, §4.3a), `kWispAmp` ships **1.0**
    and `kWispFreq1` **1/192** (L1c's tuning, §4.3b), and `kFogColor`, `kWispAmp`,
    `kWispWeight2`, the octave frequencies/velocities, `kSeepMax`, `kSeepFalloff` and
    `dMax` are all **in the tree now** rather than pending. **L1e shipped, so its three constants are in the tree too**
    (`kFloorFogDensity`, `kFloorFogPool`, `kFloorFogRange` — `marchFog` calls
    `floorFogDensity()`), leaving only **`kAreaDensity`** (L4) genuinely owed —
    `kTorchFalloff` came off this list on 2026-08-02, having shipped as `kTorchSoftR2`.
    **The two octave velocities changed again on 2026-08-01 (DOOM-0300), and the reason
    matters more than the numbers**: `kWispVel1` ships **`15 · (8, 3, 1)`** and `kWispVel2`
    ships **`−kWispVel1`**, so the "deliberately slower second octave" noted above is no
    longer true and the pair is now *exactly* opposed. Both halves are measurements against
    Silent Hill 2 rather than taste — 15× because SH2's fog restructures in under ~2.2 s
    where ours needed 24 s to cross one noise cell, and exact opposition because a
    best-shift search over SH2 explains 0–1 % of its change by translation: that fog churns
    in place and has no wind. Exact opposition also makes DOOM-0300's per-level
    `wispAngle` safe, since rotating both by one angle leaves their sum identically zero.
    **DOOM-0289 adds three quantities (five `#define`s), and none is a `k*` shader const**
    (2026-07-30). The sun-clearance field is built C-side, so they live in `r_mesh.h`
    beside `RB_SEEP_DMAX`: **`RB_SUN_DIR_X/Y/Z`** (the mirror of `pt_common.glsl`'s
    `kSunDir` — the shader stays authoritative), **`RB_SUN_NEVER`** = 128 (the finite
    no-sun sentinel, Q28) and **`RB_SUN_MARCH_MAX`** = 32 (the march's backstop). The
    shader gains **no** new constant at all: it reads two more channels of a texture it
    already samples. Only the runtime **strength** and the **per-level haze** vary at
    runtime, so only they take lanes.
  - **Budget note (INV-5):** this consumes the **last two free components** of the
    shared RT push block. Any *further* RT push value must append the final
    `misc7 uvec4` (240 → 256 B, the documented device limit; after that the block is
    full). This spec deliberately stays within the free lanes and does **not** add
    `misc7`, leaving that headroom for the future.
  - The fog lanes sit **beyond the 184-byte `-rtverify` prefix**
    (`static_assert(sizeof(RtPC)==184)`, `r_vulkan.cpp:6858`, which stops before
    `misc6`), so **`-rtverify` is unaffected** (INV-7).
- **New `rb_view_t` field for the hell flag.** `rb_view_t` (`r_mesh.h:265-273`)
  currently carries only `x,y,z,angle,extralight,skytexnum`. Add one field
  (e.g. `float hazeDensity`), computed beside `view.skytexnum = skytexture`
  (`r_backend.c:181`) from `gameepisode`/`gamemap` + the sky, and written to
  `misc6.w`. (`r_backend.c` does not reference `gameepisode`/`gamemap` today; the
  compute brings them into scope.)
- **Runtime dial `rb_fog` — already shipped (`f8c6b1f`)**, `rb_detile`-style 0..3 value,
  `rb_wet`-style wiring. **Only the menu rows below remain unbuilt** (no `ef_fog` /
  `vid_fog` / `M_ChangeFog` exists in `m_menu.c` yet):
  `extern "C" { int rb_fog = 1; }` in `r_vulkan.cpp` (`:1009`, beside `rb_wet` `:1003`) — a
  **subtle "Low" on by default**, matching the on-by-default effect siblings
  (`rb_wet=1`, `rb_filth=1`, `rb_detile=2`) so atmosphere is present out of the box
  (perf-gated at L6; flip to `0` for off-by-default if review prefers — Q10); a config
  row `{"rt_fog", &rb_fog, 1}` in `m_misc.c` defaults (`:275`, beside `rt_wet` `:274`);
  the value written to `pc.misc6[2]` (`:7459`, beside `misc6[1] = rb_wet` `:7458`).
  `rb_fog` is a
  small **strength** integer (0..3, `0` = off), so the menu "Strength" row and the
  on/off state share it. **The 0..3 → density-multiplier mapping is
  `fogStrengthScale()` = `0.35` (Low) / `0.65` (Med) / `1.0` (High)**
  (`pt_common.glsl:65-67`) — load-bearing, since §4.3b's sky-transmittance arithmetic is
  quoted against it.
- **Menu rows — place like `rb_wet` (both menus, DOOM-0206 doubled them), behave like
  `rb_detile` (a 0..3 cycle with a name table, NOT a boolean).** `rb_wet` shows as a
  row in the legacy Effects menu **and** the crisp Video menu, so the fog row goes to
  the same two menus; but a multi-value strength dial is the `rb_detile` pattern
  (`M_ChangeDetile` cycles `0..2`, drawn via `detileNames[]`), not the boolean
  `rb_wet`/`M_ChangeWet` On-Off. That is **seven** edits + one string table, not two —
  adding only the menuitem arrays ships a blank/mislabelled row (or fails to compile):
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
     (`rb_fog = (rb_fog + 1) % 4`), used by both menus, plus a new name table
     `char fogNames[4][6] = {"Off","Low","Med","High"}` — matching
     `detileNames`'s fixed 2-D form (`char detileNames[3][7]`, `m_menu.c:1229`), not a
     `const char*[]`.
  7. **Forward declaration:** `void M_ChangeFog(int choice);` beside
     `void M_ChangeWet(int choice);` (`m_menu.c:224`). Without it the menuitem arrays
     reference an undeclared function and the file does not compile.
  So the "Strength" presentation Off/Low/Med/High maps to `rb_fog` 0..3. Placement =
  wherever `rb_wet` sits; mechanism = the `rb_detile` multi-value pattern.
- **Hotkey — already shipped (`f8c6b1f`), not L6 work.** `;` is wired at
  `i_video.c:479-487` (cycles `rb_fog` 0..3 and prints `Volumetric fog: <level> (RT
  only)`); the toggle chain now runs `i_video.c:412-487`. Retained here as the record of
  why that key: it was the free one — `;`
  (`SDLK_SEMICOLON`) is unused (`]`=de-tile, `[`=filth, `'`=wet, `` ` ``/`~`=RT view
  cycle — the same physical key — and `` \ ``=per-pass GPU profiler are taken;
  verified `i_video.c:425` / `:433`). Cycles `rb_fog` and prints `Volumetric fog: <level>`.

- **2026-07-24 amendment — no new runtime resource.** The open-sky standard (§4.3a)
  and the sky-backdrop fog (§4.6a) add **no push-constant lane, no SSBO, no new image**:
  `skyExposure` is read from the seep field's `.g` mask (DOOM-0276 replaced the up-ray this
  clause used to name), the sky
  fog is a per-pixel closed form, and `kIndoorFogScale` / `kFogMaxDist` are compile-time
  `const`s. So **INV-5 holds unchanged** — still 240 B, `misc6.z/.w` the only runtime
  lanes *in `RtPushConstants`*. (Note the composite-side fog gate is a **separate**
  pre-existing lane, not touched here: `svgf_composite.comp` has its own 120-byte
  `SvgfPC` struct — `static_assert(sizeof(SvgfPC)==120)`, `r_vulkan.cpp:7529` — and L1
  mirrors `rb_fog` into its free `misc3.y` since `SvgfPC` has no `misc6`, written at
  `r_vulkan.cpp:7589`. That lane is out of scope for this "no new lane" claim, which is
  about the megakernel push block.) The **only** optional data addition is the fallback bit
  **`RB_MESH_OUTDOOR = 0x100`** on the existing per-vertex `flags` int — specified once
  in §4.3a and not restated here. **Not built:** L1b shipped the up-ray; the bit is
  retained as a standing perf lever (§4.3a, §6).

- **2026-07-25 amendment — two new sampled images; still no new push lane.**
  - **A 3-D noise volume** for the wisps (§4.3b) — `R8`, start `64³` (~256 KB),
    **generated on the CPU at startup** from a **fixed compile-time seed** with a uniform
    `[0,1]` value-noise generator (mean `0.5`, which §4.3b's mean-1 `wisp` form assumes) —
    **determinism is load-bearing, not incidental**: `-shotcompare`'s golden gate (§6) is
    only a valid pass/fail if the volume is byte-identical run to run, so a time- or
    address-seeded generator would silently turn that gate into noise. Built once after the
    Vulkan device exists, beside the other startup uploads. Not shipped as an asset —
    **no file enters the tree**, so the
    provenance/licence checklist in `docs/standards/assets.md` does not apply at all
    (that standard governs assets entering the repo, not data synthesised at runtime). Trilinear filtering, `REPEAT` wrap on
    all three axes. The two octaves are two taps at different scales, so **one** volume
    serves both; it is level-independent and built once.
  - **Host-side, per level (DOOM-0289):** two more `float[w·h]` arrays on `rb_seep_t`
    (`zLo`, `zHi`), their `std::vector<float>` mirrors on the Vulkan side, and the
    **retained** `rb_cellgeom_t` grid the clearance rebuild marches over (~16 B/cell).
    Worst case at `256×256`: ~1.0 MB of cell geometry and ~1.0 MB of float arrays, against
    the 512 KB texture — **the host allocations are the larger half**, worth stating
    because the texture is the part everyone counts. E1M1's 75×47 grid is ~110 KB all
    told, and all of it is freed with the field at level teardown.
  - **A 2-D outdoor-distance field** for the seep (§4.3a amendment) — **`RGBA16F`**, and
    the channel count has grown twice as the same grid answered more questions:
    `R16F` (L1d, distance alone) → `RG16F` (DOOM-0276 added the `.g` open-sky mask,
    2026-07-27) → **`RGBA16F` (DOOM-0289 added `.b`/`.a` = the sun-clearance interval
    `zLo`/`zHi`, 2026-07-30 — §4.4's amendment)**. Not `R8` at any width, because
    normalising `d` against `kSeepFalloff` would cap representable
    distance at 192 units, flooring `exp(-d/kSeepFalloff)` at `e⁻¹ = 0.368` and so
    `skyExposure` at ≈`0.22`, four times the intended indoor floor, everywhere).
    Covers the map's XY extent at `64`-unit cells (a large vanilla map stays well under
    `256×256`, i.e. ≤ 512 KB at four channels — ≤ 256 KB before DOOM-0289, ≤ 128 KB
    before DOOM-0276). **Rebuilt per level**, beside the existing mesh build, and
    re-flooded mid-play by DOOM-0281 when an opening flips.
    **One image, one sampler, one descriptor, one tap** — that is the whole reason each
    new question has been answered by widening this field rather than adding another
    resource: the tap is already inside the march loop, so a channel is free where a
    second lookup would not be.
  - **A small UBO carrying the field's world→texel transform** (map XY origin + inverse
    cell size + texel dimensions). This is **per-level runtime data**, so it can be
    neither a compile-time `const` nor a push lane (INV-5 is full) — without it the
    shader cannot turn `p.xy` into a texture coordinate and L1d stops at its first line
    of shader code. It rides the **same existing set 0** (`g.rtDsLayout`) as the two images —
    there is no new set; see the next bullet. Concretely: noise volume = binding 3, seep field =
    binding 4, this UBO = binding 5, continuing from the shipped bindings 0–2. INV-5 is
    about the **`RtPushConstants` block** and is unaffected by a UBO in a descriptor
    set.
  - **Neither BINDLESS sampled-image set can be appended to — so the new resources go on a
    FIXED set instead.** Both `set 1` and `set 3` end in a
    `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT` bindless array
    (`set 1` binding 2 = the R8 material array, `r_vulkan.cpp:3859-3862`; `set 3`
    binding 1 = the bindless PBR array, `:5674-5677`), and Vulkan requires that binding
    to be the **highest** in its set — so a new `set 1` binding 3 or `set 3` binding 2
    is invalid without renumbering the bindless array. `set 1` binding 1 is **not** free
    either: it is the 2D HUD/menu overlay (`r_vulkan.cpp:3854-3857`), declared host-side
    even though no compute shader names it. And `g.dsLayout` is referenced by **four** pipeline
    layouts — at **set 1** in RT (`:2381`) and bake (`:2519`), at **set 0** in the shadow
    (`:3560`) and raster-world (`:4179`) layouts — not just the bake. **But sets 0 and 2 have no such constraint** and are the right home: `g.rtDsLayout`
    (set 0) is three fixed bindings (`r_vulkan.cpp:2317-2330`) and `g.svgfDsLayout`
    (set 2) is ten fixed bindings (`:2554-2568`) — indeed **DOOM-0011 L1 already appended
    to set 2** (`fogImg` at binding 9, `pathtrace.comp:62`; `const uint32_t counts[10]`,
    `r_vulkan.cpp:2555`, "added without disturbing 0-8"). So: put the noise volume, the
    distance field and the transform UBO on **`g.rtDsLayout` (set 0)** as new bindings.
    **Two consequences that must ship with them:** (i) `g.rtDs` is allocated from a pool
    (`:2358`) sized only for `ACCELERATION_STRUCTURE` + `STORAGE_IMAGE`, so
    `COMBINED_IMAGE_SAMPLER` + `UNIFORM_BUFFER` pool sizes have to be added or the
    allocation fails; (ii) set 0 is the **same set the mode-5 `-rtverify` path binds**
    (`:6925`), so the new bindings need `PARTIALLY_BOUND` (or dummy descriptors bound on
    that path) — otherwise "`-rtverify` is unaffected" (INV-6/INV-7) stops being true the
    moment the bindings are declared.
    This keeps the set **count at 4**, leaves the `-rtverify` bind site (`:6925`) and the
    display bind site (`:7496`) each binding the same four sets, and leaves the bake
    untouched by construction (INV-6). A separate fifth set (the
    `r_vulkan.cpp:4022-4027` precedent) is the fallback if set 0 proves awkward.
  - **No new push-constant lane.** Wisp drift reuses **`misc6.x`** (DOOM-0183 ripple
    time, `r_vulkan.cpp:7455-7457`); everything else new (`kFogColor`, `kWispAmp`,
    `kWispWeight2`, the octave frequencies/velocities, `kSeepMax`, `kSeepFalloff`) is a
    compile-time `const` per house convention. **INV-5 holds unchanged** — still 240 B,
    with `misc6.z/.w` still the only fog runtime lanes.

## 6. Performance budget

**The budget at a glance.** Each layer has its own ceiling so that no early layer can quietly
eat the whole allowance and leave the later ones unbuildable. Only the last row is a
pass/fail gate; the rest are go/no-go spot-checks.

| Layer | Its own ceiling — unless marked † | Running total must stay under |
|---|---|---|
| **L1b** *(shipped)* | ≤ 4 % | 4 % |
| **L1e** | ≤ 0.5 % on top of L1b — pure ALU inside the existing loop, **no new ray** | 4 % — see ‡ |
| **L1c** | `8 % − Δ(L1b)` — whatever L1b left | 8 % |
| **L1d** | ≤ 1 % (the seep tap) | 9 % |
| **L2–L5** (+ **L2b**) | † **≥ 6 % reserved for them to share.** L2b spends none of it — it *returns* what L2 overspent | 15 % |
| **L6** | measures, adds nothing | **15 % — the formal pass/fail** |

‡ **L1e ships before the pending Δ(L1b) measurement**, and that measurement is a fog-**off** vs
fog-**on** A/B, not a per-layer one — so the Δ this table eventually records is Δ(L1b + L1e), and
that is the number L1c's `8 % − Δ(L1b)` gate reads. No other row moves. If the combined figure
overruns 4 %, the floor fog's three constants are the first lever to reach for: it is the only new
term added since L1b shipped, and its cost is arithmetic, not rays.

† The L2–L5 figure is a **floor, not a ceiling** — a promise *to* those layers. After L1c and
L1d have taken their slices, at least 6 % of the 15 % must still be unspent. All percentages are of
**present-total in milliseconds**, fog-off vs fog-on over the same walk. The prose below
derives these; the table is the version to check against.

> ### ⛔ MEASURED 2026-07-27 — the fog costs **+35 %**, and the 15 % gate is already blown
>
> The A/B this section has owed since L1b shipped, taken on the RX 6600 in E1M1 (Ultra, RT view,
> 50 % render scale, `\` profiler, one session, fog **High** then **OFF**):
>
> | | fog High | fog OFF | Δ |
> |---|---|---|---|
> | **present-total** | **32.53 ms** (median 32.84) | **24.15 ms** (median 24.36) | **+8.38 ms = +34.7 %** |
> | GPU megakernel | 21.21 ms | 13.27 ms | **+7.93 ms** |
> | GPU denoise+taau | 7.01 ms | 6.75 ms | +0.27 ms |
> | GPU blit | 0.29 ms | 0.28 ms | +0.01 ms |
> | CPU build | 3.61 ms | 3.51 ms | +0.10 ms |
>
> **Read it as Δ(L1b + L1d + L1e), not Δ(L1b).** The A/B is fog-off vs fog-on and all three had
> landed, exactly as footnote ‡ anticipated. **95 % of the cost is in the megakernel**, i.e. inside
> `marchFog` — not in the denoiser, not on the CPU, and not in L1d's texture tap (the seep fill is
> load-time and its runtime tap rides in that same figure without moving the CPU column).
>
> **Caveat, stated because it changes what the number can be used for:** the two halves were *not*
> the same walk — 74 samples with fog on against 18 with it off — so the present-total figure
> carries whatever the scene difference contributes. It is not precise enough to tune against. It
> is far more than precise enough for the decision it forces: an 8 ms step is not a walk artifact,
> and the per-pass split corroborates it independently.
>
> **Consequences, and neither is optional.** L1c's allowance was `8 % − Δ(L1b)`, which is now
> *negative*; and L1c would itself raise `kFogSteps` 24 → ~40 (**+67 % more samples, each carrying
> the up-ray**) and double the density. **A perf pass has to come before L1c**, not after it. The
> lever list below was written for exactly this moment; the up-ray is named there as the pole and
> the measurement agrees.

> ### ✅ RE-MEASURED 2026-07-27 after DOOM-0276 — the fog now costs **+4 %**, inside the gate
>
> Same RX 6600, same E1M1, same 50 % render scale, RT view, fog **High** vs **OFF**. Method
> tightened over the notice above: the camera **stands at the spawn point** instead of walking,
> both builds get an explicit `-iwad` (without it the DOOM-0060 chooser picked a *different game*
> across two otherwise-identical runs — DOOM-0280), each configuration is run **three times for 31
> profiler samples each**, and the pre-change build is measured in a git worktree at `8522b23` so
> the two are the same scene, same second, same machine state.
>
> | | fog High | fog OFF | Δ |
> |---|---|---|---|
> | **before** — present-total | 32.03 ms | 23.66 ms | **+8.37 ms = +35.4 %** |
> | **before** — GPU megakernel | 20.04 ms | 11.85 ms | +8.19 ms |
> | **after** — present-total | **24.53 ms** | 23.55 ms | **+0.98 ms = +4.2 %** |
> | **after** — GPU megakernel | **12.56 ms** | 11.75 ms | +0.81 ms |
>
> **The fog-off column is the control**, and it is the reason to trust the rest: DOOM-0276 touches
> only code inside the `rb_fog != 0` march, so fog-off *must* be unchanged — and its median is
> 12.19 ms of megakernel on both builds, to the hundredth. The before-column also reproduces the
> walk-based `+8.38 ms / +34.7 %` above to within 0.1 ms by a completely different sampling method.
>
> **Net on the frame that matters:** 32.03 → 24.53 ms present-total, **31.2 → 40.8 FPS**, and the
> feature lands at **+4.2 %** against its ≤ 15 % gate. **L1c is unblocked**, with roughly 11 points
> of gate left to spend — though the honest reading is that the fog is no longer where the frame
> goes: 23.6 ms of the 24.5 is everything else.
>
> **Look, checked before the number was believed** (`-shotverify`, same spawn view, both builds):
> mean-abs-error **2.93/255**, against **1.09/255** between two runs of the *same* build — so about
> 1.8 of real change, which is the roofline moving onto the 64-unit grid, and nowhere near what
> "the fog stopped being drawn" would score. Both sit under the project's own `-shotcompare` fail
> bar of 3.0. `-rtverify` PASS (rel-MSE 0.0796 %, white furnace 0.000000). **The doorway threshold
> is still a user play-test**, not a screenshot: that is where the half-cell error lives.

> ### ⛔ MEASURED 2026-07-30 — L2 blew the gate again, and `master` ships it
>
> Same shape of failure as the boxed notice two above, same cause (one ray per march
> sample), same fix available (a load-time field). E1M1 courtyard, **default
> `rt_fog = 1` (Low)** — not High — 50 % render scale, RT view:
>
> | | fps | GPU megakernel |
> |---|---|---|
> | pre-L2, fog **off** | 43 | 12.7 ms |
> | pre-L2, fog **Low** | 40 | 13.4 ms |
> | **L2 (`544ae84`), fog Low** | **25** | **27.0 ms** |
>
> The fog's own cost went **0.7 ms → 14.3 ms**, of which the sun ray is **13.6 ms** —
> **19× the entire rest of the fog**, for **−15 fps**. Against the ≤ 15 % gate that is
> roughly **+60 % of present-total** (derived from the fps and megakernel columns; the
> exact present-total A/B is L2b's Step 6, and it is the number that counts). At
> `rt_fog = 3` the megakernel is 26.5 ms against 15.4 ms with fog off.
>
> **Two things make this worse than the 2026-07-27 notice, not equivalent to it.**
> (i) It is measured at **Low**, the shipped default (Q10) — so it is live for anyone
> playing Ultra RT right now, not a High-only worst case. (ii) It **blocks L3**: torch
> shafts add their own per-sample work, and there is no honest way to measure that on
> top of a march that is already 19× over.
>
> **The fix is DOOM-0289 (§4.4's 2026-07-30 amendment)** — the ray becomes two more
> channels on the field the march already taps, so the 13.6 ms goes to approximately
> nothing and the fog should return to about its pre-L2 cost plus the HG phase (pure
> ALU). L2b carries the re-measurement; **L2 is not "done" until that number is in this
> section.**

> ### ✅ RE-MEASURED 2026-07-31 after DOOM-0289 (L2b) — the fog costs **+3.2 %**, and the gate passes
>
> RX 6600, E1M1 **standing at the spawn**, Ultra RT, 50 % render scale, explicit `-iwad`,
> `-noinput` on every run, the `` \ `` profiler enabled via `rt_profile` in a throwaway
> `-config` (a key press cannot reach a `-noinput` run). **Three runs per configuration**,
> ~121 profiler samples pooled per cell, **medians**. The pre-change build is a git
> worktree at `544ae84`, so both halves are the same scene, second and machine.
>
> | | present-total | vs its own fog-off | GPU megakernel | fps |
> |---|---|---|---|---|
> | **before** (`544ae84`) fog **off** | 24.88 ms | — | 14.41 ms | 40 |
> | **before** fog **Low** | 35.75 ms | +10.87 ms = **+43.7 %** | 24.50 ms | 28 |
> | **before** fog **High** | 35.93 ms | +11.05 ms = **+44.4 %** | 24.58 ms | 28 |
> | **after** (L2b) fog **off** | 22.46 ms | — | 12.22 ms | 44 |
> | **after** fog **Low** *(shipped default, Q10)* | 23.52 ms | +1.05 ms = **+4.7 %** | 13.17 ms | 42 |
> | **after** fog **High** *(the gate)* | **23.18 ms** | +0.71 ms = **+3.2 %** | 13.01 ms | 42 |
>
> **The gate is High ≤ 15 % and it lands at 3.2 %.** The 13.6 ms sun ray is gone: the fog's
> own megakernel share is **0.79 ms** where it was 10.17. On the frame that ships, Low goes
> **35.75 → 23.52 ms present-total, 28 → 42 fps**.
>
> **Low and High are within noise of each other after the change** (23.52 vs 23.18 ms), which
> is itself corroboration: `rb_fog` scales *density*, not sample count, so once the
> per-sample ray is gone there is little left for the dial to move. Reported rather than
> smoothed over — High measuring a shade cheaper than Low is a plausible early
> `trans < 0.003` break, not a result to lean on either way.
>
> #### ⚠ The fog-off control MOVED, and the explanation is a finding in its own right
>
> Task L2b's Step 8 makes fog-off the control on the grounds that this change touches only
> code inside the `rb_fog != 0` march. It moved anyway — **14.41 → 12.22 ms of megakernel**
> — which on its face invalidates the A/B. It does not, and the reason was measured rather
> than argued, on a third worktree at `b7f4329` (the commit *before* L2), same harness:
>
> | fog **off**, all three builds | megakernel | fps |
> |---|---|---|
> | pre-L2 `b7f4329` — no sun ray in the shader at all | **12.01 ms** | 45 |
> | L2 `544ae84` — ray present, never executed | **14.41 ms** | 40 |
> | L2b (this change) — ray deleted | **12.22 ms** | 44 |
>
> **L2's ray cost 2.40 ms per frame with fog switched OFF.** `rb_fog` is a *push constant*,
> not a spec constant (unlike the view mode, DOOM-0129), so the `rayQueryEXT` sat in the
> compiled megakernel on every pixel of every frame regardless of the dial, and a ray query
> costs registers whether or not control flow reaches it. Deleting it returns 2.19 of those
> 2.40 ms; the 0.21 ms residual against pre-L2 is the widened `RGBA16F` tap and run-to-run
> noise. **So the honest control is pre-L2 vs L2b — 12.01 → 12.22 ms — and it holds.**
>
> Two consequences worth carrying forward. **(i)** L2's regression was *larger* than the
> 2026-07-30 notice recorded: −15 fps with fog on, and ~5 fps more that nobody attributed to
> it because fog was off. **(ii) "It is inside a `rb_fog`-gated branch, so it cannot cost
> anything when fog is off" is false in this shader**, and any future layer that adds a ray
> query to `marchFog` must be measured with fog **off** as well as on. That is a standing
> correction to how this section's A/B is read, not a one-off.
>
> #### Method notes, both learned the hard way this session
>
> - **`-noinput` did not exist at `544ae84`.** It landed in `b23d609` (DOOM-0287), *after*
>   the L2 commit, so the older half silently ignored the flag, grabbed the pointer, and a
>   stray mouse movement turned its camera mid-run — caught by the user, not by the harness.
>   The fix is to **cherry-pick `b23d609` into any pre-DOOM-0287 worktree** before measuring;
>   it touches `i_video.c` only, nothing in the renderer. Both control worktrees here carry
>   it. Any future A/B against a commit older than `b23d609` needs the same step.
> - **Level-load fill:** 0.6–0.8 ms before, **0.8–0.9 ms after** on E1M1's 75×47 grid — the
>   clearance march adds **~0.2 ms** to a load-time pass, against L1d's ≤ 20 ms budget. The
>   field's own count is on the same line: **2937 no-sun cells of 3525, 572 of them
>   open-sky**.
> - **Clearance-only rebuild (Q30's number, and it settles Q30):** **0.08–0.18 ms**, typical
>   **0.12 ms**, measured on a live frame with a throwaway hook moving a sector plane between
>   two open heights (no opening flips, so only the clearance latch fires — the seep's
>   detector correctly stays silent). A 40-tic lift therefore costs ~40 × 0.12 ms spread over
>   a second, about **0.5 % of one frame each**. **No rate limit is needed**, and that is a
>   measurement rather than a budget — the Q22 lesson applied rather than relearned.

> ### The clearance march, verified against a falsifiable prediction (2026-07-31)
>
> The build line's no-sun count is a **weak** detector on its own and this is worth recording,
> because the obvious reading of it is wrong. An open-sky cell's interval only *empties*
> under a roof; a wall beside it merely raises `zLo`. So the count cannot respond to the sun
> at all for outdoor air — measured flat at **2937 across sun elevations from 19° to 87°**,
> which looks exactly like a march that is ignoring its input.
>
> **It is not. The statistic that must move is the lit band**, and it does, monotonically and
> in the direction geometry demands (mean `zLo` over genuine open-sky cells, E1M1, by
> temporarily varying `RB_SUN_DIR_Z`):
>
> | sun elevation | mean `zLo` |
> |---|---|
> | 19° | **109.7** — only high air is lit; low walls block the grazing ray |
> | 50° | 15.3 |
> | **67° (shipped)** | −12.7 |
> | 82° | −38.6 |
> | 87° | −38.6 — saturated at the own-column floor clamp |
>
> The cell arithmetic closes exactly and independently: of E1M1's 920 "outdoor" cells,
> **348 are genuine open-sky air and every one is lit**, and the other **572 are void cells
> outside the map** whose BSP leaf happens to name an outdoor sector — correctly blocked.
> 348 + 572 = 920.
>
> ⚠ **That 572 is why the open-sky no-sun count reads high, and it is not a defect.** The
> seep's `.g` mask takes its sky flag from the leaf's sector (INV-12, unchanged by L2b), so a
> cell out past the map edge can read "open sky" while the clearance correctly calls it
> solid. **One lookup, two verdicts** — the amendment's own rule, visible in the statistics.
>
> #### One correction to Task L2b Step 2, found by this check
>
> The plan specified nudging the void-test sample **a quarter cell** (16 units at
> `cell = 64`) off the linedef before `P_PointOnLineSide`. That is too large by an order of
> magnitude: the subsector is resolved at the **un-nudged** point, so the test is only
> meaningful while the nudged point is still in that leaf — and **BSP leaves are far smaller
> than a cell**. A 16-unit nudge pushes any cell centre within 16 units of one of its own
> segs outside its leaf, where "behind a seg" means "in the next room", not "in the void".
> Measured on E1M1: **588 open-sky cells falsely solid at 16 units, 572 at 1 unit, 542 with
> no nudge at all**. The shipped value is **`RB_SUN_NUDGE = 1.0f` world unit** — enough to
> break the exactly-on-the-line tie the nudge exists for (DOOM geometry is 64-unit aligned
> and so is the cell, so those ties are systematic), small enough that it cannot leave a leaf
> the point was comfortably inside.
>
> #### Look identity (Step 9's instrument)
>
> `-shotverify` on the same spawn view, both builds, 3840×2160, against the project's 3.0
> fail bar — and against a **same-build noise floor of 0.003/255** (two runs of one build;
> DOOM-0287's `-noinput` made this far more deterministic than the 1.09 DOOM-0276 measured
> against):
>
> | scene | mean-abs-error |
> |---|---|
> | E1M1 | **0.066/255** |
> | E1M3 | 0.079/255 |
> | E1M6 | 0.051/255 |
>
> So the change is real — ~20× the floor, i.e. the field genuinely drives the shaft rather
> than the channel being dead, which is the failure the plan warns to look for — and it is
> very small on all three.
>
> **Read with its limit:** the E1M1 spawn is roofed, where the directional term is mostly off
> in *both* builds, so this view can only bound how much moved, not confirm the outdoor
> shafts. **The doorway beam and a building's shadow on open air remain a user play-test**,
> exactly as §7's L2b row says.

- **Baseline & method:** the DOOM-0181/0183 §6 protocol — average the `` \ ``
  profiler (`rb_profile`, DOOM-0090 — the **backslash** key; `` ` ``/`~` is the RT view
  cycle, verified `i_video.c:425` / `:433`) present-total (ms, not FPS) over a fixed ~10 s walk of the **E1M1
  green-goo room** (a sky-hole/doorway scene too, for shafts), RT-on, 50 % render
  scale, with `rb_fog` **off** then **on** (same-walk A/B, the DOOM-0187 lesson).
- **Cost shape (measure, don't assert):** the march is `kFogSteps` samples/pixel,
  each with **up to two** shadow rays (as written: the open-sky up-ray, plus the sun ray L2 adds — but see the 2026-07-27 note below, the up-ray is gone; and since L3 the emitter term is a baked buffer read, not an evaluation over emitters) at **half-res**
  (¼ the pixels) + denoise. The shadow rays are the pole; half-res + few steps +
  dither + denoise is what makes it affordable.
  **Confirmed and then acted on, 2026-07-27:** the rays *were* the pole — measured at
  **7.9 of the fog's 8.2 ms** — and the up-ray is now gone (DOOM-0276, §4.3a amendment).
  Post-swap the march carries **zero** rays per sample until L2 adds the sun ray, which
  will make it the pole in its turn. Budget L2 against the ≈0.8 ms the fog costs now, not
  against the 8.2 ms it used to.
  **And it did, exactly as predicted — 13.6 ms, see the 2026-07-30 notice above.** The
  lesson is now general enough to state as a rule for this feature: **a per-sample ray is
  never affordable in this march**, at any resolution or step count, and the two times one
  has been added it has been ~90 % of the fog's cost. After DOOM-0289 the march is back to
  **zero rays per sample**, and L3 (torch shafts, §4.4(b)) should be designed on that
  basis — Q2's "optional single occlusion ray" is not optional-but-affordable, it is the
  same mistake a third time.
- **A dedicated GPU-timer slot needs the pool grown.** The RT profiler pool is sized
  `queryCount = 8` (`r_vulkan.cpp:1520`) and the RT path **already writes all 8 indices
  0–7** (`vkCmdWriteTimestamp` at `:7331`, `:7358`, `:7500`, `:7559`, `:7579`, `:7592`,
  `:7639-7641`, `:7643`, `:7773` — note the code comment at `:1506` still says "5 used"
  but is itself stale). So a fog-pass timer takes **slot 8**, i.e. `queryCount = 9`, and **all three** sites move
  together: the pool size (`:1520`), **both** `vkCmdResetQueryPool` calls (`:7330` and
  `:8326`, each hardcoding `0, 8`), and the `vkGetQueryPoolResults` readback (`:8118`).
  A small, contained change made **with** the perf layer (L6), not silently skipped.
- **Levers held ready** (measure before cutting): the `rb_fog` **strength** dial is
  the standing perf option (though a lower strength is not automatically cheaper), plus
  reduce `kFogSteps`; reduce the grid's `kFogLightsPerCell` (there is no per-sample emitter
  occlusion ray to drop — occlusion is baked, §4.4(b)); distance-gate the march
  (`kFogMaxDist`); make mode 4 half-res too; and — the biggest new lever —
  **swap the per-sample open-sky up-ray (§4.3a) for the near-free per-surface
  `RB_MESH_OUTDOOR` flag**, trading the doorway cutoff for whole-view granularity.
  **Post-L1d this lever is no longer perf-only:** the seep branches on `openSky`, so
  coarsening it to whole-view granularity also degrades the graded seep back toward the
  abrupt room-boundary step the user asked to soften. Re-judge the look, not just the
  frame time.
  **Superseded 2026-07-27 by DOOM-0276 — a third path was taken and this lever is spent.**
  The up-ray is gone, but not in favour of the whole-view flag: the open-sky test moved onto
  the seep field's own grid as a second channel, which keeps the per-sample granularity the
  paragraph above says must not be lost and coarsens only to a 64-unit cell (§4.3a
  amendment). Do **not** now also apply `RB_MESH_OUTDOOR` — there is no ray left to remove,
  and it would cost exactly the look this warning protects.
- **2026-07-24 amendment — the up-ray is the march's FIRST ray, and L1b spot-checks it.**
  *(Historical. It was also the march's LAST ray: DOOM-0276 removed it on 2026-07-27 and the
  march now casts none until L2. The reasoning below is why the check was demanded, and it
  was vindicated — the ray did turn out to be the pole.)*
  L1's `marchFog` did **zero** ray-queries per
  sample — the loop is just `density × strength × flat-sky-ambient`. The open-sky up-ray
  (§4.3a) adds **the first** ray-query, one per march sample: a `0 → 1` ray/sample jump
  ×`kFogSteps`×half-res, so expect a **large, not incremental** cost step (a ray-query is
  typically the priciest op in a march loop). (Mode 4's full-res march pays
  proportionally more, but mode 4 isn't the FPS-gated play path; L2 later adds a *second*
  ray, the sun shaft.) Because it lands in **L1b** (§7), that layer carried its **own
  hardware perf spot-check** on the RX 6600, using the §6 A/B method: measure the up-ray's
  **added present-total** (fog-off vs fog-on, same walk) — the goo room's ~40 FPS baseline
  is a pre-existing megakernel/denoiser cost, so the check is the *added* Δ, plus a
  confirmation that a **typical non-goo corridor scene** holds the same added share with
  the up-ray on. **That share is L1b's own ≤ 4 % slice (§7), not the whole-feature ≤ 15 %**
  — an early layer measured against the cumulative gate can pass while leaving every later
  layer unbuildable, which is the defect this split exists to prevent. (This criterion was originally "still holds 60 FPS"; the 2026-07-25
  amendment below relaxed that floor for RT-engaged scenes, so the share is now the only
  currency.) The up-ray passed and shipped; the `RB_MESH_OUTDOOR` fallback was
  not built. **L1b's measured Δ is not recorded here — record it, since L1c's step-count
  raise is budgeted against it.** The **formal cumulative gate stays L6** (now ≤ 15 %, see below); L1b's check is a go/no-go on which exposure method
  ships.
- **2026-07-25 amendment — the gate rises from ≤ 5 % to ≤ 15 % present-total (user
  decision 2026-07-25).** The user's reasoning: SH2 ran this look on a PS2, so a modern
  PC should afford it. Right in spirit, but it does **not** transfer literally — SH2's
  fog was **flat 2-D planes composited over the frame**, while ours is a **true 3-D
  march inside a path tracer that is already GPU-bound** (~45 FPS Ultra RT). The
  headroom is whatever the tracer leaves, not a PS2-sized gulf. The user was told this
  and chose ~15 % anyway. What the extra budget buys, **in priority order**:
  1. `kFogSteps` `24 → ~40` (§4.2/§4.3b) — the one that matters; structured wisps are
     expected to band at 24 (confirm, don't assume).
  2. **Promoting mode-6 fog from half-res to full-res** *if* the wisps read soft or
     blocky. **These two levers are NOT independent and must not be costed as if they
     were:** item 1 is ×1.67 on the step count and item 2 is ×4 on the pixels, and both
     multiply the per-sample up-ray that §4.3a calls the march's dominant cost — ~6.7×
     together, against a gate that rose only 3× (5 % → 15 %), before L2 adds a *second*
     ray per sample. So item 2 is **contingent on item 1's measured cost**, not a
     free-standing option; if both cannot fit, full-res may be restricted to the High
     strength setting. This **dissolves** the L5 upsample problem rather than solving it (no
     upsample, no guide, no sky-seam bilinear fallback). **Measure half-res
     first** — if half-res with wisps looks right, keep the cheaper path and bank the
     budget (Q18).
  3. A third noise octave, only if two read too regular.
- **The ≤ 5 % figure is superseded** wherever it appears in this section and in §7's L6
  row. The **method is unchanged** — same-walk A/B on present-total via the `` \ ``
  profiler, fixed E1M1 goo-room walk, 50 % render scale, `rb_fog` off then on. Only the
  threshold moves.
- **Conflict between the two gates, surfaced and resolved.** "≤ 15 % of frame time" and
  "a 60 FPS scene still holds 60 FPS" **cannot both bind**: 15 % of a 16.7 ms frame is
  2.5 ms, landing that scene near 52 FPS. The user was shown the concrete trade
  (`~45 → ~39 FPS` in Ultra RT) when choosing the budget and accepted it, so **for
  RT-engaged scenes the percentage governs and the 60 FPS floor is relaxed
  accordingly**. The floor still binds where it always did: **Classic and the raster
  path are untouched and must not move at all** (INV-7).
- **Gate (L6, the pass/fail):** measured at **`rb_fog = 3` (High)** — the worst case, so
  the gate cannot be passed by a cheap default and then blown by the user turning the dial
  up; the shipped default (Q10) is recorded alongside but is not the bar. The march adds
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
DOOM-0181/0183). Look acceptance is **human play-test** throughout; **L1b and L1c each
carry a measured hardware spot-check** (a go/no-go on which variant ships — exposure
method at L1b, fog resolution at L1c). **L6** carries the formal cumulative pass/fail gate, and
**L2b** carries a gate of its own — it exists to undo a measured regression, so its number is
pass/fail rather than a go/no-go between variants.

| Layer | Scope | Verify | FPS-gate? |
|-------|-------|--------|-----------|
| **L1** *(shipped 84e8b35..e7753b3)* | The march skeleton: `marchFog` over `[0,tHit]`, constant base density, **isotropic single scatter from the sky only** (no direction yet — flat sky ambient), composited via a new half-res fog target + a **plain (un-guided) bilinear** upsample — the position-guided variant is L5, not this row — + the per-mode apply (§4.6: in-megakernel `toneEncode` for mode 4, `svgf_composite.comp:123`+sky-passthrough for mode 6). Full RGB, no colour profiles. | Air picks up a faint uniform glow; surfaces behind thick fog fade; sky still visible through fog; no NaNs; modes 4 & 6 match | no |
| **L1b** *(shipped 1345c92)* | **The fog-placement standard + the mountains** (2026-07-24 amendment). Two parts: **(i) sky-backdrop aerial fog** (§4.6a) — fog sky pixels over `[0,kFogMaxDist]`, folded on the sky-passthrough branch + mode-4 sky branch, reconciling the old `SKY_FOG_COL` band (Q14); **(ii) open-sky exposure gate** (§4.3a) — per-sample up-ray sky-visibility → `skyExposure` multiplier on density, with the `RB_MESH_OUTDOOR` flag path built in as the perf fallback. | Fog clears under a roof, mountains fade into haze — **full checklist below the table** | spot-check |
| **L1c** | **The Silent Hill 2 look** (§4.3b, 2026-07-25 amendment): near-white `kFogColor`, ~~base density ≈2×~~ (**tried and reverted 2026-07-30 — `kFogBaseDensity` stays at `0.0033`; the wisps carry the look at the lower average density**), ~~`kFogSteps` 24→~40~~ (**measured, falsified and reverted 2026-07-30 — ships at 24**), **the sky closed form's in-scatter tone `SKY_COLOR` → `kFogColor`** (§4.6a — omit this and the mountains haze cool blue against a near-white foreground, which is the seam this row's own acceptance criterion exists to catch), the sky term's effective density/distance (`kFogMaxDist` or a sky-specific twin — §4.3b), and the **two-octave drifting wisps** off a CPU-generated 3-D noise volume (new sampled image, §5), drift time reusing `misc6.x`. | Near-white colourless fog with slow drifting billows that sit correctly in depth — **full checklist below the table** | spot-check (≤ 8 % cumulative, §6 table) |
| **L1d** | **Outdoor-proximity seep** (§4.3a amendment, 2026-07-25): the load-time flood-filled distance field (new per-level 2-D texture, §5) + the graded indoor `skyExposure`. | Standing in a doorway onto a courtyard, **a little fog drifts in and thins as you walk deeper**; a **sealed** room that merely shares a wall with outdoors is **visually indistinguishable from the same room before L1d** — i.e. it shows the plain `kIndoorFogScale` floor and no seep (proves the fill is through-open-space, not straight-line); the outdoor look is **unchanged from L1c** (the seep touches only the indoor branch); level load adds **≤ 20 ms** on E1M1 (measure the flood fill directly; it runs once, beside the mesh build); **and the runtime seep tap adds ≤ 1 % present-total** on the §6 walk — INV-12's "single bilinear tap" is *per march sample*, inside the loop §6 calls the dominant cost, so it is not free merely because the fill is load-time | spot-check |
| **L1e** | **The floor fog** (§4.3c, DOOM-0272): the second, short-range density term — three new constants, a third addend in the march's `sigma`, and the matching second addend in the sky closed form (§4.6a). **Outdoor half only**; the indoor half rides on L1d's seep, so this row lands *before* L1c and L1d — the letters are identifiers, not a sequence. | **The camera is yaw-only — there is no looking down** (`camUp` is world +Z at `r_vulkan.cpp:7417`), so "mist at your feet" can only ever be judged from the **lower part of the view**, where the bottom row of the 3-D view is a 29° downward ray meeting the floor ~84 units ahead. Accept when: the near floor in the bottom third reads mistier than the same view before L1e, the far end of the courtyard is **no whiter than before** (that is the whole point of the range term), there is **no line along the skyline** where sky meets a distant wall, and indoors is unchanged from L1b | spot-check |
| **L2** *(shipped 544ae84 — the ray is superseded by L2b)* | **Sky shafts:** add `kSunDir` + the sky-visibility test per sample + HG phase. Shipped with an ambient/directional **split** (`kSkyAmbientFrac`), not the outright replacement §4.4(a) originally specified, and far over budget — the ray alone is **19× the rest of the fog** and about **4× the whole ≤ 15 % gate** (§6) — L2b replaces the per-sample ray with a load-time field. | A doorway/sky-hole open to sky throws a visible slanted beam; closed rooms stay clear; the beam moves correctly as the camera orbits | no — L2b carries the gate |
| **L2b** | **The sun-clearance field** (§4.4's 2026-07-30 amendment, DOOM-0289): delete L2's per-sample sun ray; widen the seep field `RG16F → RGBA16F` with the `zLo`/`zHi` interval; build it in `RB_BuildSeepField`; widen and split DOOM-0281's re-flood (and fix its upload gate). **A pure perf change with a look-identity requirement** — it is not a new layer. | The shafts are **where they were before** (the field and the beam must agree about `kSunDir`); the doorway beam still reads; a building in the open still shadows the air beside it; **the sun ray's 13.6 ms is gone** and the whole fog is back inside the ≤ 15 % gate; a door opening updates its shaft within Q30's rebuild cadence (same frame on the full re-flood path), and **a lift moving between two open heights updates it too** (the clearance is height-keyed, so DOOM-0281's flip detector alone is not enough); `-rtverify` green | **yes** — the regression it exists to fix |
| **L3** | **Height pooling + torch shafts:** height-based density (per-sample floor reference — `pc.fogFloorZ` outdoors, the camera's floor indoors, **never** `hitP.z`; §4.3 / Q3); a per-cell torch list baked onto the seep grid at level load (≤ `kFogLightsPerCell` lights, ranked by unoccluded contribution and sight-tested), read by one indexed lookup per march sample — **no runtime emitter scan** (§4.4(b)). | Fog settles low into a floor layer; a torch in a dark room glows its surrounding air; dynamic/muzzle/flashlight do **not** scatter | no |
| **L3b** | **Re-bake the fog-light grid when the map moves** (§4.4's 2026-08-02 amendment, DOOM-0296, INV-14): arm on `RB_UPD_MOVED` beside `g.clearanceDirty`; fire from inside `RecordSeepRefresh`, **after its clearance block and before the `!needUpload` early-out** (a settle frame early-outs, so a block at the end never runs), once planes have been still for `kFogLightSettle` — or after `kFogLightMaxWait` for a mover that never settles, which bakes without clearing the arm; call the existing `BuildFogLightGrid` unchanged. **A defect fix, not a new layer** — no constant of the look moves. | Open a door onto a torch-lit room and the fog behind it picks up the torch **within ~`kFogLightSettle` (0.15 s) of the door finishing, at traced-frame granularity**, not on reload; shut it and the glow goes; a **second** L3 line prints with `lit` risen and returning (the load-time line always prints, so the second one is the signal). A **lift** between two already-open heights must also produce a second line — **its existence is the check, not the direction of any count** — which is the only fixture that catches a trigger keyed to the opening flip. On a **crusher** map the cap path fires every `kFogLightMaxWait`: judge whether the repeating fog snap reads acceptably, and if not suppress re-bakes while a cap-path mover runs rather than shortening the cap. **Exactly one** second line per door — a build that arms but never waits prints of order eighty. A **perpetual platform** settles twice per cycle and so bakes twice per cycle indefinitely without ever touching the cap: judge that snap too, it is the commoner mover. A still map prints **no second** line and is unchanged under `-shotcompare` (an **MAE ≤ 3.0** gate at 640 px, not bit-identity) | no — **4.1 ms (E1M1) / 2.9 ms (MAP01) bake+upload per settled move**, measured, against a ≤ 6 ms gate (§4.4) |
| **L4** | **Area profiles + colour:** goo tint via the primary-hit `RB_FLAG_LIQUID_NUKAGE`; hell haze via the new `rb_view_t` field → `misc6.w`; `mediumTint` colouring (light×medium). | Goo rooms fill green and pool low; hell levels gain a faint red haze; a torch shaft reads warm-through-green in goo; clear levels stay neutral | no |
| **L5** | **Denoise/quality pass:** dither tuning; escalate upsample→a-trous if it crawls (§4.6 Q6); phase/anisotropy tune. **May be largely dissolved** if L1c promotes mode-6 fog to full-res (§6 item 2) — with no upsample there is no upsample to harden; the dither/phase tuning still applies. | Fog is smooth, not grainy or crawling, in a slow pan; shafts hold their shape | no |
| **L6** | **Menu + profiler + perf** (the dial, `rt_fog` config row and `;` key already shipped, `f8c6b1f` — §5): both menu rows, the fog-pass profiler wiring (**`queryCount` 8 → 9**, fog on slot 8, and widen both resets + the readback — the pool is full, §6), the DOOM-0208 canonical-config pin (§8 INV-8), and the perf pass. | Toggle/strength flip cleanly through all four states off→low→med→high (matching `fogNames[4]`, §5); adds **≤ 15 % present-total** vs off (§6, raised from ≤ 5 % by the 2026-07-25 amendment); `-rtverify` **green**; if fog ships on-by-default (Q10) the `-shotcompare` golden is re-blessed with subtle fog, else fog-off stays byte-identical (INV-8); Classic + the raster path unmoved (INV-7) — the 60 FPS floor no longer binds RT-engaged scenes (§6, 2026-07-25) | **yes** |

**Footnote — the L1b/L1c "spot-check" FPS-gates:** *not* the formal perf gate (that stays
L6-only, §6). Each is an internal **go/no-go on which variant ships**: at **L1b** the exposure method
(the per-sample up-ray if it holds ≤ 4 %, the `RB_MESH_OUTDOOR` fallback if it doesn't);
at **L1c** the fog resolution (half-res vs full-res) within that layer's ≤ 8 %
allocation.

**Interim state (expected, not a regression):** L1 (shipped, 84e8b35..e7753b3) was a flat
**uniform** sky-ambient glow — the user play-test flagged it as too-uniform and
mountain-less, which **L1b** fixed (open-sky gating + sky-backdrop fog, 1345c92;
user-confirmed 2026-07-25). Post-L1b the haze is still **cool-blue and uniform**
(**L1c** makes it near-white and wispy) and cuts off **hard** at a roofline (**L1d**
adds the seep); it has **no** shafts (the directional term arrives at L2) and **no**
colour profiles (L4) — mirroring DOOM-0183's "sheen-before-ripple" staged interim.

### Acceptance detail — L1b and L1c

The other layers' Verify cells fit in a line. These two do not, so they live here.

**L1b — accept when:**

- Open, sky-exposed rooms stay hazy; step under a roof and the air **clears, with a mist wall
  at the threshold**.
- Distant **mountains fade into haze**, not crisp; the sky is still recognisable.
- Fog-off is byte-identical (INV-7/8).
- **Hardware perf spot-check** (§6): the up-ray's own added Δ fits **≤ 4 % present-total** —
  its slice of L1c's cumulative 8 %, *not* the whole-feature 15 % gate, which an early layer
  could pass while leaving every later layer unbuildable. Over 4 %, ship the
  `RB_MESH_OUTDOOR` fallback instead.
- **Record the measured Δ.** L1c's own allowance is `8 % − Δ(L1b)`, so this number is not
  optional bookkeeping — the next layer cannot be gated without it.

**L1c — accept when:**

- Fog reads **near-white and colourless**, not blue.
- **Outdoors at High, it is not too bright.** The user's 2026-07-26 verdict on the shipped fog was
  "slightly darker" (§4.3b) — and this layer pushes brightness *up*, so this is an acceptance
  item, not polish. Tune `kSkyShaftStrength`, or the value of `kFogColor`; never `kFogBaseDensity`.
- **Billows of visibly differing thickness drift slowly past**, and they sit correctly **in
  depth** — passing in front of *and* behind pillars and monsters as the camera turns.
- No banding at wisp boundaries; no crawl or strobe in a slow pan.
- With `kFogColor`, `kFogBaseDensity` and `kFogSteps` temporarily held at their L1b values,
  `kWispAmp = 0` is byte-identical to L1b — i.e. the wisp term alone is a no-op (INV-11).
- Fog-off is byte-identical (INV-8).
- **Distant sky is still readable at High strength** (the sky closed form shares
  `kFogBaseDensity` — §4.3b).
- **No visible discontinuity at the sky/wall seam** between wisped foreground and un-wisped
  sky (INV-10).
- **The mountains haze the same near-white as the foreground** — the proof that the
  `SKY_COLOR` → `kFogColor` swap landed.
- **Gate:** cumulative fog-off→fog-on Δ **≤ 8 %** present-total (§6 method and table). L5
  carries no gate of its own, so an upsample→a-trous escalation would otherwise go
  unbudgeted until L6 — which is why the reservation for L2–L5 is a floor, not a leftover.
- **Record L1b's Δ first.** L1c's own increment is `8 % − Δ(L1b)`; if L1b already exceeds
  8 %, the split is re-cut rather than L1c failing by construction.
- **This layer also decides half-res vs full-res.** Measure half-res **first**; promote mode-6
  fog to full-res only if the wisps read soft *and* the promotion still fits the 8 %
  allocation (§6, Q18).

## 8. Invariants

- **INV-1:** Fog is **single-scattering** along the **primary view ray** only —
  one in-scatter event per march sample, no secondary bounces, no path extension.
  It is composited as `surface·transmittance + inscatter` after the primary hit.
  *Falsifiable by diff:* `marchFog` contains no `rayQueryProceed` loop that spawns a
  secondary bounce, and no call site extends a path.
  **Clarified 2026-08-02 (DOOM-0295) — this invariant is UNCHANGED, and the note exists
  only to stop a misreading.** L3 now evaluates its torch **radiance** once per pair of
  march samples (§4.4(b)), which looks at a glance like it halves the in-scatter rate.
  It does not: every sample still performs exactly one in-scatter event
  (`inscatter += trans · sigma · Ls · dt`, once per iteration). What is evaluated at
  half rate is one **addend of `Ls`** — how finely that addend's radiance is sampled
  along the ray, which this invariant has never constrained and which is a quadrature
  choice. The *Falsifiable by diff* clause above is untouched. It does not, however, test
  the sub-claim this clarification rests on, so one more falsifier joins it: *`marchFog`'s
  loop body contains exactly one `inscatter +=`.* That is what "one in-scatter event per
  march sample" means operationally, and it is checkable by grep.
- **INV-2:** Fog scatters light from **sky + static emitters `[0, omniStart)`
  only** (`omniStart = pc.misc4.y`). Dynamic sprite lights `[omniStart, emitCount)`,
  the muzzle flash (`misc2.z`), and the flashlight (`misc2.w`) **never** scatter.
  *Falsifiable:* L3's own acceptance row — "dynamic/muzzle/flashlight do **not** scatter"
  (§7) — plus the **bake's** input slice read by diff: `BuildFogLightGrid` clusters the
  static emitter set only, and the march's own loop bound is `kFogLightsPerCell`, not any
  emitter count. (The pre-L3 falsifier named a `k < omniStart` march loop; L3 deleted it.)
- **INV-3:** The sun direction stays a **compile-time `const`** in v1 — no runtime
  sun-direction control is added. (`kSunDir` is already declared but unread at
  `pt_common.glsl:42`; L2 wires it in. It is the engine's *first* directional light —
  before it there was only the positional sky backdrop + constant `SKY_COLOR`.)
  *Falsifiable:* no push lane, uniform or descriptor carries a sun direction — INV-5's
  240 B `static_assert` still holds and no new UBO field appears. Per-level control is
  deferred (Q1).
  **Amended 2026-07-30 (DOOM-0289) — this stops being a v1 simplification and becomes
  structural.** The sun-clearance field (INV-13) is baked at level load *from* `kSunDir`,
  so a runtime sun direction would invalidate the field rather than merely re-aiming a
  ray: it would need a rebuild per direction. The user confirmed the same day that DOOM 1
  and 2 have no day/night cycle and none is wanted (Q27), so the constraint is accepted
  rather than worked around. **A second falsifiable clause comes with it:** `kSunDir` is
  now mirrored C-side (`RB_SUN_DIR_*`, `r_mesh.h`), and the two copies must agree — if
  they drift, the beam and its shadows point in different directions, which is a look
  defect, not a build error.
- **INV-4:** Fog is a **separate channel composited *after* the SVGF albedo
  re-multiply** (`svgf_composite.comp:123`) — it never rides `gillum`/`illum` (which
  is multiplied by surface albedo). `inscatter`/`transmittance` are **linear
  radiance**, folded in **before** the tonemap on **both** the surface path and the
  sky-passthrough branch (`:93-107`) in the *same* colour space, so the sky/wall seam
  matches (§4.6). *Falsifiable by diff:* the fold sits **after** the albedo re-multiply, and
  `inscatter`/`transmittance` appear in no `gillum`/`illum` expression.
- **INV-5:** The two runtime values ride **`misc6.z` (fog strength) + `misc6.w`
  (haze density)** — the **last two free components** of the 240-byte
  `RtPushConstants`. This feature adds **no** struct growth and does **not** append
  `misc7`; the C++ struct, its `static_assert` (`r_vulkan.cpp:7405`), `pcr.size`
  (`:2368`), and the GLSL push block stay at 240 B. **But the gate is mirrored, not
  shared:** `svgf_composite.comp` has its own 120-byte `SvgfPC` and no `misc6`, so L1
  copies `rb_fog` into its free `misc3.y` (`r_vulkan.cpp:7589`; read at
  `svgf_composite.comp:100`/`:128`). An implementer who plumbs only `misc6.z` gets a
  megakernel that marches and a composite that never folds. *Falsifiable:* the 240-byte
  `static_assert` still compiles, `pcr.size` is unchanged, and `rb_fog` is greppable at **both**
  `misc6.z` and `SvgfPC.misc3.y`.
- **INV-6:** The GI bake (`bake.comp`) is **untouched** — fog is a view-ray term and
  never enters the bake (which computes surface irradiance). No double-count.
  *Falsifiable by diff:* `bake.comp` is unchanged, and the fog tap lives in `marchFog`
  (`pathtrace.comp`) rather than in the `pt_common.glsl` that `bake.comp` `#include`s
  (§4.3b) — so the bake never even declares the noise volume.
- **INV-7:** Ultra **and** Solid, **RT engaged only** (`rb_rtdebug` ∈ {4, 6}).
  Classic and the raster path (RT off) are **byte-identical by construction** — fog
  lives only in the RT megakernel, so no raster/Classic code is touched. (There is no
  golden test for *this* claim: `-shotcompare` renders the Ultra-RT view only and
  cannot exercise the raster path — that gate is INV-8's, below. The claim is instead
  falsifiable **by diff**: no raster or Classic source file is touched.) The fog lanes sit
  beyond the 184-byte `-rtverify` prefix (`r_vulkan.cpp:6858`), so **`-rtverify` is
  unaffected**; the headless verify mode (5), the debug views (1–3), and RT-off (0)
  are untouched.
- **INV-8:** Every fog cost is **`rb_fog`-gated** — `rb_fog == 0` skips the march
  entirely (the branch is not taken), so the RT path with fog off is byte-identical to
  today **by construction** (like INV-7 — no golden needed). Two *distinct*
  `-shotcompare` roles, not to be conflated: **(a)** the DOOM-0208 canonical config
  pins effect toggles to their **shipped defaults** (`rb_detile=2, rb_filth=1,
  rb_wet=1`, `r_vulkan.cpp:8212`), so when fog ships it pins `rb_fog` to *its*
  shipped default (§5) and the golden is **re-blessed *with* subtle fog** — the gate
  then guards the fog *look* (exactly how DOOM-0183 re-blessed for wet). **(b)** The
  fog-*off* byte-identity is structural; if an empirical check is wanted, a temporary
  `-config` forcing `rb_fog=0` vs the pre-feature golden proves it — that is *not* the
  canonical run.

- **INV-9 (open-sky standard, 2026-07-24):** fog density is gated by **open-sky
  exposure** — see §4.3b for the single authoritative form of `σ_final`,
  with `skyExposure = 1` under open sky and `kIndoorFogScale` (`const`) under a solid
  roof. **`skyExposure` gates the sky-sourced haze ONLY, never `areaMult`** — otherwise
  goo/hell/torch-lit interiors (all roofed) would lose their fog entirely (§4.3a).
  v1 measures exposure **per march sample** via one up-ray — **MISS = open sky, solid-
  geometry hit = indoor** (the mask mechanism is derived once in §4.3a; **superseded
  2026-07-27 by DOOM-0276** — same per-sample granularity, but read from the seep
  field's mask channel instead of traced). It is the user's
  "true volumetric" pick; the per-surface `RB_MESH_OUTDOOR` flag was the cheap fallback; L1b shipped the up-ray,
  so the flag stays unbuilt as a standing perf lever. "Open sky" = `ceilingpic ==
  skyflatnum`, the engine's own open-air signal. *Falsifiable:* L1b's acceptance row —
  roofed rooms clear, a mist wall at the threshold (§7) — and, for the split, L4's
  "E3M1 shows haze" check, which fails by construction if `skyExposure` ever multiplies
  `areaMult`. **Amended 2026-07-25:** the open-sky
  branch is still exactly `1`, but the **indoor** branch is no longer the flat
  `kIndoorFogScale` — it is `mix(kIndoorFogScale, kSeepMax, exp(-d/kSeepFalloff))`,
  where `d` is the **through-open-space** distance to outdoor air (§4.3a amendment).
  **Amended 2026-07-27 (§4.3c, DOOM-0272):** `σ` gains a **third** addend, the floor fog, and it
  sits on the **gated** side — `(skySigma + floorSigma) · skyExposure + areaSigma`. That is what
  makes "thicker outside" free, and it is why the split must not be collapsed: fold the floor term
  into `areaSigma` and it stops clearing under a roof; fold it into `skySigma` and it inherits the
  aerial layer's `poolH`, which is the one thing it must not share. *Falsifiable:* a roofed room
  with no seep shows the floor fog at exactly `kIndoorFogScale` of its outdoor strength — i.e. the
  same faint haze as before L1e, not a bank at your feet.
  **Amended 2026-07-27 (DOOM-0276) — the mechanism, not the values.** `openSky` is now
  `texture(uSeepField, uv).g > 0.5`, a second channel on the field INV-12 already builds,
  written `1` where the cell's sector has `ceilingpic == skyflatnum` and `0` otherwise.
  Both branch values are untouched, so a frame's fog changes only where the grid disagrees
  with the wall — within half a cell of a roofline. *Falsifiable:* the mask must be its own
  channel, never an epsilon on `d`: a roofed cell one step inside a doorway also carries a
  near-zero `d`, so `d < ε` would report the room behind a door as open sky and put the full
  outdoor bank inside it.
- **INV-10 (sky-backdrop fog, 2026-07-24):** sky pixels receive **aerial-perspective
  fog** (`skyExposure = 1`) along the ray's own slant path through the layer
  (`skyFogOpticalDepth`, §4.6a), folded as `sky · transmittance +
  inscatter` on the mode-6 sky-passthrough branch (`svgf_composite.comp:93-107`) and the
  mode-4 sky branch (`pathtrace.comp:1326-1337`), in the **same linear space** as every other
  fog fold (INV-4). Fog-off (`rb_fog == 0`) → `transmittance = 1, inscatter = 0`, so the
  sky is **byte-identical** to today (INV-7/INV-8). No up-ray and no new resource
  (INV-5) — the sky is outdoors by definition, and `pc.fogFloorZ` is a push-constant, not a
  resource. **Amended 2026-07-25, re-amended twice on 2026-07-27:** the closed form omits `wisp`,
  and its optical depth is `kFogBaseDensity · exp(−h₀/kFogPoolHeight) · fogStrengthScale · path`,
  where `h₀ = max(0, ro.z − pc.fogFloorZ)` and `1/path = rd.z/kFogPoolHeight + 1/kFogSkyDist` —
  the exact integral through an exponential layer, softly saturated at the horizon, so it is still
  a closed form with no loop but the haze varies with the sky pixel's **elevation** and has no
  plateau. **Amended again 2026-07-27 (§4.3c, DOOM-0272):** the optical depth is now a **sum of
  two** closed forms, the aerial one above plus the floor layer's (§4.3c) — omit the second and the
  skyline gains a ~37 % step against the walls below it, which is the very seam this invariant
  exists to prevent. The floor addend inherits every exclusion listed here: no `wisp`, no up-ray,
  `skyExposure = 1`, `fogStrengthScale` applied once to the sum. A hard `min()` here is a defect, not a clamp (§4.6a). That is what lets a mountain rise out of the
  mist; a fixed distance never could. The wisp exclusion is
  **deliberate** (`wisp ≡ 1` for sky pixels) — a closed form requires constant density, and
  billow structure on the mountains would be sub-pixel anyway. The consequence to watch is the
  **sky/wall seam** between wisped foreground and un-wisped sky, which is an explicit L1c
  acceptance item (§7). *Falsifiable:* that acceptance row — "no visible discontinuity at the
  sky/wall seam" — plus, by diff, the sky branch never samples the noise volume and never reads
  `FogHit` at all (it takes only `ro`, `rd` and the strength).
- **INV-11 (wisps, 2026-07-25):** density is modulated by **two octaves of drifting 3-D
  value noise** read from a single CPU-generated noise volume —
  `wisp` multiplies the whole medium (so goo and hell billow too). **`kWispAmp = 0` is an
  exact no-op** — from the multiplicative form, not from mean-1. Mean-1 does **not**
  preserve the look at non-zero amplitude: transmittance is non-linear in `σ`, so by
  Jensen wisped fog reads *thinner* on average; base density is therefore re-tuned with
  wisps **on** (§4.3b). Sky pixels are excluded (INV-10). Drift time is **`misc6.x`** (DOOM-0183's
  ripple lane), so **no new push lane** is added (INV-5 holds). The noise volume is
  **generated at startup, never shipped as an asset**. *Falsifiable:* L1c's own acceptance row —
  with `kFogColor`/`kFogBaseDensity`/`kFogSteps` held at their L1b values, `kWispAmp = 0` renders
  byte-identical to L1b (§7) — and, by diff, no noise file enters the repo or a WAD.
- **INV-12 (seep field, 2026-07-25):** the outdoor-distance field is flood-filled
  **through connected open space only**, never straight-line — so fog can **never**
  reach a sealed room that merely shares a wall with an outdoor area — such a room keeps
  the plain indoor floor `kIndoorFogScale`, no seep on top (it is *not* fog-free; the
  floor is nonzero by design, §4.3a). Connectivity is
  an **opening** test (two-sided **and** `min(ceilings) > max(floors)`), **not** a
  one-sidedness test: a closed DOOM door is a two-sided linedef, so the weaker test would
  leak fog through every shut door (§4.3a). **Two further conditions are part of this guarantee,
  not incidental detail (§4.3a):** a portal whose `frontsector == backsector` is **excluded** (the
  self-referencing-sector trick is two-sided with a full opening but draws solid — the same leak in
  another costume), and the grid is **padded by one cell of void beyond the map's XY bounding box**
  (without it, an outdoor sector flush to that box would `CLAMP_TO_EDGE` its `d = 0` out past the
  map edge — the exact opposite of the guarantee). Drop either and INV-12 is false. It is rebuilt
  **per level**, read with a **single bilinear tap**, and adds **no rays** to the march.
  A 2-D field is **sufficient** (not exact) because vanilla DOOM has no room-over-room;
  `d` is grid-quantised and height-invariant (§4.3a, Q19). *Falsifiable:* L1d's own
  acceptance row — a sealed room sharing a wall with outdoors must be visually
  indistinguishable from its pre-L1d self (§7).
  **Amended 2026-07-30 (DOOM-0289):** the field is now `RGBA16F` and carries INV-13's
  clearance interval on `.b`/`.a`. Nothing in this invariant changes — the flood, the
  opening test, the self-referencing-sector exclusion and the padding ring all govern
  `.r`/`.g` exactly as before. The one thing to keep straight is that the two halves have
  **different tolerances for a wrong sector at a cell centre**: the seep decides
  connectivity on the portal graph *before* rasterising and so does not care, while the
  clearance march reads per-cell heights directly and therefore needs INV-13's void test.
- **INV-13 (sun clearance, 2026-07-30):** sun visibility in the march is read from the
  **baked clearance interval** on the seep field's `.b`/`.a` channels — built at
  level load and rebuilt whenever a plane moves (this section's re-flood and clearance
  blocks; the word was "load-time" until DOOM-0289 made the rebuild part of the contract) —
  `sunSeen = (p.z >= tap.b) && (p.z <= tap.a || openSky)` — and **never traced**. The
  `|| openSky` term is part of the formula, not a caveat on it; the fourth clause below
  is its rationale. The march casts **zero rays per
  sample**, as it has since DOOM-0276. The interval, not a threshold, is the whole
  content of this invariant: in roofed air rising both clears the wall ahead and meets the
  ceiling above, so a single lower bound is false there (§4.4's amendment derives it), and
  roofed air just inside a doorway is where the shafts are. Three further clauses are part
  of the guarantee, not detail: the stored interval is **clamped to the cell's own air
  column** (which bounds the dynamic range the finite `RB_SUN_NEVER` sentinel is sized
  against — an unbounded `zHi` blends a shadowed cell back into light); a cell whose
  centre lies in the **void or in solid** blocks the march (`R_PointInSubsector` answers
  for a point inside a wall with a *neighbouring* room's sector, so without the
  back-of-a-seg test an outdoor building casts no shadow), while leaving the seep's own
  sector lookup untouched; and the march **does not stop at the first sky cell** (an
  escape threshold usually falls with distance, so stopping early reports an open
  courtyard as unlit below head height — but a later sky sector with a higher ceiling can
  raise it, which is why "first" must mean **first with a non-empty window** and both hull
  updates must be guarded).
  A fourth clause is the shader's, not the builder's: because the clamp bounds `zHi` by
  the cell's own ceiling, an **open-sky** cell needs `p.z <= zHi || openSky` or every
  sample above a courtyard's sky plane fails the test and a horizontal seam appears across
  outdoor fog. Drop any of the four and INV-13 is
  false. *Falsifiable:* L2b's own acceptance row — the shafts land **where they did before**
  the field replaced the ray (§7) — plus, by diff, `sunRayMissesGeometry` and its
  `rayQueryEXT` are **gone from `pathtrace.comp`**, not merely unreferenced.
- **INV-14 (fog-light currency, 2026-08-02):** the fog-light grid describes the map **as it
  stands**, not as it spawned. A sector plane that moves invalidates it, and the grid is
  re-baked from the **live** map — `P_CheckSightTrace` against current
  `opentop`/`openbottom`, over a cell cache refreshed for that movement. Four clauses are
  part of the guarantee rather than detail (§4.4's DOOM-0296 amendment derives each):
  - the trigger is **`RB_UPD_MOVED`, not an `openrange` flip** — a lift that rises in front
    of a torch changes visibility without flipping anything, and the stale grid then lights
    air **through** it, the one error direction a *door* can never produce;
  - the bake runs when planes **settle**, not per moving frame — a 1.8 s door is of order
    eighty traced frames, and re-baking each is ~290 ms of CPU for seventy-nine grids
    nobody sees;
  - a mover that never settles (a crusher, not a `perpetualRaise` platform, which waits 3 s
    at each end) is bounded by `kFogLightMaxWait`, or the feature is silently dead on that
    map;
  - the bake reads the cell cache **after whichever refresh path ran that frame** — they
    are mutually exclusive (`!didReflood`): a flip swaps `g.seepField` whole, a no-flip
    move is refreshed in place by `RB_RefreshSunClearance`.

  The result is **snapped**, not eased — §4.4's clearance section's line, for its reason.

  *Falsifiable, for three of the four clauses, from the bake's own printed line.* The line
  reports `%d air / %d with a candidate / %d lit of %u cells`, and the level-load bake
  prints one whenever RT is on and a seep field exists, so the signal is always a **second**
  line:
  - **Did it re-bake at all?** Open a door onto a torch-lit room: a second line must print,
    with `lit` risen; shutting it must return the count. (The shape DOOM-0281 proved its own
    re-flood with — 835 → 761 → 835 sealed cells, Q22's closing note.)
  - **Clause 2 — settle, not per moving frame.** **Exactly one** second line per door. A
    build that arms correctly and omits the timer re-bakes every moving frame and prints of
    order eighty, each with `lit` risen — so it passes the check above and fails only this
    one. It is the clause with the stated cost, and without this line it has no falsifier.
  - **Clause 1 — is the trigger movement and not the flip?** Move a plane that flips
    nothing — a lift between two already-open heights: a second line must print. **The
    signal is the line's existence, not the direction of any count.** A flip-keyed build
    never arms, so it emits nothing at all; and `lit` can move either way here, since the
    bake samples at `fz + RB_FOG_LIGHT_TESTZ` (clamped to mid-column by
    `if (tz > cz - 4.0f)`) and a rising floor lifts the sample as well as the occluder.
  - **Clause 3 — the cap.** Run a crusher: second lines must keep arriving, one per
    `kFogLightMaxWait`, for as long as it runs. A build without the cap prints one and then
    nothing.

  ⚠ **The fourth clause — that the cache was refreshed — is NOT falsifiable from this line,
  and no fixture here should claim to be.** `P_CheckSightTrace` reads live geometry whatever
  the cache says, so a bake over a stale cache still moves `lit`; and `air` cannot be relied
  on either, because it turns on whether a 64-unit cell centre happens to fall inside the
  door sector, which a 16-unit vanilla door need not provide. It is held instead by
  **construction** (the placement §4.4 derives) and checked once, at build time, by the
  temporary instrument in the plan's Step 4 — a printed `fz` for one known cell compared
  against its live sector. Recorded as a gap in the runtime falsifier rather than papered
  over with a fixture that passes on the broken build.

## 9. Alternatives considered

**Also rejected, but argued where they arose:** §4.3a weighs the two other ways of grading
fog by outdoor-ness — per-sample sky-visibility rays, and the per-room `RB_MESH_OUTDOOR`
flag — and keeps the flag path only as L1b's perf fallback. This section indexes them; the
reasoning stays there.

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
- **Keeping L2's sun ray and paying for it elsewhere** (fewer `kFogSteps`, quarter-res
  fog, a shorter `kFogMaxDist` for the ray, or shipping `rb_fog = 0` by default).
  Rejected 2026-07-30: the ray is **13.6 ms against the fog's other 0.7 ms** (§6), so
  every one of these buys back a fraction of the cost by spending the look — fewer steps
  bands the wisps (Q26 measured that), quarter-res softens exactly the beam edge the
  feature exists to draw, and off-by-default deletes the feature rather than affording it.
  The field costs a channel on an image the march already samples, and the user chose it
  over a stopgap. **The general form of this rejection is in §6's cost-shape bullet: a
  per-sample ray is not affordable in this march at any setting.**

## 10. Open questions

- **Q1 (sun direction):** a single world-space `const` for all levels (v1), or
  per-level/per-sky tuning? Start `const`; the shaft angle is a look-tune (L2).
- **Q2 (torch-shaft cost) — CLOSED 2026-08-03, and the answer refuses the trade.** The
  question offered nearest-few with **no** occlusion ray (cheap, glows through thin walls)
  against one ray each (correct, costlier). L3 took neither: the sight test moved to the
  **level-load bake**, where a real `P_CheckSightTrace` per cell buys the correct answer at
  zero per-sample cost, so the term is occluded *and* cheaper than the cheap option
  (§4.4(b)). Worth stating rather than just ticking, because the trade looked forced and
  was not: it was only forced while the question was being asked per sample.
- **Q3 (density source + floor reference):** primary-hit-keyed goo density (v1, cheap,
  blind to goo behind/around corners) vs a per-sector fog buffer (correct, new plumbing).
  **CLOSED 2026-07-27 for the height-pooling floor reference:** it is *not* per pixel and not
  from the primary hit — that was the pass-3 defect (§4.3). Outdoor air references the per-level
  `pc.fogFloorZ`; roofed air references the camera's floor. Both are per-frame constants, so
  density is a function of the sample position alone. Revisit only if a map's interiors need a
  finer indoor reference than "the floor the player is standing on".
- **Q4 — CLOSED for mode 4 (§4.6):** mode 4 (display) marches **full-res**, matching the
  shipped code (mode 4 has no even/even gate). Revisit only if its full-res march proves
  too costly. **Mode 6's resolution is *not* settled here — it is Q18's**, re-opened at
  L1c; mode 6 marches half-res today.
- **Q5 (phase):** Henyey–Greenstein forward-bias vs isotropic — a shaft-shape tune
  (L2/L5).
- **Q6 (denoise path):** position-guided bilateral upsample (cheap, self-contained) vs
  routing the fog channel through the a-trous passes (`r_vulkan.cpp:7564`). Start
  bilateral;
  escalate if the fog crawls (L5).
- **Q7 (hell-haze tuning):** the v1 hell rule (§4.5: Inferno E≥3 / DOOM-II map≥20 /
  fire-sky) is a concrete default so L4 is testable; the exact map thresholds, haze
  density, and `kHellTint` are tuned on hardware at L4.
- **Q8 (tonemap headroom):** bright sky shafts must read strong without clipping to
  a flat white slab under the PBR-Neutral tonemap — verify at L2/L5 (same caution as
  DOOM-0183 Q7).
- **Q9 (sky fog encode point):** *L1b shipped this fold; L1c must re-check it once the
  foreground is wisped and the sky is not (INV-10).* The sky-passthrough branch stores a display-encoded
  fullbright sky (`svgf_composite.comp:93-107`); folding fog in linear (§4.6) means
  treating it as linear, compositing, then re-clamp/encode. **L1 (shipped, 84e8b35..e7753b3)**
  wired this fold with `rb_fog`-gated `fetchFogBilinearPlain`; the round-trip is a **no-op by
  construction** for an un-fogged pixel (`rb_fog==0` → `fog.a=1, fog.rgb=0` →
  `sky·1+0`), so the fog-off sky stays byte-identical (INV-7/INV-8). L1b re-verified this held once the sky-distance fog (§4.6a) began writing real values for
  sky pixels.
- **Q10 (fog on/off by default):** ship `rb_fog=1` (subtle "Low" on, matching the
  on-by-default effect siblings — the spec's current pick) vs `rb_fog=0` (off, user
  opts in). On-by-default means the DOOM-0208 golden is re-blessed *with* fog
  (§8 INV-8) — a review decision (§5).
- **Q11 (coloured absorption):** v1 uses **scalar** transmittance (§4.1) so the fog
  target stays one `RGBA16F`; per-channel transmittance (green goo darkening the
  red/blue *behind* it, not just adding green inscatter) needs a wider target —
  deferred; revisit if neutral dimming reads wrong.
- **Q12 (indoor floor, 2026-07-24):** `kIndoorFogScale = 0` (roofed air totally clear)
  is **struck** (§4.3a — L3's torch shafts need something to light). The live question is
  *how* small the nonzero value should be; shipped `0.05`, re-judge at **L1d** once the
  seep changes what the indoor floor means.
- **Q13 — CLOSED (exposure method, 2026-07-24; decided at L1b, 1345c92):** per-sample up-ray (true volumetric, the user's
  pick, fills volume + doorway cutoff) vs per-surface `RB_MESH_OUTDOOR` flag (near-free,
  whole-view granularity). Decided by L1b's hardware perf spot-check (§6) — up-ray if it
  stays inside **L1b's own ≤ 4 % slice** (§7), flag otherwise. **Decided:
  the up-ray shipped in L1b (1345c92)** — this question is closed; the fallback remains
  documented as a standing perf lever.
- **Q14 (double-haze, 2026-07-24) — CLOSED 2026-07-27: the band is OFF whenever fog is on.**
  `skyPanorama`'s `SKY_FOG_COL` band is a **screen-space** wash pinned to the frame's vertical
  midpoint. It knows nothing about the world, so it cannot agree with real fog at any setting —
  L1b's `fog *= 0.5` only halved the mismatch, and it painted a grey ramp starting on the same
  screen row no matter where the camera stood. Shipped: `if (pc.misc6[2] != 0u) fog = 0.0;`.
  The band existed to mask DOOM-0143's below-horizon row-clamp seam; real aerial fog now sits at
  ~98 % right at the horizon, which hides that seam far better. **Fog OFF is untouched**, so
  DOOM-0143's protection is intact there (INV-8). L1c's near-white base cannot re-open this — it
  changes the fog's colour, and the band no longer runs. Contributed to the "hard cut off line"
  report alongside the §4.6a hard clamp; both were fixed in the same pass.
- **Q15 (up-ray direction, 2026-07-24):** straight world `+Z` up (simplest, chosen) vs
  a small cone / toward `kSunDir`. Straight up can misclassify a roofed room with a
  tiny sky-hole directly overhead as "outdoors"; L1b shipped straight-up and nothing
  reads wrong so far; revisit at **L1d**, where the seep makes near-threshold air more
  visible and so more sensitive to a misclassification.
- **Q16 (seep reach, 2026-07-25):** `kSeepMax ≈ 0.5` and `kSeepFalloff ≈ 192` units are
  first guesses at "a little bit of fog comes in". Too far/strong and the interior
  becomes a second outdoors (defeating the L1b standard the user liked); too short and
  the seep is invisible. Look-tune at **L1d**.
- **Q17 (pooling vs wisps, 2026-07-25):** SH2 fog is vertically uniform, but L3 adds
  height pooling. Judge `kFogPoolHeight` **with** the wisps present — a strong pool plus
  strong wisps may read as soup. **L3**, after L1c.
- **Q18 (fog resolution, 2026-07-25):** keep mode-6 fog **half-res** with the wisps
  (cheaper, and L5's position-guided upsample must then hold up against structured
  density), or promote to **full-res** (dissolves the upsample problem entirely, but is
  **contingent on L1c's measured step-count cost** — §6 item 2, they are not independent
  levers)? **Measure half-res first at L1c** (§6).
- **Q19 (distance-field cell size, 2026-07-25):** `64`-unit cells match DOOM's flat grid
  and keep the texture tiny, but a doorway is often only ~64–128 units wide, so a coarse
  cell may smear the opening's edge. Finer cells cost load time + memory. Judge at
  **L1d**; bilinear filtering may make 64 sufficient.
- **Q20 (tints against a near-white base, 2026-07-25):** `kHellTint` / `kGooTint` were
  picked against a cool blue-grey fog (§4.5). Against §4.3b's near-white base they will
  read differently — likely washed-out. Re-tune at **L4**, after L1c ships, per the
  user's "colour only where it makes sense" steer.
- **Q21 (wisp scale + tiling, 2026-07-25):** `kWispFreq1 = 1/512` (one noise **texel** ≈ 512 DOOM units — the
  billow feature scale; the *binding* tiling period is octave 2's 13107 units, §4.3b) is a first guess at "billows read at room scale". Too large and the fog looks
  uniform again; too small and it reads as fog-coloured static. With a `64³` volume on
  `REPEAT` the tiling period is 13107 units (octave 2, the finer and therefore binding
  one) — longer than any vanilla map's sightline — but confirm no repetition is visible in
  a long outdoor sightline. **Q21 also owns `kWispAmp` and `kWispWeight2`**, whose starting
  values are first guesses judged by the same look call. Look-tune at **L1c**.
- **Q22 (doors and the seep field, 2026-07-25):** the distance field is built once at
  level load from **spawn-state** door heights, so a door that opens during play does not
  re-flood the field — a courtyard-facing room stays as clear as it was when shut.
  Rebuilding per door-move is far too costly — the fill is budgeted at ≤ 20 ms per level
  load (§7, L1d), which is invisible once but ruinous on every door in play. Treating door-sector linedefs as *open* at
  build time is **not** an available option — it is exactly the leak INV-12 forbids, so
  choosing it would require amending the invariant. v1 takes spawn-state; judge at
  **L1d** whether the difference is even noticeable in play.
  **ANSWERED 2026-07-27 by the L1d play-test this question asked for: it IS noticeable.** The user
  photographed a normally-closed E1M1 wall standing open onto the courtyard — *"the fog doesn't
  roll in though"*. Follow-up is **DOOM-0281**.
  **And the rejection above was wrong on its own terms.** It reasoned from the *budget* (≤ 20 ms
  per level load) rather than from a measurement: the fill actually costs **0.6 ms** on E1M1, 33×
  under that budget and a fortieth of a frame, so "ruinous on every door in play" does not survive
  contact with the number. The real obstacle is the **GPU re-upload** — `UploadSeepField` destroys
  and recreates the image, which is safe at level load only because the device is already drained
  — and the fix for that is a `vkCmdCopyBufferToImage` into the existing image, since the grid
  cannot change dimensions within a level. **The generalisable error: a cost rejected against a
  budget instead of against a measurement, in a document that elsewhere insists on measuring.**
  Note the third option this entry never listed, which is the one that should ship: re-flood on a
  **dirty flag** raised only when a sector movement makes `openrange` cross zero. It keeps INV-12
  exactly (connectivity is still decided by real openings, just re-decided), unlike the
  treat-doors-as-open option correctly rejected above.
  **CLOSED 2026-07-27 — that third option shipped as DOOM-0281, and the estimates above held.**
  The dirty flag is one cached open/shut bit per linedef, diffed only on frames where
  `RB_UpdateMeshHeights` already reported `RB_UPD_MOVED` (**0.0039 ms**, and never on a still
  map); the re-flood is the same **0.6–0.7 ms** fill, once per flip rather than once per frame
  of door motion; the upload is the `vkCmdCopyBufferToImage` into the existing image this entry
  predicted. One thing the entry did **not** anticipate and the user's wording did: a swapped
  field makes the fog *pop*, so the answer is eased across with a 0.32 s time constant — *"roll
  in"* was a requirement, not a turn of phrase. Proof it moves the right way: E1M1 spawns at 835
  sealed cells, one door opening drops it to 761, shutting it returns exactly 835. The spawn
  frame is bit-identical to the pre-change build (`-shotcompare` mae 0.000/255), so the whole
  mechanism is inert until something moves.
- **Q23 — CLOSED 2026-08-02, and the answer was neither option.** L3 shipped a third form
  this question never contemplates: the selection happens **at level load**, not per sample
  and not per ray. A per-cell list, ranked by unoccluded contribution, of at most `kFogLightsPerCell` (= 2)
  lights is baked onto the seep grid, so the march does **no** selection scan at all — one
  buffer read indexed by `fogLightCell(p.xy)`. The scan cost this question exists to bound
  is therefore zero at runtime, and the "amend §4.4(b) to match" directive below is
  discharged by `DOOM-0304` rather than by choosing between the two options. Left in place
  because the reasoning still documents *why* a runtime scan was avoided. Original text:
- **Q23 (torch-emitter selection, per sample or per ray? 2026-07-26):** §4.4(b) says pick the
  **nearest few** static emitters *to the sample*, which cuts the expensive phase evaluations
  from `steps × omniStart` to `steps × 4`. But the selection scan itself is still
  `steps × omniStart` — thousands of distance tests per pixel at ~40 steps. The alternative is
  to select **once per ray**, before the march loop, from the ray's midpoint: an `omniStart`-sized
  scan per *pixel* instead of per sample, at the cost of a stale pick on long rays that cross a
  room boundary. **Measure the scan alone at L3**; if per-sample selection does not fit L3's
  share, take the per-ray form and amend §4.4(b) to match. Reverting to "evaluate every emitter"
  is not an option — it is strictly more expensive than either. **L3.**
- **Q24a (the mountains are UNDER-hazed — user play-test 2026-07-27, evidence in hand):** the
  user asked whether the shipped look is right: *"The far walls are nearly white because of the
  mist / fog but the distant mountains are much clearer."* **It is not right, and the cause is
  arithmetic, not tuning.** Both paths use the same formula and the same `SKY_COLOR`, so the only
  difference is the distance each is given — and the sky backdrop is handed exactly
  `kFogMaxDist`, the same clamp a wall at 2048 units gets — identical haze for both. But the
  backdrop depicts terrain at
  effectively infinite distance, so it is systematically under-hazed against any surface at the
  clamp, and a nearer bright wall ends up looking *more* distant than the mountains behind it —
  aerial perspective inverted.
  **SHIPPED 2026-07-27** as `kFogSkyDist = 4096.0`, used by both sky closed forms in place of
  `kFogMaxDist`. **User-confirmed the same day:** *"the mountains now look much better in terms of
  mist / fog."* **Superseded later that day, and the constant now means something else.** The sky
  no longer has a fixed distance at all: §4.6a integrates the exact slant path through the layer,
  so haze varies with the sky pixel's elevation and the inversion cannot recur by construction —
  a horizon ray grazes the layer for longer than any wall ray, which is precisely what "further
  away" should mean. `kFogSkyDist` survives as the layer's finite **horizontal extent** — what a
  level ray gets instead of an infinite path — applied as a SOFT saturation, never a `min()`
  (§4.6a: a hard clamp put a flat plateau with a visible edge across the first ~3° of sky). It is
  back to 2048 (= `kFogMaxDist`) so the skyline is never charged more air than the furthest wall
  the march covers. The "halve it to cancel a density doubling" trick is **gone**: it only bites
  within a couple of degrees of the horizon (see Q24).
  The "slightly darker outside" note in §4.3b is a separate knob and **was answered on
  2026-07-27**: `kSkyShaftStrength` 1.0 → 0.85.
- **Q25 (floor-fog placement, 2026-07-27):** does the floor layer (§4.3c) need its own open-sky
  test, or does sharing the aerial layer's `skyExposure` suffice? Sharing is free and is what the
  amendment assumes. The risk: the floor fog is densest exactly where the aerial layer is thinnest
  — at your feet, under an overhang — so a §4.3a misclassification that is invisible today could
  become obvious. **Hardware, not review. DOOM-0272.**
- **Q26 (floor-fog range vs. the march's step count, 2026-07-27): CLOSED, and SHIPPED
  2026-07-27** — ahead of the floor fog itself, since it changes the accepted look on its own.
  `kFogSteps` = 24 over `kFogMaxDist` = 2048 gave 85-unit steps, so any short `kFloorFogRange`
  would have been resolved by two or three samples and banded. Raising the step count does **not**
  fix it (64 uniform steps still band at a 128-unit range); the march now warps its samples toward
  the camera instead — `t = tMax·s²` with the Jacobian, at the same 24 samples. Measurement,
  the check against the general fog, and the choice of exponent are in §4.3c; the loop contract
  is in §4.2.
- **Q24 (sky density after the L1c raise, 2026-07-26):** §4.3b's fork — give the sky term its own
  effective density/distance, or keep it sharing `kFogBaseDensity` and re-check the mountains
  after the ≈2× raise. L1c takes the second path, with "distant sky still readable at High" as
  its acceptance check. **Superseded in mechanism by Q24a (shipped 2026-07-27):** the sky now has
  its own distance, `kFogSkyDist`, so the resolution is to **lower that one constant** — not to add
  a second sky constant, and not to touch `kFogBaseDensity`, which would undo the foreground tuning
  L1c just did. **Re-opened 2026-07-27, wider than before:** since the sky's path is now geometric
  (§4.6a), `kFogSkyDist` no longer cancels a density change except within a degree or two of the
  horizon, so a doubling **will** move the mountains and there is no one-constant fix. The honest
  levers are `kFogPoolHeight` (a shallower layer clears the sky faster with elevation) or a
  sky-only density. Decide with the screenshot, not from the arithmetic. **L1c.**
- **Q27 (a moving sun, 2026-07-30) — CLOSED by the user the day it was asked.** Baking the
  sun into a load-time field (§4.4's amendment, INV-13) makes `kSunDir`'s constancy
  structural: a day/night cycle would need the field rebuilt per direction, and a *fast*
  one would not be affordable at all. Asked before the fix landed, exactly as DOOM-0289's
  ROADMAP body demanded. **Answer: "DOOM 1 + 2 doesn't feature a day / night cycle. So,
  that's fine."** — fixed sun, no door left open, INV-3 amended to say so. Re-opening this
  is a redesign, not a tune.
- **Q28 (`RB_SUN_NEVER` — how far light bleeds into a shadowed cell, 2026-07-30):** the
  never-sun sentinel is `zLo = ceil(own) + RB_SUN_NEVER`, `zHi = floor(own) −
  RB_SUN_NEVER`, and the value decides where the shaft edge falls between two cell
  centres: for a never-cell of column height `H` blended at weight `w` against a
  neighbour window of width `W`, the interval closes once
  `w·(H + 2·RB_SUN_NEVER) > (1−w)·W`. Starting
  value **128** puts the edge about a third of the way into the shadowed cell for a
  256-unit room — i.e. the edge lands about a **sixth of a cell short of the boundary**,
  pinching slightly, which is the conservative side of §4.3a's accepted half-cell error.
  **Both failure directions are visible, which is why this is a look-tune and not
  arithmetic:** too small and shafts bleed past the boundary into geometry that should
  shadow them; too large and they pinch to narrow stripes around cell centres. **It tunes
  the roofed and doorway edge only** — outdoors the shader's `|| openSky` term disables
  the upper bound, so only `zLo` binds there and a building's shadow edge is governed by
  the `zLo` blend alone. Judge at **L2b**, on the doorway beam, against the pre-change
  screenshot.
- **Q29 (does "an escaped `z` is never re-shadowed" ever show? 2026-07-30):** a starting
  height that has cleared a sky ceiling stays counted as sun-lit, so a structure taller
  than the sky sector it borders does not shadow across it (§4.4's amendment,
  approximation 3). **This is a claim about a height, not about the loop** — the cell
  march continues either way. It is the one of the three approximations where a real ray
  and the field can visibly disagree, and unlike the other two it is not bounded by the
  sun's steepness. **If L2b's play-test finds light where there should be shadow, look
  here first**, and the cheap fix is to keep tightening `hi` past an escape whenever a
  later cell's ceiling rises above the escape height. Not built speculatively — measure
  the defect before paying for it. **L2b.**
- **Q30 (clearance-rebuild cadence, 2026-07-30):** the clearance depends on plane
  *heights*, not just on openings, so its dirty condition is wider than DOOM-0281's flip
  detector and fires on every frame a lift or door is in motion (§4.4's amendment). The
  rebuild is the geometry-cache pass plus the march — cheaper than a full re-flood, since
  the seep's Dijkstra is provably unaffected — but it is not free, and a moving door runs
  ~30 tics. **How often it should actually run is a measurement, not a guess:** rebuild
  every frame of motion, every N-th frame, or once when the plane settles. Q22 is the
  standing warning here — that entry rejected a per-door re-flood by reasoning from a
  budget instead of a measurement, and was wrong by a factor of 33. **Take the number in
  L2b Step 8 first. L2b.**
