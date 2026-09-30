// patch_bounds.h — DOOM-0228: how much of a graphic lump blit_tile may read.
//
// r_mesh.c builds the Vulkan atlas by copying palette indices straight out of
// cached lumps. A WAD is untrusted input (docs/standards/security.md), and
// neither the flat nor the sprite path checked the lump was big enough:
//
//   - Flats are read as a fixed 64x64 block. A shorter lump is read past its
//     end. Every stock flat is exactly 4096 bytes, so nothing noticed.
//   - A sprite is a posted patch. Its header, its per-column offsets and each
//     post's texels were all followed on the lump's own say-so, so a crafted
//     patch could point a column anywhere and walk a post chain off the end.
//
// The other patch readers (the blitters, the texture builders, the sprite
// drawer) hold no lump length. They ask PatchLumpValid below, once per lump,
// through W_PatchLumpOk / W_PatchOk (DOOM-0432).
//
// The decisions live here so tests can hold them against the boundary cases
// with no WAD and no GPU — the same reason the other *_bounds.h headers exist.
#ifndef PATCH_BOUNDS_H
#define PATCH_BOUNDS_H

// A flat is read as `h` rows of 64 palette indices, taking `w` from each.
// The furthest byte touched is therefore (h-1)*64 + (w-1).
static inline int FlatFits (int lumplen, int w, int h)
{
    if (lumplen <= 0 || w <= 0 || h <= 0)
	return 0;

    // 64 is the flat row stride the reader uses, not a guess about the lump.
    if (w > 64 || h > 64)
	return 0;

    return lumplen >= (h - 1) * 64 + w;
}

// Does the lump hold a patch header at all -- width, height, leftoffset,
// topoffset, four shorts? Three readers take only those fields (the atlas tile
// size, the sprite-height cache and R_InitSpriteLumps) and none checked, so a
// lump shorter than 8 bytes between the sprite markers was read past its end.
// Zero-length marker lumps inside S_START..S_END are ordinary in PWADs, so the
// callers treat a short lump as an empty sprite rather than refusing the WAD.
static inline int PatchHasHeader (int lumplen)
{
    return lumplen >= 8;
}

// A patch begins with width, height, leftoffset, topoffset (four shorts), then
// one 32-bit column offset per column. Reading column `w-1` needs all of it.
static inline int PatchHeaderFits (int lumplen, int width)
{
    if (lumplen < 8 || width <= 0)
	return 0;

    // Guard the multiply before doing it: width is a short read from the lump.
    if (width > (lumplen - 8) / 4)
	return 0;

    return 1;
}

// A column offset must land inside the lump with room for a post header.
// A post is topdelta, length, one pad byte, `length` texels, one pad byte.
static inline int PatchColumnFits (int columnofs, int lumplen)
{
    return columnofs >= 0 && columnofs <= lumplen - 2;
}

// `pos` is the post header's offset. The texels start at pos+3, so the last
// byte read is pos+2+length. Written as a subtraction so nothing overflows.
static inline int PatchPostFits (int pos, int length, int lumplen)
{
    if (pos < 0 || length < 0 || lumplen < 3)
	return 0;

    if (pos > lumplen - 3)
	return 0;

    return length <= lumplen - 3 - pos;
}

// DOOM-0432: is the WHOLE lump a well-formed patch?
//
// The helpers above answer one read at a time, for a reader that holds the
// lump's length. Most patch readers hold only a pointer, so this walks the
// lump once and W_PatchLumpOk / W_PatchOk (w_wad.c) remember the answer.
//
// Accepted means every byte a reader follows is inside the lump: the header,
// the whole column table, and for each column every post's header, texels,
// both pad bytes and the first byte of whatever comes next. A terminator on
// the lump's last byte is fine -- stock patches end that way.
//
// It does not compare a post with the patch's declared height. That is about
// the destination, and each reader bounds its own (V_PostInBounds).
//
// Fields are read byte by byte, little-endian, so this needs no m_swap.h and
// can be held against plain byte arrays in tests/patch_bounds_test.cpp.
static inline int PatchLumpValid (const unsigned char* data, int len)
{
    int	width, height, col;

    if (!data || len < 8)
	return 0;

    width  = (short)(data[0] | (data[1] << 8));
    height = (short)(data[2] | (data[3] << 8));

    if (width < 1 || height < 1)
	return 0;

    if (width > (len - 8) / 4)
	return 0;

    for (col = 0; col < width; col++)
    {
	const unsigned char*	t = data + 8 + col * 4;
	// Assembled unsigned: a shift into the sign bit is undefined.
	unsigned int		u = (unsigned int)t[0] | ((unsigned int)t[1] << 8)
				  | ((unsigned int)t[2] << 16)
				  | ((unsigned int)t[3] << 24);
	int			pos;

	// Covers a negative offset too: it is a huge unsigned value.
	if (u > (unsigned int)(len - 1))
	    return 0;
	pos = (int)u;

	// pos only grows and is re-checked against len each time round, so the
	// walk ends whatever the lump says.
	while (data[pos] != 0xff)
	{
	    int	length;

	    if (pos + 1 > len - 1)
		return 0;
	    length = data[pos + 1];

	    // Subtraction, so nothing a lump supplies is added to an int.
	    if (length + 4 > len - 1 - pos)
		return 0;
	    pos += length + 4;
	}
    }

    return 1;
}

#endif // PATCH_BOUNDS_H
