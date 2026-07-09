# DOOM-0170 — Raster "performance mode" (RT-off lighting stack)

**Status:** Reviewed — `/cold-eyes` loops 1–4 (see log), **polish-converged
2026-07-09**. Implementation-ready pending user acceptance. Design contract for the
ray-tracing-**off** raster renderer.
**Depends on:** DOOM-0008 (Stage-1 raster 3D: mesh, materials, sprites, UI
composite) shipped; DOOM-0009 (path tracer — the RT-**on** back-end) **in-progress**
(the RT-on back-end is usable — it is the shared quality-mode renderer this stack
mirrors); DOOM-0169 (`RB_ApplyTierRt` — the render tier drives `rb_rtdebug`) **in-progress**
(the core `RB_ApplyTierRt` landed in commit cf07b91; its own bake-gating follow-up
is *superseded* by §4.2 here — see §4.2); DOOM-0119 (per-subsector `subSec` buffer
+ REJECT cull) shipped.
**Coordinates / extends (does not duplicate):**
- **DOOM-0010** (dynamic lighting, considered) — DOOM-0170 **extends** DOOM-0010's
  dynamic-lighting intent into new RT-off **static/sprite** point lights
  (torches/lamps/barrels pooling light, no cast shadows), which DOOM-0010's own entry
  does not separately scope. DOOM-0010's documented RT-off item is the muzzle flash —
  and that is *not* part of §4.1's loop:
  it already exists in raster via the per-vertex `extralight` screen-brighten and is
  left untouched (§4.1 details that path). The RT-**on** path-traced flash stays in
  DOOM-0009.
- **DOOM-0043** (shipped — Ultra RT-on ambient floor) — left Solid/RT-off rooms
  intentionally pitch-black and *deferred to play-test* whether to soften them.
  §4.2 (baked-probe bounce in raster) is the RT-off *mechanism* for that softening;
  the soften-vs-stay-dark decision remains a play-test call (§9 Q1).
- **DOOM-0044** (shipped — flashlight) — the RT-off raster spotlight cone already
  lives in `mesh.frag`. §4.4 adds a shadow map so that cone (the dominant light)
  casts shadows in raster too.
- **DOOM-0012** (60 FPS floor, considered) — this stack is designed *under* that
  floor (§6); it is not itself the perf pass.
- **DOOM-0146** (Ultra selectable on all Vulkan GPUs, **planned**) — this raster
  stack is what both Solid and "Ultra-without-RT" render. On a *non-RT* GPU the
  "Ultra-without-RT renders this stack" claim also depends on DOOM-0146 landing
  (today Ultra is hidden on non-RT GPUs); on an RT GPU it already holds.
**Supersedes (per user request 2026-07-09):** DOOM-0009 §2 frames dynamic lights
and modern lighting as *Ultra-tier-defining* (features Solid lacks), and DOOM-0043
left Solid RT-off rooms deliberately dark. Both were explicitly play-test-deferred.
DOOM-0170 extends RT-off dynamic point lights (§4.1) **and** baked bounce (§4.2) to
**both** 3D tiers; where DOOM-0009 §2 / DOOM-0043 imply these are Ultra-only for the
RT-off case, this spec is now canonical.
**Feeds:** the eventual five-look matrix in DOOM-0009 §2 (this is the shared
RT-**off** back-end the two 3D tiers sit on, mirroring DOOM-0009 as the RT-on one).

---

## Contents

- §1 Goal — §2 Where this sits (tier × RT) — §3 Architecture (the pass pipeline)
- §4 Subsystems: 4.1 point lights · 4.2 probe bounce · 4.3 SSAO · 4.4 key-light
  shadow map · 4.5 blob shadows · 4.6 scoped SSR
- §5 Data reuse & new resources — §6 Performance budget (Constants table) &
  fallbacks — §7 Build order (layers L1–L3) — §8 Invariants — §9 Open questions

---

## 1. Goal

Give the ray-tracing-**off** raster view (the "performance mode") modern lighting
cues — dynamic point lights, soft indirect bounce, contact + key-light + blob
shadows, and scoped reflections — done the **raster way**, so it looks clearly
better than the Classic software renderer while holding a smooth, consistent frame
rate (the numeric bar below) and **still feeling like DOOM**. This is the
console-style split the user asked for:

- **RT on** (`rb_rtdebug == 6`) = *quality mode* — the DOOM-0009 path tracer.
- **RT off** (`rb_rtdebug == 0`) = *performance mode* — this raster stack.

The split lives *inside* each 3D tier (per DOOM-0009 §2 and DOOM-0169): the tier
picks the **art set** (Solid = original 1993 art, Ultra = HD art), and the RT
On/Off toggle picks quality-vs-performance within it. **Both tiers share this one
raster stack** with RT off, exactly as they share the one path tracer with RT on.
(`rb_rtdebug` is the actual RT switch; `RB_ApplyTierRt` sets it to `0` on RT-off
tiers. The developer "Debug Views" cycle — `rb_rtdebug_menu` — can override it to a
diagnostic value; that dev path is out of scope here, which assumes `0` = RT off.)

**Reference scale (used by every FPS invariant below):** 1920×1080 at **100 %
Render Scale** on the reference GPU (AMD RX 6600 / RDNA2 / RADV). "Render Scale" is
the player-facing perf dial — the `render_scale` config option (`m_misc.c`) exposed
in the Renderer menu; FPS numbers are always quoted with their scale — never compare
across scales.

**Success bar:** at the reference scale, with the full stack on, the RT-off view
holds **≥ 60 FPS** with visibly more depth than Classic (rooms lit by their lamps,
objects grounded by shadow, wet floors catching a sheen) and no stutter on level
entry, turning, or firing (frame-time consistency — INV-3).

**Non-goals:** volumetric god rays (DOOM-0011); moving/coloured/flickering light
animation beyond what the existing emitter list already carries (DOOM-0010's
richer dynamics); any change to the RT-on path (it must stay byte-for-byte
unaffected — INV-4). HD art is DOOM-0042, not here.

## 2. Where this sits — the tier × RT model

The tier × RT settings model is defined once in **DOOM-0009 §2** and is not
restated here. The single fact this spec relies on: with RT **off**, `rb_rtdebug`
is `0` (set by `RB_ApplyTierRt`, DOOM-0169) in *both* Solid and Ultra, the
per-frame `rtActive` gate (`r_vulkan.cpp`) is false, the path-tracer branch
(`RecordRtTrace`) is skipped, and the raster render pass runs. This stack is the
body of that raster render pass. What differs between Solid-RT-off and
Ultra-RT-off is only the **art set** the tier loads, never this lighting code.

## 3. Architecture — raster grows into a short multi-pass pipeline

Today the RT-off path is a single forward pass: `mesh.vert`/`mesh.frag` shade each
fragment (albedo × sector-light × distance, plus the DOOM-0044 flashlight cone)
straight into the swapchain image. Screen-space effects (SSAO, SSR) need the
frame's depth **and** surface normals available *before* final compositing, so the
path grows into a small, cheap pipeline of passes recorded into the frame command
buffer when `rtActive` is false:

```
[once, at level load]   RunGiBake  — SH-L1 irradiance probes (already built; §4.2 now reads them in raster)

[every RT-off frame]
  Pass A  Key-light shadow map   depth-only render from the dominant light (the "key light") (§4.4)
  Pass B  Main geometry pass     mesh.frag extended → THREE offscreen targets:
                                   • DIRECT   (HDR): albedo × sector-light × dist
                                              + Σ nearest point lights       (§4.1)
                                              + dominant light × Pass-A shadow (§4.4)
                                   • INDIRECT (HDR): baked-probe bounce / ambient (§4.2)
                                   • NORMAL+SHINE (packed): view normal + shine bit (§4.6)
                                 (+ the existing depth attachment)
                                 The direct/indirect split lets Pass E apply AO to the
                                 indirect term only (§4.3), which one combined target can't.
  Pass B′ Blob shadows           soft dark oval decals under things, composited into
                                 DIRECT before the screen passes read it            (§4.5)
  Pass C  SSAO (half-res)        from depth + NORMAL+SHINE → AO texture              (§4.3)
  Pass D  Scoped SSR (half-res)  shine-flagged texels ray-march depth/DIRECT        (§4.6)
  Pass E  Composite → swapchain  DIRECT + AO×INDIRECT + shine·reflection, tonemap;
                                 then the existing 2D overlay (HUD/menu) on top
```

**Sprites/billboards.** Monsters, items and barrels are billboards and the weapon is
a screen-space psprite; they draw inside **Pass B** into the DIRECT target (exactly
as the raster path draws them today, DOOM-0008). Their silhouettes additionally feed
**Pass A** (they cast into the shadow map, §4.4) and **Pass B′** (each is a blob-shadow
anchor, §4.5). No separate sprite pass is added.

**Why offscreen targets are new.** Passes C–E read what Pass B wrote, so Pass B
must render into off-screen images (a DIRECT and an INDIRECT HDR target + a
normal/shine target, sharing the existing depth attachment) rather than straight to
the swapchain; Pass E is the one that writes the swapchain image and hands it to the
overlay. Splitting direct from indirect is what makes §4.3's "AO darkens ambient,
not direct light" implementable — a single combined colour target could not. This
is the one structural change; each individual effect below is small.

**Non-RT GPUs.** Passes A–E are pure raster (no ray queries) and run on any
Vulkan GPU. Only the §4.2 probe bounce depends on `RunGiBake` (which uses ray
queries); where no bake exists (non-RT GPU, or a bake that produced nothing) the
bounce term falls back to a flat ambient floor (§4.2), so nothing crashes and the
worst case degrades to "today's look + point lights + SSAO + shadows".

## 4. Subsystems

### 4.1 Dynamic point lights (reuse the NEE emitter list)

The NEE emitter list — static wall/flat emitters (`BuildStaticEmitterSet`) plus
per-frame emissive-sprite emitters (torches, lamps, burning barrels, candelabra;
`BuildDynamicEmitters`) — is already assembled every frame for the path tracer in
`g.emitBuf` (`g.emitCount` records). **Each record is a triangle *area* light**, not
a point light: the layout is `v0[3] v1[3] v2[3] Le[3] cdf pdf` (per
`BuildStaticEmitterSet`/`BuildDynamicEmitters`). So the raster path must *derive* a
point-light approximation from each triangle — it cannot read `pos`/`intensity`
fields, because there are none.

**Where the nearest-N cull runs — CPU, per subsector, per frame (not in-shader).**
A map's light-heavy rooms can hold ~100+ emitters (DOOM-0092), so an in-shader
loop-and-sort over the whole `emitBuf` is not affordable on the hot path. Instead,
right after `FinalizeEmitters`, a CPU pass buckets the finalized emitters into a
**per-subsector candidate list**: for each subsector, apply the DOOM-0119
`subSec`→sector + REJECT-matrix cull, then keep the **nearest N** emitters by
centroid distance, storing each as a derived point light `{centroid, power, colour}`
where `centroid = mean(v0,v1,v2)`, `colour = normalize(Le)`, and
`power = luminance(Le) · triangle_area` (the emitter build already computes this
`luminance(Le)·area` weight for the NEE CDF — reuse the stored value rather than
recomputing it, per the reuse-before-rewrite rule). This compact per-subsector list (≤ N
entries each) uploads to a new SSBO; the fragment shader indexes it by the
fragment's subsector id (the per-vertex subsector-id attribute defined in §4.2) and
loops **only its subsector's ≤ N entries** — no in-shader sort, bounded cost (INV-6):

```
for k in this fragment's subsector list (≤ N):
    d       = light[k].centroid - worldPos
    dist    = length(d)
    atten   = light[k].power / (1 + (dist/RADIUS)^2)   // smooth inverse-square-ish; RADIUS a tuned constant (§6)
    diffuse += light[k].colour * atten * max(dot(normal, normalize(d)), 0)
```

- **No cast shadows** here (that is the RT-on job, and the one raster shadow we do
  cast is the key light, §4.4). This matches DOOM-0010's stated RT-off behaviour:
  "the room brightens, no cast shadows".
- **Default N = 16** (`RASTER_MAX_LIGHTS_PER_SUBSECTOR`); §9 Q4 tunes within 12–16.
  INV-6 tests the cap at this default.
- **The muzzle flash is *not* in this loop.** It is not an `emitBuf` emitter — it
  rides the existing `extralight` path, which in raster is a per-vertex screen
  brighten (the `pc.extralight` field in `mesh.vert`, uploaded as `pcData[16] =
  g.lastView.extralight` in `r_vulkan.cpp`) that already lights the room on fire.
  DOOM-0170 leaves that path untouched; §4.1 adds the *static/sprite* emitter point
  lights — extending DOOM-0010's dynamic-lighting intent to RT-off (DOOM-0010's own
  entry scopes only the muzzle flash for the RT-off case).

**Cost of the CPU cull (the one genuinely new per-frame CPU work — feeds INV-3).**
The build walks each finalized emitter once and, via the REJECT/sector cull, adds it
only to the subsectors whose sector can see it, keeping each subsector's list to the
nearest `N`. Worst case is `O(emitters × visible-subsectors-per-sector)`, but REJECT
prunes the inner set to a handful on id maps, so measured cost stays well under the
**≤ 1 ms** budget in §6. It runs on the same per-frame cadence as `FinalizeEmitters`.
If a pathological custom map blew the budget, the fallback is to bucket only the
brightest M emitters (the CDF already ranks them) — the dim tail contributes little.

### 4.2 Baked-probe indirect bounce (the RT-off mechanism for DOOM-0043's deferred Solid call)

`RunGiBake` bakes one SH-L1 irradiance probe per subsector (`g.probeBuf`, keyed by
`g.triSsBuf` triangle → subsector, per DOOM-0009 §7 build-step 4). The path tracer already
reads it; the raster main pass now reads it too:

```
probe   = probeBuf[ subSec(this fragment) ]
indirect = evalSH_L1(probe, normal)              // low-frequency coloured bounce
shade   += albedo * indirect
```

- The per-fragment subsector id reaches `mesh.frag` via a new flat per-vertex
  attribute built alongside the mesh (the same triangle→subsector map `tri_ss` that
  feeds `triSsBuf`), so no ray query is needed at shade time — just one probe
  lookup and an SH evaluation.
- **This is the RT-off analogue of DOOM-0043's ambient floor.** DOOM-0043 shipped
  the ambient/scene-light floor for Ultra RT-on and *deferred to play-test* whether
  to soften pitch-black Solid/RT-off rooms. This baked bounce is that softening,
  now proposed for both tiers with RT off. Whether a room *should* still go near-black
  (horror tension) versus lit by bounce stays a play-test tuning call (§9 Q1).
- **The bake runs eagerly at level load — superseding DOOM-0169's raster-skip.**
  DOOM-0169's open follow-up plans to *skip* `RunGiBake` in raster mode (it was dead
  GPU work when raster ignored the probes). §4.2 makes raster *read* the probes, so
  that skip no longer holds. DOOM-0170's rule: on an RT-capable GPU the probes are
  baked **at level load, regardless of tier** (both tiers' RT-off bounce reads them),
  so a normal play frame never triggers a bake. `RunGiBake` is **synchronous/blocking**
  (DOOM-0009 §4.1) — it runs under the level-load screen, not mid-play, so it is
  **excluded from INV-3's steady-state frame-time bar** (rough one-time cost: tens of
  ms, hidden by the load). A mid-session tier or RT toggle reuses that load-time bake
  — no re-bake, no hitch — since the probes were baked at load regardless of tier. The
  per-frame path stays a single probe lookup. On a non-RT GPU there is no bake at all
  (next bullet).
- **Fallback:** where `g.probeCount == 0` (no bake — non-RT GPU, or empty result),
  `indirect` is a small constant ambient so the world stays navigable and nothing
  reads an unbound buffer.

### 4.3 SSAO — contact / ambient shadows

A standard screen-space ambient-occlusion pass (Pass C) reads the depth buffer and
the Pass-B NORMAL+SHINE target at **half resolution**, samples a kernel of **16 depth
taps** in a hemisphere around each pixel, and writes a single-channel AO factor; a
short bilateral blur removes the noise. Pass E multiplies the **INDIRECT** target
(the §4.2 ambient/bounce term) by AO — never the DIRECT target — so corners and
object-to-floor contacts darken without punching dark halos into directly-lit
surfaces. This is exactly why Pass B keeps direct and indirect in separate targets
(§3): AO on a single combined colour would be physically wrong. No per-light cost;
cost is a fixed fraction of the frame.

### 4.4 Key-light shadow map (adds shadows to the DOOM-0044 flashlight)

The **dominant light** (the "key light") — the player flashlight when on
(DOOM-0044), else none (the sun is deferred, §9 Q2) — renders the world mesh (and alpha-tested monster/item
billboards, so they cast into it) depth-only from the light's point of view into a
single **2048×2048** depth shadow map (Pass A). The main pass (Pass B) transforms
each fragment into light space and compares depth to shadow the dominant light's
contribution (3×3 percentage-closer filtering for a soft edge). (A directional
outdoor "sun" is a natural second key light, but stock DOOM carries no sun
direction/colour in its map data — deriving one is deferred; see §9 Q2.)

- **One** shadow map, not one per light — this is the single biggest lever against
  the 60 FPS floor. Torches/lamps (§4.1) stay unshadowed. (2048² is a fixed budget,
  independent of Render Scale; §9 Q2 covers cascade/size tuning if a map needs it.)
- When no dominant light exists this pass is skipped entirely (zero cost).
- This is a strict enhancement to DOOM-0044's RT-off cone: same light, now casting.

### 4.5 Blob shadows — always-grounded things

Independent of the key light, each monster / barrel / dropped item gets a soft dark
oval decal projected onto the floor beneath it (a cheap textured quad, built
alongside the Pass-B billboard draw). It is composited into the **DIRECT** target in
**Pass B′** (§3), after
the geometry pass and before the screen-space passes read it, so SSAO and SSR see a
consistent frame. This grounds things even in a room with no dominant light (where
Pass A is skipped), so nothing floats. Radius scales with the thing's size; the
decal fades with the sector light so it never reads as a hard black spot.

- **Avoiding double-darkening with SSAO (§4.3).** A blob decal and SSAO can both
  darken the same contact patch. Because the blob writes DIRECT and AO modulates
  only INDIRECT, they act on different targets and do not multiply into a black
  smear; the blob is the grounding shadow, AO the ambient occlusion around it.

### 4.6 Scoped SSR — wet-sheen reflections on liquids/metal

Only surfaces flagged shiny — **nukage, water, blood, polished metal** — reflect.
The flag is a per-material bit set at mesh build (from the flat/texture name, the
same place materials are classified today) and packed into the Pass-B NORMAL+SHINE
target. Pass D, at **half resolution**, ray-marches the depth buffer from each
shine-flagged pixel along the reflected view ray, samples the **DIRECT** colour
buffer at the hit, and blurs the result a touch (wet sheen, not a mirror). Pass E
blends the reflection in, weighted by the shine flag; off-screen or missed rays fall
back to the probe/sky colour so edges never go black. All other surfaces are matte
(zero SSR cost).

## 5. Data reuse and new resources

**Reused as-is (no new build cost):** `g.emitBuf`/`g.emitCount` (§4.1),
`g.probeBuf`/`g.triSsBuf` (§4.2), `g.subSecBuf` + the REJECT matrix (§4.1), the
existing depth attachment, the `mesh.vert`/`mesh.frag` pipeline and material atlas.

**New per-frame GPU resources:** a DIRECT and an INDIRECT HDR colour target + a
packed NORMAL+SHINE target for Pass B (RGBA16F: view-normal in rgb, shine flag in
alpha); a 2048² depth shadow map (Pass A); half-res
AO and SSR targets (Pass C/D). Full-res targets are sized to the render-scale
resolution, half-res to half of it; all allocated once and resized with the
swapchain. **Rough VRAM at 1080p (RGBA16F full-res):** DIRECT+INDIRECT ≈ 2×16 MB,
NORMAL+SHINE ≈ 16 MB, shadow map (D32) ≈ 16 MB, half-res AO (R8) ≈ 1 MB + SSR
(RGBA16F) ≈ 4 MB → **≈ 69 MB**, comfortably within the reference GPU's 8 GB. Plus one new per-frame SSBO: the
per-subsector light list (§4.1), ≈ `numSubsectors × N × 32 B` (≈ 1 MB on a large map).

**New shaders:** `shadow_depth.vert/.frag` (Pass A), `ssao.comp` + a blur (Pass C),
`ssr.comp` (Pass D), `composite.comp` or `.frag` (Pass E); `mesh.frag` extended
(§4.1/4.2/4.4 shading + the three-target output). New descriptor bindings on the
raster set for the per-subsector light list, `probeBuf`, `subSec`, and the shadow map.

**New per-vertex attribute:** flat subsector id (§4.2). **New per-material bit:**
shine flag (§4.6). **New CPU per-frame pass:** the per-subsector light-list build
(§4.1), after `FinalizeEmitters`.

## 6. Performance budget and fallbacks (the 60 FPS floor is hard)

**Constants and budgets** (pinned here; tuning ranges live in §9):

| Constant | Value | Where |
|---|---|---|
| Reference scale (all FPS invariants) | 1920×1080 @ 100 % Render Scale, RX 6600 | §1 |
| Frame budget at the floor | 16.6 ms (60 FPS) | INV-2 |
| Point lights per subsector, `N` | **16** (`RASTER_MAX_LIGHTS_PER_SUBSECTOR`) | §4.1, §9 Q4 |
| Point-light falloff radius, `RADIUS` | seed 512 world-units (tune 256–768) | §4.1, §9 Q4 |
| Point-light CPU cull budget | ≤ **1 ms** / frame | §4.1 |
| SSAO resolution / taps | half-res / 16 depth taps | §4.3 |
| SSR resolution | half-res | §4.6 |
| Shadow-map resolution / PCF | 2048² D32 / 3×3 | §4.4 |
| Frame-time consistency floor | 1 %-low ≥ **0.6× mean**; no frame > 1.5× budget (24.9 ms) | INV-3 |

**Rough per-pass GPU budget at the reference scale** (must sum under the 16.6 ms
floor; these are design targets to bisect against during the L2/L3 build, not
guarantees):

| Pass | Budget |
|---|---|
| A — key-light shadow map | ~2.0 ms |
| B — main geometry + lighting | ~4.0 ms |
| B′ — blob shadows | ~0.3 ms |
| C — SSAO (half-res) | ~2.0 ms |
| D — SSR (half-res) | ~2.0 ms |
| E — composite + tonemap | ~1.0 ms |
| **Total GPU** | **~11.3 ms** (≈ 5 ms headroom) |

- Every screen-space pass (SSAO, SSR) runs at **half resolution**; there is one
  fixed-size shadow map; point lights are capped at **`N` nearest per subsector**.
- Full-res targets scale with the existing **Render Scale** setting (the
  `render_scale` option, §1), so the player's perf/quality dial already covers this
  stack; the shadow map is a fixed 2048² regardless.
- **Each effect is individually gated** by a debug/CVAR bit so a misbehaving pass
  can be switched off in isolation on a given GPU without unpicking the stack
  (this also makes the layered build order §7 verifiable one layer at a time).
- **Graceful degradation, never a crash:** no bake → flat-ambient bounce (§4.2);
  no dominant light → no shadow map (§4.4); SSR miss → probe/sky fallback (§4.6);
  non-RT GPU → the whole stack still runs minus the RT-baked bounce.

## 7. Build order — verifiable layers (each rebuilt + play-tested before the next)

The design is one system, but it ships in layers so there is a smooth, playable
checkpoint early and each layer lands against a small diff.

1. **L1 — Lighting.** §4.1 point lights + §4.2 probe bounce, shaded forward in the
   existing single pass (no offscreen targets yet — bounce + point lights need only
   the new buffer bindings and the subsector attribute). *Verify:* rebuilds clean,
   `make test` green; on E1M1 a torch/lamp visibly pools light and rooms show soft
   coloured bounce; RT-on path unchanged; FPS still ≥ 60 at the reference scale.
2. **L2 — Shadows.** Introduce the offscreen HDR + normal targets and the composite
   pass (Pass B/E), then §4.3 SSAO, §4.4 key-light shadow map, §4.5 blob shadows.
   *Verify:* objects grounded, the flashlight casts, a monster casts under the key
   light; frame time flat; ≥ 60 FPS at the reference scale.
3. **L3 — Reflections.** §4.6 scoped SSR + the shine material flag. *Verify:*
   nukage/water catch a moving sheen, matte surfaces unchanged; ≥ 60 FPS at the
   reference scale.

Each layer is one commit (or a small set), rebuilt (`make` + `make test`) and
handed to the user for an on-RX-6600 play-test before the next begins.

## 8. Invariants

- **INV-1 (tier-shared).** This stack renders identically for Solid-RT-off and
  Ultra-RT-off except for the art set the tier loads. *Test:* run the parity diff on
  a shared-art scene (or force a flat-white material) so lighting math — not the
  differing art — is what's compared (via the headless screenshot capture); the two
  tiers' lit output then matches.
- **INV-2 (60 FPS floor).** RT off holds ≥ 60 FPS at the **reference scale** (§1:
  1920×1080 @ 100 % Render Scale, RX 6600) with the full stack on. *Test:* the `\`
  profiler on E1M1 and on a busy multi-light room (>N emitters in view), RT off, at
  the reference scale; the reported FPS is ≥ 60.
- **INV-3 (frame-time consistency).** During **steady-state play** (turning, firing,
  moving) in RT-off there is no periodic hitch. The one-time level-load GI bake
  (§4.2) runs under the load screen and is **excluded** from this bar. *Test:*
  frame-time trace over a 60 s E1M1 run **after the level has loaded**, at the
  reference scale; the **1 %-low frame rate is ≥ 0.6× the mean** and **no single frame
  exceeds 1.5× the frame budget (24.9 ms)** — in particular no spike from the shadow
  map (Pass A) or SSR (Pass D).
- **INV-4 (RT-on untouched).** With RT on (`rb_rtdebug == 6`) the path-traced frame
  is byte-for-byte what it was before DOOM-0170 — the RT-on-side companion to
  DOOM-0009's INV-10 (which guards the RT-off side). *Test:* via the headless
  screenshot capture, an RT-on frame is identical before/after DOOM-0170.
- **INV-5 (graceful fallback).** On a non-RT GPU (or `probeCount == 0`) the RT-off
  view renders without the baked bounce and without error. *Test:* force
  `probeCount = 0`; the frame renders with flat-ambient bounce, no validation error.
- **INV-6 (bounded lights).** Per-fragment point-light cost is capped at the default
  `N = 16` (§4.1) regardless of emitter count — the fragment loops only its
  subsector's ≤ N list, never the full `emitBuf`. *Test:* a room with > 16 emitters
  in view (e.g. a DOOM-0092 light-heavy scene) shows a per-subsector list length
  ≤ 16 and no per-fragment cost growth beyond that cap.

## 9. Open questions (each names the layer that closes it)

- **Q1 (L1, play-test).** Does the baked bounce (§4.2) make Solid rooms feel *lit*
  in a good way, or does it kill the horror-dark tension DOOM-0043 wanted to keep
  optional? Tuning call with the user after L1 lands (the DOOM-0043-deferred
  decision, now for RT-off). May end as a brightness dial, not on/off.
- **Q2 (post-L2, deferred).** An outdoor directional "sun" as a second key light:
  stock DOOM has no sun direction/colour in map data, so deriving one from sky-flat
  sectors (e.g. a fixed down-tilted vector + a sky-tinted colour) is a follow-up,
  tracked separately. For L2 the dominant light is the flashlight only (§4.4).
- **Q3 (L3).** Which exact flats/textures get the shine flag (§4.6)? Start with the
  obvious liquids (nukage/water/blood) + known metal floor flats; refine by
  eyeballing during L3.
- **Q4 (L1).** Point-light attenuation curve, the falloff `RADIUS` (seed 512
  world-units, §6), and `N` (default 16, tune within **12–16**, §4.1/§6) — tune
  against the RT-on look so performance mode tracks quality mode as closely as
  raster allows (the PS5 goal).

## Cold-eyes loop log

- **2026-07-09 — DOOM-0170 initial review (4 cold passes; 2 lanes each — accuracy/
  conflicts + consistency/implementability).**
  - **Loop 1** (0 CRITICAL): fixed the muzzle-flash-is-an-emitter error (it rides
    `extralight`, not `emitBuf` — verified against `BuildDynamicEmitters`); corrected
    the emitter record to a triangle *area* light (`v0 v1 v2 Le cdf pdf`, no
    pos/intensity/radius); specified the nearest-N cull as a CPU per-subsector list
    (not an in-shader sort over 100+ emitters); split Pass B into DIRECT/INDIRECT so
    AO modulates the indirect term only (§4.3 was unbuildable against one combined
    target); reconciled the DOOM-0169 raster-skip conflict (raster now reads the
    probes, so the bake must run); pinned the reference scale (INV-2 was
    unfalsifiable); gave blob shadows a pass slot (B′); added the §6 constants table
    + a TOC; retitled §4.2 (mechanism, not "resolves").
  - **Loop 2** (0 CRITICAL): loop-1 fixes held (not re-raised). Fixed the `DOOM-0145`
    mis-citation (a Windows bug, not the Render Scale feature — repointed to
    `render_scale`/`m_misc.c`); the `pcData[16]`/`mesh.vert` attribution (the shader
    field is `pc.extralight`; `pcData[16]` is the `r_vulkan.cpp` upload); `DOOM-0009`
    "shipped" → in-progress (roadmap 🚧); budgeted the new per-frame CPU cull (≤ 1 ms)
    + added the per-pass ms table; made INV-3 numeric (1 %-low ≥ 0.6× mean, no frame
    > 24.9 ms); "test scale" → "reference scale".
  - **Loop 3** (0 CRITICAL/HIGH): fixed the bake-vs-INV-3 contradiction (`RunGiBake`
    is synchronous — bake eager at load under the load screen, explicitly excluded
    from INV-3's steady-state bar); deferred the undefined "sun" dominant light (DOOM
    has no sun in map data — flashlight-only for now, §9 Q2); added the
    sprite/billboard draw to the §3 pipeline (Pass A/B′ depend on it); reworded the
    DOOM-0010 provenance (extends, not "is its RT-off portion"); added a **reciprocal
    supersession pointer to DOOM-0009 §2**; retagged Q1 (L2→L1).
  - **Loop 4** (0 CRITICAL/HIGH) — **polish-converged.** Only citation-label / wording
    nits left: `RunGiBake`-synchronous cite §4.3→§4.1; removed a vestigial
    mid-session-bake sentence; "sprite pass"→"Pass-B billboard draw"; deduped the
    extralight mechanism to §4.1; reuse the stored emitter power weight
    (reuse-before-rewrite); pinned the NORMAL+SHINE format; split two long sentences.
    Stopped per the polish-convergence rule.
  - **Verified against disk each loop:** emitter record layout
    (`BuildStaticEmitterSet`/`BuildDynamicEmitters`), muzzle flash rides
    `extralight`/`pcData[16]`, `RunGiBake` synchronous, `RB_ApplyTierRt` 0/6,
    `render_scale` (`m_misc.c`), and every cited roadmap status.
