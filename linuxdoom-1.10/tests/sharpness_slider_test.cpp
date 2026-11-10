// sharpness_slider_test.cpp — the Sharpness slider's mapping to RCAS's lobe scale.
//
// Why this exists: DOOM-0599's sharpening adds shimmer at every strength, so it ships
// as a player's choice that starts at Off. Off must mean the pass does not run.
//
// Contract:
//   1. Off (0) gives scale 0, which the engine reads as "skip the pass".
//   2. Every step right is stronger, and the ends are the measured strengths
//      (step 1 = 2 stops, step 5 = 0.4 stops).
//   3. A value outside the slider's range acts as its nearest end.
#include <cmath>

#include "../rb_sharpness.h"
#include "check_util.h"

int main()
{
    // 1.
    check(RB_SharpnessScale(RB_SHARPNESS_MIN) == 0.0f, "Off gives no sharpening");

    // 2.
    for (int s = 1; s < RB_SHARPNESS_MAX; s++)
        check(RB_SharpnessScale(s + 1) > RB_SharpnessScale(s), "a step right is stronger");
    check(std::fabs(RB_SharpnessScale(1) - std::exp2(-2.0f)) < 1e-6f, "step 1 is 2 stops");
    check(std::fabs(RB_SharpnessScale(RB_SHARPNESS_MAX) - std::exp2(-0.4f)) < 1e-6f, "step 5 is 0.4 stops");

    // 3.
    check(RB_SharpnessScale(-5) == RB_SharpnessScale(RB_SHARPNESS_MIN), "below the range is Off");
    check(RB_SharpnessScale(99) == RB_SharpnessScale(RB_SHARPNESS_MAX), "above the range is the right end");

    return check_summary("sharpness_slider_test");
}
