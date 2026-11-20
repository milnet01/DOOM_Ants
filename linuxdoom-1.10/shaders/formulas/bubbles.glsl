// bubbles.glsl -- DOOM-0619: small pale bubbles that rise through a model's glowing liquid.
// The one copy of the pattern; mesh.frag and pathtrace.comp include it, and both feed it the
// same clock (r_vulkan.cpp's RippleSeconds), so the two views show the same bubbles.
//
// The pattern lives in world space: columns of cells kBubbleCell map units across, each
// column sliding upward at its own speed. A cell holds at most one bubble, kept far enough
// from the cell's faces that it never crosses one, so a point reads its own cell and no
// neighbour. A model's shell shows a bubble where the two meet, as the baked pattern this
// replaced did (the 2026-11-15 flask's Voronoi dots).
//
// A caller draws it only on a material whose MatCtrl.flags carry kCtrlBubbles (models.csv's
// third word), and only where that model's glow map is lit: that is the liquid.
#ifndef BUBBLES_GLSL
#define BUBBLES_GLSL

// Needs pcgHash (pcg_hash.glsl, which has no include guard) from the including shader.

const uint  kCtrlBubbles  = 64u;    // RB_FLAG_BUBBLES in rb_materials.h
const float kBubbleCell   = 1.2;    // map units; the flask is about 8 wide and 14 tall
const float kBubbleRadius = 0.26;   // of a cell, at most; the smallest are half that
const float kBubbleRise   = 2.2;    // map units a second, give or take kBubbleRiseVary
const float kBubbleRiseVary = 0.45;
const float kBubbleShare  = 0.7;    // of cells that hold a bubble
const float kBubbleMix    = 0.55;   // how far a bubble pulls the liquid toward kBubblePale
const vec3  kBubblePale   = vec3(0.85, 0.92, 1.0);
const float kBubbleGlowLo = 0.02;   // the glow map's brightest channel, linear: below this is
const float kBubbleGlowHi = 0.10;   // glass or wax, above it liquid

float bubbleRand(uvec3 c, uint salt)
{
    return float(pcgHash(c.x ^ pcgHash(c.y ^ pcgHash(c.z ^ salt)))) / 4294967295.0;
}

// 0 outside a bubble, up to kBubbleMix at a bubble's middle. glow is the model's glow map
// there, linear.
float bubbleAt(vec3 worldPos, float seconds, vec3 glow)
{
    const float liquid = smoothstep(kBubbleGlowLo, kBubbleGlowHi, max(glow.r, max(glow.g, glow.b)));
    if (liquid <= 0.0)
        return 0.0;
    vec3  q   = worldPos / kBubbleCell;
    // Offset far into the positive numbers: a map's coordinates are within +-32768.
    const uvec2 col  = uvec2(ivec2(floor(q.xy)) + 65536);
    const float rise = kBubbleRise * (1.0 + kBubbleRiseVary * (2.0 * bubbleRand(uvec3(col, 0u), 1u) - 1.0));
    q.z -= seconds * rise / kBubbleCell + 64.0 * bubbleRand(uvec3(col, 0u), 2u);
    const uvec3 cell = uvec3(col, uint(int(floor(q.z)) + 1048576));
    if (bubbleRand(cell, 3u) > kBubbleShare)
        return 0.0;
    const float r    = kBubbleRadius * (0.5 + 0.5 * bubbleRand(cell, 4u));
    const vec3  seed = kBubbleRadius + (1.0 - 2.0 * kBubbleRadius)
                     * vec3(bubbleRand(cell, 5u), bubbleRand(cell, 6u), bubbleRand(cell, 7u));
    return kBubbleMix * liquid * (1.0 - smoothstep(0.7 * r, r, distance(fract(q), seed)));
}

#endif
