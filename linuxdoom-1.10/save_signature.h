// save_signature.h — DOOM-0426: which builds' savegames this build may read.
//
// A .dsg is a raw image of the engine's structs, so what it means depends on how
// a particular binary lays player_t, mobj_t and sector_t out.
//
// The only gate on that was a strncmp against "version %i" of VERSION -- id's
// DOOM 1.10 number, which has not moved since 1997 and does not move when this
// fork changes a struct. So two builds calling themselves the same version, or
// a 32- against a 64-bit build, read each other's saves as garbage rather than
// refusing them. That garbage is the attacker-controlled state DOOM-0373's
// write primitives consume, which makes this a security question too.
//
// The fix is to stamp what the version cannot say: the sizes of the structs
// actually archived, plus the pointer width. A build that changes any of them
// produces a different signature and its saves are refused with a message
// rather than misread.
//
// What it catches: a struct that gained, lost or reordered a field such that
// its size changed; a 32- against a 64-bit build; a different alignment.
// What it does NOT catch: two layouts that differ while every size stays the
// same -- swapping two same-sized fields, or changing what a field MEANS. A
// format version bumped by hand would catch those, and needs someone to
// remember; serialising field by field would remove the problem instead of
// detecting it. Both are recorded on DOOM-0426 as the larger jobs they are.
//
// Factored out here, rather than left inline in g_game.c, so tests can hold it
// against known inputs with no savegame and no loaded level -- the same reason
// save_bounds.h and the other *_bounds.h headers exist.
#ifndef SAVE_SIGNATURE_H
#define SAVE_SIGNATURE_H

#include <stdio.h>

// The stamped field is fixed-width, like the version field beside it, so the
// header stays a fixed size and can still be bounds-checked in one go.
// "layout " plus eight hex digits is 15 bytes, and the field is 16.
#define SAVE_SIGNATURE_SIZE	16

// Fold the layout-defining sizes into one 32-bit id.
//
// FNV-1a over the four values, byte by byte and least-significant first. Byte
// by byte rather than by value so that no two fields can be swapped without
// changing the answer; a plain sum or xor would let a byte gained by mobj_t and
// lost by player_t cancel out, which is exactly the change this is meant to
// see. Pure integer arithmetic, so every compiler and platform agrees on the
// number for a given set of sizes -- the point is to compare BUILDS, and a
// signature that varied by compiler would refuse a build's own saves.
static unsigned SaveLayoutId
( unsigned	mobjsize,
  unsigned	playersize,
  unsigned	sectorsize,
  unsigned	ptrsize )
{
    const unsigned	sizes[4] = { mobjsize, playersize, sectorsize, ptrsize };
    unsigned		h = 2166136261u;	// FNV-1a offset basis
    int			i;
    int			b;

    for (i = 0; i < 4; i++)
    {
	for (b = 0; b < 4; b++)
	{
	    h ^= (sizes[i] >> (b * 8)) & 0xffu;
	    h *= 16777619u;			// FNV-1a prime
	}
    }

    return h;
}

// Write the stamped field. `out` must have room for SAVE_SIGNATURE_SIZE bytes;
// the field is zero-filled first so the unused tail is never uninitialised
// stack on its way into a file.
static void SaveFormatSignature
( char*		out,
  unsigned	mobjsize,
  unsigned	playersize,
  unsigned	sectorsize,
  unsigned	ptrsize )
{
    int	i;

    for (i = 0; i < SAVE_SIGNATURE_SIZE; i++)
	out[i] = 0;

    snprintf (out, SAVE_SIGNATURE_SIZE, "layout %08x",
	      SaveLayoutId (mobjsize, playersize, sectorsize, ptrsize));
}

#endif // SAVE_SIGNATURE_H
