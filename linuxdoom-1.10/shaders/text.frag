#version 450
//
// DOOM-0206 (L1b) — display-resolution menu text (fragment stage).
//
// The atlas is single-channel (R8) coverage baked by stb_truetype. The final colour
// is the per-vertex tint modulated by that coverage, output PREMULTIPLIED (rgb already
// scaled by alpha) so the pipeline's ONE / ONE_MINUS_SRC_ALPHA blend composites glyph
// edges and overlapping quads correctly over the paletted 2D overlay beneath.
//
layout(set = 0, binding = 0) uniform sampler2D atlasTex;   // R8 glyph coverage

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;

void main()
{
    float a = vColor.a * texture(atlasTex, vUV).r;
    outColor = vec4(vColor.rgb * a, a);
}
