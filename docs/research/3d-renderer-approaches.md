# 3D / path-traced renderer — research survey

**Status:** Reference notes (not a spec or contract). Compiled 2026-06-24 from a
four-lane web survey to inform Phase 2 (DOOM-0008 … DOOM-0012, plus DOOM-0042/
0043/0044). When a recommendation here becomes a commitment it graduates into
the relevant spec (`docs/specs/DOOM-0008-3d-renderer.md`) or ADR
(`docs/decisions/0001-renderer-language-and-api.md`), which is where the binding
decisions live.

The four lanes: (1) how other DOOM/Quake source ports and id-tech remakes built
their GPU/3D/RT renderers; (2) real-time path-tracing technique; (3) 2.5D→3D
geometry + paletted materials; (4) Vulkan standards, libraries, tooling.

---

## The north star: Quake II RTX / Q2VKPT

The single closest precedent — a 1997 paletted id game turned into a shipping
real-time Vulkan path tracer, and **GPL-v2 like us, so we can read (and borrow
from) its source**.

- **Pipeline** (from Q2RTX's own `path_tracer.h`): primary rays (ray-traced, not
  rasterised, so per-pixel TAA jitter + motion vectors are free) → reflect/refract
  → direct lighting → **a single indirect bounce** (run up to twice). Budget
  **~4 rays/pixel** (direct hit + indirect hit + a shadow ray to one randomly
  chosen light each).
- **Denoise does the heavy lifting:** A-SVGF reconstructs a stable 1080p image
  from ~1 path/pixel in ~10 ms. This — not more samples — is how it hits 60 FPS.
- **Don't path-trace every emitter.** Bright surfaces (lamps, lava, exit signs,
  sky windows) are flagged as **analytic polygonal/sphere lights** via a per-
  texture material table; a PVS-limited subset is sampled per frame. This is the
  line between "noisy" and "shippable."
- **Paletted art → PBR via a `materials.csv` sidecar** (no map edits): per-texture
  Kind (chrome/glass/water/lava/sky/screen), a Light flag, an sRGB "correct
  albedo" flag, roughness/metallic/emissive channels. Our starting art is the
  same vintage of 8-bit paletted assets — this pattern transfers directly, and
  is the natural backbone for the DOOM-0042 HD art set.

DOOM-specific RT precedents confirming this runs on **AMD hardware ray query**
(not NV-only): **RTGL1** (Sultim Tsyrendashiev — a port library: KHR ray tracing
+ A-SVGF + ReSTIR), and its uses **Doom: Ray Traced** (PrBoom) and **gzdoom-rt /
"Doom II: Ray Traced"** (which replaces flat sprites with **voxels** to avoid
billboard artefacts in a 3D world).

---

## Geometry: 2.5D → 3D meshes

- **Upload the whole map once as static GPU geometry; animate moving sectors via a
  plane-height buffer, never regenerating geometry** (Doomsday's model). This maps
  1:1 onto ray tracing: **static map = one big BLAS; moving sectors (doors/lifts)
  = a small per-sector BLAS refit per frame; everything in a TLAS** rebuilt each
  frame. Group static geometry into few BLASes (Wolfenstein: Youngblood got a 60%
  BLAS-count saving this way).
- **Triangulate sectors directly:** BSP subsectors are **convex by construction**,
  so a triangle fan is free; keep an ear-clipping fallback (`mapbox::earcut`,
  MIT) only for degenerate/hole subsectors. This is exactly GZDoom's
  `hw_vertexbuilder` approach.
- **Source geometry from freshly-built GL nodes**, not raw vanilla nodes — GPU/RT
  renderers need the tighter convexity/precision, and GL segs are pre-split at
  partitions which kills most **T-junctions** (a T-junction that a software
  renderer tolerates becomes a visible crack / **light leak** under ray tracing).
- **Watertightness is the make-or-break issue** (Woop/Benthin/Wald 2013): weld
  every shared vertex to one bit-exact position by indexing DOOM's VERTEXES once;
  make wall-bottom/floor-edge vertices identical.
- **Sky** = a bounded sky-light + miss-shader environment, **not** real geometry
  (and not a giant emissive skybox, which drowns the denoiser). Flag sky surfaces;
  a ray that hits one is treated as a miss into the sky environment.
- **Masked/translucent two-sided middles + sprites** = non-opaque geometry, alpha-
  tested in the any-hit shader (`ignoreIntersectionEXT` on transparent texels);
  keep opaque walls flagged OPAQUE so they skip any-hit entirely.
- **Expect a clean 3D conversion to expose vanilla's tolerated render hacks**
  (missing upper/lower textures, self-referencing sectors) as visible holes —
  GZDoom carries dedicated `hw_renderhacks` code for exactly this. Vanilla
  `doom1.wad`/`doom.wad` have **no** Boom 3D-floors/deep-water, so sector-over-
  sector can be deferred entirely for now.

---

## Materials & the paletted look

- **Keep art paletted on the GPU: R8 palette-index images + a PLAYPAL LUT, decode
  in-shader** (done in the DOOM-0008 materials slice). Preserves DOOM's exact
  colours and makes the damage-red / pickup / radsuit **palette flashes a single
  LUT-row swap** (DOOM-0043-adjacent). Nearest-sample indices and **disable
  filtering/mipmaps on index textures** — interpolating palette indices is
  meaningless (DSDA-Doom's "Indexed Lightmode" learned this).
- **Atlas vs bindless — a flagged decision.** The current slice uses one **atlas**
  (simple, no descriptor-indexing plumbing, works for the raster pass). For the
  **ray-traced hit shaders**, the consensus is **bindless array-of-textures**
  (`VK_EXT_descriptor_indexing`, core in Vulkan 1.2): one image per texture with
  native REPEAT wrap, indexed by material id — RT hit shaders have no per-draw
  descriptor and must index a global texture table. **Recommendation: migrate
  atlas → bindless when the path tracer lands (DOOM-0009).** All the surrounding
  work (R8-index + LUT decode, vertex plumbing, staging upload, descriptor set)
  carries over; only "1 atlas + manual UV-wrap" becomes "N images + native wrap".
- **Light in linear space; treat PLAYPAL colour as albedo.** DOOM's palette is
  sRGB display values — convert sRGB→linear after the LUT lookup, light in linear,
  sRGB-encode at output. **Do NOT bake COLORMAP light-diminishing (maps 0–31) into
  albedo** — that double-darkens once GI runs. Reserve COLORMAP for the
  invulnerability map (32).
- **"Vanilla look" as a final pass, not the model:** compute lighting physically
  (HDR radiance), then optionally quantise through COLORMAP/PLAYPAL as a post pass
  — one renderer, a toggle between "true HDR GI" and "vanilla-tinted." (DSDA's
  Indexed approach, lifted to a post-process.)
- **Tonemap:** Khronos **PBR Neutral** (13 lines, invertible) preserves DOOM's
  saturated palette better than ACES/AgX, which wash out well-exposed sRGB content.
  Offer ACES only as an optional "filmic" toggle.

---

## Real-time path-tracing technique — adopt / later / skip

**Adopt now (the baseline pipeline):** 1 spp + temporal accumulation + motion-
vector reprojection · next-event estimation (NEE) · multiple importance sampling
(power heuristic) · trivial light selection (one level = few lights; uniform/power
CDF, no hierarchy) · Lambertian diffuse + cosine sampling · Russian-roulette
termination + firefly clamp + NaN guards · **SVGF** denoise · static BLAS (build
once, `PREFER_FAST_TRACE`, compacted) · TLAS instance transforms for doors/lifts/
sprites · `VK_KHR_ray_query` inline in a compute megakernel.

**Adopt soon:** **A-SVGF** (gradient-adaptive — kills SVGF ghosting/lag on light
changes; what Q2RTX shipped) · **FSR2** temporal upscaling (MIT, Vulkan-native,
AMD-first — a key 60 FPS lever; reuses the motion vectors SVGF already needs).

**Later (gate on measured need):** GGX/VNDF specular (only if surfaces get glossy —
matte DOOM art rarely needs it) · **ReSTIR GI** (the highest-upside GI upgrade at
1 spp, but heavy — ship the SVGF baseline and measure first) · BLAS refit (DOOM has
almost no deforming geometry) · FSR3 frame-gen.

**Skip:** ReSTIR DI / ReGIR (scale to millions of lights we don't have) · DLSS
(NVIDIA-only — abstract the upscaler so it *could* slot in, but build to FSR2).

**RADV / RX 6600 specifics:** RDNA2 accelerates only ray-vs-node *intersection* —
**BVH traversal runs as compute-shader code**, so the inner loop is shader-bound.
**Register pressure → occupancy is the #1 bottleneck**; keep the megakernel lean
(this is the strongest argument against loading ReSTIR/ReGIR in early). **Ray
query is faster than the RT pipeline on RADV today** and matches the "compute
megakernel" plan. Keep **Mesa current** — RADV RT gets free perf each release.

---

## Tooling & standards

- **VMA** (Vulkan Memory Allocator, MIT) — the de-facto memory allocator; GPL-v2
  clean. **Adopt** (arrives with the many image/buffer allocations of the RT work).
- **GLSL → SPIR-V via glslc, AOT-compiled and embedded** (already in the Makefile)
  — keeps the self-contained binary; permissive toolchain. **Keep.**
- **Slang** (Apache-2.0) — rising shading language with autodiff (would suit the
  Vestige Formula Workbench fitting curves). Use only as an **external build tool**
  (compile to SPIR-V), never vendored — Apache-2.0 is FSF-incompatible with GPL-v2.
- **Dev loop:** Vulkan validation layers + `vkconfig`, RenderDoc, **AMD Radeon GPU
  Profiler** (the RADV profiler). **White-furnace test** + reference-image
  regression for the integrator.
- **Renderer seam:** GZDoom's API-agnostic `FRenderState` let GL+Vulkan share scene
  code, but the GL-shaped abstraction throttled Vulkan and forced the VKDoom fork.
  Our back-end is greenfield C++ Vulkan — **build the command-buffer/mesh model
  Vulkan/RT-first** (we already have the DOOM-0026 seam at the right altitude).
  **vkDOOM3** is a clean Vulkan-engineering reference.

### License red flags (must NOT ship inside the GPL-v2 binary)
- **NVIDIA RTXGI / RTXDI** — proprietary EULA with an explicit anti-open-source
  clause. Reimplement the *published* algorithms (DDGI, ReSTIR) instead; don't
  vendor the SDK.
- **Apache-2.0 code** (Slang compiler, nvpro-samples, Khronos Vulkan-Samples,
  vk_raytracing_tutorial_KHR) — FSF treats plain Apache-2.0 as GPL-v2-incompatible
  (patent clause). Safe to **study** and to **use as build tools**; not safe to
  vendor source. Their SPIR-V *output* is fine to ship.
- **MIT** (VMA, AMD FidelityFX/FSR, earcut) — clean; just retain the licence text.
- **GPL-v2** (Quake II RTX) — same licence as us; study deeply, code is borrowable.

---

## Suggested build order (Phase 2)

1. **DOOM-0008 (current):** static map → 3D mesh, per-texel paletted materials
   (done), then sprites + UI composite. Migrate atlas → **bindless** here or at the
   DOOM-0009 boundary.
2. **DOOM-0009:** primary-ray G-buffer + 1-path NEE+MIS+RR+clamp on a static BLAS
   via ray query → a noisy-but-correct frame (white-furnace verified) → temporal
   accumulation → **SVGF** → **A-SVGF** → muzzle-flash dynamic shadow.
3. **DOOM-0010/0043/0044:** analytic-light extraction from bright textures + a
   material sidecar; deliberate scene lights + ambient floor (DOOM-0043); player
   flashlight as a camera-mounted analytic light (DOOM-0044).
4. **DOOM-0011/0012:** volumetrics; then the perf pass — **FSR2**, render-scale,
   bounce/sample budgets — to the 60 FPS floor. Reassess **ReSTIR GI** against
   measured GI noise.
5. **DOOM-0042:** the HD / sci-fi-horror art set as a selectable "theme" over the
   bindless material pipeline + the `materials.csv` sidecar — curated CC0/free
   packs, level layout unchanged.

### Key sources to keep open
- Q2VKPT write-up <http://brechpunkt.de/q2vkpt/> · Q2RTX `path_tracer.h`
  <https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/path_tracer.h>
- SVGF <https://research.nvidia.com/labs/rtr/publication/schied2017spatiotemporal/>
- RTGL1 (AMD-capable RT lib) <https://github.com/sultim-t/RayTracedGL1>
- GZDoom geometry <https://github.com/ZDoom/gzdoom/blob/master/src/rendering/hwrenderer/hw_vertexbuilder.cpp>
- Doomsday GPU-first geometry <https://blog.dengine.net/2018/06/further-rendering-explorations-part-1/>
- DSDA Indexed Lightmode <https://github.com/kraflab/dsda-doom/pull/97>
- Q2RTX material pipeline for old art <https://steamcommunity.com/sharedfiles/filedetails/?id=1921024536>
- Khronos Vulkan ray-tracing best practices <https://www.khronos.org/blog/vulkan-ray-tracing-best-practices-for-hybrid-rendering>
- VMA <https://gpuopen.com/vulkan-memory-allocator/> · FSR2 <https://gpuopen.com/fidelityfx-superresolution-2/>
