#version 450
// DOOM-0206 v2 — crisp menu skull cursor. Samples an RGBA sprite (the decoded WAD
// M_SKULL1) and outputs it PREMULTIPLIED x per-vertex tint, matching the text
// pipeline's ONE / ONE_MINUS_SRC_ALPHA blend.
layout(set = 0, binding = 0) uniform sampler2D cursorTex;
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 0) out vec4 outColor;
void main()
{
    vec4 c = texture(cursorTex, vUV);      // straight (non-premultiplied) RGBA
    float a = c.a * vColor.a;
    outColor = vec4(c.rgb * vColor.rgb * a, a);
}
