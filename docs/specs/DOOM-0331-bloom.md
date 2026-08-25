# DOOM-0331 — Bloom: the shared core and the rasterised view

**Status:** spec draft (2026-08-12).
**Kind:** feature.
**Source:** ROADMAP DOOM-0331 (`upstream-review-2026-08-05`, GZDoom
`wadsrc/static/shaders/pp`). Scope calls taken with the user 2026-08-07 and
2026-08-12 — see §3.
**Split from:** the 1496-line umbrella of this same id, on 2026-08-12. This part
keeps the id and the path; the ray-traced chain became DOOM-0345. See §2's split
note and §13's `0-split` row.
**Blocker for:** DOOM-0345 — that spec reuses this one's blur shader, bloom
targets and preset table, and adds only what the RT chain needs on top.

**Layman:** Lamps, muzzle flashes, glowing goo and lava will bleed a little
light into the air around them, the way bright things do in a photo — so a lamp
stops looking like a pale wall and starts looking like a lamp.

**Depends on:**

- **DOOM-0170 L2a/L2b** — the raster path's HDR seam. The world is drawn into two
  16-bit float targets (`kSceneFormat`, `VK_FORMAT_R16G16B16A16_SFLOAT`) —
  AMBIENT (`sceneImage`) and DIRECT (`sceneDirImage`) — and `composite.frag`
  recombines and tone-maps them. That recombined value is where this feature
  taps the picture.
- **DOOM-0205 / DOOM-0206** — the effects toggles and the consolidated Video menu
  they now live in. DOOM-0205 shipped them as a Render Effects submenu; DOOM-0206
  folded that into one `VideoDef` with an `— Effects —` group, which is where the
  dial goes (§4.5).
- **DOOM-0084 / DOOM-0302 / DOOM-0183** — the emitters that make bloom worth
  having. Nothing here reads their masks (see §3, decision 2); they are the
  reason there is something above white to extract.

**Delivers / subsumes:** nothing. No existing roadmap item is closed by this.

**Defers (explicitly NOT in this build):**

- **The ray-traced view.** DOOM-0345. This spec ships bloom in the rasterised
  view only; the dial, the presets, the blur and the bloom targets it builds are
  what DOOM-0345 then hooks the RT chain onto.
- **Auto-exposure / eye adaptation** — no item filed; the manual Brightness
  slider (`rb_exposure`, DOOM-0096) stays the only exposure control.
- **Lens dirt, streaks, anamorphic flares** — no item filed. This is a plain
  radially-symmetric glow.
- **FXAA and depth of field** — the other two post-process passes GZDoom ships
  and this engine does not. Neither is filed; neither is in scope.

**Scope:** Solid and Ultra whenever the **raster chain draws the frame** — i.e.
`!rtActive`, which is *wider* than `rb_rtdebug == 0` and must be gated as such
(§2). Classic is untouched (INV-1). Every surface is byte-identical when the dial is
Off (INV-2), and the HUD is never bloomed and never bloomed over (INV-3).

---

## Contents

- §1 Goal — §2 Where this sits (incl. the split note and the invariant map) —
  §3 Scope decisions — §4 Design (4.1 where it hooks · 4.2 the bright pass ·
  4.3 the blur · 4.4 the combine · 4.5 the dial) — §5 Data & resources —
  §6 Performance budget — §7 Build order — §8 Invariants —
  §9 Alternatives considered — §10 Open questions — §11 What checks this —
  §12 Cross-doc impact — §13 Cold-eyes loop log

---

## 1. Goal

A light source looks like a light source. After this ships, anything genuinely
brighter than white — a ceiling lamp, a lit switch, a muzzle flash, a fireball,
glowing nukage, lava — bleeds a soft halo into the pixels around it, in Solid
and in Ultra, in the **rasterised** view. Ordinary art does not: a plain wall at
full sector light is not a light source and does not glow.

One dial in the Video menu's Effects group (`Bloom: Off / Low / Medium / High`)
scales it, and Off restores the current picture exactly. The same dial, the same
preset table and the same blur then serve the ray-traced view when DOOM-0345
lands — this spec is deliberately the half that owns everything shared.

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0331? |
|-----------------|----------|-----------------------|
| Classic | paletted software renderer | **No** (INV-1) |
| Solid, RT off (`rb_rtdebug == 0`) | raster stack | **Yes** — §4.1 |
| Ultra, RT off (`rb_rtdebug == 0`) | raster stack | **Yes** — same chain |
| Solid, RT on (`rb_rtdebug == 6`) | path tracer + denoiser | **No** — DOOM-0345 |
| Ultra, RT on (`rb_rtdebug == 6`) | path tracer + denoiser | **No** — DOOM-0345 |
| Path-tracer debug views (`rb_rtdebug` 1–4) | path tracer | **No** — DOOM-0345 §2 |

**The gate is which chain drew the frame, never the tier label**, and rows 2–5
are what prove it: `rtActive` in `RB_Vulkan_Present` is `rb_rtdebug &&
g.rtEnabled && g.tlas && g.rtModule && g.haveCamera && g.vbuf && g.atlasReady` —
**no `rendermode` term at all**, verified. Solid with the Ray Tracing row on runs
the RT chain, exactly as `CLAUDE.md` says ("Each of Solid and Ultra has both a
rasterised and a ray-traced view"). An implementer who gates this feature's hook
on `rendermode` ships something that appears in the wrong half of its
configurations — this spec's hook belongs to `!rtActive`, and DOOM-0345's to
`rtActive && rb_rtdebug == 6`.

**Gate on `!rtActive`, NOT on `rb_rtdebug == 0`, and the difference is most of
the userbase.** `rb_rtdebug == 0` is a strict *subset* of `!rtActive`: the raster
chain also draws whenever `g.rtEnabled`, `g.tlas`, `g.rtModule`, `g.haveCamera`,
`g.vbuf` or `g.atlasReady` is false — which is every machine without working ray
tracing, and every frame before the TLAS is built. On such a machine
`rb_rtdebug` stays at its persisted default of **6** (`m_misc.c`:
`{"rt_view",&rb_rtdebug, 6}`) while the raster chain draws every frame. Gate the
three dispatches and the combine on `rb_rtdebug == 0` and bloom is silently
absent on all of them at default config — and INV-2 still passes, because
everything is off. The table's `rb_rtdebug` column above is illustration; the
gate is `!rtActive`.

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

### The split, and where the umbrella's invariants went

DOOM-0331 was one 1496-line spec covering both chains. It converged by cap at
three cold-eyes loops without going quiet, and five of loop 2's ten CRIT+HIGH
findings were collateral from loop 1's own fixes — the signature of a document
larger than the review's design point (`spec-format.md` §5.4). It was split along
the §4 design seam on 2026-08-12: everything shared plus the raster chain stayed
here, and the RT chain — which is where all of the structural risk lives — became
**DOOM-0345**.

Each part renumbers its invariants from 1, per `/write-spec`'s splitting rule.
The umbrella's numbering is dead; this table is what keeps its citations
findable. Neither part inherits the other's review history — both run the gate
from loop 1 on their own bytes.

| Umbrella INV | Was | Now |
|---|---|---|
| INV-1 | Classic byte-identical | **DOOM-0331 INV-1** |
| INV-2 | `bloom 0` byte-identical, both chains | **DOOM-0331 INV-2** (raster arm) + **DOOM-0345 INV-1** (RT arm) |
| INV-3 | HUD never bloomed, both chains | **DOOM-0331 INV-3** (raster arm) + **DOOM-0345 INV-2** (RT arm) |
| INV-4 | `-rtverify` unaffected | **DOOM-0345 INV-3** |
| INV-5 | only over-white light blooms | **DOOM-0331 INV-4** (owns the preset floor) |
| INV-6 | Solid keeps 60 fps | **DOOM-0331 INV-5** |
| INV-7 | every bloom pass timed | **DOOM-0331 INV-6** (raster slots) + **DOOM-0345 INV-7** (RT slots + the 8→10 pool widening) |
| INV-8 | `-shotverify` pin | **DOOM-0331 INV-7** |
| INV-9 | one preset table | **DOOM-0331 INV-8** (defines it) + **DOOM-0345 INV-8** (must not add a second) |
| INV-10 | sky never generates bloom | **DOOM-0331 INV-9** (raster arm) + **DOOM-0345 INV-5** (RT arm) |
| — | *new* | **DOOM-0345 INV-4** (the fogged sky keeps its encode) and **INV-6** (exposure applied exactly once) — both promoted from What-checks-this rows, because §3 decision 5 gave them a way to fail; and **DOOM-0345 INV-9** (Ultra's ≤ 5 % bound), which the umbrella carried as a §6 budget rather than an invariant |

## 3. Scope decisions (agreed with the user)

Five preference calls: four taken with the user on 2026-08-07, the fifth on
2026-08-12. They are recorded because each has a cheaper wrong answer that will
otherwise be re-proposed.

1. **The bright pass reads the picture BEFORE the tone-map, not after.** This is
   the decision the whole design turns on. The Khronos PBR-Neutral operator both
   paths use compresses hard above its knee, so a lamp at 4.0 linear and a
   white wall at 1.0 linear arrive at the screen as **0.9833 and 0.8691** — a 4×
   brightness difference squashed to **1.13×**. Extracting after the tone-map
   would therefore bloom bright walls almost as readily as lamps, which is
   precisely the complaint on the roadmap bullet ("a lamp and a white wall can
   end up looking alike"). Extracting before it separates them completely.

   Those two figures are the shipped operator's own output, not an estimate,
   and they are the **raster** path's screen values — `composite.frag` applies
   the operator and stops there. The RT path sRGB-encodes afterwards, which
   compresses the ratio further still; the argument holds a fortiori and
   DOOM-0345 does not restate it.

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

   The knee: `startCompression` is `0.8 - 0.04 = 0.76`, compared against the
   **post-offset** peak — and the offset is keyed on the **min** channel
   (`float x = min(color.r, min(color.g, color.b)); float offset = x < 0.08 ?
   x - 6.25*x*x : 0.04;`). So compression begins at **0.76 in input terms for a
   fully saturated colour** (min channel 0, offset 0) and at **0.80 for grey**
   (offset 0.04), sliding between the two. Worth stating precisely because §4.2
   is built around saturated emitters — fireballs, lava, nukage — where the
   0.04 shift does not apply at all. Harmless for every preset here regardless
   (all thresholds ≥ 1.35).

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
   pinned in the `-shotverify` block (INV-7).
5. **What counts as a light source does not move with the Brightness slider.**
   Taken with the user 2026-08-12, closing the umbrella's §10 Q3, which had been
   blocking the RT build step. The threshold is defined in **scene radiance
   units on both chains** — the preset number means one fixed thing, and
   `rb_exposure` only makes the whole picture lighter or darker, never changes
   which surfaces qualify.

   On this chain that is free: `composite.frag` computes
   `hdr = direct * aoDirect + ambient * ao` and hands it straight to
   `pbrNeutralToneMapping` with **no exposure multiply anywhere** (verified). So
   the raster threshold is already in scene units and this decision costs it
   nothing. What it costs is on the RT chain, and DOOM-0345 §3 pays it.

   **The accepted trade, stated once here because it is the shared table's
   property:** thresholding in scene units means the threshold and the tone-map
   knee are *not* in the same units on a chain that applies exposure. Those two
   properties are mutually exclusive — commensurability with the knee is exactly
   what would make the threshold move with the slider — and the user chose
   stability. The residual is a *look* question, not a correctness one: whether
   one shared preset table reads the same on both chains once each is on screen.
   §10 Q3 carries it, and its answer is a named per-chain scale constant if
   needed, never a second table (INV-8).

## 4. Design

Four stages: **extract → blur → blur → combine**. The extract's source is the
only thing DOOM-0345 changes; the blur, the presets and the combine's arithmetic
are shared and defined here.

### 4.1 Where it hooks

The rasterised view — every frame where `!rtActive` (§2), in **either** tier. The
frame today is:
shadow pass → scene pass (MRT: AMBIENT + DIRECT) → SSAO pass (half-res) →
swapchain pass, inside which `composite.frag` tone-maps to the screen and the 2D
overlay is drawn on top. Bloom inserts three compute dispatches between the SSAO
pass and the swapchain pass — a legal place because dispatches cannot be recorded
inside a render pass, and the SSAO pass has ended by then:

```
shadow pass
scene pass          -> AMBIENT + DIRECT   (kSceneFormat, HDR)
SSAO pass           -> aoImage            (half-res R8)
bloom_extract_raster  (dispatch, half-res)              -> bloomImage[0]
bloom_blur            (dispatch, quarter-res, dir = +X) -> bloomImage[1]
bloom_blur            (dispatch, quarter-res, dir = +Y) -> bloomImage[2]
swapchain pass
  composite.frag    -> hdr += bloomImage[2] * intensity, then tone-map   <-- combine
  overlay draw      -> HUD / menu, keyed, on top
  FlushMenuText()
```

The combine costs no extra pass: `composite.frag` already samples three
textures and already holds the recombined HDR value, so the bloom is one more
fetch and one add before the operator it already applies. The combine therefore
lands **after the scene is fully shaded and before the HUD exists** — which is
what the roadmap bullet asked for, and what INV-3 locks.

### 4.2 The bright pass — what crosses the threshold

The threshold is applied to **the value the path is about to tone-map, in scene
radiance units** (§3 decision 5). On this chain that is `composite.frag`'s `hdr`,
i.e. `direct * aoDirect + ambient * ao`, with no exposure multiply — the raster
path applies none.

Extraction, per source texel, with a soft knee so a surface crossing the
threshold ramps in rather than pops. `threshold` and `knee` both come from
`kBloomPresets` (§4.5) — neither is a shader literal (INV-8):

```glsl
// bloom_extract_raster.comp — soft-knee bright pass.
// peak, NOT Rec.709 luminance — see the note below; this is the same quantity
// pbrNeutralToneMapping keys its own compression on.
float  peak   = max(c.r, max(c.g, c.b));
float  soft   = clamp(peak - threshold + knee, 0.0, 2.0 * knee);
soft          = soft * soft / (4.0 * knee + 1e-4);
float  weight = max(soft, peak - threshold) / max(peak, 1e-4);
vec3   bright = c * weight;             // 0 below threshold-knee; above it, c scaled so peak -> peak-threshold
```

**This block is the shared definition.** DOOM-0345's extract is the same
arithmetic over a different source and must not restate or re-derive it.

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
units on this chain, which is the whole point of §4.2.

Two further properties this shape has to keep:

- **The floor is `threshold − knee ≥ 1.0`, NOT `threshold ≥ 1.0`.** This is the
  easy thing to get wrong, and getting it wrong silently defeats the feature.
  The knee makes extraction begin at `threshold − knee`, not at `threshold`: with
  a threshold of 1.00 and a knee of 0.5, a wall at 0.9 linear gets
  `soft = clamp(0.9 − 1.0 + 0.5, 0, 1.0) = 0.4`, hence
  `weight = 0.4²/(4×0.5) / 0.9 = 0.089` — it blooms, faintly, and INV-4's own
  test would fail on a faithful implementation. **The quantity that must clear
  the art's brightness is the point where the ramp *starts*, not the threshold** —
  and what it must clear is measured, not assumed; see the next paragraph.
  §4.5's presets are a provisional answer to it.

  **The AMBIENT ceiling is NOT 1.0, and this is the floor argument's weakest
  point.** `mesh.frag` writes `ambient = albedo * sect` and then, for any
  subsector with a GI probe, **adds** `GI_BOUNCE_STRENGTH * albedo *
  giIrradiance(...)` with `GI_BOUNCE_STRENGTH = 1.0`. Sector light alone tops out
  at 1.0; sector light plus baked bounce does not, and nothing clamps the sum. So
  a plain wall in a strongly bounce-lit room can exceed 1.0 in AMBIENT alone and
  cross a preset's ramp start — the "pale walls glow" failure this whole feature
  exists to avoid.

  **So the floor is a measured gate, not an arithmetic guarantee.** L3 captures
  the AMBIENT target's maximum over a non-emissive wall on E1M1 and the presets'
  ramp start must sit above it; if it does not, the presets rise. §10 Q5 carries
  the measurement and the presets stay provisional until it is taken. Stating
  otherwise — as "paletted art tops out at 1.0" did — makes INV-4 read as proven
  when it is not.

  **What the floor does NOT bound either: DIRECT.** The thresholded value is
  `direct * aoDirect + ambient * ao`, a sum, and DIRECT (point lights, flashlight,
  muzzle) has no ceiling — a wall with several lamps trained on it genuinely
  exceeds 1.0 and *will* bloom. That is intended, not a leak: §3 decision 2
  chose brightness over an emissive flag precisely so stacked point lights and
  explosions glow. So the floor's guarantee is narrower than it first reads —
  **ordinary art at ordinary sector light cannot bloom; art blasted by direct
  light can** — and it is the same accepted case as §10 Q2's flashlight. INV-4's
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
- **The sky contributes nothing here.** In raster the sky reaches the composite at
  paletted magnitude and so cannot *exceed* any preset's ramp start
  (`threshold − knee`, floored at 1.0) — at the High preset it sits exactly on it
  and extracts exactly zero, per the worked table above. INV-9 states the bound
  and its mechanism, which the AMBIENT ceiling above does **not** supply: the sky
  rides in the *unbounded* DIRECT target.

The extract runs at **half display resolution**, and each output texel gathers
the source texels under it by **thresholding each one first and averaging
after**. That order matters: averaging first would dilute a single bright texel
below the threshold and lose thin emitters (a distant lamp, a switch), which a
bilinear fetch cannot avoid. Thresholding first keeps a one-texel emitter's
excess energy at its fair fraction, which is correct and does not flicker as the
camera moves.

**How many source texels that is depends on the render scale, and the shipped
default makes it one.** The world fills only the `[0, uvScale]` corner of the
raster scene targets, so the ratio between source and output is
`renderScale / 50 %`:

| `render_scale` | source region @ 1920×1080 | extract output | source texels per output texel |
|---|---|---|---|
| 100 % | 1920×1080 | 960×540 | 2×2 = 4 |
| 75 % | 1440×810 | 960×540 | 1.5×1.5 |
| 67 % | 1286×723 | 960×540 | ~1.34×1.34 |
| **50 % (default)** | **960×540** | **960×540** | **1×1 — no averaging at all** |

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

**The extract writes the whole half-res target, and the bloom targets are mapped
over the whole frame** — the same convention `aoImage` already uses, and the
reason nothing has to be reallocated when the render-scale menu changes
mid-frame:

- The extract covers **every** texel of `bloomImage[0]`, i.e.
  `[0, dispW/2) × [0, dispH/2)`, with no unwritten region. It reaches its source
  by scaling *inward* — it is a **compute** shader throughout, with no `vUV`
  varying, so it derives its own normalised coordinate from
  `gl_GlobalInvocationID` and samples the scene at `uv * uvScale`.
- The blur therefore reads a fully-written target and needs no corner clamp, and
  the combine samples bloom in **plain full-frame UV** — which is what
  `composite.frag`'s existing `vUV` already is.
- The bloom samplers use **`LINEAR` filtering** and **`CLAMP_TO_EDGE`
  addressing**, both pinned because both are silently wrong-able.
  `CLAMP_TO_EDGE` (as the scene sampler already does) stops a blur tap landing
  outside the image at the frame border from reading black and darkening the halo
  there. `LINEAR` matters most at the **combine**, which upsamples a quarter-res
  target to full display resolution: a `NEAREST` sampler gives every halo
  4×4-pixel blocky edges while INV-2, INV-4, INV-5 and INV-9 all still pass,
  leaving only §10 Q1's human look to catch it.

### 4.3 The blur

Two dispatches at **quarter** display resolution, the same
`bloom_blur.comp` twice with the direction in a push constant. **DOOM-0345 reuses
this shader unchanged** — it is chain-agnostic, reading and writing only bloom
targets.

The first pass reads the half-res `bloomImage[0]` with a bilinear sampler, so the
½ → ¼ downsample is free (no second threshold is applied downstream, so bilinear
cannot push a value back under one — though it *can* attenuate a lone bright
texel; see the aliasing note below).

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
halo is subtly oval.

**`dir.xy` carries that per-pass factor; `srcTexelSize` never does.** The shader
steps by `offset * pc.dir * pc.srcTexelSize`, where `srcTexelSize` is exactly
`1 / size-of-the-image-being-read` and the caller passes `dir = (2, 0)` for
pass 1 and `dir = (0, 1)` for pass 2. Said explicitly because a unit `dir`
alongside a bare `1/size` is the natural reading of §5's push table, and it
builds pass 1 at half its intended reach — the oval halo above.

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

One additive term, applied to the pre-tone-map value, inside the shader that
already computes it:

```glsl
// composite.frag — fragment, bloomTex at set 0 binding 3
if (pc.bloomIntensity > 0.0)
    hdr += texture(bloomTex, vUV).rgb * pc.bloomIntensity;
```

**A branch, not a multiply by zero.** `hdr + bloom * 0.0` is exact for finite
`bloom`, but a NaN or Inf that reached `bloomImage[2]` would survive the multiply
and poison the frame — and the **RT** chain already guards against non-finite
radiance (`if (any(isnan(L)) || any(isinf(L))) L = vec3(0.0)`, in
`svgf_composite.comp`), which says such values do occur in this engine. The
raster chain has no such guard anywhere, which is why the next paragraph puts one
in the extract. With the branch, `rb_bloom == 0` cannot reach the add at all, which is
what makes INV-2 a structural guarantee rather than a floating-point argument.

**And the dial being ON needs its own guard, which the branch does not provide.**
One NaN texel in the scene targets, thresholded and then blurred, spreads over a
±16-pixel halo. The extract therefore clamps as its first act: `if (any(isnan(c)) || any(isinf(c)))
c = vec3(0.0);`, before the threshold. Cheaper there than anywhere downstream,
because it is the only pass that reads unguarded values. **DOOM-0345's extract
carries the same clamp**, for the same reason.

**Off costs nothing on this chain**: with the dial Off none of the three
dispatches is recorded, the combine is behind a branch, and no image this feature
adds is written. That is what makes §6's Off row genuinely zero and INV-2
byte-exact. (The RT chain cannot get this for free, because DOOM-0345 has to move
a tone-map to make room; gating that split is DOOM-0345's problem and its INV-1.)

### 4.5 The dial

One integer, four presets, one menu row, following the `rb_fog` precedent
exactly. **This is the shared control surface — DOOM-0345 adds no dial, no
config key and no menu row.**

| Name | Where | Value |
|---|---|---|
| `rb_bloom` | `r_vulkan.cpp`, `extern "C"` alongside `rb_fog` | 0 Off / 1 Low / 2 Medium / 3 High |
| `rt_bloom` | `m_misc.c` config table, default `2` | persisted to `~/.doomrc`. The `rt_` prefix is the convention every render-effect toggle uses (`rt_fog`, `rt_wet`, `rt_filth`, `rt_detile`, `rt_profile`) — and "follow the `rb_fog` precedent exactly" means this, since `rb_fog`'s key is `rt_fog` |
| `vid_bloom` | `m_menu.c` `videoitem_e`, after `vid_fog` | row label `"Bloom"`, hotkey `'m'` |
| `M_ChangeBloom` | `m_menu.c` | `rb_bloom = (rb_bloom + 1) % 4` |
| `bloomNames` | `m_menu.c`, beside `fogNames` | `{"Off","Low","Med","High"}` — the **value** strings, indexed by `rb_bloom` |
| `M_VideoCrispValue` | `m_menu.c`, a new `case vid_bloom:` | supplies this row's displayed value from `bloomNames` |
| `kBloomPresets` | `r_vulkan.cpp`, one table | `{threshold, knee, intensity}` per level |

**The knee lives in the preset table, not in the shaders.** It is a tuning
number exactly like the threshold — and §4.2 shows the two are not independent,
since the floor constrains `threshold − knee` rather than either alone. Keeping
the knee as a shader constant would put half of a coupled pair out of reach of
the dial and would violate INV-8. `rb_bloom` indexes the table; the shaders
receive both numbers in a push constant and hold no literal.

**Pin the declaration form**, because INV-4's floor read anchors on it and an
`awk` range that does not match returns empty and passes vacuously. Write it
exactly:

```cpp
static const struct BloomPreset { float threshold, knee, intensity; } kBloomPresets[4] = {
```

**`rb_bloom` is clamped at every site that indexes an array with it**, not
trusted from the config file: `~/.doomrc` is plain text a user can hand-edit, and
`bloom 9` would read past a four-entry array. There are exactly two such sites,
and the second fires without any keypress:

- `kBloomPresets[rb_bloom]` in `r_vulkan.cpp`, where the passes take their
  threshold, knee and intensity.
- `bloomNames[rb_bloom]` in `m_menu.c` — the **value**-name array this row needs,
  read while the Video menu is drawn, before any input arrives.

`M_ChangeBloom`'s `(rb_bloom + 1) % 4` is *not* a third site: it maps a
hand-edited 9 to 2 rather than reading out of range.

**The guards at those two sites are the whole defence — there is no clamp at
config load.** An earlier draft asked for one as well; L1 showed it to be a
special case with nothing to buy. `M_LoadDefaults` is a generic loop over
`defaults[]` with no per-variable hook, so a bloom-only clamp there would read as
an oddity beside `rb_fog`, `rb_detile` and `fpsCorner`, none of which has one.
Verified at L1: a hand-edited `rt_bloom 9` boots and shuts down cleanly, the menu
row reads "Off", and cycling it maps to Medium. The out-of-range value does
persist in `~/.doomrc` rather than being healed on save, which is the accepted
cost and matches every sibling dial's behaviour.

**Copy `fogNames`, which already does this correctly** —
`fogNames[(rb_fog >= 0 && rb_fog <= 3) ? rb_fog : 0]`, at both of its `m_menu.c`
sites. `rb_renderscale`'s two-site clamp in `r_vulkan.cpp`
(`rb_renderscale < 25 ? 25 : rb_renderscale > 100 ? 100 : rb_renderscale`) is the
same shape on the engine side. Note what `videoLabels` is *not*: it is indexed by
the `videoitem_e` **row**, never by a dial's value, so it is not one of the sites
above and needs nothing.

`'m'` for "blooM", because `'b'` is already `vid_brightness`. (`'v'` is
currently used twice — `vid_fog` and `vid_debugviews` — a pre-existing collision
this feature does not touch and must not add to.)

Starting preset values, **to be tuned with the user on hardware** (§10 Q1/Q2) —
these are a defensible opening position, not a measurement. The `ramp starts`
column is `threshold − knee`, the quantity §4.2's floor constrains, and **no
preset may put it below 1.00**:

| Level | Threshold | Knee | Ramp starts | Intensity |
|---|---|---|---|---|
| Off | 1.00 | 0.00 | 1.00 | 0.00 |
| Low | 1.80 | 0.30 | 1.50 | 0.20 |
| Medium | 1.50 | 0.35 | 1.15 | 0.35 |
| High | 1.35 | 0.35 | **1.00** | 0.55 |

**Off's row carries real numbers, not dashes.** `kBloomPresets[4]` is an array of
three floats per row, so row 0 needs values whatever they are — and with the dial
Off nothing reads them, since no dispatch is recorded and the combine is behind a
branch (§4.4). They are pinned at `{1.00f, 0.00f, 0.00f}` rather than left to the
implementer so that INV-4's floor check can read **every** row without a special
case: Off's ramp start is 1.00, on the floor, exactly like High's.

Low → High *lowers* the ramp start (1.50 → 1.15 → 1.00), so more of the scene
qualifies as a light source and the halo strengthens with it. High sits exactly on
the floor: at that preset a surface at 1.00 linear contributes precisely zero,
which §4.2's worked table checks. Tuning may move all four numbers, but never
below the floor — a preset whose ramp starts under 1.00 breaks INV-4 and INV-9
rather than merely looking different.

The table is defined **once** and read by both chains (INV-8). Whether the two
need different intensities — their lighting magnitudes are not identical, so the
same threshold may catch different things — is §10 Q3; the answer must not be two
tables.

No debug key — §9 records why.

**Row count.** `VideoMenu` carries 20 entries today, so `vid_bloom` makes 21:

```sh
awk '/menuitem_t[[:space:]]+VideoMenu\[\]/,/^\};/' linuxdoom-1.10/m_menu.c \
  | grep -c '^[[:space:]]\{1,\}{'
# 20 today, 21 after L1 — counts the three {-1,"",0} separators, which occupy
# rows on screen. The leading-whitespace requirement matters: `^[[:space:]]*{`
# also matches the array's own opening brace at column 0 and returns 21 today.
```

DOOM-0206's menu contract requires the result stay HUD-safe and scroll if it
outgrows the screen; that mechanism is already in place for the existing 20 and
this adds one row to a group that already has six. §10 Q4 owns checking it.

## 5. Data & resources

### New render targets

Sized from the swapchain extent and recreated on resize with the existing scene
targets, so a mid-frame render-scale change reallocates nothing. **All three are
allocated by this spec and shared with DOOM-0345**, which adds only its own
full-size `rtHdrImage`:

| Image | Size | Format | Usage | Used by |
|---|---|---|---|---|
| `bloomImage[0]` | ½ display | `kSceneFormat` | STORAGE + SAMPLED | both chains |
| `bloomImage[1]` | ¼ display | `kSceneFormat` | STORAGE + SAMPLED | both chains |
| `bloomImage[2]` | ¼ display | `kSceneFormat` | STORAGE + SAMPLED | both chains |

`kSceneFormat` is `VK_FORMAT_R16G16B16A16_SFLOAT` = 8 bytes/pixel. At
1920×1080:

```
bloomImage[0]   960 x  540 x 8 =  4,147,200 B  =  3.96 MiB
bloomImage[1]   480 x  270 x 8 =  1,036,800 B  =  0.99 MiB
bloomImage[2]   480 x  270 x 8 =  1,036,800 B  =  0.99 MiB
                                 ----------       --------
                                  6,220,800 B  =   5.93 MiB
```

Arithmetic, not a measurement — `960*540*8 = 4147200`, and so on. Bounded and
non-growing: three fixed fractions, all freed with the swapchain. They are shared
by both chains because only one chain runs per frame; the mode toggle already
drains the device (`vkDeviceWaitIdle` on `modeChanged`, verified in
`RB_Vulkan_Present`), so nothing aliases across the transition.

**Naming: `bloomImage[0..2]` throughout.** The umbrella used `bloom[0..2]` and
`bloomImage[0..2]` interchangeably for the same three targets; one name.

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

**Barriers between the three dispatches — and BOTH directions, which is the half
that gets forgotten.** Each dispatch reads the previous one's output, so
extract → blurH → blurV needs a `GENERAL` (compute write) →
`SHADER_READ_ONLY_OPTIMAL` (sampled read) transition plus an execution
dependency at each hop, and `bloomImage[2]` ends the chain in
`SHADER_READ_ONLY_OPTIMAL` for the combine. The existing `svgfBarrier()` helper
is the idiom to follow.

**And each dispatch needs the RETURN transition on the image it writes**, back to
`GENERAL` before the `imageStore` — from `SHADER_READ_ONLY_OPTIMAL` on every
frame after the first, and from the parked layout on the very first bloomed
frame. `bloomImage[2]` is parked in `SHADER_READ_ONLY_OPTIMAL` at creation (just
below), so blurV's *first* store already targets an image in the wrong layout;
this is not a second-frame-only concern. Stating only the write→read direction —
which is the natural way to describe a chain — leaves every storage write in an
invalid layout.

**The extract's three inputs are already in the right LAYOUT, but are not
synchronised for a COMPUTE reader.** The two halves come apart here, and the
layout half is the one that looks settled.

*Layout — nothing to do.* `bloom_extract_raster` reads `sceneImage`,
`sceneDirImage` and `aoImage`. The first two are already moved
`COLOR_ATTACHMENT_OPTIMAL` → `SHADER_READ_ONLY_OPTIMAL` by an explicit
`vkCmdPipelineBarrier` recorded immediately after the scene pass and **before**
the SSAO pass — it has to be, since SSAO reads the DIRECT target's packed
forward-distance depth — and `aoImage` reaches the same layout through its own
render pass's `finalLayout`.

*Dependency — one barrier to add.* Both of those existing dependencies stop at
the **fragment** stage: the explicit barrier's `dstStageMask` is
`VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT`, and the AO render pass's implicit
external dependency ends at `BOTTOM_OF_PIPE` with no access mask. Neither
synchronises a compute dispatch. So the extract needs one execution + memory
barrier ahead of it — `COLOR_ATTACHMENT_OUTPUT | FRAGMENT_SHADER` →
`COMPUTE_SHADER`, `dstAccessMask = SHADER_READ`, covering all three images, with
**no layout transition**. Omit it and it is a read-after-write hazard: validation
reports it, and stale or garbage halo texels appear on tiled and AMD drivers.

**Pipelines and descriptor sets.** Two new compute shaders means two pipelines,
two set layouts and their pool allocations: `g.bloomExtractRasterPipeline` and
`g.bloomBlurPipeline`.

### New shaders

Two new compiled shaders, added to `SHADER_SRCS` in `linuxdoom-1.10/Makefile`,
plus one shared include, which is **not** in `SHADER_SRCS` (it is `#include`d,
never compiled on its own) and instead needs an explicit dependency line:

| File | Stage | Job |
|---|---|---|
| `bloom_extract_raster.comp` | compute | recombine AMBIENT/DIRECT/AO, threshold, gather (§4.2) |
| `bloom_blur.comp` | compute | separable Gaussian, direction from a push constant |
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
INV-2 and INV-4 both rest on those agreeing. So the include exposes one function,
and **it must take the sample coordinate** — the two consumers do not share one,
because `composite.frag` has a `vUV` varying and the extract is compute and
derives its own:

```glsl
// formulas/scene_recombine.glsl
vec3 sceneRecombine(sampler2D amb, sampler2D dir, sampler2D ao,
                    vec2 uv, vec2 uvScale, float aoEnable);
// samples the scene at uv*uvScale and AO at uv, exactly as composite.frag does today
```

Both consumers call it, and neither computes any part of `hdr` itself.

**The include fetches with `textureLod(..., 0.0)`, never `texture()`.** Its two
consumers are different shader stages: `composite.frag` is fragment, where
implicit-LOD `texture()` is fine, but `bloom_extract_raster.comp` is **compute**,
which has no derivatives and where implicit-LOD sampling is invalid. One body has
to compile in both stages, so the explicit-LOD form is the only one available.
It is behaviourally identical here — all three targets are single-mip — which is
what keeps L2's bit-identity gate meaningful once `composite.frag` is moved onto
the include. An implementer who leaves `texture()` in either fails to compile the
`.comp`, and one who "fixes" it by editing only the fragment side has changed
`composite.frag`'s sampling on a step the spec calls a no-op.

One existing shader is edited: `composite.frag` — add the combine, and move the
recombination wholesale into the new include.

`glslc` emits no auto-dependency for a GLSL `#include` (`renderer.md`), so
`composite.frag.spv.h` and `bloom_extract_raster.comp.spv.h` both need an
explicit `scene_recombine.glsl` dependency line beside the existing
`pt_common.glsl` and `formulas/` rules, or an edit to the include will not
rebuild its consumers. **The same hazard is already live for a file this feature
edits:** `composite.frag` `#include`s `formulas/pbr_neutral_tonemap.glsl`, but the
Makefile's existing `formulas/` dependency line names only `pathtrace.comp.spv.h`
and `svgf_composite.comp.spv.h` — so `composite.frag` is today rebuilt only by
luck when that include changes. Add it to that line in the same edit.

### Push constants

| Pass | Push contents |
|---|---|
| `bloom_extract_raster` | `uvScale.xy`, `aoEnable`, `threshold`, `knee` |
| `bloom_blur` | `dir.xy` — blur axis **and** the per-pass step factor, `(2,0)` for pass 1 and `(0,1)` for pass 2 (§4.3); `srcTexelSize.xy` — exactly 1/size of the image being read, with no factor folded in |
| `composite.frag` | existing `{uvScale, aoEnable, pad}` — `pad` becomes `bloomIntensity` |

`composite.frag`'s existing push block already carries an unused `float pad`
after `aoEnable`, so the raster combine needs no layout change at all — the
intensity goes in the slot that is already reserved and ignored.

### GPU profiler slots

`performance.md` mandates measuring with the per-pass profiler, so a pass with
no timestamp is invisible to the instrument that is supposed to gate it.

**This spec does NOT widen the query pool.** Verified against the current tree:
the raster chain writes slots 0–5 today, so inserting one bloom slot takes it to
0–6 — seven slots, inside the pool's existing eight. The 8 → 10 widening belongs
to DOOM-0345, whose RT chain already uses all eight. That partition is what keeps
this half small: **DOOM-0011's fix-ledger row 9.4 records this project getting a
profiler widening wrong** (it "named 3 sites; there are 7", the misses including a
`uint64_t ts[8]` stack array `vkGetQueryPoolResults` would have overflowed), and
the cheapest way not to repeat it is for the part that does not need the widening
not to touch it.

Six sites. Rows 2–5 exist in the tree today; rows 1 and 6 are new code:

| # | Site | Today | After |
|---|---|---|---|
| 1 | a **new** `vkCmdWriteTimestamp(..., 4)` recorded after the bloom dispatches | — | new |
| 2 | the existing "after composite" write | slot `4` | slot **`5`** |
| 3 | the existing "after HUD/present" write | slot `5` | slot **`6`** |
| 4 | `uint32_t nq = g.profRasterFrame ? 6u : 8u` | `6 : 8` | **`7 : 8`** — raster arm only |
| 5 | the `[raster_profile]` `printf` — format string **and** its five `profMs[]` args | 5 buckets | 6 |
| 6 | a dummy-timestamp block for the `bloom 0` arm | — | new, see below |

**Rows 1–3 are the ones a "widen `nq` and the print" reading misses**, and
missing them is fatal rather than cosmetic: inserting bloom at slot 4 in frame
order means a new write must exist *and* the two writes above it must be
renumbered. Set `nq = 7u` without them and slot 6 is never written, so
`vkGetQueryPoolResults` returns `VK_NOT_READY` and the **entire**
`[raster_profile]` print disappears — silently disabling §6's gate and INV-5.
That is the same shape as DOOM-0011 row 9.4's under-counted widening.

**Site 5 is the one that corrupts the measurement rather than breaking it.** The
`[raster_profile]` print passes five `profMs[]` values against five hard-coded
labels (`shadow`, `scene`, `ssao`, `composite`, `hud`); insert bloom at slot 4 and
the sixth bucket has nowhere to go, so bloom's cost is printed under the word
`composite`, composite's under `hud`, and HUD is never printed at all. §6's gate
would then read a plausible, wrong table. Format string *and* argument list change
together.

**Raster inserts in frame order**, because its slots are chronological today:
`... 3 = SSAO, 4 = bloom+blur, 5 = composite, 6 = HUD`. **Only two of the five
`g.profMs[]` assignments move** — not all five: `[0]` shadow (`ts[1]-ts[0]`),
`[1]` scene and `[2]` SSAO all difference slots below the insert and are
untouched. `[3]` becomes bloom, `[4]` becomes composite, and a new `[5]` takes
HUD. Re-labelling all five would mislabel three correct rows.

**Site 6 exists because the bloom dial is itself a gate.** `nq` is a
compile-time-shaped constant, but the new slot is only *written* when
`rb_bloom > 0` (§4.4 skips every bloom dispatch when the dial is Off). A
reset-but-unwritten slot returns `VK_NOT_READY` and the readback drops the
**whole** print — which would kill the profiler on precisely the `bloom 0` arm
that §6's measurement and INV-5 compare against. So mirror the existing
`if (prof && !denoise)` dummy-timestamp block:

```
// NOTE the gate variable: the raster arm's is `rprof` (declared in
// RB_Vulkan_Present); `prof` belongs to the RT record path in RecordRtTrace.
if (rprof && !bloomActive)          // bloomActive == (rb_bloom > 0)
{
    // collapse the bloom slot onto the preceding point so its segment reads ~0
    vkCmdWriteTimestamp(..., <raster slot 4>);
}
```

### No new external dependency

Nothing is added to the dependency set. `glslc` and `xxd` already build every
shader; the blur and the threshold are arithmetic.

## 6. Performance budget

**Budget: ≤ 5 % of present-total in Solid, and Solid must stay above the 60 fps
floor at 50 % render scale on the reference RX 6600.**

**The numerator is the GPU-total delta between the two arms, not the bloom
bucket** (decided with the user 2026-08-13, after L4's measurement found the two
disagree by 2.8 points). The bucket is a `BOTTOM_OF_PIPE` segment fenced by the
bloom barriers, so with the dial on it absorbs pipeline drain that the
`composite` and `scene` buckets carried before — it over-reads bloom's true cost
by roughly a third. Sum every `[raster_profile]` row in each arm and difference
the totals; `[cpu_profile]`'s present-total stays the denominator. The bloom
bucket is still what makes the pass *visible* to the profiler (INV-6), and it
remains the right number for "is the blur itself getting expensive" — it is just
not the number this budget divides.

No number here is measured yet, and none will be quoted until it is. What the
design costs, structurally:

| Pass | Output pixels @ 1920×1080 | Work per pixel |
|---|---|---|
| extract | 518,400 (½ res) | 1 source tap at the default 50 % scale, 4 at 100 % (§4.2) × 3 textures, threshold |
| blur ×2 | 129,600 each (¼ res) | 5 bilinear fetches |
| combine | 0 extra | folded into `composite.frag`, which already samples three textures |

No row here is a *new pass* in the sense of costing anything with the dial Off —
all three dispatches are skipped and the combine is behind a branch, so the Off
row of the measurement below is genuinely zero.

The measurement, at the L4 gate (§7), following `performance.md`'s comparison
rule — same map, same render scale, reference GPU, both arms from the same build:

```
# Solid, 50% render scale, E1M1: bloom default vs bloom off
#   [cpu_profile]    -> fps and present-total  (the 60 fps floor; the 5% denominator)
#   [raster_profile] -> the per-pass rows, incl. the new bloom bucket (the numerator)
\   (rb_profile) with bloom=2, then with bloom=0
```

Levers, cheapest first, if the budget is missed:

1. **Shrink the blur from 9 taps (5 fetches) to 5 taps (3 fetches).** Cuts the
   blur's fetch count by 40 %; costs reach, which §4.3 already calls tight.
2. **Drop the extract to quarter res.** §9 records why it is not there already,
   and §4.2 records that the argument is scale-dependent.

And the dial itself is the player-facing lever — `Off` skips every pass — which
is what `performance.md` asks a heavy effect to ship with.

## 7. Build order

Each step ends with something observable. `make` and `make test` after every
one (`always-rebuild-engine`).

**Every A/B verify below takes THREE captures, not two** — `ab_diff.py`'s
signature is `<on.png> <off.png> <control.png>`, and the control is a second
capture from the *same* build with identical settings. Its own header says why:
"a control that cannot move proves the harness before it proves the effect", and
SIGNAL is meaningless unless NOISE is quoted beside it. A two-argument call does
not fail cleanly, it raises on unpacking.

- **L1 — the dial, doing nothing.** `rb_bloom` + the `rt_bloom` config key + the
  `vid_bloom` menu row + `M_ChangeBloom` + `bloomNames[4]` + the `kBloomPresets`
  table in its pinned declaration form + the menu's index guard + the
  `-shotverify` pin. No render change. **The engine-side index guard is NOT part
  of L1** — nothing reads `kBloomPresets` until L2, and an accessor with no caller
  is dead code; it lands with its first reader. *Verify:* the row cycles
  Off/Low/Medium/High, survives a restart (`grep rt_bloom "$CFG"` against the temp
  config — never `~/.doomrc`, which the engine rewrites on exit), is absent in
  Classic, a hand-edited out-of-range value boots clean, and the 21-row menu
  clears §10 Q4's arithmetic.
- **L2 — the extract.** `bloomImage[0..2]` with the `SHADER_READ_ONLY_OPTIMAL`
  park, `bloom_extract_raster.comp`, `formulas/scene_recombine.glsl`, and
  `composite.frag` refactored onto the include with the combine still absent.
  *Verify:* the frame is unchanged — `ab_diff.py <post-L2> <pre-L2>
  <post-L2-control>` → SIGNAL mean 0.00, max 0.0. The refactor's no-op status is
  **asserted by this gate**, not proved a priori — bit-identity under `glslc -O`
  is an empirical result, and this is where a `scene_recombine.glsl` that dropped
  the AO blur shows up.
  **If SIGNAL is non-zero, the include is the cause — and the include stays.**
  Reverting it so the extract re-derives `hdr` is the one thing §5 forbids, since
  then the value thresholded is not the value bloomed. So: confirm the difference
  is confined to sub-1/255 rounding (max ≤ 1.0, mean ≤ 0.01), record the measured
  delta here, and re-baseline INV-2 against the **post-L2** commit instead of the
  pre-L2 one — INV-2 asks that the *dial* change nothing, and from L2 onward that
  is exactly what it measures. Anything larger is a real defect in the include
  rather than a rounding artefact, and stops the step.

  **Measured 2026-08-12, and the clause above does not fit what came back.**
  Solid, `renderer 2`, E1M1 `-warpto 3274 -3353 200`, three captures:
  SIGNAL (post-L2 vs pre-L2) **mean 0.00 / max 13.7 / 0.0 % of pixels > 2**,
  NOISE (post-L2 vs its own same-build control) **mean 0.11 / max 102.7 /
  0.3 %**, EFFECT (moved by the change but not by the control) **0 px**. The
  same-build control moves *more* than the change does, so `max ≤ 1.0` cannot
  discriminate anything here: `-freeze` holds mobj thinkers only — sector-light
  and animated-texture thinkers keep running — and `-devshot` counts presents,
  not tics, so the shot lands on a different gametic run to run. A second spot
  (`1056 -3616 90`) behaves the same: NOISE mean 0.14, max 97.3. The clause
  states its bound against SIGNAL alone when the harness quotes a NOISE row
  precisely because an absolute bound does not hold; **fixing the threshold is
  an open decision, not something this step took.**

  What carries the step instead is deterministic and stronger than the capture:
  `composite.frag`'s SPIR-V function body, id-normalised after
  `spirv-opt --strip-debug`, is **instruction-for-instruction identical** across
  the refactor — 109 lines each way — with exactly two differences, both
  expected: six `OpImageSampleImplicitLod` → `OpImageSampleExplicitLod … Lod 0`
  (the `textureLod` requirement §5 states), and one `OpAccessChain`/`OpLoad` pair
  for `aoEnable` scheduled earlier. Nothing dropped, no arithmetic changed — so
  the AO blur and the `AO_DIRECT_WEIGHT` mix provably survived the move into the
  include. INV-2's baseline therefore stays the **pre-L2** commit.
- **L3 — the blur + combine.** `bloom_blur.comp` ×2, the composite's fourth
  descriptor binding, and the `composite.frag` add. *Verify:* a lamp in Solid
  gains a halo; `bloom 0` is byte-identical (INV-2); a plain wall away from any
  lamp does not move (INV-4); a sky-facing capture does not generate bloom
  (INV-9). **And §10 Q5's measurement**: the AMBIENT ceiling over non-emissive
  geometry, against each preset's ramp start. The presets are provisional until
  it is taken, because §4.2's floor is a measured property of this engine's GI
  bounce, not arithmetic.

  **Measured 2026-08-13. Q5's ceiling is ~1.35, well above the 1.0 §4.2 assumed —
  and the presets clear it anyway, because the soft knee's ramp start is not the
  point where extraction becomes visible.** Solid, `renderer 2`, three E1M1 spots,
  each swept with a HARD knee (knee 0, intensity 1.0) so the threshold read
  directly as the ramp start:

  | Spot | Extraction gone by |
  |---|---|
  | `3274 -3353 200` (corridor) | < 1.00 |
  | `1056 -3616 270` (start room, light panels) | ~1.35 |
  | `3000 -4400 90` (bounce-heavy room) | ~1.20 |

  At the light-panel spot the frame-wide moved fraction falls 37.1 % (T 0.75) →
  24.5 % (1.00) → 9.1 % (1.15) → 1.4 % (1.25) → 0.0 % (1.35), and its block map at
  T 1.20 separates the lamp from the wall by 17× (23.4/255 in the lamp block, ≤ 1.4
  everywhere else). So the *bulk* of non-emissive art sits under ~1.15 with a thin
  tail to ~1.35.

  **Two of the four presets' ramp starts (Medium 1.15, High 1.00) sit inside that
  tail, and INV-4 still holds** — because a ramp start is where the quadratic soft
  knee begins at *zero* weight, not where it contributes. At the shipped presets the
  same spot moves 0.44/255 mean and 0.4 % of pixels at High and 0.30/255 at Medium,
  against a same-build NOISE floor of 0.41/255 and 0.4 %: indistinguishable from the
  harness's own noise. **No retune taken.** The `if the ceiling exceeds 1.00, the
  presets rise to clear it` branch of Q5 is therefore NOT exercised: it was written
  against a hard threshold, and the knee is what absorbs the difference. If Q1/Q2's
  look call at L4 reports wall glow, the lever is the intensity column first.

  The harness could not gate this photographically at the shipped presets — SIGNAL
  and NOISE are equal there, and `ab_diff.py`'s EFFECT row at this spot is the
  fullscreen HUD numerals changing between runs, not a halo. What carries L3 instead
  is (a) the forced-threshold captures, where the light strips visibly bleed into the
  dark gaps between them, and (b) INV-2 proved deterministically rather than
  photographically: `composite.frag`'s SPIR-V after the combine is
  **purely additive** against the pre-L3 build — id-normalised after
  `spirv-opt --strip-debug`, 172 → 190 lines, every pre-existing instruction
  unchanged and in the same order, the additions being only the binding-3
  declaration, the push load at member 2, the `OpBranchConditional`, the sample, the
  multiply-add and the `OpPhi`. With `bloomIntensity == 0` the phi returns the
  original `hdr`, so `bloom 0` is byte-identical by construction.

- **L4 — profiler slot, then the gate.** All **six** profiler sites (§5) — the
  new slot-4 write, the two renumbered writes above it, `nq`, the `printf`
  format string *and* its argument list, and the dummy block — then the §6 measurement,
  and the human look call on hardware (§10 Q1/Q2). Then the `-shotcompare`
  golden's re-bless is owed — see §12; it is already owed for `rt_fog`, so this
  adds to an existing debt rather than creating one. Only after all of that does
  the ROADMAP bullet flip and CHANGELOG gain an entry.

  **Built 2026-08-13. Five sites, not six — sites 1 and 6 collapsed into one
  unconditional write, and the collapse also closes a hole the spec's own
  formulation left open.** §5 site 6 gates its dummy on `if (rprof &&
  !bloomActive)`, but `bloomActive` is not the predicate that decides whether the
  chain records: the block's full guard is `bloomActive &&
  bloomExtractRasterPipeline && bloomExtractDs && haveCamera && atlasReady`. On a
  frame where the dial is on and the camera is not up (the menu, the intermission,
  the first frames of a level load) neither the real write nor the `!bloomActive`
  dummy fires, slot 4 goes unwritten, and the print vanishes — the exact
  `VK_NOT_READY` failure site 6 exists to prevent, on a different arm. A single
  `if (rprof) vkCmdWriteTimestamp(…, 4)` placed *after* the bloom block covers
  every case: with the chain recorded it closes the segment at the same
  instruction, and with it skipped it collapses onto slot 3 and reads ~0 ms.
  Sites 2–5 landed as written.

  **§6's measurement, and INV-5.** Solid (`renderer 2`, `rt_view 0`), 50 % render
  scale, E1M1 `-warpto 1056 -3616 270 -freeze -noinput`, reference RX 6600, both
  arms from the same build, config regenerated per run (the engine rewrites it on
  exit). Eight steady-state one-second prints each, no stray engine, zero Vulkan
  validation output:

  | | fps | present-total | shadow | scene | ssao | **bloom** | composite | hud | GPU total |
  |---|---|---|---|---|---|---|---|---|---|
  | `bloom 2` | 160.1 | 5.75 ms | 0.01 | 1.02 | 1.98 | **0.43** | 0.37 | 0.12 | 3.93 ms |
  | `bloom 0` | 161.1 | 5.71 ms | 0.00 | 1.09 | 2.01 | **0.01** | 0.45 | 0.10 | 3.66 ms |

  **The 60 fps floor is cleared by 2.7×** — 160 fps against a 60 fps floor, and
  the two arms are indistinguishable in fps and present-total (0.04 ms apart,
  inside the run-to-run spread). The frame is CPU-build-bound here, not GPU-bound
  (`fenceWait` 0.03 ms, `build` 5.4 ms of a 5.7 ms present-total), so bloom's GPU
  cost is hidden entirely.

  **The ≤ 5 % clause reads differently depending on which numerator it means, and
  §6 does not say.** Against the bloom bucket alone — which §6 names as "the
  numerator" — 0.43 / 5.75 = **7.5 %, over budget**. Against the measured GPU
  delta between the arms, 0.27 / 5.75 = **4.7 %, inside it**. Against frame time,
  **0 %**. The bucket over-reads because it is a `BOTTOM_OF_PIPE` segment bounded
  by the bloom barriers: with the dial on, drain that the `composite` and `scene`
  buckets absorbed before now lands in bloom's (composite falls 0.45 → 0.37 and
  scene 1.09 → 1.02 across the arms, which is where the 0.43 − 0.27 goes).

  **Resolved with the user 2026-08-13: the numerator is the GPU-total delta, so
  INV-5 PASSES at 4.7 %** — §6 now states it. The reasoning is that the bucket
  charges bloom for drain the other buckets used to absorb, which is an artefact
  of where the timestamp sits rather than work bloom does. **INV-5's remaining
  arm is unmeasured, and deliberately so:** this budget was taken on a
  CPU-build-bound frame (`build` 5.4 ms of a 5.7 ms present-total), where bloom's
  GPU cost is hidden entirely. It is a true reading of the reference machine as
  it stands today, not a claim that the cost is free — DOOM-0074's build-ahead is
  what put the frame here, and a future CPU-side win moves the bottleneck back to
  the GPU and re-arms this measurement. Re-measure if `fenceWait` ever climbs off
  the floor.

`scripts/ab_capture.sh` needs one change to serve L2 and L3, because **as written
it cannot capture Solid at all.** It ends with
`grep -m1 'HD load done' "$OUT/$NAME.log" || { …; exit 1; }`, which is the right
guard for Ultra (the HD-assets `cwd` trap is a real one) and fatal for Solid:
`EnsureHdMaterials` opens with `if (rendermode != TIER_RT3D || g.hdBuilt) return;`
so a Solid run never prints that line.

The script takes no tier argument — it inherits the tier from the config it
copies (`renderer` in `~/.doomrc`). So the mechanism is: **make the HD check
conditional on the `renderer` value in `$CFG`**, which the script already has in
hand:

```sh
# only Ultra (renderer 1) loads HD art, so only Ultra owes the log line
if grep -qE '^renderer[[:space:]]+1$' "$CFG"; then
    grep -m1 'HD load done' "$OUT/$NAME.log" || { echo "!! $NAME: PALETTED"; exit 1; }
fi
```

That keeps the Ultra guard exactly as strict as it is today and stops it firing on
a tier that was never going to satisfy it. **Selecting a tier is the caller's job,
and both arms need saying:** pass `DOOMCFG=` a config with `renderer 2` for Solid
and `renderer 1` for Ultra — the script's existing env hook already supports it.
Without an explicit `DOOMCFG`, a capture inherits whatever tier the user last
played in, which is how a "Solid" measurement silently becomes an Ultra one.

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

- **INV-2** — `rb_bloom == 0` restores the current picture **byte for byte** on
  the raster chain. No extract, blur or combine is recorded, and the combine sits
  behind a branch rather than a multiply by zero (§4.4).
  *Test:* `scripts/ab_capture.sh` at a fixed coordinate with `bloom 0`, against
  the **pre-L2 commit — or the post-L2 commit if L2 recorded a rounding delta**
  (§7 L2's escape hatch; without this clause the invariant is unsatisfiable
  whenever that hatch is taken), plus a same-build control —
  `ab_diff.py <bloom0> <pre-L2> <bloom0-control>` → SIGNAL mean 0.00, max 0.0,
  with the NOISE row quoted beside it. Run in Solid (`DOOMCFG` with
  `renderer 2`) and in Ultra with the ray-traced view off — **`renderer 1` plus
  `rt_view 0`**. Name that second key: `rt_view` defaults to **6**, so an
  inherited config runs the RT chain, where this spec's combine does not execute
  at all and the arm would pass without exercising anything. The harness cannot
  toggle it by keystroke (§9), so it has to come from the config.
  *Breaks when:* a dispatch is recorded unconditionally, or the combine becomes
  `hdr += bloom * intensity` with no guard and a non-finite value reaches
  `bloomImage[2]`.

- **INV-3** — the HUD, status bar, menu text and weapon sprite are never bloomed
  and never bloomed over on the raster chain. The combine runs inside the
  `composite.frag` draw, which the overlay draw and `FlushMenuText()` follow
  within the same swapchain pass.
  *Test:* the ordering claim is what this invariant really rests on, so check it
  structurally **and** photographically — the strip test alone cannot see the
  weapon sprite or an open menu, both of which sit inside the world area:
  ```
  # 1. structural: the combine must precede the overlay
  awk '/^extern "C" void RB_Vulkan_Present/,/^}/' linuxdoom-1.10/r_vulkan.cpp \
    | grep -n 'compositePipeline\|overlayPipeline\|FlushMenuText'
  # expect compositePipeline BEFORE overlayPipeline before FlushMenuText
  # 2. photographic: the status-bar strip is bit-identical (bottom 19.5%,
  #    the region ab_diff.py crops)
  python3 -c "import numpy,sys;from PIL import Image;\
a,b=(numpy.asarray(Image.open(p).convert('RGB'),dtype=numpy.float32) for p in sys.argv[1:3]);\
h=int(a.shape[0]*0.805);print(numpy.abs(a[h:]-b[h:]).max())" on.png off.png
  # expect 0.0
  # 3. weapon + menu: capture with the weapon drawn and the menu open, and read
  #    ab_diff.py's block map - the weapon and menu blocks must not move
  ```
  *Breaks when:* the combine moves after `FlushMenuText()`, or the extract is
  pointed at the swapchain image after the overlay has been drawn into it.

- **INV-4** — only genuinely over-white light blooms; paletted, non-emissive art
  does not. The threshold is applied to the pre-tone-map value, and the point at
  which extraction *starts* — `threshold − knee`, not `threshold` (§4.2) — sits
  **above the measured AMBIENT ceiling for non-emissive art**. That ceiling is
  not 1.0 and is not arithmetic: `mesh.frag` adds a GI bounce term on top of
  `albedo * sect` with nothing clamping the sum (§4.2), so §10 Q5 measures it at
  L3 and the presets clear whatever it turns out to be. Until then this invariant
  is asserted, not proven.
  *Test:* three parts — the first because the arithmetic can be checked without a
  build, the second because the look cannot, and the third because the ceiling the
  first is compared against is runtime data that no source-scraping test can see:
  ```
  # 1. every preset's ramp start is >= 1.0 (the floor) AFTER every per-chain factor
  #    the threshold test applies -- see the 2026-08-25 amendment below; reading the
  #    table alone is what missed the breach. `make test` runs it:
  linuxdoom-1.10/tests/bloom_threshold_test.cpp
  #    Its table half anchors on the pinned declaration form (4.5), not a bare
  #    'kBloomPresets[]' which does not match it:
  awk '/kBloomPresets\[4\][[:space:]]*=/,/\};/' linuxdoom-1.10/r_vulkan.cpp
  # for each row: threshold - knee >= 1.0   (Low 1.50, Med 1.15, High 1.00)
  # 2. E1M1, flashlight off, bloom 3 vs bloom 0 + a same-build control:
  #    ab_diff.py's block map moves only in blocks holding a lamp, a lit switch
  #    or a liquid, and reads 0.00 mean on plain wall and floor blocks
  #    THAT ARE NOT UNDER A LAMP - the DIRECT term is unbounded by design
  #    (see 4.2), so a heavily point-lit wall is expected to bloom.
  #    Quote the NOISE row beside SIGNAL.
  # 3. the map's AMBIENT ceiling, printed by RunGiBake on every level load. Aim
  #    part 2 at the coordinate it names - that is the map's worst case, and it is
  #    not usually the room that looks brightest:
  grep 'GI bounce ceiling' <run log>
  # Compare the printed bound against each preset's ramp start. A bound BELOW every
  # ramp start proves this invariant outright; a bound above one is not a failure by
  # itself (the knee's weight at the ramp start is zero), and what settles it is part
  # 2 aimed at the probe. E1M1 on 2026-08-25: bound 1.174, above Medium and High.
  ```
  *Breaks when:* a preset's `threshold − knee` drops below 1.0 (the failure the
  knee makes easy — a threshold of 1.0 with a knee of 0.5 starts extracting at 0.5
  and blooms a lit wall), **or a per-chain factor divides that ramp start on its way
  to the comparison** (the 2026-08-20 breach, below), or the extract is moved to read
  the post-tone-map image, where §3 decision 1's measured 1.13× compression leaves no
  threshold that catches a lamp without catching a lit wall.

  *Amended 2026-08-25, recording what was built.* **This invariant was breached from
  2026-08-20 to 2026-08-25 and part 1 of the test above could not see it.** DOOM-0345
  added `kBloomRasterScale` (1.5) as a sixth push constant multiplying the raster
  chain's peak *before* the threshold comparison. Multiplying the peak is
  arithmetically a **division of every ramp start**: Medium's 1.15 became 0.767 on
  that chain — below paper-white — while the preset table part 1 reads never moved.
  Measured 2026-08-21: a plain STARTAN3 wall at sector light 255, no emitter in
  frame, flashlight off, bloomed across 15.9 % of pixels on the raster chain while
  both traced arms read 0.0 %.

  The repair keeps the constant and narrows what it multiplies, because the ramp
  start bounds **AMBIENT** and §4.2 says so: AMBIENT is sector light plus a GI bounce
  and tops out near paper-white, while DIRECT is unbounded by design and this
  invariant states outright that a heavily point-lit wall is expected to bloom. So
  the unit conversion belongs on DIRECT alone — `pk = sp.direct * chainScale +
  sp.ambient`, thresholded against the table's own numbers. `sceneRecombineParts()`
  hands the extract the two terms separately; `sceneRecombine()` is defined as their
  sum, so §5's one-derivation rule still holds and `composite.frag`'s compiled SPIR-V
  is unchanged (opcode multiset identical; only two constant-pool entries reorder).

  Measured at E1M1 `1432 -3456 180`, Solid raster, bloom 2 vs 0, each arm against a
  same-build control: the wall goes from **0.60 mean / 5.1 % of pixels** (noise 0.16 /
  0.3 %) to **0.10 / 0.2 %** (noise 0.03 / 0.1 %). The approved emitter glow at
  `1056 -3616 270` survives at **19.2 %** of pixels (noise 0.2 %), down from 33.8 % —
  and that is *toward* the traced view's 7 %, which is the match §10 Q3's look call
  asked for. `-rtverify` PASS, rel-MSE unchanged at 0.2058 %.

  *Amended 2026-08-25, second pass — §10 Q5 answered arithmetically.* The AMBIENT
  ceiling this invariant rests on is no longer estimated from photographs. `RunGiBake`
  walks the SH-L1 payload it already maps and prints the map's bound:

  ```
  RB_Vulkan: GI bounce ceiling — max giIrradiance 0.424 at probe 22 (2080 -3648 80);
  AMBIENT bound 1.174 = 0.75 sector + bounce (DOOM-0331 INV-4 floor is 1.00)
  ```

  On E1M1 that bound is **1.174**, so **Medium's ramp start of 1.15 does not clear it**
  and neither does High's 1.00; **Low's 1.50 does, outright**. The ramp start is
  nevertheless the wrong place to stop reading, because it is where the quadratic knee
  begins at *zero* weight: at peak 1.174 the extract's own arithmetic gives
  `w = 0.00035` at Medium and `w = 0.018` at High.

  Photographed at the worst case the print names — `2080 -3648`, facing the brightest
  plain wall in the room, Solid raster, each arm against a same-build control:

  | Preset | Ramp start | World blocks | Noise |
  |---|---|---|---|
  | Low 1 | 1.50 | zero by arithmetic (1.50 > 1.174) | — |
  | Medium 2 (default) | 1.15 | **0.0/255, every block** | 0.0 |
  | High 3 | 1.00 | **0.1–0.6/255** | 0.0 |

  So INV-4 holds at the shipped default with nothing to measure, and at High it is
  breached by a quantity that peaks at 0.6/255 of one block's mean — real, and below a
  quarter of one 8-bit step. That is recorded rather than retuned: the lever §10 Q5
  wrote for this branch is the intensity column, and moving it would cost the halo the
  user approved to remove something no play-test can see.

  The earlier 1.5 % at `3000 -4400 90` is accounted for and was never AMBIENT. Its
  block map moves only in the ceiling's red lamp array and in a nukage pool, both of
  which reach the extract through DIRECT (`mesh.frag`: `direct += POINT_LIGHT_STRENGTH
  * albedo * plight`), which this invariant exempts by name. The rest was the
  fullscreen HUD numerals, which the same capture's NOISE row also moves.

- **INV-5** — Solid keeps the 60 fps floor. With `bloom` at its shipped default,
  Solid at 50 % render scale on the reference RX 6600 stays at or above 60 fps
  on E1M1, and the bloom passes cost ≤ 5 % of present-total.
  *Test:* with `rb_profile` on (`\`), **two** prints, because neither carries
  both halves: `[cpu_profile]` for fps and present-total (the floor, and the
  denominator of the 5 % bound) and `[raster_profile]` for the per-pass rows
  including the new bloom bucket (the numerator). `bloom 2` vs `bloom 0`, same map
  and same render scale (`performance.md`'s comparison rule). No expected value —
  this is the L4 measurement, not a recorded one.
  *Breaks when:* the blur is run at full resolution, or the single level grows
  into a pyramid without a re-measure.

- **INV-6** — the raster bloom pass is timed, and the profiler's five existing
  buckets keep their labels. A pass with no timestamp is invisible to the
  profiler `performance.md` mandates, and would show up as a mysteriously slower
  neighbour; a widened bucket list with an unwidened `printf` is worse, because
  it prints a plausible wrong table (§5 site 2).
  *Test:* symbol-anchored, so it survives edits above the sites:
  ```
  awk '/^extern "C" void RB_Vulkan_Present/,/^}/' linuxdoom-1.10/r_vulkan.cpp \
    | grep -o 'gpuTimerPool, [0-9]' | sort -u   # today 0-5; after L4 0-6
  # Anchor on the RASTER arm alone. DOOM-0345 rewrites the RT arm to `10u`, so
  # pinning the whole line would make this clause return 0 on an intact tree the
  # moment the sibling lands - and the obvious "fix" is to revert the RT arm to
  # 8u, which silently truncates the RT readback.
  grep -c 'nq = g.profRasterFrame ? 7u :' linuxdoom-1.10/r_vulkan.cpp   # -> 1
  # The print must carry six buckets, not five - and anchor on the FORMAT STRING,
  # not a -A4 window. That window also spans the profMs[] argument list, so a
  # build that widened the args and left the labels at five still scores 1 if any
  # arg line carries a `// bloom` comment - the exact breach 5 site 5 describes.
  grep -c 'bloom %\.2f' linuxdoom-1.10/r_vulkan.cpp   # -> 1
  ```
  Today those read `0 1 2 3 4 5`, the `nq` line is `? 6u : 8u`, and the print has
  no `bloom` — all verified against the current tree, and all three **must
  change**, which is why the expected values above are the post-L4 ones.
  *Breaks when:* a dispatch is added between two existing timestamps without
  inserting one — the earlier pass then absorbs its cost silently — or the
  `printf` argument list is widened without its format string.

- **INV-7** — the `-shotverify` / `-shotcompare` golden gate stays
  config-independent. `rb_bloom` is pinned to its shipped default in the
  DOOM-0208 pin block, beside `rb_fog` and the rest.
  *Test:* `awk '/DOOM-0208: pin a canonical/,/^        \}/' linuxdoom-1.10/r_vulkan.cpp | grep -c rb_bloom`
  → `1` (today: `0`, verified).
  *Breaks when:* a new look dial ships unpinned — the exact leak that block's
  own comment records happening with `rt_fog`.

- **INV-8** — the threshold, knee and intensity presets exist in exactly one
  place, and every chain reads that one. This is the invariant DOOM-0345 is most
  able to breach, since it is the second consumer.
  *Test:* two clauses, and the second needs a command of its own because the
  first cannot see it — a hardcoded `1.35` in a shader leaves
  `grep -rn kBloomPresets` returning exactly one definition:
  ```
  # 1. one definition, plus the reads that consume it
  grep -rn 'kBloomPresets' linuxdoom-1.10/
  # 2. no TUNING literal hardcoded in a bloom shader. Allowed: 4.3's five kernel
  #    weights and two offsets, and 4.2's own shape constants - the 0.0, 2.0 and
  #    4.0 of the soft knee and its 1e-4 guards. Anything else is a preset value
  #    that escaped the table. Note the exponent form: a plain [0-9]+\.[0-9]+
  #    misses 1e-4 entirely while flagging the shape constants.
  grep -nE '[0-9]*\.?[0-9]+(e-?[0-9]+)?' linuxdoom-1.10/shaders/bloom_*.comp
  # reviewed by eye against that allowed list
  ```
  *Breaks when:* the two chains need different tuning and someone answers that
  with a second table instead of a named per-chain scale constant (§10 Q3).

- **INV-9** — the raster sky never *generates* bloom. The sky needs its own
  argument, **not §4.2's AMBIENT floor**: the **scene pass** (`mesh.frag`) writes
  sky into the **DIRECT** target, and §4.2 is explicit that DIRECT is unbounded.
  (`composite.frag` only *reads* that target; its header comment describing the
  contents as "flashlight + point lights + sprite/sky colour" is a description,
  not the write.) What bounds the sky is `mesh.frag`'s sky branch, which writes
  `outDirect = vec4(skyOut, 100000.0)` and `outAmbient = vec4(0.0)`: the sky's
  AMBIENT term is exactly zero, and its DIRECT term is a palette colour written
  once and never multiplied by a light term. The far depth makes SSAO skip the
  sky, so `aoDirect` is 1 there and the thresholded value is `skyOut` itself —
  which cannot exceed 1.0 and so cannot reach any preset's ramp start.
  **This governs generation, not reception.** The raster combine has no sky test,
  so a lamp beside a sky edge does bleed its halo onto sky pixels there. That is
  deliberate and physically right — light does spill in front of a distant
  backdrop. (DOOM-0345's RT chain differs on reception, and its INV-5 says why.)
  *Test:* a sky-facing capture (E1M1's outdoor courtyard), `bloom 3` vs `bloom 0`
  + a control → 0.00 mean delta in sky blocks **that have no bright neighbour**.
  Sky blocks adjacent to a lamp are expected to move.
  *Breaks when:* a preset's ramp start (`threshold − knee`) drops below 1.0, at
  which point the sky starts generating its own bloom rather than merely
  receiving a neighbour's.

  *Amended 2026-08-25, recording what was built.* **This invariant went with INV-4,
  by the same arithmetic and unmeasured at the time.** The reasoning above is sound
  but it rests on a *comparison* — a sky texel's DIRECT is a palette colour that
  cannot exceed 1.0, so it cannot reach a ramp start of 1.15 — and
  `kBloomRasterScale` moved the other side of that comparison. At 1.5 a sky texel
  reaches 1.5, and the invariant rode on whatever that constant happened to be.
  Measured 2026-08-25 at E1M1 `1900 -3100 270` (the courtyard), Solid raster, bloom
  2 vs 0: on the pre-fix build the sky band moved **0.55/255 against a 0.00 noise
  floor** — a real, small breach.

  Generation is now **stated** rather than left to arithmetic: `mesh.frag` writes the
  sky as `outDirect = vec4(skyOut, 100000.0)` with `outAmbient = 0`, `ssao.frag`
  already reads that same far depth at `>= 50000.0` to skip it, and the extract now
  zeroes the weight there. **DOOM-0345 INV-5 is the precedent** — the RT extract
  multiplies its own sky flag in for exactly this reason, so the two chains now agree
  on *generation* and still differ on *reception*, which is the design.

  **Reception is untouched, deliberately**: the combine still has no sky test, so a
  lamp beside a sky edge bleeds its halo onto the backdrop as this invariant says it
  should. The post-fix photographic arm cannot assert an exact zero — that spot's own
  same-build control moves between 0.56 and 0.80/255 across runs, and the residual
  signal sits inside it — so the structural guard, not the capture, is what this
  invariant now rests on. `tests/bloom_threshold_test.cpp` locks it.

### Trust boundary

None crossed, so no invariant above defends one. No file is read, no user input
is parsed beyond one integer that the menu and config layers clamp (§4.5), and no
network, IPC or model output is involved. Recorded explicitly rather than
omitted, because an absent boundary section reads as an oversight.

## 9. Alternatives considered (and rejected)

- **Extract after the tone-map.** One chain for both paths, no `svgf_composite`
  surgery, no `rtHdrImage`. Rejected by the user (§3 decision 1, which carries the
  measured compression ratio): the effect degrades into "anything pale glows" —
  the complaint the roadmap bullet opens with.
- **Gate bloom on the emissive mask** (DOOM-0084/0302). Rejected by the user
  (§3 decision 2): explosions, muzzle flash and stacked point lights are not
  flagged emitters and would stop glowing, and it threads a mask through two
  more shaders to buy a narrower result.
- **Let the threshold ride the Brightness slider** — i.e. threshold whatever
  value each chain's tone-map operator sees, so threshold and knee are always
  commensurate. Rejected by the user (§3 decision 5): it means dragging
  Brightness silently changes which surfaces count as light sources. The two
  properties cannot both hold; stability won.
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
- **`B10G11R11_UFLOAT_PACK32` for the bloom targets.** Halves their bandwidth and
  memory; bloom values are non-negative, so the lost sign costs nothing. Rejected
  for v1 because it buys ~3 MiB for a second format to reason about, and because
  DOOM-0345's `rtHdrImage` needs an alpha channel the packed format has not — so
  taking it here would split the bloom chain's format from the HDR target's.
- **A debug key** for bloom, matching `]` `[` `'` `;`. Rejected: the A/B harness
  drives effects through a temp config rather than the keyboard (`ab_capture.sh`
  copies and rewrites a config, and cannot inject keystrokes under Wayland
  anyway), and the menu row is the player's control. Unused *menu* hotkeys do
  remain (`a`, `e`, `h`, `j`, `n`), but that is a different namespace from the
  punctuation debug keys, so scarcity is neither the reason nor the
  counter-argument.

## 10. Open questions

Five questions: two look calls, one measurement-then-judgement, one arithmetic
check, and one measurement that gates the presets. **None blocks drafting; Q4's
arithmetic gates L1 and Q5's measurement gates L3.** The umbrella's Q3 — which did block a build step — was answered by
the user on 2026-08-12 and is recorded as §3 decision 5; what remains under Q3
below is the look residual it leaves.

- **Q1 — is the halo the right size and strength?** The σ = 2.0-at-quarter-res
  kernel gives a ±16 display-pixel reach at 1080p (§4.3), and the intensity
  presets are a starting position. **User**, on hardware, at the L4 gate.
  *Blocks:* the ROADMAP flip only. If the halo reads too tight, the 13-tap kernel
  then §9's pyramid are the levers; if too strong, the intensity presets drop
  before the threshold does.
- **Q2 — does a flashlight on a plain lit wall bloom, and does that read badly?**
  §3 decision 2 accepted the possibility. **User**, same gate. *Blocks:* the
  ROADMAP flip only. The fix if it does is raising the presets' ramp start above
  1.0, not an emissive gate — that was weighed and rejected.

  *Reworded 2026-08-20, example only — the question is unchanged.* This asked
  for **a white wall**, and the user pointed out that DOOM 1 and 2 have none:
  the palette is browns, greys and greens, so the test named a surface that
  does not exist and could not be run as written. What Q2 is actually asking
  is whether an **ordinary, non-emissive surface** blooms when the flashlight
  lights it hard — a false positive, where the halo should mark light
  *sources* rather than anything merely bright. The surfaces to aim at are the
  brightest ordinary art in the game: the pale tech panels (`STARTAN`,
  `STARGR`) and the light concrete and metal flats, at point-blank range with
  the flashlight on. The elsewhere-in-this-spec uses of "white wall" as the
  **1.0-linear reference value** in §4.2's table and the luminance worked
  example are a unit, not a surface, and are left alone.
- **Q3 — does one shared preset table read the same on both chains?** §3
  decision 5 settled the *units* question: the threshold is in scene radiance on
  both chains, so what glows no longer moves with the Brightness slider. What it
  does not settle is whether the two chains' lighting magnitudes are close enough
  that 1.35 means the same thing to a player in Solid and in Ultra RT.
  **Claude to measure** (capture one coordinate in both chains at the shipped
  presets and report which blocks bloom), **user to judge**, once DOOM-0345 has
  landed and there is an RT arm to compare against. *Blocks:* nothing in this
  spec. If the answer is no, the fix is one named per-chain scale constant
  applied to the shared table — never a second table (INV-8).
- **Q4 — does a 21st `VideoMenu` row still fit?** `VideoMenu` has 20 entries
  today (§4.5 carries the counting command) and DOOM-0206's contract requires the
  menu stay HUD-safe and scroll if it outgrows the screen. Two gates, and the
  umbrella conflated them: **Claude to check the arithmetic at L1** — 21 rows
  against DOOM-0206's line height and HUD-safe height, which is a calculation over
  constants in the source and needs no running game — and **the user to confirm
  visually** at the L4 gate, because the harness cannot inject the keystrokes that
  open a menu under Wayland (§9). *Blocks:* L1's completion for the arithmetic
  half only. If it does not fit, that is DOOM-0206's scroll mechanism to
  exercise, not a reason to drop the row.
- **Q5 — how high does AMBIENT actually go on non-emissive art?** §4.2's floor
  argument originally assumed 1.0, and that is wrong: `mesh.frag` adds
  `GI_BOUNCE_STRENGTH * albedo * giIrradiance(...)` on top of `albedo * sect`,
  with nothing clamping the sum. Until the real ceiling is measured, no preset's
  ramp start can be shown to sit above it, so INV-4 is asserted rather than
  proven and the §4.5 values stay provisional. **Claude to measure** at L3 —
  capture the AMBIENT target's maximum over plain wall and floor geometry away
  from lamps on E1M1 (and one bounce-heavy room), and compare against each
  preset's `threshold − knee`. *Blocks:* L3's completion. If the ceiling exceeds
  1.00, the presets rise to clear it; that is a retune, not a redesign.
  **Answered at L3 on 2026-08-13; §7 L3 carries the numbers.** The ceiling is
  ~1.35, so §4.2's 1.0 assumption was indeed wrong — but no retune followed. The
  question was framed against a hard threshold, and the quadratic soft knee absorbs
  the difference: at the shipped presets the residual over plain wall sits at the
  capture harness's own noise floor. INV-4 is measured rather than asserted from
  here on, and the §4.5 values stop being provisional.

  **Closed 2026-08-25 by computing it instead of photographing it.** The L3 answer was
  a threshold sweep at three camera spots, which reads the *sum* and can only bound the
  rooms it was pointed at — and its ~1.20 at `3000 -4400 90` was in fact the ceiling
  lamps' DIRECT tail, not the bounce. The ceiling has a closed form: `mesh.frag` writes
  `AMBIENT = albedo * sect + albedo * giIrradiance(probe, n)` with `albedo ≤ 1` and
  `sect ≤ BASE_SECTOR_DIM`, and `giIrradiance` is an SH-L1 evaluation whose maximum over
  a normal has an exact expression. `RunGiBake` now takes it over every probe on the map
  during the readback it already performs, and prints the bound with the worst probe's
  position, so the photographic half of INV-4's test can be aimed at the surface the
  number describes rather than at a room somebody guessed was bright. On E1M1:
  **max giIrradiance 0.424 at (2080 -3648 80), AMBIENT bound 1.174**. The two methods
  agree within 0.03 where they overlap.

  The bound is deliberately conservative — per channel independently, over every normal
  DOOM can present, with `albedo`, `distLight` and AO all at their maxima — so it cannot
  read low, and the measured residual at that spot is far under it. The verdict, the
  per-preset weights and the captures are in INV-4's second 2026-08-25 amendment. Short
  version: **the ramp start does not clear the ceiling at Medium or High, and INV-4
  still holds at Medium with a photographic zero; High leaves 0.1–0.6/255 on the
  brightest plain wall, recorded and not retuned.**

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 Classic untouched | the two INV-1 greps, run at L4 |
| INV-2 `bloom 0` byte-identical | `ab_capture.sh` ×3 + `ab_diff.py` at L3 |
| INV-3 HUD/weapon/menu never bloomed | INV-3's structural grep + the strip compare + the weapon/menu block map |
| INV-4 paletted art does not bloom | the `kBloomPresets` floor read (`bloom_threshold_test`, which also holds `kAmbientSectorMax` to `mesh.frag`'s `BASE_SECTOR_DIM`) + `RunGiBake`'s `GI bounce ceiling` line + `ab_diff.py` block map at the probe that line names |
| INV-5 60 fps floor in Solid | `rb_profile` measurement at L4 |
| INV-6 the bloom pass is timed | INV-6's slot and `nq` greps at L4 |
| The profiler's LABELS still match its buckets | INV-6's third grep — the `bloom` label must appear in the `[raster_profile]` format string |
| INV-7 golden gate pinned | the INV-7 grep at L1 |
| INV-8 clause 1 — one preset table, both chains read it | the `kBloomPresets` grep at L4, and again when DOOM-0345 adds its consumer |
| INV-8 clause 2 — no tuning literal hardcoded in a bloom shader | the `bloom_*.comp` float-literal grep at L4, **reviewed by eye** against §4.3's kernel constants |
| INV-9 raster sky never generates bloom | sky-facing `ab_diff.py` capture at L3 |
| The extract and the composite compute the same `hdr` | L2's byte-identical refactor gate — a `scene_recombine.glsl` that dropped the AO blur fails it |
| The 21-row menu still fits | Q4's arithmetic at L1 (mechanical) **plus** a user look at L4 |
| The halo reads as light, not haze | **nothing** — a human look call (§10 Q1); no automated test can judge it |
| Solid and Ultra agree on what glows | **nothing** mechanical — §10 Q3 is a measurement plus a judgement |
| The preset values are the right ones | **nothing** — §10 Q1/Q2; the presets are tuning, not a contract |

**Three `nothing` rows out of sixteen** (counted from the table above, not
carried forward). All three are the same class — this feature's correctness is
mechanically checkable and its *look* is not — and that is the honest error
budget here, and why L4 is a human gate rather than a green test run.

The umbrella carried a fourth `nothing` row: the profiler's labels had no
mechanical check, because the print is a `printf` format string and nothing
compared it to the bucket it prints. Splitting the widening made that row
checkable — with only one bucket added on this chain, "the format string names
`bloom`" is a grep, and it is INV-6's third clause.

## 12. Cross-doc impact

- `CHANGELOG.md` — an `Added` entry, at L4 and not before.
- `ROADMAP.md` — DOOM-0331 flips to 🚧 at L1 and ✅ at L4. DOOM-0345's bullet
  records that it is blocked by this one.
- `docs/specs/DOOM-0345-bloom-ray-traced.md` — the sibling half. It cites this
  spec for the extract arithmetic (§4.2), the blur (§4.3), the preset table
  (§4.5) and the bloom targets (§5), and must not restate any of them.
- `docs/standards/renderer.md` — no change. The push-constant lane table is
  untouched (no new megakernel lane), and the shader list there is descriptive
  prose rather than an inventory.
- `docs/standards/performance.md` — no change. §6 uses the existing floor, the
  existing profiler and the existing comparison rule.
- `CLAUDE.md` — no change. The tier table already says effects belong to the
  view, not the tier, which is what §2 applies.
- `scripts/ab_capture.sh` — the tier-conditional HD check (§7), needed before
  the Solid arm of any look A/B can run.
- **The `-shotcompare` golden image — a re-bless is owed, and it is not this
  feature's to grant.** Shipping `bloom = 2` by default (§3 decision 4) and
  pinning it in the DOOM-0208 block (INV-7) means every `-shotcompare` run differs
  from the stored golden until that golden is re-captured. This adds to an
  existing debt rather than creating one: the pin block's own comment records that
  a re-bless is already owed for `rt_fog`. So L4 records that the re-bless is owed
  and leaves the decision to DOOM-0202, which owns the golden; it must not
  re-bless as a side effect of shipping bloom.
- `docs/specs/DOOM-0011-fix-ledger.md` — no change, but read row 9.4 before doing
  L4: it is the record of this project getting a profiler widening wrong, and §5's
  decision not to widen the pool here rests on it.

## 13. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 0-split | 2026-08-12 | 0 | 0 | 0 | 0 | 0 | **No reviewer dispatched.** Narrowed in place from the 1496-line umbrella of this same id, which had converged by cap at 3 loops with build-changing findings still arriving. The RT chain left for DOOM-0345; invariants renumbered from 1 and mapped in §2. The umbrella's 10-item deferred tail was folded in rather than re-reviewed, and its §10 Q3 was closed by the user as §3 decision 5. **No review history is inherited** — the umbrella's three loops ran against a document that no longer exists, so this part runs the gate from loop 1 on its own bytes. |
| 1 | 2026-08-12 | 2 | 5 | 1 | 1 | 1 | All 8 verified against the tree, **0 dismissed**, all 8 fixed. **Five of the eight were false claims about existing code**, and every one would have sent an implementer to the wrong place: INV-9 attributed the sky's DIRECT write to `composite.frag`, which only *reads* that target (`mesh.frag` writes it, and writes `outAmbient = vec4(0.0)` — so the sky's ambient term is exactly zero, which is what actually bounds it); §4.4 said "both chains already guard against non-finite radiance" when only the RT chain does, contradicting its own next paragraph; §5 instructed the implementer to bring a barrier forward that already fires immediately after the scene pass and before SSAO; §4.5 named `videoLabels` (indexed by menu *row*, never by a dial value) as a read site to clamp and called `rb_fog`'s already-guarded lookup a latent defect; and §6/INV-5 named `[cpu_profile]` for per-pass rows that only `[raster_profile]` prints. The Q2 was `srcTexelSize` carrying a per-pass ×2 factor that §5 defined away, the Q3 was the preset table's Off row having no values while INV-4 read "each row", and the Q4 was INV-6 pinning the whole `nq` line, which DOOM-0345 rewrites — so that clause would fail on an intact tree the moment the sibling landed. One collateral fix from the 4b sweep: §4.2's "falls below every ramp start" went imprecise once INV-9 was made exact — at the High preset the sky sits *on* the ramp start and extracts exactly zero. |
| 2 | 2026-08-12 | 2 | 1 | 2 | 3 | 2 | All 8 verified, **0 dismissed**, all 8 fixed. **The profiler site table was the serious one**: it listed three sites where the frame-order insert needs six — a *new* slot-4 write plus renumbering the two existing writes above it — so an implementer working the table would set `nq = 7u`, never write slot 6, and lose the entire `[raster_profile]` print to `VK_NOT_READY`. That is DOOM-0011 row 9.4's failure again, in the section that cites it. Two findings landed on loop 1's own fixes: the barrier paragraph loop 1 rewrote was still wrong in the other direction (the layouts *are* already correct, but the existing dependency's `dstStageMask` stops at `FRAGMENT_SHADER` and the extract is a compute dispatch), and the §2 split map missed DOOM-0345's INV-9. The rest were unspecified detail an implementer would have had to invent: `sceneRecombine` never pinned `textureLod` over `texture()`, which cannot compile in a compute stage at all; `bloomTex`'s filter mode was unpinned, and `NEAREST` gives blocky halos that every invariant still passes; INV-2's Ultra arm named no `rt_view 0`, and that key defaults to **6**, so the arm would have run the RT chain and passed without exercising this spec's combine; INV-8's "no shader literals" clause had no command that could see it; and L2 admitted its refactor might not be bit-identical while defining no response. One collateral fix from the 4b sweep: L4 still said "all three profiler sites". |
| 3 | 2026-08-12 | 2 | 4 | 3 | 1 | 1 | **Converged by cap.** All 9 verified, 0 dismissed, all 9 fixed; **no deferred tail**. Two were serious. First, the gate: §4.1 and Scope said `rb_rtdebug == 0`, but that is a strict *subset* of `!rtActive` — the raster chain also draws on any machine without working ray tracing, where `rb_rtdebug` sits at its persisted default of **6**. Gating on `rb_rtdebug == 0` would have made bloom silently absent on every non-RT machine at default config, with INV-2 passing because everything was off. Second, §4.2's floor rested on "paletted art tops out at 1.0 in the AMBIENT term", and `mesh.frag` **adds** `GI_BOUNCE_STRENGTH * albedo * giIrradiance(...)` on top of `albedo * sect` with nothing clamping the sum — so ordinary art in a bounce-lit room can exceed 1.0 and bloom, the very failure the feature exists to prevent. The floor is now a measured gate (§10 Q5, at L3) and INV-4 says it is asserted rather than proven. Also: the §4.5 row-count command returned **21**, not the 20 it claimed (the array's own opening brace matches `^[[:space:]]*{`) — both lanes ran it; the PBR-Neutral offset is keyed on the **min** channel, so compression begins at 0.76 for a saturated colour and 0.80 only for grey, and §4.2 is built on saturated emitters; §5 defined only the write→read barrier direction and never the return to `GENERAL` before each store, which bites on the first bloomed frame because `bloomImage[2]` is parked read-only; INV-8's allowed-literal list forbade the extract's own `0.0`/`2.0`/`4.0` shape constants while its regex could not match the `1e-4` it did allow; INV-6's `grep -A4` window could pass a mislabelled build; INV-2's baseline contradicted L2's own re-baseline hatch; and §5's dummy-timestamp snippet used `prof`, which is the RT record path's gate — the raster arm's is `rprof`. |
| 3-impl | 2026-08-12 | 0 | 0 | 0 | 0 | 0 | **No reviewer dispatched** — fold-back from implementing L1, which built green and cleared every L1 gate. Three clauses the review could not have caught, because a reader has no compiler and no config file. **(1)** The config key is `rt_bloom`, not `bloom`: every render-effect toggle in `m_misc.c` uses the `rt_` prefix, and §4.5's own "follow the `rb_fog` precedent exactly" *means* this, since `rb_fog` persists as `rt_fog`. **(2)** The "single clamp at config load" is dropped. `M_LoadDefaults` is a generic loop over `defaults[]` with no per-variable hook, so a bloom-only clamp there is a special case no sibling dial carries; the two index-site guards are the whole defence, and L1 verified a hand-edited `rt_bloom 9` boots and shuts down cleanly with the row reading "Off". **(3)** L1's list included the engine-side index guard, but nothing reads `kBloomPresets` until L2 — writing the accessor now would ship an unused static function, which `-Wall` flags and which is dead code by definition. It lands with its first caller. **Owed:** these are contract edits, so rule 14 wants a re-gate; it has not been run. |

| 3-L3 | 2026-08-13 | 0 | 0 | 0 | 0 | 0 | **No reviewer dispatched** — fold-back from implementing L2 and L3, both built green and cleared their gates. Two edits, neither a design change but the second one status-bearing. **(1)** §7 L2 and §7 L3 gained their measured gate results, including why L2's `max ≤ 1.0` clause did not fit what the harness returned and why L3 substituted a SPIR-V identity proof for a capture. Those are records, not contract. **(2)** §10 Q5 is answered and §4.5's preset values **stop being provisional** — the measured ceiling is ~1.35, above two presets' ramp starts, and INV-4 nevertheless holds because the quadratic soft knee's ramp start is not where extraction becomes visible. That last clause changes the table's status from provisional to settled, which is contract. **Owed:** rule 14 wants a re-gate for it, and it has not been run — this row now carries that debt jointly with `3-impl`. |
| 3-L4 | 2026-08-13 | 0 | 0 | 0 | 0 | 0 | **No reviewer dispatched** — fold-back from implementing L4, which built green, cleared INV-6's three greps and took §6's measurement. Three edits. **(1)** §7 L4 gained the measured result and the five-sites-not-six record, including why §5 site 6's `!bloomActive` dummy uses the wrong predicate and leaves slot 4 unwritten on a no-camera frame. A record of what was built, plus a correction to a site table the implementer has now superseded. **(2)** §6 gained the numerator the ≤ 5 % bound divides — the GPU-total delta between arms, not the bloom bucket, decided with the user after the two readings came back 2.8 points apart. **That is contract**: it changes what a future implementer measures and therefore what passes. **(3)** §7 L4 records that INV-5 was taken on a CPU-build-bound frame and re-arms if `fenceWait` climbs. **Owed:** rule 14 wants a re-gate for edit (2); the user chose 2026-08-13 to batch it with `3-impl` and `3-L3` rather than spend a pass per amendment, so all three land in one gate before the ROADMAP bullet flips. |

**What the umbrella's review bought, kept here because the reasoning is load-bearing
and the document it was written in is gone.** Three findings that would each have
shipped a defect, all in material this part still owns:

1. The bright pass thresholded **Rec.709 luminance** while every threshold, floor
   and preset was stated in **linear magnitude**. Those agree only for greys, so
   at the High preset a red fireball or pure-red lava at 4× white — luminance
   0.850 — extracted **zero**, a blue emitter 0.289, while a white wall at 1.0 sat
   exactly on the threshold. The feature would have shipped glowing pale walls and
   not lava, which is the roadmap bullet's own complaint with the causality
   reversed. Fixed by thresholding the **max channel** (§4.2).
2. The soft knee made the "threshold floor is 1.0" claim false — extraction begins
   at `threshold − knee` — so INV-4 and INV-9 were unsatisfiable and their own
   tests would have failed a faithful build. Fixed by constraining
   `threshold − knee ≥ 1.0` (§4.5).
3. The floor was claimed over a **sum** (`direct + ambient`) while only bounding
   `ambient`, so a lamp-lit wall could bloom and INV-4's test would have failed a
   faithful build. Now stated as the accepted, intended case it is (§4.2).
