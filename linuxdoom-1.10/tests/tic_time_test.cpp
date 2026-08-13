// tic_time_test.cpp — DOOM-0350: the game clock must never step backwards.
//
// The bug: I_GetTime computed its sub-second term as `tv_nsec * (long)TICRATE`.
// `long` is 64 bits on Linux and 32 bits on Windows, so on Windows the product
// overflowed once tv_nsec passed ~61.4 ms and wrapped negative. The returned tic
// count sawtoothed instead of climbing, going backwards about eight times a
// second. wipe_doMelt's `while (ticks--)` then received a negative tick count
// and counted down through ~2 billion iterations — the screen-melt hang that
// froze roughly one Windows launch in forty.
//
// What this test does and does not catch, stated plainly. It pins the arithmetic
// contract: monotonic, in range, and exact at the overflow boundary. Built on a
// 64-bit-`long` host the ORIGINAL expression also satisfies it, so this file
// would not have caught the original bug on Linux — the 200-run Windows soak is
// what proved the fix. What it does catch is any future narrowing of this
// arithmetic to a 32-bit type, since `int` is 32 bits on every host we build for;
// case 4 fails immediately under one. That is the regression worth locking: the
// defect was a width assumption, not a formula error.
#include <cstdint>

#include "../tic_time.h"
#include "check_util.h"

namespace {

const int TICRATE = 35;
const int64_t NS = 1000000000LL;

// The exact input captured from the hung Windows run:
//   [c] I_GetTime BACKWARDS 37 -> 33 (sec=1213354 nsec=61356700 base=1213353)
// 61356700 * 35 = 2147484500, which is INT32_MAX + 853. In 32-bit arithmetic
// that wraps to -2147482796, and dividing by 1e9 gives -2 instead of +2 — so the
// call returned 35 + (-2) = 33 where the previous call had returned 37.
const int64_t kHangNsec = 61356700;

void case_boundary_value ()
{
    // One whole second past the base, plus the nanosecond remainder that hung.
    check (I_TicsFrom (1, kHangNsec, TICRATE) == 37,
	   "the captured hang input yields 37, not the wrapped 33");

    // The sub-second term alone, either side of where 32-bit arithmetic breaks.
    check (I_TicsFrom (0, kHangNsec - 1, TICRATE) == 2, "just under the wrap: 2");
    check (I_TicsFrom (0, kHangNsec, TICRATE) == 2,     "just over the wrap: 2");
}

void case_range ()
{
    check (I_TicsFrom (0, 0, TICRATE) == 0, "start of a second is tic 0");
    check (I_TicsFrom (0, NS - 1, TICRATE) == TICRATE - 1,
	   "end of a second is the last sub-second tic");
    check (I_TicsFrom (1, 0, TICRATE) == TICRATE,
	   "one second on is exactly TICRATE tics");
}

// The invariant itself: sweep a full second and assert the result never drops.
// The step is deliberately not a divisor of the tic period, so the samples do
// not land only on tic boundaries.
void case_monotonic_within_a_second ()
{
    int worst = 0;
    int prev = I_TicsFrom (0, 0, TICRATE);
    for (int64_t nsec = 7919 ; nsec < NS ; nsec += 7919)
    {
	const int now = I_TicsFrom (0, nsec, TICRATE);
	if (now - prev < worst)
	    worst = now - prev;
	prev = now;
    }
    check (worst == 0, "no backward step anywhere inside one second");
}

// And across second boundaries, which is where the whole-seconds term and the
// sub-second term have to hand over cleanly.
void case_monotonic_across_seconds ()
{
    int worst = 0;
    int prev = I_TicsFrom (0, 0, TICRATE);
    for (int64_t secs = 0 ; secs < 5 ; secs++)
    {
	for (int64_t nsec = 0 ; nsec < NS ; nsec += 999983)
	{
	    const int now = I_TicsFrom (secs, nsec, TICRATE);
	    if (now - prev < worst)
		worst = now - prev;
	    prev = now;
	}
    }
    check (worst == 0, "no backward step across five seconds");
}

// A negative delta is what actually hung the engine, so assert the shape the
// melt depends on: successive samples of a rising clock never produce one.
void case_no_negative_delta_reaches_the_melt ()
{
    bool anyNegative = false;
    int prev = I_TicsFrom (0, 0, TICRATE);
    for (int64_t nsec = 61000000 ; nsec < 62000000 ; nsec += 101)
    {
	const int now = I_TicsFrom (0, nsec, TICRATE);
	if (now - prev < 0)
	    anyNegative = true;
	prev = now;
    }
    check (!anyNegative,
	   "no negative tic delta around the 32-bit overflow point");
}

}  // namespace

int main ()
{
    case_boundary_value ();
    case_range ();
    case_monotonic_within_a_second ();
    case_monotonic_across_seconds ();
    case_no_negative_delta_reaches_the_melt ();
    return check_summary ("tic_time");
}
