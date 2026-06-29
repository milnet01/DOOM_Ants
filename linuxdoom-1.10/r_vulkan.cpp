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

// Compiled shaders, embedded as byte arrays (Makefile: GLSL -> SPIR-V -> xxd).
#include "shaders/mesh.vert.spv.h"
#include "shaders/mesh.frag.spv.h"
#include "shaders/overlay.vert.spv.h"
#include "shaders/overlay.frag.spv.h"
#include "shaders/pathtrace.comp.spv.h"
#include "shaders/bake.comp.spv.h"
#include "shaders/svgf_temporal.comp.spv.h"
#include "shaders/svgf_atrous.comp.spv.h"
#include "shaders/svgf_composite.comp.spv.h"
#include "shaders/label.comp.spv.h"
#include "shaders/taau.comp.spv.h"

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

    int best = (ndev > 0) ? TIER_RASTER3D : TIER_CLASSIC;
    char bestName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE] = "";

    for (VkPhysicalDevice d : devs)
    {
        VkPhysicalDeviceProperties props = {};
        vkGetPhysicalDeviceProperties(d, &props);

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

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline       pipeline       = VK_NULL_HANDLE;
    // Same layout/shaders as `pipeline`, but depth test + write disabled, so the
    // sky backdrop paints behind everything and the world overdraws it.
    VkPipeline       skyPipeline    = VK_NULL_HANDLE;
    // Wireframe variant of `pipeline` (polygonMode LINE) for the debug view
    // toggled by rb_wireframe / the gamepad Share button. Only built when the GPU
    // advertises fillModeNonSolid (wireSupported); else the toggle is a no-op.
    VkPipeline       wirePipeline   = VK_NULL_HANDLE;
    bool             wireSupported  = false;
    // 2D HUD/menu compositor (DOOM-0008): a vertexless full-screen pass that
    // draws the paletted screens[0] overlay over the rendered 3D scene, keying
    // out the transparent index. Shares pipelineLayout + descriptor set 0.
    VkPipeline       overlayPipeline = VK_NULL_HANDLE;
    // DOOM-0094: LOAD-variant of renderPass (colour loadOp=LOAD to keep the path-
    // traced blit, depth cleared). Used after RecordRtTrace to draw the weapon
    // viewmodel + the 2D HUD/menu/FPS overlay over the traced view. Format-compatible
    // with renderPass, so it reuses g.framebuffers and the world/overlay pipelines.
    VkRenderPass     rtOverlayPass   = VK_NULL_HANDLE;

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

    // DOOM-0090: opt-in per-pass GPU timer (toggled by rb_profile / the `\` key).
    // A timestamp query pool is sampled at the path tracer's pass boundaries in
    // RecordRtTrace and read back at the top of the next frame. Single-frame-in-
    // flight means that frame is already complete by then, so the read adds no
    // stall. profMs accumulates the four segment costs; printed once a second.
    VkQueryPool gpuTimerPool    = VK_NULL_HANDLE;
    float       timestampPeriod = 0.0f;   // ns per tick (0 = timestamps unusable)
    bool        gpuTimersInUse  = false;  // last frame wrote timestamps
    double      profMs[4]       = { 0, 0, 0, 0 };  // megakernel/denoise/label/blit
    int         profFrames      = 0;
    int         profLastReport  = 0;      // I_GetTimeMS of the last printf

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
    bool           overlayReady = false;

    // column-major MVP from RB_Vulkan_RenderView; identity until the first
    // camera update so a frame drawn before then is well-defined (DOOM-0037).
    float viewProj[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    bool  haveCamera = false;

    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

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
    VkPipeline            rtPipeline   = VK_NULL_HANDLE;

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
extern "C" { int rb_upscaler = 0; int rb_renderscale = 100; int rb_exposure = 10; }

// DOOM-0090: per-pass GPU profiler toggle (the `\` key; persisted as rt_profile).
// When on, the path tracer's per-stage GPU cost is timestamped and printed to the
// terminal once a second. Off by default; RT-only (the raster path never reads it).
extern "C" { int rb_profile = 0; }
extern "C" int I_GetTimeMS(void);   // i_system.c; for the rt_profile once-a-second report

// INV-6 headless self-test latch (DOOM-0009 build step 4d). Set from the
// `-rtverify` command-line parm; the first ready present runs RB_RtVerify (the
// rel-MSE + white-furnace proof) and exits. -1 = unchecked, 0 = off, 1 = armed.
int rb_rtverify = -1;

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
            create(g.instance, &dci, nullptr, &g.debug);
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
    vkEnumeratePhysicalDevices(g.instance, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    if (n)
        vkEnumeratePhysicalDevices(g.instance, &n, devs.data());

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
    // (Graceful probe-time gating so the menu never offers 3D on such a GPU is
    // tracked separately — see ROADMAP.)
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
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;         // always supported; vsync
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = g.swapchain;

    VkSwapchainKHR created = VK_NULL_HANDLE;
    Check(vkCreateSwapchainKHR(g.device, &sci, nullptr, &created),
          "vkCreateSwapchainKHR");

    if (g.swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(g.device, g.swapchain, nullptr);
    g.swapchain = created;

    uint32_t ic = 0;
    vkGetSwapchainImagesKHR(g.device, g.swapchain, &ic, nullptr);
    g.images.resize(ic);
    vkGetSwapchainImagesKHR(g.device, g.swapchain, &ic, g.images.data());
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
    geom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;
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

    // ---- TLAS: instance 0 = the static world BLAS; instance 1 = the per-frame
    // sprite BLAS (added each frame in RecordRtTrace with mask 0x02). Sized for 2,
    // built here with just the world instance (the sprite BLAS has no geometry yet),
    // then rebuilt every traced frame. ----
    static const uint32_t kMaxTlasInstances = 2;
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
    // Instance 1 (sprites) is left zeroed (mask 0 -> never hit) until a frame fills it.

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
    trange.primitiveCount = 1;                  // world only at level-load
    const VkAccelerationStructureBuildRangeInfoKHR* pTrange = &trange;

    cb = BeginOneTime();
    g.pfnCmdBuildAS(cb, 1, &tbgi, &pTrange);
    EndOneTime(cb);
    g.blasDirty = false;        // freshly built from the current heights

    printf("RB_Vulkan: built BLAS (%u tris, %.1f->%.1f KiB compacted) + TLAS (2-instance cap, world live); AS %.1f KiB.\n",
           triCount,
           (double)sizes.accelerationStructureSize / 1024.0,
           (double)blasSize / 1024.0,
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
void RefitAS()
{
    if (g.blas == VK_NULL_HANDLE || g.tlas == VK_NULL_HANDLE || !g.vertexCount)
        return;

    const uint32_t triCount = g.vertexCount / 3;

    // BLAS update: same geometry description as the build, mode UPDATE, src == dst.
    VkAccelerationStructureGeometryKHR geom = {};
    geom.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geom.flags        = VK_GEOMETRY_OPAQUE_BIT_KHR;
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

    VkCommandBuffer cb = BeginOneTime();
    g.pfnCmdBuildAS(cb, 1, &bgi, &pRange);
    EndOneTime(cb);

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

        insts[1].transform.matrix[0][0] = 1.0f;
        insts[1].transform.matrix[1][1] = 1.0f;
        insts[1].transform.matrix[2][2] = 1.0f;
        insts[1].instanceCustomIndex = 1u;        // megakernel: "this hit is a sprite"
        insts[1].mask  = 0x02;                     // primary rays only (shadow/NEE cull to 0x01)
        insts[1].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        insts[1].accelerationStructureReference = g.spriteBlasAddr;
    }
    else
    {
        insts[1].mask = 0u;                        // no sprites this frame
    }

    const uint32_t instCount = haveSpr ? 2u : 1u;

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

    // Push constant: 4x vec4 (camera) + 4x uvec4 (mode/w/h/numWall, emitter+probe
    // counts, verify seed/spp/estimator, DOOM-0100 sprite base + omni-emitter start)
    // + 6x uint64 (vertex / emitter / Le / probe-cache / tri-subsector / sprite-vert
    // addresses) = 176 bytes. MUST match sizeof(RtPushConstants) in RecordRtTrace —
    // a short range silently drops misc4/spriteVerts (the omni-light fix), so the
    // verify struct's 152-byte push is a valid partial of this 176-byte range. Within
    // the 256-byte device limit.
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 176;
    // Three sets: 0 = RT (TLAS + output image), 1 = the raster materials set
    // (g.dsLayout: PLAYPAL LUT + bindless material array), reused verbatim so the
    // textured trace (step 3a) decodes surfaces with no parallel material path,
    // 2 = the SVGF denoiser G-buffer (step 6; mode 6 writes its feed half). The
    // megakernel statically references set 2 (mode 6), so EVERY dispatch of this
    // pipeline must bind all three sets — RecordRtTrace + RB_RtVerify both do.
    // g.dsLayout + g.svgfDsLayout are created before this (Init order).
    VkDescriptorSetLayout setLayouts[3] = { g.rtDsLayout, g.dsLayout, g.svgfDsLayout };
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 3;
    plci.pSetLayouts            = setLayouts;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges    = &pcr;
    Check(vkCreatePipelineLayout(g.device, &plci, nullptr, &g.rtPipeLayout),
          "vkCreatePipelineLayout(rt)");

    VkShaderModule cs = MakeShader(pathtrace_comp_spv, pathtrace_comp_spv_len);
    VkComputePipelineCreateInfo cpci = {};
    cpci.sType        = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = cs;
    cpci.stage.pName  = "main";
    cpci.layout       = g.rtPipeLayout;
    Check(vkCreateComputePipelines(g.device, VK_NULL_HANDLE, 1, &cpci, nullptr, &g.rtPipeline),
          "vkCreateComputePipelines(rt)");
    vkDestroyShaderModule(g.device, cs, nullptr);

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

    // Push constant: uvec4 (probeCount/numWall/emitterCount/giEnabled) + 6 uint64
    // buffer addresses (verts, emit, matEmis, probes, prevProbes, triSs) = 64 bytes.
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 64;
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
        // The output is also a blit source; the histories are storage-only.
        ici.usage         = VK_IMAGE_USAGE_STORAGE_BIT
                          | ((i == TA_OUT) ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
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
}

void CreatePipeline()
{
    VkShaderModule vert = MakeShader(mesh_vert_spv, mesh_vert_spv_len);
    VkShaderModule frag = MakeShader(mesh_frag_spv, mesh_frag_spv_len);

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
    VkPipelineColorBlendStateCreateInfo cb = {};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;

    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynState = {};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dyn;

    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pcr.offset = 0;
    pcr.size = 24 * sizeof(float);   // mat4 MVP + extralight + sky yaw + camera xyz
                                     // + numWall/numFlat (material-id offsets)
                                     // + flashlight on/off (DOOM-0044)

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &g.dsLayout;
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
    pci.pColorBlendState = &cb;
    pci.pDynamicState = &dynState;
    pci.layout = g.pipelineLayout;
    pci.renderPass = g.renderPass;
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

    vkDestroyShaderModule(g.device, vert, nullptr);
    vkDestroyShaderModule(g.device, frag, nullptr);
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
}

void RecreateSwapchain()
{
    vkDeviceWaitIdle(g.device);
    DestroyFramebufferResources();
    if (g.rtEnabled) DestroyRtTargets();   // swapchain-sized; rebuilt below
    CreateSwapchain();   // reuses g.swapchain as oldSwapchain, then replaces it
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();
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

    VkBufferCreateInfo bci = {};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(g.device, &bci, nullptr, &g.spriteVbuf), "vkCreateBuffer(sprites)");

    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(g.device, g.spriteVbuf, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.spriteVbufMemory), "vkAllocateMemory(sprites)");
    Check(vkBindBufferMemory(g.device, g.spriteVbuf, g.spriteVbufMemory, 0), "vkBindBufferMemory(sprites)");
    Check(vkMapMemory(g.device, g.spriteVbufMemory, 0, size, 0, &g.spriteMapped), "vkMapMemory(sprites)");
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

// --- Per-material emission precompute (DOOM-0009 build step 3b, §4.2) ----------
// Tuning constants. INV-7 backfill: these are provisional closed-form thresholds
// authored inline (user-approved 2026-06-27); they migrate to a Vestige Formula
// Workbench `shaders/formulas/*.glsl` artifact when 3c's curves are formalised.
namespace emis {
    // A palette texel counts as "bright" (an emitter contributor) when its linear
    // luminance clears this. ~0.5 = brighter than mid-grey: lamp/screen/switch
    // fullbrights and the brightest slime greens pass; ordinary wall/floor art does
    // not. (§4.2 "palette colours above a threshold".)
    constexpr float kBrightLum = 0.5f;
    // A material becomes an NEE emitter only when its area-weighted emitted
    // luminance clears this — keeps the candidate set to genuine light sources, not
    // a stray bright speck. Below it the (tiny) Le is dropped to zero.
    constexpr float kEmitterMinLum = 0.02f;
    // Global emissive intensity (radiance) scale. INV-7 tunable. DOOM's emissive
    // textures are small, so at palette brightness (1.0) they subtend too little
    // solid angle to light a room — they need a high radiance to read as lights.
    // 40 makes lamps/screens visibly pool light + cast shadows; the shader's
    // per-sample radiance clamp bounds the near-field. (The kEmitterMinLum gate
    // below scales by this too, so the emitter SET is unchanged — only intensity.)
    constexpr float kEmissiveScale = 40.0f;
    // Rec.709 luminance weights (linear RGB).
    constexpr float kLumR = 0.2126f, kLumG = 0.7152f, kLumB = 0.0722f;

    inline float srgb2lin(float c) {
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    inline float luminance(float r, float gg, float b) {
        return kLumR * r + kLumG * gg + kLumB * b;
    }
    // Brightness as VALUE (max channel), not luminance — luminance underweights
    // saturated colours (pure red scores only 0.21), so a red ceiling light would
    // miss the "bright"/emitter thresholds and never glow. Value is hue-agnostic, so
    // a saturated red/blue/green light is rated by its intensity and emits in colour.
    inline float value(float r, float gg, float b) {
        return std::max(r, std::max(gg, b));
    }
}

// Fill out[3*n] with each material's emitted radiance Le (linear RGB), computed
// once from the packed atlas: for every texel of a tile, decode its palette index
// to linear RGB; texels brighter than kBrightLum sum into the tile's emission,
// divided by the tile's total texel count (area-weighted — a small bright lamp on
// a dark plate emits proportionally less than a fully-lit screen). Materials whose
// result is below kEmitterMinLum are zeroed (not light sources). The id ordering
// matches RB_BuildAtlas (walls, then flats, then sprites), so the shader indexes
// Le by the same unified id the textured decode uses.
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

    for (int id = 0; id < n; id++)
    {
        const int ox = (int)a->rects[id].ox, oy = (int)a->rects[id].oy;
        const int w  = (int)a->rects[id].w,  h  = (int)a->rects[id].h;
        const int total = w * h;
        if (total <= 0)
            continue;

        double sr = 0.0, sg = 0.0, sb = 0.0;
        for (int row = 0; row < h; row++)
        {
            const unsigned char* line = a->pixels + (size_t)(oy + row) * a->atlasw + ox;
            for (int col = 0; col < w; col++)
            {
                const float* c = palLin[line[col]];
                if (emis::value(c[0], c[1], c[2]) > emis::kBrightLum)
                {
                    sr += c[0]; sg += c[1]; sb += c[2];
                }
            }
        }

        float le[3] = {
            (float)(sr / total) * emis::kEmissiveScale,
            (float)(sg / total) * emis::kEmissiveScale,
            (float)(sb / total) * emis::kEmissiveScale,
        };
        if (emis::value(le[0], le[1], le[2]) < emis::kEmitterMinLum * emis::kEmissiveScale)
            continue;   // not a light source — leave Le at zero
        out[(size_t)id * 3 + 0] = le[0];
        out[(size_t)id * 3 + 1] = le[1];
        out[(size_t)id * 3 + 2] = le[2];
    }
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
    CreateFramebuffers();
    CreateDescriptors();       // set layout + sampler (needed by the pipeline layout)
    CreatePipeline();
    CreateCommandsAndSync();
    InitPaletteAndDescriptorSet();  // PLAYPAL LUT + descriptor set, so the HUD/menu
                                    // overlay composites from the first frame (DOOM-0045)
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
        CreateOverlayResources(w, h);
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
void FinalizeEmitters(const std::vector<float>* dynEmit, const std::vector<float>* dynWgt)
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
}

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
        litSprites++;
        maxLe = std::max(maxLe, emis::value(lr, lg, lb));
    }
    FinalizeEmitters(&emit, &wgt);

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
    g.emitMapped = nullptr;
    g.emitCount  = 0;
    g.emitCap    = 0;
    g.staticEmit.clear();
    g.staticWgt.clear();

    if (!g.rtEnabled || !g.levelMesh || g.matEmissive.empty())
        return;

    const int matCount = (int)(g.matEmissive.size() / 3);
    const rb_vertex_t* v = g.levelMesh->verts;
    const int numtris = g.levelMesh->numverts / 3;

    // Each emitter record is 14 tight floats: v0[3] v1[3] v2[3] Le[3] cdf pdf.
    // The trailing cdf/pdf are a power-importance sampling table (build step
    // 3c-2): the shader picks an emitter by binary-searching cdf, so a bright/large
    // light is sampled proportionally more often than a dim/small one, then divides
    // by pdf. Weight per emitter = luminance(Le) * triangle area (a radiant-power
    // proxy). The static walls/flats set is cached (cdf/pdf zeroed; FinalizeEmitters
    // builds the CDF over static + per-frame sprites — DOOM-0084).
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

        // Triangle area = 1/2 |(v1-v0) x (v2-v0)|, for the power weight.
        const float ex1 = tri[1].x - tri[0].x, ey1 = tri[1].y - tri[0].y, ez1 = tri[1].z - tri[0].z;
        const float ex2 = tri[2].x - tri[0].x, ey2 = tri[2].y - tri[0].y, ez2 = tri[2].z - tri[0].z;
        const float cx = ey1 * ez2 - ez1 * ey2;
        const float cy = ez1 * ex2 - ex1 * ez2;
        const float cz = ex1 * ey2 - ey1 * ex2;
        const float area = 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
        g.staticWgt.push_back(emis::luminance(le[0], le[1], le[2]) * area);
    }

    // Host-visible, persistently-mapped emitter buffer sized for the static set plus
    // a per-frame budget of emissive-sprite triangles (DOOM-0084). The megakernel
    // reads it by device address; FinalizeEmitters refills it each traced frame.
    g.emitCap = (uint32_t)g.staticWgt.size() + SPR_EMIT_MAX;
    CreateRtBuffer((VkDeviceSize)g.emitCap * 14u * sizeof(float),
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.emitBuf, &g.emitMem);
    Check(vkMapMemory(g.device, g.emitMem, 0, VK_WHOLE_SIZE, 0, &g.emitMapped), "vkMapMemory(emit)");

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
    } bp = {};
    static_assert(sizeof(BakePush) == 64, "bake push-constant layout must match the shader");
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
        uint64_t vertsAddr, emitAddr, matEmisAddr, probeAddr, triSsAddr;
    } pc = {};
    static_assert(sizeof(RtPC) == 152, "verify push-constant layout must match the shader");
    pc.camPos[0] = g.lastView.x; pc.camPos[1] = g.lastView.y; pc.camPos[2] = g.lastView.z;
    pc.camDir[0] = cc;           pc.camDir[1] = ss;
    pc.camRight[0] = ss;         pc.camRight[1] = -cc;        pc.camRight[3] = 1.0f;
    pc.camUp[2]  = 1.0f;         pc.camUp[3] = (float)H / (float)W;
    pc.misc[0] = 5u; pc.misc[1] = W; pc.misc[2] = H; pc.misc[3] = (uint32_t)g.matNumWall;
    pc.misc2[0] = g.emitCount; pc.misc2[1] = g.probeCount;
    pc.vertsAddr   = BufferAddress(g.vbuf);
    pc.emitAddr    = g.emitBuf    ? BufferAddress(g.emitBuf)    : 0;
    pc.matEmisAddr = g.matEmisBuf ? BufferAddress(g.matEmisBuf) : 0;
    pc.probeAddr   = g.probeBuf   ? BufferAddress(g.probeBuf)   : 0;   // unused by mode 5
    pc.triSsAddr   = g.triSsBuf   ? BufferAddress(g.triSsBuf)   : 0;

    VkDescriptorSet sets[3] = { g.rtDs, g.ds, g.svgfDs };

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
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g.rtPipeline);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    g.rtPipeLayout, 0, 3, sets, 0, nullptr);
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
    g.probeCount = 0;

    if (!g.rtEnabled || !g.levelMesh)
        return;

    const int n = RB_NumSubsectors();
    if (n <= 0)
        return;

    std::vector<rb_probe_t> probes(n);
    const int got = RB_BuildProbes(probes.data(), n);
    g.probeCount = (uint32_t)got;

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
    if (g.levelMesh)
    {
        RB_FreeMesh(g.levelMesh);
        g.levelMesh = nullptr;
    }

    // The texture atlas is WAD-global; build + upload it on the first level only.
    if (!g.atlasReady)
        UploadAtlas();

    g.levelMesh = RB_BuildLevelMesh();

    printf("RB_Vulkan_BuildLevel: %d triangles (%d vertices) from the map.\n",
           g.levelMesh->numtris, g.levelMesh->numverts);
    fflush(stdout);

    // (Re)create the vertex buffer sized to this level's mesh.
    if (g.vbufMapped) { vkUnmapMemory(g.device, g.vbufMemory); g.vbufMapped = nullptr; }
    if (g.vbuf)       { vkDestroyBuffer(g.device, g.vbuf, nullptr);  g.vbuf = VK_NULL_HANDLE; }
    if (g.vbufMemory) { vkFreeMemory(g.device, g.vbufMemory, nullptr); g.vbufMemory = VK_NULL_HANDLE; }
    g.vertexCount = 0;

    VkDeviceSize size = (VkDeviceSize)g.levelMesh->numverts * sizeof(rb_vertex_t);
    if (size == 0)
        return;   // empty map (no drawable geometry); nothing to upload

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
    Check(vkCreateBuffer(g.device, &bci, nullptr, &g.vbuf), "vkCreateBuffer(vbuf)");

    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(g.device, g.vbuf, &req);
    VkMemoryAllocateFlagsInfo vbufFlags = {};
    vbufFlags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    vbufFlags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.pNext = g.rtEnabled ? &vbufFlags : nullptr;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.vbufMemory), "vkAllocateMemory(vbuf)");
    Check(vkBindBufferMemory(g.device, g.vbuf, g.vbufMemory, 0), "vkBindBufferMemory(vbuf)");

    // Kept mapped for the whole level: RB_UpdateMeshHeights patches moving-sector
    // z's into it each frame (host-coherent, so no flush). Unmapped on rebuild
    // (above) and shutdown.
    Check(vkMapMemory(g.device, g.vbufMemory, 0, size, 0, &g.vbufMapped), "vkMapMemory(vbuf)");
    std::memcpy(g.vbufMapped, g.levelMesh->verts, (size_t)size);

    g.vertexCount = (uint32_t)g.levelMesh->numverts;

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
        uint64_t vertsAddr;
        uint64_t emitAddr;      // step-3b emitter list (0 if none)
        uint64_t matEmisAddr;   // per-material Le table
        uint64_t probeAddr;     // step-4 GI probe cache (0 if none)
        uint64_t triSsAddr;     // per-triangle subsector id (0 if none)
        uint64_t spriteVertsAddr; // DOOM-0100: per-frame billboard verts (0 if none)
    } pc = {};
    static_assert(sizeof(RtPushConstants) == 176, "RT push-constant layout must match the shader");
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
    pc.vertsAddr   = BufferAddress(g.vbuf);
    pc.emitAddr    = g.emitBuf    ? BufferAddress(g.emitBuf)    : 0;
    pc.matEmisAddr = g.matEmisBuf ? BufferAddress(g.matEmisBuf) : 0;
    pc.probeAddr   = g.probeBuf   ? BufferAddress(g.probeBuf)   : 0;
    pc.triSsAddr   = g.triSsBuf   ? BufferAddress(g.triSsBuf)   : 0;
    pc.spriteVertsAddr = g.sprWorldBuf ? BufferAddress(g.sprWorldBuf) : 0;

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
    // materials as the raster pass (step 3a); set 2 = the SVGF G-buffer (step 6).
    // rtActive gates on g.atlasReady, so the material array (binding 2) is written
    // before this binds it. All three sets are bound (the megakernel statically
    // references set 2 via mode 6, so it must be valid for every dispatch).
    VkDescriptorSet sets[3] = { g.rtDs, g.ds, g.svgfDs };
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.rtPipeline);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            g.rtPipeLayout, 0, 3, sets, 0, nullptr);
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

        // 2) edge-aware a-trous: N iterations, hole step doubling, ping-ponging
        //    atrous[0]<->atrous[1]. Iteration 0 also writes the colour history.
        const int N = 5;
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

        // 3) composite: re-modulate albedo + re-add emission + tonemap -> rtImage.
        svgfBarrier();
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.svgfComposite);
        spc.misc[3]  = ping;                      // final a-trous source index
        spc.matEmis  = g.matEmisBuf ? BufferAddress(g.matEmisBuf) : 0;
        vkCmdPushConstants(g.cmd, g.svgfPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(spc), &spc);
        vkCmdDispatch(g.cmd, gx, gy, 1);

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

    if (prof) vkCmdWriteTimestamp(g.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, g.gpuTimerPool, 3);

    // The image the present path blits: the upscaled TAAU output (display-res) when
    // TAAU ran, else rtImage (the megakernel / composite output, display-res on every
    // non-TAAU path). The label + blit below operate at display resolution on it.
    const VkImage finalImage = taauActive ? g.taImg[TA_OUT] : g.rtImage;

    // On-screen mode label (debug): stamp the active `~` mode's title top-centre into
    // the final image before the blit (the trace path skips the normal HUD, so this
    // is the only on-screen mode proof). Runs for every RT display mode; the
    // compute->compute barrier orders it after the megakernel (modes 1-4) / composite
    // (mode 6) / TAAU (mode 6 upscaled) write, and it only touches the label box.
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

    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            g.pipelineLayout, 0, 1, &g.ds, 0, nullptr);

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
        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
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

    vkCmdEndRenderPass(g.cmd);
}

extern "C" void RB_Vulkan_Present(void)
{
    if (!g.ready)
        return;

    if (g.needRecreate)
    {
        RecreateSwapchain();
        g.needRecreate = false;
    }

    vkWaitForFences(g.device, 1, &g.inFlight, VK_TRUE, UINT64_MAX);

    // DOOM-0090: read back the previous frame's per-pass GPU timestamps (the fence
    // wait above guarantees that frame is complete, so this never stalls), convert
    // ticks -> ms, and print the running averages once a second. RT-only / opt-in.
    if (g.gpuTimersInUse && g.gpuTimerPool)
    {
        uint64_t ts[5] = {};
        if (vkGetQueryPoolResults(g.device, g.gpuTimerPool, 0, 5, sizeof(ts), ts,
                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS)
        {
            const double k = (double)g.timestampPeriod / 1.0e6;   // ticks -> ms
            g.profMs[0] += (double)(ts[1] - ts[0]) * k;   // sprite BLAS/TLAS rebuild
            g.profMs[1] += (double)(ts[2] - ts[1]) * k;   // megakernel trace
            g.profMs[2] += (double)(ts[3] - ts[2]) * k;   // denoiser chain + TAAU
            g.profMs[3] += (double)(ts[4] - ts[3]) * k;   // label + blit + present
            g.profFrames++;
            int now = I_GetTimeMS();
            if (g.profLastReport == 0) g.profLastReport = now;
            if (now - g.profLastReport >= 1000 && g.profFrames > 0)
            {
                const double f = 1.0 / (double)g.profFrames;
                int omni = (int)g.emitCount - (int)g.staticWgt.size();
                printf("[rt_profile] %3d fps | sprites %.2f | megakernel %.2f | "
                       "denoise+taau %.2f | blit %.2f ms | omni %d/%d lights "
                       "(avg/frame, RT GPU only)\n",
                       g.profFrames, g.profMs[0] * f, g.profMs[1] * f,
                       g.profMs[2] * f, g.profMs[3] * f,
                       omni < 0 ? 0 : omni, (int)g.emitCount);
                fflush(stdout);
                g.profMs[0] = g.profMs[1] = g.profMs[2] = g.profMs[3] = 0.0;
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
        g.rtPipeline != VK_NULL_HANDLE && g.haveCamera &&
        g.vbuf != VK_NULL_HANDLE && g.atlasReady)
    {
        rb_rtverify = 0;
        RB_RtVerify();
        exit(0);
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

    // Path-tracer debug view (DOOM-0009 build step 2c): when the rb_rtdebug toggle
    // is on and the GPU has RT with a built TLAS + a camera, the frame is the
    // traced image blitted to the swapchain instead of the raster pass. Gated so
    // the raster (Solid) path is byte-for-byte unaffected (INV-10).
    const bool rtActive = rb_rtdebug && g.rtEnabled && g.tlas != VK_NULL_HANDLE
                       && g.rtPipeline != VK_NULL_HANDLE && g.haveCamera
                       && g.vbuf != VK_NULL_HANDLE && g.atlasReady;

    // Rebuild this frame's billboard sprites into the persistently-mapped buffer.
    // Safe to overwrite now: the fence wait above guarantees the previous frame's
    // draw (which read this buffer) has finished. Host-coherent, so no flush.
    g.spriteVertCount = 0;
    g.skyVertCount    = 0;
    g.sprWorldVertCount = 0;
    if (!rtActive && g.spriteMapped && g.haveCamera && g.atlasReady)
    {
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

        // DOOM-0084: append this frame's emissive sprites (lamps/torches/barrels)
        // to the NEE light list so they pool light onto their surroundings.
        BuildDynamicEmitters();
    }

    // Re-height moving sectors (doors/lifts) in the static level buffer from the
    // live sector heights. Same fence-safe window as the sprites above (the wait
    // guarantees the previous frame's draw finished); host-coherent, no flush.
    // A non-zero return means geometry actually shifted this frame -> latch the BLAS
    // dirty so the trace refits it (build step 5). Latching (rather than refitting
    // here) means a move that finished under the raster path is still caught the
    // first time the trace is shown.
    if (g.levelMesh && g.vbufMapped)
    {
        if (RB_UpdateMeshHeights(g.levelMesh, (rb_vertex_t*)g.vbufMapped))
            g.blasDirty = true;
    }

    // Moving-sector AS refit (build step 5): only when the trace is active and the
    // geometry moved since the last refit. The fence wait above guarantees the
    // previous frame finished, so the in-place BLAS/TLAS update is race-free.
    if (rtActive && g.blasDirty)
    {
        RefitAS();
        g.blasDirty = false;
    }

    // Copy this frame's 2D overlay (screens[0]) into the mapped staging buffer.
    // Same race-safe window as the sprites above: the fence wait guarantees the
    // previous frame's copy (which read this buffer) has finished.
    // DOOM-0094: the 2D overlay (HUD/menu/messages/FPS, all composited from screens[0])
    // now draws in BOTH the raster and the path-traced present paths.
    bool drawOverlay = g.overlayReady && g.overlaySrc;
    if (drawOverlay)
        std::memcpy(g.overlayMapped, g.overlaySrc,
                    (size_t)g.overlayW * g.overlayH);

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

    // Clear to a dark slate (world background) + far depth, then draw the level
    // mesh. The render pass transitions the colour image to PRESENT_SRC for us.
    VkClearValue clears[2] = {};
    clears[0].color = { { 0.05f, 0.06f, 0.09f, 1.0f } };
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rp = {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = g.renderPass;
    rp.framebuffer = g.framebuffers[idx];
    rp.renderArea.extent = g.extent;
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(g.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    // Draw once we have the texture atlas and a camera (e.g. not in a pre-level
    // menu); otherwise the clear alone presents. The static level mesh and the
    // per-frame billboard sprites share the pipeline, descriptor set, and view
    // matrix — sprites just bind a second vertex buffer and draw after the walls.
    if (g.haveCamera && g.atlasReady)
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
        // mat4 MVP, then the muzzle-flash brighten (mesh.vert adds it to every
        // shade) and the view yaw (mesh.frag pans the sky by it). Push constants
        // and descriptor sets are layout-scoped, so they outlive the pipeline
        // binds below — set them once for both the sky and world pipelines.
        float pcData[24];
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
        vkCmdPushConstants(g.cmd, g.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 24 * sizeof(float), pcData);

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
        // Sprites + weapon: same buffer as the sky, but skip its leading verts.
        if (g.spriteVbuf && g.spriteVertCount)
        {
            vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.spriteVbuf, &off);
            vkCmdDraw(g.cmd, g.spriteVertCount, 1, g.skyVertCount, 0);
        }
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

    vkCmdEndRenderPass(g.cmd);
    }   // end of the non-RT (raster) recording branch

    Check(vkEndCommandBuffer(g.cmd), "vkEndCommandBuffer");

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
        if (g.rtPipeline)   vkDestroyPipeline(g.device, g.rtPipeline, nullptr);
        if (g.rtPipeLayout) vkDestroyPipelineLayout(g.device, g.rtPipeLayout, nullptr);
        if (g.rtDsPool)     vkDestroyDescriptorPool(g.device, g.rtDsPool, nullptr);
        if (g.rtDsLayout)   vkDestroyDescriptorSetLayout(g.device, g.rtDsLayout, nullptr);
        // INV-6 verify accumulator + readback (step 4d).
        if (g.rtAccumView)  vkDestroyImageView(g.device, g.rtAccumView, nullptr);
        if (g.rtAccum)      vkDestroyImage(g.device, g.rtAccum, nullptr);
        if (g.rtAccumMem)   vkFreeMemory(g.device, g.rtAccumMem, nullptr);
        if (g.rtReadback)   vkDestroyBuffer(g.device, g.rtReadback, nullptr);
        if (g.rtReadbackMem) vkFreeMemory(g.device, g.rtReadbackMem, nullptr);
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
        if (g.matEmisBuf)   vkDestroyBuffer(g.device, g.matEmisBuf, nullptr);
        if (g.matEmisMem)   vkFreeMemory(g.device, g.matEmisMem, nullptr);
        // GI bake probes + per-triangle subsector map (step 4).
        if (g.probeBuf)     vkDestroyBuffer(g.device, g.probeBuf, nullptr);
        if (g.probeMem)     vkFreeMemory(g.device, g.probeMem, nullptr);
        if (g.probeBuf2)    vkDestroyBuffer(g.device, g.probeBuf2, nullptr);
        if (g.probeMem2)    vkFreeMemory(g.device, g.probeMem2, nullptr);
        if (g.triSsBuf)     vkDestroyBuffer(g.device, g.triSsBuf, nullptr);
        if (g.triSsMem)     vkFreeMemory(g.device, g.triSsMem, nullptr);
    }
    if (g.vbufMapped)       vkUnmapMemory(g.device, g.vbufMemory);
    if (g.vbuf)             vkDestroyBuffer(g.device, g.vbuf, nullptr);
    if (g.vbufMemory)       vkFreeMemory(g.device, g.vbufMemory, nullptr);
    if (g.spriteVbuf)       vkDestroyBuffer(g.device, g.spriteVbuf, nullptr);
    if (g.spriteVbufMemory) vkFreeMemory(g.device, g.spriteVbufMemory, nullptr);

    // Material + palette resources.
    if (g.dsPool)      vkDestroyDescriptorPool(g.device, g.dsPool, nullptr);
    if (g.dsLayout)    vkDestroyDescriptorSetLayout(g.device, g.dsLayout, nullptr);
    if (g.texSampler)  vkDestroySampler(g.device, g.texSampler, nullptr);
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

    DestroyFramebufferResources();   // framebuffers, depth, swapchain image views
    if (g.pipeline)       vkDestroyPipeline(g.device, g.pipeline, nullptr);
    if (g.wirePipeline)   vkDestroyPipeline(g.device, g.wirePipeline, nullptr);
    if (g.skyPipeline)    vkDestroyPipeline(g.device, g.skyPipeline, nullptr);
    if (g.overlayPipeline) vkDestroyPipeline(g.device, g.overlayPipeline, nullptr);
    if (g.pipelineLayout) vkDestroyPipelineLayout(g.device, g.pipelineLayout, nullptr);
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
