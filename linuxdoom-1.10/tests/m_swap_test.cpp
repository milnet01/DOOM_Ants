// m_swap_test.cpp — DOOM-0401: the endian swaps are reachable and correct.
//
// These two functions had never been compiled by any build. m_swap.h declared
// them under `#ifdef __BIG_ENDIAN__` and m_swap.c defined them under
// `#ifndef __BIG_ENDIAN__` -- exact complements, so the only configuration
// that calls them is the one with no definition to link against. The
// false-positive ledger meanwhile cites big-endian as the reason for keeping
// the LONG()/SHORT() macros at all, so that dismissal rested on code that
// could not build.
//
// They are compiled unconditionally now, which is what makes this test
// possible on the little-endian machine the fork builds on: SHORT()/LONG() are
// still the identity here, so nothing else in the engine exercises the swap.
//
// The types matter as much as the reachability. The old signatures took
// `unsigned long`, 64 bits on LP64, so the 32-bit shift pattern would have
// swapped the wrong width on the very platform it was written for. The
// high-byte cases below are the ones an `unsigned long` version fails.
//
// The .c is included rather than linked, as mus2mid_test.cpp does -- each test
// is one translation unit and the Makefile wires no engine objects into it.
#include <cstdio>
#include <cstdint>

#include "check_util.h"
#include "../m_swap.c"

int main()
{
    // --- 16-bit. ---
    check(SwapSHORT(0x0000) == 0x0000, "zero swaps to zero");
    check(SwapSHORT(0x1234) == 0x3412, "a 16-bit value has its two bytes exchanged");
    check(SwapSHORT(0x00ff) == 0xff00, "the low byte moves to the high byte");
    check(SwapSHORT(0xff00) == 0x00ff, "the high byte moves to the low byte");
    check(SwapSHORT(0xffff) == 0xffff, "all bits set swaps to itself");
    check(SwapSHORT(SwapSHORT(0x1234)) == 0x1234, "swapping twice is the identity");

    // A WAD's SHORT fields are signed on the C side. 0x8000 is the sign bit,
    // and it must move as a bit pattern rather than sign-extend.
    check(SwapSHORT(0x8000) == 0x0080, "the sign bit is moved, not extended");

    // --- 32-bit. ---
    check(SwapLONG(0x00000000u) == 0x00000000u, "zero swaps to zero");
    check(SwapLONG(0x12345678u) == 0x78563412u,
          "a 32-bit value has its four bytes reversed");
    check(SwapLONG(0x000000ffu) == 0xff000000u, "byte 0 moves to byte 3");
    check(SwapLONG(0xff000000u) == 0x000000ffu, "byte 3 moves to byte 0");
    check(SwapLONG(0xffffffffu) == 0xffffffffu, "all bits set swaps to itself");
    check(SwapLONG(SwapLONG(0x12345678u)) == 0x12345678u,
          "swapping twice is the identity");

    // The width case. With a 64-bit parameter `x << 24` on a value carrying
    // bits in its top byte lands at bit 48 instead of falling off the end, so
    // the result has bits above 31 and these two comparisons fail.
    check(SwapLONG(0x80000000u) == 0x00000080u,
          "the top bit lands in the low byte, not past bit 31");
    check(SwapLONG(0xdeadbeefu) == 0xefbeaddeu,
          "a value with its high byte set reverses within 32 bits");

    // --- The macros, on whichever host builds this. ---
#ifdef __BIG_ENDIAN__
    check(SHORT(0x1234) == (short)0x3412, "SHORT swaps on a big-endian build");
    check(LONG(0x12345678) == (int)0x78563412, "LONG swaps on a big-endian build");
#else
    check(SHORT(0x1234) == 0x1234, "SHORT is the identity on little-endian");
    check(LONG(0x12345678) == 0x12345678, "LONG is the identity on little-endian");
#endif

    return check_summary("m_swap_test");
}
