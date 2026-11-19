# Performance Standard

Phase 2's promise is a modern-looking DOOM that still runs smoothly. That makes
performance a feature, not an afterthought — but only in the place that actually
costs: the GPU renderer. The 1997 CPU game loop is already fast enough on any
modern machine; don't spend effort micro-optimising it.

## The frame-rate floors

Each floor holds on the reference GPU (an AMD RX 6600).

**Classic: 35 FPS, the original game's.** Classic presents once per 35 Hz game
tic, as the 1997 game did, so it never reaches 60. A change that drops Classic
below 35 is a regression (DOOM-0490).

**The rasterised views of Solid and Ultra: 60 FPS.** A change that drops one
below 60 is a regression.

**The ray-traced views of Solid and Ultra: a steady 30 FPS** (user decision
2026-11-15). Steady means evenly paced frames, not a 30 average (DOOM-0277).
The user measured Ultra traced on E1M1 at 22 FPS on 2026-11-15, so this floor
is a target the views do not yet meet. Reaching 60 here is DOOM-0279's stretch
goal.

The Classic and raster floors are absolute. A change that drops a ray-traced
view further is a regression to fix or a trade to justify, never something to
ship quietly. To justify a trade: state the before and after numbers, get the
user's decision on the record, and ship a quality dial that lets them opt out.
The worked example is **DOOM-0011 §6**: volumetric fog raised its own budget
from ≤ 5 % to ≤ 15 % of present-total, with `rb_fog` 0..3 as the opt-out.

## The main lever: render scale

The world is drawn at a fraction of native resolution and upscaled. In the
traced view a temporal upscaler (TAAU) reconstructs the full-res image; the
rasterised view is stretched (DOOM-0594). This is the first knob for hitting
the floor.

The Video menu's Render Scale row has two kinds of choice
(`linuxdoom-1.10/render_scale.h`):

- **Dynamic**, the default (config key `render_scale_dynamic`). The scale
  follows the measured GPU time to hold the view's floor, between 33 % and
  100 % (DOOM-0596).
- **A fixed size**, listed as a resolution with its percentage (config key
  `render_scale`, `rb_renderscale`). It defaults to **50 %**.

A `-frameledger` run and the golden-image modes draw at the fixed size even
when the row says Dynamic, so two runs compare the same work. A capture or a
hand-timed run does not: put `render_scale_dynamic 0` in its config.

## Measuring

Use the **per-pass GPU profiler** — the `` \ `` (backslash) key, `rb_profile`,
DOOM-0090 — which prints each render pass's GPU time (Solid *and* Ultra). Measure
before and after any performance-affecting change; don't guess.

For a per-frame record, run with `-frameledger FILE` (`linuxdoom-1.10/d_ledger.c`
lists its flags). It writes frameledger's run file, which `frameledger check`,
`report` and `floor --fps 60` (`--fps 30` for a traced run) read. A private display gives a small window by
default, so pass `-scale`: `-scale 6` with `render_scale` 50 traces 1920x1200.

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
