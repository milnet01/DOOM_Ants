# DOOM-0183 — Glowing, wet nukage (and glowing lava) in the Ultra RT view

**Status:** **Design — pre-implementation.** Not yet built; not yet run through
`/cold-eyes` (rule 14 gate is **open** — this doc must loop clean before any
implementation lands). Design approved by the user 2026-07-17 (scope: "cheap wins
first" — glow + wet look now, **true mirror reflections deferred to DOOM-0103**;
green nukage gets the full wet treatment; lava gets glow + cast-light only).

This is the **first** of the goo work. It makes green nukage read as a *glowing,
wet, rippling liquid* and makes lava *glow and cast coloured light* — using only
cheap, self-contained techniques (a direct-light sheen and a procedural ripple
normal), **no reflection rays** and **no general GGX lobe**. The heavy "you can
see the room mirrored in the goo" capability stays DOOM-0103.

> **Framing correction from the DOOM-0183 cheaper-RT research (2026-07-17).**
> A background survey (id Software's DOOM: The Dark Ages SIGGRAPH 2025 talk +
> the broader field) confirmed the goo room's ~40 FPS is the **megakernel +
> denoiser**, not any surface effect — and that the real perf wins are
> whole-engine, not per-feature. Those were captured as their **own** roadmap
> items (DOOM-0188 reduced-res GI trace, DOOM-0189 world-space radiance cache,
> DOOM-0190 async/2-frames, DOOM-0191 A-SVGF denoiser, DOOM-0192 ReSTIR
> far-future) and are **out of scope here**. The one research finding this
> feature *acts on*: an emissive liquid should light its surroundings through
> the **existing NEE emitter path**, not a new light system, and ReSTIR is
> overkill for a handful of pools (measure first). This spec stays lean.

**Depends on:**
- **DOOM-0009** (path tracer) — the RT back-end. This hooks the primary hit in
  `pathtrace.comp` **mode 4** (NEE display) and **mode 6** (denoised play), the
  same two modes DOOM-0181 hooks, at the same shading point (after the material
  is resolved, alongside the `applyGrime` call). Cast-light reuses the NEE
  emitter set (`ComputeMaterialEmissive` → `g.matEmissive[id]` →
  `BuildStaticEmitterSet`, `r_vulkan.cpp`).
- **DOOM-0042** (Ultra HD PBR materials) — provides the per-material control
  SSBO (`MatCtrl ctrl[]`, `pathtrace.comp:73`) whose `flags` bitfield
  (`bit0 pom, bit1 noPom, bit2 sprite`) carries the new **liquid bit**. Note the
  liquid bit rides `ctrl[]`, which is populated for **every** material (paletted
  included — `InitHdDefault` seeds an all-paletted set), so nukage/lava need **no**
  HD hero; they are paletted and still read the flag.

**Delivers / subsumes:**
- **DOOM-0083** (green slime/nukage emits a faint green glow) — this feature
  *delivers* it: the nukage green `Le` + cast-light (§4.3) is exactly DOOM-0083's
  scope, done here as the glow layer. DOOM-0083 graduates when DOOM-0183 ships.
- Lava glow + cast-light is **new** to this feature — no existing item owns
  lava-*flat* lighting. (DOOM-0084 covers free-standing sprite light *objects* —
  floor lamps, torches, burning barrels — a different surface class; it remains
  its own item and does **not** cover lava flats.)

**Defers (explicitly NOT in this build):**
- **True reflections** — the room mirrored in the goo — needs reflection rays +
  the GGX/VNDF lobe. Stays **DOOM-0103**. The wet **sheen** here is a *direct-light
  highlight only* (§4.4), not a reflection and not the DOOM-0103 lobe.
- **Water and blood** (`FWATER*`/`SWATER*`, `BLOOD*`) — untouched; the mechanism
  extends to them later by flagging their flats.
- **A liquid replacement texture** (hand-authored wet normal-map art) — v1 uses a
  *procedural* ripple normal (no asset, animates for free); revisit if it reads
  flat (§9).
- **ReSTIR** emissive-light sampling — far-future **DOOM-0192**; a few pools do
  not need it.

**Scope:** Ultra RT view only (`pathtrace.comp` modes 4 + 6). Classic and the
raster stack (Solid, **and Ultra with RT off**) stay **byte-identical**. Every
non-liquid surface is byte-identical too — the sheen and ripple branches are
gated on the liquid bit — **except** a floor carrying a DOOM-0181 green-goo
puddle stain, which takes a light wet touch (sheen + faint glow) via the goo
mask (§4.6). A wall or ordinary (non-puddle) floor takes no new work and no new
look; the one shared change is a new `misc6` push-constant lane (§5, INV-6),
which every non-liquid shading path simply ignores.

---

## Contents

- §1 Goal — §2 Where this sits — §3 The problem, precisely — §4 Design
  (4.1 hook · 4.2 liquid identity · 4.3 glow + cast-light · 4.4 wet sheen ·
  4.5 ripples · 4.6 puddles) — §5 Data & resources — §6 Performance budget —
  §7 Build order — §8 Invariants — §9 Alternatives considered — §10 Open questions

---

## 1. Goal

In the Ultra ray-traced view, make the **green toxic sludge** (the source nukage
pool *and* the DOOM-0181 goo puddles that pool near it) read as a **glowing, wet,
gently rippling liquid** instead of a flat green floor; and make **lava** glow and
throw warm coloured light. Keep the DOOM feel — stylised, a little exaggerated to
match the ray-traced-DOOM look the user is after (see DOOM-0193 glow dial-up), not
photoreal — and keep it cheap: no reflection rays, no new light system, no new art.

Four layered effects, each liquid-gated:

- **Glow + cast-light (§4.3)** — nukage self-lights green and casts green light on
  nearby geometry; lava the same in orange. Reuses the existing emitter path.
- **Wet sheen (§4.4)** — a bright, tight highlight where a light (flashlight,
  lamp, muzzle) bounces toward the eye. A *direct-light* specular, on nukage
  **and goo puddles** (not lava).
- **Ripples (§4.5)** — a scrolling procedural wave normal wobbles the nukage
  surface so the sheen shimmers and the diffuse lighting ripples. Nukage only.
- **Puddle wet (§4.6)** — the DOOM-0181 green-goo puddles get the sheen + a faint
  glow (but **no** ripples — they are a thin film, not a deep pool).

Lava takes **only** the glow + cast-light (it reads molten and hot, not wet) —
no sheen, no ripples (user, 2026-07-17). Water and blood take nothing this build.

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0183? |
|-----------------|----------|-----------------------|
| Classic | paletted software | No |
| Solid, or Ultra with RT **off** | raster stack (DOOM-0170) | No |
| Ultra with RT **on** (`rb_rtdebug` 4/6) | path tracer | **Yes** — flagged liquid surfaces, plus goo-puddle floors (§4.6) |

The gate is *RT engaged* (path-trace modes 4/6), **not** the tier label — exactly
like DOOM-0181. Modes 4 (NEE display) and 6 (denoised play) get identical
treatment. The offline GI bake (`bake.comp`) shares only `pt_common.glsl` and never
samples `ctrl[]`/the sheen/ripple branch, so the **view- and time-dependent** wet
effects (sheen, ripples) live only in the megakernel and cannot touch the bake. The
**cast-light `Le`** (§4.3) *does* enter the shared emitter set, so the baked
bounce and the directly-traced frame agree on the green/orange glow (INV-8).

## 3. The problem, precisely

Today, in the RT view:

1. **Nukage is a flat green floor.** It has no glow (DOOM-0083 unbuilt), no
   highlight (the tracer shades with **albedo + normal + AO only** — there is *no*
   specular term anywhere yet; verified 2026-07-16, DOOM-0181 §"Defers"), and no
   motion. It reads as painted concrete, not liquid.
2. **Lava is a flat orange floor** — same, no glow, no cast light.
3. **The only "is this liquid?" signal is a crude albedo guess.** `applyGrime`
   early-returns on a saturated-green albedo (`g > r·1.15 && g > b·1.15`,
   DOOM-0181 INV-10) purely to avoid painting dirt on goo. That guess (a) also
   fires on any green *wall* and (b) cannot tell the *source pool* from a *painted
   puddle* — so it is unfit to drive a visual effect. We need a **surface-true**
   liquid signal.

The fix is a proper per-material **liquid bit** set from the flat *name* at level
build (§4.2), driving each effect below, with the crude albedo guess retired as a
detection mechanism (it stays only as DOOM-0181's dirt-guard).

## 4. Design

### 4.1 Where it hooks

Same primary-hit shading point as DOOM-0181, in **both** `pathtrace.comp` mode 4
and mode 6 (kept in lockstep). After the material `id` and control record
`mc = ctrl[id]` are resolved and the surface normal `n`, world hit `hitP`, and
albedo are computed, the liquid path runs in this order:

1. **Ripple** (§4.5) — if nukage, perturb `n` *before* lighting, so the wobble
   rides into both the diffuse shade and the sheen.
2. **Shade** — the existing `shadeSurface` diffuse NEE + self-emission (unchanged;
   the green/orange `Le` from §4.3 is already in `g.matEmissive[id]`, so glow +
   cast-light come "for free" through the normal emitter path).
3. **Wet sheen** (§4.4) — if nukage **or a §4.6 goo puddle**, add the direct-light
   specular highlight on top of the shaded result (nukage is triggered by the
   liquid bit; a puddle by the DOOM-0181 goo-stain mask).

Non-liquid, non-puddle hits skip 1 and 3 entirely (nukage ripple/sheen gated on
the liquid bit; puddle sheen gated on the goo mask) and are byte-identical to
today. The whole block is Ultra-RT-only by construction.

### 4.2 Liquid identity — a per-material bit from the flat name

- **`MatCtrl.flags` gains two bits** (the field already exists,
  `pathtrace.comp:76`; `bit0 pom, bit1 noPom, bit2 sprite` are used, bits 3+ free):
  - **`bit3 = LIQUID_NUKAGE`** — green toxic sludge (full wet treatment).
  - **`bit4 = LIQUID_LAVA`** — lava (glow + cast-light only).
- **Set on the CPU at material build**, where the `ctrl[]` SSBO `flags` are
  assembled (DOOM-0042 T8 path, `r_vulkan.cpp`), by an **allow-list of flat
  names** taken from the engine's own animation table (`p_spec.c animdefs[]`):
  - Nukage: **`NUKAGE1`, `NUKAGE2`, `NUKAGE3`** (all three animation frames — the
    live flat cycles via `flattranslation`/DOOM-0066, and each frame is its own
    material id, so all three carry the bit).
  - Lava: **`LAVA1`–`LAVA4`**.
- **Deliberately NOT flagged in v1** (INV-2): water (`FWATER1-4`, `SWATER1-4`),
  blood (`BLOOD1-3`), and the DOOM II `SLIME01-12` flats — several `SLIME*` frames
  are dry rock / hazard-stripe floor, not liquid, so a blanket "SLIME = liquid"
  would mis-flag them. Extending to water/blood later is a one-line allow-list
  addition per flat.
- **The shader reads the bit** off `mc.flags` (available for paletted nukage/lava
  too — the record exists for every material). This **replaces the albedo-green
  guess** as the effect trigger; DOOM-0181's guess stays only as its own dirt-guard.

### 4.3 Glow + cast-light — reuse the emitter path

- **Nukage and lava get a guaranteed, constant material emissive `Le`** — a fixed
  green for nukage (`kNukageLe`), orange for lava (`kLavaLe`) — **forced** for the
  allow-listed flats in `ComputeMaterialEmissive` (`r_vulkan.cpp:4371`), written
  into `g.matEmissive[id]`. This is a **new forced-constant path**, *not* the
  existing `derive_material_le` fallback: that routine's `allowFaint`
  (DOOM-0157, `emissive_derive.h:86`) is **conditional** (it returns 0 unless the
  tile holds a texel over `kBrightLum`, `emissive_derive.h:116`) and **per-texel
  derived** (`Le = Σ bright texels / total × kEmissiveScale`, `:120`) — so it
  neither guarantees emission nor gives a controllable colour. Forcing a constant
  `Le` for the named liquids sidesteps the peak-region gate entirely — the exact
  "may need a per-flat allow-list or a lowered threshold" case DOOM-0083 flagged —
  and makes the glow tunable.
- **This is the entire cast-light mechanism.** A material with `matEmis > 0`
  (a) **enters the NEE emitter set** via `BuildStaticEmitterSet` (which reads
  `g.matEmissive` directly, *unmodulated*), so it casts soft coloured light on
  neighbours; and (b) **self-glows on the primary ray**, where the term is
  `matEmis[id] · emissiveMask(albedo)` (`pt_common.glsl:213`; `emissiveMask` =
  `smoothstep(0.30, 0.60, max-channel(albedo))`, `:46`) — i.e. the *self*-glow is
  scaled by the surface's own brightness, so the saturated-green/orange liquid
  albedo (max-channel well above 0.60) passes it and glows fully. No new light
  type, no new buffer (INV-7). Nukage animation already triggers an emitter-set
  rebuild (the DOOM-0082 live-retex path), so a cycling nukage flat keeps emitting.
- **Colours and strength are compile-time `const`s** (`kNukageLe`, `kLavaLe` — the
  green/orange linear radiance), tuned on hardware toward the exaggerated RT-DOOM
  look (DOOM-0193). Start faint; the goo should tint a room, not floodlight it.

### 4.4 Wet sheen — a direct-light specular (no reflection rays)

The tracer has no specular term. Rather than pull in DOOM-0103's general GGX lobe
(F0/VNDF/MIS — a whole feature), this adds a **narrow, gated, direct-light**
highlight — the honest "cheap win":

- **Trigger:** a **nukage** hit (the liquid bit, §4.2) **or** a **goo puddle**
  (the DOOM-0181 goo-stain mask, §4.6). Lava and every other surface get no sheen.
- For a triggered hit, add a **Blinn-Phong-style specular** term: `spec =
  kWetSheenStrength · pow(max(dot(n, H), 0), kWetSheenPower)`, where `H` is the
  half-vector between the view direction and the **dominant light direction**, and
  `n` is the *rippled* normal (§4.5, nukage only — a puddle uses its un-rippled
  normal, §4.6). It is a highlight of *actual light sources* (flashlight, nearby
  lamp/emitter, muzzle) — **not** a mirror of the room. No reflection ray is cast
  (INV-3).
- **Dominant-light choice is an implementation decision** (Q2): the simplest cut
  is the **flashlight** (always present, reliable in dark goo rooms) plus the
  muzzle flash; a fuller cut sums the sheen over the NEE-sampled lights. Start with
  the dominant light; widen only if it reads sparse.
- Computed in the mode-4/6 block (where `mc.flags` is in scope), **after**
  `shadeSurface`, so it does not perturb diffuse GI and stays out of the bake.
- **Tightness/strength are compile-time `const`s** (`kWetSheenPower` high = a small
  sharp glint; `kWetSheenStrength` its brightness). Must respect the PBR-Neutral
  tonemap headroom so a glint reads bright without blowing to a white disc (Q7).
- **Fallback (drop-clean):** the sheen is the engine's *first* specular term, with
  no prior strength to inherit and a real tonemap blow-out risk (Q7). If it cannot
  be made to read bright-without-clipping, it **drops** without affecting the glow
  (§4.3, permanent) or the ripples (§4.5) — mirroring the L5 puddle drop path.

### 4.5 Ripples — an animated procedural normal

- For **nukage only** (not lava), perturb the surface normal before lighting:
  `n' = normalize(n + rippleGrad(hitP.xy, time))`, where `rippleGrad` is a small
  **procedural wave gradient** — two or three scrolling sine/gradient-noise layers
  at different world scales and directions, summed, driving a shallow tangent-plane
  tilt. World-XY-keyed (`hitP.xy`) so the pattern is continuous across a pool and
  does not swim with the camera.
- **Time** is a float in **seconds** (frame-rate-independent, so `kRippleSpeed`
  has a fixed meaning), supplied per frame on the new `misc6.x` push-constant lane
  (§5). There is **no** existing time/animation uniform in the shader, and no free
  lane in modes 4/6 (verified 2026-07-17), so a per-frame time value must be added
  (§5, INV-6).
- Amplitude is deliberately **shallow** (`kRippleAmp`) — a wet shimmer, not a
  choppy sea; speed/scale are `kRippleSpeed`/`kRippleScale` `const`s. Because the
  rippled `n` feeds both `shadeSurface` and the sheen, the whole surface
  glimmers coherently.
- Off-liquid and on lava, `n` is **unperturbed** — byte-identical normal path
  (INV-4).

### 4.6 Puddles — the DOOM-0181 goo, made wet (light touch)

The DOOM-0181 filth system already paints **green-goo puddles** on up-facing
floors near the goo room (its `stain`/goo mask, `applyGrime`, INV-11). Give those
puddles the **sheen (§4.4) + a faint green glow**, but **no ripples** (a thin film
should not undulate like a deep pool).

- The puddle is **not** flat-flagged (it is painted onto an ordinary floor), so its
  wet trigger is the **goo-stain mask** the DOOM-0181 stain path already computes.
  Whether to *expose* that mask out of `applyGrime` or *re-derive* the goo test in
  the mode block is an implementation call (Q3) — the mask is the honest,
  targeted signal (keying off "albedo is green after grime" would also catch green
  walls).
- The faint puddle glow is a small additive green term (not a full `matEmissive`
  entry — puddles are not materials); strength `kPuddleGlow`, well below the source
  pool.
- This is the **last** build layer and the one most at risk of looking "floaty"
  on a thin stain; if it does, it drops without affecting the source-pool work
  (§7 L5, Q3).

## 5. Data & resources

- **No new images.** Ripples are procedural (§4.5); glow is a colour `const`
  (§4.3); the sheen is ALU (§4.4). (Contrast DOOM-0181, which added `dirt.png` +
  AO maps.)
- **No new GPU buffers / bindings / SSBOs.** The liquid bit rides the existing
  `MatCtrl.flags`; the glow rides the existing `g.matEmissive` + emitter set.
- **Two runtime scalars reach the shader — ripple `time` and the wet toggle — via
  a new `misc6` uvec4 push-constant lane.** No existing lane is free in modes 4/6:
  `misc`, `misc4`, `misc5` are fully assigned, and `misc2.z`/`.w` — despite the
  **stale** "z,w reserved" comment at `pathtrace.comp:254` — actually carry the
  muzzle-flash strobe (`misc2.z`, read in `muzzleFlashDelta`, `pathtrace.comp:405`)
  and the flashlight toggle (`misc2.w`, `flashlightDelta`, `:432`); `r_vulkan.cpp:6382`
  labels them correctly. `misc3` is verify-only (0 in display modes), and overloading
  it with display data would repeat exactly this stale-comment trap — so it is **not**
  reused. Hence a genuinely new lane:
  - `misc6.x` = **`time`** (float-bits, seconds); `misc6.y` = **wet toggle**
    (`rb_wet`, 1/0); `.z`/`.w` reserved 0.
  - Appended so it respects std430 **16-byte alignment** (a `uvec4` at the current
    216-byte tail is not 16-aligned, so alignment padding is required — the exact
    resulting size, e.g. ~240 B, is pinned at implementation, not asserted here) and
    sits **after** the 184-byte `-rtverify` range (`RtPC == 184`, `r_vulkan.cpp:5902`)
    so verify is unaffected. Update in lockstep — the C++ struct + its `static_assert`
    (currently `216`, `r_vulkan.cpp:6396`), the `pcr.size` range (currently `216`,
    `r_vulkan.cpp:2201`), and the GLSL `layout(push_constant)` — staying within the
    **256-byte** device limit (`r_vulkan.cpp:2197`).
  - **What the toggle gates:** `rb_wet` gates only the **shader-side, view/time**
    layers — sheen (§4.4) + ripples (§4.5) + puddle wet (§4.6). It **cannot**
    disable the glow/cast-light (§4.3): that `Le` is CPU-built into
    `g.matEmissive`, the NEE emitter set, and the GI bake, none of which a
    per-frame push constant can un-build — and the glow is *permanent* anyway
    (it delivers DOOM-0083). So `rb_wet` is a **sheen/ripple/puddle** toggle, not a
    whole-feature master switch.
- **`MatCtrl.flags` bits:** `bit3 LIQUID_NUKAGE`, `bit4 LIQUID_LAVA` (set on the
  allow-listed flats at material build).
- **`g.matEmissive` allow-list:** `NUKAGE1-3` → forced constant green `Le`
  (`kNukageLe`); `LAVA1-4` → forced constant orange `Le` (`kLavaLe`) — set directly
  in `ComputeMaterialEmissive`, bypassing `derive_material_le` (§4.3).
- **Tuning knobs (compile-time `const`s, like DOOM-0181's `k*`):** `kNukageLe`,
  `kLavaLe` (glow colour/strength), `kWetSheenPower`, `kWetSheenStrength`,
  `kRippleSpeed`, `kRippleScale`, `kRippleAmp`, `kPuddleGlow`, `kPuddleSheenScale`.
  Per house convention (DOOM-0181 §5), *tuning* lives in `const`s, not push
  constants — only the runtime *toggle* and *time* go in a push-constant lane.
- **New runtime dial:** `rb_wet` (`int`, default 1) in `r_vulkan.cpp`, a config
  entry `rt_wet` in `m_misc.c` (mirroring `rt_detile`/`rt_filth`), and a keyboard
  toggle in `i_video.c` printing `Ultra wet-liquid ON/OFF`.

## 6. Performance budget

- **Baseline & method:** same protocol as DOOM-0181 §6 — average the `\`
  profiler (`[cpu_profile]` / `[rt_profile]`) present-total (ms, not FPS) over a
  fixed ~10-second walk of the **authoritative gate scene — the E1M1 green-goo
  room** (a lava map too, *when one is available*), RT-on, 50 % render scale,
  flashlight, with `rb_wet` **off** then **on** (the toggle gives a same-walk A/B,
  avoiding walk-to-walk variance — the DOOM-0187 lesson). Note the A/B isolates
  only the **sheen + ripple + puddle** cost; the toggle does **not** gate the
  glow/emitter cost (§5), which is measured separately (flag-present vs
  flag-absent build) or accepted as a marginal always-on cost.
- **Expected cost (to be measured, not asserted):**
  - **Glow + cast-light:** marginal — a few more entries in the NEE emitter set
    (the goo/lava surfaces), plus a per-animation-tic emitter-set rebuild for the
    cycling nukage flats. Not a per-pixel cost; the DOOM-0119 REJECT cull and
    the static-emitter cache (2026-07-14) already bound emitter cost.
  - **Wet sheen:** a few ALU + one `pow` per **liquid** pixel (a minority of the
    screen), no texture fetch. Cheapest layer.
  - **Ripples:** procedural noise ALU per **nukage** pixel — 2–3 wave layers. No
    fetch. Scales with on-screen nukage area.
  - All three are **liquid-gated**, so a scene with no goo/lava on screen pays
    ~nothing (the branch is not taken).
- **Perf levers held ready** (measure before cutting):
  1. The **`rb_wet` toggle** — already the isolation instrument and a standing perf
     option (mirrors DOOM-0187's filth toggle). No look change on. Gates
     sheen/ripple/puddle only (§5), not the glow.
  2. Drop ripple layers 3 → 2.
  3. Distance/LOD-gate the sheen + ripples (far nukage goes flat).
- **Gate (evaluated at the perf layer, §7 L6):** the sheen + ripple + puddle layer
  must add **≤ 5 %** to present-total vs the `rb_wet`-off RT-on baseline on the
  E1M1 goo-room walk — the same ≤ 5 % bar DOOM-0181 held. L1–L5 are visual
  play-test only (no FPS gate). Lava's cast-light cost is gated on a lava map
  when one is available, else measured when one is (Q6).

## 7. Build order

Each layer is independently play-testable (renderer look is a play-test call, per
DOOM-0179/0042/0181). L1–L5 acceptance is **human play-test**; only **L6**'s ≤ 5 %
perf check is objective.

| Layer | Scope | Verify | FPS-gate? |
|-------|-------|--------|-----------|
| **L1** | Liquid bit: `MatCtrl.flags` bit3/bit4 + flat-name allow-list on CPU; shader reads it (debug-tint nukage/lava to prove detection, then remove the tint) | Only actual nukage/lava flats light up; green *walls* do not; pool vs painted-puddle distinguished | no |
| **L2** | Glow + cast-light: forced-constant green/orange `Le` for the allow-listed flats via `ComputeMaterialEmissive`; enters the emitter set (**delivers DOOM-0083**) | Nukage self-glows green + tints neighbours; lava glows orange + casts light; animation keeps emitting; dark room shows the goo. Colour/strength are const-tunable if wrong (glow is not dropped — it delivers DOOM-0083) | no |
| **L3** | Wet sheen (§4.4): direct-light specular on nukage (dominant light first) | A bright glint tracks the flashlight/lamp across the nukage; walls unaffected; no blow-out under tonemap. Drops clean (§4.4) if it can't be made to read right | no |
| **L4** | Ripples (§4.5): time lane (§5) + procedural wave normal on nukage; feeds shade + sheen | The nukage surface shimmers; the sheen breaks up and moves; lava + non-liquid normals unchanged | no |
| **L5** | Puddle wet (§4.6): DOOM-0181 goo puddles take sheen + faint glow, **no** ripples | Puddles read wet, not floaty; drop this layer if it looks like floating film (no impact on L1–L4) | no |
| **L6** | Runtime toggle (`rb_wet` + key, §5) + perf pass | Sheen + ripple + puddle adds ≤ 5 % vs `rb_wet`-off baseline (§6); the sheen/ripple/puddle toggle flips cleanly (the glow stays — it is permanent); `-rtverify` green | **yes** |

Lava is complete at **L2** (glow + cast-light); L3–L5 are nukage/puddle only.

**Interim state (expected, not a regression):** L3 ships the sheen on the
**un-rippled** normal; the ripple coupling (§4.4's "rippled normal") arrives at
L4. Between L3 and L4 the glint is static — expected, mirroring DOOM-0181's
"de-tile a subset first" interim (its INV-3).

## 8. Invariants

- **INV-1:** Liquid identity comes from **`MatCtrl.flags`** (a bit set from the
  flat *name* at material build), **not** from an albedo-colour guess. The
  DOOM-0181 saturated-green early-return stays *only* as its dirt-guard, never as
  the trigger for a DOOM-0183 effect.
- **INV-2:** v1 flags **only** `NUKAGE1-3` (nukage) and `LAVA1-4` (lava). Water
  (`FWATER*`/`SWATER*`), blood (`BLOOD*`), and DOOM II `SLIME*` flats are **not**
  flagged (some `SLIME*` frames are dry rock — flagging them would wet a floor).
- **INV-3:** The wet sheen is a **direct-light specular** only — no reflection ray,
  no GGX/VNDF/MIS lobe (that is DOOM-0103). Every non-liquid surface takes **no**
  specular term and is byte-identical to today, **except** a floor carrying a
  DOOM-0181 goo-stain, which takes the sheen via the goo mask (§4.6).
- **INV-4:** Ripples perturb the normal on **flagged nukage only**. Lava, goo
  puddles, and every non-liquid surface keep an **unperturbed** normal (identical
  normal path).
- **INV-5:** Ultra RT only; modes 4 and 6 get identical treatment. Classic and the
  raster stack (Solid, **and Ultra with RT off**) stay **byte-identical**.
  Non-liquid RT surfaces are byte-identical too, **except** floors carrying a
  DOOM-0181 goo-stain (§4.6), which take sheen + a faint glow via the goo mask
  (not the liquid bit).
- **INV-6:** The ripple `time` + wet toggle ride a **new `misc6` uvec4 lane** —
  no existing lane is free in modes 4/6 (`misc2.z`/`.w` are muzzle-flash/flashlight
  despite the stale `pathtrace.comp:254` comment; `misc3` is verify-only and not
  reused). The lane is std430-16-byte-aligned (needs alignment padding past the
  216-byte tail), sits beyond the 184-byte `-rtverify` prefix so verify is
  unaffected, and stays within the 256-byte device limit (`r_vulkan.cpp:2195-2201`).
  The C++ struct, `static_assert`, `pcr.size`, and GLSL block update in lockstep.
  (Contrast DOOM-0181 INV-9, which needed no per-frame runtime value at all.)
- **INV-7:** Cast-light reuses the existing NEE emitter path
  (`ComputeMaterialEmissive` → `g.matEmissive` → `BuildStaticEmitterSet`) — **no**
  new light type, buffer, or dispatch. The nukage/lava `Le` is a **forced constant**
  (`kNukageLe`/`kLavaLe`) set by a flat-name allow-list, bypassing the conditional
  `derive_material_le`/`allowFaint` path so the glow is guaranteed and tunable,
  unlike the peak-gated lamp path.
- **INV-8:** The GI bake (`bake.comp`) never samples `ctrl[]` or the sheen/ripple
  branch, so the **view- and time-dependent** wet effects live only in the
  megakernel. The cast-light `Le` **does** enter the shared emitter set, so the
  baked bounce and the directly-traced glow agree (one `Le` source, no
  double-count).
- **INV-9:** Lava gets **glow + cast-light only** (L2). No sheen, no ripples — it
  reads molten, not wet (user 2026-07-17).

## 9. Alternatives considered

- **Do the whole thing now — true reflections via the GGX lobe + reflection
  rays.** Rejected for v1 (user chose "cheap wins first"): it is a much heavier,
  higher-risk capability that also unlocks reflective metal/floors engine-wide.
  Kept whole as **DOOM-0103**; the sheen here is the honest cheap stand-in.
- **Screen-space reflection (SSR) sheen.** Rejected for v1: needs SSR infra
  (a scoped-SSR pass), which is DOOM-0170/DOOM-0103 territory. A direct-light
  Blinn-Phong sheen needs none and still reads wet.
- **ReSTIR for the emissive liquid.** Rejected as overkill for a handful of pools
  (the research verdict, 2026-07-17): plain NEE with the guaranteed `Le` handles a
  few emitters fine. Parked far-future as **DOOM-0192** for a many-emitter scene.
- **Detect liquid by the albedo-green guess (current code).** Rejected as the
  trigger: mis-fires on green walls and cannot separate the source pool from a
  painted puddle. Replaced by the name-based `MatCtrl.flags` bit (§4.2).
- **A hand-authored wet normal-map texture for the nukage.** Considered (the
  roadmap floated it). Rejected for v1: an asset to make/store, and it would not
  animate without extra work. The procedural ripple (§4.5) needs no art and moves
  for free. Revisit if procedural reads flat (Q1).

## 10. Open questions

- **Q1 (ripple look):** exact wave construction (count of scrolling layers,
  directions, world scales) and amplitude — a play-test tune (L4). Fall back to a
  hand-authored wet normal texture (§9) only if procedural reads flat.
- **Q2 (sheen light source):** dominant light (flashlight/muzzle) vs summing over
  all NEE-sampled lights. Start dominant; widen if sparse (L3).
- **Q3 (puddle integration):** expose the DOOM-0181 goo-stain mask out of
  `applyGrime`, or re-derive the goo test in the mode block? And does the puddle
  sheen read wet or floaty? — decided at L5; the layer drops cleanly if it fails.
- **Q4 (toggle key):** which key drives `rb_wet` (the `]`/`[` family is taken by
  de-tile/filth) — pick a free, memorable key at implementation.
- **Q5 (lava shimmer):** user set lava as molten/no-wet for v1; revisit a subtle
  heat-shimmer (a slow, low-amplitude ripple variant) if the flat lava reads dead.
- **Q6 (perf gate):** the ≤ 5 % bar (§6) is inherited from DOOM-0181; confirm it
  holds on a *lava*-heavy scene too, not just the goo room, if one is available.
- **Q7 (tonemap headroom):** verify the sheen glint reads bright without clipping
  to a flat white disc under the PBR-Neutral tonemap (the tracer's first specular
  term — no prior art in this engine to inherit a strength from).
- **Q8 (stale comment cleanup):** the `pathtrace.comp:254` comment calls `misc2.z`/
  `.w` "reserved", but they carry the muzzle-flash strobe / flashlight toggle
  (`:405`/`:432`). Fix that one-line comment while adding `misc6` so the next reader
  isn't misled the same way (a trivial code cleanup, done during implementation).
