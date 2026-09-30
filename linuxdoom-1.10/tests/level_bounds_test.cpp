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
#include <vector>

#include "../level_bounds.h"
#include "check_util.h"

// ---- LevelBspIsTree (security review F-C) -----------------------------------
//
// P_LoadNodes range-checks each child but nothing checked the nodes form a
// tree. A node naming itself or an ancestor made r_mesh.c carve_caps() recurse
// without end and the software renderer's BSP walks loop forever.
static const unsigned short SUB = 0x8000;   // NF_SUBSECTOR

// Runs LevelBspIsTree the way the caller must: seen zeroed, stack numnodes
// ints. Past the numnodes bytes of seen sits a ZEROED slack (large enough for
// any 15-bit child index), then a guard byte. Zero slack matters: an unchecked
// seen[child] for an out-of-range child then reads 0 ("not seen"), so a dropped
// range check cannot return 0 by accident off a non-zero guard -- it walks on.
// kids is padded past 2*numnodes with subsector entries so that walk stays
// inside this test's memory. A dropped range check therefore shows up either as
// a return of 1 or as a write into the slack/guard, and both fail here.
static int is_tree(int n, const std::vector<unsigned short>& kidsIn)
{
    const int slack = 0x8000;
    std::vector<unsigned short> kids(kidsIn);
    if ((int)kids.size() < 2 * (n + slack)) kids.resize(2 * (n + slack), SUB);
    std::vector<unsigned char> seen(n + slack + 1, 0);
    std::vector<int>           stack(n + 1, -12345);
    seen[n + slack] = 0xAA;
    int r = LevelBspIsTree(n, kids.data(), seen.data(), stack.data());
    bool clean = true;
    for (int i = n; i < n + slack; i++) if (seen[i]) { clean = false; break; }
    check(clean, "LevelBspIsTree wrote past the numnodes bytes of seen[]");
    check(seen[n + slack] == 0xAA, "LevelBspIsTree wrote far past the end of seen[]");
    check(stack[n] == -12345, "LevelBspIsTree wrote past the end of stack[]");
    return r;
}

static void check_bsp_tree()
{
    // INV-1: an empty node list is trivially a tree (a map may have none).
    {
        std::vector<unsigned short> none(2, 0);
        check(is_tree(0, none) == 1, "INV-1: numnodes 0 is a tree");
    }

    // INV-2: a well-formed tree is accepted, subsector children never followed.
    {
        std::vector<unsigned short> k = { SUB | 0, SUB | 1 };
        check(is_tree(1, k) == 1, "INV-2: one node with two subsectors");
    }
    {   // root = node 2; 2 -> {1, sub}; 1 -> {0, sub}; 0 -> {sub, sub}
        std::vector<unsigned short> k = { SUB, SUB,  0, SUB,  1, SUB };
        check(is_tree(3, k) == 1, "INV-2: a three-deep chain");
    }
    {   // subsector index equal to a node number must not be followed
        std::vector<unsigned short> k = { SUB | 0, SUB | 0 };
        check(is_tree(1, k) == 1,
              "INV-2: subsector 0 is not node 0 (0x8000 children are never followed)");
    }
    {   // 0xFFFF has the flag set: the old "-1 means subsector 0" form
        std::vector<unsigned short> k = { 0xFFFF, 0xFFFF };
        check(is_tree(1, k) == 1, "INV-2: 0xFFFF is a subsector, not node 65535");
    }

    // INV-3: a node naming itself is refused.
    {
        std::vector<unsigned short> k = { 0, SUB };
        check(is_tree(1, k) == 0, "INV-3: root naming itself (child 0)");
        std::vector<unsigned short> k2 = { SUB, 0 };
        check(is_tree(1, k2) == 0, "INV-3: root naming itself (child 1)");
    }
    {   // self-loop on a non-root node still reachable from the root
        std::vector<unsigned short> k = { 0, SUB,  0, SUB };   // node 0 -> 0; root 1 -> 0
        check(is_tree(2, k) == 0, "INV-3: a reachable inner node naming itself");
    }

    // INV-4: a cycle through an ancestor is refused.
    {   // root 1 -> 0 -> 1
        std::vector<unsigned short> k = { 1, SUB,  0, SUB };
        check(is_tree(2, k) == 0, "INV-4: child names its parent");
    }
    {   // root 2 -> 1 -> 0 -> 2
        std::vector<unsigned short> k = { 2, SUB,  0, SUB,  1, SUB };
        check(is_tree(3, k) == 0, "INV-4: cycle back to the root through two levels");
    }
    {   // root 2 -> {1, sub}; 1 -> {sub, 0}; 0 -> {sub, 1}: cycle NOT through the root
        std::vector<unsigned short> k = { SUB, 1,  SUB, 0,  1, SUB };
        check(is_tree(3, k) == 0, "INV-4: a cycle below the root");
    }

    // INV-5: two parents sharing one child node is refused.
    {   // root 2 -> {0, 1}; 1 -> {0, sub}
        std::vector<unsigned short> k = { SUB, SUB,  0, SUB,  0, 1 };
        check(is_tree(3, k) == 0, "INV-5: node 0 reached by two parents");
    }
    {   // one parent naming the same child twice
        std::vector<unsigned short> k = { SUB, SUB,  0, 0 };
        check(is_tree(2, k) == 0, "INV-5: both children of one node are the same node");
    }

    // INV-6: a child node index outside the list is refused, not read.
    {
        std::vector<unsigned short> k = { 1, SUB };
        check(is_tree(1, k) == 0, "INV-6: child index == numnodes");
        std::vector<unsigned short> k2 = { 5, SUB };
        check(is_tree(1, k2) == 0, "INV-6: child index past numnodes");
        std::vector<unsigned short> k3 = { SUB, 0x7FFF };
        check(is_tree(1, k3) == 0, "INV-6: the largest node index (0x7FFF)");
    }

    // INV-7: nodes not reachable from the root do not matter.
    {   // root 2 has only subsectors; nodes 0 and 1 form a cycle nobody reaches
        std::vector<unsigned short> k = { 1, SUB,  0, SUB,  SUB, SUB };
        check(is_tree(3, k) == 1, "INV-7: an unreachable cycle is ignored");
    }
    {   // unreachable node with an out-of-range child
        std::vector<unsigned short> k = { 9, 9,  SUB, SUB };
        check(is_tree(2, k) == 1, "INV-7: an unreachable bad child is ignored");
    }

    // INV-8: scale. A long chain and a wide tree are accepted without touching
    // more than the stated scratch, and the same shapes with one back-edge are
    // refused. (A recursive walk would also risk the C stack on the chain.)
    {
        const int n = 20000;
        std::vector<unsigned short> k(2 * n, SUB);
        for (int i = 1; i < n; i++) k[2 * i] = (unsigned short)(i - 1);   // i -> i-1
        check(is_tree(n, k) == 1, "INV-8: a 20000-deep chain");
        k[0] = (unsigned short)(n - 1);                                   // node 0 -> root
        check(is_tree(n, k) == 0, "INV-8: the same chain with a back-edge to the root");
    }
    {
        const int n = 511;   // complete binary tree; heap slot h is node n-1-h
        std::vector<unsigned short> k(2 * n, SUB);
        for (int h = 0; h < n; h++)
            for (int c = 0; c < 2; c++)
            {
                int ch = 2 * h + 1 + c;
                if (ch < n) k[2 * (n - 1 - h) + c] = (unsigned short)(n - 1 - ch);
            }
        check(is_tree(n, k) == 1, "INV-8: a complete 511-node tree");
        k[0] = (unsigned short)(n - 1);   // a leaf's child becomes the root
        check(is_tree(n, k) == 0, "INV-8: the same tree with a leaf pointing at the root");
    }
}

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

    check_bsp_tree();

    return check_summary("level_bounds");
}
