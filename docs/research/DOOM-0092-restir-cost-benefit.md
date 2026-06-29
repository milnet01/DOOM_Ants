# DOOM-0092 — ReSTIR cost/benefit on RDNA2, and static SH-L1 bake vs a dynamic DDGI probe field

**Status:** Research / decision notes (not a spec or contract). Compiled
2026-06-29 to close the coverage gap flagged in
`DOOM-0009-rt-denoiser-upscaler-bestpractices.md` §4 (the 2026-06-28
lighting-efficiency coverage gap) and to re-examine the "no ReSTIR" call (`DOOM-0009-path-tracer.md` §4.4, the *"No
ReSTIR in Stage 2"* line) in light of measurements taken this session. Binding
decisions stay in the DOOM-0009 spec; this is the evidence behind whether
DOOM-0012 / Stage-3 ever opens a ReSTIR work-item. Reviewed via independent
cold-eyes passes, looped to clean (log at the foot). Complements
`DOOM-0009-performance.md` (the 2026-06-25 survey) — that ranked techniques in the
abstract; this one judges *one* technique against *measured* numbers from the
running engine.

**Citation note:** `DOOM-0009-performance.md` uses flat numbered lists under its
`## 2` (survey findings) and `## 3` (DOOM-native ideas) headings — it has no
`§2.x`/`§3.x` sub-anchors. References below cite it as "perf §2 item N" /
"perf §3 idea N".

**Scale discipline:** every FPS / millisecond figure carries its render scale.
Never pair a 100%-scale cost with a 50%-scale frame-rate — the megakernel and
denoiser both scale with render-pixel count, so a mixed-scale ratio conflates the
resolution drop with the code change.

**Target (unchanged):** AMD Radeon RX 6600 (RDNA2) on RADV/Mesa Linux; classic
DOOM levels; 1080p @ 60 FPS floor (the DOOM-0012 aspiration the megakernel is
judged against); GPL-v2 binary (no NVIDIA RTX SDKs); inline `VK_KHR_ray_query`
in a compute megakernel.

---

## 0. Why this is being asked again

The 2026-06-25 survey rejected ReSTIR for one reason: *"DOOM is a few-light
scene"* (perf §2 item 7; spec §4.4). That premise is now **partly false, and
measured so.**

The DOOM-0090 per-pass GPU profiler (the `\` key) on the RX 6600 showed the
megakernel — not the denoiser — is the dominant cost, and that it **scales with
the number of emitters in view**. The root cause (fixed in 7ac4a13, the DOOM-0090
omni-cull): the omnidirectional sprite-light NEE loop cast **one shadow ray per
in-view emissive sprite, per pixel, per bounce, with no cull** — O(emitters)/pixel.

Same-scale figures (each with its render scale, per the discipline note above):

- **At 100% render scale:** the megakernel ran **80–110 ms** in a light-heavy
  "glowing-prop" room (**8–9 fps**) *pre-cull*; the cull cut it to **~54–65 ms**
  (**10–11 fps**). (ROADMAP.md validated 2026-06-29.)
- **At 50% render scale** (the user's normal play scale): the same room ran
  **~20 fps** pre-cull → **~35–42 fps** from the cull alone (megakernel ~14 ms),
  then **~37–46 fps** after the *later* SVGF a-trous 5→4 trim (megakernel ~15 ms,
  the clear #1 cost; denoiser ~8.5 → ~7.5 ms). (ROADMAP.md validated 2026-06-29,
  lines 844/846 — note the 37–46 figure needed two optimizations, not the cull
  alone.)

The capture counted **22–36 omni emitters + ~70–90 static** (the static figure is
*derived*: total − omni) = **~104–118 total emitters in view** in the light-heavy
room. (Emitter count is a scene property — independent of render scale — so the
~100-light premise holds at any scale.)

So DOOM's worst case is not "a few lights" — it is **~100 lights**, and the
integrator already pays O(N) per pixel there. The cull (DOOM-0090) removed the
*wasted shadow rays* (it skips a sprite's shadow ray when its unshadowed
contribution is negligible) but left the **candidate set unchanged**: a resampler
choosing one light to shade still faces ~100 emitters. That is precisely the
regime where "sample one light well instead of looping all of them" (RIS /
ReSTIR) is *designed* to help, and the cull's O(N) structure is exactly what a
resampler would replace. Hence: re-open the question, with numbers this time.

This note answers the three DOOM-0092 sub-questions and ends with a go/no-go.

---

## 1. Q1 — ReSTIR DI/GI cost on the RX 6600 megakernel

### 1.1 What ReSTIR is, and where its wins actually land (verified)

ReSTIR DI (Bitterli et al., 2020) keeps a per-pixel *reservoir* — a running
weighted sample of "the best light found so far" — and resamples it across
neighbours (spatial) and across frames (temporal). Its headline result:
scenes of **thousands to millions** of emissive triangles in real time,
equal-error **6–60×** faster than prior light sampling, ~8 rays/pixel
(NVIDIA/Dartmouth, 2020; figures quoted from the paper, not independently
re-measured here). Production follow-ups (Wyman & Panteleev, 2021) cut the cost a
further ~7× and ship as RTXDI.

The load-bearing caveat for us, also verified: ReSTIR's advantage **scales with
lighting *complexity*, not absolute count.** The community consensus (Wyman's
2023 SIGGRAPH course; Interplay-of-Light's "gentler introduction") is that with
*few* lights, or when most lights at a point are shadowed/negligible, the
reservoir + spatial-reuse machinery is **pure overhead** — a single spatial pass
is already redundant in simple lighting, and a second is wasted work. ReSTIR is a
big constant-and-complexity win at thousands of lights, a wash-to-loss at a
handful.

DOOM's ~100-emitter worst case sits **between** those poles. Nothing in the
literature gives a clean "≥N lights → ReSTIR wins" threshold; the honest reading
is that ~100 lights is in the grey zone where it *might* help. Crucially, the
grey-zone evidence cuts both ways — it supports adopting the *cheap* end of the
resampling family (RIS) as readily as it supports full ReSTIR. The deciding
factor between them is not the light count but the **per-GPU register cost**,
which on RDNA2 is the adversarial case (below).

### 1.2 Why RDNA2 is ReSTIR's worst hardware case (verified mechanism, unmeasured ms)

On RDNA2 only ray-box / ray-triangle intersection is fixed-function; **BVH
traversal is compute** (perf §2 item 3, citing Chips and Cheese + GPUOpen). So
the path tracer's speed is gated by **occupancy** — how many wave32 warps stay
resident — which is gated by **VGPR (vector register) pressure**. A measured RT
kernel in that survey hit 96 VGPRs → only 10 of 16 possible waves.

ReSTIR makes this worse in two compounding ways:

1. **The reservoir is live state across the whole kernel.** A DI reservoir holds
   the chosen sample + its target-function value + running weight sum + sample
   count (and for spatial reuse, neighbour reservoirs are read back in). That is
   live registers held *across* the BVH-traversal section that already wants every
   register it can get. Reservoir state and traversal state contend for the same
   VGPR file.
2. **Spatial reuse adds neighbour BVH work** (visibility-reuse rays) and more
   divergence (each pixel's neighbours resampled differently).

The spec already names this: *"No ReSTIR in Stage 2 (few lights; its reservoirs
are RDNA2's worst register case)"* (§4.4). As a rough order-of-magnitude only:
the 2020 paper's ~8.9 ms @ 1080p / 1 spp on a 3090, scaled by a crude
cross-vendor guess (~28–30% RT throughput — and even that ratio is *borrowed*
from perf §2 item 4's 6900 XT→6600 scaling, not a 3090→6600 one), lands near
**~30–32 ms for DI alone @ 1080p / 100% scale**. Compared *at the same full
resolution*,
the current post-cull megakernel is **~54–65 ms @ 100%** — so ReSTIR DI would add
roughly half-again the existing kernel cost (or, if it *replaced* the NEE loop,
land in the same ballpark). Every number in that chain is an estimate stacked on
an estimate; treat it as **"plausibly far too expensive to add,"** not as a fact.
(The current ~15 ms figure is the **50%-scale** megakernel — don't ratio it
against the 1080p DI estimate; they are different resolutions.)

### 1.3 The unmeasured number that actually decides this → RGP (DOOM-0090)

The single fact that gates a full-ReSTIR go/no-go is: **how many VGPRs does the
current megakernel use, and how much occupancy headroom is left?** This number
comes only from a GPU capture — **AMD Radeon GPU Profiler (RGP) via RADV's
trace path** — which is exactly the DOOM-0090 occupancy pass (capture steps:
`docs/research/DOOM-0090-rgp-capture-guide.md`, run by the user on the RX 6600).

The decision rule, pinned numerically against the survey's measured spill point
(96 VGPRs → 10/16 waves). Every capture outcome maps to a decision:

- **Reject full ReSTIR on the 6600** if the megakernel is already
  **≥ ~96 VGPRs / ≤ 10 waves per SIMD** (VGPR-limited). Adding reservoir state
  spills to scratch and tanks occupancy — non-starter.
- **Keep full ReSTIR as a measure-further candidate** only if there is clear
  headroom: **≤ ~64 VGPRs / ≥ 12 waves per SIMD.**
- **Mid-band (65–95 VGPRs / 11 waves) → default to RIS-only**: treat a mid-band
  result as "no" for *full* ReSTIR unless occupancy is clearly ≥ 12 waves.

Either way the cheap ladder (§1.4) is built first; this capture only decides
whether *full* ReSTIR is ever worth prototyping on this hardware.

### 1.4 The cheaper middle ground ReSTIR's framing hides

ReSTIR is the *heavyweight* end of a family. The family's cheap end is plain
**RIS** (Resampled Importance Sampling) — pick M candidate lights, resample down
to 1 by contribution weight, cast **one** shadow ray — **with no reservoir kept
across frames or neighbours.** That removes the register-resident reservoir and
the neighbour-reuse rays (the two things that hurt RDNA2 in §1.2) while still
collapsing the O(N) omni loop to O(1) shadow rays + O(M) cheap unshadowed
weight evals. It is strictly cheaper than the current "shadow-ray-every-omni"
loop and *much* cheaper than full ReSTIR.

DOOM also has a **free, exact, engine-native** lever the generic literature
can't assume (perf §3 idea 1; also spec §4.3):

- **REJECT-lump light culling.** DOOM ships a precomputed sector→sector
  visibility bitmatrix. A shading point in sector *S* can *exactly* skip every
  emissive sector REJECT marks invisible from *S* — turning the ~100-light set
  into a small exact candidate set per surface, with **zero** wasted shadow rays
  and **zero** added register state. This is cheaper *and* less noisy than any
  stochastic scheme, and the data already exists in the WAD. (REJECT is a
  *recommendation* here — it is not yet implemented in the shaders.)

The recommended progression is therefore **REJECT cull → RIS (1 shadow ray) →
*only then* measure whether ReSTIR's temporal/spatial reuse adds anything** — not
"jump straight to ReSTIR." Each step is cheaper to build and de-risks the next.

### 1.5 ReSTIR GI (second bounce)

ReSTIR GI (resampling indirect paths) is strictly heavier than DI — it resamples
whole sample *paths*, more register state again. DOOM-0009's indirect bounce is
**baked**, not traced live (spec §4.1/§4.3; the sector-keyed irradiance cache).
A baked cache lookup is ~free and leak-free; ReSTIR GI would be paying RDNA2's
worst register case to recompute what the bake already stores. **ReSTIR GI stays
deferred** (spec §8 performance budget + §7 step-7 note) unless the bake proves
inadequate for dynamic indirect — a separate, later question.

---

## 2. Q2 — Static SH-L1 bake staleness vs a dynamic DDGI probe field

The spec's indirect-lighting plan is a **sector-keyed irradiance cache**, keyed
by `(subsector, height band)` (spec §4.1, §4.3; perf §3 idea 2). Storage
granularity (per-vertex irradiance vs per-subsector SH-L1 probe volume) is an
open §9 item. The DOOM-0092 question: **does a *static* bake go stale when
doors/lifts change local visibility, and is a *dynamic* field worth its cost?**

### 2.1 When a static bake is actually wrong

A static bake assumes geometry-and-light visibility is fixed. DOOM breaks that in
exactly three ways, all bounded:

1. **Doors/lifts** change whether sector A sees sector B's light. A bake done
   with the door shut under-lights A when it opens (and vice-versa). This is the
   real staleness case.
2. **Light-level specials** (blink/glow/flicker sectors, `extralight` from
   firing) change emitter *intensity*, not geometry. A bake keyed (proposed —
   this is a design choice, not yet built) on *unit* irradiance per emitter,
   scaled at runtime by the live sector light level, would handle these with
   **no re-bake** — intensity is a multiply, not a visibility change.
3. **Monsters/projectiles** are dynamic occluders/emitters. These are handled by
   the *live* NEE/direct pass (the dynamic delta), not the indirect bake — out of
   scope for this question.

So the only genuine staleness is **(1) door/lift visibility**, and it is local
(one or two adjacent sectors) and event-driven (fires on the door tic, not every
frame).

### 2.2 The cheap fix beats a dynamic field

A full dynamic irradiance field (DDGI-style, re-traced probes every N frames)
costs continuous ray budget *and* reintroduces the thin-wall light-leak that the
sector keying was chosen to avoid (DDGI grid cells cross walls; sector probes
never do — spec §4.3; perf §2 item 9 / §3 idea 2). Paying that every frame to fix
an event that fires a few times per level is the wrong trade.

The DOOM-native fix: **lazy per-sector re-bake on the visibility-change event.**
When a door/lift opens or closes, mark the ≤2 affected sectors dirty and re-fill
*their* cache entries (a few subsectors' probes), not the level. The trigger
already exists in the engine (the sector-move thinker). This keeps the steady
state a free lookup and spends rays only on the actual change — the same
"rays on the delta only" philosophy as the rest of the design (perf §3 idea 3).

**Recommendation (Q2):** keep the **static sector-keyed bake**; add **event-driven
lazy re-bake of dirtied sectors** on door/lift moves. Do **not** adopt a
continuously-traced dynamic probe field — it costs more and leaks. SH-L1 vs
per-vertex storage stays the §9 measurement item (decided by bake size/quality on
E1M1 + a large WAD), independent of this staleness question.

---

## 3. Q3 — External correctness check on the current direct-lighting estimator

The roadmap phrased this as checking "NEE + power-importance + **MIS** variance."
**Verified against the shipped shader (`pt_common.glsl`): there is no MIS in the
integrator.** The path is:

- **Direct:** NEE only, **split by emitter kind since DOOM-0084**
  (`pt_common.glsl:186–228`). The MANY static wall/flat emitters `[0, omniStart)`
  are power-importance-sampled by a CDF binary search (`NEE_SAMPLES` shadow rays
  averaged — `NEE_SAMPLES` is the caller's constant in `pathtrace.comp`, passed
  into `shadeSurface` as `nSamples`). The FEW omni sprite emitters
  `[omniStart, emitCount)` are sampled **directly** in a separate loop (pdfSel = 1,
  now contribution-culled by `OMNI_CULL_VALUE = 0.0025`).
- **Indirect:** a single **cosine-weighted hemisphere** diffuse bounce; for
  Lambert, `brdf·cos/pdf` collapses to `albedo` exactly.

MIS (multiple importance sampling) only earns its keep when you *also*
BSDF-sample toward lights and must weight the two strategies so neither
double-counts. A pure-Lambert + NEE-only path **never BSDF-samples a light**, so
**MIS is moot** — there is nothing to combine. (An unused `mis_power_heuristic`
snippet exists in `formulas.glsl` but is not on the live path.)

> **Doc-side follow-up (binding spec is now stale):** spec §4.4 still describes
> the integrator as *"NEE + multiple importance sampling (power heuristic)"* and
> build-step 3 (§7) as *"NEE + MIS + RR"*. The shipped
> integrator has no MIS. The spec should be corrected to "NEE-only (MIS N/A for a
> pure-Lambert path)" — unless BSDF-light sampling is still *planned* for a future
> specular path, in which case the spec should mark MIS as future, not present.
> Surfaced for a maintainer; not edited here (out of this note's scope).

The correctness question therefore reduces to: *is the power-importance NEE
selection unbiased?* INV-6 (spec invariant) cross-checks exactly that in-engine
via two unclamped estimators in `pt_common.glsl`: `directNEEVerify` (a
power-importance CDF pick) vs `directAllLights` (brute-force over every emitter).
**INV-6 soundly proves the power-importance *selection* is unbiased — that is its
purpose and it does it correctly.**

But there is a coverage gap this note must flag: **both verify estimators are
invoked with `omniStart == emitCount`** (`pathtrace.comp:427,429` — both args are
`pc.misc2.x`), so the `k >= omniStart` omni branch inside `sampleEmitter` is
**never taken on the verify path**. INV-6 therefore exercises the static
power-importance selection only; it does **not** validate the production omni
direct-sampling loop (the `[omniStart, emitCount)` branch in `shadeSurface`) at
all. The verify estimator's own header comment (`pt_common.glsl:254–256`,
*"Mirrors shadeSurface's emitter pick"*) is **stale** — it predates the DOOM-0084
static/omni split and no longer describes the shipping selection.

So Q3 is **partly satisfied**: INV-6 validates the static-set power-importance
weighting; the omni direct-sampling path is **unverified**, and the omni
contribution-cull (`OMNI_CULL_VALUE`) is an additional deliberate one-sided bias
INV-6 doesn't cover (the cull drops a shadow ray when the *unshadowed*
contribution is below threshold; since shadowing only shrinks the term, the bias
is one-signed and bounded by threshold × culled-count). Two follow-ups fall out,
both **code-side** (surfaced to the maintainer; this docs note does not edit code):

1. **INV-6 omni-coverage gap (the priority):** extend the verify path to mirror
   the post-DOOM-0084 split (pass the real `omniStart`, so the omni direct-sampling
   loop is checked against `directAllLights` too), and refresh the stale
   `pt_common.glsl:254–256` comment. Until then, do not claim INV-6 proves the
   *whole* direct estimator unbiased — only the static set.
2. **Quantify the omni-cull bias** once against the brute-force reference, to
   record the bound rather than assume it negligible.

---

## 4. Decision and measurement plan

**Headline:** ReSTIR is gated behind two cheaper steps. The 100-light discovery
justifies *resampling* the omni light set, but the cheap end of that family
(REJECT cull + RIS) almost certainly captures the win at a fraction of ReSTIR's
RDNA2 register cost. **Full ReSTIR is only ever reconsidered if RIS still misses
60 fps *and* the RGP capture shows occupancy headroom** — so the primary path is
"build the cheap ladder," and the RGP number is a later, conditional gate, not a
blocker on the whole decision.

Ordered conclusions:

1. **Q1 (ReSTIR DI):** *Conditional defer.* Build the cheap ladder first —
   **REJECT-lump light culling** (free, exact, engine-native) → **RIS without
   reservoirs** (1 shadow ray, no cross-frame state). Re-evaluate ReSTIR DI
   **only** if RIS still leaves the light-heavy room short of 60 fps *and* RGP
   shows occupancy headroom by the §1.3 threshold. → new roadmap items for REJECT
   cull + RIS; ReSTIR DI stays a measure-first research gate.
2. **Q1 (ReSTIR GI):** *Defer* (unchanged). The indirect bounce is baked; ReSTIR
   GI would pay RDNA2's worst case to recompute the bake. Revisit only if the
   bake proves inadequate for dynamic indirect (separate question).
3. **Q2 (probes):** *Decided.* Keep the static sector-keyed bake; add
   **event-driven lazy re-bake** of door/lift-dirtied sectors. No continuous
   dynamic field (costs more, leaks through walls). Storage granularity stays the
   §9 measurement item.
4. **Q3 (correctness):** *Partly satisfied.* INV-6 proves the **static**
   power-importance selection unbiased; MIS is N/A (pure-diffuse NEE-only). Two
   code-side follow-ups: (a) close the INV-6 **omni**-coverage gap (verify path
   runs with `omniStart == emitCount`, so the omni loop is unchecked); (b)
   quantify the omni-cull's one-sided bias. Plus a doc-side follow-up: correct the
   spec's stale "NEE + MIS" wording (§3).

**If the RGP capture is never run, the decision still resolves:** build the cheap
ladder (REJECT cull → RIS); full ReSTIR simply never opens. The capture matters
*only* if RIS proves insufficient — it is not a prerequisite for any of the work
that is actually recommended next.

**Blocked-on-measurement (only conclusion 1's *full-ReSTIR* branch):**

- **RGP occupancy capture of the megakernel** (= DOOM-0090's occupancy pass).
  Gives the current megakernel VGPR count + occupancy (waves/SIMD) judged by the
  §1.3 rule. User-run on the RX 6600; steps in
  `docs/research/DOOM-0090-rgp-capture-guide.md`.

### Key sources

- ReSTIR DI (Bitterli et al., 2020), NVIDIA/Dartmouth —
  <https://research.nvidia.com/labs/rtr/publication/bitterli2020spatiotemporal/>
- Rearchitecting ReSTIR for production (Wyman & Panteleev, 2021) —
  <https://research.nvidia.com/labs/rtr/publication/wyman2021rearchitecting/>
- A Gentle Introduction to ReSTIR (Wyman, SIGGRAPH 2023 course notes) —
  <https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf>
- A gentler introduction to ReSTIR (Interplay of Light, 2023) —
  <https://interplayoflight.wordpress.com/2023/12/17/a-gentler-introduction-to-restir/>
- Spatiotemporal reservoir resampling overview (Wikipedia) —
  <https://en.wikipedia.org/wiki/Spatiotemporal_reservoir_resampling>
- RDNA2 RT throughput / traversal-is-compute (Chips and Cheese) —
  <https://chipsandcheese.com/p/raytracing-on-amds-rdna-2-3-and-nvidias-turing-and-pascal>
- GPUOpen occupancy explained —
  <https://gpuopen.com/learn/occupancy-explained/>
- Internal: `docs/research/DOOM-0009-performance.md` §2 items 3/4/7/9, §3 ideas
  1/2/3; `docs/specs/DOOM-0009-path-tracer.md` §4.1/§4.3/§4.4/§7/§8/§9, INV-6;
  `linuxdoom-1.10/shaders/pt_common.glsl`, `pathtrace.comp`;
  `docs/research/DOOM-0090-rgp-capture-guide.md`.

---

### Cold-eyes loop log

- **Loop 1 (2026-06-29, 2 lanes)** — 0 CRITICAL; HIGH: dangling `§2.x/§3.x`
  anchors into the perf doc (it uses flat lists), DDGI-leak miscited to §4.4
  (is §4.3), ReSTIR-GI deferral miscited to §9 (is §8); the Q3 "INV-6 satisfied"
  claim over-stated (verify estimator predates the DOOM-0084 split). All verified
  + fixed.
- **Loop 2 (2026-06-29, 2 lanes)** — 1 CRITICAL: the 80–110 ms / 8–9 fps figure
  was mislabeled "@ 50%" — ROADMAP's validated note pins it at **100%** (a
  scale-conflation, the exact error the perf-discipline rule forbids). HIGH:
  ReSTIR-DI estimate ratio'd a 1080p cost against the 50% megakernel budget; spec
  §4.4/step-3 still say "NEE + MIS" (now stale) — surfaced. MEDIUM: mid-band VGPR
  result had no resolution rule; "~100 lights" premise needed a post-cull
  clarification. All verified + fixed (scales separated per-figure; DI estimate
  compared at matched 1080p; mid-band → RIS-only; spec-MIS surfaced as a
  follow-up).
- **Loop 3 (2026-06-29, 2 lanes)** — 0 CRITICAL, 0 structural. Both lanes
  independently caught the same residual: the @50% line credited the cull alone
  with 37–46 fps / ~15 ms, but that needed a *second* optimization (the a-trous
  trim; pure cull was 35–42 fps / ~14 ms). Also: the coverage-gap source was
  miscited to `DOOM-0009-performance.md` §4 (it is the denoiser-bestpractices doc
  §4); Q2's "dynamic probe field" should name DDGI to match the roadmap; the DI
  estimate + spec-MIS line-numbers wanted inline scale / durable §-anchors. All
  verified + fixed. Remaining items were verified-polish only.
- **Loop 4 (2026-06-29, confirmation pass)** — 0 CRITICAL, 0 HIGH, 0 structural.
  Every load-bearing claim re-verified against source (scale attribution vs
  ROADMAP 844/846; all §-citations; the `omniStart == emitCount` Q3 finding;
  decision soundness). One MEDIUM: the borrowed throughput ratio read "~25–30%"
  vs perf §2 item 4's "~28–30%" — fixed (and the DI estimate retightened to
  ~30–32 ms). Converged: no structural or decision-changing findings remain.
