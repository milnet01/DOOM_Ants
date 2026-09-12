# DOOM-0431 — Repeatable benchmark harness

**Status:** spec draft (2026-09-12).
**Kind:** implement.
**Source:** ROADMAP DOOM-0431 (`user-request-2026-09-12`). Scope calls taken with
the user 2026-09-12 — see §3. Reference design: the Vestige project
(`/mnt/Games/Scripts/Linux/Vestige/`), `engine/profiler/profile_log.h` and
`tools/perf_gate.py`, MIT and by the same author.

**Layman:** One command runs the game through the same fixed situations every
time, prints what is costing the most, and tells you whether a change made
things slower than they were last week.

**Depends on:**

- **DOOM-0090** — the per-pass GPU timer. `rb_profile` (the `` \ `` key) gates a
  `VkQueryPool` whose timestamps land in `profMs[10]`, printed by
  `[raster_profile]` and `[rt_profile]`. This spec reads those buckets; it does
  not re-time them.
- **DOOM-0170** — the CPU-side segment timers `cpuMs[5]` (fenceWait / build /
  record / submit / present-total) and `cpuBuildMs[3]` (sprites / lights /
  reheight), gated by the same `rb_profile`.
- **DOOM-0268** — `-warpto X Y [ANGLE]`, which places the player at an exact map
  position after the level loads. This is the still-spot fixture.
- **DOOM-0294** — `-inspect` and `-freeze`, which stop monsters acquiring the
  player and hold monsters and missiles still. A live level is not a
  measurement; that spec's own reasoning applies unchanged here.
- **DOOM-0351** — `-rtview N`, the headless route to the Solid + ray-traced-view
  combination.

**Delivers / subsumes:** nothing. No existing roadmap item is closed by this.
It is, however, the mechanical replacement for the hand-measured perf gates in
DOOM-0011 §6, DOOM-0181 and DOOM-0183 — each of which defined its own walked
route and read a scrolling log. Those gates are not retro-fitted here; see §12.

**Defers (explicitly NOT in this build):**

- **Any automatic trigger.** The user chose on-demand only (§3 decision 4). No
  pre-push hook, no CI job, no release step. `standards/local-gate.md` is
  untouched by this spec.
- **Driving the Windows box.** `ssh wintest` is available and the runner could
  drive `linuxxdoom.exe` over it, but that machine's GPU differs from the
  reference RX 6600, so its numbers need their own baseline and can never be
  compared to the Linux ones. Not filed; raised in §10 Q3.
- **A flame graph, a trace viewer, or any per-frame capture.** The log is
  interval averages. Single-frame spikes are a different instrument.
- **Optimising anything.** This spec ships a measuring device. What it finds is
  somebody else's roadmap item.

**Scope:** a new engine flag, a new emitter module, and two new files under
`tools/`. The engine's rendering behaviour does not change. Every tier is
measurable — Classic, Solid and Ultra, rasterised and ray-traced — because the
question "which tier is slow" is one of the questions.

---

## Contents

- §1 Goal — §2 Where this sits — §3 Scope decisions — §4 Design (4.1 the bench
  log · 4.2 the metric name table · 4.3 the sidecar · 4.4 the scene list ·
  4.5 the runner · 4.6 the table · 4.7 the comparator · 4.8 the `-timedemo`
  exit · 4.9 phase-2 counters) — §5 Data & resources — §6 Performance budget —
  §7 Build order — §8 Invariants — §9 Alternatives considered — §10 Open
  questions — §11 What checks this — §12 Cross-doc impact — §13 Cold-eyes loop
  log

---

## 1. Goal

After this ships, one command answers two questions that currently take a
play-test and a guess.

**Where is the time going?** A sorted table, biggest cost first, each row with
its share of the frame, for a named situation the machine can reproduce
exactly.

**Did this change make it worse?** The same numbers compared against a
committed baseline, per metric, with a verdict and an exit code — so "is this a
regression" stops being a judgement call made by squinting at a terminal.

The engine already counts nearly everything the first question needs. What it
has never had is a way to ask the question twice and get comparable answers.

## 2. Where this sits

### What already exists

**The counters.** `rb_profile` (declared `extern "C" { int rb_profile = 0; }` in
`r_vulkan.cpp`, toggled by the `` \ `` key) gates three families:

| Family | Storage | Printed as |
|---|---|---|
| CPU wall-clock segments | `cpuMs[5]` | `[cpu_profile]` |
| CPU build sub-timers | `cpuBuildMs[3]` | `[cpu_build]` |
| Per-pass GPU timestamps | `profMs[10]` | `[raster_profile]` / `[rt_profile]` |

All three accumulate per frame and print a per-frame average once a second,
then reset. That once-a-second reduction is already the right shape for a log;
it is simply written to `stdout` where nothing can read it back.

**The repeatable workloads, both already built and neither used for this.**

`-timedemo <lump>` replays a recorded demo. `G_TimeDemo` sets `singletics =
true`, which routes `D_DoomLoop` down its `singletics` branch — one tic and one
`D_Display` per iteration with no real-time pacing. That matters: the ordinary
demo path is *not* free-running, because `D_DoomLoop`'s `uncapped` predicate
excludes `demoplayback` and `TryRunTics` then blocks until real time catches
up. Only the `singletics` route `-timedemo` takes measures rendering rather
than measuring the clock.

`-warpto X Y [ANGLE]` (`G_WarpToSpot`) unlinks the player, sets the position,
relinks it and drops it to the destination sector's floor. With `-freeze` and
`-inspect` from `G_DevInspectFromArgv` the world then holds still. That is a
still-spot fixture with no motion noise.

**The reference implementation.** Vestige solved this shape already. Its
`ProfileLog` (`engine/profiler/profile_log.h`) writes
`time_s,category,name,depth,ms,fps` at roughly 1 Hz, as interval averages, and
is off unless a path is opened. Its `tools/perf_gate.py` parses that, drops the
warm-up interval, takes a median per metric, compares to a committed baseline
JSON and returns `OK` / `WARN` / `FAIL` / `IMPROVED` / `MISSING` / `SKIPPED` /
`INCONCLUSIVE` with four documented exit codes. Both are MIT, by this
project's author, and GPL-v2-compatible for inclusion here.

### What this must not break

**`docs/standards/performance.md` § The comparison rule.** *"Always quote an FPS
number together with the render scale it was measured at, and only compare
like-for-like."* `render_scale` defaults to **50** (`m_misc.c`,
`{"render_scale",&rb_renderscale, 50}`). A harness that records a frame time
without recording the scale it was taken at produces exactly the comparison
that standard forbids, and does it automatically, at volume. §4.3 and INV-2
make the scale part of every record and make a cross-scale comparison a refusal
rather than a number.

**DOOM-0345 INV-7** — *"every RT bloom pass is timed, and all nine widening
sites moved"*. The RT path writes ten timestamp slots and the raster path
seven; `vkGetQueryPoolResults` is asked for exactly `nq` of them and returns
`VK_NOT_READY` — dropping the whole print — if asked for a slot nobody wrote.
The RT path's print order is also not its slot order: `profMs[3]` (blit) is
printed last, and `profMs[4..9]` are a sub-breakdown of the `profMs[2]`
umbrella. A second hand-maintained list of pass names is therefore a list that
will disagree with the first. §4.2 and INV-3 are that constraint carried into
this spec. This spec adds no timestamp slot and moves none.

## 3. Scope decisions (agreed with the user)

Taken 2026-09-12, in the conversation that filed DOOM-0431.

1. **Both workload kinds.** Demos for the honest moving-gameplay number, still
   spots for isolating one effect's cost without motion noise. The user chose
   "both" over either alone; they answer different questions and the still spot
   is the one that can attribute a cost to a feature.
2. **Whole frame, plus memory and load time.** Not the renderer alone. This
   includes the blind spot `r_vulkan.cpp` documents in its own comment beside
   the `[cpu_profile]` print: *"The gap between the FPS counter's frame time and
   present-total is the rest of the engine (game tick + the software 2D overlay
   drawn into screens[0] each frame)."* Nothing times either today.
3. **Both output forms.** A human-readable table and a machine-readable file
   with a baseline comparison — the same numbers, two renderings.
4. **On demand only.** No automatic trigger of any kind in this build.
5. **Built in two phases (approach B).** Phase 1 is the whole loop — log,
   scene list, runner, table, comparator — over the counters that already
   exist. Phase 2 adds the decision-2 counters. Chosen over building everything
   at once because the plumbing is what is missing: the existing counters
   already found the DOOM-0074 bottleneck — a per-frame light rebuild costing
   **8.0 ms of an 11.0 ms CPU build**, measured on E1M1 at 50 % render scale
   with the flashlight on, recorded in `ROADMAP.md` under DOOM-0170's
   2026-07-14c progress note — without any of decision 2's additions.
   Rejected alternatives in §9.

## 4. Design

### 4.1 The bench log

A new engine flag `-benchlog <path>` opens a CSV and writes the counters to it.

**It implies `rb_profile = 1`.** The counters are gated on that flag and a log
with the profiler off records nothing. The implication is one-way: `-benchlog`
turns the profiler on, and never turns it off, so a run that also wanted the
on-screen print keeps it.

**The row schema is Vestige's, unchanged:**

```
time_s,category,name,depth,ms,fps
```

`category` is one of `frame`, `gpu`, `cpu`, `mem`. `name` is the metric's name
within its category. `depth` is nesting depth, `0` for everything phase 1
emits. `ms` carries milliseconds — except on `mem` rows, where it carries
megabytes, which is Vestige's convention and is kept so its parser transfers
unmodified. `fps` is filled only on the `frame,total` row and left blank
elsewhere.

**Rows are written once per completed interval, not per frame.** The engine
already accumulates and divides by a frame count once a second; the log writes
that same reduced value. A per-frame log would be a different instrument (§10
Q2) and would perturb what it measures.

**Off unless opened.** With no `-benchlog` the module allocates nothing, opens
nothing and is never called from the frame path. INV-1.

### 4.2 The metric name table

Each emitted row needs a name, and the GPU rows need one per timestamp slot.
The failure to design against is DOOM-0345 INV-7's: a pass is added, the slot
count moves, and a name list somewhere else keeps its old length — so one
bucket silently absorbs another's time and every reading still looks
plausible.

**One table per path, keyed by slot index, declared beside the readback that
uses it**, in `r_vulkan.cpp`:

```c
struct BenchSlotName { int slot; const char* name; };
static const BenchSlotName kBenchRasterSlots[] = { /* 0..5 */ };
static const BenchSlotName kBenchRtSlots[]     = { /* 0..9 */ };
```

The raster table has one entry per bucket the raster arm fills (`profMs[0..5]`,
six buckets from seven timestamps) and the RT table one per RT bucket
(`profMs[0..9]`, ten buckets from ten timestamps). The emitter iterates the
table rather than the array, so a bucket with no name cannot be written and a
name with no bucket cannot be read.

**The count is tied to the readback, not restated.** `nq` is already computed as
`g.profRasterFrame ? 7u : 10u`. A static assertion ties each table's length to
its path's bucket count, so adding a pass without adding a name fails the
build rather than shipping a mislabelled row. INV-3.

**The names are the printed words, not new coinages.** `[raster_profile]`
prints `shadow / scene / ssao / bloom / composite / hud`; `[rt_profile]` prints
`sprites / megakernel / denoise+post+taau / temporal / atrous / composite /
bloom / tonemap / taau / blit`. Reusing them means a CSV row and a terminal
line name the same thing, and a reader who knows one knows the other.

### 4.3 The run sidecar

The CSV says what the numbers are. It does not say what they are *of*, and
`performance.md`'s comparison rule turns that omission into a wrong answer
rather than a missing one.

So `-benchlog <path>` also writes `<path>.meta.json` beside it, at open time:

```json
{
  "schema": 1,
  "scene": "e1m1-start",
  "map": "E1M1",
  "tier": "solid",
  "rt_view": 0,
  "render_scale": 50,
  "width": 1920, "height": 1080,
  "gpu": "AMD Radeon RX 6600 (RADV NAVI23)",
  "workload": "spot"
}
```

The engine fills everything it knows. `scene` comes from an optional
`-benchscene <name>` argument, defaulting to the empty string; the runner
always passes it. The commit is **not** the engine's to record — there is no
build-commit symbol in the tree — so the runner stamps it into the result file
from `git rev-parse HEAD` (§4.5).

**Why a sidecar rather than extra CSV columns.** The row schema stays
byte-compatible with Vestige's parser, and per-run facts are recorded once
instead of repeated on every row. Rejected alternative in §9.

### 4.4 The scene list

`tools/bench-scenes.json`. One entry per named situation:

```json
{
  "schema": 1,
  "scenes": [
    {
      "name": "e1m1-start",
      "workload": "spot",
      "iwad": "doom.wad",
      "warp": [1, 1],
      "warpto": [1056, -3616, 90],
      "tier": "solid", "rt_view": 0,
      "render_scale": 50,
      "seconds": 12
    },
    {
      "name": "e1m1-demo1",
      "workload": "demo",
      "iwad": "doom.wad",
      "demo": "demo1",
      "tier": "ultra", "rt_view": 6,
      "render_scale": 50
    }
  ]
}
```

`workload` is `spot` or `demo` and decides which flags the runner builds.
A `spot` entry carries `warp`, `warpto` and a duration; a `demo` entry carries
a demo lump and runs to the demo's own end. `tier` and `rt_view` together
select what is measured — the two are independent, which is why both are
present and why "Solid, ray-traced" is expressible.

**`render_scale` is required on every scene**, with no default. A default here
is how a scene silently changes workload when the config file changes
underneath it, which is the comparison rule's failure with extra steps. INV-2.

The starting set covers what is already known to be interesting: the E1M1 open
start, the goo room that sits near 40 FPS, a monster-heavy fight via demo, and
a large outdoor sky view. Which exact spots is §10 Q1, because the coordinates
have to be taken from a running engine.

### 4.5 The runner

`tools/bench.py`, standard library only — DOOM_Ants has no Python test runner
and no requirements file, and adding a dependency tree to run a benchmark is a
cost the benchmark then has to justify.

For each scene it builds one command line:

| Scene field | Flags emitted |
|---|---|
| `iwad` | `-iwad <abs path to wads/<iwad>>` |
| `warp` | `-warp E M` |
| `warpto` | `-warpto X Y ANGLE` |
| `demo` | `-timedemo <lump>` |
| `tier`, `rt_view`, `render_scale` | written into a throwaway `-config` file |
| always, on a `spot` | `-freeze -inspect` |
| always | `-benchlog <tmp>/<scene>.csv -benchscene <name>` |

The tier goes through a generated config rather than a flag because that is how
the renderer is already selected: the key is `renderer` (`0` Classic, `1`
Ultra/RT3D, `2` Solid/Raster3D) and a wrong key is silently ignored and yields
the default. The runner writes the config it wants and never touches
`~/.doomrc`, so a benchmark run cannot disturb the user's settings and the
user's settings cannot disturb a benchmark run.

A `spot` scene needs an exit condition the engine does not currently have for
this shape: it must render for a stated duration and then stop. `-bootsmoke N`
already exits 0 after N tics, and is the mechanism — the runner converts
`seconds` to tics at 35 per second. A `demo` scene ends when the demo does.

The runner collects each CSV and its sidecar, stamps `git rev-parse HEAD` and
`git status --porcelain` emptiness into the result, and writes one
`bench-result.json`.

**A scene the machine cannot run is `SKIPPED`, never a pass.** A ray-traced
scene on a machine without working ray tracing, or one whose engine exits
non-zero, produces no measurement — and a harness that quietly substitutes
whatever did render reports a comfortable number for a thing it never
measured. This has a live precedent: DOOM-0347 records `-rtverify` hanging
silently forever on a non-Ultra config. Every launch therefore gets a timeout
and a non-zero exit is preserved. INV-9.

### 4.6 The table

`tools/bench.py` with no comparison flag prints, per scene, the reduced metrics
sorted by cost descending, each with its share of `frame,total`:

```
e1m1-start   solid/raster  scale 50%   1920x1080   58.3 fps (17.15 ms)
  gpu   scene              5.20 ms   30.3%
  cpu   build              3.10 ms   18.1%
  gpu   ssao               1.80 ms   10.5%
  ...
  mem   gpu_mb           412.00 MB        —
```

The share column is the point: a millisecond figure alone does not say whether
a pass is the problem. `mem` rows carry no share, because megabytes are not a
fraction of a frame.

### 4.7 The comparator

`tools/bench.py --compare` reduces and compares; `--update-baseline` writes
`tools/bench-baseline.json`.

**Reduction**, ported from `perf_gate.py`: drop the first interval, require at
least three usable intervals, take the **median**. The first interval is
discarded because it is never representative — shader pipelines compile, the
GPU clocks up, and on the RT path the SVGF denoiser and TAAU are still
settling. The median rather than the mean because one stalled interval should
not move a verdict. A series with fewer than three usable intervals yields
`INCONCLUSIVE`, not a pass. INV-5 and INV-6.

**Thresholds** carried in the baseline, defaulting to `perf_gate.py`'s:
`warn_pct` 10, `fail_pct` 25, `min_abs_ms` 0.05, `min_abs_mb` 1.0. The absolute
floors exist so a metric that moves from 0.02 ms to 0.03 ms is not a 50 %
regression.

**Verdicts:** `OK`, `WARN`, `FAIL`, `IMPROVED`, `MISSING`, `SKIPPED`,
`INCONCLUSIVE`. **Exit codes:** `0` pass, `1` a gated metric regressed, `2` no
gated metric reached a conclusive verdict (re-runnable), `3` the baseline is
unreadable or unsupported (fix the file; do not retry).

**A comparison across differing run conditions is refused, not scaled.** If a
scene's sidecar `render_scale`, `tier`, `rt_view` or resolution differs from
the baseline's for that scene, the metric is `SKIPPED` with the mismatch named.
Scaling one to the other would manufacture a number that was never measured.
INV-2.

**`--selftest` runs the reduction and verdict arithmetic over committed
fixtures and needs no GPU.** That is where this tool's correctness is actually
checked: the engine side is proved by running it, but the arithmetic is pure
and deserves a real test. The fixtures live in `tools/bench-fixtures/`. INV-8.

### 4.8 The `-timedemo` exit

`G_CheckDemoStatus` reports a timed demo through `I_Error`:

```c
I_Error ("timed %i gametics in %i realtics", gametic, endtime-starttime);
```

`I_Error` writes to `stderr` and exits non-zero. So a `-timedemo` run that
finishes perfectly is indistinguishable, to a script, from one that crashed.
That is the failure class where a run prints its expected message and then
dies: a caller grepping for the message records it as a pass, because nothing
read the exit code.

**Under `-benchlog`, a completed timed demo closes the log, prints its result
to `stdout` and exits 0.** Without `-benchlog` the existing behaviour is
untouched, because the demo-fixture regression check reads that `I_Error` line
and expects its gametic counts (30 / 30 / 30 / 70 / 350) — changing it
unconditionally would break a check this project relies on. INV-4.

### 4.9 Phase-2 counters

Decision 2's additions, all on the existing `rb_profile` gate and all emitted
through the same log:

| Metric | Category | Source |
|---|---|---|
| `tick` | `cpu` | around `G_Ticker` / `M_Ticker` in `D_DoomLoop` |
| `overlay` | `cpu` | around the software 2D draw into `screens[0]` |
| `sound` | `cpu` | around `S_UpdateSounds` and `I_UpdateSound` |
| `wipe` | `cpu` | around the screen-wipe path |
| `zone_kb` | `mem` | `Z_FreeMemory()`, declared in `z_zone.h` |
| `gpu_mb` | `mem` | summed Vulkan device allocations |
| `level_load` | `frame` | level-start to first presented frame |

`tick` and `overlay` together are the documented gap between the FPS counter
and `present-total`; measuring them is what turns that comment into a number.
Phase 2 adds **no GPU timestamp slot** — every one of these is CPU-side or a
counter read — which is what keeps DOOM-0345 INV-7 out of this spec's way.

## 5. Data & resources

### Files added

| Path | What it is |
|---|---|
| `linuxdoom-1.10/bench_log.c` / `.h` | the CSV emitter and sidecar writer |
| `tools/bench.py` | runner, table, comparator, `--selftest` |
| `tools/bench-scenes.json` | the scene list |
| `tools/bench-baseline.json` | the committed baseline |
| `tools/bench-fixtures/` | `--selftest` inputs |

### Files changed

| Path | Change |
|---|---|
| `linuxdoom-1.10/r_vulkan.cpp` | the two slot-name tables; call the emitter where the counters already reset |
| `linuxdoom-1.10/d_main.c` | parse `-benchlog` / `-benchscene`; open and close the log |
| `linuxdoom-1.10/g_game.c` | `G_CheckDemoStatus`'s `-benchlog` exit path |
| `linuxdoom-1.10/Makefile` | one object; no test-wiring edit (tests are one file each) |

### The CSV schema

| Column | Type | Notes |
|---|---|---|
| `time_s` | float | seconds since the log opened, at interval end |
| `category` | enum | `frame` \| `gpu` \| `cpu` \| `mem` |
| `name` | string | metric name within the category |
| `depth` | int | nesting depth; `0` throughout phase 1 |
| `ms` | float | milliseconds — **megabytes on `mem` rows** |
| `fps` | float | filled only on `frame,total`; blank elsewhere |

### The baseline schema

`schema`, a `reduction` block (`drop_warmup_intervals`, `min_usable_intervals`,
`stat`), a `thresholds` block (`warn_pct`, `fail_pct`, `min_abs_ms`,
`min_abs_mb`), a `gate` allowlist of `category,name` keys, and per scene the
recorded conditions (`render_scale`, `tier`, `rt_view`, `width`, `height`) plus
each metric's reduced value. Conditions are stored per scene because §4.7
compares them before it compares numbers.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | pass — no `FAIL`, and at least one gated metric conclusive |
| 1 | a gated metric regressed |
| 2 | inconclusive — no gated metric reached a conclusive verdict; re-runnable |
| 3 | config error — unreadable or unsupported baseline; fix the file |

## 6. Performance budget

A measuring device that changes what it measures is worthless, so this one has
a budget of its own.

**With `-benchlog` absent: exactly zero.** No allocation, no open file, no call
from the frame path. The observable is that the flag's absence leaves the
binary's behaviour unchanged, which INV-1 states as byte-identical output on a
golden capture.

**With `-benchlog` present: under 1 % of present-total**, measured as the
`[cpu_profile]` present-total average over a scene run with the flag against
the same scene run with `rb_profile` on and the flag off. The work per interval
is a few dozen `fprintf` calls once a second against a per-frame budget of
roughly 17 ms at 60 FPS, so the bound is expected to be met with room; it is
stated as a gate rather than assumed, and B6 measures it.

**The 60 FPS floor does not apply to a benchmark run.** `performance.md` sets
that floor for the game as played. A scene deliberately chosen because it is
slow is not a regression against it.

## 7. Build order

Phase 1 is B1–B6 and is the whole loop end to end. Phase 2 is B7–B8.

- **B1 — the emitter, headless.** `bench_log.c` with the row formatter split out
  as a pure function. *Verify:* a `*_test.cpp` in `linuxdoom-1.10/tests/` feeds
  a known sample and asserts the exact CSV lines, including the blank `fps` on
  non-total rows and megabytes on `mem` rows. `make test` green.
- **B2 — wire it into the engine.** `-benchlog` / `-benchscene`, the two slot
  name tables with their static assertions, the sidecar. *Verify:* a `-warp 1 1
  -bootsmoke 400 -benchlog /tmp/a.csv` run produces a CSV with at least three
  `frame,total` rows and a sidecar naming the tier and render scale it ran at.
- **B3 — the `-timedemo` exit.** *Verify:* `-timedemo demo1 -benchlog` exits 0
  and prints to stdout; `-timedemo demo1` without the flag still exits non-zero
  with its `I_Error` line, and the five demo fixtures still report 30 / 30 /
  30 / 70 / 350 gametics.
- **B4 — the scene list and the runner.** *Verify:* `tools/bench.py` runs every
  scene and prints a table whose rows sum, within rounding, to `frame,total`;
  a scene pointed at a tier the machine cannot reach reports `SKIPPED` and the
  process still exits 0.
- **B5 — the comparator and `--selftest`.** *Verify:* `--selftest` passes with
  no GPU present; a fixture with a 30 % regression on a gated metric exits 1; a
  fixture with two usable intervals exits 2; a truncated baseline exits 3.
- **B6 — baseline capture and the harness's own budget.** Capture the baseline
  on the reference RX 6600; measure the flag's overhead against §6's bound.
  *Verify:* the same scene run twice on an unchanged tree compares `OK` on
  every gated metric (INV-7), and the measured overhead is stated.
- **B7 — the CPU blind-spot counters.** `tick`, `overlay`, `sound`, `wipe`.
  *Verify:* on a scene at a known frame time, `frame,total` minus the sum of
  the `cpu` rows is smaller than it was before B7 — the gap the comment
  describes has shrunk, and by how much is recorded.
- **B8 — memory and load time.** `zone_kb`, `gpu_mb`, `level_load`. *Verify:*
  `zone_kb` tracks `Z_FreeMemory()` across a level change; `level_load` is
  non-zero and differs between a small map and a large one.

## 8. Invariants

- **INV-1** — with `-benchlog` absent the engine does no benchmark work and
  behaves identically. *Breaks when:* the emitter is called unconditionally and
  gated inside itself, so an inactive log still costs a call and a branch per
  frame — or worse, formats rows it then discards.
  *Test:* a golden capture of one scene with the flag absent, before and after
  this feature, compares byte-identical via `scripts/ab_diff.py`; and
  `-benchlog`'s parse site is the only reference to the emitter's `open`
  entry point outside the module.

- **INV-2** — every measurement records the render scale, tier, ray-traced view
  and resolution it was taken at, and a comparison across any difference in
  those is refused rather than performed. *Breaks when:* a scene omits
  `render_scale` and the runner falls back to the config's value, so the
  workload changes when `~/.doomrc` does and two runs of "the same" scene are
  compared across different pixel counts. This is `performance.md`'s comparison
  rule, mechanised.
  *Test:* a scene entry with no `render_scale` is rejected by the runner with a
  named error, not defaulted; and a `--selftest` fixture pair differing only in
  the sidecar's `render_scale` yields `SKIPPED` with the mismatch named, never
  a numeric verdict.

- **INV-3** — every GPU bucket the active path fills is named exactly once, and
  a path's name-table length equals its bucket count. *Breaks when:* a pass is
  added, `nq` and the print widen, and the CSV name table does not — so one
  bucket's time is written under its neighbour's name and every row still looks
  plausible. This is DOOM-0345 INV-7's failure in a new list.
  *Test:* the static assertion tying each table's length to its bucket count
  fails the build when a bucket is added without a name; and a run of each path
  emits exactly as many distinct `gpu` names as that path has buckets.

- **INV-4** — a benchmark run that completed exits 0, and a non-zero exit means
  a real failure. *Breaks when:* the `-timedemo` result keeps its `I_Error`
  route under `-benchlog`, so the runner records every successful demo scene as
  a crash — or the exit path is changed unconditionally and the demo-fixture
  regression check, which reads that `I_Error` line, stops finding it.
  *Test:* `-timedemo demo1 -benchlog <tmp>` exits 0; `-timedemo demo1` without
  the flag exits non-zero and still prints `timed N gametics in M realtics`;
  the five fixtures still report 30 / 30 / 30 / 70 / 350.

- **INV-5** — a verdict is decided from a reduced series, never a single
  interval, and a series too short to reduce reports `INCONCLUSIVE` rather than
  a verdict. *Breaks when:* `min_usable_intervals` is dropped or set to 1, so a
  scene that crashed after two seconds returns `OK` and a green run means
  nothing.
  *Test:* a `--selftest` fixture with two usable intervals on a gated metric
  exits 2 and reports `INCONCLUSIVE`; the same fixture with three exits 0.

- **INV-6** — the warm-up interval is discarded before reduction. *Breaks
  when:* `drop_warmup_intervals` is 0, so the first second — shader compilation,
  GPU clock ramp, and on the RT path an unsettled SVGF and TAAU — is averaged
  into the figure, and the baseline then encodes a cost that never recurs.
  *Test:* a `--selftest` fixture whose first interval is an order of magnitude
  above the rest reduces to the steady-state value, not to a value between the
  two.

- **INV-7** — the same scene run twice on an unchanged tree compares `OK` on
  every gated metric. *Breaks when:* the scene is not actually pinned — a live
  monster walks, a texture animates, the spot sits where `-freeze` does not
  hold what matters — so run-to-run variance exceeds `warn_pct` and every real
  comparison is drowned in noise.
  *Test:* B6 runs the full scene list twice with no tree change and compares;
  any metric not `OK` or `IMPROVED` identifies a scene that is not pinned, and
  that scene is fixed or dropped before the baseline is committed.

- **INV-8** — the comparator's reduction and verdict arithmetic is covered by
  `--selftest`, and `--selftest` needs no GPU and no engine. *Breaks when:* the
  fixtures are generated by running the tool being tested, so the test asserts
  the current behaviour rather than the intended one and a wrong threshold
  passes forever.
  *Test:* `--selftest` passes with `SDL_VIDEODRIVER=dummy` and no engine binary
  present; each fixture's expected verdict is stated in the fixture file as a
  literal, not computed.

- **INV-9** — a scene the machine could not run is reported as `SKIPPED` and
  never contributes a number. *Breaks when:* a launch fails, hangs or falls
  back to another tier and the runner reads whatever CSV happens to be on disk
  — including a previous run's — and reports it as this scene's result. The
  stale-pickup half is a recorded trap in this project's capture harness, and
  DOOM-0347 records `-rtverify` hanging silently forever on a non-Ultra config.
  *Test:* the runner deletes each scene's CSV path before launching and treats
  a missing file, a non-zero exit or a timeout as `SKIPPED`; a scene pointed at
  a deliberately invalid tier reports `SKIPPED` with the cause named, and no
  numeric row for it appears in the table.

## 9. Alternatives considered (and rejected)

**Build every counter and both workload kinds at once (approach A).** Rejected
in favour of phasing. The counters that already exist found this project's last
two real bottlenecks — the per-frame light rebuild of §3 decision 5, and the
serialised CPU/GPU frame DOOM-0074 then overlapped — with none of §4.9's
additions. Phasing gets the loop working
against proven counters and adds the new ones into plumbing that already
carries numbers.

**Put the whole harness inside the engine, no external tool (approach C).**
Real advantages: it would work identically on the Windows box with nothing
installed, and it could not drift from the counters. Rejected because it means
teaching the engine to parse a scene list and hold a baseline, and because the
comparison logic — median reduction, per-metric thresholds, seven verdicts — is
where the subtlety is, and it is far cheaper to test as a pure function. §4.7's
`--selftest` is that bet.

**Extra CSV columns instead of a sidecar.** Rejected: it repeats per-run facts
on every row and breaks byte-compatibility with the parser this design is
ported from, for no gain.

**Mean rather than median.** Rejected: one stalled interval — a compositor
hiccup, a background process — moves a mean enough to flip a verdict, and the
thing being defended against is a false `FAIL` that trains everyone to ignore
the tool.

**Log every frame rather than every interval.** Rejected for phase 1: it is a
different instrument, answering "what caused that stutter" rather than "where
does the time go", and it writes enough data to perturb the measurement. Raised
as §10 Q2.

**Reuse `-shotverify`'s golden-image machinery for timing.** Rejected: that
harness renders a fixed number of warm-up frames and exits, which is the right
shape for a look capture and the wrong one for a timing series — there is no
steady state to reduce.

## 10. Open questions

- **Q1 — which spots?** The scene list needs real coordinates, and they have to
  be read off a running engine (the menu prints the current spot as a `-warpto`
  line, per DOOM-0268). Four are wanted: the E1M1 open start, the goo room, a
  monster-heavy fight, and a wide outdoor sky view. Resolved during B4 by
  capturing them; blocks nothing before then.
- **Q2 — per-frame capture later?** A frame-by-frame log would answer "what
  caused that stutter", which interval averages cannot. Not filed, not in this
  build. Worth a roadmap item if stutter rather than throughput becomes the
  question.
- **Q3 — the Windows box.** `ssh wintest` is available. The runner could drive
  the `.exe` there, but that machine's GPU differs from the reference RX 6600,
  so it needs a separate baseline and its numbers can never be compared to the
  Linux ones. Deferred; the decision is whether a second baseline is worth
  maintaining.
- **Q4 — should the baseline be committed, or kept out of the tree?** Committed
  is assumed here, because a baseline nobody can see is a baseline nobody
  trusts. The cost is that it changes whenever the reference machine's driver
  does, and a driver-caused diff will look like a regression. Confirm with the
  user before B6 captures it.
- **Q5 — which metrics are gated?** `perf_gate.py`'s default allowlist is
  `frame,total`, `gpu,total`, `cpu,total`, `mem,gpu_mb`. Gating every pass would
  make the tool noisy; gating only the totals would miss a pass that doubled
  while another halved. Settled at B6 against the measured run-to-run variance
  from INV-7.

## 11. What checks this

| Claim | What catches it |
|---|---|
| INV-1 zero cost when absent | the golden capture compare at B2, plus the single-reference check |
| INV-2 conditions recorded and mismatches refused | the runner's rejection of a scene with no `render_scale`, plus a `--selftest` mismatch fixture |
| INV-3 every bucket named once | the static assertions (build-time), plus the distinct-name count per path at B2 |
| INV-4 completed run exits 0 | B3's three-part check, including the five demo fixtures |
| INV-5 no single-interval verdicts | `--selftest` fixture at B5 |
| INV-6 warm-up discarded | `--selftest` fixture at B5 |
| INV-7 run-to-run stability | B6's double run — **this is the one that decides whether the harness is worth anything**, and it can only be run on the reference GPU |
| INV-8 arithmetic covered | `--selftest` itself; its fixtures carry literal expected verdicts |
| INV-9 unreachable scene is SKIPPED | B4's deliberately-invalid-tier scene |
| §6's under-1 % overhead bound | **Partial:** measured once at B6 on one scene. Nothing re-measures it if a later change makes the emitter expensive |
| The scene list covers the situations that matter | **nothing** — a judgement call. A bottleneck in a situation no scene covers is invisible to this harness, and nothing will say so |
| The metric names stay meaningful as passes change | **Partial:** INV-3 catches a *missing* name; nothing catches a name that is still present and now describes different work. That is DOOM-0345 INV-7's residue and it is a hand read |
| The baseline still describes the reference machine | **nothing** — a driver or hardware change invalidates it silently (§10 Q4) |

## 12. Cross-doc impact

- **`docs/standards/performance.md`** — § Measuring names the `` \ `` key and
  `rb_profile` as the way to measure. Once this ships there is a second way,
  and the standard should name it. **No edit is made by this spec**: the tool
  does not exist yet, and a standard naming a tool nobody can run is worse than
  one naming only the tool that works. The edit belongs with B6.
- **`docs/specs/DOOM-0345-bloom-ray-traced.md` INV-7** — constrains the GPU
  timestamp slots. This spec adds no slot and moves none; §4.2 carries the
  constraint into the new name tables. No edit owed.
- **`docs/specs/DOOM-0011-volumetric-lighting.md` §6**, **DOOM-0181**,
  **DOOM-0183** — each defines a hand-measured perf gate. None is retro-fitted
  to this harness: their measurements are already taken and their features
  shipped, and re-basing a settled gate onto a new instrument would change what
  those specs claim was proven. Future gates should use the harness.
- **`CLAUDE.md`** — its house-rules summary names the test layout and the
  four-part regression check. A benchmark command is worth a line there once it
  exists. Deferred to B6 with the standard's edit.

## 13. Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
