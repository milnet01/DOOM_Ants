# DOOM-0431 — Repeatable benchmark harness

**Status:** Reviewed — `review-contract` loops 1–2 (see §13), stopped at the
spec cap of 2 with every verified finding fixed and no deferred tail. Ready to
implement; §13's loop-2 row records that the cap was a violent one and what
that means for re-gating.
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

**Scope:** two new engine flags, a new emitter module, and two new files under
`tools/`. The engine's rendering behaviour does not change.

**Solid and Ultra only, rasterised and ray-traced.** Classic is out of scope,
and the reason is the breakdown rather than the frame time. Every per-pass
counter this harness reads — `cpuMs`, `cpuBuildMs`, `profMs` — is a member of
the Vulkan backend's state struct in `r_vulkan.cpp`, and both prints sit in the
Vulkan present path. A Classic scene emits no `gpu` rows and no `cpu` rows,
which is the whole of "where is the time going".

**A renderer-independent frame rate does exist**, so the gap is narrower than
no measurement at all. `HU_DrawFPS` in `hu_stuff.c` counts every presented
frame and tracks the slowest in its window, drawing into `screens[0]` — its own
comment records that this is deliberate, so the figure shows under every
renderer. Whether a frame-time-only Classic scene earns its keep is §10 Q6.

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
sites moved"*. Each path writes its own number of timestamp slots;
`vkGetQueryPoolResults` is asked for exactly `nq` of them and returns
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
within its category. `ms` carries milliseconds — except on `mem` rows, where it
carries megabytes, which is Vestige's convention and is kept so its parser
transfers unmodified. `fps` is filled only on the `frame,total` row and left
blank elsewhere.

**`depth` is load-bearing, because these counters nest.** A `depth` of 0 is a
top-level cost; a `depth` of 1 is a *component* of the nearest depth-0 row
above it in the same category, already counted inside it. Three of the existing
families nest, and nothing in the counters themselves says so:

| Umbrella (depth 0) | Components (depth 1) |
|---|---|
| `gpu,denoise+post+taau` (`profMs[2]`) | `temporal`, `atrous`, `composite`, `bloom`, `tonemap`, `taau` (`profMs[4..9]`) |
| `cpu,present-total` (`cpuMs[4]`) | `fenceWait`, `record`, `submit` (`cpuMs[0]`, `[2]`, `[3]`) |
| `cpu,build` (`cpuMs[1]`) | `sprites`, `lights`, `reheight` (`cpuBuildMs[0..2]`) |

**`cpu,build` is the awkward one and the emitter must not assume its place.**
`cpuMs[4]` is `tEnd - tPresent0`, and `fenceWait`, `record` and `submit` are
all stamped inside that span. `build` is not reliably: `cpuMs[1]` is written
from two sites, and under DOOM-0074's build-ahead the build runs inside the
present span while on a non-build-ahead frame it does not. So `build` is
emitted at depth 0 and **whether it is already inside `present-total` is a
property of the frame, not of the schema**.

**Therefore no sum check.** Depth separates a component from its umbrella,
which is what the share column needs; it does not make the depth-0 rows a
partition, because `build` may or may not be one. A verification demanding the
rows add up would be unfalsifiable on one path and false on the other. §7 B4
checks what is actually true instead: no depth-0 row exceeds `frame,total`, and
every emitted `category,name` key appears exactly once.

**Rows are written once per completed interval, not per frame.** The engine
already accumulates and divides by a frame count once a second; the log writes
that same reduced value. A per-frame log would be a different instrument (§10
Q2) and would perturb what it measures.

**The two counter families reset at different places on different clocks, and
the emitter must not try to unify them.** `profMs` is zeroed inside
`if (g.gpuTimersInUse && g.gpuTimerPool)` on its own `profLastReport` clock,
near the top of the present; `cpuMs` and `cpuBuildMs` are zeroed inside
`if (cprof)` on `cpuLastReport`, at the end of it. The GPU block is not inside
the `cprof` gate.

So the emitter exposes **one flush entry point, called at each of those two
sites, inside that site's existing gate** — each call emitting its own family's
rows with its own `time_s`. A single call sited in the `cprof` block would read
`profMs` after the GPU block had already zeroed it that frame, and every `gpu`
row would log `0.00` with all its names present, passing every name check.
Differing interval boundaries between the families are harmless: the comparator
reduces each metric independently, keyed by name.

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

The raster table has one entry per bucket the raster arm fills (`profMs[0..5]`)
and the RT table one per RT bucket (`profMs[0..9]`). The emitter iterates the
table rather than the array, so a bucket with no name cannot be written and a
name with no bucket cannot be read.

**Tying the table to the readback needs a constant that does not exist yet.**
`nq` is a runtime local — `g.profRasterFrame ? 7u : 10u` — and it counts
*timestamps*, not buckets; the raster path derives one fewer bucket than it has
timestamps, the RT path the same number. So a static assertion written against
a hand-typed literal asserts nothing: widening the ternary and the `printf`
leaves it green while a mislabelled row ships, which is INV-3's *Breaks when*
exactly.

**So this spec requires the constants and rewrites `nq` to read them.** Name
`kRasterTimestamps` and `kRtTimestamps` beside the tables, assert each table's
length against the bucket count its path derives from them, and replace the
ternary's literals with the constants. Then adding a pass means touching a
constant, and any table that did not grow with it fails the build. INV-3.

**The names are the printed words, not new coinages** — a CSV row and a
terminal line then name the same thing. **But they are keyed by SLOT, and the
RT print order is not slot order.** `[rt_profile]` prints its arguments as
`profMs[0,1,2,4,5,6,8,9,7,3]`, so transcribing the printed line positionally
mislabels slots 3 and 7 — the blit charged to TAAU and TAAU to the blit. That
is the silent-absorption failure §2 cites DOOM-0345 INV-7 for, committed while
copying the very list that warns about it. So the mapping is written out:

| Path | Slot | Name | Depth |
|---|---|---|---|
| raster | 0 | `shadow` | 0 |
| raster | 1 | `scene` | 0 |
| raster | 2 | `ssao` | 0 |
| raster | 3 | `bloom` | 0 |
| raster | 4 | `composite` | 0 |
| raster | 5 | `hud` | 0 |
| RT | 0 | `sprites` | 0 |
| RT | 1 | `megakernel` | 0 |
| RT | 2 | `denoise+post+taau` | 0 |
| RT | 3 | `blit` | 0 |
| RT | 4 | `temporal` | 1 |
| RT | 5 | `atrous` | 1 |
| RT | 6 | `composite` | 1 |
| RT | 7 | `taau` | 1 |
| RT | 8 | `bloom` | 1 |
| RT | 9 | `tonemap` | 1 |

### The complete phase-1 metric set

Three artefacts must agree on these names — the emitter, the committed
baseline, and the comparator — so the set is closed here rather than left to
each to infer:

| Key | Source |
|---|---|
| `frame,total` | wall-clock interval frame time, with `fps` on the same row; the interval's frame count over its duration, which is what `[cpu_profile]`'s leading `%3d fps` already counts |
| `gpu,<slot name>` | one row per entry in the active path's slot table above |
| `gpu,total` | the sum of the **depth-0** slot rows for the path that ran. Depth 0, and **not itself a slot row** — every count of "distinct `gpu` names" in this spec means slot rows, and `gpu,total` is one more besides |
| `cpu,fenceWait` `cpu,build` `cpu,record` `cpu,submit` `cpu,present-total` | `cpuMs[0..4]` |
| `cpu,sprites` `cpu,lights` `cpu,reheight` | `cpuBuildMs[0..2]`, depth 1 under `cpu,build` |

**There is no `cpu,total`.** `cpu,present-total` is that number and a second
name for it would be a duplicate that drifts. The default gate list in §4.7 is
therefore `frame,total`, `gpu,total` and `cpu,present-total` — **not**
`perf_gate.py`'s `mem,gpu_mb`, which phase 1 never emits and which would sit
`MISSING` on every run.

### 4.3 The run sidecar

The CSV says what the numbers are. It does not say what they are *of*, and
`performance.md`'s comparison rule turns that omission into a wrong answer
rather than a missing one.

So `-benchlog <path>` also writes `<path>.meta.json` beside it. **Not at open
time** — `RB_Init` runs inside `D_DoomLoop`, long after `D_DoomMain` parses
argv, so a sidecar written where the flag is parsed cannot know which backend
will draw. It is written after `RB_Init`, or at the first flush:

```json
{
  "schema": 1,
  "scene": "e1m1-start",
  "map": "E1M1",
  "tier_requested": "solid",
  "tier_rendered": "solid",
  "rt_view": 0,
  "render_scale": 50,
  "width": 1920, "height": 1080,
  "gpu": "AMD Radeon RX 6600 (RADV NAVI23)",
  "workload": "spot"
}
```

**`tier_rendered` is read from `rendermode` after the backend has initialised,
and it is the field that makes INV-9 mechanical.** A tier can be overridden
between the config being read and the frame being drawn — DOOM-0203's pin is
one such override and there may be others, such as a machine with no working
ray tracing. Recording only what was *asked for* makes every such substitution
invisible, which is the whole failure INV-9 exists to catch. Recording both
makes it a comparison the runner performs rather than a hazard a reader has to
remember.

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

A scene's `tier` is what the run **asks for**, and it lands in the sidecar as
`tier_requested`; `tier_rendered` is what the engine actually drew, and §4.7
compares against that one. The scene list has no field for it, because nothing
in a request can state what a request will get.

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
| `seconds` (spot only) | `-benchtics <seconds × 35>` — see below |
| `tier`, `render_scale` | written into a throwaway `-config` file |
| `rt_view` | `-rtview N` on the command line, **not** the config |
| always, on a `spot` | `-freeze -inspect`, which need a `DEV=1` build |
| always | `-benchlog <tmp>/<scene>.csv -benchscene <name>` |

The tier goes through a generated config rather than a flag because that is how
the renderer is already selected: the key is `renderer` (`0` Classic, `1`
Ultra/RT3D, `2` Solid/Raster3D) and a wrong key is silently ignored and yields
the default. The runner writes the config it wants and never touches
`~/.doomrc`, so a benchmark run cannot disturb the user's settings and the
user's settings cannot disturb a benchmark run.

**A `spot` scene needs a stop condition, and `-bootsmoke` is not it.** That
flag exits 0 after N tics, which is the right shape — but it also pins the
tier. `D_DoomLoop` runs `if (bootsmoke_tics > 0) rendermode = RB_CLASSIC;`
before the backend is chosen, which is DOOM-0203 stopping a GPU-less CI runner
being dragged onto the Vulkan path. It overrides the `renderer` key the runner
just wrote. Every Solid and Ultra spot scene would render in Classic, exit 0,
and produce a file with no `gpu` rows under a sidecar claiming Ultra.

So this spec adds **`-benchtics N`**: exit 0 after N tics, pinning nothing.
DOOM-0203's pin is left exactly as it is — it guards a CI path that has no GPU,
and weakening it to serve a benchmark that only runs on a GPU would be the
wrong trade. A `demo` scene needs no stop flag; it ends when the demo does.

**`-freeze` and `-inspect` exist only in a `DEV=1` build.** The Makefile guards
`-DDOOM_DEV` behind `ifeq ($(DEV),1)`, and its own comment records why the
default is off: every release path runs a plain `make`, so a published build is
clean without anyone having to remember a flag. An ordinary binary ignores both
flags silently — monsters walk, the world is live, and INV-7's stability check
fails for a reason nothing reports. **The runner therefore refuses to run a
`spot` scene unless the launch printed `-inspect: monsters ignore you`**, which
`G_DevInspectFromArgv` emits. A warning is not enough: the run would still
produce numbers, and those numbers would be wrong.

**The ray-traced view is selected by `-rtview N`, not by the config.** Both
routes reach `rb_rtdebug` — `m_misc.c` carries `{"rt_view",&rb_rtdebug, 6}` —
but DOOM-0351 added the argv flag for exactly this job, and its comment records
that it "pins NOTHING ELSE, deliberately", unlike `-shotverify`, which drags a
whole canonical config with it. A benchmark wants one knob moved and nothing
else, so it takes the flag built for that.

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
sorted by cost descending, each with its share of `frame,total`. **Depth-1 rows
are indented under their umbrella and carry no share of their own** — they are
already counted inside the row above, so giving them a frame share would print
percentages that add to more than the frame:

```
e1m1-start   solid/raster  scale 50%   1920x1080   58.3 fps (17.15 ms)
  gpu   scene                5.20 ms   30.3%
  cpu   build                3.10 ms   18.1%
    cpu   lights               2.80 ms       (of build)
    cpu   reheight             0.27 ms       (of build)
  gpu   ssao                 1.80 ms   10.5%
  ...
  mem   gpu_mb             412.00 MB        —
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

**The gated set is `frame,total`, `gpu,total` and `cpu,present-total`** — §4.2
closes the metric namespace those names come from. `perf_gate.py`'s own default
list also carries `mem,gpu_mb`, which phase 1 does not emit; taking its list
unchanged would leave a gated metric `MISSING` on every run.

**A comparison across differing run conditions is refused, not scaled.** If a
scene's sidecar `render_scale`, `tier_rendered`, `rt_view` or resolution
differs from the baseline's for that scene, the metric is `SKIPPED` with the
mismatch named. Scaling one to the other would manufacture a number that was
never measured. **The comparison is against `tier_rendered`, not
`tier_requested`** — comparing what was asked for would compare two runs that
both asked for Ultra and one of which delivered Classic. INV-2.

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
| `zone_mb` | `mem` | zone memory **in use**, in MB — the heap size less `Z_FreeMemory()`, declared in `z_zone.h`. Not free memory: the comparator reads a rise as `FAIL`, so logging the free figure would report a leak as `IMPROVED` |
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
| `linuxdoom-1.10/d_main.c` | parse `-benchlog` / `-benchscene` / `-benchtics`; open and close the log; the `-benchtics` exit. DOOM-0203's `-bootsmoke` Classic pin is **not** touched |
| `linuxdoom-1.10/g_game.c` | `G_CheckDemoStatus`'s `-benchlog` exit path |
| `linuxdoom-1.10/Makefile` | one object; no test-wiring edit (tests are one file each) |

### The CSV schema

| Column | Type | Notes |
|---|---|---|
| `time_s` | float | seconds since the log opened, at interval end |
| `category` | enum | `frame` \| `gpu` \| `cpu` \| `mem` |
| `name` | string | metric name within the category |
| `depth` | int | `0` top-level, `1` a component of the depth-0 row above it in the same category (§4.1) |
| `ms` | float | milliseconds — **megabytes on `mem` rows** |
| `fps` | float | filled only on `frame,total`; blank elsewhere |

### The baseline schema

`schema`, a `reduction` block (`drop_warmup_intervals`, `min_usable_intervals`,
`stat`), a `thresholds` block (`warn_pct`, `fail_pct`, `min_abs_ms`,
`min_abs_mb`), a `gate` allowlist of `category,name` keys, and per scene the
recorded conditions (`render_scale`, `tier_rendered`, `rt_view`, `width`,
`height`) plus
each metric's reduced value — **and the tier field is `tier_rendered`, written
from the sidecar and never from the scene list.** Conditions are stored per
scene because §4.7 compares them before it compares numbers, and it compares
against what rendered. A baseline that recorded the *requested* tier would
launder a substitution: capture it on a run that silently fell back, and every
later correct run is measured against numbers from a renderer nobody asked
for.

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
the same scene run with the profiler on and the flag off. **The control arm is
reachable headlessly**: `rb_profile` has a config key, `rt_profile` in
`m_misc.c`, so the runner writes `rt_profile 1` into the throwaway config
rather than needing the `` \ `` keypress — which cannot be injected into a
Wayland client at all. The work per interval
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
- **B2 — wire it into the engine.** `-benchlog` / `-benchscene` / `-benchtics`,
  the two slot name tables with their static assertions, the sidecar. *Verify:*
  four things, and the last two are what INV-1 and INV-3 are actually caught
  by. (a) A `-warp 1 1 -benchtics 400 -benchlog /tmp/a.csv` run on a Solid
  config produces a CSV with at least three `frame,total` rows, and a sidecar
  whose `tier_rendered` is `solid` — **not Classic**, which is what the same
  run with `-bootsmoke` would have recorded. (b) The same run on an Ultra RT
  config emits one distinct `gpu` slot name per entry in that path's slot table,
  plus `gpu,total`; likewise the Solid one against its own table. (c) A golden
  capture of the scene with `-benchlog` absent compares byte-identical to a
  pre-feature build via `scripts/ab_diff.py`. (d) Every reference to the
  emitter's flush entry point in `r_vulkan.cpp` sits inside the gate that
  resets its counter family, and there is none outside them.
- **B3 — the `-timedemo` exit.** *Verify:* `-timedemo demo1 -benchlog` exits 0
  and prints to stdout; `-timedemo demo1` without the flag still exits non-zero
  with its `I_Error` line, and the demo fixtures still report the gametic
  counts `ROADMAP.md` records for them.
- **B4 — the scene list and the runner.** *Verify:* `tools/bench.py` runs every
  scene and prints a table in which **no depth-0 row exceeds `frame,total`** and
  **every emitted `category,name` key appears exactly once** — the key, not the
  bare name, which repeats legitimately across categories (`frame,total` and
  `gpu,total`); §4.1 says why this is the check and a sum is not; a scene whose `render_scale` is omitted is rejected
  by name rather than defaulted (INV-2); a `spot` scene launched against a
  non-`DEV` binary is refused rather than measured; and a scene pointed at a
  tier the machine cannot reach reports `SKIPPED` — with `tier_rendered`
  disagreeing with `tier_requested` as one of the ways that is detected — while
  the process still exits 0.
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
- **B8 — memory and load time.** `zone_mb`, `gpu_mb`, `level_load`. *Verify:*
  `zone_mb` tracks `Z_FreeMemory()` across a level change; `level_load` is
  non-zero and differs between a small map and a large one.

## 8. Invariants

- **INV-1** — with `-benchlog` absent the engine does no benchmark work and
  behaves identically. *Breaks when:* the emitter is called unconditionally and
  gated inside itself, so an inactive log still costs a call and a branch per
  frame — or worse, formats rows it then discards.
  *Test:* the emitter is never called unconditionally from the frame path —
  every reference to its flush entry point sits inside the gate that resets
  that counter family (§4.1 names both), and there is none outside those gates.
  A golden capture is the second clause and the weaker one: an emitter called
  every frame and self-gating internally renders identical pixels and passes a
  capture compare, so a test resting on the capture alone would record this
  invariant as held in exactly the case its *Breaks when* describes. B2 (c) and
  (d) run both clauses.

- **INV-2** — every measurement records the render scale, the tier **actually
  rendered**, the ray-traced view and the resolution it was taken at, and a
  comparison across any difference in those is refused rather than performed.
  *Breaks when:* a scene omits `render_scale` and the runner falls back to the
  config's value, so the workload changes when `~/.doomrc` does and two runs of
  "the same" scene are compared across different pixel counts — or the sidecar
  records the tier *requested* rather than the one that drew, which makes a
  silent substitution invisible to every later comparison. This is
  `performance.md`'s comparison rule, mechanised.
  *Test:* a scene entry with no `render_scale` is rejected by the runner with a
  named error, not defaulted; and a `--selftest` fixture pair differing only in
  the sidecar's `render_scale` yields `SKIPPED` with the mismatch named, never
  a numeric verdict.

- **INV-3** — every GPU bucket the active path fills is named exactly once, and
  a path's name-table length equals its bucket count. *Breaks when:* a pass is
  added, `nq` and the print widen, and the CSV name table does not — so one
  bucket's time is written under its neighbour's name and every row still looks
  plausible. This is DOOM-0345 INV-7's failure in a new list.
  *Test:* the static assertion tying each table's length to the bucket count its
  path derives from `kRasterTimestamps` / `kRtTimestamps` fails the build when a
  bucket is added without a name; and a run of each path emits one distinct
  `gpu` slot name per entry in that path's table, with `gpu,total` the only
  further `gpu` row.

- **INV-4** — a benchmark run that completed exits 0, and a non-zero exit means
  a real failure. *Breaks when:* the `-timedemo` result keeps its `I_Error`
  route under `-benchlog`, so the runner records every successful demo scene as
  a crash — or the exit path is changed unconditionally and the demo-fixture
  regression check, which reads that `I_Error` line, stops finding it.
  *Test:* `-timedemo demo1 -benchlog <tmp>` exits 0; `-timedemo demo1` without
  the flag exits non-zero and still prints `timed N gametics in M realtics`;
  the demo fixtures still report the gametic counts `ROADMAP.md` records.

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
  any metric not `OK` identifies a scene that is not pinned, and that scene is
  fixed or dropped before the baseline is committed. **`IMPROVED` fails this
  test like any other verdict** — on an unchanged tree it means run-to-run
  variance crossed the threshold, which is this invariant's *Breaks when*
  rather than good news.

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
  a missing file, a non-zero exit or a timeout as `SKIPPED`; **and a run whose
  sidecar `tier_rendered` differs from its `tier_requested` is `SKIPPED` on
  that ground alone**, which is the clause that catches a substitution the
  process exit code cannot see. A scene pointed at a deliberately invalid tier
  reports `SKIPPED` with the cause named, and no numeric row for it appears in
  the table. **The substitution arm needs a fixture that renders**, and
  `-bootsmoke` is not one: Classic emits no rows at all, so that run is caught
  by the missing-or-empty arm and the `tier_rendered` clause — the one an exit
  code cannot see — is never exercised. Its fixture is therefore a
  `--selftest` sidecar pair whose `tier_rendered` and `tier_requested` disagree
  over an otherwise complete CSV, asserted `SKIPPED`. The `-bootsmoke` run is
  still worth keeping as the fixture for the empty-CSV arm.

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

- **Q1 — which spots?** The scene list needs real coordinates, read off a
  running engine (the menu prints the current spot as a `-warpto` line, per
  DOOM-0268). Wanted: the E1M1 open start, the goo room, a monster-heavy fight,
  and a wide outdoor sky view. Resolved during B4 by capturing them; blocks
  nothing before then.
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
- **Q5 — should any per-pass metric be gated as well as the three totals?**
  §4.7 gates `frame,total`, `gpu,total` and `cpu,present-total`. Gating every
  pass would make the tool noisy; gating only the totals misses a pass that
  doubled while another halved. Settled at B6 against the measured run-to-run
  variance from INV-7 — a pass whose variance is well inside `warn_pct` is a
  candidate, one that is not never will be.
- **Q6 — a frame-time-only Classic scene?** Classic has no per-pass counters,
  so it can never answer "where is the time going". It can answer "how fast",
  because `HU_DrawFPS` already measures frame rate under every renderer. A
  Classic scene would therefore carry `frame,total` and nothing else. Whether
  that is worth a scene, or is misleading sitting in a table whose other rows
  are breakdowns, is unresolved. Not filed.

## 11. What checks this

| Claim | What catches it |
|---|---|
| INV-1 zero cost when absent | B2 (d), the single-reference-inside-the-gate check — the load-bearing half; B2 (c)'s golden capture alone cannot falsify it |
| INV-2 conditions recorded and mismatches refused | B4's rejection of a scene with no `render_scale`, plus a `--selftest` mismatch fixture at B5 |
| INV-3 every bucket named once | the static assertions (build-time), plus B2 (b)'s distinct-name count per path |
| INV-4 completed run exits 0 | B3's three-part check, including the five demo fixtures |
| INV-5 no single-interval verdicts | `--selftest` fixture at B5 |
| INV-6 warm-up discarded | `--selftest` fixture at B5 |
| INV-7 run-to-run stability | B6's double run — **this is the one that decides whether the harness is worth anything**, and it can only be run on the reference GPU |
| INV-8 arithmetic covered | `--selftest` itself; its fixtures carry literal expected verdicts |
| INV-9 unreachable scene is SKIPPED | B4's deliberately-invalid-tier scene, and INV-9's `-bootsmoke` substitution fixture — the arm that exits 0 with a full CSV |
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
| 1 | 2026-09-12 | 3 | 3 | 3 | 2 | 2 | **10 verified, 0 dismissed, all 10 fixed.** Three lanes independently found the same first defect, and it was the one that would have wasted the most time: §4.5 used `-bootsmoke N` as the spot-scene stop condition, but `D_DoomLoop` runs `if (bootsmoke_tics > 0) rendermode = RB_CLASSIC;` before the backend is chosen (DOOM-0203, guarding a GPU-less CI runner), so **every Solid and Ultra spot scene would have rendered in Classic**, exited 0, and written a CSV with no `gpu` rows under a sidecar claiming Ultra. INV-9 was written to catch "falls back to another tier" and could not have, because nothing recorded which tier drew. Fixed by adding `-benchtics N` (exit after N tics, pinning nothing; DOOM-0203's pin deliberately untouched) and by splitting the sidecar's `tier` into `tier_requested` / `tier_rendered`, which makes INV-9 mechanical and gives it a fixture that exits 0 with a full CSV and must still be `SKIPPED`. One lane alone found the widest defect: § Scope claimed all three tiers were measurable, but `cpuMs`, `cpuBuildMs` and `profMs` are members of the Vulkan backend's own state struct and there is no frame timer anywhere outside `r_vulkan.cpp` — a Classic scene emits nothing at all, so Classic is now out of scope with §10 Q6 carrying the gap. Two lanes found that `-freeze` and `-inspect` exist only under `ifeq ($(DEV),1)`, so the runner now refuses a spot scene whose launch did not print `-inspect: monsters ignore you` rather than measuring a live world. Two found B4 demanding rows that sum to `frame,total` while §2 of this same document says `profMs[4..9]` nest inside `profMs[2]`; `depth` now marks components, and the sum check is replaced by one that can pass — no depth-0 row exceeds the total, every name appears once — because `cpu,build`'s containment varies with DOOM-0074's build-ahead path and no sum is true on both. Two found `zone_kb` written into a column the schema defines as megabytes, which would also have applied the 1 MB absolute floor to a kilobyte figure. One found the baseline's gate list naming `gpu,total`, `cpu,total` and `mem,gpu_mb` while nothing specified emitting any of them; §4.2 now closes the phase-1 metric namespace and the gated set is `frame,total` / `gpu,total` / `cpu,present-total`. One found §11 crediting checks to a B2 whose verify clause ran neither. One found INV-1's own test unable to falsify its *Breaks when*: a self-gating emitter called every frame renders identical pixels and passes a golden-capture compare, so the test now names the per-frame emit call site. Promoted from a lane's open question: §4.2 listed the RT names in **print** order (`profMs[0,1,2,4,5,6,8,9,7,3]`) while requiring a slot-keyed table, so a positional transcription would swap the blit and TAAU labels — the exact silent-absorption failure the section cites DOOM-0345 INV-7 for. The mapping is now written out slot by slot. Two open questions resolved clean and are not in the tally: `rb_profile` **is** reachable headlessly (config key `rt_profile`, now named in §6, since a `` \ `` keypress cannot be injected under Wayland), and §2's `uncapped`-excludes-`demoplayback` claim is correct. |
| 2 | 2026-09-12 | 3 | 2 | 5 | 1 | 1 | **9 verified, 0 dismissed, all 9 fixed. A violent cap: 6 of the 9 landed on text loop 1 itself wrote.** All three lanes independently found the same one — loop 1 required the emitter's flush to be "referenced exactly once, inside the existing `if (cprof)` gate", but the two counter families reset at different sites on different clocks (`profMs` inside `if (g.gpuTimersInUse && g.gpuTimerPool)` on `profLastReport`, near the top of the present; `cpuMs`/`cpuBuildMs` inside `if (cprof)` on `cpuLastReport`, at its end), and the GPU block is not inside the `cprof` gate. A single call sited there would read `profMs` after the GPU block had zeroed it, logging every `gpu` row as `0.00` with all its names present — passing loop 1's own name check. The emitter now has one flush entry point called at both sites, each inside its own gate, and INV-1's test is "never called unconditionally" rather than a reference count. Two lanes found that `gpu,total` is itself a `gpu` row, so loop 1's "exactly ten distinct `gpu` names" made a correct build fail its own gate; counts of `gpu` names now mean slot rows, with `gpu,total` named as one more besides. One lane found loop 1's INV-9 substitution fixture impossible: a `-bootsmoke` run renders Classic, which this spec's own Scope says emits nothing, so it is caught by the empty-CSV arm and the `tier_rendered` clause — the one an exit code cannot see — is never exercised; that arm is now a `--selftest` sidecar pair over a complete CSV. One found the baseline still storing `tier` from the scene list while §4.7 compares `tier_rendered`, which would have laundered a substitution into the baseline itself. Three were pre-existing rather than loop-1 collateral. INV-7 read "compares `OK` on every gated metric" while its own test accepted `IMPROVED`; on an unchanged tree `IMPROVED` means variance crossed the threshold, which is that invariant's *Breaks when*. `nq` is a runtime local counting *timestamps*, not a compile-time bucket count, so loop 1's static assertion could not be written as described — the spec now requires `kRasterTimestamps` / `kRtTimestamps` and rewrites `nq` to read them. And `Z_FreeMemory()` returns **free** bytes, so a phase-2 leak would have been reported as `IMPROVED`; `zone_mb` now logs memory in use. One finding was the orchestrator's, while settling an open question two lanes raised: loop 1's Scope claimed "there is no frame-time or FPS counter anywhere outside that file", and `HU_DrawFPS` in `hu_stuff.c` is exactly that — it counts every presented frame and draws into `screens[0]` so the figure shows under every renderer, deliberately. Classic stays out of scope because it has no per-pass breakdown, but the stated reason was false and §10 Q6 now asks the real question. Also folded in this loop, at the user's instruction and not from a lane: `/mnt/Games/CLAUDE.md`'s prose rules, which forbid counts that go stale. Every census of timestamps, buckets, rows, spots and demo fixtures is replaced by a name — and the `gpu,total` finding above is a worked example of why, since that count was wrong the moment a synthesised row joined the category. |
| 2-plan | 2026-09-12 | 0 | 0 | 0 | 0 | 0 | **No reviewer was dispatched against this document.** Fold-back from the gate on `docs/plans/DOOM-0431-bench-harness.md`, where a lane found that §4.3 contradicted itself: the sidecar was written "at open time" while `tier_rendered` was read "after the backend has initialised". `RB_Init` runs inside `D_DoomLoop`, long after `D_DoomMain` parses argv, so both cannot hold — and the losing branch is the silent one, because a sidecar written at the argv parse records the config's renderer in both fields, they always agree, and the mismatch clause added in loop 1 never fires. §4.3 now says the sidecar is written after `RB_Init` or at the first flush. **Not re-gated.** Rule 14's test says an amendment changing direction for work still to come re-arms the gate, and this one does. §At the cap governs instead: loop 2 was a violent cap, so a third run on this document would start at loop 1 against text whose last two loops were each repairing the one before. The document routes to implementation, which is the better third reviewer, and this clause is now consistent with the plan the build will follow. |
