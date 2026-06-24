#version 450
//
// DOOM-0008 — 2D HUD/menu compositor (vertex stage).
//
// A vertexless full-screen triangle generated from gl_VertexIndex (draw 3
// vertices, no vertex buffer). The triangle's clip positions (-1,-1),(3,-1),
// (-1,3) cover the whole framebuffer; the matching UVs (0,0),(2,0),(0,2)
// interpolate to [0,1] across it. In Vulkan clip space y points down, so
// clip (-1,-1) is the framebuffer's top-left — UV (0,0) there samples row 0
// of the 320x200 (HIRES-scaled) screens[0] overlay, i.e. the top of the HUD.
//

layout(location = 0) out vec2 vUV;

void main()
{
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
    vUV = p;
}
