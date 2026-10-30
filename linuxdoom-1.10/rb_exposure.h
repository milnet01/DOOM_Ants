// rb_exposure.h — the Brightness slider's EV. No Vulkan or DOOM headers, so
// tests/exposure_slider_test.cpp includes it as is.
#ifndef RB_EXPOSURE_H
#define RB_EXPOSURE_H

#define RB_EXPOSURE_MIN		0
#define RB_EXPOSURE_MAX		15
#define RB_EXPOSURE_DEFAULT	10

// The tonemap's gain is 1 / (2^EV * 1.2), so a larger EV is a darker picture.
static inline float RB_ExposureEV (int slider)
{
    int s = slider < RB_EXPOSURE_MIN ? RB_EXPOSURE_MIN
	  : (slider > RB_EXPOSURE_MAX ? RB_EXPOSURE_MAX : slider);
    return -1.5f - 0.25f * (float)(s - RB_EXPOSURE_DEFAULT);
}

#endif
