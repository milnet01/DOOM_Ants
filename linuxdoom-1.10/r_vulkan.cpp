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
//    This first increment implements only the headless capability probe used
//    for tier auto-detection (RT3D / Raster3D / Classic). It needs no window,
//    surface, swapchain, or device — it enumerates physical devices and their
//    extensions to decide which 3D tier the machine can run. The renderer
//    itself (geometry, materials, path tracer) lands in following increments.
//
//-----------------------------------------------------------------------------

#include <vulkan/vulkan.h>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

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
// DOOM-0008 Stage 1 — Vulkan device + swapchain bring-up.
//
// The C++ side of the renderer_backend_t seam (DOOM-0026) for the RB_RT3D /
// RB_RASTER3D slots. It recreates the SDL window as a Vulkan window, owns a
// surface + swapchain, and presents through the seam. This first increment
// presents a *cleared* frame (geometry, materials, and the path tracer land in
// following increments); it proves the window-recreation, device, swapchain,
// and present loop end to end.
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

    VkSwapchainKHR       swapchain = VK_NULL_HANDLE;
    VkFormat             format    = VK_FORMAT_UNDEFINED;
    VkExtent2D           extent    = { 0, 0 };
    std::vector<VkImage> images;

    VkCommandPool   cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd     = VK_NULL_HANDLE;

    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence     inFlight       = VK_NULL_HANDLE;

    VkDebugUtilsMessengerEXT debug = VK_NULL_HANDLE;

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

// Full-subresource colour range for a 2D swapchain image.
VkImageSubresourceRange ColorRange()
{
    VkImageSubresourceRange r = {};
    r.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    r.levelCount = 1;
    r.layerCount = 1;
    return r;
}

void RecreateSwapchain()
{
    vkDeviceWaitIdle(g.device);
    CreateSwapchain();   // reuses g.swapchain as oldSwapchain, then replaces it
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

extern "C" void RB_Vulkan_RenderView(void)
{
    // The world is drawn during Present's clear for now; the G-buffer and path
    // tracer that fill this in arrive in following increments.
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

    VkImageSubresourceRange range = ColorRange();

    // UNDEFINED -> TRANSFER_DST so we can clear it.
    VkImageMemoryBarrier toClear = {};
    toClear.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toClear.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toClear.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toClear.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toClear.image = g.images[idx];
    toClear.subresourceRange = range;
    toClear.srcAccessMask = 0;
    toClear.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(g.cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toClear);

    // Placeholder world: a dark slate clear so it is obvious the 3D back-end is
    // presenting. The path tracer replaces this with the rendered scene.
    VkClearColorValue clear = {};
    clear.float32[0] = 0.05f;
    clear.float32[1] = 0.06f;
    clear.float32[2] = 0.09f;
    clear.float32[3] = 1.0f;
    vkCmdClearColorImage(g.cmd, g.images[idx],
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

    // TRANSFER_DST -> PRESENT_SRC for the presentation engine.
    VkImageMemoryBarrier toPresent = toClear;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(g.cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &toPresent);

    Check(vkEndCommandBuffer(g.cmd), "vkEndCommandBuffer");

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
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
    if (!g.instance)
        return;
    if (g.device)
        vkDeviceWaitIdle(g.device);

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
