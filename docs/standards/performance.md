# Performance Standard

Phase 2's promise is a modern-looking DOOM that still runs smoothly. That makes
performance a feature, not an afterthought — but only in the place that actually
costs: the GPU renderer. The 1997 CPU game loop is already fast enough on any
modern machine; don't spend effort micro-optimising it.

## The 60 FPS floor

The Ultra path tracer targets a **60 FPS floor** on the reference GPU (an AMD RX
6600). Solid and Classic sit comfortably above it. A change that drops Ultra
below the floor on the reference hardware is a regression to fix or a trade to
justify, not something to ship quietly.

## The main lever: render scale

`rb_renderscale` (config key `render_scale`) renders the world at a fraction of
native resolution and upscales — presets **100 / 75 / 67 / 50 %**. It defaults
to **50 %** so the engine boots playable rather than at single-digit FPS at
native. A temporal upscaler (TAAU) reconstructs the full-res image. This is the
first knob for hitting the floor.

## Measuring

Use the **per-pass GPU profiler** — the `` \ `` (backslash) key, `rb_profile`,
DOOM-0090 — which prints each render pass's GPU time (Solid *and* Ultra). Measure
before and after any performance-affecting change; don't guess.

## The comparison rule (this one bites)

**Always quote an FPS number together with the render scale it was measured at,
and only compare like-for-like.** Never pair, say, "8 FPS at 100 %" against a
"50 %" result and call it a speed-up — that compares two different workloads. A
before/after for a perf change is measured at the **same** render scale, on the
same map, on the reference GPU.

## Trades are explicit

- A performance change states its before/after (same scale, same scene).
- A *look* change that costs frames is a deliberate trade — say so, and confirm
  the floor still holds. Effects that are heavy get an on/off or quality toggle
  (as the de-tile and filth effects do) so the cost is opt-out.
- Where a lighting or build step is deliberately *not* optimised because it also
  drives correctness (e.g. the per-vertex reheight scan also drives
  animated-texture cycling), leave a note saying why, so nobody "optimises" it
  into a bug later.
