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
static int WadLumpFits (long pos, long size, long filelen)
{
    if (pos < 0 || size < 0 || filelen < 0)
	return 0;
    if (pos > filelen)
	return 0;
    return size <= filelen - pos;
}

#endif
