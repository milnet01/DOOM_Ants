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
    myargc = argc;
    myargv = argv;

    D_DoomMain ();

    return 0;
} 
