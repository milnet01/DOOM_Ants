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
//    Renderer back-end seam (DOOM-0026). Lets the Classic software renderer
//    and a future 3D renderer be selected at runtime, behind one plain-C
//    interface. See docs/specs/DOOM-0026-renderer-backend.md.
//
//-----------------------------------------------------------------------------

#ifndef __R_BACKEND__
#define __R_BACKEND__

#include "doomtype.h"
#include "d_player.h"

typedef enum
{
    RB_CLASSIC,         // the original software renderer
    RB_RT3D,            // future: Vulkan hybrid with hardware ray tracing
    RB_RASTER3D,        // future: Vulkan raster-only (no hardware RT)
    RB_NUMMODES
} rendermode_t;

typedef struct
{
    const char* name;
    boolean     (*Available)(void);             // can this run on this machine?
    void        (*Init)(void);
    void        (*SetResolution)(int w, int h);
    void        (*RenderPlayerView)(player_t* player);  // the world view
    void        (*Present)(void);               // composite UI + world, flip
    void        (*Shutdown)(void);
} renderer_backend_t;

// Selected mode, persisted via m_misc.c's defaults[] table. Holds an RB_*
// value; typed int so it matches default_t.location (an int*).
extern int rendermode;

// Resolve + init the back-end. Clamps `rendermode` to one that is actually
// available (always at least Classic). Call once, after I_InitGraphics.
void RB_Init(void);

// Frame hooks the main loop (D_Display) calls through the active back-end.
void RB_RenderPlayerView(player_t* player);
void RB_Present(void);

// Switch back-end at runtime (the menu). Re-inits; clamps to available.
void RB_SetMode(rendermode_t mode);

// Menu helpers: display name for a mode, and whether it can be selected now.
const char* RB_ModeName(rendermode_t mode);
boolean     RB_ModeAvailable(rendermode_t mode);

#endif
