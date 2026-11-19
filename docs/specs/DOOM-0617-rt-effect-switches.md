<!-- ants-spec-format: 1 -->
# DOOM-0617 — Ray tracing per effect: a fast base, a switch per traced effect, and a hardware test that picks them

**Status:** accepted (2026-11-15).
**Kind:** feature.
**Source:** ROADMAP DOOM-0617 (user requests 2026-11-15).
**Serves:** DOOM-0279 (a steady 30 FPS in the traced views).
**Pairs with:** DOOM-0618 (a fake for every traced effect), DOOM-0170 (the rasterised lighting
stack this builds on), DOOM-0104 (traced reflections), DOOM-0189 (live bounce light), DOOM-0188
(lighting at reduced resolution).

**Layman:** Today the game either draws with no ray tracing or ray traces everything, and
everything is too much for the RX 6600 at 30 FPS. This lets the player turn on ray tracing one
effect at a time — shadows first, then corner shading — on top of the fast view, with a cheap fake
standing in for each effect left off. A built-in test measures the graphics card once and picks
the settings it can hold. "Full ray tracing" stays as today's look.

## Contents

1. Goal · 2. Where this sits · 3. Scope decisions · 4. Design · 5. Data & resources ·
6. Performance budget · 7. Build order · 8. Invariants · 9. Alternatives · 10. Open questions ·
11. What checks this · 12. Cross-doc impact · 13. Loop log

## 1. Goal

On a card that cannot trace the whole frame at 30 FPS, the player still gets ray tracing where it
shows most. The rasterised view is the base. Each traced effect is switched on by itself and,
when on, replaces that effect's fake in the rasterised view. A preset row and an automatic test
choose the switches, and the player can override any of them. The top preset is today's fully
traced view, unchanged.

## 2. Where this sits

`RB_Vulkan_Present` in `r_vulkan.cpp` draws one of two frames. `rtActive` (true when `rb_rtdebug`
is non-zero, `g.rtEnabled`, and a TLAS exists) selects `RecordRtTrace`, the traced frame: the
`pathtrace.comp` megakernel, the SVGF denoiser (`svgf_temporal.comp`, `svgf_atrous.comp`,
`svgf_composite.comp`), bloom and TAAU. Otherwise the rasterised frame runs: the flashlight's
shadow pass, the scene pass (`mesh.frag`), SSAO (`ssao.frag`), bloom and `composite.frag`.

What the traced frame traces, from `pathtrace.comp` mode 6 and `pt_common.glsl`:

- one primary ray per pixel;
- shadow rays to the emitter list for direct light (`shadeSurface`, `occluded`), which with the
  denoiser is most of the frame (`docs/research/DOOM-0279-rt-frame-time-2026-11-15.md` § 1);
- one shadow ray each for the muzzle flash (`muzzleFlashDelta`) and flashlight
  (`flashlightDelta`).

It does not trace bounce light, which both frames read from probes `bake.comp` traces once per
level (`RunGiBake`, `formulas/gi_probe.glsl`). Its fog is marched, not traced (`marchFog`). It has
no reflection rays and no traced ambient occlusion.

What the rasterised frame fakes (DOOM-0170): `mesh.frag` sums the nearest point lights per
subsector from `BuildRasterPointLights`, with no cast shadows; SSAO darkens the ambient term, and
the direct term by half (`AO_DIRECT_WEIGHT` in `formulas/scene_recombine.glsl`, the recombination
the composite and the bloom bright pass share); blob decals sit under things; the flashlight alone has a shadow map. `mesh.frag` writes
`outAmbient` and `outDirect`, whose alpha is linear depth. It writes no normal or world position.
No raster shader uses ray queries, and no TLAS is bound to the raster pipeline layout.

The TLAS is built once per level whenever `g.rtEnabled` (`BuildAccelerationStructures`), but its
per-frame upkeep — `RecordRefitAS` for moving sectors and `BuildSpriteTlas` — runs only inside
`RecordRtTrace`. So in rasterised frames it goes stale: a door that opened is still shut in it.

Which frame runs is decided by `rb_rtdebug`. `RB_ApplyTierRt` in `r_backend.c` forces it to 0 for
Solid and 6 for Ultra at boot and on each tier change; the `~` key (`I_ToggleRtView`) and the
menu's Ray Tracing row (`M_ChangeRayTracing`) flip it between 0 and 6.

## 3. Scope decisions

All by the user, 2026-11-15, unless marked.

- **The rasterised view is the base; each traced effect has its own switch; an effect left off
  is faked.** The user's words: "turn it on for global illumination, ambient occlusion, shadows,
  reflections ... For the rest, we fall back to the rasterised view."
- **"Full ray tracing" keeps today's look.** It is the top preset. Earlier the same day the user
  required that the top setting keep today's look and that before-and-after shots with FPS be shown
  before shipping. Both still bind.
- **An automatic test picks the settings for the hardware.**
- **A preset row stays** (Low / Medium / High / Ultra, and Custom once a row is changed by hand),
  from the first shape agreed that day.
- **Which effects get a switch now** (user, 2026-11-15, as proposed from § 2):
  - *Shadows* — traced now. It is where nearly all the traced frame's cost goes.
  - *Corner shading* (ambient occlusion) — traced now. It is cheap, short rays.
  - *Reflections* — no switch until DOOM-0104 builds traced reflections; a switch with nothing
    behind it would mislead.
  - *Bounce light* — no switch. Both views already read bounce light that rays baked at level
    load, for almost nothing per frame. A live version is DOOM-0189's, and gains its switch there.
- **The settings are the same in Solid and Ultra.** The tier picks the art; the settings pick how
  it is traced. `CLAUDE.md` § Render tiers already calls these independent choices, so
  `RB_ApplyTierRt` stops forcing the view.
- **Fog, bloom and the other Effects rows are not in the preset.** They are matters of taste with
  their own rows. Render scale is in it, because it is the main speed lever
  (`docs/standards/performance.md` § The main lever).

## 4. Design

### 4.1 Three ways to draw

A new setting `rt_mode` chooses:

| `rt_mode` | Name in the menu | Frame |
|---|---|---|
| 0 | Off | the rasterised frame, as today |
| 1 | Effects | the rasterised frame, with each switched-on effect traced (§ 4.3) |
| 2 | Full | `RecordRtTrace`, as today |

`rt_mode` sets `rb_rtdebug`: 0 for Off and Effects, 6 for Full. The developer paths that already
own `rb_rtdebug` keep it: Debug Views (`rb_rtdebug_menu`) and `-rtview N` still win, as in
`RB_ApplyTierRt` today. `~` flips between Off and the player's last mode other than Off (Effects
when they have none). On a GPU without ray tracing (`g.rtEnabled` false) `rt_mode` is held at 0.

### 4.2 The menu

The Video menu's Ray Tracing row (`vid_raytracing`) becomes the `rt_mode` row. New rows sit above
the Effects header:

- **Preset** — Low / Medium / High / Ultra, shown as Custom when the rows below differ from
  every preset. Choosing one sets the rows in § 4.4's table.
- **Ray Tracing** — Off / Effects / Full.
- **Traced Shadows** — Off / On. Shown only when the Ray Tracing row says Effects.
- **Traced Corner Shading** — Off / On. Shown only when the Ray Tracing row says Effects.
- **Trace Quality** — Low / Medium / High: the resolution the Effects passes trace at (§ 4.3).
  Shown only when the Ray Tracing row says Effects.
- **Test My Hardware** — runs § 4.5's test.

On a GPU without ray tracing only Preset (Low and Medium) and Test My Hardware show.

### 4.3 The Effects frame

The rasterised frame gains what a traced pass needs, then one compute pass per effect.

- **What the scene pass adds.** `mesh.frag` writes two more targets: the geometric normal
  (RGBA16F) and the triangle's subsector (R32UI), so a later pass can find that subsector's point
  lights. The subsector is `0xFFFFFFFF` where `mesh.frag` adds no point lights: sprites, the weapon
  and sky. World position is rebuilt from `outDirect.a`'s linear depth with the same camera the
  scene pass used. The extra targets are written only when `rt_mode` is 1.
- **TLAS upkeep.** When `rt_mode` is 1, the frame refits the world BLAS (`RecordRefitAS`, when
  `g.blasDirty`) and then rebuilds the TLAS, as the traced frame does, so doors and lifts move with
  the world. A refit alone leaves the TLAS on the old extents: the traced frame's rebuild is in
  `BuildSpriteTlas`. Shadow rays cull to the world mask (`0x01`, as `occluded` does), so the
  rebuild leaves out the sprite instance. The raster frame does not read the sprite buffer: it
  holds the last traced frame's sprites.
- **Traced Shadows.** A compute pass computes the point lights' direct light with a shadow ray
  per light, using the same per-subsector list and falloff as `mesh.frag`. It outputs the shadowed
  point-light sum after `mesh.frag`'s gain and roll-off, with no surface colour in it. When the
  switch is on, `mesh.frag` leaves its point-light diffuse term out of `outDirect` and writes the
  surface factor it would have multiplied by (`albedo * diffW`) to a surface target instead;
  `scene_recombine.glsl` multiplies the two and adds the product to the direct term, before the AO
  factor, so the composite and bloom both see it. The point-light highlight (`pspec`) stays in
  `outDirect`, unshadowed (user, 2026-11-15: not at first). It traces at the Trace Quality resolution — quarter, half or full of the
  render resolution — accumulates over frames, and is upscaled with a depth-and-normal-aware
  (bilateral) filter, as idTech 8 does
  (`docs/research/DOOM-0279-rt-frame-time-2026-11-15.md` § 2). Lights past a fixed count per pixel,
  nearest first, keep the unshadowed sum, so a crowded room costs a bounded number of rays.
- **Traced Corner Shading.** A compute pass casts short rays over the hemisphere at each pixel and
  writes the open fraction, at the same resolution and with the same accumulation and upscale.
  When the switch is on its image replaces SSAO's as the AO input to `scene_recombine.glsl`, so it
  darkens the ambient and direct terms with SSAO's weights; SSAO does not run.
- **Shared noise handling.** Both passes take a fresh random sample each frame and blend it into
  a history kept per pixel, re-projected with the camera and rejected where depth or normal
  disagree, as `svgf_temporal.comp` does for the traced frame.

### 4.4 Presets

| Preset | `rt_mode` | Traced Shadows | Traced Corner Shading | Trace Quality | `render_scale` |
|---|---|---|---|---|---|
| Low | Off | — | — | — | 50 |
| Medium | Off | — | — | — | 75 |
| High | Effects | On | On | Medium (half) | 50 |
| Ultra | Full | — | — | — | 50 |

Low and Medium are the rasterised view with every fake. Ultra is today's traced view at today's
default render scale. The settings rows keep their own values when a preset does not set them
(— in the table).

`render_scale` is the Render Scale row's fixed size. While that row says Dynamic (DOOM-0596) the
renderer does not read it, so Low and Medium draw alike until a fixed size is picked.

### 4.5 The hardware test

- **When it runs.** The first time a 3D tier starts with no GPU identity recorded, and again when
  the identity changes. The identity is the Vulkan vendor ID, device ID and driver version. It
  also runs when the player picks Test My Hardware. It measures with Ultra's art wherever the card
  supports Ultra, because the settings are shared and Ultra's art costs more (user, 2026-11-15);
  otherwise with Solid's. Classic, the default tier, does not start Vulkan, so it never runs
  there. A line of text says what it is
  doing. Any key cancels it.
- **What it measures.** It loads the IWAD's first map at the player start, with monsters held
  still (as `-freeze` does), and turns the camera a full circle over each test. For each preset,
  most demanding first, it renders a settling span and then a measured span. It keeps each
  frame's GPU time from the timestamps the profiler and `-frameledger` already read
  (`g.gpuTimerPool`, `RL_Active`).
- **How it decides.** It picks the most demanding preset whose slow frames still meet the target:
  the 99th-percentile frame time at or under the target frame time. When none meets it, Low. The decision is a pure
  function in a new header, so a test can hold it without a GPU (§ 8).
- **The target.** Each preset is held to its view's floor in `docs/standards/performance.md`:
  30 FPS for a preset that traces, 60 FPS for one that does not (user, 2026-11-15, "for a
  start"; § 10).
- **What it writes.** The chosen preset's rows, and the GPU identity it measured. A cancelled or
  failed test writes nothing and leaves the settings as they were. Low is the fallback when no
  test has completed.
- **The player wins.** A row changed by hand keeps its value, and the Preset row shows Custom when
  the rows match no preset (§ 4.2). A hand change also records the GPU identity, so the test does
  not run again on its own until that identity changes.

## 5. Data & resources

New config keys, in `m_misc.c`'s `defaults[]`, each read through `config_bounds.h` and clamped:

| Key | Values | Default |
|---|---|---|
| `gfx_preset` | 0 Low, 1 Medium, 2 High, 3 Ultra, 4 Custom | 0 |
| `rt_mode` | 0 Off, 1 Effects, 2 Full | 0 |
| `rt_shadows` | 0, 1 | 1 |
| `rt_ao` | 0, 1 | 1 |
| `rt_quality` | 0 quarter, 1 half, 2 full | 1 |
| `gfx_gpu_id` | text: vendor, device and driver, hex | empty |

`rt_view` stays, for the developer paths; the menu no longer writes it. A config from before this
change has no `rt_mode`, so its defaults apply and the test runs the next time a 3D tier starts,
because `gfx_gpu_id` is empty.

New GPU resources, created only when `g.rtEnabled`: the normal (RGBA16F), subsector (R32UI) and
surface-factor (RGBA16F) targets at render resolution; for each Effects pass, a trace-resolution image, a full-resolution history
pair and the upscaled result. The two compute passes have their own pipeline layouts, which
bind the TLAS. Two new compute shaders, one per effect, with a shared include for the history and
upscale.

## 6. Performance budget

Measured per `docs/standards/performance.md`, at the user's scale (`-scale 5`, `render_scale` 50,
as in the research note), on E1M1's start under `-inspect -freeze`, interleaved A/B:

- **Off:** the rasterised frame's GPU time does not grow: same build, `rt_mode` 0, against the
  commit before B1. The new targets are not written in Off.
- **Effects, Traced Shadows on, Trace Quality Medium:** a target of at most 6 ms of GPU time over
  Off.
- **Effects, Traced Corner Shading on, Trace Quality Medium:** a target of at most 3 ms over Off.
- **The High preset holds 30 FPS steady** (§ 4.5's rule) on the RX 6600. If it does not, Trace
  Quality Medium drops to quarter resolution before anything else gives.
- **Full:** unchanged, as INV-1 holds.

The 6 ms and 3 ms are design targets to bisect against, not measured figures.

## 7. Build order

- **B1** Settings: the keys (§ 5), the menu rows (§ 4.2), `rt_mode` driving `rb_rtdebug` in place of
  `RB_ApplyTierRt`'s forcing, `~`. Effects draws as Off until B3. Check: `make test`; the rows
  appear and save; Off and Full captures match the commit before, its arm pinned with `-rtview 0`
  or `-rtview 6`.
- **B2** The Effects frame's plumbing: the normal and subsector targets, the TLAS binding, the refit
  and TLAS rebuild in Effects frames. Check: an Effects capture with both switches off matches Off; the Off frame time
  is unchanged.
- **B3** Traced Shadows. Check: a capture where a pillar stands between a lamp and a wall shows its
  shadow in Effects and not in Off; GPU time against § 6.
- **B4** Traced Corner Shading. Check: corners darken; SSAO's pass does not run; GPU time against
  § 6.
- **B5** The hardware test: the decision header and its test, the in-game run, the first-launch
  trigger, the menu row. Check: `make test`; a run on the RX 6600 reports each preset's 99th-
  percentile frame time and the preset it chose.
- **B6** Show the user before-and-after shots and FPS for each preset, at their scale, before the
  defaults ship.

## 8. Invariants

- **INV-1** — Full ray tracing is today's traced view. *Test:* `-shotcompare` against the golden in
  use at the commit before B1. *Breaks when:* a change for Effects reaches `RecordRtTrace` or
  `pathtrace.comp`.
- **INV-2** — Off is today's rasterised view, pixel for pixel. *Test:* `scripts/ab_capture.sh`
  at E1M1 start in `renderer 2` and `renderer 1` with `rt_mode` 0, against the commit before B1 pinned with
  `-rtview 0` (there Ultra, `renderer 1`, draws traced), with a same-build repeat as the noise control: the difference within the control's. *Breaks
  when:* the scene pass writes or reads the new targets in Off, or the composite reads an Effects
  image there.
- **INV-3** — An Effects frame with both switches off looks like Off. *Test:* the same capture pair,
  `rt_mode` 1 with `rt_shadows` 0 and `rt_ao` 0 against `rt_mode` 0, same build. *Breaks when:*
  `mesh.frag` drops its point-light sum without the shadow pass adding it back, or SSAO stops when
  traced corner shading is off.
- **INV-4** — Each effect is drawn at most once, never the fake and the traced version together,
  and is drawn whenever a setting asks for it. *Test:* `tests/gfx_settings_test.cpp` holds the gate
  function that maps the settings to the passes that run, over every combination of `rt_shadows`,
  `rt_ao` and `ssao` with `rt_mode` 0 and 1. It checks that point lights have exactly one source,
  and corner shading one when `ssao` is on or `rt_mode` is 1 with `rt_ao` on, and none otherwise. *Breaks when:* the point-light sum and the shadow
  pass both add light, or SSAO and traced corner shading both darken.
- **INV-5** — A door that moves moves in the traced shadows too. *Test:* a capture sequence at an
  E1M1 door with `rt_mode` 1 and Traced Shadows on, door shut then open: the light through the
  doorway appears once it opens. *Breaks when:* `RecordRefitAS` stays inside `RecordRtTrace` only, or the TLAS is not rebuilt
  after the refit.
- **INV-6** — A GPU without ray tracing never runs a traced pass. *Test:*
  `tests/gfx_settings_test.cpp` — with ray tracing unavailable, every preset and every `rt_mode`
  value resolve to the rasterised frame, and the test's decision never returns High or Ultra.
  *Breaks when:* the `rt_mode` clamp or the decision ignores the capability.
- **INV-7** — The test picks the most demanding preset that meets its target, and only that.
  *Test:* `tests/gfx_settings_test.cpp` feeds the decision function made-up frame-time lists: one
  where every preset passes (picks Ultra), one where only Low passes, one where High passes and
  Ultra's slowest two frames in a hundred miss, and one where Medium misses 60 while High holds
  30 (picks High). *Breaks when:* the percentile, the comparison or the order is wrong.
- **INV-8** — The test runs again only when the GPU identity changes or the player asks, and a
  cancelled or failed test changes nothing. *Test:* `tests/gfx_settings_test.cpp` on the
  should-run function: same identity (no), changed driver (yes), empty identity (yes); and on the
  result writer: a cancelled result leaves every key as it was, and a row changed by hand records
  the identity. *Breaks when:* the identity
  compare is reduced to the device ID, or a partial result is written.
- **INV-9** — A hand-edited config cannot break the settings. *Test:* `tests/config_bounds_test.cpp`
  gains the § 5 keys' bounds: negative, past the top, and text where a number belongs. *Breaks
  when:* a value reaches an array index or a pipeline choice unclamped.
- **INV-10** — The § 6 budgets hold. *Test:* `-frameledger` on both arms as § 6 sets them, medians
  and 99th percentiles compared. *Breaks when:* a pass traces past its resolution or its ray cap.

## 9. Alternatives considered (and rejected)

- **Quality rows on the full traced view only** (the first shape agreed on 2026-11-15). The user
  chose switches over it. The research lever it rested on, lighting at reduced resolution, stays
  as DOOM-0188 for the Full mode.
- **Switches inside the full traced view.** Its cost is direct light with shadows and the
  denoiser behind it, so with shadows off little would be saved and little traced would be left.
- **Shadow rays from `mesh.frag` directly.** Simplest to write, but the scene pass shades hidden
  fragments too (it has no depth pre-pass), so rays would be spent on pixels the player never
  sees. A pass after the scene traces each visible pixel once.
- **Shadow maps for every lamp.** A raster fake, which is DOOM-0618's to weigh; it is not a
  traced effect.
- **A hardware table instead of a measurement.** A table of cards goes stale and cannot see
  drivers, clocks or the player's resolution; the measurement sees all of them.

## 10. Open questions

- **Presets above today's look.** The user, 2026-11-15: ray tracing "should shift with the
  capability of the hardware". The test already picks the top preset a card holds; presets past
  Ultra (more rays, full-resolution lighting) are later work, filed on DOOM-0279.
- **Trace Quality in Full.** Should the row also drive DOOM-0188's reduced-resolution lighting in
  the Full mode once that lands? Left to DOOM-0188.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `-shotcompare`, by hand at B1 to B4 |
| INV-2 | **nothing** automatic — `scripts/ab_capture.sh` by hand at B1 to B4 |
| INV-3 | **nothing** automatic — `scripts/ab_capture.sh` by hand at B2 to B4 |
| INV-4 | `tests/gfx_settings_test.cpp` |
| INV-5 | **nothing** automatic — a capture by hand at B3 |
| INV-6 | `tests/gfx_settings_test.cpp` |
| INV-7 | `tests/gfx_settings_test.cpp` |
| INV-8 | `tests/gfx_settings_test.cpp` |
| INV-9 | `tests/config_bounds_test.cpp` |
| INV-10 | **nothing** automatic — `-frameledger` by hand at B3 to B5 |

## 12. Cross-doc impact

- `CLAUDE.md` § Render tiers: each 3D tier has three ways to draw, not two.
- `docs/standards/renderer.md` § The three tiers: `~` and `RB_ApplyTierRt` no longer pin the view
  to the tier.
- `docs/standards/performance.md` § The frame-rate floors: the Effects frame is a traced view, with
  the 30 FPS floor.
- DOOM-0618 lists the fakes this spec's Off side relies on.
- `CHANGELOG.md` on shipping.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0617-rt-effect-switches-loop-log.md`.
