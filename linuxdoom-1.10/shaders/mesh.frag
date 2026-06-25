#version 450
#extension GL_EXT_nonuniform_qualifier : require
//
// DOOM-0008 Stage 1 — primary-visibility fragment shader (per-texel materials).
//
// Each surface samples one paletted material from a bindless array of R8 images
// (DOOM-0009 build step 1): the surface's unified id (walls first, then flats,
// then sprites) indexes materialTex[id], native REPEAT tiling wraps the UV, an
// 8-bit palette index is read, and that index is decoded to colour through the
// PLAYPAL lookup texture. One image per material (its own size, REPEAT address
// mode) replaces the single packed atlas + manual fract() wrap, so the future
// ray/path tracer can light each surface's real material by hit id. Keeping the
// art paletted (index + LUT, not pre-decoded RGB) preserves DOOM's exact colours.
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
layout(location = 5) in vec2  vScreenUV;   // [0,1] across the frame (sky only)
layout(location = 6) in float vDist;       // world distance camera->fragment

layout(location = 0) out vec4 outColor;

// Must match mesh.vert's block byte-for-byte (shared push-constant range). The
// sky path reads pc.yaw to pan the panorama; numWall/numFlat give the material-id
// offsets (walls|flats|sprites); eyeX/Y/Z are vertex-only (folded into vDist
// there); the rest are vertex-only.
layout(push_constant) uniform Push {
    mat4  mvp;
    float extralight;
    float yaw;          // view yaw, radians
    float eyeX;
    float eyeY;
    float eyeZ;
    int   numWall;      // flats start at this id; sprites at numWall + numFlat
    int   numFlat;
} pc;

layout(set = 0, binding = 0) uniform sampler2D paletteTex;   // 256x1 PLAYPAL RGB

// Bindless material array: one R8 palette-index image per material (wall/flat/
// sprite), sized to itself, REPEAT-addressed. Indexed by the unified id; the id
// is per-fragment-divergent within a draw, so every access is nonuniformEXT-
// qualified. Unsized -> runtimeDescriptorArray (core Vulkan 1.2).
layout(set = 0, binding = 2) uniform sampler2D materialTex[];

const int FLAG_FLAT    = 0x1;   // matches RB_MESH_FLAT in r_mesh.h
const int FLAG_MASKED  = 0x2;   // matches RB_MESH_MASKED in r_mesh.h
const int FLAG_SPRITE  = 0x4;   // matches RB_MESH_SPRITE in r_mesh.h
const int FLAG_PSPRITE = 0x8;   // matches RB_MESH_PSPRITE in r_mesh.h
const int FLAG_SKY     = 0x10;  // matches RB_MESH_SKY in r_mesh.h

const float PI = 3.14159265358979;

void main()
{
    // Sky backdrop: a cylindrical panorama keyed on the view yaw, exactly like
    // classic DOOM. The sky is a wall texture (atlas id == texnum). 90 deg of
    // view spans one texture width; screen x maps to a ray-yaw offset via atan
    // (perspective, 90 deg horizontal FOV -> tan(45)=1), and pc.yaw pans it as
    // the player turns. Vertically the 128px sky fills the top half of the
    // frame with the horizon at screen centre. Fullbright, so no shade term and
    // the muzzle flash never touches it.
    if ((vFlags & FLAG_SKY) != 0)
    {
        int   id    = vTexnum;                  // the sky is a wall texture
        vec2  sz    = vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
        float ndcX  = vScreenUV.x * 2.0 - 1.0;
        // DOOM's sky column is viewangle + xtoviewangle[x], and xtoviewangle is
        // +left/-right; ndcX is -left/+right, so the screen term is -atan(ndcX).
        float ang   = pc.yaw - atan(ndcX);
        float col   = ang / (PI * 0.5) * sz.x;  // REPEAT wraps the column
        float row   = clamp(vScreenUV.y * 2.0 * sz.y, 0.0, sz.y - 1.0);
        vec2  uv    = vec2(col, row) / sz;      // u tiles (REPEAT), v stays clamped
        float idx   = texture(materialTex[nonuniformEXT(id)], uv).r * 255.0;
        outColor    = vec4(texture(paletteTex, vec2((idx + 0.5) / 256.0, 0.5)).rgb, 1.0);
        return;
    }

    // Unified material id: walls [0,numWall), flats [numWall,numWall+numFlat),
    // sprites after. The vertex's texnum is the per-category index.
    int id;
    if ((vFlags & FLAG_SPRITE) != 0)    id = pc.numWall + pc.numFlat + vTexnum;
    else if ((vFlags & FLAG_FLAT) != 0) id = pc.numWall + vTexnum;
    else                                id = vTexnum;

    // Native REPEAT tiling: divide the raw texel UV by the material's own size
    // and let the sampler wrap. (Sprite UVs are pre-inset half a texel and stay
    // in range, so REPEAT is a no-op for them — no bleed across a tile border.)
    vec2  sz      = vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
    float index   = texture(materialTex[nonuniformEXT(id)], vUV / sz).r * 255.0;

    // Sprites and two-sided masked mid-walls (grates/fences) store transparency
    // as palette index 0 (the gaps between posts); drop those texels so they
    // read as cut-outs, not boxes. A genuine index-0 (black) texel inside such
    // art is dropped too — DOOM art avoids it.
    if ((vFlags & (FLAG_SPRITE | FLAG_MASKED)) != 0 && index < 0.5)
        discard;

    vec3  albedo  = texture(paletteTex, vec2((index + 0.5) / 256.0, 0.5)).rgb;

    vec3  n     = normalize(vNormal);
    vec3  L     = normalize(vec3(0.3, 0.4, 0.85));   // arbitrary key direction
    float diff  = max(dot(n, L), 0.0);

    // DOOM-style distance light diminishing: far surfaces fade toward dark, the
    // depth cue the flat sector-light pass lacks (near stays at full sector
    // light, so this adds near/far contrast rather than darkening everything).
    // The weapon psprite is NDC (vDist meaningless), so it keeps full brightness.
    float distLight = 1.0;
    if ((vFlags & FLAG_PSPRITE) == 0)
        distLight = clamp(1.0 - vDist / 3000.0, 0.35, 1.0);

    float shade = vLight * distLight * (0.55 + 0.45 * diff);

    outColor = vec4(albedo * shade, 1.0);
}
