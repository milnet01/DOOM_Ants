// Regression test for the zone allocator's alignment (z_zone.c).
//
// Vanilla Z_Malloc rounded every request to 4 bytes, the pointer size it was
// written for. On 64-bit the block header carries 8-byte pointers, so an odd-
// sized request (a lump, a string, a 4-byte array) left the NEXT block's header
// and the pointer handed back after it misaligned -- and the structs the engine
// keeps in the zone (mobj_t, line_t, ...) are full of pointers. Every pointer
// Z_Malloc returns must now sit on an 8-byte boundary, whatever came before it,
// and the heap must still check out after splits, purges and merges.
//
// Single-TU include like the sibling tests. z_zone.c needs only I_ZoneBase and
// I_Error from the platform layer, so the test supplies both.
#include "check_util.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

// z_zone.c passes string literals to I_Error's char* parameter, which C allows
// and C++ warns about. The unit's code is C; the warning is about the language
// it is being recompiled as, not about the code. Likewise C11's _Static_assert
// is spelled static_assert in C++.
#define _Static_assert static_assert
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include "../z_zone.c"
#pragma GCC diagnostic pop

#define TEST_ZONE_BYTES (256 * 1024)

byte* I_ZoneBase (int* size)
{
    *size = TEST_ZONE_BYTES;
    return (byte*) std::malloc (TEST_ZONE_BYTES);
}

void I_Error (char* error, ...)
{
    std::printf ("  FAIL: I_Error: %s\n", error);
    std::exit (1);
}

static bool aligned (const void* p)
{
    return ((uintptr_t) p % 8) == 0;
}

int main ()
{
    Z_Init ();

    // Odd sizes back to back: under the 4-byte rounding every second one of
    // these came back 4 bytes off an 8-byte boundary.
    {
	static const int sizes[] = { 1, 4, 5, 12, 13, 20, 3, 100, 7, 4, 4 };
	void*	ptrs[sizeof(sizes) / sizeof(sizes[0])];
	bool	all = true;
	size_t	i;

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
	{
	    ptrs[i] = Z_Malloc (sizes[i], PU_STATIC, NULL);
	    if (!aligned (ptrs[i]))
		all = false;
	}
	check (all, "every PU_STATIC block of an odd size is 8-byte aligned");
	Z_CheckHeap ();

	// Free alternate blocks, then refill the holes: blocks carved out of
	// freed space and merged fragments must keep the boundary too.
	for (i = 0; i < sizeof(ptrs) / sizeof(ptrs[0]); i += 2)
	    Z_Free (ptrs[i]);
	Z_CheckHeap ();
	all = true;
	for (i = 0; i < sizeof(ptrs) / sizeof(ptrs[0]); i += 2)
	{
	    ptrs[i] = Z_Malloc (sizes[i] + 1, PU_STATIC, NULL);
	    if (!aligned (ptrs[i]))
		all = false;
	}
	check (all, "blocks reallocated into freed holes stay 8-byte aligned");
	Z_CheckHeap ();

	for (i = 0; i < sizeof(ptrs) / sizeof(ptrs[0]); i++)
	    Z_Free (ptrs[i]);
	Z_CheckHeap ();
    }

    // Purgeable blocks are thrown out to make room; the block that replaces
    // them is assembled from their merged space.
    {
	void*	cache[40];
	void*	big;
	bool	all = true;
	int	i;

	for (i = 0; i < 40; i++)
	{
	    Z_Malloc (4096 + 3 * i + 1, PU_CACHE, &cache[i]);
	    if (!aligned (cache[i]))
		all = false;
	}
	check (all, "every PU_CACHE block of an odd size is 8-byte aligned");

	// Larger than what is left free, so Z_Malloc must purge cache blocks.
	big = Z_Malloc (TEST_ZONE_BYTES / 2 + 5, PU_STATIC, NULL);
	check (aligned (big), "a block built from purged cache space is 8-byte aligned");
	Z_CheckHeap ();
	Z_Free (big);
	Z_CheckHeap ();
    }

    return check_summary ("zone_align");
}
