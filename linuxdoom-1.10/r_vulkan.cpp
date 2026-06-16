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

#include <cstdio>
#include <cstring>
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
        return TIER_CLASSIC;
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
    return best;
}
