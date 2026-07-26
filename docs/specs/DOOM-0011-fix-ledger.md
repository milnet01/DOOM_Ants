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
grep -n "depth-guided\|depth guide"  $S $P   # must be EMPTY: L5 guides on WORLD POSITION;
                                        # gpos.w is a material id, there is no depth buffer
grep -n "\* wisp \*"              $P   # must be EMPTY: wisp is a FUNCTION -- wisp(p, t_s)
grep -n "fogHeightPool"          $P   # L3 defines it, L4 calls it; `pool` is not a local there
grep -n "\bFLAG_OUTDOOR\b"        $P   # must be EMPTY: one bit, one name -- RB_MESH_OUTDOOR
grep -n "h\.matFlags & .*LIQUID"  $P   # must be EMPTY: liquid rides MatCtrl -> FogHit.ctrlFlags
grep -n "float haze = view\."     $P   # must be EMPTY: RecordRtTrace has no `view`, use g.lastView
grep -n "skyExists"               $P   # must show a DECLARATION, not just the `if`
grep -n "gpos\[cur\]"             $P   # must be EMPTY: `cur` is local to main(); use pc.misc.x
grep -n "kFogDepthSigma.*pt_common" $P # must be EMPTY (bar the warning): it lives in
                                        # svgf_composite.comp, which never includes pt_common.glsl
```

**Better than any grep: compile the snippet.** Loop 10's code lane lifted the real
`svgf_composite.comp`, pasted in the doc's proposed function, and ran `glslangValidator -V`. It
found in seconds two defects that nine cold reads had missed, and it cleared five other tasks
positively rather than by absence of suspicion. **Reconstruct-and-compile is the check this
document's code blocks actually need**; treat the greps as the cheap pre-filter.

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

## Loop 8 — 2026-07-26

**Tally:** CRITICAL 2 · HIGH 0 · MEDIUM 4 · LOW 5 · INFO 0 — **11 verified, 0 dismissed.**
Two Sonnet lanes (~385k) plus the orchestrator's standing-grep pre-pass, which came back clean
this time. First loop to carry a **second mandate**: the user asked for the docs to be made
plainer and shorter, so both lanes reported prose findings alongside correctness ones.

**The spec lane returned zero correctness findings** — the first clean correctness read in eight
loops. Every symbol, struct field, constant, binding and derivation it checked matched the tree.
All six of its findings were prose. **Both CRITICALs came from the plan lane**, and both were the
same defect class as loops 5, 6 and 7: shader code written into the doc that could not compile.

| # | Sev | Fix | Ripples chased |
|---|-----|-----|----------------|
| 8.1 | CRITICAL | **L4's `sigma` line used `pool` and `wisp` as bare values.** `pool` was a local inside `fogDensity()`'s body — invisible to any caller, and L4 stops calling `fogDensity()` anyway; `wisp` is a *function* (L1c Step 3), so the bare identifier is not a value. Split `fogHeightPool()` out of `fogDensity()` at L3 Step 1, and L4 now calls both: `fogHeightPool(p, floorZ)` and `wisp(p, t_s)` | L3 Step 1 code block rewritten (the split); a note added there explaining why the helper exists, so a later editor does not inline it back; a note at L4 that `fogDensity()` loses its last caller and should be deleted in the same commit; confirmed `floorZ` (L3) and `t_s` (L1c) are both already local to `marchFog()`'s loop, so nothing new is plumbed |
| 8.2 | CRITICAL | **L5's "depth-guided bilateral" had no depth on either side.** It weighted by `gp.w`, which is the primary hit's **material id** (`uint id = uint(gp.w + 0.5);` in the shipped `svgf_composite.comp`), and no earlier task ever stored a depth for the four half-res fog neighbours. Rewritten as a **position-guided** filter: `gpos.xyz` already holds the hit's world position, and each neighbour's own full-res gbuffer texel supplies its comparison point — no new image, no new push lane, no current-camera constant | Every "depth-guided" mention retitled across both docs (task heading, goal, Files, Interfaces, commit message, the file-map table, L1's forward reference, spec §4.6, Q6, Q18, the §7 L1 row, the self-review's §4.6 mapping); `kFogDepthSigma`'s spec §5 entry now states its **units** (world units between two hit positions, not a depth ratio); the sky fallback reworded from "no depth guide" to "unguided"; a new standing grep added above so `depth-guided` can never creep back |
| 8.3 | MEDIUM | **The spec's 132-line cold-eyes log** sat between the reader and §1, duplicating a document that exists for exactly this content. Moved **verbatim** into this ledger as an appendix; the spec keeps a three-row summary table and a pointer | Spec status header ("log below" → the ledger); the plan's banner ("Its log lives in the spec's header" → the ledger); loop count 7 → 8 in both; § Contents never listed the log, so no change there. Spec: 1599 → 1491 lines |
| 8.4 | MEDIUM | **§7's L1b and L1c "Verify" cells were paragraph-length**, defeating the point of a table. Both cut to a one-line summary, with the full checklists moved to a new **"Acceptance detail — L1b and L1c"** subsection below the table | Every criterion preserved as a bullet, none dropped; the L1c gate bullet now points at §6's new budget table instead of re-deriving the arithmetic (see 8.5); INV-11 named explicitly on the `kWispAmp = 0` bullet, which the prose version left implicit |
| 8.5 | MEDIUM | **The layered perf budget existed only as prose** — five percentages and two opposite senses of "≤/≥" spread across non-adjacent paragraphs, with "L2–L5 share ≥ 6 %" easy to misread as a ceiling when it is a floor. Added a five-row budget table at the top of §6, with a plain-English note that the L2–L5 row is a promise *to* those layers | The §7 L1c gate bullet now defers to it; the prose derivations below are left intact as the working, with the table declared the version to check against |
| 8.6 | MEDIUM | **The same "RT-only, `rb_fog`-gated, fog-off byte-identical" clause opened L1b, L1c and L1d** near word-for-word, when Global Constraints already states it | All three shortened to one clause pointing at Global Constraints; INV-7/8 still named at each site so the tasks stay self-checking |
| 8.7 | LOW | "Jensen's inequality" invoked with no gloss for a reader who is not a graphics programmer | Replaced with the plain statement ("averaging *through* a curve is not the same as taking the curve of the average"). The transmittance formula itself is load-bearing and stays |
| 8.8 | LOW | The seg-flood-fill rationale was one run-on sentence carrying two independent reasons | Split into "for two reasons — First… Second… Either way…". No claim changed |
| 8.9 | LOW | §9 "Alternatives considered" omitted the two rejected exposure methods argued inline in §4.3a, so it was not the complete index its title promises | A lead paragraph now indexes them and points at §4.3a, where the reasoning stays |
| 8.10 | LOW | L1b Step 5 re-printed the three build commands verbatim; every other task references "L1 Step 7 commands" | L1b now references them too. The one detail L1b's copy carried and L1's lacked (that the SPIR-V compile is what catches GLSL errors) was **moved up into L1 Step 7**, not dropped, plus a line making it explicit that every later task means those same three commands |
| 8.11 | LOW | L1c's `/64.0` explainer packed a claim, its reason, its consequence and an aside into one 45-word sentence | Split into four short sentences, with the "fold `1/N` into the consts instead" alternative moved to its own trailing paragraph. Precision kept — this one documents a bug that shipped wrong once |

**Post-batch ripple sweep:** all standing greps re-run plus the three new ones. `depth-guided`
returns empty across both docs; `fogHeightPool` appears in exactly the four expected places
(L3 definition, L3 wrapper, L4 call, L4 orphan note); no bare `* wisp *` remains; no `7 loops`
leftovers. The moved log left no dangling reference.

**Lesson.** The prose mandate paid for itself in a way worth recording: the spec lane found
**zero** correctness defects and six real readability ones, while the plan lane found two
unbuildable code blocks and almost nothing else. The two documents have different failure modes —
the spec rots into *density*, the plan rots into *code that does not compile*. Reviewing both
with one checklist under-serves each. Loop 9, if run, should keep the split.

---

## Loop 9 — 2026-07-26

**Tally:** CRITICAL 3 · HIGH 3 · MEDIUM 2 · LOW 3 · INFO 1 — **11 verified, 0 dismissed** (+1 INFO
noted, not actioned). Two Sonnet lanes (~465k), split by **failure mode** rather than by document,
which loop 8's lesson predicted would pay: one lane did nothing but resolve every identifier in
every code block against the real source; the other checked whole-doc coherence after loop 8's
restructuring.

**The split worked.** The code lane found all three CRITICALs and confirmed L1, L1b, L1c, L1d and
L6 otherwise resolve cleanly — the first time any loop has been able to say that. The coherence
lane found two spec-vs-plan divergences that eight prior loops had read past, and confirmed loop
8's restructuring broke no seams.

| # | Sev | Fix | Ripples chased |
|---|-----|-----|----------------|
| 9.1 | CRITICAL | **L2's sky term gated on `skyExists`, which is declared nowhere.** The plan described the test in prose (`misc4.w != 0xFFFFFFFF`) but never assigned it. Added the declaration | Confirmed the sentinel's real form against `skyPanorama()`; the comment that used to carry the expression is now redundant and was dropped |
| 9.2 | CRITICAL | **L4's goo test read the wrong flags word — the feature was dead by construction.** It tested `h.matFlags` for `RB_FLAG_LIQUID_NUKAGE`, but `FogHit.matFlags` is filled from the per-**vertex** flags word at both call sites, whose live bits are `FLAG_FLAT`/`FLAG_MASKED`/`FLAG_EMISSIVE`…; the liquid bit is a **`MatCtrl.flags`** bit read via `isNukage(mc)`. No goo room would ever have rendered green, and nothing would have failed to compile. L4 now widens `FogHit` with a `ctrlFlags` field, filled from `mc.flags` at both sites | L4's **Interfaces/Consumes** line still said "`FogHit.matFlags` (now read for goo)" — caught by the post-batch sweep, not by either lane, and rewritten; "Produces" now names the widened struct; verified `mc` is in scope at both `FogHit` constructions; checked spec §4.5 and §7's L4 row, which describe the mechanism without naming the field, so neither needed changing |
| 9.3 | CRITICAL | **L4's `misc6.w` write used `view.hazeDensity`, and there is no `view` in that function.** `RecordRtTrace()` takes no `rb_view_t` parameter; every other per-frame field it reads comes off the cached global `g.lastView`. Corrected, with the reason inline so it is not "fixed" back | The `r_backend.c` sites that legitimately *do* have a local `view` were checked and left alone — only the `r_vulkan.cpp` write was wrong; the Files line describing that same edit was tightened to `g.lastView` too |
| 9.4 | HIGH | **The profiler widening named 3 sites; there are 7.** Missed: `uint64_t ts[8]` (a fixed stack array — `vkGetQueryPoolResults` would write 72 bytes into 64, silently), the `nq = g.profRasterFrame ? 6u : 8u` ternary's non-raster branch, `double profMs[8]` (all 8 slots already assigned, so there is no free slot for fog), and the `pi < 8` reset loop. Replaced the prose with a seven-row table | The `queryCount 8 → 9` standing grep still passes; added "grep `\b8\b` around the profiler block before declaring this done" — the array sizes compile silently, which is what makes them dangerous |
| 9.5 | HIGH | **One bit, two names.** The spec says the C-side bit and its shader mirror share one identifier ("one name, not two"); the plan then declared `RB_MESH_OUTDOOR` in C and `FLAG_OUTDOOR` in GLSL | Both shader sites renamed plus the Files line; added a standing grep so `FLAG_OUTDOOR` cannot come back; the `r_mesh.h` row of the file table, which omitted L1b entirely although the `#define` lives there, now lists it |
| 9.6 | HIGH | **L3's torch loop contradicted its own heading.** Task title, spec §4.4(b), §7's L3 row and Q2 all say *nearest-few*; the code looped over every static emitter, with nearest-few demoted to an optional tweak. Rewritten as the spec's two-pass form: a cheap distance scan keeping the nearest 4, then the expensive phase evaluation for those 4 only | **This one is not fully closed and says so.** The two-pass form cuts phase evals from `steps × omniStart` to `steps × 4`, but pass 1 is *still* `steps × omniStart` distance tests — thousands per pixel at ~40 steps. Rather than silently invent a design, logged **Q23** naming per-ray selection as the fallback and requiring pass 1 be measured alone at L3. Reverting to all-emitters is explicitly ruled out as strictly worse |
| 9.7 | MEDIUM | **L5 called `fetchFogBilinearPlain`, which no step creates.** The shipped function is `fetchFogBilinear`; the plan meant "keep the old body under a new name" but never said so | The rename instruction now sits in the snippet itself, where an implementer reading only the code block will hit it |
| 9.8 | MEDIUM | **L1c silently took one arm of a fork §4.3b poses, with no remedy if its acceptance check fails.** Added the contingency (a separate `kFogSkyDensity`, *not* another base-density change) and logged **Q24** | §10's question list extended; the plan's pointer and the spec's entry cross-reference each other |
| 9.9 | LOW | L1's pre-loop `skyAmbient` local is left dead by L2's edit | L2 Step 1 now says to delete it in the same edit |
| 9.10 | LOW | The file-structure table credited `r_mesh.c` with the outdoor bit but not `r_mesh.h`, where the `#define` lives | Folded into 9.5 |
| 9.11 | LOW | **§6's budget table — loop 8's own fix.** Its column header read "Its own added cost" while the L2–L5 row holds a *floor*, so the cell could not be read without the footnote beneath it | Header reworded and the row daggered, so the table stands alone |

**INFO, not actioned:** `fogDensity(vec3)` stays compiling-but-unused across the L3→L4 boundary.
L4 already owns deleting it; noting that the cost is real but small and already assigned.

**Post-batch ripple sweep:** all standing greps re-run plus the four new ones. `FLAG_OUTDOOR`,
`RB_FLAG_LIQUID_NUKAGE` in a `h.matFlags` test, `view.hazeDensity` in `r_vulkan.cpp`, and
`depth-guided` all return empty; `skyExists` shows a declaration; Q23/Q24 resolve in both
directions. One ripple (9.2's Interfaces line) was caught only by this sweep — **the third
consecutive batch where the greps caught something neither lane did.**

**Lesson.** Splitting the lanes by *failure mode* rather than by document found strictly more than
loop 8's split by document: three CRITICALs the previous eight loops had read past, all in code
blocks, all in tasks a prose-oriented reviewer had "reviewed" before. Two of the three would have
failed to compile, which is the harmless kind. **9.2 would have compiled and shipped a dead
feature** — green fog that never appears, with nothing to point at. That is the one to fear, and a
reviewer told "resolve every identifier against the source" finds it where one told "review this
document" does not.

---

## Loop 10 — 2026-07-26

**Tally:** CRITICAL 2 · HIGH 0 · MEDIUM 1 · LOW 2 · INFO 0 — **5 verified, 0 dismissed.** Two
Sonnet lanes (~493k), same split by failure mode as loop 9.

**The methodology changed, and it is the headline.** The code lane did not read the snippets — it
**reconstructed them into the real shaders and compiled them with `glslangValidator`**. That found
both CRITICALs immediately, and, just as valuable, let it clear L1c, L1d, L2, L3, L4 and L6
*positively* (they compile) rather than by absence of suspicion. Nine previous loops read these
same blocks and could not do either.

**Both CRITICALs were in L5 — the passage loop 8 rewrote.** Sixth consecutive loop where the worst
findings sat in the previous batch's own new text.

| # | Sev | Fix | Ripples chased |
|---|-----|-----|----------------|
| 10.1 | CRITICAL | **`fetchFog()` read `gpos[cur]`, and `cur` is a local inside `main()`.** The new function sits above `main()` like the one it replaces, so it cannot see it — `glslangValidator` says `'cur' : undeclared identifier`. Changed to `pc.misc.x`, the push-constant `cur` is itself read from, so no call-site threading is needed | Both the prose sentence and the in-snippet comment carried `gpos[cur]`; both fixed. Added an explicit warning **not** to "fix" the compile error by hardcoding `gpos[0]` — that silently reads the wrong half of the double-buffered gbuffer, which is a worse outcome than the error. New standing grep |
| 10.2 | CRITICAL | **`kFogDepthSigma` was to be declared in `pt_common.glsl`, which `svgf_composite.comp` does not include.** That shader includes only `formulas.glsl` and `pbr_neutral_tonemap.glsl` — **the exact limitation spec §4.6a leans on** to justify computing the sky fog in the megakernel instead. The plan walked into the trap its own spec documents two sections earlier. The const now sits in `svgf_composite.comp` with a starting value | Spec §5's constant inventory listed it among the `pt_common.glsl` consts — carved out with the reason, so the two documents cannot drift back. L5's Files line already named `svgf_composite.comp`, so no change there. New standing grep |
| 10.3 | MEDIUM | **L1c is gated on a number no task has produced.** Its ceiling is `8 % − Δ(L1b)`; Δ(L1b) has never been measured, and prose asking for it has now survived several loops unactioned. L1c opens with a **blocking notice** — the task cannot be closed until the number is in spec §6 | This is the ledger's own oldest Open item, now enforced by task structure rather than by prose an implementer can skim past. The measurement itself is hardware work and remains outstanding |
| 10.4 | LOW | L4's sigma block was fenced at function-scope indentation but reads `p` and `skyExposure`, which only exist inside `marchFog`'s per-sample loop | Re-indented to the document's own loop-body convention, with a comment saying so explicitly. Verified the other snippets' indentation already matches their scope |
| 10.5 | LOW | One `§4.4a` citation among five `§4.4(a)`s — and §4.4 has no `4.4a` heading, unlike the real §4.3a/§4.3b | Normalised. Checked both documents for other bare-letter subsection citations; none |

**What the compile pass cleared, positively:** L1c (noise binding 3, `wisp()`, the three
`SKY_COLOR`→`kFogColor` sites), L1d (`SeepXform` UBO, bindings 4/5, `worldToSeepUV()`, and
`P_LineOpening`'s globals), L2 (`sunRayMissesGeometry` against `occluded()`'s real 4-arg
signature, `skyExists` against `pc.misc4[3]`), L3 (the two-pass nearest-4 torch loop with its new
helpers, built from the real 14-float emitter record), L4 (the widened `FogHit`, `mc` in scope at
both call sites), and L6 (all seven hardcoded `8`s confirmed, all eight `profMs` slots confirmed
assigned, the menu symbols confirmed absent). That is a materially stronger statement than any
prior loop could make.

**Post-batch ripple sweep:** every standing grep re-run plus the two new ones. `gpos[cur]` and
`kFogDepthSigma`-in-`pt_common` both return empty; `depth-guided`, `FLAG_OUTDOOR`,
`view.hazeDensity` and the liquid-bit-on-`matFlags` test all still empty. No ledger-only ripple
this time — the first batch in four where the greps found nothing the lanes had missed.

**Lesson.** Ten loops of careful reading found real defects every single time, and the eleventh
check that mattered was not reading at all — it was pasting the code into a compiler. For a
document whose payload is source code, **review converges slowly and compilation converges
immediately.** The remaining risk is now narrow and known: L5's own fixes are the only snippet in
the document that has not been through the compiler.

---

## Open — not yet fixed

- **Cold-eyes has not converged**, but the remaining gap is now *narrow and named* rather than
  open-ended. Trend: 15C+24H → 3C+2H → 2C+5H → 2C+2H → 2C+0H → 3C+3H → **2C+0H**.
  **Judgement for whoever picks this up:** loop 10 compiled reconstructions of L1c, L1d, L2, L3,
  L4 and checked L6 against source — all clean. The only snippet in the document that has **not**
  been through a compiler was **L5's**, because loop 10's two CRITICALs were in L5 and the fixes
  post-dated its compile pass. **That gap is now closed** — the orchestrator reconstructed L5
  Step 1 into the real `svgf_composite.comp` (rename, the new const, `fetchFog()` in full, **both**
  call sites wired) and `glslangValidator -V` returned clean, 2026-07-26. **Every code block in
  the plan has now been through a compiler.** What remains is one cold pass to confirm the loop-10
  fixes read correctly as prose. **Do not commission another prose-only sweep beyond that**: three
  consecutive lanes have found the spec correctness-clean, and the last two rounds of real findings
  came from compilation, not from reading.
- **A shipped source comment is stale.** `svgf_composite.comp`'s comment above `fetchFogBilinear`
  still says the L5 upsample will be "depth-guided". L5 now owns fixing it (noted in the task) —
  flagged rather than edited, because this is a documentation pass and that is engine source.
- **Q23 (torch-emitter selection) is open and blocks nothing yet, but it will shape L3's code.**
  The nearest-few scan may not fit the budget even in its two-pass form; the per-ray fallback is
  named but undecided, and the decision needs a measurement, not a review loop.
- ~~The plan has no L1c and no L1d task.~~ **Written 2026-07-26**, and **first cold-read at loop 7**
  (2 CRITICALs, both undeclared shader identifiers, both fixed).
- **L1b's measured Δ is still unrecorded**, so L1c's `8 % − Δ(L1b)` allowance cannot be
  computed yet (§6).

---

## Appendix — the cold-eyes loop log

Moved here verbatim from `DOOM-0011-volumetric-lighting.md` on 2026-07-26 (loop 8): it is
review history, and this is the review-history document. The spec keeps a summary table and
a pointer to this appendix.

**Cold-eyes log — 2026-07-25 amendment** (rule 14 — looped until convergence; 3 lanes
= amendment-vs-code accuracy + whole-doc coherence + cross-doc drift, each loop cold):
- **Loop 7** — CRITICAL 2 · HIGH 2 · MEDIUM 4 · LOW 4 · INFO 0 (2 lanes + an orchestrator
  standing-grep pre-pass; 12 verified, 0 dismissed). First loop to read the **L1c/L1d tasks**,
  which had never been reviewed. Both CRITICALs were unbuildable code in those new tasks:
  L1d's `uSeepField`/`worldToSeepUV` were used but **never declared** — no binding, no UBO layout,
  no transform formula anywhere in either document — and the plan's `svgf_composite.comp` snippet
  gated fog on `pc.misc6[2]`, a field that shader's 120-byte `SvgfPC` does not have (the shipped
  code correctly uses `misc3.y`; L5 edits that same block). Also: the plan's own self-review
  claimed `sunRayMissesGeometry`/`emitterCentroid`/`emitterLe` were pre-existing interfaces when
  its own steps author all three; `occluded()` takes 4 args, not the 3 the L1b sketch passed; and
  the octave-2 wisp tap was missing its `/64.0`. **The pattern held for a fourth loop: the worst
  findings were defects in the previous batch's own new text.**
- **Loop 6** — CRITICAL 2 · HIGH 5 · MEDIUM 3 · LOW 2 · INFO 0 (2 lanes, citations out of scope,
  ~304k). Four of the seven worst findings were defects inside **loop 5's own `σ`-split fix**:
  an undefined `gooMult`, a set-but-never-read `densMul`, a `wisp` term with no owning task, and
  an invented `kGooDensityMul` the spec never names. Two further ripples were caught only by the
  fix ledger's standing greps, not by either lane.
- **Loop 1** — CRITICAL 1 · HIGH 10 · MEDIUM 15 · LOW 12 · INFO 2. Headline: `skyExposure`
  multiplied `areaMult`, so every roofed room (all goo, all hell interiors, every
  torch-lit room) would have had its fog driven to 0–10 % of base — cancelling the
  coloured-fog feature by construction. Also: the seep's connectivity test was "not
  one-sided", but a **closed DOOM door is two-sided**, so fog would pour through every
  shut door; the profiler is `` \ `` not `` ` ``; `kWispWeight2`'s "SH2 90/128"
  justification was wrong twice over (90 is the Enhanced Edition's *modified* alpha, and
  a 2-D compositing alpha is not an octave weight). Side-effects fixed outside this spec:
  `docs/standards/renderer.md`'s push ledger was stale for **shipped** code, and
  DOOM-0183's spec still claimed `misc6.z/.w` reserved.
- **Loop 2** — CRITICAL 0 · HIGH 6 · MEDIUM 12 · LOW 12 · INFO 0. The wisp drift formula
  put velocity **outside** the frequency scale, drifting the field 512× too fast; L1c's
  spot-check reused the whole-feature gate, so passing L1c would have guaranteed L6
  fails; loop 1's split-density fix rescued goo and hell but **not** the plain roofed
  room, so Q12's `kIndoorFogScale = 0` had to be struck; `kAreaDensity` appeared in every
  formula and was defined nowhere; the sky closed form still in-scattered `SKY_COLOR`,
  which would have hazed the mountains blue against a near-white foreground.
- **Loop 5** — CRITICAL 3 · HIGH 2 · MEDIUM 2 · LOW 0 · INFO 0 (2 lanes, narrowed:
  citations were excluded, having been re-verified against source mechanically). Notably,
  **two of the three CRITICALs were defects in loop 4's own fixes**, which is the argument
  for the cold re-read. (i) Loop 4 moved the seep graph to **sectors** to dodge the
  miniseg problem, but a sector-indexed Dijkstra settles exactly **one** distance per
  sector — the per-node value the very next step declares "would defeat the whole
  feature". The search state has to be the **portal**; rewritten that way. (ii) The new
  "border cells are `dMax` by construction" was asserted rather than made true — a level
  whose outdoor sector runs flush to its bounding box has `d = 0` at the true edge texel,
  which `CLAMP_TO_EDGE` would then project *outward*, inverting the guarantee the sentence
  exists to give; fixed with an explicit one-cell void padding rule. (iii) In the plan,
  **L4 was charged with replacing the `σ = … × skyExposure` form and never did** — its
  edit folds profile density into `fogDensity()`, whose result is still multiplied by
  `skyExposure`, so every roofed goo/hell room would have kept ~5 % of its intended
  density while the play-test passed weakly on a thin green tint. Also: the plan's perf
  gate was still `≤ 5 %` in the two places that actually decide pass/fail (heading said
  15 %, criterion said 5 %), and loop 4's own "all twelve invariants are pinned" claim was
  false — only INV-1..8 were, since INV-9..12 belong to the L1c/L1d tasks, unwritten at the
  time (written 2026-07-26; all twelve are pinned now).
- **Loop 4** — CRITICAL 15 · HIGH 24 · MEDIUM 34 · LOW 34 · INFO 8 (6 lanes; 3 findings
  dismissed unverified). Two classes dominated. **(1) The seep's traversal was broken in
  two independent ways.** Vanilla DOOM has **no minisegs** — `P_LoadSegs` gives every seg
  a linedef (`p_setup.c:196-198`) — so a *subsector* graph built from segs leaves every
  multi-leaf room disconnected and `d` cannot propagate inward from a doorway at all; and
  resolving `d` **per node** rather than per grid cell reproduces exactly the abrupt
  room-boundary step the section itself rejects as option (C). Both fixed by moving the
  graph to **sectors** and resolving distance **per cell** from portal points. Also
  unhandled: self-referencing sectors (two-sided, full opening, drawn as a solid wall — a
  fresh INV-12 leak), levels with no sky at all, the unreachable/void sentinel (an `R16F`
  `+inf` under a zero bilinear weight yields `NaN`), the map-extent overflow, and the
  sampler address mode (`REPEAT` would wrap outdoor `d = 0` onto indoor air at the
  opposite map edge). **(2) Every `file:line` citation had rotted again** — `r_vulkan.cpp`
  by +4..+6 and `pathtrace.comp` by +2, because both files grew under DOOM-0254/0263 after
  loop 3 re-anchored them. 50 citations re-verified against the source and corrected;
  three landed on entirely unrelated constructs (`m_menu.c:1464` was the menu *title*, not
  the De-tile row). Also: L2's sky term never said whether it **replaces or adds to** L1's
  shipped flat sky ambient (`marchFog`'s own comment reads "L2 adds…" — the double-count
  class this doc has shipped before); `kSunDir` was called "new" though it is already
  declared at `pt_common.glsl:42`; §7 credited L1 with a "bilateral upsample" when the
  shipped code is a plain bilinear; L1b's spot-check reused the whole-feature ≤ 15 % gate
  (the identical defect loop 2 fixed for L1c); L1c's own row bound two different
  thresholds (8 % and 15 %) to one decision; the menu shopping list was one edit short (a
  missing forward declaration — the build would not compile); and the implementation plan
  carried nine CRITICALs of its own, including the struck `σ = … × skyExposure` form and a
  `kFogBaseDensity` 19× the shipped value.
- **Loop 3** — CRITICAL 1 · HIGH 2 · MEDIUM 11 · LOW 14 · INFO 2. The flood fill was
  specified cell-to-cell, but comparing two cells' *sector heights* carries no
  information about whether a **wall** stands between them — it would have walked
  straight through walls in the common case. Rewritten to flood over **segs**
  (`P_LineOpening`) and rasterise afterwards. Also: the distance field had no
  world→texel transform and no lane to carry one (added a UBO); sets 0 and 2 were never
  ruled out as descriptor homes — set 2 is where L1 already put `fogImg` — so the
  "needs its own set" conclusion was over-engineered.

**Cold-eyes log — 2026-07-24 amendment** (rule 14 — looped until convergence; 2 lanes
= amendment-accuracy + whole-doc-coherence, each loop cold):
- **Loop 1** — CRITICAL 0 · HIGH 3 · MEDIUM 3 · LOW 5 · INFO 3 (9 fixed, 2 dismissed).
  The up-ray + L2 sun ray "reaches custom-index-2 sky" was wrong (shadow mask `0x01`
  can't hit the mask-`0x04` sky instance → detect open sky via the **miss**); the mode-6
  sky-distance fog can't run in `svgf_composite.comp` (no `pt_common` consts) → the
  megakernel writes `fogImg`, the existing fold reads it; plus a sweep of post-L1
  citation drift in the pre-existing body (`main()` 762→798, push-constant asserts,
  etc.); profiler-pool "8/8 full" corrected.
- **Loop 2** — CRITICAL 1 · HIGH 6 · MEDIUM 2 · LOW 3 · INFO 2 (10 fixed, 2 dismissed).
  INV-9 still said "custom-index 2" (reconciled to the mask/miss mechanism); "up-ray
  roughly doubles the ray count" was wrong — the shipped march does **zero** rays/sample,
  so it is the *first* ray; the L1b 60 FPS spot-check collided with the goo room's
  pre-existing ~40 FPS (pinned to a non-goo scene + added-Δ); more "Depends on"/INV-7
  citation drift; the profiler pool is in fact **all 8 slots used** (loop-1 trusted a
  stale code comment).
- **Loop 3** — CRITICAL 0 · HIGH 1 · MEDIUM 1 · LOW 6 · INFO 2 (8 fixed, 1 dismissed).
  Only citation-precision + wording left: sky-branch range `:93-104`→`:93-107`; INV-8
  pin `:8177`→`:8207`; three menu draw citations; a note that L1's composite-side gate
  rides a separate `SvgfPC.misc3.y` lane; `skyExposure` is binary per-sample. An
  independent cold audit verified ~45 other citations byte-exact. **Converged**
  (polish) — no design/structural/mechanism finding remains.

**Cold-eyes log** (rule 14 — looped until convergence, 2026-07-23):
- **Loop 1** (2 lanes) — CRITICAL 0 · HIGH 1 · MEDIUM 5 · LOW 4 · INFO 3, all verified
  & fixed. Headline: the fog composite mixed colour spaces at the sky/wall seam (pinned
  linear-radiance on both branches); the menu plumbing listed 2 of 6 sites; "DOOM-0042
  (emitter set)" was the wrong ID (→ DOOM-0009 buffer / DOOM-0084 static slice); the
  perf gate read in FPS not ≤ 5 % present-total; INV-7/8 named no falsifier
  (→ `-shotcompare` / by-construction).
- **Loop 2** — CRITICAL 0 · HIGH 0 · MEDIUM 3 · LOW 4 · INFO 2, all verified & fixed.
  The mode-4 composite hook was unspecified (added `pathtrace.comp:1024`); the INV-7
  `-shotcompare` falsifier was wrong — it renders RT-only, so reserved for INV-8;
  "mirror `rb_wet` exactly" was the wrong menu template for a 0..3 dial (→ `rb_detile`).
- **Loop 3** — CRITICAL 0 · HIGH 1 · MEDIUM 2 · LOW 4 · INFO 1, all verified & fixed.
  `rb_fog`'s shipped default conflicted with DOOM-0208's canonical-config pin
  (reconciled: default `=1`, golden re-blessed with fog, fog-off identity
  by-construction); "no new bindings" vs the new fog image (reworded); transmittance
  "RGB or scalar" vs the `RGBA16F` packing (pinned scalar; coloured absorption → Q11).
- **Loop 4** — CRITICAL 0 · HIGH 0 · MEDIUM 1 · LOW 3, all verified & fixed. One
  completeness gap — the half-res fog upsample at sky/far-depth pixels (added a
  plain-bilinear fallback at the sky sentinel); the rest polish. Reviewer verdict:
  "genuinely tight." **Converged** — no substantive finding remains.
