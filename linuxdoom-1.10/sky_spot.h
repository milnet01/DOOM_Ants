// sky_spot.h — DOOM-0658: stronger sky fill at a hand-picked spot of one map.
//
// The GI bake fills a ray that escapes to the sky with the constant SKY_COLOR
// (pt_common.glsl), far dimmer than the sky the player sees. Raising it everywhere would
// change every map's look, so a spot the user picked gets its own multiplier instead:
// probes inside the spot's box take SKY_COLOR times its gain. The rest of the map, and
// the sky itself, are unchanged.
//
// A spot names its map by game, episode and map, and by the map's sector and line counts,
// so Freedoom's or a mod's map of the same name does not take it.
//
// Apart from the engine so a test can reach it; RunGiBake (r_vulkan.cpp) reads it.
#ifndef SKY_SPOT_H
#define SKY_SPOT_H

typedef struct {
    int   doom2;               // 1 = a DOOM 2 map (MAPnn), 0 = DOOM 1 (ExMy)
    int   episode, map;        // episode is 1 for DOOM 2
    int   numsectors, numlines;
    float lo[3], hi[3];        // the box, map units: x, y, then z (floor height)
    float gain;                // the sky fill's multiplier inside it
} rb_sky_spot_t;

static const rb_sky_spot_t kRbSkySpots[] = {
    // E2M1: the start room and its south window onto the yard (sectors 77 and 78). The
    // user's pick, 2026-11-24, from the x16 trial (Scratchpad d0658/sky16grid.png).
    { 0, 2, 1, 92, 415, { -128.0f, -128.0f, 0.0f }, { 128.0f, 384.0f, 128.0f }, 16.0f },
};

// The spot on this map, or 0 when it has none.
static inline const rb_sky_spot_t* RB_SkySpotFor(int doom2, int episode, int map,
                                                 int numsectors, int numlines)
{
    for (unsigned i = 0; i < sizeof kRbSkySpots / sizeof kRbSkySpots[0]; i++)
    {
        const rb_sky_spot_t* s = &kRbSkySpots[i];
        if (s->doom2 == (doom2 != 0) && s->episode == episode && s->map == map
            && s->numsectors == numsectors && s->numlines == numlines)
            return s;
    }
    return 0;
}

#endif // SKY_SPOT_H
