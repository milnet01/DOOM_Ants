#version 450
#extension GL_EXT_nonuniform_qualifier : require
//
// DOOM-0170 L2c — flashlight cast-shadow map, fragment stage (Pass A, §4.4).
//
// Depth is written automatically by the rasteriser; this stage exists only to punch
// alpha-tested cut-outs so monster/item billboards and masked mid-walls (grates/fences)
// cast their real silhouette instead of a solid rectangle. Opaque walls/flats never hit
// the discard. Reuses the bindless material array (set 0 = the shared g.ds set), so the
// id math mirrors mesh.frag exactly.
//
layout(location = 0) in vec2 vUV;
layout(location = 1) flat in int vTexnum;
layout(location = 2) flat in int vFlags;

layout(push_constant) uniform Push {
    mat4 lightVP;
    int  numWall;
    int  numFlat;
} pc;

layout(set = 0, binding = 2) uniform sampler2D materialTex[];

const int FLAG_FLAT   = 0x1;   // matches RB_MESH_FLAT
const int FLAG_MASKED = 0x2;   // matches RB_MESH_MASKED
const int FLAG_SPRITE = 0x4;   // matches RB_MESH_SPRITE

void main()
{
    // Only cut-out surfaces need the test; opaque walls/flats cast solid.
    if ((vFlags & (FLAG_SPRITE | FLAG_MASKED)) == 0)
        return;

    // Unified bindless id, same mapping as mesh.frag (sprites last; a masked mid-wall
    // is a wall, so it uses the raw texnum).
    int id;
    if ((vFlags & FLAG_SPRITE) != 0) id = pc.numWall + pc.numFlat + vTexnum;
    else                             id = vTexnum;

    vec2  sz    = vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
    float index = texture(materialTex[nonuniformEXT(id)], vUV / sz).r * 255.0;
    if (index < 0.5)   // palette index 0 = transparent gap -> no occluder here
        discard;
}
