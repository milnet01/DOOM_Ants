// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	DOOM-0350: the monotonic-clock -> game-tic arithmetic, split out of
//	I_GetTime so it can be unit tested. Same reason save_bounds.h exists:
//	i_system.c itself is not testable (clock_gettime, process-lifetime
//	statics), but the decision it makes is a pure function of two integers.
//
//	The invariant this owns: given a monotonically increasing clock, the
//	result never decreases.
//
//	That was false on Windows, and only on Windows. `long` is 32 bits there
//	(LLP64) and 64 bits on Linux (LP64), so `tv_nsec * (long)TICRATE`
//	overflowed once tv_nsec passed ~61.4 ms -- 61356700 * 35 = 2147484500,
//	just over INT32_MAX -- and wrapped NEGATIVE. The sub-second term then
//	sawtoothed through roughly [-2, +2] about eight times a second instead of
//	climbing 0..34 once, so I_GetTime stepped backwards several times a
//	second. Any `now - then` delta could come out negative; wipe_doMelt's
//	`while (ticks--)` turned one such delta into a ~2-billion-iteration spin,
//	hanging about 2-3% of Windows launches on a black screen.
//
//	Everything below is therefore explicitly 64-bit. Do not reintroduce
//	`long` here, whose width is a platform detail.
//
//-----------------------------------------------------------------------------

#ifndef __TIC_TIME__
#define __TIC_TIME__

#include <stdint.h>

//
// I_TicsFrom
//
// Tics elapsed, from whole seconds since the base instant plus the nanosecond
// remainder of the current second. `ticrate` is a parameter rather than TICRATE
// so this header stays free of doomdef.h and the test can pin the boundary
// arithmetic directly.
//
static inline int I_TicsFrom (int64_t secs, int64_t nsec, int ticrate)
{
    return (int)(secs * (int64_t)ticrate
		 + nsec * (int64_t)ticrate / 1000000000LL);
}

#endif
