# DOOM-0090 — RGP occupancy capture guide (RX 6600 / RADV / openSUSE)

**Status:** Operational how-to (not a spec). Steps for capturing one GPU trace of
the Ultra path-tracer megakernel and reading off its **VGPR count + wave
occupancy** — the single number that gates the ReSTIR go/no-go in
`DOOM-0092-restir-cost-benefit.md` §1.3.

**Who runs this:** the user, on the RX 6600. It needs the AMD Radeon GPU Profiler
GUI, a focused game window, and one captured frame in a light-heavy room.

---

## What we're after

One question: **how many vector registers (VGPRs) does the path-trace compute
shader use, and how many wave32 waves stay resident per SIMD?** If the kernel is
already near the ceiling (≈96+ VGPRs → ≤10/16 waves), there is no room to add
ReSTIR's reservoir state and the answer is "no ReSTIR on this card." If there's
headroom, ReSTIR DI *might* fit and we measure further.

## Step 0 — capture mechanism (current Mesa)

Recent Mesa (25.x+) drives RADV traces through `MESA_VK_TRACE_*` env vars. The
old `RADV_THREAD_TRACE` / Radeon Developer Panel path is **deprecated for RADV**
— don't use it. Confirm your Mesa is ≥ 25.1:

```bash
vulkaninfo --summary | grep -i "driverName\|driverInfo"   # expect: radv, Mesa 25.x
```

## Step 1 — get the Radeon GPU Profiler GUI

RGP is not in the openSUSE repos. Download the Linux build of the **Radeon
Developer Tool Suite** (RGP is inside it) from the GitHub releases:

- <https://github.com/GPUOpen-Tools/radeon_gpu_profiler/releases>

Extract the tarball anywhere (e.g. `~/rgp/`). The GUI binary is
`RadeonGPUProfiler`. No install/root needed — it's a self-contained build.
(The capture itself is done by RADV, *not* by this tool — RGP only *opens* the
`.rgp` file afterward.)

## Step 2 — capture a trace from DOOM_Ants

1. Launch the game in **Ultra** mode at your normal **50% render scale**, e.g.:

   ```bash
   MESA_VK_TRACE=rgp MESA_VK_TRACE_TRIGGER=/tmp/rgp_trigger \
     ./linuxdoom-1.10/linuxdoom -iwad /path/to/doom.wad
   ```

   - `MESA_VK_TRACE=rgp` arms RGP/SQTT trace dumping (output lands in `/tmp`).
   - `MESA_VK_TRACE_TRIGGER=/tmp/rgp_trigger` sets a trigger *file* — the trace
     fires when that file appears.

2. In-game, **walk into the worst light-heavy room** — the glowing-prop room that
   profiled at 8–9 fps with the `\` per-pass profiler. We want the megakernel at
   its heaviest. Let it settle for a second.

3. Trigger one capture, from a second terminal:

   ```bash
   touch /tmp/rgp_trigger
   ```

   (Alternatively, on **X11** you can press **F1** with the game window focused —
   same effect. The trigger-file method works regardless of X11/Wayland, so
   prefer it if F1 does nothing.)

4. A `.rgp` file is written to `/tmp` (look for `/tmp/*.rgp`). One capture is
   enough. Quit the game.

> Note: the path tracer is a **compute** workload. If the capture comes back
> empty or missing the dispatch, add `MESA_VK_TRACE_PER_SUBMIT=1` (valid only
> with `MESA_VK_TRACE=rgp`) and re-capture — it enables per-submit capture for
> compute-only work.

## Step 3 — read VGPR + occupancy in RGP

1. Open the file: `~/rgp/RadeonGPUProfiler /tmp/<that>.rgp`
2. Find the path-trace dispatch: the **Events / Wavefront occupancy** timeline →
   pick the long compute event (the megakernel — by far the biggest compute
   block in the frame).
3. Open the **Pipeline state / Shader** view for that dispatch. Read off:
   - **VGPRs used** (vector general-purpose registers per thread)
   - **Occupancy** — "waves per SIMD" (out of the hardware max, 16 on RDNA2) or
     the limiting factor RGP names (VGPR-limited / LDS-limited / etc.)
   - **SGPRs used** (scalar registers — secondary, note it too)

## Step 4 — report back these four numbers

Paste these to me and I'll close DOOM-0092 §1.3 / DOOM-0090's occupancy line:

1. **VGPRs used** by the megakernel: ____
2. **Waves per SIMD** (occupancy): ____ / 16
3. **What RGP says is the limiter** (VGPR / LDS / SGPR / wave-slot): ____
4. **SGPRs used**: ____

### Decision rule (so you know what we'll conclude)

- **≤10 waves / VGPR-limited at ~96+** → kernel is register-starved already;
  ReSTIR DI is a non-starter on the 6600. We stay on the cheap ladder (REJECT
  cull → RIS) — see `DOOM-0092-restir-cost-benefit.md` §1.4.
- **≥12 waves with headroom** → ReSTIR DI *could* fit; we'd prototype the cheap
  ladder first anyway, then measure whether reservoirs add anything.

Either way the cheap ladder (REJECT light culling, then RIS with one shadow ray)
comes first — this capture only decides whether full ReSTIR is ever worth
prototyping on this hardware.

### Sources

- Mesa env vars (`MESA_VK_TRACE`, `_TRIGGER`, `_FRAME`, `_PER_SUBMIT`) —
  <https://docs.mesa3d.org/envvars.html>
- Radeon GPU Profiler releases + manual —
  <https://github.com/GPUOpen-Tools/radeon_gpu_profiler/releases>,
  <https://gpuopen.com/manuals/rgp_manual/>
- RADV RGP-compat background — <https://www.phoronix.com/news/RADV-Radeon-GPU-Profiler>
