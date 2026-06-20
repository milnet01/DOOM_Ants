#version 450
//
// DOOM-0008 Stage 1 — primary-visibility vertex shader. Transforms the level
// mesh (r_mesh.c, world units: x east, y north, z up) by the camera's
// view-projection matrix pushed each frame from RB_Vulkan_RenderView.
//
// Position, normal, sector light and per-surface average albedo are bound; the
// UV / texnum attributes are consumed by the per-texel texturing + path-traced
// increments that follow.
//

layout(push_constant) uniform Push {
    mat4 mvp;   // projection * view, column-major (Vulkan clip space)
} pc;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in float inLight;
layout(location = 3) in vec3  inAlbedo;

layout(location = 0) out vec3  vNormal;
layout(location = 1) out float vLight;
layout(location = 2) out vec3  vAlbedo;

void main()
{
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vLight  = inLight;
    vAlbedo = inAlbedo;
}
