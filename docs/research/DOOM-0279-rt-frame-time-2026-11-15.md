# DOOM-0279 — where the ray-traced frame goes, and what others cut

**Status:** Research notes, not a spec. Compiled 2026-11-15 for DOOM-0279
(a steady 30 FPS in the ray-traced views). Binding decisions live in the
roadmap and in any spec that follows.

**Target:** AMD RX 6600 (RDNA2) on RADV, Mesa 26.2.6. Each compute unit has
one Ray Accelerator, which tests boxes and triangles. BVH traversal runs as
shader code. There are no matrix cores.

## 1. The measured frame

Measured 2026-11-15 at f62bee4 on the user's settings: Ultra, ray traced,
render scale 50, fog 3, bloom 2, specular on. E1M1 start, `-inspect -freeze`,
`rt_profile 1`. The private display gave a 4270x2000 swapchain (`-scale 5`),
3% more pixels than the user's 3840x2160. Figures are means of the
once-a-second reports after the first two.

| Run | Megakernel | Denoise + post + TAAU | of which à-trous | Present-total | FPS |
|---|---|---|---|---|---|
| Baseline | 31.01 ms | 16.23 ms | 10.28 ms | 50.95 ms | 19.6 |
| `specular 0` | 26.56 ms | 16.48 ms | 10.41 ms | 45.77 ms | 21.8 |
| `rt_fog 0` | 27.94 ms | 15.39 ms | 9.54 ms | 46.92 ms | 21.3 |
| `RADV_PERFTEST=cswave32` | 35.07 ms | 17.02 ms | 10.67 ms | 58.01 ms | 17.2 |
| Baseline, re-run after it | 35.63 ms | 17.19 ms | 10.82 ms | 58.99 ms | 17.0 |

What the table shows:

- **The GPU is the wall.** CPU build, record and submit stay under 1 ms each.
- **The baseline drifts by about 15% between runs** (50.95 against 58.99 ms).
  Compare an A/B pair only when its runs are interleaved and repeated.
- **Wave32 compute changes nothing** measurable.
- **The denoiser filters specular even with specular off**: its à-trous cost
  does not fall in the `specular 0` run.
- **Since DOOM-0597, every pixel runs NEE when specular is on.** The half-res
  lighting gate in `pathtrace.comp` mode 6 now skips work only when the lobe
  is off. So the "3 of 4 lanes return early" finding in DOOM-0279's
  2026-11-09 list no longer holds on default settings.
- **Shadow rays already use `TerminateOnFirstHit | Opaque`** and the world-only
  cull mask (`occluded` in `pt_common.glsl`).

A steady 30 FPS needs 33.3 ms, so about 18 ms must go. No single lossless
lever on DOOM-0279's list is likely to reach that; most are guessed at
under 1 ms.

## 2. What shipped engines cut, ranked for this card

Savings are guesses unless a source is named.

1. **idTech 8's final gather** (Sousa, SIGGRAPH 2025). It traces one ray
   per pixel at ½ or ¼ resolution, with "0 shading, only use caches": a
   screen-space hit, then a world hash radiance cache, then irradiance
   volumes. Series S (RDNA2): world sampling 0.38, gather 0.6, denoise 0.21,
   upscale 0.65 ms; about 1.71 ms with async compute. High against Low is
   "relatively close, bit softer". Our megakernel runs NEE at the hit
   instead, which is the expensive part. This extends DOOM-0188 and DOOM-0189.
2. **Denoise at lighting resolution, so only the upscale runs at full
   resolution.** idTech 8 runs a separable bilateral blur at gather
   resolution, from FP16 in shared memory, then a 4-tap bilateral upscale.
   Today our à-trous runs four full-render-res passes (10.28 ms above).
   This is DOOM-0191.
3. **Lighting density as the quality setting.** Metro Exodus EE on PC
   traces at half resolution on Normal, checkerboarded on High and native
   on Ultra. On consoles it halved the per-pixel ray count. Lumen
   reflections trace at a quarter rate with `DownsampleFactor 2`.
4. **Async compute.** idTech 8 saved 0.4–0.5 ms of about 2 ms and said it
   could not hit its target without it. This is DOOM-0190.
5. **Bounces and reflections.** Portal RTX reaches 30 FPS on Steam Deck at
   864x486 with secondary bounces off. Q2RTX's second bounce skips specular
   and local lights. idTech 8 replaced RT reflections on consoles with SSR
   plus probes.
6. **Frame pacing for a locked 30.** RADV has `VK_EXT_present_timing` from
   Mesa 26.1 (Wayland) and 26.2 (X11), so a present every second vblank is
   available. Khronos says `VK_KHR_present_wait` "lacks sufficient
   accuracy". This is DOOM-0277.

Low value on this card:

- **FSR 3 frame generation and AMD Fluid Motion Frames.** AMD says to use
  them from about 60 FPS before generation, and to avoid them under 30.
  They cannot lift 22 FPS to 30.
- **Ray sorting, packed FP16 and Brixelizer GI.** No RDNA2 figures exist
  for any of them.

## 3. How games split the settings

- Metro EE has one RT-quality setting that changes trace resolution.
- Lumen's scalability levels each cost about half the level above. Its
  GI, reflection and fog budget at 1080p is 4 ms for 60 FPS and 8 ms for 30.
- idTech 8 has one GI-quality setting (½ against ¼ gather).

The research's ranking of rows, most frame time per unit of visible loss
first: lighting resolution, second bounce, reflections, shadow rays per
light, denoiser quality.

## Sources

- Sousa, "Fast as Hell: idTech 8 Global Illumination", SIGGRAPH 2025: <https://www.advances.realtimerendering.com/s2025/content/SOUSA_SIGGRAPH_2025_Final.pdf>
- 4A Games, Metro Exodus PS5/Series upgrade: <https://4a-games.com.mt/4a-dna/everything-technical-about-metro-exodus-upgrade-for-playstation-5-and-xbox-series-x-s>
- PC Gamer, Metro Exodus EE settings: <https://www.pcgamer.com/best-settings-metro-exodus-enhanced-edition-pc-performance>
- AMD GPUOpen, Radeon Raytracing Analyzer 1.3: <https://gpuopen.com/learn/rra_1_3/>
- Epic, Lumen performance guide: <https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine>
- NVIDIA Q2RTX path tracer source: <https://raw.githubusercontent.com/NVIDIA/Q2RTX/master/src/refresh/vkpt/shader/path_tracer.h>
- PC Gamer, Portal RTX on Steam Deck: <https://www.pcgamer.com/heres-path-tracing-running-on-the-steam-deck-at-30-fps-thanks-to-some-settings-tweaks>
- Mesa 26.1.0 and 26.2.0 release notes: <https://docs.mesa3d.org/relnotes/26.1.0.html>, <https://docs.mesa3d.org/relnotes/26.2.0.html>
- Khronos, `VK_KHR_present_wait`: <https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_present_wait.html>
- AMD, FSR 3 frame generation: <https://gpuopen.com/amd-fsr-framegeneration/>
