# DOOM-0009 — Hardware path tracer (Stage 2)

**Status:** Draft (pre-`/cold-eyes`). Design contract for the Stage-2 renderer.
**Depends on:** DOOM-0008 (Stage 1 raster 3D: meshes, materials, sprites, UI
composite) shipped; DOOM-0026 renderer-backend seam; ADR
`docs/decisions/0001-renderer-language-and-api.md`.
**Feeds:** DOOM-0010/0011/0012 (Stage 3: dynamic lights, volumetrics, the 60 FPS
performance pass), DOOM-0042 (HD art theme), DOOM-0043/0044 (scene lights,
flashlight).
**Evidence base:** `docs/research/DOOM-0009-performance.md` (cited cost survey +
DOOM-native optimisations) and `docs/research/3d-renderer-approaches.md`. Binding
decisions live here; the research docs are the "why".

---

## 1. Goal

Light DOOM's existing 3D scene (the Stage-1 mesh) with a real hardware
**Monte-Carlo path tracer** — global illumination, ray-traced shadows, derived
emissive surfaces — so the world looks physically lit while still **feeling like
DOOM**. Stage 2 ships when a path-traced frame converges to a reference image
(white-furnace verified) and muzzle-flash dynamic shadows are visible, at a
playable frame rate on the reference GPU (AMD RX 6600 / RDNA2 / RADV).

Non-goals for Stage 2 (deferred to Stage 3): moving/coloured/flickering dynamic
lights beyond the muzzle flash (DOOM-0010), volumetrics (DOOM-0011), and the full
performance pass to a hard 60 FPS floor (DOOM-0012). Stage 2 must be *correct and
playable*; Stage 3 makes it *fast and rich*.

## 2. Settings model — ray tracing is a toggle, orthogonal to art

Per the 2026-06-25 direction decision, ray tracing is a **player setting**, not a
fixed property of a renderer tier, and it is **independent of the art set**:

- **Renderer:** Classic (1997 software) · 3D (Vulkan). Unchanged seam (DOOM-0026).
- **Ray Tracing: On / Off** — only meaningful in 3D. Maps onto the existing tiers:
  *Off* = `RB_RASTER3D` ("Solid", raster lighting), *On* = `RB_RT3D` ("Ultra",
  path-traced). Surfaced in Options as a clear "Ray Tracing" item; internally it
  still selects the Solid/Ultra back-end so the frozen `rendermode_t` enum and the
  tier auto-probe (DOOM-0008) are untouched. "Ray Tracing: On" is greyed out when
  the GPU lacks `VK_KHR_acceleration_structure` + `VK_KHR_ray_query`.
- **Art set: Classic / HD** — a separate theme toggle (DOOM-0042), **orthogonal**
  to RT. The path tracer consumes whatever material is bound; it never assumes a
  specific art set. So all four combinations are valid: {Classic-art, HD-art} ×
  {RT off, RT on}. Stage 2 ships with Classic-art only; HD-art arrives in
  DOOM-0042 with no path-tracer changes required (INV-9).

**INV-9 (art-set agnostic):** the integrator and lighting code reference only the
bindless material interface (albedo/roughness/metallic/emissive/normal samplers
indexed by material id), never the source art set. Swapping Classic↔HD art changes
only which textures the material table points at.

## 3. Architecture

Inline **`VK_KHR_ray_query` in a compute megakernel** (per ADR 0001 and the RADV
survey: ray-query is mature on RADV and matches the megakernel plan; the RT
pipeline path is a later option, not Stage 2). One dispatch over screen pixels.

- **Acceleration structure:** the static map = one **BLAS** built once with
  `PREFER_FAST_TRACE | ALLOW_COMPACTION` (compacted). Per-entity BLASes (none
  deform in classic DOOM — sprites are billboards) reused. A **TLAS rebuilt every
  frame** over a few hundred instances, updating only instance transforms for
  moving sectors (doors/lifts, via the DOOM-0049 plane-height data) and
  billboards. No BLAS refit. (`docs/research/DOOM-0009-performance.md` §2.5.)
- **Materials → bindless.** Migrate the Stage-1 single R8 atlas to a bindless
  array-of-textures (`VK_EXT_descriptor_indexing`, core 1.2): one image per
  texture/flat/sprite with native REPEAT wrap, indexed by material id from the hit
  shader. The R8-index + PLAYPAL-LUT decode, vertex plumbing, and staging upload
  carry over; only "1 atlas + manual UV-wrap" becomes "N images + native wrap".
  This is the seam DOOM-0042's HD PBR set plugs into (INV-9).
- **Memory:** VMA (the many image/buffer allocations of the RT work). RADV AS
  structures are fat (~137 B/tri) — trivial for one DOOM map, but budget VRAM for
  large external WADs and always compact.
- **Colour:** light in **linear** space (sRGB→linear after the PLAYPAL lookup,
  treat palette colour as albedo); do **not** bake COLORMAP light-diminishing into
  albedo (it double-darkens once GI runs). Tonemap with Khronos **PBR Neutral**
  (preserves DOOM's saturated palette). A "vanilla-tint" post pass that quantises
  the HDR result back through COLORMAP/PLAYPAL is an optional toggle, not the model.

## 4. Lighting model

DOOM's derived emission is ~95% **static** (sector-lightlevel glow, bright/lamp/
computer emissive textures, sky sun). Only the muzzle flash is dynamic in Stage 2.
So split static from dynamic (the central performance lever):

### 4.1 Static GI bake + dynamic delta
- **Bake** a converged static-GI solution once per level load (the same ray-query
  integrator, amortised/async on a worker queue) into a **sector-keyed irradiance
  cache** (§4.3). The steady-state (player not firing) frame is then a cache
  lookup + primary visibility — near-zero ray cost.
- **Dynamic delta:** when a muzzle flash (`player->extralight` / a live dynamic
  light) is active, path-trace its **ray-traced shadows** for the few active
  frames only (dynamic-light gating), composited over the baked static. Idle
  frames stay cheap; firing frames pay for the delta.

### 4.2 Emission model (from DOOM-0008 spec)
- Sector `lightlevel` seeds a surface emissive term (brighter sectors emit more).
- Known light/lamp/computer textures + bright sector specials become stronger
  emitters, sampled by next-event estimation (NEE). Per-texture emissive strength
  = mean linear luminance of the texture's palette colours above a threshold,
  precomputed once at atlas/material build (no per-frame cost).
- **Sky** is a bounded sky-light + miss-shader environment (NOT emissive geometry,
  which drowns the denoiser). A ray that hits a sky-flat surface is a miss into the
  sky environment. **HDRI option:** the sky environment may be sampled from an
  Outdoor HDRI (the user's asset library) for realistic outdoor sky light + a
  directional sun extracted from the HDRI's brightest region; falls back to a
  procedural sky tint + sun when no HDRI is selected. (Direction-agnostic: helps
  both art sets.)

### 4.3 DOOM-native optimisations (from the research doc)
- **REJECT-driven light selection:** use DOOM's `REJECT` sector-visibility lump to
  cull, per shading point, the emissive sectors that cannot be seen — an exact,
  tiny NEE candidate set instead of a CDF over all lights. Fall back to a shadow
  ray where REJECT is conservative.
- **Sector-keyed irradiance cache:** key the cache by `(subsector, height band)`
  rather than a uniform world hash grid. DOOM sectors are exactly the regions of
  piecewise-constant lighting bounded by walls, so a few probes per subsector
  capture the low-frequency irradiance with far fewer probes and **no thin-wall
  leak** (DDGI's failure mode). This *is* what the §4.1 bake fills.
- **Palette quantisation as a free denoiser:** when the vanilla-tint post pass is
  on, residual sub-quantum noise collapses to the same palette index, so the
  denoiser can target a looser convergence threshold.

### 4.4 Integrator
1 path/pixel + temporal accumulation + motion-vector reprojection · NEE + multiple
importance sampling (power heuristic) · Lambertian diffuse + cosine sampling (GGX/
VNDF specular gated to measured need — matte DOOM art rarely needs it) · Russian-
roulette termination + firefly clamp + NaN guards · **half-resolution indirect**
trace reconstructed inside the denoiser's à-trous wavelet (biggest ms lever, near-
free on matte art) · **A-SVGF** denoise (purpose-built for the muzzle-flash sudden-
light case; clean-room from the paper, Q2RTX as readable GPL reference) · **FSR2**
upscale last, decoupled, denoise-before-upscale, jitter-consistent. No ReSTIR in
Stage 2 (few lights; its reservoirs are RDNA2's worst register case).

## 5. Shading curves (Vestige Formula Workbench)

**INV-7 (no magic constants):** every numerical tuning curve in the path-tracer
shaders traces to a Workbench-exported `shaders/formulas/*.glsl` artifact (safe-
math NaN-guarded), compiled by `glslc` and committed; coefficient drift is
regression-locked by the Workbench harness. Curves to author there: GGX/VNDF
sample+PDF, cosine-hemisphere PDF, MIS power-heuristic weight, Russian-roulette
survival probability, A-SVGF temporal-blend α + edge-stopping weights, sRGB↔linear,
exposure/tonemap (PBR Neutral). The safe-math guards matter here specifically: one
NaN becomes a firefly the denoiser then smears. Workbench at
`/mnt/Games/Scripts/Linux/3D_Engine/`; requests tracked there as `3D_E-0006…0010`.

## 6. Invariants

- **INV-6 (unbiased):** the integrator is unbiased up to Russian-roulette/clamp;
  converged accumulation matches a brute-force reference within a small relative-
  MSE tolerance (threshold: **≤ 0.5% rel-MSE** on the white-furnace + a reference
  Cornell-style DOOM room, measured at 4096 spp accumulation).
- **INV-7 (no magic constants):** §5.
- **INV-8 (validation-clean):** zero Vulkan validation-layer errors over a
  multi-second run on every tier path (must be exercised on a box with the layers
  installed — flagged unmet in some DOOM-0008 runs).
- **INV-9 (art-set agnostic):** §2.
- **INV-10 (toggle parity):** switching Ray Tracing Off→On→Off mid-game leaves the
  scene correct each time (reuses the DOOM-0051 level-rebuild + screen-wipe path);
  RT Off (Solid) output is unchanged by any RT-only code.
- **INV-11 (graceful fallback):** on a non-RT GPU, "Ray Tracing: On" is
  unselectable and the engine stays on Solid/Classic; no crash, no half-state.

## 7. Build order (cheapest-first; each step independently verifiable)

1. **Bindless materials** — migrate atlas → array-of-textures; Solid output
   unchanged (INV-10). Verify: screenshot parity with the atlas path.
2. **BLAS + TLAS** on the static mesh; white-furnace + intersection test (INV-6/8).
3. **Direct lighting only:** NEE + MIS + RR + firefly clamp + NaN guards via ray-
   query, REJECT-culled light set. Reference-image regression (INV-6).
4. **Static GI bake** into the sector-keyed cache at level load (§4.1, §4.3).
5. **Dynamic delta:** muzzle-flash analytic light + ray-traced shadows, gated on
   activity, composited over the baked static. Verify: visible muzzle-flash shadow.
6. **Half-res indirect + A-SVGF** (§4.4), then **FSR2**. Verify: stable image, no
   ghosting on a muzzle flash.
7. **Perf pass** toward the 60 FPS floor; reassess a runtime hash cache + optional
   ReSTIR GI against measured noise (this tips into Stage 3 / DOOM-0012).

## 8. Performance budget (planning; measure on the RX 6600)

Target 1080p @ a playable rate, 60 FPS floor deferred to DOOM-0012. Must-measure-
before-committing (no published RADV numbers): A-SVGF ms @ 1080p; the leaned
cache/bake ms; AS build/refit ms; the per-level static-bake time. Lean the
megakernel hard (split shading out of traversal, wave32, opaque-only geometry +
separate alpha sprites, early-terminate shadow rays, one live RayQuery) — RDNA2
runs BVH traversal as shader code, so occupancy/registers dominate. Build on Mesa
≥ 25.2.

## 9. Open questions (resolve during `/cold-eyes` or early build)

- Bake storage: per-vertex irradiance vs per-subsector probe volume — pick by the
  measured bake size/quality on E1M1 + a large WAD.
- HDRI sun extraction: brightest-texel vs a fitted directional — defer to DOOM-0043
  (deliberate scene lights) if the procedural sun suffices for Stage 2.
- Whether the "Ray Tracing" toggle should also expose a quality sub-setting (spp /
  render-scale) in Stage 2 or wait for DOOM-0012.

---

## Cold-eyes loop log

*(to be filled as `/cold-eyes` runs — per global rule 14, looped until a pass
returns zero verified findings before any implementation.)*
