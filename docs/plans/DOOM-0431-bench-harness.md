# DOOM-0431 — Repeatable benchmark harness: implementation plan

**Status:** not started.
**Contract:** `docs/specs/DOOM-0431-bench-harness.md`. Design rationale lives
there; this file is the build order and nothing else.

**Goal:** one command runs the engine through fixed scenes, prints what costs
the most, and compares the result to a committed baseline.

**Tech:** C (engine, `linuxdoom-1.10/`), Python standard library only
(`tools/`). Build `make -C linuxdoom-1.10`. Tests `make -C linuxdoom-1.10 test`.

---

## Global constraints

Copied from the spec. Every task's requirements include these.

- **`-benchlog` absent ⇒ nothing happens.** No allocation, no open file, no
  call from the frame path (INV-1).
- **The emitter flushes at each counter family's own reset site**, inside that
  site's existing gate. Never one unified call — the families reset at
  different places on different clocks (spec §4.1).
- **No new GPU timestamp slot, and no existing slot moved.** DOOM-0345 INV-7
  governs that layout.
- **Every measurement records both the tier requested and the tier actually
  rendered**, and every comparison is against the rendered one (INV-2).
- **`-bootsmoke`'s Classic pin is not touched.** It guards a GPU-less CI
  runner. `-benchtics` exists because of it.
- **Solid and Ultra only.** Classic has no per-pass counters.

## Verification model — read once

Every task verifies with some subset of:

1. **Build:** `make -C linuxdoom-1.10 -j"$(nproc)"` — no new warnings.
2. **Regression:** `make -C linuxdoom-1.10 test` — green. **`make` does not
   build the tests; only `make test` does.** Rebuild before trusting a test
   you just changed.
3. **Demo fixtures:** `-timedemo` over each fixture still reports the gametic
   count `ROADMAP.md` records for it. This is the sensitive one — a changed
   count means the playsim moved.
4. **Headless run:** the capture-harness recipe — a throwaway `-config`, an
   absolute `-iwad`, `SDL_VIDEODRIVER=dummy` only where no GPU is wanted.
   **Read the exit code**; a run that prints the expected line and then dies
   looks identical to one that worked.

---

### B1 — the emitter, headless

Write `linuxdoom-1.10/bench_log.c` / `.h`. Split the row formatter out as a
pure function taking a reduced sample and returning CSV lines.

- [ ] `bench_log.c` / `.h`: open, flush, close, and the pure row formatter.
- [ ] `linuxdoom-1.10/tests/bench_log_test.cpp` feeding a known sample.
- [ ] Makefile: one object. No test-wiring edit — tests are one file each.

**Verify:** `make test` green, with the new test asserting the exact CSV lines
— including the blank `fps` on non-total rows, megabytes on `mem` rows, and
`depth` 1 on a component row.

### B2 — wire it into the engine

- [ ] `-benchlog <path>`, `-benchscene <name>`, `-benchtics N` in `d_main.c`.
      `-benchlog` sets `rb_profile`. `-benchtics` exits 0 after N tics and
      pins nothing.
- [ ] `kRasterTimestamps` / `kRtTimestamps` in `r_vulkan.cpp`; rewrite `nq` to
      read them.
- [ ] **In the same change**, update DOOM-0345 INV-7's standing grep to the
      constant form. It greps for the literal `nq = g.profRasterFrame ? 7u :
      10u;` and expects one hit; the rewrite takes it to zero, and a live test
      on a shipped feature would read as broken.
- [ ] The two slot-name tables, with a static assertion tying each table's
      length to the bucket count its path derives from those constants.
- [ ] **A static assertion that `ts[]` is at least `kRtTimestamps` long.** The
      readback buffer is a fixed `uint64_t ts[10]`, and the code's own comment
      warns that widening `nq` without widening it is a silent overflow. These
      constants exist so a future pass is added by touching one — which is that
      edit. Nothing is wrong today; the assertion is what keeps it that way.
- [ ] A flush call at each reset site, inside that site's own gate.
- [ ] The sidecar, carrying `tier_requested` and `tier_rendered`. **Write it
      after `RB_Init` — not at log-open.** `RB_Init` runs inside `D_DoomLoop`,
      long after `D_DoomMain` parses argv, so a sidecar written where the flag
      is parsed records the config's renderer in both fields. They then always
      agree, and the mismatch check never fires.

**Verify:**

- [ ] (a) `-warp 1 1 -benchtics 400 -benchlog /tmp/a.csv` on a Solid config
      writes a CSV with `frame,total` rows and a sidecar whose `tier_rendered`
      is `solid`. Swap `-benchtics` for `-bootsmoke` and it reads `classic` —
      that contrast is the point of the flag.
- [ ] (b) The Ultra RT run emits one distinct `gpu` slot name per entry in
      `kBenchRtSlots`, plus `gpu,total`; **and the Solid raster run emits one
      per entry in `kBenchRasterSlots`, plus `gpu,total`.** Both arms, because
      the static assertion is build-time and cannot see an unemitted row.
- [ ] (c) A golden capture with `-benchlog` absent compares byte-identical to a
      pre-feature build via `scripts/ab_diff.py`.
- [ ] (d) Every reference to the flush entry point sits inside the gate that
      resets its family; none outside.

**Stop if** (a) reports `classic` on the `-benchtics` arm — the pin is being
reached by a path this plan did not expect.

### B3 — the `-timedemo` exit

- [ ] Under `-benchlog`, a completed timed demo closes the log, prints to
      stdout and exits 0. Without the flag, `G_CheckDemoStatus` is unchanged.

**Verify:**

- [ ] `-timedemo demo1 -benchlog <tmp>` exits 0.
- [ ] `-timedemo demo1` alone still exits non-zero and prints
      `timed N gametics in M realtics`.
- [ ] The demo fixtures still report their recorded gametic counts.

### B4 — the scene list and the runner

- [ ] `tools/bench-scenes.json` — the spots from §10 Q1, captured with the
      Developer menu's print-position row (`M_DevPrintPos`), which emits a
      ready-to-paste `-warpto` line to stdout and the HUD. Needs `DEV=1`.
- [ ] `tools/bench.py`: build each command line, run it, collect CSV + sidecar,
      stamp `git rev-parse HEAD`, write `bench-result.json`, print the table.
- [ ] **The reduction, here and not in B5** — drop the first interval, require
      at least three usable, take the median. The table is of reduced metrics,
      so B4 cannot print it without this; left in B5 a builder invents an
      ad-hoc reduction, and the table and `--compare` then disagree about the
      same CSV.
- [ ] Delete each scene's CSV path before launching. Timeout every launch.

**Verify:**

- [ ] **In the table `bench.py` prints:** no depth-0 row exceeds `frame,total`,
      and every `category,name` key appears once. Not the CSV — there each key
      recurs once per interval, and B5 needs several.
- [ ] A scene with no `render_scale` is rejected by name, not defaulted.
- [ ] A `spot` scene against a non-`DEV` binary is refused — the launch did not
      print `-inspect: monsters ignore you`.
- [ ] A scene pointed at an unreachable tier reports `SKIPPED`; the process
      still exits 0.

**Note:** spot scenes need `make DEV=1`. A plain `make` silently ignores
`-freeze` and `-inspect`.

### B5 — the comparator and `--selftest`

- [ ] `--compare`, `--update-baseline`, `--selftest` in `tools/bench.py`.
- [ ] Refuse a comparison whose conditions differ; compare `tier_rendered`.
- [ ] `tools/bench-fixtures/`, each fixture stating its expected verdict as a
      literal.

**Verify:**

- [ ] `--selftest` passes with no GPU and no engine binary present.
- [ ] A fixture regressing a gated metric past `fail_pct` exits 1.
- [ ] A fixture with two usable intervals exits 2 and reads `INCONCLUSIVE`.
- [ ] A truncated baseline exits 3.
- [ ] A sidecar pair differing only in `render_scale` yields `SKIPPED`.
- [ ] A sidecar pair whose `tier_rendered` and `tier_requested` disagree over an
      otherwise complete CSV yields `SKIPPED`.
- [ ] A fixture whose first interval is far above the rest reduces to the
      steady-state value.

### B6 — baseline capture, and the harness's own cost

Reference hardware only (RX 6600).

- [ ] Run the scene list once and write a **provisional** baseline from it —
      `--compare` has nothing to read otherwise, and a missing baseline is
      exit 3.
- [ ] Run it again on the same unchanged tree and `--compare` against that
      provisional baseline.
- [ ] **Ask the user whether the baseline is committed at all** — spec §10 Q4
      leaves it open, and the cost of committing is that a driver change will
      read as a regression. Ask before capturing, not after.
- [ ] **Settle the gate list** — spec §10 Q5. Take the per-pass run-to-run
      variance from the double run, and gate a pass only where its variance
      sits well inside `warn_pct`. Record the decision and the variance it
      rested on.
- [ ] Only once the double run comes back clean, commit
      `tools/bench-baseline.json`.
- [ ] Measure `-benchlog`'s overhead: present-total with the flag against the
      same scene with `rt_profile 1` and the flag off.

**Verify:**

- [ ] Every gated metric compares `OK` on the double run. **`IMPROVED` is a
      failure here** — on an unchanged tree it means variance crossed the
      threshold. Any scene that fails is fixed or dropped before the baseline
      is committed.
- [ ] The measured overhead is under 1 % of present-total, and is stated.

**Then, and only then:** add the harness to `docs/standards/performance.md`
§ Measuring, and a line to `CLAUDE.md`. Both were deliberately left until the
tool exists (spec §12).

**Stop if** the double run cannot come back clean on a scene — that scene is
not pinned, and a baseline built on it is worthless.

### B7 — the CPU blind-spot counters

- [ ] `tick` (around `G_Ticker` / `M_Ticker`), `overlay` (the 2D draw into
      `screens[0]`), `sound` (`S_UpdateSounds` + `I_UpdateSound`), `wipe`.

**Verify:** the gap is `frame,total` minus `cpu,present-total` — that pair, and
not a sum of depth-0 `cpu` rows, because `cpu,build` is also depth 0 and may
already sit inside `present-total` (spec §4.1 forbids the sum for this reason).
Measure that gap on a scene before B7, then require `tick` + `overlay` +
`sound` + `wipe` to account for **at least half** of it. Record the share.
A floor is needed or nothing fails: four timers each measuring an empty span
still produce a share, and "record the share" would tick the box.

**Stop if** the four new rows sum to more than the measured gap — they are then
timing spans that overlap each other or overlap the present.

### B8 — memory and load time

- [ ] `zone_mb` — memory **in use**, the heap less `Z_FreeMemory()`. Not free
      memory: the comparator reads a rise as `FAIL`.
- [ ] `gpu_mb` — summed Vulkan device allocations.
- [ ] `level_load` — level start to first presented frame.

**Verify:**

- [ ] `zone_mb` plus `Z_FreeMemory()` converted to MB equals the heap size,
      both before and after a level change. **No direction is asserted** — a
      correct in-use figure falls when the next map is smaller, and the
      natural "repair" for that would be to log the free figure, which is the
      inversion this step's own definition forbids.
- [ ] `level_load` is non-zero and differs between a small map and a large one.
- [ ] Re-run `--compare`: the phase-2 metrics appear and are not gated by
      default.

---

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-12 | 3 | 0 | 3 | 1 | 3 | **7 verified, 0 dismissed, all 7 fixed.** All three lanes found B8's direction claim: it required `zone_mb` to move "in the direction that says more in use", but a correct in-use figure FALLS when the next map is smaller, and the step directly above warns that logging the free figure is the bug — so the natural repair for the false failure was the inversion the spec forbids. B8 now checks `zone_mb` plus `Z_FreeMemory()` against the heap size and asserts no direction. All three also found B7 subtracting the summed depth-0 `cpu` rows, which double-counts `build` under DOOM-0074 build-ahead — spec §4.1 forbids that sum by name. The gap is `frame,total` minus `cpu,present-total`, which is what `r_vulkan.cpp`'s comment actually describes, and the new rows must now account for a stated share of it. One lane found the sidecar's write timing unsatisfiable, and it reached back into the contract: §4.3 wrote the sidecar "at open time" while reading `tier_rendered` "after the backend has initialised", and `RB_Init` runs inside `D_DoomLoop`, long after argv is parsed. Left alone, both fields would carry the config's renderer, always agree, and the loop-1 mismatch clause would never fire. Fixed in both documents; the spec's §13 carries the fold-back row. One found B2 checking `gpu` rows on the Ultra arm only, while INV-3 requires a run of each path and the static assertion is build-time and cannot see an unemitted row — a flush sited so raster emits nothing would have passed. One found the global constraint saying a measurement records "the tier that actually rendered", one tier, while B2 builds two fields and B5 compares them. One found B4's key-uniqueness check had lost its subject: it belongs to the printed table, and applied to the CSV it can never pass, since B5 needs several intervals and every key recurs per interval. One found B6 ordered to `--compare` before capturing the baseline it reads. Resolved clean and not in the tally: two lanes could not settle whether a menu row prints a pasteable `-warpto` line. `M_DevPrintPos` does, to stdout and the HUD; B4 now names it, and that it needs `DEV=1`. |
| 2 | 2026-09-12 | 3 | 0 | 3 | 1 | 1 | **5 verified, 0 dismissed, all 5 fixed. A calm cap: 2 of the 5 landed on text loop 1 wrote.** The serious one was cross-document and neither gate had seen it: B2 rewrites `nq` to read the new timestamp constants, and DOOM-0345 INV-7 carries a standing grep for that exact literal expecting one hit. The rewrite takes it to zero, so a live invariant test on a shipped feature would read as broken — while this plan's own global constraint claimed INV-7 was untouched and the contract's §12 said no edit was owed. Both corrected; B2 now updates that grep in the same change. Two lanes found the contract's own B7 still prescribing the sum of the `cpu` rows that §4.1 forbids by name — drift this plan's loop 1 created by fixing only its own side. The spec now states the same `frame,total` minus `cpu,present-total` pairing. One lane found B4 printing a table of reduced metrics while the reduction was a B5 deliverable, so a builder would invent an ad-hoc one and the table and `--compare` would disagree about the same CSV; the reduction moves to B4. One found B7's "account for a stated share" unfalsifiable — four timers measuring empty spans still produce a share — so a floor is named. One found B6 silently settling two questions the contract schedules there: whether the baseline is committed at all (§10 Q4) and which metrics are gated (§10 Q5). Both are now explicit items, and Q4 asks the user before capture rather than after. Adopted from a lane's open question rather than filed as a finding: `ts[]` is a fixed `uint64_t ts[10]` whose own comment warns that widening `nq` without widening it is a silent overflow. Nothing is wrong today, but these constants exist so a future pass is added by touching one — which is that edit. B2 now asserts the buffer length too. |
