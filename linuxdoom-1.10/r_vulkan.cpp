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

    VkCommandPool   cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd     = VK_NULL_HANDLE;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence     inFlight       = VK_NULL_HANDLE;

    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;

    rb_mesh_t* levelMesh = nullptr;   // current level's CPU geometry (DOOM-0008)

    // GPU vertex buffer for the level mesh (uploaded at BuildLevel).
    VkBuffer       vbuf       = VK_NULL_HANDLE;
    VkDeviceMemory vbufMemory = VK_NULL_HANDLE;
    uint32_t       vertexCount = 0;

    // column-major MVP from RB_Vulkan_RenderView; identity until the first
    // camera update so a frame drawn before then is well-defined (DOOM-0037).
    float viewProj[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    bool  haveCamera = false;

    static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

    bool ready        = false;
    bool needRecreate = false;
};

VulkanState g;

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

    // Swapchain is all we need for the clear path; RT device extensions are
    // enabled in the increment that first traces rays.
    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci = {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = devExts;

    Check(vkCreateDevice(g.phys, &dci, nullptr, &g.device), "vkCreateDevice");
    vkGetDeviceQueue(g.device, g.queueFamily, 0, &g.queue);
}

void CreateSwapchain()
{
    VkSurfaceCapabilitiesKHR caps = {};
    Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g.phys, g.surface, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    // Surface format: prefer 8-bit BGRA sRGB, else whatever the surface offers.
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g.phys, g.surface, &fn, formats.data());
    VkSurfaceFormatKHR fmt = formats[0];
    for (const VkSurfaceFormatKHR& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
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
    Check(vkCreateSemaphore(g.device, &semci, nullptr, &g.renderFinished), "vkCreateSemaphore");

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

    // Vertex layout: only the attributes this pass reads (position, normal,
    // sector light). The full rb_vertex_t stride is kept so UV/material fields
    // stay available to later increments without re-uploading.
    VkVertexInputBindingDescription bind = {};
    bind.binding = 0;
    bind.stride = sizeof(rb_vertex_t);
    bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(rb_vertex_t, x) };
    attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(rb_vertex_t, nx) };
    attrs[2] = { 2, 0, VK_FORMAT_R32_SFLOAT,       (uint32_t)offsetof(rb_vertex_t, light) };

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
    pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcr.offset = 0;
    pcr.size = 16 * sizeof(float);   // mat4 MVP

    VkPipelineLayoutCreateInfo plci = {};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
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
    CreateSwapchain();   // reuses g.swapchain as oldSwapchain, then replaces it
    CreateImageViews();
    CreateDepthResources();
    CreateFramebuffers();
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
    CreateSwapchain();
    CreateImageViews();
    CreateRenderPass();
    CreateDepthResources();
    CreateFramebuffers();
    CreatePipeline();
    CreateCommandsAndSync();
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

    g.levelMesh = RB_BuildLevelMesh();

    printf("RB_Vulkan_BuildLevel: %d triangles (%d vertices) from the map.\n",
           g.levelMesh->numtris, g.levelMesh->numverts);
    fflush(stdout);

    // (Re)create the vertex buffer sized to this level's mesh.
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
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    Check(vkCreateBuffer(g.device, &bci, nullptr, &g.vbuf), "vkCreateBuffer(vbuf)");

    VkMemoryRequirements req = {};
    vkGetBufferMemoryRequirements(g.device, g.vbuf, &req);
    VkMemoryAllocateInfo mai = {};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Check(vkAllocateMemory(g.device, &mai, nullptr, &g.vbufMemory), "vkAllocateMemory(vbuf)");
    Check(vkBindBufferMemory(g.device, g.vbuf, g.vbufMemory, 0), "vkBindBufferMemory(vbuf)");

    void* mapped = nullptr;
    Check(vkMapMemory(g.device, g.vbufMemory, 0, size, 0, &mapped), "vkMapMemory(vbuf)");
    std::memcpy(mapped, g.levelMesh->verts, (size_t)size);
    vkUnmapMemory(g.device, g.vbufMemory);

    g.vertexCount = (uint32_t)g.levelMesh->numverts;
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

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Check(vkBeginCommandBuffer(g.cmd, &bi), "vkBeginCommandBuffer");

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

    // Draw the world only once we have both geometry and a camera (e.g. not in
    // a pre-level menu); otherwise the clear alone presents.
    if (g.vbuf && g.vertexCount && g.haveCamera)
    {
        VkViewport vpRect = {};
        vpRect.width = (float)g.extent.width;
        vpRect.height = (float)g.extent.height;
        vpRect.maxDepth = 1.0f;
        vkCmdSetViewport(g.cmd, 0, 1, &vpRect);
        VkRect2D scissor = { { 0, 0 }, g.extent };
        vkCmdSetScissor(g.cmd, 0, 1, &scissor);

        vkCmdBindPipeline(g.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
        vkCmdPushConstants(g.cmd, g.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
                           0, 16 * sizeof(float), g.viewProj);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(g.cmd, 0, 1, &g.vbuf, &off);
        vkCmdDraw(g.cmd, g.vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(g.cmd);
    Check(vkEndCommandBuffer(g.cmd), "vkEndCommandBuffer");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &g.imageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &g.cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &g.renderFinished;
    Check(vkQueueSubmit(g.queue, 1, &si, g.inFlight), "vkQueueSubmit");

    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &g.renderFinished;
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

    if (g.vbuf)           vkDestroyBuffer(g.device, g.vbuf, nullptr);
    if (g.vbufMemory)     vkFreeMemory(g.device, g.vbufMemory, nullptr);
    DestroyFramebufferResources();   // framebuffers, depth, swapchain image views
    if (g.pipeline)       vkDestroyPipeline(g.device, g.pipeline, nullptr);
    if (g.pipelineLayout) vkDestroyPipelineLayout(g.device, g.pipelineLayout, nullptr);
    if (g.renderPass)     vkDestroyRenderPass(g.device, g.renderPass, nullptr);
    if (g.inFlight)       vkDestroyFence(g.device, g.inFlight, nullptr);
    if (g.renderFinished) vkDestroySemaphore(g.device, g.renderFinished, nullptr);
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
