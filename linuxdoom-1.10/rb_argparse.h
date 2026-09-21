#ifndef RB_ARGPARSE_H
#define RB_ARGPARSE_H
/* Parsing for the renderer's own -rtview / -rippletime command-line arguments.
 *
 * Both call sites used atoi()/atof(), which report no error: a value that is not
 * a number parses as 0, and 0 is a value both settings accept. So `-rtview hgh`
 * silently selected view 0 and `-rippletime fast` silently set the ripple period
 * to zero -- in each case overriding the default the user would otherwise have
 * had, with nothing printed. `atoi("3abc")` is the same trap one step quieter: it
 * returns 3, so a typo'd argument lands on a plausible wrong value.
 *
 * The contract added here is only that a value which is not WHOLLY a number is
 * refused, leaving the caller's default in place. It deliberately does not exit:
 * DOOM-0026 INV-3 requires an unusable setting to resolve to a working one and
 * never to an error or a blank screen. Range is the caller's business -- each
 * flag has its own -- so these helpers answer "is this a number?" and nothing
 * more.
 *
 * Two things the caller's range test CANNOT be the business of, both for the
 * same reason as the atoi() trap above: the wrong value lands inside the valid
 * range, so the guard passes.
 *
 * Overflow (DOOM-0405). BOTH the errno test and the int-bounds comparison below
 * are load-bearing, and neither may replace the other -- each is blind exactly
 * where the other sees. On LP64 a value between INT_MAX and LONG_MAX fits a long
 * fine, so nothing sets errno and only the comparison catches it. On LLP64 that
 * same value saturates, so the comparison cannot see it and only errno does.
 * "3000000000" is the input that separates them and the test carries it.
 *
 * The bound is INT_MIN/INT_MAX and must not be "tightened" to -2147483647.
 * INT_MIN is a legal int on both targets, and the strict spelling REJECTS it --
 * dropping the caller to its default, which is the same silent wrong value this
 * header exists to prevent. The strict form looks safer because its negative
 * half really does catch a saturated input, so a reviewer who probes that case
 * sees it work; errno is what actually covers the saturation, on either
 * spelling. The test carries "-2147483648" as an ACCEPTED value.
 *
 * strtol SATURATES and reports the fact in errno, which nothing here used to
 * read. `long` is 64-bit on Linux (LP64) and 32-bit on
 * Windows (LLP64), so a guard written against the int bounds alone is correct on
 * one platform and inert on the other: measured under mingw + wine,
 * `-rtview 9999999999` saturated to LONG_MAX, compared equal to the upper bound
 * rather than above it, and was accepted as INT_MAX. tic_time.h says the same
 * thing about the same type -- its width is a platform detail. errno is what the
 * saturating path actually sets, so that is what is tested.
 *
 * Non-finite (DOOM-0405). strtod consumes "inf", "infinity", "nan" and an
 * overflowing "1e999" WHOLLY, so the trailing-garbage test passes them. They are
 * not a range question and no range test can catch them: `inf >= 0.0f` is true,
 * clamping inf yields inf, and every comparison against a NaN is false, so a
 * NaN slips through a guard written either way round. -rippletime feeds a shader
 * push constant. The test is on the narrowed FLOAT, which also catches a value
 * like "1e300" -- a finite double that is inf once it reaches the caller.
 *
 * Contract and cases: tests/rb_argparse_test.cpp (INV-1..INV-4). */

#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <limits.h>

/* Parse a whole string as an int. Returns 1 and writes *out on success; returns
 * 0 and leaves *out untouched for an empty, blank, non-numeric, trailing-garbage
 * or out-of-int-range value. Trailing ASCII space is tolerated so a quoted " 3 "
 * from a shell still works. */
static inline int RB_ParseIntArg(const char* s, int* out)
{
    char* end;
    long  v;

    if (!s || !*s)
        return 0;

    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s)                        /* no digits consumed at all */
        return 0;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end)                            /* trailing non-space garbage */
        return 0;
    if (errno == ERANGE)                 /* saturated -- the value is not `v` */
        return 0;
    if (v < INT_MIN || v > INT_MAX)      /* would not survive the cast */
        return 0;

    *out = (int)v;
    return 1;
}

/* The float twin, same contract, plus: a value that is not FINITE is refused.
 * See the non-finite paragraph at the top -- no caller range test can catch one. */
static inline int RB_ParseFloatArg(const char* s, float* out)
{
    char*  end;
    double v;

    if (!s || !*s)
        return 0;

    v = strtod(s, &end);
    if (end == s)
        return 0;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end)
        return 0;

    /* Test the MAGNITUDE, and test it BEFORE converting. One check for three
     * cases: "inf"/"nan" consumed wholly, "1e999" overflowing the double, and
     * "1e300", a perfectly finite double that no float can hold.
     *
     * Not `isfinite((float)v)`, which is what this first shipped as. Converting
     * a double whose value is outside float's range is UNDEFINED BEHAVIOUR --
     * inf is what IEEE-754 hardware happens to produce, not something the
     * language promises -- so that spelling tests a value it was not entitled to
     * compute. It is also removable: under -ffast-math the compiler is free to
     * assume no non-finite values exist and fold the check away, leaving a guard
     * that decides nothing. This build does not pass -ffast-math today, which is
     * the reason it was a latent defect rather than a live one.
     *
     * Written negated so NaN is refused: every comparison against a NaN is
     * false, so `!(fabs(v) <= FLT_MAX)` is true for one. It refuses a hair more
     * than it must -- a double just above FLT_MAX would round to FLT_MAX rather
     * than overflow -- which costs a caller nothing and keeps one comparison.
     *
     * errno is deliberately NOT read here. strtod is permitted to report
     * UNDERFLOW in errno, and "1e-999" is wholly a number whose float value --
     * 0.0 -- is the right answer, so refusing it would break this header's own
     * contract. Measured under glibc, where the underflow does set ERANGE; the
     * C standard leaves that implementation-defined, which makes reading errno
     * here unportable as well as wrong. The int twin DOES read errno, because
     * saturation there loses the value outright. */
    if (!(fabs(v) <= (double)FLT_MAX))
        return 0;

    *out = (float)v;
    return 1;
}

#endif
