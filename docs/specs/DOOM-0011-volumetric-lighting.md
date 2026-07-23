# DOOM-0011 — Volumetric lighting (god-rays + fog) in the ray-traced view

**Status:** **Draft — pre-`/cold-eyes`.** Design approved by the user 2026-07-21
(brainstorm) and scope-widened 2026-07-23: the volumetrics run whenever the
**ray-traced path is engaged**, so they cover **both Solid-RT and Ultra-RT**, not
Ultra alone. The **rasterised** "Original" view (Solid-raster / Ultra-raster) gets a
**separate, faked** screen-space treatment tracked as its own item
(**DOOM-0238**, "match the RT look as closely as raster allows", user 2026-07-23) —
**out of scope here**. This spec is the RT technique only. Not yet implemented; runs
through `/cold-eyes` (rule 14) before any code.

**Cold-eyes log** (rule 14 — loop until convergence): _pending — loop 1 to run._

---

This adds **single-scattering participating media** to the path tracer: light
visibly travelling *through the air* — dramatic **shafts** where a strong light
(sky through a doorway/hole, a torch in a dark room) meets darkness, **coloured fog
pooling low** in toxic/goo rooms, and a **thin global haze** on hell levels so fires
glow and soften. It reuses the existing emitter set and the SVGF half-res +
denoise machinery; it adds **one** new thing the engine does not have today — a
**directional sky term** (a "sun" vector) so sky light can form slanted shafts.

Keep the DOOM feel: stylised and a little exaggerated (the RT-DOOM look the user is
after, cf. DOOM-0193), not photoreal — and **cheap & smooth**: half-res, dithered,
denoised, staying within a handful of FPS of today (Ultra ~45 FPS, goo room ~40),
tuned on the user's RX 6600 together. Start subtle.

**Depends on:**
- **DOOM-0009** (path tracer) — the RT back-end. This adds a **view-ray march**
  over `t ∈ [0, tHit]` in `shaders/pathtrace.comp` **mode 4** (NEE display) and
  **mode 6** (denoised play), the same two display modes DOOM-0181/0183 hook, right
  after the primary hit is resolved (`tHit`/`hitP` at `pathtrace.comp:915-916`
  mode 4, `:1093-1094` mode 6). Unlike those features there is **no existing
  shading point to extend** — the march is genuinely new code between the primary
  hit and the final composite.
- **DOOM-0042 / DOOM-0119** (emitter set) — the fog's light sources are the
  **existing** static emitters: the `Emitters` device-address buffer
  (14 floats/record, `pt_common.glsl:52-56`), sliced `[0, omniStart)` = oriented
  static wall/flat lights via `omniStart = pc.misc4.y` (`r_vulkan.cpp:7400`). Fog
  scatters **only** sky + those big static lights (§4.4) — no new light system.

**Delivers / relates:**
- The **sky "sun" direction** added here (§4.4) is the first directional light in
  the engine; a future real-sky-shadows or god-rays-from-windows feature can reuse
  it. No existing item owns it.
- Complements **DOOM-0183** (glowing liquid): a goo pool now both *glows wet*
  (0183) **and** *fills its room with green fog* (this). They share the liquid
  flag (`RB_FLAG_LIQUID_NUKAGE`, `rb_materials.h:17`) but are independent layers.

**Defers (explicitly NOT in this build):**
- **The rasterised fake** — screen-space shafts + colour fog for the Solid/Ultra
  *Original* (non-RT) view. Its own item **DOOM-0238** (user 2026-07-23), because
  the technique (screen-space post-process) shares almost nothing with the traced
  march. Sequenced after this ships.
- **Classic** (1997 software renderer) — no volumetrics; the software rasteriser
  can't and the user did not ask for it.
- **Multiple scattering / true GI through the medium** — v1 is single scattering
  only (one in-scatter event per march sample). Dense-media multi-bounce is far
  future.
- **Dynamic lights in fog** — fireball/sprite lights (`[omniStart, emitCount)`),
  muzzle flash (`misc2.z`), and the flashlight (`misc2.w`) do **not** scatter
  (perf + avoids combat-strobe noise in the air). User decision 2026-07-21.
- **Per-sector authored fog volumes** — v1 derives density from the level
  (hell flag) + the primary hit (liquid flag), not hand-placed volumes (§4.5, Q3).

**Scope:** the RT path only — `pathtrace.comp` modes 4 + 6, i.e. **RT engaged**
(`rb_rtdebug` ∈ {4, 6}) in **either** the Solid or the Ultra tier. Classic, and the
raster path (Solid/Ultra with RT off), stay **byte-identical**. The offline GI bake
(`bake.comp`) is untouched — fog is a **view-ray** term and never enters the bake
(INV-6).

---

## Contents

- §1 Goal — §2 Where this sits — §3 The problem, precisely — §4 Design
  (4.1 hook · 4.2 the march · 4.3 density & colour · 4.4 light sources & shafts ·
  4.5 area profiles · 4.6 half-res, denoise, composite) — §5 Data & resources —
  §6 Performance budget — §7 Build order — §8 Invariants — §9 Alternatives
  considered — §10 Open questions

---

## 1. Goal

In the ray-traced view, make light **visible in the air**:

- **Shafts (§4.4)** — where sky light reaches the fog through a hole/doorway, or a
  torch lights the fog in a dark room, a visible beam forms (bright lit fog beside
  shadowed fog; the eye integrates the difference into a shaft).
- **Coloured fog by area (§4.5)** — default clear air with subtle shafts; **thick
  low green fog** in nukage/goo rooms; **thin red-tinted haze** across hell levels.
- **Pooling (§4.3)** — fog is denser near the floor, so it settles into a layer
  rather than filling rooms uniformly.
- **Colour is the effect** — the in-scattered light is tinted by both the light
  making it (warm torch, sky-toned sky) and the medium it is in (sickly green in
  goo, ember/red in hell).

Cheap and smooth, subtle by default, tuned on hardware.

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0011? |
|-----------------|----------|-----------------------|
| Classic | paletted software | No |
| Solid or Ultra, RT **off** ("Original") | raster stack | No — faked separately in **DOOM-0238** |
| Solid or Ultra, RT **on** (`rb_rtdebug` 4/6) | path tracer | **Yes** — the view-ray march |

The gate is **RT engaged** (`rb_rtdebug` ∈ {4, 6}), **not** the tier label — so
toggling RT on in Solid gets the same volumetrics as Ultra. Modes 4 (NEE display)
and 6 (denoised play) get identical treatment (kept in lockstep, as with
DOOM-0181/0183). The headless verify mode (5) and the debug modes (0–3) are
untouched (§8 INV-7).

## 3. The problem, precisely

Today the path tracer is a **single-primary-ray megakernel** (`pathtrace.comp:762`,
8×8 workgroup `:40`, dispatched at `r_vulkan.cpp:7468`). It shades the **first hit**
and composites; the air between camera and surface contributes **nothing**. Three
concrete gaps:

1. **There is no air-march and no shading point to extend.** DOOM-0181/0183 hooked
   an *existing* primary-hit block; volumetrics instead needs **new** code that
   marches the segment `t ∈ [0, tHit]` (camera `pc.camPos.xyz`, ray built at
   `pathtrace.comp:775-780`; `tHit`/`hitP` at `:915-916` / `:1093-1094`) *before*
   the final colour is written.
2. **There is no directional sky light.** The sky is a positional backdrop
   (TLAS custom-index 2, `pathtrace.comp:816-817`) sampled by `skyPanorama()`
   (`:731`) plus a constant ambient `SKY_COLOR = vec3(0.20,0.26,0.40)`
   (`pt_common.glsl:31`). A project-wide search finds **no** sun/light-direction
   vector anywhere. Sky *shafts* need a direction (which way the light slants), so
   this spec **adds** one (§4.4).
3. **Fog cannot ride the denoised lighting channel.** The SVGF composite
   re-multiplies the denoised illumination by surface albedo —
   `L = albedo * illum + emis * emisMask * ga.a` (`svgf_composite.comp:88`). A
   view-ray fog term has **no** surface albedo; multiplying it by the wall's albedo
   would be wrong. Fog must be a **separate channel composited *after* that
   re-modulation** (§4.6), and the sky-passthrough path (`svgf_composite.comp:66-71`)
   must also receive the fog term (fog in front of visible sky).

## 4. Design

### 4.1 Where it hooks

A new function `marchFog(ray origin, ray dir, float tHit, hitInfo)` runs in **both**
mode 4 and mode 6, **after** the primary hit and its surface colour are resolved but
**before** the value is written to the output image. It returns two quantities:

- **`inscatter`** (RGB) — light scattered *toward the eye* along the segment.
- **`transmittance`** (RGB or scalar) — how much of the surface behind the fog
  survives to the eye (`exp(-∫σ dt)`).

Composite (both the surface path and the sky-passthrough path, §4.6):
`outColor = surfaceColor * transmittance + inscatter`.

Non-RT paths never call it (it lives only in the modes 4/6 megakernel). The bake
never calls it (INV-6).

### 4.2 The march — single scattering, dithered, few steps

March `N` steps from the camera to the primary hit over `t ∈ [0, tHit]`:

- **Step count** `kFogSteps` (a compile-time `const`, start ~24) — fixed, not
  adaptive, for coherence and simplicity. `tHit` is clamped to a `kFogMaxDist` so a
  long sightline down a corridor does not blow the step budget (steps past
  `kFogMaxDist` contribute negligibly for the target densities).
- **Dither the start offset** per pixel (interleaved-gradient / blue-noise, reusing
  the frame counter `pc.misc3.x` on the mode-6 path) so the fixed step count does
  not band; the denoise (§4.6) then cleans the dither noise. This is the standard
  cheap-volumetrics recipe.
- At each sample point `p = origin + t·dir`:
  1. Evaluate **density** `σ(p)` (§4.3).
  2. Evaluate **in-scattered light** `Ls(p)` from the fog's sources (§4.4), each
     with a **phase function** `phase(cosθ)` weighting forward/back scatter.
  3. Accumulate `inscatter += transmittance · σ(p) · Ls(p) · dt` and then
     `transmittance *= exp(-σ(p) · dt)`.
- **Early-out** when `transmittance` falls below a small epsilon (thick fog occludes
  the rest of the march cheaply).

Phase: a **Henyey–Greenstein** phase with a mild forward bias `kFogAnisotropy`
(a `const`) — light scatters a little more toward its travel direction, which makes
sky/torch shafts read as beams rather than a flat glow. Isotropic (g=0) is the
fallback if HG reads busy (Q5).

### 4.3 Density & colour

- **Base density** `kFogBaseDensity` — a small always-on `const` so "clear air"
  still shows faint shafts (pure zero = no shafts at all). This is the "clear"
  profile.
- **Height pooling** — density scales up toward the floor:
  `σ_height = exp(-max(0, p.z − floorZ) / kFogPoolHeight)`. The floor reference
  `floorZ` for v1 is the **primary hit's** `hitP.z` when the hit faces up (a floor);
  otherwise a level-min fallback. This makes fog **settle low** without new geometry
  data (uses only `hitP`, already in hand). Its coarseness (one floor reference per
  pixel) is an accepted v1 approximation (Q3).
- **Area multiplier & tint** (§4.5) — the profile scales `σ` and sets the medium's
  **scattering tint** `mediumTint` (green in goo, red in hell, neutral in clear).
- **Colour of a shaft = light colour × medium tint.** Sky shafts inherit the sky
  tone (`SKY_COLOR` / `skyPanorama`), torch shafts inherit the emitter's `Le`
  colour; both are then multiplied by `mediumTint`. So a torch shaft in a goo room
  is warm-through-green; a sky shaft in hell is sky-through-red. Tint colours and
  strengths are compile-time `const`s (`kGooTint`, `kHellTint`, …), tuned on
  hardware toward the DOOM-0193 exaggerated look. Start subtle.

### 4.4 Light sources & shafts

Fog scatters light from **two** sources only — sky and big static emitters
(user 2026-07-21). Muzzle/flashlight/sprite lights are excluded by construction
(they are push-constant deltas / the `[omniStart, emitCount)` dynamic slice, never
iterated here — INV-3).

**(a) Sky shafts — needs a new direction.** Add a **sun direction** `kSunDir`
(world-space, a compile-time `const` default for v1 — a plausible steep slant; per
-level control deferred, Q1). At a march sample, cast **one** shadow ray toward
`kSunDir`; if it reaches the sky (TLAS custom-index 2, the same test as
`pathtrace.comp:816-817`) the sample is **sky-lit** — add
`skyRadiance · phase · mediumTint`. Samples whose sun ray is blocked by geometry are
dark. The bright/dark boundary *is* the shaft (a beam through a doorway/sky-hole).
One ray per sample keeps it affordable at half-res (§4.6).

**(b) Torch shafts — the existing static emitters.** Iterate the static slice
`k ∈ [0, omniStart)` (`omniStart = pc.misc4.y`, `r_vulkan.cpp:7400`; record layout
`pt_common.glsl:52-56`). For cost control, **do not** shadow-test every emitter at
every sample — that is `steps × emitters` rays. Instead (Q2, start cheap):
- pick the **nearest few** static emitters to the sample (distance from the record's
  centroid), and
- add each as `Le · falloff(dist) · phase · mediumTint`, with an **optional single**
  occlusion ray to the chosen emitter (start *without* occlusion — a torch glows its
  air even through a thin wall, usually acceptable and much cheaper; add occlusion
  only if light-through-wall reads wrong, Q2).

Both sources feed the same `Ls(p)` accumulation (§4.2). The sky path is the primary
shaft mechanism; torch shafts are the secondary "dark room glows around the flame"
effect.

### 4.5 Area profiles — clear / goo / hell

Three profiles select the density multiplier + `mediumTint`:

- **Clear (default).** Base density only; neutral tint. Subtle shafts, no colour.
- **Goo / toxic.** Where the **primary hit is flagged liquid nukage**
  (`RB_FLAG_LIQUID_NUKAGE = 8u`, `rb_materials.h:17`, set on the flat at
  `FlagLiquidFlats`, `r_vulkan.cpp:5910`), thicken density and set `mediumTint =
  kGooTint` (green). This is **primary-hit-keyed**: the room reads goo-foggy when
  you are looking at/across the goo. It does **not** know about goo behind you or
  around a corner (no per-sector volume in v1) — an accepted approximation (Q3);
  the honest alternative (a per-sector fog buffer) is deferred.
- **Hell.** A per-**level** flag: a **thin global haze** everywhere with
  `mediumTint = kHellTint` (faint red). Detected CPU-side from the level
  (`gameepisode`/`gamemap` + a hell fire-sky check) and crossed to the shader via a
  **new `rb_view_t` field** (§5) → the `misc6.w` lane. Default subtle; tunable.

Profiles compose: a goo room *on* a hell level gets both (green pool + red haze).

### 4.6 Half-res, denoise, composite

Fog is low-frequency, so compute it **cheaply and smooth it**:

- **Half-res march.** Mirror mode 6's existing even/even 2×2 half-res gate
  (`pathtrace.comp:1140-1142`): march fog on one pixel in four, into a **new
  half-res fog target** (`inscatter.rgb` + `transmittance` packed, e.g. one
  `RGBA16F` image). Mode 4 (display) may march full-res or half-res (Q4) — start
  half-res in both for a single code path.
- **Denoise / upsample.** Fog **cannot** ride the SVGF illumination channel
  (albedo re-multiply, §3.3). Two candidate paths (Q6): (a) a **bilateral upsample**
  of the half-res fog target guided by depth, cheapest and self-contained; (b) run
  the existing edge-aware **a-trous** passes (`r_vulkan.cpp:7545`) on the fog channel
  too. Start with (a); escalate to (b) only if the fog crawls/flickers.
- **Composite after re-modulation.** In `svgf_composite.comp`, after
  `L = albedo * illum + emis * … ` (`:88`), apply
  `L = L * transmittance + inscatter`. The **sky-passthrough** branch
  (`:66-71`, which `return`s the stored sky before re-modulation) must apply the
  **same** fog term to the sky (fog in front of a visible sky hole), so shafts read
  against the sky too.

## 5. Data & resources

- **No new images beyond one half-res fog target** (`RGBA16F`, §4.6). No new SSBOs,
  bindings, or emitter buffers — fog reuses the existing `Emitters` buffer + sky.
- **Push constants — the two genuinely-free lanes.** DOOM-0183 grew
  `RtPushConstants` to **240 B** (`static_assert(sizeof==240)`, `r_vulkan.cpp:7374`;
  `pcr.size = 240`, `:2355`) and consumed `misc6.x` (ripple time), `misc6.y` (wet
  toggle). The **only** free components today are **`misc6.z` and `misc6.w`**
  (`r_vulkan.cpp:7428-7429`, currently written 0). This feature uses **exactly those
  two**, needing **no** struct growth:
  - **`misc6.z` = `rb_fog` strength** (float; `0` = off, which also *is* the on/off
    state — one dial doubles as the toggle, §"dial" below).
  - **`misc6.w` = global haze density** (float; the hell-level haze from `rb_view_t`,
    §4.5; `0` on non-hell levels).
  - Everything else is a **compile-time `const`** per house convention
    (DOOM-0181/0183 §5): `kSunDir`, `kFogSteps`, `kFogBaseDensity`, `kFogMaxDist`,
    `kFogPoolHeight`, `kFogAnisotropy`, `kGooTint`, `kHellTint`, the per-source
    strengths. Only the runtime **strength** and the **per-level haze** vary at
    runtime, so only they take lanes.
  - **Budget note (INV-5):** this consumes the **last two free components** of the
    shared RT push block. Any *further* RT push value must append the final
    `misc7 uvec4` (240 → 256 B, the documented device limit; after that the block is
    full). This spec deliberately stays within the free lanes and does **not** add
    `misc7`, leaving that headroom for the future.
  - The fog lanes sit **beyond the 184-byte `-rtverify` prefix**
    (`static_assert(sizeof(RtPC)==184)`, `r_vulkan.cpp:6828`, which stops before
    `misc6`), so **`-rtverify` is unaffected** (INV-7).
- **New `rb_view_t` field for the hell flag.** `rb_view_t` (`r_mesh.h:265-273`)
  currently carries only `x,y,z,angle,extralight,skytexnum`. Add one field
  (e.g. `float hazeDensity`), computed beside `view.skytexnum = skytexture`
  (`r_backend.c:181`) from `gameepisode`/`gamemap` + the sky, and written to
  `misc6.w`. (`r_backend.c` does not reference the level globals today; the compute
  brings them into scope.)
- **New runtime dial `rb_fog`** — mirror the `rb_wet`/`rb_detile` pattern exactly:
  `extern "C" { int rb_fog = <default>; }` in `r_vulkan.cpp` (beside `rb_wet`
  `:1001`); a config row `{"rt_fog", &rb_fog, <default>}` in `m_misc.c` defaults
  (beside `rt_wet` `:270`); the value written to `pc.misc6[2]` (beside `misc6[1] =
  rb_wet` `:7427`). `rb_fog` is a small **strength** integer (0..K, `0` = off), not
  just a bool, so the menu "Strength" row and the on/off state share it.
- **Menu rows — both menus (DOOM-0206 doubled them).** Add a fog row to **both**
  the legacy `EffectsMenu[]`/`EffectsDef` (`m_menu.c:512-530`) **and** the crisp
  `VideoMenu[]`/`VideoDef` (`m_menu.c:567-598`), a shared `M_ChangeFog` handler
  (beside `M_ChangeWet` `:2234`), and a value-string case (beside `:1557-1560`).
  A "Strength" presentation (Off / Low / Med / High) maps to `rb_fog` 0..3.
- **New hotkey.** A free key in the `i_video.c:441-475` toggle block — `;`
  (`SDLK_SEMICOLON`) is unused (`]`=de-tile, `[`=filth, `'`=wet, `~`=view cycle,
  `` ` ``=profiler are taken). Cycles `rb_fog` and prints `Volumetric fog: <level>`.

## 6. Performance budget

- **Baseline & method:** the DOOM-0181/0183 §6 protocol — average the `` ` ``
  profiler present-total (ms, not FPS) over a fixed ~10 s walk of the **E1M1
  green-goo room** (a sky-hole/doorway scene too, for shafts), RT-on, 50 % render
  scale, with `rb_fog` **off** then **on** (same-walk A/B, the DOOM-0187 lesson).
- **Cost shape (measure, don't assert):** the march is `kFogSteps` samples/pixel,
  each with **one** sky shadow ray + a few emitter evaluations, at **half-res**
  (¼ the pixels) + denoise. The shadow rays are the pole; half-res + few steps +
  dither + denoise is what makes it affordable.
- **A dedicated GPU-timer slot needs pool growth.** The RT profiler pool is **full
  (8/8 slots used)** (`queryCount = 8`, `r_vulkan.cpp:1510`). A fog-pass timer
  requires bumping `queryCount` and widening the two resets (`:7300`, `:8285`) and
  the readback (`:8076-8086`) — a small, contained change made **with** the perf
  layer (L6), not silently skipped.
- **Levers held ready** (measure before cutting): the `rb_fog` **strength** dial is
  itself the standing perf option (lower strength → cheaper is *not* automatic, so
  also:) reduce `kFogSteps`; drop the emitter occlusion ray (§4.4b); distance-gate
  the march (`kFogMaxDist`); half-res in mode 4 too.
- **Gate (L6):** with `rb_fog` at its shipped default, the march adds **≤ a few FPS**
  vs the `rb_fog`-off RT-on baseline on the goo-room walk (the user's "within a
  handful of FPS" bar; the DOOM-0181 ≤ 5 % present-total figure is the reference).
  Ultra must stay above the 60 FPS floor where it is today; the goo room's existing
  ~40 FPS is the megakernel/denoiser (per DOOM-0183 framing), and fog must not make
  it materially worse.

## 7. Build order

Each layer is independently play-testable (renderer look is a play-test call, per
DOOM-0181/0183). L1–L5 acceptance is **human play-test**; only **L6**'s perf + verify
is objective.

| Layer | Scope | Verify | FPS-gate? |
|-------|-------|--------|-----------|
| **L1** | The march skeleton: `marchFog` over `[0,tHit]`, constant base density, **isotropic single scatter from the sky only** (no direction yet — flat sky ambient), composited via a new half-res fog target + bilateral upsample + the `svgf_composite.comp:88` post-multiply (incl. the sky-passthrough branch). Full RGB, no colour profiles. | Air picks up a faint uniform glow; surfaces behind thick fog fade; sky still visible through fog; no NaNs; modes 4 & 6 match | no |
| **L2** | **Sky shafts:** add `kSunDir` + the one-ray sky-visibility test per sample + HG phase. | A doorway/sky-hole open to sky throws a visible slanted beam; closed rooms stay clear; the beam moves correctly as the camera orbits | no |
| **L3** | **Height pooling + torch shafts:** height-based density (`hitP.z` floor ref); iterate static emitters `k<omniStart` (nearest-few, no occlusion first). | Fog settles low into a floor layer; a torch in a dark room glows its surrounding air; dynamic/muzzle/flashlight do **not** scatter | no |
| **L4** | **Area profiles + colour:** goo tint via the primary-hit `RB_FLAG_LIQUID_NUKAGE`; hell haze via the new `rb_view_t` field → `misc6.w`; `mediumTint` colouring (light×medium). | Goo rooms fill green and pool low; hell levels gain a faint red haze; a torch shaft reads warm-through-green in goo; clear levels stay neutral | no |
| **L5** | **Denoise/quality pass:** dither tuning; escalate upsample→a-trous if it crawls (§4.6 Q6); phase/anisotropy tune. | Fog is smooth, not grainy or crawling, in a slow pan; shafts hold their shape | no |
| **L6** | **Runtime dial + menu + key + perf:** `rb_fog` (`rt_fog` config), both menu rows, the `;` key, the profiler-slot growth, and the perf pass. | Toggle/strength flip cleanly off→low→high; adds ≤ a handful of FPS vs off (§6); `-rtverify` **green**; 60 FPS floor held | **yes** |

**Interim state (expected, not a regression):** L1 ships a flat sky-ambient glow
with **no** shafts (the directional term arrives at L2) and **no** colour (profiles
arrive at L4) — mirroring DOOM-0183's "sheen-before-ripple" staged interim.

## 8. Invariants

- **INV-1:** Fog is **single-scattering** along the **primary view ray** only —
  one in-scatter event per march sample, no secondary bounces, no path extension.
  It is composited as `surface·transmittance + inscatter` after the primary hit.
- **INV-2:** Fog scatters light from **sky + static emitters `[0, omniStart)`
  only** (`omniStart = pc.misc4.y`). Dynamic sprite lights `[omniStart, emitCount)`,
  the muzzle flash (`misc2.z`), and the flashlight (`misc2.w`) **never** scatter.
- **INV-3:** The sky **sun direction** (`kSunDir`) is **new** — no directional light
  existed before (only positional sky + constant `SKY_COLOR`). v1 is a compile-time
  `const`; per-level control is deferred (Q1).
- **INV-4:** Fog is a **separate channel composited *after* the SVGF albedo
  re-multiply** (`svgf_composite.comp:88`) — it never rides `gillum`/`illum` (which
  is multiplied by surface albedo). The sky-passthrough branch (`:66-71`) receives
  the same fog term.
- **INV-5:** The two runtime values ride **`misc6.z` (fog strength) + `misc6.w`
  (haze density)** — the **last two free components** of the 240-byte
  `RtPushConstants`. This feature adds **no** struct growth and does **not** append
  `misc7`; the C++ struct, its `static_assert` (`r_vulkan.cpp:7374`), `pcr.size`
  (`:2355`), and the GLSL push block stay at 240 B.
- **INV-6:** The GI bake (`bake.comp`) is **untouched** — fog is a view-ray term and
  never enters the bake (which computes surface irradiance). No double-count.
- **INV-7:** Ultra **and** Solid, **RT engaged only** (`rb_rtdebug` ∈ {4, 6}).
  Classic and the raster path (RT off) are **byte-identical**. The fog lanes sit
  beyond the 184-byte `-rtverify` prefix (`r_vulkan.cpp:6828`), so **`-rtverify` is
  unaffected**; the headless verify mode (5) and debug modes (0–3) are untouched.
- **INV-8:** Every fog cost is **`rb_fog`-gated** — `rb_fog == 0` skips the march
  entirely (the branch is not taken), so the RT path with fog off is byte-identical
  to today.

## 9. Alternatives considered

- **Screen-space / post-process god-rays (radial blur from a bright source).**
  Rejected for the RT path: it is the *raster fake* (now **DOOM-0238**). In the
  traced view a real march gives correct off-screen-source shafts and volumetric
  occlusion that a screen-space blur cannot.
- **Froxel (volumetric texture) fog, à la Frostbite.** Rejected for v1: a 3D froxel
  grid + its own populate/scatter passes is a much larger system. A per-pixel march
  reuses the existing megakernel + SVGF and is enough for DOOM's room scales;
  revisit only if the per-pixel march proves too noisy/expensive (Q6).
- **Fog inside the SVGF illumination channel.** Rejected — the composite
  re-multiplies by albedo (`:88`), which a view-ray term must not receive (INV-4).
- **Per-sector authored fog volumes.** Deferred: needs a per-sector attribute buffer
  the engine does not have (the seam carries only `skytexnum`). v1 derives density
  from the level (hell) + primary hit (liquid) instead (§4.5, Q3).
- **Growing the push block now (add `misc7`) for richer runtime control (sun dir,
  per-area colours as uniforms).** Rejected: v1 needs only two runtime scalars;
  colours/direction are `const`s. Keeping to `misc6.z/.w` preserves the last
  `misc7` uvec4 of headroom (INV-5).
- **Scattering dynamic + flashlight lights too.** Rejected by the user (2026-07-21):
  perf, plus fog strobing with the muzzle/flashlight would be combat noise.

## 10. Open questions

- **Q1 (sun direction):** a single world-space `const` for all levels (v1), or
  per-level/per-sky tuning? Start `const`; the shaft angle is a look-tune (L2).
- **Q2 (torch-shaft cost):** nearest-few emitters with **no** occlusion ray (cheap,
  may glow through thin walls) vs one occlusion ray each (correct, costlier). Start
  no-occlusion; add if light-through-wall reads wrong (L3).
- **Q3 (density source):** primary-hit-keyed goo density (v1, cheap, blind to
  goo behind/around corners) vs a per-sector fog buffer (correct, new plumbing).
  v1 takes the primary-hit key; revisit if the room-fill reads wrong (L4).
- **Q4 (mode-4 resolution):** march mode 4 (display) at full-res or half-res? Start
  half-res both for one path; raise mode 4 only if the display view looks soft.
- **Q5 (phase):** Henyey–Greenstein forward-bias vs isotropic — a shaft-shape tune
  (L2/L5).
- **Q6 (denoise path):** depth-guided bilateral upsample (cheap, self-contained) vs
  routing the fog channel through the a-trous passes (`:7545`). Start bilateral;
  escalate if the fog crawls (L5).
- **Q7 (hell detection):** which `gameepisode`/`gamemap` + sky signals classify a
  level as "hell" for the haze — a data/tuning call at L4 (default subtle, tunable).
- **Q8 (tonemap headroom):** bright sky shafts must read strong without clipping to
  a flat white slab under the PBR-Neutral tonemap — verify at L2/L5 (same caution as
  DOOM-0183 Q7).
