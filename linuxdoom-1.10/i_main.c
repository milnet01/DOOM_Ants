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
//	Main program, simply calls D_DoomMain high level loop.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] __attribute__((used)) = "$Id: i_main.c,v 1.4 1997/02/03 22:45:10 b1 Exp $";



#include <stdio.h>

#include "doomdef.h"

#include "m_argv.h"
#include "d_main.h"

#ifdef _WIN32
// Keep our own int main() as the entry point on Windows rather than letting
// SDL redirect it to WinMain (DOOM-0006). SDL_MAIN_HANDLED suppresses the
// redirect; SDL_SetMainReady() below satisfies SDL's main-was-entered check.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#endif

int
main
( int		argc,
  char**	argv )
{
#ifdef _WIN32
    SDL_SetMainReady ();
#endif
    // DOOM-0348: make the engine's own diagnostics survive to the console.
    // Windows block-buffers both streams into a pipe or file and the exit path
    // never flushes them, so every fprintf(stderr, ...) in the engine produced
    // NOTHING there -- including the messages that say why sound or music is
    // unavailable, which is exactly what a Windows bug report needs to carry.
    // Only I_Error's survived, and only because it fflushes by hand.
    // Unconditional rather than #ifdef _WIN32: this is already the behaviour
    // Linux gives, so it is a no-op there and there is no second path to keep
    // in step. stdout is line-buffered rather than unbuffered because nothing
    // prints per frame (the profilers report once a second), so the syscalls
    // cost nothing and a crash can no longer truncate the log mid-run.
    setvbuf (stderr, NULL, _IONBF, 0);
    setvbuf (stdout, NULL, _IOLBF, 0);

    myargc = argc;
    myargv = argv;

    D_DoomMain ();

    return 0;
} 
