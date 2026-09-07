// level_bounds_test.cpp — DOOM-0399: a map may not ask for an array whose size
// Z_Malloc's int parameter cannot hold.
//
// P_SetupLevel derives each per-level array's element count by dividing a lump's
// length, then allocates count * sizeof(element). The product is computed in
// size_t and does not overflow, but Z_Malloc takes an int, so a product above
// INT_MAX is truncated at the call -- a huge request returns a small block. The
// loop that follows fills the array by COUNT and writes past it.
//
// p_setup.c cannot be unit tested: P_LoadSegs wants the zone, the real lump
// cache and a loaded map. So the decision it makes per array lives in
// level_bounds.h and is tested here, exactly as wad_bounds.h and save_bounds.h
// are.
#include <cstdio>
#include <climits>
#include <cstddef>

#include "../level_bounds.h"
#include "check_util.h"

int main()
{
    // seg_t is the largest per-level element on LP64 and so the first to
    // overflow; the exact figure is not pinned here because the struct may grow.
    const size_t kSeg = 72;
    const size_t kBig = 4096;

    // --- Counts that genuinely fit. ---
    check(LevelAllocFits(0, kSeg) != 0, "an empty lump is legal");
    check(LevelAllocFits(1, kSeg) != 0, "a single element fits");
    check(LevelAllocFits(100000, kSeg) != 0, "a large but ordinary map fits");
    check(LevelAllocFits(INT_MAX, 1) != 0, "INT_MAX single bytes fit exactly");
    check(LevelAllocFits((int)(INT_MAX / kSeg), kSeg) != 0,
          "the largest count that still fits is accepted");

    // A zero element size cannot overflow anything, and refusing it would refuse
    // a caller that is asking for nothing. This is the case a division-based
    // check gets wrong by dividing by zero.
    check(LevelAllocFits(1000, 0) != 0, "a zero element size is legal");
    check(LevelAllocFits(0, 0) != 0, "zero of nothing is legal");

    // --- Counts that do not. ---
    check(LevelAllocFits((int)(INT_MAX / kSeg) + 1, kSeg) == 0,
          "one element past the limit is refused");
    check(LevelAllocFits(INT_MAX, kSeg) == 0,
          "INT_MAX segs is refused");
    check(LevelAllocFits(INT_MAX, 2) == 0,
          "a product of exactly INT_MAX+1 is refused");
    check(LevelAllocFits(INT_MAX / 2 + 1, kBig) == 0,
          "a large count of large elements is refused");

    // A negative count reaches here from a lump length divided by an element
    // size, so it means the caller's own arithmetic already went wrong. It must
    // not be multiplied.
    check(LevelAllocFits(-1, kSeg) == 0, "a negative count is refused");
    check(LevelAllocFits(INT_MIN, kSeg) == 0, "INT_MIN is refused");
    check(LevelAllocFits(-1, 0) == 0,
          "a negative count is refused even for a zero element size");

    return check_summary("level_bounds");
}
