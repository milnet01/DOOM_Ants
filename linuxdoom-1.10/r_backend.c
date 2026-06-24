// Emacs style mode select   -*- C -*-
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
//    Renderer back-end seam (DOOM-0026). Classic is the software renderer;
//    the RB_RT3D / RB_RASTER3D slots are the Vulkan 3D back-end (DOOM-0008),
//    which brings up a device + swapchain and presents through this seam.
//    Classic stays the default and is untouched.
//
//-----------------------------------------------------------------------------

#include <math.h>

#include "r_backend.h"
#include "r_main.h"     // R_RenderPlayerView
#include "i_video.h"    // I_FinishUpdate
#include "m_fixed.h"    // FRACUNIT
#include "r_mesh.h"     // rb_view_t (POD camera across the seam)

// A level is loaded once the BSP segs exist (r_state.h). Used by RB_Init to
// catch up the scene build when a map was loaded before the back-end came up
// (the -warp autostart path: G_InitNew runs P_SetupLevel before D_DoomLoop).
extern int numsegs;

// Selected mode (RB_*). Default Classic — exact parity for anyone who never
// touches the menu option. Persisted by m_misc.c's defaults[] table.
int rendermode = RB_CLASSIC;

//
// Classic back-end — wraps the existing software renderer unchanged. The
// engine already calls R_Init in D_DoomMain, and Classic draws the world
// into screens[0] then presents via the usual I_FinishUpdate, exactly as
// before; the only difference is the call is dispatched through `active`.
//
static boolean Classic_Available(void)              { return true; }
static void    Classic_Init(void)                   { }
static void    Classic_SetResolution(int w, int h)  { (void)w; (void)h; }
static void    Classic_RenderPlayerView(player_t* p){ R_RenderPlayerView(p); }
static void    Classic_Present(void)                { I_FinishUpdate(); }
static void    Classic_Shutdown(void)               { }

//
// Vulkan 3D back-end (DOOM-0008), implemented in r_vulkan.cpp. Its entry points
// are declared here rather than in a header so r_vulkan.cpp stays free of the
// DOOM C headers (which are not C++-clean); the seam struct is assembled from
// them below. RT3D and Raster3D share one implementation — they differ only in
// the capability they require (hardware ray tracing vs. plain Vulkan) and, in
// later increments, the integrator path; this Stage-1 increment presents a
// cleared frame for both, proving the device + swapchain + present loop.
//
extern int  RB_Vulkan_Available(int want_rt);   // want_rt: require RT extensions
extern void RB_Vulkan_Init(void);
extern void RB_Vulkan_SetResolution(int w, int h);
extern void RB_Vulkan_RenderView(const rb_view_t* view);
extern void RB_Vulkan_Present(void);
extern void RB_Vulkan_Shutdown(void);
extern void RB_Vulkan_BuildLevel(void);

static boolean Vulkan_RT_Available(void)     { return RB_Vulkan_Available(1); }
static boolean Vulkan_Raster_Available(void) { return RB_Vulkan_Available(0); }
static void    Vulkan_Init(void)                   { RB_Vulkan_Init(); }
static void    Vulkan_SetResolution(int w, int h)  { RB_Vulkan_SetResolution(w, h); }

// Convert the player's view to a POD camera and hand it across the seam. viewz
// is the eye height (player->viewz); angle_t is a 32-bit binary angle (full
// circle = 2^32), so scale it to radians. mo is always set for a live view.
static void Vulkan_RenderPlayerView(player_t* p)
{
    rb_view_t view;
    view.x     = p->mo->x  / (float)FRACUNIT;
    view.y     = p->mo->y  / (float)FRACUNIT;
    view.z     = p->viewz  / (float)FRACUNIT;
    view.angle = (float)(p->mo->angle * (2.0 * M_PI / 4294967296.0));
    // Muzzle-flash brighten: A_Light1/2 set extralight to 1/2 light-segments while
    // a gun fires (p_pspr.c). The software renderer adds it to the light index
    // (lightnum = (lightlevel >> LIGHTSEGSHIFT) + extralight); one segment is
    // 1<<LIGHTSEGSHIFT = 16 of the 0..255 lightlevel units. Fold the same step
    // into the [0,1] shade so the whole 3D view flickers brighter, as it always did.
    view.extralight = p->extralight * (16.0f / 255.0f);
    RB_Vulkan_RenderView(&view);
}
static void    Vulkan_Present(void)                { RB_Vulkan_Present(); }
static void    Vulkan_Shutdown(void)               { RB_Vulkan_Shutdown(); }
static void    Vulkan_BuildLevel(void)             { RB_Vulkan_BuildLevel(); }

static renderer_backend_t backends[RB_NUMMODES] =
{
    [RB_CLASSIC] =
    {
        "Classic", Classic_Available, Classic_Init, Classic_SetResolution,
        Classic_RenderPlayerView, Classic_Present, Classic_Shutdown, NULL
    },
    [RB_RT3D] =
    {
        "3D (ray traced)", Vulkan_RT_Available, Vulkan_Init, Vulkan_SetResolution,
        Vulkan_RenderPlayerView, Vulkan_Present, Vulkan_Shutdown, Vulkan_BuildLevel
    },
    [RB_RASTER3D] =
    {
        "3D (raster)", Vulkan_Raster_Available, Vulkan_Init, Vulkan_SetResolution,
        Vulkan_RenderPlayerView, Vulkan_Present, Vulkan_Shutdown, Vulkan_BuildLevel
    },
};

// Display names for the menu, including the not-yet-built 3D modes so the
// option can show them as unavailable.
static const char* modeNames[RB_NUMMODES] =
{
    [RB_CLASSIC]  = "Classic",
    [RB_RT3D]     = "3D (ray traced)",
    [RB_RASTER3D] = "3D (raster)",
};

static renderer_backend_t* active = &backends[RB_CLASSIC];

boolean RB_ModeAvailable(rendermode_t mode)
{
    return mode >= 0 && mode < RB_NUMMODES
        && backends[mode].Available
        && backends[mode].Available();
}

const char* RB_ModeName(rendermode_t mode)
{
    if (mode >= 0 && mode < RB_NUMMODES && modeNames[mode])
        return modeNames[mode];
    return "?";
}

void RB_Init(void)
{
    // DOOM-0008: report the 3D tier this machine supports. The Vulkan back-ends
    // are not selectable yet (their Available() is wired in once the renderer
    // draws), so this only logs detection for now — it does not change the
    // clamp below, which keeps the engine on Classic.
    RB_VulkanProbe();

    // Clamp a persisted choice to a back-end that actually exists here.
    // Classic is always available, so this never leaves `active` NULL.
    if (!RB_ModeAvailable(rendermode))
        rendermode = RB_CLASSIC;

    active = &backends[rendermode];
    if (active->Init)
        active->Init();

    // If a map was already loaded before we got here (the -warp autostart path
    // runs P_SetupLevel before D_DoomLoop inits the back-end), build its scene
    // now. The normal new-game path builds via P_SetupLevel's RB_BuildLevel,
    // which by then sees the initialised 3D back-end.
    if (numsegs > 0 && active->BuildLevel)
        active->BuildLevel();
}

void RB_RenderPlayerView(player_t* player)
{
    active->RenderPlayerView(player);
}

void RB_Present(void)
{
    active->Present();
}

void RB_BuildLevel(void)
{
    if (active->BuildLevel)
        active->BuildLevel();
}

void RB_SetMode(rendermode_t mode)
{
    if (!RB_ModeAvailable(mode) || mode == rendermode)
        return;

    if (active->Shutdown)
        active->Shutdown();

    rendermode = mode;
    active = &backends[rendermode];
    if (active->Init)
        active->Init();
}
