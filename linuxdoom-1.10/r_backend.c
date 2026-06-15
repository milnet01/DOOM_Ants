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
//    Renderer back-end seam (DOOM-0026). Only the Classic software renderer
//    is implemented; the 3D back-ends are designed (see the spec) but not
//    built, so they report unavailable and the engine stays on Classic.
//
//-----------------------------------------------------------------------------

#include "r_backend.h"
#include "r_main.h"     // R_RenderPlayerView
#include "i_video.h"    // I_FinishUpdate

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

static renderer_backend_t backends[RB_NUMMODES] =
{
    [RB_CLASSIC] =
    {
        "Classic", Classic_Available, Classic_Init, Classic_SetResolution,
        Classic_RenderPlayerView, Classic_Present, Classic_Shutdown
    },
    // RB_RT3D / RB_RASTER3D: not built yet. Zero-initialised, so Available is
    // NULL and RB_ModeAvailable reports them unselectable (DOOM-0008).
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
    // Clamp a persisted choice to a back-end that actually exists here.
    // Classic is always available, so this never leaves `active` NULL.
    if (!RB_ModeAvailable(rendermode))
        rendermode = RB_CLASSIC;

    active = &backends[rendermode];
    if (active->Init)
        active->Init();
}

void RB_RenderPlayerView(player_t* player)
{
    active->RenderPlayerView(player);
}

void RB_Present(void)
{
    active->Present();
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
