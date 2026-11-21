<!-- ants-spec-format: 1 -->
# DOOM-0652 — Light that fades smoothly: a shared light grid, wall-true lamp shadows and blended bounce light

**Status:** accepted (2026-11-21). Not built; B1 is next.
**Kind:** fix.
**Source:** ROADMAP DOOM-0652 (user report 2026-11-21, light lab on E1M1).
**Also closes:** DOOM-0649 (dim lights crowd bright ones out of a subsector's 16 slots): the
capped nearest-N list it describes is removed here.
**Amends:** DOOM-0170 INV-6 (the per-fragment light cap's unit changes from subsector to cell).
**Pairs with:** DOOM-0011 (the fog's light grid, which this grows out of), DOOM-0642 (honest
light), DOOM-0009 (the path tracer, whose bounce read changes with the probes).
**Design input:** cold reviews and design answers from the Vestige, UT_Ants and ExperiRays
sessions, 2026-11-21 (relayed by message; their sources are cited where used).

**Layman:** The smooth view lights the map in invisible patches, and light jumps where two
patches meet, drawing straight lines on floors and ceilings. This replaces the patches. Every
light fades smoothly to nothing, every spot of floor hears from every light that can reach it,
walls block lamp light along the real walls, and the soft bounced light blends between points
instead of jumping.

## Contents

1. Goal · 2. Where this sits · 3. Scope decisions · 4. Design · 5. Data & resources ·
6. Performance budget · 7. Build order · 8. Invariants · 9. Alternatives · 10. Open questions ·
11. What checks this · 12. Cross-doc impact · 13. Loop log

## 1. Goal

In the rasterised view of Solid and Ultra, light on a floor, wall or ceiling changes only where
something in the world changes it: distance from a light, a wall between, a change of surface.
No light or dark edge follows a BSP split, a sector border or a grid line.

## 2. Where this sits

Two terms in `mesh.frag` are looked up once per subsector (`pc.triSs[gl_PrimitiveID]`) and
never blended:

- **Point lights.** `RebuildStaticPointLightCache` keeps each subsector's nearest
  `RASTER_MAX_LIGHTS_PER_SUBSECTOR` emitter triangles by 2D distance to `g.subCentroid`, culled
  by `RejectHides`. `BuildRasterPointLights` merges the frame's sprite lights by the same key.
  The shader's falloff, `1 / (1 + d2 * RASTER_INV_RADIUS2)`, never reaches zero, so a light on
  one subsector's list and not its neighbour's stops in a straight line.
- **Bounce.** `giIrradiance` (`shaders/formulas/gi_probe.glsl`) reads one SH-L1 probe per
  subsector. `RB_BuildProbes` places it at the mean of the subsector's seg ends, at sector
  mid-height. `bake.comp` seeds each probe's rays independently.

Measured 2026-11-21, E1M1 at shot-0100's spot (`-warpto 1833 -3622 107`), Ultra, rasterised,
render scale 50, private Xvfb display, DEV build, kit `Scratchpad/DOOM_Ants/d0652/ab.sh`:

- `-lightlab 111` (all but room and torch) against `-lightlab 79` (bounce off as well): the
  floor bands keep their full strength; whole-frame mean luma 50.13 against 50.58.
  `LAB_BOUNCE` off still bakes the sky's fill into the probes (`RunGiBake` passes no emitters),
  so this removes only the lamps' bounce.
- `-lightlab 0` (sky only, so no point lights): the same split lines show, faint.

So the point lights cause most of the lines, and the probes add a faint layer. All three
reviews had ranked the probes first; the measurement reversed it.

The traced view's fog already has most of the needed parts (DOOM-0011 L3):
`ClusterStaticFogLights` snaps emitter triangles to a lattice of `RB_FOG_LIGHT_CLUSTER` units
and gives each cluster a reach; `BuildFogLightGrid` lists lights per cell of the seep field
(`g.seepField`, 2D, cell size from `RB_SEEP_CELL0`, doubled until both sides fit
`RB_SEEP_MAXDIM`), keeping `RB_FOG_LIGHTS_PER_CELL` of them after sight tests with
`P_CheckSightTrace`.

## 3. Scope decisions

- **One light grid, built for every reader; the rasterised view reads it first.** The traced
  view's light choice (its dim-area speckle) and the fog move onto it under their own roadmap
  items. Chosen by this session 2026-11-21; the user delegated technical calls ("I can just
  judge the final product").
- **Occlusion is a per-light polar wall map, not REJECT and not a per-cell flag** (Vestige's
  proposal, 2026-11-21). A cell or sector visibility gate puts visibility on a coarse grid and
  draws its own lines.
- **Probes move to a lattice with a per-probe reach test** (UT_Ants' `probes.glsl`
  `probeReaches` / `indirectAt`, UTA-0112 § 4.13). Per-surface baked shadow masks (UT_Ants'
  `chartedLight`) were weighed and left for later: § 9.
- **The look may shift a little.** A light's far tail now ends at its range, and light that
  leaked through walls stops. Measured at the § 8 spots; the user judges the result.

## 4. Design

### 4.1 Cluster lights

Static emitter triangles are grouped by the fog's lattice snap (`RB_FOG_LIGHT_CLUSTER`), in one
builder the fog and the raster grid share. A raster cluster carries the area-weighted centre,
pushed 2 units off its surface along the mean normal so its own wall does not shadow it, and a
colour equal to the sum of its triangles' raster colours (`Le / max(maxchannel, RASTER_LE_FULL)`
today, per triangle), so a panel lights a floor about as brightly as its nearest-N triangles
did. The fog keeps its own gains.

Dynamic lights (sprites, models, fireballs) stay one light per record, as today.

### 4.2 Finite range

Every light's falloff becomes `att(d) * win(d, R)`, with `win = clamp(1 - (d/R)^4, 0, 1)^2`.
Its value and slope reach zero at `R`.

- `R` is where the light's peak channel times `att` falls to `RASTER_LIGHT_CUTOFF`, one
  constant scene-wide, so two lights of equal strength reach equally far. It is capped at
  `RASTER_LIGHT_MAXRANGE`.
- A pickup keeps its reach (it already falls to zero).
- A lamp's pool keeps its own reach. Its room light takes `R` like any other light.

### 4.3 The cell lists

A 2D grid with the seep field's origin and cell size. Each cell lists every light whose range
reaches the cell's box, with no ranking. Static lights are listed at load and when the light
set changes (`g.staticLightsDirty`). Dynamic lights are added each frame, but only to the cells
inside their range.

The cell cap is `RASTER_CELL_LIGHTS`, sized from the worst cell over the IWADs' maps (§ 7 B1
measures it). A cell that overflows drops the light whose window is smallest at the cell's
nearest point, and the load log reports the overflow count. An overflow is the one case where a
step can come back, so the count is watched.

`mesh.frag` finds its cell from `vWorldPos.xy` and loops that cell's list. `triSs` is no longer
read for lights.

### 4.4 Wall-true occlusion: the polar wall map

Each listed light has a polar map: `RASTER_POLAR_BINS` angle bins round the light, each holding
the 2D distance to the nearest blocking line. A compute pass builds it from the linedefs within
the light's range. A blocking line is:

- one-sided; or
- two-sided with no opening (the smaller ceiling is at or below the larger floor), which is how
  a closed door blocks.

A two-sided line with an opening does not block (§ 10).

The fragment takes its angle and 2D distance to the light. It is lit by that light when it is
nearer than the bin's blocker, plus a bias for the wall's own surface, with a few-tap PCF across
neighbouring bins. This gives a soft edge one bin wide.

The static lights' maps are rebuilt when a sector whose lines are in range changes height: a
door or a lift, at the moment it settles or every frame while it moves. Dynamic lights' maps
are rebuilt each frame. The compute pass needs no ray tracing hardware.

`RejectHides` is no longer used for raster lights.

### 4.5 Probe lattice with reach

Probes sit on a 3D lattice:

- xy spacing is the seep cell's;
- z spacing is `RB_PROBE_DZ`, doubled until the layers fit `RB_PROBE_MAXLAYERS`;
- only points in the air of some sector are baked.

`bake.comp` is unchanged but for its positions. Every probe seeds by ray index only, so
neighbouring probes share their noise. Each probe also stores its reach: the distance to the
first surface along ±x, ±y and ±z.

`giIrradianceAt(pos, n)` blends the 8 lattice corners around `pos + n * RB_PROBE_NBIAS`
trilinearly. A corner is dropped when it is not baked, or when its reach toward the point falls
short of it. Its weight goes to the corners left; with none left the answer is zero.

`mesh.frag`, `pathtrace.comp` and `bake.comp`'s bounce passes call it in place of
`giIrradiance(…, subId, …)`.

## 5. Data & resources

- Light grid: cells × `RASTER_CELL_LIGHTS` × a light index (uint), plus a light table of
  position, colour, range and reach. Host-visible, one copy per frame in flight, as
  `g.lightBufSlot` is today. It replaces the per-subsector `lightBuf`.
- Polar maps: lights × `RASTER_POLAR_BINS` floats, device-local, written by compute.
- Probes: lattice points × (16 floats today + 6 reach). The dense lattice is bounded by the seep
  field's `RB_SEEP_MAXDIM` squared, times `RB_PROBE_MAXLAYERS`. B4 measures the largest IWAD
  map's bake time before the caps are fixed.
- Push constants: `pc.triSs` stays for the traced view and specular. The light and probe
  addresses change meaning; the 128-byte block keeps its size.

## 6. Performance budget

- Rasterised Solid at the reference scale (DOOM-0170 § 1) stays at or above 60 FPS on E1M1 and
  a light-heavy room, as DOOM-0170 INV-2 requires.
- Per-frame CPU for the light grid's dynamic part stays under the 1 ms that
  `BuildRasterPointLights` was held to.
- The load-time bake (probes and static polar maps) fits under the load screen. B4 records it
  for the largest IWAD map. A door settling must not cost a frame over DOOM-0170 INV-3's
  1.5× budget.

## 7. Build order

Each stage ends with `make`, `make test` and the § 8 captures.

- **B1 — Cluster lights, finite range, cell lists.** The shared cluster builder, the window, the
  grid, the `mesh.frag` loop. Occlusion stays on REJECT for now, gated per light by the
  fragment's sector. Measure the worst cell over every IWAD map and set `RASTER_CELL_LIGHTS`.
  Amend DOOM-0170 INV-6.
- **B2 — Polar wall maps.** The compute pass and the fragment test. Drop REJECT for raster
  lights, and rebuild on door and lift movement.
- **B3 — Probe lattice and reach.** Placement, the bake's positions and seed, the reach rays,
  `giIrradianceAt` in all three readers.
- **B4 — Measure and tune.** Load and bake time on the largest IWAD map, FPS, frame-time trace,
  the look at the § 8 spots. Ask UT_Ants and Vestige for a code review (the user's request,
  2026-11-21).

## 8. Invariants

- **INV-1** No light or dark edge follows a BSP split. *Breaks when:* a lighting term is read
  per subsector, or a light's contribution is non-zero where a neighbouring cell has dropped it.
  *Test:* `d0652/ab.sh 111 1833 -3622 107` and `ab.sh 110 2270 -3649 128`, rasterised. In a
  contrast-stretched floor crop, the straight bands seen in shot-0100 and shot-0103 are gone.
  A `-lightlab 0` capture shows no bands from the probes.
- **INV-2** Every light's contribution is zero at and beyond its range, with zero slope there.
  *Breaks when:* the window is skipped for a light kind (static, lamp, pickup, fireball).
  *Test:* `tests/light_grid_test.cpp` evaluates the shader's falloff, mirrored in C, at `R`
  and `R + 1` for each kind. Both are zero, and so is the difference across `R ± 0.5`.
- **INV-3** A cell's list holds every light whose range reaches the cell, unless the cell
  overflowed, and every overflow is counted in the load log. *Breaks when:* a list is ranked
  and cut silently. *Test:* `tests/light_grid_test.cpp` builds a grid from a synthetic set
  with one cell over the cap. Every other cell's list equals the brute-force set, and the
  overflow count is 1.
- **INV-4** A wall blocks a lamp's light along the wall's own line. *Breaks when:* occlusion
  falls back to a sector or cell gate. *Test:* `tests/light_grid_test.cpp` builds a polar map
  for a light beside a one-sided line. A point behind the line reads blocked, and a point at
  the same distance beside the line's end reads open.
- **INV-5** A closed door blocks light, and an open one does not. *Breaks when:* polar maps are
  not rebuilt when a sector moves. *Test:* manual. On E1M2, light behind the first door before
  and after opening it; the far room's floor brightens only once it is open.
- **INV-6** A probe on the far side of a wall adds nothing to a point. *Breaks when:* the reach
  test is skipped. *Test:* `tests/light_grid_test.cpp` blends a point beside a wall with one
  corner across it holding bright SH. The result equals the blend of the near corners alone.
- **INV-7** DOOM-0642's E1M2 case stays fixed: the blue `FLAT22` ceiling panels do not tint the
  brown building outside their room. *Breaks when:* occlusion leaks. *Test:*
  `scripts/ab_capture.sh` at the DOOM-0497 spot. The building's mean hue matches a
  `-lightlab` capture with flats off.
- **INV-8** Classic is unchanged. *Breaks when:* a change reaches the software renderer.
  *Test:* `scripts/ab_capture.sh` in Classic at E1M1's start, before against after. No pixel
  differs.

## 9. Alternatives considered (and rejected)

- **Rank by contribution, or 32 slots per subsector** (DOOM-0649's options). Either keeps a
  per-subsector list. A ranked list still drops a non-zero light at a split, so the line
  stays.
- **Per-cell sight-test flag or fraction** (the fog's way). Visibility on a 64-to-256-unit grid
  draws grid-aligned steps, or soft gradients that still leak through a thin wall inside a cell
  (Vestige).
- **Per-surface baked shadow masks** (UT_Ants `chartedLight`, UTA-0326). Exact at texel
  resolution, but it needs a chart atlas and a bake per surface and light. Doom's moving sectors
  invalidate it, which needs a mover-pair list. The polar map is exact for Doom's vertical
  walls at a fraction of the machinery.
- **Ray-query shadow rays in `mesh.frag`.** Correct, but a ray per light per pixel at the
  rasterised view's 60 FPS floor. It is also absent on the non-RT path that the polar map
  could serve later.
- **Per-vertex probe averaging** (a stopgap both reviews named). Doom's large floor triangles
  smear it, and T-junctions keep small seams.

## 10. Open questions

- Two-sided lines with an opening (windows, steps, low ceilings) never block in the polar map.
  A light beyond a tall step lights the floor behind it. Whether that shows is judged at B4.
  The fix would be per-bin opening heights, checked against the fragment's height.
- Ceilings take the same polar map as floors. A ceiling lamp above a lip is not shadowed by the
  lip.

## 11. What checks this

| Invariant | Catcher |
|---|---|
| INV-1 | Partial: the two captures; other maps are unchecked |
| INV-2 | `tests/light_grid_test.cpp` |
| INV-3 | `tests/light_grid_test.cpp` |
| INV-4 | `tests/light_grid_test.cpp` |
| INV-5 | nothing (manual) |
| INV-6 | `tests/light_grid_test.cpp` |
| INV-7 | Partial: one capture spot |
| INV-8 | `scripts/ab_capture.sh` |
| The shader and C copies of the falloff and grid constants agree | `tests/shader_mirror_test.cpp`, once B1 adds the pairs |

## 12. Cross-doc impact

- DOOM-0170: INV-6 is amended at B1 (cap per cell, not per subsector). § 4.1's nearest-N list
  is superseded.
- DOOM-0011: the cluster builder becomes shared; the fog's grid and gains are unchanged.
- DOOM-0642: INV-6 (no stage darkens the default look) is about its own stages. This spec's
  look shift is measured at B4 and judged by the user.
- ROADMAP: DOOM-0649 closes with B1. The traced view's light choice is DOOM-0653, reading
  this grid.

## 13. Cold-eyes loop log

No automatic review: nothing here is hard to undo (rule 14). Log:
`docs/reviews/DOOM-0652-shared-light-grid-loop-log.md`.
