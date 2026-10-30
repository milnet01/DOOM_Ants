// bot_steer_test.cpp — DOOM-0529: the demo bot's turning and moving arithmetic.
//
// Why this exists: a sign wrong here makes every run walk away from its goal, and a
// run still records and still replays, so nothing else would object.
//
// Contract:
//   1. A turn goes the way of the error and never passes BOT_MAX_TURN.
//   2. A turn eases: it closes only part of a small error.
//   3. A small error still turns by at least what a demo can store, and a
//      negligible one does not turn at all.
//   4. Moving at the way the player faces is all forward, and to its left is
//      negative side, matching a ticcmd.
//   5. Moving is limited to each axis's top speed, scaled by speed.
//   6. A ray measures the distance to a segment it crosses, and misses one that
//      is behind it or to the side.
//   7. A wall nearer than the margin pushes a move away from it; walls farther off,
//      and equal walls on both sides, change nothing.
//   8. The look turns from a near wall toward open space, and stays on the goal
//      when the goal's way is open.
#include <cmath>

#include "../bot_steer.h"
#include "check_util.h"

int main()
{
    // 1.
    check(BOT_TurnFor(30000) == BOT_MAX_TURN, "a large left error turns left at the cap");
    check(BOT_TurnFor(-30000) == -BOT_MAX_TURN, "a large right error turns right at the cap");

    // 2.
    check(BOT_TurnFor(2000) == 600, "a small error closes in part");

    // 3.
    check(BOT_TurnFor(300) == 256, "a turn is at least one stored step");
    check(BOT_TurnFor(-300) == -256, "in either direction");
    check(BOT_TurnFor(100) == 0, "a negligible error does not turn");

    // 4.
    const double pi = 3.14159265358979;
    int f = 0, s = 0;
    BOT_MoveFor(1.0, 1.0, 1.0, 50, 40, &f, &s);
    check(f == 50 && s == 0, "moving where it faces is all forward");
    BOT_MoveFor(pi / 2, 0.0, 1.0, 50, 40, &f, &s);
    check(f == 0 && s == -40, "moving to its left is negative side");
    BOT_MoveFor(-pi / 2, 0.0, 1.0, 50, 40, &f, &s);
    check(f == 0 && s == 40, "moving to its right is positive side");
    BOT_MoveFor(pi, 0.0, 1.0, 50, 40, &f, &s);
    check(f == -50 && s == 0, "moving behind it is all backward");

    // 5.
    BOT_MoveFor(0.0, 0.0, 0.5, 50, 40, &f, &s);
    check(f == 25, "speed scales the move");

    // 6.
    check(std::fabs(BOT_RayToSegment(0, 0, 1, 0, 100, -50, 100, 50) - 100.0) < 1e-6,
          "a ray measures the wall it crosses");
    check(BOT_RayToSegment(0, 0, -1, 0, 100, -50, 100, 50) < 0, "a wall behind it is missed");
    check(BOT_RayToSegment(0, 0, 1, 0, 100, 10, 100, 50) < 0, "a wall to the side is missed");

    // 7. Eight directions, the first along +x, each a quarter of pi further on.
    double open[8], near_east[8], corridor[8];
    for (int i = 0; i < 8; i++)
        open[i] = near_east[i] = corridor[i] = 1000.0;
    near_east[0] = 20.0;
    corridor[2] = corridor[6] = 20.0;
    const double goal = pi / 2;
    double m = BOT_RepelFrom(goal, open, 8, 64.0);
    check(std::fabs(m - goal) < 1e-6, "open space leaves the move alone");
    m = BOT_RepelFrom(goal, near_east, 8, 64.0);
    check(std::cos(m) < -0.1, "a near wall to the east pushes the move west");
    m = BOT_RepelFrom(0.0, corridor, 8, 64.0);
    check(std::fabs(std::sin(m)) < 1e-6 && std::cos(m) > 0, "equal walls on both sides cancel");

    // 8. Twelve directions around the goal, the first being the goal itself.
    double blocked[12], clear[12];
    for (int i = 0; i < 12; i++)
        blocked[i] = clear[i] = 800.0;
    blocked[0] = 40.0;
    check(BOT_BestLook(clear, 12) == 0, "an open way keeps the look on the goal");
    check(BOT_BestLook(blocked, 12) != 0, "a wall in the way turns the look away");
    blocked[1] = 800.0;
    int look = BOT_BestLook(blocked, 12);
    check(look == 1 || look == 11, "the look turns no farther from the goal than it must");

    return check_summary("bot_steer_test");
}
