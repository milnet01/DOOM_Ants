# DOOM-0009 path tracer — performance & cost research

**Status:** Reference notes (not a spec or contract). Compiled 2026-06-25 from a
focused performance/cost web survey (2024–2026 sources) plus DOOM-specific
engineering ideas, to feed the DOOM-0009 design spec. Binding decisions live in
`docs/specs/DOOM-0009-path-tracer.md` once written; this is the evidence behind
them. Complements the broader survey in `3d-renderer-approaches.md` — that one
covers *what* to build; this one covers *how to make it cheap* on the target.

**Target (the constraints every recommendation is judged against):** AMD Radeon
RX 6600 (RDNA2) on RADV/Mesa Linux; classic DOOM levels (static map ≈ one BLAS,
few moving sectors, few dynamic lights); 8-bit paletted matte art; 1080p @ 60 FPS
floor; GPL-v2 binary (no NVIDIA RTX SDKs, no vendored Apache-2.0; MIT + GPL-v2
sources OK); inline `VK_KHR_ray_query` in a compute megakernel.

---

## 1. The headline: DOOM's lighting is *mostly static* — bake it, trace the delta

The single biggest cost lever is DOOM-specific and isn't in the generic survey:
**the entire derived emission model is static.** Sector-lightlevel glow,
bright/lamp/computer emissive textures, and the sky sun do not move during play.
The *only* dynamic light in Stage 2 is the muzzle flash (and later DOOM-0010
coloured/flicker lights). So:

> Precompute a converged static-GI solution once per level, then at runtime
> path-trace only the **dynamic delta** (muzzle-flash shadows + a cheap contact
> term), composited over the baked static irradiance.

Why this fits: in the common case (player not firing) the per-frame path-tracing
cost collapses to a cache/lightmap lookup — near-zero — and the 60 FPS floor is
trivially met. When the gun fires, only the few active frames pay for dynamic
shadow rays. This is the classic "static GI baked / dynamic GI traced" split, and
DOOM is close to the best-case scene for it (≈95% static lighting). It also keeps
the look stable and noise-free where it matters most (navigation), which reads as
"feels like DOOM."

Bake target options (decide in the spec): per-vertex irradiance on the level
mesh (cheap, low-frequency — matches DOOM's flat per-sector lighting), or a
per-sector irradiance volume (see §3). The bake itself runs the same ray-query
integrator at level load, amortised across many frames or async on a worker queue.

**Confidence:** HIGH on the strategy; the bake-vs-realtime-cache split is the
spec's central decision.

---

## 2. Survey findings (cited), ranked best-bang-per-millisecond for this target

Condensed from the 2026-06-25 survey. Every load-bearing number is from a primary
source; cross-vendor extrapolations to the RX 6600 are flagged "estimate".

1. **Half-res indirect GI trace, reconstructed inside the denoiser's à-trous
   wavelet** (not a separate bilateral upsample → avoids double-blur). Biggest ms
   lever; near-free quality on matte low-frequency art (the failure case is sharp
   gloss/water, which DOOM lacks). Start straight half-res, not checkerboard
   (checkerboard's reproject pass fights the temporal denoiser). [MED-HIGH]
2. **A-SVGF denoiser** (clean-room from the 2018 paper; Q2RTX's GPL-lineage code
   as readable reference). Purpose-built for the muzzle-flash failure mode: a
   per-pixel temporal factor driven by a temporal gradient (re-shade a sparse 3×3
   strata subset with the same RNG seed, diff to detect lighting change, drop
   stale history). Plain SVGF ghosts/lags on sudden lights. Budget ~2–3 ms @
   1080p on the 6600 and **measure** (no published RADV number). [HIGH]
3. **Lean inline ray-query compute megakernel + ruthless VGPR minimisation.** On
   RDNA2 only ray-box/triangle intersection is HW; **BVH traversal is compute**,
   so occupancy/registers dominate (a measured RT kernel hit 96 VGPRs → 10/16
   waves). Tricks (all verified): split material shading out of the traversal
   kernel ("Megakernels Considered Harmful", Laine 2013); wave32; 8×4=32
   threadgroups; one live `RayQuery`; **all geometry opaque, no any-hit** (handle
   alpha sprites/grates separately); early-terminate shadow rays; **skip software
   ray sorting** (arXiv 2506.11273, 2025: sort overhead wipes the 1.3–2.0× trace
   gain); build on **Mesa ≥25.2 / 26.0** (BVH-builder + codegen wins reach RDNA2;
   the `ds_bvh_stack_*` traversal speedups are RDNA3/4-only). [HIGH]
4. **World-space hash radiance cache** (SHARC *algorithm*, clean-room; AMD's
   **MIT** GI-1.0 / Capsaicin as the reference implementation) to replace bounce
   rays with a lookup — *if* §1's static bake isn't sufficient and a runtime cache
   is wanted for dynamic indirect. AMD GI-1.0: 3.5 ms (Kitchen)/4.2 ms (Sponza) @
   1080p on a 6900 XT at ¼ spp; scaled to the 6600 (~28–30% throughput) ≈10–14 ms
   for *full* GI-1.0 (estimate) → run it **leaner** (fewer probes, DOOM's tiny
   light set), don't ship full GI-1.0. SHARC code is NVIDIA-RTX-licensed (study
   only); the algorithm is published/reimplementable. [HIGH fit, MED exact ms]
5. **Static BLAS (`PREFER_FAST_TRACE` + `ALLOW_COMPACTION`, built once) +
   per-frame TLAS rebuild, transform-only instance updates.** Nothing in classic
   DOOM deforms (sprites are billboards), so moving doors/lifts/monsters need only
   a new instance transform — **no BLAS refit**. A TLAS over a few hundred
   instances is cheap; rebuild beats refit-quality-decay. RADV caveat: AS structs
   are fatter on RADV (~137 B/tri vs ~45 on NVIDIA) — trivial for one map, budget
   VRAM for large external WADs; always compact. No primary RDNA2/RADV build-ms
   figure — measure with RGP/RRA. [HIGH]
6. **FSR2 upscale last, decoupled from the denoiser, denoise-before-upscale,
   jitter-consistent.** Real frames, low latency, MIT, Vulkan-native, AMD-first;
   reuses the motion vectors the denoiser already needs. [HIGH]
7. **Plain NEE + MIS (power heuristic) + Russian roulette + firefly clamp for
   direct lighting** — already chosen; **no ReSTIR needed** for a few-light scene
   (ReSTIR's register-heavy reservoirs are RDNA2's worst case, and its benefit
   scales with light count DOOM doesn't have: ~8.9 ms @ 1080p/1spp on a 3090). [HIGH]
8. **Defer:** ReSTIR GI as an optional second-bounce knob (measure register cost
   first); variance-driven extra-ray budget steered by A-SVGF's temporal gradient
   for 1–2 frames *after* a muzzle flash (→ its own roadmap item); FSR3 frame-gen
   as an **off-by-default** smoothness toggle only (adds +10–20 ms latency,
   RDNA2 has no Anti-Lag 2 offset, path-traced input is the adversarial case for
   optical flow — pin FSR source to **v1.1.4 MIT**, the current `main` is a
   restrictive non-GPL-compatible license). [MED]
9. **Avoid (proprietary / wrong tool / no payoff):** NRD/ReBLUR/ReLAX, RTXGI,
   SHARC *code*, RTXDI, DLSS (NVIDIA-RTX-licensed → GPL-incompatible, study only);
   FidelityFX Denoiser as the *main* denoiser (shadow/gloss only, not diffuse GI);
   DDGI (thin-wall light leaks — DOOM is all thin walls); Brixelizer GI (SDF loses
   ~1-unit walls, built for dynamic worlds); software ray sorting; HIP-RT /
   RadeonRays (wrong API surface, loses RADV in-driver traversal).

**Must-measure-on-the-6600 before committing:** (a) A-SVGF ms @ 1080p on RADV;
(b) leaned cache/GI ms (the 6900 XT→6600 scaling is an estimate); (c) AS
build/refit ms on RDNA2/RADV; (d) the static-bake time per level. Each is a
"microbench before the design hardens" item.

---

## 3. My own DOOM-native optimisation ideas (beyond the survey)

These exploit DOOM's *specific* data structures — they're cheaper than the
generic techniques because they use information the engine already has.

1. **REJECT-driven light selection (exact, not stochastic).** DOOM ships a
   `REJECT` lump: a precomputed sector-to-sector visibility bitmatrix (used by
   monster sight). For NEE, a shading point in sector *S* can skip every emissive
   sector the REJECT table says is invisible from *S* — turning a CDF over all
   lights into an exact, tiny candidate set per surface. Cheaper *and* less noisy
   than uniform/power light sampling, and free (the lump already exists, or is
   buildable). Where REJECT is conservative, fall back to a shadow ray.

2. **Sector-keyed irradiance cache (DOOM-native DDGI without the leaks).** Instead
   of a uniform world hash grid, key the radiance cache by `(subsector, height
   band)`. DOOM's sectors are exactly the regions of piecewise-constant lighting,
   bounded by walls — so a few probes per subsector capture the low-frequency
   irradiance with far fewer probes than a uniform grid, and **cannot leak through
   walls** (probes never cross a sector boundary, unlike DDGI's grid cells). This
   is the cheap, correct middle ground between DDGI (leaks) and a full hash cache
   (more probes than DOOM needs). Pairs naturally with §1's static bake: the bake
   *is* filling this cache.

3. **Dynamic-light gating (idle scenes are ~free).** Spawn the dynamic-shadow ray
   work and any per-frame TLAS-light updates only while a muzzle flash / dynamic
   light is actually active (`player->extralight` or a live dynamic-light list).
   The common non-firing frame does a baked lookup + primary visibility only.
   This is what lets a static scene sit comfortably above 60 FPS with headroom for
   the firing frames.

4. **Palette quantisation as a free denoiser.** The "vanilla look" post pass
   quantises HDR radiance back through COLORMAP/PLAYPAL (32 light levels × 256
   colours). That quantisation *hides residual noise*: sub-quantum variance
   collapses to the same palette index. So the denoiser can target a looser
   convergence threshold when the vanilla-tint toggle is on — fewer rays for the
   same *perceived* result. (Only applies with the palette post pass active; the
   true-HDR toggle still needs full convergence.)

5. **2D BSP short-circuit for horizontal shadow rays (later, gate on need).** Many
   shadow rays to sector-glow lights are near-horizontal within a sector; DOOM's
   BSP + segs answer 2D line-of-sight in log time (`P_CheckSight`). For
   same-height/coplanar occlusion a 2D BSP test could skip a full 3D BVH
   traversal. Likely over-engineering for Stage 2 — record as a measured-need
   optimisation, not a baseline.

6. **Emissive strength from the palette, precomputed once.** Per-texture emissive
   weight = mean linear luminance of its palette colours above a threshold,
   computed at atlas-build time (we already scan PLAYPAL there). No per-frame cost;
   feeds both the REJECT light set (§3.1) and the material sidecar.

The strongest three — **static bake (§1)**, **sector-keyed cache (§3.2)**, and
**REJECT light culling (§3.1)** — together make the steady-state frame cost a
lookup, with rays spent only on the dynamic delta. Recommend the spec build
around them rather than around a generic per-pixel full-GI path tracer.

---

## 4. Vestige Formula Workbench integration (the shading-curve mechanism)

Per global rule and the DOOM-0008 spec (INV-7: *no magic constants in path-tracer
shaders — every tuning curve traces to a Workbench-exported artifact*), all
numerical shading curves route through the Vestige Formula Workbench at
`/mnt/Games/Scripts/Linux/3D_Engine/`:

- **What it is:** an authoring/fitting tool (`tools/formula_workbench/`) that fits
  numerical formulas and **exports GLSL** snippets (with a safe-math NaN-guard
  prelude), plus optional C++ and `.vlut` LUTs. Build-time only — zero runtime
  dependency; the generated `.glsl` artifacts are what ship.
- **Flow:** author/fit curve → `formula_workbench --export-glsl <library.json>
  --out shaders/formulas/` → commit the generated `shaders/formulas/*.glsl` →
  `glslc` compiles them alongside the path-tracer compute shader → SPIR-V embeds
  in the binary. Each export carries R²/RMSE + library-hash provenance, and the
  Workbench reference harness regression-locks coefficient drift (INV-7 gate).
- **Curves to author there for DOOM-0009:** GGX/VNDF sample + PDF, cosine-
  hemisphere PDF, MIS power-heuristic weight, Russian-roulette survival
  probability, A-SVGF temporal-blend α and edge-stopping weights, sRGB↔linear
  decode for paletted albedo, exposure/tonemap (Khronos PBR Neutral). Existing
  Workbench templates already cover `fresnel_schlick`, `ggx_distribution`,
  `schlick_geometry`, `beer_lambert`, `aces_tonemap`, `exposure_ev`.
- **Why it matters here specifically:** the safe-math guards are not cosmetic — in
  a path tracer one NaN becomes a firefly or a black pixel that the temporal
  denoiser then smears across frames. Domain-guarded `atan/asin/sqrt` at the curve
  boundary is cheap insurance.
- New curve requests for DOOM-0009 are tracked in the Vestige roadmap
  (`3D_E-0006`…`3D_E-0010`) via `/mnt/Games/Scripts/Linux/3D_Engine/DOOM_Ants_Feedback.md`.

---

## 5. Proposed Stage-2 build order (cheapest-first)

1. **Migrate atlas → bindless** materials (`VK_EXT_descriptor_indexing`, core 1.2)
   — RT hit shaders index a global texture table; carries over the R8-index + LUT
   decode, vertex plumbing, staging upload.
2. **Static BLAS + TLAS** (§2.5); white-furnace test the AS + intersection.
3. **Direct lighting only:** NEE + MIS + RR + firefly clamp + NaN guards via
   ray-query, REJECT-culled light set (§3.1). Reference-image regression.
4. **Static GI bake** into the sector-keyed cache (§1, §3.2) at level load.
5. **Dynamic delta:** muzzle-flash analytic light + ray-traced dynamic shadows,
   gated on activity (§3.3), composited over the baked static.
6. **Half-res indirect + A-SVGF** (§2.1, §2.2); then **FSR2** (§2.6).
7. **Perf pass to the 60 FPS floor**; reassess the runtime hash cache (§2.4) and
   optional ReSTIR GI / post-flash ray boost (§2.8) against measured noise.

### Key sources
- AMD GI-1.0 (MIT reference, world cache) <https://gpuopen.com/download/GPUOpen2022_GI1_0.pdf> · arXiv <https://arxiv.org/pdf/2310.19855>
- A-SVGF (adaptive temporal filtering) <https://cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering/adaptive_temporal_filtering.pdf>
- Q2RTX path tracer (GPL-lineage reference) <https://developer.nvidia.com/blog/path-tracing-quake-ii/>
- SHARC integration (algorithm, code is RTX-licensed) <https://github.com/NVIDIA-RTX/SHARC/blob/main/docs/Integration.md>
- RADV ray tracing on <https://pixelcluster.github.io/RADV-Raytracing-ON/> · Mesa 26 <https://pixelcluster.github.io/Mesa-26/>
- RDNA2 RT throughput <https://chipsandcheese.com/p/raytracing-on-amds-rdna-2-3-and-nvidias-turing-and-pascal>
- Ray reordering doesn't pay off (2025) <https://arxiv.org/html/2506.11273v1>
- GPUOpen occupancy <https://gpuopen.com/learn/occupancy-explained/>
- Measuring acceleration structures <https://zeux.io/2025/03/31/measuring-acceleration-structures/>
- FSR frame generation <https://gpuopen.com/amd-fsr-framegeneration/> (pin source to v1.1.4 MIT)
