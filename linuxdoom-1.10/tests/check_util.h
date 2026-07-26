// check_util.h — the one assertion helper every tests/*_test.cpp uses.
//
// Why not assert()? Two reasons, both found by the 2026-07-26 test audit:
//
//   1. assert() is erased by -DNDEBUG. Where the call under test sat INSIDE the
//      assert -- assert(mus2mid(...) != 0) -- the call itself vanished with it,
//      so the binary still printed "all passed" and exited 0 having verified
//      nothing. Reproduced: building mus2mid_test.cpp with -DNDEBUG passed with
//      zero checks executed.
//   2. assert() aborts on the first failure, hiding every later case in the same
//      binary. rb_materials_test runs ten independent cases; one regression in
//      the first hid the other nine until it was fixed and the suite re-run.
//
// check() has neither problem: it is an ordinary function (no macro, nothing to
// compile away), it records the failure and keeps going, and the process exit
// code comes from check_summary() at the end of main().
//
// Usage:
//     #include "check_util.h"
//     int main() { check(x == y, "x equals y"); return check_summary("mytest"); }
#ifndef DOOM_TESTS_CHECK_UTIL_H
#define DOOM_TESTS_CHECK_UTIL_H

#include <cstdio>

static int g_failures = 0;

// Record one condition. `what` is the behaviour being claimed, phrased so the
// FAIL line reads as a sentence about the bug.
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  FAIL: %s\n", what); g_failures++; }
}

// Same, but prints the two values that disagreed -- worth it on the handful of
// comparisons where "which value did it actually see?" is the whole diagnosis.
// [[maybe_unused]]: not every test needs the value-printing form, and -Wall
// would otherwise warn in the ones that don't.
[[maybe_unused]] static void check_eq_int(long got, long want, const char* what)
{
    if (got != want)
    {
        std::printf("  FAIL: %s (got %ld, want %ld)\n", what, got, want);
        g_failures++;
    }
}

// End-of-main epilogue: print the verdict and return the process exit code.
static int check_summary(const char* label)
{
    if (g_failures == 0)
        std::printf("%s: all passed\n", label);
    else
        std::printf("%s: FAILED -- %d check(s) failed.\n", label, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#endif
