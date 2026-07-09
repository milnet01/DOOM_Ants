#version 450
#extension GL_EXT_buffer_reference     : require
#extension GL_EXT_scalar_block_layout  : require
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

// DOOM-0170 L1a: the fragment stage reads baked GI probes through these; the vertex
// stage ignores them, but the push block must match mesh.frag byte-for-byte.
layout(buffer_reference, scalar) readonly buffer ProbesRO { float p[]; };
layout(buffer_reference, scalar) readonly buffer TriSs    { uint  s[]; };

layout(push_constant) uniform Push {
    mat4  mvp;          // projection * view, column-major (Vulkan clip space)
    float extralight;   // muzzle-flash view brighten, added to every shade [0,1]
    float yaw;          // view yaw (radians); the sky fragment path pans by it
    float eyeX;         // camera world position (separate floats: tight push-
    float eyeY;         // constant packing, no vec3 16-byte alignment hole);
    float eyeZ;         // drives the distance light falloff in the fragment shader
    int   numWall;      // material-id offsets (fragment-only; the vertex stage
    int   numFlat;      // ignores them, but the block must match mesh.frag's)
    float flashlight;   // DOOM-0044 headlamp on/off (1=on); Solid raster cone
    ProbesRO probes;    // DOOM-0170 L1a (fragment-only; block must match mesh.frag)
    TriSs    triSs;
    uint     probeCount;
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
// noperspective: this is a screen-space quantity (sky panorama lookup), so it must
// interpolate linearly in window space, not perspective-correctly. That recovers
// each fragment's true screen pixel for the projected DOOM-0162 sky dome as well as
// the flat NDC backdrop quad (see the vScreenUV assignment below).
layout(location = 5) noperspective out vec2 vScreenUV;   // [0,1] across the frame; sky uses it
layout(location = 6) out float vDist;      // world distance camera->vertex (falloff)
layout(location = 7) out vec3  vWorldPos;  // world position; DOOM-0044 flashlight cone

const int FLAG_PSPRITE = 0x8;   // matches RB_MESH_PSPRITE in r_mesh.h
const int FLAG_SKY     = 0x10;  // matches RB_MESH_SKY in r_mesh.h (NDC backdrop quad)
const int FLAG_SKYDOME = 0x40;  // matches RB_MESH_SKYDOME (world-space sky occluder)

void main()
{
    // The weapon overlay (FLAG_PSPRITE) and the full-screen sky backdrop
    // (FLAG_SKY) arrive already in clip space (NDC, z=0), so they skip the
    // view-projection. Everything else -- ordinary world geometry AND the
    // DOOM-0162 world-space sky dome (FLAG_SKYDOME) -- is a world-space vertex
    // and runs through the MVP so it lands at the right depth and can occlude.
    vec4 clip = ((inFlags & (FLAG_PSPRITE | FLAG_SKY)) != 0)
              ? vec4(inPos, 1.0)
              : pc.mvp * vec4(inPos, 1.0);
    gl_Position = clip;
    vNormal = inNormal;
    vUV     = inUV;
    // Muzzle flash brightens the whole view; fullbright surfaces stay clamped at 1.
    vLight  = clamp(inLight + pc.extralight, 0.0, 1.0);
    vTexnum = inTexnum;
    vFlags  = inFlags;
    // Screen position in [0,1] for the sky panorama lookup. Taken from the clip xy
    // so it is correct for BOTH the NDC backdrop quad (w=1 -> inPos.xy*0.5+0.5, the
    // old formula) and the perspective-projected sky dome. Combined with the
    // noperspective qualifier, this gives every fragment its true screen pixel, so
    // the dome samples exactly the same sky the backdrop would at that pixel.
    // (Vulkan y points down, so .y=0 is the top.)
    vScreenUV = clip.xy / clip.w * 0.5 + 0.5;
    // World distance from the camera for the fragment's distance light falloff.
    // Meaningless for NDC psprite/sky verts; the fragment shader ignores it there.
    vDist = length(inPos - vec3(pc.eyeX, pc.eyeY, pc.eyeZ));
    // World position for the fragment's flashlight cone (DOOM-0044). Meaningless
    // for NDC psprite/sky verts; the fragment shader excludes those from the cone.
    vWorldPos = inPos;
}
