#version 450
//
// DOOM-0008 — 2D HUD/menu compositor (fragment stage).
//
// The engine draws every 2D element (status bar, on-screen messages, the menu,
// intermission/finale screens, the pause pic) into the paletted screens[0]
// buffer. In the 3D back-ends the world goes through Vulkan instead, so that
// buffer is otherwise never shown. This pass draws screens[0] as a full-screen
// layer on top of the rendered 3D scene: the index is decoded through the same
// PLAYPAL LUT the world uses, and the transparent-key index (the 3D view's
// footprint, cleared to it each frame in r_backend.c) is discarded so the 3D
// scene shows through there. Everything the engine painted (HUD/menu/borders)
// is opaque and composites over the world.
//

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D paletteTex;   // 256x1 PLAYPAL RGB
layout(set = 0, binding = 3) uniform sampler2D overlayTex;   // R8 screens[0] indices

// Transparent key — must match RB_OVERLAY_KEY in r_mesh.h (palette index 251,
// pure magenta, unused by any DOOM HUD/menu/font art).
const float KEY = 251.0;

void main()
{
    float index = texture(overlayTex, vUV).r * 255.0;
    if (abs(index - KEY) < 0.5)
        discard;                         // the 3D view shows through here
    vec3 rgb = texture(paletteTex, vec2((index + 0.5) / 256.0, 0.5)).rgb;
    outColor = vec4(rgb, 1.0);
}
