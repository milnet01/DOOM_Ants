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

    return check_summary("bot_steer_test");
}
