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
#include <string.h>     // memset (wipe screens[0] on a mode switch)

#include "r_backend.h"
#include "doomdef.h"    // SCREENWIDTH/SCREENHEIGHT
#include "r_main.h"     // R_RenderPlayerView, view-window globals
#include "i_video.h"    // I_FinishUpdate
#include "m_fixed.h"    // FRACUNIT
#include "r_sky.h"      // skytexture (the sky's wall-texture index)
#include "r_mesh.h"     // rb_view_t (POD camera across the seam), RB_OVERLAY_KEY
#include "v_video.h"    // screens[] (the paletted 2D overlay buffer)
#include "st_stuff.h"   // ST_Start (force a full status-bar redraw on switch)

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
static void    Classic_Init(void)                   { I_ReinitGraphicsForClassic(); }
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
extern void RB_Vulkan_SetOverlay(const unsigned char* pixels, int w, int h);
extern void RB_Vulkan_Present(void);
extern void RB_Vulkan_Shutdown(void);
extern void RB_Vulkan_BuildLevel(void);

// Software-renderer automap "seen" marking (r_main.c). The 3D back-ends bypass
// the seg pipeline that sets ML_MAPPED, so the automap needs this run per play
// frame to reveal what the player sees (DOOM-0064).
extern void R_MarkAutomapLines(player_t* player);

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
    // Sky backdrop: hand the sky's wall-texture index across the seam so the
    // 3D back-end can sample it (RB_BuildSky). skytexture is set per level by
    // R_SetupLevel; the C++ side never touches DOOM globals.
    view.skytexnum = skytexture;
    RB_Vulkan_RenderView(&view);

    // Reveal the automap the same way Classic does: the software seg renderer
    // sets ML_MAPPED on every visible linedef (R_StoreWallRange), but the Vulkan
    // path never runs it, so the map stayed blank in Solid/Ultra. Run a cheap
    // mark-only BSP pass each play frame -- frustum + occlusion correct, no
    // drawing -- so what the player can see is flagged seen (DOOM-0064). Only
    // during play: when the automap is open this whole function is skipped
    // (D_Display gates it on !automapactive), matching Classic, which likewise
    // marks nothing new while you stare at the map.
    R_MarkAutomapLines(p);

    // The 3D world goes to Vulkan, so the engine never draws it into screens[0].
    // Clear the view's footprint there to the transparent key (RB_OVERLAY_KEY) so
    // the 2D overlay composited on top (RB_Vulkan_SetOverlay) keys this region out
    // and the rendered 3D scene shows through. The 2D drawers that run after this
    // (HU_Drawer messages, M_Drawer menu, the pause pic) paint opaquely over the
    // key and so appear on top of the 3D view. Mirrors D_Display's gate for this
    // call, so the rect always matches the frame the renderer just produced.
    {
        int x, yy;
        for (yy = 0; yy < viewheight; yy++)
        {
            byte* row = screens[0] + (viewwindowy + yy) * SCREENWIDTH + viewwindowx;
            for (x = 0; x < viewwidth; x++)
                row[x] = RB_OVERLAY_KEY;
        }
    }
}
static void    Vulkan_Present(void)
{
    // Hand the freshly-drawn 2D overlay (status bar / menu / messages) to the
    // back-end, then present; it composites screens[0] over the 3D frame.
    RB_Vulkan_SetOverlay(screens[0], SCREENWIDTH, SCREENHEIGHT);
    RB_Vulkan_Present();
}
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
        "Ultra", Vulkan_RT_Available, Vulkan_Init, Vulkan_SetResolution,
        Vulkan_RenderPlayerView, Vulkan_Present, Vulkan_Shutdown, Vulkan_BuildLevel
    },
    [RB_RASTER3D] =
    {
        "Solid", Vulkan_Raster_Available, Vulkan_Init, Vulkan_SetResolution,
        Vulkan_RenderPlayerView, Vulkan_Present, Vulkan_Shutdown, Vulkan_BuildLevel
    },
};

// Player-facing names for the menu (DOOM-0008). Classic = 1997 software
// renderer; Solid = real 3D with the original flat-shaded look (raster); Ultra
// = 3D with ray-traced lighting. The rendermode_t enum values are frozen for
// config/tier lockstep, so the menu's cycle order lives in cycleOrder[] below.
static const char* modeNames[RB_NUMMODES] =
{
    [RB_CLASSIC]  = "Classic",
    [RB_RT3D]     = "Ultra",
    [RB_RASTER3D] = "Solid",
};

// Menu cycle order: ascending visual fidelity (Classic -> Solid -> Ultra),
// decoupled from the enum's numeric values (RB_RT3D=1, RB_RASTER3D=2).
static const rendermode_t cycleOrder[RB_NUMMODES] =
{
    RB_CLASSIC, RB_RASTER3D, RB_RT3D
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

// Next selectable mode after `cur`, walking cycleOrder[] (Classic -> Solid ->
// Ultra -> ...) and skipping any unavailable on this machine. Returns `cur`
// when nothing else is available, so the menu can say "not yet available".
rendermode_t RB_NextAvailableMode(rendermode_t cur)
{
    int i, j;
    for (i = 0; i < RB_NUMMODES; i++)
        if (cycleOrder[i] == cur)
            break;
    for (j = 1; j <= RB_NUMMODES; j++)
    {
        rendermode_t m = cycleOrder[(i + j) % RB_NUMMODES];
        if (RB_ModeAvailable(m))
            return m;
    }
    return cur;
}

void RB_Init(void)
{
    // DOOM-0008: log the 3D tier this machine supports. The Vulkan back-ends are
    // selectable via the menu (Solid/Ultra) and a persisted "renderer" choice;
    // the clamp below keeps a config that names an unavailable mode on Classic.
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

    // Switching mid-game: the new back-end starts with no scene. Rebuild the
    // current level so the 3D back-ends draw the world immediately instead of a
    // blank view (RB_Init does the same on the -warp autostart path). Classic's
    // BuildLevel is NULL, so this is a no-op when switching to it.
    if (numsegs > 0 && active->BuildLevel)
        active->BuildLevel();

    // screens[0] (the paletted 2D buffer) is shared across back-ends and carries
    // stale pixels across the hand-off -- the 3D compositor's magenta key colour
    // and any half-drawn menu. Wipe it so the next mode starts clean, and force a
    // full status-bar repaint. We deliberately do NOT force setsizeneeded here:
    // recomputing the view size drives the hi-res view-border + visplane path,
    // which has latent DOOM-0027 bugs that crashed Classic on switch (DOOM-0055).
    // The view tables stay valid from startup, so Classic renders without it.
    memset(screens[0], 0, SCREENWIDTH * SCREENHEIGHT);
    ST_Start();             // full status-bar repaint (sets st_firsttime)
}
