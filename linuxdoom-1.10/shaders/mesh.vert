#version 450
//
// DOOM-0008 Stage 1 — primary-visibility vertex shader. Transforms the level
// mesh (r_mesh.c, world units: x east, y north, z up) by the camera's
// view-projection matrix pushed each frame from RB_Vulkan_RenderView.
//
// Only the attributes this bring-up pass needs are bound (position, normal,
// sector light); UV / texnum / material attributes are consumed by the
// textured + path-traced increments that follow.
//

layout(push_constant) uniform Push {
    mat4 mvp;   // projection * view, column-major (Vulkan clip space)
} pc;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in float inLight;

layout(location = 0) out vec3  vNormal;
layout(location = 1) out float vLight;

void main()
{
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vLight  = inLight;
}
