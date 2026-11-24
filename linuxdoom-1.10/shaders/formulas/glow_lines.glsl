// glow_lines.glsl — DOOM-0660: the red teleporter pentagrams glow on their drawn lines only.
// The one copy of the test; pathtrace.comp and mesh.frag include it.
//
// GATE1-3 (r_vulkan.cpp kGlowLineFlats) draw their star and circle in palette reds 67-79
// (encoded value up to 0.31) on a field of 91 and up (0.357). The red test keeps the black
// specks and the brown rim dark.
#ifndef GLOW_LINES_GLSL
#define GLOW_LINES_GLSL

const uint GLOW_LINES = 256u;   // matches rb_materials.h RB_FLAG_GLOW_LINES

// The colour, display-encoded, the raster view shows the lines at: what the traced view
// shows them at in Solid, light lab 79 (about 182, 33, 33 of 255).
const vec3 kGlowLinesShown = vec3(0.71, 0.13, 0.13);

// The glow's weight on a texel of linear albedo: 1 on the drawn lines, 0 on the field.
// Needs linearToSrgb (formulas.glsl) included first.
float glowLinesMask(vec3 albedo)
{
    bool red = albedo.r > 0.0 && albedo.r > 4.0 * max(albedo.g, albedo.b);
    return red ? 1.0 - smoothstep(0.31, 0.35, linearToSrgb(albedo.r)) : 0.0;
}

#endif // GLOW_LINES_GLSL
