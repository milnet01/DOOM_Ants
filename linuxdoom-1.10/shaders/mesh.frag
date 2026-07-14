#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference     : require
#extension GL_EXT_scalar_block_layout  : require
//
// DOOM-0008 Stage 1 — primary-visibility fragment shader (per-texel materials).
//
// Each surface samples one paletted material from a bindless array of R8 images
// (DOOM-0009 build step 1): the surface's unified id (walls first, then flats,
// then sprites) indexes materialTex[id], native REPEAT tiling wraps the UV, an
// 8-bit palette index is read, and that index is decoded to colour through the
// PLAYPAL lookup texture. One image per material (its own size, REPEAT address
// mode) replaces the single packed atlas + manual fract() wrap, so the future
// ray/path tracer can light each surface's real material by hit id. Keeping the
// art paletted (index + LUT, not pre-decoded RGB) preserves DOOM's exact colours.
//
// This is still a *bring-up* shader: the fixed-direction Lambert term below is
// placeholder shading to make the 3D read as 3D, NOT a tuned rendering curve. It
// is replaced wholesale by the Workbench-fitted path-traced lighting (DOOM-0009),
// so its constants are exempt from the "no magic constants" rule (INV-7), which
// governs the path tracer's formulas. Likewise colour is written straight to the
// sRGB swapchain (parity with the prior bring-up look); the scene-linear
// workflow lands with the path tracer.
//

layout(location = 0) in vec3  vNormal;
layout(location = 1) in vec2  vUV;
layout(location = 2) in float vLight;
layout(location = 3) flat in int vTexnum;
layout(location = 4) flat in int vFlags;
// noperspective must match mesh.vert: vScreenUV is a screen-space sky-panorama
// lookup, interpolated linearly in window space so the projected DOOM-0162 sky
// dome resolves to the same per-pixel sky as the flat NDC backdrop quad.
layout(location = 5) noperspective in vec2  vScreenUV;   // [0,1] across the frame (sky only)
layout(location = 6) in float vDist;       // world distance camera->fragment
layout(location = 7) in vec3  vWorldPos;   // world position (DOOM-0044 flashlight)

// DOOM-0170 L2b — two colour outputs feeding the scene pass's two targets. outAmbient (0)
// is DOOM's non-directional light (sector light × distance + baked indirect bounce) — the
// term SSAO darkens (§4.3). outDirect (1) is the directional light (the DOOM-0044 flashlight
// cone + the §4.1 point lights) plus the FULL colour of sprites/sky/psprite, so ambient
// occlusion never dims a monster, the weapon, the sky, or a directly-lit surface. The
// composite recombines them as DIRECT + AO×AMBIENT; before L2b-2's AO it is a plain sum,
// so the split is invisible. Splitting HERE is the reason the scene pass grew a 2nd target.
//
// SINGLE_TARGET variant (mesh_overlay.frag.spv, built with -DSINGLE_TARGET): the RT weapon
// overlay draws the weapon into the single-attachment swapchain pass, so that build writes
// the COMBINED colour (ambient + direct) to one location — the weapon looks exactly as it did
// before the split, keeping the RT view byte-for-byte unchanged (INV-4).
#ifdef SINGLE_TARGET
layout(location = 0) out vec4 outColor;
#else
layout(location = 0) out vec4 outAmbient;
layout(location = 1) out vec4 outDirect;
#endif

// DOOM-0170 L1a — baked GI probes, read in the RASTER (RT-off) path so rooms get
// the same soft coloured indirect bounce the path tracer bakes. Layout matches
// pt_common.glsl exactly (ProbesRO: 16 floats/probe = pos[3] pad + SH-L1 R[4] G[4]
// B[4]; TriSs: per-triangle primitive id -> subsector/probe index).
layout(buffer_reference, scalar) readonly buffer ProbesRO { float p[]; };
layout(buffer_reference, scalar) readonly buffer TriSs    { uint  s[]; };

// DOOM-0170 L1b — per-subsector dynamic point lights derived from the same NEE
// emitter list (torches/lamps/emissive walls). Flat float array: RASTER_MAX_LIGHTS
// slots per subsector, 6 floats/light = centroid[3] Le[3]. Empty slots are zero and
// packed nearest-first, so the shader stops at the first zero-Le slot.
layout(buffer_reference, scalar) readonly buffer LightsRO { float d[]; };

// Must match mesh.vert's block byte-for-byte (shared push-constant range). The
// sky path reads pc.yaw to pan the panorama; numWall/numFlat give the material-id
// offsets (walls|flats|sprites); eyeX/Y/Z are vertex-only (folded into vDist
// there); the rest are vertex-only.
layout(push_constant) uniform Push {
    mat4  mvp;
    float extralight;
    float yaw;          // view yaw, radians
    float eyeX;
    float eyeY;
    float eyeZ;
    int   numWall;      // flats start at this id; sprites at numWall + numFlat
    int   numFlat;
    float flashlight;   // DOOM-0044 headlamp on/off (1=on); Solid raster cone
    // DOOM-0170 L1a/L1b: baked SH-L1 GI probes + per-subsector point lights read in
    // raster (RT-off). 64-bit device addresses (8-byte aligned; byte offset 96/104/112,
    // probeCount at 120) keep the block at 124 B — under the 128 B guaranteed floor.
    ProbesRO probes;    // baked per-subsector SH-L1 radiance (0 if no bake)
    TriSs    triSs;      // per-triangle primitive id -> subsector/probe index
    LightsRO lights;     // DOOM-0170 L1b: per-subsector nearest-N point lights
    uint     probeCount; // subsector/probe count; bounds triSs+lights, 0 disables both
} pc;

layout(set = 0, binding = 0) uniform sampler2D paletteTex;   // 256x1 PLAYPAL RGB

// Bindless material array: one R8 palette-index image per material (wall/flat/
// sprite), sized to itself, REPEAT-addressed. Indexed by the unified id; the id
// is per-fragment-divergent within a draw, so every access is nonuniformEXT-
// qualified. Unsized -> runtimeDescriptorArray (core Vulkan 1.2).
layout(set = 0, binding = 2) uniform sampler2D materialTex[];

// DOOM-0170 L2c — flashlight cast-shadow map (§4.4). shadowMap is the scene depth rendered
// from the torch's viewpoint (Pass A, shadow.vert/frag); lightVP transforms a world
// fragment into that light's clip space for a 3x3 PCF compare. Only the flashlight's
// additive term is shadowed (the base sector light is untouched), and only when the torch
// is on — when it is off the shadow pass is skipped and this is never sampled.
layout(set = 1, binding = 0) uniform sampler2D shadowMap;
layout(set = 1, binding = 1) uniform ShadowUbo { mat4 lightVP; } shadow;

const int FLAG_FLAT    = 0x1;   // matches RB_MESH_FLAT in r_mesh.h
const int FLAG_MASKED  = 0x2;   // matches RB_MESH_MASKED in r_mesh.h
const int FLAG_SPRITE  = 0x4;   // matches RB_MESH_SPRITE in r_mesh.h
const int FLAG_PSPRITE = 0x8;   // matches RB_MESH_PSPRITE in r_mesh.h
const int FLAG_SKY     = 0x10;  // matches RB_MESH_SKY in r_mesh.h (NDC backdrop quad)
const int FLAG_SKYDOME = 0x40;  // matches RB_MESH_SKYDOME (world-space sky occluder)

const float PI = 3.14159265358979;

// DOOM-0170 L1a bounce strength (§9 Q1 tuning dial). 1.0 = the physically-correct
// baked indirect, matching the RT view (which reads the same probes). The bake's
// indirect irradiance is low (mean DC ~0.013 on E1M1), so this fill is faint on top
// of DOOM's bright flat sector light — the visible lift comes from L1b's direct
// point lights + a later sector-light rebalance; kept here as the tunable dial.
const float GI_BOUNCE_STRENGTH = 1.0;

// DOOM-0170 L1b point-light dials (§6 seeds; §9 Q1/Q4 tuning). RASTER_MAX_LIGHTS must
// match RASTER_MAX_LIGHTS_PER_SUBSECTOR in r_vulkan.cpp. RADIUS is the half-bright
// distance (world units). Each emitter's Le is used as COLOUR ONLY (normalized by its
// max channel), because the raw Le is a radiometric NEE value (sprite lamps carry a ×12
// build boost, big surfaces enter as many triangles) that summed as raw intensity blows
// straight to white. STRENGTH sets the max additive level after the sum is rolled off.
const uint  RASTER_MAX_LIGHTS    = 16u;
const float RASTER_LIGHT_RADIUS  = 512.0;
const float RASTER_INV_RADIUS2   = 1.0 / (RASTER_LIGHT_RADIUS * RASTER_LIGHT_RADIUS);
// Two decoupled dials so "how obvious a single lamp is" and "the ceiling that stops a
// cluster blowing to white" tune independently: GAIN sets how fast a light climbs to
// full before the roll-off (the pop), STRENGTH is the max additive level after it.
const float POINT_LIGHT_GAIN     = 3.0;
const float POINT_LIGHT_STRENGTH = 0.7;

// DOOM-0170 §9 Q1 — sector-light rebalance. DOOM's flat sector light is bright, so the
// additive point lights + probe bounce have no headroom in already-bright areas. Dim the
// base a touch; the lights/bounce/flashlight lift lit areas back up while unlit areas sit
// a little darker (the "performance-mode" look). 1.0 = classic brightness; tune w/ user.
const float BASE_SECTOR_DIM      = 0.75;

// Evaluate the baked SH-L1 GI cache for subsector `subId` along world normal `n`,
// returning the diffuse reflected-radiance factor (multiply by albedo). Copied
// verbatim from pt_common.glsl giIrradiance() so the raster bounce is identical to
// the path tracer's: the clamped-cosine convolution (A0=PI, A1=2PI/3) folds with the
// Lambert 1/PI to weight 1 on the DC term and 2/3 on the linear terms; basis order
// 1<-n.y, 2<-n.z, 3<-n.x. SH-L1 can ring slightly negative, so clamp to >= 0.
vec3 giIrradiance(ProbesRO pr, uint subId, vec3 n)
{
    uint  b  = subId * 16u + 4u;             // SH coeffs start at float 4
    float y0 = 0.282095;
    float y1 = 0.488603 * n.y;
    float y2 = 0.488603 * n.z;
    float y3 = 0.488603 * n.x;
    const float k = 2.0 / 3.0;
    vec3 gi;
    gi.r = pr.p[b+0u]*y0 + k*(pr.p[b+1u]*y1 + pr.p[b+2u]*y2 + pr.p[b+3u]*y3);
    gi.g = pr.p[b+4u]*y0 + k*(pr.p[b+5u]*y1 + pr.p[b+6u]*y2 + pr.p[b+7u]*y3);
    gi.b = pr.p[b+8u]*y0 + k*(pr.p[b+9u]*y1 + pr.p[b+10u]*y2 + pr.p[b+11u]*y3);
    return max(gi, vec3(0.0));
}

// DOOM-0170 L2c — 3x3 PCF against the flashlight shadow map. 1.0 = lit, 0.0 = fully
// shadowed. Fragments outside/behind the light frustum return 1.0 (the cone + facing terms
// already limit where the beam reaches, so "no shadow data" means "not occluded").
float flashlightShadow(vec3 worldPos)
{
    vec4 lc = shadow.lightVP * vec4(worldPos, 1.0);
    if (lc.w <= 0.0)
        return 1.0;
    vec3 ndc = lc.xyz / lc.w;                 // Vulkan clip: z already in [0,1]
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 1.0;
    float cur = ndc.z - 0.0015;              // small depth bias vs. surface acne
    const float texel = 1.0 / 2048.0;        // matches kShadowDim
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
        sum += (cur <= texture(shadowMap, uv + vec2(x, y) * texel).r) ? 1.0 : 0.0;
    return sum / 9.0;
}

void main()
{
    // Sky backdrop: a cylindrical panorama keyed on the view yaw, exactly like
    // classic DOOM. The sky is a wall texture (atlas id == texnum). 90 deg of
    // view spans one texture width; screen x maps to a ray-yaw offset via atan
    // (perspective, 90 deg horizontal FOV -> tan(45)=1), and pc.yaw pans it as
    // the player turns. Vertically the 128px sky fills the top half of the
    // frame with the horizon at screen centre. Fullbright, so no shade term and
    // the muzzle flash never touches it.
    // FLAG_SKY is the full-screen NDC backdrop quad; FLAG_SKYDOME is the DOOM-0162
    // world-space occluder. Both sample the sky panorama identically (screen yaw +
    // vScreenUV); they differ only in the vertex stage (NDC vs MVP-projected).
    if ((vFlags & (FLAG_SKY | FLAG_SKYDOME)) != 0)
    {
        int   id    = vTexnum;                  // the sky is a wall texture
        vec2  sz    = vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
        float ndcX  = vScreenUV.x * 2.0 - 1.0;
        // DOOM's sky column is viewangle + xtoviewangle[x], and xtoviewangle is
        // +left/-right; ndcX is -left/+right, so the screen term is -atan(ndcX).
        float ang   = pc.yaw - atan(ndcX);
        float col   = ang / (PI * 0.5) * sz.x;  // REPEAT wraps the column
        // Vertical: DOOM's fixed sky scale -- one 320x200 logical screen pixel per
        // texel, with the texture's skytexturemid (row 100) pinned to the horizon
        // at screen centre. Mirrors classic r_sky (dc_texturemid = skytexturemid =
        // 100<<FRACBITS, dc_iscale = FRACUNIT).
        float row   = 100.0 + (vScreenUV.y - 0.5) * 200.0;
        // DOOM-0143/0162: below the horizon the fixed scale runs row past the 128px
        // sky texture. The old "* 2.0 then clamp to the base row" squashed the
        // panorama and showed a black band across distant outdoor views where the
        // floor did not reach the horizon (DOOM-0076); leaving the vertical REPEAT
        // instead wraps to the bright top-of-sky rows -- a bright band. Both are
        // wrong: the pixel is undefined (classic never draws sky below the horizon).
        // Clamp to stop the bright wrap, then fade the below-horizon strip into a
        // soft haze so sky dissolves into mist at the wall line rather than either
        // seam. This matters now DOOM-0162 draws the sky occluder in the raster view
        // (it fills the window below eye level). Mirrors pathtrace.comp skyPanorama;
        // col still REPEAT-wraps for the 360deg panorama.
        row = min(row, sz.y - 0.5);
        vec2  uv    = vec2(col, row) / sz;
        float idx   = texture(materialTex[nonuniformEXT(id)], uv).r * 255.0;
        vec3  skyCol = texture(paletteTex, vec2((idx + 0.5) / 256.0, 0.5)).rgb;
        const vec3 SKY_FOG_COL = vec3(0.5);     // neutral haze tone (see DOOM-0143)
        float fog   = smoothstep(0.50, 0.63, vScreenUV.y);
        vec3 skyOut = mix(skyCol, SKY_FOG_COL, fog);
#ifdef SINGLE_TARGET
        outColor    = vec4(skyOut, 1.0);
#else
        outDirect   = vec4(skyOut, 100000.0);   // sky: fullbright DIRECT + far depth (SSAO skips it)
        outAmbient  = vec4(0.0);                 // ...and never AO-darkened
#endif
        return;
    }

    // Unified material id: walls [0,numWall), flats [numWall,numWall+numFlat),
    // sprites after. The vertex's texnum is the per-category index.
    int id;
    if ((vFlags & FLAG_SPRITE) != 0)    id = pc.numWall + pc.numFlat + vTexnum;
    else if ((vFlags & FLAG_FLAT) != 0) id = pc.numWall + vTexnum;
    else                                id = vTexnum;

    // Native REPEAT tiling: divide the raw texel UV by the material's own size
    // and let the sampler wrap. (Sprite UVs are pre-inset half a texel and stay
    // in range, so REPEAT is a no-op for them — no bleed across a tile border.)
    vec2  sz      = vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
    float index   = texture(materialTex[nonuniformEXT(id)], vUV / sz).r * 255.0;

    // Sprites and two-sided masked mid-walls (grates/fences) store transparency
    // as palette index 0 (the gaps between posts); drop those texels so they
    // read as cut-outs, not boxes. A genuine index-0 (black) texel inside such
    // art is dropped too — DOOM art avoids it.
    if ((vFlags & (FLAG_SPRITE | FLAG_MASKED)) != 0 && index < 0.5)
        discard;

    vec3  albedo  = texture(paletteTex, vec2((index + 0.5) / 256.0, 0.5)).rgb;

    // Classic DOOM shades every surface by sector light + distance ONLY -- there
    // is no normal/directional term -- so a floor and a ceiling at the same
    // sector light and distance read equally bright. An earlier Lambert key
    // light here (0.55 + 0.45*dot(N,L)) dropped down-facing ceilings to ~0.55x
    // and pushed dim ones to black, while floors sat near 0.93x (DOOM-0069);
    // faithful lighting omits it. (Wall "fake contrast" -- a small walls-only
    // +/-1 light-level step on axis-aligned segs -- is the only orientation
    // effect DOOM has; it belongs in a per-vertex build-time pass, not here.)
    //
    // DOOM-style distance diminishing: far surfaces fade toward dark (near keeps
    // full sector light). The weapon psprite is NDC, so vDist is meaningless there.
    float distLight = 1.0;
    if ((vFlags & FLAG_PSPRITE) == 0)
        distLight = clamp(1.0 - vDist / 3000.0, 0.35, 1.0);

    // DOOM's non-directional sector light (with distance diminishing). This is the ambient
    // term SSAO occludes; an earlier Lambert key light here was dropped as unfaithful (see
    // the DOOM-0069 note above). Max is 0.75 (BASE_SECTOR_DIM), so it never self-clips.
    float sect = vLight * distLight * BASE_SECTOR_DIM;   // DOOM-0170 §9 Q1 rebalance

    // DOOM-0170 L2b — accumulate AMBIENT (sector + bounce; AO-affected) and DIRECT (flashlight
    // + point lights; never AO'd) into separate targets. Sprites and the weapon psprite are
    // billboards/cut-outs, so their whole shade goes to DIRECT — a depth-based world-geometry
    // AO must not darken a monster or the gun.
    bool spriteLike = (vFlags & (FLAG_SPRITE | FLAG_PSPRITE)) != 0;
    vec3 ambient = spriteLike ? vec3(0.0)        : albedo * sect;
    vec3 direct  = spriteLike ? albedo * sect    : vec3(0.0);

    // DOOM-0044 player flashlight (Solid tier / ray tracing OFF): an additive
    // raster spotlight at the eye, aimed along the view yaw. Cast shadows come from
    // the L2c shadow map (flashlightShadow). Skips the sky (returned above) and the
    // NDC weapon psprite (vWorldPos is meaningless there). The cone cosines match
    // the path tracer's so both tiers feel like the same lamp. It is a DIRECT term.
    // The additive amount is capped at (1 - sect) so a fully-lit surface still tops out
    // at albedo (matching the pre-L2b combined clamp(sect+flash,0,1)); the split is a no-op.
    if (pc.flashlight > 0.5 && (vFlags & FLAG_PSPRITE) == 0)
    {
        vec3  eye    = vec3(pc.eyeX, pc.eyeY, pc.eyeZ);
        vec3  fwd    = vec3(cos(pc.yaw), sin(pc.yaw), 0.0);
        vec3  toFrag = vWorldPos - eye;
        float fdist  = length(toFrag);
        vec3  fdir   = toFrag / max(fdist, 1e-4);             // eye -> fragment
        float spot   = smoothstep(0.82, 0.92, dot(fdir, fwd));    // cone ~35->23 deg
        float fall   = clamp(1.0 - fdist / 1200.0, 0.0, 1.0);     // near-bright beam
        float facing = max(dot(normalize(vNormal), -fdir), 0.0);  // surface faces lamp
        float sh     = flashlightShadow(vWorldPos);               // L2c cast-shadow PCF
        float flash  = min(spot * fall * facing * sh, max(0.0, 1.0 - sect));
        direct += albedo * flash;
    }

    // DOOM-0170 L1a/L1b: baked indirect bounce (AMBIENT) + this subsector's nearest point
    // lights (DIRECT), on world surfaces (RT-off). The fragment's subsector =
    // triSs[gl_PrimitiveID] (the same triangle->subsector map the path tracer indexes by
    // primitive id). Flag test first so sprites/psprite/sky short-circuit before reading
    // probeCount. Off when probeCount==0 (non-RT GPU or no bake) -> the plain sector-lit look.
    if ((vFlags & (FLAG_SPRITE | FLAG_PSPRITE | FLAG_SKY | FLAG_SKYDOME)) == 0 &&
        pc.probeCount > 0u)
    {
        uint sub = pc.triSs.s[uint(gl_PrimitiveID)];
        if (sub < pc.probeCount)
        {
            vec3 nrm = normalize(vNormal);
            ambient += GI_BOUNCE_STRENGTH * albedo * giIrradiance(pc.probes, sub, nrm);

            // DOOM-0170 L1b: direct light from this subsector's nearest emitters. Each
            // slot is a point approximation of an NEE area light — Le gives the COLOUR
            // (normalized by its max channel so a white lamp stays white and the raw ×12
            // NEE scale drops out), a smooth inverse-square falloff (half-bright at RADIUS)
            // and a Lambert facing term. No cast shadows (that is the RT-on / key-light
            // job, §4.4). Slots are nearest-packed; the first zero-Le slot ends the list.
            uint base = sub * (RASTER_MAX_LIGHTS * 6u);
            vec3 plight = vec3(0.0);
            for (uint k = 0u; k < RASTER_MAX_LIGHTS; k++)
            {
                uint o  = base + k * 6u;
                vec3 Le = vec3(pc.lights.d[o + 3u], pc.lights.d[o + 4u], pc.lights.d[o + 5u]);
                float m = max(Le.r, max(Le.g, Le.b));
                if (m <= 0.0)
                    break;
                vec3  col = Le / m;                     // unit-max colour, hue preserved
                vec3  c   = vec3(pc.lights.d[o], pc.lights.d[o + 1u], pc.lights.d[o + 2u]);
                vec3  dd  = c - vWorldPos;
                float d2  = dot(dd, dd);
                float att = 1.0 / (1.0 + d2 * RASTER_INV_RADIUS2);
                float ndl = max(dot(nrm, dd * inversesqrt(max(d2, 1e-8))), 0.0);
                plight += col * att * ndl;
            }
            // A big emissive surface enters as many emitter triangles, so summing the
            // nearest N over-counts one physical light. GAIN drives a single light up to
            // visible fast, then a Reinhard roll-off caps the accumulated point light at
            // < 1 per channel so a cluster saturates gracefully to its colour instead of
            // stacking to white; STRENGTH is the max additive level after that.
            plight *= POINT_LIGHT_GAIN;
            plight = plight / (1.0 + plight);
            direct += POINT_LIGHT_STRENGTH * albedo * plight;   // point lights are DIRECT
        }
    }

#ifdef SINGLE_TARGET
    outColor   = vec4(ambient + direct, 1.0);   // RT weapon overlay: combined into one target
#else
    // DOOM-0170 L2b — pack forward-distance linear depth into DIRECT.a for the SSAO pass. The
    // raster camera is yaw-only, so d = (worldPos - eye)·forward is exactly the projection's
    // clip.w; ssao.frag reconstructs view positions from it. The weapon psprite has no world
    // position (and is never AO'd), so it just writes the near plane.
    //
    // Sprites (monsters/items) are flat billboards standing on the floor. Feeding their real
    // depth to a screen-space AO makes the floor pixels beside them read the sprite as a near
    // wall — a black halo around every monster — and darkens the sprite's own silhouette. They
    // are grounded by blob shadows (L2d) instead, so they must not take part in SSAO at all.
    // Mark them by NEGATING the packed depth: ssao.frag treats a negative sample as "skip"
    // (neither receiver nor occluder). Sky stays +100000 (> its far guard); world stays [1, ∞).
    vec3  vfwd  = vec3(cos(pc.yaw), sin(pc.yaw), 0.0);
    float viewZ;
    if ((vFlags & FLAG_PSPRITE) != 0)
        viewZ = 1.0;
    else {
        float d = max(dot(vWorldPos - vec3(pc.eyeX, pc.eyeY, pc.eyeZ), vfwd), 1.0);
        viewZ = ((vFlags & FLAG_SPRITE) != 0) ? -d : d;   // sprites: negative = SSAO ignores
    }
    outAmbient = vec4(ambient, 1.0);
    outDirect  = vec4(direct, viewZ);
#endif
}
