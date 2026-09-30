// device_pick_test.cpp — DOOM-0225: which Vulkan device the 3D renderer opens.
//
// Why this exists: on a machine with two GPUs the renderer could open a
// present-capable device that lacks the bindless (descriptor-indexing) features
// and then abort, while another present-capable device had them. RB_PickDevice is
// the pure rule; PickPhysicalAndDevice must consult it (bounds_wiring_test).
//
// The rule, in order of preference:
//   1. the first device with present AND bindless AND rt
//   2. else the first with present AND bindless
//   3. else the first with present (the caller reports the missing feature)
//   4. else -1
#include <cstdio>

#include "../device_pick.h"
#include "check_util.h"

static rb_devcaps_t dev(int present, int bindless, int rt)
{
    rb_devcaps_t d;
    d.present  = (unsigned char)present;
    d.bindless = (unsigned char)bindless;
    d.rt       = (unsigned char)rt;
    return d;
}

int main()
{
    // ---- Nothing to pick. ----
    check_eq_int(RB_PickDevice(nullptr, 0), -1, "DOOM-0225: n=0 with NULL picks nothing");
    {
        rb_devcaps_t d[1] = { dev(1, 1, 1) };
        check_eq_int(RB_PickDevice(d, 0), -1, "DOOM-0225: an empty list picks nothing");
    }
    {
        rb_devcaps_t d[2] = { dev(0, 0, 0), dev(0, 0, 0) };
        check_eq_int(RB_PickDevice(d, 2), -1, "DOOM-0225: no present-capable device picks nothing");
    }
    {
        rb_devcaps_t d[2] = { dev(0, 1, 1), dev(0, 1, 0) };
        check_eq_int(RB_PickDevice(d, 2), -1,
                     "DOOM-0225: bindless and rt without present never win");
    }

    // ---- One device. ----
    {
        rb_devcaps_t d[1] = { dev(1, 1, 0) };
        check_eq_int(RB_PickDevice(d, 1), 0, "DOOM-0225: one present+bindless device is picked");
    }

    // ---- The defect: a present-only device must not beat a bindless one. ----
    {
        rb_devcaps_t d[2] = { dev(1, 0, 0), dev(1, 1, 0) };
        check_eq_int(RB_PickDevice(d, 2), 1,
                     "DOOM-0225: [present-only, present+bindless] picks the bindless one");
    }
    {
        rb_devcaps_t d[2] = { dev(1, 0, 1), dev(1, 1, 0) };
        check_eq_int(RB_PickDevice(d, 2), 1,
                     "DOOM-0225: rt without bindless does not win over present+bindless");
    }
    {
        rb_devcaps_t d[2] = { dev(0, 1, 1), dev(1, 1, 0) };
        check_eq_int(RB_PickDevice(d, 2), 1,
                     "DOOM-0225: a bindless+rt device that cannot present is skipped");
    }

    // ---- Preference order. ----
    {
        rb_devcaps_t d[2] = { dev(1, 1, 0), dev(1, 1, 1) };
        check_eq_int(RB_PickDevice(d, 2), 1,
                     "DOOM-0225: present+bindless+rt beats present+bindless");
    }
    {
        rb_devcaps_t d[2] = { dev(1, 1, 1), dev(1, 1, 1) };
        check_eq_int(RB_PickDevice(d, 2), 0, "DOOM-0225: on a tie the first device wins");
    }
    {
        rb_devcaps_t d[2] = { dev(1, 0, 0), dev(1, 0, 0) };
        check_eq_int(RB_PickDevice(d, 2), 0,
                     "DOOM-0225: with no bindless device, the first present one is picked (to report it)");
    }
    {
        rb_devcaps_t d[3] = { dev(0, 0, 0), dev(1, 0, 1), dev(1, 0, 0) };
        check_eq_int(RB_PickDevice(d, 3), 1,
                     "DOOM-0225: fallback 3 takes the first present device, skipping one that cannot present");
    }

    return check_summary("device_pick");
}
