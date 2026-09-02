// save_bounds.h — DOOM-0255: how far the savegame read cursor may advance.
//
// A .dsg is untrusted input (docs/standards/security.md names it a trust
// boundary), and vanilla read it with no idea where the file ended: G_DoLoadGame
// threw away M_ReadFile's length and every P_UnArchive* function walked `save_p`
// forward on faith. On a truncated or hand-edited save that walks off the end of
// the heap allocation — the version-string strcmp on a short file is enough on
// its own, before a single struct is read back.
//
// The two decisions the cursor makes are factored out here rather than left
// inline in p_saveg.c so tests/save_bounds_test.cpp can hold the boundary cases
// against them with no WAD, no level and no Z_Malloc (mirrors seg_project.h).
// Both are phrased in terms of the bytes REMAINING rather than an end pointer,
// so the check can never itself form the out-of-range pointer it exists to
// prevent, and so the padding cannot be applied before it has been checked.
#ifndef SAVE_BOUNDS_H
#define SAVE_BOUNDS_H

#include <stddef.h>
#include <stdint.h>

// Bytes of 4-byte alignment padding sitting between `p` and the next aligned
// position — the alignment step vanilla's PADSAVEP macro made, which both
// P_SaveNeedAligned and P_SaveRoomAligned now own.
//
// This has to be counted as part of the read, not applied before it: at the very
// end of a truncated file the padding alone steps the cursor past the allocation
// while the caller still believes it has not read anything yet. The write side
// has the same shape against the end of its buffer (DOOM-0374).
static size_t SavePadBytes (const void* p)
{
    return (size_t)((4u - ((uintptr_t)p & 3u)) & 3u);
}

// Does the next read fit — `pad` bytes of alignment followed by `count` bytes of
// payload — in the `remaining` bytes left in the loaded file?
//
// The subtraction is the point: testing `pad + count <= remaining` would wrap on
// a count near SIZE_MAX and report a colossal read as fitting. Checking `pad`
// first, then comparing against what is left after it, cannot overflow whatever
// the file claims.
static int SaveFits (size_t remaining, size_t pad, size_t count)
{
    if (pad > remaining)
	return 0;
    return count <= remaining - pad;
}

#endif
