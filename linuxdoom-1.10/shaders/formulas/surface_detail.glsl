// surface_detail.glsl — DOOM-0273 §4.3: read a wall's or flat's derived surface map
// (surface_derive.h) and offset its UV by the height. One copy, included by the
// traced view (pathtrace.comp) and the rasterised one (mesh.frag).
//
// The map of material `id` is materialTex[SURF_BASE + id], RGBA8:
//   r, g = tangent-space normal x, y as 0.5 + 0.5 * n (OpenGL Y+); z is rebuilt
//   b    = height, 1.0 = the surface itself, lower = recessed
//   a    = roughness
// Every UV here is in 0..1 tile space (uv01 = hit UV / paletted texture size), and
// every helper returns one; the paletted colour wants texels, so the caller scales.
//
// The includer declares materialTex[] and defines, before the #include:
//   SURF_LANE        the setting's lane value (pc.misc7.x, or ShadowUbo flags.x)
//   SURF_BASE        the first surface map's index in materialTex[] (misc7.z / flags.z)
//   SURF_SPRITE_BASE the first sprite id (numWall + numFlat); sprites have no map
#ifndef SURFACE_DETAIL_GLSL
#define SURFACE_DETAIL_GLSL

// The lane value meaning "on". r_vulkan.cpp's kSurfaceDetailOn is its twin
// (tests/shader_mirror_test.cpp holds them equal).
const uint SURF_DETAIL_ON = 1u;

// How deep the relief reads, as a fraction of one tile. Twice the HD path's
// kPomHeightScale: the user's pick 2026-10-26 from a side-by-side at the E2M1 test
// scene (spec §10). Derived heights come from luma, so it is tuned on its own.
const float kSurfHeightScale = 0.12;

// Below this tangent-space view z, both parallax helpers treat the view as this grazing,
// so the offset cannot run away along the surface.
const float kSurfMinViewZ = 0.05;

// The paletted texture's size in texels: uv01 = hit UV / this.
vec2 surfTexSize(uint id)
{
    return vec2(textureSize(materialTex[nonuniformEXT(id)], 0));
}

bool surfHas(uint id)
{
    return SURF_LANE == SURF_DETAIL_ON && id < SURF_SPRITE_BASE;
}

// x, y, z = tangent-space normal; w = height 0..1.
vec4 surfSample(uint id, vec2 uv01)
{
    vec4 t = textureLod(materialTex[nonuniformEXT(SURF_BASE + id)], uv01, 0.0);
    vec2 xy = t.rg * 2.0 - 1.0;
    return vec4(xy, sqrt(max(1.0 - dot(xy, xy), 0.0)), t.b);
}

float surfRoughness(uint id, vec2 uv01)
{
    return textureLod(materialTex[nonuniformEXT(SURF_BASE + id)], uv01, 0.0).a;
}

float surfHeight(uint id, vec2 uv01)
{
    return textureLod(materialTex[nonuniformEXT(SURF_BASE + id)], uv01, 0.0).b;
}

// Traced view: hdParallaxUV's march over the derived height — steps into the surface
// until the ray dips below the height field, then one bisection. Primary hit only, no
// self-shadowing. viewTS is the unit direction toward the eye in the tangent frame.
// A flat map (height 1.0 everywhere) stops at once and returns uv01 unchanged.
vec2 surfParallaxUV(uint id, vec2 uv01, vec3 viewTS)
{
    float nv     = clamp(viewTS.z, kSurfMinViewZ, 1.0);
    int   steps  = int(mix(32.0, 16.0, nv));       // more steps at grazing angles
    vec2  maxOff = (viewTS.xy / nv) * kSurfHeightScale;
    float layer  = 1.0 / float(steps);
    float curH = surfHeight(id, uv01), prevH = curH;
    vec2  curUV = uv01, prevUV = uv01;
    float curD = 0.0, prevD = 0.0;
    for (int i = 0; i < steps; i++) {
        prevUV = curUV; prevH = curH; prevD = curD;
        curD += layer;
        curUV = uv01 - maxOff * curD;
        curH = surfHeight(id, curUV);
        if (curD >= (1.0 - curH)) break;
    }
    float h0 = (1.0 - prevH) - prevD;              // gap above the surface, previous step
    float h1 = (1.0 - curH)  - curD;               // gap below it, this step
    float t  = h0 / max(h0 - h1, 1e-5);
    return mix(prevUV, curUV, clamp(t, 0.0, 1.0));
}

// Rasterised view: plain parallax offset mapping, one height read. Height 1.0 is the
// surface, so it does not move there.
vec2 surfOffsetUV(uint id, vec2 uv01, vec3 viewTS)
{
    float h = surfHeight(id, uv01);
    return uv01 + kSurfHeightScale * (h - 1.0) * viewTS.xy / max(viewTS.z, kSurfMinViewZ);
}

#endif // SURFACE_DETAIL_GLSL
