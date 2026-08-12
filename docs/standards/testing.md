# Testing Standard

Tests exist to stop a change from silently breaking something that already
worked. That matters doubly here: we are modernising a 1997 engine and evolving
its renderer, so it is easy to fix one thing and quietly change another. A test
is how we prove the fix landed *and* nothing else moved.

## The harness

Unit tests live in `linuxdoom-1.10/tests/` as `*_test.cpp` files. The rules the
harness relies on:

- **One file, self-contained.** A test single-TU `#include`s the unit it tests and
  exercises it directly — the header where the logic is header-only
  (`save_bounds_test.cpp` takes `../save_bounds.h`), the `.c` where it is not
  (`mus2mid_test.cpp` takes `../mus2mid.c`). No WAD, no GPU, no Vulkan link —
  pure CPU logic. Factoring testable logic into a header so a test can reach it
  without linking the engine is the usual move, not a workaround.
- **Adding a test is one file.** `make test` auto-discovers every
  `tests/*_test.cpp` (`TEST_SRCS=$(wildcard tests/*_test.cpp)` in the Makefile).
  Drop the file in — **no Makefile edit**.
- **Convention:** `main()` runs the cases through `check()` from
  `tests/check_util.h`, then `return check_summary("<name>")`, which prints
  `"<name>: all passed\n"` and returns non-zero if any case failed. A non-zero
  exit from any test fails the whole `make test` target.
- **Never `assert()`.** It is a macro: `-DNDEBUG` deletes it *and any call
  inside it*, so `assert(mus2mid(...) != 0)` becomes a test that runs nothing
  and still prints "all passed" — reproduced during the 2026-07-26 test audit.
  It also aborts the process on the first failure, hiding every later case in
  the same binary. `check()` has neither problem. (`static_assert` is fine —
  it is a compile-time construct and `NDEBUG` does not touch it.)
- **Fixtures come from the repo, not the host.** Load a bundled asset (e.g.
  `rb_text_test.cpp` bakes the embedded `assets/Oxanium-SemiBold.ttf`) or a
  committed file addressed via `DOOM_TESTS_ROOT` — the absolute path of
  `linuxdoom-1.10/` that the Makefile passes to every test — so the test works
  from any working directory. A test must **never** skip itself when something
  is missing: a skip that exits 0 is indistinguishable from a pass.
- Built at `-O2` (these are CPU-only and loop hard; the optimisation is free
  speed since test code never ships).
- **Known limit of the single-TU pattern:** tests are `.cpp`, so a `.c` unit
  under test is recompiled as C++23 (`g++`) while the shipping object is built
  as gnu11 (`gcc`). The logic is the same, the language rules are not — these
  tests validate the algorithm, not the exact object the game links. Anything
  that turns on C-versus-C++ semantics needs the boot smoke or a play-test.

Run them with `make test` from `linuxdoom-1.10/`. Incremental in the build only:
an unchanged test is not recompiled, but every test binary is executed on every
invocation, so a slow test costs its full runtime every time.

## When a test is required

- **Every bug fix gets a regression test first.** Reproduce the bug with a
  failing test, confirm it fails for the right reason, *then* fix. Two wins in
  one move: it proves the diagnosis, and it locks the bug out for good. The
  `mus2mid` fix from the 2026-07-23 hardening pass (`tests/mus2mid_test.cpp`) is
  the model here. The netgame and WAD-bounds fixes from that same pass shipped
  *without* their regression tests — a known gap, tracked as `DOOM-0240`, not a
  precedent to copy.
- **Skip only for the mechanical.** A typo or a one-line off-by-one where writing
  a test would be pure ceremony can go without — judgement, not a loophole.

## What belongs in a unit test vs. not

- **Unit-testable:** parsers, bounds and index math, table/format conversions,
  anything with a clear input → output and no display. This is where regression
  tests go.
- **Not unit-testable here:** how the world *looks* (renderer output) and how it
  *feels* (gameplay). Those are covered two other ways:
  - **Golden images** — `tests/goldens/` holds reference renders (e.g.
    `e1m1_ultra_rt.png`), compared with the in-engine `-shotcompare` gate
    (DOOM-0202) to catch an unintended visual change.
  - **Play-test sign-off** — a human confirms feel, lighting, and performance on
    real hardware (the RX 6600 reference GPU). Noted in the roadmap item.

## Savegames: the round-trip is the gate, and ASAN cannot see it

A unit test can pin the bounds arithmetic (`tests/save_bounds_test.cpp`), but it
cannot prove the on-disk layout is unchanged. The gate binds on any edit to the
savegame layout, which lives in **two** files: the `P_Archive*` / `P_UnArchive*`
bodies in `p_saveg.c`, and the fixed header and trailing `0x1d` marker that
`G_DoSaveGame` / `G_DoLoadGame` write and read in `g_game.c`. Changing a field in
either shifts the cursor for every later read and makes existing `.dsg` files
unreadable. Two things to know before touching them, both established while
shipping DOOM-0255:

- **Make the save with the pre-change binary and load it with the patched one.**
  There is no command-line save, so drive it under gdb: break on `P_Ticker`, set
  `savegameslot`, `strcpy` a `savedescription`, then `call (void)G_DoSaveGame()`.
  Both runs go headless the usual way (`SDL_VIDEODRIVER=dummy`, a throwaway
  `-config` forcing `renderer 0`).

  **Assert the world, not just the exit code.** A rejected save is loud, not
  silent: `G_DoLoadGame` returns without loading anything, `-loadgame` has already
  left `gameaction` at `ga_loadgame` so the title screen never starts either, and
  the boot smoke ticks a level that was never loaded until it dies with SIGSEGV in
  `P_PlayerThink` — exit 139, reproduced 2026-08-12 with a hand-edited version
  field. But exit 0 still only proves *a* world ticked, not that it came from the
  file. Save on a map the reload is *not* started on and check `gamemap` under
  gdb: `-warp 1 -loadgame 5` observing `gamemap == 5` can only have come from the
  save. `save_end - save_p` is the sharper check — exactly 1 at that point, the
  unread `0x1d` marker, proving the file was consumed to the byte with no field
  drift.

  Truncating that same file to a range of lengths is the other half — each cut
  should be refused by name, not read through.
- **AddressSanitizer does not see an overrun that stays inside the zone.**
  `Z_Malloc` sub-allocates one large block (`mainzone`, from `I_ZoneBase`), which
  ASAN sees as a single valid allocation, so a read past a savegame's end but
  still inside that block is invisible to it. Measured on the *pre-DOOM-0255*
  engine — the only configuration where that unbounded read still exists — where
  ASAN reported nothing at the read itself and the damage surfaced as an unrelated
  downstream crash. Against the current engine those same truncations are refused
  by name before any read, so the lesson is for the next unbounded zone-allocated
  parser rather than for savegames: do not read a clean ASAN run as evidence that
  a zone-allocated buffer is bounded. Two practical notes: DOOM-0342's startup
  global-array overflow aborts the process before the load path, so
  `-fsanitize-recover=address` with `ASAN_OPTIONS=halt_on_error=0` is needed to
  get past it; and the same reasoning should hold for WAD lumps, which are
  zone-allocated too, but that was not tested.

## Tests are a gate

`make test` runs as the first step of a release (`packaging/release.sh`) — a
broken test never gets tagged or published. Keep tests fast and honest: a test
that is slow, flaky, or asserts the wrong thing is worse than no test.
