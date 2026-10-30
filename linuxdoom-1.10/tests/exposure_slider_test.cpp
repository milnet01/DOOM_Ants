// exposure_slider_test.cpp — the Brightness slider's mapping to the tonemap's EV.
//
// Why this exists: DOOM-0096 mapped a larger slider value to a larger EV, and the
// tonemap's gain is 1 / (2^EV * 1.2), so moving Brightness right darkened the picture.
// Nothing read the direction, so it stayed that way until a sweep showed it.
//
// Contract:
//   1. Every step right makes the picture brighter.
//   2. The default position gives the EV it always gave, so a default picture does not
//      change.
//   3. A value outside the slider's range acts as its nearest end.
#include <cmath>

#include "../rb_exposure.h"
#include "check_util.h"

static double gain(int slider)
{
    return 1.0 / (std::pow(2.0, RB_ExposureEV(slider)) * 1.2);
}

int main()
{
    // 1.
    for (int s = RB_EXPOSURE_MIN; s < RB_EXPOSURE_MAX; s++)
        check(gain(s + 1) > gain(s), "a step right is brighter");

    // 2.
    check(RB_ExposureEV(RB_EXPOSURE_DEFAULT) == -1.5f, "the default keeps its EV");

    // 3.
    check(RB_ExposureEV(-5) == RB_ExposureEV(RB_EXPOSURE_MIN), "below the range is the left end");
    check(RB_ExposureEV(99) == RB_ExposureEV(RB_EXPOSURE_MAX), "above the range is the right end");

    return check_summary("exposure_slider_test");
}
