// fps_window_test.cpp — DOOM-0507: the FPS counter reads true on evenly paced frames.
//
// The bug: the counter timed frames in whole milliseconds and truncated its
// division, so perfectly paced 60 Hz frames showed "60/58": a low below the
// floor on a run that never dropped below it.
//
// Contract (fps_window.h):
//   1. Evenly paced frames read their own rate as both avg and low, at 60, 144
//      and 35 Hz.
//   2. One slow frame in a window sets low from that frame alone.
//   3. A gap of a whole window or more (a level load) starts a fresh window, and
//      the frame after it is not counted, so the next average is not inflated.
//   4. The first frame publishes nothing.
#include <cstdint>

#include "../fps_window.h"
#include "check_util.h"

namespace {

// Frame i of an evenly paced run at hz, to the microsecond, starting at t0.
int64_t at(int64_t t0, int hz, int i)
{
    return t0 + (int64_t)i * 1000000 / hz;
}

// Feeds frames first..last of a run at hz and returns the last published window.
fps_window_t run(fps_window_t w, int64_t t0, int hz, int first, int last, int* published)
{
    for (int i = first; i <= last; i++)
        *published += FPS_Sample(&w, at(t0, hz, i));
    return w;
}

} // namespace

int main()
{
    const int64_t T0 = 5000000;   // any start; not 0, to show 0 has no meaning

    // 1. Evenly paced.
    {
        const int rates[] = {60, 144, 35};
        for (int hz : rates)
        {
            fps_window_t w = {};
            int published = 0;
            w = run(w, T0, hz, 0, hz * 3, &published);
            check(published >= 5, "an even run publishes about twice a second");
            check_eq_int(w.fps, hz, "avg on an even run is the frame rate");
            check_eq_int(w.low, hz, "low on an even run is the frame rate");
        }
    }

    // 2. One 50 ms frame among 60 Hz frames: the window holding it reads low 20.
    {
        fps_window_t w = {};
        int published = 0;
        w = run(w, T0, 60, 0, 10, &published);
        const int64_t hitch = at(T0, 60, 10) + 50000;
        FPS_Sample(&w, hitch);
        int i = 1;
        while (!FPS_Sample(&w, at(hitch, 60, i)))
            i++;
        check_eq_int(w.low, 20, "low is the rate of the one 50 ms frame");
        check_eq_int(w.fps, 56, "avg: 29 gaps over 516.7 ms, the hitch counted once");
    }

    // 3. A two-second stall, then 60 Hz again.
    {
        fps_window_t w = {};
        int published = 0;
        w = run(w, T0, 60, 0, 60, &published);
        const int64_t back = at(T0, 60, 60) + 2000000;
        published = 0;
        w = run(w, back, 60, 0, 31, &published);
        check_eq_int(published, 1, "one window after the stall");
        check_eq_int(w.fps, 60, "the stall did not drag or inflate the average");
        check_eq_int(w.low, 60, "the stall is not reported as a hitch");
    }

    // 4. The first frame.
    {
        fps_window_t w = {};
        check_eq_int(FPS_Sample(&w, T0), 0, "the first frame publishes nothing");
    }

    return check_summary("fps_window_test");
}
