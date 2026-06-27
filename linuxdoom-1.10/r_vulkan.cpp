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
#include <vector>

// POD-only, DOOM-header-free seam: the C geometry builder (r_mesh.c) and the
// per-frame camera (rb_view_t).
#include "r_mesh.h"

// Compiled shaders, embedded as byte arrays (Makefile: GLSL -> SPIR-V -> xxd).
#include "shaders/mesh.vert.spv.h"
#include "shaders/mesh.frag.spv.h"
#include "shaders/overlay.vert.spv.h"
#include "shaders/overlay.frag.spv.h"
#include "shaders/pathtrace.comp.spv.h"

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

namespace {

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
    VkBuffer                   tlasInstBuf = VK_NULL_HANDLE;  // one instance, host-visible
    VkDeviceMemory             tlasInstMem = VK_NULL_HANDLE;

    // VK_KHR_acceleration_structure entry points — not core, so loaded by name once
    // the device is up (LoadRtEntryPoints); null while rtEnabled is false.
    PFN_vkGetAccelerationStructureBuildSizesKHR    pfnGetASBuildSizes = nullptr;
    PFN_vkCreateAccelerationStructureKHR           pfnCreateAS        = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR        pfnCmdBuildAS      = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR pfnGetASAddress    = nullptr;
    PFN_vkDestroyAccelerationStructureKHR          pfnDestroyAS       = nullptr;
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
    VkBuffer       emitBuf    = VK_NULL_HANDLE;     // per-level emitter list
    VkDeviceMemory emitMem    = VK_NULL_HANDLE;
    uint32_t       emitCount  = 0;

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
extern "C" { int rb_rtdebug = 0; }

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
    if (!g.pfnGetASBuildSizes || !g.pfnCreateAS || !g.pfnCmdBuildAS
        || !g.pfnGetASAddress || !g.pfnDestroyAS)
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
}

// Build the static BLAS (every level-mesh triangle) and a one-instance identity
// TLAS over it (DOOM-0009 build step 2b). Runs once per level load after the
// vertex buffer is uploaded; the mesh is a non-indexed triangle list with the
// world position at byte offset 0 of rb_vertex_t. Built non-compacted: a ~2k-tri
// DOOM map is trivially small, so compaction (spec §3, a VRAM win for large WADs)
// is deferred to the step-7 perf pass.
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
    bgi.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
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

    VkCommandBuffer cb = BeginOneTime();
    g.pfnCmdBuildAS(cb, 1, &bgi, &pRange);
    EndOneTime(cb);   // submits + waits: the BLAS is fully built before the TLAS reads it

    vkDestroyBuffer(g.device, scratchBuf, nullptr);
    vkFreeMemory(g.device, scratchMem, nullptr);

    // ---- TLAS: one identity instance of the BLAS ----
    VkAccelerationStructureDeviceAddressInfoKHR adi = {};
    adi.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    adi.accelerationStructure = g.blas;
    const VkDeviceAddress blasAddr = g.pfnGetASAddress(g.device, &adi);

    VkAccelerationStructureInstanceKHR inst = {};
    inst.transform.matrix[0][0] = 1.0f;   // identity 3x4 row-major
    inst.transform.matrix[1][1] = 1.0f;
    inst.transform.matrix[2][2] = 1.0f;
    inst.mask  = 0xFF;
    inst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    inst.accelerationStructureReference = blasAddr;

    CreateRtBuffer(sizeof(inst),
                   VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &g.tlasInstBuf, &g.tlasInstMem);
    void* instMapped = nullptr;
    Check(vkMapMemory(g.device, g.tlasInstMem, 0, sizeof(inst), 0, &instMapped), "vkMapMemory(tlasInst)");
    std::memcpy(instMapped, &inst, sizeof(inst));
    vkUnmapMemory(g.device, g.tlasInstMem);

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

    const uint32_t instCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR tsizes = {};
    tsizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    g.pfnGetASBuildSizes(g.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                         &tbgi, &instCount, &tsizes);

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

    VkBuffer tScratchBuf = VK_NULL_HANDLE; VkDeviceMemory tScratchMem = VK_NULL_HANDLE;
    CreateRtBuffer(tsizes.buildScratchSize + g.scratchAlign,
                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &tScratchBuf, &tScratchMem);

    tbgi.dstAccelerationStructure  = g.tlas;
    tbgi.scratchData.deviceAddress = AlignUp(BufferAddress(tScratchBuf), g.scratchAlign);

    VkAccelerationStructureBuildRangeInfoKHR trange = {};
    trange.primitiveCount = 1;
    const VkAccelerationStructureBuildRangeInfoKHR* pTrange = &trange;

    cb = BeginOneTime();
    g.pfnCmdBuildAS(cb, 1, &tbgi, &pTrange);
    EndOneTime(cb);

    vkDestroyBuffer(g.device, tScratchBuf, nullptr);
    vkFreeMemory(g.device, tScratchMem, nullptr);

    printf("RB_Vulkan: built BLAS (%u tris) + TLAS (1 instance); AS %.1f KiB.\n",
           triCount,
           (double)(sizes.accelerationStructureSize + tsizes.accelerationStructureSize) / 1024.0);
    fflush(stdout);

    // The path-tracer compute descriptor binds this TLAS; re-point it now that
    // the per-level TLAS exists (the storage image half is written at init /
    // swapchain rebuild). No-op until the compute pipeline is up.
    UpdateRtComputeDescriptor();
}

// ---------------------------------------------------------------------------
// Path-tracer compute pass (DOOM-0009 build step 2c)
// ---------------------------------------------------------------------------

// Build the once-per-run compute pipeline: a descriptor set (TLAS + storage
// image), an 88-byte push-constant range (camera basis + mode + vertex-buffer
// address), and the pathtrace.comp megakernel. RT-only; never called without it.
void CreateRtComputePipeline()
{
    VkDescriptorSetLayoutBinding binds[2] = {};
    binds[0].binding         = 0;   // TLAS
    binds[0].descriptorType  = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    binds[0].descriptorCount = 1;
    binds[0].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
    binds[1].binding         = 1;   // output storage image
    binds[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binds[1].descriptorCount = 1;
    binds[1].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dlci = {};
    dlci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 2;
    dlci.pBindings    = binds;
    Check(vkCreateDescriptorSetLayout(g.device, &dlci, nullptr, &g.rtDsLayout),
          "vkCreateDescriptorSetLayout(rt)");

    VkDescriptorPoolSize pools[2] = {};
    pools[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR; pools[0].descriptorCount = 1;
    pools[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;              pools[1].descriptorCount = 1;
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

    // Push constant: 4x vec4 (camera) + 2x uvec4 (mode/w/h/numWall, emitterCount) +
    // 3x uint64 (vertex / emitter-list / Le-table addresses) = 120 bytes (step 3b).
    VkPushConstantRange pcr = {};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset     = 0;
    pcr.size       = 120;
    // Two sets: 0 = RT (TLAS + output image), 1 = the raster materials set
    // (g.dsLayout: PLAYPAL LUT + bindless material array), reused verbatim so the
    // textured trace (step 3a) decodes surfaces with no parallel material path.
    // g.dsLayout is created by CreateDescriptors, which Init runs before this.
    VkDescriptorSetLayout setLayouts[2] = { g.rtDsLayout, g.dsLayout };
    VkPipelineLayoutCreateInfo plci = {};
    plci.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount         = 2;
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
}

// (Re)create the swapchain-sized storage image the compute pass writes and the
// blit reads. R8G8B8A8_UNORM so vkCmdBlitImage matches components by name into
// the B8G8R8A8 swapchain with no red/blue swap. STORAGE (compute) + TRANSFER_SRC
// (blit). RT-only.
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
}

void DestroyRtTargets()
{
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
    pcr.size = 23 * sizeof(float);   // mat4 MVP + extralight + sky yaw + camera xyz
                                     // + numWall/numFlat (material-id offsets)

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
                if (emis::luminance(c[0], c[1], c[2]) > emis::kBrightLum)
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
        if (emis::luminance(le[0], le[1], le[2]) < emis::kEmitterMinLum * emis::kEmissiveScale)
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
        CreateRtComputePipeline();
        CreateRtTargets();
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

// Build this level's NEE emitter list (DOOM-0009 build step 3b): the subset of
// static mesh triangles whose material is emissive (per-material Le from
// ComputeMaterialEmissive). Each record is 12 tight floats — v0[3] v1[3] v2[3]
// Le[3] — uploaded to a device-address buffer the megakernel samples for direct
// lighting (step 3c). Rebuilt per level (the geometry changes); the Le table it
// reads is WAD-global. Positions are the baked (static) heights — emitters on a
// moving sector would lag, acceptable for now. Switch ON-faces are not yet
// emitters: the list is static, while a pressed switch swaps texture at runtime
// (DOOM-0066) — tracking that live swap is the DOOM-0082 follow-up.
void BuildEmitterList()
{
    if (g.emitBuf) { vkDestroyBuffer(g.device, g.emitBuf, nullptr); g.emitBuf = VK_NULL_HANDLE; }
    if (g.emitMem) { vkFreeMemory(g.device, g.emitMem, nullptr);    g.emitMem = VK_NULL_HANDLE; }
    g.emitCount = 0;

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
    // proxy). This cuts NEE variance enough to drop the shadow-ray count ~4x.
    std::vector<float> emit;
    std::vector<float> wgt;
    emit.reserve(64 * 14);
    wgt.reserve(64);
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
            emit.push_back(tri[k].x); emit.push_back(tri[k].y); emit.push_back(tri[k].z);
        }
        emit.push_back(le[0]); emit.push_back(le[1]); emit.push_back(le[2]);
        emit.push_back(0.0f); emit.push_back(0.0f);   // cdf, pdf — filled in below

        // Triangle area = 1/2 |(v1-v0) x (v2-v0)|, for the power weight.
        const float ex1 = tri[1].x - tri[0].x, ey1 = tri[1].y - tri[0].y, ez1 = tri[1].z - tri[0].z;
        const float ex2 = tri[2].x - tri[0].x, ey2 = tri[2].y - tri[0].y, ez2 = tri[2].z - tri[0].z;
        const float cx = ey1 * ez2 - ez1 * ey2;
        const float cy = ez1 * ex2 - ex1 * ez2;
        const float cz = ex1 * ey2 - ey1 * ex2;
        const float area = 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
        wgt.push_back(emis::luminance(le[0], le[1], le[2]) * area);
    }

    g.emitCount = (uint32_t)wgt.size();

    // Normalise the weights into each record's cdf (cumulative upper edge) + pdf
    // (selection probability). Uniform fallback if every weight is zero (all
    // triangles degenerate), so the shader's 1/pdf divide is always finite.
    double total = 0.0;
    for (float w : wgt) total += w;
    double acc = 0.0;
    for (uint32_t i = 0; i < g.emitCount; i++)
    {
        const float pdf = total > 0.0 ? (float)(wgt[i] / total) : 1.0f / (float)g.emitCount;
        acc += pdf;
        emit[(size_t)i * 14 + 12] = total > 0.0 ? (float)acc : (float)(i + 1) / (float)g.emitCount;
        emit[(size_t)i * 14 + 13] = pdf;
    }
    if (g.emitCount)
        emit[(size_t)(g.emitCount - 1) * 14 + 12] = 1.0f;   // exact upper edge for u->1

    if (g.emitCount)
        UploadAddressBuffer(emit.data(), (VkDeviceSize)emit.size() * sizeof(float),
                            &g.emitBuf, &g.emitMem);

    printf("RB_Vulkan: %u emitter triangles for NEE (power-sampled).\n", g.emitCount);
    fflush(stdout);
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
}

// Record the path-tracer debug frame into g.cmd (caller began the buffer):
// dispatch the megakernel into rtImage, then blit it onto the acquired
// swapchain image. Assumes rtActive was checked (RT on, TLAS + camera + pipeline
// present). The submit that follows waits the acquire semaphore at TRANSFER.
void RecordRtTrace(uint32_t idx)
{
    const uint32_t w = g.extent.width, h = g.extent.height;

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
        uint32_t misc2[4];      // emitterCount, reserved, reserved, reserved
        uint64_t vertsAddr;
        uint64_t emitAddr;      // step-3b emitter list (0 if none)
        uint64_t matEmisAddr;   // per-material Le table
    } pc = {};
    static_assert(sizeof(RtPushConstants) == 120, "RT push-constant layout must match the shader");
    pc.camPos[0] = g.lastView.x; pc.camPos[1] = g.lastView.y; pc.camPos[2] = g.lastView.z;
    pc.camDir[0] = c;            pc.camDir[1] = s;            pc.camDir[2] = 0.0f;
    pc.camRight[0] = s;          pc.camRight[1] = -c;         pc.camRight[2] = 0.0f;
    pc.camRight[3] = 1.0f;                                   // tan(45 deg)
    pc.camUp[2] = 1.0f;          pc.camUp[3] = (float)h / (float)w;
    pc.misc[0] = (uint32_t)rb_rtdebug;
    pc.misc[1] = w; pc.misc[2] = h;
    pc.misc[3] = (uint32_t)g.matNumWall;   // flat-id offset for mode-3 textured decode
    pc.misc2[0] = g.emitCount;             // NEE emitter triangle count (step 3b)
    pc.vertsAddr   = BufferAddress(g.vbuf);
    pc.emitAddr    = g.emitBuf    ? BufferAddress(g.emitBuf)    : 0;
    pc.matEmisAddr = g.matEmisBuf ? BufferAddress(g.matEmisBuf) : 0;

    // Set 0 = RT (TLAS + output image); set 1 = the raster materials set (palette
    // LUT + bindless material array), so the textured trace samples the same
    // materials as the raster pass (step 3a). rtActive gates on g.atlasReady, so
    // the material array (binding 2) is written before this binds it.
    VkDescriptorSet sets[2] = { g.rtDs, g.ds };
    vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g.rtPipeline);
    vkCmdBindDescriptorSets(g.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            g.rtPipeLayout, 0, 2, sets, 0, nullptr);
    vkCmdPushConstants(g.cmd, g.rtPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);
    vkCmdDispatch(g.cmd, (w + 7) / 8, (h + 7) / 8, 1);

    // rtImage GENERAL -> TRANSFER_SRC; swapchain UNDEFINED -> TRANSFER_DST.
    VkImageMemoryBarrier toSrc = toGeneral;
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

    // 1:1 blit (matched extents); component-by-name copy, so R8G8B8A8 -> the
    // B8G8R8A8 swapchain carries no red/blue swap.
    VkImageBlit blit = {};
    blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.srcOffsets[1]  = { (int32_t)w, (int32_t)h, 1 };
    blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    blit.dstOffsets[1]  = { (int32_t)w, (int32_t)h, 1 };
    vkCmdBlitImage(g.cmd, g.rtImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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

    // Re-height moving sectors (doors/lifts) in the static level buffer from the
    // live sector heights. Same fence-safe window as the sprites above (the wait
    // guarantees the previous frame's draw finished); host-coherent, no flush.
    if (g.levelMesh && g.vbufMapped)
        RB_UpdateMeshHeights(g.levelMesh, (rb_vertex_t*)g.vbufMapped);

    // Copy this frame's 2D overlay (screens[0]) into the mapped staging buffer.
    // Same race-safe window as the sprites above: the fence wait guarantees the
    // previous frame's copy (which read this buffer) has finished.
    bool drawOverlay = !rtActive && g.overlayReady && g.overlaySrc;
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
    }
    else
    {

    // Upload the overlay staging buffer into its sampled image before the render
    // pass (transfers are illegal inside one). oldLayout UNDEFINED is fine: every
    // texel is overwritten, so the previous frame's contents need not survive.
    if (drawOverlay)
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
        float pcData[23];
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
        vkCmdPushConstants(g.cmd, g.pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 23 * sizeof(float), pcData);

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
        // Direct-lighting buffers (step 3b): per-level emitter list + WAD-global Le table.
        if (g.emitBuf)      vkDestroyBuffer(g.device, g.emitBuf, nullptr);
        if (g.emitMem)      vkFreeMemory(g.device, g.emitMem, nullptr);
        if (g.matEmisBuf)   vkDestroyBuffer(g.device, g.matEmisBuf, nullptr);
        if (g.matEmisMem)   vkFreeMemory(g.device, g.matEmisMem, nullptr);
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
    if (g.renderPass)     vkDestroyRenderPass(g.device, g.renderPass, nullptr);
    if (g.inFlight)       vkDestroyFence(g.device, g.inFlight, nullptr);
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
