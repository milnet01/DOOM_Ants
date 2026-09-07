// level_bounds.h — DOOM-0399: how large a per-level array a map may ask for.
//
// A map's lumps are untrusted input (docs/standards/security.md names a WAD a
// trust boundary). P_SetupLevel derives every array's element count by dividing
// a lump's length, then allocates count * sizeof(element).
//
// That product is computed in size_t, so it does not overflow -- but Z_Malloc
// takes an int, so a product above INT_MAX is TRUNCATED at the call, and a
// negative or small result is returned for a huge request. The loop that
// follows then fills the array using the count, not the size, and writes past
// whatever was actually allocated. Nothing downstream can notice: the count and
// the block are both exactly what they claim to be, separately.
//
// The sizes involved are large but not unreachable. seg_t is the biggest at 72
// bytes on LP64, so a SEGS lump of about 358 MB is enough, and a WAD is a file
// a player was handed.
//
// The decision is factored out here, rather than left inline in p_setup.c, so
// tests/level_bounds_test.cpp can hold the boundary cases against it with no
// WAD and no zone -- the same reason wad_bounds.h and save_bounds.h exist.
#ifndef LEVEL_BOUNDS_H
#define LEVEL_BOUNDS_H

#include <limits.h>
#include <stddef.h>

// Can `count` elements of `elemsize` bytes be asked of Z_Malloc without the
// size being truncated by its int parameter?
//
// A zero count is legal and must stay so: a map may carry an empty lump, and
// Z_Malloc is happy to return a minimal block for it.
//
// The division is the point. Testing `count * elemsize <= INT_MAX` computes the
// product first, and while size_t will not overflow for any count an int can
// hold, writing the test that way invites the same expression to be reused
// somewhere the product is not widened. Comparing count against what INT_MAX
// leaves room for never forms the product at all.
static int LevelAllocFits (int count, size_t elemsize)
{
    if (count < 0)
	return 0;

    if (elemsize == 0)
	return 1;

    return (size_t)count <= (size_t)INT_MAX / elemsize;
}

#endif // LEVEL_BOUNDS_H
