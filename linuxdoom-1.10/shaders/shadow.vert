#version 450
//
// DOOM-0170 L2c — flashlight cast-shadow map, vertex stage (Pass A, §4.4).
//
// Renders the world mesh depth-only from the flashlight's point of view so mesh.frag
// can later test each fragment against it (3x3 PCF) and shadow the DOOM-0044 raster
// flashlight cone. lightVP is the light's view-projection, built CPU-side each torch-on
// frame exactly like the camera matrix (Mat4LookAt * Mat4PerspectiveH), at the eye and
// along the view yaw.
//
// Exclusions: the NDC weapon psprite and the sky (flat backdrop + world dome) must never
// cast, so their verts are pushed past the far plane (z > w) and clipped away. Ordinary
// walls/flats AND monster/item billboards fall through and cast their true silhouette
// (the fragment stage alpha-tests the cut-outs).
//
layout(push_constant) uniform Push {
    mat4 lightVP;
    int  numWall;   // material-id offsets, so the fragment stage maps texnum -> bindless id
    int  numFlat;
} pc;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;   // unused; binding layout must match mesh.vert
layout(location = 2) in vec2  inUV;
layout(location = 3) in float inLight;     // unused
layout(location = 4) in int   inTexnum;
layout(location = 5) in int   inFlags;

layout(location = 0) out vec2 vUV;
layout(location = 1) flat out int vTexnum;
layout(location = 2) flat out int vFlags;

const int FLAG_PSPRITE = 0x8;   // matches RB_MESH_PSPRITE in r_mesh.h
const int FLAG_SKY     = 0x10;  // matches RB_MESH_SKY
const int FLAG_SKYDOME = 0x40;  // matches RB_MESH_SKYDOME

void main()
{
    vUV     = inUV;
    vTexnum = inTexnum;
    vFlags  = inFlags;
    // Weapon psprite (NDC) + sky must not cast: emit a clipped vertex (z > w).
    if ((inFlags & (FLAG_PSPRITE | FLAG_SKY | FLAG_SKYDOME)) != 0)
    {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        return;
    }
    gl_Position = pc.lightVP * vec4(inPos, 1.0);
}
