// atlas_bounds.h — DOOM-0221: how big a tile, and how tall an atlas, a WAD may ask for.
//
// r_mesh.c packs every wall texture, flat and sprite into one CPU-side atlas and
// the Vulkan back-end then cuts each tile back out as its own image. A tile's
// size is whatever the WAD's texture definition or patch header says, and a WAD
// is untrusted input (docs/standards/security.md). Two things went wrong:
//
//   1. Width was clamped to the atlas width; height was only kept above zero.
//      A header may declare a height up to 32767, which became an image taller
//      than a device need accept -- invalid usage at vkCreateImage, not a
//      failure the driver has to report.
//
//   2. The packer summed shelf heights with no ceiling. The blitters index the
//      atlas as (row * width) in int, so a few dozen maximum-size tiles pushed
//      that product past INT_MAX and the write landed before the buffer -- after
//      a calloc of several gigabytes had already been asked for.
//
// The decisions live here, rather than inline in r_mesh.c, so
// tests/atlas_bounds_test.cpp can hold the boundary cases against them with no
// WAD and no GPU -- the same reason the other *_bounds.h headers exist.
#ifndef ATLAS_BOUNDS_H
#define ATLAS_BOUNDS_H

// The atlas is this many texels wide and grows downward.
#define RB_ATLAS_WIDTH		2048

// The tallest tile kept. 4096 is the smallest maxImageDimension2D a Vulkan
// device may report, so a tile this size is a valid image everywhere; the
// tallest stock DOOM texture is far below it.
#define RB_ATLAS_MAX_TILE_H	4096

// The most rows the atlas may reach. With RB_ATLAS_WIDTH this is 512 MiB of
// palette indices, many times what both IWADs need together, and it keeps
// (rows + one more tile) * width inside a signed 32-bit int -- the blitters'
// index arithmetic. The test derives that relation from these constants.
#define RB_ATLAS_MAX_ROWS	262144

// Bring a tile's declared size into what the atlas and the device can hold.
// A too-large tile is cropped rather than refused: the map still loads, and a
// cropped texture is a visible artefact on a WAD that was malformed anyway.
static void AtlasClampTile (int* w, int* h)
{
    if (*w < 1)
	*w = 1;
    if (*h < 1)
	*h = 1;
    if (*w > RB_ATLAS_WIDTH)
	*w = RB_ATLAS_WIDTH;
    if (*h > RB_ATLAS_MAX_TILE_H)
	*h = RB_ATLAS_MAX_TILE_H;
}

// May the atlas be `rows` tall? Taken in long long so the caller can add a
// shelf to the running total without the sum itself overflowing first.
static int AtlasRowsFit (long long rows)
{
    return rows >= 0 && rows <= RB_ATLAS_MAX_ROWS;
}

#endif // ATLAS_BOUNDS_H
