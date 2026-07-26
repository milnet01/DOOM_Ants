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
//
//-----------------------------------------------------------------------------

static const char
rcsid[] __attribute__((used)) = "$Id: m_bbox.c,v 1.1 1997/02/03 22:45:10 b1 Exp $";


#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <stdarg.h>
#include <sys/time.h>
#include <time.h>	// clock_gettime / CLOCK_MONOTONIC (I_GetTime)
#include <unistd.h>

#include "doomdef.h"
#include "m_misc.h"
#include "i_video.h"
#include "i_sound.h"

#include "d_net.h"
#include "g_game.h"

#ifdef __GNUG__
#pragma implementation "i_system.h"
#endif
#include "i_system.h"




int	mb_used = 6;


void
I_Tactile
( int	on,
  int	off,
  int	total )
{
  // UNUSED.
  on = off = total = 0;
}

ticcmd_t	emptycmd;
ticcmd_t*	I_BaseTiccmd(void)
{
    return &emptycmd;
}


int  I_GetHeapSize (void)
{
    return mb_used*1024*1024;
}

byte* I_ZoneBase (int*	size)
{
    byte*	base;

    *size = mb_used*1024*1024;
    base = (byte *) malloc (*size);
    if (!base)
	I_Error ("I_ZoneBase: failed to allocate %d bytes for the zone heap",
		 *size);
    return base;
}



//
// I_GetTime
// returns time in 1/70th second tics
//
int  I_GetTime (void)
{
    // DOOM-0254: CLOCK_MONOTONIC, not gettimeofday. Game pacing is driven by
    // the delta between successive calls, so an NTP step or a manual clock
    // change used to jump or stall the tic counter.
    struct timespec	tp;
    int			newtics;
    static time_t	basetime=0;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    if (!basetime)
	basetime = tp.tv_sec;
    newtics = (int)((tp.tv_sec-basetime)*TICRATE
		    + tp.tv_nsec*(long)TICRATE/1000000000L);
    return newtics;
}


//
// I_GetTimeMS
// Wall-clock time in milliseconds. Finer than I_GetTime's 35Hz tics, which is
// too coarse to measure a frame rate that can run well above TICRATE; used by
// the FPS counter's rolling average (DOOM-0046).
//
int  I_GetTimeMS (void)
{
    // Monotonic for the same reason as I_GetTime: this feeds the FPS counter's
    // rolling average, which a backward clock step turned into a negative delta.
    struct timespec	tp;
    static time_t	basetime=0;

    clock_gettime(CLOCK_MONOTONIC, &tp);
    if (!basetime)
	basetime = tp.tv_sec;
    return (int)((tp.tv_sec-basetime)*1000 + tp.tv_nsec/1000000L);
}



//
// I_Init
//
void I_Init (void)
{
    I_InitSound();
    //  I_InitGraphics();
}

//
// I_Quit
//
// Shut everything down cleanly WITHOUT exiting: net, audio, config save, video.
// Factored out of I_Quit (DOOM-0060) so the game-select relaunch can reuse the
// exact same teardown before it re-execs the engine (on Windows, where the old
// process lingers and must release the window + audio device before the child
// grabs them).
void I_QuitTeardown (void)
{
    D_QuitNetGame ();
    I_ShutdownSound();
    I_ShutdownMusic();
    M_SaveDefaults ();
    I_ShutdownGraphics();
}

void I_Quit (void)
{
    I_QuitTeardown ();
    exit(0);
}

void I_WaitVBL(int count)
{
#ifdef SGI
    sginap(1);                                           
#else
#ifdef SUN
    sleep(0);
#else
    usleep (count * (1000000/70) );                                
#endif
#endif
}

void I_BeginRead(void)
{
}

void I_EndRead(void)
{
}

byte*	I_AllocLow(int length)
{
    byte*	mem;
        
    mem = (byte *)malloc (length);
    if (!mem)
	I_Error ("I_AllocLow: failed to allocate %d bytes", length);
    memset (mem,0,length);
    return mem;
}


//
// I_Error
//
extern boolean demorecording;

__attribute__((noreturn)) void I_Error (char *error, ...)
{
    va_list	argptr;

    // Message first.
    va_start (argptr,error);
    fprintf (stderr, "Error: ");
    vfprintf (stderr,error,argptr);
    fprintf (stderr, "\n");
    va_end (argptr);

    fflush( stderr );

    // Shutdown. Here might be other errors.
    if (demorecording)
	G_CheckDemoStatus();

    D_QuitNetGame ();
    I_ShutdownGraphics();
    
    exit(-1);
}
