<!-- ants-spec-format: 1 -->
# DOOM-0642 — Honest light: every lit thing is lit by a source the player can find

**Status:** building; B1 and B2 built (2026-11-21), B3 built (2026-11-27).
**Kind:** feature.
**Source:** ROADMAP DOOM-0642 (user requests 2026-11-20).
**Delivers:** DOOM-0209 (candles and candelabra cast light), as its first stage.
**Pairs with:** DOOM-0170 (the rasterised lighting stack), DOOM-0009 (the path tracer),
DOOM-0103 (the gleam a dark room needs), DOOM-0044 (the player's torch).

**Layman:** Today a room is lit evenly from nowhere, by a brightness number the 1993 map gives
each room. This makes light come only from things that glow: lamps, candles, screens, lava, the
sky, gunfire and the player's torch. A room with none of those is dark. It is built in three
stages so the game is never left half-lit: first every lamp and candle really casts light, then
monsters and the weapon take that light, and only then is the light from nowhere removed.

## Contents

1. Goal · 2. Where this sits · 3. Scope decisions · 4. Design · 5. Data & resources ·
6. Performance budget · 7. Build order · 8. Invariants · 9. Alternatives · 10. Open questions ·
11. What checks this · 12. Cross-doc impact · 13. Loop log

## 1. Goal

In Solid and Ultra, in both the rasterised and the traced view, a surface, a monster and the
weapon receive light only from a source: an emitting wall or floor, a light Thing, the sky, the
muzzle flash, the torch, and the bounce of those. Tuning the look comes after this, not before
(user, 2026-11-20).

## 2. Where this sits

Light with no source today, each read in the code on 2026-11-20 (the roadmap item holds the
list):

- Rasterised: the `sect` term in `mesh.frag` (the room's light through `horrorSectorShade`,
  times `BASE_SECTOR_DIM`) on surfaces, sprites and the weapon; and `pc.extralight` in
  `mesh.vert`, which lifts the whole view when the player fires.
- Traced: the ambient term in `primaryHitAddTerms` (`AMBIENT_SECTOR_SCALE`, `AMBIENT_FLOOR`),
  taken as a max with the baked bounce; and `kPuddleGlow`.

Light that has a source, and its gaps:

- **Candles and candelabra cast nothing by the emitter gate; B1 puts them on the faint
  path.** `emis::derive_material_le` refuses a picture with
  too few near-white texels (`kEmitterMinBrightTexels`). Scratchpad `d0642/lightle.py` runs
  that rule over `doom.wad`: `CANDA0` and `CBRAA0` come out at zero, and the lamp, the torches
  and the burning barrel pass. In Ultra the candelabra's model carries a light of its own, so
  the gap there is the candle alone. E2M1's first corridor holds only candles, which is why it
  went black under `-sourcedlight` before B1.
- **Sprite lights are direct light only.** `bake.comp` bakes the bounce of wall and floor
  emitters; a candle's light never bounces.
- **A sprite takes no point light in the rasterised view.** The point-light block in
  `mesh.frag` skips `FLAG_SPRITE` and `FLAG_PSPRITE`, and reads its subsector from
  `pc.triSs`, which holds world triangles only. So a monster and the weapon have `sect` and
  nothing else.
- **A sprite takes no bounce in either view.** `primaryHitLighting` gives a sprite no probe.
- **Some screens are not lights.** A wall emits only if `wall_light_tex` in `r_mesh.c` names
  it. E1M1's wall of screens went dark under `-sourcedlight`.

`-sourcedlight` (`SourcedLightOnly` in `r_vulkan.cpp`) already switches most of the sourceless
terms off for a test run. Its measurements are on the roadmap item.

## 3. Scope decisions

- Every map, no special rooms; honest light first, tuning second (user, 2026-11-20).
- The order of the three stages in section 7 (user, 2026-11-20).
- The sky is a source. The baked bounce keeps its sky fill (the test switch's choice,
  2026-11-20).
- Solid and Ultra both get all of it, per `CLAUDE.md`'s render tiers. Classic is untouched.
- A fullbright sprite still shows itself bright. A fireball is its own source.
- The old light stays as a test switch only, never a menu option (user, 2026-11-21).
- Whether the torch starts switched on is decided at B7's sheets (user, 2026-11-21).
- A room's flicker and strobe will drive the lamps in it, as DOOM-0643, after B7 (user,
  2026-11-21).
- Decided here, not yet seen by the user: stage 3 becomes the default only after the user has
  looked at before-and-after sheets of it.

## 4. Design

### 4.1 Stage 1 — every light Thing and screen casts light

- **Candles and candelabra.** Their sprite lumps join the lumps that may emit faintly
  (`RB_SpriteLumpGlows`, the `allowFaint` path), so their flame texels set their light, as a
  pickup's glow does. No new table: strength comes from the art until tuning.
- **Key-door strips (DOOM-0646, built 2026-11-21).** `DOORBLU`, `DOORRED`, `DOORYEL` and
  their skull forms are on `wall_light_tex`. The plain blue and red strips have no texel
  over `kBrightLum`, so `wall_dim_light` in `r_mesh.c` names them and they are read again
  at `kDimLightBrightLum` (`emis::derive_listed_wall_le`). `wall_light_cast` sets the blue
  and red strengths. `COMPUTE2` is listed and derives nothing in the same way; it is not
  named dim, so it still casts nothing.
- **Screens.** A capture sheet of each map's computer and screen textures that are not on
  `wall_light_tex` goes to the user; the ones the user calls lights join the list.
- **Bounce from light Things.** Light Things that never move (candles, lamps, torches, the
  burning barrel) join the emitters `bake.comp` bakes from. A barrel that can be shot away is
  the edge: its bounce would outlive it, so it stays direct light only.

### 4.2 Stage 2 — monsters and the weapon take sourced light

- **A Thing carries its subsector.** `RB_BuildSprites` writes the Thing's subsector into a
  spare field of its vertices. Which field is the builder's choice.
- **Rasterised view.** With that subsector a sprite fragment runs the same point-light loop
  and the same probe bounce as a world surface, facing the light (no Lambert term: a billboard
  has no real normal). A model already takes point lights and gains the bounce.
- **Traced view.** A sprite hit reads its probe by the same field, so a monster gains the
  bounce it lacks today.
- **The weapon.** It is lit as a sprite standing where the player stands: the camera's
  subsector, the camera's position. The muzzle flash and the torch do not light it.
- **Gunfire in the rasterised view.** While `extralight` is set, the muzzle flash joins the
  rasterised point lights at the player's position, as the traced view's `muzzleFlashDelta`
  already places it. `pc.extralight`'s whole-view lift stays until stage 3.

### 4.3 Stage 3 — remove the light with no source

- The default becomes what `-sourcedlight` shows today, and further: the weapon's sector light
  and `pc.extralight`'s lift go too. The constants named in section 2 leave the shaders.
- `-sourcedlight` is replaced by its opposite, a test switch that restores the old light, kept
  for before-and-after captures.
- The inventory is run again over fog, bloom and the visor before the removal, because each
  reads the room's light (section 12).

## 5. Data & resources

No new file and no new format. One more field in use on a sprite vertex. More sprite emitters
per frame, inside `SPR_EMIT_MAX`. A longer static emitter list for the bake.

## 6. Performance budget

Solid's rasterised view is the smoothest way to play and stays so. Stage 2 adds a point-light
loop to every sprite fragment. Budget: the median frame time of Solid rasterised, render scale
100, at E1M1's start and at a spot with a crowd of monsters, rises by no more than 5 percent,
measured with `-frameledger` on a quiet machine. Over budget, the sprite's light moves to its
vertices.

## 7. Build order

- **B1** — candles and candelabra emit. Check: E2M1's first corridor under `-sourcedlight`.
  Built.
- **B2** — the screen sheet, the user's pick, the list. Built.
- **B3** — static light Things in the bake. Built: `RB_BuildBakeLights` in `r_mesh.c`;
  `RunGiBake` feeds them as omni lights after the wall and floor set.
- **B4** — the subsector on sprite vertices; the bounce on sprites in the traced view.
- **B5** — point lights and bounce on sprites in the rasterised view; the budget check.
- **B6** — the weapon; the rasterised muzzle flash.
- **B7** — sheets of today against stage 3 for the user. On the user's yes: the removal, the
  amendments in section 12.

Each of B1 to B6 ships on its own and leaves the default look no darker.

## 8. Invariants

- **INV-1** — Classic is unchanged. *Breaks when:* a change reaches the software renderer.
  *Test:* `scripts/ab_capture.sh` in Classic at E1M1's start, before B1 against after each
  stage: no pixel differs.
- **INV-2** — Every light Thing casts light in both views, in Solid and in Ultra: candle,
  candelabra, floor lamp, both tall lamps, the six torches, the burning barrel. *Breaks when:*
  a light Thing's sprite derives to zero and nothing else gives it light. *Test:* a case in
  `tests/emissive_derive_test.cpp` feeding a candle-sized tile with two peak texels through
  the faint path: its light is above zero. The rule it isolates is the peak gate; without the
  faint path the same tile returns zero.
- **INV-3** — After B5, a monster standing beside a lamp is lit by it in the rasterised view,
  and a monster in a room with no source is dark. *Breaks when:* the sprite's subsector is
  wrong or unset. *Test:* two captures under `-sourcedlight`, a monster by a lamp and one in
  an unlit room; the mean brightness of the monster's pixels is well above black in the first
  and near black in the second. The spots are picked in B5.
- **INV-4** — After B7, no shader adds light from the room's brightness number. *Breaks when:*
  a sourceless term is left or comes back. *Test:* `grep -c "BASE_SECTOR_DIM\|AMBIENT_FLOOR\|AMBIENT_SECTOR_SCALE\|kPuddleGlow" linuxdoom-1.10/shaders/mesh.frag linuxdoom-1.10/shaders/pathtrace.comp`
  prints zero for both files. Today it prints a count above zero for each.
- **INV-5** — The budget in section 6 holds. *Breaks when:* the sprite light loop costs more
  than it. *Test:* `-frameledger` medians, before B5 against after.
- **INV-6** — Until B7's removal, no stage makes the default look darker. *Breaks when:* a
  stage removes or dims a term early. *Test:* `scripts/ab_capture.sh` at the four spots the
  roadmap item measured, before against after: the view's mean brightness does not fall.

## 9. Alternatives considered (and rejected)

- **A table of light Things with a hand-set colour and strength each** (DOOM-0209's first
  idea). Rejected for now: the user asked for honest light before tuning, and the art already
  says what colour a flame is. It is the natural home for tuning later.
- **Remove the sourceless light first.** Rejected by the user's order: it leaves monsters
  black and candle rooms unplayable until the rest lands.
- **Light a sprite on the CPU, per Thing.** Kept as the fallback in section 6. The fragment
  loop reuses the world's code and lights a tall sprite unevenly, as a real light would.

## 10. Open questions

None. The three asked of the user are decided in section 3.

## 11. What checks this

`make test` for INV-2. Captures and `-frameledger` for the rest; no golden covers them.
Files touched: `r_mesh.c`, `r_vulkan.cpp`, `shaders/mesh.frag`, `shaders/mesh.vert`,
`shaders/pathtrace.comp`, `shaders/bake.comp`, `tests/emissive_derive_test.cpp`,
`tests/bloom_threshold_test.cpp`.

## 12. Cross-doc impact

B7 changes clauses in force elsewhere; each is amended in the same commit as the code:

- DOOM-0170 §4.1: the base shade and `BASE_SECTOR_DIM`.
- DOOM-0331 INV-4: its bound rests on sector light, and `bloom_threshold_test` holds
  `kAmbientSectorMax` to `BASE_SECTOR_DIM`.
- DOOM-0455: `visorLitFade` reads `sect` against `BASE_SECTOR_DIM`.
- DOOM-0183: `kPuddleGlow`.
- DOOM-0009: the muzzle flash's `extralight` path in the rasterised view.
- DOOM-0651: the traced fog's sky light follows sight (`skyT` in `pathtrace.comp`) only
  under `sourcedLightOnly()`, because of INV-6. B7 makes it the default.
- `docs/standards/renderer.md` and `CLAUDE.md`'s tier table, where they describe the light.

## 13. Cold-eyes loop log

[`docs/reviews/DOOM-0642-honest-light-loop-log.md`](../reviews/DOOM-0642-honest-light-loop-log.md).
No review has run. Rule 14 asks for none: this spec is owed for its design choices and its
reach, not for a contract or anything hard to undo.
