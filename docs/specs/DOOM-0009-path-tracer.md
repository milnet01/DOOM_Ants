# DOOM-0009 — Hardware path tracer (Stage 2)

**Status:** Reviewed — `/cold-eyes` loops 1–5 (see log), user-accepted 2026-06-25.
**§2 settings model revised 2026-06-27** (three tiers + a per-tier ray-tracing
toggle, art bound to the tier); revision re-run through `/cold-eyes` to clean (3
cold passes, see the loop log below). Implementation-ready. Design contract for the
Stage-2 renderer.
**Depends on:** DOOM-0008 (Stage 1 raster 3D: meshes, materials, sprites, UI
composite) shipped; DOOM-0026 renderer-backend seam; ADR
`docs/decisions/0001-renderer-language-and-api.md`.
**Feeds:** DOOM-0010/0011/0012 (Stage 3: dynamic lights, volumetrics, the 60 FPS
performance pass), DOOM-0042 (HD art theme), DOOM-0043/0044 (scene lights,
flashlight).
**Evidence base:** `docs/research/DOOM-0009-performance.md` (cited cost survey +
DOOM-native optimisations) and `docs/research/3d-renderer-approaches.md`. Binding
decisions live here; the research docs are the "why".

---

## 1. Goal

Light DOOM's existing 3D scene (the Stage-1 mesh) with a real hardware
**Monte-Carlo path tracer** — global illumination, ray-traced shadows, derived
emissive surfaces — so the world looks physically lit while still **feeling like
DOOM**. Stage 2 ships when a path-traced frame converges to a reference image
(white-furnace verified) and muzzle-flash dynamic shadows are visible, at an
**interactive ≥ 30 FPS at 1080p** on the reference GPU (AMD RX 6600 / RDNA2 /
RADV). The hard **60 FPS floor is deferred to DOOM-0012** — Stage 2's bar is
*correct and interactive*, not *fast*.

Non-goals for Stage 2 (deferred to Stage 3): moving/coloured/flickering dynamic
lights beyond the muzzle flash (DOOM-0010), volumetrics (DOOM-0011), and the full
performance pass to a hard 60 FPS floor (DOOM-0012). Stage 2 must be *correct and
playable*; Stage 3 makes it *fast and rich*.

## 2. Settings model — three tiers, ray tracing a toggle inside the two 3D tiers

Per the **2026-06-27 direction decision** (superseding the 2026-06-25 model, in
which ray tracing was orthogonal to a free-floating Classic/HD *art* toggle), the
player picks a **render tier**, and ray tracing is an **On/Off toggle that lives
inside both 3D tiers**. The art set is **bound to the tier**, not chosen separately:

- **Classic** — the 1997 software renderer: fully original art and lighting, no ray
  tracing, no flashlight. Untouched by Stage 2.
- **Solid** — 3D (Vulkan) with the **original 1993 art** and original-style
  lighting. Ray Tracing On/Off. Player flashlight available (DOOM-0044).
- **Ultra** — 3D (Vulkan) with the **HD PBR art set** (DOOM-0042) plus modern
  lighting — scene lights so rooms are never unintentionally black (DOOM-0043),
  volumetrics/god rays (DOOM-0011), dynamic lights (DOOM-0010) — and the same
  player flashlight (DOOM-0044). Ray Tracing On/Off.

**Ray Tracing is orthogonal to the tier, not its identity.** Each 3D tier renders
with RT *off* (raster lighting) or RT *on* (path-traced GI + ray-traced shadows) —
so the two 3D tiers each have an RT-off and an RT-on form, which with Classic makes
**five looks in all** — the eventual target matrix, completed only once the
tier-defining items land (DOOM-0042 art, DOOM-0010/0011 lighting); this spec ships
the shared RT-on back-end they sit on. (This is a *tier × RT* split, not the
retired *art × RT* toggle: the art set is fixed by the tier, per the bullets
above.) This path tracer
(DOOM-0009) is the **RT-on back-end** shared by both tiers; what differs between
Solid-RT-on and Ultra-RT-on is only the art set and the modern-lighting layer the
tier loads, never the integrator.

**Scope — what DOOM-0009 delivers vs. what the tier carries.** §2 describes the
tier *model*; this spec delivers only the shared RT-on path-traced back-end (build
order §7). The features that *define* the Ultra tier ship under their own roadmap
items, not here — HD PBR art (DOOM-0042), scene lights + ambient floor (DOOM-0043),
the player flashlight (DOOM-0044, shared with Solid), and dynamic/volumetric
lighting (DOOM-0010/0011, deferred to Stage 3 per §1 Non-goals) — each owning its
own RT-off/on behaviour. Listing them here defines the tier; it does not put them
in this spec's build order.

**In Stage 2 specifically,** the only *observable* difference between Solid-RT-on
and Ultra-RT-on is the **art set** — the modern-lighting layer (DOOM-0010/0011)
lands in Stage 3. Dark rooms are handled **per tier**: **Solid** deliberately leaves unlit rooms
dark — the player flashlight (DOOM-0044) is the answer, and whether to soften
pitch-black Solid rooms is left to playtest, not decided here; **Ultra** gets
deliberate scene lights + a gentle ambient floor (DOOM-0043) so it reads as a lit,
atmospheric space. The **muzzle flash is the exception** to the per-tier split: it
lights the room in **both 3D tiers, RT on or off** (firing-lights-the-room is core
DOOM feedback — Classic already does it via `extralight`). With RT **on**, this
spec adds the flash's *ray-traced shadows* (§4.1); with RT **off** it is a plain
raster flash. The broader dynamic-*atmosphere* lighting (DOOM-0010 — flickering
lamps, coloured lights) stays an Ultra feature.

**(Superseded for the RT-off case by DOOM-0170, per user request 2026-07-09.)** The
RT-off raster "performance mode" (DOOM-0170) extends dynamic **point lights** and
**baked-probe indirect bounce** to *both* 3D tiers — see DOOM-0170 §4.1/§4.2. So the
"Solid deliberately leaves unlit rooms dark" stance and the "stays an Ultra feature"
line above hold for RT-**on**; with RT **off**, DOOM-0170 is canonical (it is the
shared RT-off back-end, mirroring this spec as the RT-on one). The Solid-dark
soften-vs-keep call remains a play-test decision, now owned by DOOM-0170 §9 Q1.

**Menu rework (Stage-2).** Today the Options menu has one `"Renderer:"` item that
*cycles* Classic → Solid → Ultra (`m_menu.c` `M_ChangeRenderer` →
`RB_NextAvailableMode`, via `cycleOrder[]`), with the three menu positions mapping
1:1 onto the three `rendermode_t` values **in cycle order** — `RB_CLASSIC`, then
`RB_RASTER3D` (Solid), then `RB_RT3D` (Ultra). (That is the *menu* order; the
enum's own declaration order differs — `RB_CLASSIC`=0, `RB_RT3D`=1, `RB_RASTER3D`=2
— and `cycleOrder[]` deliberately decouples display order from enum value.) Stage 2
splits this into **three controls** (two tier×RT axes plus a Stage-2 quality
sub-setting):
- **Renderer:** Classic / Solid / Ultra — selects the tier (art + lighting set).
- **Ray Tracing: On / Off** — disabled in Classic; greyed out on a GPU lacking
  `VK_KHR_acceleration_structure` + `VK_KHR_ray_query` (INV-11).
- **Upscaler:** TAAU / FSR 2 / FSR 3.1 — the Stage-2 image-reconstruction method for
  the path-traced render (§4.4); the upscaler-*method* is settled (§4.4), while
  spp/render-scale exposure stays open (§9).

  *These three are the Stage-2 **target** menu. Shipped so far: the Renderer and
  Upscaler controls exist, but the Ray-Tracing axis is today the `~` debug toggle
  (`rb_rtdebug`), not yet a menu item, and the Upscaler control cycles **Off / TAAU**
  only — FSR 2 / FSR 3.1 are future entries. §9 tracks the remainder.*

Because RT is now independent of the tier, the 3-value `rendermode_t` enum can no
longer stand 1:1 for the player choice (it cannot express Ultra-with-RT-off). The
enum stays frozen and continues to name the **active back-end** (`RB_RASTER3D` =
RT-off raster path, `RB_RT3D` = RT-on path-traced path); the **tier** (Solid/Ultra)
becomes a separate art-and-lighting theme carried alongside it. The DOOM-0008 tier
auto-probe and the `RB_SetMode` rebuild path (DOOM-0026/0051) are reused unchanged;
only the menu gains the second axis plus a theme selector. The tier (Classic /
Solid / Ultra), the Ray-Tracing on/off toggle, and the upscaler-method selector
(§4.4) each persist as their own `m_misc.c` default, since the single `rendermode`
value used today can no longer encode these axes. (Implementation detail, settled here so step 1's menu work has a
target; not a product question.)

**INV-9 (art-set agnostic):** the integrator and lighting code reference only the
bindless material interface (albedo/roughness/metallic/emissive/normal samplers
indexed by material id), never the source art set. Selecting Solid↔Ultra changes
only which textures the material table points at.

## 3. Architecture

Inline **`VK_KHR_ray_query` in a compute megakernel** (per ADR 0001 and the RADV
survey: ray-query is mature on RADV and matches the megakernel plan; the RT
pipeline path is a later option, not Stage 2). One dispatch over screen pixels.

- **Acceleration structure:** the static map = one **BLAS** built once with
  `PREFER_FAST_TRACE | ALLOW_COMPACTION` (compacted). A **TLAS rebuilt every
  frame** over a few hundred instances; billboards (sprites) update via TLAS
  instance transforms. **Moving sectors (doors/lifts)** animate via DOOM-0049's
  per-vertex plane-height patching (`RB_UpdateMeshHeights`) — and because that
  *moves vertices*, the affected sector's BLAS must be **refit** each active
  frame: a rigid TLAS instance transform cannot express a non-rigid wall-height
  change. Only moving sectors refit; static geometry never does. **The cited
  research docs diverge here:** `DOOM-0009-performance.md` §2 says "instance
  transform only, no refit" — true only for *rigid* motion (billboards, monsters)
  — while `3d-renderer-approaches.md` says "per-sector BLAS refit per frame"; the
  latter wins for DOOM doors, whose wall-height change is non-rigid. This
  deliberately **overrides perf §2's [HIGH]-confidence "no refit"** — that finding
  rests on the premise "nothing in classic DOOM deforms," which DOOM-0049's
  per-vertex patching disproves (door walls do deform). (DOOM-0008 §Approach and
  §Geometry both state this — rigid movers → TLAS instance transform, moving
  sectors → per-sector BLAS refit; this spec's §3 is what they defer to.) The
  *only* part left open (§9) is the **granularity**, not whether to refit.
- **Materials → bindless** *(shipped — build step 1, 2026-06-25)*. The Stage-1
  single R8 atlas (which packed walls, flats *and* sprites — `rb_atlas_t::numsprite`)
  has been migrated to a bindless array-of-textures (`VK_EXT_descriptor_indexing`,
  core Vulkan 1.2): one image per texture/flat/sprite with native REPEAT wrap,
  indexed by material id from the hit shader. The R8-index + PLAYPAL-LUT decode (LUT
  supplied by `r_mesh.c` `RB_PlayPal`, applied in the mesh/overlay shaders), vertex
  plumbing, and staging upload carried over; "1 atlas + manual UV-wrap" became "N
  images + native wrap".
  This is the seam DOOM-0042's HD PBR set plugs into (INV-9).
- **Memory:** VMA (Vulkan Memory Allocator; the many image/buffer allocations of the
  RT work). RADV AS structures are fat (~137 B/tri vs ~45 on NVIDIA — estimate, confirm
  with RRA, the Radeon Raytracing Analyzer) —
  trivial for one DOOM map, but budget VRAM for large external WADs and always
  compact.
- **Colour:** light in **linear** space (sRGB→linear after the PLAYPAL lookup,
  treat palette colour as albedo); do **not** bake COLORMAP light-diminishing into
  albedo (it double-darkens once GI runs). Tonemap with Khronos **PBR Neutral**
  (preserves DOOM's saturated palette). *PBR Neutral supersedes DOOM-0008's earlier
  ACES default — a reconciliation `docs/specs/DOOM-0008-3d-renderer.md`
  §"The path tracer" already records
  (`aces_tonemap`, a DOOM-0008 Workbench-formula name not yet in engine code,
  predated the PBR-Neutral choice and is retired as the default operator, returning
  only as an optional "filmic" toggle, never the Stage-2 default).* A
  "vanilla-tint" post pass that quantises
  the HDR result back through COLORMAP/PLAYPAL is an optional toggle, not the model.

## 4. Lighting model

DOOM's derived emission is ~95% **static** (sector-lightlevel glow, bright/lamp/
computer emissive textures, sky sun). Only the muzzle flash is dynamic in Stage 2.
So split static from dynamic (the central performance lever):

### 4.1 Static GI bake + dynamic delta
- **Bake** a converged static-GI solution once per level load (the same ray-query
  integrator, run **synchronously at level load** — `RunGiBake` blocks, waiting
  idle between each of its 3 bounces) into a **sector-keyed irradiance
  cache** (§4.3). The steady-state (player not firing) frame is then a cache
  lookup + primary visibility — near-zero ray cost.
- **Dynamic delta:** when a muzzle flash is active, path-trace its **ray-traced
  shadows** for the few active frames only (dynamic-light gating), composited over
  the baked static. Idle frames stay cheap; firing frames pay for the delta.
  Note `player->extralight` is only the *gating signal* (a positionless `int`
  screen-brighten); the flash's **world position** is the view eye
  (`camPos` = `viewx`/`viewy`/`viewz`) offset **forward** along the view direction
  and **dropped** below it to the gun barrel (`muzzle = camPos + camDir·MUZZLE_FORWARD
  − (0,0,MUZZLE_DROP)`, `pathtrace.comp` `muzzleFlashDelta`), while `player->extralight`
  (set to 1/2 by the weapon fire states in `p_pspr.c`) stays the positionless on/off +
  brightness gate. So the delta has a real world origin — one that swings with the view
  — to cast shadows from, even though `extralight` itself carries no position.

### 4.2 Emission model (from DOOM-0008 spec)
- Sector `lightlevel` seeds a surface emissive term (brighter sectors emit more).
- Known light/lamp/computer textures + bright sector specials become stronger
  emitters, sampled by next-event estimation (NEE). Per-texture emissive strength
  = mean linear luminance of the texture's palette colours above a threshold,
  precomputed once at atlas/material build (no per-frame cost).
- **Sky** is a bounded sky-light + miss-shader environment (NOT emissive geometry,
  which drowns the denoiser). A ray that hits a sky-flat surface is a miss into the
  sky environment. **HDRI option:** the sky environment may be sampled from an
  Outdoor HDRI (the user's asset library) for realistic outdoor sky light + a
  directional sun extracted from the HDRI's brightest region; falls back to a
  procedural sky tint + sun when no HDRI is selected. (Spec-originated, not from
  the cited survey — both research docs treat sky as bounded sky-light only.
  Art-set-agnostic: helps both Classic and HD art.)

### 4.3 DOOM-native optimisations (from the research doc)
- **REJECT-driven light selection:** use DOOM's `REJECT` sector-visibility lump to
  cull, per shading point, the emissive sectors that provably cannot be seen — a
  conservatively-culled, tiny NEE candidate set instead of a CDF over all lights.
  REJECT is sector-to-sector visibility, *not* point-exact occlusion, so a
  surviving candidate still casts a shadow ray to confirm the actual light path.
  (In the shipped code this cull gates the **omnidirectional sprite-light** NEE loop —
  `pt_common.glsl`; the GI bake passes it off.)
- **Sector-keyed irradiance cache:** key the cache by `(subsector, height band)`
  rather than a uniform world hash grid. DOOM sectors are exactly the regions of
  piecewise-constant lighting bounded by walls, so a few probes per subsector
  capture the low-frequency irradiance with far fewer probes and **no thin-wall
  leak** (the failure mode of DDGI — dynamic diffuse global illumination). This *is* what the §4.1 bake fills. The *keying* by
  `(subsector, height band)` is settled; the *storage granularity* (per-vertex
  irradiance vs per-subsector probe volume) is the §9 open item.
- **Palette quantisation as a free denoiser:** when the vanilla-tint post pass is
  on, residual sub-quantum noise collapses to the same palette index, so the
  denoiser can target a looser convergence threshold.

### 4.4 Integrator
1 path/pixel (≈ primary hit + live direct/NEE shadow rays + a *cached* indirect
lookup — the indirect bounce is baked, §4.1/§4.3, not traced live per pixel) + temporal
accumulation + motion-vector reprojection · NEE (next-event estimation) — **no MIS**:
the shipped integrator is pure-Lambert, so there is no BSDF-light sampling to weight
(MIS returns only if the GGX/VNDF specular path below lands) · Lambertian diffuse +
cosine sampling (GGX/VNDF specular gated to measured need — matte DOOM art rarely
needs it) · firefly clamp + NaN guards (Russian-roulette termination is moot while the
live path stays single-bounce) · **half-resolution lighting**
trace — the live noisy direct/NEE shadow-ray lighting is cast for one pixel per 2×2
block (the fixed even/even sample — *straight* half-res, not checkerboard; the indirect
bounce is already baked into the GI cache, §4.3, so the half-res lever applies to the
live *direct* trace, not a live indirect bounce; the G-buffer + albedo stay full-res
for sharp edges + demodulation), reconstructed in the denoiser by a joint-bilateral
upsample at the temporal stage (guided by the full-res G-buffer) that then feeds the
à-trous spatial pass — which IS the A-SVGF spatial filter, not a separate stage;
biggest ms lever, near-free on matte art · **A-SVGF**
(adaptive spatiotemporal variance-guided filtering) denoise (purpose-built for the
muzzle-flash sudden-light case; clean-room from the paper, Q2RTX as readable GPL
reference). *On disk this ships as three passes that keep the shorter `svgf_*` file
names — `svgf_temporal` + `svgf_atrous` + `svgf_composite`; the adaptive **A** is the
step-6 anti-ghosting history-clamp inside `svgf_temporal`, not a separate stage.* ·
**upscale last** — decoupled, denoise-before-upscale, jitter-consistent
(the upscaler is fed the same per-frame sub-pixel jitter the path tracer used). No ReSTIR in
Stage 2 (few lights; its reservoirs are RDNA2's worst register case).

**Upscaler — player-selectable method (2026-06-28 decision).** The final upscale is a
settings axis, not a fixed choice: **custom TAAU** (temporal anti-aliasing upsampling) ·
**FSR 2** · **FSR 3.1** (AMD FidelityFX Super Resolution). All three consume the *same*
engine inputs (motion vectors + depth + jitter + exposure), so the upscale plumbing is
built once (for TAAU) and each FSR backend is then wired behind that same resource
contract. Offering both FSR generations is a *player look/cost preference*, not a fixed
quality ladder (FSR 2 tends sharper/more-aliased, FSR 3.1 more temporally stable); their
relative GPU cost on the RX 6600 is **measured in the step-7 perf pass** (§8), not assumed
here — note the well-known FSR 3 cost is *frame generation* (deferred below), not the
upscaler, so "FSR 2 is lighter" is not assumed without measurement. **FSR 3.1** is the
current FidelityFX Super Resolution upscaler (the exact FidelityFX SDK version is pinned
at integration, per the latest-dependency rule) and the recommended default on the
RX 6600 (RDNA2, the §8 target); **FSR 4** is RDNA4-exclusive (ML, hardware-gated — per
AMD's stated FSR 4 requirements) and therefore not an option on this GPU, so FSR 3.1 is
the ceiling here. The selected method persists as its own `m_misc.c` default alongside
the tier + RT axes (§2). **Frame generation is explicitly out of scope for DOOM-0009:**
FSR 3's frame-gen hooks the present/swapchain, needs HUD/UI composition handling, and is
coupled to frame pacing — none of which has a clean seam while the engine is present-
locked to the 35 Hz tic. It ships as its own roadmap item (**DOOM-0088**) gated behind
the render-rate decouple (DOOM-0048, now shipped — so the gate is cleared and DOOM-0088
is unblocked), not here. The upscaler integrates the FidelityFX SDK (MIT-licensed,
GPL-v2-compatible — confirm against the SDK LICENSE at integration).

## 5. Shading curves (Vestige Formula Workbench)

**INV-7 (no magic constants):** every numerical tuning curve in the path-tracer
shaders traces to a Workbench-exported `linuxdoom-1.10/shaders/formulas/*.glsl`
artifact (that `formulas/` directory was created during build steps 3/6 and now
exists — `formulas.glsl` + `pbr_neutral_tonemap.glsl` alongside the path-tracer and
denoiser shaders under `linuxdoom-1.10/shaders/`) (safe-math NaN-guarded), compiled
by `glslc` and committed; coefficient
drift is regression-locked by the Workbench harness. Curves to author there:
GGX/VNDF sample+PDF, cosine-hemisphere PDF, MIS power-heuristic weight, Russian-
roulette survival probability, A-SVGF temporal-blend α + edge-stopping weights,
sRGB↔linear, exposure/tonemap (PBR Neutral). The safe-math guards matter here
specifically: one NaN becomes a firefly the denoiser then smears. Workbench at
`/mnt/Games/Scripts/Linux/3D_Engine/`; requests tracked in that project's
`DOOM_Ants_Feedback.md` as `3D_E-0006…0010`.

## 6. Invariants

**INV-6/7/8 continue DOOM-0008's INV-6/7/8** (same numbers, same intent) — this
spec fills the threshold DOOM-0008 INV-6 deferred, and **tightens INV-8's test**
to *every tier path* **and** a validation-layer-equipped box (DOOM-0008's INV-8
read "level-load + play + exit on the dev machine"). **INV-9/10/11 are new to
Stage 2.** (INV numbers are per-spec: DOOM-0026's INV-1..5 are a separate series
with different meanings — don't conflate across specs.)

- **INV-6 (unbiased):** the integrator is unbiased up to Russian-roulette/clamp;
  converged accumulation matches a brute-force reference within a small relative-
  MSE tolerance. **Acceptance bar (spec-chosen, not a research-doc figure): ≤ 0.5%
  rel-MSE** on the white-furnace + a reference Cornell-style DOOM room (a small
  test scene this spec's implementer authors), measured against a **4096-samples-per-pixel (spp)
  brute-force reference** (the offline convergence point the 1-spp + temporal
  result is compared to). The reference counts as converged only when **doubling it
  to 8192 spp shifts the image by < 0.5% rel-MSE** (the same bar); if that self-
  consistency check fails, raise the reference spp until it holds — an objective
  test, not a "looks noisy" judgement. This is the threshold DOOM-0008 INV-6
  explicitly defers here. *(Known coverage gap: the shipped verify exercises the
  static light-selection path only — `omniStart == emitCount` — so the
  omnidirectional sprite-light NEE path is not yet covered by this bar; DOOM-0122.)*

  **Amended 2026-08-02 (DOOM-0297) — the SAMPLE COUNT is per-`gamemode`; the BAR is not.**
  The shipped gate ran one configuration against whichever IWAD was loaded and
  deterministically failed on DOOM 2 (3.4993% rel-MSE) while passing on DOOM 1 (0.1091%)
  **on the same build** — `git stash` to an untouched tree reproduced it to four decimals.
  That is an under-sampled gate, not a renderer defect, and the fix is more samples rather
  than a looser bar:

  | `gamemode` | NEE spp | reference spp | bar | measured | invocation |
  |---|---|---|---|---|---|
  | `shareware` / `registered` / `retail` | 16384 | 4096 | **≤ 0.50%** | 0.1091% | `-iwad doom.wad -warp 1 1 -noinput -rtverify` |
  | `commercial` | 262144 | 16384 | **≤ 0.50%** | 0.3665% | `-iwad doom2.wad -warp 1 -noinput -rtverify` |

  ⚠ **The score is a property of a map AND a camera, not of an IWAD.** `RB_RtVerify` builds
  its view from `g.lastView` at the first ready present, so a different `-warp` gives a
  different number. The rows above are defined **only** at the invocations quoted; running
  `-rtverify` elsewhere is a diagnostic, not this gate. The `gamemode` key selects the
  sample count, and `gamemode` is what the engine actually has — `plutonia.wad`, `tnt.wad`
  and any PWAD loaded over DOOM 2 are all `commercial` and inherit the higher count.
  **That is safe precisely because the bar did not move:** more samples can only tighten a
  variance-limited gate, whereas an inherited *bar* would have hidden a real defect. It is
  the reason the bar is left alone rather than raised to fit.

  **Evidence that DOOM 2's residual is variance.** Holding the reference fixed at 16384 spp
  and quadrupling NEE: **0.7330% → 0.3665%**. An earlier reading suggested a floor near
  0.72%, and this disproves it — that apparent plateau came from a third point which
  quadrupled only the *reference* (0.7330% → 0.7245%) and so could not move the NEE term at
  all. Separating the two terms from the pair that varied only the reference
  (3.4993% at 4096 spp vs 2.9124% at 65536 spp, NEE fixed) puts the NEE variance at ~0.489%
  of the 0.7330% reading and the whole remainder at ~0.244%.

  ⚠ **What is NOT established: that the residual is exactly zero.** Quadrupling NEE gave a
  2.0× fall where pure variance predicts 4.0×, and independent extrapolations of the
  constant term scatter by about ±0.2% rel-MSE — so any true bias is bounded by roughly
  that, not shown to be absent. The gate does not need it to be: 0.3665% clears 0.50% with
  27% headroom, against run-to-run scatter measured at ±16% on DOOM 1 (0.0796–0.1091%
  across runs; the table quotes the worst). Raising the reference as well moves it only to
  0.3419%, which is why the extra 4× is not spent.

  ⚠ **A bias-extrapolation gate was derived and MEASURED before this was chosen, and it
  failed.** Model `E = a/N_nee + c/N_ref + b²` — the constant is **b²**, not `b`, because
  rel-MSE is a squared metric, and it is a negative *b²* that is unphysical. Quadrupling
  both counts quarters both variance terms, giving `b² = (4·E_4x − E_1x)/3` from two runs.
  Measured: **+0.0135% on DOOM 1** (0.1091% → 0.0374%) and **−0.1891% on DOOM 2**
  (3.4993% → 0.7330%). The negative is impossible, so the figure is slop of the same order
  as the bar it would replace, and the 4.00× fall it tests for came out **2.92× and 4.77×**.
  Recorded so it is not re-proposed: it is the obvious next idea and it is worse than what
  it replaces at these counts.

  **Scope.** This amendment governs the **NEE-vs-reference rel-MSE gate only**. The
  reference-convergence self-check and the white-furnace bar (`< 1e-3`, analytic and
  scene-independent) are unchanged and stay IWAD-independent. The authored Cornell-style
  test scene named in the paragraph above **has never been built** — the shipped gate has
  always measured a game map at the spawn view — so that clause describes intended future
  coverage, not what `-rtverify` implements today; it is recorded here rather than left to
  read as a contradiction.

- **INV-7 (no magic constants):** §5.
- **INV-8 (validation-clean):** zero Vulkan validation-layer errors over a
  multi-second run on every tier path. Must be exercised on a box with
  `VK_LAYER_KHRONOS_validation` installed — the layer was not installed on the dev
  box during early Stage-1 bring-up (ROADMAP DOOM-0008 progress note), so this
  invariant needs a fresh check on a layer-equipped machine.
- **INV-9 (art-set agnostic):** §2.
- **INV-10 (toggle parity):** switching Ray Tracing Off→On→Off mid-game leaves the
  scene correct each time (reuses the `RB_SetMode` level-rebuild + screen-wipe path
  from the DOOM-0026 seam, hardened by the DOOM-0051 mid-game-switch fix); RT-Off
  (raster) output — in either Solid or Ultra — is unchanged by any RT-only code.
- **INV-11 (graceful fallback):** on a non-RT GPU, "Ray Tracing: On" is
  unselectable and the engine stays on Solid/Classic; no crash, no half-state.

## 7. Build order (cheapest-first; each step independently verifiable)

*Status audited against on-disk code 2026-07-04: steps 1–5 shipped (step 3 as
NEE-only — see step 3); step 6 shipped bar the FSR upscaler backends (TAAU only on
disk); step 7 ongoing (DOOM-0090). MIS and Russian-roulette appear in `formulas.glsl`
but are **N/A for the shipped pure-Lambert NEE-only integrator**, not pending work
(the DOOM-0092 research decision); they would return only if a future GGX-specular /
multi-bounce path lands (§4.4).*

1. **Bindless materials** — *shipped 2026-06-25* (the materials path is now
   bindless-only, no atlas fallback — `r_vulkan.cpp`). Migrated atlas →
   array-of-textures; Solid output unchanged (INV-10). Verified: screenshot parity
   with the atlas path.
2. **BLAS + TLAS** — *shipped* (`g.blas`/`g.tlas` in `r_vulkan.cpp`; ray-query
   traversal `rayQueryInitializeEXT` in `pathtrace.comp`/`pt_common.glsl`, plus a
   per-frame sprite BLAS and a static sky BLAS as extra TLAS instances).
   White-furnace + intersection test (INV-6/8).
3. **Direct lighting only:** — *shipped (NEE-only).* NEE + firefly clamp + NaN guards
   via ray-query with a genuine `REJECT`-culled light set (`pt_common.glsl` reads the
   packed REJECT bitmatrix fed by `RB_RejectMatrix`) are wired. **MIS is N/A** — the
   shipped integrator is pure-Lambert with a cosine-hemisphere bounce, so there is no
   BSDF-light sampling for MIS to weight (the DOOM-0092 research decision); **Russian-
   roulette is likewise moot** — the live path is single-bounce (indirect is baked,
   §4.1), so there is nothing to terminate. Both remain library-only `formulas.glsl`
   functions (`mis_power_heuristic`, `rr_survival`, no call sites) held for a possible
   future GGX-specular / multi-bounce path. Reference-image regression (INV-6).
4. **Static GI bake** — *shipped* into the sector-keyed cache at level load (§4.1,
   §4.3): `bake.comp` runs a 3-bounce ping-pong per level into one SH-L1 irradiance
   probe per subsector; the megakernel reads the baked `probeBuf` (not live-traced).
5. **Dynamic delta:** — *shipped.* `muzzleFlashDelta()` (`pathtrace.comp`) casts a
   ray-traced shadow from a gun-barrel point light, composited over the baked static
   and gated on `extralight` (set by the weapon fire states, §4.1). Verify: a
   muzzle-flash shadow whose *direction tracks the muzzle position* as the player
   rotates (a positionless screen-brighten would fail this — it guards the §4.1
   derivation).
6. **Half-res direct/NEE trace + A-SVGF** — *shipped* (50% render-scale default;
   three real SVGF passes `svgf_temporal`/`svgf_atrous`/`svgf_composite`, §4.4), then
   **upscale** — *TAAU shipped; FSR 2 / FSR 3.1 backends pending.* The upscale
   plumbing is built once behind the TAAU resource contract (`taau.comp`); the FSR
   backends wire in behind that same contract (frame generation excluded — DOOM-0088,
   itself gated behind DOOM-0048).
   Verify: stable image; no visible trailing/smearing of the flash highlight in the
   frames after it ends (the A-SVGF history clamp), screenshot-compared frame N vs N+3.
7. **Perf pass** — *ongoing (DOOM-0090)* toward the 60 FPS floor; reassess a runtime
   hash cache + optional ReSTIR GI against measured noise (this tips into Stage 3 /
   DOOM-0012).

## 8. Performance budget (planning; measure on the RX 6600)

Stage-2 target: **1080p @ ≥ 30 FPS interactive** on the RX 6600 (the §1 ship bar);
the hard 60 FPS floor is formally owned by DOOM-0012. The ≥ 30 figure is a
spec-chosen, conservative Stage-2 floor: Stage 2 gates on *correctness*, not the
final frame rate. (The research's "60 FPS trivially met" is an optimistic,
unmeasured estimate — ≥ 30 is the safe bar until it's measured on the RX 6600.) Must-measure-before-committing (no
published RADV numbers): A-SVGF ms @ 1080p (research plans ~2–3 ms — the number to
beat); the leaned cache/bake ms; AS
build/refit ms (worst case to budget: an open door's per-frame BLAS refit
coinciding with a muzzle-flash dynamic-delta trace — the two costs stack); the
per-level static-bake time; and the ReSTIR-GI register/occupancy cost on RDNA2
(the gate for the step-7 reassessment). Lean the
megakernel hard (split
shading out of traversal, wave32, opaque-only geometry + separate alpha sprites,
early-terminate shadow rays, one live RayQuery) — RDNA2 runs BVH traversal as
shader code, so occupancy/registers dominate. Build on **Mesa ≥ 25.2 (26.0
preferred** — the BVH-builder/codegen wins land at 25.2; `ds_bvh_stack_*`
traversal speedups are RDNA3/4-only, per the research doc).

## 9. Open questions (each names the build step that must close it)

*Three of the four below were **resolved on disk** as their build steps shipped
(audited 2026-07-04); the choice each made is recorded inline. Only the
spp/render-scale exposure remains genuinely open.*

- **Bake storage** (build step 4) — **resolved: per-subsector SH-L1 probe.** The bake
  writes one spherical-harmonic (L1) irradiance probe per subsector (`bake.comp`,
  `RunGiBake`), keyed triangle → subsector → probe via `triSsBuf` — not the per-vertex
  alternative.
- **Moving-sector AS granularity** (build step 5) — **resolved: whole-affected-BLAS
  refit (the coarse option).** An in-place `ALLOW_UPDATE` refit re-reads the patched
  vertex buffer on any frame `RB_UpdateMeshHeights` flags a moving door/lift; on DOOM's
  ~2k-tri meshes it measures well under budget, so splitting rigid caps into separate
  TLAS instances was judged unnecessary (`r_vulkan.cpp` moving-sector refit). §3 had
  settled *that* a refit is needed; this settles the *granularity*.
- **HDRI sun extraction** (soft gate — step 5 shipped without it) — **still open,
  deferred to DOOM-0043:** brightest-texel vs a fitted directional; the procedural sun
  suffices for Stage 2, so the extraction defers to DOOM-0043 (deliberate scene lights).
- **Quality sub-setting** (the Stage-2 menu rework, §2) — **partially resolved.** An **upscaler**
  menu control shipped (`M_ChangeUpscaler`, `rb_upscaler`) but today cycles only
  **Off / TAAU**; FSR 2 / FSR 3.1 are future entries on that same control (the method is
  settled in §4.4 — TAAU shipped, FSR pending). The
  **Ray Tracing** On/Off axis is today the `~` debug toggle, not yet a dedicated menu
  item. **Still open:** whether the Ray-Tracing control also exposes spp / render-scale
  in Stage 2 (else deferred to DOOM-0012).

---

## Cold-eyes loop log

**3-lane partition each loop:** (A) spec vs the two research docs, (B) spec vs
sibling specs (DOOM-0008/0026, ADR-0001, ROADMAP), (C) spec vs cited engine code.
Each loop briefed cold — no prior-loop findings handed to the reviewers.

- **Loop 1** — 0 CRITICAL, 6 HIGH, 10 MEDIUM + lows. Substantive: broken `§2.5`
  xref; "playable frame rate" unquantified; the "Ray Tracing" menu item described
  as existing (code has a 3-way cycle); `extralight` is positionless; ACES↔PBR
  tonemap conflict; INV-6/7/8 shared with DOOM-0008 unflagged; the moving-sector
  AS claim physically wrong (vertices move ⇒ BLAS refit). All verified + fixed.
- **Loop 2** — mostly self-inflicted nits from loop-1 edits (bare `DOOM-0008 §`
  placeholders, `4096 spp` unjustified, `137 B/tri` lost its baseline, INV-9 stated
  twice) + `aces_tonemap` still un-marked in DOOM-0008's curve list. Fixed.
- **Loop 3** — §3-asserts-vs-§9-open contradiction on moving-sector AS resolved
  (rigid → instance, non-rigid → refit); FPS/4096-spp wording corrected; INV-8
  tightening + INV-10 repoint to `RB_SetMode`. Lane C (doc-vs-code) **clean**.
- **Loop 4** — Lane C **clean** again. FPS framing de-attributed from the research;
  refit override of the [HIGH] "no refit" finding made explicit; A-SVGF budget,
  bake keying/storage split, per-spec INV note. Fixed.
- **Loop 5** — polish + 2 real sibling drifts: DOOM-0008 header never flipped to
  Shipped; its Workbench list named only the retired `aces_tonemap`. Fixed. No
  design defects; no CRITICAL across any loop.

*(Looped per global rule 14. Severity converged loop-over-loop — substantive in
loop 1, polish-only by loop 5, with doc-vs-code clean for the final three loops.)*

### 2026-06-27 — §2 tier-model revision re-review (3 cold passes)

§2 was rewritten (RT becomes an On/Off toggle inside *both* 3D tiers; the art set
is bound to the tier — Solid = original art, Ultra = HD PBR; "Solid/Ultra" is no
longer a synonym for "RT off/on"). Re-looped per rule 14, same 3-lane partition,
each pass briefed cold.

- **Pass 1** — 0 CRITICAL. Enum-order misstatement in §2 (listed cycle order as if
  it were the `rendermode_t` declaration order; flagged by all three lanes);
  `RB_PlayPal` mis-described as performing the PLAYPAL decode (it only supplies the
  LUT — decode is in the shaders); §2 implied this spec delivers the whole Ultra
  tier; "four configs" phrasing echoed the retired art×RT matrix. All verified +
  fixed.
- **Pass 2** — ROADMAP DOOM-0009 bullet still stated the retired "Solid=RT-off,
  Ultra=RT-on" model (annotated to the 2026-06-27 model); §3/§7 described the
  bindless-materials migration (build step 1) as future when it shipped 2026-06-25
  (marked shipped); §2 ambiguities resolved (Stage-2 Solid-vs-Ultra difference is
  art-set only; DOOM-0043's ambient floor scoped — **subsequently set Ultra-only**
  by the 2026-06-27 user decision (Solid leaves unlit rooms dark, the flashlight is
  the answer; revisit at playtest — see §2); the two player axes persist as two
  `m_misc.c` defaults). Fixed.
- **Pass 3** — Lane C **clean**. ROADMAP DOOM-0009 status `💭`→`🚧` (shipped +
  in-flight work); tonemap-supersession reworded to the reconciled state
  (DOOM-0008 already records it); "five looks" qualified as the eventual target
  matrix. Fixed. Lane A's residual findings were pre-existing §3/§4 research-
  fidelity nits outside this revision's scope (muzzle world-position source for
  build step 5; "exact"→"conservative" REJECT wording; DOOM-0008 cross-ref anchor)
  — captured as a roadmap doc-fix follow-up rather than reopening previously-
  converged sections.

*(Edited surface converged: doc-vs-code clean, no CRITICAL/HIGH left on the §2
model. No prior-pass findings handed to reviewers — each pass cold.)*
- **2026-06-28 — §4.4 upscaler-method decision (2 cold passes, 2 lanes each)** — Recorded the player-selectable upscaler decision (custom TAAU / FSR 2 / FSR 3.1; frame-gen deferred to DOOM-0088, gated behind DOOM-0048). Loop 1 (2 lanes): 1 HIGH (§9 dangling "build step 6-d" — §7 has flat steps 1-7), 2 MED (§2 upscaler axis dangling; "no ghosting" untestable), several LOW (A-SVGF/spp/TAAU/FSR/VMA/RRA/DDGI unexpanded; FSR currency/license referents; jitter-consistent undefined). All verified + fixed. Loop 2 (2 lanes): loop-1 fixes held (none resurfaced); surfaced introduced-by-edit nits (m_misc 2-vs-3 defaults, "step 6 perf pass" → step 7, §7-6 frame-gen echo) — all fixed — plus PRE-EXISTING drift orthogonal to the upscaler decision: spec stale vs shipped 6a/6b (formulas/ now exists — fixed; build-step statuses, §2 tier×RT prose repeated 3-4x, A-SVGF vs svgf_* shader naming) — bundled onto DOOM-0081. Per user direction (move on when only verified polish remains, no structural/architectural findings), stopped the loop.
- **2026-06-28 — §4.4 6-c half-res reconcile (1 quick cold pass)** — Reconciled §4.4 for the build-step-6c scope decision: the live indirect bounce is baked (§4.1/§4.3), so "half-res indirect" became "half-res LIGHTING" — the live direct/NEE shadow-ray trace is cast for one even/even pixel per 2×2 block, reconstructed in the denoiser (joint-bilateral upsample at the temporal stage → à-trous), G-buffer + albedo full-res. One quick cold pass (1 lane): 2 HIGH (§7 step 6 still said "indirect"; §4.4's "+1 indirect bounce" cost formula contradicted the baked-cache reality), 1 MED (à-trous-vs-A-SVGF read as two stages not one), 2 LOW (sample-placement unspecified; sentence density). All verified + fixed: §7 step 6 → "Half-res direct/NEE trace"; line 217 → "cached indirect lookup … baked, not traced live"; à-trous named as the A-SVGF spatial filter; pinned "fixed even/even sample, straight half-res". Per user direction (move on when only verified polish remains), did not re-loop.
- **2026-07-04 — DOOM-0057 + DOOM-0081 reconciliation (5 cold passes)** — Closed the two deferred doc-fix bundles: DOOM-0057 (align DOOM-0008 §Approach with §Geometry + this spec's §3 on moving-sector AS) and DOOM-0081 (six §3/§4/§7 polish nits). Loop 1 (2 lanes, DOOM-0008 + DOOM-0009): DOOM-0057 confirmed clean by both lanes; surfaced the now-stale §3 "DOOM-0008 is split" parenthetical + pre-existing DOOM-0008 drift (seam described as `R_VulkanBackend()` vs shipped `RB_Vulkan_*` set; settings-model superseded by §2). Loop 2: all loop-1 accuracy fixes held; found H1 — my §7 shipped-status audit now conflicted with §9's "gates step N" framing. Loop 3: §7↔§9 reconciled clean; fixed M1 (upscaler menu is Off/TAAU, not a 3-way method axis), M2 (§7-6 frame-gen cite DOOM-0048 → DOOM-0088), M3 (MIS/RR reframed **N/A** not "pending", per DOOM-0092 — resolves DOOM-0124). Loop 4: verified clean; fixed DOOM-0008 stage-table row + §2 shipped-vs-target note + INV-6 DOOM-0122 coverage-gap disclosure. Loop 5: **0 CRITICAL/HIGH**, doc-vs-code clean; last fix = supersession banner on DOOM-0008's integrator prose (MIS/RR/multi-bounce). **Verified against disk each loop:** muzzle origin (`pathtrace.comp` `muzzleFlashDelta`), synchronous `RunGiBake`, per-subsector SH-L1 bake, whole-BLAS coarse refit, `mis_power_heuristic`/`rr_survival` uncalled, Off/TAAU menu. Deferred by scope decision (surgical doc-fix, not a rewrite): §2 tier×RT prose collapse + §4.4/§3 sentence density + TOC → **DOOM-0168**; residual DOOM-0008 seam/settings-model drift → **DOOM-0167**. Stopped at the rule-14 max-loop cap with only those tracked readability items outstanding.
