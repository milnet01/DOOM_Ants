#version 450
//
// DOOM-0170 L2d — blob-shadow fragment stage.
//
// Draws a soft dark radial oval on the floor beneath a Thing (a cheap grounding
// shadow, §4.5). The quad's four corners carry [-1,1] "radial" UV (RB_BuildBlobs),
// so |uv| is 0 at the centre and 1 at the rim; that becomes a smooth falloff. The
// decal fades with the owning sector's light so an already-dark room never gets a
// hard black spot. The stage outputs black with a falloff alpha; the blob pipeline
// alpha-blends it (SRC_ALPHA, ONE_MINUS_SRC_ALPHA) so it only darkens the floor.
//
// Paired with blob.vert (the blob pipeline's own vertex stage) — this stage reads
// just the interpolated radial UV and the sector light from that stage's outputs.
//
layout(location = 1) in vec2  vUV;      // [-1,1] across the quad (radial coord)
layout(location = 2) in float vLight;   // owning sector lightlevel, 0..1

// DOOM-0170 L2b — the scene pass has two colour attachments. A blob is an opaque grounding
// shadow: the thing blocks light reaching the floor from ALL sources, so it darkens BOTH the
// AMBIENT and the DIRECT target (same alpha to each; the pipeline alpha-blends both). Darkening
// ambient-only made blobs vanish on emitter-lit floors where DIRECT dominates.
layout(location = 0) out vec4 outAmbient;
layout(location = 1) out vec4 outDirect;

const float BLOB_STRENGTH = 0.55;       // peak darkening at the centre, in full light

void main()
{
    float r = length(vUV);
    if (r >= 1.0)
        discard;                        // outside the oval — nothing to darken
    // Solid core out to 0.35, then a soft feathered rim to 0 at the edge.
    float falloff = 1.0 - smoothstep(0.35, 1.0, r);
    float alpha   = falloff * BLOB_STRENGTH * vLight;
    outAmbient = vec4(0.0, 0.0, 0.0, alpha);
    outDirect  = vec4(0.0, 0.0, 0.0, alpha);   // darken DIRECT too -> total floor light drops
}
