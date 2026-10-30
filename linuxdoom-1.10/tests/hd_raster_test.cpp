// hd_raster_test.cpp - DOOM-0182: Ultra's HD art in the rasterised view.
//
// Scrapes the shaders and r_vulkan.cpp, like shader_mirror_test.cpp. What it locks:
//   - The HD maps have mips, and an implicit LOD outside a fragment shader is not
//     defined, so the trace must read them at an explicit level. A plain
//     texture(hdTex...) in pathtrace.comp could pick a tiny mip on some drivers.
//   - The shared code samples through the includer's HD_TEX macros only, so mesh.frag's
//     explicit gradients reach every read, across de-tile cell edges too.
//   - mesh.frag's HD path is off unless g.hdPipeline set it, so Solid stays free of it,
//     and only the world's twin turns it on.
//   - An HD material takes no derived surface map (DOOM-0273 INV-6's raster half).
//   - The HD tangent frame carries the sign that makes it dP/du, dP/dv. Without it every
//     surface had both axes reversed, seen 2026-10-30 by painting them by direction.
//
#include <fstream>
#include <sstream>
#include <string>

#ifndef DOOM_TESTS_ROOT
#define DOOM_TESTS_ROOT "."
#endif

#include "check_util.h"

static std::string slurp(const char* path)
{
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static int count(const std::string& s, const std::string& what)
{
    int n = 0;
    for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) n++;
    return n;
}

int main()
{
    const std::string pt = slurp(DOOM_TESTS_ROOT "/shaders/pathtrace.comp");
    const std::string hs = slurp(DOOM_TESTS_ROOT "/shaders/formulas/hd_surface.glsl");
    const std::string mf = slurp(DOOM_TESTS_ROOT "/shaders/mesh.frag");
    const std::string vk = slurp(DOOM_TESTS_ROOT "/r_vulkan.cpp");
    check(!pt.empty() && !hs.empty() && !mf.empty() && !vk.empty(), "the sources are readable");

    check(count(pt, "texture(hdTex") == 0, "pathtrace.comp has no implicit-LOD read of an HD map");
    check(pt.find("#define HD_TEX(i, uv)           textureLod(") != std::string::npos,
          "pathtrace.comp's HD_TEX reads at an explicit level");
    check(count(hs, "texture(hdTex") == 0 && count(hs, "textureLod(hdTex") == 0,
          "hd_surface.glsl has no direct HD map read");
    check(mf.find("#define HD_TEX(i, uv)           textureGrad(") != std::string::npos,
          "mesh.frag's HD_TEX passes explicit gradients");
    check(count(mf, "texture(hdTex") == 0, "mesh.frag has no implicit-LOD read of an HD map");

    check(mf.find("layout(constant_id = 0) const bool kHdPipeline = false;") != std::string::npos,
          "mesh.frag's HD path is off by default");
    check(count(vk, "pSpecializationInfo = &si;") >= 1 && count(vk, "&g.hdPipeline)") == 1,
          "exactly one pipeline, g.hdPipeline, is built with it on");
    check(vk.find("g.hdBuilt ? g.hdPipeline : g.pipeline") != std::string::npos,
          "the world draws with g.hdPipeline only when the HD set is built");

    check(mf.find("vec3 T = (py * dux.x + px * duy.x) * hand;") != std::string::npos
              && mf.find("const float hand = sign(dot(n, cross(dpx, dpy)));") != std::string::npos,
          "the HD tangent frame is corrected for window y running down");

    check(mf.find("else if ((vFlags & FLAG_PSPRITE) == 0 && surfHas(uint(id)))") != std::string::npos,
          "an HD material takes no derived surface map");

    return check_summary("hd_raster_test");
}
