# DOOM-0181 — De-tiled, grimy Ultra surfaces

**Status:** Draft — pre-`/cold-eyes`. Design contract for stochastic de-tiling +
filth on HD (`usePBR`) surfaces in the Ultra ray-traced view.

**Depends on:**
- **DOOM-0042** (Ultra HD PBR materials) — provides the HD material set this
  operates on: the `ctrl[]` control SSBO, the bindless `hdTex[]` array, the
  `usePBR` gate, the shared `sUV` sampling pipeline (`hdBaseUV` → optional
  `hdParallaxUV`), and the `hdAlbedo/hdShadingNormal/hdAO` samplers
  (`pathtrace.comp`: `ctrl[]` SSBO L79, `hdBaseUV` L115, `hdParallaxUV` L148,
  `hdAlbedo` L179, `hdShadingNormal` L192, `hdAO` L203).
- **DOOM-0009** (path tracer) — the RT back-end. This hooks the primary hit in
  `pathtrace.comp` **mode 4** (NEE display, `else if (mode == 4u)` at L620, hit
  shading ~L637–649) and **mode 6** (denoised play path, `else if (mode == 6u)`
  at L756, hit shading ~L762–784).

**Extends / completes:**
- **DOOM-0179** (world-position grime overlay, in-progress) — becomes the
  **filth** layer here (§4.3). De-tiling (§4.2) is the primary anti-repeat
  mechanism the grime multiply alone could not deliver (user play-test
  2026-07-14: "still a very, very tiled look"). This spec **supersedes
  DOOM-0179's play-test gate**: DOOM-0179 stays 🚧 until **DOOM-0181 ships** (§7's
  L5 passes and the look is user-accepted — the point at which the filth layer
  lands), then graduates to ✅ + CHANGELOG. Merging this spec does not flip it.
  DOOM-0179's ROADMAP bullet is updated to name this new dependency when the spec
  is adopted (the bullet today predates it).

**Defers:**
- Grime-driven roughness / gloss smears (dirt goes matte, worn edges stay
  shiny) — depends on **DOOM-0103** (GGX specular lobe) being consumed by the
  primary shading, which it is **not** today (the tracer shades with albedo +
  normal + AO only; verified 2026-07-16). Picked up when specular lands.

**Scope:** Ultra RT view only (`pathtrace.comp` modes 4 + 6). Classic, the raster
stack (Solid, **and Ultra with RT off**), and all paletted (non-`usePBR`) surfaces
stay **byte-identical** to today.

---

## Contents

- §1 Goal — §2 Where this sits — §3 The problem, precisely — §4 Design
  (4.1 hook · 4.2 de-tiling · 4.3 filth · 4.4 POM interaction) —
  §5 Data & resources — §6 Performance budget — §7 Build order (L1–L5) —
  §8 Invariants — §9 Alternatives considered — §10 Open questions

---

## 1. Goal

Kill the "extremely tiled" look on HD walls and floors in the Ultra ray-traced
view, and make E1M1 read as a **filthy, monster-overrun base** rather than the
uniform clean surface it is today. Do it while keeping the DOOM feel and holding
performance — the de-tiling overhead is measured against the *current RT-on Ultra
frame rate* and must stay small (§6).

Two mechanisms, layered:

- **De-tiling (§4.2)** — break the visible repetition of a texture *across a
  single surface* and *between surfaces*.
- **Filth (§4.3)** — grime that reads as *neglect*: darker matte stains pooling
  in crevices, a faint grimy tint; tunable from "lived-in" to "abandoned."

*Terms:* **grunge** = the loaded overlay texture (`misc5.x`); **grime** = today's
`applyGrime` brightness multiply (`kGrimeStrength`); **filth** = this spec's
enrichment of grime (§4.3: darken + desaturate + crevice + tint); **de-tiling** =
the §4.2 anti-repeat mechanism, independent of all three. (Heads-up: the existing
code names the overlay *texture* itself "grime" — `hdGrungeIdx`, the "grime
overlay id" printf — so grepping "grime" in the source finds what this spec calls
"grunge.")

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0181? |
|-----------------|----------|-----------------------|
| Classic | paletted software | No |
| Solid, or Ultra with RT **off** | raster stack (DOOM-0170) | No |
| Ultra with RT **on** (`rb_rtdebug` 4/6) | path tracer | **Yes** — HD `usePBR` surfaces only |

The gate is *RT engaged* (path-trace modes 4/6), **not** the tier label: Ultra
with ray tracing toggled off is the DOOM-0170 raster performance mode and is left
untouched, exactly like Solid. The de-tiling and filth apply identically in mode
4 (the NEE display path) and mode 6 (the shipped denoised play path). Mode 6
factors albedo into `galbedo` before the SVGF store, so any change to the sampled
albedo/normal here rides into the denoised image exactly as the existing HD
samples do.

The offline GI bake (`bake.comp`) does **not** sample the HD material path at all
(it shares only `pt_common.glsl`; it never calls `hdAlbedo`/`applyGrime`). De-tiling
lives entirely inside the HD fetch, so it cannot affect the bake — there is no
divergence to reconcile.

## 3. The problem, precisely

There are **two** kinds of repetition, and they need different tools:

1. **Within-surface (dominant).** One wall shows the same tile many times
   horizontally; one floor shows an N×M grid of identical tiles. This is the
   user's complaint — the green-goo room's wall and the start-area perimeter
   wall both read as "extremely tiled" (user play-test, 2026-07-14).
2. **Between-surface.** Two *different* walls sharing a texture look like clones.

A **per-surface** offset/rotation fixes only #2 and does nothing for #1 (shifting
a whole floor by one offset leaves it tiling internally on the same grid). The
grime multiply (DOOM-0179) camouflages #1 only weakly. Even the current grime
strength (`kGrimeStrength = 0.32`, `pathtrace.comp:108`) — a ±32 % swing at the
map's extremes, far less on its soft mid-tones — cannot hide sharp detail
repeating on a hard grid. Neither clears an *extreme* #1. De-tiling (§4.2) attacks
#1 head-on, and because it is **world-keyed** it clears #2 for free (§4.2, INV-4).

## 4. Design

### 4.1 Where it hooks

At the primary hit in modes 4 & 6, after `id`, `mc = ctrl[id]`, `hitUV`, and the
surface UV are resolved. Today the HD path computes
`baseUV = hdBaseUV(id, hitUV, mc.uvScale)`, optionally marches it with
`hdParallaxUV` (POM) to `sUV`, then every HD map samples at `sUV`
(`hdAlbedo/hdShadingNormal/hdAO` → `texture(hdTex[m], sUV)`).

DOOM-0181 interposes a **de-tiling fetch** that replaces the direct
`texture(hdTex[m], sUV)` for the HD maps, and enriches the existing `applyGrime`
into the filth layer. No change to the paletted (`usePBR == 0`) branch.

**Precondition — the `hitP` hoist (L1).** The de-tile cell needs the world hit
point `hitP` (§4.2), currently computed *after* the HD map fetches
(`pathtrace.comp:648` mode 4 / `:781` mode 6). `hitP` must be **hoisted above the
`hdBaseUV`/`hdParallaxUV`/`hdAlbedo` block** — `tHit`/`hitP` come free from the ray
query (`rayQueryGetIntersectionTEXT`) before shading and have **no dependency on
`sUV`**, so the hoist is trivial. This is an L1 precondition.

The filth term needs a *separate* AO sample, but that is an **L4** concern, not L1:
`hdAO(mc, id, sUV)` takes `sUV` as input, so it cannot move above the block that
computes `sUV` — it is instead sampled right after `sUV`/`hdAlbedo` and before the
filth call (§4.3, §7 L4).

### 4.2 De-tiling — stochastic tile-blend (world-keyed, no rotation)

The variation grid is defined in **world space**, so one coordinate system drives
both the cell boundaries and the random seed — there is no texture-space vs
world-space mismatch. For each cell we deterministically vary that copy of the
texture, then blend across cell borders so the boundary is hidden in the common
case (the one exception — a POM march crossing a boundary — is handled in §4.4).

- **Cell & seed (one world grid).** Take the dominant-axis projection of the hit
  point `hitP` — the same projection `applyGrime` uses (z-up DOOM: floors/ceilings
  → `hitP.xy`, ±x walls → `hitP.yz`, ±y walls → `hitP.xz`) — into a 2-D world
  coordinate `w`, and quantise it by a world-unit cell size `kDetileWorldCell`:
  `cell = floor(w / kDetileWorldCell)`, `f = fract(w / kDetileWorldCell)`. The
  per-cell hash is a small `vec3 hash(ivec2 cell)` **wrapper built around the
  existing `pcgHash` primitive** (`pt_common.glsl:75`) — reuse that PRNG, do not
  invent a new algorithm. Recipe: fold the cell into one seed
  `s = pcgHash(uint(cell.x)*0x9E3779B1u ^ uint(cell.y)*0x85EBCA77u)`, then expand
  to three decorrelated channels
  `vec3(pcgHash(s), pcgHash(s+1u), pcgHash(s+2u)) * (1.0 / 4294967296.0)` (the
  `1.0/4294967296.0` divisor is the existing `rnd()` idiom in `pt_common.glsl`, not
  a new one). (The
  distinct odd multipliers before the XOR are what stop adjacent cells from
  correlating — the load-bearing detail behind INV-4.) Cell and seed can never
  disagree because both come from this one grid. World-keying is the crux:
  (a) adjacent cells across one wall differ (breaks #1) and (b) cells on different
  walls at different world coordinates differ (breaks #2) — no per-surface ID
  needed. Cells need **not** align to the texture's own repeat; the border blend
  below hides the boundary wherever it falls, exactly as the Inigo-Quilez
  stochastic-tiling method does on arbitrary content (the Heitz–Neyret variant is
  the rejected §9 alternative, not this one).
  *Starting values* (all tuned per §10 Q2): `kDetileWorldCell` = 96 units,
  `mirrorProb` = 0.5, `kDetileOffsetMag` = 0.5 (→ ±0.5 tile).
- **Per-cell transform.** Applied to the sampling coordinate (the `baseUV` /
  POM-marched `sUV` fed to the map fetches): a sub-tile UV **offset**, **centred**
  so it is signed — `(hash(cell).xy - 0.5) * 2.0 * kDetileOffsetMag` (a raw
  `hash(cell).xy` would only ever shift positive) — plus an optional **horizontal
  mirror** — flip U *within the
  cell*, reflecting the fractional coordinate about the cell centre, when
  `hash(cell).z > mirrorProb`. **No rotation, no vertical flip** — DOOM wall
  textures are vertically oriented (panel lines, rivets), so only
  orientation-preserving transforms are allowed (INV-1).
- **Seam handling.** The Inigo-Quilez 4-corner blend: a per-pixel weighted blend
  of the (up to 4) neighbouring cell variants by `f` — **not** an edge-only band.
  4 taps; a 2-tap mode is the perf dial (§6). The blend is a **per-map wrapper**
  around each `texture(hdTex[m], …)` call — it does **not** mutate the one shared
  `sUV`, so a map that has not yet been wrapped (an L1/L2 interim build) keeps
  reading the plain coordinate. It wraps the **non-height map fetches**
  (albedo/normal/AO); the POM **height march is not per-step blended** (that would
  cost taps × march-steps — see §4.4). Which maps carry the wrapper at each build
  layer is set in §7 (albedo from L1, normal/AO from L2, height/POM from L3).
- **Map consistency.** Albedo, normal, AO, and height use the **same** per-cell
  hash/transform, sampled at the same de-tiled coordinate, so they stay registered
  (INV-3). On a horizontal mirror the sampled tangent-space normal's **X is
  negated**, and the POM march's tangent-space X is negated in lockstep (§4.4), so
  lit relief and parallax depth do not invert (INV-2).
- Applies to HD (`usePBR`) maps only, via the `usePBR` gate (INV-5, INV-8);
  sprites never carry `usePBR` materials in v1, so the same gate excludes them.
- *Assumption:* the dominant-axis projection is exact for DOOM's axis-aligned
  walls/flats; on a rare diagonal linedef it is approximate — the de-tile still
  varies and stays registered (no crash, no INV-3/4 break), it just is not
  axis-perfect. Inherited from DOOM-0179's grime projection.

### 4.3 Filth — grime as neglect (completes DOOM-0179)

Keep the world-space grunge sample (the `misc5.x` overlay, DOOM-0179), but
enrich the blend from a pure brightness multiply into *dirt*. `applyGrime` gains
an AO input for the crevice term — new signature
`applyGrime(albedo, hitP, n, ao)`:

- **Darken + desaturate** in grimy areas (dirt is matte and dark), not just a
  symmetric brightness wobble — the blend is biased toward darkening (neglect),
  within a clamped floor so nothing goes black (INV-6).
- **Crevice pooling.** Multiply the grime darkening by `(1 - AO)`, so corners and
  recesses get dirtier — dirt collects where nothing wipes it. **The `hdAO` sample
  must be hoisted ahead of the filth call:** today `hdAO` is fetched later, at the
  ambient term (`pathtrace.comp:690` mode 4, `:839` mode 6), *after* `applyGrime`
  (`:649` / `:784`) — so the ambient-term call is moved **up** to before the filth
  call, and the ambient term then reuses that one value: a single (de-tiled) AO
  fetch per hit, not two (INV-7). Open surfaces (AO ≈ 1) still take the base grime
  (INV-7).
- **Faint grimy tint.** A fixed muted colour (grease/rust/mould), low weight, so
  it is not merely "darker grey."
- Applied **after** de-tiling, so filth sits on the de-tiled albedo.
- **Missing overlay.** `applyGrime` today early-returns when no grunge overlay is
  loaded (`gid == 0xFFFFFFFF`). The crevice-AO darkening and the tint do **not**
  need the grunge texture, so they still apply in that case — the early-return is
  replaced by a path that skips only the grunge *sample* while keeping crevice +
  tint.
- **Dials:** `kGrimeStrength` (existing), `kGrimeCrevice` (AO coupling weight),
  `kGrimeTint` (colour + weight). Tunable from lived-in to abandoned.

### 4.4 POM interaction

POM (`hdParallaxUV`) marches the sampling coordinate along the height field per
pixel; de-tiling also transforms that coordinate. The de-tile **cell is computed
from world position (`hitP`), independent of POM** (§4.2), so the two never fight
over which cell a pixel is in.

**Order and cost.** Compute the per-cell offset/mirror **once** and apply it to
`baseUV` (a single de-tiled coordinate for the pixel), *then* run the POM march on
that de-tiled coordinate using the de-tiled height map. The march is **not**
4-corner-blended per step — it uses the pixel's single dominant cell — so POM cost
stays ~unchanged (the blend's `taps×` multiplier applies only to the non-height
map fetches, §4.2). Parallax relief, albedo, and normal then all come from the same
transformed tile. On a mirrored cell the march's tangent-space X is negated with
the normal's (INV-2).

**Materials with no height map** (`mc.maps[6] < 0`) skip §4.4 entirely:
`hdParallaxUV` already early-returns `baseUV`, so the de-tiled `baseUV` is sampled
directly. Because each pixel's march uses a single cell (its whole march stays in
that cell's transform), two adjacent pixels near a cell boundary can resolve to
different cells' transforms and show a faint relief seam there — the non-height-map
border blend does not cover the march. **Fallback if it shows:** inside a border
band around each cell edge (start: within `0.1` of `f`'s 0/1 extremes on either
axis) skip the parallax march and sample the de-tiled (offset+mirror) `baseUV`
there — *not* the pre-de-tile coordinate. Play-test call (§10 Q1).

## 5. Data & resources

- **No new GPU buffers, descriptor bindings, or images.** Reuses the existing
  `hdTex[]` (including the DOOM-0179 grunge overlay via `misc5.x`), the `ctrl[]`
  SSBO, and the dominant-axis world projection.
- **New tuning knobs.** De-tile offset magnitude, mirror probability
  (`mirrorProb`), and world cell size (`kDetileWorldCell`); filth crevice + tint
  weights. (The 4-corner blend has no width knob — its weights come from `f`, the
  fractional position in the cell.) Compile-time `const`s to start (like
  `kGrimeStrength`/`kGrimeWorldScale`).
- **Runtime quality/strength dial.** Use a currently-reserved `misc5` lane
  (`misc5.y` = de-tile quality/enable: 0 = off, 1 = 2-tap, 2 = 4-tap; any other
  value is treated as off) so the effect can be toggled/tuned and profiled without
  a shader recompile. The
  "4-tap albedo + cheaper other maps" split (§6) is a **compile-time** variant,
  not a separate `misc5.y` value. This lane is already carried on the mode-5
  verify struct's `misc5` padding (DOOM-0179), so `-rtverify` is unaffected
  (INV-9).

## 6. Performance budget

- **Baseline (measure first, at L5).** The reference is the *current* RT-on E1M1
  Ultra frame rate — path tracer, modes 4/6 — at 50 % render scale with flashlight,
  measured **before** de-tiling is enabled. This is **not** the ~65 FPS DOOM-0170
  raster performance-mode number — that is the RT-*off* raster stack, a different
  and cheaper workload. The RT-on megakernel runs lower, so the gate below is
  *relative* to whatever this measured baseline is, not to an absolute FPS. Profiled
  via the `\` key (`rb_profile`), which prints both the CPU build/frame timings
  (`[cpu_profile]`/`[cpu_build]`) and the GPU per-pass timings.
- **De-tiling cost — non-height maps:** up to `(taps − 1)×` extra `hdTex` fetches
  for each non-height HD map that carries the blend (albedo, normal, AO). GI
  bounces are baked (§2) and never sample HD materials, so this is paid once per
  pixel, at the primary hit. 4-tap = up to 4× *total* fetches per blended map
  (3× extra).
- **De-tiling cost — height/POM:** ~unchanged. The de-tile offset/mirror is
  applied once before the march (§4.4); the march is not per-step blended, so it
  does **not** pay the `taps×` multiplier.
- **Filth cost:** a few ALU ops on already-sampled values — not separately
  profiled; folded into the L5 perf pass.
- **Gate (evaluated at L5 only).** L1–L4 are visual play-test only, with **no FPS
  gate** — FPS numbers there are diagnostic. The single L5 pass/fail line: **4-tap
  de-tiling must add ≤ 5 % to the average frame time** (measured in ms —
  `[cpu_profile]` present-total — not FPS) **vs the measured RT-on baseline above.**
  If it costs more, fall back to 2-tap via the `misc5.y` dial, or a cheaper
  compile-time split (§10 Q4); once a fallback is selected it is accepted as the
  shipped quality (the fallback is cheaper than 4-tap by construction, so it needs
  no separate budget check). **Advisory, non-blocking:** de-tiling should avoid
  being the change that drops RT-on Ultra below DOOM-0012's aspirational 60 FPS
  floor (💭 considered — not yet an adopted commitment), but that floor is not this
  feature's gate — the only L5 pass/fail is the ≤ 5 % rule above. Measure with the
  `\` profiler.

## 7. Build order

Each layer is independently play-testable; stop and get user acceptance per
layer (renderer look is a play-test call). Only **L5** gates ship on FPS (§6);
L1–L4 FPS numbers are diagnostic.

| Layer | Scope | Verify | FPS-gate? |
|-------|-------|--------|-----------|
| **L1** | De-tile **albedo** (offset + mirror + world hash + 4-corner blend); hoist `hitP` (§4.1) | Green-goo + perimeter walls no longer read as repeated; same-texture walls differ | no |
| **L2** | Extend the same transform + blend to **normal, AO** (INV-2 normal-X negate) | Relief/lighting stay registered with albedo; no colour-vs-relief mismatch | no |
| **L3** | De-tile **height** + fold POM march into de-tiled space (§4.4, mirror-march negate) | POM relief agrees with de-tiled albedo/normal; no boundary artefacts (else §4.4 fallback) | no |
| **L4** | **Filth**: hoist `hdAO` (§4.1/§4.3), crevice coupling + tint + darken/desat | Reads as neglect not just dark; dials behave; stays within the INV-6 clamp | no |
| **L5** | Runtime dial (`misc5.y`) + perf pass | 4-tap adds ≤ 5 % frame time vs measured RT-on baseline (§6); dial steps 0/1/2 = off→2-tap→4-tap; `-rtverify` green | **yes** |

Height is de-tiled in **L3, not L2**: its only consumer is the POM march, which is
not wired into de-tiled space until L3, so de-tiling it earlier would be
unobservable.

**DOOM-0181 ships** — and, with it, the filth layer graduates DOOM-0179 — when L5
passes the §6 gate and the look is user-accepted.

## 8. Invariants

- **INV-1:** De-tile transform set = {sub-tile offset, horizontal mirror} only.
  No rotation, no vertical flip (preserves DOOM's vertical texture orientation).
- **INV-2:** On a horizontal mirror, the sampled tangent-space normal's X
  component **and** the POM parallax march's tangent-space X are negated in
  lockstep, so lit relief and parallax depth do not invert.
- **INV-3:** In the **final (L3+) state**, all HD maps of one hit share one
  de-tile transform (registration). L1/L2 interim builds de-tile only a subset
  (albedo at L1; +normal/AO at L2), so registration between de-tiled and
  not-yet-de-tiled maps is knowingly broken in those interim builds — expected,
  not a regression; full registration holds from L3.
- **INV-4:** The per-cell hash is seeded by **world position**, not by the
  texture-space cell index alone — otherwise two walls both starting at cell 0
  would clone.
- **INV-5:** The paletted / non-`usePBR` path is unchanged (no de-tile, no
  enriched filth).
- **INV-6:** Filth only darkens/tints within the existing grime clamp
  (`clamp(m, 0.35, 1.65)`, `pathtrace.comp:445`), biased toward the dark end; it
  never brightens a surface beyond that ceiling (it must always read as dirt).
- **INV-7:** Crevice coupling uses the same AO the ambient term uses; filth still
  applies at base strength where AO ≈ 1 (open surfaces).
- **INV-8:** Ultra RT only; modes 4 and 6 get identical treatment. Classic, the
  raster stack (Solid, **and Ultra with RT off**), and paletted surfaces stay
  byte-identical.
- **INV-9:** `-rtverify` (mode 5) is unaffected — no push-constant layout change
  (the `misc5` lane used for the runtime dial already exists as padding on the
  mode-5 verify struct, DOOM-0179).

## 9. Alternatives considered

- **Macro grime only (DOOM-0179 turned up).** Rejected as the primary fix: the
  user play-tested it and it did not clear the tiling; a low-frequency brightness
  drift cannot hide sharp detail repeating on a hard grid. Kept as the
  *complementary* filth layer (§4.3), not the anti-repeat mechanism.
- **Bigger tiles (raise `uvScale`).** Rejected: fewer repeats but blurrier
  surfaces, and the longest walls still repeat visibly.
- **Per-surface offset/rotation.** Rejected: fixes only the between-surface clone
  case (#2), not the dominant within-surface grid (#1).
- **Full Heitz–Neyret histogram-preserving hex-tiling** (precomputed per-texture
  transform + inverse-histogram LUT). Rejected for v1: needs an offline
  precompute pass and extra VRAM per material, for a quality gain the lite
  offset-+-mirror-+-blend variant mostly captures at lower cost and complexity.
  Revisit if the lite version blurs objectionably.

**Technique references** (for the implementer): the chosen blend is Inigo Quilez,
"Texture repetition" (iquilezles.org/articles/texturerepetition/) — variant 3, the
per-tile-hash 4-corner blend. The rejected full method is Heitz & Neyret,
"High-Performance By-Example Noise using a Histogram-Preserving Blending Operator"
(HPG 2018).

## 10. Open questions (play-test)

- **Q1 (POM):** POM-in-de-tiled-space vs POM-off inside the blend band — which
  looks cleaner at cell boundaries?
- **Q2 (aggressiveness):** mirror probability, offset magnitude, and cell size
  (`kDetileWorldCell`) — how far before it looks unnatural on oriented textures?
- **Q3 (filth level):** "lived-in" vs "abandoned" as the shipped default.
- **Q4 (perf/quality):** 4-tap everywhere vs 4-tap albedo + cheaper rest.
