# ADR 0001 — 3D renderer: graphics API, language, and shading language

**Status:** Accepted (2026-06-15).

**Context:** Phase 2 of DOOM_Ants ("the spin", DOOM-0008..0012) evolves the
1997 software renderer toward true 3D with hardware ray tracing, dynamic and
volumetric lighting, and a 60 FPS floor. `docs/standards/coding.md` requires the
graphics-API, language, and shading-language decisions to be recorded — as a
design doc plus this ADR — *before* renderer code is written. The seam that
makes the renderer swappable (DOOM-0026) is being built now and must be shaped
by these decisions, so they are settled here. The design doc with full
architecture is `docs/specs/DOOM-0026-renderer-backend.md`.

## Decision

1. **Graphics API: Vulkan, hybrid raster + hardware ray tracing.** Rasterise
   geometry for primary visibility (fast, predictable), then use hardware ray
   tracing (`VK_KHR_acceleration_structure`, `VK_KHR_ray_tracing_pipeline` /
   `VK_KHR_ray_query`) for shadows and reflections.

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
- **Hybrid, not full path tracing:** the 60 FPS floor (DOOM-0012) is a hard
  requirement; rasterised primary visibility with ray-traced effects is the
  established way to hit interactive frame-rates while still being genuinely
  ray-traced. Full path tracing stays a possible future quality tier.
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
- **Full Vulkan path tracer** — most accurate, but the hardest way to hold
  60 FPS. Kept as a future optional quality tier, not the baseline.
- **Pure C Vulkan back-end** — one language/toolchain, but manual handle cleanup
  and no C++ helper ecosystem; more error-prone for an RT codebase. Rejected.
- **Slang / HLSL shaders** — viable and modern, but thinner ray-tracing tutorial
  coverage than GLSL today. Rejected for now; revisit with a new ADR if the
  shader codebase grows enough to want Slang's modules/generics.
