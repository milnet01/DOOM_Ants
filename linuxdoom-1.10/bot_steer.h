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

// How far along a ray, from (ox,oy) with unit direction (dx,dy), it crosses the segment
// from a to b, or negative if it does not.
static inline double BOT_RayToSegment (double ox, double oy, double dx, double dy,
				       double ax, double ay, double bx, double by)
{
    const double ex = bx - ax, ey = by - ay;
    const double den = dx * ey - dy * ex;
    double t, u;
    if (den > -1e-9 && den < 1e-9)
	return -1.0;
    t = ((ax - ox) * ey - (ay - oy) * ex) / den;
    u = ((ax - ox) * dy - (ay - oy) * dx) / den;
    return (t >= 0.0 && u >= 0.0 && u <= 1.0) ? t : -1.0;
}

// A move toward goal_rad, bent away from walls nearer than margin. dist holds the clear
// distance in n directions evenly round the circle, the first along angle 0. A wall at
// the margin pushes not at all, and one touching pushes twice as hard as the goal pulls.
static inline double BOT_RepelFrom (double goal_rad, const double* dist, int n, double margin)
{
    const double step = 2.0 * 3.14159265358979 / n;
    double x = cos (goal_rad), y = sin (goal_rad);
    int i;
    for (i = 0; i < n; i++)
    {
	if (dist[i] < margin)
	{
	    const double push = 2.0 * (margin - dist[i]) / margin;
	    x -= cos (i * step) * push;
	    y -= sin (i * step) * push;
	}
    }
    return atan2 (y, x);
}

// Which of n directions to look along, where the first is the goal and the rest go evenly
// round the circle. Open space scores up to a room's length, and turning from the goal
// costs up to most of the score, so the look leaves the goal only to get off a wall.
#define BOT_LOOK_ROOM	600.0
static inline int BOT_BestLook (const double* dist, int n)
{
    double best_score = -1.0;
    int best = 0, i;
    for (i = 0; i < n; i++)
    {
	const int off = i <= n / 2 ? i : n - i;
	const double room = dist[i] < BOT_LOOK_ROOM ? dist[i] : BOT_LOOK_ROOM;
	const double score = room * (1.0 - 0.7 * (2.0 * off / n));
	if (score > best_score)
	{
	    best_score = score;
	    best = i;
	}
    }
    return best;
}

#endif
