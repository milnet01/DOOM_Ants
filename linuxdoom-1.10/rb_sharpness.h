// rb_sharpness.h — the Sharpness slider's RCAS strength (DOOM-0599). No Vulkan or DOOM
// headers, so tests/sharpness_slider_test.cpp includes it as is.
#ifndef RB_SHARPNESS_H
#define RB_SHARPNESS_H

#include <math.h>

#define RB_SHARPNESS_MIN	0	// Off: the pass does not run
#define RB_SHARPNESS_MAX	5

// rcas.comp's lobe scale, exp2(-stops). Step 1 is 2 stops and step 5 is 0.4 stops, the
// two ends measured at DOOM-0599 (shimmer grew about as much as sharpness at each).
static inline float RB_SharpnessScale (int slider)
{
    int s = slider < RB_SHARPNESS_MIN ? RB_SHARPNESS_MIN
	  : (slider > RB_SHARPNESS_MAX ? RB_SHARPNESS_MAX : slider);
    return s == 0 ? 0.0f : exp2f(-(2.4f - 0.4f * (float)s));
}

#endif
