# DOOM-0322 — Raise the 3D sky lid so a taller sky area shows over a lower one

**Status:** spec draft (2026-10-01).
**Kind:** fix.
**Source:** ROADMAP DOOM-0322 (user play-test 2026-08-04, E3M1's opening
courtyard).

**Layman:** In Solid and Ultra, a building standing just past a low outdoor
wall is cut off at the wall's height, with sky where Classic draws the rest of
the building. After this, the 3D views show it the way Classic does.

**Depends on:**

- **DOOM-0141** — the sky surfaces in the 3D mesh (`emit_sky_cap`,
  `emit_sky_wall`, the sky list `rb_mesh_t::sky`). This spec moves the caps and
  replaces one of the wall calls.
- **DOOM-0162** — the raster view draws the same sky list as an occluder, so
  every change here reaches Solid as well as Ultra.
- **DOOM-0142** — fills an untextured wall step, with sky where the neighbouring
  ceiling is sky. Its branches are kept unchanged (§4.3).
- **DOOM-0267** — both views skip the back of a wall face. §4.4 leans on it.

**Delivers:** DOOM-0322.

## Contents

1. Goal
2. Where this sits
3. Scope decisions
4. Design
5. Data & resources
6. Performance budget
7. Build order
8. Invariants
9. Alternatives considered (and rejected)
10. Open questions
11. What checks this
12. Cross-doc impact
13. Cold-eyes loop log

## 1. Goal

Where two outdoor areas meet and one has a higher sky ceiling, Solid and Ultra
show the taller area's walls above the lower area's ceiling, as Classic does.
Nothing DOOM-0141 hid becomes visible: a view still cannot see over an outdoor
area's outer wall.

## 2. Where this sits

**What Classic does.** DOOM has no sky surface. A sector whose ceiling is
`F_SKY1` draws the sky picture wherever its ceiling would be. Where two such
sectors meet, `R_StoreWallRange` in `r_segs.c` sets the front sector's top to
the back sector's (`worldtop = worldhigh`). So no upper wall is drawn between
them, and the lower sector's ceiling does not clip what lies beyond. A wall in
the taller sector shows above the lower sector's ceiling height. That is the
classic "sky hack".

**What the 3D mesh does.** DOOM-0141 made sky solid. `emit_subsector_caps`
emits every sky ceiling as a sky cap at that sector's own ceiling height, and
the seg loop in `RB_BuildLevelMesh` emits a sky wall in the height gap where
two sky ceilings differ. Both go in the sky list, which Ultra traces
(the sky instance, mask `0x04`) and Solid draws depth-on (DOOM-0162).

**Why the building is cut off.** Fixture: E3M1, `-warp 3 1 -warpto 192 -980
90`. The camera stands in sector 31 (sky ceiling 56). Line 16 separates it from
sector 32 (sky ceiling 192), a strip eight units deep. Line 8 is the far side
of that strip: the building's face, whose `SP_HOT1` upper wall rises from 80 to
192. A view ray to the upper part of that face climbs past height 56 while
still over sector 31, and there it meets sector 31's sky cap. The cap is the
occluder.

**The roadmap bullet named the wrong occluder.** It blamed the sky wall in the
56–192 gap on line 16. Measured 2026-10-01 on a scratch build of `1ab95f8`:

| Probe build | Facade above the wall, Solid and Ultra |
|---|---|
| unchanged | cut off |
| gap sky wall removed | cut off; frame unchanged |
| gap sky wall and every sky ceiling cap removed | drawn |

The gap wall still has to go. Once the lids are raised (§4.1), a view ray from
the lower area crosses line 16 above height 56 and meets the gap wall instead.
Measured on the same fixture in Solid: raised lids with the gap wall kept
changed 1151 pixels (difference over 24, rows 0–439) against the unchanged
build, and the facade stayed cut off; with it removed, 33170. §4.3 removes it.

**What the cap protects.** DOOM-0141's report was geometry floating against the
sky on E1M2. An outdoor area's outer wall stops at that area's ceiling height.
Without a lid, a view ray passes over it and sees the rest of the map. So a
fix may not simply delete the caps. With every cap removed, the fixture shows
the building and also the tops of walls beyond the courtyard that Classic does
not show.

**How often each case occurs.** Census of every map in both IWADs, one command
over the map data (`census.py`, scratch, reads `LINEDEFS`, `SIDEDEFS` and
`SECTORS`):

| Line class | doom.wad | doom2.wad |
|---|---|---|
| two sky ceilings of different height (this bug) | 244 | 256 |
| sky sector beside a roofed sector with a HIGHER ceiling (§4.4) | 2 | 6 |
| one-sided line of a sky sector (must stay sealed) | 2246 | 1815 |

Of the eight lines in the second row, one carries a textured upper wall on the
roofed side: E3M9 line 154 (`SP_HOT1`). The other seven are untextured, so
DOOM-0142 fills them with sky.

**What the sky's shading reads.** Neither view shades sky from where the sky
surface is. Ultra's sky hit takes the same branch as a miss and shades the
panorama from the ray's direction; its fog is `skyFogOpticalDepth(origin, dir,
strength)`, which reads the eye and the direction only. Solid's sky branch in
`mesh.frag` samples the panorama by screen position. So moving a sky surface
changes which pixels are sky, never what colour a sky pixel is.

## 3. Scope decisions

- **Match Classic's sky hack for the case the user reported; do not reproduce
  its clipping exactly.** Classic clips at the nearest sky-to-sky line, so in a
  chain of three sky areas of rising height it can hide the third area's walls
  where a true 3D view shows them. And from the taller side it clips at the
  lower ceiling only where the two sectors' light levels differ, because
  `R_StoreWallRange` sets `markceiling` on a light change. The design below
  shows what lies beyond in every one of these cases. Decided by the
  author, not the user: the bullet's goal is "where Classic draws it", and this
  shows at least what Classic shows. Recorded so the divergence is not
  re-reported as a defect.
- **Moving sky ceilings stay out of scope.** The sky list is built once per
  level and never re-heighted, today and after this change (§10 Q1).
- **The DOOM-0142 step fills are not revisited.** The user chose the flat fill
  on 2026-09-30.

## 4. Design

### 4.1 One lid height per level

`RB_BuildLevelMesh` computes `lid`, the highest ceiling among sectors whose
ceiling is sky, before the seg loop. A level with no sky ceiling has no lid and
emits no sky ceiling cap and no seal.

Every sky ceiling cap is emitted at `lid`, not at its sector's ceiling height.
Sky floor caps are unchanged.

Why one level-wide height: every sky area is a vertical column closed at the
top by the lid and on each side by a real wall or a seal (§4.2). Two sky areas
sharing a line share their column above the lower ceiling, which is the sky
hack. A per-area lid would need a seal between two sky areas of different lid,
which is the wall this spec removes (§9).

### 4.2 The seal

For every seg whose front sector has a sky ceiling, and whose back sector is
missing or has a non-sky ceiling, emit a sky wall on that seg from the front
sector's ceiling up to `lid`. It stands where the lid used to be enough: above
an outer wall, and above a window or door into a roofed sector.

A seg whose back sector also has a sky ceiling gets no seal. That line is where
the view passes from one sky column into the next.

A zero-height seal (front ceiling equal to `lid`) is not emitted, as
`emit_sky_wall` already refuses one.

### 4.3 The upper step between two sky ceilings

The upper-step branch in the seg loop emits nothing when both ceilings are sky.
It no longer calls `emit_sky_wall`. The other branches keep their outcome:

| Front higher than back, and… | Emits |
|---|---|
| both ceilings sky | **nothing** (was a sky wall) |
| a top texture | the textured wall |
| no top texture, back ceiling sky | a sky wall (DOOM-0142) |
| no top texture, back ceiling not sky | the back ceiling's flat (DOOM-0142) |

### 4.4 The offset seal

Where the back sector's ceiling is higher than the sky front's, the back side
of the line can carry a wall between the two ceilings: a textured upper, or
DOOM-0142's fill. The seal occupies the same span of the same plane.

From the sky side there is no conflict: DOOM-0267 skips the back of a wall
face, so the seal shows. From the roofed side the wall faces the viewer, and
the seal is at the same distance. Sky surfaces are not subject to
DOOM-0267's rule, and Ultra's tracer keeps either of two hits at one distance.
So such a seal is moved `RB_SKY_SEAL_OFFSET` map units (1/8) off the line,
into its front sector: along the seg's right-hand perpendicular
`(dy, -dx) / len`. That is the negation of the normal `emit_sky_wall` builds,
`(-dy, dx) / len`, which points into the back sector; moving along that normal
would put the seal in front of the wall. The wall then wins from the roofed side and the seal
from the sky side, in both views, by distance rather than by tie order.

Measured on E3M9 line 154, from inside the roofed sector
(`-warp 3 9 -warpto 200 128 180`), against the unchanged build, counting pixels
differing by more than 24 above the weapon (rows 0–439). Ultra `-rtview 3`:
with the offset, 0; without it, 169, all on the lintel. Solid: 0 either way,
since the raster sky draw loses the tie to the wall drawn before it.

A one-sided seg is never offset: nothing lies behind it above the front
ceiling.

### 4.5 Where the decisions live

The three decisions go in a new header, `sky_seal.h`, so
`tests/sky_seal_test.cpp` can test them with no WAD or GPU, as `seg_project.h`
is tested. `r_mesh.c` calls them and keeps the emitting.

```c
#define RB_SKY_SEAL_OFFSET 0.125f   /* map units, into the front sector */

/* The level's sky lid: the highest ceiling among sectors whose ceiling is
   sky. Returns 0, leaving *lid alone, when no ceiling is sky. */
static int RB_SkyLidHeight(const int* ceilingheight,
                           const unsigned char* ceilingIsSky,
                           int numsectors, int* lid);

/* The seal on one seg. Returns 1 and fills [*zb, *zt] and *offset (1 = move
   RB_SKY_SEAL_OFFSET into the front sector) when a seal is emitted. */
static int RB_SkySeal(int frontCeilIsSky, int frontCeil,
                      int hasBack, int backCeilIsSky, int backCeil,
                      int lid, int* zb, int* zt, int* offset);

/* What the upper step between front and back emits, given front's ceiling
   is higher. */
enum { RB_UPPER_NONE, RB_UPPER_WALL, RB_UPPER_SKY, RB_UPPER_FLAT };
static int RB_UpperStepKind(int frontCeilIsSky, int backCeilIsSky,
                            int hasTopTexture);
```

Heights are `fixed_t` values passed as `int`, so the header needs no engine
include. `emit_sky_wall` gains the offset as a parameter. Its seg-line
projection (`seg_line_xy`, DOOM-0180) is unchanged, and the offset is applied
after it.

## 5. Data & resources

The sky list grows by the seals and shrinks by the removed gap walls. No new
buffer, no new file, no config key. The sky BLAS and the raster sky draw read
the same list as today.

## 6. Performance budget

Sky triangles per level, the engine's own `built BLAS` log line, prototype
against unchanged, at the nine outdoor starts of §7 B3:

| Map | before | after |
|---|---|---|
| E1M1 | 117 | 149 |
| E1M2 | 173 | 281 |
| E3M6 | 638 | 928 |
| E4M3 | 437 | 441 |
| E4M6 | 863 | 1323 |
| MAP01 | 161 | 203 |
| MAP13 | 428 | 814 |
| MAP19 | 958 | 974 |
| MAP25 | 352 | 404 |

E4M6's acceleration-structure total went from 2456.8 to 2528.1 KiB on the same
line. The sky list is built once per level. Per frame, Solid draws it and Ultra
traces it, so each frame meets the extra triangles above and nothing else.
Budget: at the nine starts, no more than twice the unchanged build's sky
triangles, read from the `built BLAS` line at B3. Frame time is not budgeted
and nothing measures it.

## 7. Build order

- **B1 — `sky_seal.h` and its test.** Write `tests/sky_seal_test.cpp` first,
  against a stub header whose functions return the pre-fix outcomes; see it
  fail; then write the header. *Verify:* `make test` fails on the stub and
  passes on the header (INV-1 to INV-4).
- **B2 — wire `r_mesh.c`.** Lid before the seg loop; seal in the seg loop,
  before the one-sided `continue`; upper step through `RB_UpperStepKind`; sky
  ceiling caps at `lid`; `emit_sky_wall`'s offset. *Verify:* `make DEV=1` and
  `make test` build with no warning; the E3M1 fixture shows the facade (INV-5).
- **B3 — regression captures.** The nine outdoor starts (player 1's start and
  angle on E1M1, E1M2, E3M6, E4M3, E4M6, MAP01, MAP13, MAP19, MAP25), Solid,
  before and after. *Verify:* INV-6's bound; the E3M9 line 154 capture in
  Ultra (INV-4);
  §6's triangle counts re-read from the log.
- **B4 — gates.** `-rtverify` PASS; Vulkan validation silent in Solid and
  Ultra; the 68-map boot sweep; the five demo fixtures. *Verify:* each reports
  as it did at `1ab95f8`.

## 8. Invariants

- **INV-1** — every sky ceiling cap sits at the level's lid, the highest sky
  ceiling. *Breaks when:* a cap is emitted at its own sector's height, or the
  lid ignores a sky sector. *Test:* `tests/sky_seal_test.cpp` checks
  `RB_SkyLidHeight` on E3M1's four courtyard sectors (56, 192, 128, 128 → 192),
  on a set with no sky (returns 0), and with a non-sky sector taller than every
  sky one (ignored); the same file scrapes `r_mesh.c` for the ceiling
  `emit_sky_cap` call taking the lid rather than `ceilingheight`.
- **INV-2** — a seg whose front ceiling is sky gets a seal from that ceiling to
  the lid exactly when it has no back sector or its back ceiling is not sky.
  *Breaks when:* a sky-to-sky line is sealed (the reported bug returns), or an
  outer wall is not (DOOM-0141's floating geometry returns). *Test:*
  `tests/sky_seal_test.cpp`, the four combinations of back missing, back sky,
  back not sky below, and back not sky above, plus front not sky (no seal) and
  front ceiling equal to the lid (no seal).
- **INV-3** — the upper step between two sky ceilings emits nothing; the other
  three rows of §4.3 are unchanged. *Breaks when:* the DOOM-0141 gap wall comes
  back, or a DOOM-0142 fill changes kind. *Test:* `tests/sky_seal_test.cpp`,
  one check per row of §4.3.
- **INV-4** — a seal is offset into its front sector exactly when a back sector
  exists with a higher ceiling, along the direction §4.4 names. *Breaks when:*
  the offset is dropped or pushed into the back sector (the lintel tie of §4.4
  returns, or the seal covers the lintel), or applied to every seal. *Test:*
  `tests/sky_seal_test.cpp` for the rule; the direction only by the E3M9 line
  154 capture of §4.4, by hand at B3 in Ultra (`renderer 1 -rtview 3`; Solid
  reads 0 either way), expecting 0 changed pixels above row 440 against the
  unchanged build.
- **INV-5** — at the E3M1 fixture the building's face shows above the courtyard
  wall in Solid and in Ultra. *Breaks when:* any sky surface still stands
  between the courtyard and line 8 above height 56. *Test:* by hand at B2:
  `-warp 3 1 -warpto 192 -980 90`, `renderer 2` and `renderer 1 -rtview 3`,
  captured before and after. The prototype changed 33170 (Solid) and 20770
  (Ultra) pixels by more than 24 in rows 0–439, all inside the facade's box.
- **INV-6** — at the nine outdoor starts of B3, Solid changes by no more than
  0.05% of pixels (difference over 24) against the unchanged build. *Breaks
  when:* a seal is missing somewhere a start looks, so geometry floats. *Test:*
  by hand at B3. The prototype measured 0.00% to 0.01%.

## 9. Alternatives considered (and rejected)

- **Delete the gap sky wall only** (the roadmap's own lead). Measured: the
  frame does not change (§2). The cap is the occluder.
- **Delete the sky ceiling caps.** Shows the building, and also the tops of
  walls beyond the courtyard that Classic hides (§2). It undoes DOOM-0141.
- **Raise each sky area's lid only to its tallest sky neighbour.** Two
  neighbouring areas can then have different lids, and the gap between them
  needs a wall, the same wall that caused the problem from the taller side. A
  chain of areas needs the raise carried across it, which is the level-wide
  lid with more code.
- **Keep the lids and let a sky hit continue the ray into a taller sky area.**
  Works in Ultra alone: Solid draws the sky list with the depth test and has no
  way to continue past it.
- **Make sky walls one-sided, instead of offsetting §4.4's seal.** It would
  resolve the tie with no offset, but it changes every sky wall in both views
  (the sky instance's culling in Ultra, the discard in `mesh.frag` in Solid)
  to fix a case that occurs once in the stock maps with a textured wall. The
  offset touches only that case.

## 10. Open questions

- **Q1 — a sky ceiling that moves.** Neither the caps nor the seals follow a
  moving sector, before or after this change: the sky list carries no height
  tag. A sky ceiling that rises above the lid would poke through it. No stock
  map has been checked for one. Deferred; not yet queued.

## 11. What checks this

| Rule | What catches a breach |
|---|---|
| INV-1 lid height | `tests/sky_seal_test.cpp`; the cap call by scrape |
| INV-2 seal rule | `tests/sky_seal_test.cpp` |
| INV-3 upper step | `tests/sky_seal_test.cpp` |
| INV-4 offset | `tests/sky_seal_test.cpp`; the direction by the E3M9 capture in Ultra, by hand at B3 |
| INV-5 the fixture | the capture by hand at B2. Nothing in `make test` renders |
| INV-6 no floating geometry | the nine captures by hand at B3. Nothing re-runs them |
| §6's budget | the `built BLAS` log line, read by hand at B3 |

## 12. Cross-doc impact

- `ROADMAP.md`, DOOM-0322: a note that the root cause it records is wrong and
  this spec is its contract (written through `roadmap_log`).
- `CHANGELOG.md`, Unreleased, Fixed: one entry at B4.
- No other spec states the gap wall or the cap height as a rule; the DOOM-0011
  and DOOM-0310 specs mention the sky backdrop only for its fog, which §2 shows
  is unaffected.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0322-sky-lid-height-loop-log.md`.
