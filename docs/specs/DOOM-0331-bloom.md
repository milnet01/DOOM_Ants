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
- **DOOM-0205** — the Render Effects submenu, which is where the dial goes.
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
  §11 What checks this

---

## 1. Goal

A light source looks like a light source. After this ships, anything genuinely
brighter than white — a ceiling lamp, a lit switch, a muzzle flash, a fireball,
glowing nukage, lava — bleeds a soft halo into the pixels around it, in Solid
and in Ultra, in the rasterised view and the ray-traced one. Ordinary art does
not: a plain wall at full sector light is not a light source and does not glow.

One dial in the Render Effects submenu (`Bloom: Off / Low / Medium / High`)
scales it, and Off restores the current picture exactly.

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0331? |
|-----------------|----------|-----------------------|
| Classic | paletted software renderer | **No** (INV-1) |
| Solid (`rb_rtdebug == 0`) | raster stack | **Yes** — §4.1 raster chain |
| Ultra with RT off (`rb_rtdebug == 0`) | raster stack | **Yes** — same chain |
| Ultra with RT on (`rb_rtdebug == 6`) | path tracer + denoiser | **Yes** — §4.1 RT chain |
| Path-tracer debug views (`rb_rtdebug` 1–4) | path tracer | **No** — §9 |

The gate is **which chain drew the frame**, not the tier label. Solid and Ultra
share one Vulkan backend (`renderer.md`), so both take the raster chain when RT
is off and the RT chain when it is on. That is why the feature is not, and must
not be, gated on Ultra: `CLAUDE.md` makes effects a property of the *view*, and
Solid's smoothness a protected property — hence the 60 fps measurement in §6
before this ships.

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
   cross the threshold. §10 Q1 owns whether that reads badly.
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

**Raster chain** (Solid, and Ultra with RT off). The frame today is: shadow
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

**RT chain** (Ultra with RT on, `rb_rtdebug == 6`). The frame today is:
megakernel → SVGF temporal → 4× a-trous → `svgf_composite.comp` (re-modulate,
re-add emission, fold fog, **tone-map**, write `rtImage`) → TAAU → blit →
`RecordRtOverlay` (weapon + HUD). Bloom splits the tone-map out of
`svgf_composite` (§4.6) and inserts itself in the gap:

```
megakernel
SVGF temporal
a-trous x4
svgf_composite.comp  -> rtHdr    (linear exposed radiance; alpha = sky flag)
bloom_extract_rt       (dispatch, half-res)   -> bloom[0]
bloom_blur             (dispatch, quarter-res, dir = +X) -> bloom[1]
bloom_blur             (dispatch, quarter-res, dir = +Y) -> bloom[2]
rt_tonemap.comp      -> rtImage  (rtHdr + bloom[2] * intensity, tone-mapped)  <-- combine
TAAU
blit -> swapchain
RecordRtOverlay      -> weapon + HUD, on top
```

Both combines therefore land **after the scene is fully shaded and before the
HUD exists** — which is what the roadmap bullet asked for, and what INV-3
locks.

### 4.2 The bright pass — what crosses the threshold

The threshold is applied to **the value the path is about to tone-map**, in that
path's own units. This is the one rule that makes the two views agree on what a
light source is:

- Raster: `composite.frag`'s `hdr`, i.e. `direct * aoDirect + ambient * ao`.
  No exposure multiply — the raster path applies none.
- RT: `svgf_composite.comp`'s `L * exposureEv(EV)`, the exposed radiance
  `toneEncode()` currently feeds to `pbrNeutralToneMapping`. Thresholding the
  *unexposed* `L` would make the dial mean something different at every
  Brightness-slider position.

Extraction, per source texel, with a soft knee so a surface crossing the
threshold ramps in rather than pops:

```glsl
// bloom_extract_*.comp — soft-knee bright pass (kBloomKnee = 0.5)
float  lum    = dot(c, vec3(0.2126, 0.7152, 0.0722));
float  soft   = clamp(lum - threshold + knee, 0.0, 2.0 * knee);
soft          = soft * soft / (4.0 * knee + 1e-4);
float  weight = max(soft, lum - threshold) / max(lum, 1e-4);
vec3   bright = c * weight;                 // 0 below the knee, c - threshold above
```

Two properties this shape has to keep:

- **The threshold floor is 1.0.** Paletted art at full sector light tops out at
  1.0 linear, so a threshold at or above 1.0 means no amount of ordinary
  texture can bloom — only light that is genuinely over-white. INV-5 and INV-10
  both rest on this; a preset below 1.0 breaks them.
- **The sky contributes nothing.** In RT the sky is written display-encoded and
  deliberately un-tone-mapped (`svgf_composite.comp`, the `gp.w < 0.0` branch),
  so it is not radiance and must not be thresholded as if it were; `rtHdr`'s
  alpha carries the flag and the extract multiplies it in. In raster the sky is
  written at paletted magnitude and falls below the 1.0 threshold on its own.

The extract runs at **half display resolution**, and each output texel is the
average of the four source texels under it, **thresholded first and averaged
after**. That order matters: averaging first would dilute a single bright texel
below the threshold and lose thin emitters (a distant lamp, a switch), which a
bilinear fetch cannot avoid. Thresholding first keeps a one-texel emitter's
excess energy at a quarter weight, which is correct and does not flicker as the
camera moves.

Both extracts write the whole half-res target. Under render scale the world
occupies only the `[0, uvScale]` corner of the raster scene targets and the
`[0, renderW) × [0, renderH)` corner of `rtHdr`, so each extract scales its
source coordinate by its own corner and the bloom targets stay mapped over the
**whole** frame — the same convention `aoImage` already uses, and the reason the
blur needs no edge clamping and nothing has to be reallocated when the
render-scale menu changes mid-frame.

### 4.3 The blur

Two dispatches at **quarter** display resolution, the same
`bloom_blur.comp` twice with the direction in a push constant. The first reads
the half-res `bloom[0]` with a bilinear sampler, so the ½ → ¼ downsample is
free and safe (the values are already thresholded, so bilinear cannot dilute
anything below the knee).

A 9-tap Gaussian collapsed to 5 bilinear-paired fetches, σ = 2.0 texels at
quarter res. At 1920×1080 that is σ ≈ 8 display pixels and a visible reach of
roughly 24 — a lamp halo, not a fog. The offset/weight table is generated from
σ in the build step, not hand-picked.

One level, not a pyramid. The roadmap bullet allowed "a separable blur
pyramid"; a single quarter-res level is the cheapest thing that can work and is
what "cheapest wins first" asks for. §9 records what a second and third level
would buy and what they would cost.

### 4.4 The combine

One additive term, applied to the pre-tone-map value:

```glsl
// composite.frag (raster) and rt_tonemap.comp (RT), the same two lines
if (bloomIntensity > 0.0)
    hdr += texture(bloomTex, vUV).rgb * bloomIntensity;
```

**A branch, not a multiply by zero.** `hdr + bloom * 0.0` is exact for finite
`bloom`, but a NaN or Inf that reached `bloom[2]` would survive the multiply and
poison the frame — and both paths already guard against non-finite radiance
(`if (any(isnan(L)) || any(isinf(L))) L = vec3(0.0)`), which says such values do
occur. With the branch, `rb_bloom == 0` cannot reach the add at all, which is
what makes INV-2 a structural guarantee rather than a floating-point argument.

When the dial is Off, none of the three dispatches is recorded either, so the
Off path costs nothing and reads nothing.

### 4.5 The dial

One integer, four presets, one menu row, following the `rb_fog` precedent
exactly.

| Name | Where | Value |
|---|---|---|
| `rb_bloom` | `r_vulkan.cpp`, `extern "C"` alongside `rb_fog` | 0 Off / 1 Low / 2 Medium / 3 High |
| `bloom` | `m_misc.c` config table, default `2` | persisted to `~/.doomrc` |
| `vid_bloom` | `m_menu.c` `videoitem_e`, after `vid_fog` | row label `"Bloom"`, hotkey `'m'` |
| `M_ChangeBloom` | `m_menu.c` | `rb_bloom = (rb_bloom + 1) % 4` |
| `kBloomPresets` | `r_vulkan.cpp`, one table | `{threshold, intensity}` per level |

`'m'` for "blooM", because `'b'` is already `vid_brightness`. (`'v'` is
currently used twice — `vid_fog` and `vid_debugviews` — a pre-existing collision
this feature does not touch and must not add to.)

Starting preset values, **to be tuned with the user on hardware** (§10 Q2) —
these are a defensible opening position, not a measurement:

| Level | Threshold | Intensity |
|---|---|---|
| Off | — | 0.00 |
| Low | 1.30 | 0.20 |
| Medium | 1.10 | 0.35 |
| High | 1.00 | 0.55 |

The table is defined **once** and read by both paths (INV-9). Whether the two
paths need different intensities — their lighting magnitudes are not identical,
so the same threshold may catch different things — is §10 Q3; the answer must
not be two tables.

No debug key. Every other effect in this family took one (`]` `[` `'` `;`), but
the A/B harness drives effects through a temp config, not the keyboard, and the
menu row is the player-facing control. §9 records the trade.

### 4.6 What the RT path needs that the raster path does not

The raster combine is free because `composite.frag` computes the HDR value and
tone-maps it in the same shader — the bloom slots between the two. The RT path
has no such gap: `svgf_composite.comp` computes `L` and tone-maps it in one
step, and the bloom cannot be added before a value that has not been extracted
yet.

So the tone-map moves out into its own pass:

- **`svgf_composite.comp` now writes `rtHdr`** instead of `rtImage`: linear
  exposed radiance (`L * exposureEv(EV)`), with `alpha = 1.0` for a shaded
  surface and `alpha = 0.0` for the sky. Everything it does before that —
  albedo re-modulation, emission re-add with the DOOM-0302 weight, the fog fold,
  the non-finite guard, the motion-vector write — is unchanged. The sky branch
  writes its display-encoded colour with `alpha = 0.0` and still returns early.
- **`rt_tonemap.comp` is new** and does what `toneEncode()` did:
  `if (a < 0.5) out = rgb; else out = toneEncode(rgb + bloom * intensity)`, then
  writes `rtImage`. The sky passes through untouched and unbloomed, exactly as
  today.
- Everything downstream — TAAU, the label stamp, the blit, `RecordRtOverlay`,
  the `-shotverify` capture — reads `rtImage` and is unchanged.

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

All swapchain-sized fractions, recreated on resize with the existing scene
targets, so a mid-frame render-scale change reallocates nothing:

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
                                                --------
                                                 21.76 MiB   (15.82 of it RT-only)
```

Arithmetic, not a measurement — `1920*1080*8 = 16588800`, and so on. Bounded and
non-growing: three fixed fractions plus one full-size image, all freed with the
swapchain. The three bloom images are shared by both paths because only one
chain runs per frame; the mode toggle already drains the device
(`vkDeviceWaitIdle` on `modeChanged`), so nothing aliases across the transition.

### New shaders

Four new compiled shaders, added to `SHADER_SRCS` in `linuxdoom-1.10/Makefile`,
plus one shared include, which is **not** in `SHADER_SRCS` (it is `#include`d,
never compiled on its own) and instead needs an explicit dependency line:

| File | Stage | Job |
|---|---|---|
| `bloom_extract_raster.comp` | compute | recombine AMBIENT/DIRECT/AO, threshold, 2×2 average |
| `bloom_extract_rt.comp` | compute | read `rtHdr`, threshold, 2×2 average, sky = 0 |
| `bloom_blur.comp` | compute | separable Gaussian, direction from a push constant |
| `rt_tonemap.comp` | compute | `rtHdr` + bloom → tone-map → `rtImage` |
| `formulas/scene_recombine.glsl` | include | the `direct * aoDirect + ambient * ao` formula |

Two files are edited: `composite.frag` (add the combine; take the recombination
from the new include instead of holding it inline) and `svgf_composite.comp`
(write `rtHdr` instead of tone-mapping to `rtImage`).

`glslc` emits no auto-dependency for a GLSL `#include` (`renderer.md`), so
`composite.frag.spv.h` and `bloom_extract_raster.comp.spv.h` both need an
explicit `scene_recombine.glsl` dependency line beside the existing
`pt_common.glsl` and `formulas/` rules, or an edit to the include will not
rebuild its consumers.

### Push constants

**No new RT megakernel lane.** `renderer.md` records that `misc6` and both pad
words are full and the next RT push value must open a `misc7`; this feature
adds none, because nothing it does happens inside `pathtrace.comp`. The bloom
passes carry their own small push blocks:

| Pass | Push contents |
|---|---|
| `bloom_extract_raster` | `uvScale.xy`, `aoEnable`, `threshold`, `knee` |
| `bloom_extract_rt` | `renderScale.xy`, `threshold`, `knee` |
| `bloom_blur` | `dir.xy`, `srcScale.xy` |
| `composite.frag` | existing `{uvScale, aoEnable, pad}` — `pad` becomes `bloomIntensity` |
| `rt_tonemap` | `renderScale.xy`, `bloomIntensity` |

`rt_tonemap` carries **no** exposure EV: `svgf_composite` already applied
`exposureEv(EV)` before writing `rtHdr` (§4.2 requires the threshold to be in
post-exposure units), so all that is left downstream is
`pbrNeutralToneMapping` + the sRGB encode. Passing EV twice would double-expose
the frame.

`composite.frag`'s existing push block already carries an unused `float pad`
after `aoEnable`, so the raster combine needs no layout change at all — the
intensity goes in the slot that is already reserved and ignored.

### GPU profiler slots

`performance.md` mandates measuring with the per-pass profiler, so a pass with
no timestamp is invisible to the instrument that is supposed to gate it. The
query pool is created with `qpci.queryCount = 8` and the readback reads
`nq = g.profRasterFrame ? 6u : 8u` — verified: the raster path writes slots
0–5, the RT path writes all of 0–7. Timestamps are differenced against their
neighbours, so the new slots are **inserted in frame order**, not appended:

- Raster: `... 3 = SSAO, 4 = bloom, 5 = composite, 6 = HUD` → `nq` 6 → 7. The
  five `g.profMs[]` assignments shift by one and gain a sixth.
- RT: `... 7 = svgf_composite, 8 = bloom, 9 = rt_tonemap`, then TAAU →
  `queryCount` 8 → 10 and `nq` 8 → 10.

The `!denoise` fallback that writes dummy timestamps into slots 5–7 (so a
non-mode-6 frame does not return `VK_NOT_READY` and drop the whole print) gains
the two new slots for the same reason.

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
| extract | 518,400 (½ res) | 4 source taps × (3 textures raster / 1 image RT), threshold |
| blur ×2 | 129,600 each (¼ res) | 5 bilinear taps |
| combine | 0 extra | folded into an existing pass (raster) / a new bandwidth-bound pass (RT) |
| `rt_tonemap` | render-res, RT only | 2 loads + tone-map + 1 store |

The measurement, at the L6 gate (§7), following the comparison rule — same map,
same render scale, reference GPU, both arms from the same build:

```
# Solid, 50% render scale, E1M1: bloom default vs bloom off
#   read the [cpu_profile] fps line and the composite/bloom pass rows
\   (rb_profile) with bloom=2, then with bloom=0
# Ultra RT, 50% render scale, E1M1: same pair, read [rt_profile]
```

Levers, cheapest first, if the budget is missed: drop the extract to quarter res
(§9 records why it is not there already); shrink the blur to 5 taps; on the RT
side, make `rtHdrImage` half-res-point-sampled instead of full res. The dial
itself is the player-facing lever — `Off` skips every pass — which is what
`performance.md` asks a heavy effect to ship with.

## 7. Build order

Each step ends with something observable. `make` and `make test` after every
one (`always-rebuild-engine`).

- **L1 — the dial, doing nothing.** `rb_bloom` + the `bloom` config key + the
  `vid_bloom` menu row + `M_ChangeBloom` + the `kBloomPresets` table + the
  `-shotverify` pin. No render change. *Verify:* the row cycles
  Off/Low/Medium/High, survives a restart (`grep bloom ~/.doomrc` — against a
  **temp** config, never the user's live file), and is absent in Classic.
- **L2 — the raster extract.** `bloomImage[0..2]`, `bloom_extract_raster.comp`,
  `formulas/scene_recombine.glsl`, and `composite.frag` refactored onto the
  include with the combine still absent. *Verify:* the frame is unchanged
  (`ab_diff.py` SIGNAL max 0.0 against the pre-L2 build) — the refactor is
  provably a no-op before anything is added on top.
- **L3 — the raster blur + combine.** `bloom_blur.comp` ×2 and the
  `composite.frag` add. *Verify:* a lamp in Solid gains a halo; `bloom 0` is
  byte-identical (INV-2); a plain wall does not move (INV-5).
- **L4 — the RT tone-map split.** `rtHdrImage`, `svgf_composite.comp` writing it,
  `rt_tonemap.comp` doing the encode, with the bloom term absent. *Verify:*
  Ultra RT is byte-identical to the pre-L4 build (this is the risky step, and it
  is deliberately separated so the split can be proven neutral on its own), and
  `-rtverify` still PASSes.
- **L5 — the RT extract + combine.** `bloom_extract_rt.comp` and the
  `rt_tonemap` add. *Verify:* the same lamp gains a halo in Ultra RT; the sky
  does not (INV-10); `bloom 0` is byte-identical.
- **L6 — profiler slots, then the gate.** The slot renumbering above, then the
  §6 measurement in both paths, `-rtverify`, and the human look call on
  hardware (§10). Only after that does the ROADMAP bullet flip and CHANGELOG
  gain an entry.

`scripts/ab_capture.sh` needs one change to serve L3: it exits 1 when the log
has no `HD load done` line, which is correct for an Ultra capture and wrong for
a Solid one (`EnsureHdMaterials` returns immediately unless
`rendermode == TIER_RT3D`, so a Solid run never prints it). The check becomes
conditional on the tier the run requested. Without that, the Solid arm of every
look A/B below cannot be captured at all.

## 8. Invariants

- **INV-1** — Classic renders byte-identically. Nothing bloom-related executes
  under `RB_CLASSIC`, and the menu row is not reachable there.
  *Test:* `grep -rl 'bloom' linuxdoom-1.10/*.c linuxdoom-1.10/*.cpp linuxdoom-1.10/*.h`
  names only `m_misc.c`, `m_menu.c` and `r_vulkan.cpp` (today the same command
  returns nothing — verified, `wc -l` = 0 — so the post-feature list is a clean
  delta).
  *Breaks when:* a bloom read is added to the software renderer or the present
  path it shares, or the `Bloom` row is added to `RendererDef` (the menu Classic
  actually shows).

- **INV-2** — `rb_bloom == 0` restores the current picture exactly. No extract,
  blur or combine is recorded, and the combine is behind a branch rather than a
  multiply by zero (§4.4).
  *Test:* `scripts/ab_capture.sh` at a fixed coordinate with `bloom 0`, against
  the same capture from the commit before L2, compared with
  `scripts/ab_diff.py` → SIGNAL mean 0.00, max 0.0. Run for both Solid and
  Ultra RT.
  *Breaks when:* a dispatch is recorded unconditionally, or the combine becomes
  `hdr += bloom * intensity` with no guard and a non-finite value reaches
  `bloom[2]`.

- **INV-3** — the HUD, status bar, menu and weapon sprite are never bloomed and
  never bloomed over. Both combines run before the overlay draw: in raster
  inside the `composite.frag` draw, which the overlay draw and `FlushMenuText()`
  follow within the same swapchain pass; in RT inside `rt_tonemap.comp`, which
  precedes the blit and therefore `RecordRtOverlay`.
  *Test:* capture `bloom 3` and `bloom 0` at the same coordinate and compare the
  status-bar strip alone — the bottom 19.5 % that `ab_diff.py` crops:
  `python3 -c "import numpy,sys;from PIL import Image;a,b=(numpy.asarray(Image.open(p).convert('RGB'),dtype=numpy.float32) for p in sys.argv[1:3]);h=int(a.shape[0]*0.805);print(numpy.abs(a[h:]-b[h:]).max())" on.png off.png`
  → `0.0`.
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
  does not. The threshold is applied to the pre-tone-map value and its floor is
  1.0, the ceiling of a paletted colour at full sector light.
  *Test:* E1M1, flashlight off, `bloom 3` vs `bloom 0` — `ab_diff.py`'s block map
  shows movement only in blocks containing a lamp, a lit switch or a liquid, and
  0.00 mean on plain wall and floor blocks. Quote the NOISE row beside it.
  *Breaks when:* a preset threshold drops below 1.0, or the extract is moved to
  read the post-tone-map image, where a wall at 1.0 linear sits at 0.8691 and a
  lamp at 4.0 sits at 0.9833 (§3 decision 1) — close enough that any threshold
  catching the lamp catches the wall too.

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
  *Test:* after L6 the raster path writes slots 0–6 and the RT path 0–9, and
  `nq` matches each. Symbol-anchored, so it survives edits above the sites:
  ```
  awk '/^extern "C" void RB_Vulkan_Present/,/^}/' linuxdoom-1.10/r_vulkan.cpp \
    | grep -o 'gpuTimerPool, [0-9]' | sort -u        # today: 0 1 2 3 4 5
  awk '/^void RecordRtTrace/,/^}/'   linuxdoom-1.10/r_vulkan.cpp \
    | grep -o 'gpuTimerPool, [0-9]' | sort -u        # today: 0 1 2 3 4 5 6 7
  grep -o 'uint32_t nq = g.profRasterFrame ? 6u : 8u;' linuxdoom-1.10/r_vulkan.cpp
  ```
  All three outputs above are the current tree's, verified. (`RB_Vulkan_Present`
  also holds `vkCmdResetQueryPool(..., 0, 8)`, whose `0` the first grep picks up
  — harmless, since slot 0 is genuinely written too.)
  *Breaks when:* a dispatch is added between two existing timestamps without
  inserting one — the earlier pass then absorbs its cost silently.

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

- **INV-10** — the sky never blooms. In RT the sky is flagged
  (`rtHdr.a == 0.0`) and the extract multiplies it out; in raster the sky is
  written at paletted magnitude and cannot reach a threshold at or above 1.0.
  *Test:* a sky-facing capture (E1M1's outdoor courtyard), `bloom 3` vs
  `bloom 0` → 0.00 mean delta in the sky blocks of `ab_diff.py`'s block map.
  *Breaks when:* the RT extract is moved above `svgf_composite`'s `gp.w < 0.0`
  early-out, or a raster threshold drops below 1.0.

### Trust boundary

None crossed, so no invariant above defends one. No file is read, no user input
is parsed beyond one integer that the menu and config layers clamp the same way
they clamp `rb_fog`, and no network, IPC or model output is involved. Recorded
explicitly rather than omitted, because an absent boundary section reads as an
oversight.

## 9. Alternatives considered (and rejected)

- **Extract after the tone-map.** One chain for both paths, no `svgf_composite`
  surgery, no `rtHdrImage`. Rejected by the user (§3 decision 1): the operator
  has already pulled a lamp and a lit white wall to within ~2.6× of each other,
  so the effect degrades into "anything pale glows" — the complaint the roadmap
  bullet opens with.
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
  emitters vanishing (§4.2), and a quarter-res extract needs 16 point taps per
  output pixel to do that — trading output pixels for fetches at no clear net
  gain. It is listed as a §6 lever because at that point the trade is being made
  against a measured number rather than a guess.
- **`B10G11R11_UFLOAT_PACK32` for the bloom and HDR targets.** Halves their
  bandwidth and memory; bloom values are non-negative, so the lost sign costs
  nothing. Rejected for v1 because `rtHdrImage` needs the alpha channel for the
  sky flag, and using one format for the bloom chain and another for `rtHdr`
  buys ~3 MiB for a second format to reason about.
- **A debug key** for bloom, matching `]` `[` `'` `;`. Rejected: the A/B harness
  drives effects through a temp config rather than the keyboard, the menu row is
  the player's control, and the free-key list is nearly exhausted.
- **Bloom on the path-tracer debug views** (`rb_rtdebug` 1–4). Rejected: those
  are diagnostics behind the Debug Views toggle, mode 4 tone-maps inside
  `pathtrace.comp` rather than in the composite, and adding a third hook site
  for a view that exists to show raw estimator output is a cost with no reader.

## 10. Open questions

Each names who answers it and what it blocks. None blocks drafting; all three
are look calls at the L6 gate.

- **Q1 — is the halo the right size and strength?** The σ = 2.0-at-quarter-res
  kernel and the intensity presets are a starting position. **User**, on
  hardware, at the L6 gate. If the halo reads too tight, §9's pyramid is the
  first lever; if it reads too strong, the intensity presets drop before the
  threshold does.
- **Q2 — does a flashlight on a white wall bloom, and does that read badly?**
  §3 decision 2 accepted the possibility. **User**, same gate. The fix if it
  does is a higher threshold floor, not an emissive gate — that was already
  weighed and rejected.
- **Q3 — do Solid and Ultra need different intensities?** Their lighting
  magnitudes are not identical, so the same threshold may catch different
  things. **Claude to measure** at L6 (capture the same coordinate in both and
  compare which blocks moved), **user to judge**. If they do differ, the answer
  is one named per-path scale constant applied to the shared table, never a
  second table (INV-9).

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 Classic untouched | the INV-1 grep, run at L6 |
| INV-2 `bloom 0` byte-identical | `ab_capture.sh` + `ab_diff.py` pair, both paths, at L3 and L5 |
| INV-3 HUD never bloomed | the status-bar-strip compare in INV-3 |
| INV-4 `-rtverify` unaffected | the INV-4 grep + a real `-rtverify` run at L4 and L5 |
| INV-5 paletted art does not bloom | `ab_diff.py` block map, E1M1, at L3 and L5 |
| INV-6 60 fps floor in Solid | `rb_profile` measurement at L6 |
| INV-7 every pass timed | the slot greps at L6 |
| INV-8 golden gate pinned | the INV-8 grep at L1 |
| INV-9 one preset table | the INV-9 grep at L6 |
| INV-10 sky never blooms | sky-facing `ab_diff.py` capture at L5 |
| The halo reads as light, not haze | **nothing** — a human look call (§10 Q1); no automated test can judge it |
| Solid and Ultra agree on what glows | **nothing** mechanical — §10 Q3 is a measurement plus a judgement |
| The preset values are the right ones | **nothing** — §10 Q1/Q2; the presets are tuning, not a contract |

Three `nothing` rows, all three the same class: this feature's correctness is
mechanically checkable and its *look* is not. That is the honest error budget
here, and it is why L6 is a human gate rather than a green test run.

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

## 13. Cold-eyes loop log

| Loop | Date | Lanes | CRIT | HIGH | MED | LOW | Outcome |
|------|------|-------|------|------|-----|-----|---------|
