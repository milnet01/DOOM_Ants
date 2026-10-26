<!-- ants-spec-format: 1 -->
# DOOM-0273 — Give the original textures surface detail, made from the player's own WAD

**Status:** accepted (2026-10-26).
**Kind:** feature.
**Source:** ROADMAP DOOM-0273 (user request 2026-07-27, the Solid tier's
definition in `CLAUDE.md` § Render tiers; joint design with DOOM-0103 chosen
by the user 2026-10-26, after the DOOM-0511 play-test).
**Pairs with:** DOOM-0103 (`docs/specs/DOOM-0103-specular-lobe.md`), which
reads the roughness this spec supplies. **Blocker for:** DOOM-0103.
**Amends:** DOOM-0042 (what changes there is listed in §12 of this spec).

**Layman:** DOOM's walls and floors are flat pictures. After this, the game
works out from each picture where its bricks, cracks and tiles stand out, so
light catches their edges, while the pictures themselves stay exactly as they
were.

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

When a level loads, every wall texture and flat the WAD declares gets a
surface map made from its own pixels: a normal, a height and a roughness.
Solid uses them in both views. The ray-traced view shades with the normal and
offsets the texture by the height (parallax), so bricks stand out from their
mortar. The rasterised view shades with the normal and shifts the texture by
a cheap one-sample parallax. The colours are
DOOM's own paletted art, point-sampled, as today. A texture with HD maps in
Ultra keeps them; every other texture in Ultra gets the derived map too. One
setting turns the feature off, and then every frame is as it was.

## 2. Where this sits

1. **Solid has no surface detail today.** `EnsureHdMaterials`
   (`r_vulkan.cpp`) returns at once unless the tier is Ultra
   (`TIER_RT3D`), so no Solid texture gets HD or derived maps.
   `InitHdDefault` gives every material `usePBR = 0`. In `pathtrace.comp`,
   `hdParallaxUV`, `hdShadingNormal` and `hdAlbedo` all fall back to the flat
   paletted path when `usePBR` is 0. `mesh.frag` reads no map but the
   paletted `materialTex[]`.
2. **The derive already exists, offline.** `scripts/pbr_derive.py` builds a
   height from BT.601 luma (`_height_field`), a normal from a 3x3 Sobel of
   that height with wrap-around and `strength=2.0` (`derive_normal`, OpenGL
   Y+), and a roughness from a name-prefix table (`_FAMILY`, longest prefix
   wins, default 0.70). DOOM-0042 runs it by hand for Ultra and keeps its
   PNGs out of the repo, because they are derivative works of id's art.
3. **A load-time derive from WAD pixels has a precedent.**
   `ComputeMaterialEmissive` (`r_vulkan.cpp`) walks every atlas tile through
   `emis::derive_material_le` (`emissive_derive.h`) at atlas time, and
   `tests/emissive_derive_test.cpp` locks it. This spec copies that shape.
4. **Both views can already reach one more image per material.**
   `materialTex[]` is a variable-count bindless array bound in `mesh.frag`
   (set 0, binding 2), `pathtrace.comp` (set 1, binding 2) and `bake.comp`.
   It is sized before the atlas exists. `CreateDescriptors` declares it at
   `RB_MaterialCount()` and stops with `I_Error` when the device cannot bind
   that many images. `InitPaletteAndDescriptorSet` sizes the pool and the
   variable count to the same number. `CreateHdSetLayout` reserves room for
   it when it clamps set 3. `UploadAtlas` only fills the slots.
5. **Material ids put walls first, then flats, then sprites.** `misc.w` is
   the flat offset (numWall) and `misc4.x` the sprite base (numWall +
   numFlat), per `docs/standards/renderer.md`'s lane table.
6. **No push-constant lane is free.** `docs/standards/renderer.md`: the next
   ray-traced push value appends a `misc7 uvec4` (240 to 256 bytes). The
   mesh push block is full at 128 bytes (`r_vulkan.cpp`'s static_assert
   message), so `mesh.frag` already takes overflow data from a uniform
   buffer, `ShadowUbo`.

**Files this touches:** `surface_derive.h` (new), `r_vulkan.cpp`,
`r_mesh.c`, `r_mesh.h` and `r_data.c` (a texture's name and mask, §4.1),
`m_misc.c`, `m_menu.c`,
`shaders/formulas/surface_detail.glsl` (new), `shaders/pathtrace.comp`,
`shaders/pt_common.glsl`, `shaders/mesh.frag`, `shaders/formulas/README.md`,
`tests/surface_derive_test.cpp` (new), `tests/shader_mirror_test.cpp`,
`Makefile`. All under `linuxdoom-1.10/`.

## 3. Scope decisions

- **Solid keeps DOOM's own colours.** The albedo stays the paletted texture,
  point-sampled. Only the surface map is new. `CLAUDE.md` § Render tiers:
  "Solid enhances DOOM's own art; Ultra substitutes for it."
- **Detail and shine are designed together; upscaling comes later.** The
  user chose this on 2026-10-26. DOOM-0103 adds the shine. DOOM-0172
  (upscaling) is not part of this, so the maps are made at each texture's
  own size.
- **Both of Solid's views get it: the full effect when ray traced, a cheap
  one when rasterised.** The user chose this on 2026-10-26. The user's
  wording of the tiers, the same day: Solid's rasterised view gets "nice
  lighting effects, shadows and some nice bells and whistles but done in a
  cheap / fake way"; its ray-traced view "must be rt but can use some
  cheaper methods to help keep performance up". So the rasterised view gets
  the normal and a one-sample parallax offset. The traced view gets stepped
  parallax.
- **The maps are made in the engine at load, never written to disk.** The
  WAD's art cannot be committed (`docs/standards/assets.md`), so maps made
  from it cannot be either. Made in memory from the loaded WAD, they also
  follow a PWAD that replaces a texture.
- **Out of scope:** sprites, ambient occlusion maps (§10), metallic, Classic.

## 4. Design

### 4.1 The derive: `surface_derive.h`

A header-only C++ namespace `surf`, in the shape of `emissive_derive.h`, so
`tests/surface_derive_test.cpp` can call it without a GPU.

```cpp
namespace surf {
// One texture's composited RGB (from the atlas tile and PLAYPAL), its mask
// (true where a texel is transparent) and its name.
// Writes w*h RGBA8 texels:
//   R, G = normal x, y encoded as 0.5 + 0.5 * n   (OpenGL Y+, z rebuilt)
//   B    = height, 0 = low, 255 = high
//   A    = roughness, 0..255 for 0..1
void derive(const uint8_t* rgb, const bool* transparent, int w, int h,
            const char* name, uint8_t* out);
float family_roughness(const char* name);   // _FAMILY's table and default
}
```

- **Height and normal** port `_height_field` and `derive_normal` exactly:
  BT.601 luma, 3x3 Sobel with wrap-around, `strength` 4.0 (§10), Y+.
- **Roughness** is `family_roughness(name)` for every texel: `_FAMILY`'s
  prefixes and values, longest prefix wins, default 0.70. One value per
  texture today. It is stored per texel so a later item can vary it without
  changing the format.
- **Flat cases.** These get a flat map: normal (0, 0, 1), height 255,
  roughness from the table. Height 255 is the surface itself, so neither
  parallax helper moves the UV there (§4.3).
  - Every flat in `kLiquidFlats`. Liquids have their own ripple and sheen
    (DOOM-0183). The list moves from `r_vulkan.cpp` into `surface_derive.h`,
    and `r_vulkan.cpp` reads it from there, so there is still one list.
  - The sky flat `F_SKY1`.
  - Any texture with a transparent texel. Parallax on a see-through middle
    texture would slide its holes.
- **The mask is patch coverage, not palette index 0.** A wall texel is
  transparent when no patch post covers it in `R_RenderTextureToAtlas`. The
  atlas cannot tell this: it stores both an uncovered texel and a real black
  texel as index 0. Many opaque walls hold real index-0 black (COMP2,
  BIGBRIK1). Keying on index 0 would give them a flat map. Flats have no
  transparent texels.
- **The atlas carries neither the mask nor the name** (`rb_atlas_t`), so
  `r_mesh.c` and `r_mesh.h` supply both. `texture_t` and `R_TexPatchOk` are
  private to `r_data.c`, so a wall's mask and name come from `R_RenderTextureToAtlas`,
  which only the atlas build calls. It is the one software-renderer file this
  change edits (INV-9).

### 4.2 Upload: one more image per wall and flat

`UploadAtlas` calls `surf::derive` for every wall and flat. It appends each
result to `materialTex[]` as an RGBA8 image, after the `RB_MaterialCount()`
paletted images. The surface map of material `id` (where `id < numWall +
numFlat`) is then `materialTex[RB_MaterialCount() + id]`. No index table is
needed. Sprites get none. The shaders learn that base, `RB_MaterialCount()`,
from the setting lanes (§4.5).

- These images use `hdSampler`, which is linear and REPEAT. The paletted
  images keep `texSampler` (nearest).
- The array's size, set in `CreateDescriptors` and
  `InitPaletteAndDescriptorSet` (§2), includes the surface maps, and so does
  `CreateHdSetLayout`'s reserve. Where the device can bind the paletted
  images but not the surface maps too, the array keeps its old size, the
  surface maps are skipped and the feature is off for the session, with one
  log line. The game never stops for this.
- The derive runs once per WAD, with the atlas. It does not rerun on a tier
  change.

### 4.3 The shader helper: `formulas/surface_detail.glsl`

One include, used by `pathtrace.comp` and `mesh.frag`:

```glsl
// x,y,z = tangent-space normal; w = height 0..1. roughness via surfRoughness.
bool  surfHas(uint id);                 // id < numWall + numFlat and the feature is on
vec4  surfSample(uint id, vec2 uv01);   // uv01 = hit UV / paletted texture size
float surfRoughness(uint id, vec2 uv01);
vec2  surfParallaxUV(uint id, vec2 uv01, vec3 viewTS);  // traced view: stepped; returns uv01
vec2  surfOffsetUV(uint id, vec2 uv01, vec3 viewTS);    // rasterised view: one sample; returns uv01
```

- `surfOffsetUV` is plain parallax offset mapping: one height read, and the
  UV moved by `kSurfHeightScale * (h - 1.0) * viewTS.xy / viewTS.z`, with
  `viewTS.z` floored so grazing angles cannot run away. Height 1.0 is the
  surface, as in `hdParallaxUV`, whose march stops at once there.

- `surfParallaxUV` reuses the method of `hdParallaxUV`: steps plus one
  bisection, primary hit only, no self-shadowing. It uses its own height
  scale, `kSurfHeightScale`, because derived heights come from luma and are
  rougher than authored ones. It is 0.12, twice `kPomHeightScale` (§10).
- The tangent frame:
  - In the traced view it is the triangle's dP/dU and dP/dV, from
    `hdTangentFrame`. `primaryHitSurface` calls it only when `usePBR` is
    set, so the surface-map branch calls it too.
  - In `mesh.frag` it is built per pixel from screen derivatives of the world
    position and UV. No vertex attribute changes.

### 4.4 Where it is used

**Traced view (`pathtrace.comp`), in `primaryHitSurface` and
`primaryHitLighting`.** For a material with `usePBR == 1`, the HD path runs
as today. Otherwise, where `surfHas(id)`:

1. The UV is offset by `surfParallaxUV`. `decodeAlbedo` takes texel units,
   so it reads the paletted colour at that result times the paletted texture
   size. Colour and relief stay aligned.
2. The shading normal is the surface normal in the tangent frame. It feeds
   the static NEE, the sprite lights, the flashlight and the muzzle flash,
   which all take the shading normal today.
3. The G-buffer normal (`gnorm`) stays the geometric normal, because the
   denoiser edge-stops on it.

The GI bake (`bake.comp`) does not use surface maps. It lights probes per
subsector, where texel detail does not show.

**Rasterised view (`mesh.frag`).** Where `surfHas(id)`:

1. The UV is offset by `surfOffsetUV`, and the paletted colour is read at
   the offset UV.
2. The normal used for the flashlight's `facing` term and the point lights'
   `ndl` term is the surface normal. Sector light and GI are unchanged.

**The weapon overlay** (`mesh.frag` built `-DSINGLE_TARGET`) is unchanged:
the `surf` code sits inside `#ifndef SINGLE_TARGET`, so that build has none.

### 4.5 The setting

- **Config key `surface_detail`**, 0 = off, 1 = on, default 1. It lives in
  `m_misc.c`'s defaults beside `rt_detile`, with a row in the same menu.
- **Traced view:** it appends `misc7 uvec4` to the push block. `misc7.x` is
  the setting, 0 also when §4.2 skipped the maps. `misc7.y` is reserved for
  DOOM-0103's specular setting. `misc7.z` is the surface-map base,
  `RB_MaterialCount()`. `.w` stays free. Both places `renderer.md` names are
  updated: the `PC` struct comment and the lane table.
- **Rasterised view:** `ShadowUbo` gains a `uvec4 flags` after `lightVP`.
  Today the buffer is written only while the flashlight is on. `flags` is
  written every frame, whatever the flashlight does.
  `flags.x` is the setting, 0 also when §4.2 skipped the maps. `flags.y` is
  reserved for DOOM-0103, `flags.z`
  is the surface-map base, and `flags.w` stays free. `surfHas` compares
  against the sprite base, which each view already carries: `misc4.x` when
  traced, `pc.numWall + pc.numFlat` in `mesh.frag`.
- **Classic** ignores the setting.

### 4.6 Failure modes

- **Luma is not height.** DOOM's art has light painted into it, so a bright
  highlight reads as raised. That is accepted: it is the same assumption
  `pbr_derive.py` makes for Ultra's derive rows. The height scale is tuned
  by eye on the test scene (§10).
- **Point-sampled colour over linear-filtered relief.** Up close, a texel's
  colour is a hard square while its normal is smooth. Accepted: the user
  wants the original pixels kept, and DOOM-0172 is where texels get finer.
- **Parallax at grazing angles.** The offset is bounded as in
  `hdParallaxUV`. The UV is not clamped, which is right for REPEAT textures.
- **Out of bindless room.** §4.2: the feature turns off with a log line.
- **Animated textures.** Each frame of an animation is its own material id,
  so it has its own map. Nothing extra is needed.

## 5. Data & resources

- **Memory.** One RGBA8 texel per wall or flat texel. Measured 2026-10-26 by
  summing TEXTURE1/TEXTURE2 sizes and 4096 texels per flat: `doom.wad` has
  4,077,632 texels (287 walls, 107 flats), about 16 MB. `doom2.wad` has
  5,572,608 (428 walls, 147 flats), about 22 MB. Re-run from the repository
  root:

  ```python
  import struct
  for p in ("wads/doom.wad", "wads/doom2.wad"):
      d = open(p, "rb").read(); n, off = struct.unpack("<4xii", d[:12])
      e = [(d[off+16*i+8:off+16*i+16].rstrip(b"\0").decode(),
            *struct.unpack("<ii", d[off+16*i:off+16*i+8])) for i in range(n)]
      walls = texels = 0
      for nm, o, s in e:
          if nm in ("TEXTURE1", "TEXTURE2"):
              for k in range(struct.unpack("<i", d[o:o+4])[0]):
                  t = o + struct.unpack("<i", d[o+4+4*k:o+8+4*k])[0]
                  w, h = struct.unpack("<hh", d[t+12:t+16]); walls += 1; texels += w*h
      names = [x[0] for x in e]
      flats = sum(1 for x in e[names.index("F_START")+1:names.index("F_END")] if x[2] == 4096)
      print(p, walls, flats, texels + 4096*flats)
  ```

- **Images.** `materialTex[]` grows by numWall + numFlat images.
- **Config.** One new key, `surface_detail` (§4.5).
- **Push constants.** The traced block grows by 16 bytes to 256, the limit
  `renderer.md` names. `ShadowUbo` grows by 16 bytes.
- No new file format, save-game field, command-line flag or dependency.

## 6. Performance budget

Measured with the `\` profiler at the same render scale, on the RX 6600,
before and after, with `surface_detail` 1:

- **Rasterised view:** at the DOOM-0170 reference (1920x1080, 100% scale,
  E1M1 start), the scene pass (`profMs[1]`) grows by at most 0.3 ms, and
  the frame rate stays at or above 60 (DOOM-0170 INV-2).
- **Traced view:** at E1M1's start, 50% scale, the megakernel grows by at
  most 1.0 ms. DOOM-0279 asks each new effect for a budget share; this is
  this feature's share.
- **Load:** the derive takes at most 150 ms for `doom2.wad`, logged by the
  engine on one line with the image count and megabytes.

## 7. Build order

Each step ends with `make DEV=1` and `make test` green.

- **B1 — the derive.** `surface_derive.h` and `tests/surface_derive_test.cpp`.
  *Verify:* INV-1 to INV-3 pass, and each fails against a mutated derive
  (Sobel sign flipped, prefix match made a substring match, mask ignored).
- **B2 — the upload and the setting.** §4.2 and §4.5, with no shader reading
  the maps yet. *Verify:* the log line reports numWall + numFlat images and
  is within §6's load budget; INV-5's captures match.
- **B3 — the traced view.** §4.3 and the `pathtrace.comp` half of §4.4.
  *Verify:* INV-4 and INV-6's recipes on the test scene; `-rtverify` passes
  (`renderer.md`).
- **B4 — the rasterised view.** The `mesh.frag` half of §4.4.
  *Verify:* INV-7's recipe; §6's raster measurement.
- **B5 — closing checks.** INV-5's captures on the finished build, §6's
  traced measurement, INV-8 and INV-9's commands, the user's look at the test
  scene (§10), the §12 edits.

**Test scene:** E2M1, `-warp 2 1 -skill 3 -warpto 424 382 352`, the corridor
of STONE2 walls and FLOOR5_2 floor leading to the REDWALL1 cross
(`dev-shots/shot-0026.png` is the before).

## 8. Invariants

- **INV-1** — `surf::derive`'s height and normal equal `pbr_derive.py`'s for
  the same texture, within one step of 8-bit rounding.
  *Test:* `tests/surface_derive_test.cpp` derives a fixed synthetic 8x8
  texture and compares with values generated once by `pbr_derive.py` and
  pasted into the test, with the command that made them in a comment.
  *Breaks when:* the Sobel sign or the Y convention flips, or wrap-around is
  dropped at the edges.

- **INV-2** — `surf::family_roughness` matches `_FAMILY`: longest prefix
  wins, 0.70 when nothing matches.
  *Test:* `tests/surface_derive_test.cpp`: STONE2 0.85, FLOOR5_2 0.80,
  METAL1 0.35, STARTAN3 0.70, and XMETAL 0.70, because a prefix matches only
  at the start of the name.
  *Breaks when:* a prefix matches anywhere in the name, or the default
  changes. No prefix in `_FAMILY` begins another, so no input can tell
  longest-wins from first-wins and the test does not try.

- **INV-3** — Liquids, the sky flat and textures with a transparent texel get
  a flat map: every texel normal (0, 0, 1) and height 255.
  *Test:* `tests/surface_derive_test.cpp`, with NUKAGE1, F_SKY1 and an 8x8
  texture with one transparent texel.
  *Breaks when:* the mask is ignored, so a see-through texture gets relief,
  or the liquid list is not consulted.

- **INV-4** — In both views the paletted colour is read at the parallax UV,
  so colour and relief stay aligned.
  *Test:* `tests/surface_derive_test.cpp` scrapes both shaders. In the
  surface-map branch of `primaryHitSurface` (`pathtrace.comp`), the UV passed
  to `decodeAlbedo` is computed from the result of `surfParallaxUV`. In `mesh.frag`, the UV
  of the `materialTex` read is the result of `surfOffsetUV`.
  *Breaks when:* the colour is read at the raw hit UV, which is what
  `hdAlbedo`'s fallback does today.

- **INV-5** — With `surface_detail` 0, every Solid and Ultra frame, in both
  views, matches the build before this change.
  *Test:* `scripts/ab_capture.sh`, old build against new, at E1M1's start and
  at the test scene, in each view. Each pair differs by no more than a
  same-build control pair.
  *Breaks when:* a shader samples or applies the surface map without checking
  the setting, or the new push field shifts an old one.

- **INV-6** — In Ultra a material with `usePBR == 1` uses its HD maps and
  never the derived map.
  *Test:* `tests/surface_derive_test.cpp` scrapes `pathtrace.comp`: every
  call to a `surf` helper sits in a branch where `usePBR` is 0.
  *Breaks when:* the derived normal is applied on top of an HD normal.

- **INV-7** — In the rasterised view the surface normal changes only the
  flashlight and point-light terms. Sector light and GI are not touched, and
  the weapon overlay is unchanged.
  *Test:* `tests/surface_derive_test.cpp` scrapes `mesh.frag`: the surface
  normal is read only by the `facing` and `ndl` expressions, and every
  `surf` call sits inside `#ifndef SINGLE_TARGET`.
  *Breaks when:* the sector or GI term uses the bumped normal, which darkens
  whole walls under flat light.

- **INV-8** — Nothing the derive makes is written to disk.
  *Test:* `grep -nE "fopen|fwrite|ofstream|M_WriteFile" linuxdoom-1.10/surface_derive.h`
  → no output.
  *Breaks when:* a debug dump of the maps is left in.

- **INV-9** — Classic is unchanged: no file the software renderer runs is
  edited, except `R_RenderTextureToAtlas` in `r_data.c`, which only the atlas
  build calls.
  *Test:* `git diff --name-only <base>.. -- linuxdoom-1.10/r_main.c linuxdoom-1.10/r_segs.c linuxdoom-1.10/r_plane.c linuxdoom-1.10/r_things.c linuxdoom-1.10/r_draw.c linuxdoom-1.10/r_bsp.c linuxdoom-1.10/r_sky.c`
  → no output. Then `git diff <base>.. -- linuxdoom-1.10/r_data.c`, read by
  hand: every hunk lies inside `R_RenderTextureToAtlas` or a declaration it
  needs.
  *Breaks when:* the change reaches the software renderer.

- **INV-10** — The push-lane and setting values agree in C and GLSL: the
  `misc7` and `ShadowUbo.flags` layouts, and `surface_detail`'s on value.
  *Test:* `tests/shader_mirror_test.cpp` gains the pairs, scraped by name.
  *Breaks when:* one side moves a field alone.

## 9. Alternatives considered (and rejected)

- **Ship derived maps as files.** They are derivative works of the WAD
  (DOOM-0042, `docs/standards/assets.md`), and would go stale under a PWAD.
- **Run `pbr_derive.py` at first launch and cache the PNGs.** It needs
  Python on the player's machine and writes derived art to disk.
- **Derive on the GPU in a compute pass.** Faster, but the CPU derive is
  well inside §6's load budget, and the CPU version is testable without a
  GPU, as `emissive_derive.h` is.
- **Use DOOM-0042's HD set (set 3) for the derived maps.** Set 3 is
  compute-only (`CreateHdSetLayout`), so the rasterised view cannot read it
  without DOOM-0182's work. Its five-maps-per-material layout would also cost
  five bindless images per texture where §4.2 needs one.
- **Linear-filter the colour too.** It blurs DOOM's pixels, which the user
  wants kept. DOOM-0172 owns any change to how colour is sampled.
- **Stepped parallax in the rasterised view.** 16 or more height reads per
  pixel land on Solid's smoothest view. The user asked for cheap methods
  there (§3), and one-sample offset mapping is the cheap form.

## 10. Open questions

- **The height scale and normal strength** are look calls. The user chose
  0.12 and 4.0 on 2026-10-26, from a side-by-side of four pairs at the test
  scene; the start values were 0.06 and 2.0.
- **Ambient occlusion.** `derive_ao` exists, but the four channels are full
  and it costs about 32 taps per texel at load. Whether crevice darkening is
  worth a second image is decided after the user has seen B5. It is not
  queued.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/surface_derive_test.cpp` |
| INV-2 | `tests/surface_derive_test.cpp` |
| INV-3 | `tests/surface_derive_test.cpp` |
| INV-4 | `tests/surface_derive_test.cpp` (scrape) |
| INV-5 | **manual** — the §8 capture recipe, run at B2 and B5 |
| INV-6 | `tests/surface_derive_test.cpp` (scrape) |
| INV-7 | `tests/surface_derive_test.cpp` (scrape) |
| INV-8 | the §8 grep, run at B5; **nothing** stops a later dump |
| INV-9 | the §8 diff, run at B5 |
| INV-10 | `tests/shader_mirror_test.cpp` |
| §6 budgets | **manual** — the profiler, at B4 and B5 |

Each scrape is run once against a mutated shader to see it fail. Rebuild with
`make test` before trusting either result: `make` does not rebuild tests.

## 12. Cross-doc impact

- `docs/specs/DOOM-0042-ultra-hd-pbr-materials.md` — amended in the same
  change. Its scope row "Ultra only … Solid/Classic keep paletted art", its
  Verification hard gate (Solid pixel-identical) and "No regression
  off-Ultra" now hold with `surface_detail` 0 only. Its line "it never runs
  the generator in-engine" stays true of its own PNG derive. This spec's
  in-engine derive is a separate one.
- `docs/standards/renderer.md` — the lane table gains `misc7` (§4.5).
- `shaders/formulas/README.md` — a line for the new include.
- `CHANGELOG.md` — an Added entry when this ships.
- `docs/specs/DOOM-0103-specular-lobe.md` — reads `surfRoughness` and the
  reserved `misc7.y` and `ShadowUbo.flags.y`.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0273-solid-surface-detail-loop-log.md`.
