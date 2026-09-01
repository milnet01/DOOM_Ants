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
 * Contract and cases: tests/rb_argparse_test.cpp (INV-1..INV-4). */

#include <stdlib.h>
#include <ctype.h>

/* Parse a whole string as an int. Returns 1 and writes *out on success; returns
 * 0 and leaves *out untouched for an empty, blank, non-numeric or
 * trailing-garbage value. Trailing ASCII space is tolerated so a quoted " 3 "
 * from a shell still works. */
static inline int RB_ParseIntArg(const char* s, int* out)
{
    char* end;
    long  v;

    if (!s || !*s)
        return 0;

    v = strtol(s, &end, 10);
    if (end == s)                        /* no digits consumed at all */
        return 0;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end)                            /* trailing non-space garbage */
        return 0;
    if (v < -2147483647L || v > 2147483647L)   /* would not survive the cast */
        return 0;

    *out = (int)v;
    return 1;
}

/* The float twin, same contract. */
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

    *out = (float)v;
    return 1;
}

#endif
