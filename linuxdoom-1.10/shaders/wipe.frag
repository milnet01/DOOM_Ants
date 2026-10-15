#version 450
//
// DOOM-0379 — Classic's screen melt over the 3D tiers' world.
//
// The CPU melt (f_wipe.c) already moves screens[0]'s two-pixel columns, so the 2D
// overlay melts by itself. What it cannot move is the 3D world under the overlay's
// transparent key. So when a melt starts the old world is kept in an image, and this
// pass draws it over the new world, each column moved down by the same amount as the
// melt's column. Above that point the new world shows. The overlay is drawn after, and
// covers every pixel that is not the key, exactly as on any other frame.
// Full-screen triangle from overlay.vert; the column maths is in framebuffer pixels.
//

layout(set = 0, binding = 0) uniform sampler2D oldWorld;   // the world when the melt began
layout(std430, set = 0, binding = 1) readonly buffer Melt { int y[]; } melt;

// cols: two-pixel columns in screens[0]; rows: screens[0]'s height. melt.y[i] is how
// far column i has moved down, in those rows (below zero: not yet moving).
layout(push_constant) uniform Wipe { int cols; int rows; } pc;

layout(location = 0) out vec4 outColor;

void main()
{
    ivec2 size = textureSize(oldWorld, 0);
    ivec2 p    = ivec2(gl_FragCoord.xy);
    int   col  = min(p.x * pc.cols / size.x, pc.cols - 1);
    int   down = max(melt.y[col], 0) * size.y / pc.rows;
    if (p.y < down)
        discard;                         // the melt has passed: the new world shows
    outColor = vec4(texelFetch(oldWorld, ivec2(p.x, p.y - down), 0).rgb, 1.0);
}
