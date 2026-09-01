// rb_argparse_test.cpp — a malformed -rtview / -rippletime value must leave the
// setting at its default, not silently parse as 0.
//
// Rationale. Found by check-code (finding F12): r_backend.c:358 reads `-rtview`
// with a bare `atoi(myargv[p + 1])`, and r_vulkan.cpp:9414 reads `-rippletime`
// with a bare `(float)atof(myargv[p + 1])`. Neither C function has a way to
// report failure — `atoi("banana")` and `atof("banana")` both return 0, exactly
// as if the player had typed "-rtview 0" / "-rippletime 0" on purpose. r_backend.c
// happens to treat 0 as a legal rtview index, so a typo silently jumps to a
// different debug view than the default the player expected. Range checking (is
// 0..6, is >= 0.0) does not catch this, because 0 is a value *inside* both valid
// ranges — the corruption is invisible to the caller's own guard. This is the
// same failure `docs/specs/DOOM-0026-renderer-backend.md` INV-3 already names
// for the renderer-backend config value: "never errors, never a blank screen" —
// a bad value must resolve to the default, not to whatever byte pattern the
// parser happened to produce.
//
// Scope. `rb_argparse.h` is a fresh, deliberate extraction of the parsing logic
// at those two call sites into one header with one test — the call sites
// themselves are not touched by this change (they still call atoi()/atof()
// directly) and are out of scope here; wiring them to RB_ParseIntArg /
// RB_ParseFloatArg is separate follow-up work. This file locks the CONTRACT the
// header must satisfy once that wiring lands, not the current call-site
// behaviour. Range checking (0..6 for rtview, >= 0.0 for rippletime) is each
// call site's own business per the task description and is deliberately not
// exercised here — these two functions only decide well-formed vs malformed.
//
// Regression history. rb_argparse.h was extracted verbatim from the two call
// sites, so as of this commit it still ENCODES the defect on purpose: both
// helpers are thin atoi()/atof() wrappers that always return 1 (see the header's
// own comment). That means every MALFORMED case below is expected to fail until
// the header is fixed to reject non-numeric input and leave *out untouched.
//
// Invariants:
//   INV-1: A string that is wholly a valid integer (optional leading '-', at
//          least one digit, nothing else) is accepted: the parser returns 1 and
//          writes the value to *out.
//   INV-2: A string that is not wholly a valid integer — empty, whitespace-only,
//          non-numeric, or numeric with trailing garbage (e.g. "3abc", which
//          atoi() silently reads as 3) — is refused: the parser returns 0 and
//          *out is left untouched.
//   INV-3: A string that is wholly a valid float (optional leading '-', digits,
//          at most one '.') is accepted: the parser returns 1 and writes the
//          value to *out.
//   INV-4: A string that is not wholly a valid float — empty, whitespace-only,
//          non-numeric, a lone sign, multiple decimal points, or numeric with
//          trailing garbage (e.g. "3.5xyz", which atof() silently reads as 3.5)
//          — is refused: the parser returns 0 and *out is left untouched.
//
// Build/run: `make test` (from linuxdoom-1.10/). No WAD or GPU needed.
#include "check_util.h"

#include "../rb_argparse.h"

namespace {

// check_eq_int (check_util.h) only takes long; add the float sibling so a
// mismatch prints what was actually written to *out, not just that it differed.
[[maybe_unused]] static void check_eq_float(float got, float want, const char* what)
{
    if (got != want)
    {
        std::printf("  FAIL: %s (got %f, want %f)\n", what, (double)got, (double)want);
        g_failures++;
    }
}

// Sentinel values a malformed call must NOT disturb. Chosen well away from 0 —
// the value atoi()/atof() silently produce for garbage input — so a helper that
// (bug) always writes *out shows up as a mismatch against the sentinel rather
// than by accident matching it.
const int   kIntSentinel   = 777;
const float kFloatSentinel = 777.5f;

void case_int_well_formed_accepted()
{
    int out = 0;

    out = kIntSentinel;
    check(RB_ParseIntArg("0", &out) == 1, "INV-1: \"0\" is accepted");
    check_eq_int(out, 0, "INV-1: \"0\" parses to 0");

    out = kIntSentinel;
    check(RB_ParseIntArg("42", &out) == 1, "INV-1: \"42\" is accepted");
    check_eq_int(out, 42, "INV-1: \"42\" parses to 42");

    out = kIntSentinel;
    check(RB_ParseIntArg("-17", &out) == 1, "INV-1: \"-17\" is accepted");
    check_eq_int(out, -17, "INV-1: \"-17\" parses to -17");
}

// The headline case: exactly the failure mode the bug report names. A typo'd
// -rtview value must not silently become 0 -- it must be refused so the caller
// keeps rb_rtdebug at its default.
void case_int_malformed_refused()
{
    const char* bad[] = { "", "   ", "banana", "3abc", "12.5", "--5", "5-5" };
    for (const char* s : bad)
    {
        char msg[128];
        int out = kIntSentinel;
        int rc = RB_ParseIntArg(s, &out);
        const char* shown = s ? s : "(null)";
        std::snprintf(msg, sizeof msg,
                      "INV-2: malformed int \"%s\" is refused (rc == 0, got rc=%d)", shown, rc);
        check(rc == 0, msg);
        std::snprintf(msg, sizeof msg,
                      "INV-2: malformed int \"%s\" leaves *out untouched", shown);
        check_eq_int(out, kIntSentinel, msg);
    }
}

void case_float_well_formed_accepted()
{
    float out = 0.0f;

    out = kFloatSentinel;
    check(RB_ParseFloatArg("0", &out) == 1, "INV-3: \"0\" is accepted");
    check_eq_float(out, 0.0f, "INV-3: \"0\" parses to 0.0");

    out = kFloatSentinel;
    check(RB_ParseFloatArg("3.5", &out) == 1, "INV-3: \"3.5\" is accepted");
    check_eq_float(out, 3.5f, "INV-3: \"3.5\" parses to 3.5");

    out = kFloatSentinel;
    check(RB_ParseFloatArg("-2.25", &out) == 1, "INV-3: \"-2.25\" is accepted");
    check_eq_float(out, -2.25f, "INV-3: \"-2.25\" parses to -2.25");
}

// The other headline case: a typo'd -rippletime value must not silently become
// 0.0, which the r_vulkan.cpp guard (>= 0.0f) would then happily accept.
void case_float_malformed_refused()
{
    const char* bad[] = { "", "   ", "banana", "3.5xyz", "1.2.3", "-", "xyz" };
    for (const char* s : bad)
    {
        char msg[128];
        float out = kFloatSentinel;
        int rc = RB_ParseFloatArg(s, &out);
        const char* shown = s ? s : "(null)";
        std::snprintf(msg, sizeof msg,
                      "INV-4: malformed float \"%s\" is refused (rc == 0, got rc=%d)", shown, rc);
        check(rc == 0, msg);
        std::snprintf(msg, sizeof msg,
                      "INV-4: malformed float \"%s\" leaves *out untouched", shown);
        check_eq_float(out, kFloatSentinel, msg);
    }
}

}  // namespace

int main()
{
    case_int_well_formed_accepted();
    case_int_malformed_refused();
    case_float_well_formed_accepted();
    case_float_malformed_refused();
    return check_summary("rb_argparse");
}
