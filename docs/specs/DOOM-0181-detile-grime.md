# DOOM-0181 — De-tiled, grimy Ultra surfaces

**Status:** Draft — pre-`/cold-eyes`. Design contract for stochastic de-tiling +
filth on HD (`usePBR`) surfaces in the Ultra ray-traced view.

**Depends on:**
- **DOOM-0042** (Ultra HD PBR materials) — provides the HD material set this
  operates on: the `ctrl[]` control SSBO, the bindless `hdTex[]` array, the
  `usePBR` gate, the shared `sUV` sampling pipeline (`hdBaseUV` → optional
  `hdParallaxUV`), and the `hdAlbedo/hdShadingNormal/hdAO` samplers
  (`pathtrace.comp` ~§160–220).
- **DOOM-0009** (path tracer) — the RT back-end. This hooks the primary hit in
  `pathtrace.comp` **mode 4** (NEE display, ~L637–649) and **mode 6** (denoised
  play path, ~L723–784).

**Extends / completes:**
- **DOOM-0179** (world-position grime overlay, in-progress) — becomes the
  **filth** layer here (§4.3). De-tiling (§4.2) is the primary anti-repeat
  mechanism the grime multiply alone could not deliver (user play-test
  2026-07-14: "still a very, very tiled look"). **On this spec landing,
  DOOM-0179 graduates** as the filth layer.

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
uniform clean surface it is today — while keeping the DOOM feel and the ~60 FPS
floor (measured against the ~65 FPS E1M1 baseline from the DOOM-0119 perf split).

Two mechanisms, layered:

- **De-tiling (§4.2)** — break the visible repetition of a texture *across a
  single surface* and *between surfaces*.
- **Filth (§4.3)** — grime that reads as *neglect*: darker matte stains pooling
  in crevices, a faint grimy tint; tunable from "lived-in" to "abandoned."

## 2. Where this sits

| Tier    | RT | Touched by DOOM-0181? |
|---------|----|-----------------------|
| Classic | –  | No (paletted software renderer)          |
| Solid   | off (raster) | No (raster stack, DOOM-0170)   |
| Ultra   | **on** (path tracer) | **Yes** — modes 4 & 6, HD `usePBR` surfaces only |

The de-tiling and filth apply identically in mode 4 (the NEE display path) and
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
grime multiply (DOOM-0179) camouflages #1 only weakly — a ~10 % brightness drift
cannot hide sharp detail repeating on a hard grid. Neither clears an *extreme*
#1. De-tiling (§4.2) attacks #1 head-on, and because it is **world-keyed** it
clears #2 for free (§4.2, INV-4).

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

### 4.2 De-tiling — stochastic per-tile variation ("hex-tiling-lite")

The repeat unit is one texture tile (integer cell of the tiling coordinate). For
each cell we deterministically vary that copy, then blend across cell borders so
the boundary never shows.

- **Cell & seed.** The cell index comes from the tiling coordinate (integer part
  of the pre-POM `baseUV`). The per-cell hash is seeded by the cell's **world
  position** — the dominant-axis projection of `hitP` already used by
  `applyGrime` (z-up DOOM: floors/ceilings → `hitP.xy`, ±x walls → `hitP.yz`,
  ±y walls → `hitP.xz`), quantised to the cell. World-keying is the crux: cells
  decorrelate by *where they are in the world*, so (a) adjacent cells on one wall
  differ (breaks #1) and (b) cells on different walls at different world
  coordinates differ (breaks #2) — with no per-surface ID needed.
- **Per-cell transform.** A sub-tile UV **offset** plus an optional **horizontal
  mirror** (flip U). **No 90° rotation, no vertical flip** — DOOM wall textures
  are vertically oriented (panel lines, rivets), so only orientation-preserving
  transforms are allowed (INV-1).
- **Seam handling.** Blend neighbouring cells' variants over a border band
  (smoothstep weight). Baseline is the Inigo-Quilez 4-corner blend (4 taps) on
  the maps that matter; a 2-tap cheaper mode is the perf dial (§6).
- **Map consistency.** Albedo, normal, AO, and height use the **same** per-cell
  hash/transform so they stay registered (INV-3). On a horizontal mirror the
  sampled tangent-space normal's **X component is negated** (a mirror flips
  handedness) so lit relief does not invert (INV-2).
- Applies to HD (`usePBR`) maps only (INV-5).

### 4.3 Filth — grime as neglect (completes DOOM-0179)

Keep the world-space grunge sample (the `misc5.x` overlay, DOOM-0179), but
enrich the blend from a pure brightness multiply into *dirt*:

- **Darken + desaturate** in grimy areas (dirt is matte and dark), not just a
  symmetric brightness wobble — the blend is biased toward darkening (neglect),
  within a clamped floor so nothing goes black (INV-6).
- **Crevice pooling.** Multiply the grime darkening by `(1 - AO)` using the AO
  already sampled by `hdAO`, so corners and recesses get dirtier — dirt collects
  where nothing wipes it. Open surfaces (AO ≈ 1) still take the base grime
  (INV-7).
- **Faint grimy tint.** A fixed muted colour (grease/rust/mould), low weight, so
  it is not merely "darker grey."
- Applied **after** de-tiling, so filth sits on the de-tiled albedo.
- **Dials:** `kGrimeStrength` (existing), `kGrimeCrevice` (AO coupling weight),
  `kGrimeTint` (colour + weight). Tunable from lived-in to abandoned.

### 4.4 POM interaction

POM (`hdParallaxUV`) marches the coordinate along the height field per pixel;
de-tiling transforms the coordinate per cell. To keep relief, colour, and normal
in agreement, **de-tiling is applied first** (compute the per-cell transform from
`baseUV`/world cell), and **POM then marches in the de-tiled coordinate space**
using the de-tiled height map — so the parallax relief you see and the de-tiled
albedo/normal all come from the same transformed tile. The only risk is a POM
march crossing a cell boundary mid-step; the §4.2 border blend covers the
common case. **Fallback if artefacts show:** disable POM inside the blend band
(fall back to the base coordinate there). Play-test call (§10 Q1).

## 5. Data & resources

- **No new GPU buffers, descriptor bindings, or images.** Reuses the existing
  `hdTex[]` (including the DOOM-0179 grunge overlay via `misc5.x`), the `ctrl[]`
  SSBO, and the dominant-axis world projection.
- **New tuning knobs.** De-tile offset magnitude, mirror probability, and
  border-blend width; filth crevice + tint weights. Compile-time `const`s to
  start (like `kGrimeStrength`/`kGrimeWorldScale`).
- **Runtime quality/strength dial.** Use a currently-reserved `misc5` lane
  (`misc5.y` = de-tile quality/enable, e.g. 0 = off, 1 = 2-tap, 2 = 4-tap) so
  the effect can be toggled/tuned and profiled without a shader recompile. This
  lane is already carried on the mode-5 verify struct's `misc5` padding
  (DOOM-0179), so `-rtverify` is unaffected (INV-9).

## 6. Performance budget

- **Baseline:** E1M1 Ultra, 50 % render scale, flashlight ≈ **65 FPS**
  (post-DOOM-0119 perf split; the profiler keys are `\` for CPU, `rb_profile`
  for GPU per-pass).
- **De-tiling cost:** up to `(taps − 1)×` extra `hdTex` fetches **on the primary
  hit only** (not secondary bounces), for each HD map used in shading (albedo,
  normal, AO, height). 4-tap = up to 4× those primary-hit fetches; expected
  single-digit-% FPS cost since primary-hit texture sampling is a small slice of
  the frame. **If over budget:** drop to 2-tap, or 4-tap albedo + 1-tap the rest,
  via the `misc5.y` dial.
- **Filth cost:** negligible — a few ALU ops on values already sampled.
- Gate: must hold the ~60 FPS floor at the baseline settings; measure each build
  layer with the profilers before sign-off.

## 7. Build order

Each layer is independently play-testable; stop and get user acceptance per
layer (renderer look is a play-test call).

- **L1 — De-tile albedo.** Offset + mirror + world-keyed hash + 4-corner blend,
  albedo map only. *Verify:* the green-goo wall + perimeter wall no longer read
  as repeated; two same-texture walls differ. Play-test.
- **L2 — De-tile normal/AO/height.** Extend the *same* transform to the other HD
  maps, with the INV-2 normal-X negate on mirror. *Verify:* relief and lighting
  stay registered with the albedo; no colour-vs-relief mismatch.
- **L3 — POM in de-tiled space + border blend.** *Verify:* no visible cell
  seams; POM relief agrees with the de-tiled albedo. Apply the §4.4 fallback if
  needed.
- **L4 — Filth.** Crevice (AO) coupling + tint + darken/desaturate on top.
  *Verify:* reads as neglect, not just dark; dials behave; net exposure not too
  dark.
- **L5 — Runtime dial + perf pass.** `misc5.y` quality/strength; measure against
  the §6 floor. *Verify:* FPS within floor at baseline; quality knob works;
  `-rtverify` still green.

## 8. Invariants

- **INV-1:** De-tile transform set = {sub-tile offset, horizontal mirror} only.
  No rotation, no vertical flip (preserves DOOM's vertical texture orientation).
- **INV-2:** On a horizontal mirror, the sampled tangent-space normal's X
  component is negated, so lit relief does not invert.
- **INV-3:** All HD maps of one hit share one de-tile transform (registration).
- **INV-4:** The per-cell hash is seeded by **world position**, not by the
  texture-space cell index alone — otherwise two walls both starting at cell 0
  would clone.
- **INV-5:** The paletted / non-`usePBR` path is unchanged (no de-tile, no
  enriched filth).
- **INV-6:** Filth only darkens/tints within a clamped floor; it never brightens
  a surface beyond the current grime ceiling (it must always read as dirt).
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
