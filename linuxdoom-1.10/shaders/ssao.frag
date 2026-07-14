#version 450
//
// DOOM-0170 L2b — SSAO (screen-space ambient occlusion / contact shadows), §4.3.
//
// Runs at HALF resolution after the scene pass. The main pass packs a linear
// forward-distance depth into the DIRECT target's alpha (the raster camera is yaw-only,
// so that distance is exactly the projection's clip.w); this stage reconstructs a
// view-space position for each pixel from that depth alone — no depth buffer, no matrices,
// just the fixed 90-degree FOV (tanH) and the frame aspect. It samples a 16-point hemisphere
// kernel oriented by the reconstructed normal and outputs an occlusion factor: 1.0 = open,
// ->0 in a tight corner / where a nearer surface crowds the sample. The composite blurs this
// and multiplies it into the AMBIENT term only (§4.3), so directly-lit surfaces never darken.
//
layout(location = 0) in  vec2  vUV;
layout(location = 0) out float outAO;

layout(set = 0, binding = 0) uniform sampler2D directTex;   // .a = forward-distance linear depth

layout(push_constant) uniform Push {
    vec2  uvScale;    // the render-scaled corner of the scene target the world filled (L2a)
    float tanH;       // tan(hfov/2) — 0.5*horizontal FOV; DOOM's is 90deg so this is 1.0
    float aspect;     // frame width / height
    float radius;     // hemisphere sample radius, world units
    float bias;       // depth bias to kill self-occlusion acne, world units
    float intensity;  // occlusion strength (higher = darker corners)
    float power;      // contrast exponent on the final factor
} pc;

const int KERNEL = 16;

// Fixed hemisphere kernel (tangent space, +z = surface normal). Normalized in use; each
// sample's radius accelerates with the index so taps cluster near the origin (better contact
// definition) then reach out — the standard hemisphere-kernel weighting without a UBO upload.
const vec3 KERN[16] = vec3[16](
    vec3( 0.53,  0.18,  0.83), vec3(-0.62, -0.20,  0.75), vec3( 0.10,  0.61,  0.78),
    vec3(-0.15, -0.55,  0.82), vec3( 0.74, -0.42,  0.52), vec3(-0.70,  0.45,  0.55),
    vec3( 0.34,  0.72,  0.60), vec3(-0.28, -0.80,  0.53), vec3( 0.88,  0.10,  0.46),
    vec3(-0.85, -0.05,  0.52), vec3( 0.05,  0.20,  0.98), vec3(-0.10,  0.08,  0.99),
    vec3( 0.45, -0.30,  0.84), vec3(-0.40,  0.35,  0.85), vec3( 0.20, -0.10,  0.97),
    vec3(-0.22,  0.14,  0.96)
);

// Forward-distance linear depth at a frame uv (uv in [0,1] over the whole frame; the world
// only filled the [0,uvScale] corner of the target, so scale into it).
float sampleZ(vec2 uv) { return texture(directTex, uv * pc.uvScale).a; }

// Reconstruct a view-space position (x right, y up, z = forward distance) from a frame uv + z.
// Inverse of the fixed-FOV projection: ndc.x = vx/(z*tanH), ndc.y = -aspect*vy/(z*tanH).
vec3 viewPos(vec2 uv, float z)
{
    vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * z * pc.tanH, -ndc.y * z * pc.tanH / pc.aspect, z);
}

// Project a view-space position back to a frame uv (the forward map of viewPos()).
vec2 projUV(vec3 p)
{
    float ndcX =  p.x / (p.z * pc.tanH);
    float ndcY = -p.y * pc.aspect / (p.z * pc.tanH);
    return vec2(ndcX, ndcY) * 0.5 + 0.5;
}

// Interleaved gradient noise (Jimenez 2014) -> a per-pixel rotation angle, so the 16 taps
// cover more directions than a fixed kernel and the composite's blur cleans the dither.
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main()
{
    float z = sampleZ(vUV);
    if (z >= 50000.0) { outAO = 1.0; return; }   // sky / far backdrop: never occluded
    if (z <= 0.0)     { outAO = 1.0; return; }   // sprite billboard (negative-tagged): no AO on monsters/items

    vec3 P = viewPos(vUV, z);
    // View normal from screen-space derivatives of the reconstructed position. Cheap, and it
    // avoids a dedicated normal target; slightly noisy at silhouettes, which the range check
    // and the composite blur absorb.
    vec3 N = normalize(cross(dFdx(P), dFdy(P)));

    // Random rotation basis (TBN) around the normal.
    float ang  = ign(gl_FragCoord.xy) * 6.2831853;
    vec3  rvec = vec3(cos(ang), sin(ang), 0.0);
    vec3  T    = normalize(rvec - N * dot(rvec, N));
    vec3  B    = cross(N, T);
    mat3  TBN  = mat3(T, B, N);

    float occ = 0.0;
    for (int i = 0; i < KERNEL; i++)
    {
        // Accelerating radius: taps bunch near the contact then spread out.
        float s  = mix(0.1, 1.0, float(i) / float(KERNEL - 1));
        s = s * s;
        vec3  sp  = P + TBN * normalize(KERN[i]) * (s * pc.radius);
        vec2  suv = projUV(sp);
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0))))
            continue;
        float sceneZ = sampleZ(suv);   // real geometry depth at that screen position
        if (sceneZ <= 0.0)
            continue;                  // sprite billboard: never an occluder (no halo around monsters)
        // Occluded when real geometry sits closer to the camera than the sample point; the
        // range check fades out taps whose depth gap is larger than the radius (stops distant
        // background from darkening a foreground pixel across a silhouette).
        float rangeCheck = smoothstep(0.0, 1.0, pc.radius / max(abs(z - sceneZ), 1e-4));
        occ += ((sceneZ < sp.z - pc.bias) ? 1.0 : 0.0) * rangeCheck;
    }
    occ = occ / float(KERNEL);
    outAO = pow(clamp(1.0 - occ * pc.intensity, 0.0, 1.0), pc.power);
}
