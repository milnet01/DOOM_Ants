// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2026 DOOM_Ants contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//    Vulkan 3D renderer back-end (DOOM-0008) — the C++ side of the plain-C
//    renderer_backend_t seam (DOOM-0026). See docs/specs/DOOM-0008-3d-renderer.md
//    and ADR docs/decisions/0001-renderer-language-and-api.md.
//
//    Built up incrementally. So far: the headless tier probe (RT3D / Raster3D
//    / Classic), the Vulkan device + swapchain, and a depth-buffered raster
//    pass that draws the converted level mesh (r_mesh.c) from the player's
//    camera with simple bring-up shading. Materials/textures, sprites, the UI
//    composite, and the path tracer land in following increments.
//
//-----------------------------------------------------------------------------

#include <vulkan/vulkan.h>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <chrono>      // DOOM-0170 perf: CPU-side wall-clock frame profiler
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>     // exit() for the -rtverify self-test
#include <vector>

// POD-only, DOOM-header-free seam: the C geometry builder (r_mesh.c) and the
// per-frame camera (rb_view_t).
#include "r_mesh.h"

// Power-importance emitter-sampling math (build step 3c-2), shared with the
// unbiasedness test (tests/nee_sampling_test.cpp) so both use one implementation.
#include "nee_sampling.h"

// Per-material emitter derivation (build step 3b, §4.2), shared with
// tests/emissive_derive_test.cpp. Defines the `emis` namespace used below.
#include "emissive_derive.h"

// DOOM-0042 Ultra HD PBR materials. rb_materials.h is header-only (inline pure
// logic: sidecar parse, name->id resolution, control table, VRAM budget), shared
// with tests/rb_materials_test.cpp. rb_image.h is the vendored-stb PNG decoder
// (compiled in rb_image.c; self-guards extern "C").
#include "rb_materials.h"
#include "rb_image.h"
// DOOM-0206 (L1b): the pure-logic stb_truetype glyph-atlas baker (compiled in rb_text.c;
// self-guards extern "C"). The GPU side (atlas image + text pipeline + batch API) is here.
#include "rb_text.h"

// C engine symbols the HD material loader resolves DOOM names against (defined in
// r_data.c / w_wad.c). Declared here since r_vulkan.cpp doesn't pull r_state.h.
extern "C" {
    int R_CheckTextureNumForName(char* name);   // wall texnum, or -1
    int W_CheckNumForName(char* name);          // lump number, or -1
    extern int firstflat;                       // lump index of the first flat
    extern int numflats;                        // flat count
    extern int rendermode;                      // r_backend.h: selected tier (TIER_* mirror below)
    // DOOM-0206 (L2): gamestate/screenblocks drive the HUD-safe bound (rb_menu_safe_bottom).
    // gamestate's real C type is gamestate_t (doomdef.h enum typedef, GS_LEVEL==0; the extern
    // lives in doomstat.h); mirrored here as plain int rather than pulling those headers in (not
    // C++-clean, see the probe
    // comment below). Unlike rendermode (a genuine plain `int`, r_backend.c), this relies on
    // the enum's int-width representation under this toolchain (no -fshort-enums; enums default
    // to int width here), so declaring it `int` across the C/C++ boundary is safe but not a
    // like-for-like mirror of the rendermode dodge.
    // screenblocks is genuinely `int` in m_menu.c already, so no mismatch there.
    extern int gamestate;                       // doomdef.h: gamestate_t, GS_LEVEL == 0
    extern int screenblocks;                    // m_menu.c: HUD size 0-10 (DOOM-0148 clamp)
}

// The GPU control struct mirrors this byte-for-byte in pathtrace.comp (std430).
static_assert(sizeof(rb_matctrl_t) == 40, "rb_matctrl_t must be 40 bytes (std430 MatCtrl)");

// Compiled shaders, embedded as byte arrays (Makefile: GLSL -> SPIR-V -> xxd).
#include "shaders/mesh.vert.spv.h"
#include "shaders/mesh.frag.spv.h"
#include "shaders/mesh_overlay.frag.spv.h"   // DOOM-0170 L2b: 1-output variant (RT weapon overlay)
#include "shaders/shadow.vert.spv.h"
#include "shaders/shadow.frag.spv.h"
#include "shaders/blob.vert.spv.h"
#include "shaders/blob.frag.spv.h"
#include "shaders/overlay.vert.spv.h"
#include "shaders/overlay.frag.spv.h"
#include "shaders/composite.vert.spv.h"
#include "shaders/composite.frag.spv.h"
#include "shaders/text.vert.spv.h"          // DOOM-0206 L1b: display-res menu glyph text
#include "shaders/text.frag.spv.h"
#include "shaders/cursor.frag.spv.h"        // DOOM-0206 v2: RGBA menu skull cursor (M_SKULL1)
#include "shaders/ssao.frag.spv.h"          // DOOM-0170 L2b: half-res SSAO (contact shadows)
#include "shaders/pathtrace.comp.spv.h"
#include "shaders/bake.comp.spv.h"
#include "shaders/svgf_temporal.comp.spv.h"
#include "shaders/svgf_atrous.comp.spv.h"
#include "shaders/svgf_composite.comp.spv.h"
#include "shaders/label.comp.spv.h"
#include "shaders/taau.comp.spv.h"
#include "assets/Oxanium-SemiBold.ttf.h"    // DOOM-0206 L4: bundled OFL menu font (oxanium_ttf[])

// Tier values returned by RB_VulkanProbe — kept numerically in lockstep with
// rendermode_t in r_backend.h (RB_CLASSIC=0, RB_RT3D=1, RB_RASTER3D=2). The
// probe deliberately does not include the DOOM C headers (which are not C++
// clean), so the values are mirrored here with this contract instead.
enum { TIER_CLASSIC = 0, TIER_RT3D = 1, TIER_RASTER3D = 2 };

//
// RB_VulkanProbe — pick the best 3D tier this machine supports.
//
// RT3D     : a device exposing VK_KHR_acceleration_structure + VK_KHR_ray_query
//            (hardware ray tracing — the full path-traced tier).
// Raster3D : a Vulkan device, but without those ray-tracing extensions.
// Classic  : no usable Vulkan at all (caller stays on the software renderer).
//
// Headless: no SDL window or VkSurface is created, so this is safe to call at
// startup before any window rework (per the spec's Window & device ownership).
//
extern "C" int RB_VulkanProbe(void)
{
    // Probe once and cache: RB_Init logs it, and the back-end Available()
    // checks reuse it (the menu may poll repeatedly). One instance create,
    // one line of output.
    static int cached = -1;
    if (cached >= 0)
        return cached;

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "DOOM_Ants";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;

    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ici, nullptr, &inst) != VK_SUCCESS)
    {
        printf("RB_VulkanProbe: no Vulkan instance - software (Classic) only.\n");
        return cached = TIER_CLASSIC;
    }

    uint32_t ndev = 0;
    vkEnumeratePhysicalDevices(inst, &ndev, nullptr);
    std::vector<VkPhysicalDevice> devs(ndev);
    if (ndev)
        vkEnumeratePhysicalDevices(inst, &ndev, devs.data());

    // Start pessimistic and upgrade only for a device that can actually run the
    // 3D path (DOOM-0059): a device merely existing is no longer enough.
    int best = TIER_CLASSIC;
    char bestName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";

    for (VkPhysicalDevice d : devs)
    {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(d, &props);

        // DOOM-0059: the 3D renderer is bindless-only — it needs the four Vulkan
        // 1.2 descriptor-indexing features PickPhysicalAndDevice enables (and
        // I_Errors without). Gate the tier here on the SAME four so a GPU lacking
        // them is reported Classic-only and never offered Solid/Ultra, instead of
        // aborting at device creation after the user has picked a 3D mode.
        VkPhysicalDeviceVulkan12Features f12 = {};
        f12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 f2 = {};
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        f2.pNext = &f12;
        vkGetPhysicalDeviceFeatures2(d, &f2);
        if (!(f12.runtimeDescriptorArray &&
              f12.shaderSampledImageArrayNonUniformIndexing &&
              f12.descriptorBindingVariableDescriptorCount &&
              f12.descriptorBindingPartiallyBound))
            continue;  // cannot run the bindless 3D path — leave it on Classic.

        uint32_t next = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &next, nullptr);
        std::vector<VkExtensionProperties> exts(next);
        if (next)
            vkEnumerateDeviceExtensionProperties(d, nullptr, &next, exts.data());

        bool accel = false, rayq = false;
        for (const VkExtensionProperties& e : exts)
        {
            if (!strcmp(e.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
                accel = true;
            if (!strcmp(e.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME))
                rayq = true;
        }

        if (accel && rayq)
        {
            best = TIER_RT3D;
            strncpy(bestName, props.deviceName, sizeof(bestName) - 1);
            break;  // RT-capable is the top tier; no need to look further.
        }
        // Bindless-capable but no RT: at least the raster-3D tier. (An RT device
        // always breaks above, so best is only ever CLASSIC or RASTER3D here.)
        best = TIER_RASTER3D;
        if (bestName[0] == '\0')
            strncpy(bestName, props.deviceName, sizeof(bestName) - 1);
    }

    if (best == TIER_RT3D)
        printf("RB_VulkanProbe: hardware ray tracing on \"%s\" - RT3D (path-traced) tier.\n", bestName);
    else if (best == TIER_RASTER3D)
        printf("RB_VulkanProbe: Vulkan device \"%s\" without hardware ray tracing - raster-3D tier.\n", bestName);
    else
        printf("RB_VulkanProbe: no Vulkan device - software (Classic) only.\n");

    vkDestroyInstance(inst, nullptr);
    return cached = best;
}

//=============================================================================
//
// DOOM-0008 Stage 1 — Vulkan device + swapchain + raster primary visibility.
//
// The C++ side of the renderer_backend_t seam (DOOM-0026) for the RB_RT3D /
// RB_RASTER3D slots. It recreates the SDL window as a Vulkan window, owns a
// surface + swapchain + depth buffer, and draws the converted level mesh
// (r_mesh.c) in a depth-buffered render pass from the player's camera, then
// presents. Materials/textures, sprites, the UI composite, and the path tracer
// land in following increments — this proves geometry, camera, and the draw
// loop end to end (the rasterised G-buffer the spec's integrator builds on).
//
// Once Available() has reported a tier usable, a later failure is a genuine bug
// (not a capability gap), so these abort loudly via I_Error rather than
// silently degrading to Classic — by then the Classic window is already gone.
//
//=============================================================================

// From i_video.c (C). The window crosses the seam as an opaque pointer so the
// DOOM C translation units need not include SDL; we cast it back here.
extern "C" void* I_GetWindow(void);
extern "C" void  I_ShutdownGraphicsForVulkan(void);
extern "C" [[noreturn]] void I_Error(const char* error, ...);
extern "C" int M_CheckParm(const char* check);   // m_argv.c — for -rtverify (step 4d)
// DOOM-0206 v2: decode the real WAD menu skull (M_SKULL1) to a brightened RGBA buffer,
// cached in m_menu.c. Uploaded once as the crisp cursor texture (CreateTextResources).
extern "C" const unsigned char* M_CursorSkullRGBA(int* out_w, int* out_h);
// DOOM-0206: the real M_DOOM logo lump decoded to RGBA (main-menu crisp title).
extern "C" const unsigned char* M_MenuLogoRGBA(int* out_w, int* out_h);
// DOOM-0202: argv (optional -shotverify output path) + the vendored PNG writer
// (stbi_write_png, implemented in rb_image.c). Used only by the -shotverify capture.
extern "C" { extern int myargc; extern char** myargv; }
extern "C" int stbi_write_png(const char* filename, int w, int h, int comp,
                              const void* data, int stride_in_bytes);

namespace {

// SVGF denoiser images (DOOM-0009 build step 6), indices into g.svImg/svMem/svView.
// gpos/gnorm/hcol/hmom/atrous ping-pong (2 each); albedo/illum are current-frame
// only. gpos is rgba32f (world position needs the precision + holds matId exactly);
// all others are rgba16f.
enum {
    SV_GPOS0, SV_GPOS1, SV_GNORM0, SV_GNORM1, SV_ALBEDO, SV_ILLUM,
    SV_HCOL0, SV_HCOL1, SV_HMOM0, SV_HMOM1, SV_ATROUS0, SV_ATROUS1,
    SV_MOTION,   // build step 6-d: render-res motion vector (rg16f), composite writes it
    SV_COUNT
};

// build step 6-d TAAU targets (display-resolution): two history images that
// ping-pong by frame parity, plus the upscaled output the present path blits.
enum { TA_HIST0, TA_HIST1, TA_OUT, TA_COUNT };

// DOOM-0206 (L1b): one textured glyph-quad vertex. Position is in DISPLAY PIXELS with a
// top-left origin (text.vert converts to NDC via the invDisplay push constant), UV indexes
// the R8 atlas, and the colour is R8G8B8A8_UNORM (decoded to a normalized vec4 tint).
struct TextVertex { float x, y, u, v; unsigned char r, g, b, a; };

struct VulkanState
{
    VkInstance       instance    = VK_NULL_HANDLE;
    VkSurfaceKHR     surface     = VK_NULL_HANDLE;
    VkPhysicalDevice phys        = VK_NULL_HANDLE;
    VkDevice         device      = VK_NULL_HANDLE;
    uint32_t         queueFamily = 0;
    VkQueue          queue       = VK_NULL_HANDLE;

    VkSwapchainKHR           swapchain = VK_NULL_HANDLE;
    VkFormat                 format    = VK_FORMAT_UNDEFINED;
    VkExtent2D               extent    = { 0, 0 };
    std::vector<VkImage>     images;
    std::vector<VkImageView> imageViews;

    // Depth buffer (recreated with the swapchain).
    VkImage        depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView    depthView   = VK_NULL_HANDLE;

    VkRenderPass               renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    // DOOM-0170 L2a — off-screen scene canvas. The world is drawn into sceneImage via
    // scenePass (colour STORE, final layout SHADER_READ_ONLY); a full-screen composite
    // pass (compositePipeline, in renderPass) then samples it to the swapchain. This is
    // the seam where the HDR tone-map + the L2 shadow/AO composites land. sceneFb binds
    // sceneView + the shared depthView. Same size/format as the swapchain (step 1); the
    // format flips to HDR float in step 2.
    VkImage        sceneImage  = VK_NULL_HANDLE;
    VkDeviceMemory sceneMemory = VK_NULL_HANDLE;
    VkImageView    sceneView   = VK_NULL_HANDLE;
    VkFramebuffer  sceneFb     = VK_NULL_HANDLE;
    VkRenderPass   scenePass   = VK_NULL_HANDLE;
    // DOOM-0170 L2b — the scene pass now writes TWO HDR colour targets so SSAO can darken
    // ambient light without touching direct light (§3/§4.3). sceneImage is the AMBIENT
    // target (attachment 0: sector light × dist + baked bounce — the term AO multiplies);
    // sceneDirImage is the DIRECT target (attachment 1: flashlight cone + point lights, and
    // the full colour of sprites/sky so AO never darkens them). The composite recombines
    // them: DIRECT + AO×AMBIENT. Both kSceneFormat, same size, freed together.
    VkImage        sceneDirImage  = VK_NULL_HANDLE;
    VkDeviceMemory sceneDirMemory = VK_NULL_HANDLE;
    VkImageView    sceneDirView   = VK_NULL_HANDLE;

    // DOOM-0170 L2b — SSAO (contact shadows, §4.3). A HALF-resolution R8 occlusion image the
    // ssao.frag pass writes (reading the DIRECT target's packed depth), which the composite
    // blurs + multiplies into AMBIENT. aoImage/View/Memory + aoFb are size-dependent (rebuilt
    // on resize with the scene target); aoPass/aoPipeline/aoPipeLayout + the ssaoDs (which
    // samples the DIRECT target) are size-independent (viewport is dynamic).
    VkImage        aoImage   = VK_NULL_HANDLE;
    VkDeviceMemory aoMemory  = VK_NULL_HANDLE;
    VkImageView    aoView    = VK_NULL_HANDLE;
    VkFramebuffer  aoFb      = VK_NULL_HANDLE;
    VkExtent2D     aoExtent  = { 0, 0 };
    VkRenderPass   aoPass    = VK_NULL_HANDLE;
    VkPipeline            aoPipeline    = VK_NULL_HANDLE;
    VkPipelineLayout      aoPipeLayout  = VK_NULL_HANDLE;
    VkDescriptorSetLayout ssaoDsLayout  = VK_NULL_HANDLE;
    VkDescriptorPool      ssaoDsPool    = VK_NULL_HANDLE;
    VkDescriptorSet       ssaoDs        = VK_NULL_HANDLE;

    // DOOM-0170 L2c — flashlight cast-shadow map (§4.4). A fixed 2048^2 depth image the
    // world is rendered into (depth-only) from the flashlight's viewpoint each torch-on
    // frame; mesh.frag samples it (3x3 PCF, set 1) to shadow the DOOM-0044 cone. Fixed
    // size (independent of window/render-scale), so these are built once and never
    // recreated on resize. shadowUbo carries lightVP to mesh.frag (the push block is full).
    VkImage               shadowImage      = VK_NULL_HANDLE;
    VkDeviceMemory        shadowMemory     = VK_NULL_HANDLE;
    VkImageView           shadowView       = VK_NULL_HANDLE;
    VkSampler             shadowSampler    = VK_NULL_HANDLE;
    VkRenderPass          shadowPass       = VK_NULL_HANDLE;
    VkFramebuffer         shadowFb         = VK_NULL_HANDLE;
    VkPipeline            shadowPipeline   = VK_NULL_HANDLE;
    VkPipelineLayout      shadowPipeLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadowDsLayout   = VK_NULL_HANDLE;   // set 1 for the world pipeline
    VkDescriptorPool      shadowDsPool     = VK_NULL_HANDLE;
    VkDescriptorSet       shadowDs         = VK_NULL_HANDLE;
    VkBuffer              shadowUbo        = VK_NULL_HANDLE;    // lightVP, persistently mapped
    VkDeviceMemory        shadowUboMemory  = VK_NULL_HANDLE;
    void*                 shadowUboMapped  = nullptr;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    // World pipeline. DOOM-0170 L2a step 3: built against the 16-bit-float g.scenePass
    // (the raster path draws the whole scene into the HDR off-screen target), so it is
    // NOT format-compatible with the 8-bit swapchain passes — the RT weapon overlay uses
    // rtWeaponPipeline (below) instead.
    VkPipeline       pipeline       = VK_NULL_HANDLE;
    // DOOM-0170 L2a step 3: 8-bit twin of `pipeline` (same shaders/state) built against
    // renderPass, for the RT/Ultra weapon overlay which draws onto the 8-bit swapchain
    // after the traced blit — the one place the world pipeline still meets an 8-bit target.
    VkPipeline       rtWeaponPipeline = VK_NULL_HANDLE;
    // Same layout/shaders as `pipeline`, but depth test + write disabled, so the
    // sky backdrop paints behind everything and the world overdraws it.
    VkPipeline       skyPipeline    = VK_NULL_HANDLE;
    // Wireframe variant of `pipeline` (polygonMode LINE) for the debug view
    // toggled by rb_wireframe / the gamepad Share button. Only built when the GPU
    // advertises fillModeNonSolid (wireSupported); else the toggle is a no-op.
    VkPipeline       wirePipeline   = VK_NULL_HANDLE;
    bool             wireSupported  = false;
    // DOOM-0170 L2d: blob-shadow decals. Same mesh.vert + g.pipelineLayout as the world,
    // but blob.frag + alpha blend + depth-test-on/write-off, so each floor quad darkens
    // the floor without occluding. Drawn from g.spriteVbuf after the world, before sprites.
    VkPipeline       blobPipeline   = VK_NULL_HANDLE;
    // 2D HUD/menu compositor (DOOM-0008): a vertexless full-screen pass that
    // draws the paletted screens[0] overlay over the rendered 3D scene, keying
    // out the transparent index. Shares pipelineLayout + descriptor set 0.
    VkPipeline       overlayPipeline = VK_NULL_HANDLE;
    // DOOM-0094: LOAD-variant of renderPass (colour loadOp=LOAD to keep the path-
    // traced blit, depth cleared). Used after RecordRtTrace to draw the weapon
    // viewmodel + the 2D HUD/menu/FPS overlay over the traced view. Format-compatible
    // with renderPass, so it reuses g.framebuffers and the world/overlay pipelines.
    VkRenderPass     rtOverlayPass   = VK_NULL_HANDLE;

    // DOOM-0170 L2a composite pass: its own 1-sampler descriptor set (the scene target),
    // kept separate from g.dsLayout whose variable-count material array must stay the last
    // binding. compositePipeLayout binds this set + a vec2 push constant (the
    // render-scale UV fraction the composite upscales from -- L2a step 2).
    VkPipeline            compositePipeline   = VK_NULL_HANDLE;
    VkPipelineLayout      compositePipeLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout compositeDsLayout   = VK_NULL_HANDLE;
    VkDescriptorPool      compositeDsPool     = VK_NULL_HANDLE;
    VkDescriptorSet       compositeDs         = VK_NULL_HANDLE;
    // DOOM-0170 L2a step 2: linear + clamp sampler for the composite's upscale of the
    // render-scaled scene (texSampler is nearest+repeat, wrong for a smooth upscale).
    VkSampler             compositeSampler    = VK_NULL_HANDLE;

    VkCommandPool   cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd     = VK_NULL_HANDLE;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    // One renderFinished per swapchain image, indexed by the acquired image index.
    // A single shared semaphore is unsafe: the queue submit signals it but the
    // *presentation engine* consumes it, which the per-frame fence does not track,
    // so the next submit could re-signal it while a present still holds it
    // (VUID-vkQueueSubmit-pSignalSemaphores-00067). Image idx is only re-acquired
    // after its previous present finished, so renderFinished[idx] is then unsignalled.
    std::vector<VkSemaphore> renderFinished;
    VkFence     inFlight       = VK_NULL_HANDLE;

    // DOOM-0074: build-ahead double-buffering. The expensive CPU work each frame is
    // the "build" (moving-sector re-height + per-subsector point-light cull + sprite
    // billboards), not command recording or the GPU. Running that build BEFORE the
    // top-of-frame fence wait lets the CPU prepare frame N+1's data while the GPU is
    // still rendering frame N -- recovering the CPU/GPU overlap the single-frame
    // design left on the table (toward the 60 FPS floor). The GPU itself stays on ONE
    // serialized timeline behind g.inFlight: one command buffer, one set of render
    // targets, the BLAS/TLAS and the denoiser history are all untouched and race-free.
    // Only the three buffers the build writes and the GPU then reads (vbuf vertex
    // input, spriteVbuf vertex input, lightBuf point-light SSBO by device address)
    // need one copy per in-flight slot: g.vbuf / g.spriteVbuf / g.lightBuf (+ memory +
    // mapped ptr) are re-pointed at slot[frameSlot] at the top of each frame, so every
    // downstream bind / device-address / re-height site keeps using g.<name>. Build-
    // ahead is used only in steady-state raster (Solid); a traced frame or a render-
    // mode toggle serializes instead (see RB_Vulkan_Present) so the single-copy RT
    // resources are never in flight -- full RT-mode overlap is a separate follow-up.
    static constexpr uint32_t kFramesInFlight = 2;
    uint32_t       frameSlot = 0;
    VkBuffer       vbufSlot[kFramesInFlight]       = {};
    VkDeviceMemory vbufMemSlot[kFramesInFlight]    = {};
    void*          vbufMappedSlot[kFramesInFlight] = {};
    VkBuffer       spriteVbufSlot[kFramesInFlight]    = {};
    VkDeviceMemory spriteVbufMemSlot[kFramesInFlight] = {};
    void*          spriteMappedSlot[kFramesInFlight]  = {};
    VkBuffer       lightBufSlot[kFramesInFlight]    = {};
    VkDeviceMemory lightMemSlot[kFramesInFlight]    = {};
    void*          lightMappedSlot[kFramesInFlight] = {};
    bool           lastRtActive = false;   // last frame's render mode (toggle -> drain)

    // DOOM-0090: opt-in per-pass GPU timer (toggled by rb_profile / the `\` key).
    // A timestamp query pool is sampled at the path tracer's pass boundaries in
    // RecordRtTrace and read back at the top of the next frame. Single-frame-in-
    // flight means that frame is already complete by then, so the read adds no
    // stall. profMs accumulates the four segment costs; printed once a second.
    VkQueryPool gpuTimerPool    = VK_NULL_HANDLE;
    float       timestampPeriod = 0.0f;   // ns per tick (0 = timestamps unusable)
    bool        gpuTimersInUse  = false;  // last frame wrote timestamps
    bool        profRasterFrame = false;  // last timed frame was raster (routes the readback: raster passes vs RT passes)
    // [0]=sprite-AS [1]=megakernel [2]=denoise+TAAU [3]=blit, then the DOOM-0144
    // sub-breakdown of [2]: [4]=temporal [5]=a-trous [6]=composite [7]=TAAU.
    double      profMs[8]       = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int         profFrames      = 0;
    int         profLastReport  = 0;      // I_GetTimeMS of the last printf

    // DOOM-0170 perf: CPU-side wall-clock profiler (same `\` toggle). The GPU pass
    // timers above only see GPU execution; this measures the CPU frame cost the
    // single-frame-in-flight design can't overlap — the fence WAIT (CPU blocked on the
    // previous frame's GPU: large => GPU-bound, ~0 => CPU-bound), plus CPU prep
    // (sprite/emitter/point-light build) and command recording. Printed on the same
    // 1-second cadence as the GPU line so both land together.
    double      cpuMs[5]        = { 0, 0, 0, 0, 0 };  // fenceWait / build / record / submit / present-total
    // Sub-breakdown of cpuMs[1] (build): [0] sprite/sky/blob billboard builds,
    // [1] NEE emitter refill + per-subsector point-light cull, [2] moving-sector re-height.
    double      cpuBuildMs[3]   = { 0, 0, 0 };
    int         cpuFrames       = 0;
    int         cpuLastReport   = 0;

    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;

    rb_mesh_t* levelMesh = nullptr;   // current level's CPU geometry (DOOM-0008)

    // GPU vertex buffer for the level mesh (uploaded at BuildLevel).
    VkBuffer       vbuf       = VK_NULL_HANDLE;
    VkDeviceMemory vbufMemory = VK_NULL_HANDLE;
    void*          vbufMapped = nullptr;   // host-visible, kept mapped so moving
                                           // sectors re-height per frame (DOOM-0049)
    uint32_t       vertexCount = 0;

    // Per-frame billboard sprites (DOOM-0008): things move, so this host-visible
    // buffer is persistently mapped and refilled by RB_BuildSprites every frame,
    // then drawn after the static level mesh with the same pipeline. lastView is
    // the camera RenderView stashed, used to build the sprites in Present.
    VkBuffer       spriteVbuf       = VK_NULL_HANDLE;
    VkDeviceMemory spriteVbufMemory = VK_NULL_HANDLE;
    void*          spriteMapped     = nullptr;
    uint32_t       spriteVertCap    = 0;
    uint32_t       spriteVertCount  = 0;
    uint32_t       skyVertCount     = 0;   // sky verts at the front of spriteVbuf
    uint32_t       blobVertCount    = 0;   // DOOM-0170 L2d: blob-shadow verts...
    uint32_t       blobVertOffset   = 0;   // ...at this offset in spriteVbuf (after sprites)
    rb_view_t      lastView         = {};

    // Bindless material array (DOOM-0009 build step 1): one R8 palette-index image
    // per material (wall/flat/sprite), each sized to itself and REPEAT-addressed,
    // indexed by unified id in the shader. WAD-global and constant — built once,
    // reused across levels. All N images share one device allocation (a minimal
    // manual sub-allocator; VMA replaces it before the image count nears the
    // driver's per-allocation limit). Supersedes the single packed atlas + rect
    // buffer the Stage-1 raster path used.
    std::vector<VkImage>     matImages;
    std::vector<VkImageView> matViews;
    VkDeviceMemory           matMemory  = VK_NULL_HANDLE;  // one alloc, N images
    int                      matNumWall = 0;   // id offsets pushed to the shader
    int                      matNumFlat = 0;   // (flats at numWall, sprites after)
    VkImage        palImage    = VK_NULL_HANDLE;   // 256x1 PLAYPAL LUT
    VkDeviceMemory palMemory   = VK_NULL_HANDLE;
    VkImageView    palView     = VK_NULL_HANDLE;
    VkSampler      texSampler  = VK_NULL_HANDLE;   // nearest, REPEAT (native tiling)
    VkSampler hdSampler = VK_NULL_HANDLE;   // DOOM-0042: linear+mip+REPEAT for HD PBR maps

    // DOOM-0042 HD PBR material path (parallel to the R8 matImages). A new descriptor
    // set (set 3 of the RT pipeline): binding 0 = the per-material control SSBO, binding
    // 1 = a variable-count bindless array of RGBA8 PBR maps. Built per level in Ultra by
    // EnsureHdMaterials; always valid there (even with no materials.csv) so the shader's
    // ctrl[] read is safe every dispatch.
    VkDescriptorSetLayout    hdSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool         hdPool      = VK_NULL_HANDLE;
    VkDescriptorSet          hdSet       = VK_NULL_HANDLE;
    std::vector<VkImage>     hdImages;
    std::vector<VkImageView> hdViews;
    VkDeviceMemory           hdMemory    = VK_NULL_HANDLE;
    VkBuffer                 hdCtrlBuf   = VK_NULL_HANDLE;
    VkDeviceMemory           hdCtrlMem   = VK_NULL_HANDLE;
    bool                     hdBuilt     = false;   // per-level guard; reset on map change
    int                      hdGrungeIdx = -1;      // DOOM-0179: world-grime overlay slot in
                                                    // the hdTex[] array (-1 = none loaded)
    int                      hdDirtIdx = -1;        // DOOM-0181: world-space dirt colour texture
                                                    // slot in hdTex[] (-1 = none loaded)

    VkDescriptorSetLayout dsLayout = VK_NULL_HANDLE;
    VkDescriptorPool      dsPool   = VK_NULL_HANDLE;
    VkDescriptorSet       ds       = VK_NULL_HANDLE;
    bool                  atlasReady = false;

    // 2D overlay (screens[0]) resources for the HUD/menu compositor. The image
    // is device-local R8 palette indices; a persistently-mapped staging buffer
    // streams the engine's 320x200 (HIRES-scaled) overlay into it each frame.
    VkImage        overlayImage   = VK_NULL_HANDLE;
    VkDeviceMemory overlayMemory  = VK_NULL_HANDLE;
    VkImageView    overlayView    = VK_NULL_HANDLE;
    VkBuffer       overlayStaging = VK_NULL_HANDLE;
    VkDeviceMemory overlayStagingMem = VK_NULL_HANDLE;
    void*          overlayMapped  = nullptr;
    const unsigned char* overlaySrc = nullptr;  // this frame's screens[0]
    int            overlayW = 0, overlayH = 0;
    int            overlayCapW = 0, overlayCapH = 0;   // size the resources were built at
    bool           overlayReady = false;

    // DOOM-0206 (L1b): display-resolution crisp menu text. A stb_truetype glyph atlas
    // (rb_text.c) baked ONCE at init into an R8 image, drawn as alpha-blended textured quads
    // by a dedicated 2D pipeline AFTER the paletted overlay, in the same present render pass.
    // Additive + 2D-only: no RT resource / push constant is touched (INV-5). menuFont keeps
    // the CPU-side glyph metrics for the whole session; its pixel buffer is freed right after
    // the one-time upload. menuFontReady=false (no system font) disables every text entry —
    // the game still runs, text is a menu-only overlay. rb_menu_text_active (a free-standing
    // extern) gates the per-frame flush so the paletted HUD/menu is untouched until m_menu
    // opts in (Tasks 3-6).
    rb_atlas_font_t menuFont       = {};
    bool            menuFontReady  = false;
    VkImage         textAtlas       = VK_NULL_HANDLE;
    VkDeviceMemory  textAtlasMemory = VK_NULL_HANDLE;
    VkImageView     textAtlasView   = VK_NULL_HANDLE;
    VkSampler       textSampler     = VK_NULL_HANDLE;   // linear + clamp (smooth glyph edges)
    VkPipeline            textPipeline       = VK_NULL_HANDLE;
    VkPipelineLayout      textPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout textDsLayout       = VK_NULL_HANDLE;
    VkDescriptorPool      textDsPool         = VK_NULL_HANDLE;
    VkDescriptorSet       textDs             = VK_NULL_HANDLE;
    // Per-frame glyph-quad vertex buffer (host-visible, persistently mapped). rb_text_draw
    // appends to the host-side textVerts vector during the frame; FlushMenuText memcpys it in
    // and draws it during command recording — after the top-of-frame fence, so the single copy
    // the GPU read last frame is finished (no double-buffering needed, unlike spriteVbuf).
    VkBuffer        textVbuf       = VK_NULL_HANDLE;
    VkDeviceMemory  textVbufMemory = VK_NULL_HANDLE;
    void*           textVbufMapped = nullptr;
    uint32_t        textVbufCap    = 0;   // capacity in vertices
    std::vector<TextVertex> textVerts;    // this frame's queued glyph quads

    // DOOM-0206 v2: the crisp menu skull cursor — the real WAD M_SKULL1 lump decoded to RGBA
    // and drawn through its own RGBA-sampling pipeline (cursor.frag), sized to a text row and
    // brightened. Reuses the text pipeline layout/DS-layout/sampler/vbuf; the verts are queued
    // into cursorVerts and appended after the glyph draw in FlushMenuText. cursorReady=false
    // (decode/upload failed) makes m_menu fall back to the paletted skull.
    VkImage          cursorImage  = VK_NULL_HANDLE;
    VkDeviceMemory   cursorMemory = VK_NULL_HANDLE;
    VkImageView      cursorView   = VK_NULL_HANDLE;
    VkDescriptorPool cursorDsPool = VK_NULL_HANDLE;
    VkDescriptorSet  cursorDs     = VK_NULL_HANDLE;
    VkPipeline       cursorPipeline = VK_NULL_HANDLE;
    bool             cursorReady  = false;
    int              cursorW = 0, cursorH = 0;
    std::vector<TextVertex> cursorVerts;   // this frame's queued cursor quad

    // DOOM-0206: the real M_DOOM logo lump, a SECOND RGBA menu sprite drawn on the main
    // menu only (bright, undimmed). Reuses g.cursorPipeline + textPipelineLayout/sampler —
    // only its own texture + descriptor + per-frame verts are new. logoReady=false falls
    // back to the crisp "DOOM" text title.
    VkImage          logoImage  = VK_NULL_HANDLE;
    VkDeviceMemory   logoMemory = VK_NULL_HANDLE;
    VkImageView      logoView   = VK_NULL_HANDLE;
    VkDescriptorPool logoDsPool = VK_NULL_HANDLE;
    VkDescriptorSet  logoDs     = VK_NULL_HANDLE;
    bool             logoReady  = false;
    int              logoW = 0, logoH = 0;
    std::vector<TextVertex> logoVerts;   // this frame's queued logo quad

    // column-major MVP from RB_Vulkan_RenderView; identity until the first
    // camera update so a frame drawn before then is well-defined (DOOM-0037).
    float viewProj[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    bool  haveCamera = false;

    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
    // DOOM-0170 L2a step 3: the off-screen scene canvas is 16-bit float so the world
    // shader's lighting can exceed 1.0 without clipping to flat white; the composite
    // pass then tone-maps it back into [0,1]. R16G16B16A16_SFLOAT has universal
    // colour-attachment + sampled + linear-filter support on desktop Vulkan.
    static constexpr VkFormat kSceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    // DOOM-0170 L2c — flashlight cast-shadow map (§4.4): a fixed 2048^2 D32 depth target,
    // sized independently of the swapchain/render-scale.
    static constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr uint32_t kShadowDim    = 2048;

    // Hardware ray tracing (DOOM-0009 build step 2). Enabled on the logical device
    // only when the chosen GPU advertises VK_KHR_acceleration_structure +
    // VK_KHR_ray_query *and* their feature bits + bufferDeviceAddress. Gates all
    // path-tracer work; the raster (Solid) path never touches it, so RT-off output
    // is unaffected (INV-10). False on a non-RT GPU, where Ultra is never offered
    // (INV-11).
    bool rtEnabled = false;

    // Ray-tracing acceleration structures (DOOM-0009 build step 2b), rebuilt once
    // per level from the static level mesh. The TLAS holds a single identity
    // instance of the BLAS for now; per-frame sprites + moving-sector refit land
    // in later build steps. All RT-gated — untouched while rtEnabled is false.
    VkAccelerationStructureKHR blas        = VK_NULL_HANDLE;
    VkBuffer                   blasBuf     = VK_NULL_HANDLE;
    VkDeviceMemory             blasMem     = VK_NULL_HANDLE;
    VkAccelerationStructureKHR tlas        = VK_NULL_HANDLE;
    VkBuffer                   tlasBuf     = VK_NULL_HANDLE;
    VkDeviceMemory             tlasMem     = VK_NULL_HANDLE;
    VkBuffer                   tlasInstBuf = VK_NULL_HANDLE;  // up to 2 instances, host-visible
    VkDeviceMemory             tlasInstMem = VK_NULL_HANDLE;
    void*                      tlasInstMapped = nullptr;      // [0]=world, [1]=sprites

    // Moving-sector AS refit (DOOM-0009 build step 5). The BLAS/TLAS are built
    // ALLOW_UPDATE so an open door/lift (DOOM-0049 patches the vertices each frame)
    // can be refit in place — far cheaper than a rebuild — instead of leaving the
    // traced geometry stale. Persistent update-scratch buffers (sized at build) feed
    // the in-place updates; blasDirty latches a moving-geometry frame so the refit
    // fires once the trace is active (even if the move happened under the raster path).
    VkBuffer       blasUpdScratch  = VK_NULL_HANDLE;
    VkDeviceMemory blasUpdScratchMem = VK_NULL_HANDLE;
    VkBuffer       tlasUpdScratch  = VK_NULL_HANDLE;
    VkDeviceMemory tlasUpdScratchMem = VK_NULL_HANDLE;
    bool           blasDirty       = false;
    // DOOM-0082: a switch press/revert (or an animated texture) swapped a face's
    // live texture id last frame; rebuild the NEE emitter set from the live vertex
    // buffer so a now-lit switch face pools light (and a reverted one stops).
    bool           worldEmitDirty  = false;
    bool           refitTimed      = false;   // print the measured refit cost once

    // DOOM-0100: world sprites (monsters/items/barrels) in the traced view. Each
    // frame the billboards RB_BuildSprites already produces (the raster path) are
    // written into this host-visible buffer, a throwaway triangle-soup BLAS is
    // (re)built over them, and that BLAS is added to the TLAS as a 2nd instance so
    // primary rays hit the sprites with real depth + lighting. The geometry is
    // NON-opaque (palette index 0 alpha-tests in the trace's candidate loop) and
    // carries instance mask 0x02 — shadow/NEE rays cull to 0x01 (world only), so
    // sprites neither cast (rectangular) shadows nor block light this increment.
    // As-built note: the DOOM-0008/0009 spec describes one unit-quad BLAS reused
    // via per-instance transforms; this triangle-soup-per-frame variant is the
    // simpler reuse of the existing billboard build (same on-screen result). The
    // transform variant is a perf follow-up (DOOM-0107). Same rb_vertex_t layout
    // as the world mesh, so the megakernel decodes a sprite hit identically.
    VkBuffer       sprWorldBuf       = VK_NULL_HANDLE;   // billboard verts (BLAS input + shader attrs)
    VkDeviceMemory sprWorldMem       = VK_NULL_HANDLE;
    void*          sprWorldMapped    = nullptr;
    uint32_t       sprWorldVertCap   = 0;
    uint32_t       sprWorldVertCount = 0;               // this frame's verts (6 per sprite)
    VkAccelerationStructureKHR spriteBlas        = VK_NULL_HANDLE;
    VkBuffer                   spriteBlasBuf     = VK_NULL_HANDLE;
    VkDeviceMemory             spriteBlasMem     = VK_NULL_HANDLE;
    VkBuffer                   spriteBlasScratch    = VK_NULL_HANDLE;
    VkDeviceMemory             spriteBlasScratchMem = VK_NULL_HANDLE;
    VkDeviceAddress            spriteBlasAddr    = 0;
    uint32_t                   sprBlasMaxTris    = 0;
    VkBuffer                   tlasBuildScratch    = VK_NULL_HANDLE;  // persistent: per-frame TLAS rebuild
    VkDeviceMemory             tlasBuildScratchMem = VK_NULL_HANDLE;

    // DOOM-0141: RT-only sky backdrop. A STATIC BLAS over the level's sky ceilings/
    // borders (rb_mesh_t.sky), on a 3rd TLAS instance (custom index 2, mask 0x04)
    // that ONLY primary rays see — so it occludes the view like classic DOOM's sky
    // without casting shadows or perturbing the GI bake (both cull to mask 0x01).
    // The vert buffer is created in BuildProbes (with g.vbuf); the BLAS in
    // BuildAccelerationStructures. skyTexnum is the sky wall-texture bindless id
    // (passed to the trace in misc4.w for the panorama sample).
    VkBuffer                   skyMeshBuf   = VK_NULL_HANDLE;
    VkDeviceMemory             skyMeshMem   = VK_NULL_HANDLE;
    uint32_t                   skyMeshVerts = 0;   // distinct from raster skyVertCount (line ~286)
    int                        skyMeshTexnum = 0;
    VkAccelerationStructureKHR skyBlas      = VK_NULL_HANDLE;
    VkBuffer                   skyBlasBuf   = VK_NULL_HANDLE;
    VkDeviceMemory             skyBlasMem   = VK_NULL_HANDLE;
    VkDeviceAddress            skyBlasAddr  = 0;

    // VK_KHR_acceleration_structure entry points — not core, so loaded by name once
    // the device is up (LoadRtEntryPoints); null while rtEnabled is false.
    PFN_vkGetAccelerationStructureBuildSizesKHR    pfnGetASBuildSizes = nullptr;
    PFN_vkCreateAccelerationStructureKHR           pfnCreateAS        = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR        pfnCmdBuildAS      = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetASAddress    = nullptr;
    PFN_vkDestroyAccelerationStructureKHR          pfnDestroyAS       = nullptr;
    // DOOM-0091: BLAS compaction (query compacted size + copy-compact).
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR pfnCmdWriteASProps = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR             pfnCmdCopyAS       = nullptr;
    VkDeviceSize                                   scratchAlign       = 256;

    // Path-tracer compute pass (DOOM-0009 build step 2c). A megakernel casts one
    // primary ray per pixel against the TLAS and writes a debug image (rtImage)
    // the present path blits to the swapchain when the rb_rtdebug toggle is on.
    // rtImage is swapchain-sized (recreated with it); the pipeline/descriptor are
    // built once. The compute descriptor binds the TLAS (b0) + storage image (b1)
    // and is rewritten whenever either changes (level load / swapchain rebuild).
    // All RT-gated — null/untouched while rtEnabled is false (INV-10/11).
    VkImage               rtImage    = VK_NULL_HANDLE;   // R8G8B8A8 storage target
    VkDeviceMemory        rtMemory   = VK_NULL_HANDLE;
    VkImageView           rtView     = VK_NULL_HANDLE;
    VkDescriptorSetLayout rtDsLayout = VK_NULL_HANDLE;
    VkDescriptorPool      rtDsPool   = VK_NULL_HANDLE;
    VkDescriptorSet       rtDs       = VK_NULL_HANDLE;
    VkPipelineLayout      rtPipeLayout = VK_NULL_HANDLE;
    // DOOM-0129: the megakernel's view-mode is a spec-constant, so each mode gets
    // its own specialised pipeline (the unused debug modes dead-strip out). The
    // module is kept alive to build variants lazily; rtPipeline is indexed by mode
    // value (1..6, slot 0 unused — mode 0 = RT off, never dispatched).
    VkShaderModule        rtModule     = VK_NULL_HANDLE;
    VkPipeline            rtPipeline[7] = {};

    // INV-6 verify accumulator (DOOM-0009 build step 4d). A fixed-size rgba32f
    // storage image (compute binding 2) the megakernel's mode-5 verify path sums
    // direct-only radiance into, plus a host-visible buffer the result is copied to
    // for the CPU rel-MSE / white-furnace check. Created once with the pipeline;
    // exercised only by RB_RtVerify (the `-rtverify` headless self-test), never the
    // display path. RT-gated.
    VkImage        rtAccum     = VK_NULL_HANDLE;   // rgba32f: rgb sum, a = sampleN
    VkDeviceMemory rtAccumMem  = VK_NULL_HANDLE;
    VkImageView    rtAccumView = VK_NULL_HANDLE;
    VkBuffer       rtReadback  = VK_NULL_HANDLE;   // host-visible copy target
    VkDeviceMemory rtReadbackMem = VK_NULL_HANDLE;

    // DOOM-0202: -shotverify headless screenshot / visual-regression capture (Ultra RT
    // first cut). A host-visible copy target for the RT finalImage + a small state
    // machine driven from the present path: arm from the parm, let the SVGF denoiser
    // settle for kShotWarmup rendered frames, capture the final display image on that
    // frame, then write a PNG and exit. shotCapture is set BEFORE RecordRtTrace records
    // this frame's copy; the PNG write + exit happen after the frame presents.
    VkBuffer       shotBuf     = VK_NULL_HANDLE;   // host-visible RGBA8 copy of the final image
    VkDeviceMemory shotBufMem  = VK_NULL_HANDLE;
    VkDeviceSize   shotBufSize = 0;
    int            shotFrame   = 0;                // rendered RT presents counted since arm
    bool           shotCapture = false;            // record the copy into THIS frame's cmd
    uint32_t       shotW = 0, shotH = 0;           // captured extent (display res)

    // SVGF denoiser (DOOM-0009 build step 6). Swapchain-sized G-buffer + history
    // images (recreated with the swapchain), one shared descriptor set, and three
    // compute pipelines (temporal accumulation 6a, edge-aware a-trous 6b, composite
    // 6a). The feed is pathtrace.comp mode 6 (writes set 2 = svgfDs). Only exercised
    // by the mode-6 display path (the `~` toggle's denoised view); RT-gated.
    VkImage        svImg[SV_COUNT]  = {};
    VkDeviceMemory svMem[SV_COUNT]  = {};
    VkImageView    svView[SV_COUNT] = {};
    VkDescriptorSetLayout svgfDsLayout   = VK_NULL_HANDLE;
    VkDescriptorPool      svgfDsPool     = VK_NULL_HANDLE;
    VkDescriptorSet       svgfDs         = VK_NULL_HANDLE;
    VkPipelineLayout      svgfPipeLayout = VK_NULL_HANDLE;
    VkPipeline            svgfTemporal   = VK_NULL_HANDLE;
    VkPipeline            svgfAtrous     = VK_NULL_HANDLE;
    VkPipeline            svgfComposite  = VK_NULL_HANDLE;
    // On-screen path-tracer mode label (debug). Re-uses svgfDsLayout (binding 7 =
    // rtImage) with its own push range; stamps the mode title before the blit.
    VkPipelineLayout      labelPipeLayout = VK_NULL_HANDLE;
    VkPipeline            labelPipeline   = VK_NULL_HANDLE;
    // Second descriptor set on svgfDsLayout whose output (binding 7) points at the
    // TAAU output instead of rtImage, so the label can be stamped on the upscaled
    // image (build step 6-d). The other bindings reuse the SVGF views (unread by the
    // label shader). Allocated with svgfDs; its binding 7 is written by the TAAU
    // descriptor write (after the TAAU output exists).
    VkDescriptorSet       labelTaauDs     = VK_NULL_HANDLE;

    // Temporal upscaler (DOOM-0009 build step 6-d). Display-resolution history +
    // output images, one descriptor set, one compute pipeline. Active only on the
    // mode-6 denoised path with the Upscaler setting on TAAU; otherwise the path
    // tracer + SVGF run at display resolution and the present path blits rtImage as
    // before (no behaviour change). RT-gated.
    VkImage        taImg[TA_COUNT]  = {};
    VkDeviceMemory taMem[TA_COUNT]  = {};
    VkImageView    taView[TA_COUNT] = {};
    VkDescriptorSetLayout taauDsLayout   = VK_NULL_HANDLE;
    VkDescriptorPool      taauDsPool     = VK_NULL_HANDLE;
    VkDescriptorSet       taauDs         = VK_NULL_HANDLE;
    VkPipelineLayout      taauPipeLayout = VK_NULL_HANDLE;
    VkPipeline            taauPipeline   = VK_NULL_HANDLE;
    uint32_t svgfFrame = 0;                       // parity counter for the history ping-pong
    // Previous denoised frame's camera basis, for the temporal reprojection.
    float prevCamPos[3]   = {};
    float prevCamDir[3]   = {};
    float prevCamRight[4] = {};                   // w = tan(hFov/2)
    float prevCamUp[4]    = {};                   // w = tan(vFov/2)

    // GI bake compute pass (DOOM-0009 build step 4b-ii). Its own descriptor set
    // (TLAS only — no output image) + pipeline; set 1 (materials) is shared with
    // the megakernel. Created once; the dispatch runs at each level load.
    VkDescriptorSetLayout bakeDsLayout   = VK_NULL_HANDLE;
    VkDescriptorPool      bakeDsPool     = VK_NULL_HANDLE;
    VkDescriptorSet       bakeDs         = VK_NULL_HANDLE;
    VkPipelineLayout      bakePipeLayout = VK_NULL_HANDLE;
    VkPipeline            bakePipeline   = VK_NULL_HANDLE;

    // Direct-lighting emitters (DOOM-0009 build step 3b). matEmisBuf is the
    // WAD-global per-material Le table (3 floats/material, linear RGB) built with
    // the material array; matEmissive is the CPU mirror the per-level emitter
    // extraction reads to decide which triangles emit. emitBuf is this level's
    // emitter-triangle list (12 floats/tri: v0 v1 v2 Le), rebuilt per level;
    // emitCount is its triangle count. Both are device-address storage buffers the
    // megakernel reads via buffer_reference (NEE, step 3c). RT-gated.
    std::vector<float> matEmissive;                 // CPU Le mirror, 3*matCount
    VkBuffer       matEmisBuf = VK_NULL_HANDLE;     // GPU Le table (device address)
    VkDeviceMemory matEmisMem = VK_NULL_HANDLE;
    VkBuffer       emitBuf    = VK_NULL_HANDLE;     // emitter list (host-visible, persistent)
    VkDeviceMemory emitMem    = VK_NULL_HANDLE;
    void*          emitMapped = nullptr;
    uint32_t       emitCount  = 0;                  // emitters this frame (static + sprites)
    uint32_t       emitCap    = 0;                  // record capacity of emitBuf
    // DOOM-0084: the static walls/flats emitters are extracted once at level load and
    // cached here (14 floats/record, cdf/pdf zeroed). Each traced frame the emissive
    // world sprites (lamps/torches/burning barrels — material Le > 0) are appended and
    // the merged list + CDF re-finalised into emitBuf, so those free-standing lights
    // pool light + cast shadows onto their surroundings via the same NEE path.
    std::vector<float> staticEmit;                  // cached static emitter records
    std::vector<float> staticWgt;                   // cached static power weights

    // GI bake probes (DOOM-0009 build step 4). One irradiance probe per subsector
    // (placed by RB_BuildProbes at the convex-cell centroid), baked once per level
    // load. Each record is 16 floats: pos[3] + pad + SH-L1 directional irradiance
    // (channel-major: R[4] G[4] B[4]). A host-visible device-address SSBO — the CPU
    // writes the positions, the bake compute pass writes the SH payload, and the
    // megakernel reads it to replace the flat ambient fill (step 4c). RT-gated.
    VkBuffer       probeBuf   = VK_NULL_HANDLE;
    VkDeviceMemory probeMem   = VK_NULL_HANDLE;
    uint32_t       probeCount = 0;
    // Second probe buffer + per-triangle subsector-id buffer (step 4c). The bake
    // ping-pongs probeBuf <-> probeBuf2 across bounce passes (read the previous
    // pass, write the current); the final bounce lands in probeBuf, which the
    // megakernel reads, keyed per hit by triSsBuf (triangle -> subsector -> probe).
    VkBuffer       probeBuf2  = VK_NULL_HANDLE;
    VkDeviceMemory probeMem2  = VK_NULL_HANDLE;
    VkBuffer       triSsBuf   = VK_NULL_HANDLE;
    VkDeviceMemory triSsMem   = VK_NULL_HANDLE;

    // DOOM-0119 REJECT-lump light cull. subSecBuf maps subsector -> owning sector
    // (per level); rejectBuf is the WAD REJECT visibility bitmatrix packed as uint
    // words (per level); emitSecBuf is per-emitter sector, host-visible + refilled
    // each frame alongside emitBuf (static records carry a 0xFFFFFFFF sentinel = no
    // cull). numSectors gates the shader cull (0 = no REJECT lump -> cull off).
    VkBuffer       subSecBuf     = VK_NULL_HANDLE;
    VkDeviceMemory subSecMem     = VK_NULL_HANDLE;
    VkBuffer       rejectBuf     = VK_NULL_HANDLE;
    VkDeviceMemory rejectMem     = VK_NULL_HANDLE;
    VkBuffer       emitSecBuf    = VK_NULL_HANDLE;
    VkDeviceMemory emitSecMem    = VK_NULL_HANDLE;
    void*          emitSecMapped = nullptr;
    uint32_t       numSectors    = 0;            // REJECT matrix dimension (0 = cull off)

    // DOOM-0170 L1b: per-subsector dynamic point lights (RT-off raster). lightBuf is a
    // host-visible device-address SSBO — RASTER_MAX_LIGHTS_PER_SUBSECTOR records of 6
    // floats (centroid[3] Le[3]) per subsector — refilled each raster frame by
    // BuildRasterPointLights from the NEE emitter list. The CPU cull reuses the probe
    // centroids (subCentroid), the per-subsector sector map + REJECT matrix (numSectors
    // above / rejectCPU), and a reused emitter-centroid scratch. Allocated whenever
    // probeCount>0, so mesh.frag's probeCount>0 guard also guarantees lightBuf is bound.
    VkBuffer       lightBuf    = VK_NULL_HANDLE;
    VkDeviceMemory lightMem    = VK_NULL_HANDLE;
    void*          lightMapped = nullptr;
    std::vector<rb_probe_t> subCentroid;         // per-subsector centroid (cull ranking)
    std::vector<int32_t>    subSecSector;        // per-subsector sector (REJECT cull; empty = off)
    const unsigned char*    rejectCPU = nullptr; // REJECT bitmatrix (PU_LEVEL, level lifetime)
    std::vector<float>      emitCentroidScratch; // reused per-frame emitter centroids
    // DOOM-0170 perf: per-subsector nearest-N cache over the STATIC emitters only. Those
    // records carry the no-reject sentinel, so each is tested against EVERY subsector —
    // an unpruned O(subsectors × staticEmitters) cull that dominated the frame (~8 ms).
    // It is camera- and frame-invariant, so RebuildStaticPointLightCache computes it once
    // (on staticLightsDirty) and the per-frame BuildRasterPointLights copies it and merges
    // only the handful of moving sprite emitters on top.
    std::vector<float>      staticLightCache;    // sub × N × 6 floats (centroid[3] Le[3])
    std::vector<int32_t>    staticLightCount;    // per-subsector count of cached static lights
    bool                    staticLightsDirty = true;  // static emitter set changed -> recache

    bool ready        = false;
    bool needRecreate = false;
};

VulkanState g;

// Defined below (after the AS build that first calls it): (re)point the compute
// descriptor at the current TLAS + storage image once both exist.
void UpdateRtComputeDescriptor();
// Defined further down; used by the path-tracer pipeline built above it.
VkShaderModule MakeShader(const unsigned char* code, unsigned len);

// Debug wireframe toggle: 0 = normal fill, non-zero = draw the world (and
// sprites) as wireframe over a filled sky backdrop. Flipped from i_video.c's
// gamepad poll (PS4 Share button). C linkage so the C input layer can extern it.
// No effect in Classic (no Vulkan path) or when the GPU lacks fillModeNonSolid.
extern "C" { int rb_wireframe = 0; }

// Path-tracer debug view (DOOM-0009), cycled by the `~` key:
// 0 = off (normal raster/overlay present), 1 = ray-traced intersection/normal
// visualization, 2 = white-furnace energy check, 3 = textured/sector-lit surface
// (build step 3a), 4 = NEE direct lighting with ray-traced shadows (build step
// 3c). Only acts when the GPU has RT and a TLAS exists (in-level); harmless
// otherwise. C linkage for i_video.c.
// DOOM-0116: persisted via m_misc.c ("rt_view"); defaults to 6 (denoised SVGF) so
// Ultra shows the path-traced view on first run. Loaded value clamped in RB_Init.
extern "C" { int rb_rtdebug = 6; }

// DOOM-0135: "Debug Views" menu toggle. When 0 (default) the `~` key is a plain
// ray-tracing on/off switch (rb_rtdebug 6<->0); when 1 it cycles the full set of
// path-tracer diagnostic views (1-4) as before. Persisted via m_misc.c
// ("rt_debug_views"); read only by the i_video.c `~` handler + the menu. C linkage.
extern "C" { int rb_rtdebug_menu = 0; }

// Player flashlight / headlamp (DOOM-0044). Toggled by the player (keyboard F or
// gamepad L1, edge-detected in i_video.c) and persisted by m_misc.c. A spotlight
// at the eye aimed along the view: in Ultra the path tracer casts its ray-traced
// shadows (pathtrace.comp, gated on misc2.w); in Solid the raster fragment shader
// lights a cone with no shadows (mesh.frag). 0 = off, 1 = on. No effect in
// Classic. C linkage for the C input/config layers.
extern "C" { int rb_flashlight = 0; }

// Temporal upscaler settings (DOOM-0009 build step 6-d), set from the Options menu
// and persisted by m_misc.c. rb_upscaler: 0 = Off (present rtImage at full display
// resolution, as before), 1 = TAAU (custom temporal upscaler; FSR 2 / FSR 3.1 are
// later phases on the same contract). rb_renderscale: the path tracer's render
// resolution as a percent of the display (100/75/67/50); only takes effect with an
// upscaler active and on the mode-6 denoised path. C linkage for the C menu/config.
extern "C" { int rb_upscaler = 1; int rb_renderscale = 50; int rb_exposure = 10; }   // DOOM-0090: TAAU @ 50% default

// DOOM-0170 L2b — SSAO (contact/ambient shadows) on/off, persisted as "ssao". Per §6 each
// screen-space effect gets its own gate so a misbehaving pass can be switched off in isolation.
// 1 = on (default). Gates both the half-res SSAO pass and the composite's AO×ambient multiply.
extern "C" { int rb_ssao = 1; }

// DOOM-0170 L2b SSAO tuning dials (§4.3; §9 play-test). RADIUS is the hemisphere reach in
// world units (DOOM scale: 16 = player radius, 56 = player height) — how far a corner/contact
// darkens; INTENSITY how strong; POWER the contrast curve; BIAS kills self-occlusion acne.
// Play-test 2026-07-14: RADIUS dropped 64->40 so taps stay near genuine contacts (a wide reach
// amplified the normal-from-depth streak at convex wall corners and the view-dependent haloing);
// BIAS 1.5->2.0 to kill the vertical corner acne; INTENSITY 1.6->1.3 as the floor AO read a touch
// strong once it also weighted DIRECT (composite AO_DIRECT_WEIGHT).
static const float kSsaoRadius    = 40.0f;
static const float kSsaoBias      = 2.0f;
static const float kSsaoIntensity = 1.3f;
static const float kSsaoPower     = 1.5f;

// DOOM-0090: per-pass GPU profiler toggle (the `\` key; persisted as rt_profile).
// When on, the path tracer's per-stage GPU cost is timestamped and printed to the
// terminal once a second. Off by default; RT-only (the raster path never reads it).
extern "C" { int rb_profile = 0; }
extern "C" int I_GetTimeMS(void);   // i_system.c; for the rt_profile once-a-second report

// DOOM-0181: de-tile quality dial (the `]` key; persisted as rt_detile). 0 = off, 1 = 2-tap,
// 2 = 4-tap. Drives pc.misc5.y on the Ultra RT path; the shader ignores it unless HD materials
// are loaded (it is gated on g.hdBuilt at the populate site). Default 4-tap; RT-only.
extern "C" { int rb_detile = 2; }

// DOOM-0187: filth (dirt-stain) master toggle (the `[` key; persisted as rt_filth). 1 = on
// (shipped look), 0 = off. Drives pc.misc5.w on the Ultra RT path; gates applyGrime on ALL
// non-sprite world surfaces (not HD-gated, unlike de-tile). Lets the stain cost be A/B'd live
// and stands as a perf/quality option. Default on; RT-only (no effect in Classic/Solid).
extern "C" { int rb_filth = 1; }

// DOOM-0183: wet-liquid toggle (the `'` key; persisted as rt_wet). 1 = on (shipped look),
// 0 = off. Drives pc.misc6.y on the Ultra RT path; gates ONLY the shader-side, view/time
// layers — the wet sheen (§4.4), the nukage ripples (§4.5), and the goo-puddle wet (§4.6).
// It does NOT gate the glow/cast-light: that Le is CPU-built into g.matEmissive + the NEE
// emitter set + the GI bake, is permanent, and delivers DOOM-0083 (§5). Default on; RT-only.
extern "C" { int rb_wet = 1; }

// INV-6 headless self-test latch (DOOM-0009 build step 4d). Set from the
// `-rtverify` command-line parm; the first ready present runs RB_RtVerify (the
// rel-MSE + white-furnace proof) and exits. -1 = unchecked, 0 = off, 1 = armed.
int rb_rtverify = -1;

// DOOM-0202: -shotverify headless screenshot / visual-regression latch. Set from the
// `-shotverify [path]` OR `-shotcompare <ref.png>` parm; renders the Ultra RT view for
// kShotWarmup frames (so the SVGF denoiser converges on the static spawn view), copies
// the final display image, and exits. -1 = unchecked, 0 = off, 1 = armed. First cut is
// Ultra-only (the raster/Solid path renders straight into the swapchain with no
// TRANSFER_SRC image to copy — a follow-up). A watchdog gives up if the RT view never
// becomes ready. -shotverify writes a full-res PNG (eyeballing); -shotcompare is the
// automated regression gate (downscaled golden + mean-abs-error threshold, below).
int rb_shotverify = -1;
static const int    kShotWarmup = 45;    // rendered RT frames before capture (denoiser settle)
static const int    kShotGiveUp = 600;   // presents while armed w/o an RT frame -> bail (misconfig)
static const int    kGoldenEdge = 640;   // -shotcompare: canonical golden longest edge (git-friendly)
static const double kGoldenMAE  = 3.0;   // -shotcompare: max mean-abs-error (0..255) before FAIL

// Verify accumulator resolution — small + fixed (independent of the swapchain) so
// the high-sample-count convergence runs in well under a second per estimator.
static const uint32_t kVerifyW = 320;
static const uint32_t kVerifyH = 200;

[[noreturn]] void Fail(const char* what, VkResult r)
{
    I_Error("R_Vulkan: %s failed (VkResult %d)", what, (int)r);
}

inline void Check(VkResult r, const char* what)
{
    if (r != VK_SUCCESS)
        Fail(what, r);
}

// Is a named instance layer available? (Used to enable validation only when
// the Vulkan SDK's validation layer is actually installed — INV-8.)
bool HasInstanceLayer(const char* name)
{
    uint32_t n = 0;
    vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    if (n)
        vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (const VkLayerProperties& l : layers)
        if (!strcmp(l.layerName, name))
            return true;
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        printf("R_Vulkan [validation]: %s\n", data->pMessage);
    return VK_FALSE;
}

void CreateInstance()
{
    SDL_Window* window = (SDL_Window*)I_GetWindow();

    // SDL tells us which instance extensions the surface needs on this platform.
    unsigned extCount = 0;
    if (!SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr))
        I_Error("R_Vulkan: SDL_Vulkan_GetInstanceExtensions: %s", SDL_GetError());
    std::vector<const char*> exts(extCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, exts.data());

    // Enable validation + debug-utils only if the layer is present locally.
    const char* kValidation = "VK_LAYER_KHRONOS_validation";
    bool validate = HasInstanceLayer(kValidation);
    std::vector<const char*> layers;
    if (validate)
    {
        layers.push_back(kValidation);
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo app = {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "DOOM_Ants";
    app.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &app;
    ici.enabledExtensionCount = (uint32_t)exts.size();
    ici.ppEnabledExtensionNames = exts.data();
    ici.enabledLayerCount = (uint32_t)layers.size();
    ici.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    Check(vkCreateInstance(&ici, nullptr, &g.instance), "vkCreateInstance");

    if (validate)
    {
        auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            g.instance, "vkCreateDebugUtilsMessengerEXT");
        if (create)
        {
            VkDebugUtilsMessengerCreateInfoEXT dci = {};
            dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            dci.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dci.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dci.pfnUserCallback = DebugCallback;
            // DOOM-0073: the messenger is a debug-only convenience, so a failure is
            // non-fatal (unlike the load-bearing Check() calls) — capture the result
            // and warn rather than silently drop it or abort the app.
            VkResult dbg = create(g.instance, &dci, nullptr, &g.debug);
            if (dbg != VK_SUCCESS)
                std::fprintf(stderr, "R_Vulkan: vkCreateDebugUtilsMessengerEXT "
                             "failed (VkResult %d); continuing without validation logging\n",
                             (int)dbg);
        }
    }
}

// Does this device expose the hardware ray-tracing extensions? (Used to prefer
// an RT-capable GPU when more than one device can present.)
bool DeviceHasRT(VkPhysicalDevice d)
{
    uint32_t n = 0;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    if (n)
        vkEnumerateDeviceExtensionProperties(d, nullptr, &n, exts.data());
    bool accel = false, rayq = false;
    for (const VkExtensionProperties& e : exts)
    {
        if (!strcmp(e.extensionName, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME))
            accel = true;
        if (!strcmp(e.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME))
            rayq = true;
    }
    return accel && rayq;
}

// First queue family with graphics + present support for our surface, or -1.
int FindGraphicsPresentFamily(VkPhysicalDevice d)
{
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &n, nullptr);
    std::vector<VkQueueFamilyProperties> fams(n);
    if (n)
        vkGetPhysicalDeviceQueueFamilyProperties(d, &n, fams.data());
    for (uint32_t i = 0; i < n; ++i)
    {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(d, i, g.surface, &present);
        if ((fams[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
            return (int)i;
    }
    return -1;
}

void PickPhysicalAndDevice()
{
    uint32_t n = 0;
    // DOOM-0073: device selection depends on this list; surface a failure precisely
    // rather than proceed with an empty/garbage device set (Check-wrap, INV init path).
    Check(vkEnumeratePhysicalDevices(g.instance, &n, nullptr), "vkEnumeratePhysicalDevices(count)");
    std::vector<VkPhysicalDevice> devs(n);
    if (n)
        Check(vkEnumeratePhysicalDevices(g.instance, &n, devs.data()), "vkEnumeratePhysicalDevices(fill)");

    // Prefer an RT-capable device that can present; otherwise the first device
    // that can present. (The integrator chooses RT vs. raster later; a cleared
    // frame works on any.)
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    int chosenFamily = -1;
    for (VkPhysicalDevice d : devs)
    {
        int fam = FindGraphicsPresentFamily(d);
        if (fam < 0)
            continue;
        if (chosen == VK_NULL_HANDLE)
        {
            chosen = d;
            chosenFamily = fam;
        }
        if (DeviceHasRT(d))
        {
            chosen = d;
            chosenFamily = fam;
            break;
        }
    }
    if (chosen == VK_NULL_HANDLE)
        I_Error("R_Vulkan: no Vulkan device can present to the window.");

    g.phys = chosen;
    g.queueFamily = (uint32_t)chosenFamily;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci = {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g.queueFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    // Swapchain is always required. The hardware-RT extension chain (acceleration
    // structure + ray query + their deferred-host-ops dependency) is appended below
    // only when the chosen GPU supports ray tracing (DOOM-0009 build step 2); on a
    // non-RT GPU the device stays raster-only.
    std::vector<const char*> devExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    // Bindless materials (DOOM-0009 build step 1) index an array-of-textures by
    // material id. That needs descriptor indexing — core in Vulkan 1.2, which we
    // already target, so we enable it through VkPhysicalDeviceVulkan12Features
    // rather than the legacy VK_EXT_descriptor_indexing string. The materials
    // path is now bindless-only (no atlas fallback), so a device that lacks these
    // four features cannot run the 3D renderer — fail init clearly, the same way
    // a non-presenting device does above. This is effectively unreachable on real
    // hardware: any GPU whose driver exposes Vulkan 1.2 supports all four.
    // RB_VulkanProbe now applies this same four-feature gate at probe time
    // (DOOM-0059), so the menu never offers Solid/Ultra on such a GPU and this
    // I_Error is a belt-and-braces backstop rather than the primary guard.
    //
    // The same features-2 query also reads the RT feature bits (accelerationStructure
    // + rayQuery + bufferDeviceAddress, the AS build reads vertices by GPU address):
    // the extension strings alone don't guarantee the feature is usable.
    VkPhysicalDeviceVulkan12Features have12 = {};
    have12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR haveAccel = {};
    haveAccel.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayQueryFeaturesKHR haveRayQuery = {};
    haveRayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    have12.pNext = &haveAccel;
    haveAccel.pNext = &haveRayQuery;
    VkPhysicalDeviceFeatures2 have2 = {};
    have2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    have2.pNext = &have12;
    vkGetPhysicalDeviceFeatures2(g.phys, &have2);

    bool hasDescriptorIndexing =
        have12.runtimeDescriptorArray &&
        have12.shaderSampledImageArrayNonUniformIndexing &&
        have12.descriptorBindingVariableDescriptorCount &&
        have12.descriptorBindingPartiallyBound;
    if (!hasDescriptorIndexing)
        I_Error("R_Vulkan: GPU lacks Vulkan 1.2 descriptor indexing "
                "(runtimeDescriptorArray / nonUniform / variableCount / "
                "partiallyBound) — the 3D renderer needs bindless materials.");

    // Ray tracing is enabled when the device advertises the extensions (DeviceHasRT,
    // the same gate the tier-probe uses) AND the matching feature bits. Otherwise
    // Ultra is silently unavailable (INV-11) and only the raster 3D path runs.
    g.rtEnabled = DeviceHasRT(g.phys)
                  && haveAccel.accelerationStructure
                  && haveRayQuery.rayQuery
                  && have12.bufferDeviceAddress;

    VkPhysicalDeviceVulkan12Features enable12 = {};
    enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enable12.runtimeDescriptorArray                     = VK_TRUE;
    enable12.shaderSampledImageArrayNonUniformIndexing  = VK_TRUE;
    enable12.descriptorBindingVariableDescriptorCount   = VK_TRUE;
    enable12.descriptorBindingPartiallyBound            = VK_TRUE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR enableAccel = {};
    enableAccel.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    enableAccel.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceRayQueryFeaturesKHR enableRayQuery = {};
    enableRayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    enableRayQuery.rayQuery = VK_TRUE;

    if (g.rtEnabled)
    {
        // bufferDeviceAddress lets the AS build read the mesh vertex buffer by GPU
        // address; the two RT feature structs chain after the 1.2 features.
        enable12.bufferDeviceAddress = VK_TRUE;
        enable12.pNext     = &enableAccel;
        enableAccel.pNext  = &enableRayQuery;
        devExts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        devExts.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        devExts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        printf("RB_Vulkan: hardware ray tracing enabled "
               "(acceleration structure + ray query).\n");
    }
    else
    {
        printf("RB_Vulkan: hardware ray tracing unavailable; raster 3D only.\n");
    }
    fflush(stdout);

    // Optional: fillModeNonSolid enables the polygonMode-LINE wireframe debug
    // pipeline (rb_wireframe / gamepad Share). Near-universal on desktop GPUs,
    // but strictly optional -- if absent we just skip building wirePipeline.
    g.wireSupported = have2.features.fillModeNonSolid;
    VkPhysicalDeviceFeatures enableBase = {};
    enableBase.fillModeNonSolid = g.wireSupported ? VK_TRUE : VK_FALSE;
    // The SVGF denoiser (DOOM-0009 step 6) indexes storage-image ARRAYS by a
    // runtime (dynamically-uniform) parity — gpos[cur], atrous[ping], hcol[prev].
    // That needs shaderStorageImageArrayDynamicIndexing; without it the index
    // silently collapses to 0 on some drivers, degenerating the temporal + a-trous
    // ping-pong (the denoiser becomes a no-op). Near-universal on RT-class GPUs;
    // request it when present, warn (don't fail) if absent — only mode 6 needs it.
    if (have2.features.shaderStorageImageArrayDynamicIndexing)
        enableBase.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    else if (g.rtEnabled)
        printf("RB_Vulkan: WARNING — no shaderStorageImageArrayDynamicIndexing; "
               "the SVGF denoiser (~ mode 6) will not function.\n");

    // DOOM-0170 L1a: the raster fragment shader reads gl_PrimitiveID (triangle ->
    // subsector -> GI probe / point-light list), which SPIR-V exposes only under the
    // Geometry capability, so the geometryShader feature must be enabled or the
    // shader module reads garbage / is rejected. Core Vulkan 1.0, effectively
    // universal on desktop GPUs (RX 6600, GTX 1050/2060). Enable when present; if a
    // device genuinely lacks it the pipeline create below fails loudly via Check().
    if (have2.features.geometryShader)
        enableBase.geometryShader = VK_TRUE;

    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &enable12;   // required (we I_Error above if unsupported)
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)devExts.size();
    dci.ppEnabledExtensionNames = devExts.data();
    dci.pEnabledFeatures = &enableBase;

    Check(vkCreateDevice(g.phys, &dci, nullptr, &g.device), "vkCreateDevice");
    vkGetDeviceQueue(g.device, g.queueFamily, 0, &g.queue);
}

void CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps = {};
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.phys, g.surface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    // Surface format: prefer 8-bit BGRA *UNORM* (not _SRGB). The bring-up shader
    // writes DOOM's palette colours, which are already display-encoded (sRGB)
    // values; a _SRGB swapchain would apply the sRGB transfer a second time and
    // wash the image out (and lift the dark background to a glowing grey). A
    // UNORM swapchain presents our bytes as-is, matching the software renderer's
    // output exactly. The scene-linear workflow + a real tonemap (writing linear
    // radiance into an _SRGB target) arrives with the path tracer (DOOM-0009).
    uint32_t fn = 0;
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fn, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR(count)");
    if (fn == 0)
        I_Error("R_Vulkan: surface reports no formats");
    std::vector<VkSurfaceFormatKHR> formats(fn);
    Check(vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fn, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
    VkSurfaceFormatKHR fmt = formats[0];
    for (const VkSurfaceFormatKHR& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            fmt = f;
            break;
        }
    g.format = fmt.format;

    // Extent: honour the surface's fixed size, else the window's drawable size.
    if (caps.currentExtent.width != UINT32_MAX)
    {
        g.extent = caps.currentExtent;
    }
    else
    {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize((SDL_Window*)I_GetWindow(), &w, &h);
        g.extent.width  = (uint32_t)w;
        g.extent.height = (uint32_t)h;
        if (g.extent.width  < caps.minImageExtent.width)  g.extent.width  = caps.minImageExtent.width;
        if (g.extent.height < caps.minImageExtent.height) g.extent.height = caps.minImageExtent.height;
        if (g.extent.width  > caps.maxImageExtent.width)  g.extent.width  = caps.maxImageExtent.width;
        if (g.extent.height > caps.maxImageExtent.height) g.extent.height = caps.maxImageExtent.height;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = g.surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = fmt.format;
    sci.imageColorSpace = fmt.colorSpace;
    sci.imageExtent = g.extent;
    sci.imageArrayLayers = 1;
    // TRANSFER_DST so the clear path can vkCmdClearColorImage directly; later
    // increments add COLOR_ATTACHMENT for the G-buffer/composite passes.
    sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;   // single graphics+present queue
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // Present mode: prefer MAILBOX so the render loop runs flat-out and shows its
    // true frame rate with the lowest input lag and no tearing -- on a high-refresh
    // monitor this delivers the full rate, and on a 60 Hz panel the engine still
    // runs ahead of the display (snappier controls, honest FPS number). FIFO (plain
    // vsync, always supported) is the fallback when MAILBOX is unavailable. Neither
    // caps below the monitor's refresh -- the old FIFO-only path already rose to
    // whatever the display could show; MAILBOX just stops it blocking on vsync.
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;   // guaranteed present
    {
        uint32_t pmCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g.phys, g.surface, &pmCount, nullptr);
        std::vector<VkPresentModeKHR> modes(pmCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g.phys, g.surface, &pmCount, modes.data());
        for (VkPresentModeKHR m : modes)
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) { presentMode = m; break; }
    }
    sci.presentMode = presentMode;
    // MAILBOX needs a third image to hold the queued-but-not-presented frame; bump
    // the count if the surface minimum left us at two (clamped to the max below).
    if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR && sci.minImageCount < 3)
    {
        sci.minImageCount = 3;
        if (caps.maxImageCount && sci.minImageCount > caps.maxImageCount)
            sci.minImageCount = caps.maxImageCount;
    }
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = g.swapchain;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    Check(vkCreateSwapchainKHR(g.device, &sci, nullptr, &created),
          "vkCreateSwapchainKHR");

    if (g.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(g.device, g.swapchain, nullptr);
    g.swapchain = created;

    uint32_t ic = 0;
    // DOOM-0073: the image count sizes every per-image resource; a dropped failure
    // here would resize g.images from a garbage count. Check-wrap both calls.
    Check(vkGetSwapchainImagesKHR(g.device, g.swapchain, &ic, nullptr),
          "vkGetSwapchainImagesKHR(count)");
    g.images.resize(ic);
    Check(vkGetSwapchainImagesKHR(g.device, g.swapchain, &ic, g.images.data()),
          "vkGetSwapchainImagesKHR(fill)");
}

// (Re)create the per-swapchain-image renderFinished semaphores, sized to the
// current image count. Destroys any existing set first, so it is safe to call on
// swapchain recreate (the image count can change). Callers guarantee the queue is
// idle (init order, or vkDeviceWaitIdle in RecreateSwapchain) before the destroy.
void CreateRenderFinishedSemaphores()
{
    for (VkSemaphore s : g.renderFinished)
        vkDestroySemaphore(g.device, s, nullptr);
    g.renderFinished.assign(g.images.size(), VK_NULL_HANDLE);

    VkSemaphoreCreateInfo semci = {};
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore& s : g.renderFinished)
        Check(vkCreateSemaphore(g.device, &semci, nullptr, &s),
              "vkCreateSemaphore(renderFinished)");
}

void CreateCommandsAndSync()
{
    VkCommandPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = g.queueFamily;
    Check(vkCreateCommandPool(g.device, &pci, nullptr, &g.cmdPool),
          "vkCreateCommandPool");

    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    Check(vkAllocateCommandBuffers(g.device, &ai, &g.cmd),
          "vkAllocateCommandBuffers");

    VkSemaphoreCreateInfo semci = {};
    semci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    Check(vkCreateSemaphore(g.device, &semci, nullptr, &g.imageAvailable), "vkCreateSemaphore");
    CreateRenderFinishedSemaphores();   // one per swapchain image (g.images is up)

    // Created signalled so the first frame's wait passes (single frame in flight).
    VkFenceCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    Check(vkCreateFence(g.device, &fci, nullptr, &g.inFlight), "vkCreateFence");

    // DOOM-0090: timestamp query pool for the opt-in per-pass GPU profiler. Needs a
    // non-zero timestampPeriod (the device can convert ticks to ns) and a queue that
    // can write timestamps (timestampValidBits != 0); otherwise leave it null and the
    // profiler stays a no-op. 8 slots (5 used) to keep the reset/read simple.
    VkPhysicalDeviceProperties pdp = {};
    vkGetPhysicalDeviceProperties(g.phys, &pdp);
    g.timestampPeriod = pdp.limits.timestampPeriod;
    uint32_t nqf = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &nqf, nullptr);
    std::vector<VkQueueFamilyProperties> qfp(nqf);
    vkGetPhysicalDeviceQueueFamilyProperties(g.phys, &nqf, qfp.data());
    if (g.timestampPeriod > 0.0f && g.queueFamily < nqf &&
        qfp[g.queueFamily].timestampValidBits != 0)
    {
        VkQueryPoolCreateInfo qpci = {};
        qpci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qpci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
        qpci.queryCount = 8;
        Check(vkCreateQueryPool(g.device, &qpci, nullptr, &g.gpuTimerPool),
              "vkCreateQueryPool(timers)");
    }
}

// glibc only exposes M_PI under -std=gnu*, not strict -std=c++17 (this TU);
// keep our own so the build doesn't depend on feature-test macros.
constexpr float kPi = 3.14159265358979323846f;

// A memory type satisfying `typeBits` (from VkMemoryRequirements) with all the
// requested property flags (e.g. DEVICE_LOCAL, or HOST_VISIBLE|HOST_COHERENT).
uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mem = {};
    vkGetPhysicalDeviceMemoryProperties(g.phys, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    I_Error("R_Vulkan: no memory type with the required properties.");
}

// A short-lived primary command buffer for one-off uploads (image staging
// copies, layout transitions). Recorded, submitted, and waited on inline — the
// per-level/atlas uploads are not on the frame hot path.
VkCommandBuffer BeginOneTime()
{
    VkCommandBufferAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = g.cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    Check(vkAllocateCommandBuffers(g.device, &ai, &cb), "vkAllocateCommandBuffers(oneTime)");

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer(oneTime)");
    return cb;
}

void EndOneTime(VkCommandBuffer cb)
{
    Check(vkEndCommandBuffer(cb), "vkEndCommandBuffer(oneTime)");
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    Check(vkQueueSubmit(g.queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit(oneTime)");
    vkQueueWaitIdle(g.queue);
    vkFreeCommandBuffers(g.device, g.cmdPool, 1, &cb);
}

inline VkDeviceAddress AlignUp(VkDeviceAddress v, VkDeviceSize a)
{
    return a ? ((v + a - 1) / a) * a : v;
}

// GPU address of a buffer (core in Vulkan 1.2; the memory must have been allocated
// with VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT, which CreateRtBuffer ensures).
VkDeviceAddress BufferAddress(VkBuffer b)
{
    VkBufferDeviceAddressInfo i = {};
    i.sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    i.buffer = b;
    return vkGetBufferDeviceAddress(g.device, &i);
}

// Buffer + dedicated allocation for ray-tracing use (AS storage, scratch, the TLAS
// instance array). When the usage includes SHADER_DEVICE_ADDRESS the allocation
// gets the device-address flag so BufferAddress works.
void CreateRtBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags props,
                    VkBuffer* buf, VkDeviceMemory* mem)
{
    VkBufferCreateInfo bci = {};
    bci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size        = size;
    bci.usage       = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(g.device, &bci, nullptr, buf), "vkCreateBuffer(rt)");

    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(g.device, *buf, &req);

    VkMemoryAllocateFlagsInfo flags = {};
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo mai = {};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.pNext           = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) ? &flags : nullptr;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
    Check(vkAllocateMemory(g.device, &mai, nullptr, mem), "vkAllocateMemory(rt)");
    Check(vkBindBufferMemory(g.device, *buf, *mem, 0), "vkBindBufferMemory(rt)");
}

// Create a host-visible device-address STORAGE buffer and fill it with `bytes` of
// `data`. The path tracer reads these (the per-material Le table, the per-level
// emitter list) by GPU address via buffer_reference, the same way it reads the
// vertex buffer. Host-visible + coherent so the one-shot upload is a plain memcpy
// (these are small and written once per level/session — no staging needed).
void UploadAddressBuffer(const void* data, VkDeviceSize bytes,
                         VkBuffer* buf, VkDeviceMemory* mem)
{
    CreateRtBuffer(bytes,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   buf, mem);
    void* mapped = nullptr;
    Check(vkMapMemory(g.device, *mem, 0, bytes, 0, &mapped), "vkMapMemory(addrBuf)");
    std::memcpy(mapped, data, (size_t)bytes);
    vkUnmapMemory(g.device, *mem);
}

// Resolve the VK_KHR_acceleration_structure entry points and query the scratch
// alignment. Called once after device creation; no-op without RT.
void LoadRtEntryPoints()
{
    if (!g.rtEnabled)
        return;
    g.pfnGetASBuildSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)
        vkGetDeviceProcAddr(g.device, "vkGetAccelerationStructureBuildSizesKHR");
    g.pfnCreateAS = (PFN_vkCreateAccelerationStructureKHR)
        vkGetDeviceProcAddr(g.device, "vkCreateAccelerationStructureKHR");
    g.pfnCmdBuildAS = (PFN_vkCmdBuildAccelerationStructuresKHR)
        vkGetDeviceProcAddr(g.device, "vkCmdBuildAccelerationStructuresKHR");
    g.pfnGetASAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
        vkGetDeviceProcAddr(g.device, "vkGetAccelerationStructureDeviceAddressKHR");
    g.pfnDestroyAS = (PFN_vkDestroyAccelerationStructureKHR)
        vkGetDeviceProcAddr(g.device, "vkDestroyAccelerationStructureKHR");
    g.pfnCmdWriteASProps = (PFN_vkCmdWriteAccelerationStructuresPropertiesKHR)
        vkGetDeviceProcAddr(g.device, "vkCmdWriteAccelerationStructuresPropertiesKHR");
    g.pfnCmdCopyAS = (PFN_vkCmdCopyAccelerationStructureKHR)
        vkGetDeviceProcAddr(g.device, "vkCmdCopyAccelerationStructureKHR");
    if (!g.pfnGetASBuildSizes || !g.pfnCreateAS || !g.pfnCmdBuildAS
        || !g.pfnGetASAddress || !g.pfnDestroyAS
        || !g.pfnCmdWriteASProps || !g.pfnCmdCopyAS)
        I_Error("R_Vulkan: ray-tracing entry points missing despite enabled extensions.");

    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps = {};
    asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 p2 = {};
    p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    p2.pNext = &asProps;
    vkGetPhysicalDeviceProperties2(g.phys, &p2);
    g.scratchAlign = asProps.minAccelerationStructureScratchOffsetAlignment;
}

// Tear down the per-level acceleration structures + their buffers. Safe when none
// exist (level rebuild, shutdown); caller guarantees the GPU is idle.
void DestroyAccelerationStructures()
{
    if (g.tlas)        { g.pfnDestroyAS(g.device, g.tlas, nullptr); g.tlas = VK_NULL_HANDLE; }
    if (g.tlasBuf)     { vkDestroyBuffer(g.device, g.tlasBuf, nullptr); g.tlasBuf = VK_NULL_HANDLE; }
    if (g.tlasMem)     { vkFreeMemory(g.device, g.tlasMem, nullptr); g.tlasMem = VK_NULL_HANDLE; }
    if (g.tlasInstBuf) { vkDestroyBuffer(g.device, g.tlasInstBuf, nullptr); g.tlasInstBuf = VK_NULL_HANDLE; }
    if (g.tlasInstMem) { vkFreeMemory(g.device, g.tlasInstMem, nullptr); g.tlasInstMem = VK_NULL_HANDLE; }
    if (g.blas)        { g.pfnDestroyAS(g.device, g.blas, nullptr); g.blas = VK_NULL_HANDLE; }
    if (g.blasBuf)     { vkDestroyBuffer(g.device, g.blasBuf, nullptr); g.blasBuf = VK_NULL_HANDLE; }
    if (g.blasMem)     { vkFreeMemory(g.device, g.blasMem, nullptr); g.blasMem = VK_NULL_HANDLE; }
    // Moving-sector refit scratch (build step 5).
    if (g.blasUpdScratch)    { vkDestroyBuffer(g.device, g.blasUpdScratch, nullptr); g.blasUpdScratch = VK_NULL_HANDLE; }
    if (g.blasUpdScratchMem) { vkFreeMemory(g.device, g.blasUpdScratchMem, nullptr); g.blasUpdScratchMem = VK_NULL_HANDLE; }
    if (g.tlasUpdScratch)    { vkDestroyBuffer(g.device, g.tlasUpdScratch, nullptr); g.tlasUpdScratch = VK_NULL_HANDLE; }
    if (g.tlasUpdScratchMem) { vkFreeMemory(g.device, g.tlasUpdScratchMem, nullptr); g.tlasUpdScratchMem = VK_NULL_HANDLE; }
    // DOOM-0100 sprite RT resources.
    if (g.spriteBlas)        { g.pfnDestroyAS(g.device, g.spriteBlas, nullptr); g.spriteBlas = VK_NULL_HANDLE; }
    if (g.spriteBlasBuf)     { vkDestroyBuffer(g.device, g.spriteBlasBuf, nullptr); g.spriteBlasBuf = VK_NULL_HANDLE; }
    if (g.spriteBlasMem)     { vkFreeMemory(g.device, g.spriteBlasMem, nullptr); g.spriteBlasMem = VK_NULL_HANDLE; }
    if (g.spriteBlasScratch)    { vkDestroyBuffer(g.device, g.spriteBlasScratch, nullptr); g.spriteBlasScratch = VK_NULL_HANDLE; }
    if (g.spriteBlasScratchMem) { vkFreeMemory(g.device, g.spriteBlasScratchMem, nullptr); g.spriteBlasScratchMem = VK_NULL_HANDLE; }

    // DOOM-0141: sky backdrop BLAS (the vert buffer is freed in BuildProbes, with vbuf).
    if (g.skyBlas)    { g.pfnDestroyAS(g.device, g.skyBlas, nullptr); g.skyBlas = VK_NULL_HANDLE; }
    if (g.skyBlasBuf) { vkDestroyBuffer(g.device, g.skyBlasBuf, nullptr); g.skyBlasBuf = VK_NULL_HANDLE; }
    if (g.skyBlasMem) { vkFreeMemory(g.device, g.skyBlasMem, nullptr); g.skyBlasMem = VK_NULL_HANDLE; }
    g.skyBlasAddr = 0;
    if (g.tlasBuildScratch)    { vkDestroyBuffer(g.device, g.tlasBuildScratch, nullptr); g.tlasBuildScratch = VK_NULL_HANDLE; }
    if (g.tlasBuildScratchMem) { vkFreeMemory(g.device, g.tlasBuildScratchMem, nullptr); g.tlasBuildScratchMem = VK_NULL_HANDLE; }
    if (g.sprWorldBuf)       { vkDestroyBuffer(g.device, g.sprWorldBuf, nullptr); g.sprWorldBuf = VK_NULL_HANDLE; }
    if (g.sprWorldMem)       { vkFreeMemory(g.device, g.sprWorldMem, nullptr); g.sprWorldMem = VK_NULL_HANDLE; }
    g.sprWorldMapped = nullptr; g.tlasInstMapped = nullptr;
    g.spriteBlasAddr = 0; g.sprWorldVertCount = 0;
}

// Build the static BLAS (every level-mesh triangle) and a one-instance identity
// TLAS over it (DOOM-0009 build step 2b). Runs once per level load after the
// vertex buffer is uploaded; the mesh is a non-indexed triangle list with the
// world position at byte offset 0 of rb_vertex_t. The world BLAS is built with
// ALLOW_COMPACTION and then compacted (DOOM-0091): it lives for the whole level, so
// reclaiming its worst-case storage is a 20-50% VRAM win that scales with large WADs.
// ALLOW_UPDATE survives compaction, so moving-sector refits still run on the
// compacted AS. The per-frame sprite BLAS is left non-compacted (a compacted-size
// query round-trip would stall the frame).
void BuildAccelerationStructures()
{
    DestroyAccelerationStructures();
    if (!g.vertexCount)
        return;

    const uint32_t triCount = g.vertexCount / 3;

    // ---- BLAS: the level triangles ----
    VkAccelerationStructureGeometryKHR geom = {};
    geom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    // DOOM-0163: NON-opaque so the primary ray alpha-tests two-sided masked mid-walls
    // (grates/fences) against palette index 0 in the trace candidate loop -- see-through
    // in the ray-traced view like the raster path (mesh.frag FLAG_MASKED). Opaque
    // walls/flats confirm on first candidate (one flag read), so they keep vanilla
    // occlusion. Shadow/NEE/muzzle/flashlight/GI-bake rays all force gl_RayFlagsOpaqueEXT,
    // so this only affects the (single, coherent) primary ray -- masked walls still cast
    // solid shadows for now (patterned shadows deferred, cf. DOOM-0108 for sprites).
    geom.flags        = 0;
    geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = BufferAddress(g.vbuf);
    geom.geometry.triangles.vertexStride = sizeof(rb_vertex_t);
    geom.geometry.triangles.maxVertex    = g.vertexCount - 1;
    geom.geometry.triangles.indexType    = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR bgi = {};
    bgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    // ALLOW_UPDATE so a moving door/lift can refit the BLAS in place each frame
    // (build step 5) instead of leaving traced geometry stale; also makes the size
    // query fill updateScratchSize.
    bgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                      | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
                      | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;  // DOOM-0091
    bgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    bgi.geometryCount = 1;
    bgi.pGeometries   = &geom;

    VkAccelerationStructureBuildSizesInfoKHR sizes = {};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g.pfnGetASBuildSizes(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &bgi, &triCount, &sizes);

    CreateRtBuffer(sizes.accelerationStructureSize,
                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &g.blasBuf, &g.blasMem);

    VkAccelerationStructureCreateInfoKHR asci = {};
    asci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asci.buffer = g.blasBuf;
    asci.size   = sizes.accelerationStructureSize;
    asci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    Check(g.pfnCreateAS(g.device, &asci, nullptr, &g.blas), "vkCreateAccelerationStructureKHR(blas)");

    VkBuffer scratchBuf = VK_NULL_HANDLE; VkDeviceMemory scratchMem = VK_NULL_HANDLE;
    CreateRtBuffer(sizes.buildScratchSize + g.scratchAlign,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &scratchBuf, &scratchMem);

    bgi.dstAccelerationStructure  = g.blas;
    bgi.scratchData.deviceAddress = AlignUp(BufferAddress(scratchBuf), g.scratchAlign);

    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount = triCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    // DOOM-0091: build the BLAS and, in the same submit, query the size it would
    // compact to. A barrier orders the build write before the property read.
    VkQueryPool compactQp = VK_NULL_HANDLE;
    VkQueryPoolCreateInfo qpci = {};
    qpci.sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
    qpci.queryCount = 1;
    Check(vkCreateQueryPool(g.device, &qpci, nullptr, &compactQp), "vkCreateQueryPool(blasCompact)");

    VkCommandBuffer cb = BeginOneTime();
    vkCmdResetQueryPool(cb, compactQp, 0, 1);
    g.pfnCmdBuildAS(cb, 1, &bgi, &pRange);
    VkMemoryBarrier asWrite = {};
    asWrite.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    asWrite.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    asWrite.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0,
                         1, &asWrite, 0, nullptr, 0, nullptr);
    g.pfnCmdWriteASProps(cb, 1, &g.blas,
                         VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR, compactQp, 0);
    EndOneTime(cb);   // submits + waits: build + size query complete on return

    vkDestroyBuffer(g.device, scratchBuf, nullptr);
    vkFreeMemory(g.device, scratchMem, nullptr);

    // Compact into a right-sized AS and swap it in for the worst-case one. The
    // compacted size is guaranteed <= the build size; a 0 result (driver quirk) or a
    // non-shrink falls back to keeping the original.
    VkDeviceSize blasSize = sizes.accelerationStructureSize;   // reported below
    VkDeviceSize compactSize = 0;
    Check(vkGetQueryPoolResults(g.device, compactQp, 0, 1, sizeof(compactSize), &compactSize,
                                sizeof(compactSize), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
          "vkGetQueryPoolResults(blasCompact)");
    vkDestroyQueryPool(g.device, compactQp, nullptr);

    if (compactSize > 0 && compactSize < sizes.accelerationStructureSize)
    {
        VkBuffer cBuf = VK_NULL_HANDLE; VkDeviceMemory cMem = VK_NULL_HANDLE;
        CreateRtBuffer(compactSize,
                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                       | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &cBuf, &cMem);
        VkAccelerationStructureCreateInfoKHR cci = {};
        cci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        cci.buffer = cBuf;
        cci.size   = compactSize;
        cci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        VkAccelerationStructureKHR cAS = VK_NULL_HANDLE;
        Check(g.pfnCreateAS(g.device, &cci, nullptr, &cAS), "vkCreateAccelerationStructureKHR(blasCompact)");

        VkCopyAccelerationStructureInfoKHR copy = {};
        copy.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
        copy.src   = g.blas;
        copy.dst   = cAS;
        copy.mode  = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
        cb = BeginOneTime();
        g.pfnCmdCopyAS(cb, &copy);
        EndOneTime(cb);

        g.pfnDestroyAS(g.device, g.blas, nullptr);
        vkDestroyBuffer(g.device, g.blasBuf, nullptr);
        vkFreeMemory(g.device, g.blasMem, nullptr);
        g.blas = cAS; g.blasBuf = cBuf; g.blasMem = cMem;
        blasSize = compactSize;
    }

    // Persistent scratch for in-place BLAS refits (build step 5); sized by the
    // update query above. Kept for the level's lifetime so a refit allocates nothing.
    CreateRtBuffer(sizes.updateScratchSize + g.scratchAlign,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &g.blasUpdScratch, &g.blasUpdScratchMem);

    // ---- Sprite BLAS storage (DOOM-0100): a throwaway triangle-soup BLAS over
    // this frame's billboards, rebuilt each frame in RecordRtTrace. Allocate it
    // (and a persistent build scratch + the host-visible vert buffer) once here,
    // sized to the sprite cap; the per-frame rebuild reuses the storage. NON-opaque
    // geometry so palette-0 alpha-tests in the trace candidate loop. ----
    g.sprWorldVertCap = 4096u * 6u;        // up to ~4096 things/frame, 6 verts each
    g.sprBlasMaxTris  = g.sprWorldVertCap / 3u;
    CreateRtBuffer((VkDeviceSize)g.sprWorldVertCap * sizeof(rb_vertex_t),
                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.sprWorldBuf, &g.sprWorldMem);
    Check(vkMapMemory(g.device, g.sprWorldMem, 0, VK_WHOLE_SIZE, 0, &g.sprWorldMapped),
          "vkMapMemory(sprWorld)");

    VkAccelerationStructureGeometryKHR sgeom = {};
    sgeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    sgeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    sgeom.flags        = 0;   // NON-opaque: alpha-tested in the trace candidate loop
    sgeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    sgeom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    sgeom.geometry.triangles.vertexData.deviceAddress = BufferAddress(g.sprWorldBuf);
    sgeom.geometry.triangles.vertexStride = sizeof(rb_vertex_t);
    sgeom.geometry.triangles.maxVertex    = g.sprWorldVertCap - 1;
    sgeom.geometry.triangles.indexType    = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR sbgi = {};
    sbgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    sbgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    sbgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;  // rebuilt every frame
    sbgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    sbgi.geometryCount = 1;
    sbgi.pGeometries   = &sgeom;

    VkAccelerationStructureBuildSizesInfoKHR ssizes = {};
    ssizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g.pfnGetASBuildSizes(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &sbgi, &g.sprBlasMaxTris, &ssizes);

    CreateRtBuffer(ssizes.accelerationStructureSize,
                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &g.spriteBlasBuf, &g.spriteBlasMem);
    VkAccelerationStructureCreateInfoKHR sasci = {};
    sasci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    sasci.buffer = g.spriteBlasBuf;
    sasci.size   = ssizes.accelerationStructureSize;
    sasci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    Check(g.pfnCreateAS(g.device, &sasci, nullptr, &g.spriteBlas), "vkCreateAccelerationStructureKHR(spriteBlas)");
    CreateRtBuffer(ssizes.buildScratchSize + g.scratchAlign,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &g.spriteBlasScratch, &g.spriteBlasScratchMem);

    VkAccelerationStructureDeviceAddressInfoKHR sadi = {};
    sadi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    sadi.accelerationStructure = g.spriteBlas;
    g.spriteBlasAddr = g.pfnGetASAddress(g.device, &sadi);

    // ---- Sky BLAS (DOOM-0141): a STATIC, opaque backdrop BLAS over this level's sky
    // ceilings/borders (g.skyMeshBuf, built in BuildProbes). It rides TLAS instance 1 on
    // mask 0x04 (primary rays only) so it occludes the view like classic DOOM's sky
    // while shadow rays + the GI bake (mask 0x01) never hit it. Sky planes don't move,
    // so it's built once here — no per-frame rebuild, no refit. Skipped if the level
    // has no sky surfaces (fully enclosed map). ----
    if (g.skyMeshVerts >= 3 && g.skyMeshBuf != VK_NULL_HANDLE)
    {
        const uint32_t skyTris = g.skyMeshVerts / 3u;
        VkAccelerationStructureGeometryKHR kgeom = {};
        kgeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        kgeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        kgeom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;   // solid backdrop: auto-commits
        kgeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        kgeom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        kgeom.geometry.triangles.vertexData.deviceAddress = BufferAddress(g.skyMeshBuf);
        kgeom.geometry.triangles.vertexStride = sizeof(rb_vertex_t);
        kgeom.geometry.triangles.maxVertex    = g.skyMeshVerts - 1;
        kgeom.geometry.triangles.indexType    = VK_INDEX_TYPE_NONE_KHR;

        VkAccelerationStructureBuildGeometryInfoKHR kbgi = {};
        kbgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        kbgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        kbgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        kbgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        kbgi.geometryCount = 1;
        kbgi.pGeometries   = &kgeom;

        VkAccelerationStructureBuildSizesInfoKHR ksizes = {};
        ksizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        g.pfnGetASBuildSizes(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                             &kbgi, &skyTris, &ksizes);

        CreateRtBuffer(ksizes.accelerationStructureSize,
                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                       | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &g.skyBlasBuf, &g.skyBlasMem);
        VkAccelerationStructureCreateInfoKHR kci = {};
        kci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        kci.buffer = g.skyBlasBuf;
        kci.size   = ksizes.accelerationStructureSize;
        kci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        Check(g.pfnCreateAS(g.device, &kci, nullptr, &g.skyBlas), "vkCreateAccelerationStructureKHR(skyBlas)");

        VkBuffer kScratch = VK_NULL_HANDLE; VkDeviceMemory kScratchMem = VK_NULL_HANDLE;
        CreateRtBuffer(ksizes.buildScratchSize + g.scratchAlign,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &kScratch, &kScratchMem);
        kbgi.dstAccelerationStructure  = g.skyBlas;
        kbgi.scratchData.deviceAddress = AlignUp(BufferAddress(kScratch), g.scratchAlign);
        VkAccelerationStructureBuildRangeInfoKHR krange = {};
        krange.primitiveCount = skyTris;
        const VkAccelerationStructureBuildRangeInfoKHR* pKr = &krange;
        VkCommandBuffer kcb = BeginOneTime();
        g.pfnCmdBuildAS(kcb, 1, &kbgi, &pKr);
        EndOneTime(kcb);                 // submits + waits: the static sky BLAS is ready
        vkDestroyBuffer(g.device, kScratch, nullptr);
        vkFreeMemory(g.device, kScratchMem, nullptr);

        VkAccelerationStructureDeviceAddressInfoKHR kadi = {};
        kadi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        kadi.accelerationStructure = g.skyBlas;
        g.skyBlasAddr = g.pfnGetASAddress(g.device, &kadi);
    }
    const bool skyPresent = (g.skyBlas != VK_NULL_HANDLE);

    // ---- TLAS: instance 0 = the static world BLAS; instance 1 = the static sky BLAS
    // (DOOM-0141, mask 0x04, present only when the level has sky); the per-frame sprite
    // BLAS (mask 0x02) is appended each frame in BuildSpriteTlas at the slot after the
    // static instances. Sized for 3, built here with the static instance(s), then
    // rebuilt every traced frame. ----
    static const uint32_t kMaxTlasInstances = 3;
    VkAccelerationStructureDeviceAddressInfoKHR adi = {};
    adi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    adi.accelerationStructure = g.blas;
    const VkDeviceAddress blasAddr = g.pfnGetASAddress(g.device, &adi);

    CreateRtBuffer(sizeof(VkAccelerationStructureInstanceKHR) * kMaxTlasInstances,
                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.tlasInstBuf, &g.tlasInstMem);
    Check(vkMapMemory(g.device, g.tlasInstMem, 0, VK_WHOLE_SIZE, 0, &g.tlasInstMapped), "vkMapMemory(tlasInst)");
    VkAccelerationStructureInstanceKHR* insts = (VkAccelerationStructureInstanceKHR*)g.tlasInstMapped;
    std::memset(insts, 0, sizeof(VkAccelerationStructureInstanceKHR) * kMaxTlasInstances);
    insts[0].transform.matrix[0][0] = 1.0f;   // world: identity 3x4 row-major
    insts[0].transform.matrix[1][1] = 1.0f;
    insts[0].transform.matrix[2][2] = 1.0f;
    insts[0].mask  = 0x01;                     // world: primary + shadow/NEE rays
    insts[0].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    insts[0].accelerationStructureReference = blasAddr;
    // DOOM-0141: instance 1 = the static sky backdrop (mask 0x04 -> primary rays only;
    // custom index 2 -> the megakernel shades it as sky). Set once; never per-frame.
    if (skyPresent)
    {
        insts[1].transform.matrix[0][0] = 1.0f;
        insts[1].transform.matrix[1][1] = 1.0f;
        insts[1].transform.matrix[2][2] = 1.0f;
        insts[1].instanceCustomIndex = 2u;
        insts[1].mask  = 0x04;
        insts[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        insts[1].accelerationStructureReference = g.skyBlasAddr;
    }
    // The sprite slot (1 when no sky, else 2) is left zeroed until a frame fills it.

    VkAccelerationStructureGeometryKHR tgeom = {};
    tgeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tgeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tgeom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;
    tgeom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tgeom.geometry.instances.arrayOfPointers    = VK_FALSE;
    tgeom.geometry.instances.data.deviceAddress = BufferAddress(g.tlasInstBuf);

    VkAccelerationStructureBuildGeometryInfoKHR tbgi = {};
    tbgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tbgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    // No ALLOW_UPDATE: the TLAS is fully rebuilt each traced frame (cheap for 2
    // instances) so the sprite instance's BLAS extents are always current.
    tbgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tbgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tbgi.geometryCount = 1;
    tbgi.pGeometries   = &tgeom;

    VkAccelerationStructureBuildSizesInfoKHR tsizes = {};
    tsizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g.pfnGetASBuildSizes(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &tbgi, &kMaxTlasInstances, &tsizes);

    CreateRtBuffer(tsizes.accelerationStructureSize,
                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &g.tlasBuf, &g.tlasMem);

    VkAccelerationStructureCreateInfoKHR tasci = {};
    tasci.sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tasci.buffer = g.tlasBuf;
    tasci.size   = tsizes.accelerationStructureSize;
    tasci.type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    Check(g.pfnCreateAS(g.device, &tasci, nullptr, &g.tlas), "vkCreateAccelerationStructureKHR(tlas)");

    // Persistent TLAS build scratch (sized for the max instance count), reused by
    // the per-frame rebuild so a traced frame allocates nothing.
    CreateRtBuffer(tsizes.buildScratchSize + g.scratchAlign,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &g.tlasBuildScratch, &g.tlasBuildScratchMem);

    tbgi.dstAccelerationStructure  = g.tlas;
    tbgi.scratchData.deviceAddress = AlignUp(BufferAddress(g.tlasBuildScratch), g.scratchAlign);

    VkAccelerationStructureBuildRangeInfoKHR trange = {};
    trange.primitiveCount = skyPresent ? 2u : 1u;   // static world [+ sky] at level-load
    const VkAccelerationStructureBuildRangeInfoKHR* pTrange = &trange;

    cb = BeginOneTime();
    g.pfnCmdBuildAS(cb, 1, &tbgi, &pTrange);
    EndOneTime(cb);
    g.blasDirty = false;        // freshly built from the current heights

    printf("RB_Vulkan: built BLAS (%u tris, %.1f->%.1f KiB compacted) + sky BLAS (%u tris) + TLAS (3-instance cap, world%s live); AS %.1f KiB.\n",
           triCount,
           (double)sizes.accelerationStructureSize / 1024.0,
           (double)blasSize / 1024.0,
           g.skyMeshVerts / 3u,
           skyPresent ? "+sky" : "",
           (double)(blasSize + tsizes.accelerationStructureSize
                    + ssizes.accelerationStructureSize) / 1024.0);
    fflush(stdout);

    // The path-tracer compute descriptor binds this TLAS; re-point it now that
    // the per-level TLAS exists (the storage image half is written at init /
    // swapchain rebuild). No-op until the compute pipeline is up.
    UpdateRtComputeDescriptor();
}

// Refit the world BLAS in place from the just-patched vertex buffer (DOOM-0009
// build step 5). Called when RB_UpdateMeshHeights reports a moving door/lift, so
// the traced geometry (and its shadows) track the live world instead of the
// build-time snapshot. An in-place UPDATE re-reads the same vertex buffer and reuses
// the existing AS storage — far cheaper than a rebuild, and the persistent update
// scratch means it allocates nothing. The whole-BLAS refit is the coarse option from
// the spec's step-5 open question; on DOOM's ~2k-tri meshes it measures well under
// budget (printed once), so splitting rigid caps into separate TLAS instances is
// unnecessary. The TLAS itself is rebuilt unconditionally each traced frame (for the
// sprite instance, DOOM-0100), so it picks up the refit BLAS extents there — no TLAS
// refit needed here. Recorded as a one-time submit before the frame's command buffer;
// the caller guarantees the previous frame finished.
// DOOM-0131: record the moving-sector world-BLAS in-place update into a caller-
// supplied command buffer. Previously this owned its own BeginOneTime/EndOneTime
// submit (EndOneTime waits the queue idle), which stalled the GPU mid-frame on
// every door/lift frame. The caller now folds it into the frame command buffer
// (g.cmd) ahead of the TLAS rebuild — see RecordRtTrace — so it pipelines with
// the rest of the frame instead of bubbling.
void RecordRefitAS(VkCommandBuffer cb)
{
    if (g.blas == VK_NULL_HANDLE || g.tlas == VK_NULL_HANDLE || !g.vertexCount)
        return;

    const uint32_t triCount = g.vertexCount / 3;

    // BLAS update: same geometry description as the build, mode UPDATE, src == dst.
    VkAccelerationStructureGeometryKHR geom = {};
    geom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags        = 0;   // DOOM-0163: NON-opaque, must match the build (masked mid-wall alpha test)
    geom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    geom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    geom.geometry.triangles.vertexData.deviceAddress = BufferAddress(g.vbuf);
    geom.geometry.triangles.vertexStride = sizeof(rb_vertex_t);
    geom.geometry.triangles.maxVertex    = g.vertexCount - 1;
    geom.geometry.triangles.indexType    = VK_INDEX_TYPE_NONE_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR bgi = {};
    bgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    bgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    bgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR
                      | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    bgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    bgi.srcAccelerationStructure = g.blas;
    bgi.dstAccelerationStructure = g.blas;
    bgi.geometryCount = 1;
    bgi.pGeometries   = &geom;
    bgi.scratchData.deviceAddress = AlignUp(BufferAddress(g.blasUpdScratch), g.scratchAlign);

    VkAccelerationStructureBuildRangeInfoKHR range = {};
    range.primitiveCount = triCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;

    g.pfnCmdBuildAS(cb, 1, &bgi, &pRange);

    if (!g.refitTimed)          // resolve the spec's step-5 cost gate, once
    {
        g.refitTimed = true;
        printf("RB_Vulkan: moving-sector world-BLAS refit active (%u tris, in-place update).\n",
               triCount);
        fflush(stdout);
    }
}

// DOOM-0100: record this frame's sprite BLAS rebuild + TLAS rebuild into g.cmd,
// before the trace dispatch. The host has already filled g.sprWorldBuf (the
// billboards) and set g.sprWorldVertCount this frame. When sprites are present we
// (re)build the throwaway sprite BLAS over them and add it to the TLAS as instance
// 1 (mask 0x02); otherwise the TLAS is rebuilt with the world instance only. A
// barrier orders each AS write before its reader (TLAS reads the BLAS extents; the
// compute trace reads the TLAS). Returns the instance count built.
uint32_t BuildSpriteTlas()
{
    const uint32_t sprTris = g.sprWorldVertCount / 3u;
    const bool haveSpr = sprTris > 0u && g.spriteBlas != VK_NULL_HANDLE
                       && g.tlasBuildScratch != VK_NULL_HANDLE;

    auto asBarrier = [&]() {
        VkMemoryBarrier mb = {};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
                         | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0,
                             1, &mb, 0, nullptr, 0, nullptr);
    };

    VkAccelerationStructureInstanceKHR* insts =
        (VkAccelerationStructureInstanceKHR*)g.tlasInstMapped;

    // DOOM-0141: static instances occupy the low slots — 0 = world, 1 = sky (when the
    // level has sky). The per-frame sprite instance lands right after them. The static
    // slots are filled once in BuildAccelerationStructures and persist in the mapped
    // buffer, so the rebuild here only touches the sprite slot.
    const uint32_t base = (g.skyBlas != VK_NULL_HANDLE) ? 2u : 1u;

    if (haveSpr)
    {
        // Sprite BLAS rebuild over this frame's billboards (non-opaque triangles).
        VkAccelerationStructureGeometryKHR sgeom = {};
        sgeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        sgeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        sgeom.flags        = 0;   // NON-opaque: palette-0 alpha-tested in the trace
        sgeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        sgeom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        sgeom.geometry.triangles.vertexData.deviceAddress = BufferAddress(g.sprWorldBuf);
        sgeom.geometry.triangles.vertexStride = sizeof(rb_vertex_t);
        sgeom.geometry.triangles.maxVertex    = g.sprWorldVertCount - 1;
        sgeom.geometry.triangles.indexType    = VK_INDEX_TYPE_NONE_KHR;

        VkAccelerationStructureBuildGeometryInfoKHR sbgi = {};
        sbgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        sbgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        sbgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
        sbgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        sbgi.geometryCount = 1;
        sbgi.pGeometries   = &sgeom;
        sbgi.dstAccelerationStructure  = g.spriteBlas;
        sbgi.scratchData.deviceAddress = AlignUp(BufferAddress(g.spriteBlasScratch), g.scratchAlign);

        VkAccelerationStructureBuildRangeInfoKHR srange = {};
        srange.primitiveCount = sprTris;
        const VkAccelerationStructureBuildRangeInfoKHR* pSr = &srange;
        g.pfnCmdBuildAS(g.cmd, 1, &sbgi, &pSr);
        asBarrier();   // TLAS rebuild below reads the sprite BLAS extents

        insts[base].transform.matrix[0][0] = 1.0f;
        insts[base].transform.matrix[1][1] = 1.0f;
        insts[base].transform.matrix[2][2] = 1.0f;
        insts[base].instanceCustomIndex = 1u;      // megakernel: "this hit is a sprite"
        insts[base].mask  = 0x02;                   // primary rays only (shadow/NEE cull to 0x01)
        insts[base].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        insts[base].accelerationStructureReference = g.spriteBlasAddr;
    }
    else
    {
        insts[base].mask = 0u;                      // no sprites this frame
    }

    const uint32_t instCount = haveSpr ? (base + 1u) : base;

    VkAccelerationStructureGeometryKHR tgeom = {};
    tgeom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tgeom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tgeom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;
    tgeom.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tgeom.geometry.instances.arrayOfPointers    = VK_FALSE;
    tgeom.geometry.instances.data.deviceAddress = BufferAddress(g.tlasInstBuf);

    VkAccelerationStructureBuildGeometryInfoKHR tbgi = {};
    tbgi.sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tbgi.type          = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tbgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tbgi.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tbgi.geometryCount = 1;
    tbgi.pGeometries   = &tgeom;
    tbgi.dstAccelerationStructure  = g.tlas;
    tbgi.scratchData.deviceAddress = AlignUp(BufferAddress(g.tlasBuildScratch), g.scratchAlign);

    VkAccelerationStructureBuildRangeInfoKHR trange = {};
    trange.primitiveCount = instCount;
    const VkAccelerationStructureBuildRangeInfoKHR* pTr = &trange;
    g.pfnCmdBuildAS(g.cmd, 1, &tbgi, &pTr);

    // The compute trace reads the TLAS; order the build before it.
    VkMemoryBarrier toTrace = {};
    toTrace.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    toTrace.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    toTrace.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                         1, &toTrace, 0, nullptr, 0, nullptr);
    return instCount;
}

// ---------------------------------------------------------------------------
// Path-tracer compute pass (DOOM-0009 build step 2c)
// ---------------------------------------------------------------------------

// DOOM-0129: build (and cache) the megakernel pipeline variant for one view-mode.
// `mode` is pathtrace.comp's spec-constant 0, so the driver folds the other modes'
// branches to `if (false)` and dead-strips them — fewer VGPRs, higher RDNA2
// occupancy, pixel-identical output. Variants are built on first use; mode 6 (the
// Ultra play path) is pre-built in CreateRtComputePipeline so the first traced
// frame has no compile hitch. Slot index == mode value (1..6).
static VkPipeline RtPipelineForMode(uint32_t mode)
{
    if (mode >= 7u) mode = 6u;                 // clamp to the play path defensively
    if (g.rtPipeline[mode] != VK_NULL_HANDLE)
        return g.rtPipeline[mode];

    const uint32_t modeVal = mode;
    VkSpecializationMapEntry me = { 0u, 0u, sizeof(uint32_t) }; // constant_id 0
    VkSpecializationInfo     si = {};
    si.mapEntryCount = 1;
    si.pMapEntries   = &me;
    si.dataSize      = sizeof(modeVal);
    si.pData         = &modeVal;

    VkComputePipelineCreateInfo cpci = {};
    cpci.sType                     = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module              = g.rtModule;
    cpci.stage.pName               = "main";
    cpci.stage.pSpecializationInfo = &si;
    cpci.layout                    = g.rtPipeLayout;
    Check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr,
                                   &g.rtPipeline[mode]),
          "vkCreateComputePipelines(rt)");
    return g.rtPipeline[mode];
}

static void CreateHdSetLayout();   // DOOM-0042: set-3 layout, needed by the RT pipeline layout
static void InitHdDefault();       // DOOM-0042: seed an all-paletted set 3 at atlas time

// Build the once-per-run compute pipeline: a descriptor set (TLAS + storage
// image), an 88-byte push-constant range (camera basis + mode + vertex-buffer
// address), and the pathtrace.comp megakernel. RT-only; never called without it.
void CreateRtComputePipeline()
{
    VkDescriptorSetLayoutBinding binds[3] = {};
    binds[0].binding         = 0;   // TLAS
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding         = 1;   // output storage image (display modes 1-4)
    binds[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[2].binding         = 2;   // rgba32f verify accumulator (mode 5; step 4d)
    binds[2].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[2].descriptorCount = 1;
    binds[2].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 3;
    dlci.pBindings    = binds;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.rtDsLayout),
          "vkCreateDescriptorSetLayout(rt)");

    VkDescriptorPoolSize pools[2] = {};
    pools[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; pools[0].descriptorCount = 1;
    pools[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;              pools[1].descriptorCount = 2;
    VkDescriptorPoolCreateInfo pci = {};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets       = 1;
    pci.poolSizeCount = 2;
    pci.pPoolSizes    = pools;
    Check(vkCreateDescriptorPool(g.device, &pci, nullptr, &g.rtDsPool),
          "vkCreateDescriptorPool(rt)");

    VkDescriptorSetAllocateInfo dai = {};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = g.rtDsPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &g.rtDsLayout;
    Check(vkAllocateDescriptorSets(g.device, &dai, &g.rtDs), "vkAllocateDescriptorSets(rt)");

    // Push constant: 4x vec4 (camera) + 5x uvec4 (mode/w/h/numWall, emitter+probe
    // counts, verify seed/spp/estimator, DOOM-0100 sprite base + omni-emitter start +
    // DOOM-0119 REJECT sector count + DOOM-0141 sky id, DOOM-0179 grime-overlay id) +
    // 9x uint64 (vertex / emitter / Le / probe-cache / tri-subsector / sprite-vert +
    // DOOM-0119 subsector-sector / emitter-sector / reject-matrix addresses) = 216 bytes,
    // + DOOM-0183 misc6 (8-byte std430 pad + uvec4) = 240 bytes. MUST match
    // sizeof(RtPushConstants) in RecordRtTrace — a short range silently drops the trailing
    // fields, so the verify struct's 184-byte push (RB_RtVerify, which mirrors misc4/misc5
    // as padding) is a valid PREFIX of this 240-byte range. Within the 256-byte device limit.
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 240;
    // Three sets: 0 = RT (TLAS + output image), 1 = the raster materials set
    // (g.dsLayout: PLAYPAL LUT + bindless material array), reused verbatim so the
    // textured trace (step 3a) decodes surfaces with no parallel material path,
    // 2 = the SVGF denoiser G-buffer (step 6; mode 6 writes its feed half). The
    // megakernel statically references set 2 (mode 6), so EVERY dispatch of this
    // pipeline must bind all three sets — RecordRtTrace + RB_RtVerify both do.
    // g.dsLayout + g.svgfDsLayout are created before this (Init order).
    // DOOM-0042: set 3 = the HD PBR material set (control SSBO + bindless RGBA8 array).
    // Added to the layout here; the per-frame bind of set 3 + the shader that samples
    // it land together in Task 10. A 4-set layout with only 3 bound is valid (set 3 is
    // unreferenced by the shader until then), so the existing RT path is unaffected.
    CreateHdSetLayout();
    VkDescriptorSetLayout setLayouts[4] = { g.rtDsLayout, g.dsLayout, g.svgfDsLayout, g.hdSetLayout };
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 4;
    plci.pSetLayouts            = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.rtPipeLayout),
          "vkCreatePipelineLayout(rt)");

    // DOOM-0129: keep the module alive — each view-mode is compiled into its own
    // specialised pipeline (RtPipelineForMode), built lazily from this module. Pre-
    // build mode 6 (the default Ultra play path) so the first traced frame has no
    // pipeline-compile hitch; the diagnostic modes (1-5) build on first toggle.
    g.rtModule = MakeShader(pathtrace_comp_spv, pathtrace_comp_spv_len);
    RtPipelineForMode(6u);

    // INV-6 verify accumulator (build step 4d): a fixed-size rgba32f storage image
    // (compute binding 2) + a host-visible readback buffer. Created once; mode 5
    // (RB_RtVerify) sums radiance into it and copies it here for the CPU rel-MSE
    // check. binding 2 is statically referenced by the shader, so it must hold a
    // valid view even for the display dispatches that never touch it — written now.
    VkImageCreateInfo aci = {};
    aci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    aci.imageType     = VK_IMAGE_TYPE_2D;
    aci.format        = VK_FORMAT_R32G32B32A32_SFLOAT;
    aci.extent        = { kVerifyW, kVerifyH, 1 };
    aci.mipLevels     = 1;
    aci.arrayLayers   = 1;
    aci.samples       = VK_SAMPLE_COUNT_1_BIT;
    aci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    aci.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                      | VK_IMAGE_USAGE_TRANSFER_DST_BIT;   // DST for the clear
    aci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    aci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(g.device, &aci, nullptr, &g.rtAccum), "vkCreateImage(accum)");

    VkMemoryRequirements areq = {};
    vkGetImageMemoryRequirements(g.device, g.rtAccum, &areq);
    VkMemoryAllocateInfo amai = {};
    amai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    amai.allocationSize  = areq.size;
    amai.memoryTypeIndex = FindMemoryType(areq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &amai, nullptr, &g.rtAccumMem), "vkAllocateMemory(accum)");
    Check(vkBindImageMemory(g.device, g.rtAccum, g.rtAccumMem, 0), "vkBindImageMemory(accum)");

    VkImageViewCreateInfo avci = {};
    avci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    avci.image            = g.rtAccum;
    avci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    avci.format           = VK_FORMAT_R32G32B32A32_SFLOAT;
    avci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    Check(vkCreateImageView(g.device, &avci, nullptr, &g.rtAccumView), "vkCreateImageView(accum)");

    // Park it in GENERAL once. The descriptor (binding 2) advertises GENERAL and the
    // shader statically references it, so even the display dispatches that never read
    // it expect the layout to match — transition now, then it stays GENERAL for life.
    {
        VkCommandBuffer cb = BeginOneTime();
        VkImageMemoryBarrier toGen = {};
        toGen.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGen.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        toGen.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        toGen.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGen.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGen.image            = g.rtAccum;
        toGen.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toGen.srcAccessMask    = 0;
        toGen.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGen);
        EndOneTime(cb);
    }

    CreateRtBuffer((VkDeviceSize)kVerifyW * kVerifyH * 4 * sizeof(float),
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.rtReadback, &g.rtReadbackMem);

    VkDescriptorImageInfo accInfo = {};
    accInfo.imageView   = g.rtAccumView;
    accInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkWriteDescriptorSet accWrite = {};
    accWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    accWrite.dstSet          = g.rtDs;
    accWrite.dstBinding      = 2;
    accWrite.descriptorCount = 1;
    accWrite.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    accWrite.pImageInfo      = &accInfo;
    vkUpdateDescriptorSets(g.device, 1, &accWrite, 0, nullptr);
}

// Build the once-per-run GI bake pipeline (DOOM-0009 build step 4b-ii). Like the
// megakernel but its set 0 holds the TLAS alone (the bake writes the probe SSBO by
// address, not an image); set 1 reuses the raster materials set. The dispatch runs
// at each level load (RunGiBake), so the descriptor's TLAS half is (re)written
// there. RT-only; CreateRtComputePipeline (which makes g.dsLayout's sibling) and
// CreateDescriptors run before this.
void CreateBakePipeline()
{
    VkDescriptorSetLayoutBinding tlasBind = {};
    tlasBind.binding         = 0;   // TLAS
    tlasBind.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    tlasBind.descriptorCount = 1;
    tlasBind.stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 1;
    dlci.pBindings    = &tlasBind;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.bakeDsLayout),
          "vkCreateDescriptorSetLayout(bake)");

    VkDescriptorPoolSize pool = {};
    pool.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; pool.descriptorCount = 1;
    VkDescriptorPoolCreateInfo pci = {};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets       = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes    = &pool;
    Check(vkCreateDescriptorPool(g.device, &pci, nullptr, &g.bakeDsPool),
          "vkCreateDescriptorPool(bake)");

    VkDescriptorSetAllocateInfo dai = {};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = g.bakeDsPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &g.bakeDsLayout;
    Check(vkAllocateDescriptorSets(g.device, &dai, &g.bakeDs), "vkAllocateDescriptorSets(bake)");

    // Push constant: uvec4 (probeCount/numWall/emitterCount/giEnabled) + 8 uint64
    // buffer addresses (verts, emit, matEmis, probes, prevProbes, triSs, + DOOM-0119
    // emitSec/reject — always 0 here, but the shared shadeSurface references them so
    // the range must cover them) = 80 bytes. MUST match sizeof(BakePush) below.
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 80;
    VkDescriptorSetLayout setLayouts[2] = { g.bakeDsLayout, g.dsLayout };
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 2;
    plci.pSetLayouts            = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.bakePipeLayout),
          "vkCreatePipelineLayout(bake)");

    VkShaderModule cs = MakeShader(bake_comp_spv, bake_comp_spv_len);
    VkComputePipelineCreateInfo cpci = {};
    cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = cs;
    cpci.stage.pName  = "main";
    cpci.layout       = g.bakePipeLayout;
    Check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g.bakePipeline),
          "vkCreateComputePipelines(bake)");
    vkDestroyShaderModule(g.device, cs, nullptr);
}

// (Re)create the swapchain-sized storage image the compute pass writes and the
// blit reads. R8G8B8A8_UNORM so vkCmdBlitImage matches components by name into
// the B8G8R8A8 swapchain with no red/blue swap. STORAGE (compute) + TRANSFER_SRC
// (blit). RT-only.
// SVGF descriptor-set layout (DOOM-0009 build step 6). Created BEFORE the
// path-trace pipeline (whose layout references it as set 2). One set shared by the
// megakernel's mode-6 feed and all three denoiser passes: eight storage-image
// bindings, the ping-pong targets as 2-element arrays the shaders index by parity.
void CreateSvgfDescriptorLayout()
{
    // gpos,gnorm,albedo,illum,hcol,hmom,atrous,out,motion (binding 8 = 6-d MV).
    const uint32_t counts[9] = { 2, 2, 1, 1, 2, 2, 2, 1, 1 };
    VkDescriptorSetLayoutBinding b[9] = {};
    uint32_t total = 0;
    for (uint32_t i = 0; i < 9; i++) {
        b[i].binding         = i;
        b[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = counts[i];
        b[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        total += counts[i];
    }
    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 9;
    dlci.pBindings    = b;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.svgfDsLayout),
          "vkCreateDescriptorSetLayout(svgf)");

    // Two sets from this layout: svgfDs (the denoiser chain) + labelTaauDs (the
    // label-on-upscaled-output variant, binding 7 retargeted in WriteTaauDescriptor).
    VkDescriptorPoolSize pool = {};
    pool.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool.descriptorCount = total * 2;
    VkDescriptorPoolCreateInfo pci = {};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets       = 2;
    pci.poolSizeCount = 1;
    pci.pPoolSizes    = &pool;
    Check(vkCreateDescriptorPool(g.device, &pci, nullptr, &g.svgfDsPool),
          "vkCreateDescriptorPool(svgf)");

    VkDescriptorSetAllocateInfo dai = {};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = g.svgfDsPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &g.svgfDsLayout;
    Check(vkAllocateDescriptorSets(g.device, &dai, &g.svgfDs), "vkAllocateDescriptorSets(svgf)");
    Check(vkAllocateDescriptorSets(g.device, &dai, &g.labelTaauDs), "vkAllocateDescriptorSets(labelTaau)");
}

// The three SVGF compute pipelines (temporal accumulation, edge-aware a-trous,
// composite), sharing one pipeline layout: set 0 = svgfDsLayout + a 120-byte push
// range (the SvgfPC struct in RecordRtTrace). Created after the descriptor layout.
void CreateSvgfPipelines()
{
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 120;
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &g.svgfDsLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.svgfPipeLayout),
          "vkCreatePipelineLayout(svgf)");

    struct { const unsigned char* code; unsigned len; VkPipeline* out; } passes[3] = {
        { svgf_temporal_comp_spv,  svgf_temporal_comp_spv_len,  &g.svgfTemporal  },
        { svgf_atrous_comp_spv,    svgf_atrous_comp_spv_len,    &g.svgfAtrous    },
        { svgf_composite_comp_spv, svgf_composite_comp_spv_len, &g.svgfComposite },
    };
    for (auto& p : passes) {
        VkShaderModule cs = MakeShader(p.code, p.len);
        VkComputePipelineCreateInfo cpci = {};
        cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        cpci.stage.module = cs;
        cpci.stage.pName  = "main";
        cpci.layout       = g.svgfPipeLayout;
        Check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, p.out),
              "vkCreateComputePipelines(svgf)");
        vkDestroyShaderModule(g.device, cs, nullptr);
    }
}

// On-screen RT mode label pipeline (debug). Re-uses svgfDsLayout (it already binds
// rtImage at binding 7); its own pipeline layout carries the 64-byte label push.
void CreateLabelPipeline()
{
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 64;   // matches LabelPC in RecordRtTrace + label.comp
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &g.svgfDsLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.labelPipeLayout),
          "vkCreatePipelineLayout(label)");

    VkShaderModule cs = MakeShader(label_comp_spv, label_comp_spv_len);
    VkComputePipelineCreateInfo cpci = {};
    cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = cs;
    cpci.stage.pName  = "main";
    cpci.layout       = g.labelPipeLayout;
    Check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g.labelPipeline),
          "vkCreateComputePipelines(label)");
    vkDestroyShaderModule(g.device, cs, nullptr);
}

// Temporal upscaler (build step 6-d): its own 4-binding descriptor set + compute
// pipeline. b0 = render-res denoised colour (rtImage), b1 = render-res motion vector
// (SV_MOTION), b2 = display-res history[2], b3 = display-res output. The push range
// is the 48-byte TaauPC (parity + display/render dims + jitter). Built once with the
// SVGF pipelines; the image views are pointed in by WriteTaauDescriptor on (re)size.
void CreateTaauPipeline()
{
    const uint32_t counts[4] = { 1, 1, 2, 1 };   // inColor, inMotion, hist[2], out
    VkDescriptorSetLayoutBinding b[4] = {};
    uint32_t total = 0;
    for (uint32_t i = 0; i < 4; i++) {
        b[i].binding         = i;
        b[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        b[i].descriptorCount = counts[i];
        b[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        total += counts[i];
    }
    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 4;
    dlci.pBindings    = b;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.taauDsLayout),
          "vkCreateDescriptorSetLayout(taau)");

    VkDescriptorPoolSize pool = {};
    pool.type            = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    pool.descriptorCount = total;
    VkDescriptorPoolCreateInfo pci = {};
    pci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets       = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes    = &pool;
    Check(vkCreateDescriptorPool(g.device, &pci, nullptr, &g.taauDsPool), "vkCreateDescriptorPool(taau)");

    VkDescriptorSetAllocateInfo dai = {};
    dai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool     = g.taauDsPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts        = &g.taauDsLayout;
    Check(vkAllocateDescriptorSets(g.device, &dai, &g.taauDs), "vkAllocateDescriptorSets(taau)");

    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 48;   // matches TaauPC in RecordRtTrace + taau.comp
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 1;
    plci.pSetLayouts            = &g.taauDsLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.taauPipeLayout),
          "vkCreatePipelineLayout(taau)");

    VkShaderModule cs = MakeShader(taau_comp_spv, taau_comp_spv_len);
    VkComputePipelineCreateInfo cpci = {};
    cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = cs;
    cpci.stage.pName  = "main";
    cpci.layout       = g.taauPipeLayout;
    Check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g.taauPipeline),
          "vkCreateComputePipelines(taau)");
    vkDestroyShaderModule(g.device, cs, nullptr);
}

// Map an RT debug mode to its on-screen title as label.comp glyph indices (font
// order: 0=space, A C D E F H I N O R S T U X Y). Returns the character count.
uint32_t ModeLabel(int mode, uint32_t* out)
{
    // Indices: A1 C2 D3 E4 F5 H6 I7 N8 O9 R10 S11 T12 U13 X14 Y15.
    static const uint32_t HITS[]     = { 6,7,12,11 };               // mode 1
    static const uint32_t FURNACE[]  = { 5,13,10,8,1,2,4 };         // mode 2
    static const uint32_t TEXTURED[] = { 12,4,14,12,13,10,4,3 };    // mode 3
    static const uint32_t NOISY[]    = { 8,9,7,11,15 };             // mode 4
    static const uint32_t DENOISED[] = { 3,4,8,9,7,11,4,3 };        // mode 6
    static const uint32_t RT[]       = { 10,12 };                   // fallback
    const uint32_t* s; uint32_t n;
    switch (mode) {
        case 1: s = HITS;     n = 4; break;
        case 2: s = FURNACE;  n = 7; break;
        case 3: s = TEXTURED; n = 8; break;
        case 4: s = NOISY;    n = 5; break;
        case 6: s = DENOISED; n = 8; break;
        default: s = RT;      n = 2; break;
    }
    for (uint32_t i = 0; i < n; i++) out[i] = s[i];
    return n;
}

// Point the SVGF descriptor set at the current image views. Binding 7 (output)
// re-uses the megakernel's rtImage view — the composite writes the same image the
// present path already blits. Called from CreateSvgfTargets (init + each resize).
void WriteSvgfDescriptor()
{
    VkDescriptorImageInfo info[14] = {};
    const VkImageView views[14] = {
        g.svView[SV_GPOS0],  g.svView[SV_GPOS1],            // b0 gpos[2]
        g.svView[SV_GNORM0], g.svView[SV_GNORM1],           // b1 gnorm[2]
        g.svView[SV_ALBEDO],                                // b2 albedo
        g.svView[SV_ILLUM],                                 // b3 illum
        g.svView[SV_HCOL0],  g.svView[SV_HCOL1],            // b4 hcol[2]
        g.svView[SV_HMOM0],  g.svView[SV_HMOM1],            // b5 hmom[2]
        g.svView[SV_ATROUS0],g.svView[SV_ATROUS1],          // b6 atrous[2]
        g.rtView,                                           // b7 outColor (rtImage)
        g.svView[SV_MOTION],                                // b8 motion vector (6-d)
    };
    for (uint32_t i = 0; i < 14; i++) {
        info[i].imageView   = views[i];
        info[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    struct { uint32_t binding, first, count; } map[9] = {
        {0,0,2},{1,2,2},{2,4,1},{3,5,1},{4,6,2},{5,8,2},{6,10,2},{7,12,1},{8,13,1}
    };
    VkWriteDescriptorSet wr[9] = {};
    for (int i = 0; i < 9; i++) {
        wr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet          = g.svgfDs;
        wr[i].dstBinding      = map[i].binding;
        wr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[i].descriptorCount = map[i].count;
        wr[i].pImageInfo      = &info[map[i].first];
    }
    vkUpdateDescriptorSets(g.device, 9, wr, 0, nullptr);
}

void DestroySvgfTargets()
{
    for (uint32_t i = 0; i < SV_COUNT; i++) {
        if (g.svView[i]) { vkDestroyImageView(g.device, g.svView[i], nullptr); g.svView[i] = VK_NULL_HANDLE; }
        if (g.svImg[i])  { vkDestroyImage(g.device, g.svImg[i], nullptr);      g.svImg[i]  = VK_NULL_HANDLE; }
        if (g.svMem[i])  { vkFreeMemory(g.device, g.svMem[i], nullptr);        g.svMem[i]  = VK_NULL_HANDLE; }
    }
}

// Create the swapchain-sized SVGF G-buffer + history images, park them all in
// GENERAL (storage read+write for their whole life), clear the histories (gpos
// histories to matId = -1 so a stale prev pixel can never validate against this
// frame), and (re)point the descriptor set. Called from CreateRtTargets, so it
// rebuilds with the swapchain. Resets the frame parity + prev-camera so the first
// post-resize frame starts a fresh temporal history.
void CreateSvgfTargets()
{
    const uint32_t W = g.extent.width, H = g.extent.height;
    for (uint32_t i = 0; i < SV_COUNT; i++) {
        VkFormat fmt = (i == SV_GPOS0 || i == SV_GPOS1) ? VK_FORMAT_R32G32B32A32_SFLOAT
                     : (i == SV_MOTION)                 ? VK_FORMAT_R16G16_SFLOAT
                                                        : VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImageCreateInfo ici = {};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = fmt;
        ici.extent        = { W, H, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Check(vkCreateImage(g.device, &ici, nullptr, &g.svImg[i]), "vkCreateImage(svgf)");

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(g.device, g.svImg[i], &req);
        VkMemoryAllocateInfo mai = {};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(g.device, &mai, nullptr, &g.svMem[i]), "vkAllocateMemory(svgf)");
        Check(vkBindImageMemory(g.device, g.svImg[i], g.svMem[i], 0), "vkBindImageMemory(svgf)");

        VkImageViewCreateInfo vci = {};
        vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image            = g.svImg[i];
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = fmt;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(g.device, &vci, nullptr, &g.svView[i]), "vkCreateImageView(svgf)");
    }

    {
        VkCommandBuffer cb = BeginOneTime();
        VkImageMemoryBarrier toGen[SV_COUNT] = {};
        for (uint32_t i = 0; i < SV_COUNT; i++) {
            toGen[i].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toGen[i].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            toGen[i].newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            toGen[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGen[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGen[i].image            = g.svImg[i];
            toGen[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            toGen[i].srcAccessMask    = 0;
            toGen[i].dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT
                                      | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, SV_COUNT, toGen);

        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkClearColorValue sky = {};  sky.float32[3] = -1.0f;   // matId < 0 -> never validates
        VkClearColorValue zero = {};
        const int clearSky[2]  = { SV_GPOS0, SV_GPOS1 };
        const int clearZero[4] = { SV_HCOL0, SV_HCOL1, SV_HMOM0, SV_HMOM1 };
        for (int i : clearSky)  vkCmdClearColorImage(cb, g.svImg[i], VK_IMAGE_LAYOUT_GENERAL, &sky,  1, &range);
        for (int i : clearZero) vkCmdClearColorImage(cb, g.svImg[i], VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        EndOneTime(cb);
    }

    WriteSvgfDescriptor();
    g.svgfFrame = 0;
    std::memset(g.prevCamPos,   0, sizeof(g.prevCamPos));
    std::memset(g.prevCamDir,   0, sizeof(g.prevCamDir));
    std::memset(g.prevCamRight, 0, sizeof(g.prevCamRight));
    std::memset(g.prevCamUp,    0, sizeof(g.prevCamUp));
}

// Point the TAAU descriptor set at the current views (build step 6-d): the render-
// res denoised colour (rtImage) + motion vector feed it, the display-res history +
// output close it. Also repoint labelTaauDs's binding 7 at the TAAU output so the
// debug label stamps on the upscaled image (the rest mirror svgfDs, unread by the
// label shader). Called from CreateTaauTargets (init + each resize).
void WriteTaauDescriptor()
{
    VkDescriptorImageInfo info[5] = {};
    const VkImageView views[5] = {
        g.rtView,                 // b0 inColor  (render-res denoised)
        g.svView[SV_MOTION],      // b1 inMotion (render-res MV)
        g.taView[TA_HIST0], g.taView[TA_HIST1],   // b2 hist[2]
        g.taView[TA_OUT],         // b3 outColor (display-res)
    };
    for (uint32_t i = 0; i < 5; i++) {
        info[i].imageView   = views[i];
        info[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    struct { uint32_t binding, first, count; } map[4] = { {0,0,1},{1,1,1},{2,2,2},{3,4,1} };
    VkWriteDescriptorSet wr[4] = {};
    for (int i = 0; i < 4; i++) {
        wr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[i].dstSet          = g.taauDs;
        wr[i].dstBinding      = map[i].binding;
        wr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        wr[i].descriptorCount = map[i].count;
        wr[i].pImageInfo      = &info[map[i].first];
    }
    vkUpdateDescriptorSets(g.device, 4, wr, 0, nullptr);

    // labelTaauDs: same 9 bindings as svgfDs but binding 7 (output) = TAAU output.
    VkDescriptorImageInfo lin[14] = {};
    const VkImageView lviews[14] = {
        g.svView[SV_GPOS0],  g.svView[SV_GPOS1],
        g.svView[SV_GNORM0], g.svView[SV_GNORM1],
        g.svView[SV_ALBEDO], g.svView[SV_ILLUM],
        g.svView[SV_HCOL0],  g.svView[SV_HCOL1],
        g.svView[SV_HMOM0],  g.svView[SV_HMOM1],
        g.svView[SV_ATROUS0],g.svView[SV_ATROUS1],
        g.taView[TA_OUT],                                   // b7 -> upscaled output
        g.svView[SV_MOTION],
    };
    for (uint32_t i = 0; i < 14; i++) {
        lin[i].imageView   = lviews[i];
        lin[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    struct { uint32_t binding, first, count; } lmap[9] = {
        {0,0,2},{1,2,2},{2,4,1},{3,5,1},{4,6,2},{5,8,2},{6,10,2},{7,12,1},{8,13,1}
    };
    VkWriteDescriptorSet lwr[9] = {};
    for (int i = 0; i < 9; i++) {
        lwr[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        lwr[i].dstSet          = g.labelTaauDs;
        lwr[i].dstBinding      = lmap[i].binding;
        lwr[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        lwr[i].descriptorCount = lmap[i].count;
        lwr[i].pImageInfo      = &lin[lmap[i].first];
    }
    vkUpdateDescriptorSets(g.device, 9, lwr, 0, nullptr);
}

void DestroyTaauTargets()
{
    for (uint32_t i = 0; i < TA_COUNT; i++) {
        if (g.taView[i]) { vkDestroyImageView(g.device, g.taView[i], nullptr); g.taView[i] = VK_NULL_HANDLE; }
        if (g.taImg[i])  { vkDestroyImage(g.device, g.taImg[i], nullptr);      g.taImg[i]  = VK_NULL_HANDLE; }
        if (g.taMem[i])  { vkFreeMemory(g.device, g.taMem[i], nullptr);        g.taMem[i]  = VK_NULL_HANDLE; }
    }
}

// Create the display-resolution TAAU history (rgba16f x2) + output (rgba8) images,
// park them in GENERAL for their whole life, clear the histories to zero, and point
// the descriptor sets. Called from CreateRtTargets, so it rebuilds with the swapchain.
void CreateTaauTargets()
{
    const uint32_t W = g.extent.width, H = g.extent.height;
    for (uint32_t i = 0; i < TA_COUNT; i++) {
        VkFormat fmt = (i == TA_OUT) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT;
        VkImageCreateInfo ici = {};
        ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType     = VK_IMAGE_TYPE_2D;
        ici.format        = fmt;
        ici.extent        = { W, H, 1 };
        ici.mipLevels     = 1;
        ici.arrayLayers   = 1;
        ici.samples       = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
        // The output is also a blit source; the histories are cleared at init/
        // resize via vkCmdClearColorImage, which requires TRANSFER_DST (DOOM-0133).
        ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT
                          | ((i == TA_OUT) ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                           : VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Check(vkCreateImage(g.device, &ici, nullptr, &g.taImg[i]), "vkCreateImage(taau)");

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(g.device, g.taImg[i], &req);
        VkMemoryAllocateInfo mai = {};
        mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize  = req.size;
        mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(g.device, &mai, nullptr, &g.taMem[i]), "vkAllocateMemory(taau)");
        Check(vkBindImageMemory(g.device, g.taImg[i], g.taMem[i], 0), "vkBindImageMemory(taau)");

        VkImageViewCreateInfo vci = {};
        vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image            = g.taImg[i];
        vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        vci.format           = fmt;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(g.device, &vci, nullptr, &g.taView[i]), "vkCreateImageView(taau)");
    }

    {
        VkCommandBuffer cb = BeginOneTime();
        VkImageMemoryBarrier toGen[TA_COUNT] = {};
        for (uint32_t i = 0; i < TA_COUNT; i++) {
            toGen[i].sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toGen[i].oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            toGen[i].newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            toGen[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGen[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGen[i].image            = g.taImg[i];
            toGen[i].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            toGen[i].srcAccessMask    = 0;
            toGen[i].dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT
                                      | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, TA_COUNT, toGen);
        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkClearColorValue zero = {};
        vkCmdClearColorImage(cb, g.taImg[TA_HIST0], VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        vkCmdClearColorImage(cb, g.taImg[TA_HIST1], VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
        EndOneTime(cb);
    }

    WriteTaauDescriptor();
}

void CreateRtTargets()
{
    VkImageCreateInfo ici = {};
    ici.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent        = { g.extent.width, g.extent.height, 1 };
    ici.mipLevels     = 1;
    ici.arrayLayers   = 1;
    ici.samples       = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(g.device, &ici, nullptr, &g.rtImage), "vkCreateImage(rt)");

    VkMemoryRequirements req = {};
    vkGetImageMemoryRequirements(g.device, g.rtImage, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize  = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.rtMemory), "vkAllocateMemory(rt)");
    Check(vkBindImageMemory(g.device, g.rtImage, g.rtMemory, 0), "vkBindImageMemory(rt)");

    VkImageViewCreateInfo vci = {};
    vci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image            = g.rtImage;
    vci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    vci.format           = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    Check(vkCreateImageView(g.device, &vci, nullptr, &g.rtView), "vkCreateImageView(rt)");

    UpdateRtComputeDescriptor();   // re-point the storage-image half at the new view
    CreateSvgfTargets();           // SVGF G-buffer/history (binding 7 re-uses rtView)
    CreateTaauTargets();           // 6-d upscaler history/output (reads rtView + SV_MOTION)
}

void DestroyRtTargets()
{
    DestroyTaauTargets();          // display-sized TAAU images, rebuilt with rtImage
    DestroySvgfTargets();          // swapchain-sized SVGF images, rebuilt with rtImage
    if (g.rtView)   { vkDestroyImageView(g.device, g.rtView, nullptr);  g.rtView = VK_NULL_HANDLE; }
    if (g.rtImage)  { vkDestroyImage(g.device, g.rtImage, nullptr);     g.rtImage = VK_NULL_HANDLE; }
    if (g.rtMemory) { vkFreeMemory(g.device, g.rtMemory, nullptr);      g.rtMemory = VK_NULL_HANDLE; }
}

void UpdateRtComputeDescriptor()
{
    // Needs the compute set, a TLAS (per level) and the storage image (per
    // swapchain). Called from each of their creation points; the first call with
    // all three present wires the set, later ones re-point it.
    if (g.rtDs == VK_NULL_HANDLE || g.tlas == VK_NULL_HANDLE || g.rtView == VK_NULL_HANDLE)
        return;

    VkWriteDescriptorSetAccelerationStructureKHR asInfo = {};
    asInfo.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures    = &g.tlas;

    VkDescriptorImageInfo imgInfo = {};
    imgInfo.imageView   = g.rtView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].pNext           = &asInfo;
    writes[0].dstSet          = g.rtDs;
    writes[0].dstBinding      = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet          = g.rtDs;
    writes[1].dstBinding      = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo      = &imgInfo;
    vkUpdateDescriptorSets(g.device, 2, writes, 0, nullptr);
}

// Create a device-local sampled image of the given format and upload `pixels`
// through a staging buffer, leaving it in SHADER_READ_ONLY_OPTIMAL with a view.
void CreateSampledImage(uint32_t w, uint32_t h, VkFormat fmt,
                        const void* pixels, VkDeviceSize bytes,
                        VkImage* outImage, VkDeviceMemory* outMem,
                        VkImageView* outView)
{
    // Staging buffer (host visible) holding the source texels.
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    Check(vkCreateBuffer(g.device, &bci, nullptr, &staging), "vkCreateBuffer(staging)");

    VkMemoryRequirements sreq = {};
    vkGetBufferMemoryRequirements(g.device, staging, &sreq);
    VkMemoryAllocateInfo smai = {};
    smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    smai.allocationSize = sreq.size;
    smai.memoryTypeIndex = FindMemoryType(sreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    Check(vkAllocateMemory(g.device, &smai, nullptr, &stagingMem), "vkAllocateMemory(staging)");
    Check(vkBindBufferMemory(g.device, staging, stagingMem, 0), "vkBindBufferMemory(staging)");

    void* mapped = nullptr;
    Check(vkMapMemory(g.device, stagingMem, 0, bytes, 0, &mapped), "vkMapMemory(staging)");
    std::memcpy(mapped, pixels, (size_t)bytes);
    vkUnmapMemory(g.device, stagingMem);

    // Device-local sampled image.
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = { w, h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(g.device, &ici, nullptr, outImage), "vkCreateImage(sampled)");

    VkMemoryRequirements req = {};
    vkGetImageMemoryRequirements(g.device, *outImage, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, outMem), "vkAllocateMemory(sampled)");
    Check(vkBindImageMemory(g.device, *outImage, *outMem, 0), "vkBindImageMemory(sampled)");

    // UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY.
    VkCommandBuffer cb = BeginOneTime();

    VkImageMemoryBarrier toDst = {};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = *outImage;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { w, h, 1 };
    vkCmdCopyBufferToImage(cb, staging, *outImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);

    EndOneTime(cb);

    vkDestroyBuffer(g.device, staging, nullptr);
    vkFreeMemory(g.device, stagingMem, nullptr);

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = *outImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    Check(vkCreateImageView(g.device, &vci, nullptr, outView), "vkCreateImageView(sampled)");
}

//
// Minimal column-major 4x4 matrix math (matches GLSL mat4 layout, so the
// result uploads straight into the push constant with no transpose). Only the
// three operations the camera needs — no general matrix library (YAGNI).
//

// out = a * b  (both column-major; out[col*4+row]).
void Mat4Mul(const float a[16], const float b[16], float out[16])
{
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
        {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k)
                s += a[k * 4 + row] * b[col * 4 + k];
            out[col * 4 + row] = s;
        }
}

// Right-handed look-at, column-major (glm::lookAtRH convention).
void Mat4LookAt(const float eye[3], const float fwd[3], const float up[3],
                float out[16])
{
    // s = right = normalize(fwd x up); u = up = s x fwd.
    float s[3] = { fwd[1]*up[2] - fwd[2]*up[1],
                   fwd[2]*up[0] - fwd[0]*up[2],
                   fwd[0]*up[1] - fwd[1]*up[0] };
    float sl = std::sqrt(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
    s[0] /= sl; s[1] /= sl; s[2] /= sl;
    float u[3] = { s[1]*fwd[2] - s[2]*fwd[1],
                   s[2]*fwd[0] - s[0]*fwd[2],
                   s[0]*fwd[1] - s[1]*fwd[0] };

    out[0] = s[0]; out[4] = s[1]; out[8]  = s[2];  out[12] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    out[1] = u[0]; out[5] = u[1]; out[9]  = u[2];  out[13] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    out[2] = -fwd[0]; out[6] = -fwd[1]; out[10] = -fwd[2]; out[14] = (fwd[0]*eye[0] + fwd[1]*eye[1] + fwd[2]*eye[2]);
    out[3] = 0.0f; out[7] = 0.0f; out[11] = 0.0f;  out[15] = 1.0f;
}

// Perspective with a fixed horizontal FOV, Vulkan clip space (z in [0,1], and
// the Y axis flipped vs. OpenGL). Column-major.
void Mat4PerspectiveH(float hfovRad, float aspect, float zn, float zf,
                      float out[16])
{
    float tanH = std::tan(hfovRad * 0.5f);
    std::memset(out, 0, 16 * sizeof(float));
    out[0]  = 1.0f / tanH;                 // horizontal FOV fixed at hfov
    out[5]  = -aspect / tanH;              // negative => Vulkan Y-flip
    out[10] = zf / (zn - zf);
    out[11] = -1.0f;
    out[14] = -(zf * zn) / (zf - zn);
}

void CreateImageViews()
{
    g.imageViews.resize(g.images.size());
    for (size_t i = 0; i < g.images.size(); ++i)
    {
        VkImageViewCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image = g.images[i];
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = g.format;
        ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.levelCount = 1;
        ci.subresourceRange.layerCount = 1;
        Check(vkCreateImageView(g.device, &ci, nullptr, &g.imageViews[i]),
              "vkCreateImageView");
    }
}

void CreateDepthResources()
{
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VulkanState::kDepthFormat;
    ici.extent = { g.extent.width, g.extent.height, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(g.device, &ici, nullptr, &g.depthImage), "vkCreateImage(depth)");

    VkMemoryRequirements req = {};
    vkGetImageMemoryRequirements(g.device, g.depthImage, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.depthMemory), "vkAllocateMemory(depth)");
    Check(vkBindImageMemory(g.device, g.depthImage, g.depthMemory, 0), "vkBindImageMemory(depth)");

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = g.depthImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VulkanState::kDepthFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    Check(vkCreateImageView(g.device, &vci, nullptr, &g.depthView), "vkCreateImageView(depth)");
}

// DOOM-0170 L2a: (re)create the off-screen scene colour target the world renders into
// before the composite pass. Swapchain-sized, 16-bit float (kSceneFormat) so the world
// shader's HDR lighting survives to the composite tone-map (step 3). usage
// COLOR_ATTACHMENT (rendered into) + SAMPLED (read by the composite).
void CreateSceneTarget()
{
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VulkanState::kSceneFormat;
    ici.extent = { g.extent.width, g.extent.height, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(g.device, &ici, nullptr, &g.sceneImage), "vkCreateImage(scene)");

    VkMemoryRequirements req = {};
    vkGetImageMemoryRequirements(g.device, g.sceneImage, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.sceneMemory), "vkAllocateMemory(scene)");
    Check(vkBindImageMemory(g.device, g.sceneImage, g.sceneMemory, 0), "vkBindImageMemory(scene)");

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = g.sceneImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VulkanState::kSceneFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    Check(vkCreateImageView(g.device, &vci, nullptr, &g.sceneView), "vkCreateImageView(scene)");

    // DOOM-0170 L2b — the DIRECT target (attachment 1), identical to the AMBIENT one above.
    // Same format/usage/size so the two are interchangeable framebuffer attachments; the
    // composite samples both (ambient binding 0, direct binding 1).
    Check(vkCreateImage(g.device, &ici, nullptr, &g.sceneDirImage), "vkCreateImage(sceneDir)");
    vkGetImageMemoryRequirements(g.device, g.sceneDirImage, &req);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.sceneDirMemory), "vkAllocateMemory(sceneDir)");
    Check(vkBindImageMemory(g.device, g.sceneDirImage, g.sceneDirMemory, 0), "vkBindImageMemory(sceneDir)");
    vci.image = g.sceneDirImage;
    Check(vkCreateImageView(g.device, &vci, nullptr, &g.sceneDirView), "vkCreateImageView(sceneDir)");

    // DOOM-0170 L2b — the half-res SSAO occlusion image (R8). Rendered by the ssao pass,
    // sampled by the composite. Half of the swapchain extent (the effect is cheap and low-
    // frequency, §4.3/§6). COLOR_ATTACHMENT (written) + SAMPLED (read by composite).
    g.aoExtent = { g.extent.width / 2u  ? g.extent.width / 2u  : 1u,
                   g.extent.height / 2u ? g.extent.height / 2u : 1u };
    VkImageCreateInfo aci = ici;
    aci.format = VK_FORMAT_R8_UNORM;
    aci.extent = { g.aoExtent.width, g.aoExtent.height, 1 };
    aci.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    Check(vkCreateImage(g.device, &aci, nullptr, &g.aoImage), "vkCreateImage(ao)");
    vkGetImageMemoryRequirements(g.device, g.aoImage, &req);
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.aoMemory), "vkAllocateMemory(ao)");
    Check(vkBindImageMemory(g.device, g.aoImage, g.aoMemory, 0), "vkBindImageMemory(ao)");
    VkImageViewCreateInfo avci = vci;
    avci.image  = g.aoImage;
    avci.format = VK_FORMAT_R8_UNORM;
    Check(vkCreateImageView(g.device, &avci, nullptr, &g.aoView), "vkCreateImageView(ao)");

    // Park the AO image in SHADER_READ so the composite may sample it even on a frame where
    // the SSAO pass is skipped (rb_ssao off). When the pass runs it re-clears via UNDEFINED.
    {
        VkCommandBuffer cb = BeginOneTime();
        VkImageMemoryBarrier bar = {};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = g.aoImage;
        bar.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        EndOneTime(cb);
    }
}

// Free the scene target (+ its framebuffer, built in CreateFramebuffers). Called from
// DestroyFramebufferResources so it rides the existing resize/teardown path.
void DestroySceneTarget()
{
    if (g.sceneFb)     { vkDestroyFramebuffer(g.device, g.sceneFb, nullptr); g.sceneFb = VK_NULL_HANDLE; }
    if (g.sceneView)   { vkDestroyImageView(g.device, g.sceneView, nullptr); g.sceneView = VK_NULL_HANDLE; }
    if (g.sceneImage)  { vkDestroyImage(g.device, g.sceneImage, nullptr);    g.sceneImage = VK_NULL_HANDLE; }
    if (g.sceneMemory) { vkFreeMemory(g.device, g.sceneMemory, nullptr);     g.sceneMemory = VK_NULL_HANDLE; }
    // DOOM-0170 L2b — the DIRECT target rides the same resize/teardown path.
    if (g.sceneDirView)   { vkDestroyImageView(g.device, g.sceneDirView, nullptr); g.sceneDirView = VK_NULL_HANDLE; }
    if (g.sceneDirImage)  { vkDestroyImage(g.device, g.sceneDirImage, nullptr);    g.sceneDirImage = VK_NULL_HANDLE; }
    if (g.sceneDirMemory) { vkFreeMemory(g.device, g.sceneDirMemory, nullptr);     g.sceneDirMemory = VK_NULL_HANDLE; }
    // DOOM-0170 L2b — the half-res AO image + its framebuffer, same resize path.
    if (g.aoFb)     { vkDestroyFramebuffer(g.device, g.aoFb, nullptr); g.aoFb = VK_NULL_HANDLE; }
    if (g.aoView)   { vkDestroyImageView(g.device, g.aoView, nullptr); g.aoView = VK_NULL_HANDLE; }
    if (g.aoImage)  { vkDestroyImage(g.device, g.aoImage, nullptr);    g.aoImage = VK_NULL_HANDLE; }
    if (g.aoMemory) { vkFreeMemory(g.device, g.aoMemory, nullptr);     g.aoMemory = VK_NULL_HANDLE; }
}

// DOOM-0170 L2c — build the flashlight cast-shadow map (§4.4): a fixed 2048^2 depth image
// the world renders into from the torch's viewpoint, plus its render pass, framebuffer,
// sampler, the lightVP uniform buffer mesh.frag reads, and the set-1 descriptor. All
// size-independent (never rebuilt on resize), so this runs once at init. The shadow
// PIPELINE is built in CreatePipeline (it reuses that function's vertex-input state);
// this only needs g.shadowPass + g.shadowPipeLayout ready before then. Requires the
// command pool (the one-time layout park) + g.dsLayout (the shadow pipeline layout binds
// set 0 for the material array), so RB_Vulkan_Init calls it after CreateDescriptors.
void CreateShadowResources()
{
    const uint32_t dim = VulkanState::kShadowDim;

    // Depth image: DEPTH_STENCIL_ATTACHMENT (rendered into) + SAMPLED (mesh.frag PCF).
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VulkanState::kShadowFormat;
    ici.extent = { dim, dim, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(g.device, &ici, nullptr, &g.shadowImage), "vkCreateImage(shadow)");

    VkMemoryRequirements req = {};
    vkGetImageMemoryRequirements(g.device, g.shadowImage, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.shadowMemory), "vkAllocateMemory(shadow)");
    Check(vkBindImageMemory(g.device, g.shadowImage, g.shadowMemory, 0), "vkBindImageMemory(shadow)");

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = g.shadowImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VulkanState::kShadowFormat;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    Check(vkCreateImageView(g.device, &vci, nullptr, &g.shadowView), "vkCreateImageView(shadow)");

    // Depth sampler: point-sampled (we do our own 3x3 PCF), clamped to a white border so
    // samples outside the light frustum read depth 1.0 (far) = "not shadowed" = lit.
    VkSamplerCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_NEAREST;
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    sci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    Check(vkCreateSampler(g.device, &sci, nullptr, &g.shadowSampler), "vkCreateSampler(shadow)");

    // Depth-only render pass: one attachment, cleared to far, left in SHADER_READ_ONLY so
    // the scene pass samples it. Dependencies order the previous frame's sampling before
    // the write, and this write before mesh.frag's read.
    VkAttachmentDescription datt = {};
    datt.format = VulkanState::kShadowFormat;
    datt.samples = VK_SAMPLE_COUNT_1_BIT;
    datt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    datt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    datt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    datt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    datt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    datt.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference dref = { 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &dref;
    VkSubpassDependency deps[2] = {};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpci = {};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &datt;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;
    Check(vkCreateRenderPass(g.device, &rpci, nullptr, &g.shadowPass), "vkCreateRenderPass(shadow)");

    VkFramebufferCreateInfo fci = {};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = g.shadowPass;
    fci.attachmentCount = 1;
    fci.pAttachments = &g.shadowView;
    fci.width = dim;
    fci.height = dim;
    fci.layers = 1;
    Check(vkCreateFramebuffer(g.device, &fci, nullptr, &g.shadowFb), "vkCreateFramebuffer(shadow)");

    // lightVP uniform buffer (single mat4). Single-frame-in-flight (one g.inFlight fence),
    // so one persistently-mapped buffer is safe to overwrite each frame.
    CreateRtBuffer(16 * sizeof(float), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.shadowUbo, &g.shadowUboMemory);
    Check(vkMapMemory(g.device, g.shadowUboMemory, 0, 16 * sizeof(float), 0, &g.shadowUboMapped),
          "vkMapMemory(shadowUbo)");

    // Shadow-pass pipeline layout: set 0 = the shared g.ds (material array for the cut-out
    // alpha test) + a push range carrying lightVP and the material-id offsets.
    VkPushConstantRange spcr = {};
    spcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    spcr.offset = 0;
    spcr.size = 16 * sizeof(float) + 2 * sizeof(int);   // mat4 lightVP + numWall/numFlat
    VkPipelineLayoutCreateInfo splci = {};
    splci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    splci.setLayoutCount = 1;
    splci.pSetLayouts = &g.dsLayout;
    splci.pushConstantRangeCount = 1;
    splci.pPushConstantRanges = &spcr;
    Check(vkCreatePipelineLayout(g.device, &splci, nullptr, &g.shadowPipeLayout),
          "vkCreatePipelineLayout(shadow)");

    // Set-1 descriptor for mesh.frag: the shadow map (binding 0) + lightVP UBO (binding 1).
    // Written once (image view + buffer are stable for life).
    VkDescriptorPoolSize sps[2] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1 },
    };
    VkDescriptorPoolCreateInfo sdpci = {};
    sdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    sdpci.maxSets = 1;
    sdpci.poolSizeCount = 2;
    sdpci.pPoolSizes = sps;
    Check(vkCreateDescriptorPool(g.device, &sdpci, nullptr, &g.shadowDsPool),
          "vkCreateDescriptorPool(shadow)");

    VkDescriptorSetAllocateInfo sdsai = {};
    sdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    sdsai.descriptorPool = g.shadowDsPool;
    sdsai.descriptorSetCount = 1;
    sdsai.pSetLayouts = &g.shadowDsLayout;
    Check(vkAllocateDescriptorSets(g.device, &sdsai, &g.shadowDs), "vkAllocateDescriptorSets(shadow)");

    VkDescriptorImageInfo sii = { g.shadowSampler, g.shadowView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkDescriptorBufferInfo sbi = { g.shadowUbo, 0, 16 * sizeof(float) };
    VkWriteDescriptorSet sw[2] = {};
    sw[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sw[0].dstSet = g.shadowDs; sw[0].dstBinding = 0; sw[0].descriptorCount = 1;
    sw[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sw[0].pImageInfo = &sii;
    sw[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sw[1].dstSet = g.shadowDs; sw[1].dstBinding = 1; sw[1].descriptorCount = 1;
    sw[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    sw[1].pBufferInfo = &sbi;
    vkUpdateDescriptorSets(g.device, 2, sw, 0, nullptr);

    // Park the depth image in SHADER_READ_ONLY once. mesh.frag statically references the
    // sampler (set 1), so on flashlight-off frames — when the shadow pass is skipped —
    // the descriptor must still point at a validly-laid-out image. The first torch-on
    // frame re-clears it via the render pass (initialLayout UNDEFINED).
    {
        VkCommandBuffer cb = BeginOneTime();
        VkImageMemoryBarrier bar = {};
        bar.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = g.shadowImage;
        bar.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &bar);
        EndOneTime(cb);
    }
}

void CreateRenderPass()
{
    VkAttachmentDescription att[2] = {};
    att[0].format = g.format;                                  // colour (swapchain)
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;       // present directly
    att[1].format = VulkanState::kDepthFormat;                 // depth
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep = {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci = {};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 2;
    rpci.pAttachments = att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    Check(vkCreateRenderPass(g.device, &rpci, nullptr, &g.renderPass),
          "vkCreateRenderPass");

    // DOOM-0170 L2a: scene pass — like renderPass but its colour attachment is the
    // off-screen scene target in the 16-bit float kSceneFormat (step 3: HDR headroom for
    // the composite tone-map), left in COLOR_ATTACHMENT_OPTIMAL (an explicit barrier after
    // the pass transitions it to SHADER_READ_ONLY for the composite sample) instead of the
    // swapchain in PRESENT_SRC. Because the colour format now differs from renderPass, the
    // world/sky/wire pipelines are built against THIS pass (see g.scenePipeline &co below);
    // the 8-bit g.pipeline is kept only for the RT weapon overlay. Built from copies so the
    // shared att/dep/rpci stay intact for the rtOverlayPass below.
    // DOOM-0170 L2b — the scene pass gains a SECOND colour attachment: attachment 0 =
    // AMBIENT (sceneImage), attachment 1 = DIRECT (sceneDirImage), attachment 2 = depth.
    // Both colour targets are the HDR float format, cleared + stored, and left in
    // COLOR_ATTACHMENT_OPTIMAL (the post-pass barrier transitions both to SHADER_READ for
    // the composite). Every pipeline drawn into this pass writes both colour outputs.
    {
        VkAttachmentDescription satt[3] = { att[0], att[0], att[1] };
        satt[0].format      = VulkanState::kSceneFormat;
        satt[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        satt[1].format      = VulkanState::kSceneFormat;
        satt[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // satt[2] = depth (att[1]) unchanged.
        VkAttachmentReference scolorRefs[2] = {
            { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
            { 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
        };
        VkAttachmentReference sdepthRef = { 2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
        VkSubpassDescription ssub = sub;
        ssub.colorAttachmentCount = 2;
        ssub.pColorAttachments    = scolorRefs;
        ssub.pDepthStencilAttachment = &sdepthRef;
        VkRenderPassCreateInfo srpci = rpci;
        srpci.attachmentCount = 3;
        srpci.pAttachments    = satt;
        srpci.pSubpasses      = &ssub;
        Check(vkCreateRenderPass(g.device, &srpci, nullptr, &g.scenePass),
              "vkCreateRenderPass(scene)");
    }

    // DOOM-0170 L2b — the SSAO pass: a single R8 colour attachment (the half-res occlusion
    // image), no depth. Contents are fully written by the full-screen ssao.frag, so loadOp is
    // DONT_CARE; it is left in SHADER_READ_ONLY for the composite to sample. The subpass->
    // EXTERNAL dependency publishes the AO write to the composite's fragment read.
    {
        VkAttachmentDescription aatt = {};
        aatt.format         = VK_FORMAT_R8_UNORM;
        aatt.samples        = VK_SAMPLE_COUNT_1_BIT;
        aatt.loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        aatt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        aatt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        aatt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        aatt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        aatt.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkAttachmentReference aref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription asub = {};
        asub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        asub.colorAttachmentCount = 1;
        asub.pColorAttachments    = &aref;
        VkSubpassDependency adep[2] = {};
        adep[0].srcSubpass = VK_SUBPASS_EXTERNAL;   // DIRECT target read is ordered by the
        adep[0].dstSubpass = 0;                     // explicit barrier in Present; this just
        adep[0].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;   // chains the colour write
        adep[0].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        adep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        adep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        adep[1].srcSubpass = 0;                     // publish the AO write to the composite read
        adep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        adep[1].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        adep[1].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        adep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        adep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo arpci = {};
        arpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        arpci.attachmentCount = 1;
        arpci.pAttachments    = &aatt;
        arpci.subpassCount    = 1;
        arpci.pSubpasses      = &asub;
        arpci.dependencyCount = 2;
        arpci.pDependencies   = adep;
        Check(vkCreateRenderPass(g.device, &arpci, nullptr, &g.aoPass),
              "vkCreateRenderPass(ao)");
    }

    // DOOM-0094: a LOAD-variant for the path-traced present path. RecordRtTrace blits
    // the traced world to the swapchain and leaves it in PRESENT_SRC; this pass LOADs
    // that colour (loadOp=LOAD, initialLayout PRESENT_SRC) and draws the weapon + the
    // 2D overlay on top, handing the image back in PRESENT_SRC. Depth is still cleared
    // (the weapon is a screen-space psprite that always sits on top). Same attachment
    // formats, so it is render-pass-compatible with g.framebuffers and the pipelines.
    att[0].loadOp        = VK_ATTACHMENT_LOAD_OP_LOAD;
    att[0].initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;   // keep the blitted trace
    // The blit that fills the colour image is a TRANSFER write; order it before the
    // colour LOAD + attachment writes (the base dep only chains the colour/depth stages).
    dep.srcStageMask  = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    Check(vkCreateRenderPass(g.device, &rpci, nullptr, &g.rtOverlayPass),
          "vkCreateRenderPass(rtOverlay)");
}

void CreateFramebuffers()
{
    g.framebuffers.resize(g.imageViews.size());
    for (size_t i = 0; i < g.imageViews.size(); ++i)
    {
        VkImageView attach[2] = { g.imageViews[i], g.depthView };
        VkFramebufferCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = g.renderPass;
        fci.attachmentCount = 2;
        fci.pAttachments = attach;
        fci.width = g.extent.width;
        fci.height = g.extent.height;
        fci.layers = 1;
        Check(vkCreateFramebuffer(g.device, &fci, nullptr, &g.framebuffers[i]),
              "vkCreateFramebuffer");
    }

    // DOOM-0170 L2a: the single off-screen scene framebuffer (scene colour + the shared
    // depth), bound by scenePass. CreateSceneTarget (which makes g.sceneView) runs before
    // this in both the init and resize paths.
    // DOOM-0170 L2b — attachment order matches g.scenePass: AMBIENT, DIRECT, depth.
    VkImageView sattach[3] = { g.sceneView, g.sceneDirView, g.depthView };
    VkFramebufferCreateInfo sfci = {};
    sfci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    sfci.renderPass = g.scenePass;
    sfci.attachmentCount = 3;
    sfci.pAttachments = sattach;
    sfci.width = g.extent.width;
    sfci.height = g.extent.height;
    sfci.layers = 1;
    Check(vkCreateFramebuffer(g.device, &sfci, nullptr, &g.sceneFb),
          "vkCreateFramebuffer(scene)");

    // DOOM-0170 L2b — the half-res SSAO framebuffer (one R8 colour attachment, no depth).
    VkFramebufferCreateInfo afci = {};
    afci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    afci.renderPass = g.aoPass;
    afci.attachmentCount = 1;
    afci.pAttachments = &g.aoView;
    afci.width = g.aoExtent.width;
    afci.height = g.aoExtent.height;
    afci.layers = 1;
    Check(vkCreateFramebuffer(g.device, &afci, nullptr, &g.aoFb),
          "vkCreateFramebuffer(ao)");
}

VkShaderModule MakeShader(const unsigned char* code, unsigned len)
{
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = len;
    ci.pCode = reinterpret_cast<const uint32_t*>(code);
    VkShaderModule m = VK_NULL_HANDLE;
    Check(vkCreateShaderModule(g.device, &ci, nullptr, &m), "vkCreateShaderModule");
    return m;
}

// Descriptor set 0 for the materials pass: the paletted atlas, the PLAYPAL LUT,
// and the per-id atlas-rect storage buffer. Plus the nearest/clamped sampler
// shared by both images (the shader wraps UVs itself, so addressing is clamp).
void CreateDescriptors()
{
    // Three bindings: PLAYPAL LUT (0) + HUD/menu overlay (1) are single images the
    // title screen needs before any level; the bindless material array (2) is a
    // variable-count array of every wall/flat/sprite image, written once the atlas
    // is built (UploadAtlas). The variable-count binding must be the highest, and
    // PARTIALLY_BOUND lets the set be valid at the title screen with the material
    // slot still unwritten (the overlay pass never samples it).
    const uint32_t matCount = (uint32_t)RB_MaterialCount();

    VkDescriptorSetLayoutBinding binds[3] = {};
    // PLAYPAL LUT (0) and the bindless material array (2) are also read by the
    // path-tracer compute megakernel (DOOM-0009 step 3a, pathtrace.comp set 1),
    // which binds this very set, so they carry COMPUTE as well as FRAGMENT. The
    // HUD/menu overlay (1) is fragment-only (the trace never samples it). Adding a
    // stage is permissive — it does not alter the raster pass (INV-10).
    binds[0].binding = 0;   // PLAYPAL LUT
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;   // 2D HUD/menu overlay (R8 screens[0] indices)
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binds[2].binding = 2;   // bindless material array (R8 palette-index images)
    binds[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[2].descriptorCount = matCount;
    binds[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorBindingFlags flags[3] = {
        0, 0,
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
    };
    VkDescriptorSetLayoutBindingFlagsCreateInfo bfci = {};
    bfci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bfci.bindingCount = 3;
    bfci.pBindingFlags = flags;

    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.pNext = &bfci;
    dlci.bindingCount = 3;
    dlci.pBindings = binds;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.dsLayout),
          "vkCreateDescriptorSetLayout");

    VkSamplerCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_NEAREST;          // paletted art: point sampling
    sci.minFilter = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;   // per-image native tiling
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    Check(vkCreateSampler(g.device, &sci, nullptr, &g.texSampler), "vkCreateSampler");

    // DOOM-0042: HD material sampler — linear filtering + full mip chain + REPEAT
    // tiling (walls tile U 0..N). Distinct from the nearest paletted g.texSampler.
    VkSamplerCreateInfo hsci = {};
    hsci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    hsci.magFilter = VK_FILTER_LINEAR;
    hsci.minFilter = VK_FILTER_LINEAR;
    hsci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    hsci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    hsci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    hsci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    hsci.maxLod = VK_LOD_CLAMP_NONE;
    Check(vkCreateSampler(g.device, &hsci, nullptr, &g.hdSampler), "vkCreateSampler(hd)");

    // DOOM-0170 L2a step 2: the composite samples a render-scaled scene target and
    // upscales it to the swapchain, so it wants linear filtering (smooth, not blocky)
    // and clamp-to-edge (the scene fills only the [0,scale] corner of a full-size
    // image -- repeat would fold the far edge back in at the seam).
    VkSamplerCreateInfo lci = {};
    lci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    lci.magFilter = VK_FILTER_LINEAR;
    lci.minFilter = VK_FILTER_LINEAR;
    lci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    lci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    lci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    lci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Check(vkCreateSampler(g.device, &lci, nullptr, &g.compositeSampler),
          "vkCreateSampler(composite)");

    // DOOM-0170 L2a composite descriptor: one combined-image-sampler (the scene target),
    // its own layout/pool/set + a pipeline layout binding only it. Separate from
    // g.dsLayout so the variable-count material array there stays the last binding.
    {
        // DOOM-0170 L2b — three combined-image-samplers: binding 0 = AMBIENT scene target,
        // binding 1 = DIRECT scene target, binding 2 = the half-res SSAO factor. The composite
        // outputs DIRECT + AO×AMBIENT.
        VkDescriptorSetLayoutBinding cb[3] = {};
        cb[0].binding = 0;
        cb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        cb[0].descriptorCount = 1;
        cb[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        cb[1] = cb[0]; cb[1].binding = 1;
        cb[2] = cb[0]; cb[2].binding = 2;
        VkDescriptorSetLayoutCreateInfo clci = {};
        clci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        clci.bindingCount = 3;
        clci.pBindings = cb;
        Check(vkCreateDescriptorSetLayout(g.device, &clci, nullptr, &g.compositeDsLayout),
              "vkCreateDescriptorSetLayout(composite)");

        VkDescriptorPoolSize cps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 };
        VkDescriptorPoolCreateInfo cdpci = {};
        cdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        cdpci.maxSets = 1;
        cdpci.poolSizeCount = 1;
        cdpci.pPoolSizes = &cps;
        Check(vkCreateDescriptorPool(g.device, &cdpci, nullptr, &g.compositeDsPool),
              "vkCreateDescriptorPool(composite)");

        VkDescriptorSetAllocateInfo cdsai = {};
        cdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        cdsai.descriptorPool = g.compositeDsPool;
        cdsai.descriptorSetCount = 1;
        cdsai.pSetLayouts = &g.compositeDsLayout;
        Check(vkAllocateDescriptorSets(g.device, &cdsai, &g.compositeDs),
              "vkAllocateDescriptorSets(composite)");

        // DOOM-0170 L2a step 2: a vec2 push constant carries the fraction of the
        // scene target the render-scaled world actually filled, so composite.frag
        // samples only that [0,scale] corner and upscales it to the full swapchain.
        VkPushConstantRange cpc = {};
        cpc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        cpc.offset = 0;
        cpc.size = 4 * sizeof(float);   // DOOM-0170 L2b: vec2 uvScale + aoEnable + pad
        VkPipelineLayoutCreateInfo cplci = {};
        cplci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        cplci.setLayoutCount = 1;
        cplci.pSetLayouts = &g.compositeDsLayout;
        cplci.pushConstantRangeCount = 1;
        cplci.pPushConstantRanges = &cpc;
        Check(vkCreatePipelineLayout(g.device, &cplci, nullptr, &g.compositePipeLayout),
              "vkCreatePipelineLayout(composite)");
    }

    // DOOM-0170 L2b — the SSAO pass descriptor + pipeline layout: one combined-image-sampler
    // (the DIRECT scene target, whose alpha carries the packed depth) + an 8-float push block
    // (uvScale, tanH, aspect, radius, bias, intensity, power). Separate small set/pool/layout,
    // like the composite's, so it stays independent of the material set's variable-count array.
    {
        VkDescriptorSetLayoutBinding sb = {};
        sb.binding = 0;
        sb.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sb.descriptorCount = 1;
        sb.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo slci = {};
        slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        slci.bindingCount = 1;
        slci.pBindings = &sb;
        Check(vkCreateDescriptorSetLayout(g.device, &slci, nullptr, &g.ssaoDsLayout),
              "vkCreateDescriptorSetLayout(ssao)");

        VkDescriptorPoolSize sps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
        VkDescriptorPoolCreateInfo sdpci = {};
        sdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        sdpci.maxSets = 1;
        sdpci.poolSizeCount = 1;
        sdpci.pPoolSizes = &sps;
        Check(vkCreateDescriptorPool(g.device, &sdpci, nullptr, &g.ssaoDsPool),
              "vkCreateDescriptorPool(ssao)");

        VkDescriptorSetAllocateInfo sdsai = {};
        sdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        sdsai.descriptorPool = g.ssaoDsPool;
        sdsai.descriptorSetCount = 1;
        sdsai.pSetLayouts = &g.ssaoDsLayout;
        Check(vkAllocateDescriptorSets(g.device, &sdsai, &g.ssaoDs),
              "vkAllocateDescriptorSets(ssao)");

        VkPushConstantRange spc = {};
        spc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        spc.offset = 0;
        spc.size = 8 * sizeof(float);
        VkPipelineLayoutCreateInfo splci = {};
        splci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        splci.setLayoutCount = 1;
        splci.pSetLayouts = &g.ssaoDsLayout;
        splci.pushConstantRangeCount = 1;
        splci.pPushConstantRanges = &spc;
        Check(vkCreatePipelineLayout(g.device, &splci, nullptr, &g.aoPipeLayout),
              "vkCreatePipelineLayout(ssao)");
    }

    // DOOM-0170 L2c — the flashlight shadow map's set-1 layout for the world pipeline:
    // the depth map (binding 0) + the lightVP uniform buffer (binding 1), both read by
    // mesh.frag. Kept a separate set (not appended to g.dsLayout) because that set's
    // variable-count material array must stay its last binding. The descriptor SET, image
    // and buffer are created later in CreateShadowResources; only the layout is needed
    // here so CreatePipeline can put it at set 1 of the world pipeline layout.
    {
        VkDescriptorSetLayoutBinding sb[2] = {};
        sb[0].binding = 0;   // shadow depth map
        sb[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sb[0].descriptorCount = 1;
        sb[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        sb[1].binding = 1;   // lightVP
        sb[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sb[1].descriptorCount = 1;
        sb[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo slci = {};
        slci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        slci.bindingCount = 2;
        slci.pBindings = sb;
        Check(vkCreateDescriptorSetLayout(g.device, &slci, nullptr, &g.shadowDsLayout),
              "vkCreateDescriptorSetLayout(shadow)");
    }
}

// DOOM-0170 L2a: point the composite descriptor at the current scene view. The scene
// image is recreated on every resize, so this runs after each CreateSceneTarget.
void UpdateCompositeDescriptor()
{
    if (!g.compositeDs || !g.sceneView || !g.sceneDirView || !g.aoView || !g.ssaoDs)
        return;
    // DOOM-0170 L2b — composite: binding 0 = AMBIENT, 1 = DIRECT, 2 = the half-res AO factor.
    // The SSAO set samples the DIRECT target (its alpha carries the packed depth). All use the
    // linear+clamp composite sampler (smooth upscale). Recreated on every resize.
    VkDescriptorImageInfo ii[3] = {};
    ii[0].sampler = g.compositeSampler;
    ii[0].imageView = g.sceneView;
    ii[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ii[1] = ii[0]; ii[1].imageView = g.sceneDirView;
    ii[2] = ii[0]; ii[2].imageView = g.aoView;
    VkWriteDescriptorSet w[4] = {};
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = g.compositeDs;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &ii[0];
    w[1] = w[0]; w[1].dstBinding = 1; w[1].pImageInfo = &ii[1];
    w[2] = w[0]; w[2].dstBinding = 2; w[2].pImageInfo = &ii[2];
    // The SSAO pass's own set samples the DIRECT target for the packed depth.
    w[3] = w[0]; w[3].dstSet = g.ssaoDs; w[3].dstBinding = 0; w[3].pImageInfo = &ii[1];
    vkUpdateDescriptorSets(g.device, 4, w, 0, nullptr);
}

void CreatePipeline()
{
    VkShaderModule vert = MakeShader(mesh_vert_spv, mesh_vert_spv_len);
    VkShaderModule frag = MakeShader(mesh_frag_spv, mesh_frag_spv_len);
    // DOOM-0170 L2b — 1-output build of mesh.frag for the RT weapon overlay (below), which
    // targets the single-attachment swapchain pass; the 2-output `frag` would leave its
    // location-1 write attachmentless there.
    VkShaderModule fragOverlay = MakeShader(mesh_overlay_frag_spv, mesh_overlay_frag_spv_len);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // Vertex layout: position, normal, texel UV, sector light, texture id, and
    // mesh flags — everything the per-texel materials pass samples with.
    VkVertexInputBindingDescription bind = {};
    bind.binding = 0;
    bind.stride = sizeof(rb_vertex_t);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[6] = {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(rb_vertex_t, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(rb_vertex_t, nx) };
    attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    (uint32_t)offsetof(rb_vertex_t, u) };
    attrs[3] = { 3, 0, VK_FORMAT_R32_SFLOAT,       (uint32_t)offsetof(rb_vertex_t, light) };
    attrs[4] = { 4, 0, VK_FORMAT_R32_SINT,         (uint32_t)offsetof(rb_vertex_t, texnum) };
    attrs[5] = { 5, 0, VK_FORMAT_R32_SINT,         (uint32_t)offsetof(rb_vertex_t, flags) };

    VkPipelineVertexInputStateCreateInfo vin = {};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 6;
    vin.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;   // viewport + scissor are dynamic (survive resize)

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;   // both wall faces visible; winding-agnostic
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba = {};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    // 1-attachment blend for the swapchain-targeted pipelines (rtWeapon/overlay/composite).
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    // DOOM-0170 L2b — the scene pass now has TWO colour attachments (AMBIENT + DIRECT), so
    // the pipelines drawn into it (world/sky/wire) need a matching 2-attachment blend state;
    // both are opaque (blend off). A pipeline's blend-attachment count must equal its render
    // pass's colour-attachment count, so the swapchain pipelines keep the 1-attachment `cb`.
    VkPipelineColorBlendAttachmentState cbaScene[2] = { cba, cba };
    VkPipelineColorBlendStateCreateInfo cbScene = cb;
    cbScene.attachmentCount = 2;
    cbScene.pAttachments = cbaScene;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState = {};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;

    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 31 * sizeof(float);   // mat4 MVP + extralight + sky yaw + camera xyz
                                     // + numWall/numFlat (material-id offsets)
                                     // + flashlight on/off (DOOM-0044)
                                     // + DOOM-0170 L1a/L1b: probes/triSs/lights device
                                     //   addrs (3x u64) + probeCount (u32) = 124 B (<128 floor)

    // DOOM-0170 L2c: set 0 = the shared g.ds (palette/materials/overlay); set 1 = the
    // flashlight shadow map + lightVP (g.shadowDsLayout) that mesh.frag samples for the
    // cast-shadow PCF. sky/wire/overlay/rtWeapon share this layout; only mesh.frag reads
    // set 1, so the raster scene draw and RecordRtOverlay bind g.shadowDs at set 1.
    VkDescriptorSetLayout worldSets[2] = { g.dsLayout, g.shadowDsLayout };
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 2;
    plci.pSetLayouts = worldSets;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.pipelineLayout),
          "vkCreatePipelineLayout");

    VkGraphicsPipelineCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cbScene;   // DOOM-0170 L2b: world/sky/wire target the 2-target scene pass
    pci.pDynamicState = &dynState;
    pci.layout = g.pipelineLayout;
    // DOOM-0170 L2a step 3: the world/sky/wire pipelines render into the HDR off-screen
    // scene target, so they are built against the float g.scenePass (not the 8-bit
    // swapchain renderPass). rtWeaponPipeline below rebuilds this config against renderPass
    // for the RT weapon overlay.
    pci.renderPass = g.scenePass;
    pci.subpass = 0;
    Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &g.pipeline), "vkCreateGraphicsPipelines");

    // Sky variant: identical, but with depth test + write off so the full-screen
    // backdrop always paints and never occludes the world drawn over it.
    VkPipelineDepthStencilStateCreateInfo skyDs = ds;
    skyDs.depthTestEnable  = VK_FALSE;
    skyDs.depthWriteEnable = VK_FALSE;
    pci.pDepthStencilState = &skyDs;
    Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &g.skyPipeline), "vkCreateGraphicsPipelines(sky)");

    // Wireframe variant of the world pipeline (debug view). Same mesh shaders and
    // depth-tested state, only polygonMode flipped to LINE; built last so the FILL
    // mode is restored for any pipeline created after. Skipped without the feature.
    if (g.wireSupported)
    {
        rs.polygonMode = VK_POLYGON_MODE_LINE;
        pci.pDepthStencilState = &ds;   // depth-tested like the world it mirrors
        Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                        &g.wirePipeline), "vkCreateGraphicsPipelines(wire)");
        rs.polygonMode = VK_POLYGON_MODE_FILL;
    }

    // DOOM-0170 L2a step 3: 8-bit twin of the world pipeline for the RT weapon overlay.
    // The scene pipelines above target the float g.scenePass, so they cannot draw onto the
    // 8-bit swapchain; RecordRtOverlay draws the weapon over the traced blit and needs a
    // renderPass-compatible pipeline. Identical state (FILL, depth-on) — only the pass
    // differs. (rs is FILL and pDepthStencilState is left at &ds by the blocks above, but
    // set both explicitly so this stays correct regardless of the wire branch.)
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cb;   // DOOM-0170 L2b: back to 1 attachment for the swapchain renderPass
    pci.renderPass = g.renderPass;
    // DOOM-0170 L2b — the weapon overlay uses the SINGLE_TARGET mesh.frag (combined colour to
    // location 0) so it matches this 1-attachment pass. Swap the frag module for this build,
    // then restore `stages` for any pipeline created afterward.
    VkPipelineShaderStageCreateInfo rwStages[2] = { stages[0], stages[1] };
    rwStages[1].module = fragOverlay;
    pci.pStages = rwStages;
    Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &g.rtWeaponPipeline), "vkCreateGraphicsPipelines(rtWeapon)");
    pci.pStages = stages;

    // DOOM-0170 L2c: flashlight cast-shadow map depth pass (Pass A). Same mesh vertex
    // layout (reuses `vin`), but shadow shaders, no colour attachment (depth-only), a
    // depth bias against shadow acne, and the g.shadowPass/g.shadowPipeLayout built in
    // CreateShadowResources. Rendered each torch-on frame before the scene pass.
    {
        VkShaderModule svVert = MakeShader(shadow_vert_spv, shadow_vert_spv_len);
        VkShaderModule svFrag = MakeShader(shadow_frag_spv, shadow_frag_spv_len);
        VkPipelineShaderStageCreateInfo svStages[2] = { stages[0], stages[1] };
        svStages[0].module = svVert;
        svStages[1].module = svFrag;

        VkPipelineRasterizationStateCreateInfo svRs = rs;   // FILL, cull-none
        svRs.depthBiasEnable = VK_TRUE;
        svRs.depthBiasConstantFactor = 1.25f;   // push casters slightly further from the
        svRs.depthBiasSlopeFactor    = 1.75f;   // light so self-shadow acne does not creep

        VkPipelineColorBlendStateCreateInfo svCb = {};   // depth-only: zero colour attachments
        svCb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        svCb.attachmentCount = 0;

        // shadow.vert consumes only position/UV/texnum/flags (no normal, no sector light),
        // so describe just those 4 attributes — same binding/stride, fewer locations — to
        // keep the validator quiet about unconsumed inputs.
        VkVertexInputAttributeDescription svAttrs[4] = { attrs[0], attrs[2], attrs[4], attrs[5] };
        VkPipelineVertexInputStateCreateInfo svVin = vin;
        svVin.vertexAttributeDescriptionCount = 4;
        svVin.pVertexAttributeDescriptions = svAttrs;

        VkGraphicsPipelineCreateInfo svPci = pci;
        svPci.pStages = svStages;
        svPci.pVertexInputState = &svVin;
        svPci.pRasterizationState = &svRs;
        svPci.pColorBlendState = &svCb;
        svPci.pDepthStencilState = &ds;
        svPci.layout = g.shadowPipeLayout;
        svPci.renderPass = g.shadowPass;
        svPci.subpass = 0;
        Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &svPci, nullptr,
                                        &g.shadowPipeline), "vkCreateGraphicsPipelines(shadow)");
        vkDestroyShaderModule(g.device, svVert, nullptr);
        vkDestroyShaderModule(g.device, svFrag, nullptr);
    }

    // DOOM-0170 L2d: blob-shadow decal pipeline. Reuses mesh.vert (world MVP path) + the
    // world pipeline layout/push, but blob.frag paints a soft dark oval and the state
    // ALPHA-BLENDS it onto the floor: depth test ON (a wall in front occludes it; it sits
    // on the floor it is emitted 1 unit above) but depth WRITE OFF, so blobs never occlude
    // the billboards or each other and only darken what is already drawn. Same g.scenePass.
    {
        VkShaderModule blVert = MakeShader(blob_vert_spv, blob_vert_spv_len);
        VkShaderModule blFrag = MakeShader(blob_frag_spv, blob_frag_spv_len);
        VkPipelineShaderStageCreateInfo blStages[2] = { stages[0], stages[1] };
        blStages[0].module = blVert;
        blStages[1].module = blFrag;

        // blob.vert consumes only position/UV/light; describe just those 3 attributes
        // (same binding/stride) so the validator sees no unconsumed vertex inputs.
        VkVertexInputAttributeDescription blAttrs[3] = { attrs[0], attrs[2], attrs[3] };
        VkPipelineVertexInputStateCreateInfo blVin = vin;
        blVin.vertexAttributeDescriptionCount = 3;
        blVin.pVertexAttributeDescriptions    = blAttrs;

        VkPipelineDepthStencilStateCreateInfo blDs = ds;
        blDs.depthWriteEnable = VK_FALSE;   // decals don't occlude; overlapping blobs blend

        // DOOM-0170 L2b — two attachments (AMBIENT + DIRECT), both the SAME alpha-blend state
        // (the independentBlend feature is not enabled, so all attachments must be identical).
        // The per-attachment effect is instead selected by blob.frag's per-output ALPHA: it
        // writes alpha = darkening on AMBIENT (a grounding shadow on the always-present sector
        // light, so blobs stay visible in unlit rooms) and alpha = 0 on DIRECT, which makes the
        // identical blend a no-op there — a blob never touches the flashlight/lamp light (the
        // L2c cast-shadow map owns that).
        VkPipelineColorBlendAttachmentState blAtt = {};
        blAtt.blendEnable         = VK_TRUE;
        blAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blAtt.colorBlendOp        = VK_BLEND_OP_ADD;   // out = 0*a + dst*(1-a) = darkened floor
        blAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
        blAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                  | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendAttachmentState blAtts[2] = { blAtt, blAtt };
        VkPipelineColorBlendStateCreateInfo blCb = {};
        blCb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blCb.attachmentCount  = 2;
        blCb.pAttachments     = blAtts;

        VkGraphicsPipelineCreateInfo blPci = pci;
        blPci.pStages             = blStages;
        blPci.pVertexInputState   = &blVin;   // trimmed to pos/uv/light
        blPci.pRasterizationState = &rs;    // FILL, cull-none
        blPci.pDepthStencilState  = &blDs;
        blPci.pColorBlendState    = &blCb;
        blPci.layout              = g.pipelineLayout;
        blPci.renderPass          = g.scenePass;
        blPci.subpass             = 0;
        Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &blPci, nullptr,
                                        &g.blobPipeline), "vkCreateGraphicsPipelines(blob)");
        vkDestroyShaderModule(g.device, blVert, nullptr);
        vkDestroyShaderModule(g.device, blFrag, nullptr);
    }

    // 2D HUD/menu compositor: own shaders, no vertex input (a full-screen
    // triangle generated from gl_VertexIndex), depth off, drawn last over the
    // 3D scene. Shares this pipeline layout (descriptor set 0 + push range),
    // sampling the overlay image (binding 1) and PLAYPAL LUT (binding 0).
    VkShaderModule ovVert = MakeShader(overlay_vert_spv, overlay_vert_spv_len);
    VkShaderModule ovFrag = MakeShader(overlay_frag_spv, overlay_frag_spv_len);
    VkPipelineShaderStageCreateInfo ovStages[2] = { stages[0], stages[1] };
    ovStages[0].module = ovVert;
    ovStages[1].module = ovFrag;
    VkPipelineVertexInputStateCreateInfo ovVin = {};
    ovVin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pci.pStages = ovStages;
    pci.pVertexInputState = &ovVin;        // vertexless
    pci.pDepthStencilState = &skyDs;       // depth test + write off
    Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &g.overlayPipeline), "vkCreateGraphicsPipelines(overlay)");
    vkDestroyShaderModule(g.device, ovVert, nullptr);
    vkDestroyShaderModule(g.device, ovFrag, nullptr);

    // DOOM-0170 L2a composite pipeline: vertexless full-screen tri (composite.vert) that
    // samples the off-screen scene target (composite.frag) into the swapchain via
    // renderPass. Depth off (reuse skyDs + the overlay's empty vertex input); binds only
    // g.compositePipeLayout (the 1-sampler composite set), not g.pipelineLayout.
    VkShaderModule coVert = MakeShader(composite_vert_spv, composite_vert_spv_len);
    VkShaderModule coFrag = MakeShader(composite_frag_spv, composite_frag_spv_len);
    VkPipelineShaderStageCreateInfo coStages[2] = { ovStages[0], ovStages[1] };
    coStages[0].module = coVert;
    coStages[1].module = coFrag;
    pci.pStages = coStages;
    pci.pVertexInputState = &ovVin;         // vertexless
    pci.pDepthStencilState = &skyDs;        // depth test + write off
    pci.layout = g.compositePipeLayout;
    Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &g.compositePipeline), "vkCreateGraphicsPipelines(composite)");

    // DOOM-0170 L2b — the SSAO pass pipeline: reuses composite.vert (the same full-screen
    // triangle) with ssao.frag, into the half-res g.aoPass, binding only g.aoPipeLayout (the
    // 1-sampler SSAO set + its push block). Vertexless, depth off, 1 opaque R8 attachment.
    VkShaderModule ssaoFrag = MakeShader(ssao_frag_spv, ssao_frag_spv_len);
    VkPipelineShaderStageCreateInfo aoStages[2] = { coStages[0], coStages[1] };
    aoStages[1].module = ssaoFrag;
    pci.pStages = aoStages;
    pci.pVertexInputState = &ovVin;         // vertexless
    pci.pDepthStencilState = &skyDs;        // depth test + write off
    pci.pColorBlendState = &cb;             // single opaque attachment
    pci.layout = g.aoPipeLayout;
    pci.renderPass = g.aoPass;
    Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &g.aoPipeline), "vkCreateGraphicsPipelines(ssao)");
    vkDestroyShaderModule(g.device, ssaoFrag, nullptr);

    pci.renderPass = g.renderPass;          // restore (defensive)
    pci.layout = g.pipelineLayout;          // restore (defensive)
    vkDestroyShaderModule(g.device, coVert, nullptr);
    vkDestroyShaderModule(g.device, coFrag, nullptr);

    vkDestroyShaderModule(g.device, vert, nullptr);
    vkDestroyShaderModule(g.device, frag, nullptr);
    vkDestroyShaderModule(g.device, fragOverlay, nullptr);   // DOOM-0170 L2b
}

// Destroy the size-dependent resources (framebuffers, depth, image views) so
// the swapchain can be rebuilt on a resize. Render pass + pipeline are size-
// independent (viewport/scissor are dynamic) and outlive a recreate.
void DestroyFramebufferResources()
{
    for (VkFramebuffer fb : g.framebuffers)
        vkDestroyFramebuffer(g.device, fb, nullptr);
    g.framebuffers.clear();

    if (g.depthView)   { vkDestroyImageView(g.device, g.depthView, nullptr); g.depthView = VK_NULL_HANDLE; }
    if (g.depthImage)  { vkDestroyImage(g.device, g.depthImage, nullptr);    g.depthImage = VK_NULL_HANDLE; }
    if (g.depthMemory) { vkFreeMemory(g.device, g.depthMemory, nullptr);     g.depthMemory = VK_NULL_HANDLE; }

    for (VkImageView v : g.imageViews)
        vkDestroyImageView(g.device, v, nullptr);
    g.imageViews.clear();

    DestroySceneTarget();   // DOOM-0170 L2a off-screen colour + its framebuffer
}

void RecreateSwapchain()
{
    vkDeviceWaitIdle(g.device);
    DestroyFramebufferResources();
    if (g.rtEnabled) DestroyRtTargets();   // swapchain-sized; rebuilt below
    CreateSwapchain();   // reuses g.swapchain as oldSwapchain, then replaces it
    CreateImageViews();
    CreateDepthResources();
    CreateSceneTarget();                // DOOM-0170 L2a off-screen colour (before its framebuffer)
    CreateFramebuffers();
    UpdateCompositeDescriptor();        // re-point the composite sampler at the new scene view
    CreateRenderFinishedSemaphores();   // image count may have changed; resize set
    if (g.rtEnabled) CreateRtTargets();    // re-point the compute descriptor too
}

// Build the paletted texture atlas (r_mesh.c), upload the atlas + PLAYPAL LUT
// images and the per-id rect table, and write descriptor set 0. WAD-global, so
// this runs once on the first level build and is reused thereafter.
// Persistently-mapped, host-visible vertex buffer for the per-frame billboard
// sprites. Sized once for a generous cap; RB_BuildSprites refills it each frame.
void CreateSpriteBuffer()
{
    g.spriteVertCap = 4096 * 6;   // up to ~4096 things per frame, 6 verts each
    VkDeviceSize size = (VkDeviceSize)g.spriteVertCap * sizeof(rb_vertex_t);

    // DOOM-0074: one copy per in-flight slot so the next frame's build-ahead billboard
    // fill can't clobber the copy the GPU is still drawing from.
    for (uint32_t s = 0; s < VulkanState::kFramesInFlight; s++)
    {
        VkBufferCreateInfo bci = {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Check(vkCreateBuffer(g.device, &bci, nullptr, &g.spriteVbufSlot[s]), "vkCreateBuffer(sprites)");

        VkMemoryRequirements req = {};
        vkGetBufferMemoryRequirements(g.device, g.spriteVbufSlot[s], &req);
        VkMemoryAllocateInfo mai = {};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(g.device, &mai, nullptr, &g.spriteVbufMemSlot[s]), "vkAllocateMemory(sprites)");
        Check(vkBindBufferMemory(g.device, g.spriteVbufSlot[s], g.spriteVbufMemSlot[s], 0), "vkBindBufferMemory(sprites)");
        Check(vkMapMemory(g.device, g.spriteVbufMemSlot[s], 0, size, 0, &g.spriteMappedSlot[s]), "vkMapMemory(sprites)");
    }
    // Point the g.<name> aliases at the active slot so a frame drawn before the first
    // RB_Vulkan_Present re-point has valid handles.
    g.spriteVbuf       = g.spriteVbufSlot[g.frameSlot];
    g.spriteVbufMemory = g.spriteVbufMemSlot[g.frameSlot];
    g.spriteMapped     = g.spriteMappedSlot[g.frameSlot];
}

// Build the WAD-global PLAYPAL colour LUT and the descriptor set at init, BEFORE
// any level/atlas exists, so the 2D HUD/menu overlay composites from the very
// first frame (the title/demo screen in Solid/Ultra) instead of only after a
// level is built (DOOM-0045). The set's bindless material array (binding 2) is
// filled later by UploadAtlas; the overlay image (binding 1) by
// CreateOverlayResources. overlay.frag samples only the palette (0) and overlay
// (1) bindings, so the title-screen draw is valid with the material array (2)
// still unwritten — its PARTIALLY_BOUND flag makes that legal.
// Needs the command pool (CreateSampledImage stages through a one-time buffer),
// so RB_Vulkan_Init calls this after CreateCommandsAndSync.
void InitPaletteAndDescriptorSet()
{
    // PLAYPAL as a 256x1 RGBA LUT (UNORM: raw palette colours, decoded straight
    // like the world path — no extra colour conversion). Sourced from the cached
    // WAD lump via the C seam, so no level/atlas is needed.
    const unsigned char* playpal = RB_PlayPal();
    unsigned char lut[256 * 4];
    for (int i = 0; i < 256; ++i)
    {
        lut[i * 4 + 0] = playpal[i * 3 + 0];
        lut[i * 4 + 1] = playpal[i * 3 + 1];
        lut[i * 4 + 2] = playpal[i * 3 + 2];
        lut[i * 4 + 3] = 255;
    }
    CreateSampledImage(256, 1, VK_FORMAT_R8G8B8A8_UNORM, lut, sizeof(lut),
                       &g.palImage, &g.palMemory, &g.palView);

    // Descriptor pool + set: every binding is a combined-image-sampler — PLAYPAL
    // (1) + HUD overlay (1) + the bindless material array (N). The material slots
    // are reserved now (variable-count = the WAD's material total) but written
    // when the atlas is built (UploadAtlas); the overlay (binding 1) arrives via
    // CreateOverlayResources. PLAYPAL (binding 0) is written here so the title
    // screen composites from the first frame.
    const uint32_t matCount = (uint32_t)RB_MaterialCount();

    VkDescriptorPoolSize sizes[1] = {};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[0].descriptorCount = 2 + matCount;
    VkDescriptorPoolCreateInfo dpci = {};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = sizes;
    Check(vkCreateDescriptorPool(g.device, &dpci, nullptr, &g.dsPool), "vkCreateDescriptorPool");

    // The variable-count binding (2) needs its actual size declared at allocation.
    VkDescriptorSetVariableDescriptorCountAllocateInfo varCount = {};
    varCount.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    varCount.descriptorSetCount = 1;
    varCount.pDescriptorCounts = &matCount;

    VkDescriptorSetAllocateInfo dsai = {};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.pNext = &varCount;
    dsai.descriptorPool = g.dsPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g.dsLayout;
    Check(vkAllocateDescriptorSets(g.device, &dsai, &g.ds), "vkAllocateDescriptorSets");

    // Write the palette LUT (binding 0) now; the overlay needs only this + the
    // overlay image (binding 1), so the title screen composites immediately.
    VkDescriptorImageInfo palInfo = { g.texSampler, g.palView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = g.ds; write.dstBinding = 0; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &palInfo;
    vkUpdateDescriptorSets(g.device, 1, &write, 0, nullptr);
}

// DOOM-0183 L2: forced-constant glow Le (linear RGB) for the liquid flats. Start faint —
// the goo should TINT a room, not floodlight it (§4.3); tune on hardware (DOOM-0193 look).
static const float kNukageLe[3] = { 0.35f, 1.30f, 0.15f };   // green toxic-sludge glow
static const float kLavaLe[3]   = { 2.20f, 0.75f, 0.12f };   // hot orange lava glow

// DOOM-0183 L2: force a guaranteed, tunable emissive Le on the nukage/lava flats by NAME,
// OVERWRITING whatever the peak-gated derive produced. This is the whole cast-light
// mechanism (INV-7): a material with Le>0 enters the NEE emitter set (BuildStaticEmitterSet)
// and self-glows on the primary ray, with no new light type. Delivers DOOM-0083. The flat's
// unified id is numWall + (lump - firstflat), matching the textured-decode id ordering.
static void ForceLiquidEmissive(const rb_atlas_t* a, std::vector<float>& out)
{
    const struct { const char* name; const float* le; } lut[] = {
        { "NUKAGE1", kNukageLe }, { "NUKAGE2", kNukageLe }, { "NUKAGE3", kNukageLe },
        { "LAVA1", kLavaLe }, { "LAVA2", kLavaLe }, { "LAVA3", kLavaLe }, { "LAVA4", kLavaLe },
    };
    for (const auto& e : lut) {
        char nm[9]; strncpy(nm, e.name, 8); nm[8] = '\0';
        int lump = W_CheckNumForName(nm);
        if (lump < 0) continue;
        int fi = lump - firstflat;
        if (fi < 0 || fi >= a->numflat) continue;
        int id = a->numwall + fi;
        if ((size_t)(id * 3 + 2) < out.size()) {
            out[id * 3 + 0] = e.le[0];
            out[id * 3 + 1] = e.le[1];
            out[id * 3 + 2] = e.le[2];
        }
    }
}

// Fill out[3*n] with each material's emitted radiance Le (linear RGB). The per-tile
// derivation (bright-texel colour + the near-fullbright peak-region emitter gate)
// lives in emissive_derive.h so it is shared with tests/emissive_derive_test.cpp;
// this just decodes the palette once and walks every atlas tile through it. The id
// ordering matches RB_BuildAtlas (walls, then flats, then sprites), so the shader
// indexes Le by the same unified id the textured decode uses.
static void ComputeMaterialEmissive(const rb_atlas_t* a, std::vector<float>& out)
{
    const int n = a->numwall + a->numflat + a->numsprite;
    out.assign((size_t)n * 3, 0.0f);

    // Pre-decode the 256 palette entries to linear RGB once (not per texel).
    float palLin[256][3];
    for (int i = 0; i < 256; i++)
    {
        palLin[i][0] = emis::srgb2lin(a->playpal[i * 3 + 0] / 255.0f);
        palLin[i][1] = emis::srgb2lin(a->playpal[i * 3 + 1] / 255.0f);
        palLin[i][2] = emis::srgb2lin(a->playpal[i * 3 + 2] / 255.0f);
    }

    const int spriteBase = a->numwall + a->numflat;
    for (int id = 0; id < n; id++)
    {
        const int ox = (int)a->rects[id].ox, oy = (int)a->rects[id].oy;
        const int w  = (int)a->rects[id].w,  h  = (int)a->rects[id].h;
        // DOOM-0157: a glowing-collectible sprite lump (skull eyes, armour gleam) has
        // too few bright texels to clear the room-lighting peak gate, so grant it a
        // faint self-emission floor; walls/flats and other sprites keep the strict gate.
        const bool faint = id >= spriteBase &&
                           RB_SpriteLumpGlows(id - spriteBase) != 0;
        emis::derive_material_le(a->pixels, (int)a->atlasw, ox, oy, w, h,
                                 palLin, &out[(size_t)id * 3], faint);
    }

    ForceLiquidEmissive(a, out);   // DOOM-0183 L2: forced glow Le on nukage/lava (delivers DOOM-0083)
}

void UploadAtlas()
{
    // Reuse the Stage-1 packer purely as a pixel source: it composites each
    // material (walls, flats, sprites) into one packed buffer and hands back each
    // tile's origin+size. We re-cut every tile into its own R8 image for the
    // bindless array; the packed image itself is never uploaded.
    rb_atlas_t* a = RB_BuildAtlas();
    int n = a->numwall + a->numflat + a->numsprite;
    printf("RB_Vulkan: %d bindless materials (%d walls + %d flats + %d sprites).\n",
           n, a->numwall, a->numflat, a->numsprite);
    fflush(stdout);

    g.matNumWall = a->numwall;
    g.matNumFlat = a->numflat;
    g.matImages.resize(n);
    g.matViews.resize(n);

    // One staging buffer holds every tile's palette indices back to back, so all
    // N copies ride a single command submission — a per-image submit+wait would be
    // thousands of GPU stalls at level load. Each tile's offset is 4-byte aligned
    // (vkCmdCopyBufferToImage requires bufferOffset % 4 == 0).
    std::vector<VkDeviceSize> texOffset(n);
    VkDeviceSize stageBytes = 0;
    for (int i = 0; i < n; i++)
    {
        texOffset[i] = stageBytes;
        VkDeviceSize sz = (VkDeviceSize)(int)a->rects[i].w * (int)a->rects[i].h;
        stageBytes += (sz + 3) & ~(VkDeviceSize)3;
    }

    VkBufferCreateInfo sbci = {};
    sbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbci.size = stageBytes;
    sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    Check(vkCreateBuffer(g.device, &sbci, nullptr, &staging), "vkCreateBuffer(mat staging)");

    VkMemoryRequirements sreq = {};
    vkGetBufferMemoryRequirements(g.device, staging, &sreq);
    VkMemoryAllocateInfo smai = {};
    smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    smai.allocationSize = sreq.size;
    smai.memoryTypeIndex = FindMemoryType(sreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    Check(vkAllocateMemory(g.device, &smai, nullptr, &stagingMem), "vkAllocateMemory(mat staging)");
    Check(vkBindBufferMemory(g.device, staging, stagingMem, 0), "vkBindBufferMemory(mat staging)");

    // Cut each tile out of the packed atlas, row by row, into its staging slot.
    unsigned char* sp = nullptr;
    Check(vkMapMemory(g.device, stagingMem, 0, stageBytes, 0, (void**)&sp), "vkMapMemory(mat staging)");
    for (int i = 0; i < n; i++)
    {
        int ox = (int)a->rects[i].ox, oy = (int)a->rects[i].oy;
        int w  = (int)a->rects[i].w,  h  = (int)a->rects[i].h;
        unsigned char* dst = sp + texOffset[i];
        for (int row = 0; row < h; row++)
            std::memcpy(dst + (size_t)row * w,
                        a->pixels + (size_t)(oy + row) * a->atlasw + ox,
                        (size_t)w);
    }
    vkUnmapMemory(g.device, stagingMem);

    // Create all N images, then back them with ONE device allocation (a minimal
    // manual sub-allocator: each image binds at its own aligned offset). This
    // keeps the per-allocation count at 1 instead of N, well clear of the driver's
    // limit on big WADs; VMA does this properly in a later increment.
    VkDeviceSize memBytes = 0;
    std::vector<VkDeviceSize> imgOffset(n);
    uint32_t memTypeBits = 0xffffffffu;
    for (int i = 0; i < n; i++)
    {
        VkImageCreateInfo ici = {};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R8_UNORM;
        ici.extent = { (uint32_t)(int)a->rects[i].w, (uint32_t)(int)a->rects[i].h, 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Check(vkCreateImage(g.device, &ici, nullptr, &g.matImages[i]), "vkCreateImage(material)");

        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(g.device, g.matImages[i], &req);
        memBytes = (memBytes + req.alignment - 1) & ~(req.alignment - 1);
        imgOffset[i] = memBytes;
        memBytes += req.size;
        memTypeBits &= req.memoryTypeBits;
    }

    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memBytes;
    mai.memoryTypeIndex = FindMemoryType(memTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.matMemory), "vkAllocateMemory(materials)");
    for (int i = 0; i < n; i++)
        Check(vkBindImageMemory(g.device, g.matImages[i], g.matMemory, imgOffset[i]),
              "vkBindImageMemory(material)");

    // One command buffer: all UNDEFINED->TRANSFER_DST barriers, all copies, then
    // all TRANSFER_DST->SHADER_READ barriers.
    std::vector<VkImageMemoryBarrier> toDst(n), toRead(n);
    for (int i = 0; i < n; i++)
    {
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = g.matImages[i];
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst[i] = b;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRead[i] = b;
    }

    VkCommandBuffer cb = BeginOneTime();
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, (uint32_t)n, toDst.data());
    for (int i = 0; i < n; i++)
    {
        VkBufferImageCopy region = {};
        region.bufferOffset = texOffset[i];
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { (uint32_t)(int)a->rects[i].w, (uint32_t)(int)a->rects[i].h, 1 };
        vkCmdCopyBufferToImage(cb, staging, g.matImages[i],
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, (uint32_t)n, toRead.data());
    EndOneTime(cb);

    vkDestroyBuffer(g.device, staging, nullptr);
    vkFreeMemory(g.device, stagingMem, nullptr);

    // Image views + one array write filling the bindless binding (2) slots [0,n).
    std::vector<VkDescriptorImageInfo> infos(n);
    for (int i = 0; i < n; i++)
    {
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = g.matImages[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = VK_FORMAT_R8_UNORM;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(g.device, &vci, nullptr, &g.matViews[i]), "vkCreateImageView(material)");
        infos[i] = { g.texSampler, g.matViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    }
    VkWriteDescriptorSet warr = {};
    warr.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    warr.dstSet = g.ds;
    warr.dstBinding = 2;
    warr.dstArrayElement = 0;
    warr.descriptorCount = (uint32_t)n;
    warr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    warr.pImageInfo = infos.data();
    vkUpdateDescriptorSets(g.device, 1, &warr, 0, nullptr);

    // Per-material emission table (DOOM-0009 build step 3b). Computed from the
    // packed atlas (needs a->pixels/playpal, freed just below) into a CPU mirror the
    // per-level emitter extraction reads, plus a GPU device-address buffer the
    // megakernel samples for a hit surface's own self-emission. RT-only; WAD-global,
    // built once with the material array.
    if (g.rtEnabled)
    {
        ComputeMaterialEmissive(a, g.matEmissive);
        UploadAddressBuffer(g.matEmissive.data(),
                            (VkDeviceSize)g.matEmissive.size() * sizeof(float),
                            &g.matEmisBuf, &g.matEmisMem);
        int emis = 0;
        for (size_t i = 0; i < g.matEmissive.size(); i += 3)
            if (g.matEmissive[i] + g.matEmissive[i + 1] + g.matEmissive[i + 2] > 0.0f)
                emis++;
        printf("RB_Vulkan: %d emissive materials (of %d).\n", emis, n);
        fflush(stdout);
    }

    CreateSpriteBuffer();   // per-frame billboard buffer (uses the material array)

    RB_FreeAtlas(a);
    g.atlasReady = true;

    // DOOM-0042: the material array is live -> seed set 3 with an all-paletted default so
    // every RT dispatch (verify + first frame) has a valid set 3 before a level's HD maps
    // load. EnsureHdMaterials replaces it per level in Ultra.
    InitHdDefault();
}

// Create the HUD/menu overlay's GPU resources (device-local R8 image + a
// persistently-mapped staging buffer) sized to the engine's screens[0], and
// point descriptor binding 1 at it. Lazy and one-shot: called the first time an
// overlay arrives (RB_Vulkan_SetOverlay) once the descriptor set exists. The
// image is left UNDEFINED here; the per-frame copy in Present fills + transitions
// it. screens[0] is a fixed size for the session, so this runs exactly once.
void CreateOverlayResources(int w, int h)
{
    VkDeviceSize bytes = (VkDeviceSize)w * h;

    // Persistent host-visible staging buffer, mapped for the whole session.
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(g.device, &bci, nullptr, &g.overlayStaging), "vkCreateBuffer(overlay)");
    VkMemoryRequirements sreq = {};
    vkGetBufferMemoryRequirements(g.device, g.overlayStaging, &sreq);
    VkMemoryAllocateInfo smai = {};
    smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    smai.allocationSize = sreq.size;
    smai.memoryTypeIndex = FindMemoryType(sreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(g.device, &smai, nullptr, &g.overlayStagingMem), "vkAllocateMemory(overlay staging)");
    Check(vkBindBufferMemory(g.device, g.overlayStaging, g.overlayStagingMem, 0), "vkBindBufferMemory(overlay)");
    Check(vkMapMemory(g.device, g.overlayStagingMem, 0, bytes, 0, &g.overlayMapped), "vkMapMemory(overlay)");

    // Device-local R8 image (palette indices), sampled by overlay.frag.
    VkImageCreateInfo ici = {};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8_UNORM;
    ici.extent = { (uint32_t)w, (uint32_t)h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    Check(vkCreateImage(g.device, &ici, nullptr, &g.overlayImage), "vkCreateImage(overlay)");
    VkMemoryRequirements ireq = {};
    vkGetImageMemoryRequirements(g.device, g.overlayImage, &ireq);
    VkMemoryAllocateInfo imai = {};
    imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imai.allocationSize = ireq.size;
    imai.memoryTypeIndex = FindMemoryType(ireq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &imai, nullptr, &g.overlayMemory), "vkAllocateMemory(overlay image)");
    Check(vkBindImageMemory(g.device, g.overlayImage, g.overlayMemory, 0), "vkBindImageMemory(overlay)");

    VkImageViewCreateInfo vci = {};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = g.overlayImage;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = VK_FORMAT_R8_UNORM;
    vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    Check(vkCreateImageView(g.device, &vci, nullptr, &g.overlayView), "vkCreateImageView(overlay)");

    // Point descriptor binding 1 at the overlay image (the set already exists).
    VkDescriptorImageInfo ovInfo = { g.texSampler, g.overlayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet write = {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = g.ds; write.dstBinding = 1; write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &ovInfo;
    vkUpdateDescriptorSets(g.device, 1, &write, 0, nullptr);

    g.overlayReady = true;
}

// DOOM-0206 (L4): bake the menu glyph atlas and build the display-resolution text pipeline.
// Called once from RB_Vulkan_Init after InitPaletteAndDescriptorSet (so the command pool +
// g.renderPass exist). The font is the bundled Oxanium SemiBold (OFL), embedded as the
// oxanium_ttf[] byte array (assets/Oxanium-SemiBold.ttf.h) so the game ships self-contained —
// no system font dependency. If the bake fails the whole path is left disabled (menuFontReady
// stays false) and the game runs without crisp text — it is a menu-only overlay.
void CreateTextResources()
{
    // Glyph pixel height scaled to the display so text stays crisp at any resolution (~24 at
    // 1080p, ~48 at 2160p), floored at 24 so low-res stays legible.
    int px = (int)g.extent.height / 45;
    if (px < 24) px = 24;
    int baked = rb_text_bake(oxanium_ttf, (int)oxanium_ttf_len, px, &g.menuFont);
    printf("RB_Vulkan: menu font = Oxanium SemiBold (bundled OFL), %u bytes, glyph px=%d\n",
           oxanium_ttf_len, px);
    fflush(stdout);
    if (!baked)
    {
        fprintf(stderr, "RB_Vulkan: menu font bake failed — crisp menu text disabled.\n");
        return;
    }

    // Reserve atlas texel (0,0) = full coverage: rb_menu_dim draws its solid dark quad by
    // sampling it. stbtt leaves row 0 / column 0 empty, so this clobbers no glyph.
    g.menuFont.pixels[0] = 255;

    // Upload the R8 atlas once (static for the whole session), then free the CPU pixels —
    // rb_text_free_font keeps the glyph metrics rb_text_draw / rb_text_measure still need.
    CreateSampledImage((uint32_t)g.menuFont.w, (uint32_t)g.menuFont.h, VK_FORMAT_R8_UNORM,
                       g.menuFont.pixels, (VkDeviceSize)g.menuFont.w * g.menuFont.h,
                       &g.textAtlas, &g.textAtlasMemory, &g.textAtlasView);
    rb_text_free_font(&g.menuFont);

    // Linear + clamp sampler: the atlas is baked at ~display glyph size (scale ~1), so linear
    // gives smooth edges without the paletted art's nearest blockiness.
    VkSamplerCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    Check(vkCreateSampler(g.device, &sci, nullptr, &g.textSampler), "vkCreateSampler(text)");

    // Descriptor set: one combined-image-sampler (the atlas), its own layout/pool/set.
    VkDescriptorSetLayoutBinding b = {};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 1;
    dlci.pBindings = &b;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.textDsLayout),
          "vkCreateDescriptorSetLayout(text)");

    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
    VkDescriptorPoolCreateInfo dpci = {};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &ps;
    Check(vkCreateDescriptorPool(g.device, &dpci, nullptr, &g.textDsPool),
          "vkCreateDescriptorPool(text)");

    VkDescriptorSetAllocateInfo dsai = {};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = g.textDsPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &g.textDsLayout;
    Check(vkAllocateDescriptorSets(g.device, &dsai, &g.textDs),
          "vkAllocateDescriptorSets(text)");

    VkDescriptorImageInfo aInfo = { g.textSampler, g.textAtlasView,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet w = {};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = g.textDs; w.dstBinding = 0; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &aInfo;
    vkUpdateDescriptorSets(g.device, 1, &w, 0, nullptr);

    // Pipeline layout: the atlas set + a vec2 invDisplay push constant (vertex stage).
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = 2 * sizeof(float);
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g.textDsLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.textPipelineLayout),
          "vkCreatePipelineLayout(text)");

    // Graphics pipeline: textured quad, alpha blend ON, depth off, into g.renderPass (the
    // 8-bit swapchain pass; format-compatible with g.rtOverlayPass, so this one pipeline draws
    // in BOTH the raster and the RT-overlay present paths).
    VkShaderModule vert = MakeShader(text_vert_spv, text_vert_spv_len);
    VkShaderModule frag = MakeShader(text_frag_spv, text_frag_spv_len);
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = vert; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

    VkVertexInputBindingDescription bind = {};
    bind.binding = 0;
    bind.stride = sizeof(TextVertex);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(TextVertex, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(TextVertex, u) };
    attrs[2] = { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(TextVertex, r) };
    VkPipelineVertexInputStateCreateInfo vin = {};
    vin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vin.vertexBindingDescriptionCount = 1;
    vin.pVertexBindingDescriptions = &bind;
    vin.vertexAttributeDescriptionCount = 3;
    vin.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1; vp.scissorCount = 1;   // viewport + scissor dynamic

    VkPipelineRasterizationStateCreateInfo rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dsst = {};   // depth test + write off (2D over all)
    dsst.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    // Premultiplied-alpha blend: text.frag outputs (rgb*a, a), so src factor is ONE.
    VkPipelineColorBlendAttachmentState cba = {};
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                       | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState = {};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pci.stageCount = 2;
    pci.pStages = stages;
    pci.pVertexInputState = &vin;
    pci.pInputAssemblyState = &ia;
    pci.pViewportState = &vp;
    pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms;
    pci.pDepthStencilState = &dsst;
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dynState;
    pci.layout = g.textPipelineLayout;
    pci.renderPass = g.renderPass;
    pci.subpass = 0;
    Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &pci, nullptr,
                                    &g.textPipeline), "vkCreateGraphicsPipelines(text)");
    vkDestroyShaderModule(g.device, vert, nullptr);
    vkDestroyShaderModule(g.device, frag, nullptr);

    // Per-frame glyph-quad vertex buffer (host-visible, persistently mapped). Single copy:
    // FlushMenuText memcpys + draws it after the fence, so no in-flight double-buffering.
    g.textVbufCap = 4096 * 6;   // up to ~4096 glyphs/frame, 6 verts each
    VkDeviceSize vbytes = (VkDeviceSize)g.textVbufCap * sizeof(TextVertex);
    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = vbytes;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(g.device, &bci, nullptr, &g.textVbuf), "vkCreateBuffer(text)");
    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(g.device, g.textVbuf, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.textVbufMemory), "vkAllocateMemory(text)");
    Check(vkBindBufferMemory(g.device, g.textVbuf, g.textVbufMemory, 0), "vkBindBufferMemory(text)");
    Check(vkMapMemory(g.device, g.textVbufMemory, 0, vbytes, 0, &g.textVbufMapped), "vkMapMemory(text)");

    g.menuFontReady = true;
    printf("RB_Vulkan: menu text ready (%dpx glyphs, %dx%d atlas).\n",
           g.menuFont.px_height, g.menuFont.w, g.menuFont.h);
    fflush(stdout);

    // DOOM-0206 v2: the crisp menu skull cursor. Decode the real WAD M_SKULL1 lump to a
    // brightened RGBA buffer (M_CursorSkullRGBA, m_menu.c), upload it as an RGBA texture, and
    // build a second pipeline that reuses the text vertex format + layout but samples RGBA
    // (cursor.frag) instead of the R8 glyph atlas. On any failure cursorReady stays false and
    // m_menu falls back to the paletted skull.
    {
        int sw = 0, sh = 0;
        const unsigned char* pixels = M_CursorSkullRGBA(&sw, &sh);
        if (!pixels || sw <= 0 || sh <= 0)
        {
            fprintf(stderr, "RB_Vulkan: menu cursor skull decode failed — using the paletted skull.\n");
        }
        else
        {
            // Upload the RGBA skull once (static for the session), same helper as the atlas.
            CreateSampledImage((uint32_t)sw, (uint32_t)sh, VK_FORMAT_R8G8B8A8_UNORM,
                               pixels, (VkDeviceSize)sw * sh * 4,
                               &g.cursorImage, &g.cursorMemory, &g.cursorView);

            // Descriptor set: one combined-image-sampler (the skull), its own pool; reuses the
            // text DS layout (identical binding 0 = sampler2D at fragment stage) and sampler.
            VkDescriptorPoolSize cps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
            VkDescriptorPoolCreateInfo cdpci = {};
            cdpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            cdpci.maxSets = 1;
            cdpci.poolSizeCount = 1;
            cdpci.pPoolSizes = &cps;
            Check(vkCreateDescriptorPool(g.device, &cdpci, nullptr, &g.cursorDsPool),
                  "vkCreateDescriptorPool(cursor)");

            VkDescriptorSetAllocateInfo cdsai = {};
            cdsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            cdsai.descriptorPool = g.cursorDsPool;
            cdsai.descriptorSetCount = 1;
            cdsai.pSetLayouts = &g.textDsLayout;
            Check(vkAllocateDescriptorSets(g.device, &cdsai, &g.cursorDs),
                  "vkAllocateDescriptorSets(cursor)");

            VkDescriptorImageInfo cInfo = { g.textSampler, g.cursorView,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet cw = {};
            cw.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cw.dstSet = g.cursorDs; cw.dstBinding = 0; cw.descriptorCount = 1;
            cw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            cw.pImageInfo = &cInfo;
            vkUpdateDescriptorSets(g.device, 1, &cw, 0, nullptr);

            // Pipeline: identical to the text pipeline except the fragment shader samples RGBA.
            // Same TextVertex input, same premultiplied-alpha blend, same layout + render pass.
            VkShaderModule cvert = MakeShader(text_vert_spv, text_vert_spv_len);
            VkShaderModule cfrag = MakeShader(cursor_frag_spv, cursor_frag_spv_len);
            VkPipelineShaderStageCreateInfo cstages[2] = {};
            cstages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cstages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   cstages[0].module = cvert; cstages[0].pName = "main";
            cstages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cstages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; cstages[1].module = cfrag; cstages[1].pName = "main";

            VkVertexInputBindingDescription cbind = {};
            cbind.binding = 0;
            cbind.stride = sizeof(TextVertex);
            cbind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            VkVertexInputAttributeDescription cattrs[3] = {};
            cattrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(TextVertex, x) };
            cattrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(TextVertex, u) };
            cattrs[2] = { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(TextVertex, r) };
            VkPipelineVertexInputStateCreateInfo cvin = {};
            cvin.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            cvin.vertexBindingDescriptionCount = 1;
            cvin.pVertexBindingDescriptions = &cbind;
            cvin.vertexAttributeDescriptionCount = 3;
            cvin.pVertexAttributeDescriptions = cattrs;

            VkPipelineInputAssemblyStateCreateInfo cia = {};
            cia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            cia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkPipelineViewportStateCreateInfo cvp = {};
            cvp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            cvp.viewportCount = 1; cvp.scissorCount = 1;   // viewport + scissor dynamic

            VkPipelineRasterizationStateCreateInfo crs = {};
            crs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            crs.polygonMode = VK_POLYGON_MODE_FILL;
            crs.cullMode = VK_CULL_MODE_NONE;
            crs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            crs.lineWidth = 1.0f;

            VkPipelineMultisampleStateCreateInfo cms = {};
            cms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            cms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

            VkPipelineDepthStencilStateCreateInfo cdsst = {};   // depth test + write off
            cdsst.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

            // Premultiplied-alpha blend: cursor.frag outputs (rgb*a, a), so src factor is ONE.
            VkPipelineColorBlendAttachmentState ccba = {};
            ccba.blendEnable = VK_TRUE;
            ccba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            ccba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ccba.colorBlendOp = VK_BLEND_OP_ADD;
            ccba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            ccba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            ccba.alphaBlendOp = VK_BLEND_OP_ADD;
            ccba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            VkPipelineColorBlendStateCreateInfo ccb = {};
            ccb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            ccb.attachmentCount = 1;
            ccb.pAttachments = &ccba;

            VkDynamicState cdyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
            VkPipelineDynamicStateCreateInfo cdynState = {};
            cdynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            cdynState.dynamicStateCount = 2;
            cdynState.pDynamicStates = cdyn;

            VkGraphicsPipelineCreateInfo cpci = {};
            cpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            cpci.stageCount = 2;
            cpci.pStages = cstages;
            cpci.pVertexInputState = &cvin;
            cpci.pInputAssemblyState = &cia;
            cpci.pViewportState = &cvp;
            cpci.pRasterizationState = &crs;
            cpci.pMultisampleState = &cms;
            cpci.pDepthStencilState = &cdsst;
            cpci.pColorBlendState = &ccb;
            cpci.pDynamicState = &cdynState;
            cpci.layout = g.textPipelineLayout;
            cpci.renderPass = g.renderPass;
            cpci.subpass = 0;
            Check(vkCreateGraphicsPipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr,
                                            &g.cursorPipeline), "vkCreateGraphicsPipelines(cursor)");
            vkDestroyShaderModule(g.device, cvert, nullptr);
            vkDestroyShaderModule(g.device, cfrag, nullptr);

            g.cursorReady = true;
            g.cursorW = sw;
            g.cursorH = sh;
            printf("RB_Vulkan: menu cursor = real WAD skull M_SKULL1 (%dx%d RGBA, crisp).\n", sw, sh);
            fflush(stdout);
        }
    }

    // DOOM-0206: the real M_DOOM logo lump, a SECOND RGBA menu sprite (main-menu crisp title).
    // Only a texture + descriptor are new — it reuses g.cursorPipeline (identical RGBA sampling)
    // and the text layout/sampler. On any failure logoReady stays false and the main menu falls
    // back to the crisp "DOOM" text title.
    if (g.cursorReady)   // cursorPipeline exists only if the cursor block succeeded
    {
        int lw = 0, lh = 0;
        const unsigned char* pixels = M_MenuLogoRGBA(&lw, &lh);
        if (!pixels || lw <= 0 || lh <= 0)
        {
            fprintf(stderr, "RB_Vulkan: menu logo M_DOOM decode failed — using the crisp text title.\n");
        }
        else
        {
            CreateSampledImage((uint32_t)lw, (uint32_t)lh, VK_FORMAT_R8G8B8A8_UNORM,
                               pixels, (VkDeviceSize)lw * lh * 4,
                               &g.logoImage, &g.logoMemory, &g.logoView);

            VkDescriptorPoolSize lps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
            VkDescriptorPoolCreateInfo ldpci = {};
            ldpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            ldpci.maxSets = 1;
            ldpci.poolSizeCount = 1;
            ldpci.pPoolSizes = &lps;
            Check(vkCreateDescriptorPool(g.device, &ldpci, nullptr, &g.logoDsPool),
                  "vkCreateDescriptorPool(logo)");

            VkDescriptorSetAllocateInfo ldsai = {};
            ldsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            ldsai.descriptorPool = g.logoDsPool;
            ldsai.descriptorSetCount = 1;
            ldsai.pSetLayouts = &g.textDsLayout;
            Check(vkAllocateDescriptorSets(g.device, &ldsai, &g.logoDs),
                  "vkAllocateDescriptorSets(logo)");

            VkDescriptorImageInfo lInfo = { g.textSampler, g.logoView,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet lw2 = {};
            lw2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            lw2.dstSet = g.logoDs; lw2.dstBinding = 0; lw2.descriptorCount = 1;
            lw2.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            lw2.pImageInfo = &lInfo;
            vkUpdateDescriptorSets(g.device, 1, &lw2, 0, nullptr);

            g.logoReady = true;
            g.logoW = lw;
            g.logoH = lh;
            printf("RB_Vulkan: menu logo = real WAD M_DOOM (%dx%d RGBA, crisp).\n", lw, lh);
            fflush(stdout);
        }
    }
}

// DOOM-0206 (L1b): the extern-"C" menu-text batch API m_menu.c drives (Tasks 3-6). Every entry
// is a no-op until CreateTextResources baked a font (menuFontReady). rb_menu_text_active is set
// by m_menu each frame the crisp skin drew; FlushMenuText draws the queued quads only when it
// is set, so nothing changes for the paletted HUD/menu until the menu opts in.
extern "C" { int rb_menu_text_active = 0; }

extern "C" void rb_text_begin(void)
{
    g.textVerts.clear();
    g.cursorVerts.clear();
    g.logoVerts.clear();
}

extern "C" int rb_text_width(const char* s, float scale)
{
    if (!g.menuFontReady) return 0;
    return (int)(rb_text_measure(&g.menuFont, s) * scale + 0.5f);
}

extern "C" int rb_text_line_height(float scale)
{
    if (!g.menuFontReady) return 0;
    return (int)((float)g.menuFont.px_height * scale + 0.5f);
}

// Emit one string's glyph quads at (x,y) top-left with an explicit RGBA. Shared by the shadow
// pass and the main pass of rb_text_draw.
static void EmitTextQuads(const char* s, float x, float y, float scale,
                          unsigned char cr, unsigned char cg, unsigned char cb, unsigned char ca)
{
    const float aw = (float)g.menuFont.w, ah = (float)g.menuFont.h;
    float penX = x;
    // The API's y is the text's top-left; glyph xoff/yoff are baseline-relative, so drop the
    // pen to the baseline (top + ascent). ascent was baked in pixels at px_height.
    const float baseY = y + (float)g.menuFont.ascent * scale;
    for (const unsigned char* p = (const unsigned char*)s; *p; p++)
    {
        int idx = (int)*p - 32;
        if (idx < 0 || idx >= 96) continue;   // non-printable / out of the baked ASCII range
        const rb_glyph_t* gl = &g.menuFont.glyphs[idx];
        const float x0 = penX + gl->xoff * scale;
        const float y0 = baseY + gl->yoff * scale;
        const float x1 = x0 + (float)(gl->x1 - gl->x0) * scale;
        const float y1 = y0 + (float)(gl->y1 - gl->y0) * scale;
        const float u0 = (float)gl->x0 / aw, v0 = (float)gl->y0 / ah;
        const float u1 = (float)gl->x1 / aw, v1 = (float)gl->y1 / ah;
        const TextVertex a0 = { x0, y0, u0, v0, cr, cg, cb, ca };
        const TextVertex a1 = { x1, y0, u1, v0, cr, cg, cb, ca };
        const TextVertex a2 = { x1, y1, u1, v1, cr, cg, cb, ca };
        const TextVertex a3 = { x0, y1, u0, v1, cr, cg, cb, ca };
        g.textVerts.push_back(a0); g.textVerts.push_back(a1); g.textVerts.push_back(a2);
        g.textVerts.push_back(a0); g.textVerts.push_back(a2); g.textVerts.push_back(a3);
        penX += gl->xadvance * scale;
    }
}

extern "C" void rb_text_draw(const char* s, int x, int y, float scale, unsigned rgba)
{
    if (!g.menuFontReady || !s) return;
    const unsigned char cr = (unsigned char)((rgba >> 24) & 0xFF);
    const unsigned char cg = (unsigned char)((rgba >> 16) & 0xFF);
    const unsigned char cb = (unsigned char)((rgba >>  8) & 0xFF);
    const unsigned char ca = (unsigned char)( rgba        & 0xFF);
    // DOOM-0206 (L5): a soft drop-shadow for legibility over the dimmed 3D view. Draw the same
    // string in near-black one glyph-fraction down-right first, then the real colour on top.
    // Offset scales with the font so it reads the same at any resolution (clamped 1..3px).
    float shOff = (float)g.menuFont.ascent * scale / 18.0f;
    if (shOff < 1.0f) shOff = 1.0f;
    if (shOff > 3.0f) shOff = 3.0f;
    EmitTextQuads(s, (float)x + shOff, (float)y + shOff, scale, 0, 0, 0, (unsigned char)(ca * 3 / 4));
    EmitTextQuads(s, (float)x, (float)y, scale, cr, cg, cb, ca);
}

// DOOM-0206 (L2): INV-2, the HUD-safe bound. Returns the display-pixel Y below which nothing
// may draw -- the status bar's top edge while it's on screen, else the full display height
// (nothing to avoid). Used by rb_menu_dim here, and will be used by the crisp skin (Task 4)
// and the Classic clip (Task 6).
//
// screenblocks < 11 is DOOM-0148's always-true-in-game invariant (M_Init clamps screenblocks
// to <= 10, so 11's fullscreen-no-HUD view is currently unreachable) -- checked anyway so this
// stays correct if that clamp is ever lifted. 200/32 are ORIGHEIGHT/ST_HEIGHT (doomdef.h /
// st_stuff.h); named literally here since this file avoids pulling those C headers in (see the
// probe comment above RB_VulkanProbe).
extern "C" int rb_menu_safe_bottom(void)
{
    const int dispH = (int)g.extent.height;
    static bool logged = false;
    int safeBottom = dispH;
    if (gamestate == 0 /* GS_LEVEL */ && screenblocks < 11)
        safeBottom = dispH * (200 - 32) / 200;   // 200=ORIGHEIGHT, 32=ST_HEIGHT
    if (!logged)
    {
        printf("RB_Vulkan: rb_menu_safe_bottom = %d (dispH=%d, gamestate=%d, screenblocks=%d)\n",
               safeBottom, dispH, gamestate, screenblocks);
        fflush(stdout);
        logged = true;
    }
    return safeBottom;
}

// DOOM-0206 (L3): the display extent, in display pixels. The crisp Video menu (m_menu.c)
// centres its title, right-aligns values and maps the skull's virtual-Y from these.
extern "C" int rb_display_width(void)  { return (int)g.extent.width; }
extern "C" int rb_display_height(void) { return (int)g.extent.height; }

// DOOM-0206 (L3): a solid-colour quad in display pixels — the one quad path shared by the menu
// dim and the crisp Brightness slider. Colour via the reserved full-coverage atlas texel (0,0),
// so it needs no extra GPU pipeline. rgba is 0xRRGGBBAA. Queued into the same per-frame text
// vector as rb_text_draw, drawn by FlushMenuText.
extern "C" void rb_menu_fill(int x, int y, int w, int h, unsigned rgba)
{
    if (!g.menuFontReady) return;
    const float u = 0.5f / (float)g.menuFont.w;           // texel (0,0) centre (full coverage)
    const float v = 0.5f / (float)g.menuFont.h;
    const unsigned char cr = (unsigned char)((rgba >> 24) & 0xFF);
    const unsigned char cg = (unsigned char)((rgba >> 16) & 0xFF);
    const unsigned char cb = (unsigned char)((rgba >>  8) & 0xFF);
    const unsigned char ca = (unsigned char)( rgba        & 0xFF);
    const float x0 = (float)x,     y0 = (float)y;
    const float x1 = (float)(x+w), y1 = (float)(y+h);
    const TextVertex q0 = { x0, y0, u, v, cr, cg, cb, ca };
    const TextVertex q1 = { x1, y0, u, v, cr, cg, cb, ca };
    const TextVertex q2 = { x1, y1, u, v, cr, cg, cb, ca };
    const TextVertex q3 = { x0, y1, u, v, cr, cg, cb, ca };
    g.textVerts.push_back(q0); g.textVerts.push_back(q1); g.textVerts.push_back(q2);
    g.textVerts.push_back(q0); g.textVerts.push_back(q2); g.textVerts.push_back(q3);
}

// DOOM-0206 v2: the crisp menu cursor — the real WAD skull M_SKULL1 decoded to RGBA and drawn
// through its own RGBA-sampling pipeline (cursor.frag), sized to a text row and brightened. It
// is queued into a SEPARATE per-frame vector (cursorVerts) because it needs the cursor pipeline
// + descriptor, not the R8 text pipeline; FlushMenuText appends the draw after the glyphs.
// Present only if the skull decoded + uploaded; m_menu falls back to the paletted skull otherwise.
extern "C" int rb_menu_cursor_ready(void)
{
    return g.cursorReady;
}

// Drawn width (px) of the cursor at target height h, keeping the sprite's aspect — m_menu uses
// it to place the cursor fully left of the label column.
extern "C" int rb_menu_cursor_width(int h)
{
    if (!g.cursorReady || h <= 0 || g.cursorH <= 0) return 0;
    return (int)((float)h * (float)g.cursorW / (float)g.cursorH + 0.5f);
}

// Draw the skull cursor with its top-left at (x,y), target height h (px). One RGBA quad; the
// brightness is baked into the texture (M_CursorSkullRGBA), so the tint is plain white.
extern "C" void rb_menu_draw_cursor(int x, int y, int h)
{
    if (!g.cursorReady || h <= 0 || g.cursorH <= 0) return;
    const float dw = (float)h * (float)g.cursorW / (float)g.cursorH, dh = (float)h;
    const float x0 = (float)x, y0 = (float)y, x1 = x0 + dw, y1 = y0 + dh;
    // uv 0..1 over the whole cursor texture; white tint (brightness baked into the RGBA).
    const TextVertex t0 = { x0, y0, 0.f, 0.f, 255,255,255,255 };
    const TextVertex t1 = { x1, y0, 1.f, 0.f, 255,255,255,255 };
    const TextVertex t2 = { x1, y1, 1.f, 1.f, 255,255,255,255 };
    const TextVertex t3 = { x0, y1, 0.f, 1.f, 255,255,255,255 };
    g.cursorVerts.push_back(t0); g.cursorVerts.push_back(t1); g.cursorVerts.push_back(t2);
    g.cursorVerts.push_back(t0); g.cursorVerts.push_back(t2); g.cursorVerts.push_back(t3);
}

// DOOM-0206: the M_DOOM logo sprite (main-menu crisp title). Mirrors the cursor API — its own
// per-frame vert vector, drawn through g.cursorPipeline + g.logoDs in FlushMenuText.
extern "C" int rb_menu_logo_ready(void)
{
    return g.logoReady;
}

// Drawn width (px) of the logo at target height h, keeping the lump's aspect.
extern "C" int rb_menu_logo_width(int h)
{
    if (!g.logoReady || h <= 0 || g.logoH <= 0) return 0;
    return (int)((float)h * (float)g.logoW / (float)g.logoH + 0.5f);
}

// Draw the M_DOOM logo with its top-left at (x,y), target height h (px). One RGBA quad; the
// logo carries its own colours (no brighten), so the tint is plain white and it draws bright
// over the dim backdrop.
extern "C" void rb_menu_draw_logo(int x, int y, int h)
{
    if (!g.logoReady || h <= 0 || g.logoH <= 0) return;
    const float dw = (float)h * (float)g.logoW / (float)g.logoH, dh = (float)h;
    const float x0 = (float)x, y0 = (float)y, x1 = x0 + dw, y1 = y0 + dh;
    const TextVertex t0 = { x0, y0, 0.f, 0.f, 255,255,255,255 };
    const TextVertex t1 = { x1, y0, 1.f, 0.f, 255,255,255,255 };
    const TextVertex t2 = { x1, y1, 1.f, 1.f, 255,255,255,255 };
    const TextVertex t3 = { x0, y1, 0.f, 1.f, 255,255,255,255 };
    g.logoVerts.push_back(t0); g.logoVerts.push_back(t1); g.logoVerts.push_back(t2);
    g.logoVerts.push_back(t0); g.logoVerts.push_back(t2); g.logoVerts.push_back(t3);
}

// DOOM-0206 (L1b/L2): the play-view dim quad (menu backdrop). Darkens the world behind the
// menu but leaves the status bar undimmed (rb_menu_safe_bottom, INV-2) so the HUD stays
// readable. One quad path via rb_menu_fill (L3). Always queued; the Classic-tier gate lives in
// the caller (m_menu), per the plan. The dim strength is tunable in later menu tasks.
extern "C" void rb_menu_dim(void)
{
    if (!g.menuFontReady) return;
    // 0x000000A0 == ~63% black over the play view, from y=0 to the status-bar top (INV-2).
    rb_menu_fill(0, 0, (int)g.extent.width, rb_menu_safe_bottom(), 0x000000A0u);
}

// DOOM-0206 (L1b): draw this frame's queued glyph quads (rb_text_draw / rb_menu_dim) over the
// paletted 2D overlay, in the same present render pass. Self-contained: sets its own full-
// display viewport/scissor, binds the text pipeline + atlas set, and pushes invDisplay so the
// vertex shader maps display-pixel positions to NDC. The host vector is memcpy'd into the
// mapped vertex buffer here — after the top-of-frame fence — so the single copy the GPU read
// last frame is finished (no double-buffering). No-op unless the menu opted in this frame.
static void FlushMenuText()
{
    // Draw if the menu opted in AND queued anything this frame. The cursor/logo ride separate
    // vectors (their own pipeline/descriptor), so a frame that queued ONLY a cursor -- e.g. the
    // Game Select screen's brightened skull with no crisp text -- must not early-return here.
    if (!g.menuFontReady || !rb_menu_text_active ||
        (g.textVerts.empty() && g.cursorVerts.empty() && g.logoVerts.empty()))
        return;
    uint32_t verts = (uint32_t)g.textVerts.size();
    if (verts > g.textVbufCap) verts = g.textVbufCap;   // over-cap frame just clips the tail
    if (verts)   // a cursor-only frame (Game Select skull) has no glyph verts to copy
        std::memcpy(g.textVbufMapped, g.textVerts.data(), (size_t)verts * sizeof(TextVertex));

    VkViewport vpRect = {};
    vpRect.width = (float)g.extent.width;
    vpRect.height = (float)g.extent.height;
    vpRect.maxDepth = 1.0f;
    vkCmdSetViewport(g.cmd, 0, 1, &vpRect);
    VkRect2D scissor = { { 0, 0 }, g.extent };
    vkCmdSetScissor(g.cmd, 0, 1, &scissor);

    float invDisplay[2] = { 2.0f / (float)g.extent.width, 2.0f / (float)g.extent.height };
    vkCmdPushConstants(g.cmd, g.textPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                       0, sizeof(invDisplay), invDisplay);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g.textPipelineLayout, 0, 1, &g.textDs, 0, nullptr);
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.textPipeline);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.textVbuf, &off);
    vkCmdDraw(g.cmd, verts, 1, 0, 0);

    // DOOM-0206 v2: the skull cursor. It shares the vertex buffer (already bound) but needs its
    // own RGBA pipeline + descriptor. Pack its verts right after the glyph verts (offset `verts`)
    // and draw with firstVertex = verts. Guard the tail against the buffer capacity; the pushed
    // constant + viewport/scissor already set above apply (same layout). The dim/glyph verts drew
    // first, so the bright skull composites on top.
    uint32_t used = verts;   // running vertex offset into the shared buffer
    if (g.cursorReady && !g.cursorVerts.empty())
    {
        uint32_t cverts = (uint32_t)g.cursorVerts.size();
        if (used + cverts > g.textVbufCap) cverts = g.textVbufCap - used;   // clip if no room
        if (cverts > 0)
        {
            std::memcpy((unsigned char*)g.textVbufMapped + (size_t)used * sizeof(TextVertex),
                        g.cursorVerts.data(), (size_t)cverts * sizeof(TextVertex));
            vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.cursorPipeline);
            vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    g.textPipelineLayout, 0, 1, &g.cursorDs, 0, nullptr);
            vkCmdDraw(g.cmd, cverts, 1, used, 0);
            used += cverts;
        }
    }

    // DOOM-0206: the M_DOOM logo (main-menu crisp title). Same shared vertex buffer, its own
    // RGBA descriptor (g.logoDs) but the same g.cursorPipeline. Pack after whatever the cursor
    // wrote (firstVertex = used), guarding the tail against capacity.
    if (g.logoReady && !g.logoVerts.empty())
    {
        uint32_t lverts = (uint32_t)g.logoVerts.size();
        if (used + lverts > g.textVbufCap) lverts = g.textVbufCap - used;   // clip if no room
        if (lverts > 0)
        {
            std::memcpy((unsigned char*)g.textVbufMapped + (size_t)used * sizeof(TextVertex),
                        g.logoVerts.data(), (size_t)lverts * sizeof(TextVertex));
            vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.cursorPipeline);
            vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    g.textPipelineLayout, 0, 1, &g.logoDs, 0, nullptr);
            vkCmdDraw(g.cmd, lverts, 1, used, 0);
            used += lverts;
        }
    }

    // DOOM-0206 (L2 review fix): consume-and-reset. rb_menu_text_active is a per-frame gate that
    // m_menu.c sets to 1 only on a frame it actually queued a dim/text draw; resetting it here
    // (the path that actually drew) rather than every frame means the NEXT frame's gate value
    // reflects only what THAT frame's M_Drawer call decided. So the frame after the menu closes
    // -- where M_Drawer doesn't run at all -- sees the gate already 0 and this function's early
    // return above skips the draw, and the dim disappears immediately instead of sticking.
    rb_menu_text_active = 0;
}

// ===========================================================================
// DOOM-0042 — Ultra HD PBR material loader (walls + flats, RT view).
//
// A parallel bindless RGBA8 PBR image array + a per-material control SSBO live in
// a NEW descriptor set (set 3 of the RT pipeline) beside the R8 paletted array
// (set 1). The hit shader (DOOM-0042 Task 10) reads the control SSBO to branch
// paletted-vs-HD per material. This file builds/uploads those resources; the
// per-frame BIND of set 3 + the shader that samples it land together in Task 10.
//
// v1 scope: single-mip images (mip-gen is a fast-follow); roughness/metallic are
// baked offline but NOT uploaded (they wait for DOOM-0103's GGX lobe); walls+flats
// only (sprites stay paletted).
// ===========================================================================

static const int   kHdMaxImages = 4096;    // bindless image-array upper bound
static const int   kHdMaxEdge   = 1024;    // per-map longest-edge clamp (px)
static const float kHdBudgetMB  = 768.0f;  // per-map VRAM ceiling

// v1 uploads 5 of the 7 maps; roughness (RB_RGH) and metallic (RB_MET) are skipped.
struct HdMapSpec { int k; const char* suffix; bool srgb; };
static const HdMapSpec kHdV1Maps[] = {
    { RB_ALB,  "alb",  true  },   // sRGB (hardware de-gammas)
    { RB_NRM,  "nrm",  false },   // linear
    { RB_AO,   "ao",   false },
    { RB_EMIS, "emis", true  },
    { RB_HGT,  "hgt",  false },
};

// A raw RGBA8 image handed to BuildHdSet (pixels owned by the caller).
struct HdSrc { const unsigned char* px; int w, h; bool srgb; };

// Resolve a DOOM material name to its unified bindless id (walls direct, flats after
// numWall). Sprites are excluded from v1 HD. Mirrors r_vulkan.cpp's id math (:5098).
static int ResolveDoomName(const char* name, int* out_id)
{
    char n[9];
    std::strncpy(n, name, 8); n[8] = '\0';
    int t = R_CheckTextureNumForName(n);
    if (t >= 0) { *out_id = t; return 1; }                       // wall
    int lump = W_CheckNumForName(n);
    if (lump >= 0) {
        int flatIdx = lump - firstflat;
        if (flatIdx >= 0 && flatIdx < numflats) { *out_id = g.matNumWall + flatIdx; return 1; }  // flat
    }
    return 0;                                                    // not in this WAD (sprite: v1 skips)
}

// Free the per-level HD GPU resources (pool/set, images, control buffer). Keeps
// g.hdSetLayout (created once, referenced by the RT pipeline layout). Idempotent.
static void FreeHdMaterials()
{
    if (g.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(g.device);
    for (VkImageView v : g.hdViews) if (v) vkDestroyImageView(g.device, v, nullptr);
    for (VkImage im : g.hdImages)   if (im) vkDestroyImage(g.device, im, nullptr);
    g.hdViews.clear(); g.hdImages.clear();
    if (g.hdMemory)  { vkFreeMemory(g.device, g.hdMemory, nullptr);    g.hdMemory  = VK_NULL_HANDLE; }
    if (g.hdCtrlBuf) { vkDestroyBuffer(g.device, g.hdCtrlBuf, nullptr); g.hdCtrlBuf = VK_NULL_HANDLE; }
    if (g.hdCtrlMem) { vkFreeMemory(g.device, g.hdCtrlMem, nullptr);    g.hdCtrlMem = VK_NULL_HANDLE; }
    if (g.hdPool)    { vkDestroyDescriptorPool(g.device, g.hdPool, nullptr); g.hdPool = VK_NULL_HANDLE; }
    g.hdSet = VK_NULL_HANDLE;   // freed with the pool
}

// Create the HD descriptor SET LAYOUT once (before the RT pipeline layout). Binding
// 0 = control SSBO; binding 1 = the variable-count bindless PBR image array (the
// highest binding), VARIABLE_DESCRIPTOR_COUNT + PARTIALLY_BOUND — mirrors the R8
// material array in CreateDescriptors. Compute stage only (RT megakernel).
static void CreateHdSetLayout()
{
    VkDescriptorSetLayoutBinding binds[2] = {};
    binds[0].binding = 0;
    binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding = 1;
    binds[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binds[1].descriptorCount = kHdMaxImages;   // upper bound; the set alloc picks the actual count
    binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorBindingFlags flags[2] = {
        0,
        VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT |
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
    };
    VkDescriptorSetLayoutBindingFlagsCreateInfo bfci = {};
    bfci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    bfci.bindingCount = 2;
    bfci.pBindingFlags = flags;

    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.pNext = &bfci;
    dlci.bindingCount = 2;
    dlci.pBindings = binds;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.hdSetLayout),
          "vkCreateDescriptorSetLayout(hd)");
}

// (Re)build the HD descriptor pool + set: upload the given RGBA8 maps into a fresh
// bindless image array (single-mip in v1) and the control table into a device-local
// SSBO, then write both bindings. An empty srcs list still creates one 1x1 dummy so
// binding 1 is never a zero-length array. Frees any prior HD resources first. The
// caller still owns srcs[].px and may free it once this returns (copied to staging).
static void BuildHdSet(const std::vector<HdSrc>& srcsIn, const rb_matctrl_t* table, int nmat)
{
    FreeHdMaterials();

    static const unsigned char kDummyPx[4] = { 0, 0, 0, 255 };
    std::vector<HdSrc> srcs = srcsIn;
    if (srcs.empty()) srcs.push_back({ kDummyPx, 1, 1, false });
    const int nimg = (int)srcs.size();

    // 1. Create the images (single-mip), backed by one device allocation.
    g.hdImages.assign(nimg, VK_NULL_HANDLE);
    g.hdViews.assign(nimg, VK_NULL_HANDLE);
    std::vector<VkDeviceSize> imgOffset(nimg);
    VkDeviceSize memBytes = 0;
    uint32_t memTypeBits = 0xffffffffu;
    for (int i = 0; i < nimg; i++) {
        VkImageCreateInfo ici = {};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = srcs[i].srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        ici.extent = { (uint32_t)srcs[i].w, (uint32_t)srcs[i].h, 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        Check(vkCreateImage(g.device, &ici, nullptr, &g.hdImages[i]), "vkCreateImage(hd)");
        VkMemoryRequirements req = {};
        vkGetImageMemoryRequirements(g.device, g.hdImages[i], &req);
        memBytes = (memBytes + req.alignment - 1) & ~(req.alignment - 1);
        imgOffset[i] = memBytes;
        memBytes += req.size;
        memTypeBits &= req.memoryTypeBits;
    }
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = memBytes;
    mai.memoryTypeIndex = FindMemoryType(memTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.hdMemory), "vkAllocateMemory(hd)");
    for (int i = 0; i < nimg; i++)
        Check(vkBindImageMemory(g.device, g.hdImages[i], g.hdMemory, imgOffset[i]), "vkBindImageMemory(hd)");

    // 2. Staging buffer: every image's RGBA8 back to back (4-byte-aligned offsets).
    std::vector<VkDeviceSize> texOffset(nimg);
    VkDeviceSize stageBytes = 0;
    for (int i = 0; i < nimg; i++) {
        texOffset[i] = stageBytes;
        VkDeviceSize sz = (VkDeviceSize)srcs[i].w * srcs[i].h * 4;
        stageBytes += (sz + 3) & ~(VkDeviceSize)3;
    }
    VkBufferCreateInfo sbci = {};
    sbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbci.size = stageBytes;
    sbci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    Check(vkCreateBuffer(g.device, &sbci, nullptr, &staging), "vkCreateBuffer(hd staging)");
    VkMemoryRequirements sreq = {};
    vkGetBufferMemoryRequirements(g.device, staging, &sreq);
    VkMemoryAllocateInfo smai = {};
    smai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    smai.allocationSize = sreq.size;
    smai.memoryTypeIndex = FindMemoryType(sreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    Check(vkAllocateMemory(g.device, &smai, nullptr, &stagingMem), "vkAllocateMemory(hd staging)");
    Check(vkBindBufferMemory(g.device, staging, stagingMem, 0), "vkBindBufferMemory(hd staging)");
    unsigned char* sp = nullptr;
    Check(vkMapMemory(g.device, stagingMem, 0, stageBytes, 0, (void**)&sp), "vkMapMemory(hd staging)");
    for (int i = 0; i < nimg; i++)
        std::memcpy(sp + texOffset[i], srcs[i].px, (size_t)srcs[i].w * srcs[i].h * 4);
    vkUnmapMemory(g.device, stagingMem);

    // 3. Copy staging -> images (UNDEFINED->DST, copy, DST->SHADER_READ).
    std::vector<VkImageMemoryBarrier> toDst(nimg), toRead(nimg);
    for (int i = 0; i < nimg; i++) {
        VkImageMemoryBarrier b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = g.hdImages[i];
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toDst[i] = b;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        toRead[i] = b;
    }
    VkCommandBuffer cb = BeginOneTime();
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, (uint32_t)nimg, toDst.data());
    for (int i = 0; i < nimg; i++) {
        VkBufferImageCopy region = {};
        region.bufferOffset = texOffset[i];
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { (uint32_t)srcs[i].w, (uint32_t)srcs[i].h, 1 };
        vkCmdCopyBufferToImage(cb, staging, g.hdImages[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, (uint32_t)nimg, toRead.data());
    EndOneTime(cb);
    vkDestroyBuffer(g.device, staging, nullptr);
    vkFreeMemory(g.device, stagingMem, nullptr);

    // 4. Image views.
    for (int i = 0; i < nimg; i++) {
        VkImageViewCreateInfo vci = {};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = g.hdImages[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = srcs[i].srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        vci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        Check(vkCreateImageView(g.device, &vci, nullptr, &g.hdViews[i]), "vkCreateImageView(hd)");
    }

    // 5. Control SSBO (device-local), staged upload.
    VkDeviceSize ctrlBytes = (VkDeviceSize)nmat * sizeof(rb_matctrl_t);
    {
        VkBufferCreateInfo bci = {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = ctrlBytes;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer cs = VK_NULL_HANDLE;
        Check(vkCreateBuffer(g.device, &bci, nullptr, &cs), "vkCreateBuffer(hd ctrl staging)");
        VkMemoryRequirements creq = {};
        vkGetBufferMemoryRequirements(g.device, cs, &creq);
        VkMemoryAllocateInfo cmai = {};
        cmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        cmai.allocationSize = creq.size;
        cmai.memoryTypeIndex = FindMemoryType(creq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkDeviceMemory csMem = VK_NULL_HANDLE;
        Check(vkAllocateMemory(g.device, &cmai, nullptr, &csMem), "vkAllocateMemory(hd ctrl staging)");
        Check(vkBindBufferMemory(g.device, cs, csMem, 0), "vkBindBufferMemory(hd ctrl staging)");
        void* cp = nullptr;
        Check(vkMapMemory(g.device, csMem, 0, ctrlBytes, 0, &cp), "vkMapMemory(hd ctrl staging)");
        std::memcpy(cp, table, (size_t)ctrlBytes);
        vkUnmapMemory(g.device, csMem);

        VkBufferCreateInfo dbci = {};
        dbci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        dbci.size = ctrlBytes;
        dbci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        dbci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Check(vkCreateBuffer(g.device, &dbci, nullptr, &g.hdCtrlBuf), "vkCreateBuffer(hd ctrl)");
        VkMemoryRequirements dreq = {};
        vkGetBufferMemoryRequirements(g.device, g.hdCtrlBuf, &dreq);
        VkMemoryAllocateInfo dmai = {};
        dmai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        dmai.allocationSize = dreq.size;
        dmai.memoryTypeIndex = FindMemoryType(dreq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Check(vkAllocateMemory(g.device, &dmai, nullptr, &g.hdCtrlMem), "vkAllocateMemory(hd ctrl)");
        Check(vkBindBufferMemory(g.device, g.hdCtrlBuf, g.hdCtrlMem, 0), "vkBindBufferMemory(hd ctrl)");
        VkCommandBuffer ccb = BeginOneTime();
        VkBufferCopy cpy = { 0, 0, ctrlBytes };
        vkCmdCopyBuffer(ccb, cs, g.hdCtrlBuf, 1, &cpy);
        EndOneTime(ccb);
        vkDestroyBuffer(g.device, cs, nullptr);
        vkFreeMemory(g.device, csMem, nullptr);
    }

    // 6. Pool + set (variable image count).
    VkDescriptorPoolSize psizes[2] = {};
    psizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         psizes[0].descriptorCount = 1;
    psizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; psizes[1].descriptorCount = (uint32_t)nimg;
    VkDescriptorPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 1;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = psizes;
    Check(vkCreateDescriptorPool(g.device, &pci, nullptr, &g.hdPool), "vkCreateDescriptorPool(hd)");

    uint32_t varCount = (uint32_t)nimg;
    VkDescriptorSetVariableDescriptorCountAllocateInfo vcai = {};
    vcai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO;
    vcai.descriptorSetCount = 1;
    vcai.pDescriptorCounts = &varCount;
    VkDescriptorSetAllocateInfo dai = {};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.pNext = &vcai;
    dai.descriptorPool = g.hdPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &g.hdSetLayout;
    Check(vkAllocateDescriptorSets(g.device, &dai, &g.hdSet), "vkAllocateDescriptorSets(hd)");

    // 7. Write binding 0 (SSBO) + binding 1 (image array).
    VkDescriptorBufferInfo bufInfo = { g.hdCtrlBuf, 0, ctrlBytes };
    std::vector<VkDescriptorImageInfo> imgInfos(nimg);
    for (int i = 0; i < nimg; i++)
        imgInfos[i] = { g.hdSampler, g.hdViews[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet writes[2] = {};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = g.hdSet; writes[0].dstBinding = 0; writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bufInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = g.hdSet; writes[1].dstBinding = 1; writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = (uint32_t)nimg;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = imgInfos.data();
    vkUpdateDescriptorSets(g.device, 2, writes, 0, nullptr);
}

// DOOM-0042: seed set 3 with an all-paletted control table (usePBR=0 everywhere) and a
// 1x1 dummy image, so the shader's ctrl[] read + set-3 bind are valid for EVERY RT
// dispatch — including the -rtverify headless path (gated on g.rtEnabled, not the tier)
// and the first frame before EnsureHdMaterials runs in Ultra. Called once at atlas time;
// EnsureHdMaterials later frees this and rebuilds the real per-level set. No-op without an
// RT pipeline (g.hdSetLayout unset -> set 3 is never bound, so nothing to seed).
static void InitHdDefault()
{
    if (g.hdSetLayout == VK_NULL_HANDLE) return;
    const int N = RB_MaterialCount();
    std::vector<rb_matctrl_t> table(N);
    for (int i = 0; i < N; i++) {
        for (int m = 0; m < RB_MAP_COUNT; m++) table[i].maps[m] = -1;
        table[i].uvScale = 1.0f;
        table[i].flags   = 0u;
        table[i].usePBR  = 0u;
    }
    g.hdGrungeIdx = -1;                 // DOOM-0179: no grime on the default all-paletted set
    g.hdDirtIdx = -1;                   // DOOM-0181: no dirt-colour texture on the paletted set
    BuildHdSet({}, table.data(), N);   // empty srcs -> 1x1 dummy image; all paletted
}

// DOOM-0183 L1: flag the liquid flats (nukage/lava) by NAME on the per-level control table
// so the shader has a surface-true "is this liquid?" signal (§4.2), replacing DOOM-0181's
// crude albedo-green guess as the effect trigger. Name-derived, not CSV: the bit rides
// MatCtrl.flags even on a paletted flat (usePBR=0), so nukage/lava need no HD hero. The
// flat's unified id is matNumWall + (lump - firstflat). INV-2: only NUKAGE1-3 / LAVA1-4 —
// water/blood/SLIME* stay unflagged (some SLIME* frames are dry rock).
static void FlagLiquidFlats(rb_matctrl_t* table, int N)
{
    const struct { const char* name; unsigned int bit; } lut[] = {
        { "NUKAGE1", RB_FLAG_LIQUID_NUKAGE }, { "NUKAGE2", RB_FLAG_LIQUID_NUKAGE },
        { "NUKAGE3", RB_FLAG_LIQUID_NUKAGE },
        { "LAVA1", RB_FLAG_LIQUID_LAVA }, { "LAVA2", RB_FLAG_LIQUID_LAVA },
        { "LAVA3", RB_FLAG_LIQUID_LAVA }, { "LAVA4", RB_FLAG_LIQUID_LAVA },
    };
    for (const auto& e : lut) {
        char nm[9]; strncpy(nm, e.name, 8); nm[8] = '\0';
        int lump = W_CheckNumForName(nm);
        if (lump < 0) continue;
        int fi = lump - firstflat;
        if (fi < 0 || fi >= g.matNumFlat) continue;
        int id = g.matNumWall + fi;
        if (id >= 0 && id < N) table[id].flags |= e.bit;
    }
}

// Per-level (Ultra only): load the current map's HD material sets and rebuild the HD
// descriptor set. Idempotent (guarded by g.hdBuilt). Any per-material failure leaves
// that material paletted; a missing materials.csv leaves everything paletted. Always
// ends with a valid g.hdSet.
static void EnsureHdMaterials()
{
    if (rendermode != TIER_RT3D || g.hdBuilt) return;
    const int N = RB_MaterialCount();

    // 1. Load the sidecar (absent => all paletted; not an error).
    char csvPath[512];
    rb_asset_path(csvPath, sizeof(csvPath), "materials.csv");
    std::vector<rb_matrow_t> rows;
    if (FILE* f = fopen(csvPath, "r")) {
        char line[1024]; int lineno = 0;
        while (fgets(line, sizeof(line), f)) {
            lineno++;
            rb_matrow_t r;
            int rc = rb_parse_material_line(line, &r);
            if (rc == 1) rows.push_back(r);
            else if (rc == -1) printf("DOOM-0042: %s:%d malformed row - skipped.\n", csvPath, lineno);
        }
        fclose(f);
    } else {
        printf("DOOM-0042: no %s - Ultra uses paletted art.\n", csvPath);
    }

    // 2. Resolve names -> unified ids into the control table.
    std::vector<rb_matctrl_t> table(N);
    int dups = 0;
    rb_build_ctrl_table(rows.data(), (int)rows.size(), N, &ResolveDoomName, table.data(), &dups);
    if (dups) printf("DOOM-0042: %d duplicate doom_name row(s) - last wins.\n", dups);
    FlagLiquidFlats(table.data(), N);   // DOOM-0183 L1: name-derived liquid bit (nukage/lava)

    // last-wins row per id (matches rb_build_ctrl_table's overwrite policy).
    std::vector<int> rowForId(N, -1);
    for (int ri = 0; ri < (int)rows.size(); ri++) {
        int id; if (ResolveDoomName(rows[ri].name, &id) && id >= 0 && id < N) rowForId[id] = ri;
    }

    // 3. Traffic (world surface area per unified id) from the level mesh.
    std::vector<float> traffic(N, 0.0f);
    if (g.levelMesh && g.levelMesh->verts) {
        const rb_vertex_t* v = g.levelMesh->verts;
        int ntri = g.levelMesh->numverts / 3;
        for (int t = 0; t < ntri; t++) {
            const rb_vertex_t& a = v[t*3+0], &b = v[t*3+1], &c = v[t*3+2];
            float ax=b.x-a.x, ay=b.y-a.y, az=b.z-a.z;
            float bx=c.x-a.x, by=c.y-a.y, bz=c.z-a.z;
            float cx=ay*bz-az*by, cy=az*bx-ax*bz, cz=ax*by-ay*bx;
            float area = 0.5f * sqrtf(cx*cx + cy*cy + cz*cz);
            int id = (a.flags & RB_MESH_FLAT) ? g.matNumWall + a.texnum : a.texnum;
            if (id >= 0 && id < N) traffic[id] += area;
        }
    }

    // 4. Decode each HD material's v1 maps. Missing/undecodable ALBEDO -> that material
    //    falls back to paletted; a missing non-albedo map -> that slot stays default.
    struct Decoded { int id; int k; rb_image_t img; bool srgb; };
    std::vector<Decoded> decoded;
    std::vector<float> estMB(N, 0.0f);
    std::vector<int>   isHero(N, 0);

    for (int id = 0; id < N; id++) {
        if (!table[id].usePBR || rowForId[id] < 0) continue;
        const rb_matrow_t& r = rows[rowForId[id]];
        isHero[id] = r.is_hero;

        std::vector<Decoded> mine;
        bool albedoOk = false;
        for (const HdMapSpec& ms : kHdV1Maps) {
            char rel[192]; const char* relPath = nullptr;
            if (r.is_hero) {
                if (r.maps[ms.k][0] == '\0') continue;      // empty hero cell = no map (default)
                relPath = r.maps[ms.k];
            } else {
                snprintf(rel, sizeof(rel), "derived/%s_%s.png", r.name, ms.suffix);
                relPath = rel;
            }
            char full[720];
            rb_asset_path(full, sizeof(full), relPath);
            rb_image_t img;
            if (!rb_image_load(full, &img)) {
                if (ms.k != RB_ALB)
                    printf("DOOM-0042: %s: no %s map (%s) - default.\n", r.name, ms.suffix, full);
                continue;
            }
            rb_image_downscale_max(&img, kHdMaxEdge);
            if (ms.k == RB_ALB) albedoOk = true;
            estMB[id] += (float)img.w * img.h * 4.0f / (1024.0f*1024.0f);
            mine.push_back({ id, ms.k, img, ms.srgb });
        }
        if (!albedoOk) {
            for (Decoded& d : mine) rb_image_free(&d.img);
            table[id].usePBR = 0;
            printf("DOOM-0042: %s: no usable albedo - paletted.\n", r.name);
            continue;
        }
        for (Decoded& d : mine) decoded.push_back(d);
    }

    // 5. Budget: drop lowest-traffic over the ceiling; returns kept ids in upload order.
    std::vector<int> order(N, -1); int nLoaded = 0;
    rb_apply_budget(table.data(), N, traffic.data(), estMB.data(), isHero.data(),
                    kHdBudgetMB, order.data(), &nLoaded);

    // 6. Assemble the upload list in descending-traffic order; assign map slots. Cap the
    //    total image count at kHdMaxImages (the bindless-array upper bound): if a material's
    //    maps won't fit, drop the whole material to paletted (never a partial upload). Not
    //    reachable by v1 (full DOOM1 ~2000 maps < 4096) but keeps the never-crash contract
    //    and honours "no silent truncation" for a future full-WAD sidecar.
    std::vector<HdSrc> srcs;
    float usedMB = 0.0f;
    int   capDropped = 0;
    for (int oi = 0; oi < nLoaded; oi++) {
        int id = order[oi];
        int cnt = 0;
        for (Decoded& d : decoded) if (d.id == id) cnt++;
        if ((int)srcs.size() + cnt > kHdMaxImages) {
            table[id].usePBR = 0;               // no room in the array -> paletted
            capDropped++;
            continue;
        }
        for (Decoded& d : decoded) {
            if (d.id != id) continue;
            table[id].maps[d.k] = (int)srcs.size();
            srcs.push_back({ d.img.pixels, d.img.w, d.img.h, d.srgb });
            usedMB += (float)d.img.w * d.img.h * 4.0f / (1024.0f*1024.0f);
            printf("DOOM-0042: id %d map[%d] %dx%d  (%.1f MB)\n", id, d.k, d.img.w, d.img.h, usedMB);
        }
    }
    if (capDropped)
        printf("DOOM-0042: %d material(s) dropped to paletted (> %d-image bindless cap).\n",
               capDropped, kHdMaxImages);

    // DOOM-0179: append the world-space grime overlay as one extra bindless image — a single
    // GLOBAL map (not per-material) the shader multiplies over every usePBR surface, sampled by
    // WORLD position to break the base tiling. Loaded only when at least one HD material exists
    // (nothing else samples it); its bindless slot rides to the trace in pc.misc5.x. A missing
    // or undecodable overlay just leaves grime off (index -1) — never fatal.
    g.hdGrungeIdx = -1;
    rb_image_t grunge; bool grungeOk = false;
    if (!srcs.empty() && (int)srcs.size() < kHdMaxImages) {   // room in the bindless array
        char gpath[720];
        rb_asset_path(gpath, sizeof(gpath), "overlays/grunge.png");
        if (rb_image_load(gpath, &grunge)) {
            rb_image_downscale_max(&grunge, kHdMaxEdge);
            g.hdGrungeIdx = (int)srcs.size();
            srcs.push_back({ grunge.pixels, grunge.w, grunge.h, false });   // UNORM (raw values)
            grungeOk = true;
            printf("DOOM-0179: grime overlay id %d  %dx%d.\n", g.hdGrungeIdx, grunge.w, grunge.h);
        } else {
            printf("DOOM-0179: no %s - grime overlay off.\n", gpath);
        }
    }

    // DOOM-0181: a second global overlay — a real dirt COLOUR texture the shader samples in
    // world space for the filth-stain colour, so dirt reads as a photographed texture (organic
    // tonal + hue variation) instead of a flat tint. sRGB (a colour map, hardware de-gammas).
    // Rides to the trace in pc.misc5.z; a missing/undecodable file just leaves it off (-1).
    rb_image_t dirt; bool dirtOk = false;
    if (!srcs.empty() && (int)srcs.size() < kHdMaxImages) {
        char dpath[720];
        rb_asset_path(dpath, sizeof(dpath), "overlays/dirt.png");
        if (rb_image_load(dpath, &dirt)) {
            rb_image_downscale_max(&dirt, kHdMaxEdge);
            g.hdDirtIdx = (int)srcs.size();
            srcs.push_back({ dirt.pixels, dirt.w, dirt.h, true });   // sRGB colour texture
            dirtOk = true;
            printf("DOOM-0181: dirt overlay id %d  %dx%d.\n", g.hdDirtIdx, dirt.w, dirt.h);
        } else {
            printf("DOOM-0181: no %s - dirt overlay off.\n", dpath);
        }
    }

    // 7. Build the set (uploads images + SSBO), then free every decoded image (kept
    //    ones were copied to staging; dropped ones were never uploaded).
    BuildHdSet(srcs, table.data(), N);
    for (Decoded& d : decoded) rb_image_free(&d.img);
    if (grungeOk) rb_image_free(&grunge);
    if (dirtOk) rb_image_free(&dirt);

    printf("DOOM-0042: HD load done - %d material(s), %d image(s), %.1f MB.\n",
           nLoaded, (int)srcs.size(), usedMB);
    g.hdBuilt = true;
}

} // namespace

extern "C" int RB_Vulkan_Available(int want_rt)
{
    int tier = RB_VulkanProbe();
    if (want_rt)
        return tier == TIER_RT3D;
    return tier == TIER_RT3D || tier == TIER_RASTER3D;
}

extern "C" void RB_Vulkan_Init(void)
{
    // Recreate the SDL window as a Vulkan window (tears down the 2D renderer).
    I_ShutdownGraphicsForVulkan();

    CreateInstance();

    if (!SDL_Vulkan_CreateSurface((SDL_Window*)I_GetWindow(), g.instance, &g.surface))
        I_Error("R_Vulkan: SDL_Vulkan_CreateSurface: %s", SDL_GetError());

    PickPhysicalAndDevice();
    LoadRtEntryPoints();   // resolve the AS entry points + scratch align (RT only)
    CreateSwapchain();
    CreateImageViews();
    CreateRenderPass();
    CreateDepthResources();
    CreateCommandsAndSync();   // command pool up front: the one-time layout transitions in
                               // CreateSceneTarget (DOOM-0170 L2b SSAO image park) and
                               // CreateShadowResources (BeginOneTime) both need it
    CreateSceneTarget();       // DOOM-0170 L2a off-screen colour (before its framebuffer)
    CreateFramebuffers();
    CreateDescriptors();       // set layout + sampler (needed by the pipeline layout)
    CreateShadowResources();   // DOOM-0170 L2c flashlight shadow map (pass/fb/UBO/set 1);
                               // before CreatePipeline, which builds the shadow pipeline
    CreatePipeline();
    InitPaletteAndDescriptorSet();  // PLAYPAL LUT + descriptor set, so the HUD/menu
                                    // overlay composites from the first frame (DOOM-0045)
    UpdateCompositeDescriptor();    // DOOM-0170 L2a: point the composite sampler at the scene view
    CreateTextResources();          // DOOM-0206 L1b: bake the menu glyph atlas + text pipeline
    if (g.rtEnabled)
    {
        // Path-tracer compute pass (DOOM-0009 build step 2c). Pipeline first (it
        // allocates the descriptor set), then the swapchain-sized storage image
        // (which points the set's image half at its view). The TLAS half is
        // written later, per level, by BuildAccelerationStructures.
        CreateSvgfDescriptorLayout();  // set-2 layout (the trace pipeline layout needs it)
        CreateRtComputePipeline();
        CreateBakePipeline();          // GI bake pass (step 4b-ii), dispatched per level
        CreateSvgfPipelines();         // denoiser passes (step 6); needs svgfDsLayout
        CreateLabelPipeline();         // on-screen mode label (debug); reuses svgfDsLayout
        CreateTaauPipeline();          // 6-d temporal upscaler; needs its own set + pipeline
        CreateRtTargets();             // rt + svgf + taau images; writes the descriptor sets
    }
    g.ready = true;

    printf("RB_Vulkan_Init: swapchain up (%ux%u, %u images).\n",
           g.extent.width, g.extent.height, (unsigned)g.images.size());
}

extern "C" void RB_Vulkan_SetResolution(int w, int h)
{
    (void)w; (void)h;
    // The swapchain follows the window/surface size; flag a rebuild so the next
    // present picks up a resize.
    g.needRecreate = true;
}

extern "C" void RB_Vulkan_RenderView(const rb_view_t* view)
{
    // The draw happens in Present (which owns the swapchain image); here we just
    // turn the player's camera into the view-projection matrix the vertex shader
    // uses. DOOM's horizontal FOV is 90 degrees (FIELDOFVIEW, r_main.c).
    if (!g.ready || g.extent.height == 0)
        return;

    float c = std::cos(view->angle), s = std::sin(view->angle);
    float eye[3] = { view->x, view->y, view->z };
    float fwd[3] = { c, s, 0.0f };          // yaw in the xy plane, z up
    float up[3]  = { 0.0f, 0.0f, 1.0f };

    float v[16], p[16];
    Mat4LookAt(eye, fwd, up, v);
    float aspect = (float)g.extent.width / (float)g.extent.height;
    Mat4PerspectiveH(kPi * 0.5f, aspect, 1.0f, 100000.0f, p);
    Mat4Mul(p, v, g.viewProj);              // MVP = proj * view
    g.haveCamera = true;
    g.lastView   = *view;                   // for the sprite build in Present
}

extern "C" void RB_Vulkan_SetOverlay(const unsigned char* pixels, int w, int h)
{
    // Stash this frame's 2D overlay (screens[0]); the actual copy into the GPU
    // image happens in Present, after the fence wait, so the staging buffer is
    // never written while a prior frame might still read it. The palette LUT +
    // descriptor set are built at init (InitPaletteAndDescriptorSet), so the
    // compositor engages from the first frame — the title/demo screen before any
    // level — not only once an atlas has been built (DOOM-0045). g.ready implies
    // both exist, since RB_Vulkan_Init always creates them.
    if (!g.ready || !pixels || w <= 0 || h <= 0)
        return;
    if (!g.overlayReady)
    {
        CreateOverlayResources(w, h);
        g.overlayCapW = w;
        g.overlayCapH = h;
    }
    else if (w != g.overlayCapW || h != g.overlayCapH)
    {
        // DOOM-0073: the overlay staging buffer + image are allocated once, at the
        // first size (screens[0] is fixed for the session — V_Init allocates it
        // once). A later size change would overrun the fixed-capacity staging buffer
        // in Present (memcpy of overlayW*overlayH) and mismatch the image extent, so
        // refuse the resized overlay rather than corrupt memory. A future runtime-
        // resolution feature must recreate these resources here instead of latching.
        static bool warned = false;
        if (!warned)
        {
            std::fprintf(stderr, "RB_Vulkan_SetOverlay: overlay size changed "
                         "%dx%d -> %dx%d; ignoring (mid-session resize unsupported)\n",
                         g.overlayCapW, g.overlayCapH, w, h);
            warned = true;
        }
        return;
    }
    g.overlaySrc = pixels;
    g.overlayW   = w;
    g.overlayH   = h;
}

// DOOM-0084: per-frame budget of emissive-sprite emitter triangles appended to the
// static set (2 per emissive sprite quad). Excess (rare) is clamped and logged.
static const uint32_t SPR_EMIT_MAX = 1024;

// Merge the cached static emitters (g.staticEmit/g.staticWgt) with this frame's
// dynamic emissive-sprite emitters, rebuild the power-importance CDF over the whole
// set, and write the 14-float records into the persistently-mapped emitter buffer.
// Sets g.emitCount. dynEmit/dynWgt may be null (static-only, e.g. the level-load
// fill the GI bake reads). Host-coherent + the frame fence makes the write safe.
void FinalizeEmitters(const std::vector<float>* dynEmit, const std::vector<float>* dynWgt,
                      const std::vector<uint32_t>* dynSec = nullptr)
{
    if (!g.emitMapped || !g.emitCap) { g.emitCount = 0; return; }
    // Static emitters [0, staticN) then this frame's sprite emitters [staticN, n) —
    // the omniStart split (misc4.y) the shader keys on. The merge + cdf build is the
    // shared nee_merge_emitters() (nee_sampling.h), unit-tested in nee_sampling_test.
    const int dynN = (dynEmit && dynWgt) ? (int)dynWgt->size() : 0;
    g.emitCount = (uint32_t)nee_merge_emitters(
        g.staticEmit.data(), g.staticWgt.data(), (int)g.staticWgt.size(),
        dynN ? dynEmit->data() : nullptr, dynN ? dynWgt->data() : nullptr, dynN,
        (int)g.emitCap, (float*)g.emitMapped);

    // DOOM-0119: fill the parallel per-emitter sector buffer in lock-step with the
    // merge above (same static-then-dynamic order + same cap clamp). Static records
    // carry the no-cull sentinel (their centroid sits on a linedef -> ambiguous
    // sector); dynamic sprite records carry their Thing's sector from dynSec.
    if (g.emitSecMapped)
    {
        uint32_t* es = (uint32_t*)g.emitSecMapped;
        const int staticN = (int)g.staticWgt.size();
        const int n = (int)g.emitCount;
        for (int i = 0; i < n; i++)
        {
            if (i < staticN) { es[i] = 0xFFFFFFFFu; continue; }
            const int di = i - staticN;
            es[i] = (dynSec && di < (int)dynSec->size()) ? (*dynSec)[di] : 0xFFFFFFFFu;
        }
    }
}

// DOOM-0170 L1b: per-subsector nearest-N dynamic point-light cap. Must match
// RASTER_MAX_LIGHTS in mesh.frag. Each record is 6 floats: centroid[3] Le[3].
static const uint32_t RASTER_MAX_LIGHTS_PER_SUBSECTOR = 16;
static const uint32_t RASTER_LIGHT_FLOATS             = 6;

// DOOM-0084: each traced frame, scan this frame's world-sprite billboards (already
// built into g.sprWorldBuf for the trace, DOOM-0100) for emissive ones — a sprite
// whose material has a non-zero Le (the same area-weighted bright-texel mean that
// flags emissive lamp/computer textures, so TLMP/TLP2 floor lamps, candelabra and
// burning barrels qualify automatically, no hand-kept name list). Append each
// emissive sprite's two quad triangles as NEE area emitters so it pools light +
// casts world-occluded shadows onto its surroundings, then re-finalise the list.
void BuildDynamicEmitters()
{
    if (!g.emitMapped || g.matEmissive.empty() || !g.sprWorldMapped)
    {
        FinalizeEmitters(nullptr, nullptr);      // keep the static set live
        return;
    }
    const int      matCount = (int)(g.matEmissive.size() / 3);
    const uint32_t base     = (uint32_t)(g.matNumWall + g.matNumFlat);
    const rb_vertex_t* v    = (const rb_vertex_t*)g.sprWorldMapped;
    const uint32_t tris     = g.sprWorldVertCount / 3u;

    // DOOM-0084: cast-intensity boost for sprite lights. A lamp billboard is a small
    // quad, so as an NEE area light its radiant power (Le * area) is tiny next to the
    // big wall/flat emitters — it's selected rarely and contributes little, so lamps
    // self-glow but barely light the room. Boost the EMITTER Le (not the self-glow,
    // which reads matEmis directly) so sprite lights both weigh more in the CDF and
    // cast more. INV-7 tunable, pending a Workbench pass.
    const float kSpriteEmitBoost = 12.0f;

    std::vector<float> emit, wgt;
    std::vector<uint32_t> dynSec;            // DOOM-0119: per-emitter sector (REJECT cull)
    uint32_t litSprites = 0;
    float maxLe = 0.0f;
    for (uint32_t t = 0; t < tris; t++)
    {
        const rb_vertex_t* tri = &v[t * 3];
        if (!(tri->flags & RB_MESH_EMISSIVE))
            continue;                            // not a light object (pickup/monster/decoration)
        const int id = (int)base + tri->texnum;
        if (id < 0 || id >= matCount)
            continue;
        const float* le = &g.matEmissive[(size_t)id * 3];
        if (le[0] + le[1] + le[2] <= 0.0f)
            continue;                            // emissive Thing whose sprite texture isn't a light
        const float lr = le[0] * kSpriteEmitBoost, lg = le[1] * kSpriteEmitBoost, lb = le[2] * kSpriteEmitBoost;
        for (int k = 0; k < 3; k++)
        {
            emit.push_back(tri[k].x); emit.push_back(tri[k].y); emit.push_back(tri[k].z);
        }
        emit.push_back(lr); emit.push_back(lg); emit.push_back(lb);
        emit.push_back(0.0f); emit.push_back(0.0f);   // cdf, pdf — filled in finalise
        const float ex1 = tri[1].x - tri[0].x, ey1 = tri[1].y - tri[0].y, ez1 = tri[1].z - tri[0].z;
        const float ex2 = tri[2].x - tri[0].x, ey2 = tri[2].y - tri[0].y, ez2 = tri[2].z - tri[0].z;
        const float cxv = ey1 * ez2 - ez1 * ey2;
        const float cyv = ez1 * ex2 - ex1 * ez2;
        const float czv = ex1 * ey2 - ey1 * ex2;
        const float area = 0.5f * std::sqrt(cxv * cxv + cyv * cyv + czv * czv);
        wgt.push_back(emis::luminance(lr, lg, lb) * area);

        // DOOM-0119: tag this sprite emitter with its room for the REJECT cull. The
        // quad is a billboard centred on the Thing, so the triangle centroid's sector
        // is the Thing's sector. No REJECT lump (numSectors 0) or no sector -> sentinel
        // so the shader leaves this light uncalled.
        const float cxw = (tri[0].x + tri[1].x + tri[2].x) * (1.0f / 3.0f);
        const float cyw = (tri[0].y + tri[1].y + tri[2].y) * (1.0f / 3.0f);
        const int   sec = (g.numSectors > 0u) ? RB_SectorAtPoint(cxw, cyw) : -1;
        dynSec.push_back(sec >= 0 ? (uint32_t)sec : 0xFFFFFFFFu);
        litSprites++;
        maxLe = std::max(maxLe, emis::value(lr, lg, lb));
    }
    FinalizeEmitters(&emit, &wgt, &dynSec);

    // Diagnostic (DOOM-0084 bring-up): confirm sprite lights reach the NEE set.
    // Rate-limited so it doesn't spam at 35 Hz; prints on change of the lit count.
    static uint32_t lastLit = 0xFFFFFFFFu;
    if (litSprites != lastLit)
    {
        lastLit = litSprites;
        double sumStatic = 0.0, sumSprite = 0.0;
        for (float w : g.staticWgt) sumStatic += w;
        for (float w : wgt)         sumSprite += w;
        const double frac = (sumStatic + sumSprite) > 0.0
                          ? sumSprite / (sumStatic + sumSprite) : 0.0;
        printf("RB_Vulkan NEE: %u static + %u sprite emitters (sprite maxLe %.2f, "
               "CDF sprite share %.4f%% -> now sampled directly).\n",
               (uint32_t)g.staticWgt.size(), litSprites, maxLe, frac * 100.0);
        fflush(stdout);
    }
}

// DOOM-0170 perf: recompute the per-subsector nearest-N cache over the STATIC emitters
// only (records [0, staticN) — wall/flat lights that never move and carry the DOOM-0119
// no-reject sentinel, so each is tested against EVERY subsector). This unpruned
// O(subsectors × staticEmitters) cull was the ~8 ms/frame hotspot the DOOM-0170 CPU
// profiler pinpointed; it is camera- and frame-invariant, so it runs only when the static
// set changes (level load / switch press / animated flat, via g.staticLightsDirty) instead
// of every frame. The result is packed nearest-first into g.staticLightCache (same 6-float
// centroid[3] Le[3] layout as g.lightBuf); BuildRasterPointLights copies it and merges the
// per-frame dynamic sprite emitters on top. Reads the static records straight from the
// merged emitter buffer (g.emitMapped[0, staticN)), which FinalizeEmitters lays out
// static-first, so the cache stays in lock-step with what the shader would sample.
static void RebuildStaticPointLightCache(int staticN)
{
    const uint32_t N      = RASTER_MAX_LIGHTS_PER_SUBSECTOR;
    const int      numSub = (int)g.probeCount;
    g.staticLightCache.assign((size_t)numSub * N * RASTER_LIGHT_FLOATS, 0.0f);
    g.staticLightCount.assign((size_t)numSub, 0);
    g.staticLightsDirty = false;
    if (staticN <= 0 || numSub <= 0 || !g.emitMapped)
        return;                                   // no static lights -> cache stays zeroed
    const float* em = (const float*)g.emitMapped;

    // Precompute each static emitter's 3D centroid once (reused across every subsector).
    std::vector<float> sc((size_t)staticN * 3);
    for (int e = 0; e < staticN; e++)
    {
        const float* r = &em[(size_t)e * 14];
        sc[e * 3 + 0] = (r[0] + r[3] + r[6]) * (1.0f / 3.0f);
        sc[e * 3 + 1] = (r[1] + r[4] + r[7]) * (1.0f / 3.0f);
        sc[e * 3 + 2] = (r[2] + r[5] + r[8]) * (1.0f / 3.0f);
    }

    for (int si = 0; si < numSub; si++)
    {
        const rb_probe_t& c = g.subCentroid[si];

        // Nearest-N by 2D distance to the subsector centroid, kept sorted ascending.
        // Static emitters carry the no-reject sentinel, so no REJECT cull here.
        float bestD[RASTER_MAX_LIGHTS_PER_SUBSECTOR];
        int   bestI[RASTER_MAX_LIGHTS_PER_SUBSECTOR];
        int   cnt = 0;
        for (int e = 0; e < staticN; e++)
        {
            const float dx = sc[e * 3 + 0] - c.x;
            const float dy = sc[e * 3 + 1] - c.y;
            const float d2 = dx * dx + dy * dy;
            if (cnt == (int)N && d2 >= bestD[cnt - 1])
                continue;                         // farther than the current worst kept
            int pos = (cnt < (int)N) ? cnt : (int)N - 1;
            while (pos > 0 && bestD[pos - 1] > d2)
            {
                bestD[pos] = bestD[pos - 1];
                bestI[pos] = bestI[pos - 1];
                pos--;
            }
            bestD[pos] = d2;
            bestI[pos] = e;
            if (cnt < (int)N)
                cnt++;
        }

        float* slot = &g.staticLightCache[(size_t)si * N * RASTER_LIGHT_FLOATS];
        for (int k = 0; k < cnt; k++)
        {
            const float* r = &em[(size_t)bestI[k] * 14];
            slot[k * 6 + 0] = sc[bestI[k] * 3 + 0];
            slot[k * 6 + 1] = sc[bestI[k] * 3 + 1];
            slot[k * 6 + 2] = sc[bestI[k] * 3 + 2];
            slot[k * 6 + 3] = r[9];               // Le.r
            slot[k * 6 + 4] = r[10];              // Le.g
            slot[k * 6 + 5] = r[11];              // Le.b
        }
        g.staticLightCount[si] = cnt;
    }
}

// DOOM-0170 L1b: fill the per-subsector nearest-N dynamic point-light lists the raster
// fragment shader (mesh.frag) reads, each raster frame right after the NEE emitter list is
// finalised (BuildDynamicEmitters). A point light per emitter triangle — centroid =
// mean(v0,v1,v2), colour + intensity = the record's Le, packed nearest-first into g.lightBuf.
//
// DOOM-0170 perf split: the static emitters ([0, staticN), unpruned, unchanging) are culled
// once into g.staticLightCache (RebuildStaticPointLightCache); this per-frame pass copies that
// cache and merges only the handful of DYNAMIC sprite emitters ([staticN, emitCount) —
// torches/lamps/flying fireballs, which move and use the DOOM-0119 REJECT cull). Merging the
// dynamic set's nearest-N into the static set's cached nearest-N yields the exact nearest-N of
// the union: any static light the merge drops was already farther than N nearer lights, so it
// was never in the union's nearest-N either. Per-frame cost is now O(subsectors × dynEmitters)
// (a handful), well under the §6 1 ms budget, instead of the old ~8 ms full cull.
void BuildRasterPointLights()
{
    if (!g.lightMapped || g.probeCount == 0)
        return;
    const uint32_t N       = RASTER_MAX_LIGHTS_PER_SUBSECTOR;
    const int      numSub  = (int)g.probeCount;
    const size_t   subF    = (size_t)N * RASTER_LIGHT_FLOATS;
    float*         out     = (float*)g.lightMapped;

    const int emitN   = (int)g.emitCount;
    int       staticN = (int)g.staticWgt.size();
    if (staticN > emitN) staticN = emitN;          // clamp (over-cap merge)
    const int dynN    = emitN - staticN;           // this frame's moving sprite emitters

    // Recache the (frame-invariant) static cull only when the static set changed or the
    // subsector count did (a new level reassigns subCentroid without touching the flag).
    if (g.staticLightsDirty || (int)g.staticLightCount.size() != numSub)
        RebuildStaticPointLightCache(staticN);

    const bool haveCache = ((int)g.staticLightCount.size() == numSub &&
                            (g.staticLightCache.size() >= (size_t)numSub * subF));

    // No dynamic emitters this frame -> the cache IS the answer; copy it straight out.
    if (dynN <= 0 || !g.emitMapped)
    {
        if (haveCache)
            std::memcpy(out, g.staticLightCache.data(), (size_t)numSub * subF * sizeof(float));
        else
            std::memset(out, 0, (size_t)numSub * subF * sizeof(float));
        return;
    }

    const float*    em = (const float*)g.emitMapped;
    const uint32_t* es = (const uint32_t*)g.emitSecMapped;   // dynamic per-emitter sector

    // Dynamic-emitter centroids (records [staticN, emitN)); reused across every subsector.
    g.emitCentroidScratch.resize((size_t)dynN * 3);
    float* ec = g.emitCentroidScratch.data();
    for (int d = 0; d < dynN; d++)
    {
        const float* r = &em[(size_t)(staticN + d) * 14];
        ec[d * 3 + 0] = (r[0] + r[3] + r[6]) * (1.0f / 3.0f);
        ec[d * 3 + 1] = (r[1] + r[4] + r[7]) * (1.0f / 3.0f);
        ec[d * 3 + 2] = (r[2] + r[5] + r[8]) * (1.0f / 3.0f);
    }

    const bool cull = (g.numSectors > 0 && g.rejectCPU &&
                       (int)g.subSecSector.size() >= numSub && es);

    // The whole merge runs in local cached RAM (rec[]/bestD[]); the mapped output buffer
    // (out) is host-coherent write-combined memory, where reads and scattered read-modify-
    // write are ruinously slow — so we read the seed from g.staticLightCache (plain RAM),
    // sort in rec[], and write each subsector's N records to `out` exactly once, in order.
    for (int si = 0; si < numSub; si++)
    {
        const rb_probe_t& c    = g.subCentroid[si];
        const int         secA = cull ? g.subSecSector[si] : -1;

        float rec[RASTER_MAX_LIGHTS_PER_SUBSECTOR * RASTER_LIGHT_FLOATS] = { 0 };
        float bestD[RASTER_MAX_LIGHTS_PER_SUBSECTOR];
        int   cnt = haveCache ? g.staticLightCount[si] : 0;

        // Seed from the cached static nearest-N (plain RAM read), recovering each kept
        // light's distance so dynamic emitters can be merged in by distance.
        if (cnt > 0)
        {
            const float* cache = &g.staticLightCache[(size_t)si * subF];
            std::memcpy(rec, cache, (size_t)cnt * RASTER_LIGHT_FLOATS * sizeof(float));
            for (int k = 0; k < cnt; k++)
            {
                const float dx = rec[k * 6 + 0] - c.x;
                const float dy = rec[k * 6 + 1] - c.y;
                bestD[k] = dx * dx + dy * dy;
            }
        }

        for (int d = 0; d < dynN; d++)
        {
            const int e = staticN + d;
            if (cull && secA >= 0)
            {
                const uint32_t secE = es[e];
                if (secE != 0xFFFFFFFFu && (int)secE < (int)g.numSectors)
                {
                    const int pnum = secA * (int)g.numSectors + (int)secE;
                    if (g.rejectCPU[pnum >> 3] & (1 << (pnum & 7)))
                        continue;                  // this sector provably can't see the emitter
                }
            }
            const float dx = ec[d * 3 + 0] - c.x;
            const float dy = ec[d * 3 + 1] - c.y;
            const float d2 = dx * dx + dy * dy;
            if (cnt == (int)N && d2 >= bestD[cnt - 1])
                continue;                          // farther than the current worst kept
            // insert into the sorted rec[] (shift the record right; drop the last when full)
            int pos = (cnt < (int)N) ? cnt : (int)N - 1;
            while (pos > 0 && bestD[pos - 1] > d2)
            {
                bestD[pos] = bestD[pos - 1];
                std::memcpy(&rec[pos * 6], &rec[(pos - 1) * 6], 6 * sizeof(float));
                pos--;
            }
            bestD[pos]       = d2;
            rec[pos * 6 + 0] = ec[d * 3 + 0];
            rec[pos * 6 + 1] = ec[d * 3 + 1];
            rec[pos * 6 + 2] = ec[d * 3 + 2];
            rec[pos * 6 + 3] = em[(size_t)e * 14 + 9];    // Le.r
            rec[pos * 6 + 4] = em[(size_t)e * 14 + 10];   // Le.g
            rec[pos * 6 + 5] = em[(size_t)e * 14 + 11];   // Le.b
            if (cnt < (int)N)
                cnt++;
        }

        // Single sequential write of the whole slot (records [cnt,N) already zeroed in rec).
        std::memcpy(&out[(size_t)si * subF], rec, subF * sizeof(float));
    }
}

// Fill the cached static NEE emitter set (g.staticEmit/g.staticWgt) from a mesh
// vertex array: every triangle whose material Le > 0, with a 14-float record
// (v0[3] v1[3] v2[3] Le[3] cdf pdf) and a power weight (luminance(Le) * area).
// `v` is the STATIC baked mesh at level load, or the LIVE per-frame vertex buffer
// (g.vbufMapped) on a refresh after a switch press/revert or an animated-texture
// swap changed a face's live texnum (DOOM-0082) — reading the live buffer means a
// now-lit switch enters the light set and a reverted one drops out. The Le table
// is WAD-global (g.matEmissive); the vertex count is the mesh's (both buffers share it).
static void BuildStaticEmitterSet(const rb_vertex_t* v)
{
    g.staticEmit.clear();
    g.staticWgt.clear();
    if (!g.levelMesh || g.matEmissive.empty() || !v)
        return;

    const int matCount = (int)(g.matEmissive.size() / 3);
    const int numtris  = g.levelMesh->numverts / 3;
    g.staticEmit.reserve(64 * 14);
    g.staticWgt.reserve(64);
    for (int t = 0; t < numtris; t++)
    {
        const rb_vertex_t* tri = &v[t * 3];
        const int texnum = tri->texnum;
        const int id = (tri->flags & RB_MESH_FLAT) ? g.matNumWall + texnum : texnum;
        if (id < 0 || id >= matCount)
            continue;
        const float* le = &g.matEmissive[(size_t)id * 3];
        if (le[0] + le[1] + le[2] <= 0.0f)
            continue;   // material isn't a light source

        for (int k = 0; k < 3; k++)
        {
            g.staticEmit.push_back(tri[k].x); g.staticEmit.push_back(tri[k].y); g.staticEmit.push_back(tri[k].z);
        }
        g.staticEmit.push_back(le[0]); g.staticEmit.push_back(le[1]); g.staticEmit.push_back(le[2]);
        g.staticEmit.push_back(0.0f); g.staticEmit.push_back(0.0f);   // cdf, pdf — finalised later

        const float ex1 = tri[1].x - tri[0].x, ey1 = tri[1].y - tri[0].y, ez1 = tri[1].z - tri[0].z;
        const float ex2 = tri[2].x - tri[0].x, ey2 = tri[2].y - tri[0].y, ez2 = tri[2].z - tri[0].z;
        const float cx = ey1 * ez2 - ez1 * ey2;
        const float cy = ez1 * ex2 - ex1 * ez2;
        const float cz = ex1 * ey2 - ey1 * ex2;
        const float area = 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
        g.staticWgt.push_back(emis::luminance(le[0], le[1], le[2]) * area);
    }
    g.staticLightsDirty = true;   // DOOM-0170 perf: static set changed -> recache point lights
}

// Build this level's NEE emitter list (DOOM-0009 build step 3b): the subset of
// static mesh triangles whose material is emissive (per-material Le from
// ComputeMaterialEmissive). Each record is 14 tight floats — v0[3] v1[3] v2[3]
// Le[3] cdf pdf — written into a host-visible buffer the megakernel samples for
// direct lighting (step 3c). The static set is cached (g.staticEmit) so each traced
// frame can append emissive world sprites (DOOM-0084) without re-scanning the mesh;
// FinalizeEmitters merges them and rebuilds the CDF. Rebuilt per level (the geometry
// changes); the Le table it reads is WAD-global. Positions are the baked (static)
// heights — emitters on a moving sector would lag, acceptable for now. Switch
// ON-faces are not yet emitters (DOOM-0066 live swap, a DOOM-0082 follow-up).
void BuildEmitterList()
{
    if (g.emitBuf) { vkDestroyBuffer(g.device, g.emitBuf, nullptr); g.emitBuf = VK_NULL_HANDLE; }
    if (g.emitMem) { vkFreeMemory(g.device, g.emitMem, nullptr);    g.emitMem = VK_NULL_HANDLE; }
    if (g.emitSecBuf) { vkDestroyBuffer(g.device, g.emitSecBuf, nullptr); g.emitSecBuf = VK_NULL_HANDLE; }
    if (g.emitSecMem) { vkFreeMemory(g.device, g.emitSecMem, nullptr);    g.emitSecMem = VK_NULL_HANDLE; }
    g.emitSecMapped = nullptr;
    g.emitMapped = nullptr;
    g.emitCount  = 0;
    g.emitCap    = 0;
    g.staticEmit.clear();
    g.staticWgt.clear();

    if (!g.rtEnabled || !g.levelMesh || g.matEmissive.empty())
        return;

    // Fill the cached static emitter set from the baked mesh. A later switch press
    // or animated-texture swap re-runs BuildStaticEmitterSet on the LIVE vertex
    // buffer (g.vbufMapped) so a now-lit switch enters the light set (DOOM-0082).
    BuildStaticEmitterSet(g.levelMesh->verts);

    // Host-visible, persistently-mapped emitter buffer sized for the static set plus
    // a per-frame budget of emissive-sprite triangles (DOOM-0084). The megakernel
    // reads it by device address; FinalizeEmitters refills it each traced frame.
    g.emitCap = (uint32_t)g.staticWgt.size() + SPR_EMIT_MAX;
    CreateRtBuffer((VkDeviceSize)g.emitCap * 14u * sizeof(float),
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.emitBuf, &g.emitMem);
    Check(vkMapMemory(g.device, g.emitMem, 0, VK_WHOLE_SIZE, 0, &g.emitMapped), "vkMapMemory(emit)");

    // DOOM-0119: parallel per-emitter sector buffer (host-visible, same record cap),
    // refilled by FinalizeEmitters each frame. One uint per emitter record. Created
    // before the static-only fill below so that fill tags the static set's sentinels.
    CreateRtBuffer((VkDeviceSize)g.emitCap * sizeof(uint32_t),
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.emitSecBuf, &g.emitSecMem);
    Check(vkMapMemory(g.device, g.emitSecMem, 0, VK_WHOLE_SIZE, 0, &g.emitSecMapped), "vkMapMemory(emitSec)");

    // Static-only initial fill — this is what the level-load GI bake reads (no
    // sprites at bake time); per-frame fills add the emissive sprites.
    FinalizeEmitters(nullptr, nullptr);

    printf("RB_Vulkan: %u static emitter triangles for NEE (power-sampled; +emissive sprites/frame).\n",
           (uint32_t)g.staticWgt.size());
    fflush(stdout);
}

// 16 floats per GI probe: pos[3] + pad + SH-L1 directional irradiance (channel-
// major R[4] G[4] B[4]). Must match the bake shader and the megakernel's read.
static const uint32_t PROBE_FLOATS = 16;

// Run the GI bake (DOOM-0009 build step 4b-ii): dispatch bake.comp once over the
// just-placed probe buffer, filling each probe's SH-L1 radiance, then read it back
// to verify the bake produced finite, non-zero values. Called at the tail of
// BuildProbes (positions uploaded, SH zeroed). Needs the per-level TLAS + the
// materials/emitter/Le buffers, so it runs after BuildAccelerationStructures and
// BuildEmitterList. The probe buffer is host-visible + coherent, so after the
// one-shot dispatch waits idle, the host map sees the GPU's writes directly.
void RunGiBake()
{
    if (!g.rtEnabled || g.probeBuf == VK_NULL_HANDLE || g.probeCount == 0 ||
        g.tlas == VK_NULL_HANDLE || g.bakePipeline == VK_NULL_HANDLE ||
        g.matEmisBuf == VK_NULL_HANDLE)
        return;

    // Point the bake set's TLAS half at this level's freshly built TLAS.
    VkWriteDescriptorSetAccelerationStructureKHR asInfo = {};
    asInfo.sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asInfo.accelerationStructureCount = 1;
    asInfo.pAccelerationStructures    = &g.tlas;
    VkWriteDescriptorSet w = {};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.pNext           = &asInfo;
    w.dstSet          = g.bakeDs;
    w.dstBinding      = 0;
    w.descriptorCount = 1;
    w.descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    vkUpdateDescriptorSets(g.device, 1, &w, 0, nullptr);

    struct BakePush {
        uint32_t misc[4];       // probeCount, numWall, emitterCount, giEnabled
        uint64_t vertsAddr;
        uint64_t emitAddr;      // 0 if this level has no emitters (shader guards on count)
        uint64_t matEmisAddr;
        uint64_t probeAddr;     // write target (this pass's output)
        uint64_t prevProbeAddr; // read source (previous bounce's probes)
        uint64_t triSsAddr;
        uint64_t emitSecAddr;   // DOOM-0119: always 0 (the bake's REJECT cull is dead);
        uint64_t rejectAddr;    // present only to match the shared shadeSurface signature.
    } bp = {};
    static_assert(sizeof(BakePush) == 80, "bake push-constant layout must match the shader");
    bp.misc[0]     = g.probeCount;
    bp.misc[1]     = (uint32_t)g.matNumWall;
    bp.misc[2]     = g.emitCount;
    bp.vertsAddr   = BufferAddress(g.vbuf);
    bp.emitAddr    = g.emitBuf ? BufferAddress(g.emitBuf) : 0;
    bp.matEmisAddr = BufferAddress(g.matEmisBuf);
    bp.triSsAddr   = BufferAddress(g.triSsBuf);

    // Multi-bounce GI (user request): pass 1 bakes 1-bounce indirect; each further
    // pass feeds the previous pass's probes back in (giEnabled) for one more bounce.
    // Ping-pong probeBuf <-> probeBuf2 (read prev, write current); start so the FINAL
    // bounce lands in probeBuf (the buffer the megakernel reads): for an odd bounce
    // count that means starting the write on probeBuf. Per-frame cost is unchanged
    // (still one probe lookup); only this one-time bake does the extra passes.
    const int      BOUNCES = 3;
    VkBuffer       wbuf = (BOUNCES & 1) ? g.probeBuf : g.probeBuf2;
    VkBuffer       rbuf = (BOUNCES & 1) ? g.probeBuf2 : g.probeBuf;
    VkDescriptorSet sets[2] = { g.bakeDs, g.ds };
    for (int b = 0; b < BOUNCES; b++)
    {
        bp.misc[3]        = (b > 0) ? 1u : 0u;          // giEnabled on bounces 2+
        bp.probeAddr      = BufferAddress(wbuf);
        bp.prevProbeAddr  = BufferAddress(rbuf);

        VkCommandBuffer cb = BeginOneTime();
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g.bakePipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                g.bakePipeLayout, 0, 2, sets, 0, nullptr);
        vkCmdPushConstants(cb, g.bakePipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(bp), &bp);
        vkCmdDispatch(cb, (g.probeCount + 63) / 64, 1, 1);
        EndOneTime(cb);   // waits idle: this bounce is done before the next reads it
        std::swap(wbuf, rbuf);
    }
    // After an odd bounce count the final pass wrote g.probeBuf — readback below.

    // Verify: scan the final SH payload (floats 4..15 of each record) for
    // finiteness and non-zero energy, and report the mean DC (l=0) irradiance.
    void* mapped = nullptr;
    Check(vkMapMemory(g.device, g.probeMem, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(probeReadback)");
    const float* pf = (const float*)mapped;
    int   nonFinite = 0, nonZero = 0;
    double dcSum[3] = { 0, 0, 0 };
    for (uint32_t i = 0; i < g.probeCount; i++)
    {
        const float* sh = &pf[(size_t)i * PROBE_FLOATS + 4];   // 12 SH floats
        for (int k = 0; k < 12; k++)
        {
            if (!std::isfinite(sh[k]))      nonFinite++;
            else if (sh[k] != 0.0f)         nonZero++;
        }
        // DC term per channel (sh[0], sh[4], sh[8]) * Y00 = average radiance.
        dcSum[0] += sh[0] * 0.282095; dcSum[1] += sh[4] * 0.282095; dcSum[2] += sh[8] * 0.282095;
    }
    vkUnmapMemory(g.device, g.probeMem);
    printf("RB_Vulkan: GI bake done (3 bounces) — %u probes, %d non-finite SH coeffs, "
           "%d non-zero; mean DC irradiance rgb(%.3f, %.3f, %.3f)\n",
           g.probeCount, nonFinite, nonZero,
           dcSum[0] / g.probeCount, dcSum[1] / g.probeCount, dcSum[2] / g.probeCount);
    fflush(stdout);
}

// INV-6 headless self-test (DOOM-0009 build step 4d). Converge the megakernel's
// DIRECT-only lighting two independent ways at the current camera — power-importance
// NEE (the shipping estimator) and a brute-force cosine-hemisphere unidirectional
// estimator — and assert their images agree within the spec's 0.5% rel-MSE bar. The
// two share NO sampling machinery (the brute force doesn't even read the emitter
// list), so agreement proves the NEE integrator is unbiased. A white-furnace pass
// independently checks the pdf/throughput math integrates to exactly 1. Drives the
// mode-5 verify path over the rgba32f accumulator (binding 2), reads it back, and
// computes the metrics on the CPU. Called once from the first ready present when
// `-rtverify` is set; the caller exits afterward.
void RB_RtVerify()
{
    const uint32_t W = kVerifyW, H = kVerifyH;
    const uint32_t pxCount = W * H;

    // Same camera basis RecordRtTrace builds, so the verify view is what the player
    // sees at the trigger frame. mode 5 (verify accumulate) + the scene addresses.
    const float cc = std::cos(g.lastView.angle), ss = std::sin(g.lastView.angle);
    struct RtPC {
        float    camPos[4]; float camDir[4]; float camRight[4]; float camUp[4];
        uint32_t misc[4]; uint32_t misc2[4]; uint32_t misc3[4];
        // misc5 is pure padding for mode 5, but MUST be present so the buffer-address
        // fields below land at the SAME push-constant offsets the shader reads them from. The
        // megakernel reads pc.misc4.x (sprite id base, line 717), pc.misc4.y (omniStart,
        // DOOM-0122) and dereferences pc.emit (line 736) in mode 5; without this padding
        // those addresses shift 32 bytes and the NEE loop dereferences garbage (device-lost).
        // (Latent since DOOM-0100 added misc4; DOOM-0179's misc5 widened it — fixed here so
        // -rtverify matches the layout again.)
        uint32_t misc4[4]; uint32_t misc5[4];
        uint64_t vertsAddr, emitAddr, matEmisAddr, probeAddr, triSsAddr;
    } pc = {};
    static_assert(sizeof(RtPC) == 184, "verify push-constant layout must match the shader");
    pc.camPos[0] = g.lastView.x; pc.camPos[1] = g.lastView.y; pc.camPos[2] = g.lastView.z;
    pc.camDir[0] = cc;           pc.camDir[1] = ss;
    pc.camRight[0] = ss;         pc.camRight[1] = -cc;        pc.camRight[3] = 1.0f;
    pc.camUp[2]  = 1.0f;         pc.camUp[3] = (float)H / (float)W;
    pc.misc[0] = 5u; pc.misc[1] = W; pc.misc[2] = H; pc.misc[3] = (uint32_t)g.matNumWall;
    pc.misc2[0] = g.emitCount; pc.misc2[1] = g.probeCount;
    pc.misc4[0] = (uint32_t)(g.matNumWall + g.matNumFlat);   // sprite id base (mode 5 sprite decode)
    pc.misc4[1] = (uint32_t)g.staticWgt.size();              // DOOM-0122: real omniStart (static|omni split) so the verify path exercises the omni NEE loop, matching the display path
    pc.vertsAddr   = BufferAddress(g.vbuf);
    pc.emitAddr    = g.emitBuf    ? BufferAddress(g.emitBuf)    : 0;
    pc.matEmisAddr = g.matEmisBuf ? BufferAddress(g.matEmisBuf) : 0;
    pc.probeAddr   = g.probeBuf   ? BufferAddress(g.probeBuf)   : 0;   // unused by mode 5
    pc.triSsAddr   = g.triSsBuf   ? BufferAddress(g.triSsBuf)   : 0;

    // Set 3 = the DOOM-0042 HD materials, seeded all-paletted by InitHdDefault (this
    // path runs before any level's Ultra Present, so g.hdSet is the default here).
    VkDescriptorSet sets[4] = { g.rtDs, g.ds, g.svgfDs, g.hdSet };

    // The display image (binding 1) is statically referenced by the shader even in
    // mode 5, and its descriptor advertises GENERAL — but no frame has been rendered
    // this run, so it is still UNDEFINED. Park it in GENERAL so the verify dispatches
    // validate clean (the accumulator at binding 2 was parked at creation).
    {
        VkCommandBuffer cb = BeginOneTime();
        VkImageMemoryBarrier toGen = {};
        toGen.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toGen.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
        toGen.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
        toGen.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGen.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGen.image            = g.rtImage;
        toGen.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        toGen.srcAccessMask    = 0;
        toGen.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGen);
        EndOneTime(cb);
    }

    // Converge one estimator into the accumulator, then read it back into `out`
    // (pxCount * 4 floats: rgb radiance sum + sample count). Each step is its OWN
    // one-time submit: EndOneTime waits the queue idle, so the clear -> dispatches ->
    // copy stay ordered (and the accumulator's read-add-write hazard between passes
    // is covered) without any one submission running long enough to trip the GPU's
    // timeout watchdog (a single all-dispatches submit device-loses on the heavier
    // all-lights estimator). All accumulator ops run in GENERAL (clear/storage/copy).
    auto runEstimator = [&](uint32_t estimator, uint32_t dispatches, uint32_t sppPer,
                            std::vector<float>& out)
    {
        pc.misc3[1] = sppPer;
        pc.misc3[2] = estimator;

        VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        {
            VkCommandBuffer cb = BeginOneTime();
            VkClearColorValue zero = {};
            vkCmdClearColorImage(cb, g.rtAccum, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
            EndOneTime(cb);
        }

        for (uint32_t d = 0; d < dispatches; d++)
        {
            pc.misc3[0] = d * sppPer;       // advance the per-sample seed base
            VkCommandBuffer cb = BeginOneTime();
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, RtPipelineForMode(5u));
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    g.rtPipeLayout, 0, 4, sets, 0, nullptr);
            vkCmdPushConstants(cb, g.rtPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cb, (W + 7) / 8, (H + 7) / 8, 1);
            EndOneTime(cb);
        }

        {
            VkCommandBuffer cb = BeginOneTime();
            VkBufferImageCopy region = {};
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageExtent      = { W, H, 1 };
            vkCmdCopyImageToBuffer(cb, g.rtAccum, VK_IMAGE_LAYOUT_GENERAL, g.rtReadback, 1, &region);
            EndOneTime(cb);
        }

        void* mapped = nullptr;
        Check(vkMapMemory(g.device, g.rtReadbackMem, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(rtVerify)");
        out.assign((const float*)mapped, (const float*)mapped + (size_t)pxCount * 4);
        vkUnmapMemory(g.device, g.rtReadbackMem);
    };

    std::vector<float> nee, brute, furnace;
    runEstimator(0u, 256u, 64u, nee);       // power-NEE:          16384 spp
    runEstimator(1u,  64u, 64u, brute);     // brute (all lights):  4096 spp (low var)
    runEstimator(2u,   4u, 64u, furnace);   // white furnace:        256 spp (exact)

    // rel-MSE between the two converged direct-light images over pixels both hit:
    // sum (nee-brute)^2 / sum brute^2, summed over RGB.
    double num = 0.0, den = 0.0;
    int    litPx = 0;
    for (uint32_t i = 0; i < pxCount; i++)
    {
        const float* a = &nee[(size_t)i * 4];
        const float* b = &brute[(size_t)i * 4];
        if (a[3] <= 0.0f || b[3] <= 0.0f) continue;     // background (a primary miss)
        litPx++;
        for (int ch = 0; ch < 3; ch++)
        {
            double ma = a[ch] / a[3], mb = b[ch] / b[3];
            num += (ma - mb) * (ma - mb);
            den += mb * mb;
        }
    }
    double relMSE = (den > 0.0) ? num / den : 0.0;

    // White furnace: every hit pixel's converged mean must be 1.0 (brdf*cos/pdf==1).
    double furnMaxDev = 0.0;
    for (uint32_t i = 0; i < pxCount; i++)
    {
        const float* f = &furnace[(size_t)i * 4];
        if (f[3] <= 0.0f) continue;
        double dev = std::fabs((double)f[0] / f[3] - 1.0);
        if (dev > furnMaxDev) furnMaxDev = dev;
    }

    // DOOM-0122: report how much of the omni sprite-light NEE loop the verify view
    // exercised. omniStart == emitCount means the view had no dynamic sprite emitters
    // (static-only, as before DOOM-0084); a non-zero omni count means the omni branch
    // was checked against the brute-force reference too.
    const uint32_t vStaticN = (uint32_t)g.staticWgt.size();
    const uint32_t vOmniN   = (g.emitCount > vStaticN) ? (g.emitCount - vStaticN) : 0u;
    printf("[rtverify] emitters: %u total, omniStart=%u -> %u omni sprite emitter(s) "
           "covered by the verify path (DOOM-0122)\n", g.emitCount, vStaticN, vOmniN);

    const double bar = 0.005;       // INV-6 acceptance: <= 0.5% rel-MSE
    printf("[rtverify] INV-6 direct-light rel-MSE = %.4f%% over %d lit px "
           "(power-NEE 16384 spp vs brute-force/all-lights 4096 spp): %s (bar 0.50%%)\n",
           relMSE * 100.0, litPx, (relMSE <= bar) ? "PASS" : "FAIL");
    printf("[rtverify] white-furnace max deviation from 1.0 = %.6f: %s\n",
           furnMaxDev, (furnMaxDev < 1e-3) ? "PASS" : "FAIL");
    fflush(stdout);
}

// Place this level's GI-bake probes (DOOM-0009 build step 4b-i): one per subsector
// (RB_BuildProbes), uploaded as PROBE_FLOATS records with the SH payload zeroed —
// the bake compute pass (4b-ii) fills it, the megakernel reads it (4c). Rebuilt
// per level; no-op without RT. The buffer is host-visible so the bake's GPU writes
// stay coherent for the trace's reads.
void BuildProbes()
{
    if (g.probeBuf)  { vkDestroyBuffer(g.device, g.probeBuf, nullptr);  g.probeBuf  = VK_NULL_HANDLE; }
    if (g.probeMem)  { vkFreeMemory(g.device, g.probeMem, nullptr);     g.probeMem  = VK_NULL_HANDLE; }
    if (g.probeBuf2) { vkDestroyBuffer(g.device, g.probeBuf2, nullptr); g.probeBuf2 = VK_NULL_HANDLE; }
    if (g.probeMem2) { vkFreeMemory(g.device, g.probeMem2, nullptr);    g.probeMem2 = VK_NULL_HANDLE; }
    if (g.triSsBuf)  { vkDestroyBuffer(g.device, g.triSsBuf, nullptr);  g.triSsBuf  = VK_NULL_HANDLE; }
    if (g.triSsMem)  { vkFreeMemory(g.device, g.triSsMem, nullptr);     g.triSsMem  = VK_NULL_HANDLE; }
    if (g.subSecBuf) { vkDestroyBuffer(g.device, g.subSecBuf, nullptr); g.subSecBuf = VK_NULL_HANDLE; }
    if (g.subSecMem) { vkFreeMemory(g.device, g.subSecMem, nullptr);    g.subSecMem = VK_NULL_HANDLE; }
    if (g.rejectBuf) { vkDestroyBuffer(g.device, g.rejectBuf, nullptr); g.rejectBuf = VK_NULL_HANDLE; }
    if (g.rejectMem) { vkFreeMemory(g.device, g.rejectMem, nullptr);    g.rejectMem = VK_NULL_HANDLE; }
    for (uint32_t s = 0; s < VulkanState::kFramesInFlight; s++)   // DOOM-0074: per-slot
    {
        if (g.lightBufSlot[s]) { vkDestroyBuffer(g.device, g.lightBufSlot[s], nullptr); g.lightBufSlot[s] = VK_NULL_HANDLE; }
        if (g.lightMemSlot[s]) { vkFreeMemory(g.device, g.lightMemSlot[s], nullptr);    g.lightMemSlot[s] = VK_NULL_HANDLE; }
        g.lightMappedSlot[s] = nullptr;
    }
    g.lightBuf = VK_NULL_HANDLE; g.lightMem = VK_NULL_HANDLE; g.lightMapped = nullptr;
    g.subCentroid.clear();
    g.subSecSector.clear();
    g.rejectCPU  = nullptr;
    g.probeCount = 0;
    g.numSectors = 0;

    if (!g.rtEnabled || !g.levelMesh)
        return;

    const int n = RB_NumSubsectors();
    if (n <= 0)
        return;

    std::vector<rb_probe_t> probes(n);
    const int got = RB_BuildProbes(probes.data(), n);
    g.probeCount = (uint32_t)got;

    // DOOM-0170 L1b: cache the subsector centroids (the cull's per-subsector ranking
    // point) and allocate the per-subsector point-light buffer. Host-visible + mapped:
    // BuildRasterPointLights refills it each raster frame. Sized for `got` subsectors, so
    // probeCount>0 guarantees mesh.frag's light buffer is bound (the same guard gates the
    // bounce). Zeroed now so a frame drawn before the first fill reads no lights.
    g.subCentroid.assign(probes.begin(), probes.begin() + got);
    {
        const VkDeviceSize lsz = (VkDeviceSize)got * RASTER_MAX_LIGHTS_PER_SUBSECTOR
                               * RASTER_LIGHT_FLOATS * sizeof(float);
        // DOOM-0074: one copy per in-flight slot (build-ahead writes the next frame's
        // point-light list while the GPU shades the current frame from the other).
        for (uint32_t s = 0; s < VulkanState::kFramesInFlight; s++)
        {
            CreateRtBuffer(lsz,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &g.lightBufSlot[s], &g.lightMemSlot[s]);
            Check(vkMapMemory(g.device, g.lightMemSlot[s], 0, VK_WHOLE_SIZE, 0, &g.lightMappedSlot[s]), "vkMapMemory(light)");
            std::memset(g.lightMappedSlot[s], 0, (size_t)lsz);
        }
        g.lightBuf    = g.lightBufSlot[g.frameSlot];
        g.lightMem    = g.lightMemSlot[g.frameSlot];
        g.lightMapped = g.lightMappedSlot[g.frameSlot];
    }

    std::vector<float> rec((size_t)got * PROBE_FLOATS, 0.0f);   // SH zeroed
    for (int i = 0; i < got; i++)
    {
        rec[(size_t)i * PROBE_FLOATS + 0] = probes[i].x;
        rec[(size_t)i * PROBE_FLOATS + 1] = probes[i].y;
        rec[(size_t)i * PROBE_FLOATS + 2] = probes[i].z;
    }

    // Both probe buffers get the positions (SH zeroed): the bake ping-pongs between
    // them across bounce passes, and each pass reads its WRITE buffer's positions.
    UploadAddressBuffer(rec.data(), (VkDeviceSize)rec.size() * sizeof(float),
                        &g.probeBuf, &g.probeMem);
    UploadAddressBuffer(rec.data(), (VkDeviceSize)rec.size() * sizeof(float),
                        &g.probeBuf2, &g.probeMem2);

    // Per-triangle subsector id (step 4c): the megakernel + the multi-bounce bake
    // index it by primitive id to find a hit's room probe. uint32 per triangle.
    UploadAddressBuffer(g.levelMesh->tri_ss,
                        (VkDeviceSize)g.levelMesh->numtris * sizeof(int),
                        &g.triSsBuf, &g.triSsMem);

    // DOOM-0119: REJECT-lump light-cull data. subSec maps subsector -> owning sector
    // (same order as the probes/tri_ss above), and reject is the WAD REJECT bitmatrix
    // uploaded as uint words (the shader extracts the byte then the bit). The
    // megakernel skips an omni sprite light whose sector this hit's sector provably
    // can't see. Left disabled (numSectors 0) when the level has no REJECT lump.
    int rejectBytes = 0;
    const unsigned char* reject = RB_RejectMatrix(&rejectBytes);
    const int numSec = RB_NumSectors();
    if (reject && rejectBytes > 0 && numSec > 0)
    {
        std::vector<int32_t> subSec(n);
        RB_BuildSubsectorSectors(subSec.data(), n);
        UploadAddressBuffer(subSec.data(), (VkDeviceSize)subSec.size() * sizeof(int32_t),
                            &g.subSecBuf, &g.subSecMem);

        // DOOM-0170 L1b: keep CPU copies for the raster point-light cull. The GPU path
        // (megakernel) reads the uploaded buffers; the raster cull runs on the CPU each
        // frame, so it needs the subsector->sector map + the raw REJECT bytes here. The
        // reject pointer is the PU_LEVEL lump, valid for this level's lifetime.
        g.subSecSector.assign(subSec.begin(), subSec.begin() + got);
        g.rejectCPU = reject;

        // Pad the byte matrix up to a uint-word multiple so the shader's last word
        // read is fully backed; the high padding bytes are never addressed.
        std::vector<uint32_t> rej((size_t)((rejectBytes + 3) / 4), 0u);
        std::memcpy(rej.data(), reject, (size_t)rejectBytes);
        UploadAddressBuffer(rej.data(), (VkDeviceSize)rej.size() * sizeof(uint32_t),
                            &g.rejectBuf, &g.rejectMem);
        g.numSectors = (uint32_t)numSec;
        printf("RB_Vulkan: DOOM-0119 REJECT cull active (%d sectors, %d-byte matrix).\n",
               numSec, rejectBytes);
        fflush(stdout);
    }

    // 4b-i verification: probe count + the spatial bounds of the placed probes
    // (should sit inside the map's coordinate extent — a sanity check on placement).
    float lo[3] = {  1e30f,  1e30f,  1e30f };
    float hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < got; i++)
    {
        const float* p = &rec[(size_t)i * PROBE_FLOATS];
        for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], p[k]); hi[k] = std::max(hi[k], p[k]); }
    }
    printf("RB_Vulkan: %u GI probes (1/subsector). bounds x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n",
           g.probeCount, lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
    fflush(stdout);

    // Build step 4b-ii: bake the indirect irradiance into the probes now that the
    // positions are uploaded (and the TLAS / emitter / Le buffers are ready).
    RunGiBake();
}

extern "C" void RB_Vulkan_BuildLevel(void)
{
    // Convert the freshly-loaded map to a 3D triangle mesh (r_mesh.c) and upload
    // it to a GPU vertex buffer for the raster pass. The ray-tracing accel-
    // eration structure (BLAS/TLAS) for the path tracer lands in a later
    // increment; a host-visible buffer (and plain device memory rather than VMA)
    // is enough for one static per-level mesh — VMA arrives with the many
    // image/buffer allocations of the materials increment.

    // DOOM-0093/indie-review: this rebuild frees and recreates live GPU buffers
    // (vertex slots, sky mesh, and via the rebuild the emitter/probe/light buffers)
    // that the last submitted frame may still be reading. The mode-switch, recreate
    // and shutdown paths all drain first; the level-load path must too, or the
    // in-flight frame can use-after-free g.vbuf. Level load is not perf-critical.
    if (g.device)
        vkDeviceWaitIdle(g.device);

    if (g.levelMesh)
    {
        RB_FreeMesh(g.levelMesh);
        g.levelMesh = nullptr;
    }

    // DOOM-0042: a new map uses a different texnum set, so invalidate the HD material
    // build. EnsureHdMaterials rebuilds it (in Ultra) at the next Present, freeing the
    // prior set then — so the old set stays valid until replaced (no unbound window).
    g.hdBuilt = false;

    // The texture atlas is WAD-global; build + upload it on the first level only.
    if (!g.atlasReady)
        UploadAtlas();

    g.levelMesh = RB_BuildLevelMesh();

    printf("RB_Vulkan_BuildLevel: %d triangles (%d vertices) from the map.\n",
           g.levelMesh->numtris, g.levelMesh->numverts);
    fflush(stdout);

    // (Re)create the vertex buffer sized to this level's mesh. DOOM-0074: one copy per
    // in-flight slot (the build-ahead re-height writes the next frame's slot).
    for (uint32_t s = 0; s < VulkanState::kFramesInFlight; s++)
    {
        if (g.vbufMappedSlot[s]) { vkUnmapMemory(g.device, g.vbufMemSlot[s]); g.vbufMappedSlot[s] = nullptr; }
        if (g.vbufSlot[s])       { vkDestroyBuffer(g.device, g.vbufSlot[s], nullptr); g.vbufSlot[s] = VK_NULL_HANDLE; }
        if (g.vbufMemSlot[s])    { vkFreeMemory(g.device, g.vbufMemSlot[s], nullptr); g.vbufMemSlot[s] = VK_NULL_HANDLE; }
    }
    g.vbuf = VK_NULL_HANDLE; g.vbufMemory = VK_NULL_HANDLE; g.vbufMapped = nullptr;
    g.vertexCount = 0;

    // DOOM-0141: (re)create the RT-only sky backdrop vertex buffer for this level (the
    // sky BLAS built from it is torn down in DestroyAccelerationStructures).
    if (g.skyMeshBuf) { vkDestroyBuffer(g.device, g.skyMeshBuf, nullptr); g.skyMeshBuf = VK_NULL_HANDLE; }
    if (g.skyMeshMem) { vkFreeMemory(g.device, g.skyMeshMem, nullptr); g.skyMeshMem = VK_NULL_HANDLE; }
    g.skyMeshVerts = 0;
    g.skyMeshTexnum    = 0;

    VkDeviceSize size = (VkDeviceSize)g.levelMesh->numverts * sizeof(rb_vertex_t);
    if (size == 0)
        return;   // empty map (no drawable geometry); nothing to upload

    // DOOM-0074: allocate + fill both in-flight slots with identical geometry. The
    // build-ahead re-height writes the next frame's slot while the GPU reads the other.
    for (uint32_t s = 0; s < VulkanState::kFramesInFlight; s++)
    {
        VkBufferCreateInfo bci = {};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = size;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        // With RT on, the same buffer is read as BLAS build input (by GPU address), so
        // it also needs the AS-input + device-address usage and a device-address alloc.
        if (g.rtEnabled)
            bci.usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                       | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        Check(vkCreateBuffer(g.device, &bci, nullptr, &g.vbufSlot[s]), "vkCreateBuffer(vbuf)");

        VkMemoryRequirements req = {};
        vkGetBufferMemoryRequirements(g.device, g.vbufSlot[s], &req);
        VkMemoryAllocateFlagsInfo vbufFlags = {};
        vbufFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        vbufFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo mai = {};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.pNext = g.rtEnabled ? &vbufFlags : nullptr;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        Check(vkAllocateMemory(g.device, &mai, nullptr, &g.vbufMemSlot[s]), "vkAllocateMemory(vbuf)");
        Check(vkBindBufferMemory(g.device, g.vbufSlot[s], g.vbufMemSlot[s], 0), "vkBindBufferMemory(vbuf)");

        // Kept mapped for the whole level: RB_UpdateMeshHeights patches moving-sector
        // z's into it each frame (host-coherent, so no flush). Unmapped on rebuild
        // (above) and shutdown.
        Check(vkMapMemory(g.device, g.vbufMemSlot[s], 0, size, 0, &g.vbufMappedSlot[s]), "vkMapMemory(vbuf)");
        std::memcpy(g.vbufMappedSlot[s], g.levelMesh->verts, (size_t)size);
    }
    // Both slots hold identical geometry now, so the BLAS built below (from g.vbuf)
    // matches whichever slot is active on the first traced frame.
    g.vbuf       = g.vbufSlot[g.frameSlot];
    g.vbufMemory = g.vbufMemSlot[g.frameSlot];
    g.vbufMapped = g.vbufMappedSlot[g.frameSlot];

    g.vertexCount = (uint32_t)g.levelMesh->numverts;

    // DOOM-0141: upload this level's sky backdrop tris (if any) into a host-visible
    // buffer the static sky BLAS builds from (in BuildAccelerationStructures, below).
    // skyTexnum is read off the verts (all carry texnum == skytexture) so the trace
    // can sample the sky panorama without reaching across the C/C++ seam.
    if (g.rtEnabled && g.levelMesh->numsky >= 3 && g.levelMesh->sky)
    {
        VkDeviceSize ssz = (VkDeviceSize)g.levelMesh->numsky * sizeof(rb_vertex_t);
        CreateRtBuffer(ssz,
                       VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                       | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                       // DOOM-0162: also draw this occluder mesh in the raster pass
                       // (depth-tested sky) so distant geometry stops floating there,
                       // matching the RT view -- needs the vertex-buffer usage bit.
                       | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &g.skyMeshBuf, &g.skyMeshMem);
        void* skyMapped = nullptr;
        Check(vkMapMemory(g.device, g.skyMeshMem, 0, ssz, 0, &skyMapped), "vkMapMemory(sky)");
        std::memcpy(skyMapped, g.levelMesh->sky, (size_t)ssz);
        vkUnmapMemory(g.device, g.skyMeshMem);
        g.skyMeshVerts = (uint32_t)g.levelMesh->numsky;
        g.skyMeshTexnum    = g.levelMesh->sky[0].texnum;
    }

    // DOOM-0009 build step 2b: (re)build the static BLAS + TLAS over this mesh so
    // the path tracer has something to trace against. No-op without RT.
    if (g.rtEnabled)
        BuildAccelerationStructures();

    // Build step 3b: extract this level's NEE emitter triangles from the mesh +
    // the WAD-global Le table. No-op without RT (or before the atlas is uploaded).
    BuildEmitterList();

    // Build step 4b-i: place this level's per-subsector GI probes. (The bake that
    // fills them — 4b-ii — runs after this; 4c reads them in the megakernel.)
    BuildProbes();
}

// Record the path-tracer debug frame into g.cmd (caller began the buffer):
// dispatch the megakernel into rtImage, then blit it onto the acquired
// swapchain image. Assumes rtActive was checked (RT on, TLAS + camera + pipeline
// present). The submit that follows waits the acquire semaphore at TRANSFER.
// Halton radical-inverse low-discrepancy value, for the build step 6-d sub-pixel
// camera jitter. Bases 2 and 3 over a 16-frame cycle give a well-distributed sample
// pattern the temporal upscaler integrates into reconstructed sub-pixel detail.
static float RadicalInverse(uint32_t i, uint32_t base)
{
    float inv = 1.0f / (float)base, r = 0.0f, f = inv;
    while (i > 0u) { r += (float)(i % base) * f; i /= base; f *= inv; }
    return r;
}

void RecordRtTrace(uint32_t idx)
{
    const uint32_t dispW = g.extent.width, dispH = g.extent.height;

    // build step 6-d: the temporal upscaler runs only on the mode-6 denoised path
    // with the Upscaler set to TAAU. When active, the path tracer + SVGF render into a
    // render-scale sub-rectangle of the (display-sized) storage images and TAAU
    // reconstructs the full display image; otherwise everything runs at display
    // resolution and the present path blits rtImage as before (no behaviour change).
    const bool taauActive = (rb_rtdebug == 6) && (rb_upscaler == 1)
                          && g.taauPipeline != VK_NULL_HANDLE;
    uint32_t renderW = dispW, renderH = dispH;
    float jitterX = 0.0f, jitterY = 0.0f;
    if (taauActive) {
        uint32_t sc = (uint32_t)(rb_renderscale < 25 ? 25
                                 : (rb_renderscale > 100 ? 100 : rb_renderscale));
        renderW = ((dispW * sc / 100u) + 1u) & ~1u;   // round to even
        renderH = ((dispH * sc / 100u) + 1u) & ~1u;
        if (renderW < 2u)    renderW = 2u;
        if (renderW > dispW) renderW = dispW;
        if (renderH < 2u)    renderH = 2u;
        if (renderH > dispH) renderH = dispH;
        // Halton(2,3) sub-pixel offset in render pixels, centred ([-0.5, 0.5)).
        uint32_t j = (g.svgfFrame % 16u) + 1u;
        jitterX = RadicalInverse(j, 2u) - 0.5f;
        jitterY = RadicalInverse(j, 3u) - 0.5f;
    }
    // From here `w`,`h` are the RENDER dimensions driving the trace + SVGF + the TAAU
    // input; `dispW`,`dispH` are the display dimensions for the label + final blit.
    const uint32_t w = renderW, h = renderH;

    // DOOM-0090: per-pass GPU timer. Reset the pool + stamp the frame's GPU start,
    // then stamp each pass boundary below. Read back next frame (single-frame-in-
    // flight, so it's complete). Segments: sprite-AS / megakernel / denoise+TAAU / blit.
    extern int rb_profile;
    const bool prof = rb_profile && g.gpuTimerPool;
    if (prof) {
        vkCmdResetQueryPool(g.cmd, g.gpuTimerPool, 0, 8);
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g.gpuTimerPool, 0);
    }

    // DOOM-0131: refit the moving-sector world BLAS in-line (build step 5) when a
    // door/lift shifted geometry this frame. Recorded into g.cmd ahead of the TLAS
    // rebuild below (which reads the BLAS extents); an AS write->read barrier orders
    // them. Folding this into the frame buffer removes the old standalone one-time
    // submit that bubbled the GPU. blasDirty is latched in the present path even
    // under raster, so a move finished off-screen is caught the first traced frame.
    if (g.blasDirty)
    {
        RecordRefitAS(g.cmd);
        VkMemoryBarrier rmb = {};
        rmb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        rmb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        rmb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR
                          | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0,
                             1, &rmb, 0, nullptr, 0, nullptr);
        g.blasDirty = false;
    }

    // DOOM-0100: rebuild the per-frame sprite BLAS + TLAS (world + sprites) into
    // g.cmd before the trace dispatch, so primary rays this frame hit the world
    // sprites with real depth + lighting. No-op-ish when there are no sprites.
    BuildSpriteTlas();
    if (prof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 1);

    // rtImage -> GENERAL for the compute write (prior contents are discarded).
    VkImageMemoryBarrier toGeneral = {};
    toGeneral.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    toGeneral.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image            = g.rtImage;
    toGeneral.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toGeneral.srcAccessMask    = 0;
    toGeneral.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGeneral);

    // Camera basis: DOOM yaw in the xy plane, z up; 90 deg horizontal FOV
    // (FIELDOFVIEW), vertical derived from the aspect — matching the raster
    // Mat4PerspectiveH so the intersection view lines up with the raster one.
    const float c = std::cos(g.lastView.angle), s = std::sin(g.lastView.angle);
    struct RtPushConstants {
        float    camPos[4];
        float    camDir[4];
        float    camRight[4];   // w = tan(hFov/2)
        float    camUp[4];      // w = tan(vFov/2)
        uint32_t misc[4];       // mode, width, height, numWall (flat-id offset)
        uint32_t misc2[4];      // emitterCount, probeCount, muzzle-flash(z), flashlight(w)
        uint32_t misc3[4];      // 4d verify only (seed/spp/estimator); 0 for display
        uint32_t misc4[4];      // DOOM-0100: x = sprite material base (numWall+numFlat); rest reserved
        uint32_t misc5[4];      // DOOM-0179: x = world-grime overlay id in hdTex[] (0xFFFFFFFF = none)
        uint64_t vertsAddr;
        uint64_t emitAddr;      // step-3b emitter list (0 if none)
        uint64_t matEmisAddr;   // per-material Le table
        uint64_t probeAddr;     // step-4 GI probe cache (0 if none)
        uint64_t triSsAddr;     // per-triangle subsector id (0 if none)
        uint64_t spriteVertsAddr; // DOOM-0100: per-frame billboard verts (0 if none)
        uint64_t subSecAddr;      // DOOM-0119: subsector -> sector (0 if cull off)
        uint64_t emitSecAddr;     // DOOM-0119: per-emitter sector (0 if cull off)
        uint64_t rejectAddr;      // DOOM-0119: REJECT bitmatrix words (0 if cull off)
        // DOOM-0183: ripple time + wet toggle. std430 aligns a uvec4 to 16 bytes, so the
        // shader places misc6 at offset 224 (padded from the 216-byte tail) — mirror that
        // pad here or the GLSL block and this struct disagree (INV-6). Appended AFTER the
        // 184-byte -rtverify prefix, so verify is unaffected; stays within the 256-byte limit.
        uint32_t _pad_misc6[2];   // pad rejectAddr's 216 tail up to a 16-byte boundary (224)
        uint32_t misc6[4];        // x = ripple time (float bits, seconds); y = wet toggle; z,w = 0
    } pc = {};
    static_assert(sizeof(RtPushConstants) == 240, "RT push-constant layout must match the shader");
    pc.camPos[0] = g.lastView.x; pc.camPos[1] = g.lastView.y; pc.camPos[2] = g.lastView.z;
    pc.camDir[0] = c;            pc.camDir[1] = s;            pc.camDir[2] = 0.0f;
    pc.camRight[0] = s;          pc.camRight[1] = -c;         pc.camRight[2] = 0.0f;
    pc.camRight[3] = 1.0f;                                   // tan(45 deg)
    // Vertical FOV from the DISPLAY aspect (the frustum is display-shaped); the render
    // sub-rect is a uniform scale of it, so the same tangents drive both.
    pc.camUp[2] = 1.0f;          pc.camUp[3] = (float)dispH / (float)dispW;
    pc.camPos[3] = jitterX;      pc.camDir[3] = jitterY;     // 6-d sub-pixel jitter (0 when off)
    pc.misc[0] = (uint32_t)rb_rtdebug;
    pc.misc[1] = w; pc.misc[2] = h;
    pc.misc[3] = (uint32_t)g.matNumWall;   // flat-id offset for mode-3 textured decode
    pc.misc2[0] = g.emitCount;             // NEE emitter triangle count (step 3b)
    pc.misc2[1] = g.probeCount;            // GI probe count (step 4c; 0 -> no GI)
    // Muzzle-flash gate (step 5): player->extralight [0,1] forwarded as the flash
    // intensity (0 = not firing -> the dynamic light is skipped). Bit-cast into the
    // uint slot; the shader reads it back with uintBitsToFloat.
    std::memcpy(&pc.misc2[2], &g.lastView.extralight, sizeof(float));
    // DOOM-0044 flashlight gate: the player headlamp on/off (rb_flashlight). Its
    // position + aim are the eye + view dir already in camPos/camDir, so only the
    // enable bit is forwarded, into the spare misc2.w slot (read as misc2.w != 0u).
    pc.misc2[3] = rb_flashlight ? 1u : 0u;
    pc.misc4[0]    = (uint32_t)(g.matNumWall + g.matNumFlat);   // sprite material base (DOOM-0100)
    // DOOM-0084: emitters [staticCount, emitCount) are the per-frame sprite lights —
    // the shader treats them as omnidirectional (a lamp emits all ways, not as a
    // camera-facing card). Static wall/flat emitters before this index stay oriented.
    pc.misc4[1]    = (uint32_t)g.staticWgt.size();
    // DOOM-0119: REJECT cull dimension (sector count). 0 when the level has no
    // REJECT lump (rejectBuf absent), which disables the shader cull entirely.
    pc.misc4[2]    = g.rejectBuf ? g.numSectors : 0u;
    // DOOM-0141: sky wall-texture bindless id for the panorama sample (sky hit /
    // miss). 0xFFFFFFFF when the level has no sky geometry -> the shader falls back
    // to the flat SKY_COLOR fill (a miss in an enclosed level is degenerate anyway).
    pc.misc4[3]    = (g.skyMeshVerts > 0) ? (uint32_t)g.skyMeshTexnum : 0xFFFFFFFFu;
    // DOOM-0179: world-grime overlay slot in hdTex[] (loaded per level by EnsureHdMaterials;
    // -1 on the default all-paletted set -> 0xFFFFFFFF disables the shader's grime branch).
    pc.misc5[0]    = (g.hdGrungeIdx >= 0) ? (uint32_t)g.hdGrungeIdx : 0xFFFFFFFFu;
    // DOOM-0181: de-tile quality dial (misc5.y) from rb_detile (0=off,1=2-tap,2=4-tap; `]` key).
    // Forced 0 when no HD materials are loaded (nothing to de-tile), so the dial is a no-op there.
    pc.misc5[1]    = (g.hdBuilt && g.matNumWall + g.matNumFlat > 0) ? (uint32_t)rb_detile : 0u;
    // DOOM-0181: dirt-colour texture slot in hdTex[] (misc5.z) for the filth-stain colour;
    // 0xFFFFFFFF when absent -> shader falls back to its procedural earthy ramp.
    pc.misc5[2]    = (g.hdDirtIdx >= 0) ? (uint32_t)g.hdDirtIdx : 0xFFFFFFFFu;
    // DOOM-0187: filth master toggle (misc5.w) from rb_filth (1=on,0=off; `[` key). Not HD-gated —
    // applyGrime paints paletted surfaces too — so this is a plain runtime on/off of the stain layer.
    pc.misc5[3]    = rb_filth ? 1u : 0u;
    // DOOM-0183: ripple time (seconds, wall-clock) + wet toggle (misc6). steady_clock is
    // frame-rate-independent so kRippleSpeed has a fixed meaning, and immune to NTP jumps;
    // zeroed at first use so the seconds stay small (float precision). y gates the shader's
    // sheen/ripple/puddle layers only (rb_wet) — never the glow (that Le is CPU-built).
    static const auto rippleT0 = std::chrono::steady_clock::now();
    float rippleSec = std::chrono::duration<float>(std::chrono::steady_clock::now() - rippleT0).count();
    std::memcpy(&pc.misc6[0], &rippleSec, sizeof(float));
    pc.misc6[1]    = rb_wet ? 1u : 0u;
    pc.misc6[2]    = 0u;
    pc.misc6[3]    = 0u;
    pc.vertsAddr   = BufferAddress(g.vbuf);
    pc.emitAddr    = g.emitBuf    ? BufferAddress(g.emitBuf)    : 0;
    pc.matEmisAddr = g.matEmisBuf ? BufferAddress(g.matEmisBuf) : 0;
    pc.probeAddr   = g.probeBuf   ? BufferAddress(g.probeBuf)   : 0;
    pc.triSsAddr   = g.triSsBuf   ? BufferAddress(g.triSsBuf)   : 0;
    pc.spriteVertsAddr = g.sprWorldBuf ? BufferAddress(g.sprWorldBuf) : 0;
    pc.subSecAddr  = g.subSecBuf  ? BufferAddress(g.subSecBuf)  : 0;   // DOOM-0119
    pc.emitSecAddr = g.emitSecBuf ? BufferAddress(g.emitSecBuf) : 0;
    pc.rejectAddr  = g.rejectBuf  ? BufferAddress(g.rejectBuf)  : 0;

    // SVGF denoised view (step 6): mode 6. The feed writes this frame's G-buffer
    // half into gpos[parity]/gnorm[parity]; the temporal pass reprojects last
    // frame's [parity^1]. Parity advances once per denoised frame (end of this fn).
    const bool     denoise    = (rb_rtdebug == 6);
    const uint32_t svgfParity = g.svgfFrame & 1u;
    pc.misc3[3] = svgfParity;
    // Per-frame seed base for the mode-6 feed: SVGF needs each frame to be an
    // INDEPENDENT noise sample, or temporal accumulation averages identical values
    // (no convergence) and the temporal variance is 0 (the a-trous luminance
    // edge-stop then collapses to identity — the denoiser does nothing). The frozen
    // px-only seed mode 4 uses is fine for a one-shot noisy view but fatal here.
    pc.misc3[0] = g.svgfFrame;

    // Set 0 = RT (TLAS + output image); set 1 = the raster materials set (palette
    // LUT + bindless material array), so the textured trace samples the same
    // materials as the raster pass (step 3a); set 2 = the SVGF G-buffer (step 6);
    // set 3 = the DOOM-0042 HD PBR materials (control SSBO + RGBA8 map array).
    // rtActive gates on g.atlasReady, so the material array (binding 2) is written
    // before this binds it; InitHdDefault seeds set 3 at the same atlas point. All
    // four sets are bound (the megakernel statically references set 2 via mode 6 and
    // set 3 via hdAlbedo, so both must be valid for every dispatch).
    VkDescriptorSet sets[4] = { g.rtDs, g.ds, g.svgfDs, g.hdSet };
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                      RtPipelineForMode((uint32_t)rb_rtdebug));
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            g.rtPipeLayout, 0, 4, sets, 0, nullptr);
    vkCmdPushConstants(g.cmd, g.rtPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDispatch(g.cmd, (w + 7) / 8, (h + 7) / 8, 1);
    if (prof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 2);

    // Denoiser chain (step 6a/6b): feed (the dispatch above, mode 6) -> temporal
    // accumulation -> N edge-aware a-trous iterations -> composite, all reading/
    // writing SVGF storage images in GENERAL with a compute->compute barrier
    // between passes. The composite writes rtImage (the same image the blit below
    // reads), so the rtImage transitions are unchanged. Only mode 6 runs this.
    if (denoise)
    {
        // Compute->compute RAW barrier (storage image write -> read) between passes.
        auto svgfBarrier = [&]() {
            VkMemoryBarrier mb = {};
            mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 1, &mb, 0, nullptr, 0, nullptr);
        };

        // Push constants shared by the three denoiser passes (prefix-compatible: the
        // temporal/a-trous shaders ignore the trailing matEmis address).
        struct SvgfPC {
            float    prevPos[4], prevDir[4], prevRight[4], prevUp[4];
            uint32_t misc[4];    // x=cur parity, y=w, z=h, w=a-trous step / final src
            uint32_t misc2[4];   // x=a-trous ping (source index), y=iter
            uint32_t misc3[4];
            uint64_t matEmis;
        } spc = {};
        static_assert(sizeof(SvgfPC) == 120, "SVGF push-constant layout must match the shaders");
        spc.prevPos[0]   = g.prevCamPos[0];   spc.prevPos[1]   = g.prevCamPos[1];   spc.prevPos[2]   = g.prevCamPos[2];
        spc.prevDir[0]   = g.prevCamDir[0];   spc.prevDir[1]   = g.prevCamDir[1];   spc.prevDir[2]   = g.prevCamDir[2];
        spc.prevRight[0] = g.prevCamRight[0]; spc.prevRight[1] = g.prevCamRight[1]; spc.prevRight[2] = g.prevCamRight[2];
        spc.prevRight[3] = g.prevCamRight[3];
        spc.prevUp[0]    = g.prevCamUp[0];    spc.prevUp[1]    = g.prevCamUp[1];    spc.prevUp[2]    = g.prevCamUp[2];
        spc.prevUp[3]    = g.prevCamUp[3];
        spc.misc[0] = svgfParity;
        spc.misc[1] = w; spc.misc[2] = h;
        // DOOM-0096 brightness: map the rb_exposure slider (0..15) to a photographic
        // EV [-4.0, -0.25] (pos 7 == the old fixed -2.25) and hand it to the composite
        // tonemap via misc3.x (bit-cast float). Only svgf_composite reads misc3 here.
        {
            int e = rb_exposure < 0 ? 0 : (rb_exposure > 15 ? 15 : rb_exposure);
            float ev = -4.0f + 0.25f * (float)e;
            std::memcpy(&spc.misc3[0], &ev, sizeof(float));
        }

        vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                g.svgfPipeLayout, 0, 1, &g.svgfDs, 0, nullptr);

        const uint32_t gx = (w + 7) / 8, gy = (h + 7) / 8;

        // 1) temporal accumulation (feed -> atrous[0]).
        svgfBarrier();
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.svgfTemporal);
        spc.misc[3] = 0; spc.misc2[0] = 0; spc.misc2[1] = 0;
        vkCmdPushConstants(g.cmd, g.svgfPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(spc), &spc);
        vkCmdDispatch(g.cmd, gx, gy, 1);
        // DOOM-0144: split the denoise+TAAU bucket into its sub-passes (slots 5/6/7).
        if (prof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 5);

        // 2) edge-aware a-trous: N iterations, hole step doubling, ping-ponging
        //    atrous[0]<->atrous[1]. Iteration 0 also writes the colour history.
        // DOOM-0090: 4 a-trous iterations (was 5). The dropped pass is the coarsest
        // (hole step 16) — it only smooths very-low-frequency residual noise, the
        // least visible level, so removing it trims the denoiser's cost (its second-
        // hotspot ~36 ms at native / ~8.5 ms at 50%) with minimal visual change.
        const int N = 4;
        uint32_t ping = 0;
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.svgfAtrous);
        for (int i = 0; i < N; i++) {
            svgfBarrier();
            spc.misc[3]  = (uint32_t)(1 << i);   // hole step 1,2,4,8,16
            spc.misc2[0] = ping;                 // source index
            spc.misc2[1] = (uint32_t)i;          // iter (0 -> writes history)
            vkCmdPushConstants(g.cmd, g.svgfPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(spc), &spc);
            vkCmdDispatch(g.cmd, gx, gy, 1);
            ping ^= 1u;                           // last written buffer = final result
        }
        if (prof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 6);

        // 3) composite: re-modulate albedo + re-add emission + tonemap -> rtImage.
        svgfBarrier();
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.svgfComposite);
        spc.misc[3]  = ping;                      // final a-trous source index
        spc.matEmis  = g.matEmisBuf ? BufferAddress(g.matEmisBuf) : 0;
        vkCmdPushConstants(g.cmd, g.svgfPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(spc), &spc);
        vkCmdDispatch(g.cmd, gx, gy, 1);
        if (prof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 7);

        // 4) temporal upscale (build step 6-d): reconstruct the full display image
        //    from the render-res denoised colour (rtImage) + motion vectors + this
        //    frame's jitter, accumulating into the parity history. TAAU only; the
        //    blit below then sources the upscaled output instead of rtImage.
        if (taauActive)
        {
            svgfBarrier();                        // composite writes rtImage + MV -> TAAU reads
            // TAAU output -> GENERAL for the compute write (it sat in TRANSFER_SRC
            // after last frame's blit; every display pixel is overwritten here).
            VkImageMemoryBarrier toGenOut = {};
            toGenOut.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            toGenOut.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            toGenOut.newLayout        = VK_IMAGE_LAYOUT_GENERAL;
            toGenOut.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGenOut.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toGenOut.image            = g.taImg[TA_OUT];
            toGenOut.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            toGenOut.srcAccessMask    = 0;
            toGenOut.dstAccessMask    = VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toGenOut);

            struct TaauPC {
                uint32_t misc[4];    // x = cur parity, y = display W, z = display H, w reserved
                uint32_t misc2[4];   // x = render W, y = render H, z,w reserved
                float    jitter[4];  // xy = render-pixel jitter
            } tpc = {};
            static_assert(sizeof(TaauPC) == 48, "TAAU push-constant layout must match taau.comp");
            tpc.misc[0] = svgfParity; tpc.misc[1] = dispW;   tpc.misc[2] = dispH;
            tpc.misc2[0] = renderW;   tpc.misc2[1] = renderH;
            tpc.jitter[0] = jitterX;  tpc.jitter[1] = jitterY;
            vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.taauPipeline);
            vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    g.taauPipeLayout, 0, 1, &g.taauDs, 0, nullptr);
            vkCmdPushConstants(g.cmd, g.taauPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(tpc), &tpc);
            vkCmdDispatch(g.cmd, (dispW + 7) / 8, (dispH + 7) / 8, 1);
        }
    }

    // DOOM-0144: when the denoiser didn't run (non-mode-6 views), slots 5-7 were
    // never written this frame. The readback reads all 8 in one batch, so define them
    // here (collapsed to the denoise-end point -> sub-segments read as ~0) to keep the
    // query batch fully available rather than VK_NOT_READY.
    if (prof && !denoise)
    {
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 5);
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 6);
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 7);
    }
    if (prof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 3);

    // The image the present path blits: the upscaled TAAU output (display-res) when
    // TAAU ran, else rtImage (the megakernel / composite output, display-res on every
    // non-TAAU path). The label + blit below operate at display resolution on it.
    const VkImage finalImage = taauActive ? g.taImg[TA_OUT] : g.rtImage;

    // On-screen mode label (debug ONLY): stamp the active `~` mode's title top-centre into
    // the final image before the blit. Gated on rb_rtdebug_menu ("Debug Views" on) so it is
    // the diagnostic cycle's on-screen proof and never shows during normal play — selecting
    // Ultra from the menu drives mode 6 (DENOISED) without Debug Views, and the player already
    // knows the tier they picked, so the "DENOISED"/"PROFILER" text would just be clutter
    // (user request 2026-07-14). The compute->compute barrier orders it after the megakernel
    // (modes 1-4) / composite (mode 6) / TAAU (mode 6 upscaled) write; it only touches the label box.
    if (rb_rtdebug_menu && !g.shotCapture)   // DOOM-0202: never bake the debug label into a -shotverify golden
    {
        VkMemoryBarrier mb = {};
        mb.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                             1, &mb, 0, nullptr, 0, nullptr);

        struct LabelPC { uint32_t w, h, count, scale; uint32_t chars[12]; } lpc = {};
        static_assert(sizeof(LabelPC) == 64, "label push-constant layout must match label.comp");
        lpc.w     = dispW;
        lpc.h     = dispH;
        lpc.scale = (dispH >= 1080u) ? 4u : (dispH >= 600u ? 3u : 2u);
        lpc.count = ModeLabel(rb_rtdebug, lpc.chars);
        // labelTaauDs retargets binding 7 (output) at the TAAU image; svgfDs writes
        // rtImage. Both layout-compatible with the label pipeline layout.
        VkDescriptorSet labelSet = taauActive ? g.labelTaauDs : g.svgfDs;
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.labelPipeline);
        vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                g.labelPipeLayout, 0, 1, &labelSet, 0, nullptr);
        vkCmdPushConstants(g.cmd, g.labelPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(lpc), &lpc);
        vkCmdDispatch(g.cmd, (dispW + 7) / 8, (dispH + 7) / 8, 1);

        // DOOM-0090: when the GPU profiler (`\`) is on, stamp "PROFILER" as a second
        // line below the mode title, so it's clear on-screen the profiler is active
        // (the per-pass timings still print to the terminal). Same pipeline + set;
        // the high byte of scale selects line 1. A shader-write->write barrier orders
        // it after the mode-label write (disjoint rows, but keeps validation happy).
        if (rb_profile)
        {
            vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                                 1, &mb, 0, nullptr, 0, nullptr);
            static const uint32_t PROFILER[8] = { 16,10,9,5,7,17,4,10 };  // P R O F I L E R
            for (int i = 0; i < 8; i++) lpc.chars[i] = PROFILER[i];
            lpc.count = 8u;
            lpc.scale |= (1u << 8);   // line 1 (below the mode title)
            vkCmdPushConstants(g.cmd, g.labelPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(lpc), &lpc);
            vkCmdDispatch(g.cmd, (dispW + 7) / 8, (dispH + 7) / 8, 1);
        }
    }

    // final image GENERAL -> TRANSFER_SRC; swapchain UNDEFINED -> TRANSFER_DST.
    VkImageMemoryBarrier toSrc = toGeneral;
    toSrc.image         = finalImage;
    toSrc.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
    toSrc.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSrc);

    VkImageMemoryBarrier toDst = {};
    toDst.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout        = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image            = g.images[idx];
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toDst.srcAccessMask    = 0;
    toDst.dstAccessMask    = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    // 1:1 blit (matched display extents); component-by-name copy, so R8G8B8A8 -> the
    // B8G8R8A8 swapchain carries no red/blue swap. The final image is display-sized
    // on every path (rtImage at display res, or the TAAU output), so this stays a
    // nearest 1:1 copy — the resolution reconstruction already happened in TAAU.
    VkImageBlit blit = {};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[1]  = { (int32_t)dispW, (int32_t)dispH, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[1]  = { (int32_t)dispW, (int32_t)dispH, 1 };
    vkCmdBlitImage(g.cmd, finalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   g.images[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);

    // DOOM-0202: -shotverify capture. finalImage is still TRANSFER_SRC here (the blit
    // above just read it — a read/read alias, so no barrier), and it's R8G8B8A8_UNORM at
    // display res: copy it into a host-visible buffer so the present path can write a PNG
    // once this frame completes. The buffer is lazily sized to the (stable) display extent.
    if (g.shotCapture)
    {
        const VkDeviceSize need = (VkDeviceSize)dispW * dispH * 4;
        if (g.shotBuf == VK_NULL_HANDLE || g.shotBufSize < need)
        {
            if (g.shotBuf)    vkDestroyBuffer(g.device, g.shotBuf, nullptr);
            if (g.shotBufMem) vkFreeMemory(g.device, g.shotBufMem, nullptr);
            CreateRtBuffer(need, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                           &g.shotBuf, &g.shotBufMem);
            g.shotBufSize = need;
        }
        VkBufferImageCopy region = {};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent      = { dispW, dispH, 1 };
        vkCmdCopyImageToBuffer(g.cmd, finalImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               g.shotBuf, 1, &region);
        g.shotW = dispW; g.shotH = dispH;
    }

    // Swapchain TRANSFER_DST -> PRESENT_SRC for vkQueuePresentKHR.
    VkImageMemoryBarrier toPresent = toDst;
    toPresent.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    if (prof) {
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 4);
        g.gpuTimersInUse = true;   // results ready to read at the top of next frame
        g.profRasterFrame = false; // this timed frame was the RT path -> RT readback interpretation
    }

    // Snapshot this frame's camera basis as "previous" for next frame's temporal
    // reprojection, and advance the history parity — but only on denoised frames,
    // so [parity^1] always names the last frame that actually wrote the G-buffer
    // (a stretch of non-denoised modes can't leave a stale prev that wrongly
    // validates). The basis mirrors the camera built at the top of this function.
    if (denoise)
    {
        g.prevCamPos[0] = g.lastView.x; g.prevCamPos[1] = g.lastView.y; g.prevCamPos[2] = g.lastView.z;
        g.prevCamDir[0] = c;            g.prevCamDir[1] = s;            g.prevCamDir[2] = 0.0f;
        g.prevCamRight[0] = s;          g.prevCamRight[1] = -c;         g.prevCamRight[2] = 0.0f;
        g.prevCamRight[3] = 1.0f;                                       // tan(hFov/2)
        g.prevCamUp[0] = 0.0f; g.prevCamUp[1] = 0.0f; g.prevCamUp[2] = 1.0f;
        g.prevCamUp[3] = (float)dispH / (float)dispW;                  // tan(vFov/2), display aspect
        g.svgfFrame++;
    }
}

// DOOM-0094: upload the per-frame 2D overlay staging buffer (screens[0]) into its
// sampled image. A transfer, so it must run OUTSIDE any render pass. Shared by the
// raster present path and the path-traced overlay pass below.
void UploadOverlayImage()
{
    VkImageMemoryBarrier toDst = {};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = g.overlayImage;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region = {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { (uint32_t)g.overlayW, (uint32_t)g.overlayH, 1 };
    vkCmdCopyBufferToImage(g.cmd, g.overlayStaging, g.overlayImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead = toDst;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);
}

// DOOM-0094: draw the 2D presentation layer over the path-traced view. RecordRtTrace
// blits the traced WORLD to the swapchain (and leaves it in PRESENT_SRC) but skips the
// HUD/menu/messages/FPS overlay (all composited from screens[0]) and the player weapon
// the raster path draws. This runs the LOAD-variant render pass (g.rtOverlayPass: colour
// loadOp=LOAD keeps the trace, depth cleared) over g.framebuffers[idx] and draws (1) the
// weapon viewmodel psprite via the world pipeline (depth-cleared so it sits on top, as
// the player weapon always does) and (2) the overlay compositor. World sprites
// (monsters/items) are out of scope -- they need TLAS depth occlusion (DOOM-0084).
void RecordRtOverlay(uint32_t idx, bool drawOverlay)
{
    const bool drawWeapon = g.spriteVbuf && g.spriteVertCount;
    if (!drawOverlay && !drawWeapon)
        return;   // nothing to composite; the trace already presents.

    // The overlay image upload is a transfer; it must precede the render pass.
    if (drawOverlay)
        UploadOverlayImage();

    VkClearValue clears[2] = {};
    clears[1].depthStencil = { 1.0f, 0 };   // colour is LOAD (the trace); depth cleared

    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g.rtOverlayPass;
    rp.framebuffer = g.framebuffers[idx];
    rp.renderArea.extent = g.extent;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(g.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vpRect = {};
    vpRect.width = (float)g.extent.width;
    vpRect.height = (float)g.extent.height;
    vpRect.maxDepth = 1.0f;
    vkCmdSetViewport(g.cmd, 0, 1, &vpRect);
    VkRect2D scissor = { { 0, 0 }, g.extent };
    vkCmdSetScissor(g.cmd, 0, 1, &scissor);

    // DOOM-0170 L2c: set 0 = g.ds (materials); set 1 = g.shadowDs (flashlight shadow map +
    // lightVP). mesh.frag statically samples set 1, so bind it even where it goes unused
    // (the weapon psprite / 2D overlay never take the flashlight branch).
    VkDescriptorSet worldDs[2] = { g.ds, g.shadowDs };
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g.pipelineLayout, 0, 2, worldDs, 0, nullptr);

    // Player weapon: a screen-space psprite (NDC, MVP skipped by mesh.vert) drawn with
    // the world pipeline, so it needs the same push constants -- only the muzzle-flash
    // brighten + material-id offsets actually affect a psprite, but mirror the raster
    // weapon draw so the gun renders identically in Solid and Ultra.
    if (drawWeapon)
    {
        float pcData[24];
        std::memcpy(pcData, g.viewProj, 16 * sizeof(float));
        pcData[16] = g.lastView.extralight;
        pcData[17] = g.lastView.angle;
        pcData[18] = g.lastView.x;
        pcData[19] = g.lastView.y;
        pcData[20] = g.lastView.z;
        std::memcpy(&pcData[21], &g.matNumWall, sizeof(int));
        std::memcpy(&pcData[22], &g.matNumFlat, sizeof(int));
        pcData[23] = rb_flashlight ? 1.0f : 0.0f;   // inert here (psprite skips the cone)
        vkCmdPushConstants(g.cmd, g.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 24 * sizeof(float), pcData);

        VkDeviceSize off = 0;
        // DOOM-0170 L2a step 3: the 8-bit twin (g.pipeline itself now targets the float
        // scene pass and is not compatible with this 8-bit swapchain overlay pass).
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.rtWeaponPipeline);
        vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.spriteVbuf, &off);
        vkCmdDraw(g.cmd, g.spriteVertCount, 1, 0, 0);
    }

    // 2D HUD/menu/messages/FPS compositor, last and over everything: samples screens[0]
    // and keys out the transparent index so the traced view (and the weapon) shows through.
    if (drawOverlay)
    {
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.overlayPipeline);
        vkCmdDraw(g.cmd, 3, 1, 0, 0);
    }

    FlushMenuText();   // DOOM-0206 L1b: crisp menu glyphs over the paletted overlay

    vkCmdEndRenderPass(g.cmd);
}

// DOOM-0170 perf: monotonic wall clock in milliseconds for the CPU-side frame
// profiler. steady_clock is immune to NTP/settimeofday jumps, unlike wall-time.
static inline double CpuNowMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// DOOM-0074: the per-frame CPU "build" — sky/sprite/weapon billboards, the NEE emitter
// refill + per-subsector point-light cull, and moving-sector re-height. Split out of
// RB_Vulkan_Present so it can run BEFORE the top-of-frame fence wait (build-ahead: the
// CPU prepares the next frame's data while the GPU still renders the current one) in
// steady-state raster, or after it (serialized) on a traced / mode-changing frame. It
// writes the active frame-slot copies of g.spriteVbuf / g.lightBuf / g.vbuf and reads
// live game state (sectors[], sprites). `cprof` gates the CPU profiler sub-timers; the
// build total lands in g.cpuMs[1].
static void BuildFrameInputs(bool rtActive, bool cprof)
{
    const double tBuild0 = cprof ? CpuNowMs() : 0.0;
    g.spriteVertCount = 0;
    g.skyVertCount    = 0;
    g.blobVertCount   = 0;   // DOOM-0170 L2d
    g.sprWorldVertCount = 0;
    if (!rtActive && g.spriteMapped && g.haveCamera && g.atlasReady)
    {
        const double tSpr0 = cprof ? CpuNowMs() : 0.0;   // sub-timer: billboard builds
        rb_vertex_t* buf = (rb_vertex_t*)g.spriteMapped;
        // Sky backdrop first (verts [0,sky)); it draws behind the world with the
        // depth-off pipeline. Sprites + weapon follow and share the main draw.
        int sky = RB_BuildSky(&g.lastView, buf, (int)g.spriteVertCap);
        int n   = RB_BuildSprites(&g.lastView, buf + sky, (int)g.spriteVertCap - sky);
        // Weapon overlay shares the buffer/draw; appended last so it sits on top.
        // Pass the swapchain aspect so the gun keeps DOOM's 4:3 proportions.
        float aspect = g.extent.height ? (float)g.extent.width / (float)g.extent.height
                                       : 1.0f;
        n += RB_BuildPSprites(buf + sky + n, (int)g.spriteVertCap - sky - n, aspect);
        g.skyVertCount    = (uint32_t)sky;
        g.spriteVertCount = (uint32_t)n;

        // DOOM-0170 L2d: blob shadows appended after the sprites+weapon; the blob pass
        // (alpha-blend pipeline) reads them from this offset, drawn before the billboards.
        g.blobVertOffset = (uint32_t)(sky + n);
        g.blobVertCount  = (uint32_t)RB_BuildBlobs(&g.lastView, buf + sky + n,
                                                   (int)g.spriteVertCap - sky - n);
        if (cprof) g.cpuBuildMs[0] += CpuNowMs() - tSpr0;   // sky/sprite/psprite/blob builds

        // DOOM-0170 L1b: the raster point-light stack needs this frame's emissive
        // sprites (torches/lamps/barrels) in the NEE emitter list. The traced branch
        // below builds the world sprites into g.sprWorldBuf; the raster branch above
        // only built the drawn billboards (into g.spriteMapped), so build the world-
        // sprite set here too, refresh the emitter list, then rebuild the per-subsector
        // nearest-N point-light lists the fragment shader reads. RT-GPU only (no
        // emitters/probes without a bake); costs one sprite build + the <=1 ms cull.
        if (g.rtEnabled && g.sprWorldMapped)
        {
            const double tW0 = cprof ? CpuNowMs() : 0.0;
            g.sprWorldVertCount = (uint32_t)RB_BuildSprites(&g.lastView,
                (rb_vertex_t*)g.sprWorldMapped, (int)g.sprWorldVertCap);
            const double tL0 = cprof ? CpuNowMs() : 0.0;
            if (cprof) g.cpuBuildMs[0] += tL0 - tW0;   // 2nd (world) sprite build -> sprites
            if (g.worldEmitDirty && g.vbufMapped)
            {
                BuildStaticEmitterSet((const rb_vertex_t*)g.vbufMapped);
                g.worldEmitDirty = false;
            }
            BuildDynamicEmitters();     // refill g.emitBuf (static + emissive sprites)
            BuildRasterPointLights();   // -> g.lightBuf (per-subsector nearest-N)
            if (cprof) g.cpuBuildMs[1] += CpuNowMs() - tL0;   // emitter refill + point-light cull
        }
    }
    // DOOM-0094/0100: in the path-traced view the world comes from the trace. The
    // player weapon is still a screen-space psprite drawn on top (g.spriteMapped),
    // but world sprites (monsters/items/barrels) are now traced as a per-frame TLAS
    // instance (DOOM-0100): build their billboards into g.sprWorldBuf so BuildSpriteTlas
    // can rebuild the sprite BLAS over them. No sky (the trace's miss shader is the sky).
    else if (rtActive && g.spriteMapped && g.haveCamera && g.atlasReady)
    {
        rb_vertex_t* buf = (rb_vertex_t*)g.spriteMapped;
        float aspect = g.extent.height ? (float)g.extent.width / (float)g.extent.height
                                       : 1.0f;
        g.spriteVertCount = (uint32_t)RB_BuildPSprites(buf, (int)g.spriteVertCap, aspect);

        g.sprWorldVertCount = 0;
        if (g.sprWorldMapped)
            g.sprWorldVertCount = (uint32_t)RB_BuildSprites(&g.lastView,
                (rb_vertex_t*)g.sprWorldMapped, (int)g.sprWorldVertCap);

        // DOOM-0082: last frame RB_UpdateMeshHeights saw a switch press/revert (or an
        // animated flat) change a face's live texture; rebuild the static emitter set
        // from the live vertex buffer so a now-lit switch pools light — and stops when
        // it reverts. Cheap and rare (only on an actual texture change).
        if (g.worldEmitDirty && g.vbufMapped)
        {
            BuildStaticEmitterSet((const rb_vertex_t*)g.vbufMapped);
            g.worldEmitDirty = false;
        }

        // DOOM-0084: append this frame's emissive sprites (lamps/torches/barrels)
        // to the NEE light list so they pool light onto their surroundings.
        BuildDynamicEmitters();
    }

    // Re-height moving sectors (doors/lifts) in the static level buffer from the
    // live sector heights. host-coherent, no flush. A non-zero return means geometry
    // actually shifted this frame -> latch the BLAS dirty so the trace refits it (build
    // step 5). Latching (rather than refitting here) means a move that finished under
    // the raster path is still caught the first time the trace is shown. The change-
    // flags diff against this slot's buffer (its state from two frames ago under build-
    // ahead); the writes are always from the authoritative sector heights, so geometry
    // is correct regardless — only a stale-by-a-frame extra refit/emitter-rebuild can
    // result, which is harmless (DOOM-0074).
    if (g.levelMesh && g.vbufMapped)
    {
        const double tH0 = cprof ? CpuNowMs() : 0.0;
        int upd = RB_UpdateMeshHeights(g.levelMesh, (rb_vertex_t*)g.vbufMapped);
        if (upd & RB_UPD_MOVED) g.blasDirty = true;      // geometry shifted -> BLAS refit
        if (upd & RB_UPD_RETEX) g.worldEmitDirty = true; // a face's texture swapped -> emitter rebuild
        if (cprof) g.cpuBuildMs[2] += CpuNowMs() - tH0;  // moving-sector re-height
    }
    if (cprof) g.cpuMs[1] += CpuNowMs() - tBuild0;
}

extern "C" void RB_Vulkan_Present(void)
{
    if (!g.ready)
        return;

    // DOOM-0042: in Ultra, ensure this level's HD material sets are loaded before the
    // frame records. Idempotent (early-returns unless Ultra + not yet built); does its
    // own one-time GPU submits, so it must run outside the frame's command buffer.
    EnsureHdMaterials();

    // DOOM-0170 perf: CPU-side timing, same `\` toggle as the GPU pass timers. Captured
    // once here so all segments share the flag even if rb_profile flips mid-frame.
    const bool cprof = rb_profile;
    const double tPresent0 = cprof ? CpuNowMs() : 0.0;

    if (g.needRecreate)
    {
        RecreateSwapchain();
        g.needRecreate = false;
    }

    // DOOM-0074: pick this frame's in-flight slot and re-point the double-buffered
    // aliases (vbuf / spriteVbuf / lightBuf) at it. The build below writes slot
    // [frameSlot]; the GPU is still reading the PREVIOUS frame's slot. The fence wait
    // that follows guarantees slot[frameSlot]'s last user (two frames ago) is done, so
    // it is free to overwrite. Only the CPU build runs ahead — one frame of GPU work is
    // in flight as before, so the command buffer, sync objects and render targets stay
    // single-copy.
    g.frameSlot = (g.frameSlot + 1u) % VulkanState::kFramesInFlight;
    g.vbuf       = g.vbufSlot[g.frameSlot];
    g.vbufMemory = g.vbufMemSlot[g.frameSlot];
    g.vbufMapped = g.vbufMappedSlot[g.frameSlot];
    g.spriteVbuf       = g.spriteVbufSlot[g.frameSlot];
    g.spriteVbufMemory = g.spriteVbufMemSlot[g.frameSlot];
    g.spriteMapped     = g.spriteMappedSlot[g.frameSlot];
    g.lightBuf    = g.lightBufSlot[g.frameSlot];
    g.lightMem    = g.lightMemSlot[g.frameSlot];
    g.lightMapped = g.lightMappedSlot[g.frameSlot];

    // Whether this frame draws the path-traced view (vs the raster/Solid path). Computed
    // before the fence so the CPU build can run ahead in steady-state raster. Same gate
    // as the record path below (INV-10: the raster path stays byte-for-byte unaffected).
    const bool rtActive = rb_rtdebug && g.rtEnabled && g.tlas != VK_NULL_HANDLE
                       && g.rtModule != VK_NULL_HANDLE && g.haveCamera
                       && g.vbuf != VK_NULL_HANDLE && g.atlasReady;

    // A render-mode toggle (~ key) means the previous frame's GPU used resources this
    // frame's mode treats as single-copy (the RT structures, or the raster targets).
    // Drain once so nothing aliases across the transition; build-ahead resumes on the
    // next same-mode frame.
    const bool modeChanged = (rtActive != g.lastRtActive);
    if (modeChanged)
        vkDeviceWaitIdle(g.device);
    g.lastRtActive = rtActive;

    // Build-ahead only in steady-state raster: run the CPU build now, overlapping the
    // previous frame's GPU. A traced or just-toggled frame builds after the fence
    // (serialized) so its single-copy RT resources are never in flight.
    const bool buildAhead = !rtActive && !modeChanged;
    if (buildAhead)
        BuildFrameInputs(false, cprof);

    const double tFence0 = cprof ? CpuNowMs() : 0.0;
    vkWaitForFences(g.device, 1, &g.inFlight, VK_TRUE, UINT64_MAX);
    // The CPU blocks here until the PREVIOUS frame's GPU work signals the fence. With
    // DOOM-0074 build-ahead the steady-state raster build already ran above (overlapping
    // that GPU work), so this residual wait shrinks toward max(0, GPU - build): a large
    // value still means the GPU is the long pole, ~0 means the CPU build outran it.
    if (cprof) g.cpuMs[0] += CpuNowMs() - tFence0;

    // DOOM-0090: read back the previous frame's per-pass GPU timestamps (the fence
    // wait above guarantees that frame is complete, so this never stalls), convert
    // ticks -> ms, and print the running averages once a second. Raster or RT (routed by
    // profRasterFrame) / opt-in.
    if (g.gpuTimersInUse && g.gpuTimerPool)
    {
        uint64_t ts[8] = {};
        // Read only the slots the timed path actually wrote: raster wrote 6 (0..5), RT wrote 8
        // (0..7). Querying an unwritten-but-reset slot returns VK_NOT_READY and drops the whole
        // print, so the count must match the path (profRasterFrame, set when the frame recorded).
        uint32_t nq = g.profRasterFrame ? 6u : 8u;
        if (vkGetQueryPoolResults(g.device, g.gpuTimerPool, 0, nq, nq * sizeof(uint64_t), ts,
                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            const double k = (double)g.timestampPeriod / 1.0e6;   // ticks -> ms
            if (g.profRasterFrame)
            {
                // Solid raster passes (DOOM-0170): ts 0..5 = start / shadow / scene / SSAO /
                // composite / HUD. A skipped optional pass (torch off, SSAO off) reads ~0 ms.
                g.profMs[0] += (double)(ts[1] - ts[0]) * k;   // flashlight shadow map
                g.profMs[1] += (double)(ts[2] - ts[1]) * k;   // scene MRT (world + lighting)
                g.profMs[2] += (double)(ts[3] - ts[2]) * k;   // SSAO
                g.profMs[3] += (double)(ts[4] - ts[3]) * k;   // composite / tone-map
                g.profMs[4] += (double)(ts[5] - ts[4]) * k;   // HUD overlay + present
            }
            else
            {
                g.profMs[0] += (double)(ts[1] - ts[0]) * k;   // sprite BLAS/TLAS rebuild
                g.profMs[1] += (double)(ts[2] - ts[1]) * k;   // megakernel trace
                g.profMs[2] += (double)(ts[3] - ts[2]) * k;   // denoiser chain + TAAU
                g.profMs[3] += (double)(ts[4] - ts[3]) * k;   // label + blit + present
                // DOOM-0144 sub-breakdown of the denoise+TAAU bucket:
                g.profMs[4] += (double)(ts[5] - ts[2]) * k;   // temporal accumulation
                g.profMs[5] += (double)(ts[6] - ts[5]) * k;   // a-trous (4 iterations)
                g.profMs[6] += (double)(ts[7] - ts[6]) * k;   // composite
                g.profMs[7] += (double)(ts[3] - ts[7]) * k;   // TAAU upscale (display res)
            }
            g.profFrames++;
            int now = I_GetTimeMS();
            if (g.profLastReport == 0) g.profLastReport = now;
            if (now - g.profLastReport >= 1000 && g.profFrames > 0)
            {
                const double f = 1.0 / (double)g.profFrames;
                if (g.profRasterFrame)
                {
                    printf("[raster_profile] %3d fps | shadow %.2f | scene %.2f | ssao %.2f | "
                           "composite %.2f | hud %.2f ms (avg/frame, Solid GPU only)\n",
                           g.profFrames, g.profMs[0] * f, g.profMs[1] * f, g.profMs[2] * f,
                           g.profMs[3] * f, g.profMs[4] * f);
                }
                else
                {
                    int omni = (int)g.emitCount - (int)g.staticWgt.size();
                    printf("[rt_profile] %3d fps | sprites %.2f | megakernel %.2f | "
                           "denoise+taau %.2f (temporal %.2f, atrous %.2f, composite %.2f, "
                           "taau %.2f) | blit %.2f ms | omni %d/%d lights (avg/frame, RT GPU only)\n",
                           g.profFrames, g.profMs[0] * f, g.profMs[1] * f, g.profMs[2] * f,
                           g.profMs[4] * f, g.profMs[5] * f, g.profMs[6] * f, g.profMs[7] * f,
                           g.profMs[3] * f, omni < 0 ? 0 : omni, (int)g.emitCount);
                }
                fflush(stdout);
                for (int pi = 0; pi < 8; pi++) g.profMs[pi] = 0.0;
                g.profFrames = 0;
                g.profLastReport = now;
            }
        }
        g.gpuTimersInUse = false;
    }

    // INV-6 self-test (DOOM-0009 build step 4d). The first in-level present with the
    // full RT scene ready runs the rel-MSE + white-furnace proof against the current
    // camera, prints PASS/FAIL, and exits — a headless gate, never the display path.
    // The fence wait above guarantees no frame is in flight before its one-time
    // dispatches. Cached so the parm is read once.
    if (rb_rtverify < 0)
        rb_rtverify = M_CheckParm("-rtverify") ? 1 : 0;
    if (rb_rtverify == 1 && g.rtEnabled && g.tlas != VK_NULL_HANDLE &&
        g.rtModule != VK_NULL_HANDLE && g.haveCamera &&
        g.vbuf != VK_NULL_HANDLE && g.atlasReady)
    {
        rb_rtverify = 0;
        RB_RtVerify();
        exit(0);
    }

    // DOOM-0202: -shotverify arm + warmup counter. Decide — before this frame records —
    // whether it captures: armed AND the RT view is active AND the denoiser has had
    // kShotWarmup rendered frames to settle on the static spawn view. The copy is recorded
    // inside RecordRtTrace; the PNG write + exit happen after present (below). A watchdog
    // bails if the RT view never becomes ready (launched in Solid, or no level warped in),
    // so a misconfigured run exits instead of spinning forever.
    if (rb_shotverify < 0)
        rb_shotverify = (M_CheckParm("-shotverify") || M_CheckParm("-shotcompare")) ? 1 : 0;
    g.shotCapture = false;
    if (rb_shotverify == 1)
    {
        static int armedPresents = 0;
        if (rtActive)
        {
            if (g.shotFrame >= kShotWarmup) g.shotCapture = true;
            g.shotFrame++;
        }
        if (++armedPresents > kShotGiveUp && g.shotFrame == 0)
        {
            fprintf(stderr, "[shotverify] Ultra RT view never became ready after %d presents "
                            "(need renderer 1 + a level warped in); giving up.\n", kShotGiveUp);
            exit(2);
        }
    }

    uint32_t idx = 0;
    VkResult acq = vkAcquireNextImageKHR(g.device, g.swapchain, UINT64_MAX,
                                         g.imageAvailable, VK_NULL_HANDLE, &idx);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR)
    {
        g.needRecreate = true;
        return;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR)
        Fail("vkAcquireNextImageKHR", acq);

    vkResetFences(g.device, 1, &g.inFlight);
    vkResetCommandBuffer(g.cmd, 0);

    // DOOM-0074: on a traced or just-toggled frame the build was NOT run ahead (its
    // single-copy RT resources must not be in flight); run it now — after the fence, so
    // the previous frame's GPU has finished reading everything. In steady-state raster
    // it already ran before the fence (buildAhead), overlapping the GPU.
    if (!buildAhead)
        BuildFrameInputs(rtActive, cprof);

    // DOOM-0131: the moving-sector world-BLAS refit (build step 5) is recorded into the
    // frame command buffer inside RecordRtTrace (ahead of the TLAS rebuild). blasDirty
    // stays latched until the first traced frame, so an off-screen move under raster is
    // still caught.

    // Copy this frame's 2D overlay (screens[0]) into the mapped staging buffer. Kept
    // after the fence (not in the build-ahead block) so the previous frame's copy has
    // finished — the single-copy staging buffer needs no double-buffering.
    // DOOM-0094: the 2D overlay (HUD/menu/messages/FPS, all composited from screens[0])
    // now draws in BOTH the raster and the path-traced present paths.
    bool drawOverlay = g.overlayReady && g.overlaySrc;
    if (drawOverlay)
        std::memcpy(g.overlayMapped, g.overlaySrc,
                    (size_t)g.overlayW * g.overlayH);

    // DOOM-0170 CPU profiler: start of command recording. The build total (g.cpuMs[1])
    // is set inside BuildFrameInputs, whether it ran before or after the fence.
    const double tRecord0 = cprof ? CpuNowMs() : 0.0;

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(g.cmd, &bi), "vkBeginCommandBuffer");

    // RT debug frame: trace + blit, then skip the raster render pass entirely
    // (RecordRtTrace owns the swapchain layout transition to PRESENT_SRC). The
    // `else` brace closes just before vkEndCommandBuffer.
    if (rtActive)
    {
        RecordRtTrace(idx);
        // DOOM-0094: draw the weapon + the 2D overlay over the traced world.
        RecordRtOverlay(idx, drawOverlay);
    }
    else
    {

    // Upload the overlay staging buffer into its sampled image before the render
    // pass (transfers are illegal inside one). oldLayout UNDEFINED is fine: every
    // texel is overwritten, so the previous frame's contents need not survive.
    if (drawOverlay)
        UploadOverlayImage();

    // DOOM-0170 L2a step 2: render-scale the world. Draw it into the [0,sceneW]x[0,sceneH]
    // corner of the full-size scene target (a fraction of the screen's pixels), then let the
    // composite upscale that corner to the swapchain -- the same dynamic-resolution trick the
    // path tracer uses (RecordRtTrace), so the render-scale menu takes effect per-frame with
    // no swapchain rebuild. render_scale clamps to [25,100]; 100 renders full-res (uvScale 1,1
    // -> byte-identical to before). The saving is the SHADING (fewer viewport pixels); the pass
    // still clears the whole target to slate so the composite's linear edge tap never reads an
    // un-rendered texel (which would seam the screen's right/bottom edge).
    uint32_t rsc = (uint32_t)(rb_renderscale < 25 ? 25
                              : (rb_renderscale > 100 ? 100 : rb_renderscale));
    uint32_t sceneW = (g.extent.width  * rsc) / 100u; if (sceneW < 1u) sceneW = 1u;
    uint32_t sceneH = (g.extent.height * rsc) / 100u; if (sceneH < 1u) sceneH = 1u;
    VkExtent2D sceneExtent = { sceneW, sceneH };
    float uvScale[2] = { (float)sceneW / (float)g.extent.width,
                         (float)sceneH / (float)g.extent.height };

    // DOOM-0170 perf: per-pass raster GPU timer (opt-in, the `\` key / rb_profile). Reset the
    // pool + stamp the raster frame start here (outside any render pass), then a timestamp at
    // each pass boundary below (shadow / scene / SSAO / composite / HUD). Read back at the top
    // of the next present; profRasterFrame routes that readback to the raster interpretation.
    // RT frames drive the same 8-slot pool from RecordRtTrace, and the two are mutually
    // exclusive per frame, so they never collide.
    const bool rprof = rb_profile && g.gpuTimerPool;
    if (rprof) {
        vkCmdResetQueryPool(g.cmd, g.gpuTimerPool, 0, 8);
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, g.gpuTimerPool, 0);
    }

    // DOOM-0170 L2c: flashlight cast-shadow map (Pass A). When the torch is on, render the
    // world (walls/flats + monster/item billboards; the psprite/sky are clipped away in
    // shadow.vert) depth-only from the flashlight's viewpoint, so mesh.frag can PCF-test the
    // cone against it below. Recorded before (outside) the scene pass. Skipped — zero cost —
    // when the torch is off; g.shadowDs then still points at the last-parked depth image.
    if (rb_flashlight && g.haveCamera && g.atlasReady && g.vbuf && g.vertexCount)
    {
        // Light view-projection: the torch is held UP and to the SIDE of the eye (Doom 3-style),
        // NOT exactly at it. A light co-located with the camera casts no VISIBLE shadow — every
        // shadow falls directly behind its caster, hidden by that caster from the same viewpoint.
        // Offsetting the light gives camera/light parallax so shadows swing into view. The offset
        // is in view space (right vector + world up) and MUST match mesh.frag's flashlight cone
        // (FLASH_OFF_RIGHT / FLASH_OFF_UP) so the shadow lines up with the lit beam. Aimed along
        // the view yaw; a square 90-degree frustum comfortably covers the ~70-degree beam.
        static const float kFlashOffRight = 28.0f;   // view-right offset, world units (tunable)
        static const float kFlashOffUp    = 22.0f;   // world-up offset, world units (tunable)
        float lcos = std::cos(g.lastView.angle), lsin = std::sin(g.lastView.angle);
        float lright[3] = { lsin, -lcos, 0.0f };     // view-right = fwd × worldUp
        float leye[3] = { g.lastView.x + lright[0] * kFlashOffRight,
                          g.lastView.y + lright[1] * kFlashOffRight,
                          g.lastView.z + kFlashOffUp };
        float lfwd[3] = { lcos, lsin, 0.0f };
        float lup[3]  = { 0.0f, 0.0f, 1.0f };
        float lview[16], lproj[16], lightVP[16];
        Mat4LookAt(leye, lfwd, lup, lview);
        Mat4PerspectiveH(kPi * 0.5f, 1.0f, 10.0f, 2000.0f, lproj);
        Mat4Mul(lproj, lview, lightVP);
        std::memcpy(g.shadowUboMapped, lightVP, 16 * sizeof(float));

        VkClearValue sclear = {};
        sclear.depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo srp = {};
        srp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        srp.renderPass = g.shadowPass;
        srp.framebuffer = g.shadowFb;
        srp.renderArea.extent = { VulkanState::kShadowDim, VulkanState::kShadowDim };
        srp.clearValueCount = 1;
        srp.pClearValues = &sclear;
        vkCmdBeginRenderPass(g.cmd, &srp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport svp = {};
        svp.width = (float)VulkanState::kShadowDim;
        svp.height = (float)VulkanState::kShadowDim;
        svp.maxDepth = 1.0f;
        vkCmdSetViewport(g.cmd, 0, 1, &svp);
        VkRect2D ssc = { { 0, 0 }, { VulkanState::kShadowDim, VulkanState::kShadowDim } };
        vkCmdSetScissor(g.cmd, 0, 1, &ssc);

        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.shadowPipeline);
        vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g.shadowPipeLayout, 0, 1, &g.ds, 0, nullptr);
        struct { float m[16]; int nw; int nf; } spc;   // matches shadow.vert/frag push block
        std::memcpy(spc.m, lightVP, sizeof(spc.m));
        spc.nw = g.matNumWall;
        spc.nf = g.matNumFlat;
        vkCmdPushConstants(g.cmd, g.shadowPipeLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(spc), &spc);

        VkDeviceSize soff = 0;
        vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.vbuf, &soff);
        vkCmdDraw(g.cmd, g.vertexCount, 1, 0, 0);
        if (g.spriteVbuf && g.spriteVertCount)   // monsters/items cast (weapon clipped away)
        {
            vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.spriteVbuf, &soff);
            vkCmdDraw(g.cmd, g.spriteVertCount, 1, g.skyVertCount, 0);
        }
        vkCmdEndRenderPass(g.cmd);
    }

    if (rprof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 1);  // after shadow pass

    // DOOM-0170 L2a: draw the world into the OFF-SCREEN scene target (scenePass leaves it
    // in SHADER_READ_ONLY); the composite pass below samples it to the swapchain. Clear to
    // a dark slate (world background) + far depth.
    // DOOM-0170 L2b — three clears matching the scene pass's attachments: AMBIENT to the dark
    // slate background, DIRECT to black (nothing directly lit until a light writes it), depth
    // to far. The composite sums AMBIENT+DIRECT, so the visible background stays the slate.
    VkClearValue clears[3] = {};
    clears[0].color = { { 0.05f, 0.06f, 0.09f, 1.0f } };   // AMBIENT background
    clears[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };      // DIRECT (additive) starts at black
    clears[2].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g.scenePass;
    rp.framebuffer = g.sceneFb;
    rp.renderArea.extent = g.extent;   // clear the whole target (see note); viewport scales shading
    rp.clearValueCount = 3;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(g.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Draw once we have the texture atlas and a camera (e.g. not in a pre-level
    // menu); otherwise the clear alone presents. The static level mesh and the
    // per-frame billboard sprites share the pipeline, descriptor set, and view
    // matrix — sprites just bind a second vertex buffer and draw after the walls.
    if (g.haveCamera && g.atlasReady)
    {
        VkViewport vpRect = {};
        vpRect.width = (float)sceneW;         // render-scaled corner (L2a step 2)
        vpRect.height = (float)sceneH;
        vpRect.maxDepth = 1.0f;
        vkCmdSetViewport(g.cmd, 0, 1, &vpRect);
        VkRect2D scissor = { { 0, 0 }, sceneExtent };
        vkCmdSetScissor(g.cmd, 0, 1, &scissor);

        // DOOM-0170 L2c: set 0 = g.ds (materials); set 1 = g.shadowDs (flashlight shadow
        // map + lightVP), which mesh.frag samples. Both bound once for the sky+world draws.
        VkDescriptorSet worldDs[2] = { g.ds, g.shadowDs };
        vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g.pipelineLayout, 0, 2, worldDs, 0, nullptr);
        // mat4 MVP, then the muzzle-flash brighten (mesh.vert adds it to every
        // shade) and the view yaw (mesh.frag pans the sky by it). Push constants
        // and descriptor sets are layout-scoped, so they outlive the pipeline
        // binds below — set them once for both the sky and world pipelines.
        float pcData[31] = {};
        std::memcpy(pcData, g.viewProj, 16 * sizeof(float));
        pcData[16] = g.lastView.extralight;
        pcData[17] = g.lastView.angle;
        pcData[18] = g.lastView.x;     // camera world pos: distance light falloff
        pcData[19] = g.lastView.y;
        pcData[20] = g.lastView.z;
        // Material-id offsets (ints, reinterpreted into the float buffer): the
        // fragment shader maps per-category texnum -> unified bindless-array id.
        std::memcpy(&pcData[21], &g.matNumWall, sizeof(int));
        std::memcpy(&pcData[22], &g.matNumFlat, sizeof(int));
        pcData[23] = rb_flashlight ? 1.0f : 0.0f;   // DOOM-0044 raster flashlight cone
        // DOOM-0170 L1a/L1b: baked-probe indirect bounce + per-subsector point lights in
        // the raster path. Pass the probe, triSs and light buffers by device address
        // (8-byte aligned at float 24/26/28) and the probe/subsector count (float 30).
        // Zero addr/count when the bake is absent (non-RT GPU) so mesh.frag falls back to
        // the plain sector-lit look. probeCount is gated on all three addrs so mesh.frag's
        // probeCount>0 guard never dereferences an unbound buffer.
        uint64_t probeAddr = (g.probeBuf && g.probeCount) ? BufferAddress(g.probeBuf) : 0;
        uint64_t triSsAddr = (g.triSsBuf && g.probeCount) ? BufferAddress(g.triSsBuf) : 0;
        uint64_t lightAddr = (g.lightBuf && g.probeCount) ? BufferAddress(g.lightBuf) : 0;
        uint32_t probeN    = (probeAddr && triSsAddr && lightAddr) ? g.probeCount : 0u;
        std::memcpy(&pcData[24], &probeAddr, sizeof(uint64_t));
        std::memcpy(&pcData[26], &triSsAddr, sizeof(uint64_t));
        std::memcpy(&pcData[28], &lightAddr, sizeof(uint64_t));
        std::memcpy(&pcData[30], &probeN,    sizeof(uint32_t));
        vkCmdPushConstants(g.cmd, g.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 31 * sizeof(float), pcData);

        VkDeviceSize off = 0;
        // Sky first, behind everything: depth-off pipeline, the 6 verts at the
        // front of the sprite buffer. The world then overdraws it wherever there
        // is solid geometry, leaving sky only in the sky-flat openings.
        if (g.spriteVbuf && g.skyVertCount)
        {
            vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.skyPipeline);
            vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.spriteVbuf, &off);
            vkCmdDraw(g.cmd, g.skyVertCount, 1, 0, 0);
        }

        // Wireframe debug view (rb_wireframe, gamepad Share): draw the world and
        // sprites as lines over the filled sky so what the mesh actually emits is
        // visible. Falls back to the fill pipeline if the wire one was not built.
        VkPipeline worldPipe = (rb_wireframe && g.wirePipeline)
                             ? g.wirePipeline : g.pipeline;
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldPipe);
        if (g.vbuf && g.vertexCount)
        {
            vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.vbuf, &off);
            vkCmdDraw(g.cmd, g.vertexCount, 1, 0, 0);
        }
        // DOOM-0162: draw the DOOM-0141 sky occluder mesh (the RT sky backdrop --
        // emit_sky_wall/emit_sky_cap) here too, with the same world pipeline
        // (cull-none, depth-ON). Its verts carry RB_MESH_SKYDOME so the vertex
        // shader MVP-projects them (world-space, unlike the NDC backdrop quad) and
        // mesh.frag paints them as the panorama; the depth test makes it occlude
        // distant geometry and be occluded by nearer walls -- exactly what the tracer gets
        // from the sky BLAS. Without this the raster sky is only the depth-OFF
        // full-screen quad above, which cannot occlude, so far buildings hang in
        // front of it (the "floating buildings" seen only in the raster view).
        if (g.skyMeshBuf && g.skyMeshVerts)
        {
            vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.skyMeshBuf, &off);
            vkCmdDraw(g.cmd, g.skyMeshVerts, 1, 0, 0);
        }
        // DOOM-0170 L2d: blob shadows. Drawn after the world (so they alpha-blend onto the
        // floor) but before the billboards (so a Thing's sprite sits on top of its own
        // shadow). The blob pipeline binds here, then worldPipe is restored for the sprites.
        if (g.blobPipeline && g.spriteVbuf && g.blobVertCount)
        {
            vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.blobPipeline);
            vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.spriteVbuf, &off);
            vkCmdDraw(g.cmd, g.blobVertCount, 1, g.blobVertOffset, 0);
            vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, worldPipe);
        }
        // Sprites + weapon: same buffer as the sky, but skip its leading verts.
        if (g.spriteVbuf && g.spriteVertCount)
        {
            vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.spriteVbuf, &off);
            vkCmdDraw(g.cmd, g.spriteVertCount, 1, g.skyVertCount, 0);
        }
    }

    // DOOM-0170 L2a: the world is now in the off-screen scene target. Close that pass and
    // open the swapchain pass; a full-screen composite samples the scene to the screen
    // (this is the tone-map seam for step 2), then the HUD draws on top in the same pass.
    vkCmdEndRenderPass(g.cmd);   // end scenePass
    if (rprof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 2);  // after scene pass

    // Transition BOTH scene targets (AMBIENT + DIRECT) COLOR_ATTACHMENT -> SHADER_READ and
    // make the world writes visible to the composite's sample (see the scenePass note above).
    {
        VkImageMemoryBarrier b[2] = {};
        b[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        b[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b[0].srcQueueFamilyIndex = b[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[0].image = g.sceneImage;
        b[0].subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        b[1] = b[0];
        b[1].image = g.sceneDirImage;
        vkCmdPipelineBarrier(g.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 2, b);
    }

    // DOOM-0170 L2b — SSAO pass (Pass C, §4.3). Half-res: reads the DIRECT target's packed
    // forward-distance depth (now SHADER_READ from the barrier above) and writes the occlusion
    // image the composite multiplies into AMBIENT. Gated by rb_ssao; when off it is skipped and
    // the composite reads the parked AO image with aoEnable=0 (ignored). No clear (fully drawn).
    if (rb_ssao && g.aoPipeline && g.ssaoDs && g.haveCamera && g.atlasReady)
    {
        VkRenderPassBeginInfo arp = {};
        arp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        arp.renderPass  = g.aoPass;
        arp.framebuffer = g.aoFb;
        arp.renderArea.extent = g.aoExtent;
        arp.clearValueCount = 0;
        vkCmdBeginRenderPass(g.cmd, &arp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport avp = {};
        avp.width = (float)g.aoExtent.width;
        avp.height = (float)g.aoExtent.height;
        avp.maxDepth = 1.0f;
        vkCmdSetViewport(g.cmd, 0, 1, &avp);
        VkRect2D asc = { { 0, 0 }, g.aoExtent };
        vkCmdSetScissor(g.cmd, 0, 1, &asc);

        vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g.aoPipeLayout, 0, 1, &g.ssaoDs, 0, nullptr);
        float aoAspect = g.extent.height ? (float)g.extent.width / (float)g.extent.height : 1.0f;
        // Matches ssao.frag's Push: uvScale, tanH (hfov 90 -> tan(45)=1), aspect, then the dials.
        float sp[8] = { uvScale[0], uvScale[1], 1.0f, aoAspect,
                        kSsaoRadius, kSsaoBias, kSsaoIntensity, kSsaoPower };
        vkCmdPushConstants(g.cmd, g.aoPipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(sp), sp);
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.aoPipeline);
        vkCmdDraw(g.cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(g.cmd);
    }

    if (rprof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 3);  // after SSAO pass

    rp.renderPass = g.renderPass;
    rp.framebuffer = g.framebuffers[idx];
    vkCmdBeginRenderPass(g.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    {
        VkViewport vpRect = {};
        vpRect.width = (float)g.extent.width;
        vpRect.height = (float)g.extent.height;
        vpRect.maxDepth = 1.0f;
        vkCmdSetViewport(g.cmd, 0, 1, &vpRect);
        VkRect2D scissor = { { 0, 0 }, g.extent };
        vkCmdSetScissor(g.cmd, 0, 1, &scissor);
        vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g.compositePipeLayout, 0, 1, &g.compositeDs, 0, nullptr);
        // Tell the composite which [0,uvScale] corner of the scene target the render-scaled
        // world filled (so it upscales exactly that region), plus whether SSAO is on (aoEnable;
        // 0 makes the composite ignore the AO texture and leave AMBIENT un-occluded).
        float coPush[4] = { uvScale[0], uvScale[1], rb_ssao ? 1.0f : 0.0f, 0.0f };
        vkCmdPushConstants(g.cmd, g.compositePipeLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(coPush), coPush);
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.compositePipeline);
        vkCmdDraw(g.cmd, 3, 1, 0, 0);
        if (rprof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 4);  // after composite
    }

    // 2D HUD/menu compositor, last and over everything: a vertexless full-screen
    // triangle that samples screens[0] and keys out the transparent index so the
    // 3D scene shows through the view area. Drawn outside the camera gate so the
    // full-screen 2D states (intermission/finale, the menu) composite even when
    // no world was rendered; it sets its own viewport/scissor and binds the set
    // for that case.
    if (drawOverlay)
    {
        VkViewport vpRect = {};
        vpRect.width = (float)g.extent.width;
        vpRect.height = (float)g.extent.height;
        vpRect.maxDepth = 1.0f;
        vkCmdSetViewport(g.cmd, 0, 1, &vpRect);
        VkRect2D scissor = { { 0, 0 }, g.extent };
        vkCmdSetScissor(g.cmd, 0, 1, &scissor);

        vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                g.pipelineLayout, 0, 1, &g.ds, 0, nullptr);
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.overlayPipeline);
        vkCmdDraw(g.cmd, 3, 1, 0, 0);
    }

    FlushMenuText();   // DOOM-0206 L1b: crisp menu glyphs over the paletted overlay

    vkCmdEndRenderPass(g.cmd);
    if (rprof) {
        vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 5);  // after HUD/present
        g.gpuTimersInUse  = true;   // results ready to read at the top of next frame
        g.profRasterFrame = true;   // this timed frame was the raster path -> raster readback
    }
    }   // end of the non-RT (raster) recording branch

    Check(vkEndCommandBuffer(g.cmd), "vkEndCommandBuffer");

    // DOOM-0170 CPU profiler: end of command recording, start of submit + present.
    const double tSubmit0 = cprof ? CpuNowMs() : 0.0;
    if (cprof) g.cpuMs[2] += tSubmit0 - tRecord0;

    // The RT path's first swapchain write is the blit (TRANSFER); the raster
    // path's is the colour attachment. Wait the acquire semaphore at the matching
    // stage so the image isn't written before it's acquired.
    VkPipelineStageFlags waitStage = rtActive
        ? VK_PIPELINE_STAGE_TRANSFER_BIT
        : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g.imageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g.renderFinished[idx];   // per-image (see struct note)
    Check(vkQueueSubmit(g.queue, 1, &si, g.inFlight), "vkQueueSubmit");

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g.renderFinished[idx];
    pi.swapchainCount = 1;
    pi.pSwapchains = &g.swapchain;
    pi.pImageIndices = &idx;
    VkResult pr = vkQueuePresentKHR(g.queue, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR)
        g.needRecreate = true;
    else if (pr != VK_SUCCESS)
        Fail("vkQueuePresentKHR", pr);

    // DOOM-0202: -shotverify write / -shotcompare gate + exit. The capture copy was
    // recorded into the frame just submitted; wait for the GPU to finish, then read the
    // host-visible buffer. One-shot: the process exits after writing / comparing.
    if (g.shotCapture && g.shotBuf)
    {
        vkDeviceWaitIdle(g.device);
        void* mapped = nullptr;
        Check(vkMapMemory(g.device, g.shotBufMem, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(shot)");

        int cmp = M_CheckParm("-shotcompare");
        if (cmp)
        {
            // Automated regression gate. Downscale the capture to a small, git-friendly
            // canonical size (reusing rb_image.c's box filter), then either bootstrap the
            // golden (ref missing — first run) or compare against it. Metric: mean-abs-error
            // over RGB (alpha is a constant 255). Exit: 0 = pass/bootstrap, 3 = fail, 1 = i/o.
            const char* ref = (cmp + 1 < myargc && myargv[cmp + 1][0] != '-')
                              ? myargv[cmp + 1] : "shotgolden.png";
            // Copy the mapped GPU buffer into a malloc'd rb_image_t (downscale frees it).
            rb_image_t cap;
            cap.w = (int)g.shotW; cap.h = (int)g.shotH;
            size_t nbytes = (size_t)cap.w * cap.h * 4;
            cap.pixels = (unsigned char*)malloc(nbytes);
            if (cap.pixels) memcpy(cap.pixels, mapped, nbytes);
            vkUnmapMemory(g.device, g.shotBufMem);
            if (!cap.pixels) { fprintf(stderr, "[shotverify] OOM copying %zu-byte capture\n", nbytes); exit(1); }
            rb_image_downscale_max(&cap, kGoldenEdge);

            rb_image_t golden;
            if (!rb_image_load(ref, &golden))
            {
                int ok = stbi_write_png(ref, cap.w, cap.h, 4, cap.pixels, cap.w * 4);
                if (ok) printf("[shotverify] wrote golden %s (%dx%d) — commit it, re-run to gate\n",
                               ref, cap.w, cap.h);
                else    fprintf(stderr, "[shotverify] failed to write golden %s\n", ref);
                free(cap.pixels);
                fflush(stdout);
                exit(ok ? 0 : 1);
            }
            if (golden.w != cap.w || golden.h != cap.h)
            {
                fprintf(stderr, "[shotverify] FAIL size mismatch: golden %dx%d vs capture %dx%d "
                                "(delete %s to re-bootstrap)\n", golden.w, golden.h, cap.w, cap.h, ref);
                rb_image_free(&golden); free(cap.pixels); fflush(stderr); exit(3);
            }
            double sum = 0.0; size_t n = (size_t)cap.w * cap.h;
            for (size_t i = 0; i < n; i++)
                for (int c = 0; c < 3; c++)
                    sum += fabs((double)cap.pixels[i * 4 + c] - (double)golden.pixels[i * 4 + c]);
            double mae = sum / (double)(n * 3);
            rb_image_free(&golden); free(cap.pixels);
            int pass = (mae <= kGoldenMAE);
            printf("[shotverify] %s mae=%.3f (threshold %.3f, %dx%d) vs %s\n",
                   pass ? "PASS" : "FAIL", mae, kGoldenMAE, cap.w, cap.h, ref);
            fflush(stdout);
            exit(pass ? 0 : 3);
        }

        // Plain -shotverify: full-res PNG for eyeballing. Path = the arg after -shotverify
        // (when present and not another flag), else "shotverify.png" in the cwd.
        const char* path = "shotverify.png";
        int p = M_CheckParm("-shotverify");
        if (p && p + 1 < myargc && myargv[p + 1][0] != '-')
            path = myargv[p + 1];
        int ok = stbi_write_png(path, (int)g.shotW, (int)g.shotH, 4, mapped, (int)g.shotW * 4);
        vkUnmapMemory(g.device, g.shotBufMem);
        if (ok) printf("[shotverify] wrote %s (%ux%u, Ultra RT)\n", path, g.shotW, g.shotH);
        else    fprintf(stderr, "[shotverify] stbi_write_png failed for %s\n", path);
        fflush(stdout);
        exit(ok ? 0 : 1);
    }

    // DOOM-0170 perf: close the CPU-side segments and print once a second. cpuMs =
    // fenceWait / build / record / submit / present-total. The gap between the FPS
    // counter's frame time and present-total is the rest of the engine (game tick +
    // the software 2D overlay drawn into screens[0] each frame). fenceWait is the key
    // read: high => GPU-bound (2-frames-in-flight helps), ~0 => CPU-bound.
    if (cprof)
    {
        const double tEnd = CpuNowMs();
        g.cpuMs[3] += tEnd - tSubmit0;        // submit + present call
        g.cpuMs[4] += tEnd - tPresent0;       // whole present (incl. fence wait idle)
        g.cpuFrames++;
        int nowc = I_GetTimeMS();
        if (g.cpuLastReport == 0) g.cpuLastReport = nowc;
        if (nowc - g.cpuLastReport >= 1000 && g.cpuFrames > 0)
        {
            const double f = 1.0 / (double)g.cpuFrames;
            printf("[cpu_profile] %3d fps | fenceWait %.2f | build %.2f | record %.2f | "
                   "submit %.2f | present-total %.2f ms (avg/frame, CPU wall clock)\n",
                   g.cpuFrames, g.cpuMs[0] * f, g.cpuMs[1] * f, g.cpuMs[2] * f,
                   g.cpuMs[3] * f, g.cpuMs[4] * f);
            printf("[cpu_build]   build %.2f = sprites %.2f + lights %.2f + reheight %.2f ms "
                   "(avg/frame; lights = NEE emitter refill + per-subsector point-light cull)\n",
                   g.cpuMs[1] * f, g.cpuBuildMs[0] * f, g.cpuBuildMs[1] * f, g.cpuBuildMs[2] * f);
            fflush(stdout);
            for (int ci = 0; ci < 5; ci++) g.cpuMs[ci] = 0.0;
            for (int ci = 0; ci < 3; ci++) g.cpuBuildMs[ci] = 0.0;
            g.cpuFrames = 0;
            g.cpuLastReport = nowc;
        }
    }
}

extern "C" void RB_Vulkan_Shutdown(void)
{
    if (g.levelMesh)
    {
        RB_FreeMesh(g.levelMesh);
        g.levelMesh = nullptr;
    }

    if (!g.instance)
        return;
    if (g.device)
        vkDeviceWaitIdle(g.device);

    if (g.rtEnabled)        DestroyAccelerationStructures();
    // Path-tracer compute resources (DOOM-0009 build step 2c).
    if (g.rtEnabled)
    {
        DestroyRtTargets();
        // DOOM-0129: free every per-mode megakernel variant + the shared module.
        for (uint32_t m = 0; m < 7; m++)
            if (g.rtPipeline[m]) vkDestroyPipeline(g.device, g.rtPipeline[m], nullptr);
        if (g.rtModule)     vkDestroyShaderModule(g.device, g.rtModule, nullptr);
        if (g.rtPipeLayout) vkDestroyPipelineLayout(g.device, g.rtPipeLayout, nullptr);
        if (g.rtDsPool)     vkDestroyDescriptorPool(g.device, g.rtDsPool, nullptr);
        if (g.rtDsLayout)   vkDestroyDescriptorSetLayout(g.device, g.rtDsLayout, nullptr);
        // INV-6 verify accumulator + readback (step 4d).
        if (g.rtAccumView)  vkDestroyImageView(g.device, g.rtAccumView, nullptr);
        if (g.rtAccum)      vkDestroyImage(g.device, g.rtAccum, nullptr);
        if (g.rtAccumMem)   vkFreeMemory(g.device, g.rtAccumMem, nullptr);
        if (g.rtReadback)   vkDestroyBuffer(g.device, g.rtReadback, nullptr);
        if (g.rtReadbackMem) vkFreeMemory(g.device, g.rtReadbackMem, nullptr);
        if (g.shotBuf)      vkDestroyBuffer(g.device, g.shotBuf, nullptr);   // DOOM-0202
        if (g.shotBufMem)   vkFreeMemory(g.device, g.shotBufMem, nullptr);
        // GI bake pipeline (step 4b-ii).
        if (g.bakePipeline)   vkDestroyPipeline(g.device, g.bakePipeline, nullptr);
        if (g.bakePipeLayout) vkDestroyPipelineLayout(g.device, g.bakePipeLayout, nullptr);
        if (g.bakeDsPool)     vkDestroyDescriptorPool(g.device, g.bakeDsPool, nullptr);
        if (g.bakeDsLayout)   vkDestroyDescriptorSetLayout(g.device, g.bakeDsLayout, nullptr);
        // SVGF denoiser pipelines (step 6); its images are freed by DestroyRtTargets.
        if (g.svgfTemporal)   vkDestroyPipeline(g.device, g.svgfTemporal, nullptr);
        if (g.svgfAtrous)     vkDestroyPipeline(g.device, g.svgfAtrous, nullptr);
        if (g.svgfComposite)  vkDestroyPipeline(g.device, g.svgfComposite, nullptr);
        if (g.svgfPipeLayout) vkDestroyPipelineLayout(g.device, g.svgfPipeLayout, nullptr);
        if (g.labelPipeline)   vkDestroyPipeline(g.device, g.labelPipeline, nullptr);
        if (g.labelPipeLayout) vkDestroyPipelineLayout(g.device, g.labelPipeLayout, nullptr);
        if (g.svgfDsPool)     vkDestroyDescriptorPool(g.device, g.svgfDsPool, nullptr);
        if (g.svgfDsLayout)   vkDestroyDescriptorSetLayout(g.device, g.svgfDsLayout, nullptr);
        // Temporal upscaler (step 6-d); its images are freed by DestroyRtTargets.
        if (g.taauPipeline)   vkDestroyPipeline(g.device, g.taauPipeline, nullptr);
        if (g.taauPipeLayout) vkDestroyPipelineLayout(g.device, g.taauPipeLayout, nullptr);
        if (g.taauDsPool)     vkDestroyDescriptorPool(g.device, g.taauDsPool, nullptr);
        if (g.taauDsLayout)   vkDestroyDescriptorSetLayout(g.device, g.taauDsLayout, nullptr);
        // Direct-lighting buffers (step 3b): per-level emitter list + WAD-global Le table.
        if (g.emitBuf)      vkDestroyBuffer(g.device, g.emitBuf, nullptr);
        if (g.emitMem)      vkFreeMemory(g.device, g.emitMem, nullptr);
        if (g.emitSecBuf)   vkDestroyBuffer(g.device, g.emitSecBuf, nullptr);
        if (g.emitSecMem)   vkFreeMemory(g.device, g.emitSecMem, nullptr);
        // DOOM-0170 L1b per-subsector point-light buffer (host-visible, per-frame fill;
        // DOOM-0074: one copy per in-flight slot).
        for (uint32_t s = 0; s < VulkanState::kFramesInFlight; s++)
        {
            if (g.lightBufSlot[s]) vkDestroyBuffer(g.device, g.lightBufSlot[s], nullptr);
            if (g.lightMemSlot[s]) vkFreeMemory(g.device, g.lightMemSlot[s], nullptr);
        }
        if (g.matEmisBuf)   vkDestroyBuffer(g.device, g.matEmisBuf, nullptr);
        if (g.matEmisMem)   vkFreeMemory(g.device, g.matEmisMem, nullptr);
        // GI bake probes + per-triangle subsector map (step 4).
        if (g.probeBuf)     vkDestroyBuffer(g.device, g.probeBuf, nullptr);
        if (g.probeMem)     vkFreeMemory(g.device, g.probeMem, nullptr);
        if (g.probeBuf2)    vkDestroyBuffer(g.device, g.probeBuf2, nullptr);
        if (g.probeMem2)    vkFreeMemory(g.device, g.probeMem2, nullptr);
        if (g.triSsBuf)     vkDestroyBuffer(g.device, g.triSsBuf, nullptr);
        if (g.triSsMem)     vkFreeMemory(g.device, g.triSsMem, nullptr);
        // DOOM-0119 REJECT-lump light-cull buffers.
        if (g.subSecBuf)    vkDestroyBuffer(g.device, g.subSecBuf, nullptr);
        if (g.subSecMem)    vkFreeMemory(g.device, g.subSecMem, nullptr);
        if (g.rejectBuf)    vkDestroyBuffer(g.device, g.rejectBuf, nullptr);
        if (g.rejectMem)    vkFreeMemory(g.device, g.rejectMem, nullptr);
    }
    // DOOM-0074: free every in-flight slot of the double-buffered vertex/sprite buffers.
    for (uint32_t s = 0; s < VulkanState::kFramesInFlight; s++)
    {
        if (g.vbufMappedSlot[s])    vkUnmapMemory(g.device, g.vbufMemSlot[s]);
        if (g.vbufSlot[s])          vkDestroyBuffer(g.device, g.vbufSlot[s], nullptr);
        if (g.vbufMemSlot[s])       vkFreeMemory(g.device, g.vbufMemSlot[s], nullptr);
        if (g.spriteVbufSlot[s])    vkDestroyBuffer(g.device, g.spriteVbufSlot[s], nullptr);
        if (g.spriteVbufMemSlot[s]) vkFreeMemory(g.device, g.spriteVbufMemSlot[s], nullptr);
    }

    // Material + palette resources.
    if (g.dsPool)      vkDestroyDescriptorPool(g.device, g.dsPool, nullptr);
    if (g.dsLayout)    vkDestroyDescriptorSetLayout(g.device, g.dsLayout, nullptr);
    if (g.texSampler)  vkDestroySampler(g.device, g.texSampler, nullptr);
    if (g.compositeSampler) vkDestroySampler(g.device, g.compositeSampler, nullptr);
    if (g.hdSampler)   vkDestroySampler(g.device, g.hdSampler, nullptr);   // DOOM-0042
    FreeHdMaterials();                                                     // DOOM-0042: pool/images/SSBO
    if (g.hdSetLayout) vkDestroyDescriptorSetLayout(g.device, g.hdSetLayout, nullptr);
    for (VkImageView v : g.matViews)  if (v) vkDestroyImageView(g.device, v, nullptr);
    for (VkImage    im : g.matImages) if (im) vkDestroyImage(g.device, im, nullptr);
    if (g.matMemory)   vkFreeMemory(g.device, g.matMemory, nullptr);
    if (g.palView)     vkDestroyImageView(g.device, g.palView, nullptr);
    if (g.palImage)    vkDestroyImage(g.device, g.palImage, nullptr);
    if (g.palMemory)   vkFreeMemory(g.device, g.palMemory, nullptr);

    // 2D HUD/menu overlay resources.
    if (g.overlayView)       vkDestroyImageView(g.device, g.overlayView, nullptr);
    if (g.overlayImage)      vkDestroyImage(g.device, g.overlayImage, nullptr);
    if (g.overlayMemory)     vkFreeMemory(g.device, g.overlayMemory, nullptr);
    if (g.overlayStaging)    vkDestroyBuffer(g.device, g.overlayStaging, nullptr);
    if (g.overlayStagingMem) vkFreeMemory(g.device, g.overlayStagingMem, nullptr);

    // DOOM-0206 (L1b) menu-text resources. Descriptor set rides its pool.
    if (g.textPipeline)       vkDestroyPipeline(g.device, g.textPipeline, nullptr);
    if (g.textPipelineLayout) vkDestroyPipelineLayout(g.device, g.textPipelineLayout, nullptr);
    if (g.textDsPool)         vkDestroyDescriptorPool(g.device, g.textDsPool, nullptr);
    if (g.textDsLayout)       vkDestroyDescriptorSetLayout(g.device, g.textDsLayout, nullptr);
    if (g.textSampler)        vkDestroySampler(g.device, g.textSampler, nullptr);
    if (g.textAtlasView)      vkDestroyImageView(g.device, g.textAtlasView, nullptr);
    if (g.textAtlas)          vkDestroyImage(g.device, g.textAtlas, nullptr);
    if (g.textAtlasMemory)    vkFreeMemory(g.device, g.textAtlasMemory, nullptr);
    if (g.textVbufMapped)     vkUnmapMemory(g.device, g.textVbufMemory);
    if (g.textVbuf)           vkDestroyBuffer(g.device, g.textVbuf, nullptr);
    if (g.textVbufMemory)     vkFreeMemory(g.device, g.textVbufMemory, nullptr);
    // DOOM-0206 v2 crisp-cursor resources (reuse the text layout/sampler, freed above/below).
    // The descriptor set rides its own pool.
    if (g.cursorPipeline)     vkDestroyPipeline(g.device, g.cursorPipeline, nullptr);
    if (g.cursorDsPool)       vkDestroyDescriptorPool(g.device, g.cursorDsPool, nullptr);
    if (g.cursorView)         vkDestroyImageView(g.device, g.cursorView, nullptr);
    if (g.cursorImage)        vkDestroyImage(g.device, g.cursorImage, nullptr);
    if (g.cursorMemory)       vkFreeMemory(g.device, g.cursorMemory, nullptr);
    // DOOM-0206 logo sprite (no pipeline of its own — reuses cursorPipeline).
    if (g.logoDsPool)         vkDestroyDescriptorPool(g.device, g.logoDsPool, nullptr);
    if (g.logoView)           vkDestroyImageView(g.device, g.logoView, nullptr);
    if (g.logoImage)          vkDestroyImage(g.device, g.logoImage, nullptr);
    if (g.logoMemory)         vkFreeMemory(g.device, g.logoMemory, nullptr);
    rb_text_free_font(&g.menuFont);   // no-op if the pixels were already freed after upload

    DestroyFramebufferResources();   // framebuffers, depth, swapchain image views
    if (g.pipeline)       vkDestroyPipeline(g.device, g.pipeline, nullptr);
    if (g.rtWeaponPipeline) vkDestroyPipeline(g.device, g.rtWeaponPipeline, nullptr);
    if (g.wirePipeline)   vkDestroyPipeline(g.device, g.wirePipeline, nullptr);
    if (g.blobPipeline)   vkDestroyPipeline(g.device, g.blobPipeline, nullptr);
    if (g.skyPipeline)    vkDestroyPipeline(g.device, g.skyPipeline, nullptr);
    if (g.overlayPipeline) vkDestroyPipeline(g.device, g.overlayPipeline, nullptr);
    if (g.pipelineLayout) vkDestroyPipelineLayout(g.device, g.pipelineLayout, nullptr);
    // DOOM-0170 L2a composite objects (size-independent; the scene image/fb are freed by
    // DestroyFramebufferResources above).
    if (g.compositePipeline)   vkDestroyPipeline(g.device, g.compositePipeline, nullptr);
    if (g.compositePipeLayout) vkDestroyPipelineLayout(g.device, g.compositePipeLayout, nullptr);
    if (g.compositeDsPool)     vkDestroyDescriptorPool(g.device, g.compositeDsPool, nullptr);
    if (g.compositeDsLayout)   vkDestroyDescriptorSetLayout(g.device, g.compositeDsLayout, nullptr);
    // DOOM-0170 L2b — SSAO pipeline/pass/descriptors (aoImage + aoFb ride DestroySceneTarget).
    if (g.aoPipeline)    vkDestroyPipeline(g.device, g.aoPipeline, nullptr);
    if (g.aoPipeLayout)  vkDestroyPipelineLayout(g.device, g.aoPipeLayout, nullptr);
    if (g.ssaoDsPool)    vkDestroyDescriptorPool(g.device, g.ssaoDsPool, nullptr);
    if (g.ssaoDsLayout)  vkDestroyDescriptorSetLayout(g.device, g.ssaoDsLayout, nullptr);
    if (g.aoPass)        vkDestroyRenderPass(g.device, g.aoPass, nullptr);
    // DOOM-0170 L2c flashlight shadow map (size-independent; built once in CreateShadowResources).
    if (g.shadowPipeline)   vkDestroyPipeline(g.device, g.shadowPipeline, nullptr);
    if (g.shadowPipeLayout) vkDestroyPipelineLayout(g.device, g.shadowPipeLayout, nullptr);
    if (g.shadowDsPool)     vkDestroyDescriptorPool(g.device, g.shadowDsPool, nullptr);
    if (g.shadowDsLayout)   vkDestroyDescriptorSetLayout(g.device, g.shadowDsLayout, nullptr);
    if (g.shadowFb)         vkDestroyFramebuffer(g.device, g.shadowFb, nullptr);
    if (g.shadowPass)       vkDestroyRenderPass(g.device, g.shadowPass, nullptr);
    if (g.shadowSampler)    vkDestroySampler(g.device, g.shadowSampler, nullptr);
    if (g.shadowView)       vkDestroyImageView(g.device, g.shadowView, nullptr);
    if (g.shadowImage)      vkDestroyImage(g.device, g.shadowImage, nullptr);
    if (g.shadowMemory)     vkFreeMemory(g.device, g.shadowMemory, nullptr);
    if (g.shadowUbo)        vkDestroyBuffer(g.device, g.shadowUbo, nullptr);
    if (g.shadowUboMemory)  vkFreeMemory(g.device, g.shadowUboMemory, nullptr);
    if (g.scenePass)      vkDestroyRenderPass(g.device, g.scenePass, nullptr);
    if (g.rtOverlayPass)  vkDestroyRenderPass(g.device, g.rtOverlayPass, nullptr);
    if (g.renderPass)     vkDestroyRenderPass(g.device, g.renderPass, nullptr);
    if (g.inFlight)       vkDestroyFence(g.device, g.inFlight, nullptr);
    if (g.gpuTimerPool)   vkDestroyQueryPool(g.device, g.gpuTimerPool, nullptr);   // DOOM-0090
    for (VkSemaphore s : g.renderFinished)
        vkDestroySemaphore(g.device, s, nullptr);
    g.renderFinished.clear();
    if (g.imageAvailable) vkDestroySemaphore(g.device, g.imageAvailable, nullptr);
    if (g.cmdPool)        vkDestroyCommandPool(g.device, g.cmdPool, nullptr);
    if (g.swapchain)      vkDestroySwapchainKHR(g.device, g.swapchain, nullptr);
    if (g.device)         vkDestroyDevice(g.device, nullptr);
    if (g.surface)        vkDestroySurfaceKHR(g.instance, g.surface, nullptr);
    if (g.debug)
    {
        auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            g.instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy)
            destroy(g.instance, g.debug, nullptr);
    }
    vkDestroyInstance(g.instance, nullptr);

    g = VulkanState{};
}
