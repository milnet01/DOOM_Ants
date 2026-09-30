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

// DOOM-0093: do the BSP nodes form a tree?
//
// P_LoadNodes range-checks every child, but a child in range may still name
// its own node or an ancestor. Every BSP walk in the engine then never ends:
// R_RenderBSPNode, P_CrossBSPNode and R_PointInSubsector loop forever, and
// r_mesh.c's carve_caps recurses until the stack is gone. A map is a file a
// player was handed, so that is a hang or a crash on load.
//
// `kids` holds two entries per node, children[0] and children[1], exactly as
// the WAD gives them: a child with the high bit set names a subsector and is
// never followed; otherwise it is a node index. The root is the last node.
// `seen` is numnodes bytes the caller has zeroed, `stack` numnodes ints of
// scratch. Returns 1 when no node is reached twice walking down from the root.
//
// Two parents sharing one child is refused as well. It would not loop, but no
// node builder writes it, and accepting it would let a small NODES lump stand
// for an exponentially large walk.
//
// Iterative on purpose: the thing being guarded against is a walk deeper than
// the stack, so the check may not recurse. Each node is pushed at most once --
// `seen` is set before the push -- so `stack` cannot overflow its numnodes.
#define LEVEL_BSP_SUBSECTOR	0x8000	// NF_SUBSECTOR (doomdata.h)

static inline int LevelBspIsTree (int numnodes, const unsigned short* kids,
			   unsigned char* seen, int* stack)
{
    int		sp = 0;
    int		n;
    int		j;

    if (numnodes <= 0)
	return 1;

    seen[numnodes - 1] = 1;
    stack[sp++] = numnodes - 1;

    while (sp > 0)
    {
	n = stack[--sp];
	for (j = 0 ; j < 2 ; j++)
	{
	    int	child = kids[n * 2 + j];

	    if (child & LEVEL_BSP_SUBSECTOR)
		continue;

	    if (child >= numnodes || seen[child])
		return 0;

	    seen[child] = 1;
	    stack[sp++] = child;
	}
    }

    return 1;
}

#endif // LEVEL_BOUNDS_H
