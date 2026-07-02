// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// DESCRIPTION:
//	System specific interface stuff.
//
//-----------------------------------------------------------------------------


#ifndef __I_VIDEO__
#define __I_VIDEO__


#include "doomtype.h"

#ifdef __GNUG__
#pragma interface
#endif


// Called by D_DoomMain,
// determines the hardware configuration
// and sets up the video mode
// DOOM-0161: uppercase label of the pad's confirm/menu-select face button
// (A / CROSS / B by controller family), or NULL when no gamepad is connected.
const char* I_ControllerConfirmLabel (void);

void I_InitWidescreen (void);	// DOOM-0147: set runtime SCREENWIDTH before V_Init
void I_SetAspect (void);	// DOOM-0147 Part C: apply 4:3 / fill-stretch present aspect
void I_InitGraphics (void);


void I_ShutdownGraphics(void);

// Takes full 8 bit values.
void I_SetPalette (byte* palette);

void I_UpdateNoBlit (void);
void I_FinishUpdate (void);

// DOOM-0008 3D back-end hooks. I_GetWindow returns the SDL_Window* as an opaque
// pointer (so the DOOM C TUs need not include SDL; the Vulkan back-end casts it
// back). I_ShutdownGraphicsForVulkan tears down the 2D SDL_Renderer path and
// recreates the window with SDL_WINDOW_VULKAN. Called only when a 3D back-end
// is the selected, available mode. I_ReinitGraphicsForClassic is the reverse:
// switching back to Classic rebuilds the 2D window/renderer/texture (no-op when
// the 2D renderer is already live).
void* I_GetWindow (void);
void  I_ShutdownGraphicsForVulkan (void);
void  I_ReinitGraphicsForClassic (void);

// Wait for vertical retrace or pause a bit.
void I_WaitVBL(int count);

void I_ReadScreen (byte* scr);

void I_BeginRead (void);
void I_EndRead (void);



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
