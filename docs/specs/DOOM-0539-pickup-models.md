<!-- ants-spec-format: 1 -->
# DOOM-0539 — Draw Ultra's pickups as real Blender models, with lights placed inside them

**Status:** spec draft (2026-11-02).
**Kind:** feature.
**Source:** ROADMAP DOOM-0539 (user request 2026-11-02).
**Pairs with:** DOOM-0112 (glowing collectibles), DOOM-0482 (pickup light reach).
**Blocked by:** DOOM-0178 for build step B4 only: the traced denoised view does not read an HD
emissive map, so a model's glowing parts cannot show there until it does.

**Layman:** Health, armour, ammo, keys and power-ups stop being flat pictures and become real 3D
objects, and their glow comes from an exact spot inside each one. They are only for show: picking
them up and walking past them work as they always have.

## 1. Goal

In both of Ultra's views, every pickup is drawn as a real model built in Blender, lit by the scene,
and casting its own light from points set inside the model. Nothing about play changes.

## 2. Where this sits

`RB_BuildSprites` in `r_mesh.c` refills one per-frame vertex buffer. `sprite_quad` writes six
`rb_vertex_t` per Thing. The rasterised view draws that buffer through `mesh.frag`, and the traced
view builds its sprite BLAS from the same buffer (`g.sprWorldBuf`). So a triangle added there
reaches both views.

Sprite light has one route. `BuildDynamicEmitters` in `r_vulkan.cpp` walks that buffer's triangles,
and those flagged `RB_MESH_EMISSIVE` become emitters, with colour from `g.matEmissive` per material.
The rasterised view's point lights derive from the same list.

There is no model loader. `rb_vertex_t` flags name a billboard (`RB_MESH_SPRITE`), and a sprite's
material is paletted: `mesh.frag` sets `worldSurf` false for sprites, and `materials.csv` resolves
only wall and flat names (`ResolveDoomName`).

## 3. Scope decisions

- **Real models, not pre-rendered sprites.** User, 2026-11-02: new models for all the pickups, so
  that light sources can be set accurately within them.
- **Ultra only; Solid keeps its sprites.** Follows from `CLAUDE.md` § Render tiers: Solid enhances
  DOOM's art and Ultra substitutes for it, and a model is substituted art. Classic is untouched.
- **Every pickup sprite, in both IWADs.** The set is every sprite whose `mobjinfo` carries
  `MF_SPECIAL`, listed in §5. Monsters, barrels and lamps stay out (DOOM-0080).
- **Show only.** A model never blocks anything: no game code reads it. It stays inside the space
  the sprite occupied, per INV-3.

## 4. Design

### 4.1 Authoring

One builder per model in `/mnt/Games/3D Engine Assets/Blender Scripts/pickups/`, sharing a
`lib/pickupbake.py`. A copy of each `.blend` goes to `Models/Props/Pickups/`. The script writes the
game's files to `assets/ultra/models/`:

- `<SPR>.dmdl`, the mesh and lights (§4.2);
- `<SPR>_alb.png`, `_nrm.png`, `_emis.png`, an atlas on the model's UVs.

A Blender empty named `light_<anything>` is a light. Its custom properties `rgb` and `reach` give
its colour and range. The model's origin is the Thing's floor point. One Blender unit is one map
unit. A lighting artist therefore places a light by moving an empty.

### 4.2 The file

Little-endian, written by the exporter and read by a new `rb_model.h`, header-only so a test reaches
it without the engine.

```
char     magic[4]   "DMDL"
uint32   version    1
uint32   numVerts   1..RB_MODEL_MAX_VERTS
uint32   numLights  0..RB_MODEL_MAX_LIGHTS
float    verts[numVerts][8]    x y z, nx ny nz, u v; three per triangle, no index list
float    lights[numLights][7]  x y z, r g b, reach
```

`RB_MODEL_MAX_VERTS` is 900, so 300 triangles. `RB_MODEL_MAX_LIGHTS` is 4.

### 4.3 Drawing

`assets/ultra/models.csv` lists `sprite,file`. `RB_BuildSprites` gets one more branch: under
`RB_RT3D`, a Thing with `MF_SPECIAL` whose sprite has a loaded model emits that model's triangles,
placed at the Thing and turned by a fixed angle from its map position, in place of `sprite_quad`.
All of a sprite's animation frames share one model. Sector light and `FF_FULLBRIGHT` are worked out
as `sprite_quad` works them.

These vertices carry `RB_MESH_SPRITE | RB_MESH_MODEL`, a new flag at `0x400`, with `texnum` the
sprite's first lump, so the existing id arithmetic holds. `mesh.frag` and `pathtrace.comp` treat
`RB_MESH_MODEL` as a world surface: HD maps apply, palette index 0 does not discard, and the
sprite-only alpha test does not run. `ctrl[]` already has a row for every sprite id, and
`EnsureHdMaterials` fills the model's row from `models.csv`.

### 4.4 Lights

For each light, the Thing emits one small triangle (2 map units across) at the light's position,
flagged `RB_MESH_EMISSIVE | RB_MESH_MODEL`, carrying its colour and reach, and never drawn or traced
as a surface. `BuildDynamicEmitters` reads colour and reach from these instead of `g.matEmissive`,
so the traced view's emitters and the rasterised view's point lights both come from them. A model's
shell does not shadow its own light (INV-5). Glow shown on the model itself comes from `_emis.png`.

### 4.5 Distance and budget

At most 12000 model triangles per frame, nearest Things first. Beyond that budget, or beyond 1024
map units, a Thing draws as its sprite. Its lights stay either way, so the room's light does not
change with distance.

## 5. Data & resources

Pickup sprites, from the game's own table, so an inventory test can compare (INV-2):

`AMMO ARM1 ARM2 BFUG BKEY BON1 BON2 BPAK BROK BSKU CELL CELP CLIP CSAW LAUN MEDI MEGA MGUN PINS PINV
PLAS PMAP PSTR PVIS RKEY ROCK RSKU SBOX SGN2 SHEL SHOT SOUL STIM SUIT YKEY YSKU`

Each atlas is at most 512x512, so at most 5.3 MB of video memory per map and 16 MB per model, inside
`kHdBudgetMB`'s accounting. `g.sprWorldVertCap` grows to hold the 12000-triangle budget. No new
dependency.

## 6. Performance budget

Ultra keeps its floors in `docs/standards/performance.md`. Against the same build with `models.csv`
absent, same map, same render scale: at most 1.0 ms added to the frame in the traced view and 0.5 ms
in the rasterised view, at E1M1's start and at E2M2 `790 1472 180`. If the traced view
misses it, build step B5 moves models to one BLAS per model plus TLAS instances.

## 7. Build order

- **B1** `rb_model.h` and its test; the exporter and one model, `BON1`. Check: `make test`; the file
  loads.
- **B2** Drawing in the rasterised view (§4.3). Check: E1M1 capture shows the model; Solid unchanged.
- **B3** Traced view, surfaces only. Check: `-rtverify` passes; the model is hit, lit and shadowed.
- **B4** Lights (§4.4), after DOOM-0178. Check: the floor under `BON1` takes its colour in both views.
- **B5** Budget and distance (§4.5); measure §6.
- **B6** The other sprites, in batches the user sees between.

## 8. Invariants

- **INV-1** — Play is untouched: no game-logic file reads a model. *Test:*
  `grep -l rb_model linuxdoom-1.10/p_*.c` → no output. *Breaks when:* a model's size reaches a blocking or pickup check.
- **INV-2** — Every pickup sprite has a model once B6 is done, and the list stays complete. *Test:*
  `tests/pickup_models_test.cpp` compares `models.csv` against `MF_SPECIAL` in `info.c`. *Breaks
  when:* a sprite in §5 has no row, or a new `MF_SPECIAL` entry lands without one.
- **INV-3** — A model stays inside the sprite's space: its footprint within the Thing's `radius`,
  its top within 1.25 times the sprite's height. The player can never see a part stand in their
  way that the sprite did not fill. *Test:* `tests/pickup_models_test.cpp` reads each `.dmdl`'s
  bounds. *Breaks when:* a builder scales a model past those bounds.
- **INV-4** — A malformed `.dmdl` is refused and that sprite draws as its sprite; it never crashes or
  reads out of bounds. Assets are read from a user-settable folder (`DOOMASSETDIR`), so this crosses
  a trust boundary. *Test:* `tests/rb_model_test.cpp`: bad magic, wrong version, counts over the
  caps, a truncated body, NaN or infinite coordinates. *Breaks when:* a count is trusted before the
  size is checked.
- **INV-5** — A light inside a model lights the room: its own shell does not block it. *Test:* a B4
  capture of `BON1` in a dark sector, Ultra traced and rasterised, with and without `lights`, floor
  mean compared against a same-build control. *Breaks when:* shadow rays hit the model that holds
  the light.
- **INV-6** — Solid and Classic are unchanged. *Test:* `scripts/ab_capture.sh` at E1M1 against the
  commit before B2, plus a same-build control, in `renderer 2` and in Classic: signal within noise.
  *Breaks when:* the model branch is not gated on `RB_RT3D`.
- **INV-7** — With no `models.csv`, Ultra draws sprites as it does today. *Test:* the same capture
  with `DOOMASSETDIR` holding no `models.csv`, in Ultra's rasterised view. *Breaks when:* the sprite
  path comes to depend on a loaded model.
- **INV-8** — A pickup's light does not change as the player walks away. *Test:* emitter count printed
  by the existing `BuildDynamicEmitters` diagnostic, near and past 1024 units from the same pickup:
  equal. *Breaks when:* a light is tied to the model being drawn.
- **INV-9** — The frame budget in §6 holds. *Test:* `-frameledger` on both arms as §6 sets them,
  medians compared. *Breaks when:* the budget or the distance cap is lifted.

## 9. Alternatives considered (and rejected)

- **Render each model to sprite frames.** No engine work, but a flat card has no inside, so it cannot
  give the accurate light positions the user asked for, and it stays flat from every angle.
- **glTF through a vendored parser.** A standard format, but a parser and a dependency to serve fields
  this project does not use. The exporter is the only writer, so a small fixed format is shorter.
- **One BLAS per model from the start.** Cheaper per frame in the trace, but it needs per-instance
  material and vertex lookup in the hit path. Kept as the fallback in §6, on measurement.
- **Solid too.** Rejected by the tier rule in §3.

## 10. Open questions

- Should pickups turn slowly? They stand still as designed, because the sprites do. It is one line to
  add if the user wants it after seeing B2.
- Powerups' sprite frames pulse. Whether `_emis.png` should pulse with `thing->frame` is left to the
  B4 look.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | **nothing** automatic — the grep by hand at B6 |
| INV-2 | `tests/pickup_models_test.cpp` |
| INV-3 | `tests/pickup_models_test.cpp` |
| INV-4 | `tests/rb_model_test.cpp` |
| INV-5 | **nothing** automatic — a capture at B4; re-run by hand |
| INV-6 | **nothing** automatic — `scripts/ab_capture.sh` by hand at B2 and B6 |
| INV-7 | **nothing** automatic — a capture by hand at B2 |
| INV-8 | **nothing** automatic — the emitter diagnostic by hand at B5 |
| INV-9 | **nothing** automatic — `-frameledger` by hand at B5 and B6 |

## 12. Cross-doc impact

`docs/standards/assets.md` gains the models folder and its licence. `assets/ultra/LICENSES` gains each
model. `CHANGELOG.md` on shipping. DOOM-0080's body points here for the file format.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0539-pickup-models-loop-log.md`.
