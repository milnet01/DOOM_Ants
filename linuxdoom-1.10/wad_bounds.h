// wad_bounds.h — DOOM-0384: how far into a WAD a lump may claim to reach.
//
// A WAD is untrusted input (docs/standards/security.md names it a trust
// boundary): the engine is handed the file, it did not write it, and players
// trade PWADs freely. DOOM-0093 bounded the lump DIRECTORY against the real file
// size and stopped there, so every filepos and size inside that directory was
// stored raw. security.md is explicit — never trust a self-declared size; bound
// it against the actual buffer.
//
// What that let a crafted file do: a lump declaring a huge size reaches
// W_CacheLumpNum's Z_Malloc and aborts the game, which is a guaranteed crash on
// any downloaded PWAD; a negative filepos makes W_ReadLump's lseek fail
// silently, after which read() takes its bytes from wherever the descriptor
// already happened to be.
//
// The decision is factored out here, rather than left inline in w_wad.c, so
// tests/wad_bounds_test.cpp can hold the boundary cases against it with no WAD
// and no file descriptor — the same reason save_bounds.h exists.
#ifndef WAD_BOUNDS_H
#define WAD_BOUNDS_H

#include <stdint.h>

// Does a lump at `pos` of `size` bytes lie inside a file of `filelen` bytes?
//
// A zero size is legal and must stay so: marker lumps (MAP01, S_START, F_END)
// are empty by design and every real WAD is full of them. A zero-length file
// admits nothing but a zero-size lump at 0.
//
// The subtraction is the point. Testing `pos + size <= filelen` overflows on a
// crafted pair near the type's maximum and reports a lump reaching past the end
// as fitting. Checking `pos` first, then comparing against what is left after
// it, cannot overflow whatever the file claims.
static inline int WadLumpFits (long pos, long size, long filelen)
{
    if (pos < 0 || size < 0 || filelen < 0)
	return 0;
    if (pos > filelen)
	return 0;
    return size <= filelen - pos;
}

// DOOM-0402: does a lump that declares its own entry count actually hold them?
//
// The shape recurs: a 4-byte count at the front of a lump, then that many
// fixed-size records. PNAMES (8-byte names) and TEXTURE1/TEXTURE2 (4-byte
// directory offsets) are both built this way, and both counts come from the
// WAD. DOOM-0254 bounded PNAMES inline and the identical pattern a few lines
// below went unbounded, so the texture directory walk read past the cached
// lump before the per-entry offset check could fire. One predicate, so the
// next lump of this shape cannot be the one that is forgotten.
//
// Dividing what is left after the header is the point, as above: testing
// `headerbytes + count*entrybytes <= lumplen` multiplies a WAD-supplied count
// and can wrap.
static inline int WadCountFitsLump (int count, int lumplen,
				    int headerbytes, int entrybytes)
{
    if (count < 0 || headerbytes < 0 || entrybytes <= 0)
	return 0;
    if (lumplen < headerbytes)
	return 0;
    return count <= (lumplen - headerbytes) / entrybytes;
}

// DOOM-0432: which lump does a cached pointer belong to?
//
// W_CacheLumpNum hands Z_Malloc the address of the lump's own cache slot as the
// block's owner, so the zone header in front of a cached lump leads straight
// back to its slot. `user` is that owner field, read by Z_BlockUser. It is the
// lump's index when it points at a slot of `cache` AND that slot still holds
// `ptr`; anything else is -1.
//
// Both halves matter. An unowned block carries (void*)2 as its owner, and a
// block owned by something other than the lump cache points elsewhere, so the
// range and alignment test comes first and nothing is dereferenced until it
// passes. The slot comparison then refuses a stale pointer whose lump has since
// been loaded at another address.
//
// The comparisons are done on integers: relational operators on pointers into
// different objects are undefined, and `user` is not ours until proven so.
static inline int WadLumpOfUser (void* const* user, void* const* cache,
				 int numlumps, const void* ptr)
{
    uintptr_t	u = (uintptr_t)user;
    uintptr_t	base = (uintptr_t)cache;
    uintptr_t	off;

    if (!user || !cache || !ptr || numlumps <= 0)
	return -1;
    if (u < base)
	return -1;
    off = u - base;
    if (off % sizeof(void*) != 0)
	return -1;
    if (off / sizeof(void*) >= (uintptr_t)numlumps)
	return -1;
    if (*user != ptr)
	return -1;
    return (int)(off / sizeof(void*));
}

#endif
