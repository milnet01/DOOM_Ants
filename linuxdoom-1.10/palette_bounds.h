// palette_bounds.h — DOOM-0404: how much of a PLAYPAL lump the engine reads.
//
// PLAYPAL is untrusted input (docs/standards/security.md names a WAD a trust
// boundary), and every consumer treats it as a fixed-size array:
//
//   - I_SetPalette walks 256 RGB triples through a `byte*` whose declaration in
//     i_video.h carries no length at all.
//   - ST_doPaletteStuff picks a palette by INDEX -- red for pain, gold for a
//     pickup, green for the radiation suit -- and offsets into the lump by
//     index * one palette. RADIATIONPAL is the largest it selects.
//   - The Vulkan back-end's PLAYPAL LUT, the HUD's gold font ramp, the menu's
//     patch decoder and the screenshot writer each memcpy or scan 256 triples.
//
// None of them checks the lump's real length, and none of them can: by the time
// they run, W_CacheLumpName has handed over a bare pointer. A PWAD carrying a
// short PLAYPAL therefore reads off the end of its zone block. Both shipped
// IWADs carry the full 14 palettes, so nothing legitimate is near the edge.
//
// The lump's length is the one fact all of them share, so D_DoomMain gates it
// once at load, before any consumer has cached it. The decision lives here,
// rather than inline there, so tests/palette_bounds_test.cpp can hold the
// boundary cases against it with no WAD and no zone -- the same reason
// wad_bounds.h, level_bounds.h and save_bounds.h exist.
#ifndef PALETTE_BOUNDS_H
#define PALETTE_BOUNDS_H

// One palette: 256 colours, three bytes each. This is what I_SetPalette reads.
#define PALETTE_COLOURS		256
#define PALETTE_BYTES		(PALETTE_COLOURS * 3)

// Palettes 0 (normal) through RADIATIONPAL inclusive. st_stuff.c owns the index
// names and static-asserts that RADIATIONPAL is inside this count, so the two
// cannot drift apart silently.
#define PALETTE_COUNT		14

// The smallest PLAYPAL the engine can read without running off the end.
#define PLAYPAL_MIN_BYTES	(PALETTE_COUNT * PALETTE_BYTES)

// Does a PLAYPAL lump of `lumplen` bytes cover every palette the engine selects?
//
// The test is ">=", not "==": a PWAD may carry EXTRA palettes past RADIATIONPAL,
// and refusing those would reject a WAD the engine reads perfectly well. It is
// the short lump that is unsafe, never the long one.
static inline int PlayPalFits (int lumplen)
{
    return lumplen >= PLAYPAL_MIN_BYTES;
}

// Is `palette` an index a caller may select, given a lump of `lumplen` bytes?
//
// PlayPalFits is the load-time gate and this is the per-selection form of the
// same question. Kept together because the pair is what ties ST_doPaletteStuff's
// index arithmetic to the length that was checked at load.
static inline int PaletteIndexFits (int palette, int lumplen)
{
    if (palette < 0 || palette >= PALETTE_COUNT)
	return 0;

    // Guard the multiply's operand, not its product: palette is already bounded
    // above, so this cannot overflow, and the comparison never forms a product
    // larger than the lump.
    return lumplen >= (palette + 1) * PALETTE_BYTES;
}

// COLORMAP, the light tables, has the same shape of problem: R_InitColormaps
// read the lump into a block sized by the lump's own length, and every reader
// indexes it as fixed-size 256-byte maps. R_InitLightTables hands out maps 0
// through NUMCOLORMAPS-1, and R_SetupFrame's fixedcolormap reaches one further,
// to INVERSECOLORMAP (32) for invulnerability -- 33 maps, which is what a reader
// needs. Both shipped IWADs carry 34 (the last one is unused), so nothing
// legitimate is near the edge. r_data.c static-asserts COLORMAP_MAPS against
// NUMCOLORMAPS so the two cannot drift apart.
#define COLORMAP_BYTES		256
#define COLORMAP_MAPS		33
#define COLORMAP_MIN_BYTES	(COLORMAP_MAPS * COLORMAP_BYTES)

// How many bytes R_InitColormaps allocates for a COLORMAP lump of `lumplen`
// bytes: the lump, but never less than every reader indexes. A short lump is
// zero-padded by the caller rather than refused -- REJECT's posture in
// P_SetupLevel -- so a PWAD whose COLORMAP stops at the 32 light levels still
// plays, with a black invulnerability map instead of an out-of-bounds one. A
// full-length lump gets exactly the block it always did.
static inline int ColormapAllocBytes (int lumplen)
{
    return lumplen > COLORMAP_MIN_BYTES ? lumplen : COLORMAP_MIN_BYTES;
}

#endif
