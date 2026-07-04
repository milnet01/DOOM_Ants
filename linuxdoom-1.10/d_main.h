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
// $Log:$
//
// DESCRIPTION:
//	System specific interface stuff.
//
//-----------------------------------------------------------------------------


#ifndef __D_MAIN__
#define __D_MAIN__

#include "d_event.h"

#ifdef __GNUG__
#pragma interface
#endif



#define MAXWADFILES             20
extern char*		wadfiles[MAXWADFILES];

void D_AddFile (char *file);



//
// D_DoomMain()
// Not a globally visible function, just included for source reference,
// calls all startup code, parses command line options.
// If not overrided by user input, calls N_AdvanceDemo.
//
void D_DoomMain (void);

// Called by IO functions when input is detected.
void D_PostEvent (event_t* ev);

	

//
// BASE LEVEL
//
void D_PageTicker (void);
void D_PageDrawer (void);
void D_AdvanceDemo (void);
void D_StartTitle (void);

//
// DOOM-0060 game-select (see docs/specs/DOOM-0060-game-select.md).
// Family codes (IWAD_DOOM1 / IWAD_DOOM2) come from iwad_detect.h.
//
void D_DetectIwads (void);              // scan for installed IWADs (call once at boot)
int  D_BothGamesPresent (void);         // 1 iff a DOOM 1 and a DOOM 2 IWAD were found
const char* D_IwadPathForFamily (int family);   // recorded path for IWAD_DOOM1/DOOM2
void D_RelaunchWithIwad (const char* iwadpath);  // re-exec the engine into another IWAD

#endif
