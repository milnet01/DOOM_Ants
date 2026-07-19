#version 450
//
// DOOM-0206 (L1b) — display-resolution menu text (vertex stage).
//
// Draws one alpha-blended textured quad per glyph, batched by the CPU (rb_text_draw
// in r_vulkan.cpp). Positions arrive in DISPLAY PIXELS with a top-left origin; a
// push-constant invDisplay = (2/dispW, 2/dispH) maps them straight to Vulkan clip
// space (y points down, so pixel-y 0 -> clip -1 = the top of the screen). UV indexes
// the R8 glyph atlas; the per-vertex colour is an R8G8B8A8 tint the frag multiplies in.
//
layout(push_constant) uniform Push { vec2 invDisplay; } pc;

layout(location = 0) in vec2 inPos;    // display pixels, top-left origin
layout(location = 1) in vec2 inUV;     // atlas [0,1]
layout(location = 2) in vec4 inColor;  // R8G8B8A8_UNORM -> normalized rgba

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
    gl_Position = vec4(inPos * pc.invDisplay - vec2(1.0), 0.0, 1.0);
    vUV = inUV;
    vColor = inColor;
}
