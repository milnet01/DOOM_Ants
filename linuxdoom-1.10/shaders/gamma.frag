#version 450
//
// DOOM-0491 — the menu's gamma setting in the 3D tiers, as the frame's last step: the
// finished picture, HUD and menu included, through a curve fitted to the game's own table
// (gamma_curve.h). Level 0 records no pass at all, so the picture is untouched.
// Full-screen triangle from overlay.vert.
//

layout(set = 0, binding = 0) uniform sampler2D finished;
layout(push_constant) uniform Gamma { float exponent; } pc;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3 c = texelFetch(finished, ivec2(gl_FragCoord.xy), 0).rgb;
    outColor = vec4(pow(c, vec3(pc.exponent)), 1.0);
}
