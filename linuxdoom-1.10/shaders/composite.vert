#version 450
//
// DOOM-0170 L2a — composite/tonemap pass vertex stage. Emits a single full-screen
// triangle from gl_VertexIndex (no vertex buffer) and hands the fragment stage the
// [0,1] screen UV to sample the off-screen scene colour target. (Same vertexless
// full-screen-tri trick as overlay.vert.)
//
layout(location = 0) out vec2 vUV;

void main()
{
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
