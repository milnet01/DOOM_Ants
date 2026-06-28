#version 450
//
// DOOM-0008 — 2D HUD/menu compositor (fragment stage).
//
// The engine draws every 2D element (status bar, on-screen messages, the menu,
// intermission/finale screens, the pause pic) into the paletted screens[0]
// buffer. In the 3D back-ends the world goes through Vulkan instead, so that
// buffer is otherwise never shown. This pass draws screens[0] as a full-screen
// layer on top of the rendered 3D scene: the index is decoded through the same
// PLAYPAL LUT the world uses, and the transparent-key index (the 3D view's
// footprint, cleared to it each frame in r_backend.c) is discarded so the 3D
// scene shows through there. Everything the engine painted (HUD/menu/borders)
// is opaque and composites over the world.
//

layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D paletteTex;   // 256x1 PLAYPAL RGB
layout(set = 0, binding = 1) uniform sampler2D overlayTex;   // R8 screens[0] indices

// Transparent key — must match RB_OVERLAY_KEY in r_mesh.h (palette index 251,
// pure magenta, unused by any DOOM HUD/menu/font art).
const float KEY = 251.0;

// Decode a palette index to its RGB (the gamma-encoded PLAYPAL colour the swapchain
// presents directly, same as the raster path's direct output).
vec3 pal(float index)
{
    return texture(paletteTex, vec2((index + 0.5) / 256.0, 0.5)).rgb;
}

// Sharp-bilinear (pixel-art-aware) upscale of the 320x200 paletted overlay. The
// buffer holds palette INDICES, so all filtering happens in RGB AFTER the palette
// decode — bilinearly blending indices would interpolate to unrelated colours. The
// transparent boundary stays exactly as crisp as point sampling: the nearest texel
// alone decides the key/discard, and key texels are excluded from the colour blend so
// magenta never bleeds into a smoothed edge. The fractional coords are compressed into
// a ~1-display-pixel band (fwidth), so texel interiors stay crisp and only the seams
// between texels are anti-aliased — sharper than plain bilinear, smoother than NEAREST.
void main()
{
    vec2 ts = vec2(textureSize(overlayTex, 0));
    vec2 px = vUV * ts - 0.5;            // source-texel space (texel centres at integers)
    vec2 ip = floor(px);
    vec2 fp = px - ip;                   // position within the 2x2 tap quad, [0,1)

    vec2 w = fwidth(px);                 // source texels per display pixel (<1 upscaling)
    fp = clamp((fp - 0.5) / max(w, vec2(1e-4)) + 0.5, 0.0, 1.0);

    // Nearest tap decides transparency (crisp 2D/3D boundary, no key colour bleed).
    ivec2 nq = clamp(ivec2(floor(px + 0.5)), ivec2(0), ivec2(ts) - 1);
    float nIdx = texelFetch(overlayTex, nq, 0).r * 255.0;
    if (abs(nIdx - KEY) < 0.5)
        discard;                         // the 3D view shows through here

    // Bilinear weights across the 2x2 neighbourhood; transparent-key taps excluded.
    ivec2 c = ivec2(ip);
    float wgt[4];
    wgt[0] = (1.0 - fp.x) * (1.0 - fp.y);
    wgt[1] =        fp.x  * (1.0 - fp.y);
    wgt[2] = (1.0 - fp.x) *        fp.y;
    wgt[3] =        fp.x  *        fp.y;
    ivec2 off[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));

    vec3  acc = vec3(0.0);
    float sw  = 0.0;
    for (int i = 0; i < 4; i++) {
        ivec2 t = clamp(c + off[i], ivec2(0), ivec2(ts) - 1);
        float idx = texelFetch(overlayTex, t, 0).r * 255.0;
        if (abs(idx - KEY) < 0.5) continue;        // skip transparent taps
        acc += pal(idx) * wgt[i];
        sw  += wgt[i];
    }
    outColor = vec4(sw > 0.0 ? acc / sw : pal(nIdx), 1.0);
}
