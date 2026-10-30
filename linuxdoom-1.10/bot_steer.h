// bot_steer.h — DOOM-0529: the arithmetic of the demo bot's hands.
//
// What makes a run read as played rather than scripted is here: turning eases into
// its target and tops out at a rate a mouse hand reaches, and moving is a direction
// in the world whatever way the player is facing, so it can run past what it shoots at.
//
// No DOOM headers, so tests/bot_steer_test.cpp includes it as is.
#ifndef BOT_STEER_H
#define BOT_STEER_H

#include <math.h>

// The most a tic may turn, in ticcmd angleturn units (65536 = a full circle), about
// 390 degrees a second at 35 tics. Fast for a hand, and not an instant snap.
#define BOT_MAX_TURN	2300

// The ticcmd turn that closes part of err, a signed angle in the same units. A demo
// stores a turn to 256 units, so a nonzero turn below that would be lost.
static inline int BOT_TurnFor (int err)
{
    int turn = err * 3 / 10;
    if (turn > BOT_MAX_TURN)
	turn = BOT_MAX_TURN;
    if (turn < -BOT_MAX_TURN)
	turn = -BOT_MAX_TURN;
    if (turn > -256 && turn < 256)
	turn = err > 128 ? 256 : (err < -128 ? -256 : 0);
    return turn;
}

// A world direction and a speed from 0 to 1, as ticcmd forward and side moves for a
// player facing facing_rad, each limited to its own top speed. Side is positive to
// the player's right, as a ticcmd's is.
static inline void BOT_MoveFor (double world_rad, double facing_rad, double speed,
				int max_forward, int max_side, int* forward, int* side)
{
    double rel = world_rad - facing_rad;
    *forward = (int)lround (cos (rel) * speed * max_forward);
    *side = (int)lround (-sin (rel) * speed * max_side);
}

#endif
