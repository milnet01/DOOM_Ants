// Emacs style mode select   -*- C -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
//    Convert a DOOM in-WAD MUS music lump to a Standard MIDI byte stream.
//    Ported from Chocolate DOOM (GPL v2); see mus2mid.c for credits.
//
//-----------------------------------------------------------------------------

#ifndef __MUS2MID_H__
#define __MUS2MID_H__

// Convert the MUS lump at `mus` to a heap-allocated MIDI byte stream.
//
// The MUS header carries its own length (score start + score length), so no
// separate length argument is needed (matching I_RegisterSong's bare pointer).
//
// On success returns 0, stores a malloc'd MIDI buffer in *mid_out and its
// length in *mid_len (caller frees with free()). On failure returns non-zero
// and leaves *mid_out NULL / *mid_len 0 (the lump is not MUS, or the score is
// malformed).
int mus2mid(const unsigned char *mus, unsigned char **mid_out, int *mid_len);

#endif

//-----------------------------------------------------------------------------
