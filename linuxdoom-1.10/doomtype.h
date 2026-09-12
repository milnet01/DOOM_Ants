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
//	Simple basic typedefs, isolated here to make it easier
//	 separating modules.
//    
//-----------------------------------------------------------------------------


#ifndef __DOOMTYPE__
#define __DOOMTYPE__


#ifndef __BYTEBOOL__
#define __BYTEBOOL__
// Fixed to use builtin bool type with C++.
#ifdef __cplusplus
typedef bool boolean;
#else
typedef enum {false, true} boolean;
#endif
typedef unsigned char byte;
#endif


// Predefined with some OS.
// glibc supplies MAXINT/MININT/... via <values.h>; mingw (Windows) has no
// such header (DOOM-0006). On Windows the same names come from <winnt.h> /
// <basetsd.h> whenever a source file pulls in <windows.h>, with identical
// values, so each definition below is #ifndef-guarded: we only supply one when
// the platform has not already, avoiding "macro redefined" cross-build warnings.
#if defined(LINUX) && !defined(_WIN32)
#include <values.h>
#else
#ifndef MAXCHAR
#define MAXCHAR		((char)0x7f)
#endif
#ifndef MAXSHORT
#define MAXSHORT	((short)0x7fff)
#endif
// Max pos 32-bit int.
#ifndef MAXINT
#define MAXINT		((int)0x7fffffff)
#endif
#ifndef MAXLONG
#define MAXLONG		((long)0x7fffffff)
#endif
#ifndef MINCHAR
#define MINCHAR		((char)0x80)
#endif
#ifndef MINSHORT
#define MINSHORT	((short)0x8000)
#endif
// Max negative 32-bit integer.
#ifndef MININT
#define MININT		((int)0x80000000)
#endif
#ifndef MINLONG
#define MINLONG		((long)0x80000000)
#endif
#endif


// DOOM-0403: O_BINARY lives here because three translation units wanted it and
// each had grown its own copy -- m_misc.c, w_wad.c, and m_menu.c, which is the
// one that never did and read a savegame in text mode as a result. Same shape
// and same reason as the predefines above: a no-op on Unix, supplied by mingw
// on Windows, so the guard only fills in what the platform has not.
//
// It is not decoration on Windows. In text mode the CRT ends a read at the
// first 0x1A byte, and DOOM's files are all binary -- a WAD, a savegame, a
// demo. A save description is user-entered text, so it can hold one.
#ifndef O_BINARY
#define O_BINARY	0
#endif




#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
