# Testing Standard

Tests exist to stop a change from silently breaking something that already
worked. That matters doubly here: we are modernising a 1997 engine and evolving
its renderer, so it is easy to fix one thing and quietly change another. A test
is how we prove the fix landed *and* nothing else moved.

## The harness

Unit tests live in `linuxdoom-1.10/tests/` as `*_test.cpp` files. The rules the
harness relies on:

- **One file, self-contained.** A test single-TU `#include`s the `.c` it tests
  (e.g. `mus2mid_test.cpp` does `#include "../mus2mid.c"`) and exercises it
  directly. No WAD, no GPU, no Vulkan link — pure CPU logic.
- **Adding a test is one file.** `make test` auto-discovers every
  `tests/*_test.cpp` (`TEST_SRCS=$(wildcard tests/*_test.cpp)` in the Makefile).
  Drop the file in — **no Makefile edit**.
- **Convention:** `main()` runs the cases with `assert`, prints
  `"<name>: all passed\n"`, and returns non-zero if any case fails. A non-zero
  exit from any test fails the whole `make test` target.
- Built at `-O2` (these are CPU-only and loop hard; the optimisation is free
  speed since test code never ships).

Run them with `make test` from `linuxdoom-1.10/`. It is cheap and incremental —
unchanged tests are skipped.

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

## Tests are a gate

`make test` runs as the first step of a release (`packaging/release.sh`) — a
broken test never gets tagged or published. Keep tests fast and honest: a test
that is slow, flaky, or asserts the wrong thing is worse than no test.
