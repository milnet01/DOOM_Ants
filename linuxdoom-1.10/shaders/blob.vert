#version 450
//
// DOOM-0170 L2d — blob-shadow vertex stage.
//
// Transforms a floor decal quad (RB_BuildBlobs, world space) by the world
// view-projection and passes ONLY the radial UV and the owning sector light to
// blob.frag. A minimal interface (vs. reusing mesh.vert) so the blob pipeline has
// no unconsumed vertex-output warnings. Shares the world pipeline layout's push
// range — mvp is its first 64 bytes.
//
layout(push_constant) uniform Push { mat4 mvp; } pc;

layout(location = 0) in vec3  inPos;    // world position of the quad corner
layout(location = 2) in vec2  inUV;     // [-1,1] radial coord
layout(location = 3) in float inLight;  // owning sector lightlevel, 0..1

layout(location = 1) out vec2  vUV;
layout(location = 2) out float vLight;

void main()
{
    gl_Position = pc.mvp * vec4(inPos, 1.0);
    vUV    = inUV;
    vLight = inLight;
}
