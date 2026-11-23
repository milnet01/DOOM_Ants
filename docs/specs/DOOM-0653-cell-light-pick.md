<!-- ants-spec-format: 1 -->
# DOOM-0653 — The traced view picks wall and floor lights by what reaches the point

**Status:** accepted (2026-11-22); B1 built (2026-11-23).
**Kind:** fix.
**Source:** ROADMAP DOOM-0653 (light lab handoff, 2026-11-21).
**Builds on:** DOOM-0652 (the shared light grid: `ClusterStaticLights`, the seep grid, the
polar wall maps).
**Must keep:** DOOM-0009 INV-6 (the integrator is unbiased up to the firefly clamp).

**Layman:** In the ray-traced view, dark corners look grainy. Each pixel tries a few lights
per frame, and today it picks them by how bright they are across the whole map, so a dim
room spends its tries on bright lamps far away. After this, each patch of floor keeps a short
list of the lights that actually reach it, and picks mostly from that. The picture's true
brightness does not change; the grain drops.

## Contents

1. Goal · 2. Where this sits · 3. Scope decisions · 4. Design · 5. Data & resources ·
6. Performance budget · 7. Build order · 8. Invariants · 9. Alternatives · 10. Open questions ·
11. What checks this · 12. Cross-doc impact · 13. Loop log

## 1. Goal

In the traced view of Solid and Ultra, a dim area shows at most half the speckle it shows
today, with the expected brightness of every pixel unchanged.

## 2. Where this sits

`shadeSurface` (`shaders/pt_common.glsl`) lights a hit two ways:

- **Static emitters** (wall and flat triangles, `[0, omniStart)`). Each of `nSamples` samples
  binary-searches the merged emitter table's cdf over the static prefix, so a triangle is
  picked in proportion to its luminance times area over the whole map. `nee_pick_static`
  (`nee_sampling.h`) mirrors the draw.
- **Sprite lights** (`[omniStart, emitCount)`). A RIS loop over every sprite, weighted by its
  unshadowed contribution at the point, culled by REJECT. Not changed here.

The camera frame calls it with `NEE_SAMPLES` samples; `bake.comp` calls it with `BAKE_NEE`.

Measured 2026-11-22, E1M1, Ultra (`renderer 1`, scale 50), private Xvfb display, DEV build at
3a86047, `-rtview 4` (the undenoised NEE view). Kit `Scratchpad/DOOM_Ants/d0653`: `cap.sh`
captures, `speck.py` counts specks — pixels more than 24 above their 5×5 median, among pixels
whose median is under 48 (of 255), in the 3D view.

| Spot | `-lightlab` | Specks per 1000 dim pixels |
|---|---|---|
| Start room, `1056 -3616 90` | 3 (wall and flat lights only) | 77.26 |
| Start room | 111 (all but room and torch) | 80.62 |
| Courtyard, `1833 -3622 107` | 3 | 24.29 |
| Courtyard | 4 (lamps only) | 7.57 |
| Courtyard | 111 | 22.38 |
| Courtyard, `-rtview 6` (denoised) | 111 | 0.72 |

So the static pick makes nearly all the speckle; the sprite RIS makes little. The denoiser
hides most of it at rest, which is why the user sees it as crawling grain in dim areas rather
than as dots.

`-rtverify` the same day, private GPU display (`d0653/rv.sh`, Ultra, `rt_fog 0`), static
emitters only (`-lightlab 3`): 0.2027 % at E1M1's start, PASS; 62.9448 % at the courtyard
(`-warpto 1833 -3622 107`), FAIL. A failure at a warped spot is DOOM-0566, open: nobody has
yet told the test's own noise from bias there.

## 3. Scope decisions

- **Static emitters only.** The sprite RIS already weighs each light at the point. Chosen by
  this session; the user delegated technical calls (2026-11-21).
- **A mixture, never a replacement.** A share `NEE_GLOBAL_FRAC` of samples keeps today's
  global pick, so every emitter that can be picked today still can, and the estimate stays
  unbiased whatever a cell's list leaves out. A list's errors cost noise, never light.
- **The bake keeps today's pick.** `bake.comp` has no seep grid, and 128 rays per probe
  average its noise away. It passes no cell, which selects the global pick alone.
- **The look may brighten a little where the firefly clamp was eating light.** A better pick
  gives smaller per-sample weights, so fewer samples reach `FIREFLY_MAX`. That is the clamp's
  bias shrinking, not new light.
- **Fog (DOOM-0651), dark rooms (DOOM-0642) and the sky term are not here.** They may read
  the same lists later, under their own items.

## 4. Design

### 4.1 What a cell lists

The cells are the seep grid's, as DOOM-0652's raster lists use. The entries are the static
light clusters from `ClusterStaticLights` with `bySector` true on the `RASTER_LIGHT_CLUSTER`
lattice, so a cluster stays in its own room. Every static emitter of non-zero area belongs to
exactly one cluster.

A cluster's power `I` is the sum over its triangles of luminance(Le) × area: the weight the
global cdf already uses. A cluster's estimate at a cell is

    E = I × V / max(d², NEE_NEAR²)

where `d` is the 2D distance from the cluster's centre to the cell's box (`BoxDist`), and `V`
is 0 where the cluster's raster polar map says it cannot reach the cell
(`PolarBoxMaySee`), else 1. A cluster with no raster light, or a cell beyond its raster range,
takes `V = 1`. A cluster beyond `NEE_LIST_RANGE` is not listed.

A cell keeps its `NEE_CELL_LIGHTS` highest estimates with `E > 0`, sorted by cluster id, with
a cdf over `E`. The load log prints the map's longest pre-cap list and the number of cells
cut.

### 4.2 The pick

For a hit in cell `c` with a non-empty list, each sample draws `u`:

- With probability `NEE_GLOBAL_FRAC`: today's global pick of triangle `t`.
- Otherwise: a cluster from `c`'s cdf, then a triangle within it from the cluster's own cdf
  by triangle power.

Either way the sample is weighted by the mixture pdf

    p(t) = a × pg(t) + (1 − a) × pc(cl(t)) × pin(t)

with `a = NEE_GLOBAL_FRAC`, `pg` today's renormalised static pdf, `pc` the cell's pdf of `t`'s
cluster (0 when it is not listed, found by binary search on cluster id), and `pin` `t`'s
in-cluster pdf. A degenerate triangle has no cluster and `pc = 0`.

An empty list, or no cell (the bake), sets `a = 1`, which is today's pick exactly.

The specular lobe uses the same sample, as it does today. The firefly clamp is unchanged.

### 4.3 When the lists are built

Whenever DOOM-0652's static raster grid is rebuilt (`RebuildStaticLightGrid`): at load,
when the static emitter set changes (`g.staticLightsDirty`, a light-lab switch), and when the
seep grid changes. The rebuild keeps the clusters and marks the lists stale; the next traced
frame, or `-rtverify`, builds and uploads them (`BuildNeeLists`), so the rasterised view
never pays for them. Both views keep the grid current each frame (`UpdateStaticLightGrid`).

An Le-only change (`g.staticLightLeDirty`, an animated flat) keeps the lists. The pick reads
the pdfs stored with them, so a stale list costs noise, not bias. A door's polar-map redraw
does not rebuild them (§ 10).

### 4.4 The verify path

`directNEEVerify` takes the production static pick. With probability `cdfMax` (the static
share of the merged table) it makes § 4.2's pick, weighted by `cdfMax × p(t)`; otherwise it
picks a sprite light by the merged cdf, as today. `-rtverify` then compares the shipped pick,
not a stand-in, against `directAllLights`.

## 5. Data & resources

- One host-written buffer, rebuilt with the lists, written with the device idle. Layout in
  `nee_sampling.h`, beside the pick that reads it: a header (cell count, the static emitter
  count the lists index, the sections' offsets, cluster count), two words per cell (first
  entry, count), the entries (cluster id, cdf, pdf), two words per cluster (first triangle,
  count), the triangles by cluster (emitter index, in-cluster cdf), and one word pair per
  static emitter (cluster id, in-cluster pdf). No cell holds more than `NEE_CELL_LIGHTS`
  entries. A header whose emitter count differs from `omniStart` gives the global pick alone.
- The megakernel reaches it by a device address carried in the `SeepXform` block (set 0,
  binding 5) as a `uvec2` (`GL_EXT_buffer_reference_uvec2`); the push range is full
  (`pc.misc7` closes it at 256 bytes). The cell is `fogLightCell(hitP.xy)`. `shadeSurface`
  takes the address and the cell; the bake passes cell −1.
- Constants: `NEE_GLOBAL_FRAC` 0.25, mirrored between `nee_sampling.h` and `pt_common.glsl`
  with the layout's names. `NEE_CELL_LIGHTS` 32, `NEE_NEAR` 64 and `NEE_LIST_RANGE` 2048 are
  read by the CPU builder alone, in `light_grid.h`. B2 tunes them.

## 6. Performance budget

- Ultra traced at scale 100 on a real display (`d0652/perf2.sh CFG=perfcfg`), E1M4 and E3M6:
  the megakernel's time rises by at most 0.3 ms over 3a86047.
- The list build fits under the load screen: at most 20 ms on MAP24 on the CPU, from a real
  display.
- Solid's rasterised view is untouched (`mesh.frag` does not change).

## 7. Build order

Each stage ends with `make`, `make test` and the § 8 checks.

- **B1 — Lists and pick.** `BuildNeeCellLists` in `light_grid.h`; the mixture's pick and pdf
  in `nee_sampling.h`; their tests; the build, upload and address in `r_vulkan.cpp`;
  `shadeSurface`'s pick; the verify path. Measure the worst cell over every IWAD map and
  confirm `NEE_CELL_LIGHTS`. Captures at both spots; `-rtverify` at the start, and the
  courtyard's figure for DOOM-0566.
- **B2 — Tune and judge.** Sweep `NEE_GLOBAL_FRAC` and `NEE_NEAR` against the speck counts.
  Real-display timings for § 6. Captures for the user to judge, `-rtview 6`, before and after.

## 8. Invariants

- **INV-1** The new pick is unbiased. *Breaks when:* a sample is weighted by the strategy that
  drew it rather than by the mixture pdf, or a globally drawn triangle's cell share is left
  out. *Test:* `d0653/rv.sh lab3 -lightlab 3` (`-rtverify` at E1M1's start, static
  emitters only): `INV-6 direct-light rel-MSE` PASS, bar 0.50 %. At 3a86047, on today's
  pick → `0.2027% ... PASS`.
- **INV-2** Every static emitter that today's pick can draw keeps at least `NEE_GLOBAL_FRAC`
  of its global pdf. *Breaks when:* `a` is 0, or a non-empty list skips the global draw.
  *Test:* `tests/nee_sampling_test.cpp` builds a synthetic emitter set and cell list, and
  checks `p(t) >= a × pg(t)` for every `t` with `pg(t) > 0`.
- **INV-3** The mixture pdf sums to 1 over the static emitters, and the realised pick
  frequencies match it. *Breaks when:* a cluster's in-cluster cdf or a cell's cdf is not
  normalised, or the pick and the pdf disagree on a cluster. *Test:*
  `tests/nee_sampling_test.cpp` draws a million picks through the mirrored pick and compares
  each emitter's count with `p(t)`, as the existing `nee_pick` test does.
- **INV-4** A hit with no cell, or a cell with an empty list, picks exactly as today.
  *Breaks when:* the empty case still splits the draw. *Test:* `tests/nee_sampling_test.cpp`
  compares the mixture pick with `nee_pick_static` for the same `u` sequence; every pick and
  pdf is equal.
- **INV-5** Dim areas speckle at most half as much. *Breaks when:* the lists rank by global
  power instead of the estimate at the cell, or the global share is too large. *Test:*
  `d0653/cap.sh 3 1056 -3616 90 4 in_static` and `cap.sh 3 1833 -3622 107 4 s1_static`, then
  `speck.py`: at most 38.6 and 12.1 specks per 1000 dim pixels (half of § 2's 77.26 and
  24.29).
- **INV-6** A cell lists no more than `NEE_CELL_LIGHTS` clusters, each with `E > 0`, sorted by
  cluster id, and a cell over the cap keeps the largest estimates. *Breaks when:* the cut keeps
  the wrong end, or the sort the pdf lookup depends on is skipped. *Test:*
  `tests/light_grid_test.cpp` builds lists from a synthetic set with one cell over the cap and
  checks each cell against a brute-force ranking.
- **INV-7** The rasterised view does not change. *Breaks when:* the shared cluster builder's
  raster output moves. *Test:* `d0652/ab.sh 111 1833 -3622 107` rasterised, before and after:
  no pixel differs.

## 9. Alternatives considered (and rejected)

- **A light tree (BVH over emitters, as in many-light renderers).** Adapts per point and
  needs no grid, but it is a new structure, rebuilt with the light set, walked per sample.
  DOOM-0652's grid already exists and its cells are small next to a room.
- **Per-point RIS over the cell's whole list.** Normal-aware, but it loops every listed
  cluster per sample, and the megakernel's register pressure is already the RDNA2 limit
  (DOOM-0120's note). Kept as B2's fallback over a few cell candidates (§ 10).
- **Cell lists without the global share.** Cheaper and simpler, but a light the list missed
  (a door opened, a light past `NEE_LIST_RANGE`) would vanish from the picture: bias, not
  noise.
- **More samples per pixel.** Noise falls only with the square root of the cost.

## 10. Open questions

- If B2 misses INV-5's target, draw a few clusters from the cell's cdf and resample one by its
  unshadowed weight at the point (RIS), which adds the surface's facing.
- A door that opens does not refresh the lists, so a light seen through it stays reachable
  only through the global share: no worse than today. Refreshing the cells a redrawn polar map
  reaches is the fix if it shows.
- The courtyard's `-rtverify` failure (§ 2, DOOM-0566). If the new pick brings it under the
  bar, the failure was the old pick's noise; B1 runs `rv.sh court -warpto 1833 -3622 107
  -lightlab 3` and records the figure on DOOM-0566.
- `V` comes from raster polar maps built to the raster range (512). Past it, a light behind a
  wall is listed on distance alone.
- The cell is a 2D column; a light on an upper floor ranks as if level with the point.

## 11. What checks this

| Invariant | Catcher |
|---|---|
| INV-1 | Partial: `-rtverify` at E1M1's start; other spots and maps are unchecked |
| INV-2 | `tests/nee_sampling_test.cpp` |
| INV-3 | `tests/nee_sampling_test.cpp` |
| INV-4 | `tests/nee_sampling_test.cpp` |
| INV-5 | Partial: two capture spots on E1M1 |
| INV-6 | `tests/light_grid_test.cpp` |
| INV-7 | Partial: one capture spot |
| The C and GLSL copies of `NEE_GLOBAL_FRAC` and the layout's names agree | `tests/shader_mirror_test.cpp` |

## 12. Cross-doc impact

- DOOM-0009: INV-6's verify estimator becomes the shipped static pick (§ 4.4), not a stand-in.
- DOOM-0652: the cluster builder gains a per-emitter cluster id; its raster output does not
  change (INV-7).
- `nee_sampling.h`'s header comment describes the global pick only; B1 adds the mixture.

## 13. Cold-eyes loop log

No automatic review: nothing here is hard to undo (rule 14). Log:
`docs/reviews/DOOM-0653-cell-light-pick-loop-log.md`. Kit: `Scratchpad/DOOM_Ants/d0653`.
