<!-- ants-spec-format: 1 -->
# DOOM-0455 — Make invulnerability and the light-amp visor work in the 3D views

**Status:** accepted (2026-10-24).
**Kind:** implement.
**Source:** ROADMAP DOOM-0455 (`review-code` 2026-09-01, lane backend-seam,
DOOM-0405 F1; split out 2026-09-21; visor design settled 2026-09-21 with the
Vestige session, which owns the path tracer's shading formulas).

**Layman:** In Solid and Ultra, picking up an invulnerability sphere or the
light-amplification visor changes nothing on screen. After this, invulnerability
turns the view into a grey negative as in the original game, and the visor
lights up dark rooms, in all four 3D views.

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

When the player holds invulnerability or the light-amp visor, every 3D view
shows it: Solid and Ultra, each rasterised and ray-traced. Invulnerability
draws the world and the weapon as a grey negative, leaving the sky alone, as
Classic does. The visor adds light from the player's viewpoint, so a room with
no light shows its shapes. The end-of-powerup blink works as in Classic. With
neither powerup, every 3D frame is unchanged.

## 2. Where this sits

1. Classic honours the powerups through one renderer global.
   `P_PlayerThink`'s colormap block in `p_user.c` sets `player->fixedcolormap`
   to `INVERSECOLORMAP` (32) for invulnerability and to 1 for the visor, and
   back to 0 on alternate 8-tic blinks during the last 128 tics (about 3.7
   seconds).
   `R_SetupFrame` (`r_main.c`) turns that into `fixedcolormap`, which
   `r_segs.c`, `r_plane.c` and `r_things.c` use in place of the light tables.
2. The 3D views never see it. `Vulkan_RenderPlayerView` (`r_backend.c`) copies
   `player->extralight` into `rb_view_t` (`r_mesh.h`) but nothing about
   `fixedcolormap`, and the name appears nowhere in `r_vulkan.cpp` or
   `shaders/`. `docs/standards/renderer.md` requires the 3D back-ends to
   replicate such software-renderer side effects.
3. Classic exempts the sky and nothing else, and the 3D views must match:
   - `R_DrawPlanes` (`r_plane.c`) always draws the sky with `colormaps`,
     row 0. Its comment: "sky is not affected by INVUL inverse mapping".
   - World sprites (`R_ProjectSprite`) and the weapon (`R_DrawPSprite`, both
     in `r_things.c`) take `fixedcolormap`. Only shadow-drawn things
     (`MF_SHADOW`, the invisibility powerup) skip it.
4. The final pixel has a different owner in each view, so no single shader
   change covers all four:

   | View | Owner of the world's final colour | Owner of the weapon's |
   |---|---|---|
   | Rasterised (Solid or Ultra) | `composite.frag` | `composite.frag` (the weapon is drawn into the scene targets by `mesh.frag`) |
   | Ray-traced, bloom off | `svgf_composite.comp` | `mesh.frag` built `-DSINGLE_TARGET`, in `RecordRtOverlay`, after the blit |
   | Ray-traced, bloom on | `rt_tonemap.comp` | the same overlay draw |

   The ray-traced weapon is drawn after `vkCmdBlitImage`, so nothing that runs
   on the traced image can reach it.

**Files this touches:** `fixed_colormap.h` (new), `r_mesh.h`, `r_backend.c`,
`r_vulkan.cpp`, `shaders/formulas/fixed_colormap.glsl` (new),
`shaders/formulas/README.md`, `shaders/mesh.vert`, `shaders/mesh.frag`,
`shaders/composite.frag`, `shaders/svgf_composite.comp`,
`shaders/rt_tonemap.comp`, `tests/fixed_colormap_test.cpp` (new),
`tests/shader_mirror_test.cpp`, `Makefile`, `scripts/make_map_fixture.py`.
All under `linuxdoom-1.10/` except the script.

## 3. Scope decisions

- **The visor adds light; it is not a filter on the final colour.** Settled on
  2026-09-21 with the Vestige session and recorded on the DOOM-0455 roadmap
  item. A pixel with no light carries no shape. Any operation on the final
  colour alone can only turn black into flat grey, and in the traced view a
  gain also amplifies noise. So the visor is a lamp at the camera, computed
  from each surface's albedo and normal, with no extra rays.
- **Tint is neutral white.** Classic's row 1 is untinted. Green goggles are a
  later convention and would be a departure.
- **Classic's literal distance behaviour is not copied into the traced view.**
  Classic's visor cancels distance dimming. The traced view has no
  camera-distance dimming to cancel, so the visor controls only the falloff of
  its own term (`VISOR_FLATNESS`, §4.2).
- **All four views ship together.** A powerup that works in one view reads as a
  bug in the other three.
- **Out of scope:** fuzz-drawn things under invulnerability and the ray-traced
  debug views (§4.5); other palette effects (damage red, pickup gold, radiation
  suit green), which the 3D views already show through `tint.frag`.

Every other choice below follows from §2.

## 4. Design

### 4.1 The seam: one field on `rb_view_t`

A new header, `linuxdoom-1.10/fixed_colormap.h`, holds the mode codes and the
mapping, so a test can include it with no engine:

```c
// DOOM-0455: what the player's fixedcolormap asks the 3D views to do.
enum { RB_FIXED_NONE = 0, RB_FIXED_VISOR = 1, RB_FIXED_INVERT = 2 };
enum { RB_INVERSECOLORMAP = 32 };   // p_user.c's INVERSECOLORMAP (INV-2)

// player->fixedcolormap -> mode. Classic sets only 0, 1 (visor) and
// INVERSECOLORMAP; any other non-zero row is shaded as the visor.
static inline int RB_FixedMode(int fixedcolormap)
{
    if (fixedcolormap == 0)                  return RB_FIXED_NONE;
    if (fixedcolormap == RB_INVERSECOLORMAP) return RB_FIXED_INVERT;
    return RB_FIXED_VISOR;
}
```

`rb_view_t` gains `int fixedMode;`. `Vulkan_RenderPlayerView` sets it from
`RB_FixedMode(p->fixedcolormap)` beside `extralight`. It reads the player and
writes nothing back, so game state, demos and netgames are untouched. The blink
needs no code: `player->fixedcolormap` already toggles per tic.

`p_user.c`'s `INVERSECOLORMAP` stays where it is. INV-2 binds
`RB_INVERSECOLORMAP` to it.

### 4.2 The formulas: one include

A new `shaders/formulas/fixed_colormap.glsl` holds both formulas, the mode
codes and the look constants. Every site includes it; no site writes either
formula itself.

```glsl
const uint RB_FIXED_NONE = 0u, RB_FIXED_VISOR = 1u, RB_FIXED_INVERT = 2u;

// Invulnerability: the greyscale negative of a DISPLAY-ENCODED colour.
// Rec.601 weights, measured against COLORMAP row 32 (§4.6).
vec3 fixedInvert(vec3 encoded)
{
    return vec3(1.0 - dot(clamp(encoded, 0.0, 1.0), vec3(0.299, 0.587, 0.114)));
}

// Visor: how strongly the camera-attached lamp lights a surface, 0..1.
// Wrapped Lambert on |N.V| (a surface the camera sees faces it, so the
// stored normal's sign carries nothing), times a falloff that
// VISOR_FLATNESS blends away.
const vec3  VISOR_TINT     = vec3(1.0);
const float VISOR_WRAP     = 0.5;     // 0 = plain Lambert, 1 = no normal term
const float VISOR_FLATNESS = 0.15;    // 1 = no distance falloff, 0 = full falloff
const float VISOR_REACH    = 1000.0;  // world units; the falloff's zero point
const float VISOR_GAIN_RT  = 1.0;     // traced view only: radiance scale (§10)
float visorShade(vec3 N, vec3 toEye, float dist)
{
    float facing = (abs(dot(N, toEye)) + VISOR_WRAP) / (1.0 + VISOR_WRAP);
    float fall   = mix(clamp(1.0 - dist / VISOR_REACH, 0.0, 1.0), 1.0, VISOR_FLATNESS);
    return facing * fall;
}

// Visor: how much of the lamp a surface takes given the light it already
// has, 1 in the dark down to 0 at `full` (§4.5).
const float VISOR_LIT_FULL_RT = 1.0;  // traced view: demodulated illum luminance
float visorLitFade(float lit, float full)
{
    return 1.0 - smoothstep(0.0, full, lit);
}
```

The values were settled from the first captures (§10), in this file only.

`glslc` emits no dependency for an `#include`, so the `Makefile` names this
file as a prerequisite of every shader that includes it, as it already does
for `scene_recombine.glsl` (`docs/standards/renderer.md`, Shaders).

### 4.3 Where each formula runs

**Visor: two sites, both before any tone operator.**

- `mesh.frag`, both builds. After the flashlight and point lights, into
  `direct` and never `ambient`. The composite darkens AMBIENT by the full AO
  and DIRECT by half of it (`AO_DIRECT_WEIGHT`), so a camera-attached lamp is
  darkened less by the occlusion of the surface it lights. The visor fills
  only the headroom left by sector light and the flashlight, so a surface lit
  by those tops out at its albedo, full bright as in Classic's row 1. Point
  lights are outside that headroom, so a point-lit surface can exceed its
  albedo, as it already can without the visor:

  ```glsl
  if (pc.fixedMode == RB_FIXED_VISOR) {
      if ((vFlags & FLAG_PSPRITE) != 0)
          direct = albedo;                       // the weapon: full bright
      else {
          vec3  toEye = vec3(pc.eyeX, pc.eyeY, pc.eyeZ) - vWorldPos;
          float v     = visorShade(normalize(vNormal), normalize(toEye), vDist)
                      * visorLitFade(sect + flash, BASE_SECTOR_DIM);
          float room  = max(0.0, 1.0 - sect - flash);
          direct += VISOR_TINT * albedo * min(v, room);
      }
  }
  ```

  `flash` is the flashlight's added amount, hoisted out of its branch and 0
  where that branch did not run. The sky returns before this point, so it is
  never lit.
- `svgf_composite.comp`, both builds. After `L = albedo * illum + shownEmission(emis, albedo, ga.a)`
  and before the fog fold, so mist still reads in front of a lit wall:
  `L += VISOR_TINT * albedo * visorShade(N, toEye, dist) * visorLitFade(lit,
  VISOR_LIT_FULL_RT) * VISOR_GAIN_RT`, where `lit` is the Rec.709 luminance
  of the denoised illumination `illum`.
  `N` comes from `gnorm[cur]` at binding 1. The SVGF set layout
  (`CreateSvgfDescriptorLayout`) already carries that binding, which
  `svgf_temporal.comp` reads, so the composite only declares it. `toEye` and
  `dist` run from the hit point in `gpos[cur]` to this frame's camera
  (`g.lastView`), which the host passes as bit-cast floats in `misc2[2]`,
  `misc2[3]` and `misc3[3]` (§4.4). `pc.prevPos` is not used: it is the
  previous denoised frame's camera, and 0 when the SVGF targets are created.
  The term is added before exposure, so the Brightness setting
  scales it like any other light. One insertion covers bloom on and off,
  because both tone-map paths read this `L`.

**Invert: four sites, each in the display-encoded domain, after bloom and
tone-map, never on the sky.**

| Site | Where in the shader | Sky test it skips |
|---|---|---|
| `composite.frag` | On the value `pbrNeutralToneMapping` returns, after the bloom add | `viewZ >= 50000.0`, the test `ssao.frag` and `bloom_extract_raster.comp` use. `composite.frag` switches to `sceneRecombineParts` to read it. |
| `svgf_composite.comp`, un-split build | On the `toneExposeEncode` result of the surface store | Its existing sky branch returns first |
| `rt_tonemap.comp` | On the `toneExposeEncode` result | Its existing `h.a < 0.5` sky branch returns first |
| `mesh.frag` `-DSINGLE_TARGET` build (the traced weapon) | On `outColor`, which is already display-referred | Not needed: the overlay draws only the weapon |

The split `svgf_composite.comp` stores linear radiance, so it never inverts.
`rt_tonemap.comp` does it one pass later. Exactly one site touches any pixel.

Encoded, because Classic inverts a gamma-encoded palette entry: inverting
linear light crushes midtones toward white. After bloom, because an inverted
halo becomes a dark smudge. Exposure cannot reach the inverted value, which
matches Classic, whose inversion is absolute.

### 4.4 The push constants

| Block | Today | Change |
|---|---|---|
| `mesh.vert` / `mesh.frag` `Push` | 124 bytes: 31 floats | `uint fixedMode` at offset 124, making 128 bytes, the guaranteed `maxPushConstantsSize` floor. Host: `pcr.size`, both `float pcData[31]` arrays (the raster draw and `RecordRtOverlay`), the `31 * sizeof(float)` size both of their `vkCmdPushConstants` calls pass, and `lastWeaponPc` grow to 32 entries. The wipe's weapon redraw copies `lastWeaponPc` whole, so it follows. |
| `composite.frag` `Push` | 16 bytes | `uint fixedMode` appended: 20 bytes. Host: `cpc.size`, the `float coPush[4]` write and `lastCoPush` grow to 5 entries. The wipe's composite redraw pushes `lastCoPush` whole, so it follows. |
| `rt_tonemap.comp` `Push` | 16 bytes, `RtTonemapPC` | `uint fixedMode` appended: 20 bytes. Host: its `static_assert` and the `16` its `CreateComputeLayout` call passes move to 20. |
| `SvgfPC` | 120 bytes | No growth: `misc3[2]` carries the mode, and `misc2[2]`, `misc2[3]` and `misc3[3]` the camera position, set before the composite's push. The temporal and a-trous passes read only `misc2[0]` and `misc2[1]` of these, and no SVGF pass reads `misc3` but the composite. |

The mesh block is then full. The next field needs another channel: a uniform
buffer, or a lane freed elsewhere. Recorded here so it is not found by a
validation error.

### 4.5 Failure modes

- **The blink and the temporal upscaler.** In the last 128 tics the mode
  flips every 8 tics. TAAU's history then holds frames of the other mode. Its
  3x3 neighbourhood clamp should limit each blink's ghost to a frame or two.
- **The visor in a bright room.** An added term can push PBR-Neutral into its
  roll-off and generate bloom. The bright-room capture showed no bloom, but the
  user wanted a lit room to keep its own shading. So `visorLitFade` scales the
  lamp by how lit the surface already is, in all four views: the raster views
  by `sect + flash` against `BASE_SECTOR_DIM`, the traced view by the
  illumination's luminance against `VISOR_LIT_FULL_RT`.
- **Partial invisibility.** Classic draws a shadow-drawn thing or weapon with
  its fuzz effect, ignoring `fixedcolormap`. The 3D views draw such things
  their own way, and §4.3 inverts whatever is on screen. A fuzzy weapon under
  invulnerability can differ from Classic. Accepted, not fixed here.
- **The ray-traced debug views (`rb_rtdebug` 1 to 4)** get neither powerup.
  They are diagnostic views, not a play mode. `RecordRtOverlay` still draws
  the weapon there, so it pushes `RB_FIXED_NONE` unless `rb_rtdebug` is 6.

### 4.6 Measurement: the invert weights

Measured 2026-10-24 on `wads/doom.wad` and `wads/doom2.wad`. COLORMAP row 32
maps every palette colour to a grey. Against that grey, `255 * (1 - luma)` is
off by:

| Weights | Mean error (0–255) | Worst error |
|---|---|---|
| Rec.601 (0.299, 0.587, 0.114) | 3.0 | 11.0 |
| Rec.709 (0.2126, 0.7152, 0.0722) | 7.0 | 43.4 |
| Plain mean | 10.3 | 58.0 |

Both IWADs give the same figures. Rec.601 is chosen; what remains is the
table's own rounding to the palette's grey ramp. Re-run from the repository
root:

```python
import sys; sys.path.insert(0, "scripts")
from wad import read_directory
for path in ("wads/doom.wad", "wads/doom2.wad"):
    data, d = read_directory(path)
    get = lambda n: next(data[o:o+s] for nm, o, s in d if nm.upper() == n)
    pal, row = get("PLAYPAL"), get("COLORMAP")[32*256:33*256]
    rgb = [tuple(pal[3*i:3*i+3]) for i in range(256)]
    for name, w in (("rec601", (.299, .587, .114)), ("rec709", (.2126, .7152, .0722)), ("mean", (1/3,)*3)):
        e = [abs(255 - sum(a*b for a, b in zip(w, rgb[i])) - sum(rgb[row[i]])/3) for i in range(256)]
        print(path, name, "mean %.1f max %.1f" % (sum(e)/256, max(e)))
```

## 5. Data & resources

Four bytes more in three push blocks (§4.4). No new image, buffer or
descriptor set. No file format, config key, save-game field or command-line
flag changes. No new dependency.

## 6. Performance budget

With neither powerup, each site costs one branch on a push constant that is
the same for every pixel. With the visor, `svgf_composite.comp` reads one more
storage texel per pixel (the normal), and both visor sites add a few
arithmetic operations. Invert adds a dot product.

No frame-time figure is set. The check is the profiler's buckets for the
changed passes: in the traced view the composite (`profMs[6]`) and
`rt_tonemap` (`profMs[9]`), in the rasterised view the scene MRT
(`profMs[1]`) and the composite (`profMs[4]`). Each is measured with no
powerup, before and after, at the same render scale: unchanged within
run-to-run noise.

## 7. Build order

Each step ends with `make` and `make test` green.

- **B1 — the seam.** `fixed_colormap.h`, the `rb_view_t` field and
  `r_backend.c`'s assignment, with `tests/fixed_colormap_test.cpp`'s INV-1
  cases. *Verify:* INV-1 passes, and fails against a stand-in that maps 32 to
  the visor.
- **B2 — the include and the push blocks.** `fixed_colormap.glsl`, the four
  push-block changes and their host writes, with no site using them yet.
  *Verify:* INV-9 builds; INV-2's mirror pairs pass; INV-3's capture pairs
  match.
- **B3 — invert.** The four invert sites. *Verify:* INV-4's recipe in all four
  views; INV-5's scrape passes and fails against a mutated `composite.frag`.
- **B4 — the visor.** The two visor sites and the dark fixture.
  *Verify:* INV-6's recipe in all four views; INV-7's scrape passes and fails
  against a mutated `mesh.frag`; the bright-room capture (§4.5).
- **B5 — closing checks.** INV-3's captures re-run on the finished build; the
  INV-8 grep; INV-10's diff; §6's measurement; the §12 edits.

## 8. Invariants

- **INV-1** — `RB_FixedMode` maps 0 to `RB_FIXED_NONE`, 32 to
  `RB_FIXED_INVERT`, and every other value in `1..NUMCOLORMAPS` to
  `RB_FIXED_VISOR`.
  *Test:* `tests/fixed_colormap_test.cpp` checks all three classes.
  *Breaks when:* 32 is mapped to the visor, or row 1 to none.

- **INV-2** — The mode codes agree in C and GLSL, and `RB_INVERSECOLORMAP`
  equals `p_user.c`'s `INVERSECOLORMAP`.
  *Test:* `tests/shader_mirror_test.cpp` gains the mode-code pairs, scraped by
  name. `INVERSECOLORMAP` is a `#define` with no `=`, which the test's
  `assigned()` cannot read, so its value is parsed from the token after the
  name.
  *Breaks when:* one side renumbers, or `INVERSECOLORMAP` changes alone.

- **INV-3** — With `RB_FIXED_NONE`, every 3D frame matches the build before
  this change, in all four views, with bloom on and off.
  *Test:* `scripts/ab_capture.sh`, old build against new, at E1M1's start and
  at one dark spot, in each view, bloom on and off. Each pair differs by no
  more than a same-build control pair.
  *Breaks when:* a site applies its formula unconditionally, for example
  `mix(c, fixedInvert(c), 0.0)`, which still turns a non-finite `c` into NaN.

- **INV-4** — Invulnerability turns the world and the weapon into
  `fixedInvert` of their display-encoded colour, in all four views. The sky is
  never inverted.
  *Test:* manual recipe. In each view, at E1M1's start facing the outside
  window, type `idbeholdv` and press F12. The world and weapon are a grey
  negative. The sky matches a shot taken without the powerup.
  *Breaks when:* a site inverts before its sky test, or the traced weapon is
  left out because it is drawn after the blit.

- **INV-5** — `fixedInvert` is applied after the bloom add and after the tone
  operator.
  *Test:* `tests/fixed_colormap_test.cpp` scrapes the three world sites. Each
  site assigns the tone operator's result to a variable, then applies
  `fixedInvert(` to that variable in a later statement. In `main`, that
  statement follows the last `pbrNeutralToneMapping(` or `toneExposeEncode(`
  call, and in `composite.frag` and `rt_tonemap.comp` it also follows the
  bloom add. `svgf_composite.comp`'s un-split build has no bloom add.
  *Breaks when:* the inversion moves ahead of the bloom add, so halos invert
  into dark smudges.

- **INV-6** — The visor adds light: a surface with no light at all shows its
  albedo and shape under the visor, in all four views.
  *Test:* manual recipe. A fixture copy of `doom2.wad`'s MAP01, the only map
  `scripts/make_map_fixture.py` reads, with every sector's light set to 0 (a
  new mutation in that script), flashlight off,
  `idbeholdl`. Walls, floor and the weapon are visible in every view, and wall
  corners read as separate faces.
  *Breaks when:* the visor is a gain or a curve on the final colour, which
  leaves the black room black.

- **INV-7** — In the rasterised views the visor goes into DIRECT and is
  bounded by the headroom sector light and the flashlight leave.
  *Test:* `tests/fixed_colormap_test.cpp` scrapes `mesh.frag`. Inside the visor
  block the term is added to `direct`, `ambient` is never assigned, and the
  term is bounded by `1.0 - sect - flash`.
  *Breaks when:* the term is added to `ambient`, so SSAO darkens a lamp at the
  camera at full strength, or the headroom cap is dropped, so lit walls bloom under the visor.

- **INV-8** — Each formula has one copy. `fixedInvert` and `visorShade` are
  defined only in `shaders/formulas/fixed_colormap.glsl`.
  *Test:* `grep -rlE "^(vec3 fixedInvert|float visorShade)\(" linuxdoom-1.10/shaders`
  → `linuxdoom-1.10/shaders/formulas/fixed_colormap.glsl`, alone.
  *Breaks when:* a site pastes its own copy of either formula.

- **INV-9** — On the host, each push block this spec grows has one size: its
  array or struct, its pipeline-layout range and its push calls agree. The
  mesh block stays within 128 bytes.
  *Test:* `static_assert`s in `r_vulkan.cpp` on the mesh push array (128
  bytes), `lastWeaponPc` (128), `coPush` (20), `lastCoPush` (20) and
  `RtTonemapPC` (20), with the layout
  ranges and push calls written as `sizeof` of those; `make` → exit 0. Nothing
  checks the GLSL side against the host.
  *Breaks when:* a host size changes in one place only, or the mesh block
  passes 128 bytes.

- **INV-10** — Classic is unchanged: no file the software renderer runs is
  edited.
  *Test:* `git diff --name-only <base>.. -- linuxdoom-1.10/r_main.c linuxdoom-1.10/r_segs.c linuxdoom-1.10/r_plane.c linuxdoom-1.10/r_things.c linuxdoom-1.10/r_draw.c linuxdoom-1.10/r_bsp.c`
  → no output.
  *Breaks when:* the change reaches the software renderer.

## 9. Alternatives considered (and rejected)

- **One post-process for the visor** (gain, shadow lift, black floor). Rejected
  in §3: no shape comes out of a black pixel, and the traced view's noise is
  amplified.
- **The visor traced as a real light in `pathtrace.comp`.** Correct shadows,
  but extra rays per pixel and more variance for the denoiser, to light a
  scene the player is meant to see evenly.
- **Invert in linear light.** Crushes midtones toward white and loses
  Classic's poster look (§4.3).
- **Invert at TAAU's output, one site for both traced paths.** TAAU is a menu
  setting (`rb_upscaler`), not always on, and it has no sky flag.
- **A lookup of COLORMAP row 32.** The 3D views shade in colour, not palette
  indices, so there is no index to look up after lighting.
- **Visor constants as push constants or config keys.** The mesh block has
  room for one field (§4.4). They are look calls tuned once, so constants in
  one include are enough.

## 10. Open questions

None. **The visor constants** were settled on 2026-10-24 from the zero-light
fixture and E1M1's courtyard in Ultra's traced view. Shapes read in all four
views, so the approach holds. The user chose a lamp that fades with distance
(`VISOR_FLATNESS` 0.15, `VISOR_REACH` 1000) and is weaker in bright rooms
(`visorLitFade`, §4.5). `VISOR_WRAP` and `VISOR_GAIN_RT` kept their starting
values.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/fixed_colormap_test.cpp` |
| INV-2 | `tests/shader_mirror_test.cpp` |
| INV-3 | **manual** — the §8 capture recipe, run at B2 and B5 |
| INV-4 | **manual** — the §8 F12 recipe, run at B3 |
| INV-5 | `tests/fixed_colormap_test.cpp` |
| INV-6 | **manual** — the §8 dark-fixture recipe, run at B4 |
| INV-7 | `tests/fixed_colormap_test.cpp` |
| INV-8 | the §8 grep, run at B5; **nothing** stops a later copy |
| INV-9 | `static_assert`s in `r_vulkan.cpp`, at every build; **nothing** checks the GLSL side |
| INV-10 | the §8 diff, run at B5 |

Each new scrape (INV-5, INV-7) is run once against a mutated shader to see it
fail. Rebuild with `make test` before trusting either result: `make` does not
rebuild tests.

## 12. Cross-doc impact

- `CHANGELOG.md` — a Fixed entry when this ships.
- `docs/specs/DOOM-0331-bloom.md` and `DOOM-0345-bloom-ray-traced.md` — no
  clause changes. Their "bloom 0 is byte-identical" invariants hold because
  §4.3's sites are skipped under `RB_FIXED_NONE` (INV-3). DOOM-0345 INV-6
  counts `toneExposeEncode` calls in `svgf_composite.comp`, and this spec adds
  none.
- `shaders/formulas/README.md` — a line for the new include.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0455-fixed-colormap-3d-loop-log.md`.
