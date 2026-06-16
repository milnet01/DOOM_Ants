#version 450
//
// DOOM-0008 Stage 1 — primary-visibility fragment shader.
//
// This is a *bring-up* shader: its job is only to make the converted 3D
// geometry legible on screen (so the mesh, normals, and per-sector light read
// correctly) before real materials exist. It deliberately uses a neutral grey
// albedo and a trivial fixed-direction Lambert term — the constants below are
// placeholder shading, NOT tuned rendering curves, and are replaced wholesale
// when textured materials and the Workbench-fitted path-traced lighting land
// (DOOM-0008 materials increment / DOOM-0009). Hence they are exempt from the
// "no magic constants" rule (INV-7), which governs the path tracer's formulas.
//

layout(location = 0) in vec3  vNormal;
layout(location = 1) in float vLight;

layout(location = 0) out vec4 outColor;

void main()
{
    vec3  n     = normalize(vNormal);
    vec3  L     = normalize(vec3(0.3, 0.4, 0.85));   // arbitrary key direction
    float diff  = max(dot(n, L), 0.0);

    // Sector light sets overall brightness; the Lambert term just gives faces
    // enough variation to read as 3D. Neutral grey stands in for albedo.
    float shade = vLight * (0.55 + 0.45 * diff);
    vec3  base  = vec3(0.72);

    outColor = vec4(base * shade, 1.0);
}
