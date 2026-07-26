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

---

## Open — not yet fixed

- **Cold-eyes has not converged.** Loop 5 returned 3 CRITICALs; the `--max-loops` cap of 5
  is reached. Loop 6 is owed.
- **The plan has no L1c and no L1d task.** Its ⚠ banner says it must not be executed past
  L1b. Writing those tasks is the work that closes INV-9..12's coverage gap.
- **L1b's measured Δ is still unrecorded**, so L1c's `8 % − Δ(L1b)` allowance cannot be
  computed yet (§6).
