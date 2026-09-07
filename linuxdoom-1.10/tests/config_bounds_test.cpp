// config_bounds_test.cpp — DOOM-0383: what a hand-edited ~/.doomrc may say.
//
// Two decisions, both on the path from a line of that file to a global the
// engine uses as an array index:
//
//   ConfigParseInt -- did the value parse at all? Vanilla ignored sscanf's
//   return into an uninitialised `parm`, so a malformed line kept the previous
//   line's number, or stack garbage on the first line of the file.
//
//   ConfigClamp -- is the value in range? Ten key_* settings index
//   gamekeydown[NUMKEYS] every tic, mouseb_*/joyb_* index small arrays, and the
//   two volumes reach an I_Error that refuses to start the game.
//
// m_misc.c cannot be unit tested -- M_LoadDefaults wants a config file and the
// whole globals table -- so the decisions it makes live in config_bounds.h and
// are tested here, exactly as the other five *_bounds.h headers are.
#include <cstdio>

#include "../config_bounds.h"
#include "check_util.h"

int main()
{
    int v;

    // ---- ConfigParseInt: values that are numbers ----
    check(ConfigParseInt("0", &v) != 0 && v == 0, "plain zero parses");
    check(ConfigParseInt("15", &v) != 0 && v == 15, "a plain decimal parses");
    check(ConfigParseInt("-1", &v) != 0 && v == -1,
          "a negative parses -- -1 is the legal \"unbound\" mouse/joy button");
    check(ConfigParseInt("0x1d", &v) != 0 && v == 0x1d,
          "hex parses, which is how key bindings are written");
    check(ConfigParseInt("0xFF", &v) != 0 && v == 255, "upper-case hex parses");

    // ---- ConfigParseInt: values that are not ----
    //
    // The regression this fix is for. On every one of these the old code left
    // `parm` holding whatever it held before -- so the check is not only that
    // the call is refused, but that it did not leave a value behind.
    v = 0x5eed;
    check(ConfigParseInt("banana", &v) == 0, "a word is refused");
    check_eq_int(v, 0, "a refused parse leaves 0, never the previous value");

    v = 0x5eed;
    check(ConfigParseInt("", &v) == 0, "an empty value is refused");
    check_eq_int(v, 0, "an empty value leaves 0");

    v = 0x5eed;
    check(ConfigParseInt(nullptr, &v) == 0, "a null value is refused");
    check_eq_int(v, 0, "a null value leaves 0");

    v = 0x5eed;
    check(ConfigParseInt("0x", &v) == 0,
          "a bare 0x has no digits and is refused, not read as zero");
    check_eq_int(v, 0, "a bare 0x leaves 0");

    v = 0x5eed;
    check(ConfigParseInt("  ", &v) == 0, "whitespace alone is refused");
    check_eq_int(v, 0, "whitespace alone leaves 0");

    // ---- ConfigClamp ----
    //
    // Volumes: the menu's range is 0..15, and anything outside 0..127 kills the
    // engine at startup by way of S_SetSfxVolume's I_Error.
    check_eq_int(ConfigClamp(0, 0, 15, 15), 0, "the bottom of a range is kept");
    check_eq_int(ConfigClamp(15, 0, 15, 15), 15, "the top of a range is kept");
    check_eq_int(ConfigClamp(8, 0, 15, 15), 8, "a middling value is kept");
    check_eq_int(ConfigClamp(16, 0, 15, 15), 15, "one past the top falls back");
    check_eq_int(ConfigClamp(-1, 0, 15, 15), 15, "one below the bottom falls back");
    check_eq_int(ConfigClamp(999, 0, 15, 15), 15,
                 "the value that would have refused to launch falls back");

    // Keys: 0..NUMKEYS-1, since they index gamekeydown[NUMKEYS].
    const int kMaxKey = 255;
    check_eq_int(ConfigClamp(0, 0, kMaxKey, 0x1d), 0, "key 0 is a legal index");
    check_eq_int(ConfigClamp(kMaxKey, 0, kMaxKey, 0x1d), kMaxKey,
                 "the last key is a legal index");
    check_eq_int(ConfigClamp(kMaxKey + 1, 0, kMaxKey, 0x1d), 0x1d,
                 "one past the end of gamekeydown falls back to the default");
    check_eq_int(ConfigClamp(-1, 0, kMaxKey, 0x1d), 0x1d,
                 "a negative key index falls back");

    // Mouse and joystick buttons: the arrays are deliberately offset by one so
    // that -1 is legal, so the low bound is -1 and NOT 0.
    check_eq_int(ConfigClamp(-1, -1, 2, 0), -1,
                 "-1 is a legal mouse button -- it means unbound");
    check_eq_int(ConfigClamp(2, -1, 2, 0), 2, "the last mouse button is legal");
    check_eq_int(ConfigClamp(3, -1, 2, 0), 0,
                 "one past the mouse array falls back");
    check_eq_int(ConfigClamp(-2, -1, 2, 0), 0,
                 "one below the offset falls back");
    check_eq_int(ConfigClamp(3, -1, 3, 0), 3,
                 "the joystick array is one longer, so 3 is legal there");

    // A range of exactly one value still works -- guards against a > vs >=
    // slip in either bound.
    check_eq_int(ConfigClamp(7, 7, 7, 0), 7, "a single-value range keeps it");
    check_eq_int(ConfigClamp(8, 7, 7, 0), 0, "a single-value range rejects 8");
    check_eq_int(ConfigClamp(6, 7, 7, 0), 0, "a single-value range rejects 6");

    return check_summary("config_bounds");
}
