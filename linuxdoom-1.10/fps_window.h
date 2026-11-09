// fps_window.h — the FPS counter's arithmetic (DOOM-0046, DOOM-0403, DOOM-0507).
//
// Split out of HU_DrawFPS so it can be unit tested, the same reason tic_time.h
// exists: the clock read is not testable, but the decision is a pure function of
// the timestamps it is fed.
//
// The counter shows "avg/low". avg is the mean frame rate over a window of about
// half a second. low is the rate implied by the slowest single frame in that
// window, so a lone hitch shows even when the average hides it.
//
// Timestamps are microseconds. Whole milliseconds made a perfectly paced 60 Hz
// frame (16.67 ms) read 16 or 17 ms, so the counter showed 60/58 on frames that
// never dropped below 60. Both figures round to nearest for the same reason:
// truncating 1000000 / 16667 gives 59.

#ifndef __FPS_WINDOW__
#define __FPS_WINDOW__

#include <stdint.h>

#define FPS_WINDOW_US	500000	// publish twice a second

typedef struct
{
    int		started;	// 0 until the first frame
    int64_t	start_us;	// when this window began
    int64_t	prev_us;	// the previous frame's timestamp
    int64_t	worst_us;	// slowest single frame this window
    int		frames;		// frames presented since start_us
    int		fps;		// last published average
    int		low;		// last published low: 1e6 / worst_us
} fps_window_t;

// Feed one presented frame. Returns 1 when fps and low were just published.
//
// DOOM-0403: a gap of a whole window or more is not a hitch. It is a period with
// no rendering in it, such as a level load, and averaging across it describes
// nothing. Such a gap starts a fresh window instead.
static inline int FPS_Sample (fps_window_t* w, int64_t now_us)
{
    int64_t elapsed;

    // The first frame, and the frame after a stall, start a window and are not
    // counted in it: frames counts the gaps between frames, not the frames.
    if (!w->started || now_us - w->prev_us >= FPS_WINDOW_US)
    {
	w->started = 1;
	w->start_us = w->prev_us = now_us;
	w->frames = 0;
	w->worst_us = 0;
	return 0;
    }

    if (now_us - w->prev_us > w->worst_us)
	w->worst_us = now_us - w->prev_us;
    w->prev_us = now_us;
    w->frames++;

    elapsed = now_us - w->start_us;
    if (elapsed < FPS_WINDOW_US)
	return 0;

    w->fps = (int)(((int64_t)w->frames * 1000000 + elapsed / 2) / elapsed);
    w->low = w->worst_us > 0 ? (int)((1000000 + w->worst_us / 2) / w->worst_us) : w->fps;
    w->frames = 0;
    w->worst_us = 0;
    w->start_us = now_us;
    return 1;
}

#endif
