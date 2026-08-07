# DOOM-0331 — Bloom on the HDR views, so bright things read as light sources

**Status:** spec draft (2026-08-07).
**Kind:** feature.
**Source:** ROADMAP DOOM-0331 (`upstream-review-2026-08-05`, GZDoom
`wadsrc/static/shaders/pp`). Scope calls taken with the user 2026-08-07 — see §3.

**Layman:** Lamps, muzzle flashes, glowing goo and lava will bleed a little
light into the air around them, the way bright things do in a photo — so a lamp
stops looking like a pale wall and starts looking like a lamp.

**Depends on:**

- **DOOM-0170 L2a/L2b** — the raster path's HDR seam. The world is drawn into two
  16-bit float targets (`kSceneFormat`, `VK_FORMAT_R16G16B16A16_SFLOAT`) —
  AMBIENT (`sceneImage`) and DIRECT (`sceneDirImage`) — and `composite.frag`
  recombines and tone-maps them. That recombined value is where this feature
  taps the raster picture.
- **DOOM-0009 / DOOM-0090** — the RT path's denoise chain. `svgf_composite.comp`
  re-modulates albedo, re-adds emission, folds fog, and tone-maps; `taau.comp`
  upscales; a blit puts the result on the swapchain. That pre-tone-map radiance
  is where this feature taps the traced picture.
- **DOOM-0205 / DOOM-0206** — the effects toggles and the consolidated Video menu
  they now live in. DOOM-0205 shipped them as a Render Effects submenu; DOOM-0206
  folded that into one `VideoDef` with an `— Effects —` group, which is where the
  dial goes (§4.5).
- **DOOM-0084 / DOOM-0302 / DOOM-0183** — the emitters that make bloom worth
  having. Nothing here reads their masks (see §3, decision 2); they are the
  reason there is something above white to extract.

**Delivers / subsumes:** nothing. No existing roadmap item is closed by this.

**Defers (explicitly NOT in this build):**

- **Auto-exposure / eye adaptation** — no item filed; the manual Brightness
  slider (`rb_exposure`, DOOM-0096) stays the only exposure control.
- **Lens dirt, streaks, anamorphic flares** — no item filed. This is a plain
  radially-symmetric glow.
- **FXAA and depth of field** — the other two post-process passes GZDoom ships
  and this engine does not. Neither is filed; neither is in scope.
- **Bloom on the path tracer's debug views** (`rb_rtdebug` 1–4). Those are
  Debug-Views diagnostics, not a play view; see §9.

**Scope:** Solid and Ultra, in **both** their views — the rasterised view
(`rb_rtdebug == 0`) and the ray-traced play view (`rb_rtdebug == 6`). Classic is
untouched (INV-1). Every surface is byte-identical when the dial is Off
(INV-2), and the HUD is never bloomed and never bloomed over (INV-3).

---

## Contents

- §1 Goal — §2 Where this sits — §3 Scope decisions — §4 Design
  (4.1 where it hooks · 4.2 the bright pass · 4.3 the blur · 4.4 the combine ·
  4.5 the dial · 4.6 what the RT path needs that raster does not) —
  §5 Data & resources — §6 Performance budget — §7 Build order —
  §8 Invariants — §9 Alternatives considered — §10 Open questions —
  §11 What checks this — §12 Cross-doc impact — §13 Cold-eyes loop log

---

## 1. Goal

A light source looks like a light source. After this ships, anything genuinely
brighter than white — a ceiling lamp, a lit switch, a muzzle flash, a fireball,
glowing nukage, lava — bleeds a soft halo into the pixels around it, in Solid
and in Ultra, in the rasterised view and the ray-traced one. Ordinary art does
not: a plain wall at full sector light is not a light source and does not glow.

One dial in the Video menu's Effects group (`Bloom: Off / Low / Medium / High`)
scales it, and Off restores the current picture exactly.

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0331? |
|-----------------|----------|-----------------------|
| Classic | paletted software renderer | **No** (INV-1) |
| Solid, RT off (`rb_rtdebug == 0`) | raster stack | **Yes** — §4.1 raster chain |
| Ultra, RT off (`rb_rtdebug == 0`) | raster stack | **Yes** — same chain |
| **Solid, RT on** (`rb_rtdebug == 6`) | path tracer + denoiser | **Yes** — §4.1 RT chain |
| Ultra, RT on (`rb_rtdebug == 6`) | path tracer + denoiser | **Yes** — same chain |
| Path-tracer debug views (`rb_rtdebug` 1–4) | path tracer | **No** — gated, see below |

**The gate is which chain drew the frame, never the tier label**, and the third
row is the one that proves it: `rtActive` in `RB_Vulkan_Present` is
`rb_rtdebug && g.rtEnabled && g.tlas && g.rtModule && g.haveCamera && g.vbuf &&
g.atlasReady` — **no `rendermode` term at all**, verified. Solid with the Ray
Tracing row on runs the RT chain, exactly as `CLAUDE.md` says ("Each of Solid
and Ultra has both a rasterised and a ray-traced view"). An implementer who
gates the RT hook on `rendermode == TIER_RT3D` ships a feature that vanishes in
half its supported configurations.

**The debug views are excluded by an explicit guard, not structurally.** `rtActive`
is true for `rb_rtdebug` 1–4 as well, and those modes tone-map inside
`pathtrace.comp` rather than in the composite — so the split, the bloom dispatches
and `rt_tonemap` are recorded only when `rb_rtdebug == 6`. Without that guard,
modes 1–4 would either take a bloom they were never designed for or read an
`rtHdrImage` nothing wrote. Say it in code as one condition beside the `rb_bloom`
gate (§4.4), not as two scattered tests.

Solid and Ultra share one Vulkan backend (`renderer.md`), which is why the
feature is not, and must not be, gated on Ultra: `CLAUDE.md` makes effects a
property of the *view*, and Solid's smoothness a protected property — hence the
60 fps measurement in §6 before this ships.

Classic is excluded structurally rather than by a flag: `backends[RB_CLASSIC]`
(`r_backend.c`) routes present to `Classic_Present`, which never enters
`RB_Vulkan_Present`, so no Vulkan pass of any kind runs. The menu row is
excluded the same way — `M_SetupNextMenu(rendermode==RB_CLASSIC ? &RendererDef
: &VideoDef)` means `VideoDef`, which owns the Render Effects rows, is never
shown under Classic.

## 3. Scope decisions (agreed with the user)

Four preference calls, all taken with the user on 2026-08-07. They are recorded
because each has a cheaper wrong answer that will otherwise be re-proposed.

1. **The bright pass reads the picture BEFORE the tone-map, not after.** This is
   the decision the whole design turns on. The Khronos PBR-Neutral operator both
   paths use compresses hard above its 0.76 knee, so a lamp at 4.0 linear and a
   white wall at 1.0 linear arrive at the screen as **0.9833 and 0.8691** — a 4×
   brightness difference squashed to **1.13×**. Extracting after the tone-map
   would therefore bloom bright walls almost as readily as lamps, which is
   precisely the complaint on the roadmap bullet ("a lamp and a white wall can
   end up looking alike"). Extracting before it separates them completely. The
   cost is that the RT path's tone-map has to move one stage later (§4.6); the
   user accepted that.

   Those two figures are the shipped operator's own output, not an estimate:

   ```
   python3 -c "
   def pbr(v):
       sc=0.8-0.04; d=1.0-sc
       off = v-6.25*v*v if v<0.08 else 0.04
       c=v-off
       return c if c<sc else c*((1.0-d*d/(c+d-sc))/c)
   print(pbr(1.0), pbr(4.0), pbr(4.0)/pbr(1.0))"
   # 0.8690909090909091 0.9832558139534884 1.1314
   ```

   Worth recording while it is in front of us: the operator is **not** identity
   below the knee — it subtracts a flat 0.04 (0.50 linear → 0.4600), with a soft
   toe below 0.08. `composite.frag`'s own comment claims identity, which is
   wrong; that is a pre-existing comment defect, filed as **DOOM-0337** rather
   than fixed here (this spec must not be the only place the correction lives).
2. **What glows is decided by brightness, not by an emissive flag.** A single
   threshold, applied to the value each path is about to tone-map. The
   alternative — reuse the DOOM-0084/0302 per-texel emissive mask so only
   flagged emitters can bloom — was rejected: it would exclude explosions,
   muzzle flash and stacked point lights, none of which is a flagged emitter,
   and it threads a mask through two more shaders to do it. The escape hatch the
   user accepted with this: a flashlight aimed point-blank at a white wall *can*
   cross the threshold. §10 Q2 owns whether that reads badly.
3. **One menu row with four presets** (`Off / Low / Medium / High`), matching the
   existing Volumetric Fog row rather than exposing threshold and intensity as
   two separate rows. Each preset moves both numbers together (§4.5).
4. **On by default, at a restrained setting** (`bloom = 2`, Medium). A feature
   nobody sees is a feature that did not ship. This is what makes the §6 floor
   measurement a gate rather than a formality, and it is why `rb_bloom` must be
   pinned in the `-shotverify` block (INV-8).

## 4. Design

Four stages, in the same order in both paths: **extract → blur → blur →
combine**. Only the extract's *source* differs between the two, and only the
RT path needs a structural change to make room for the combine (§4.6).

### 4.1 Where it hooks

**Raster chain** — the rasterised view, `rb_rtdebug == 0`, in **either** tier. The frame today is: shadow
pass → scene pass (MRT: AMBIENT + DIRECT) → SSAO pass (half-res) → swapchain
pass, inside which `composite.frag` tone-maps to the screen and the 2D overlay
is drawn on top. Bloom inserts three compute dispatches between the SSAO pass
and the swapchain pass — a legal place because dispatches cannot be recorded
inside a render pass, and the SSAO pass has ended by then:

```
shadow pass
scene pass          -> AMBIENT + DIRECT   (kSceneFormat, HDR)
SSAO pass           -> aoImage            (half-res R8)
bloom_extract_raster  (dispatch, half-res)   -> bloom[0]
bloom_blur            (dispatch, quarter-res, dir = +X) -> bloom[1]
bloom_blur            (dispatch, quarter-res, dir = +Y) -> bloom[2]
swapchain pass
  composite.frag    -> hdr += bloom[2] * intensity, then tone-map   <-- combine
  overlay draw      -> HUD / menu, keyed, on top
  FlushMenuText()
```

The combine costs no extra pass: `composite.frag` already samples three
textures and already holds the recombined HDR value, so the bloom is one more
fetch and one add before the operator it already applies.

**RT chain** — the ray-traced view, `rb_rtdebug == 6`, in **either** tier. The
frame today is:
megakernel → SVGF temporal → 4× a-trous → `svgf_composite.comp` (re-modulate,
re-add emission, fold fog, **tone-map**, write `rtImage`) → TAAU → blit →
`RecordRtOverlay` (weapon + HUD). Bloom splits the tone-map out of
`svgf_composite` (§4.6) and inserts itself in the gap:

```
megakernel
SVGF temporal
a-trous x4
                      --- rb_bloom == 0: unchanged from today ---
svgf_composite.comp  -> rtImage  (tone-maps in place, as it does now)
                      --- rb_bloom  > 0: the -DBLOOM_SPLIT variant ---
svgf_composite.comp  -> rtHdrImage  (linear exposed radiance; alpha = sky flag,
                                     and the SKY branch keeps its own toneEncode)
bloom_extract_rt       (dispatch, half-res)               -> bloom[0]
bloom_blur             (dispatch, quarter-res, dir = +X)  -> bloom[1]
bloom_blur             (dispatch, quarter-res, dir = +Y)  -> bloom[2]
rt_tonemap.comp      -> rtImage  (rtHdrImage + bloom[2] * intensity, tone-mapped)  <-- combine
                      --- both paths rejoin here ---
TAAU
blit -> swapchain
RecordRtOverlay      -> weapon + HUD, on top
```

Both combines therefore land **after the scene is fully shaded and before the
HUD exists** — which is what the roadmap bullet asked for, and what INV-3
locks.

### 4.2 The bright pass — what crosses the threshold

The threshold is applied to **the value the path is about to tone-map**, in that
path's own units. That keeps threshold and tone-map knee commensurate *within*
each path; it does **not** by itself make the two paths agree with each other,
because only one of them applies an exposure (§10 Q3 owns that gap):

- Raster: `composite.frag`'s `hdr`, i.e. `direct * aoDirect + ambient * ao`.
  No exposure multiply — the raster path applies none.
- RT: `svgf_composite.comp`'s `L * exposureEv(EV)`, the exposed radiance
  `toneEncode()` currently feeds to `pbrNeutralToneMapping` — **as a candidate,
  not a settled decision. §10 Q3 owns this choice and blocks L5 on it**, and the
  reason is the opposite of the intuitive one:

  Thresholding the exposed value means a texel blooms iff
  `L × exposureEv(EV) > T`, i.e. iff `L > T / exposureEv(EV)`. The physical
  radiance required therefore **moves with the Brightness slider** — turn
  brightness up and dimmer things start blooming. Thresholding the *unexposed*
  `L` is the arm that keeps the dial's meaning fixed. The argument for the exposed
  form is not stability but *commensurability*: it is the value the tone-map's
  knee acts on, so threshold and knee share units, and the raster path (which
  applies no exposure at all) is then the odd one out rather than both being odd.
  Neither arm is free, which is exactly why it is an open question and not a line
  in this section.

Extraction, per source texel, with a soft knee so a surface crossing the
threshold ramps in rather than pops. `threshold` and `knee` both come from
`kBloomPresets` (§4.5) — neither is a shader literal (INV-9):

```glsl
// bloom_extract_*.comp — soft-knee bright pass.
// peak, NOT Rec.709 luminance — see the note below; this is the same quantity
// pbrNeutralToneMapping keys its own compression on.
float  peak   = max(c.r, max(c.g, c.b));
float  soft   = clamp(peak - threshold + knee, 0.0, 2.0 * knee);
soft          = soft * soft / (4.0 * knee + 1e-4);
float  weight = max(soft, peak - threshold) / max(peak, 1e-4);
vec3   bright = c * weight;             // 0 below threshold-knee; above it, c scaled so peak -> peak-threshold
```

**Threshold on the max channel, never on luminance.** This is the single easiest
way to build a bloom that misses the things DOOM actually glows with, and it
looks completely reasonable in code. Rec.709 luminance weights green at 0.7152
and red at 0.2126, so a saturated emitter's luminance is a *fraction* of its
linear magnitude — while every threshold and floor in this spec is stated in
linear magnitude. Worked at the High preset (threshold 1.35), for emitters at
magnitude 4.0:

| Emitter | linear | Rec.709 `lum` | verdict on `lum` | `peak` | verdict on `peak` |
|---|---|---|---|---|---|
| red fireball / lava | (4,0,0) | 0.850 | **no bloom** | 4.000 | blooms |
| blue | (0,0,4) | 0.289 | **no bloom** | 4.000 | blooms |
| green nukage | (0,4,0) | 2.861 | blooms | 4.000 | blooms |
| orange lava | (4,1.5,0) | 1.923 | blooms | 4.000 | blooms |
| white lamp | (4,4,4) | 4.000 | blooms | 4.000 | blooms |
| **white wall** | (1,1,1) | 1.000 | no bloom | 1.000 | no bloom |

On luminance, a red fireball at four times white **does not bloom** while a white
wall sits at the very edge of blooming — the roadmap bullet's complaint restated,
with the causality inverted. `peak` also happens to be exactly what
`pbrNeutralToneMapping` keys on (`float peak = max(color.r, max(color.g,
color.b))`), so the threshold and the operator's knee end up measured in the same
units, which is the whole point of §4.2.

Two further properties this shape has to keep:

- **The floor is `threshold − knee ≥ 1.0`, NOT `threshold ≥ 1.0`.** This is the
  easy thing to get wrong, and getting it wrong silently defeats the feature.
  The knee makes extraction begin at `threshold − knee`, not at `threshold`: with
  a threshold of 1.00 and a knee of 0.5, a wall at 0.9 linear gets
  `soft = clamp(0.9 − 1.0 + 0.5, 0, 1.0) = 0.4`, hence
  `weight = 0.4²/(4×0.5) / 0.9 = 0.089` — it blooms, faintly, and INV-5's own
  test would fail on a faithful implementation. **Paletted art at full sector
  light tops out at 1.0 linear in the AMBIENT term**, so what must sit at or above
  1.0 is the point where the ramp *starts*. §4.5's presets are chosen to satisfy
  it, and INV-5 and INV-10 both rest on it.

  **What the floor does NOT bound: DIRECT.** The thresholded value is
  `direct * aoDirect + ambient * ao`, a sum, and DIRECT (point lights, flashlight,
  muzzle) has no ceiling — a wall with several lamps trained on it genuinely
  exceeds 1.0 and *will* bloom. That is intended, not a leak: §3 decision 2
  chose brightness over an emissive flag precisely so stacked point lights and
  explosions glow. So the floor's guarantee is narrower than it first reads —
  **ordinary art at ordinary sector light cannot bloom; art blasted by direct
  light can** — and it is the same accepted case as §10 Q2's flashlight. INV-5's
  test therefore samples walls *away* from lamps, or it is testing the wrong
  thing.

  Verified against the extract above, at the High preset (threshold 1.35,
  knee 0.35, so the ramp starts at exactly 1.00):

  | `peak` | `soft` (clamped, pre-square) | `weight` | `bright` |
  |---|---|---|---|
  | 0.90 | 0.00 | 0.000 | **0.000** — a lit wall, no bloom |
  | 1.00 | 0.00 | 0.000 | **0.000** — the floor, exactly zero |
  | 1.20 | 0.20 | 0.024 | 0.029 — ramping in |
  | 4.00 | 0.70 | 0.663 | 2.650 — `= peak − threshold`, fully in |
- **The sky contributes nothing.** In RT the sky is written display-encoded and
  deliberately un-tone-mapped (`svgf_composite.comp`, the `gp.w < 0.0` branch),
  so it is not radiance and must not be thresholded as if it were; `rtHdrImage`'s
  alpha carries the flag and the extract multiplies it in. In raster the sky is
  written at paletted magnitude and falls below every preset's ramp start
  (`threshold - knee`, floored at 1.0) on its own.

The extract runs at **half display resolution**, and each output texel gathers
the source texels under it by **thresholding each one first and averaging
after**. That order matters: averaging first would dilute a single bright texel
below the threshold and lose thin emitters (a distant lamp, a switch), which a
bilinear fetch cannot avoid. Thresholding first keeps a one-texel emitter's
excess energy at its fair fraction, which is correct and does not flicker as the
camera moves.

**How many source texels that is depends on the render scale, and the shipped
default makes it one.** The world fills only the `[0, uvScale]` corner of the
raster scene targets and the `[0, renderW) × [0, renderH)` corner of `rtHdrImage`, so
the ratio between source and output is `renderScale / 50 %`:

| `render_scale` | source region @ 1920×1080 | extract output | source texels per output texel |
|---|---|---|---|
| 100 % | 1920×1080 | 960×540 | 2×2 = 4 |
| 75 % | 1440×810 | 960×540 | 1.5×1.5 |
| 67 % | 1286×723 | 960×540 | ~1.34×1.34 |
| **50 % (default)** | **960×540** | **960×540** | **1×1 — no averaging at all** |

Tap count is `ceil()` of the ratio, so: 2×2 at 100 %, 75 % and 67 %; 1×1 at 50 %.

**The two intermediate presets give fractional ratios, so the tap count is
`ceil(ratio)` with each tap clamped to the source region** — 2×2 at 75 % and 67 %
just as at 100 %, with the outer taps landing on the same texel as the inner ones
near the edge rather than outside it. A fractional gather is not implementable;
rounding up and clamping costs at most a duplicated tap and never misses a texel,
which is the direction of error this section cares about.

At the default the extract is a 1:1 point read, so the thresholding-first
argument buys nothing *there* — it earns its keep at 75 % and 100 %, and at 50 %
it is simply a no-op rather than a cost. The shader therefore derives its tap
count from `uvScale`/`renderScale` rather than hardcoding 2×2, and §6's budget
row quotes the 100 % case because that is the expensive arm. This also means
**§9's rejection of a quarter-res extract is a 100 %-scale argument**; at 50 %
a quarter-res extract would be a 1:2 downsample and would need the same
thresholding-first gather the half-res one does at 100 %.

**Both extracts write the whole half-res target, and the bloom targets are mapped
over the whole frame** — the same convention `aoImage` already uses, and the
reason nothing has to be reallocated when the render-scale menu changes
mid-frame. Stated precisely, because the alternative reading misplaces the entire
halo in Ultra at the 50 % default:

- Each extract covers **every** texel of `bloomImage[0]`, i.e.
  `[0, dispW/2) × [0, dispH/2)`, with no unwritten region. It reaches its source
  by scaling *inward* — the raster extract samples the scene at its own derived `uv * uvScale` (it is
  compute — there is no `vUV` varying),
  the RT extract at `p * 2 * renderExtent / dispExtent` (the push field is
  `renderExtent`, in pixels — §5; there is no separate `renderScale` uniform) — so a full-frame output
  is filled from a corner-shaped input.
- The blur therefore reads a fully-written target and needs no corner clamp, and
  the combines sample bloom in **plain full-frame UV**. `rt_tonemap.comp`'s
  sketch in §4.4 must not scale its bloom fetch by `renderScale`: its pixel `p`
  is render-res, so the full-frame UV is
  `(vec2(p) + 0.5) / vec2(renderExtent)` — the render-res normalisation *is* the
  full-frame coordinate, with no second factor.
- The samplers still use `CLAMP_TO_EDGE`, as the scene sampler does, so a blur
  tap that lands outside the image at the frame border repeats the edge rather
  than reading black and darkening the halo there.

### 4.3 The blur

Two dispatches at **quarter** display resolution, the same
`bloom_blur.comp` twice with the direction in a push constant. The first reads
the half-res `bloom[0]` with a bilinear sampler, so the ½ → ¼ downsample is
free (no second threshold is applied downstream, so bilinear cannot push a value
back under one — though it *can* attenuate a lone bright texel; see the aliasing
note below).

A 9-tap Gaussian collapsed to **5 bilinear-paired fetches**, σ = 2.0 texels at
quarter res. The kernel is fixed at author time, not generated at build time —
there is no codegen step in this Makefile and this spec adds none, so the table
below goes in `bloom_blur.comp` as named constants:

| Fetch | Offset (quarter-res texels) | Weight |
|---|---|---|
| centre | 0.0000 | 0.204164 |
| inner pair | ±1.4073 | 0.304005 each |
| outer pair | ±3.2942 | 0.093913 each |

**The offsets are in OUTPUT texels, and pass 1 must convert them.** Pass 1 writes
quarter-res while *reading* half-res `bloomImage[0]`, so one output texel is two
source texels: its UV step is `offset × 2 × srcTexelSize`. Pass 2 reads and writes
quarter-res, so its step is `offset × 1 × srcTexelSize`. Getting this wrong is
silent and asymmetric — the X blur comes out half as wide as the Y blur and the
halo is subtly oval. Hence `srcTexelSize` in the push block, with the per-pass
factor baked into the caller's dispatch.

Pass 1's taps land at ±2.81 and ±6.59 half-res texels, so it does **not** sample
every half-res texel it passes over. A lone one-texel emitter in `bloomImage[0]`
can therefore be skipped by the downsample even though §4.2 was careful to keep it
*in* the extract. Accepted for v1 rather than solved: the surviving case is a
single half-res texel with no bright neighbour, which after a 16-pixel blur would
have contributed a barely visible glow anyway. If §10 Q1's look call reports
emitters flickering at distance, the fix is a 2×2 box per tap in pass 1 — noted
here so the next reader does not have to re-derive why it flickers.

Derived from `exp(−i²/2σ²)` for i = 0…4 at σ = 2.0, normalised, then each
adjacent pair collapsed to one bilinear fetch at its weighted-average offset:

```
python3 -c "
import math
sig=2.0; w=[math.exp(-(i*i)/(2*sig*sig)) for i in range(5)]
t=w[0]+2*sum(w[1:]); w=[x/t for x in w]
print('centre', round(w[0],6))
for a,b in ((1,2),(3,4)):
    ww=w[a]+w[b]; print('pair', round((a*w[a]+b*w[b])/ww,4), round(ww,6))
print('sum', round(w[0]+2*(w[1]+w[2])+2*(w[3]+w[4]),6))"
# centre 0.204164 / pair 1.4073 0.304005 / pair 3.2942 0.093913 / sum 1.0
```

The weights sum to exactly 1.0, so the blur is energy-preserving and the
intensity dial is the only thing scaling the halo.

**Reach is ±4 quarter-res texels = ±16 display pixels at 1920×1080** — bounded by
the kernel's extent, not by 3σ. That is a lamp halo, deliberately tight. §10 Q1
owns widening to a 13-tap (7-fetch) kernel, if that look call says it reads too
small.

One level, not a pyramid. The roadmap bullet allowed "a separable blur
pyramid"; a single quarter-res level is the cheapest thing that can work and is
what "cheapest wins first" asks for. §9 records what a second and third level
would buy and what they would cost.

### 4.4 The combine

One additive term, applied to the pre-tone-map value. The same *arithmetic* in
both paths, but not the same source line — `composite.frag` is a fragment shader
with a `vUV` varying and a sampler, `rt_tonemap.comp` is compute and derives its
coordinate from `gl_GlobalInvocationID`:

```glsl
// composite.frag (raster) — fragment, bloomTex at set 0 binding 3
if (pc.bloomIntensity > 0.0)
    hdr += texture(bloomTex, vUV).rgb * pc.bloomIntensity;
```

```glsl
// rt_tonemap.comp (RT) — the COMPLETE body, sky arm included. Omitting the sky
// arm tone-maps the sky twice, which is the §4.6 trap this sketch must not invite.
vec4 h = imageLoad(rtHdrImage, p);
if (h.a < 0.5) { imageStore(rtImage, p, vec4(h.rgb, 1.0)); return; }  // sky: done
vec3 L = h.rgb;
if (pc.bloomIntensity > 0.0) {
    // p is render-res; normalising by the RENDER extent already gives the
    // full-frame UV the bloom targets are mapped in (§4.2) - no second factor.
    vec2 uv = (vec2(p) + 0.5) / vec2(pc.renderExtent);
    L += texture(bloomTex, uv).rgb * pc.bloomIntensity;
}
imageStore(rtImage, p, vec4(toneMapEncode(L), 1.0));
```

**A branch, not a multiply by zero.** `hdr + bloom * 0.0` is exact for finite
`bloom`, but a NaN or Inf that reached `bloom[2]` would survive the multiply and
poison the frame — and both paths already guard against non-finite radiance
(`if (any(isnan(L)) || any(isinf(L))) L = vec3(0.0)`), which says such values do
occur. With the branch, `rb_bloom == 0` cannot reach the add at all, which is
what makes INV-2 a structural guarantee rather than a floating-point argument.

**And the dial being ON needs its own guard, which the branch does not provide.**
Both chains already clamp non-finite radiance before their own tone-map, but the
raster scene targets carry no such guard on the path the extract reads — so one NaN
texel, thresholded and then blurred, spreads over a ±16-pixel halo. The extract
therefore clamps as its first act: `if (any(isnan(c)) || any(isinf(c))) c =
vec3(0.0);`, before the threshold, in both extract variants. Cheaper there than
anywhere downstream, because it is the only pass that reads unguarded values.

**Off must cost nothing, and on the RT path that takes one extra measure.** With
the dial Off the raster chain simply records none of the three dispatches and is
done. The RT chain cannot be, because §4.6 moves its tone-map into
`rt_tonemap.comp` — so a naive split leaves Ultra paying for an extra
render-res pass and an HDR round-trip (~4 MiB written and ~4 MiB read back at the
50 % default, so ~8 MiB round-trip; the 15.8 MiB in §5 is the allocation, not the
per-frame cost) on every frame, *including* frames where
bloom is switched off. `performance.md` requires a heavy effect's toggle to be a
real opt-out, so the split is itself gated:

- `rb_bloom == 0` → `svgf_composite.comp` tone-maps straight to `rtImage`,
  exactly as today, and neither `rt_tonemap.comp` nor `rtHdrImage` is touched.
- `rb_bloom > 0` → `svgf_composite.comp` writes `rtHdrImage` and
  `rt_tonemap.comp` finishes the job.

Two pipelines from one source, selected by a `-DBLOOM_SPLIT` define, following
the `mesh_overlay.frag` precedent already in the Makefile
(`glslc ... -DSINGLE_TARGET`). `rtHdrImage` is allocated up front either way —
allocation is cheap and per-frame reallocation is not — but it is written only on
the split path.

This is what keeps INV-2's "byte-identical when Off" true on *both* chains
rather than only on the raster one, and it is why §6's Off row is genuinely
zero.

### 4.5 The dial

One integer, four presets, one menu row, following the `rb_fog` precedent
exactly.

| Name | Where | Value |
|---|---|---|
| `rb_bloom` | `r_vulkan.cpp`, `extern "C"` alongside `rb_fog` | 0 Off / 1 Low / 2 Medium / 3 High |
| `bloom` | `m_misc.c` config table, default `2` | persisted to `~/.doomrc` |
| `vid_bloom` | `m_menu.c` `videoitem_e`, after `vid_fog` | row label `"Bloom"`, hotkey `'m'` |
| `M_ChangeBloom` | `m_menu.c` | `rb_bloom = (rb_bloom + 1) % 4` |
| `kBloomPresets` | `r_vulkan.cpp`, one table | `{threshold, knee, intensity}` per level |

**The knee lives in the preset table, not in the shaders.** It is a tuning
number exactly like the threshold — and §4.2 shows the two are not independent,
since the floor constrains `threshold − knee` rather than either alone. Keeping
the knee as a shader constant would put half of a coupled pair out of reach of
the dial and would violate INV-9. `rb_bloom` indexes the table; the shaders
receive both numbers in a push constant and hold no literal.

**`rb_bloom` is clamped where it indexes the table**, not trusted from the
config file. `~/.doomrc` is a plain text file a user can hand-edit, and `bloom 9`
would otherwise read past a four-entry array. Clamp at **every** read site, not just the preset index: `M_ChangeBloom`'s
`(rb_bloom + 1) % 4` silently maps a hand-edited 9 to 2, and the menu's label
lookup indexes its name array before any keypress — the same shape as the
`rb_fog` defect this paragraph files. A single clamp applied at config load, plus
the use-site guard, closes both. Clamp the way
`rb_renderscale` already is (`rb_renderscale < 25 ? 25 : rb_renderscale > 100 ?
100 : rb_renderscale`, two sites in `r_vulkan.cpp`) — that is the live precedent
in this file. **Note the precedent is `rb_renderscale`, not `rb_fog`:** `rb_fog`
turns out to have no clamp anywhere, only a display-side guard in the menu, which
is its own latent out-of-range read and is filed as **DOOM-0338**.

`'m'` for "blooM", because `'b'` is already `vid_brightness`. (`'v'` is
currently used twice — `vid_fog` and `vid_debugviews` — a pre-existing collision
this feature does not touch and must not add to.)

Starting preset values, **to be tuned with the user on hardware** (§10 Q2) —
these are a defensible opening position, not a measurement. The `ramp starts`
column is `threshold − knee`, the quantity §4.2's floor constrains, and **no
preset may put it below 1.00**:

| Level | Threshold | Knee | Ramp starts | Intensity |
|---|---|---|---|---|
| Off | — | — | — | 0.00 |
| Low | 1.80 | 0.30 | 1.50 | 0.20 |
| Medium | 1.50 | 0.35 | 1.15 | 0.35 |
| High | 1.35 | 0.35 | **1.00** | 0.55 |

Low → High *lowers* the ramp start (1.50 → 1.15 → 1.00), so more of the scene
qualifies as a light source and the halo strengthens with it. High sits exactly on
the floor: at that preset a surface at 1.00 linear contributes precisely zero,
which §4.2's worked table checks. Tuning may move all four numbers, but never
below the floor — a preset whose ramp starts under 1.00 breaks INV-5 and INV-10
rather than merely looking different.

The table is defined **once** and read by both paths (INV-9). Whether the two
paths need different intensities — their lighting magnitudes are not identical,
so the same threshold may catch different things — is §10 Q3; the answer must
not be two tables.

No debug key — §9 records why.

**Row count.** `VideoMenu` carries 20 rows today (counted), so `vid_bloom` makes
21. DOOM-0206's menu contract requires the result stay HUD-safe and scroll if it
outgrows the screen; that mechanism is already in place for the existing 20 and
this adds one row to a group that already has six. Worth a look at L1's verify
step rather than an assumption (§10 Q4).

### 4.6 What the RT path needs that the raster path does not

The raster combine is free because `composite.frag` computes the HDR value and
tone-maps it in the same shader — the bloom slots between the two. The RT path
has no such gap: `svgf_composite.comp` computes `L` and tone-maps it in one
step, and the bloom cannot be added before a value that has not been extracted
yet.

So, on the split path only (`rb_bloom > 0`, §4.4), the **surface** tone-map moves
out into its own pass:

- **`svgf_composite.comp` writes `rtHdrImage`** instead of `rtImage` for the
  surface path: linear exposed radiance (`L * exposureEv(EV)`), with
  `alpha = 1.0`. Everything it does before that — albedo re-modulation, emission
  re-add with the DOOM-0302 weight, the fog fold, the non-finite guard, the
  motion-vector write — is unchanged.
- **The sky branch keeps its own `toneEncode()` call and is not moved.** This is
  the trap in this section. The `gp.w < 0.0` branch is *not* simply
  "display-encoded, no tone-map": when `pc.misc3.y != 0u` it calls
  `sky = toneEncode(skyLin * fog.a + fog.rgb)` to fold fog in linear and
  re-encode — and `rt_fog` **defaults to 1**, so that is the shipped path, not an
  edge case. An implementer told "the tone-map moves out" who deletes
  `toneEncode` from `svgf_composite` ships a linear, un-encoded fogged sky. So:
  the sky branch writes its finished display-encoded colour into `rtHdrImage`
  with `alpha = 0.0` and still returns early, and `toneEncode` **stays** in
  `svgf_composite` for it. Only the surface path's encode moves.
- **`toneEncode()` must be split in two, because it currently contains the
  exposure.** As shipped it is
  `L = max(L,0) * exposureEv(EV); L = pbrNeutralToneMapping(L); return srgb(L)` —
  a local function inside `svgf_composite.comp` reading that shader's own
  `pc.misc3.x`. So it is **not** a shared helper and cannot simply be called from
  a second shader, and calling it downstream of a store that already applied
  exposure would double-expose the frame. Split it:
  - `svgf_composite.comp` keeps the exposure and the non-negative clamp —
    `max(L, 0) * exposureEv(EV)` — and stores that into `rtHdrImage`.
  - A new **`toneMapEncode(vec3)`** — `pbrNeutralToneMapping` then `linearToSrgb`,
    no exposure — is called by `rt_tonemap.comp`, and by `svgf_composite.comp`'s sky
    branch in place of its `toneEncode` call (the sky's exposure is applied on the
    same line, unchanged).

    **It needs a new include of its own; it must NOT go in
    `formulas/pbr_neutral_tonemap.glsl`.** `linearToSrgb` lives in
    `formulas/formulas.glsl`, and `composite.frag` includes only
    `pbr_neutral_tonemap.glsl` — so defining `toneMapEncode` there gives
    `composite.frag` a body calling an undeclared function, which is a compile
    error in GLSL whether or not the function is ever called. Put it in a third new
    file, `formulas/tonemap_encode.glsl`, which `#include`s both prerequisites, and
    include *that* only from `rt_tonemap.comp` and `svgf_composite.comp`.
    `composite.frag` keeps calling `pbrNeutralToneMapping` directly — it
    deliberately does not sRGB-encode.
  This is why `rt_tonemap.comp` carries no EV in §5's push table: the exposure is
  already baked into what it reads.
- **`rt_tonemap.comp` is new** and finishes the surface path:
  `if (a < 0.5) out = rgb; else out = toneMapEncode(rgb + bloom * intensity)`,
  then writes `rtImage`. The `a < 0.5` arm carries the already-encoded sky through
  untouched and unbloomed, exactly as today; that arm keeps the sky store's
  existing `clamp(sky, 0.0, 1.0)`.
- Everything downstream — TAAU, the label stamp, the blit, `RecordRtOverlay`,
  the `-shotverify` capture — reads `rtImage` and is unchanged.

**What TAAU then does to the halo is a look question, not a correctness one, and
it is deliberately left to Q1.** The combine sits *before* the temporal upscaler,
so the bloom is reprojected and accumulated with everything else. For a static
lamp that is free and correct. For a muzzle flash — one bright frame every few
tics — TAAU's history will smear the halo across the following frames, which may
read as a pleasing afterglow or as ghosting. Combining *after* TAAU would avoid it
and costs a display-res pass instead of a render-res one (§9). Judge it on
hardware rather than in advance; §10 Q1 carries it.

**The split path is not bit-exact, and L4's gate must not ask it to be.**
`rtHdrImage` is `kSceneFormat` (fp16, ~11-bit mantissa), so a value that reaches
the tone-map through it has been rounded once more than today's fp32-register
path before being quantised to `rtImage`'s 8 bits. Most pixels land on the same
byte; some near a rounding boundary will not. So L4's `bloom 2` arm is gated at a
tolerance rather than byte-identity (§7).

**This does not weaken INV-2.** `rb_bloom == 0` runs the *un-split* pipeline, which
never touches `rtHdrImage` at all, so the Off arm stays byte-exact on both chains
and INV-2 needs no tolerance. The tolerance belongs to L4's split arm alone.

This split is what buys the two paths one shared behaviour instead of two. The
alternative (leave `svgf_composite` alone and add the bloom to the tone-mapped
`rtImage` in LDR) is cheaper by one pass and one image, and is rejected in §9:
it would make the same lamp glow differently in Solid and Ultra, and
`composite.frag`'s own comment records that keeping the two tone-matched is
deliberate.

Note that `toneEncode()` also sRGB-encodes, and the raster composite
deliberately does not (its input is already display-referred paletted colour).
That asymmetry is pre-existing and is not disturbed: the bloom is added in each
path's own pre-tone-map space, ahead of whichever encode that path applies.

## 5. Data & resources

### New render targets

All sized from the swapchain extent and recreated on resize with the existing
scene targets, so a mid-frame render-scale change reallocates nothing:

| Image | Size | Format | Usage | Used by |
|---|---|---|---|---|
| `bloomImage[0]` | ½ display | `kSceneFormat` | STORAGE + SAMPLED | both paths |
| `bloomImage[1]` | ¼ display | `kSceneFormat` | STORAGE + SAMPLED | both paths |
| `bloomImage[2]` | ¼ display | `kSceneFormat` | STORAGE + SAMPLED | both paths |
| `rtHdrImage` | full display | `kSceneFormat` | STORAGE | RT path only |

`kSceneFormat` is `VK_FORMAT_R16G16B16A16_SFLOAT` = 8 bytes/pixel. At
1920×1080:

```
bloomImage[0]   960 x  540 x 8 =  4,147,200 B  =  3.96 MiB
bloomImage[1]   480 x  270 x 8 =  1,036,800 B  =  0.99 MiB
bloomImage[2]   480 x  270 x 8 =  1,036,800 B  =  0.99 MiB
rtHdrImage     1920 x 1080 x 8 = 16,588,800 B  = 15.82 MiB
                                 ----------       --------
                                 22,809,600 B  =  21.75 MiB   (15.82 of it RT-only)
```

Arithmetic, not a measurement — `1920*1080*8 = 16588800`, and so on. Bounded and
non-growing: three fixed fractions plus one full-size image, all freed with the
swapchain. The three bloom images are shared by both paths because only one
chain runs per frame; the mode toggle already drains the device
(`vkDeviceWaitIdle` on `modeChanged`, verified in `RB_Vulkan_Present`), so
nothing aliases across the transition.

### Descriptors, layouts and barriers

The target table above is not sufficient on its own — three pieces of plumbing
have to be named or an implementer discovers them at debug time.

**A fourth binding on the composite descriptor set.** `composite.frag` binds
set 0 bindings 0/1/2 (`ambientTex`, `directTex`, `aoTex`); `bloomTex` becomes
**binding 3**, which means the composite descriptor-set layout, its pool sizing
and the `g.compositeDs` write in `UpdateCompositeDescriptor` all change. (The
push block does not change: `composite.frag` already carries an unused `float
pad` after `aoEnable`, and the intensity goes there.)

**`bloomImage[2]` is parked in `SHADER_READ_ONLY_OPTIMAL` at creation.** This is
the same reason `aoImage` is parked, and the AO comment says it outright: the
composite may sample it "even on a frame where the SSAO pass is skipped". With
the dial Off all three bloom dispatches are skipped (§4.4), so without the park
`bloomImage[2]` is still `UNDEFINED` when `composite.frag`'s descriptor set
binds it. Follow the `aoImage` pattern exactly — a one-time
`BeginOneTime`/`EndOneTime` barrier in the create path.

**Barriers between the three dispatches.** Each reads the previous one's output,
so extract → blurH → blurV needs a `GENERAL` (compute write) →
`SHADER_READ_ONLY_OPTIMAL` (sampled read) transition plus an execution
dependency at each hop, and `bloomImage[2]` ends the chain in
`SHADER_READ_ONLY_OPTIMAL` for the combine. On the RT chain the same applies
between `svgf_composite` → extract and between blurV → `rt_tonemap`; the
existing `svgfBarrier()` helper is the idiom to follow.

**And one barrier that is easy to miss on the raster chain**, because it moves a
read *earlier* in the frame: `bloom_extract_raster` samples `sceneImage`,
`sceneDirImage` and `aoImage`, whose transition to `SHADER_READ_ONLY_OPTIMAL`
today happens for the composite's benefit *inside* the swapchain pass. The extract
now needs them readable before that pass begins, so the existing scene-pass →
composite dependency has to be brought forward to scene-pass → extract (the SSAO
pass's own output likewise). This is the one hop where "it already works for the
composite" is not evidence.

**Pipelines and descriptor sets for the new work.** Four new compute shaders means
four pipelines, four set layouts and their pool allocations; and the
`-DBLOOM_SPLIT` variant of `svgf_composite.comp` needs its own pipeline **and** a
descriptor that points its output binding at `rtHdrImage` instead of `rtImage`
(today binding 7 of `g.svgfDs` re-uses `g.rtView`). The pipelines are `g.bloomExtractRasterPipeline`, `g.bloomExtractRtPipeline`,
`g.bloomBlurPipeline` and `g.rtTonemapPipeline` — the last is the identifier
INV-3's ordering test greps for, so it is named here rather than left to the
implementer. The `labelTaauDs` pattern —
an extra set that retargets one binding at a different image, layout-compatible
with the same pipeline layout — is the precedent already in the file.

### New shaders

Four new compiled shaders, added to `SHADER_SRCS` in `linuxdoom-1.10/Makefile`,
plus one shared include, which is **not** in `SHADER_SRCS` (it is `#include`d,
never compiled on its own) and instead needs an explicit dependency line:

| File | Stage | Job |
|---|---|---|
| `bloom_extract_raster.comp` | compute | recombine AMBIENT/DIRECT/AO, threshold, gather (§4.2) |
| `bloom_extract_rt.comp` | compute | read `rtHdrImage`, threshold, gather, sky (`a == 0`) → 0 |
| `bloom_blur.comp` | compute | separable Gaussian, direction from a push constant |
| `rt_tonemap.comp` | compute | `rtHdrImage` + bloom → tone-map → `rtImage` |
| `formulas/scene_recombine.glsl` | include | the **whole** raster HDR recombination — see below |

**The include owns the AO fetch and blur, not just the final multiply.** This is
the subtle one: `composite.frag`'s `hdr` is not `direct * aoDirect + ambient * ao`
alone — it is that *plus* the `aoEnable > 0.5` gate, the 4-tap bilinear box blur
of the half-res AO, the `AO_DIRECT_WEIGHT = 0.5` mix, and the rule that AO is
sampled at `vUV` while the scene targets are sampled at `vUV * uvScale`. If the
include carries only the algebra and the extract re-derives the rest, the value
the extract thresholds is **not** the value the composite blooms. Note what the
include can and cannot buy: it makes the *formula* identical, not the *value*,
because the extract runs at half res and the composite at full res, so their scene
fetches and the 4-tap AO blur land on different sample positions. Formula identity
is the contract; the sub-texel difference is accepted, and is why the halo is a
blurred approximation of the final picture rather than an exact function of it.
INV-2 and
INV-5 both rest on those agreeing. So the include exposes one function, and **it
must take the sample coordinate** — the two consumers do not share one, because
`composite.frag` has a `vUV` varying and the extract is compute and derives its
own:

```glsl
// formulas/scene_recombine.glsl
vec3 sceneRecombine(sampler2D amb, sampler2D dir, sampler2D ao,
                    vec2 uv, vec2 uvScale, float aoEnable);
// samples the scene at uv*uvScale and AO at uv, exactly as composite.frag does today
```

Both consumers call it, and neither computes any part of `hdr` itself.

Two existing *shaders* are edited: `composite.frag` (add the combine; move the recombination
wholesale into the new include) and `svgf_composite.comp` (write `rtHdrImage`
instead of tone-mapping to `rtImage`, on the `-DBLOOM_SPLIT` variant only).

`glslc` emits no auto-dependency for a GLSL `#include` (`renderer.md`), so
`composite.frag.spv.h` and `bloom_extract_raster.comp.spv.h` both need an
explicit `scene_recombine.glsl` dependency line beside the existing
`pt_common.glsl` and `formulas/` rules, or an edit to the include will not
rebuild its consumers. **The same hazard is already live for a file this feature
edits:** `composite.frag` `#include`s `formulas/pbr_neutral_tonemap.glsl`, but the
Makefile's existing `formulas/` dependency line names only `pathtrace.comp.spv.h`
and `svgf_composite.comp.spv.h` — so `composite.frag` is today rebuilt only by
luck when that include changes. Add it to that line in the same edit; the new
`formulas/tonemap_encode.glsl` (§4.6) needs its own dependency line naming its two
consumers, since it includes two other files that can each change under it. The `-DBLOOM_SPLIT` variant of `svgf_composite.comp`
needs its own `.spv.h` rule, modelled on the existing `mesh_overlay.frag.spv.h`
rule (`glslc ... -DSINGLE_TARGET`) — the one precedent in this Makefile for two
binaries from one shader source. **That precedent is two edits, not one:** the
rule itself, *and* a `SHADER_HDRS+=` append, whose existing comment says it is
there "so it builds + gates r_vulkan.o like the other shader headers". Without the
append the variant's header is never a prerequisite of `r_vulkan.o` and a stale
SPIR-V blob links silently.

### Push constants

**No new RT megakernel lane.** `renderer.md` records that `misc6` and both pad
words are full and the next RT push value must open a `misc7`; this feature
adds none, because nothing it does happens inside `pathtrace.comp`. The bloom
passes carry their own small push blocks:

| Pass | Push contents |
|---|---|
| `bloom_extract_raster` | `uvScale.xy`, `aoEnable`, `threshold`, `knee` |
| `bloom_extract_rt` | `renderScale.xy`, `threshold`, `knee` |
| `bloom_blur` | `dir.xy`, `srcTexelSize.xy` (1/size of the image being read) |
| `composite.frag` | existing `{uvScale, aoEnable, pad}` — `pad` becomes `bloomIntensity` |
| `rt_tonemap` | `renderExtent.xy` (render-res pixel dimensions), `bloomIntensity` |

`rt_tonemap` carries **no** exposure EV: `svgf_composite` already applied
`exposureEv(EV)` before writing `rtHdrImage` (§4.2 requires the threshold to be in
post-exposure units), so all that is left downstream is
`pbrNeutralToneMapping` + the sRGB encode. Passing EV twice would double-expose
the frame.

`composite.frag`'s existing push block already carries an unused `float pad`
after `aoEnable`, so the raster combine needs no layout change at all — the
intensity goes in the slot that is already reserved and ignored.

### GPU profiler slots

`performance.md` mandates measuring with the per-pass profiler, so a pass with
no timestamp is invisible to the instrument that is supposed to gate it. Widening
the pool is therefore part of this feature — and **widening it touches ten
sites, not two.** DOOM-0011 already paid for this lesson: its fix ledger's row
9.4 records a profiler widening that "named 3 sites; there are 7", the misses
including `uint64_t ts[8]`, a fixed stack array `vkGetQueryPoolResults` would
have written 72 bytes into. Do not re-learn it. Every site, verified present in
the current tree:

| # | Site | Today | After |
|---|---|---|---|
| 1 | `qpci.queryCount` (pool creation) | `8` | `10` |
| 2 | `uint64_t ts[8]` (readback stack array) | `8` | `10` — **overflows silently if missed** |
| 3 | `double profMs[8]` (`VulkanState` member) | `8` | `10` |
| 4 | `for (int pi = 0; pi < 8; pi++)` (report reset) | `8` | `10` |
| 5 | `vkCmdResetQueryPool(g.cmd, g.gpuTimerPool, 0, 8)` in `RecordRtTrace` | `8` | `10` |
| 6 | `vkCmdResetQueryPool(g.cmd, g.gpuTimerPool, 0, 8)` in `RB_Vulkan_Present` | `8` | `10` |
| 7 | `uint32_t nq = g.profRasterFrame ? 6u : 8u` | `6 : 8` | `7 : 10` |
| 8 | the `if (prof && !denoise)` dummy-timestamp block, slots 5–7 | 5,6,7 | 5,6,7,**8,9** |
| 9 | the `[raster_profile]` `printf` — format string **and** its five `profMs[]` args | 5 buckets | 6, and `composite`/`hud` labels shift by one |
| 10 | the `[rt_profile]` `printf` — format string **and** its `profMs[0..7]` args | 8 buckets | 10 |

**Sites 9 and 10 are the two easiest to miss, and missing site 9 corrupts the
measurement rather than breaking it.** The `[raster_profile]` print passes five
`profMs[]` values against five hard-coded labels; insert bloom at slot 4 and the
sixth bucket has nowhere to go, so bloom's cost is printed under the word
`composite`, composite's under `hud`, and HUD is never printed at all. §6's gate
would then read a plausible, wrong table. Both printf sites need the format string
*and* the argument list changed together.

Sites 5 and 6 are two *separate* reset calls — one per chain — and both must
move, or the chain whose reset was missed queries unreset slots and
`vkGetQueryPoolResults` drops the entire print that §6's gate depends on.

**Site 8 matters twice, and the second reason is easy to miss: the bloom dial is
itself a gate.** `nq` is a compile-time-shaped constant, but the new slots are
only *written* when `rb_bloom > 0` (§4.4 skips every bloom dispatch when the dial
is Off). A reset-but-unwritten slot returns `VK_NOT_READY` and the readback drops
the **whole** print — which would kill the profiler on precisely the `bloom 0` arm
that §6's measurement and INV-6 compare against. So the dummy-timestamp mechanism
site 8 already uses for the mode gate must also cover the bloom gate:

```
if (prof && !bloomActive)   // mirrors the existing `if (prof && !denoise)` block
{
    // collapse the bloom slots onto the preceding point so their segments read ~0
    vkCmdWriteTimestamp(..., <raster slot 4 | RT slots 8 and 9>);
}
```

Close the whole widening out with the standing grep DOOM-0011's ledger
prescribes, because every one of sites 2, 3 and 4 compiles silently when wrong.
**It must match the declarations only.** A bare `\b8\b` — or any pattern
containing `ts\[8\]` or `profMs\[8\]` — also matches the new, correct
`g.profMs[8] += (double)(ts[8] - ts[7]) * k;` assignments, so the loose form
reports a breach against a good build. Anchoring on the type keywords separates
them:

```
grep -nE 'queryCount = 8|uint64_t ts\[8\]|double +profMs\[8\]|pi < 8|Pool, 0, 8' \
     linuxdoom-1.10/r_vulkan.cpp
```

Verified both ways against the current tree: today it returns exactly the six
lines above (sites 1–6, the two resets being two of them), and it does **not**
match the post-L6 `g.profMs[8] += … ts[8] …` use lines. Expected output after
L6 is empty.

**Slot numbering: raster inserts, RT appends. They differ, and the reason is that
RT's existing slots are already out of chronological order.**

- **Raster inserts in frame order**, because its slots are chronological today:
  `... 3 = SSAO, 4 = bloom+blur, 5 = composite, 6 = HUD`. **Only two of the five
  `g.profMs[]` assignments move** — not all five: `[0]` shadow (`ts[1]-ts[0]`),
  `[1]` scene and `[2]` SSAO all difference slots below the insert and are
  untouched. `[3]` becomes bloom, `[4]` becomes composite, and a new `[5]` takes
  HUD. Re-labelling all five would mislabel three correct rows.
- **RT appends**, because its write order is chronologically `0,1,2,5,6,7,3,4` —
  slot 3 is written after TAAU and slot 4 after the blit, with 5–7 a
  sub-breakdown sitting *inside* the `[2,3]` interval. So the new passes take
  slots **8 (bloom+blur)** and **9 (`rt_tonemap`)**, placed chronologically
  between slot 7 (`svgf_composite`) and TAAU. Renumbering RT "in frame order"
  instead would break `profMs[4..7]`.

  That append has one consequence that must be written down, because it is
  exactly the silent-absorption failure INV-7 exists to catch: `profMs[7]`
  (TAAU) is currently `ts[3] - ts[7]`, which after the insert would span
  bloom + `rt_tonemap` + TAAU. It becomes **`ts[3] - ts[9]`**, with the two new
  buckets `profMs[8] = ts[8] - ts[7]` (bloom) and
  `profMs[9] = ts[9] - ts[8]` (`rt_tonemap`), and the `[rt_profile]` format
  string gaining both. `profMs[2]` (`ts[3] - ts[2]`) keeps working but its label
  goes stale: it is the umbrella bucket, today "denoiser chain + TAAU", and its
  interval now also spans bloom and `rt_tonemap`. Update the comment and the
  printed word, or the two new buckets read as double-counted.

### No new external dependency

Nothing is added to the dependency set. `glslc` and `xxd` already build every
shader; the blur, the threshold and the tone-map are arithmetic.

## 6. Performance budget

**Budget: ≤ 5 % of present-total in Solid, and Solid must stay above the 60 fps
floor at 50 % render scale on the reference RX 6600.** Ultra's floor is a target
rather than a guarantee (`performance.md`, and it already sits near 45 fps), so
the Ultra gate is the same ≤ 5 % relative bound, not an absolute frame rate.

No number here is measured yet, and none will be quoted until it is. What the
design costs, structurally:

| Pass | Output pixels @ 1920×1080 | Work per pixel |
|---|---|---|
| extract | 518,400 (½ res) | 1 source tap at the default 50 % scale, 4 at 100 % (§4.2) × (3 textures raster / 1 image RT), threshold |
| blur ×2 | 129,600 each (¼ res) | 5 bilinear fetches |
| combine (raster) | 0 extra | folded into `composite.frag`, which already samples three textures |
| `rt_tonemap` (RT) | render-res | 2 loads + tone-map + 1 store, bandwidth-bound |

`rt_tonemap` is the only row that is a *new pass* rather than added work inside an
existing one, and §4.4 gates it on `rb_bloom > 0`, so the Off row of every
measurement below is genuinely zero on both chains.

The measurement, at the L6 gate (§7), following the comparison rule — same map,
same render scale, reference GPU, both arms from the same build:

```
# Solid, 50% render scale, E1M1: bloom default vs bloom off
#   read the [cpu_profile] fps line and the composite/bloom pass rows
\   (rb_profile) with bloom=2, then with bloom=0
# Ultra RT, 50% render scale, E1M1: same pair, read [rt_profile]
```

Levers, cheapest first, if the budget is missed:

1. **Shrink the blur from 9 taps (5 fetches) to 5 taps (3 fetches).** Cuts the
   blur's fetch count by 40 %; costs reach, which §4.3 already calls tight.
2. **Drop the extract to quarter res.** §9 records why it is not there already,
   and §4.2 records that the argument is scale-dependent.
3. **Give `rtHdrImage` a packed format.** `B10G11R11_UFLOAT_PACK32` is 4 bytes
   instead of 8, halving its 15.8 MiB and its bandwidth; §9 records the catch
   (it has no alpha, and the sky flag currently rides there, so the flag would
   need another home such as a sentinel radiance value).

**Not a lever:** making `rtHdrImage` smaller than the display. It carries the
whole scene radiance that `rt_tonemap` turns into `rtImage`, so shrinking it
shrinks the *picture*, not the bloom — and it is swapchain-sized for the same
reason `rtImage` is, namely that `rb_renderscale` changes per frame from the menu
and a render-res allocation would have to be recreated mid-play.

And the dial itself is the player-facing lever — `Off` skips every pass on both
chains (§4.4) — which is what `performance.md` asks a heavy effect to ship with.

## 7. Build order

Each step ends with something observable. `make` and `make test` after every
one (`always-rebuild-engine`).

**Every A/B verify below takes THREE captures, not two** — `ab_diff.py`'s
signature is `<on.png> <off.png> <control.png>`, and the control is a second
capture from the *same* build with identical settings. Its own header says why:
"a control that cannot move proves the harness before it proves the effect", and
SIGNAL is meaningless unless NOISE is quoted beside it. A two-argument call does
not fail cleanly, it raises on unpacking.

- **L1 — the dial, doing nothing.** `rb_bloom` + the `bloom` config key + the
  `vid_bloom` menu row + `M_ChangeBloom` + the `kBloomPresets` table + the
  use-site clamp + the `-shotverify` pin. No render change. *Verify:* the row
  cycles Off/Low/Medium/High, survives a restart (`grep bloom "$CFG"` against the
  temp config — never `~/.doomrc`, which the engine rewrites on exit), is absent
  in Classic, and the 21-row menu still fits (§10 Q4).
- **L2 — the raster extract.** `bloomImage[0..2]` with the
  `SHADER_READ_ONLY_OPTIMAL` park, `bloom_extract_raster.comp`,
  `formulas/scene_recombine.glsl`, and `composite.frag` refactored onto the
  include with the combine still absent. *Verify:* the frame is unchanged —
  `ab_diff.py <post-L2> <pre-L2> <post-L2-control>` → SIGNAL mean 0.00, max 0.0.
  The refactor is provably a no-op before anything is added on top, and this is
  the step where a `scene_recombine.glsl` that dropped the AO blur would show up.
- **L3 — the raster blur + combine.** `bloom_blur.comp` ×2, the composite's
  fourth descriptor binding, and the `composite.frag` add. *Verify:* a lamp in
  Solid gains a halo; `bloom 0` is byte-identical (INV-2 raster arm); a plain
  wall does not move (INV-5).
- **L4 — the RT tone-map split, gated.** `rtHdrImage`, the `-DBLOOM_SPLIT`
  variant of `svgf_composite.comp`, `rt_tonemap.comp` doing the surface encode
  with the bloom term absent, the sky branch's own `toneEncode` left in place.
  This is the risky step and is deliberately separated so the split can be proven
  neutral on its own. *Verify,* in two arms:
  - `bloom 0` → the un-split pipeline runs, so this arm **is** byte-identical to
    the pre-L4 build: SIGNAL max 0.0.
  - `bloom 2` → the split runs with no bloom term, so this arm is
    **near**-identical, not byte-identical: `rtHdrImage` is fp16 and adds one
    rounding step ahead of the 8-bit quantise (§4.6). Gate it at SIGNAL
    mean ≤ 0.02 / max ≤ 1.0 with NOISE quoted beside it. Asking for byte
    identity here would fail a correct implementation.
  - Both arms: `-rtverify -warp 1 1 -noinput` still PASSes, and a **sky-facing
    and fog-on** capture is included — that is the arm that catches a deleted
    `toneEncode` in the sky branch, and `rt_fog` defaults to on so the default
    config already exercises it.
- **L5 — the RT extract + combine.** `bloom_extract_rt.comp` and the
  `rt_tonemap` add. *Verify:* the same lamp gains a halo in Ultra RT; the sky
  does not (INV-10); `bloom 0` is byte-identical to pre-L4 (the un-split arm,
  unchanged since L4).
- **L6 — profiler slots, then the gate.** All eight profiler sites (§5) plus the
  RT `profMs[]` re-mapping and **both `printf` sites** (§5 sites 9–10), then §5's
  declaration-anchored standing grep, the §6 measurement in both chains, `-rtverify`, and the human
  look call on hardware (§10). Then the `-shotcompare` golden's re-bless is owed
  — see §12; it is already owed for `rt_fog`, so this adds to an existing debt
  rather than creating one. Only after all of that does the ROADMAP bullet flip
  and CHANGELOG gain an entry.

`scripts/ab_capture.sh` needs one change to serve L2 and L3, because **as written
it cannot capture Solid at all.** It ends with
`grep -m1 'HD load done' "$OUT/$NAME.log" || { …; exit 1; }`, which is the right
guard for Ultra (the HD-assets `cwd` trap is a real one) and fatal for Solid:
`EnsureHdMaterials` opens with `if (rendermode != TIER_RT3D || g.hdBuilt) return;`
so a Solid run never prints that line.

The script also takes no tier argument — it inherits the tier from the config it
copies (`renderer` in `~/.doomrc`, `RB_RASTER3D == 2`). So the mechanism is:
**make the HD check conditional on the `renderer` value in `$CFG`**, which the
script already has in hand:

```sh
# only Ultra (renderer 1) loads HD art, so only Ultra owes the log line
if grep -qE '^renderer[[:space:]]+1$' "$CFG"; then
    grep -m1 'HD load done' "$OUT/$NAME.log" || { echo "!! $NAME: PALETTED"; exit 1; }
fi
```

That keeps the Ultra guard exactly as strict as it is today and stops it firing on
a tier that was never going to satisfy it. A caller then selects Solid by passing
`DOOMCFG=` a config with `renderer 2`, which the script's existing env hook
already supports.

## 8. Invariants

- **INV-1** — Classic renders byte-identically. Nothing bloom-related executes
  under `RB_CLASSIC`, and the menu row is not reachable there.
  *Test:* two greps, because the first cannot see the second breach — `m_menu.c`
  is on its allowed list either way:
  ```
  grep -rl 'bloom' linuxdoom-1.10/*.c linuxdoom-1.10/*.cpp linuxdoom-1.10/*.h
  # expect exactly: m_misc.c, m_menu.c, r_vulkan.cpp
  # (today this returns NOTHING - verified, 0 files - so the list is a clean delta)
  awk '/^menuitem_t RendererMenu\[\]=/,/^\};/' linuxdoom-1.10/m_menu.c | grep -ci bloom
  # expect: 0 - the Bloom row must live in VideoMenu, which Classic never shows
  ```
  *Breaks when:* a bloom read is added to the software renderer or the present
  path it shares, or the `Bloom` row is added to `RendererDef` — the menu Classic
  actually gets, per
  `M_SetupNextMenu(rendermode==RB_CLASSIC ? &RendererDef : &VideoDef)`.

- **INV-2** — `rb_bloom == 0` restores the current picture **byte for byte, on
  both chains.** No extract, blur or combine is recorded; the combine sits behind
  a branch rather than a multiply by zero; and on the RT chain the tone-map split
  itself is gated, so the un-split pipeline runs unchanged (§4.4). That gate is
  what makes this invariant hold on RT rather than only on raster.
  *Test:* `scripts/ab_capture.sh` at a fixed coordinate with `bloom 0`, against
  the same capture from the commit before L2, plus a same-build control —
  `ab_diff.py <bloom0> <pre-L2> <bloom0-control>` → SIGNAL mean 0.00, max 0.0,
  with the NOISE row quoted beside it. Run for **both** Solid and Ultra RT.
  *Breaks when:* a dispatch is recorded unconditionally; the combine becomes
  `hdr += bloom * intensity` with no guard and a non-finite value reaches
  `bloom[2]`; or the RT tone-map split is made unconditional, at which point the
  fp16 `rtHdrImage` round-trip (§4.6) moves some pixels by 1/255 even with the
  bloom term absent.

- **INV-3** — the HUD, status bar, menu text and weapon sprite are never bloomed
  and never bloomed over. Both combines run before the overlay draw: in raster
  inside the `composite.frag` draw, which the overlay draw and `FlushMenuText()`
  follow within the same swapchain pass; in RT inside `rt_tonemap.comp`, which
  precedes the blit and therefore `RecordRtOverlay`.
  *Test:* the ordering claim is what this invariant really rests on, so check it
  structurally **and** photographically — the strip test alone cannot see the
  weapon sprite or an open menu, both of which sit inside the world area:
  ```
  # 1. structural: the combine must precede the overlay in both chains
  awk '/^extern "C" void RB_Vulkan_Present/,/^}/' linuxdoom-1.10/r_vulkan.cpp \
    | grep -n 'compositePipeline\|overlayPipeline\|FlushMenuText'
  # expect compositePipeline BEFORE overlayPipeline before FlushMenuText
  # rt_tonemap must be dispatched in RecordRtTrace (upstream of the blit), never
  # in RecordRtOverlay. Grep the C++ PIPELINE identifier, not the GLSL filename -
  # 'rt_tonemap' appears in no C++ line, so grepping that can only ever return 0
  # and would pass whether the invariant holds or is breached.
  awk '/^void RecordRtTrace/,/^}/'   linuxdoom-1.10/r_vulkan.cpp | grep -c rtTonemapPipeline  # expect >= 1
  awk '/^void RecordRtOverlay/,/^}/' linuxdoom-1.10/r_vulkan.cpp | grep -c rtTonemapPipeline  # expect 0
  # 2. photographic: the status-bar strip is bit-identical (bottom 19.5%,
  #    the region ab_diff.py crops)
  python3 -c "import numpy,sys;from PIL import Image;\
a,b=(numpy.asarray(Image.open(p).convert('RGB'),dtype=numpy.float32) for p in sys.argv[1:3]);\
h=int(a.shape[0]*0.805);print(numpy.abs(a[h:]-b[h:]).max())" on.png off.png
  # expect 0.0
  # 3. weapon + menu: capture with the weapon drawn and the menu open, and read
  #    ab_diff.py's block map - the weapon and menu blocks must not move
  ```
  *Breaks when:* the combine moves after `FlushMenuText()`, or an extract is
  pointed at the swapchain image after the overlay has been drawn into it.

- **INV-4** — `-rtverify` is unaffected. `RB_RtVerify` binds only
  `RtPipelineForMode(5u)` and dispatches once; it never touches the denoiser,
  the composite, TAAU, or anything this feature adds.
  *Test:* `awk '/^void RB_RtVerify\(\)/,/^\}/' linuxdoom-1.10/r_vulkan.cpp | grep -c 'svgfComposite\|taauPipeline\|svgfTemporal\|svgfAtrous'`
  → `0` (verified against the current tree), and `-rtverify -warp 1 1 -noinput`
  prints PASS with an unchanged rel-MSE before and after L5.
  *Breaks when:* the verify path is ever routed through the display composite —
  at which point its rel-MSE would start moving with a look dial.

- **INV-5** — only genuinely over-white light blooms; paletted, non-emissive art
  does not. The threshold is applied to the pre-tone-map value, and the point at
  which extraction *starts* — `threshold − knee`, not `threshold` (§4.2) — sits at
  or above 1.0, the ceiling of a paletted colour at full sector light.
  *Test:* two parts, the first because the arithmetic can be checked without a
  build and the second because the look cannot:
  ```
  # 1. every preset's ramp start is >= 1.0 (the floor), read off kBloomPresets
  awk '/kBloomPresets\[\]/,/\};/' linuxdoom-1.10/r_vulkan.cpp
  # for each row: threshold - knee >= 1.0   (Low 1.50, Med 1.15, High 1.00)
  # 2. E1M1, flashlight off, bloom 3 vs bloom 0 + a same-build control:
  #    ab_diff.py's block map moves only in blocks holding a lamp, a lit switch
  #    or a liquid, and reads 0.00 mean on plain wall and floor blocks
  #    THAT ARE NOT UNDER A LAMP - the DIRECT term is unbounded by design
  #    (see 4.2), so a heavily point-lit wall is expected to bloom.
  #    Quote the NOISE row beside SIGNAL.
  ```
  *Breaks when:* a preset's `threshold − knee` drops below 1.0 (the failure the
  knee makes easy — a threshold of 1.0 with a knee of 0.5 starts extracting at 0.5
  and blooms a lit wall), or the extract is moved to read the post-tone-map image,
  where §3 decision 1's measured 1.13× compression leaves no threshold that
  catches a lamp without catching a lit wall.

- **INV-6** — Solid keeps the 60 fps floor. With `bloom` at its shipped default,
  Solid at 50 % render scale on the reference RX 6600 stays at or above 60 fps
  on E1M1, and the bloom passes cost ≤ 5 % of present-total.
  *Test:* the `rb_profile` (`\`) `[cpu_profile]` fps line and the per-pass rows,
  `bloom 2` vs `bloom 0`, same map and same render scale (`performance.md`'s
  comparison rule). No expected value — this is the L6 measurement, not a
  recorded one.
  *Breaks when:* the blur is run at full resolution, or the single level grows
  into a pyramid without a re-measure.

- **INV-7** — every bloom pass is timed. A pass with no timestamp is invisible
  to the profiler `performance.md` mandates, and would show up as a mysteriously
  slower neighbour.
  *Test:* after L6 the raster chain writes slots 0–6 and the RT chain 0–9, `nq`
  matches each, and **all ten widening sites moved** (§5) — including both
  `printf` sites, whose omission mislabels the table rather than emptying it. Symbol-anchored, so
  it survives edits above the sites:
  ```
  awk '/^extern "C" void RB_Vulkan_Present/,/^}/' linuxdoom-1.10/r_vulkan.cpp \
    | grep -o 'gpuTimerPool, [0-9]' | sort -u   # today 0-5; after L6 0-6
  awk '/^void RecordRtTrace/,/^}/'   linuxdoom-1.10/r_vulkan.cpp \
    | grep -o 'gpuTimerPool, [0-9]' | sort -u   # today 0-7; after L6 0-9
  grep -c 'nq = g.profRasterFrame ? 7u : 10u;' linuxdoom-1.10/r_vulkan.cpp   # -> 1
  # the standing grep DOOM-0011's ledger row 9.4 prescribes: no stale 8 left
  grep -n '\b8\b' linuxdoom-1.10/r_vulkan.cpp | grep -i 'querycount\|ts\[\|profMs\[\|pi <\|ResetQueryPool'
  # -> empty
  ```
  Today those read `0 1 2 3 4 5` / `0 1 2 3 4 5 6 7`, and the `nq` line is
  `? 6u : 8u` — all verified against the current tree, and all three **must
  change**, which is why the expected values above are the post-L6 ones. (The
  first grep also picks up `vkCmdResetQueryPool(..., 0, 8)`'s `0`; harmless,
  since slot 0 is genuinely written too.)
  *Breaks when:* a dispatch is added between two existing timestamps without
  inserting one — the earlier pass then absorbs its cost silently — or a widening
  site is missed, of which `uint64_t ts[8]` is a stack-buffer overflow and the
  rest are silent.

- **INV-8** — the `-shotverify` / `-shotcompare` golden gate stays
  config-independent. `rb_bloom` is pinned to its shipped default in the
  DOOM-0208 pin block, beside `rb_fog` and the rest.
  *Test:* `awk '/DOOM-0208: pin a canonical/,/^        \}/' linuxdoom-1.10/r_vulkan.cpp | grep -c rb_bloom`
  → `1` (today: `0`, verified).
  *Breaks when:* a new look dial ships unpinned — the exact leak that block's
  own comment records happening with `rt_fog`.

- **INV-9** — the threshold and intensity presets exist in exactly one place and
  both paths read it.
  *Test:* `grep -rn 'kBloomPresets' linuxdoom-1.10/` shows one definition and
  the reads that consume it; no threshold or intensity literal appears in any
  `.comp`, `.frag` or `.glsl` file.
  *Breaks when:* the two paths need different tuning and someone answers that
  with a second table instead of a named per-path scale (§10 Q3).

- **INV-10** — the sky never *generates* bloom. In RT the sky is flagged
  (`rtHdrImage.a == 0.0`) and the extract multiplies it out; in raster the sky needs its own
  argument, **not §4.2's AMBIENT floor**: `composite.frag` writes sky into the
  **DIRECT** target ("flashlight + point lights + sprite/sky colour"), and §4.2 is
  explicit that DIRECT is unbounded. What bounds the sky specifically is that it is
  a palette colour written once, never multiplied by a light term, and scaled only
  by `aoDirect ≤ 1` — so it cannot exceed 1.0 and cannot reach any preset's ramp
  start.
  **This governs generation, not reception, and the two chains differ on
  reception** — `rt_tonemap`'s `a < 0.5` arm adds no bloom to a sky pixel at all,
  whereas the raster combine has no sky test, so a lamp beside a sky edge bleeds
  its halo onto sky pixels there. That asymmetry is deliberate: the raster
  behaviour is the physically right one (light does spill in front of a distant
  backdrop) and the RT one falls out of the sky being display-encoded rather than
  radiance. Neither is worth extra machinery to change.
  *Test:* a sky-facing capture (E1M1's outdoor courtyard), fog **on** (the
  default), `bloom 3` vs `bloom 0` + a control → 0.00 mean delta in sky blocks
  **that have no bright neighbour**. Sky blocks adjacent to a lamp are expected to
  move on the raster chain and not on the RT one.
  *Breaks when:* the RT extract is moved above `svgf_composite`'s `gp.w < 0.0`
  early-out, or a preset's ramp start (`threshold - knee`) drops below 1.0 — at which point the sky
  starts generating its own bloom rather than merely receiving a neighbour's.

### Trust boundary

None crossed, so no invariant above defends one. No file is read, no user input
is parsed beyond one integer that the menu and config layers clamp the same way
they clamp `rb_fog`, and no network, IPC or model output is involved. Recorded
explicitly rather than omitted, because an absent boundary section reads as an
oversight.

## 9. Alternatives considered (and rejected)

- **Extract after the tone-map.** One chain for both paths, no `svgf_composite`
  surgery, no `rtHdrImage`. Rejected by the user (§3 decision 1, which carries the
  measured compression ratio): the effect degrades into "anything pale glows" —
  the complaint the roadmap bullet opens with.
- **Combine into the tone-mapped `rtImage` in LDR** and leave `svgf_composite`
  alone. Saves one pass and 15.8 MiB. Rejected: the same lamp would then glow
  differently in Solid (glow tone-mapped) and Ultra (glow added after), and
  `composite.frag`'s own comment records that using the same operator in both
  "so Solid and Ultra stay tone-matched" is deliberate.
- **Gate bloom on the emissive mask** (DOOM-0084/0302). Rejected by the user
  (§3 decision 2): explosions, muzzle flash and stacked point lights are not
  flagged emitters and would stop glowing, and it threads a mask through two
  more shaders to buy a narrower result.
- **A three-level pyramid** (½ → ¼ → ⅛ with an upsample-accumulate chain), as
  GZDoom uses. Buys a wider, softer falloff that reads better on a large bright
  area. Rejected for v1: it roughly triples the pass count for an effect whose
  budget is not yet measured, and the roadmap bullet's own framing is
  cheapest-wins-first. If §10 Q1's look call says the halo is too tight, adding
  levels is the first thing to try, and it is additive to this design rather
  than a redesign of it.
- **A quarter-res extract** instead of half. Cuts the extract's output pixels
  4×. Rejected because thresholding *before* averaging is what stops thin
  emitters vanishing (§4.2), and at 100 % render scale a quarter-res extract needs
  16 point taps per output pixel to do that — trading output pixels for fetches at
  no clear net gain. **This is a 100 %-scale argument**: at the shipped 50 %
  default the half-res extract is already 1:1, so a quarter-res one would need
  only the 2×2 gather that the half-res extract needs at 100 %. It is listed as a
  §6 lever because at that point the trade is being made against a measured number
  rather than a guess.
- **`B10G11R11_UFLOAT_PACK32` for the bloom and HDR targets.** Halves their
  bandwidth and memory; bloom values are non-negative, so the lost sign costs
  nothing. Rejected for v1 because `rtHdrImage` needs the alpha channel for the
  sky flag, and using one format for the bloom chain and another for `rtHdrImage`
  buys ~3 MiB for a second format to reason about. It is §6's third lever if the
  budget is missed, and the sky flag would then need another home (a sentinel
  radiance value, since the branch only needs one bit).
- **A debug key** for bloom, matching `]` `[` `'` `;`. Rejected: the A/B harness
  drives effects through a temp config rather than the keyboard (`ab_capture.sh`
  copies and rewrites a config, and cannot inject keystrokes under Wayland
  anyway), and the menu row is the player's control. Unused *menu* hotkeys do remain (`a`, `e`, `h`, `j`, `n`), but that is a
  different namespace from the punctuation debug keys, so scarcity is neither the
  reason nor the counter-argument.
- **Bloom on the path-tracer debug views** (`rb_rtdebug` 1–4). Rejected: those
  are diagnostics behind the Debug Views toggle, mode 4 tone-maps inside
  `pathtrace.comp` rather than in the composite, and adding a third hook site
  for a view that exists to show raw estimator output is a cost with no reader.

## 10. Open questions

Four questions: two look calls, one measurement-then-judgement, one mechanical
check. **None blocks drafting, but two gate build steps** — Q3 blocks L5 and Q4
blocks L1's completion. Each names who answers it, when, and what it blocks.

- **Q1 — is the halo the right size and strength?** The σ = 2.0-at-quarter-res
  kernel gives a ±16 display-pixel reach at 1080p (§4.3), and the intensity
  presets are a starting position. **User**, on hardware, at the L6 gate.
  *Blocks:* the ROADMAP flip only. If the halo reads too tight, the 13-tap kernel
  then §9's pyramid are the levers; if too strong, the intensity presets drop
  before the threshold does.
- **Q2 — does a flashlight on a white wall bloom, and does that read badly?**
  §3 decision 2 accepted the possibility. **User**, same gate. *Blocks:* the
  ROADMAP flip only. The fix if it does is raising the presets' ramp start above
  1.0, not an emissive gate — that was weighed and rejected.
- **Q3 — the two chains' thresholds are not in the same units, and one of them
  moves with the Brightness slider.** This is sharper than "may need different
  intensities". §4.2 thresholds "the value the path is about to tone-map", which on
  RT is `L * exposureEv(EV)` — scaled by the player's `rb_exposure` — and on raster
  is raw `hdr`, because the raster path applies no exposure at all. So one shared
  preset table (INV-9) meets a user-scaled quantity on one chain and an unscaled
  one on the other, and a player dragging Brightness in Ultra silently changes what
  counts as a light source. Not a defect in the design — it follows from the raster
  path genuinely having no exposure control — but it must be a recorded decision
  rather than a surprise.
  **Claude to measure** (capture one coordinate in both chains at several
  `rb_exposure` values, report which blocks bloom), **user to judge**.
  *Blocks:* **L5**, not L6 — the answer decides whether the RT threshold is
  defined pre- or post-exposure, and that is the line L5 writes. Three candidate
  answers, cheapest first: accept it and document it; make the RT threshold
  EV-relative (divide the preset by `exposureEv(EV)` so the dial means the same
  thing at every brightness); or give the table one named per-chain scale constant.
  Never a second table (INV-9).
- **Q4 — does a 21st `VideoMenu` row still fit?** `VideoMenu` has 20 rows today
  (counted) and DOOM-0206's contract requires the menu stay HUD-safe and scroll if
  it outgrows the screen. **Claude to check** at L1, mechanically — open the menu
  in all three tiers and look. *Blocks:* L1's completion. If it does not fit, that
  is DOOM-0206's scroll mechanism to exercise, not a reason to drop the row.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 Classic untouched | the two INV-1 greps, run at L6 |
| INV-2 `bloom 0` byte-identical, both chains | `ab_capture.sh` ×3 + `ab_diff.py`, both chains, at L3 and L5 |
| INV-3 HUD/weapon/menu never bloomed | INV-3's two structural greps + the strip compare + the weapon/menu block map |
| INV-4 `-rtverify` unaffected | the INV-4 grep + a real `-rtverify` run at L4 and L5 |
| INV-5 paletted art does not bloom | the `kBloomPresets` floor read + `ab_diff.py` block map, E1M1, at L3 and L5 |
| INV-6 60 fps floor in Solid | `rb_profile` measurement at L6 |
| INV-7 every pass timed, all 10 sites moved | INV-7's slot greps + §5's declaration-anchored standing grep at L6 |
| The profiler's LABELS still match its buckets | **nothing** mechanical — the print is prose; read the `[raster_profile]`/`[rt_profile]` format strings against §5 sites 9–10 by hand at L6 |
| INV-8 golden gate pinned | the INV-8 grep at L1 |
| INV-9 one preset table, no shader literals | the INV-9 grep at L6 |
| INV-10 sky never blooms | sky-facing `ab_diff.py` capture at L5, **fog on** |
| The fogged sky keeps its encode (§4.6) | L4's sky-facing, fog-on capture arm — the one thing that catches a deleted `toneEncode` |
| The extract and the composite compute the same `hdr` | L2's byte-identical refactor gate — a `scene_recombine.glsl` that dropped the AO blur fails it |
| The halo reads as light, not haze | **nothing** — a human look call (§10 Q1); no automated test can judge it |
| Solid and Ultra agree on what glows | **nothing** mechanical — §10 Q3 is a measurement plus a judgement |
| The preset values are the right ones | **nothing** — §10 Q1/Q2; the presets are tuning, not a contract |

**Four `nothing` rows out of sixteen** (counted from the table above, not carried
forward). Three are the same class — this feature's correctness is mechanically
checkable and its *look* is not — and that is the honest error budget here, and why
L6 is a human gate rather than a green test run.

The fourth is a different and more fixable thing: the profiler's *labels* have no
mechanical check, because the print is a `printf` format string and nothing
compares it to the bucket it prints. That row is a candidate for becoming
mechanical later (a test asserting the label count matches `nq`), which is the
direction this table is supposed to move in.

Two rows are worth noting as *newly* mechanical: the fogged-sky encode and the
shared-`hdr` premise were both invisible in the first draft of this table, and both
are now caught by a build-order gate rather than by review.

## 12. Cross-doc impact

- `CHANGELOG.md` — an `Added` entry, at L6 and not before.
- `ROADMAP.md` — DOOM-0331 flips to 🚧 at L1 and ✅ at L6.
- `docs/standards/renderer.md` — no change. The push-constant lane table is
  untouched (§5: no new megakernel lane), and the shader list there is
  descriptive prose rather than an inventory.
- `docs/standards/performance.md` — no change. §6 uses the existing floor, the
  existing profiler and the existing comparison rule.
- `CLAUDE.md` — no change. The tier table already says effects belong to the
  view, not the tier, which is what §2 applies.
- `scripts/ab_capture.sh` — the tier-conditional HD check (§7), needed before
  the Solid arm of any look A/B can run.
- **The `-shotcompare` golden image — a re-bless is owed, and it is not this
  feature's to grant.** Shipping `bloom = 2` by default (§3 decision 4) and
  pinning it in the DOOM-0208 block (INV-8) means every `-shotcompare` run differs
  from the stored golden until that golden is re-captured. This adds to an existing
  debt rather than creating one: the pin block's own comment records the golden
  already predating `rt_fog`, and **DOOM-0202 explicitly holds the golden stale
  until the goo glow is signed off on hardware.** So L6 records that the re-bless
  is owed and leaves it to DOOM-0202; it must not re-bless as a side effect of
  shipping bloom, which would bake an unsigned-off goo look into the reference
  image.
- `docs/specs/DOOM-0011-fix-ledger.md` — no change, but read row 9.4 before doing
  L6: it is the record of this project getting a profiler widening wrong, and §5's
  eight-site table exists because of it.

## 13. Cold-eyes loop log

| Loop | Date | Lanes | CRIT | HIGH | MED | LOW | Outcome |
|------|------|-------|------|------|-----|-----|---------|
| 1 | 2026-08-07 | 2 | 5 | 8 | 7 | 11 | All 31 verified against the tree, **0 dismissed**, all 31 fixed. Dimensions: dim 4×7, dim 5×7, dim 15×6, dim 2×4, dim 6×4, dim 13×2, dim 1×1, dim 9×1, dim 12×1. Doc 754 → 1132 lines. Not converged — re-dispatching. |
| 2 | 2026-08-07 | 2 | 3 | 7 | 13 | 9 | All 32 verified, **0 dismissed**, all 32 fixed. Origin split: 5 of the 10 CRIT+HIGH were **fix collateral from loop 1**, 5 were draft defects. Dimensions: dim 2×11, dim 5×8, dim 4×7, dim 15×4, dim 6×4, dim 7×3, dim 11×1. Doc 1132 → 1331 lines. |
| 3 | 2026-08-07 | 2 | 2 | 4 | 13 | 12 | **Converged by cap.** All 31 verified, 0 dismissed; **21 fixed, 10 deferred** — every deferred item LOW and editorial, none build-changing (tail below). Dimensions: dim 4×9, dim 5×8, dim 2×7, dim 6×6, dim 10×3, dim 7×1. Doc 1331 → 1461 lines. |
| 3-sweep | 2026-08-07 | 0 | 0 | 0 | 1 | 0 | **No reviewer dispatched** — the `/apply-fixes` post-batch sweep over loop 3's own fixes. Caught one: adding a §11 row left "Three `nothing` rows out of fifteen" stale at 4-of-16. Recorded because a row nobody writes is a row nobody wrote. |

### Deferred tail — verified, unfixed, LOW only

These are verified and unfixed. **Do not re-review to rediscover them** — a fresh
loop costs a full multi-agent dispatch to regenerate what is already written here.
Fold them in directly. None changes what gets built; all are wording or naming.

1. §3 says the operator "compresses hard above its 0.76 knee". `startCompression`
   is compared against the **post-offset** peak, so in *input* terms the knee sits
   at 0.80. Harmless for the presets (all ≥ 1.35) but imprecise.
2. Same paragraph: 0.9833 / 0.8691 are the **raster** path's screen values. The RT
   path sRGB-encodes afterwards, which compresses the ratio further. The argument
   holds; the wording overreaches.
3. `bloom[0..2]` and `bloomImage[0..2]` are used interchangeably for the same three
   targets. Pick one.
4. §2 says modes 1–4 "tone-map inside `pathtrace.comp`"; §9 says "mode 4" does.
   One of the two scopes is wrong.
5. §10 Q4 says "**Claude to check** … mechanically — open the menu … and look".
   Mechanical and look are different gates, and §9 records that the harness cannot
   inject keystrokes.
6. §12 states DOOM-0202 "explicitly holds the golden stale until the goo glow is
   signed off". The pin block's own comment says only that re-blessing is owed.
   Soften to match the evidence.
7. §7 tells a caller how to select **Solid** via `DOOMCFG=`; there is no equivalent
   line for the Ultra arm, so an Ultra capture inherits whatever tier the user last
   played in.
8. INV-5's floor read anchors on `awk '/kBloomPresets\[\]/,/\};/'`; the declaration
   form is never shown, so the anchor may not match (`kBloomPresets[4]`,
   `static const struct … =`). Pin the declaration text.
9. §7 L2 says the refactor is "provably a no-op". Bit-identity under `glslc -O` is
   an empirical result that L2's own gate measures — say "asserted by L2's gate".
10. §4.2 still describes the extract's coordinate in one place in fragment terms;
    it is compute throughout.

**Loop 1 headline** (the five that would have shipped bugs): the profiler
widening named 2 sites where 8 exist, one of them a `uint64_t ts[8]` stack array
`vkGetQueryPoolResults` would have overflowed — a lesson DOOM-0011's fix ledger
row 9.4 already records this project learning; the soft knee made the
"threshold floor is 1.0" claim false, so INV-5 and INV-10 were unsatisfiable and
their own tests would have failed a faithful build; the RT sky branch **does**
call `toneEncode` when fog is on (the default), so §4.6's "the tone-map moves
out" would have shipped a linear un-encoded fogged sky; §2's tier table had no
"Solid with RT on" row although `rtActive` carries no `rendermode` term at all;
and the RT profiler append would have let `profMs[7]` silently absorb the new
passes. Two design changes followed: the RT tone-map split is now **gated** on
`rb_bloom > 0` (so "Off costs nothing" is true on both chains, not just raster),
and L4's byte-identity gate became a stated tolerance, because `rtHdrImage` is
fp16 and no correct implementation could have passed byte-identity through it.

**Loop 2 headline.** The worst finding in the whole review landed here, and it was
a draft defect loop 1 had walked straight past: the bright pass thresholded
**Rec.709 luminance** while every threshold, floor and preset was stated in
**linear magnitude**. Those agree only for greys, so at the High preset a red
fireball or pure-red lava at 4× white — luminance 0.850 — extracted **zero**,
a blue emitter 0.289, while a white wall at 1.0 sat exactly on the blooming
threshold. The feature would have shipped glowing pale walls and not lava, which is
the roadmap bullet's own complaint with the causality reversed. Fixed by
thresholding the **max channel**, which is also the quantity
`pbrNeutralToneMapping` keys its own compression on, so threshold and knee are
finally in the same units. Two more of the same class: `toneEncode()` *contains*
the exposure and is local to `svgf_composite.comp`, so §4.6's "it is already
shared, just call it" was false and would have double-exposed the frame (now split
into an exposure-free `toneMapEncode()` in the shared include); and the floor was
claimed over a **sum** (`direct + ambient`) while only bounding `ambient`, so a
lamp-lit wall could bloom and INV-5's test would have failed a faithful build —
now stated as the accepted, intended case it is. Five of loop 2's ten CRIT+HIGH
were collateral from loop 1's own fixes, the widened profiler readback being the
sharpest: it would have returned `VK_NOT_READY` and dropped the whole profiler
print on exactly the `bloom 0` arm §6's measurement compares against.
