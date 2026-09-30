// device_pick.h — DOOM-0225: which Vulkan device the 3D renderer opens.
//
// The renderer needs a device that can draw to the window and that has the
// four descriptor-indexing features its bindless materials use. The picker
// used to take the first device that could present and then abort if that one
// lacked the features, even when another device on the same machine had both.
// RB_VulkanProbe already judged the tiers on "present and bindless", so the
// menu could offer Solid on a machine where the picker then chose the wrong
// card.
//
// The rule is here, not inline in r_vulkan.cpp, so tests/device_pick_test.cpp
// can hold it against multi-GPU cases no development machine has.
#ifndef DEVICE_PICK_H
#define DEVICE_PICK_H

// One entry per enumerated device, in enumeration order.
typedef struct
{
    unsigned char	present;	// has a queue family that can draw to the window
    unsigned char	bindless;	// has the four descriptor-indexing features
    unsigned char	rt;		// advertises the ray-tracing extensions
} rb_devcaps_t;

// The index of the device to open, or -1 when none can present.
//   1. the first that can present, has bindless and has ray tracing;
//   2. else the first that can present and has bindless;
//   3. else the first that can present, so the caller can name what it lacks.
static inline int RB_PickDevice (const rb_devcaps_t* d, int n)
{
    int	i, usable = -1, presenting = -1;

    for (i = 0; i < n; i++)
    {
	if (!d[i].present)
	    continue;
	if (presenting < 0)
	    presenting = i;
	if (!d[i].bindless)
	    continue;
	if (usable < 0)
	    usable = i;
	if (d[i].rt)
	    return i;
    }
    return usable >= 0 ? usable : presenting;
}

#endif // DEVICE_PICK_H
