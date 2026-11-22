// r_lightwall.h — DOOM-0652 B2: the map's linedefs as walls that block raster light.
//
// The C side of the polar wall map (light_grid.h). It reads the live map, so a door's
// state is whatever its sector heights are this tic.
#ifndef R_LIGHTWALL_H
#define R_LIGHTWALL_H

#ifdef __cplusplus
extern "C" {
#endif

// Byte-for-byte lgrid::Wall2D. kind is lgrid's LW_OPEN, LW_ONESIDED or LW_CLOSED.
typedef struct { float x1, y1, x2, y2; unsigned kind; } rb_lwall_t;

// Fill out[0 .. numlines) with every linedef, in linedef order, and return numlines.
// With out NULL, only return numlines. Returns 0 and writes nothing when max is smaller
// than numlines.
int RB_LightWalls(rb_lwall_t* out, int max);

#ifdef __cplusplus
}
#endif

#endif
