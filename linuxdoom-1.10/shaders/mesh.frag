#version 450
//
// DOOM-0008 Stage 1 — primary-visibility fragment shader.
//
// This is a *bring-up* shader: it makes the converted 3D geometry legible (mesh,
// normals, and per-sector light read correctly) using each surface's real
// average DOOM albedo as a flat-shaded stand-in. The albedo is now genuine
// (sampled per surface from the texture/flat through the palette in r_mesh.c),
// but the fixed-direction Lambert term and its constants are still placeholder
// shading, NOT tuned rendering curves — they are replaced wholesale when the
// per-texel texturing increment and the Workbench-fitted path-traced lighting
// land (DOOM-0009). Hence the Lambert constants are exempt from the "no magic
// constants" rule (INV-7), which governs the path tracer's formulas.
//

layout(location = 0) in vec3  vNormal;
layout(location = 1) in float vLight;
layout(location = 2) in vec3  vAlbedo;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3  n     = normalize(vNormal);
    vec3  L     = normalize(vec3(0.3, 0.4, 0.85));   // arbitrary key direction
    float diff  = max(dot(n, L), 0.0);

    // Sector light sets overall brightness; the Lambert term just gives faces
    // enough variation to read as 3D. The surface's average palette colour is
    // the albedo.
    float shade = vLight * (0.55 + 0.45 * diff);

    outColor = vec4(vAlbedo * shade, 1.0);
}
