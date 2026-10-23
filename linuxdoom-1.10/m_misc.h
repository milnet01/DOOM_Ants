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
//
//    
//-----------------------------------------------------------------------------


#ifndef __M_MISC__
#define __M_MISC__


#include <stdio.h>

#include "doomtype.h"
//
// MISC
//



boolean
M_WriteFile
( char const*	name,
  void*		source,
  int		length );

// DOOM-0399: as M_WriteFile, but the destination is replaced in one step rather
// than truncated and rewritten -- so an interrupted write leaves the previous
// file intact instead of destroying it. Use it for anything worth keeping.
boolean
M_WriteFileAtomic
( char const*	name,
  void*		source,
  int		length );

int
M_ReadFile
( char const*	name,
  byte**	buffer );

void M_ScreenShot (void);

void M_LoadDefaults (void);

void M_SaveDefaults (void);

// Every setting as `name<TAB><TAB>value` lines, the config file's own format.
// M_SaveDefaults writes the config with it; F12's shot note (DOOM-0501) too.
void M_WriteDefaults (FILE* f);



#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
