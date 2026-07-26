# DOOM-0011 — Volumetric lighting (god-rays + fog) in the ray-traced view

**Status:** **L1 + L1b implemented and user-play-tested** (uniform-haze skeleton 84e8b35, base-density
tune e7753b3; fog-placement standard + sky-backdrop aerial fog 1345c92 — user 2026-07-25:
"looking fantastic… covers the mountains… outside and not inside"). **A 2026-07-25
amendment retargets the look at Silent Hill 2 (§4.3b, wisps) and softens the indoor
cutoff into an outdoor-proximity seep (§4.3a amendment); the perf gate rises to
≤ 15 % (§6). `/cold-eyes` has run **10 loops** and has **not** converged — loop 10 returned
2 CRITICALs, both caught by **compiling** the plan's shader snippets rather than reading them.
Every code block in the plan has now been through `glslangValidator`. Loops 4–10 each had their
worst findings inside the *previous* loop's own fixes. **The `--max-loops` cap
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

**Cold-eyes log (rule 14).** Three review campaigns have run over this document. The
**full loop-by-loop record** — every severity tally, every headline finding, and what each
would have cost if it had reached the implementer — lives in
`docs/specs/DOOM-0011-fix-ledger.md`, alongside the per-fix ripple tables.

| Campaign | Loops | Outcome |
|---|---|---|
| Original spec (2026-07-23) | 4 | **Converged** — polish only by loop 4 |
| 2026-07-24 amendment (fog follows open sky) | 3 | **Converged** — polish only by loop 3 |
| 2026-07-25 amendment (SH2 look + seep) | 10 | **Not converged** — see the ledger |

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
  L1c** — owned by §4.3b's "Step count and the wisps" note, which states the banding
  hypothesis and its cost) — fixed, not
  adaptive, for coherence and simplicity. `tHit` is clamped to `kFogMaxDist` (shipped value **2048** DOOM units,
  `pt_common.glsl:38` — load-bearing, since it is also the mountains' fade distance in
  §4.6a; tuned at L1c) so a
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
  `heightPool = exp(-max(0, p.z − floorZ) / kFogPoolHeight)` (called `heightPool`
  throughout; earlier drafts named it `σ_height`). The floor reference
  `floorZ` for v1 is the **primary hit's** `hitP.z` when the hit faces up (a floor);
  otherwise a level-min fallback. This makes fog **settle low** without new geometry
  data (uses only `hitP`, already in hand). Its coarseness (one floor reference per
  pixel) is an accepted v1 approximation (Q3).
- **Area multiplier & tint** (§4.5) — the profile scales `σ` and sets the medium's
  **scattering tint** `mediumTint` (green in goo, red in hell, neutral in clear).
- **Colour of a shaft = light colour × medium tint.** Sky shafts inherit the fog's sky
  tone — **`kFogColor` (§4.3b); `L1c` work, NOT yet shipped: the tree today still has only
  `SKY_COLOR` (`pt_common.glsl:31`) and `marchFog` still in-scatters it** —
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
their **spawn** state and the field is **not** rebuilt when a door opens in play (Q22).

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
  take the **finite** sentinel `dMax = 8 · kSeepFalloff`. It must be finite: an `R16F`
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
the floor. Sufficient for a soft seep; not a distance oracle. Note the contrast with
`openSky` itself, which **is** fully 3-D (the per-sample up-ray).

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
  same base density. That is ordinary maths, not a bug. It interacts directly with the ≈2×
  density raise below, so **base density is re-tuned with wisps on**, never inferred from
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

**Step count and the wisps.** `kFogSteps` rises **24 → ~40** at L1c, and this section owns
that decision because it is the wisps that force it: a *flat* haze integrates smoothly at
24 steps, but a **structured** density field is a high-frequency signal along the ray, and
undersampling it bands — the same reason a froxel fog needs more slices than a constant
one. That is a **hypothesis to confirm at L1c by looking**, not an estimate: if 24 steps
read clean with wisps on, bank the budget and leave it. The cost is **not independent of
the resolution question** — ×1.67 on steps and ×4 on pixels both multiply the per-sample
up-ray that §4.3a calls the march's dominant cost (§6 item 2). Together with the ≈2%
density raise below, this is what L1c's ≤ 8 % cumulative spot-check (§7) measures.

**Colour and thickness.** In-scatter tone moves from `SKY_COLOR`
(`vec3(0.20, 0.26, 0.40)`, cool blue, `pt_common.glsl:31`) to a near-white desaturated
`kFogColor` — start ~`vec3(0.55, 0.56, 0.56)` in linear radiance: **brighter *and*
colourless**, so distance reads as *pale* rather than merely dim. **The value is defined in
linear**, but the sky branch it also feeds writes a *display-encoded* colour (`skyPanorama`'s
output is deliberately not tonemapped, `pathtrace.comp:1293-1299`), so whether the same
numeric triple is correct on both branches is **Q9's encode question** — resolve it there,
do not assume the number transfers unchanged. `kFogBaseDensity`
rises from `0.0008` toward ~`0.0016` (≈2×) — deliberately **not** the ~3× a wisp-free
haze would be *estimated* to need (an estimate, not a measurement — no density-to-look
tuning has been done yet), because structure sells the look at lower average density, which also
keeps enemies readable. Both tune on hardware, and the `;` strength dial still scales
the whole thing.

**The sky backdrop shares `kFogBaseDensity` — doubling it would erase the mountains.**
§4.6a's aerial-perspective term is a closed form over the full `kFogMaxDist` using the
*same* constant, so doubling density **squares** the transmittance everywhere — which costs far more
where optical depth is already high, i.e. on the sky. At today's `0.0008` × `kFogMaxDist = 2048`, sky transmittance is ≈56 % at the
shipped default (`rb_fog = 1`, strength 0.35) and ≈19 % at High; at `0.0016` those become
≈32 % and **≈3.8 %** — i.e. at High the distant peaks L1b was built to reveal are almost
entirely replaced by fog colour. So L1c must **either** give the sky term its own
effective density/distance **or** re-check the mountains after the raise; "distant sky
still readable at High" is therefore an explicit L1c acceptance item (§7).

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

### 4.4 Light sources & shafts

Fog scatters light from **two** sources only — sky and big static emitters
(user 2026-07-21). Muzzle/flashlight/sprite lights are excluded by construction
(they are push-constant deltas / the `[omniStart, emitCount)` dynamic slice, never
iterated here — INV-2).

**(a) Sky shafts — needs a direction.** `kSunDir` is **already declared but unread**
(`pt_common.glsl:42`, `normalize(vec3(0.30, 0.30, 1.0))`, commented "L2"); L2 wires it
into the march. It stays a compile-time `const` for v1 — a plausible steep slant;
per-level control deferred, Q1. At a march sample, cast **one** shadow ray toward
`kSunDir` with the standard shadow cull mask `0x01`. Because that mask excludes the
sky-backdrop instance (mask `0x04`, `r_vulkan.cpp:2020` — same as the open-sky up-ray
of §4.3a), the ray **reaching the sky = MISSING all solid geometry** toward the sun
(nothing blocks the path). A clear (miss) ray → the sample is **sky-lit**, contributing
`kFogColor · kSkyShaftStrength · phase · mediumTint` (the fog tone of §4.3b, so shaft and
surround match); a ray blocked by solid geometry → dark.

**L2's visibility-gated sky term REPLACES L1's flat sky ambient — it does not add to
it.** L1 shipped an un-shadowed `skyAmbient = SKY_COLOR · kSkyShaftStrength` applied at
every sample (`marchFog`, `pathtrace.comp:782-814`, whose code comment reads "L2 adds
directional sky + torches"). If L2 *added* its term on top, open-air samples would
in-scatter sky light **twice** — the double-count class this spec has already shipped
once. So L2 rewrites that line: the sky in-scatter becomes `vis · kFogColor ·
kSkyShaftStrength`, with `vis ∈ {0,1}` from the sun ray. The consequence is deliberate
and must be stated because it changes the look: **roofed air in-scatters no sky light at
all from L2 onward**, and its only light is L3's emitters. `kIndoorFogScale > 0` still
matters (§4.3a) — it keeps a *medium* in that air for L3's torches to light — but
between L2 and L3 a dark roofed room's fog will read darker than it did at L1b. That
interim is expected, and L3 closes it. The
bright/dark boundary *is* the shaft (a beam through a doorway/sky-hole). One ray per
sample keeps it affordable at half-res (§4.6). **Sky shafts require an open sightline
to the sky:** on a fully enclosed level with no sky (sky tex id
`misc4.w == 0xFFFFFFFF` / no sky mesh, `pathtrace.comp:739`) a solid ceiling blocks
every sun ray, so sky shafts vanish — only torch shafts (b) + the base/haze fog
remain. Expected, not a bug.

**(b) Torch shafts — the existing static emitters.** Iterate the static slice
`k ∈ [0, omniStart)` (`omniStart = pc.misc4.y`, written at `r_vulkan.cpp:7431`; record
layout `pt_common.glsl:84-87`). For cost control, **do not** shadow-test every emitter at
every sample — that is `steps × emitters` rays. Instead (Q2, start cheap):
- pick the **nearest few** static emitters to the sample (distance from the record's
  centroid), and
- add each as `Le · kTorchShaftStrength · falloff(dist) · phase · mediumTint`
  (`kTorchShaftStrength`, `pt_common.glsl:46`, is the emitter-side gain — the sky's twin
  is `kSkyShaftStrength`, `:45`; both ship at `1.0`), with an **optional single**
  occlusion ray to the chosen emitter (start *without* occlusion — a torch glows its
  air even through a thin wall, usually acceptable and much cheaper; add occlusion
  only if light-through-wall reads wrong, Q2).

Both sources feed the same `Ls(p)` accumulation (§4.2). The sky path is the primary
shaft mechanism; torch shafts are the secondary "dark room glows around the flame"
effect.

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
  (albedo re-multiply, §3 gap 3). **What L1 actually shipped is a plain, un-guided bilinear** (`fetchFogBilinear`,
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
  the `pt_common` consts) — the sky branch wrote **no** fog there before L1b — and the
  composite's **existing** `fetchFogBilinear` fold on the sky-passthrough branch
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

**The sky's in-scatter tone follows `kFogColor` too (2026-07-25).** The shipped closed
form in-scatters `SKY_COLOR` (`pathtrace.comp:1320` / `:1335`). L1c must move it to
`kFogColor` alongside the foreground — otherwise the mountains haze **cool blue** while
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
  - Everything else is a **compile-time `const`** per house convention
    (DOOM-0181/0183 §5), each with a starting value so L2–L4 are buildable without a
    round-trip: `kSunDir` = `normalize(0.30, 0.30, 1.0)`, `kFogSteps` = 24 (→ ~40 at
    L1c), `kFogBaseDensity` = 0.0008 (→ ~0.0016 at L1c), `kFogMaxDist` = 2048,
    `kFogPoolHeight` = 48, `kFogAnisotropy` = 0.40, `kGooTint` = `(0.35, 0.85, 0.30)`,
    `kHellTint` = `(0.90, 0.35, 0.30)`, `kIndoorFogScale` = 0.05, the per-source
    strengths = 1.0, **`kAreaDensity` = 0.0020** (§4.5's profile density), **`kFogFloorFallback`**
    and **`kTorchFalloff`** (both L3, §4.3/§4.4 — pooling floor height when no floor is known, and
    the torch inverse-square falloff scale). **`kFogDepthSigma`** (L5's bilateral guide, §4.6 — a
    distance in **world units** between two hit positions, not a depth ratio) is the **one
    exception: it is declared in `svgf_composite.comp` itself, NOT in `pt_common.glsl`**, because
    that shader includes only `formulas.glsl` and `pbr_neutral_tonemap.glsl` — the same limitation
    §4.6a leans on to justify computing the sky fog in the megakernel. **Shipped today** (`pt_common.glsl:37-47`, verifiable by grep): `kFogSteps`,
    `kFogMaxDist`, `kFogBaseDensity`, `kSunDir`, `kSkyShaftStrength`,
    `kTorchShaftStrength`, `kIndoorFogScale`, `kFogPoolHeight`, `kFogAnisotropy`,
    `kGooTint`, `kHellTint`. **Not yet in the tree** — `kAreaDensity` and every
    2026-07-25 constant below; each is a first guess owned by its layer's question. **The 2026-07-25 constants belong to the same inventory:**
    `kFogColor` = `(0.55, 0.56, 0.56)`, `kWispAmp` = 0.6, `kWispWeight2` = 0.7,
    `kWispFreq1` = 1/512, `kWispFreq2` = 2.5·`kWispFreq1`, `kWispVel1` = `(8, 3, 1)`,
    `kWispVel2` = `(−3, 4, 0.3)` units/s (deliberately **slower** than `kWispVel1`, §4.3b), `kWispOffset2` = `(17.3, 5.1, 23.7)`,
    `kSeepMax` = 0.5, `kSeepFalloff` = 192, `dMax` = `8 · kSeepFalloff` (the seep field's
    finite unreachable/void sentinel — §4.3a) (values derived in §4.3a/§4.3b; `kWispAmp`
    and `kWispWeight2` are owned by Q21 alongside `kWispFreq1`). Only the runtime **strength** and the **per-level haze** vary at
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
  `skyExposure` is measured per-sample by a ray-query up-ray (compute only), the sky
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
  - **A 2-D outdoor-distance field** for the seep (§4.3a amendment) — single channel
    **`R16F`** (not `R8`: normalising `d` against `kSeepFalloff` would cap representable
    distance at 192 units, flooring `exp(-d/kSeepFalloff)` at `e⁻¹ = 0.368` and so
    `skyExposure` at ≈`0.22`, four times the intended indoor floor, everywhere).
    Covers the map's XY extent at `64`-unit cells (a large vanilla map stays well under
    `256×256`, i.e. ≤ 128 KB). **Rebuilt per level**, beside the existing mesh build.
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
| **L1c** | `8 % − Δ(L1b)` — whatever L1b left | 8 % |
| **L1d** | ≤ 1 % (the seep tap) | 9 % |
| **L2–L5** | † **≥ 6 % reserved for the four to share** | 15 % |
| **L6** | measures, adds nothing | **15 % — the formal pass/fail** |

† The L2–L5 figure is a **floor, not a ceiling** — a promise *to* those layers. After L1c and
L1d have taken their slices, at least 6 % of the 15 % must still be unspent. All percentages are of
**present-total in milliseconds**, fog-off vs fog-on over the same walk. The prose below
derives these; the table is the version to check against.

- **Baseline & method:** the DOOM-0181/0183 §6 protocol — average the `` \ ``
  profiler (`rb_profile`, DOOM-0090 — the **backslash** key; `` ` ``/`~` is the RT view
  cycle, verified `i_video.c:425` / `:433`) present-total (ms, not FPS) over a fixed ~10 s walk of the **E1M1
  green-goo room** (a sky-hole/doorway scene too, for shafts), RT-on, 50 % render
  scale, with `rb_fog` **off** then **on** (same-walk A/B, the DOOM-0187 lesson).
- **Cost shape (measure, don't assert):** the march is `kFogSteps` samples/pixel,
  each with **up to two** shadow rays (the open-sky up-ray today; L2 adds the sun ray) + a few emitter evaluations, at **half-res**
  (¼ the pixels) + denoise. The shadow rays are the pole; half-res + few steps +
  dither + denoise is what makes it affordable.
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
  reduce `kFogSteps`; drop the emitter occlusion ray (§4.4(b)); distance-gate the march
  (`kFogMaxDist`); make mode 4 half-res too; and — the biggest new lever —
  **swap the per-sample open-sky up-ray (§4.3a) for the near-free per-surface
  `RB_MESH_OUTDOOR` flag**, trading the doorway cutoff for whole-view granularity.
  **Post-L1d this lever is no longer perf-only:** the seep branches on `openSky`, so
  coarsening it to whole-view granularity also degrades the graded seep back toward the
  abrupt room-boundary step the user asked to soften. Re-judge the look, not just the
  frame time.
- **2026-07-24 amendment — the up-ray is the march's FIRST ray, and L1b spot-checks it.**
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
method at L1b, fog resolution at L1c), and only **L6** carries the formal pass/fail gate.

| Layer | Scope | Verify | FPS-gate? |
|-------|-------|--------|-----------|
| **L1** *(shipped 84e8b35..e7753b3)* | The march skeleton: `marchFog` over `[0,tHit]`, constant base density, **isotropic single scatter from the sky only** (no direction yet — flat sky ambient), composited via a new half-res fog target + a **plain (un-guided) bilinear** upsample — the position-guided variant is L5, not this row — + the per-mode apply (§4.6: in-megakernel `toneEncode` for mode 4, `svgf_composite.comp:123`+sky-passthrough for mode 6). Full RGB, no colour profiles. | Air picks up a faint uniform glow; surfaces behind thick fog fade; sky still visible through fog; no NaNs; modes 4 & 6 match | no |
| **L1b** *(shipped 1345c92)* | **The fog-placement standard + the mountains** (2026-07-24 amendment). Two parts: **(i) sky-backdrop aerial fog** (§4.6a) — fog sky pixels over `[0,kFogMaxDist]`, folded on the sky-passthrough branch + mode-4 sky branch, reconciling the old `SKY_FOG_COL` band (Q14); **(ii) open-sky exposure gate** (§4.3a) — per-sample up-ray sky-visibility → `skyExposure` multiplier on density, with the `RB_MESH_OUTDOOR` flag path built in as the perf fallback. | Fog clears under a roof, mountains fade into haze — **full checklist below the table** | spot-check |
| **L1c** | **The Silent Hill 2 look** (§4.3b, 2026-07-25 amendment): near-white `kFogColor`, base density ≈2×, `kFogSteps` 24→~40, **the sky closed form's in-scatter tone `SKY_COLOR` → `kFogColor`** (§4.6a — omit this and the mountains haze cool blue against a near-white foreground, which is the seam this row's own acceptance criterion exists to catch), the sky term's effective density/distance (`kFogMaxDist` or a sky-specific twin — §4.3b), and the **two-octave drifting wisps** off a CPU-generated 3-D noise volume (new sampled image, §5), drift time reusing `misc6.x`. | Near-white colourless fog with slow drifting billows that sit correctly in depth — **full checklist below the table** | spot-check (≤ 8 % cumulative, §6 table) |
| **L1d** | **Outdoor-proximity seep** (§4.3a amendment, 2026-07-25): the load-time flood-filled distance field (new per-level 2-D texture, §5) + the graded indoor `skyExposure`. | Standing in a doorway onto a courtyard, **a little fog drifts in and thins as you walk deeper**; a **sealed** room that merely shares a wall with outdoors is **visually indistinguishable from the same room before L1d** — i.e. it shows the plain `kIndoorFogScale` floor and no seep (proves the fill is through-open-space, not straight-line); the outdoor look is **unchanged from L1c** (the seep touches only the indoor branch); level load adds **≤ 20 ms** on E1M1 (measure the flood fill directly; it runs once, beside the mesh build); **and the runtime seep tap adds ≤ 1 % present-total** on the §6 walk — INV-12's "single bilinear tap" is *per march sample*, inside the loop §6 calls the dominant cost, so it is not free merely because the fill is load-time | spot-check |
| **L2** | **Sky shafts:** add `kSunDir` + the one-ray sky-visibility test per sample + HG phase (builds on L1b's up-ray machinery). | A doorway/sky-hole open to sky throws a visible slanted beam; closed rooms stay clear; the beam moves correctly as the camera orbits | no |
| **L3** | **Height pooling + torch shafts:** height-based density (`hitP.z` floor ref); iterate static emitters `k<omniStart` (nearest-few, no occlusion first). | Fog settles low into a floor layer; a torch in a dark room glows its surrounding air; dynamic/muzzle/flashlight do **not** scatter | no |
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
- **INV-2:** Fog scatters light from **sky + static emitters `[0, omniStart)`
  only** (`omniStart = pc.misc4.y`). Dynamic sprite lights `[omniStart, emitCount)`,
  the muzzle flash (`misc2.z`), and the flashlight (`misc2.w`) **never** scatter.
  *Falsifiable:* L3's own acceptance row — "dynamic/muzzle/flashlight do **not** scatter"
  (§7) — plus the loop bound `k < omniStart` read by diff.
- **INV-3:** The sun direction stays a **compile-time `const`** in v1 — no runtime
  sun-direction control is added. (`kSunDir` is already declared but unread at
  `pt_common.glsl:42`; L2 wires it in. It is the engine's *first* directional light —
  before it there was only the positional sky backdrop + constant `SKY_COLOR`.)
  *Falsifiable:* no push lane, uniform or descriptor carries a sun direction — INV-5's
  240 B `static_assert` still holds and no new UBO field appears. Per-level control is
  deferred (Q1).
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
  geometry hit = indoor** (the mask mechanism is derived once in §4.3a). It is the user's
  "true volumetric" pick; the per-surface `RB_MESH_OUTDOOR` flag was the cheap fallback; L1b shipped the up-ray,
  so the flag stays unbuilt as a standing perf lever. "Open sky" = `ceilingpic ==
  skyflatnum`, the engine's own open-air signal. *Falsifiable:* L1b's acceptance row —
  roofed rooms clear, a mist wall at the threshold (§7) — and, for the split, L4's
  "E3M1 shows haze" check, which fails by construction if `skyExposure` ever multiplies
  `areaMult`. **Amended 2026-07-25:** the open-sky
  branch is still exactly `1`, but the **indoor** branch is no longer the flat
  `kIndoorFogScale` — it is `mix(kIndoorFogScale, kSeepMax, exp(-d/kSeepFalloff))`,
  where `d` is the **through-open-space** distance to outdoor air (§4.3a amendment).
- **INV-10 (sky-backdrop fog, 2026-07-24):** sky pixels receive **aerial-perspective
  fog** (`skyExposure = 1`) over the sky's fade distance (`kFogMaxDist` as shipped in L1b;
  L1c may substitute a sky-specific constant — §4.3b), folded as `sky · transmittance +
  inscatter` on the mode-6 sky-passthrough branch (`svgf_composite.comp:93-107`) and the
  mode-4 sky branch (`pathtrace.comp:1326-1337`), in the **same linear space** as every other
  fog fold (INV-4). Fog-off (`rb_fog == 0`) → `transmittance = 1, inscatter = 0`, so the
  sky is **byte-identical** to today (INV-7/INV-8). No up-ray and no new resource
  (INV-5) — the sky is outdoors by definition. **Amended 2026-07-25:** the closed form omits **both** `wisp` **and** `heightPool` —
  it is `kFogBaseDensity · fogStrengthScale` integrated over `[0, kFogMaxDist]`, nothing
  else. `heightPool` cannot apply (a sky ray has no `floorZ`), and the wisp exclusion is
  **deliberate** (`wisp ≡ 1` for sky pixels) — a closed form requires
  constant density, and billow structure on the mountains would be sub-pixel anyway. The
  consequence to watch is the **sky/wall seam** between wisped foreground and un-wisped
  sky, which is an explicit L1c acceptance item (§7). *Falsifiable:* that acceptance row — "no visible discontinuity at the sky/wall seam" — plus, by diff, the sky branch never samples the noise volume and never reads a floor reference.
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

## 10. Open questions

- **Q1 (sun direction):** a single world-space `const` for all levels (v1), or
  per-level/per-sky tuning? Start `const`; the shaft angle is a look-tune (L2).
- **Q2 (torch-shaft cost):** nearest-few emitters with **no** occlusion ray (cheap,
  may glow through thin walls) vs one occlusion ray each (correct, costlier). Start
  no-occlusion; add if light-through-wall reads wrong (L3).
- **Q3 (density source + floor reference):** primary-hit-keyed goo density (v1, cheap,
  blind to goo behind/around corners) vs a per-sector fog buffer (correct, new plumbing).
  **Also owns the height-pooling floor reference** — one `floorZ` per pixel from the
  primary hit (§4.3), which is coarse wherever a pixel spans two floor heights (L3).
  v1 takes the primary-hit key; revisit if the room-fill reads wrong (L4).
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
  wired this fold with `rb_fog`-gated `fetchFogBilinear`; the round-trip is a **no-op by
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
- **Q14 (double-haze, 2026-07-24):** The sky-distance fog (§4.6a) overlaps
  `skyPanorama`'s existing screen-space `SKY_FOG_COL` band (`pathtrace.comp:763-764`).
  L1b halved it (`fog *= 0.5`, `pathtrace.comp:770`); L1c's near-white base changes the
  balance again, so this **re-opens at L1c** — dial the old band down further or remove
  it so the horizon isn't hazed twice.
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
- **Q23 (torch-emitter selection, per sample or per ray? 2026-07-26):** §4.4(b) says pick the
  **nearest few** static emitters *to the sample*, which cuts the expensive phase evaluations
  from `steps × omniStart` to `steps × 4`. But the selection scan itself is still
  `steps × omniStart` — thousands of distance tests per pixel at ~40 steps. The alternative is
  to select **once per ray**, before the march loop, from the ray's midpoint: an `omniStart`-sized
  scan per *pixel* instead of per sample, at the cost of a stale pick on long rays that cross a
  room boundary. **Measure the scan alone at L3**; if per-sample selection does not fit L3's
  share, take the per-ray form and amend §4.4(b) to match. Reverting to "evaluate every emitter"
  is not an option — it is strictly more expensive than either. **L3.**
- **Q24 (sky density after the L1c raise, 2026-07-26):** §4.3b's fork — give the sky term its own
  effective density/distance, or keep it sharing `kFogBaseDensity` and re-check the mountains
  after the ≈2× raise. L1c takes the second path, with "distant sky still readable at High" as
  its acceptance check. If that check fails, the resolution is a separate `kFogSkyDensity`, **not**
  another `kFogBaseDensity` change — lowering the base would undo the foreground tuning L1c just
  did. **L1c.**
