# DOOM-0009 — RT / denoiser / upscaler best practices (verified research, 2026-06-28)

Scoped research pass for the Ultra path tracer's actual stack: **RDNA2 (RX 6600,
8 GB)**, **Vulkan `VK_KHR_ray_query`** (inline ray queries in a compute megakernel,
no RT pipelines/SBT), a clean-room **SVGF/A-SVGF** denoiser, a **half-resolution**
direct-lighting trace, a **static SH-L1 GI bake**, **NEE + power-importance + MIS**,
and the new custom **TAAU** upscaler (Halton jitter + motion vectors +
neighbourhood-clamp) that **FSR 2 / FSR 3.1** will later plug into.

**Method:** deep-research harness — 5 search angles → 23 sources fetched → 112 claims
extracted → 25 top claims put through 3-vote adversarial verification (need 2/3
refutes to kill). **22 confirmed, 3 killed.** Every finding below carries its vote and
primary citation. Sources favour AMD GPUOpen, Khronos/Vulkan docs, the SVGF/A-SVGF
papers, the FidelityFX SDK manuals, and the NVIDIA NRD README.

> Two axes came back with **no surviving verified claims** — lighting efficiency
> (ReSTIR/DDGI/MIS) and security/robustness. They are **coverage gaps**, called out
> in §4–§5 and tracked as their own research items, NOT "nothing to do".

---

## 1. Performance on RDNA2 (megakernel + acceleration structures)

**1a. The inline ray-query compute megakernel on the compute queue is AMD's
recommended fast path — but occupancy/VGPR pressure is the binding constraint.**
[high · 3-0 / 2-1]
AMD GPUOpen RDNA guide: *"The best performance comes from using it [DXR 1.1 inline
ray query] in compute shaders, on a compute queue"* and *"Always have just 1 active
RayQuery object in scope."* RDNA2 has **16 wavefront slots/SIMD** and **~1024
VGPRs/SIMD** (128 KB file), so a single high-VGPR hot-spot drops the whole kernel
below 16 wavefronts; at 1/16 occupancy a stall cannot be hidden at all (Laine et al.
2013: *"even a single hot spot that uses many registers will drive the resource usage
of the entire kernel up"*). **The RX 6600's ~1024 VGPRs make this penalty STRONGER
than the RDNA3 (1536-VGPR) examples some sources cite.**
→ Levers: keep exactly one live `rayQueryEXT`; demote rarely-used hot paths out of the
megakernel's max-VGPR footprint; profile occupancy (RGP) before micro-optimising.
Sources: gpuopen.com/learn/rdna-performance-guide, gpuopen.com/learn/occupancy-explained,
NVIDIA Megakernels-Considered-Harmful (Laine 2013).

**1b. Acceleration structures: refit/rebuild only what changed; rebuild the small
TLAS every frame on the compute queue; transform-only door/lift moves qualify for
cheap updates — but active↔inactive primitive transitions force a full rebuild.**
[high · 3-0 / 2-1]
AMD: *"Don't rebuild dynamic geometry every frame and only rebuild or refit the LODs
you need"*, *"Rebuild your TLAS every frame on the compute queue if possible."* An
in-place TLAS update (`..._MODE_UPDATE_KHR`, needs `ALLOW_UPDATE`) is *"more efficient
when only minor changes (like transforms) have occurred"* — **DOOM's doors/lifts are
transform-only, so they qualify** (validates the current refit path). Hard limit
(Khronos + Vulkan spec): *"transitioning active primitives … to an inactive state … is
prohibited through an update. For this a full acceleration structure rebuild is
required."* BLAS **compaction** (`ALLOW_COMPACTION` → query size → compact) typically
saves **20–50 % AS memory**. Counterpoint (Intel): a budgeted full per-frame TLAS
rebuild can be more frame-time-*stable* than relying on updates.
Sources: gpuopen RDNA guide, khronos.org/blog/vulkan-ray-tracing-best-practices,
docs.vulkan.org (TLAS animation + AS spec).

**Validated, do NOT chase (refuted):**
- *"Wavefront/multi-kernel beats a single megakernel even in simple scenes"* — **killed
  0-3 / 1-2.** The megakernel was a 2013 multi-material result; DOOM's low-divergence
  matte art weakens it. **The current megakernel choice is defensible.** (Caveat: the
  endorsement is against an *unbounded* megakernel — keep it bounded.)
- *"Fewer BLAS is always better for static geometry"* — **killed 1-2** (contested; a
  separate source did report a 60 % BLAS-count saving from grouping in Wolfenstein, so
  BLAS grouping is a real but scene-specific lever, not a universal rule).

## 2. Denoiser (SVGF / A-SVGF) — the current design is on canonical footing

**2a. SVGF's three stages (temporal accumulation → spatiotemporal luminance variance →
variance-driven à-trous edge-stopping) with albedo demodulate-before / remodulate-after
is the correct baseline; ~10 ms @ 1080p / 1 spp on 2017 HW.** [high · 3-0]
Schied et al. 2017, verbatim on demodulation: *"We first demodulate surface albedo …
we filter untextured illumination components and reapply texturing after
reconstruction."* This directly validates the engine's demodulated `gillum` →
à-trous → re-modulate-in-composite pipeline.
Source: cg.ivd.kit.edu/publications/2017/svgf.

**2b. A-SVGF's per-pixel adaptive temporal alpha (gradient-driven) is the right tool
for the half-res trace's ghosting** [high · 3-0]
Schied et al. 2018: adaptive α gives *"fast response … in case of sudden changes while
using more aggressive temporal filtering … where the signal is constant."* Mechanism:
per 3×3, forward-project one prev-frame sample and **re-shade with the SAME random
sequence** so the gradient is *"entirely free of spatial offsets"*. **Caveat (loose
wording corrected):** the adaptive α targets **lighting-change** ghosting; **geometric
disocclusion is still handled by inherited reprojection/consistency tests
(depth/normal/mesh-ID)**, not the gradient. The current code's anti-ghosting + history
clamp is the right shape; a true forward-projected temporal gradient is the deeper
version if ghosting persists.
Source: cg.ivd.kit.edu/publications/2018/adaptive_temporal_filtering.

**2c. Demodulation is MANDATORY for any production denoiser (NRD confirms).**
[high · 3-0 / 2-1]
NVIDIA NRD README: inputs must be radiance not irradiance — *"NRD(diffuseRadiance /
albedo) * albedo"*. NRD's **RELAX** (SVGF-lineage à-trous) and **SIGMA** (per-light
shadow-only) are production references / fallbacks if the clean-room denoiser stalls.
Source: github.com/NVIDIA-RTX/NRD.

## 3. Upscaling (TAAU now, FSR 2 / 3.1 later) — the load-bearing input contract

**3a. FSR 2's input contract is strict and the order is denoise-then-upscale.**
[high · 3-0]
GPUOpen FSR2: three **render-resolution** buffers are mandatory — **color, depth,
motion vectors** — *"optional reactive mask and exposure required for the best possible
quality."* **Jitter convention (load-bearing): color + depth are jittered; motion
vectors are NOT jittered by default** (`FFX_FSR2_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION`
opts in). **Reactive mask** is for *"particles, or alpha-blended objects which do not
write depth or motion vectors"* — **NOT** for opaque emissive geometry (that writes
depth/MVs). FSR 2 *replaces TAA*, so it owns the temporal-AA slot.
Sources: gpuopen.com/manuals/fidelityfx_sdk/.../super-resolution-temporal,
gpuopen.com/fidelityfx-superresolution-2.

**What this means for our TAAU plumbing (DOOM-0009 6-d):**
- Our motion vectors are computed geometrically (worldPos → prev camera) and are
  **un-jittered** — **already FSR2-correct.** ✅
- **We do NOT yet produce a depth buffer** for the upscaler — FSR2 needs one. The
  G-buffer stores `gpos` (worldPos); a render-res NDC/linear **depth image** must be
  added in 6-d phase 2. ⬅ concrete gap.
- **Reactive mask + exposure** inputs are needed for FSR2 quality — add as part of
  phase 2 (sparse for DOOM; mostly opaque).
- Keep **denoise-then-upscale** ordering (we already do: composite → TAAU).

**3b. FSR Ray Regeneration (AMD's ML denoiser) is UNAVAILABLE on this stack.**
[high · 3-0]
Requires **RDNA4 (RX 9000+)**, **DX12 + SM 6.6**, **Windows 11**, and is *"currently
DirectX 12 exclusive"* (no Vulkan). **Not an option on RX 6600 + Vulkan.** ⇒ The
**custom SVGF/A-SVGF + TAAU→FSR 2** path is the route; do not spend effort chasing RR.
(Time-sensitive: FSR SDK support is evolving — RDNA3 upscaling landed in SDK 2.3,
RX 6000 upscaling slated ~2027 — re-check before any FSR milestone.)
Sources: gpuopen.com/amd-fsr-rayregeneration, gpuopen.com/manuals/fsr_sdk/.../denoising.

## 4. Lighting efficiency (NEE / MIS / ReSTIR / probe GI) — COVERAGE GAP

No claims on this axis survived verification within the budget. **Unaddressed, needs a
dedicated research pass.** Open questions carried forward:
- Measured register-pressure / occupancy + frame-time cost of adding **ReSTIR DI (and
  GI)** to the inline-ray-query megakernel **on the RX 6600** — does the variance
  reduction justify the VGPR hit at the 16-wavefront cap, or does it push occupancy
  below the latency-hiding threshold? (ReSTIR is RDNA2's worst register case per the
  spec's own §4.4 note — this needs measurement, not assumption.)
- Quality limits of **per-subsector SH-L1 probes** vs a dynamic **DDGI** field, given
  DOOM's doors/lifts change local visibility (the static bake may go stale).
- NEE + power-importance + MIS variance correctness (no verified external check yet).

## 5. Security / robustness — COVERAGE GAP

No claims survived verification within the budget. **Unaddressed, needs a dedicated
pass** (and matters: WADs are untrusted input). Open questions:
- GPU memory-safety / OOB with `buffer_reference` + bindless descriptor indexing
  (bounds, `robustBufferAccess`, descriptor-indexing partial-bound hazards).
- NaN/inf hardening across the path tracer (we clamp in places; need a systematic
  pass) — driver-level **RT validation** exists (NVIDIA) and Vulkan has a robustness
  guide worth mining.
- Untrusted **WAD/map data** driving emitter lists + AS builds → DoS / device-loss
  (TDR) from degenerate or huge geometry; defensive AS-build limits.

---

## Prioritised recommendations → roadmap items

| # | Recommendation | Axis | Maps to |
|---|----------------|------|---------|
| R1 | Profile + reduce megakernel **occupancy/VGPR pressure** on the RX 6600 (RGP; one live RayQuery; bound the kernel) | perf | new item (feeds DOOM-0009 step-7 perf pass) |
| R2 | **BLAS compaction** (20–50 % AS memory) + **TLAS rebuild on the compute queue**; keep transform-only door/lift refit | perf | new item |
| R3 | **FSR 2 input contract** for 6-d phase 2: add a render-res **depth** image + **reactive mask** + **exposure**; MVs already un-jittered; keep denoise→upscale | upscaler | annotate DOOM-0009 |
| R4 | Record **FSR Ray Regeneration unavailable** (RDNA4/DX12/Win11) — don't pursue on this GPU | upscaler | annotate DOOM-0009 |
| R5 | **Research gap:** ReSTIR DI/GI cost on RDNA2 + SH-L1 bake vs DDGI probe quality | lighting | new research item |
| R6 | **Research gap + harden:** GPU memory-safety (buffer_reference/bindless bounds), NaN/inf hardening, untrusted-WAD AS-build DoS/TDR | security | new research item |
| — | **Validated, no action:** megakernel choice, SVGF/A-SVGF + demodulation, denoise-then-upscale order are all confirmed correct | — | — |

## Sources (primary unless noted)
- AMD GPUOpen: RDNA performance guide; occupancy explained; FSR2 manual; FSR 3.1
  overview; FSR Ray Regeneration; FSR SDK denoising.
- Khronos: Vulkan RT best-practices for hybrid rendering; ray tracing in Vulkan;
  Vulkan robustness guide; AS spec; TLAS-animation tutorial.
- Papers: Schied et al. 2017 (SVGF); Schied et al. 2018 (A-SVGF); Laine et al. 2013
  (Megakernels Considered Harmful); DDGI (Majercik 2019); ReSTIR (Bitterli 2020).
- NVIDIA-RTX/NRD README; NVIDIA driver-level RT validation blog.
- Blogs (corroborating): Interplay of Light (RDNA2 RT); huziliang (denoise+SR cascade).
