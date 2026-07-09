#version 450
//
// DOOM-0170 L2a — composite/tonemap pass fragment stage. Samples the off-screen
// scene colour target and writes it to the swapchain. This is the seam where the
// HDR tone-map lands in the next step (L2a step 2): for now it is a straight
// pass-through so the picture is byte-for-byte the current look while the off-screen
// canvas + composite plumbing is proven end-to-end.
//
layout(location = 0) in  vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

// DOOM-0170 L2a step 2: the world is drawn into the [0,uvScale] corner of a
// full-size scene target (render-scale sub-rectangle). Sample that corner and let
// the linear+clamp sampler upscale it to the full swapchain. uvScale = (1,1) at
// 100% render scale, so the picture is byte-identical to the full-res path.
layout(push_constant) uniform Push { vec2 uvScale; } pc;

void main()
{
    outColor = vec4(texture(sceneTex, vUV * pc.uvScale).rgb, 1.0);
}
