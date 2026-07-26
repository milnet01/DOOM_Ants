# DOOM-0008 — True-3D Vulkan path-traced renderer

**Status:** Shipped (Stage 1, 2026-06-25, user-confirmed on DOOM 1 & DOOM 2). Built
via `/cold-eyes` loops 1–2 (see loop log). Stage 2 (the hardware path tracer) is
specced separately in DOOM-0009.

This spec covers the `R_Vulkan` 3D back-end that plugs into the
`renderer_backend_t` seam shipped in DOOM-0026. The **target is a real-time
hardware path tracer** — every pixel lit by Monte-Carlo path tracing over the
true-3D level geometry — that still looks and plays like DOOM (original art,
sprites, 256-colour palette as the albedo source, vanilla movement and feel).
The user's direction is explicit: full path tracing, everything converted to 3D,
**extremely performant** (60 FPS floor, DOOM-0012), reached iteratively with real
measurement.

A path tracer that fast is built in stages — you cannot write a performant one in
one shot. The stages are the *route*, not a watering-down of the goal:

- **Stage 1 — 3D scene + bring-up (DOOM-0008 foundation):** convert the 2.5D map
  to real 3D meshes, upload materials/lights, build the ray-tracing acceleration
  structures, and get a correct image on screen via a simple primary-ray pass.
  Validates geometry, materials, sprites, camera, and UI compositing. Selectable
  as "Renderer: 3D".
- **Stage 2 — Path tracer (DOOM-0009 ray/path tracing):** the Monte-Carlo
  integrator — BRDF importance sampling, next-event estimation to emissive
  surfaces/lights, multi-bounce global illumination, hardware ray-traced
  shadows. This is "full path tracing".
- **Stage 3 — Make it fast (DOOM-0010/0011/0012):** temporal accumulation,
  ReSTIR light sampling, spatio-temporal denoising (SVGF/A-SVGF), adaptive
  sampling and render-scale — driven to the 60 FPS floor with profiling. Dynamic
  lighting (DOOM-0010) and volumetric participating media (DOOM-0011) fall out of
  the path tracer naturally and are folded in here.

All tuning curves in the path tracer (BRDF terms, importance-sampling PDFs,
attenuation, tonemap, denoiser blend weights) are **authored, fitted, and
validated in the Vestige Formula Workbench** and exported as GLSL — see
[Formula Workbench integration](#formula-workbench-integration). No hand-typed
magic constants in shaders (project coding standard).

The graphics-API / language / shader decisions (Vulkan, hardware ray tracing;
engine stays C, back-end is C++; GLSL → SPIR-V) are **owned by ADR
`docs/decisions/0001-renderer-language-and-api.md`** and the seam architecture by
`docs/specs/DOOM-0026-renderer-backend.md`; ADR 0001 is updated alongside this
spec to make path tracing the goal (not a deferred tier).

## Contents

- [Goal](#goal)
- [Background — the data we render from](#background--the-data-we-render-from)
- [Approach](#approach)
- [Window & device ownership](#window--device-ownership)
- [Tier auto-detection & fallback](#tier-auto-detection--fallback)
- [Geometry: 2.5D map → 3D meshes](#geometry-25d-map--3d-meshes)
- [Materials, palette & lights](#materials-palette--lights)
- [Things as billboarded sprites](#things-as-billboarded-sprites)
- [The path tracer](#the-path-tracer)
- [Formula Workbench integration](#formula-workbench-integration)
- [Performance plan (the 60 FPS road)](#performance-plan-the-60-fps-road)
- [Frame path & UI compositing](#frame-path--ui-compositing)
- [Build system](#build-system)
- [Components / affected files](#components--affected-files)
- [Invariants](#invariants)
- [Verification](#verification)
- [Out of scope (YAGNI)](#out-of-scope-yagni)
- [Staging summary](#staging-summary)
- [Cold-eyes loop log](#cold-eyes-loop-log)

## Goal

"Renderer: 3D" becomes a real, selectable option that path-traces the DOOM world
in real time: true 3D geometry, physically-based light transport with global
illumination and hardware ray-traced shadows, on DOOM's original paletted art and
sprites so it still reads as DOOM. Classic stays the default and untouched
(DOOM-0026 INV-1 — byte-identical Classic output — still holds). The renderer
scales by hardware tier and is pushed to a 60 FPS floor.

The dev machine is verified capable: AMD RX 6600 (RADV) with
`VkPhysicalDeviceAccelerationStructure*` and ray tracing reported,
`vulkan.h`/`vulkan.hpp`, `libvulkan 1.4.350`, `glslc`/`glslangValidator`, and
`SDL_vulkan.h` all present.

## Background — the data we render from

All world data is loaded by `p_setup.c` into global arrays (`p_setup.c:56-75`):
`vertexes`/`numvertexes`, `sectors`/`numsectors`, `sides`/`numsides`,
`lines`/`numlines`, `segs`/`numsegs`, `subsectors`/`numsubsectors`,
`nodes`/`numnodes`. Coordinates are `fixed_t` 16.16 (`m_fixed.h:33-38`); divide
by `FRACUNIT` (65536.0) for float world units.

- **Sectors** (`r_defs.h:101-135`): `floorheight`, `ceilingheight` (`fixed_t`),
  `floorpic`, `ceilingpic` (flat indices), `lightlevel` (0–255). A sector is the
  3D volume between its floor and ceiling planes.
- **Subsectors** (`r_defs.h:227-233`): the BSP's convex leaf polygons —
  `numlines` segs from `firstline`, one `sector`. The natural unit to
  triangulate floor/ceiling caps from.
- **Segs** (`r_defs.h:240-258`): wall pieces — `v1`/`v2`, `sidedef`, `linedef`,
  `frontsector`, `backsector` (NULL ⇒ one-sided/solid wall).
- **Sidedefs** (`r_defs.h:144-161`): `toptexture`/`midtexture`/`bottomtexture`,
  `textureoffset`/`rowoffset`.
- **View state** from `R_SetupFrame` (`r_main.c:830-863`): `viewx`, `viewy`,
  `viewz` (`fixed_t` camera position), `viewangle` (binary angle); horizontal FOV
  `FIELDOFVIEW 2048` binary-angle units = 90° (`r_main.c:48`).
- **Textures** composited by `R_GetColumn(tex,col)` (`r_data.c:386-405`,
  paletted columns); flats are raw 64×64 paletted lumps from `firstflat`
  (`r_data.c:587-600`).
- **Palette** `PLAYPAL`, 256×RGB (cached e.g. at `d_main.c:278`); the 8-bit→RGB
  `palette[]` LUT is built in `I_SetPalette` (`i_video.c:254-273`) and applied
  per-pixel in `I_FinishUpdate` (`i_video.c:202-241`). **Colormap** `COLORMAP`
  32×256 (`r_data.c:639-650`; `NUMCOLORMAPS 32` at `r_main.h:87`) — used by
  Classic for light diminishing; the path tracer
  uses the palette for albedo and computes its own lighting (see Materials).
- **Sprites** `spritedef_t`/`spriteframe_t` (`r_defs.h:427-453`); rotation pick
  `rot = (ang-thing->angle+(unsigned)(ANG45/2)*9)>>29` (`r_things.c:523`);
  `flip[rot]` mirrors. Patches are paletted `post_t` columns with transparent
  gaps (`r_defs.h:285-292,356-364`; metadata `r_data.c:609-632`).
- **Screen**: physical 640×400, logical UI 320×200 (`doomdef.h:100-113`,
  DOOM-0027). UI is drawn paletted into `screens[0]`, the physical full-frame
  buffer allocated in `V_Init` (`v_video.c:517-535`); the draw primitives
  (`V_DrawPatch`/`V_DrawBlock`) are elsewhere in `v_video.c`.

The current SDL layer creates a **2D `SDL_Renderer`** (`i_video.c:288-347`,
`I_FinishUpdate` `:202-241`) — no Vulkan in the window today. The coexistence
problem is handled in [Window & device ownership](#window--device-ownership).

## Approach

A new C++ back-end `r_vulkan.cpp` (+ helpers) exports a small set of `extern "C"`
entry points (`RB_Vulkan_Available` / `Init` / `RenderView` / `Present` / …);
`r_backend.c` wraps those in the `renderer_backend_t` structs it registers in the
`RB_RT3D` / `RB_RASTER3D` slots. The C engine never sees a C++ type; the seam
*is* the C/C++ boundary (ADR 0001). Vulkan is driven via `vulkan.hpp`, memory via
VMA; shaders are GLSL → SPIR-V at build time, embedded as byte arrays so the
binary is self-contained.

The scene is built **once per level** (hooked at the end of `P_SetupLevel`) into
GPU buffers and a ray-tracing **acceleration structure** (BLAS per mesh, one
TLAS). Per frame: set the camera from the view globals, refresh dynamic instance
transforms (things/sprites) and refit the BLAS of any moving sector (doors/lifts),
trace, accumulate, denoise, composite the UI,
present. Rigid movers (things/sprites) update TLAS instance transforms. Moving
sectors (doors/lifts) change wall/cap heights *non-rigidly* (§Geometry), so the
affected sector's BLAS is **refit** (not rebuilt) each active frame — a rigid
instance transform cannot express a wall-height change; DOOM-0009 §3 resolves this.
Only new or topology-changing geometry rebuilds a BLAS from scratch.

**Rasterised primary visibility (a performance lever).** Full path tracing is the
quality target; for performance the primary-visibility hit can be produced by a
cheap rasterised G-buffer (depth/normal/material/motion) instead of a primary
ray, with path tracing doing all secondary bounces, shadows, and GI. This is a
*performance toggle*, not a different renderer — Stage 1 brings up the raster G-buffer for
exactly this reason, and Stage 2's integrator consumes either a rasterised or
ray-traced primary hit behind the same interface.

## Window & device ownership

Only one back-end is active at a time and presentation belongs to it:

- **Classic** keeps the SDL_Renderer path (`I_FinishUpdate`) unchanged.
- **3D** owns a Vulkan surface on the SDL window and presents via a swapchain.

`Available()` probes capability with a **headless** `VkInstance` (enumerate
devices + extensions; **no window/surface needed**), so it is safe to call before
any window rework, after `I_InitGraphics`. When the 3D back-end's `Init()` runs as
the selected mode it **recreates the window** with `SDL_WINDOW_VULKAN` via new
`i_video.c` entry points (`I_GetWindow`, `I_ShutdownGraphicsForVulkan`), then
`SDL_Vulkan_CreateSurface`. All SDL knowledge stays in `i_video.c`; the back-end
stays Vulkan-only. The startup path (config says 3D and it is available → Vulkan
window from the first frame) is the primary, fully-in-scope path; mid-game
menu hot-swap recreating the window is deferred (DOOM-0026 already scopes it out)
— the menu writes config and shows "applies on restart" until a live recreate is
proven safe.

## Tier auto-detection & fallback

In `Available()` / `Init()` (consumed by `RB_Init`'s existing clamp logic):

- **`RB_RT3D`** — device exposes `VK_KHR_acceleration_structure` +
  `VK_KHR_ray_query` (and, if we adopt a ray-tracing pipeline,
  `VK_KHR_ray_tracing_pipeline`): the full path tracer.
- **`RB_RASTER3D`** — Vulkan present but no RT extensions: the same 3D world via
  the rasterised G-buffer, lit by a screen-space approximation (SSAO + a small
  analytic-light pass), **no** path-traced GI/shadows. Never an error — a
  graceful "3D but not ray traced" tier for non-RT GPUs.
- **No usable Vulkan** — only `RB_CLASSIC` offered (existing behaviour).

A single build runs on all three tiers; the persisted config value (config key
`renderer` ↔ engine var `rendermode`) is clamped to what is available by the
existing `RB_Init` (DOOM-0026 INV-3).

## Geometry: 2.5D map → 3D meshes

Built once per level into a vertex/index buffer and BLAS. World units =
`fixed / FRACUNIT`; axes: world `x` east, `y` north, `z` up (floor/ceiling
heights are `z`). Winding/culling consistent so geometry is watertight for ray
tracing (gaps cause light leaks).

**Walls** — for each seg with a `sidedef`:

- *One-sided* (`backsector == NULL`): one quad `frontsector->floorheight …
  ceilingheight`, textured `midtexture`.
- *Two-sided* (portal): **upper** quad between the two ceilings when
  `front.ceilingheight > back.ceilingheight` (`toptexture`); **lower** quad
  between the two floors when `front.floorheight < back.floorheight`
  (`bottomtexture`); a **mid** quad only if `midtexture != 0` (rails/grates),
  alpha-tested.
- UVs from seg `offset` + `sidedef->textureoffset` (U) and height span +
  `rowoffset` (V); `ML_DONTPEGTOP/BOTTOM` (`linedef->flags`) choose V anchoring,
  matching the software renderer's pegging. Sky-textured surfaces use the sky
  path (an emissive/environment miss), not a normal material.

**Floors & ceilings** — per subsector, triangulate its segs' `v1` points as a
fan into a cap at `sector->floorheight` (up normal) and `ceilingheight` (down
normal), textured with `floorpic`/`ceilingpic`. Flats tile on the fixed 64×64
world grid (DOOM convention) → world-position UVs. A `floorpic`/`ceilingpic`
equal to `skyflatnum` is the sky path.

Degenerate/zero-area subsectors are skipped. Animated flats and switch/animated
wall textures (`p_spec.c` cycles indices) are handled by re-resolving the texture
index per frame from the live sector/side fields. Door/lift sector-height changes
move the affected wall/cap vertices: cheap per-sector BLAS updates (refit, not
full rebuild) keep the AS valid without per-frame full rebuilds.

## Materials, palette & lights

DOOM's *art* carries the look; the path tracer provides the *light*.

- **Albedo:** upload each wall texture, flat, and sprite as an **R8 palette-index
  image** plus a 1-bit mask (from `post_t` gaps / transparent index). Decode to
  linear RGB in-shader via a `PLAYPAL` lookup texture (256×RGB) and an
  sRGB→linear transform (Formula-Workbench `srgb_to_linear`). Keeping the art
  paletted preserves DOOM's exact colours and makes dynamic palette effects
  (damage red, pickup, radsuit — `I_SetPalette`) a single lookup-row swap that
  tints world and UI uniformly.
- **Surface model:** mostly Lambertian diffuse with a GGX specular lobe at a
  default low gloss; a small material table can later classify metal/shiny
  textures by name. BRDF terms (`fresnel_schlick`, `ggx_distribution`,
  `schlick_geometry`) come from the Formula Workbench.
- **Lights / emission:** DOOM has no explicit light objects, so emission is
  derived:
  - **Sector glow:** a sector's `lightlevel` seeds an emissive term on its
    surfaces (brighter sectors emit more), so overall brightness tracks vanilla
    expectations and there is light to bounce.
  - **Bright textures:** known light/lamp/computer textures and explicitly bright
    sector specials become stronger emissive surfaces — sampled by NEE so they
    actually illuminate the room.
  - **Sky:** sky sectors emit a directional "sun" + sky-colour environment on
    miss, giving outdoor areas real sunlight and long shadows.
  - **Dynamic lights (DOOM-0010):** muzzle flash / weapon fire, and later
    flicker/strobe specials, added as analytic lights the integrator samples.
    Stage 2 ships with at least the muzzle flash so ray-traced dynamic shadows
    are visible.

`extralight` (weapon flash) raises exposure/emission exactly as the software path
applies it, so firing lights the room.

## Things as billboarded sprites

Things (monsters, items, decorations) stay **billboarded sprites** — load-bearing
for the DOOM look; no voxels/models. Each thing is an alpha-tested quad placed in
the TLAS as an instance of a unit-quad BLAS, with a **per-frame instance
transform** that yaws the quad to face the camera (upright/cylindrical billboard)
and scales it from `spritewidth`/`spriteoffset`/`spritetopoffset`
(`r_data.c:609-632`). The sprite `lump`/`flip` is chosen by the same rotation
formula as `r_things.c:523`. The sprite's masked silhouette (R8 index + 1-bit
mask) alpha-tests in the any-hit/closest-hit path, so primary rays show the
sprite and secondary/shadow rays see its silhouette — sprites cast and receive
approximate shadows/GI as flat cards (the classic cut-out look, acceptable and
in-keeping). `MF_SHADOW` (spectres) reuse a fuzz/translucent approximation.
Sprite GPU images upload lazily on first use and are cached for the level.

## The path tracer

The integrator (Stage 2). Implementation starts as a **compute-shader megakernel
using `VK_KHR_ray_query`** (one dispatch over screen pixels — simplest path to a
correct image and easiest to profile), with a refactor to a full
`VK_KHR_ray_tracing_pipeline` (rgen/rmiss/rchit/rahit) kept open if shader-record
flexibility or performance demands it.

Per pixel, per frame:

1. **Primary hit** — from the rasterised G-buffer (perf path) or a primary ray
   (reference path): position, normal, material, motion vector.
2. **Direct lighting (NEE)** — sample emissive surfaces / analytic lights, trace
   a shadow ray (ray-query) for visibility, weight by the BRDF and MIS
   (power-heuristic) between BSDF- and light-sampling.
3. **Indirect** — importance-sample the BRDF (cosine-weighted diffuse;
   GGX/VNDF specular), trace the bounce ray, recurse for *N* bounces with
   **Russian-roulette** termination (survival curve fitted in the Workbench) to
   stay unbiased and bounded.
4. **Accumulate** — add the sample to a radiance buffer; under camera motion,
   reproject prior frames via motion vectors (temporal accumulation) for many
   effective samples at 1–2 spp/frame.
5. **Denoise → tonemap → encode** — spatial denoise (SVGF/A-SVGF) using
   Workbench-fitted edge-stopping weights, exposure (`exposure_ev` + `extralight`),
   ACES tonemap (`aces_tonemap`), encode to the swapchain format. *(Superseded:
   DOOM-0009 §3 selects Khronos PBR Neutral over ACES for the shipping Stage-2
   integrator — this ACES line predates that choice.)*

Determinism/quality guards: per-pixel RNG seeded by pixel + frame; safe-math
guards (from the Workbench GLSL prelude) prevent NaN fireflies; a firefly clamp
on first-bounce radiance.

*(Superseded — Stage-2 shipping model: DOOM-0009 §3/§4 narrowed this integrator to a
**pure-Lambert, NEE-only** direct pass with **baked** static GI (per-subsector SH-L1)
plus a single live direct/shadow bounce — **no** live multi-bounce recursion, **no
MIS**, and Russian-roulette moot (nothing multi-bounce to terminate), per the DOOM-0092
research decision. The BRDF / MIS / multi-bounce / Russian-roulette description above —
and the `MIS power-heuristic (requested)` + `Russian-roulette survival (requested)`
Workbench curves listed below — are the original DOOM-0008 aspiration, not what ships;
DOOM-0009 is canonical for the Stage-2 integrator. This matches the corrected Stage-2
row in the stage table below.)*

## Formula Workbench integration

Per the project coding standard (no hand-coded magic constants in rendering
formulas — shared with Vestige CLAUDE.md Rule 6), every tunable curve in the path
tracer is authored, fitted, and validated in the **Vestige Formula Workbench**
(`/mnt/Games/Scripts/Linux/3D_Engine/tools/formula_workbench/`) and exported as a
GLSL snippet (with its safe-math prelude) committed under `shaders/formulas/`,
then compiled to SPIR-V. No runtime dependency on the tool — the generated GLSL
is what ships.

Curves sourced this way (existing Workbench templates in **bold**, requested
additions tracked in `/mnt/Games/Scripts/Linux/3D_Engine/DOOM_Ants_Feedback.md`):

- BRDF: **`fresnel_schlick`**, **`ggx_distribution`**, **`schlick_geometry`**.
- Sampling: GGX/VNDF sample + PDF, cosine-hemisphere PDF, MIS power-heuristic
  weight *(requested)*.
- Path control: Russian-roulette survival *(requested)*.
- Volumetric (DOOM-0011): **Beer-Lambert** absorption, Henyey-Greenstein phase
  (Vestige's Schlick-fit), **`caustic_depth_fade`**.
- Output: `srgb_to_linear`/`linear_to_srgb` *(requested)*, **`exposure_ev`**,
  **`pbr_neutral_tonemap`** *(the Stage-2 operator — DOOM-0009 §3)*;
  `aces_tonemap` *(superseded; only as an optional "filmic" toggle)*.
- Denoise (Stage 3): temporal-blend alpha, edge-stopping weights, adaptive
  sample-count curve *(requested)*.

The Workbench's reference-regression harness locks fitted coefficients so a later
refactor cannot silently drift the visual look. Improvement requests and
in-anger findings go in `DOOM_Ants_Feedback.md` (mirrors the Ants MCP feedback
convention); Workbench-maintainer roadmap annotations there are left intact.

## Performance plan (the 60 FPS road)

Performance is a first-class requirement — the 60 FPS floor (DOOM-0012) — reached
with measurement, not assumption. Levers, applied and profiled in Stage 3
(DOOM-0012):

- **1–2 samples/pixel/frame + temporal accumulation** (reproject via motion
  vectors) — the core real-time-PT trick.
- **ReSTIR DI** (then GI) for many-light direct lighting at low cost.
- **Spatio-temporal denoiser** (SVGF / A-SVGF) so low-spp output is clean.
- **Adaptive sampling / render-scale** under a frame-time budget; bound bounce
  depth with Russian roulette.
- **G-buffer primary visibility** (raster) to spend the ray budget on bounces.
- **Acceleration-structure discipline**: static BLAS per level, instance-transform
  updates for doors/lifts/things, refit not rebuild.

Each lever is gated behind measured frame-time on the reference GPU
(AMD RX 6600). The 60 FPS floor is its own roadmap item (DOOM-0012); this spec
lays the architecture so those levers slot in without rework.

## Frame path & UI compositing

Per DOOM-0026, the UI stays software-drawn into `screens[0]` and is composited
over the 3D image:

1. **At frame start** — before the `gamestate` switch (`d_main.c:238`) in
   `D_Display` — the 3D path clears `screens[0]` and a parallel **coverage
   mask** (see below). Guarded
   so it runs *only* under a 3D back-end; it must never touch the Classic/shared
   path (DOOM-0026 INV-1).
2. Existing per-state 2D drawing (status bar, automap, HUD, menu, pause, wipes)
   runs **unchanged** into `screens[0]`.
3. `RB_RenderPlayerView` → the path tracer produces the world image.
4. `RB_Present` → upload `screens[0]` + coverage, composite the UI **over** the
   world (covered UI pixels decode through `PLAYPAL` and win; uncovered pixels let
   the world show through), present the swapchain image.

**Coverage mask:** every palette index is a real colour, so "transparent" cannot
be a palette value. A parallel 1-byte coverage buffer (size of `screens[0]`) is
cleared to 0 at frame start and set to 1 wherever UI code writes. The stamp is
added at the low-level blit chokepoints in `v_video.c` (`V_DrawPatch`/`V_DrawBlock`
/span fills), active only under a 3D back-end (a global flag checked once per
blit, not per pixel) — exact mask, hot Classic path unaffected, view-window never
stamped so the world shows through. This is the one new engine-side mechanism;
it is isolated to `v_video.c` + the composite shader.

## Build system

`linuxdoom-1.10/Makefile`, additive:

- Compile `r_vulkan.cpp` (+ `.cpp` helpers) with `g++` (C++17); **link the final
  binary with `g++`** (libstdc++); add `-lvulkan`.
- Vendor VMA as `third_party/vk_mem_alloc.h` (GPUOpen, MIT — GPL-v2-distribution
  compatible; licence noted in the file). One `.cpp` defines `VMA_IMPLEMENTATION`.
- **Shaders:** GLSL under `shaders/` (incl. Workbench-generated `shaders/formulas/`);
  a Make rule runs `glslc` → `.spv`, then `bin2c`/`xxd -i` → `<name>.spv.h` byte
  arrays compiled in. Self-contained binary; no runtime shader files.
- The 3D back-end is a first-class part of the build (the Vulkan SDK is a build
  dependency, documented in the Makefile). A `make classic-only` escape hatch for
  a no-Vulkan build host is deferred (YAGNI).

`R_Init` and the Classic path are untouched; new `.cpp` units join `OBJS`.

## Components / affected files

| File | Change |
|---|---|
| `r_vulkan.cpp` (new) | `R_Vulkan` back-end: instance/device/swapchain, scene + AS build, material/palette upload, G-buffer + path-tracer kernels, sprite billboards, temporal/denoise, UI composite, present. *As implemented it exports the individual `extern "C"` entry points `r_backend.c` declares and wires into the tier tables (`RB_Vulkan_Available`, `RB_Vulkan_Init`, `RB_Vulkan_SetResolution`, `RB_Vulkan_RenderView`, `RB_Vulkan_SetOverlay`, `RB_Vulkan_Present`, `RB_Vulkan_Shutdown`, `RB_Vulkan_BuildLevel`), not a single `R_VulkanBackend()` accessor.* |
| `r_vk_*.cpp/.hpp` (new) | Single-purpose helpers (device, resources, accel-structure, pipelines, denoiser) as `r_vulkan.cpp` grows — keep each unit focused. |
| `shaders/*` (new) | GLSL: G-buffer, path-trace compute (ray-query), sprite, sky, denoise, UI composite; `shaders/formulas/*` = Workbench-exported BRDF/sampling/tonemap/denoise GLSL. |
| `third_party/vk_mem_alloc.h` (new) | Vendored VMA single header (MIT). |
| `r_backend.c` | Wire the Vulkan entry points into the `RB_RT3D`/`RB_RASTER3D` slots; call the per-level build hook. `RB_Init` clamp already exists. *Shipped — the slots are live, not placeholders.* |
| `i_video.c` | Add `I_GetWindow()` + `I_ShutdownGraphicsForVulkan()` (tear down SDL_Renderer/texture, recreate window `SDL_WINDOW_VULKAN`). Classic path otherwise unchanged. |
| `v_video.c` | Coverage-mask stamp at blit chokepoints, active only under a 3D back-end (global flag); Classic path unaffected. |
| `d_main.c` | Frame-start `screens[0]`+coverage clear in `D_Display`, guarded to 3D back-ends (before the `gamestate` switch). |
| `p_setup.c` | One call into the back-end's per-level scene build at the end of `P_SetupLevel` (no-op under Classic). |
| `Makefile` | C++ compile + link, `-lvulkan`, VMA, shader→SPIR-V→header rule, new objects. |

Not touched: all `r_*.c` software-renderer internals, all UI drawing logic
(only the coverage stamp is added), the Classic back-end behaviour.

## Invariants

- **INV-1** Classic output stays byte-identical (DOOM-0026 INV-1). The coverage
  stamp and frame-start clear are no-ops unless a 3D back-end is active. *Test:*
  per DOOM-0026 INV-1 — dump `screens[0]` to PNG at a fixed point in
  `-timedemo demo1` with `renderer 0`; the hash equals the pre-DOOM-0008 build.
- **INV-2** A single build runs on all three tiers (RT3D / Raster3D / Classic).
  *Test:* on the RT machine the menu offers 3D and `Available()→RB_RT3D`; forcing
  `RB_RASTER3D` still renders; on a software-only VM only Classic is offered and
  the engine runs.
- **INV-3** Path-traced output uses DOOM's palette as albedo — colours are
  `PLAYPAL`-derived, art and sprites unmodified. *Test:* sampled surface albedo
  (pre-lighting / unshadowed white-furnace) matches the texture's `PLAYPAL`
  colour; palette flashes tint the world.
- **INV-4** The UI (status bar, HUD, menu, automap, pause, wipes) appears
  identical over the 3D world as over Classic. *Test:* open menu/automap under 3D
  — UI-region pixels match Classic.
- **INV-5** Scene + AS build is O(segs+subsectors+things) at level load; per-frame
  cost is camera + instance-transform updates + trace, no full AS rebuild in a
  static scene. *Test:* no BLAS rebuild between frames standing still
  (timer/validation check).
- **INV-6** The path tracer is unbiased up to Russian-roulette/clamp: a converged
  accumulation matches a reference render of the same scene within a small
  relative-MSE tolerance (exact threshold owned by DOOM-0009 INV-6: ≤ 0.5%
  rel-MSE @ a brute-force reference, 4096-spp default, raise if noisy).
  *Test:* white-furnace / known-scene convergence check.
- **INV-7** No magic constants in path-tracer shaders — every tuning curve traces
  to a Workbench-exported `shaders/formulas/*` artifact. *Test:* grep the shaders;
  numeric tuning literals live only in generated formula files.
- **INV-8** Vulkan runs clean under `VK_LAYER_KHRONOS_validation` (no
  errors/leaks) across level-load + play + exit on the dev machine.

## Verification

- `make` builds + links with no new warnings (C engine + C++ back-end + embedded
  SPIR-V).
- Launch with bundled `wads/doom.wad`, default config → Classic, identical to
  today.
- Set `renderer` to the 3D mode → E1M1 renders as path-traced 3D: solid textured
  geometry, monsters/items as upright sprites, soft GI bounce, ray-traced
  shadows, status bar/HUD/menu/automap correct over the world.
- Walk the level: textures aligned, doors/lifts move (instance updates), animated
  flats cycle, palette flashes tint world + UI, firing throws dynamic light +
  shadows.
- Profile on the RX 6600: capture frame time; confirm the temporal/denoise/ReSTIR
  levers move it toward the 60 FPS floor (DOOM-0012 owns hitting it).
- Validation-layer clean over a play session (INV-8).
- A `tests/` conformance test for INV-1/INV-3 is desirable but gated on CI having
  a WAD; record as follow-up if CI can't supply one (dev WADs are local-only).

## Out of scope (YAGNI)

- **60 FPS** *attainment* and full quality auto-scaling — architecture here,
  hitting the floor is **DOOM-0012**.
- Full **dynamic lighting** beyond Stage-2's seed set (muzzle flash) — coloured
  lights, flicker/strobe specials as lights are **DOOM-0010**.
- **Volumetric** participating media / god-rays — **DOOM-0011** (the path tracer's
  medium integration + Beer-Lambert/phase curves land there).
- Ray-traced **reflections of sprites** beyond flat-card approximation; replacing
  sprites with 3D models/voxels (load-bearing DOOM look).
- **Free-look (mlook)** pitch — trivial later with a real 3D camera, but touches
  input/gameplay; default view matches vanilla.
- Seamless **mid-game** renderer hot-swap (DOOM-0026 defers this).
- A full PBR material-authoring system — DOOM art is paletted; a small
  name-keyed material table is enough.

## Staging summary

| Stage | Roadmap | Delivers | Ships when |
|---|---|---|---|
| 1 | DOOM-0008 | 3D meshes + materials + AS + sprites + UI composite; correct image via primary-ray/G-buffer; "Renderer: 3D" live | Scene renders, validation-clean, selectable |
| 2 | DOOM-0009 (+ DOOM-0010 seed) | Monte-Carlo path tracer: NEE direct lighting (pure-Lambert — no MIS/BRDF sampling), **baked** static GI (per-subsector SH-L1) + live direct/shadow rays, hardware ray-traced shadows; plus the DOOM-0010 *seed* light (muzzle flash) so dynamic shadows are visible | Path-traced image converges; muzzle-flash dynamic shadows |
| 3 | DOOM-0010 (full) / 0011 / 0012 | Full dynamic lighting (coloured/flicker/strobe), volumetrics, and the performance work (temporal/ReSTIR/denoise/adaptive) to the 60 FPS floor | Each its own spec, layered on the Stage-1/2 base |

## Cold-eyes loop log

**Loop 1 (2026-06-16)** — 2 lanes (spec-vs-cited-code accuracy; cross-doc
consistency), briefed cold. Verified + fixed:

- *Accuracy (MEDIUM ×2, LOW ×2):* the palette citation conflated the `palette[]`
  LUT build (`I_SetPalette`) with the per-pixel expansion (`I_FinishUpdate`) —
  split; `v_video.c:517-535` relabelled as `V_Init` *allocation* (not drawing);
  the frame-start clear given the `gamestate`-switch line (`d_main.c:238`);
  INV-1 test given DOOM-0026's screens[0]-dump capture method; INV-6 given a
  relative-MSE tolerance (threshold deferred to DOOM-0009).
- *Cross-doc conflict (CRITICAL ×1, HIGH ×2):* DOOM-0026 still framed the 3D
  back-end as "hybrid raster + ray-traced shadows/reflections, no GI", conflicting
  with the path-traced-GI goal — added a superseded-by-DOOM-0008 amendment banner
  (header + section) and softened its status to "Shipped"; ROADMAP DOOM-0009
  ("path tracing where feasible") and the DOOM-0026 Design line ("Vulkan-hybrid
  3D") reworded to the path-tracing direction; "hybrid" term collision noted.
- *Staging (MEDIUM):* the table now distinguishes the DOOM-0010 *seed* (Stage 2,
  muzzle flash) from full DOOM-0010 (Stage 3).
- *Unverified / dismissed:* none.

**Loop 2 (2026-06-16)** — same 2 lanes, cold. No CRITICAL; the two HIGH were
heading/word polish, the rest citation precision. Verified + fixed:

- *Accuracy:* `NUMCOLORMAPS 32` re-cited to `r_main.h:87` (was grouped under
  `r_data.c`); the `d_main.c:278` PLAYPAL anchor softened ("cached e.g. at");
  a `renderer` (config key) ↔ `rendermode` (engine var) note added.
- *Consistency:* retired the bold "Hybrid where it pays" lead-in → "Rasterised
  primary visibility (a performance lever)"; DOOM-0026's superseded section
  heading suffixed "(superseded — see DOOM-0008)"; `coding.md` reworded "being
  designed (not yet built)"; ADR's rejected "future quality *tier*" → "*level*"
  so "tier" means only hardware tiers; ROADMAP DOOM-0008 gained a `Design:` line
  with the Stage→ID mapping and flipped to 🚧.
- *Unverified / dismissed:* the Loop-5 (2026-06-15) DOOM-0026 log entry that
  "dropped GI" is now historically reversed but is a correctly-dated audit trail —
  left intact (house rule: don't back-fill loop logs).

Per user direction ("if the loop only returns basic polish items, continue"),
loop 2's polish was fixed and implementation proceeds rather than looping further.
