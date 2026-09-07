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
// The software renderer reaches the same data through R_DrawColumn, which is
// bounded elsewhere; this is the mesh builder's own path, and it was not.
//
// The decisions live here so tests can hold them against the boundary cases
// with no WAD and no GPU — the same reason the other *_bounds.h headers exist.
#ifndef PATCH_BOUNDS_H
#define PATCH_BOUNDS_H

// A flat is read as `h` rows of 64 palette indices, taking `w` from each.
// The furthest byte touched is therefore (h-1)*64 + (w-1).
static int FlatFits (int lumplen, int w, int h)
{
    if (lumplen <= 0 || w <= 0 || h <= 0)
	return 0;

    // 64 is the flat row stride the reader uses, not a guess about the lump.
    if (w > 64 || h > 64)
	return 0;

    return lumplen >= (h - 1) * 64 + w;
}

// A patch begins with width, height, leftoffset, topoffset (four shorts), then
// one 32-bit column offset per column. Reading column `w-1` needs all of it.
static int PatchHeaderFits (int lumplen, int width)
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
static int PatchColumnFits (int columnofs, int lumplen)
{
    return columnofs >= 0 && columnofs <= lumplen - 2;
}

// `pos` is the post header's offset. The texels start at pos+3, so the last
// byte read is pos+2+length. Written as a subtraction so nothing overflows.
static int PatchPostFits (int pos, int length, int lumplen)
{
    if (pos < 0 || length < 0 || lumplen < 3)
	return 0;

    if (pos > lumplen - 3)
	return 0;

    return length <= lumplen - 3 - pos;
}

#endif // PATCH_BOUNDS_H
