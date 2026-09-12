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
//	Endianess handling, swapping 16bit and 32bit.
//
//-----------------------------------------------------------------------------


#ifndef __M_SWAP__
#define __M_SWAP__


#ifdef __GNUG__
#pragma interface
#endif


// Endianess handling.
// WAD files are stored little endian.
//
// DOOM-0401: the declarations and the definitions used to be exact
// complements -- declared only under __BIG_ENDIAN__, defined only when it was
// absent -- so a big-endian build could not link, while the false-positive
// ledger cited big-endian as the reason for keeping these macros at all. They
// are now declared and defined unconditionally, so they link and can be tested
// on the little-endian machine that actually builds this.
//
// The types are fixed-width for the same reason. `unsigned long` is 64 bits on
// LP64, so the old 32-bit shift pattern would have swapped the wrong width on
// exactly the platform the code claimed to serve.
#include <stdint.h>

uint16_t	SwapSHORT(uint16_t);
uint32_t	SwapLONG(uint32_t);

#ifdef __BIG_ENDIAN__
#define SHORT(x)	((short)SwapSHORT((uint16_t) (x)))
#define LONG(x)         ((int)SwapLONG((uint32_t) (x)))
#else
// Identity on little-endian, which is what this fork builds for. Untouched:
// the WAD byte order matches the host, so the macro must not change the value
// or its type.
#define SHORT(x)	(x)
#define LONG(x)         (x)
#endif




#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
