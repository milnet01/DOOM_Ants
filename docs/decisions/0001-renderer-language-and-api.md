# ADR 0001 — 3D renderer: graphics API, language, and shading language

**Status:** Accepted (2026-06-15); amended (2026-06-16) — path tracing is the
explicit goal, not a deferred tier (see Decision 1 and the amendment note). The
core decisions (Vulkan, C engine / C++ back-end, GLSL→SPIR-V) are unchanged.

**Context:** Phase 2 of DOOM_Ants ("the spin", DOOM-0008..0012) evolves the
1997 software renderer toward true 3D with **real-time hardware path tracing**,
dynamic and volumetric lighting, and a 60 FPS floor. The user's direction is
full path tracing (even where it demands iterative performance work), everything
converted to 3D, and extreme performance. `docs/standards/coding.md` requires the
graphics-API, language, and shading-language decisions to be recorded — as a
design doc plus this ADR — *before* renderer code is written. The seam that
makes the renderer swappable (DOOM-0026) is being built now and must be shaped
by these decisions, so they are settled here. The design doc with full
architecture is `docs/specs/DOOM-0026-renderer-backend.md`.

## Decision

1. **Graphics API: Vulkan, hardware-ray-traced path tracing.** A real-time
   Monte-Carlo path tracer over the 3D level geometry
   (`VK_KHR_acceleration_structure` + `VK_KHR_ray_query`, optionally
   `VK_KHR_ray_tracing_pipeline`) is the target image. Rasterising primary
   visibility into a G-buffer is retained as a **performance lever** (spend the
   ray budget on bounces/shadows/GI, not primary hits), not as a separate
   renderer — the hybrid is one knob in reaching real-time path tracing.

2. **Language: the engine stays C; the Vulkan back-end is C++.** The C/C++
   boundary is the plain-C `renderer_backend_t` interface — no C++ leaks into the
   engine.

3. **Shading language: GLSL, compiled ahead-of-time to SPIR-V** with
   `glslc`/glslang from the Vulkan SDK.

## Why

- **Vulkan, not OpenGL:** Vulkan is the only cross-vendor path to *hardware* ray
  tracing on Linux; OpenGL has none, and the roadmap (DOOM-0009) calls for
  hardware ray tracing explicitly. Verified on the dev machine (AMD Radeon
  RX 6600, RDNA2, RADV reports `accelerationStructure = true`).
- **Real-time path tracing, reached with the standard levers:** the 60 FPS floor
  (DOOM-0012) is a hard requirement, and modern real-time path tracing hits
  interactive frame-rates with 1–2 samples/pixel + temporal accumulation, ReSTIR
  light sampling, and a spatio-temporal denoiser. Rasterised primary visibility
  (G-buffer) is one of those levers, not a retreat from path tracing. Quality
  scales by hardware tier; a non-RT GPU falls back to a rasterised 3D approximation
  (`RB_RASTER3D`).
- **C++ for the back-end:** Vulkan's hardware-RT API is verbose and handle-heavy.
  C++ RAII (resources freed automatically at scope exit) and the C++-first helper
  libraries (VMA for GPU memory, `vulkan.hpp`) cut a large class of leak/lifetime
  bugs that hand-rolled C would invite. Keeping the engine in C preserves the
  "modern toolchain, original logic" principle for everything that isn't new.
- **GLSL for shaders:** widest ray-tracing example base, ships with the Vulkan
  SDK, no extra toolchain. Slang and HLSL were considered; GLSL won on
  documentation breadth for the ray-tracing shader stages specifically.

## Consequences

- The build gains a C++ compiler and a SPIR-V shader-compile step **only when the
  3D back-end (DOOM-0008) lands** — DOOM-0026 itself is pure C and adds neither.
- The `renderer_backend_t` interface must stay valid C with C linkage so the C
  engine can call the C++ back-end across the seam.
- A non-Vulkan or non-RT machine never touches this code: the back-end is chosen
  by auto-detected tiers (see the design doc), falling back to the Classic
  software renderer.

## Alternatives considered

- **OpenGL rasteriser, no hardware RT** — simpler and runs anywhere, but forgoes
  hardware ray tracing and would force amending DOOM-0009. Rejected.
- **Pure path tracer, no raster G-buffer** — cleanest, but spends the ray budget
  on primary hits a rasteriser does cheaper. The G-buffer hybrid is kept as a
  performance lever toward the same path-traced result, not as a different target.
- **Rasterise-only with ray-traced effects bolted on (no GI)** — easier to make
  fast, but forgoes the global-illumination/path-traced look that is now the
  explicit goal. Rejected as the target; it survives only as the `RB_RASTER3D`
  fallback tier for non-RT hardware.
- **Pure C Vulkan back-end** — one language/toolchain, but manual handle cleanup
  and no C++ helper ecosystem; more error-prone for an RT codebase. Rejected.
- **Slang / HLSL shaders** — viable and modern, but thinner ray-tracing tutorial
  coverage than GLSL today. Rejected for now; revisit with a new ADR if the
  shader codebase grows enough to want Slang's modules/generics.

## Amendment (2026-06-16)

The original (2026-06-15) ADR framed the design as "hybrid raster + ray-traced
effects, with full path tracing as a possible future quality level." The user has
since made the goal explicit: **real-time hardware path tracing is the target**
(global illumination, ray-traced shadows), everything truly 3D, and extreme
performance. Decision 1 and the Why/Alternatives are reworded accordingly —
rasterised primary visibility is now a *performance lever* toward real-time path
tracing, not an alternative to it.

What did **not** change: Vulkan as the API, the C-engine / C++-back-end split
across the plain-C `renderer_backend_t` seam, and GLSL→SPIR-V shaders. Those
decisions and their rationale stand. The path-tracer architecture and its staged
delivery live in `docs/specs/DOOM-0008-3d-renderer.md`. Per the no-magic-constants
rule, the path tracer's tuning curves (BRDF, importance-sampling PDFs, attenuation,
tonemap, denoiser weights) are authored and validated in the Vestige Formula
Workbench and exported as GLSL.
