#version 450
//
// DOOM-0008 Stage 1 — primary-visibility vertex shader. Transforms the level
// mesh (r_mesh.c, world units: x east, y north, z up) by the camera's
// view-projection matrix pushed each frame from RB_Vulkan_RenderView.
//
// Per-texel materials slice: the texel coords (uv), the surface's texture id
// (texnum) and its mesh flags are carried flat to the fragment shader, which
// samples the paletted atlas. The sector light comes along for the bring-up
// shade term.
//

layout(push_constant) uniform Push {
    mat4 mvp;   // projection * view, column-major (Vulkan clip space)
} pc;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inUV;
layout(location = 3) in float inLight;
layout(location = 4) in int   inTexnum;
layout(location = 5) in int   inFlags;

layout(location = 0) out vec3  vNormal;
layout(location = 1) out vec2  vUV;
layout(location = 2) out float vLight;
layout(location = 3) flat out int vTexnum;
layout(location = 4) flat out int vFlags;

const int FLAG_PSPRITE = 0x8;   // matches RB_MESH_PSPRITE in r_mesh.h

void main()
{
    // The player-weapon overlay arrives already in clip space (NDC, z=0), so it
    // skips the view-projection; everything else is a world-space mesh vertex.
    if ((inFlags & FLAG_PSPRITE) != 0)
        gl_Position = vec4(inPos, 1.0);
    else
        gl_Position = pc.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vUV     = inUV;
    vLight  = inLight;
    vTexnum = inTexnum;
    vFlags  = inFlags;
}
