#version 450
//
// DOOM-0008 Stage 1 — primary-visibility fragment shader (per-texel materials).
//
// Each surface is sampled from a paletted atlas: the texture's atlas rect is
// looked up by a unified id (walls first, then flats), the UV is tiled within
// the rect, an 8-bit palette index is read from the R8 atlas, and that index is
// decoded to colour through the PLAYPAL lookup texture. Keeping the art paletted
// (index + LUT, not pre-decoded RGB) preserves DOOM's exact colours.
//
// This is still a *bring-up* shader: the fixed-direction Lambert term below is
// placeholder shading to make the 3D read as 3D, NOT a tuned rendering curve. It
// is replaced wholesale by the Workbench-fitted path-traced lighting (DOOM-0009),
// so its constants are exempt from the "no magic constants" rule (INV-7), which
// governs the path tracer's formulas. Likewise colour is written straight to the
// sRGB swapchain (parity with the prior bring-up look); the scene-linear
// workflow lands with the path tracer.
//

layout(location = 0) in vec3  vNormal;
layout(location = 1) in vec2  vUV;
layout(location = 2) in float vLight;
layout(location = 3) flat in int vTexnum;
layout(location = 4) flat in int vFlags;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D atlasTex;     // R8 palette indices
layout(set = 0, binding = 1) uniform sampler2D paletteTex;   // 256x1 PLAYPAL RGB

// Per-texture atlas rects, indexed by the unified id. std430 so each rb_rect_t
// (ox,oy,w,h) maps to one vec4.
layout(set = 0, binding = 2) readonly buffer Atlas {
    vec2 atlasSize;   // atlas dimensions in texels
    int  numWall;     // flats start at this id
    int  numFlat;     // sprites start at numWall + numFlat
    vec4 rects[];     // ox, oy, w, h  (texels)
} atlas;

const int FLAG_FLAT   = 0x1;   // matches RB_MESH_FLAT in r_mesh.h
const int FLAG_SPRITE = 0x4;   // matches RB_MESH_SPRITE in r_mesh.h

void main()
{
    // Unified atlas id: walls [0,numWall), flats [numWall,numWall+numFlat),
    // sprites after. The vertex's texnum is the per-category index.
    int id;
    if ((vFlags & FLAG_SPRITE) != 0)    id = atlas.numWall + atlas.numFlat + vTexnum;
    else if ((vFlags & FLAG_FLAT) != 0) id = atlas.numWall + vTexnum;
    else                                id = vTexnum;
    vec4 rect = atlas.rects[id];

    // Tile the UV within the texture, then map into the atlas. fract keeps the
    // sample strictly inside the rect, so nearest sampling never bleeds across
    // tile borders. (Sprite UVs are pre-inset half a texel, so fract is a no-op
    // for them and the sample never reaches a neighbouring tile.)
    vec2 local    = fract(vUV / rect.zw);
    vec2 atlasUV  = (rect.xy + local * rect.zw) / atlas.atlasSize;

    float index   = texture(atlasTex, atlasUV).r * 255.0;

    // Sprites store transparency as palette index 0 (the gaps between posts);
    // drop those texels so billboards read as cut-outs, not boxes. A genuine
    // index-0 (black) texel inside a sprite is dropped too — DOOM art avoids it.
    if ((vFlags & FLAG_SPRITE) != 0 && index < 0.5)
        discard;

    vec3  albedo  = texture(paletteTex, vec2((index + 0.5) / 256.0, 0.5)).rgb;

    vec3  n     = normalize(vNormal);
    vec3  L     = normalize(vec3(0.3, 0.4, 0.85));   // arbitrary key direction
    float diff  = max(dot(n, L), 0.0);
    float shade = vLight * (0.55 + 0.45 * diff);

    outColor = vec4(albedo * shade, 1.0);
}
