# DOOM-0181 — De-tiled, grimy Ultra surfaces

**Status:** **Shipped 2026-07-16** (L1–L5 built, user play-test accepted). This
doc has been reconciled to the **as-built** implementation — see the *As-built
divergences* box below and §4.3 / §5 / §8. Originally reviewed via `/cold-eyes`
loops 1–6, locked 2026-07-16 pre-implementation (loop 6, the confirming pass,
caught a `kDetileMirrorProb` probability inversion — fixed); the de-tiling design
(§4.2) shipped with its structure intact, though two constants were play-test-tuned
(`kDetileWorldCell` 96→64, `kDetileOffsetMag` 0.5→0.65; see §4.2 / §10 Q2). The
**filth layer (§4.3) evolved substantially during play-test** and is documented
here as built. Design contract for stochastic de-tiling **on HD (`usePBR`) surfaces** +
filth **on all non-sprite world surfaces** in the Ultra ray-traced view.

> **As-built divergences from the pre-implementation spec** (folded in
> 2026-07-16, re-run through `/cold-eyes`):
> 1. **Filth is a stain system, not a wash (§4.3).** The soft darken/desaturate/
>    tint could only fade, so it never read as "dirt." Replaced with distinct,
>    hard-edged, multi-coloured **stains**: a 3-scale grunge sum (`grungeFbm`)
>    thresholded by a narrow `smoothstep` into defined marks, coloured per-region
>    from an earthy palette (brown/ochre/soot) or a real dirt texture.
> 2. **Filth applies to all non-sprite world surfaces, not `usePBR`-only.** User
>    wanted dirt "everywhere," not only on the 11 HD heroes. The grime call is
>    gated on `!isSprite`; paletted RT surfaces now take filth too (INV-5/INV-8
>    revised). **De-tiling (§4.2) stays HD-only** — it needs the HD maps.
> 3. **A real dirt colour texture was added** (`dirt.png`, `misc5.z`) plus 10
>    baked hero AO maps — §5's "no new images" no longer holds (revised).
> 4. **Green-goo puddles** pool on floors near the goo room (§4.3).
> 5. **Liquids are never painted** — a saturated-green albedo early-returns from
>    `applyGrime` (INV-10), so no dirt/goo/crevice grime lands on flowing nukage.

**Depends on:**
- **DOOM-0042** (Ultra HD PBR materials) — provides the HD material set this
  operates on: the `ctrl[]` control SSBO, the bindless `hdTex[]` array, the
  `usePBR` gate, the shared `sUV` sampling pipeline (`hdBaseUV` → optional
  `hdParallaxUV`), and the `hdAlbedo/hdShadingNormal/hdAO` samplers
  (`pathtrace.comp`: `ctrl[]` SSBO L79, `hdBaseUV` L115, `hdParallaxUV` L148,
  `hdAlbedo` L179, `hdShadingNormal` L192, `hdAO` L203).
- **DOOM-0009** (path tracer) — the RT back-end. This hooks the primary hit in
  `pathtrace.comp` **mode 4** (NEE display, `else if (mode == 4u)` at `:817`,
  `applyGrime` call at `:863`) and **mode 6** (denoised play path,
  `else if (mode == 6u)` at `:980`, `applyGrime` call at `:1026`).

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

**Scope:** Ultra RT view only (`pathtrace.comp` modes 4 + 6). Classic and the
raster stack (Solid, **and Ultra with RT off**) stay **byte-identical** to today.
Within the RT view, two different gates apply (as-built): **de-tiling (§4.2) is
HD-only** — it needs the `usePBR` maps, so paletted surfaces are unchanged by it;
**filth (§4.3) applies to every non-sprite world surface** — paletted included —
so dirt shows everywhere, never on sprites, never on liquids (INV-10).

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

*Terms:* **grunge** = the loaded overlay texture (`misc5.x`); **grime** = the
`applyGrime` brightness multiply (`kGrimeStrength`); **filth** = this spec's
enrichment of grime (§4.3 as-built: distinct multi-coloured dirt **stains** +
crevice pooling + green-goo floor puddles, sampling a real dirt texture);
**de-tiling** = the §4.2 anti-repeat mechanism, independent of all three. (Heads-up: the existing
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
grime multiply (DOOM-0179) camouflages #1 only weakly. Even the grime strength
(`kGrimeStrength = 0.25` as-built, `pathtrace.comp:108`) — a ±25 % swing at the
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
into the filth layer. **De-tiling touches only the HD (`usePBR`) map fetches** —
the paletted branch is unchanged by it. **Filth (`applyGrime`), as-built, runs on
every non-sprite hit** (`if (!isSprite) …`, `pathtrace.comp:863`/`:1026`), so
paletted surfaces do get stains — but with `ao == 1.0` (no AO map) they take no
crevice darkening, only the grunge grounding + coloured stains.

**Precondition — the `hitP` hoist (L1, done as-built).** The de-tile cell needs
the world hit point `hitP` (§4.2). As-built, `tHit`/`hitP` are **hoisted above the
`hdBaseUV`/`hdParallaxUV`/`hdAlbedo` block** (`pathtrace.comp:837`, marked
`// HOISTED (DOOM-0181: hitP feeds the detile world-key)`) — they come free from
the ray query (`rayQueryGetIntersectionTEXT`) before shading and have no dependency
on `sUV`, so the hoist was trivial.

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
  `ivec2 cell = ivec2(floor(w / kDetileWorldCell))`,
  `f = fract(w / kDetileWorldCell)` (note the `ivec2` cast — the hash below indexes
  by integer cell). The per-cell hash is a small `vec3 hash3(ivec2 cell)` **wrapper
  built around the
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
  *Shipped values* (play-test-tuned from the 96/0.5 starting point, §10 Q2):
  `kDetileWorldCell` = 64 units, `kDetileMirrorProb` = 0.5, `kDetileOffsetMag` =
  0.65 (→ ±0.65 tile).
- **Per-cell transform.** Applied to the sampling coordinate (the `baseUV` /
  POM-marched `sUV` fed to the map fetches): a sub-tile UV **offset**, **centred**
  so it is signed — `(hash3(cell).xy - 0.5) * 2.0 * kDetileOffsetMag` (a raw
  `hash3(cell).xy` would only ever shift positive) — plus an optional **horizontal
  mirror**. As-built (`detileCellUV`, `pathtrace.comp:493`) the mirror simply
  **negates U** (`uv.x = -uv.x`) and the texture's repeat-wrap folds the tile, when
  `hash3(cell).z < kDetileMirrorProb` (the `<` makes `kDetileMirrorProb` literally
  P(mirror) and matches the codebase's `rnd() < threshold` idiom; a `>` would invert
  the knob).
  **No rotation, no vertical flip** — DOOM wall
  textures are vertically oriented (panel lines, rivets), so only
  orientation-preserving transforms are allowed (INV-1).
- **Seam handling.** The Inigo-Quilez 4-corner blend: a per-pixel weighted blend
  of the (up to 4) neighbouring cell variants by `f` — **not** an edge-only band.
  4 taps; a 2-tap mode is the perf dial (§6). The blend is a **per-map wrapper**
  around each `texture(hdTex[m], …)` call — it does **not** mutate the one shared
  `sUV`, so a map that has not yet been wrapped (an L1/L2 interim build) keeps
  reading the plain coordinate. It wraps the **non-height-map fetches**
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

### 4.3 Filth — distinct dirt stains (as-built; completes DOOM-0179)

> **As-built note.** The pre-implementation spec called for a soft
> darken/desaturate/tint wash. Play-test showed a wash can only *fade* a surface —
> it never reads as "dirt." What shipped is a **stain system**: defined,
> hard-edged, multi-coloured marks that sit *on* the surface. `applyGrime` gains
> an AO input — signature `applyGrime(albedo, hitP, n, ao)` — and runs on every
> non-sprite hit (§4.1). Constants live at `pathtrace.comp:107–135`; the two
> helpers are `grungeFbm` (`:583`) and `stainColour` (`:599`); `applyGrime` is at
> `:624`.

The layer has three stacked terms, applied after de-tiling (so filth sits on the
de-tiled albedo):

- **Grunge grounding (subtle).** Sum the `misc5.x` grunge overlay at **three
  world scales** (`grungeFbm`: `kGrimeOctaveScale` × `kGrimeWorldScale`, weights
  summing to 1, per-octave UV offsets so the scales don't align into a grid) into
  one value `g ∈ [0,1]` with **mixed-size marks**. A gentle centred multiply
  `m = 1 + (g − 0.5)·2·kGrimeStrength` (`kGrimeStrength = 0.25`) grounds the
  surface without dominating it.
- **Crevice pooling.** Subtract `(1 − ao)·kGrimeCrevice` from `m` so genuine
  recesses darken — dirt collects where nothing wipes it. The `ao` is the **same
  de-tiled AO** the ambient term reuses (one fetch per hit, hoisted per §4.1,
  INV-7). Paletted / no-AO hits have `ao == 1.0`, so they take no crevice term
  (open-surface base still applies, INV-7). All darkening stays inside the
  `clamp(m, 0.35, 1.65)` ceiling (INV-6).
- **Distinct coloured stains.** Where `g` falls below `kStainThresh`, a **narrow**
  `smoothstep(kStainThresh, kStainThresh − kStainEdge, g)` cuts a hard-edged stain
  mask (`kStainEdge = 0.07` — small band = defined edge, not a fading dot). The
  stain colour comes from `stainColour(gid, wUV, n, albedo)` and is mixed over the
  grounded albedo at `stain · kStainOpacity` (`kStainOpacity = 0.82`).

**`stainColour` — non-uniform, multi-hue dirt (`:599`):**
- **Green-goo puddles (floors only).** On up-facing surfaces (`n.z > 0.6`) that
  are **not already green** (`albedo.g > albedo.r·1.15 && > albedo.b·1.15`), a
  coarse grunge selector opens goo puddles for `< kGooChance` (0.40) of floor
  stains: `kGooColour` modulated by a finer grunge sample. This is what pools
  green goo on the goo-room floor **without** painting it on the goo liquid
  itself.
- **Real dirt texture when present.** If a dirt colour texture is loaded
  (`did = misc5.z`, DOOM-0181's `dirt.png`), the stain colour is a **macro** world
  sample × a **finer** grain sample (`macro · (0.72 + 0.55·fine)`) — so dirt reads
  as a photographed texture, mottled and non-uniform, not a flat tint.
- **Procedural fallback** (no dirt texture): an earthy ramp built from three
  grunge scales — `kDirtDark → kDirtBrown → kDirtOchre` by a coarse tone selector,
  brightness-mottled, with `kSootColour` cores in the darkest blotches.
- **No cell grid.** An early build keyed stain colour off an integer world-cell
  hash; the axis-aligned cell borders showed as **straight seams**. Replaced with
  smooth overlay samples (above), so colour varies continuously with no seams.
- **Missing overlay.** When no grunge overlay is loaded (`gid == 0xFFFFFFFF`),
  `grungeFbm`/stains are skipped (there is nothing to sample), but the crevice
  term still darkens recesses from `ao` alone.
- **Liquid guard.** `applyGrime` early-returns the albedo untouched for a
  saturated-green (liquid) surface **before** any term runs (INV-10) — no dirt,
  goo, or crevice grime on flowing nukage/slime.

**Dials:** `kGrimeStrength` (grounding depth), `kGrimeCrevice` (AO coupling),
`kStainThresh`/`kStainEdge`/`kStainOpacity` (stain coverage, edge hardness,
opacity), `kGooChance` (goo share of floor stains), the earthy palette constants,
and `kGrimeOctaveScale`/`Weight` (mark-size spread). Tunable from lived-in to
abandoned.

### 4.4 POM interaction

POM (`hdParallaxUV`) marches the sampling coordinate along the height field per
pixel; de-tiling also transforms that coordinate. The de-tile **cell is computed
from world position (`hitP`), independent of POM** (§4.2), so the two never fight
over which cell a pixel is in.

**Order and cost.** Compute the per-cell offset/mirror **once** and apply it to
`baseUV` (a single de-tiled coordinate for the pixel), *then* run the POM march on
that de-tiled coordinate using the de-tiled height map. The march is **not**
4-corner-blended per step — it uses the pixel's single dominant cell — so POM cost
stays ~unchanged (the blend's `taps×` multiplier applies only to the
non-height-map fetches, §4.2). Parallax relief, albedo, and normal then all come from the same
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

- **No new GPU buffers, descriptor bindings, or SSBOs.** Reuses the `hdTex[]`
  bindless array, the `ctrl[]` SSBO, and the dominant-axis world projection.
- **Two new bindless images** (as-built — revises the spec's original "no new
  images"), both loaded in `EnsureHdMaterials` (`r_vulkan.cpp:5139`–`5168`) as
  ordinary `hdTex[]` entries, no new binding:
  - `assets/ultra/overlays/dirt.png` — a real dirt **colour** texture (sRGB),
    first-party CC0, generated by `scripts/make_dirt.py` (FFT fractal noise →
    warm-brown palette). Rides to the trace as `misc5.z`; missing/undecodable
    leaves it off (`hdDirtIdx = -1` → `0xFFFFFFFF`), and `stainColour` falls back
    to the procedural earthy ramp.
  - 10 baked hero **AO** maps (`*_ao.png`, one per CC0 hero), produced by
    `scripts/hero_ao.py` from each hero's `_hgt.png` relief. These feed the crevice
    term for HD surfaces; second-order CC0 derivatives of the sibling height maps
    (see `assets/ultra/LICENSES`).
- **`misc5` lane map (as-built):** `.x` = grunge overlay id (DOOM-0179);
  `.y` = de-tile dial (0=off, 1=2-tap, 2=4-tap; `]` key, `rb_detile`);
  `.z` = dirt colour-texture id (this spec); `.w` = free. No push-constant layout
  change (INV-9).
- **New tuning knobs.** De-tile offset magnitude, mirror probability
  (`kDetileMirrorProb`), and world cell size (`kDetileWorldCell`); filth crevice + tint
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
- **Filth cost (as-built — revised up).** The shipped stain system is **not** just
  ALU: `applyGrime` now runs on **every non-sprite world hit** (not `usePBR`-only),
  and each hit pays:
  - **`grungeFbm`: 3 overlay fetches, always** (the 3 world scales) — needed for
    both the grounding multiply and the stain threshold.
  - **`stainColour`: +2–3 fetches, only where a stain forms** (`stain > 0`, gated
    behind the `smoothstep` threshold, so a minority of pixels): 2 for the real
    dirt texture (macro + fine), 1–2 for the goo path, or 3 for the procedural
    fallback.
  - **0 fetches on liquids** (early-return, INV-10).
  So the steady per-pixel cost is ~3 overlay fetches at the primary hit, spiking to
  ~5–6 inside stains. GI is baked (§2), so this is paid once per pixel, never per
  bounce. **This cost was added post-L5 and has not yet been isolated on the
  profiler** — it is the open perf item at ship (see the perf-levers note below).
- **Perf levers held ready** (apply after a profiler capture shows a real cost —
  measure before cutting, don't blind-optimize):
  1. Drop `grungeFbm` from 3 world scales to 2 (fine speckle octave weight is only
     0.18) — ~⅓ off the always-on cost, small look change.
  2. Add a **filth quality/off dial** on the free `misc5.w` lane (mirrors the
     de-tile `]` dial), so filth can be dropped or dialled for perf without a
     recompile — the cleanest lever, no look change when left on.
  3. Distance/LOD gate: skip the fine-grain stain fetch on far pixels.
  Levers 1 & 3 change the look (play-test call); lever 2 does not.
- **Gate (evaluated at L5 only).** L1–L4 are visual play-test only, with **no FPS
  gate** — FPS numbers there are diagnostic. The single L5 pass/fail line: **4-tap
  de-tiling must add ≤ 5 % to the frame time vs the measured RT-on baseline.**
  Measurement protocol (same for baseline and 4-tap): average the `[cpu_profile]`
  present-total (ms, not FPS) over a fixed ~10-second walk of the green-goo room,
  the same path both times.
- **Fallback.** If 4-tap exceeds 5 %, fall back to 2-tap via the `misc5.y` dial, or
  a cheaper compile-time split (§10 Q4). *Assumption:* a fallback is cheaper than
  4-tap by construction (fewer fetches), so it clears the bar — but spot-check the
  chosen fallback against the same ≤ 5 % number before accepting it.
- **Advisory (non-blocking).** De-tiling should avoid being the change that drops
  RT-on Ultra below DOOM-0012's aspirational 60 FPS floor (💭 considered — not yet
  an adopted commitment), but that floor is not this feature's gate — the only L5
  pass/fail is the ≤ 5 % rule above.

## 7. Build order

Each layer is independently play-testable; stop and get user acceptance per
layer (renderer look is a play-test call). L1–L4 acceptance is **human play-test
only** — no automated proxy metric, matching the renderer-look convention
(DOOM-0179 / DOOM-0042); only **L5**'s ≤ 5 % perf check (§6) is objective. L1–L4
FPS numbers are diagnostic.

| Layer | Scope | Verify | FPS-gate? |
|-------|-------|--------|-----------|
| **L1** | De-tile **albedo** (offset + mirror + world hash + 4-corner blend); hoist `hitP` (§4.1) | Green-goo + perimeter walls no longer read as repeated; same-texture walls differ | no |
| **L2** | Extend the same transform + blend to **normal, AO** (INV-2 normal-X negate) | Relief/lighting stay registered with albedo; no colour-vs-relief mismatch | no |
| **L3** | De-tile **height** + fold POM march into de-tiled space (§4.4, mirror-march negate) | POM relief agrees with de-tiled albedo/normal; no boundary artefacts (else §4.4 fallback) | no |
| **L4** | **Filth**: hoist `hdAO`; **stain system** (§4.3 as-built — 3-scale grunge, hard-edged coloured stains, real dirt texture, floor goo puddles, liquid guard); applied to all non-sprite world surfaces | Reads as filthy neglect, not just dark; dials behave; stays within INV-6 clamp | no |
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
- **INV-5 (as-built):** The paletted / non-`usePBR` path gets **no de-tiling**
  (de-tiling needs the HD maps). It **does** get filth (§4.3) like every non-sprite
  world surface — but with `ao == 1.0` it takes only the grunge grounding +
  coloured stains, no crevice darkening.
- **INV-6:** Filth only darkens/tints within the existing grime clamp
  (`clamp(m, 0.35, 1.65)`, `pathtrace.comp:639`), biased toward the dark end; it
  never brightens a surface beyond that ceiling (it must always read as dirt).
- **INV-7:** Crevice coupling uses the same AO the ambient term uses; filth still
  applies at base strength where AO ≈ 1 (open surfaces).
- **INV-8 (as-built):** Ultra RT only; modes 4 and 6 get identical treatment.
  Classic and the raster stack (Solid, **and Ultra with RT off**) stay
  byte-identical. Paletted RT surfaces are **not** byte-identical — they take filth
  (INV-5) — but sprites and liquids never do (INV-10).
- **INV-9:** `-rtverify` (mode 5) is unaffected — no push-constant layout change
  (the `misc5` lanes used for the runtime dial + dirt id already exist as padding
  on the mode-5 verify struct, DOOM-0179).
- **INV-10:** Filth is applied only to **non-sprite, non-liquid** world surfaces.
  Sprites are excluded at the call site (`if (!isSprite)`); liquids (a
  saturated-green albedo, `g > r·1.15 && g > b·1.15`) early-return from `applyGrime`
  before any term runs — no dirt, goo, or crevice grime on flowing nukage/slime.
- **INV-11:** Green-goo puddles form only on **up-facing floors** (`n.z > 0.6`) and
  never on an already-green surface — the goo pools *near* the liquid, not *on* it.

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

## 10. Open questions — resolutions at ship

- **Q1 (POM):** *Resolved.* POM marched in de-tiled space (§4.4); no boundary
  artefacts reported in play-test, so the blend-band fallback was not needed.
- **Q2 (aggressiveness):** *Resolved.* Shipped `kDetileWorldCell = 64`,
  `kDetileMirrorProb = 0.5`, `kDetileOffsetMag = 0.65` — play-test-tuned from the
  96/0.5 starting point (a smaller cell + larger offset break the repeat harder),
  user-accepted, reads natural on oriented textures.
- **Q3 (filth level):** *Superseded.* The wash gave way to the stain system
  (§4.3); shipped default reads as a filthy, monster-overrun base, user-accepted.
- **Q4 (perf/quality):** *Still open — the one item carried past ship.* The L5
  ≤ 5 % de-tile gate was met, but the as-built **filth stain fetches** (§6) were
  added after L5 and have **not been isolated on the profiler**. Next: a profiler
  capture of a green-goo-room walk (`\` key) to measure filth's real cost, then
  apply a §6 perf lever only if it shows a real hit. Tracked for follow-up.
