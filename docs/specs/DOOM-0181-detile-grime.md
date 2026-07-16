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
  2026-07-14: "still a very, very tiled look"). **DOOM-0179 graduates only once
  §7's L4 (Filth) ships and clears its own play-test** — merging this spec does
  not flip it; it defers to DOOM-0179's existing play-test-then-CHANGELOG gate.

**Defers:**
- Grime-driven roughness / gloss smears (dirt goes matte, worn edges stay
  shiny) — depends on **DOOM-0103** (GGX specular lobe) being consumed by the
  primary shading, which it is **not** today (the tracer shades with albedo +
  normal + AO only; verified 2026-07-16). Picked up when specular lands.

**Scope:** Ultra RT view only (`pathtrace.comp` modes 4 + 6). Classic, the Solid
raster stack, and all paletted (non-`usePBR`) surfaces stay **byte-identical** to
today.

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
uniform clean surface it is today. Do it while keeping the DOOM feel and the
**60 FPS floor** — measured against the ~65 FPS E1M1 baseline from the DOOM-0119
perf split.

Two mechanisms, layered:

- **De-tiling (§4.2)** — break the visible repetition of a texture *across a
  single surface* and *between surfaces*.
- **Filth (§4.3)** — grime that reads as *neglect*: darker matte stains pooling
  in crevices, a faint grimy tint; tunable from "lived-in" to "abandoned."

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0181? |
|-----------------|----------|-----------------------|
| Classic | paletted software | No |
| Solid, or Ultra with RT **off** | raster stack (DOOM-0170) | No |
| Ultra with RT **on** (`rb_rtdebug` 4/6) | path tracer | **Yes** — HD `usePBR` surfaces only |

The gate is *RT engaged* (path-trace modes 4/6), **not** the tier label: Ultra
with ray tracing toggled off is the DOOM-0170 raster performance mode and is left
untouched, exactly like Solid. The de-tiling and filth apply identically in mode
4 (the NEE display path) and
mode 6 (the shipped denoised play path); mode 6 factors albedo into `galbedo`
before the SVGF store, so any change to the sampled albedo/normal here rides
into the denoised image exactly as the existing HD samples do.

## 3. The problem, precisely

There are **two** kinds of repetition, and they need different tools:

1. **Within-surface (dominant).** One wall shows the same tile many times
   horizontally; one floor shows an N×M grid of identical tiles. This is the
   user's complaint — the green-goo room's wall and the start-area perimeter
   wall both read as "extremely tiled" (screenshots, session 2026-07-14).
2. **Between-surface.** Two *different* walls sharing a texture look like clones.

A **per-surface** offset/rotation fixes only #2 and does nothing for #1 (shifting
a whole floor by one offset leaves it tiling internally on the same grid). The
grime multiply (DOOM-0179) camouflages #1 only weakly — even the current grime
strength (`kGrimeStrength = 0.32`, `pathtrace.comp:108` — a ±32 % swing at the
map's extremes but far less on its soft mid-tones) cannot hide sharp detail
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

### 4.2 De-tiling — stochastic tile-blend (world-keyed, no rotation)

The variation grid is defined in **world space**, so one coordinate system drives
both the cell boundaries and the random seed — there is no texture-space vs
world-space mismatch. For each cell we deterministically vary that copy of the
texture, then blend across cell borders so the boundary never shows.

- **Cell & seed (one world grid).** Take the dominant-axis projection of the hit
  point `hitP` — the same projection `applyGrime` uses (z-up DOOM: floors/ceilings
  → `hitP.xy`, ±x walls → `hitP.yz`, ±y walls → `hitP.xz`) — into a 2-D world
  coordinate `w`, and quantise it by a world-unit cell size `kDetileWorldCell` (a
  tunable constant on the order of one texture tile, e.g. 96 units):
  `cell = floor(w / kDetileWorldCell)`, `f = fract(w / kDetileWorldCell)`. The
  per-cell hash is `hash(cell)`. Cell and seed can never disagree because both
  come from this one grid (INV-4). World-keying is the crux: (a) adjacent cells
  across one wall differ (breaks #1) and (b) cells on different walls at different
  world coordinates differ (breaks #2) — no per-surface ID needed. Cells need
  **not** align to the texture's own repeat; the border blend below hides the
  boundary wherever it falls, exactly as the Inigo-Quilez / Heitz stochastic-
  tiling methods do on arbitrary content.
- **Per-cell transform.** Applied to the sampling coordinate (the `baseUV` /
  POM-marched `sUV` fed to the map fetches): a sub-tile UV **offset**
  (`hash(cell).xy`) plus an optional **horizontal mirror** (flip U when
  `hash(cell).z > mirrorProb`). **No rotation, no vertical flip** — DOOM wall
  textures are vertically oriented (panel lines, rivets), so only
  orientation-preserving transforms are allowed (INV-1).
- **Seam handling.** Blend the current cell's variant with its neighbours across
  the border using smooth weights of `f` — the Inigo-Quilez 4-corner blend (4
  taps); a 2-tap mode is the perf dial (§6). Which maps carry the blend at each
  build layer is set in §7 (albedo from L1, all HD maps from L2).
- **Map consistency.** Albedo, normal, AO, and height use the **same** per-cell
  hash/transform, sampled at the same de-tiled coordinate, so they stay registered
  (INV-3). On a horizontal mirror the sampled tangent-space normal's **X is
  negated**, and the POM march's tangent-space X is negated in lockstep (§4.4), so
  lit relief and parallax depth do not invert (INV-2).
- Applies to HD (`usePBR`) maps only; **sprites and paletted surfaces are
  excluded** (INV-5, INV-8).
- *Assumption:* the dominant-axis projection is exact for DOOM's axis-aligned
  walls/flats; on a rare diagonal linedef it is approximate — the de-tile still
  varies and stays registered (no crash, no INV-3/4 break), it just is not
  axis-perfect. Inherited from DOOM-0179's grime projection.

### 4.3 Filth — grime as neglect (completes DOOM-0179)

Keep the world-space grunge sample (the `misc5.x` overlay, DOOM-0179), but
enrich the blend from a pure brightness multiply into *dirt*:

- **Darken + desaturate** in grimy areas (dirt is matte and dark), not just a
  symmetric brightness wobble — the blend is biased toward darkening (neglect),
  within a clamped floor so nothing goes black (INV-6).
- **Crevice pooling.** Multiply the grime darkening by `(1 - AO)`, so corners and
  recesses get dirtier — dirt collects where nothing wipes it. **The `hdAO` sample
  must be hoisted ahead of the filth call:** today `hdAO` is fetched later, at the
  ambient term (`pathtrace.comp:690` mode 4, `:839` mode 6), *after* `applyGrime`
  (`:649` / `:784`) — so the implementation moves (or reuses) that fetch before the
  filth call. Use the **de-tiled** AO (INV-3). Open surfaces (AO ≈ 1) still take
  the base grime (INV-7).
- **Faint grimy tint.** A fixed muted colour (grease/rust/mould), low weight, so
  it is not merely "darker grey."
- Applied **after** de-tiling, so filth sits on the de-tiled albedo.
- **Dials:** `kGrimeStrength` (existing), `kGrimeCrevice` (AO coupling weight),
  `kGrimeTint` (colour + weight). Tunable from lived-in to abandoned.

### 4.4 POM interaction

POM (`hdParallaxUV`) marches the sampling coordinate along the height field per
pixel; de-tiling also transforms that coordinate. The de-tile **cell is computed
from world position (`hitP`), independent of POM** (§4.2), so the two never fight
over which cell a pixel is in. Order: apply the per-cell offset/mirror to `baseUV`
first, then let POM march in that **de-tiled** coordinate space using the de-tiled
height map — so parallax relief, albedo, and normal all come from the same
transformed tile. On a mirrored cell the march's tangent-space X is negated with
the normal's (INV-2). **Materials with no height map** (`mc.maps[6] < 0`) skip
§4.4 entirely: `hdParallaxUV` already early-returns `baseUV` there, so the de-tiled
`baseUV` is sampled directly. The one risk is a POM march crossing a cell boundary
mid-step; the §4.2 border blend covers the common case. **Fallback if artefacts
show:** skip the parallax march inside the blend band and sample the de-tiled
(offset+mirror) `baseUV` there — *not* the pre-de-tile coordinate. Play-test call
(§10 Q1).

## 5. Data & resources

- **No new GPU buffers, descriptor bindings, or images.** Reuses the existing
  `hdTex[]` (including the DOOM-0179 grunge overlay via `misc5.x`), the `ctrl[]`
  SSBO, and the dominant-axis world projection.
- **New tuning knobs.** De-tile offset magnitude, mirror probability, and
  border-blend width; filth crevice + tint weights. Compile-time `const`s to
  start (like `kGrimeStrength`/`kGrimeWorldScale`).
- **Runtime quality/strength dial.** Use a currently-reserved `misc5` lane
  (`misc5.y` = de-tile quality/enable: 0 = off, 1 = 2-tap, 2 = 4-tap) so the
  effect can be toggled/tuned and profiled without a shader recompile. The
  "4-tap albedo + cheaper other maps" split (§6) is a **compile-time** variant,
  not a separate `misc5.y` value. This lane is already carried on the mode-5
  verify struct's `misc5` padding (DOOM-0179), so `-rtverify` is unaffected
  (INV-9).

## 6. Performance budget

- **Baseline:** E1M1 Ultra, 50 % render scale, flashlight ≈ **65 FPS**
  (post-DOOM-0119 perf split). Profiled via the `\` key (`rb_profile`), which
  prints both the CPU build/frame timings (`[cpu_profile]`/`[cpu_build]`) and the
  GPU per-pass timings.
- **De-tiling cost:** up to `(taps − 1)×` extra `hdTex` fetches **on the primary
  hit only** (not secondary bounces), for each HD map used in shading (albedo,
  normal, AO, height). 4-tap = up to 4× those primary-hit fetches. **Budget: hold
  ≥ 62 FPS at 4-tap** at the baseline settings (≤ ~5 % off the 65 FPS baseline).
  **If a build layer drops below the 60 FPS floor** (hard gate — < 60 fails),
  fall back to 2-tap via the `misc5.y` dial, or to the compile-time
  4-tap-albedo + 1-tap-other-maps split.
- **Filth cost:** negligible — a few ALU ops on values already sampled.
- Gate: must hold the **60 FPS floor (hard: ≥ 60 FPS)** at the baseline settings.
  Only the final **L5** measurement gates ship; earlier layers are diagnostic
  (§7). Measure with the `\` profiler before sign-off.

## 7. Build order

Each layer is independently play-testable; stop and get user acceptance per
layer (renderer look is a play-test call). Only **L5** gates ship on FPS (§6);
earlier layers are diagnostic.

- **L1 — De-tile albedo.** Offset + mirror + world-keyed hash + 4-corner border
  blend, **albedo map only**. *Verify:* the green-goo wall + perimeter wall no
  longer read as repeated; two same-texture walls differ. Play-test.
- **L2 — De-tile normal/AO/height.** Extend the *same* transform **and the same
  4-corner border blend** to the remaining HD maps, with the INV-2 normal-X negate
  on mirror — so no map ships with visible cell seams. *Verify:* relief and
  lighting stay registered with the albedo; no colour-vs-relief mismatch.
- **L3 — POM in de-tiled space.** Fold the parallax march into the de-tiled
  coordinate (§4.4), with the mirror-march negate. *Verify:* POM relief agrees
  with the de-tiled albedo/normal; no boundary artefacts. Apply the §4.4 fallback
  if needed.
- **L4 — Filth.** Hoist the `hdAO` fetch (§4.3), then crevice (AO) coupling + tint
  + darken/desaturate on top. *Verify:* reads as neglect, not just dark; dials
  behave; net exposure not too dark.
- **L5 — Runtime dial + perf pass.** `misc5.y` quality/strength; measure against
  the §6 floor. *Verify:* FPS within floor at baseline; quality knob works;
  `-rtverify` still green.

## 8. Invariants

- **INV-1:** De-tile transform set = {sub-tile offset, horizontal mirror} only.
  No rotation, no vertical flip (preserves DOOM's vertical texture orientation).
- **INV-2:** On a horizontal mirror, the sampled tangent-space normal's X
  component **and** the POM parallax march's tangent-space X are negated in
  lockstep, so lit relief and parallax depth do not invert.
- **INV-3:** All HD maps of one hit share one de-tile transform (registration).
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
- **INV-8:** Ultra RT only; modes 4 and 6 get identical treatment. Classic, Solid
  raster, and paletted surfaces stay byte-identical.
- **INV-9:** `-rtverify` (mode 5) is unaffected — no *required* push-constant
  layout change; the `misc5` lane used for the runtime dial already exists as
  padding on the mode-5 verify struct (DOOM-0179).

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

## 10. Open questions (play-test)

- **Q1 (POM):** POM-in-de-tiled-space vs POM-off inside the blend band — which
  looks cleaner at cell boundaries?
- **Q2 (aggressiveness):** mirror probability + offset magnitude — how far before
  it looks unnatural on oriented textures?
- **Q3 (filth level):** "lived-in" vs "abandoned" as the shipped default.
- **Q4 (perf/quality):** 4-tap everywhere vs 4-tap albedo + cheaper rest.
