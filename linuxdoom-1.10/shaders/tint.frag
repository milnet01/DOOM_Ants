#version 450
//
// DOOM-0379 — Classic's palette flashes (damage red, pickup gold, radsuit green)
// over the 3D tiers' finished frame.
//
// Classic swaps the whole palette; every flash palette is palette 0 scaled and
// shifted per channel (palette_tint.h). So the flash is out = scale * frame + bias,
// done by the blend unit: this outputs `bias` (blend factor ONE) and the pipeline
// multiplies what is already in the target by the blend constants, set to `scale`
// (factor CONSTANT_COLOR). Drawn after the 2D overlay, so the status bar tints too,
// as it does in Classic. Full-screen triangle from overlay.vert.
//

layout(push_constant) uniform Tint { vec4 bias; } pc;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(pc.bias.rgb, 0.0);
}
