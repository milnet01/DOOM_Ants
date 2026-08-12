# DOOM-0345 — Bloom on the ray-traced view: the tone-map split

**Status:** spec draft (2026-08-12).
**Kind:** feature.
**Source:** ROADMAP DOOM-0345 (`spec-split-2026-08-12`). Split out of the
1496-line DOOM-0331 umbrella, which converged by cap at three cold-eyes loops
with build-changing findings still arriving. Scope calls with the user
2026-08-07 and 2026-08-12 — see §3.
**Blocked by:** DOOM-0331, which ships the dial, the preset table, the blur
shader and the `bloomImage[0..2]` targets this spec reuses unchanged.

**Layman:** Make lamps, fireballs and glowing goo bleed light into the air
around them in the ray-traced view too — not just the fast one.

**Depends on:**

- **DOOM-0331** — the shared core. This spec adds no dial, no config key, no
  menu row, no preset table and no blur shader; it adds one extract, one
  tone-map pass and one HDR target.
- **DOOM-0009 / DOOM-0090** — the RT path's denoise chain.
  `svgf_composite.comp` re-modulates albedo, re-adds emission, folds fog, and
  tone-maps; `taau.comp` upscales; a blit puts the result on the swapchain. That
  pre-tone-map radiance is where this feature taps the traced picture.
- **DOOM-0096** — the Brightness slider (`rb_exposure`), whose exposure this
  spec moves one pass later. §3 decision 5 is why.

**Delivers / subsumes:** nothing. No existing roadmap item is closed by this.

**Defers (explicitly NOT in this build):**

- **Bloom on the path tracer's debug views** (`rb_rtdebug` 1–4). Those are
  Debug-Views diagnostics, not a play view; see §2 and §9.
- Everything DOOM-0331 defers — auto-exposure, lens dirt, streaks, anamorphic
  flares, FXAA, depth of field. Not restated here.

**Scope:** Solid and Ultra, in their **ray-traced** view (`rb_rtdebug == 6`).
The rasterised view is DOOM-0331; Classic is untouched by either. `rb_bloom == 0`
is byte-identical (INV-1), and the HUD and weapon are never bloomed (INV-2).

---

## Contents

- §1 Goal — §2 Where this sits — §3 Scope decisions — §4 Design
  (4.1 where it hooks · 4.2 the tone-map lift · 4.3 the extract ·
  4.4 the combine, and Off costing nothing) — §5 Data & resources —
  §6 Performance budget — §7 Build order — §8 Invariants —
  §9 Alternatives considered — §10 Open questions — §11 What checks this —
  §12 Cross-doc impact — §13 Cold-eyes loop log

---

## 1. Goal

The same lamp that glows in Solid glows in the ray-traced view, driven by the
same dial and the same preset numbers. After this ships, a ceiling lamp, a lit
switch, a muzzle flash, a fireball, glowing nukage and lava bleed a soft halo in
`rb_rtdebug == 6` exactly as they do in `rb_rtdebug == 0`, and the Brightness
slider changes how bright the picture is without changing which surfaces count as
light sources.

## 2. Where this sits

| Tier + RT state | Renderer | Touched by DOOM-0345? |
|-----------------|----------|-----------------------|
| Classic | paletted software renderer | **No** |
| Solid / Ultra, RT off (`rb_rtdebug == 0`) | raster stack | **No** — DOOM-0331 |
| **Solid, RT on** (`rb_rtdebug == 6`) | path tracer + denoiser | **Yes** — §4.1 |
| Ultra, RT on (`rb_rtdebug == 6`) | path tracer + denoiser | **Yes** — same chain |
| Path-tracer debug views (`rb_rtdebug` 1–4) | path tracer | **No** — gated, see below |

**The gate is which chain drew the frame, never the tier label**, and row 3 is
the one that proves it: `rtActive` in `RB_Vulkan_Present` is `rb_rtdebug &&
g.rtEnabled && g.tlas && g.rtModule && g.haveCamera && g.vbuf && g.atlasReady` —
**no `rendermode` term at all**, verified. Solid with the Ray Tracing row on runs
the RT chain, exactly as `CLAUDE.md` says. An implementer who gates this feature
on `rendermode == TIER_RT3D` ships something that vanishes in half its supported
configurations.

**The debug views are excluded by an explicit guard, not structurally.**
`rtActive` is true for `rb_rtdebug` 1–4 as well, so the split, the bloom
dispatches and `rt_tonemap` are recorded only when `rb_rtdebug == 6`. Without
that guard, those modes would either take a bloom they were never designed for or
read an `rtHdrImage` nothing wrote. Say it in code as one condition beside the
`rb_bloom` gate (§4.4), not as two scattered tests.

**Which of the debug modes tone-map inside `pathtrace.comp`: mode 4 only.**
Verified — `pathtrace.comp` carries its own local `toneEncode`, and its two call
sites both sit inside the `mode == 4u` branch (the surface store and that
branch's own sky arm). Modes 1–3 never call it; mode 2 stores a flat
`vec3(1.0)`. The umbrella spec said "modes 1–4" in one place and "mode 4" in
another; mode 4 is correct. It matters only as the reason the guard is written
against `rb_rtdebug == 6` rather than against "the modes that tone-map
themselves" — one condition, not a mode set to keep in sync.

## 3. Scope decisions (agreed with the user)

DOOM-0331 §3 carries decisions 1–4 (extract before the tone-map; brightness
rather than an emissive flag; four presets on one row; on by default at Medium).
They govern here unchanged and are not restated. **Decision 5 is the one this
spec pays for**, so its consequences are worked out here:

5. **What counts as a light source does not move with the Brightness slider.**
   Taken with the user 2026-08-12, closing the umbrella's §10 Q3, which had been
   blocking this spec's middle build step. The threshold is defined in **scene
   radiance units on both chains**.

   On the raster chain that is free — `composite.frag` applies no exposure at
   all. On this chain it is not, because `svgf_composite.comp`'s `toneEncode`
   multiplies by `exposureEv(EV)` before compressing, where `EV` is the player's
   `rb_exposure` arriving in `pc.misc3.x`. Threshold the value that shader hands
   to the operator and a texel blooms iff `L × exposureEv(EV) > T`, i.e. iff
   `L > T / exposureEv(EV)` — the physical radiance required moves with the
   slider.

   **So the exposure moves one pass later.** `svgf_composite.comp` writes
   *unexposed* radiance into `rtHdrImage`; the extract thresholds that directly,
   in the same units and against the same preset numbers the raster extract uses;
   and `rt_tonemap.comp` applies `exposureEv(EV)` once, after the bloom has been
   added. The halo is then exposed along with everything else, so the slider
   still does exactly what a brightness slider should — it scales the whole
   picture — and changes nothing about which surfaces qualified.

   This is a different mechanism from the one the umbrella described, and
   **strictly less surgery**: because the exposure now sits downstream, the whole
   of `toneEncode` moves out intact and is shared rather than being split into an
   exposure-free half (§4.2). One rule the umbrella wrote is inverted by it and is
   called out where it lives (§5: `rt_tonemap` **does** carry an EV).

   **The accepted trade** — that threshold and tone-map knee are no longer in the
   same units on a chain that applies exposure — is DOOM-0331 §3 decision 5's to
   state, and the residual look question is its §10 Q3. Not restated.

## 4. Design

Four stages, the same as the raster chain: **extract → blur → blur → combine**.
Two of the four are DOOM-0331's, unchanged. What is new here is that this chain
has no gap to combine *into*, so one has to be made.

### 4.1 Where it hooks

The ray-traced view, `rb_rtdebug == 6`, in **either** tier. The frame today is:

```
megakernel -> SVGF temporal -> a-trous x4 -> svgf_composite.comp
   (re-modulate, re-add emission, fold fog, TONE-MAP, write rtImage)
   -> TAAU -> blit -> RecordRtOverlay (weapon + HUD)
```

Bloom splits the tone-map out of `svgf_composite` (§4.2) and inserts itself in
the gap that creates:

```
megakernel
SVGF temporal
a-trous x4
                      --- rb_bloom == 0: unchanged from today ---
svgf_composite.comp  -> rtImage  (tone-maps in place, as it does now)
                      --- rb_bloom  > 0: the -DBLOOM_SPLIT variant ---
svgf_composite.comp  -> rtHdrImage  (UNEXPOSED linear radiance; alpha = sky flag,
                                     and the SKY branch keeps its own encode)
bloom_extract_rt       (dispatch, half-res)               -> bloomImage[0]
bloom_blur             (dispatch, quarter-res, dir = +X)  -> bloomImage[1]
bloom_blur             (dispatch, quarter-res, dir = +Y)  -> bloomImage[2]
rt_tonemap.comp      -> rtImage  (rtHdrImage + bloom * intensity, exposed and tone-mapped)
                      --- both paths rejoin here ---
TAAU
blit -> swapchain
RecordRtOverlay      -> weapon + HUD, on top
```

The combine lands **after the scene is fully shaded and before the HUD exists**,
which is what INV-2 locks. `bloom_blur.comp` is DOOM-0331's shader, used
unmodified — it reads and writes only bloom targets and knows nothing about which
chain filled them.

**What TAAU then does to the halo is a look question, not a correctness one.**
The combine sits *before* the temporal upscaler, so the bloom is reprojected and
accumulated with everything else. For a static lamp that is free and correct. For
a muzzle flash — one bright frame every few tics — TAAU's history will smear the
halo across the following frames, which may read as a pleasing afterglow or as
ghosting. Combining *after* TAAU would avoid it and costs a display-res pass
instead of a render-res one (§9). Judge it on hardware; §10 Q1 carries it.

### 4.2 The tone-map lift

The raster combine is free because `composite.frag` computes the HDR value and
tone-maps it in the same shader — the bloom slots between the two. This chain has
no such gap: `svgf_composite.comp` computes `L` and tone-maps it in one step, and
a bloom term cannot be added to a value that has already been compressed.

So, on the split path only (`rb_bloom > 0`, §4.4), the **surface** tone-map moves
out into its own pass.

**`toneEncode()` is lifted whole, not split.** As shipped it is a local function
inside `svgf_composite.comp`:

```glsl
vec3 toneEncode(vec3 L)
{
    float EXPOSURE_EV = uintBitsToFloat(pc.misc3.x);
    L = max(L, vec3(0.0)) * exposureEv(EXPOSURE_EV);
    L = pbrNeutralToneMapping(L);
    return vec3(linearToSrgb(L.r), linearToSrgb(L.g), linearToSrgb(L.b));
}
```

It reads that shader's own push block, which is why it is **not** a shared helper
today and cannot simply be called from a second shader. Under §3 decision 5 the
exposure stays *downstream* of `rtHdrImage`, so the fix is to parameterise it on
the EV rather than to carve the exposure out of it:

```glsl
// formulas/tonemap_encode.glsl  (new)
vec3 toneExposeEncode(vec3 L, float ev);
// == today's toneEncode, with the EV passed in instead of read from pc.misc3.x
```

Three consumers, and each calls it exactly once per pixel:

- `svgf_composite.comp`, **un-split variant**, surface path — `toneExposeEncode(L,
  EV)` → `rtImage`. Byte-identical to today by construction: same arithmetic,
  same EV, same store.
- `svgf_composite.comp`, **either variant**, sky branch — see below.
- `rt_tonemap.comp` — `toneExposeEncode(L + bloom, EV)` → `rtImage`.

**It needs a new include of its own; it must NOT go in
`formulas/pbr_neutral_tonemap.glsl`.** `linearToSrgb` and `exposureEv` both live
in `formulas/formulas.glsl`, and `composite.frag` includes only
`pbr_neutral_tonemap.glsl` — so defining `toneExposeEncode` there gives
`composite.frag` a body calling undeclared functions, which is a compile error in
GLSL whether or not the function is ever called. Put it in a third new file,
`formulas/tonemap_encode.glsl`, which `#include`s both prerequisites, and include
*that* only from `rt_tonemap.comp` and `svgf_composite.comp`. `composite.frag`
keeps calling `pbrNeutralToneMapping` directly — it deliberately does not
sRGB-encode.

**What the split variant changes, and nothing more:**

- **`svgf_composite.comp` writes `rtHdrImage`** instead of `rtImage` for the
  surface path: `max(L, vec3(0.0))` — **unexposed**, post-fog, with
  `alpha = 1.0`. Everything it does before that — albedo re-modulation, emission
  re-add with the DOOM-0302 weight, the fog fold, the non-finite guard, the
  motion-vector write — is unchanged. The non-negative clamp travels with the
  store because the operator downstream assumes it.
- **The sky branch is not moved and not changed at all.** This is the trap in
  this section. The `gp.w < 0.0` branch is *not* simply "display-encoded, no
  tone-map": when `pc.misc3.y != 0u` it calls
  `sky = toneEncode(skyLin * fog.a + fog.rgb)` to fold fog in linear and
  re-encode — and `rt_fog` **defaults to 1**, so that is the shipped path, not an
  edge case. An implementer told "the tone-map moves out" who deletes the encode
  from this branch ships a linear, un-encoded fogged sky. So: the sky branch
  calls `toneExposeEncode(skyLin * fog.a + fog.rgb, EV)` — the same arithmetic it
  performs today, under the shared name — and writes its finished
  display-encoded colour into `rtHdrImage` with `alpha = 0.0`, keeping its
  existing `clamp(sky, 0.0, 1.0)` and its early return. INV-4 locks it.
- **Exposure is applied exactly once on every pixel, and the two arms apply it in
  different passes.** Surface pixels on the split path: once, in
  `rt_tonemap.comp`. Surface pixels on the un-split path: once, in
  `svgf_composite.comp`. Sky pixels, either path: once, in `svgf_composite.comp`,
  and `rt_tonemap` must not touch them. Leaving the exposure in the split
  variant's surface store *and* applying it downstream double-exposes the frame —
  a mistake that looks like a brightness bug rather than a logic one. INV-6 locks
  it.

**The split path is not bit-exact, and R1's gate must not ask it to be.**
`rtHdrImage` is `kSceneFormat` (fp16, ~11-bit mantissa), so a value that reaches
the tone-map through it has been rounded once more than today's fp32-register
path before being quantised to `rtImage`'s 8 bits. Most pixels land on the same
byte; some near a rounding boundary will not. So R1's `bloom 2` arm is gated at a
tolerance rather than byte-identity (§7). Storing unexposed rather than exposed
radiance does not change this: fp16's relative precision is scale-invariant, so
the extra rounding step is the same size either way.

**This does not weaken INV-1.** `rb_bloom == 0` runs the *un-split* pipeline,
which never touches `rtHdrImage` at all, so the Off arm stays byte-exact and
INV-1 needs no tolerance. The tolerance belongs to R1's split arm alone.

### 4.3 The extract

`bloom_extract_rt.comp`, half display resolution, reading `rtHdrImage`.

**The arithmetic is DOOM-0331 §4.2's, unchanged and not restated here** — the
same soft-knee bright pass over `peak = max(c.r, max(c.g, c.b))`, the same
threshold and knee from the same `kBloomPresets` entry, the same
threshold-each-source-texel-then-average gather order, the same non-finite clamp
as its first act. Because §3 decision 5 put both chains' thresholds in scene
radiance units, there is nothing chain-specific left in the maths: **no exposure
term appears in this shader, and no EV is passed to it.**

Two things differ from the raster extract, and only two:

- **The source is one image, not three textures.** `rtHdrImage` already holds the
  fully-shaded radiance, so there is no recombination step and
  `formulas/scene_recombine.glsl` is not included here.
- **The sky is masked out.** `rtHdrImage.a == 0.0` flags a sky pixel, whose value
  is display-encoded rather than radiance (§4.2) and must not be thresholded as
  if it were. The extract multiplies the flag in, so a sky texel contributes
  exactly zero. INV-5 locks it.

**Tap count and the render-scale ratio.** DOOM-0331 §4.2's table governs — the
world fills only the `[0, renderW) × [0, renderH)` corner of `rtHdrImage`, so the
source-to-output ratio is `renderScale / 50 %` and the tap count is `ceil()` of
it, clamped to the source region: 1×1 at the 50 % default, 2×2 at 67 %, 75 % and
100 %. The extract reaches its source by scaling *inward*, at
`p * 2 * renderExtent / dispExtent` — the push field is `renderExtent`, in pixels
(§5); there is no separate `renderScale` uniform.

**The extract writes every texel of `bloomImage[0]`**, i.e. the whole
`[0, dispW/2) × [0, dispH/2)` target with no unwritten region, exactly as the
raster extract does — the bloom targets are mapped over the whole frame. This is
the property that misplaces the entire halo in Ultra at the 50 % default if it is
read the other way round, so it is stated rather than inferred.

### 4.4 The combine, and Off costing nothing

`rt_tonemap.comp` is new and finishes the surface path. The complete body, sky
arm included — **omitting the sky arm tone-maps the sky twice**, which is §4.2's
trap arriving one shader later:

```glsl
// rt_tonemap.comp — compute; p is from gl_GlobalInvocationID, render-res.
vec4 h = imageLoad(rtHdrImage, p);
if (h.a < 0.5) { imageStore(rtImage, p, vec4(h.rgb, 1.0)); return; }  // sky: already encoded
vec3 L = h.rgb;
if (pc.bloomIntensity > 0.0) {
    // p is render-res; normalising by the RENDER extent already gives the
    // full-frame UV the bloom targets are mapped in (4.3) - no second factor.
    vec2 uv = (vec2(p) + 0.5) / vec2(pc.renderExtent);
    L += texture(bloomTex, uv).rgb * pc.bloomIntensity;
}
imageStore(rtImage, p, vec4(toneExposeEncode(L, pc.ev), 1.0));
```

The sky arm carries the already-encoded sky through untouched and unbloomed,
exactly as today. **That is a deliberate asymmetry with the raster chain**, which
has no sky test and therefore lets a lamp beside a sky edge bleed its halo onto
sky pixels there. DOOM-0331 INV-9 records the raster behaviour as the physically
right one; the RT behaviour falls out of the sky being display-encoded rather
than radiance, and neither is worth extra machinery to change.

**A branch, not a multiply by zero.** `L + bloom * 0.0` is exact for finite
`bloom`, but a NaN or Inf that reached `bloomImage[2]` would survive the multiply
and poison the frame — and this chain already guards against non-finite radiance
(`if (any(isnan(L)) || any(isinf(L))) L = vec3(0.0)` in `svgf_composite`), which
says such values do occur.

**Off must cost nothing, and on this chain that takes one extra measure.** The
raster chain gets it free: with the dial Off it records none of its dispatches
and is done. This chain cannot, because §4.2 moves its tone-map into a new pass —
so a naive split leaves Ultra paying for an extra render-res pass and an HDR
round-trip (~4 MiB written and ~4 MiB read back at the 50 % default, so ~8 MiB
round-trip; the 15.8 MiB in §5 is the allocation, not the per-frame cost) on
every frame, *including* frames where bloom is switched off. `performance.md`
requires a heavy effect's toggle to be a real opt-out, so the split is itself
gated:

- `rb_bloom == 0` → `svgf_composite.comp` tone-maps straight to `rtImage`,
  exactly as today, and neither `rt_tonemap.comp` nor `rtHdrImage` is touched.
- `rb_bloom > 0` → `svgf_composite.comp` writes `rtHdrImage` and
  `rt_tonemap.comp` finishes the job.

Two pipelines from one source, selected by a `-DBLOOM_SPLIT` define, following
the `mesh_overlay.frag` precedent already in the Makefile
(`glslc ... -DSINGLE_TARGET`). `rtHdrImage` is allocated up front either way —
allocation is cheap and per-frame reallocation is not — but it is written only on
the split path.

This is what makes INV-1's "byte-identical when Off" hold on this chain rather
than only on the raster one, and it is why §6's Off row is genuinely zero.

## 5. Data & resources

### New render target

DOOM-0331 §5 allocates `bloomImage[0..2]` and this spec reuses them; only one
image is added here:

| Image | Size | Format | Usage | Used by |
|---|---|---|---|---|
| `rtHdrImage` | full display | `kSceneFormat` | STORAGE | RT chain only |

```
rtHdrImage     1920 x 1080 x 8 = 16,588,800 B  = 15.82 MiB
```

Arithmetic, not a measurement — `1920*1080*8 = 16588800`. Bounded and
non-growing: one full-size image, freed with the swapchain. Swapchain-sized for
the same reason `rtImage` is, namely that `rb_renderscale` changes per frame from
the menu and a render-res allocation would have to be recreated mid-play.

### Descriptors, layouts and barriers

**Barriers between the passes — and the hops are not all the same kind.** Each
pass reads the previous one's output, so every hop needs an execution and memory
dependency, and the existing `svgfBarrier()` helper is the idiom. The **layout**
differs by image, and stating one rule for the whole chain gets it wrong:

- **`rtHdrImage` stays in `GENERAL` for its whole life.** Its usage is STORAGE
  only (the table above) and both of its readers reach it with `imageLoad`
  (`bloom_extract_rt` in §4.3, `rt_tonemap` in §4.4). So the
  `svgf_composite` → extract hop and the → `rt_tonemap` hop take a memory barrier
  with `oldLayout == newLayout == GENERAL`. Transitioning it to
  `SHADER_READ_ONLY_OPTIMAL` is **invalid** — that layout is for a sampled
  descriptor, and this image is never sampled.
- **`bloomImage[0..2]` do take `GENERAL` → `SHADER_READ_ONLY_OPTIMAL`**, because
  DOOM-0331 §5 gives them STORAGE **+ SAMPLED** and their readers sample them
  (`texture(bloomTex, …)`). That covers extract → blurH → blurV, and leaves
  `bloomImage[2]` in `SHADER_READ_ONLY_OPTIMAL` for `rt_tonemap`'s bloom fetch.

**`bloomImage[2]` needs no park on this chain** — DOOM-0331 already parks it in
`SHADER_READ_ONLY_OPTIMAL` at creation for the composite's benefit, and
`rt_tonemap`'s bloom fetch sits behind the same `bloomIntensity > 0.0` branch, so
it never samples an unwritten image.

**Pipelines and descriptor sets.** Two new compute shaders means two pipelines,
two set layouts and their pool allocations: `g.bloomExtractRtPipeline` and
`g.rtTonemapPipeline` — the latter is the identifier INV-2's ordering test greps
for, so it is named here rather than left to the implementer. In addition, the
`-DBLOOM_SPLIT` variant of `svgf_composite.comp` needs its own pipeline **and** a
descriptor that points its output binding at `rtHdrImage` instead of `rtImage`
(today binding 7 of `g.svgfDs` re-uses `g.rtView`). The `labelTaauDs` pattern — an
extra set that retargets one binding at a different image, layout-compatible with
the same pipeline layout — is the precedent already in the file.

### New shaders

Two new compiled shaders, added to `SHADER_SRCS` in `linuxdoom-1.10/Makefile`,
plus one shared include, which is **not** in `SHADER_SRCS` (it is `#include`d,
never compiled on its own) and instead needs an explicit dependency line:

| File | Stage | Job |
|---|---|---|
| `bloom_extract_rt.comp` | compute | read `rtHdrImage`, threshold, gather, sky (`a == 0`) → 0 |
| `rt_tonemap.comp` | compute | `rtHdrImage` + bloom → expose → tone-map → `rtImage` |
| `formulas/tonemap_encode.glsl` | include | `toneExposeEncode` (§4.2) |

One existing shader is edited: `svgf_composite.comp` — call the shared
`toneExposeEncode` in place of its local `toneEncode`, and on the `-DBLOOM_SPLIT`
variant write unexposed radiance to `rtHdrImage` instead of tone-mapping to
`rtImage`.

`glslc` emits no auto-dependency for a GLSL `#include` (`renderer.md`), so
`formulas/tonemap_encode.glsl` needs its own dependency line naming its two
consumers, since it includes two other files that can each change under it. The
`-DBLOOM_SPLIT` variant of `svgf_composite.comp` needs its own `.spv.h` rule,
modelled on the existing `mesh_overlay.frag.spv.h` rule (`glslc ...
-DSINGLE_TARGET`) — the one precedent in this Makefile for two binaries from one
shader source. **That precedent is two edits, not one:** the rule itself, *and* a
`SHADER_HDRS+=` append, whose existing comment says it is there "so it builds +
gates `r_vulkan.o` like the other shader headers". Without the append the
variant's header is never a prerequisite of `r_vulkan.o` and a stale SPIR-V blob
links silently.

### Push constants

**No new RT megakernel lane.** `renderer.md` records that `misc6` and both pad
words are full and the next RT push value must open a `misc7`; this feature adds
none, because nothing it does happens inside `pathtrace.comp`. The new passes
carry their own small push blocks:

| Pass | Push contents |
|---|---|
| `bloom_extract_rt` | `renderExtent.xy`, `threshold`, `knee` |
| `rt_tonemap` | `renderExtent.xy`, `bloomIntensity`, `ev` |

**`rt_tonemap` carries the EV, and this inverts a rule the umbrella wrote.** That
document said `rt_tonemap` must carry *no* exposure, because `svgf_composite` had
already applied it before the store — passing it twice would double-expose. Under
§3 decision 5 the exposure is no longer applied upstream, so it must be applied
here, and exactly here. The invariant that replaces the old rule is INV-6: applied
once per pixel, in whichever pass owns that pixel's arm. The `ev` is the same
`uintBitsToFloat(pc.misc3.x)` value `svgf_composite` receives, forwarded by the
host from `rb_exposure` through the existing DOOM-0096 mapping.

Correspondingly, **`bloom_extract_rt` carries no EV at all** — it thresholds
unexposed radiance against the shared preset, which is the whole point of
decision 5.

### GPU profiler slots

`performance.md` mandates measuring with the per-pass profiler, so a pass with no
timestamp is invisible to the instrument that is supposed to gate it. **This spec
owns the query-pool widening; DOOM-0331 deliberately does not touch it.** Verified
against the current tree, the raster chain writes slots 0–5 and the RT chain 0–7,
so DOOM-0331's single inserted raster slot still fits the pool's existing eight
and only this spec's two appended RT slots force it to ten.

**Widening it touches nine sites, not two.** DOOM-0011 already paid for this
lesson: its fix ledger's row 9.4 records a profiler widening that "named 3 sites;
there are 7", the misses including `uint64_t ts[8]`, a fixed stack array
`vkGetQueryPoolResults` would have written 72 bytes into. Do not re-learn it.
Every site, verified present in the current tree:

| # | Site | Today | After |
|---|---|---|---|
| 1 | `qpci.queryCount` (pool creation) | `8` | `10` |
| 2 | `uint64_t ts[8]` (readback stack array) | `8` | `10` — **overflows silently if missed** |
| 3 | `double profMs[8]` (`VulkanState` member) | `8` | `10` |
| 4 | `for (int pi = 0; pi < 8; pi++)` (report reset) | `8` | `10` |
| 5 | `vkCmdResetQueryPool(g.cmd, g.gpuTimerPool, 0, 8)` in `RecordRtTrace` | `8` | `10` |
| 6 | `vkCmdResetQueryPool(g.cmd, g.gpuTimerPool, 0, 8)` in `RB_Vulkan_Present` | `8` | `10` |
| 7 | `uint32_t nq = g.profRasterFrame ? 7u : 8u` | `7 : 8` | `7 : 10` — RT arm only; the `7` is DOOM-0331's |
| 8 | a **new** `if (prof && !bloomActive)` dummy-timestamp block | — | writes **8,9**. The existing `if (prof && !denoise)` block is **unchanged** at 5,6,7 — see below |
| 9 | the `[rt_profile]` `printf` — format string **and** its `profMs[0..7]` args | 8 buckets | 10 |

**Site 9 corrupts the measurement rather than breaking it**, which makes it the
easiest to miss: the print passes its `profMs[]` values against hard-coded
labels, so a widened bucket list with an unwidened format string prints a
plausible, wrong table that §6's gate would then read as fact. Format string
*and* argument list change together.

Sites 5 and 6 are two *separate* reset calls — one per chain — and both must
move, or the chain whose reset was missed queries unreset slots and
`vkGetQueryPoolResults` drops the entire print that §6's gate depends on.

**Site 8 exists because the bloom dial is itself a gate.** `nq` is a
compile-time-shaped constant, but slots 8 and 9 are only *written* when
`rb_bloom > 0` (§4.4 skips every bloom dispatch when the dial is Off). A
reset-but-unwritten slot returns `VK_NOT_READY` and the readback drops the
**whole** print — which would kill the profiler on precisely the `bloom 0` arm
that §6's measurement and INV-9 compare against. So slots 8 and 9 get a
dummy-timestamp block of their own, mirroring the existing mode-gate one:

```
if (prof && !bloomActive)   // mirrors the existing `if (prof && !denoise)` block
{
    // collapse the bloom slots onto the preceding point so their segments read ~0
    vkCmdWriteTimestamp(..., <RT slots 8 and 9>);
}
```

**One block, and the existing `!denoise` block must NOT be extended to cover 8
and 9.** `denoise` is `(rb_rtdebug == 6)` and bloom runs only in mode 6, so
`!denoise` **implies** `!bloomActive`: the new block already fires on every frame
the old one does. Extending both leaves two `vkCmdWriteTimestamp` calls writing
each of slots 8 and 9 between resets, which is invalid. Extending the old one
*instead of* adding this one is worse still — on the ordinary denoiser-on,
`bloom 0` frame neither block fires, the two slots are reset but never written,
and the entire `[rt_profile]` print vanishes, which is the failure this site
exists to prevent.

**RT appends rather than inserts, because its existing slots are already out of
chronological order.** Its write order is chronologically `0,1,2,5,6,7,3,4` —
slot 3 is written after TAAU and slot 4 after the blit, with 5–7 a sub-breakdown
sitting *inside* the `[2,3]` interval. So the new passes take slots
**8 (bloom+blur)** and **9 (`rt_tonemap`)**, placed chronologically between slot 7
(`svgf_composite`) and TAAU. Renumbering RT "in frame order" instead would break
`profMs[4..7]`.

That append has one consequence that must be written down, because it is exactly
the silent-absorption failure INV-7 exists to catch: `profMs[7]` (TAAU) is
currently `ts[3] - ts[7]`, which after the insert would span bloom +
`rt_tonemap` + TAAU. It becomes **`ts[3] - ts[9]`**, with the two new buckets
`profMs[8] = ts[8] - ts[7]` (bloom) and `profMs[9] = ts[9] - ts[8]`
(`rt_tonemap`). `profMs[2]` (`ts[3] - ts[2]`) keeps working but its label goes
stale: it is the umbrella bucket, today "denoiser chain + TAAU", and its interval
now also spans bloom and `rt_tonemap`. Update the comment and the printed word,
or the two new buckets read as double-counted.

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

Verified both ways against the current tree: today it returns exactly six lines
(sites 1–6, the two resets being two of them), and it does **not** match the
post-R3 `g.profMs[8] += … ts[8] …` use lines. Expected output after R3 is empty.

### No new external dependency

Nothing is added to the dependency set. `glslc` and `xxd` already build every
shader; the threshold and the tone-map are arithmetic.

## 6. Performance budget

**Budget: ≤ 5 % of present-total.** Ultra's frame-rate floor is a target rather
than a guarantee (`performance.md`, and it already sits near 45 fps), so the gate
here is the relative bound, not an absolute frame rate. Solid's 60 fps floor is
DOOM-0331 INV-5's and is measured on the raster chain; this spec does not touch
that chain.

No number here is measured yet, and none will be quoted until it is. What the
design costs, structurally:

| Pass | Output pixels @ 1920×1080 | Work per pixel |
|---|---|---|
| extract | 518,400 (½ res) | 1 source tap at the default 50 % scale, 4 at 100 % (§4.3) × 1 image, threshold |
| blur ×2 | 129,600 each (¼ res) | 5 bilinear fetches |
| `rt_tonemap` | render-res | 2 loads + expose + tone-map + 1 store, bandwidth-bound |

`rt_tonemap` is the only row that is a *new pass* rather than added work inside an
existing one, and §4.4 gates it on `rb_bloom > 0`, so the Off row of the
measurement below is genuinely zero.

The measurement, at the R3 gate (§7), following the comparison rule — same map,
same render scale, reference GPU, both arms from the same build:

```
# Ultra RT, 50% render scale, E1M1: bloom default vs bloom off
#   [rt_profile]  -> the new bloom and rt_tonemap buckets   (the numerator)
#   [cpu_profile] -> present-total                          (the denominator)
\   (rb_profile) with bloom=2, then with bloom=0
```

Levers, cheapest first, if the budget is missed:

1. **Shrink the blur from 9 taps (5 fetches) to 5 taps (3 fetches).** Cuts the
   blur's fetch count by 40 %; costs reach, which DOOM-0331 §4.3 already calls
   tight. This changes the shared shader, so it moves the raster halo too.
2. **Drop the extract to quarter res.** DOOM-0331 §9 records why it is not there
   already, and that the argument is scale-dependent.
3. **Give `rtHdrImage` a packed format.** `B10G11R11_UFLOAT_PACK32` is 4 bytes
   instead of 8, halving its 15.8 MiB and its bandwidth; §9 records the catch (it
   has no alpha, and the sky flag currently rides there, so the flag would need
   another home such as a sentinel radiance value).

**Not a lever:** making `rtHdrImage` smaller than the display. It carries the
whole scene radiance that `rt_tonemap` turns into `rtImage`, so shrinking it
shrinks the *picture*, not the bloom.

And the dial itself is the player-facing lever — `Off` skips every pass and the
split with it (§4.4) — which is what `performance.md` asks a heavy effect to ship
with.

## 7. Build order

Steps are numbered **R1–R3** rather than L1–L3, so a session with both specs open
cannot confuse them with DOOM-0331's L-steps. Each ends with something
observable. `make` and `make test` after every one (`always-rebuild-engine`).

**Every A/B verify below takes THREE captures, not two** — `ab_diff.py`'s
signature is `<on.png> <off.png> <control.png>`, and the control is a second
capture from the *same* build with identical settings. SIGNAL is meaningless
unless NOISE is quoted beside it, and a two-argument call does not fail cleanly,
it raises on unpacking. **Pass `DOOMCFG=` a config with `renderer 1` for the
Ultra arm** (`renderer 2` is Solid); without an explicit `DOOMCFG` a capture
inherits whatever tier the user last played in. Confirm `HD load done` appears in
the run log before trusting any Ultra capture — the HD-assets `cwd` trap renders
1994 paletted art silently otherwise.

- **R1 — the tone-map lift and the split, gated.** `formulas/tonemap_encode.glsl`
  and `svgf_composite.comp` moved onto `toneExposeEncode` (no behaviour change),
  then `rtHdrImage`, the `-DBLOOM_SPLIT` variant, and `rt_tonemap.comp` doing the
  surface expose-and-encode with the bloom term absent. This is the risky step and
  is deliberately separated so the split can be proven neutral on its own.
  *Verify,* in three arms:
  - `bloom 0` → the un-split pipeline runs, so this arm **is** byte-identical to
    the pre-R1 build: SIGNAL max 0.0.
  - `bloom 2` → the split runs with no bloom term, so this arm is
    **near**-identical, not byte-identical: `rtHdrImage` is fp16 and adds one
    rounding step ahead of the 8-bit quantise (§4.2). Gate it at SIGNAL
    mean ≤ 0.02 / max ≤ 1.0 with NOISE quoted beside it. Asking for byte
    identity here would fail a correct implementation.
  - Both arms: `-rtverify -warp 1 1 -noinput` still PASSes, and a **sky-facing
    and fog-on** capture is included — that is the arm that catches a broken sky
    encode (INV-4), and `rt_fog` defaults to on so the default config already
    exercises it. Sweep `rb_exposure` across at least its extremes in this arm
    too: a double-exposure (INV-6) is invisible at one setting and obvious across
    the range.
- **R2 — the extract and the combine.** `bloom_extract_rt.comp` and the
  `rt_tonemap` add. *Verify:* the same lamp that gained a halo in Solid at
  DOOM-0331 L3 gains one in Ultra RT; the sky does not (INV-5); `bloom 0` is
  still byte-identical to pre-R1. **And the decision-5 check:** capture one fixed
  coordinate at several `rb_exposure` values with `bloom 2` — the set of blocks
  that bloom must not change with the slider, though their brightness will.
- **R3 — profiler slots, then the gate.** All nine widening sites (§5) plus the
  RT `profMs[]` re-mapping and the `[rt_profile]` `printf`, then §5's
  declaration-anchored standing grep, the §6 measurement, `-rtverify`, and the
  human look call on hardware (§10). Then the `-shotcompare` golden's re-bless is
  owed — see §12. Only after all of that does the ROADMAP bullet flip and
  CHANGELOG gain an entry.

## 8. Invariants

- **INV-1** — `rb_bloom == 0` restores the current picture **byte for byte** on
  the RT chain. The tone-map split is itself gated, so the un-split pipeline runs
  unchanged and `rtHdrImage` is never written (§4.4). That gate is what makes this
  invariant hold here rather than only on the raster chain.
  *Test:* `scripts/ab_capture.sh` at a fixed coordinate with `bloom 0`, against
  the same capture from the commit before R1, plus a same-build control —
  `ab_diff.py <bloom0> <pre-R1> <bloom0-control>` → SIGNAL mean 0.00, max 0.0,
  with the NOISE row quoted beside it. Run in Ultra RT and in Solid with the Ray
  Tracing row **on**.
  *Breaks when:* the tone-map split is made unconditional, at which point the
  fp16 `rtHdrImage` round-trip (§4.2) moves some pixels by 1/255 even with the
  bloom term absent; or a dispatch is recorded unconditionally.

- **INV-2** — the HUD, status bar, menu text and weapon sprite are never bloomed
  and never bloomed over. The combine runs inside `rt_tonemap.comp`, which
  precedes the blit and therefore `RecordRtOverlay`.
  *Test:* structurally **and** photographically — the strip test alone cannot see
  the weapon sprite or an open menu, both of which sit inside the world area:
  ```
  # 1. structural: rt_tonemap must be dispatched in RecordRtTrace (upstream of
  # the blit), never in RecordRtOverlay. Grep the C++ PIPELINE identifier, not
  # the GLSL filename - 'rt_tonemap' appears in no C++ line, so grepping that
  # can only ever return 0 and would pass whether the invariant holds or is
  # breached.
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
  *Breaks when:* the `rt_tonemap` dispatch is moved into `RecordRtOverlay`, or an
  extract is pointed at the swapchain image after the overlay has been drawn.

- **INV-3** — `-rtverify` is unaffected. `RB_RtVerify` binds only
  `RtPipelineForMode(5u)` and dispatches once; it never touches the denoiser,
  the composite, TAAU, or anything this feature adds.
  *Test:* `awk '/^void RB_RtVerify\(\)/,/^\}/' linuxdoom-1.10/r_vulkan.cpp | grep -c 'svgfComposite\|taauPipeline\|svgfTemporal\|svgfAtrous'`
  → `0` (verified against the current tree), and `-rtverify -warp 1 1 -noinput`
  prints PASS with an unchanged rel-MSE before and after R2.
  *Breaks when:* the verify path is ever routed through the display composite —
  at which point its rel-MSE would start moving with a look dial.

- **INV-4** — the fogged sky keeps its display encode. `svgf_composite.comp`'s
  `gp.w < 0.0` branch still exposes, tone-maps and sRGB-encodes its own pixel and
  still returns early, on both shader variants; `rt_tonemap` passes it through
  untouched. `rt_fog` defaults to 1, so this is the shipped path and not an edge
  case.
  *Test:* the sky branch must still call the encode, and the split variant must
  not have deleted it:
  ```
  awk '/gp\.w < 0\.0/,/return;/' linuxdoom-1.10/shaders/svgf_composite.comp \
    | grep -c 'toneExposeEncode'   # expect 1
  ```
  plus R1's sky-facing, fog-on capture at the stated tolerance — a linear,
  un-encoded sky is not a subtle difference and the block map shows it at once.
  *Breaks when:* an implementer reads "the tone-map moves out" as applying to the
  whole shader and deletes the encode from the sky branch, shipping a washed-out
  linear sky whenever fog is on.

- **INV-5** — the sky never *generates* bloom on this chain. The sky is flagged
  (`rtHdrImage.a == 0.0`) because it is display-encoded rather than radiance, and
  the extract multiplies the flag in, so a sky texel contributes exactly zero.
  **This governs generation, not reception**, and reception differs from the
  raster chain by design: `rt_tonemap`'s `a < 0.5` arm adds no bloom to a sky
  pixel at all, whereas the raster combine has no sky test (DOOM-0331 INV-9).
  *Test:* a sky-facing capture (E1M1's outdoor courtyard), fog **on** (the
  default), `bloom 3` vs `bloom 0` + a control → 0.00 mean delta in **all** sky
  blocks, including those adjacent to a lamp — which is the arm that distinguishes
  this chain from the raster one, where those blocks are expected to move.
  *Breaks when:* the extract is moved above `svgf_composite`'s `gp.w < 0.0`
  early-out, or the alpha flag is dropped (which is what a packed
  `B10G11R11_UFLOAT_PACK32` `rtHdrImage` would do — §9).

- **INV-6** — the exposure is applied exactly once per pixel. Surface pixels on
  the split path take it in `rt_tonemap.comp`; surface pixels on the un-split path
  take it in `svgf_composite.comp`; sky pixels take it in `svgf_composite.comp` on
  either path and `rt_tonemap` does not touch them.
  *Test:* one grep and one capture sweep, because the grep can see a duplicated
  call but not a duplicated effect:
  ```
  # Both variants are built from this ONE source file, so a correct build has
  # exactly TWO call sites: the sky branch (either variant) and the un-split
  # variant's surface store. Expecting 1 would be satisfied by deleting the
  # un-split surface encode - which breaks INV-1's byte-identity on the bloom 0
  # arm, so the wrong expected value here costs more than no test at all.
  grep -c 'toneExposeEncode' linuxdoom-1.10/shaders/svgf_composite.comp
  # expect 2
  # ...and the split arm's surface store must carry no exposure of its own:
  grep -c 'imageStore(rtHdrImage, p, vec4(max(L, vec3(0.0)), 1.0))' \
       linuxdoom-1.10/shaders/svgf_composite.comp
  # expect 1
  ```
  plus R1's `rb_exposure` sweep: capture the same coordinate at the slider's
  extremes with `bloom 2` and again with `bloom 0`, and the two arms must track
  each other. A double exposure squares the gain, so the split arm would diverge
  from the un-split one increasingly toward one end of the slider.
  *Breaks when:* the split variant keeps `* exposureEv(EV)` on its surface store
  while `rt_tonemap` also applies it — which looks like a brightness bug, not a
  logic one, and is invisible at whichever single slider position it was tested
  at.

- **INV-7** — every RT bloom pass is timed, and all nine widening sites moved. A
  pass with no timestamp is invisible to the profiler `performance.md` mandates,
  and would show up as a mysteriously slower neighbour.
  *Test:* after R3 the RT chain writes slots 0–9, `nq` matches, and the standing
  grep is empty. Symbol-anchored, so it survives edits above the sites:
  ```
  awk '/^void RecordRtTrace/,/^}/'   linuxdoom-1.10/r_vulkan.cpp \
    | grep -o 'gpuTimerPool, [0-9]' | sort -u   # today 0-7; after R3 0-9
  grep -c 'nq = g.profRasterFrame ? 7u : 10u;' linuxdoom-1.10/r_vulkan.cpp   # -> 1
  # the declaration-anchored standing grep (5): no stale 8 left
  grep -nE 'queryCount = 8|uint64_t ts\[8\]|double +profMs\[8\]|pi < 8|Pool, 0, 8' \
       linuxdoom-1.10/r_vulkan.cpp
  # -> empty
  # the print must carry both new buckets
  grep -A6 '\[rt_profile\]' linuxdoom-1.10/r_vulkan.cpp | grep -c 'bloom'   # -> 1
  ```
  Today the slot grep reads `0 1 2 3 4 5 6 7`, the `nq` line is `? 6u : 8u`
  before DOOM-0331 and `? 7u : 8u` after it, and the standing grep returns six
  lines — all verified against the current tree, and all **must change**, which is
  why the expected values above are the post-R3 ones. (The slot grep also picks up
  `vkCmdResetQueryPool(..., 0, 8)`'s `0`; harmless, since slot 0 is genuinely
  written too.)
  *Breaks when:* a dispatch is added between two existing timestamps without
  inserting one — the earlier pass then absorbs its cost silently, which is
  exactly what `profMs[7]` would do here — or a widening site is missed, of which
  `uint64_t ts[8]` is a stack-buffer overflow and the rest are silent.

- **INV-8** — this spec adds no second preset table. The threshold, knee and
  intensity come from DOOM-0331's `kBloomPresets`, read by both chains.
  *Test:* `grep -rn 'kBloomPresets' linuxdoom-1.10/` shows one definition and
  the reads that consume it; no threshold, knee or intensity literal appears in
  `bloom_extract_rt.comp` or `rt_tonemap.comp`.
  *Breaks when:* the two chains need different tuning and someone answers that
  with a second table instead of a named per-chain scale constant
  (DOOM-0331 §10 Q3).

- **INV-9** — the bloom passes cost ≤ 5 % of present-total in Ultra RT.
  *Test:* with `rb_profile` on (`\`), **two** prints, because neither carries
  both halves of the ratio: `[rt_profile]` for the new bloom and `rt_tonemap`
  buckets (the numerator) and `[cpu_profile]` for present-total (the
  denominator — `[rt_profile]` does not print it). `bloom 2` vs `bloom 0`, same
  map and same render scale (`performance.md`'s comparison rule). No expected
  value — this is the R3 measurement, not a recorded one.
  *Breaks when:* the blur is run at full resolution, the single level grows into
  a pyramid without a re-measure, or the split is made unconditional so the
  round-trip is paid on every frame.

### Trust boundary

None crossed, so no invariant above defends one. No file is read, no user input
is parsed beyond the one integer DOOM-0331's config and menu layers already
clamp, and no network, IPC or model output is involved. Recorded explicitly
rather than omitted, because an absent boundary section reads as an oversight.

## 9. Alternatives considered (and rejected)

- **Combine into the tone-mapped `rtImage` in LDR** and leave `svgf_composite`
  alone. Saves one pass and 15.8 MiB, and needs none of §4.2. Rejected: the same
  lamp would then glow differently in Solid (glow tone-mapped) and Ultra (glow
  added after), and `composite.frag`'s own comment records that using the same
  operator in both "so Solid and Ultra stay tone-matched" is deliberate.
- **Keep the exposure upstream and scale the preset by `exposureEv(EV)` in the
  extract.** Mathematically equivalent to what §4.2 does — the extract is
  positively homogeneous, so scaling colour, threshold and knee together moves
  nothing — and it satisfies §3 decision 5 just as well. Rejected as the more
  expensive way to say the same thing: it needs the EV plumbed into the *extract*
  as well as the tone-map, leaves the two chains' extracts differing by an
  exposure term that cancels, and makes `rtHdrImage` hold a quantity whose meaning
  depends on a slider. Storing unexposed radiance makes the stored value mean one
  thing and the two extracts identical apart from their source.
- **Combine after TAAU** instead of before. Avoids TAAU smearing a muzzle flash's
  halo across following frames (§4.1). Rejected for v1: it costs a display-res
  pass instead of a render-res one, and whether the smear reads as afterglow or as
  ghosting is a look call nobody has made yet — §10 Q1 owns it, and moving the
  combine is the lever if the answer is "ghosting".
- **`B10G11R11_UFLOAT_PACK32` for `rtHdrImage`.** Halves its bandwidth and
  memory; radiance is non-negative, so the lost sign costs nothing. Rejected for
  v1 because the format has no alpha and the sky flag rides there (INV-5). It is
  §6's third lever if the budget is missed, and the flag would then need another
  home — a sentinel radiance value, since the branch only needs one bit.
- **Bloom on the path-tracer debug views** (`rb_rtdebug` 1–4). Rejected: those
  are diagnostics behind the Debug Views toggle, mode 4 tone-maps inside
  `pathtrace.comp` rather than in the composite, and adding a third hook site
  for a view that exists to show raw estimator output is a cost with no reader.

## 10. Open questions

Two questions, both look calls, **neither blocking a build step.** The umbrella's
Q3 — which blocked this spec's middle step — was answered by the user on
2026-08-12 and is §3 decision 5. DOOM-0331 §10 carries the questions about the
halo's size and strength (its Q1), the flashlight-on-a-white-wall case (its Q2),
whether one preset table reads the same on both chains (its Q3) and the menu row
count (its Q4); none is restated here.

- **Q1 — does TAAU's history smear a muzzle flash's halo, and does that read as
  afterglow or as ghosting?** The combine sits before the upscaler (§4.1), so a
  one-frame emitter is reprojected and accumulated. **User**, on hardware, at the
  R3 gate. *Blocks:* the ROADMAP flip only. If it reads as ghosting, §9's
  combine-after-TAAU is the lever, at the cost of a display-res pass.
- **Q2 — does the RT halo match the raster one closely enough to feel like one
  feature?** Both chains now threshold in scene radiance units against the same
  presets (§3 decision 5), but their lighting magnitudes are not identical.
  **Claude to measure** (the same coordinate captured in both chains at the
  shipped presets, reporting which blocks bloom in each), **user to judge**, at
  the R3 gate. This is the RT half of DOOM-0331 §10 Q3 and its answer belongs
  there — a named per-chain scale constant if needed, never a second table
  (INV-8). *Blocks:* the ROADMAP flip only.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 `bloom 0` byte-identical on RT | `ab_capture.sh` ×3 + `ab_diff.py` at R1 and R2 |
| INV-2 HUD/weapon/menu never bloomed | INV-2's two structural greps + the strip compare + the weapon/menu block map |
| INV-3 `-rtverify` unaffected | the INV-3 grep + a real `-rtverify` run at R1 and R2 |
| INV-4 the fogged sky keeps its encode | INV-4's grep + R1's sky-facing, fog-on capture arm |
| INV-5 RT sky never generates bloom | sky-facing `ab_diff.py` capture at R2, **fog on** |
| INV-6 exposure applied exactly once | INV-6's grep + R1's `rb_exposure` sweep across the slider's extremes |
| INV-7 every RT pass timed, all 9 sites moved | INV-7's slot greps + §5's declaration-anchored standing grep at R3 |
| INV-8 no second preset table | the INV-8 grep at R3 |
| INV-9 ≤ 5 % of present-total | `rb_profile` measurement at R3 |
| The `[rt_profile]` format string names the two new buckets | INV-7's last grep |
| The `profMs[2]` umbrella label is still accurate after the append | **nothing** mechanical — its interval now also spans bloom and `rt_tonemap` (§5); a hand read at R3 |
| The split arm's fp16 tolerance is the right one | **nothing** — R1 gates at a stated tolerance chosen by argument, not measurement; too loose a bound would pass a real regression |
| The halo reads as light, not haze, through TAAU | **nothing** — a human look call (§10 Q1) |
| Solid and Ultra agree on what glows | **nothing** mechanical — §10 Q2 is a measurement plus a judgement |

**Four `nothing` rows out of fourteen** (counted from the table above, not
carried forward). Three are look or judgement calls, which is why R3 is a human
gate rather than a green test run. The fourth — the fp16 tolerance — is the one
that is uncomfortable rather than merely unautomatable: it is a bound set by
reasoning about `kSceneFormat`'s mantissa, and nothing proves that a real
regression could not hide under it. The mitigation is that R1's split arm is
compared against a build differing *only* in the split, so anything the tolerance
hides is at most one fp16 rounding step wide.

## 12. Cross-doc impact

- `CHANGELOG.md` — an `Added` entry, at R3 and not before.
- `ROADMAP.md` — DOOM-0345 flips to 🚧 at R1 and ✅ at R3.
- `docs/specs/DOOM-0331-bloom.md` — the sibling half, and this spec's blocker.
  It owns the extract arithmetic, the blur, the preset table, the dial and the
  bloom targets; this spec cites them and must not restate them. Its §10 Q3 gains
  this spec's §10 Q2 as its RT arm.
- `docs/standards/renderer.md` — no change. The push-constant lane table is
  untouched (§5: no new megakernel lane), and the shader list there is
  descriptive prose rather than an inventory.
- `docs/standards/performance.md` — no change. §6 uses the existing profiler and
  the existing comparison rule.
- `CLAUDE.md` — no change. The tier table already says effects belong to the
  view, not the tier, which is what §2 applies.
- **The `-shotcompare` golden image — a re-bless is owed, and it is not this
  feature's to grant.** The golden is captured in the RT view, so this spec moves
  it further than DOOM-0331 does. This adds to an existing debt rather than
  creating one: the pin block's own comment records that a re-bless is already
  owed for `rt_fog`. R3 records that it is owed and leaves the decision to
  DOOM-0202, which owns the golden.
- `docs/specs/DOOM-0011-fix-ledger.md` — no change, but read row 9.4 before doing
  R3: it is the record of this project getting a profiler widening wrong, and §5's
  nine-site table exists because of it.

## 13. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 0-split | 2026-08-12 | 0 | 0 | 0 | 0 | 0 | **No reviewer dispatched.** Split out of the 1496-line DOOM-0331 umbrella, which had converged by cap at 3 loops with build-changing findings still arriving and 5 of loop 2's 10 CRIT+HIGH being collateral from loop 1's own fixes. This part takes the RT chain; DOOM-0331 keeps the shared core and the raster chain. Invariants renumbered from 1 and mapped in DOOM-0331 §2. The umbrella's §10 Q3 was closed by the user as §3 decision 5, which **changed the mechanism** — the exposure moves downstream and `toneEncode` is lifted whole rather than split. **No review history is inherited**: the umbrella's three loops ran against a document that no longer exists, and the design in §4.2 is new since any of them. This part runs the gate from loop 1 on its own bytes. |
| 1 | 2026-08-12 | 2 | 1 | 3 | 0 | 0 | All 4 verified, **1 dismissed**, all 4 fixed. Both lanes independently found three of them. **The sharpest was INV-6's own test**: it expected `grep -c 'toneExposeEncode'` to return `1`, but both shader variants are built from one source file, so a correct build has two call sites — satisfying the clause as written meant deleting the un-split surface encode, which breaks INV-1's byte-identity on the `bloom 0` arm. A test that fails a correct build and passes a broken one. Also: §5 stated one layout rule for the whole chain, but `rtHdrImage` is STORAGE-only and read with `imageLoad`, so transitioning it to `SHADER_READ_ONLY_OPTIMAL` is invalid for a storage descriptor; and the widening table and the prose beneath it *both* claimed profiler slots 8 and 9 — since `denoise` is `(rb_rtdebug == 6)` and bloom runs only in mode 6, `!denoise` implies `!bloomActive`, so two dummy blocks would double-write those slots between resets. The Q1 was §6/INV-9 naming `[rt_profile]` for a ratio whose denominator only `[cpu_profile]` prints. **Dismissed:** one lane called INV-1's Solid+RT arm unrunnable because `ab_capture.sh`'s HD guard is unconditional; the other lane checked and DOOM-0331 §7 already owns that fix, and this spec is blocked by it — verified, and the arm stands. |

**What the umbrella's review bought, kept here because the reasoning is
load-bearing and the document it was written in is gone.** Two findings that
would each have shipped a defect, both in material this part owns:

1. **The RT sky branch does call `toneEncode` when fog is on** — and `rt_fog`
   defaults to 1, so it is the shipped path. "The tone-map moves out", taken
   literally, would have shipped a linear un-encoded fogged sky. Now INV-4, with a
   grep and a capture arm behind it (§4.2).
2. **The RT profiler append would have let `profMs[7]` silently absorb the new
   passes**, and the widening itself needs nine sites — one of them a
   `uint64_t ts[8]` stack array `vkGetQueryPoolResults` would have overflowed.
   Now §5's table and INV-7.

A third finding was design-changing and is why §4.4 exists: the RT tone-map split
had to become **gated** on `rb_bloom > 0`, or Ultra pays for an extra render-res
pass and an HDR round-trip on every frame including those with bloom switched
off. Two consequences followed — INV-1 holds byte-exactly on this chain rather
than needing a tolerance, and R1's split arm is gated at a stated tolerance
because `rtHdrImage` is fp16 and no correct implementation could pass byte
identity through it.
