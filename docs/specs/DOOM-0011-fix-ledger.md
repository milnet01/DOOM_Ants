# DOOM-0011 — fix ledger

**Purpose.** A running record of every fix applied to the DOOM-0011 docs during review,
and — the part that earns its keep — **what each fix makes stale elsewhere**. Two of loop
5's three CRITICALs were defects in loop 4's *own* fixes, and a later sweep found six more
edits that had silently failed to apply plus four places where one fix contradicted
another. A fix is not done when it is written; it is done when everything that depended on
the old text has been chased down.

**How to use it.** Before closing a fix, fill the *Ripples* column: every other section,
document, constant, count or threshold that referenced the thing you changed. Then grep for
each and confirm. An empty Ripples cell means "I checked and there are none", not "I did not
look" — write `none (checked: <what you grepped>)`.

**Standing ripple-check greps** (run all of these after any fix batch):

```bash
S=docs/specs/DOOM-0011-volumetric-lighting.md
P=docs/specs/DOOM-0011-implementation-plan.md
grep -n "≤ 5 %\|≤5 %"           $S $P   # superseded perf gate
grep -n "60 FPS floor"           $S $P   # relaxed for RT-engaged scenes
grep -n "six\b.*edit\|all six"   $S $P   # menu edit count (now seven)
grep -n "queryCount"             $S $P   # must all say 8 -> 9
grep -n "≤ 4 %\|≤ 8 %\|≤ 15 %"  $S $P   # L1b / L1c / L6 budget split
grep -n "eight invariants\|twelve invariants\|INV-1\.\.8"  $S $P
grep -n "bilateral"              $S $P   # L1 shipped PLAIN bilinear
grep -n "custom-index 2"         $S $P   # sky is detected by the MISS
grep -n "SKY_COLOR"              $S $P   # L1c moves the sky tone to kFogColor
grep -n "f₁\|f₂\|v₁\|v₂"         $S      # renamed to kWispFreq1/2, kWispVel1/2
awk '/Step 6: Apply fog in .svgf_composite/,/Step 7/' $P | grep "misc6\[2\]"   # must be EMPTY:
                                        # svgf_composite.comp has no misc6 -- it gates on misc3.y
grep -n "binding 3\|binding 4\|binding 5" $S $P   # set-0 bindings: noise 3, seep 4, UBO 5
```

**Also verify after every batch:** that each edit actually landed. A batched
exact-string replace that aborts part-way applies *nothing* — grep for a distinctive
phrase from each intended fix rather than trusting the tool's success report.

---

## Loop 4 — 2026-07-26 — commit `d2c0eb4`

| # | Fix | Ripples chased |
|---|-----|----------------|
| 4.1 | 50 `file:line` citations corrected (`r_vulkan.cpp` +4..+6, `pathtrace.comp` +2, three pointing at unrelated constructs) | Cold-eyes log entries citing old numbers left as **historical record** (they document what a past loop did) — deliberate, not drift |
| 4.2 | Seep traversal moved from a subsector graph to a sector graph (vanilla has no minisegs) | §4.3a option-(C) rejection; INV-12; §7 L1d row. **MISSED a ripple → became loop-5 CRITICAL (i)** |
| 4.3 | `d` resolved per grid cell, not per node | §4.3a's own rejection of option (C) — the two must agree |
| 4.4 | Self-referencing sector guard, no-sky level, finite `dMax` sentinel, extent overflow, `CLAMP_TO_EDGE` | §5 field sizing; INV-12. **`dMax` was NOT added to §5's constant inventory → caught in the later sweep** |
| 4.5 | §4.4(a): L2's sky term **replaces** L1's flat ambient | §4.3a's `kIndoorFogScale > 0` rationale; L3's torch-shaft justification; §7 L2 row |
| 4.6 | `kSunDir` is already declared, not new | INV-3 (rewritten to a real, falsifiable invariant); §5 inventory |
| 4.7 | L1 shipped a plain bilinear, not a bilateral upsample | §4.6 sky-seam "fallback" (was a no-op); §7 L1 row; Q6; **plan file table — MISSED, caught in the sweep** |
| 4.8 | §4.5 pointer §4.3a → §4.3b; `kAreaDensity` → `areaDensity(profile)` | §4.3b's `σ_final`; §5 inventory; the Clear-profile table row |
| 4.9 | Menu edits six → seven (missing forward declaration) | **plan Step 2, plan self-review — MISSED, caught in the sweep** |
| 4.10 | `queryCount` pinned to exactly 9 + both resets + readback | §6; **§7 L6 row still said "past 8" — MISSED, caught in the sweep** |
| 4.11 | L6 gate measured at High, not the shipped default | Q10; §7 L6 row |
| 4.12 | L1b spot-check cut from ≤ 15 % to its own ≤ 4 % slice | §7 L1b row; **§6 and Q13 — MISSED, caught in the sweep** |
| 4.13 | L1c's Verify column no longer binds 15 % against its own 8 % | §6 item 2; Q18 |
| 4.14 | L5 added to the budget reservation; L1d given a runtime perf item | §6; §7 gate column |
| 4.15 | Plan: status banner, ≤ 5 % → ≤ 15 %, 60 FPS floor, `kFogBaseDensity` 0.015 → 0.0008, `σ` split note, custom-index-2 → miss, profiler key, invariant count | **Two ≤ 5 % sites and the self-review survived → loop-5 CRITICAL** |

## Loop 5 — 2026-07-26 — commit `18aef66`

| # | Fix | Ripples chased |
|---|-----|----------------|
| 5.1 | Seep graph nodes: sectors → **portals** (a sector-indexed Dijkstra settles one distance per sector — the per-node value step 3 forbids) | §4.3a step 3; INV-12; memory note. Supersedes 4.2 |
| 5.2 | One-cell void padding ring so "border cells are `dMax`" is *true*, not asserted | §5 field sizing (`256×256` budget); Q19 |
| 5.3 | Plan L4: explicit `σ` split step + by-construction check (folding into `fogDensity()` does **not** discharge INV-9) | L1b's standing note; INV-9; §4.3b `σ_final` |
| 5.4 | Plan perf gate ≤ 5 % → ≤ 15 % at the two sites that decide pass/fail | plan heading; self-review; banner's staleness claim |
| 5.5 | Plan invariant coverage restated as INV-1..8 only | banner; the L1c/L1d gap |
| 5.6 | Plan L1b: "still holds 60 FPS" → the ≤ 4 % share | §6 2026-07-25 amendment |
| 5.7 | §4.3 `kFogColor` clause tagged as L1c, not shipped | §4.3b; status header |

## Consistency sweep — 2026-07-26 — commit `d925a29`

Triggered by the standing instruction that a fix must not make something else stale.

| # | Fix | Ripples chased |
|---|-----|----------------|
| S.1 | **Six loop-4 edits had never landed** — an exact-string batch aborted part-way and only two items were re-applied. One applied edit already pointed at a note among the missing six, leaving a dangling reference | Re-applied all six; re-grepped each for presence rather than trusting the tool report |
| S.2 | §6 + Q13 still gave L1b the whole-feature 15 % share | fix 4.12's missed ripple |
| S.3 | §7 L6 row still said "grow `queryCount` past 8" | fix 4.10's missed ripple |
| S.4 | §5 inventory used bare `f₁/f₂/v₁/v₂`; omitted `kWispOffset2` and `dMax` | fixes 4.4 + the octave-naming edit |
| S.5 | Plan: "six menu edits" ×3, bilateral in the file table, "every spec section maps to a task" | fixes 4.7 + 4.9's missed ripples |
| S.6 | Plan citations `svgf_composite.comp:58-72` → `:93`; `r_vulkan.cpp:7545` → `:7561-7568` | verified against source |

## Loop 6 — 2026-07-26 — 2 Sonnet lanes, citations out of scope (~304k)

Tally: **CRITICAL 2 · HIGH 5 · MEDIUM 3 · LOW 2 · INFO 0** — verified 12 / unverified 0, plus one
found by the orchestrator while verifying and two found by the ripple greps. **15 fixes.**

**The headline lesson of this loop:** *four* of the seven CRITICAL/HIGH findings were defects in
**loop 5's own fixes** — specifically in fix 5.3, the `σ`-split block written into plan L4. That
block introduced `gooMult` (never defined), kept a now-dead `densMul`, referenced `wisp` (an L1c
term with no task), and told the implementer to add `kGooDensityMul` (a constant the spec does not
name). Loop 4 → loop 5 had the same shape. **A fix that writes new code into a doc must be read as
new code — against the surrounding block and against the spec's own constant inventory.**

| # | Fix | Ripples chased |
|---|-----|----------------|
| 6.1 | §7 L1c row: the "≥ 7 % reserved" figure never subtracted L1d's own ≤ 1 % seep tap, though L1d draws on the same cumulative 15 % pool | §7 L1d row; §6 budget prose. L1d now named in the reservation; L2–L5's share restated as ≥ 6 % |
| 6.2 | INV-10 + INV-11 given explicit `*Falsifiable:*` clauses (they were the only two amended invariants without one) | §7 L1c acceptance row supplies both falsifiers — checked they exist before citing them |
| 6.3 | INV-4 + INV-5 likewise, so all twelve now carry the same tag | none (checked: INV-1,2,3,6,7,8,9,12 already had one inline or tagged) |
| 6.4 | INV-12 now states the two conditions that make it **true** — the `frontsector != backsector` exclusion and the one-cell void padding ring. Both lived only in §4.3a prose, so an editor reading INV-12 alone could drop either and silently falsify it | §4.3a (already says both — confirmed consistent, not duplicated wording) |
| 6.5 | §4.5 stated "profiles compose" but gave **no rule for combining `mediumTint`** — densities add, tints were undefined. Now: tints **multiply**, with the darkening caveat and the tuning dial named | plan L4's `mediumTint *= kHellTint` (already multiplicative — the spec was the side that was silent) |
| 6.6 | `v₁`/`v₂` example speeds were ‖8.60‖ vs ‖8.62‖ while the text claimed they differ in speed | **§5 inventory still held the old `(−5, 7, 0.5)` — caught by the ripple grep, not by either lane** |
| 6.7 | Plan L1's `marchFog` snippet omitted `strength` entirely, while L1b quotes the shipped line as `fogDensity(p) * strength` | Verified against shipped `pathtrace.comp:793`/`:807` — L1b was right, L1's snippet was incomplete |
| 6.8 | Plan L2: snippet calls `sunRayMissesGeometry()`, prose defined `sunRayReachesSky()`; the defining sentence was also a half-applied edit ("the closest hit is the sky instance the ray **misses** all solid geometry") | **plan `:964` still listed the old name — caught by the ripple grep** |
| 6.9 | Plan L2 uses `kFogColor`, which L1c declares and this plan has no L1c task for | Dependency called out inline; the banner already covers the general case |
| 6.10 | Plan L4 (**fix 5.3's own block**): `densMul` set-and-never-read; `gooMult` undefined; `kGooDensityMul` invented | Replaced with `areaMult` matching spec §4.5's table; closing bullet now names `kAreaDensity` |
| 6.11 | Plan L4: `wisp` and `pool` used with no stated provenance | Both annotated in-line; `wisp` flagged as a literal `1.0` placeholder until L1c exists |
| 6.12 | Plan L4 bullet said `Ls += skyRadiance() * …` — a function that appears nowhere and contradicts L2's `kFogColor * …` (orchestrator-found while verifying 6.10) | none (checked: `skyRadiance` has no other occurrence) |

**Standing greps re-run after this batch:** `≤ 5 %` survives only in explicitly historical framings
("raised from", "is superseded", cold-eyes log entries); `60 FPS floor` only where it says it no
longer binds; `queryCount` uniformly 8 → 9; `bilateral` only as L5's future work; `custom-index 2`
only on the **primary** ray, which genuinely can hit the sky instance. All 18 edits re-grepped for
presence individually.

## L1c + L1d authored — 2026-07-26 — the plan's largest gap closed

Not a review loop: the two missing tasks were written from spec §4.3a/§4.3b + §7. Logged here
because writing them made a dozen other statements stale — the exact failure mode this ledger
exists for.

| # | Change | Ripples chased |
|---|--------|----------------|
| A.1 | **Task L1c** written (8 steps): the 2026-07-25 `const`s, the noise volume + **set-0 plumbing**, `wisp()` in `pathtrace.comp` (never `pt_common.glsl` — INV-6), the `SKY_COLOR` → `kFogColor` swap at **all three** sites, the half-res/full-res spot-check, INV-11's `kWispAmp = 0` check | ⚠ banner; file table; L2's `kFogColor` dependency note; L4's `wisp` note; self-review coverage + invariant claims; spec status header |
| A.2 | **Task L1d** written (8 steps): portal graph + Dijkstra + per-cell resolve in `r_mesh.c`, the three degenerate cases, `R16F` + `CLAMP_TO_EDGE` + transform UBO, the graded indoor branch, both budgets | same set, plus a new `r_mesh.c` row in the file table |
| A.3 | Set-0 plumbing (pool sizes + `PARTIALLY_BOUND`) is needed by **both** tasks | Stated once in L1c with an explicit "whichever lands first pays for it" note in both, so it is not built twice or skipped by both |
| A.4 | Verified the two anchors the spec left implicit rather than assuming them: `r_mesh.c` already calls `R_PointInSubsector` (`:692`) and has `skyflatnum` (`:36`), but does **not** include `p_local.h` for `P_LineOpening`; per-level hook is `RB_Vulkan_BuildLevel` (`r_vulkan.cpp:~7169`) | Both named in L1d's Files/Existing-code blocks |
| A.5 | **L6 Step 2 contradicted spec §5 on the name table** — plan said `static const char *fogNames[]`, spec pins the fixed 2-D `char fogNames[4][6]` matching `detileNames`. Found while chasing the menu-edit count | The "seven edits" list also only had six numbered items; the forward declaration is now item 7 |
| A.6 | **Spec §4.3a's topic sentence still said "The graph is over SECTORS, not subsectors"** — directly contradicting step 1's "Nodes = portals, not sectors" (loop-5 fix 5.1 changed the conclusion but left the heading) | Reworded to name portals as the nodes and sectors as the source of adjacency |
| A.7 | **Spec §5's topic sentence still said the images "need their OWN descriptor set"** — contradicting its own resolution (put them on set 0; "keeps the set count at 4") and the note that L1 already appended to set 2. Same stale-headline shape as A.6 | Reworded to "neither BINDLESS set … so they go on a FIXED set" |

**Lesson to carry:** A.6 and A.7 are the same defect twice — **a fix changed a passage's
conclusion and left its topic sentence asserting the old one.** Neither cold-eyes lane caught
either. When a fix reverses a decision, grep the passage's *opening* line, not just the part you
edited.

---

## Loop 7 — 2026-07-26 — 2 Sonnet lanes + a standing-grep pre-pass (~360k)

`CRITICAL 2 · HIGH 2 · MEDIUM 4 · LOW 4 · INFO 0` — **12 verified, 0 dismissed.** The first loop to
read the L1c/L1d tasks, which were authored after loop 6 and had never been reviewed. Lane A read
those two tasks against §4.3a/§4.3b/§5/§7; lane B read both documents for whole-doc coherence.

| # | Severity | Finding | Fix | Ripples chased |
|---|----------|---------|-----|----------------|
| 7.1 | CRITICAL | **L1d Step 4 used `uSeepField` and `worldToSeepUV()` — both undeclared.** No binding, no UBO layout, no transform formula anywhere in either document. Repo-wide search: one use site, zero definitions. The task stopped at its first line of shader code. | Step 3 now fixes the exact `layout(set=0, binding=4/5)` declarations, the `SeepXform` UBO struct in both GLSL and C++ field order, and `worldToSeepUV()`'s formula | Spec §5's binding list; the "origin is the PADDED grid's texel-0 centre" note (off by one cell ⇒ INV-12's padding ring silently breaks) |
| 7.2 | CRITICAL | **The plan's `svgf_composite.comp` snippets gated fog on `pc.misc6[2]`** — a field that shader's 120-byte `SvgfPC` does not have. The plan's OWN Global Constraints say to use `misc3.y`, and the shipped code does. L5 edits this exact block. | Both snippets moved to `pc.misc3.y`, plus an explicit "never `misc6.z` here" note above them | Verified against shipped `svgf_composite.comp` (`pc.misc3.y` at both sites); L5's task text checked; new standing grep added |
| 7.3 | HIGH | Plan banner claimed the amendment **"has run 4 loops"** two bullets after saying the tasks were authored *after loop 6*; spec said 6 | Banner now says 7 loops, not converged, cap passed | Spec status header + log |
| 7.4 | HIGH | **Self-review claimed `sunRayMissesGeometry`/`emitterCentroid`/`emitterLe` were pre-existing engine interfaces** — the plan's own L2 Step 1 and L3 Step 2 author all three | Reworded: the *patterns* exist, the three helpers are new code | none (checked: no other passage repeats the claim) |
| 7.5 | MEDIUM | Spec header said `(log below)` but the log had **no Loop 6 entry** — it lived only in this ledger | Loop 6 and Loop 7 entries added to the spec's own log | Header loop count |
| 7.6 | MEDIUM | **L1c's `uNoiseVol` was never declared** and Step 2 never named a binding index | Binding **3** pinned (verified: `CreateRtComputePipeline` declares `binds[3]`, bindings 0–2), declaration added to the Step-3 block | Spec §5 binding list; L1d continues at 4/5 |
| 7.7 | MEDIUM | **The octave-2 wisp tap was missing its `/64.0`** while the prose below said both taps need it. Silently produces the exact 512-unit-tiling failure §4.3b exists to prevent | Whole argument (frequency **and** `kWispOffset2`) now divided, per §4.3b's definition of `u`; prose rewritten | none (checked: octave-1 tap already correct) |
| 7.8 | MEDIUM | **L1c Step 6 says to read "L1b's recorded Δ" — which no task ever records.** L1b Step 6 ended without writing it down | L1b Step 6 now requires writing Δ into spec §6 + this ledger before closing | The Open section below — still open until L1b is actually re-measured |
| 7.9 | LOW | Spec §5's inventory claims to make L2–L4 buildable but omits `kFogFloorFallback`, `kTorchFalloff` (both L3) and `kFogDepthSigma` (L5) | All three added | none |
| 7.10 | LOW | **`occluded()` takes 4 args** (`hitP, n, wi, dist`); L1b's sketch passed 3 | Call and prose corrected, with a note on what to pass for `n` at a volume sample (no surface ⇒ no normal) | none |
| 7.11 | LOW | `PARTIALLY_BOUND` required but no precedent cited, breaking the plan's own citation discipline | Points at the working `VkDescriptorSetLayoutBindingFlagsCreateInfo bfci` block in the set-1 bindless layout | none |
| 7.12 | LOW | *(orchestrator standing greps, not either lane)* Self-review mapped §8 as **`INV-1..8`** one line above the bullet claiming all twelve | Mapping now `INV-1..12`, split across Global Constraints and per-task guards | none |

**Ripple caught after the batch** (standing greps again, not a lane): spec §5 still said the UBO
"rides the same **new** descriptor set as the two images" — contradicting the settled decision to
use the existing set 0, and the same stale-topic-sentence family as A.6/A.7. Rewritten to name set 0
and pin bindings 3/4/5. Also de-tensed loop 4's log entry, which still called L1c/L1d "unwritten".

**Lesson to carry (fourth loop running):** *the worst findings are defects in the previous batch's
own new text.* Loop 5 broke loop 4's fixes; loop 6 broke loop 5's; loop 7's two CRITICALs were both
in the L1c/L1d tasks written immediately after loop 6. **Newly-written GLSL in a doc must be read as
code** — every symbol it names either declared in the block, listed in §5, or proven to exist. Both
CRITICALs here were undeclared identifiers, which a compiler would have caught in one second and
two cold readers took two loops to notice.

---

## Open — not yet fixed

- **Cold-eyes has not converged.** Loop 7 returned 2 CRITICALs and 2 HIGHs, all substantive, so
  loop 8 is owed. The `--max-loops` cap of 5 was passed at loop 6 — each further loop is an
  explicit user call, not an automatic re-run. Trend: 15C+24H → 3C+2H → 2C+5H → 2C+2H.
  **Judgement for whoever picks this up:** loop 7's findings were concentrated in the newly-written
  L1c/L1d text, which has now been fixed and re-swept. A loop 8 that reads the *fixed* L1c/L1d is
  worth running before implementation; a loop 8 over L1/L1b/L2–L6 is likely low yield — those have
  been read cold five times.
- ~~The plan has no L1c and no L1d task.~~ **Written 2026-07-26**, and **first cold-read at loop 7**
  (2 CRITICALs, both undeclared shader identifiers, both fixed).
- **L1b's measured Δ is still unrecorded**, so L1c's `8 % − Δ(L1b)` allowance cannot be
  computed yet (§6).
