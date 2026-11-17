// sector_light.glsl — DOOM-0630: how a room's DOOM light level becomes light in Solid and
// Ultra, in both views. The one copy of the curve; mesh.frag and pathtrace.comp include it.
//
// The aim is a horror look, not a copy of Classic's colormaps: a room's light (0..1) is
// eased from black at LO to full at HI, which sits close to Classic's near shade at mid
// levels and darker below, so a dim room is dark until a lamp, the torch or a muzzle
// flash lights it. Values are look calls, chosen against same-spot captures of Classic.
#ifndef SECTOR_LIGHT_GLSL
#define SECTOR_LIGHT_GLSL

const float HORROR_LIGHT_LO   = 0.25;    // light 64: black without a lamp or the torch
const float HORROR_LIGHT_HI   = 0.80;    // light 204: full
// The rasterised view's distance fade: a dim room's light falls away over this many world
// units (light 0 falls by 1/e here), faster the dimmer it is; light 255 never fades, as
// in Classic. The traced view has no such term: its fog and real light transport do it.
const float HORROR_DARK_REACH = 300.0;

// A room's light level (0..1) as a shade (0..1).
float horrorSectorShade(float light)
{
    return smoothstep(HORROR_LIGHT_LO, HORROR_LIGHT_HI, light);
}

// The rasterised view's distance fade (0..1) for a room's light at a distance.
float horrorDistanceFade(float light, float dist)
{
    float dark = 1.0 - light;
    return exp(-dist * dark * dark / HORROR_DARK_REACH);
}

#endif
