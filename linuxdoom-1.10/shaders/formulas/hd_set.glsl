// hd_set.glsl — DOOM-0042: the HD material set, one copy for the traced view
// (pathtrace.comp, set 3) and the rasterised one (mesh.frag, set 2).
// binding 0 = the per-material control table (usePBR flag, map-slot indices, uvScale);
// binding 1 = the bindless RGBA8 PBR map array. Built per level by EnsureHdMaterials
// (r_vulkan.cpp); InitHdDefault seeds an all-paletted set so the set is always valid.
//
// The includer defines HD_SET, the set number, before the #include.
struct MatCtrl {
    int   maps[7];    // [0]alb [1]nrm [2]rgh [3]met [4]ao [5]emis [6]hgt; -1 = none
    float uvScale;
    uint  flags;      // bit0 pom, bit1 noPom, bit2 sprite, bit3 nukage, bit4 lava (DOOM-0183)
    uint  usePBR;     // 0 = paletted fallback
};
layout(set = HD_SET, binding = 0, std430) readonly buffer MatCtrlBuf { MatCtrl ctrl[]; };
layout(set = HD_SET, binding = 1) uniform sampler2D hdTex[];   // bindless RGBA8 PBR maps

// POM march depth (Task 14): the parallax offset spans at most this fraction of one UV tile.
const float kPomHeightScale = 0.06;
