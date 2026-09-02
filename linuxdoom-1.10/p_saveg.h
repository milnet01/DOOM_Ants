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
//	Savegame I/O, archiving, persistence.
//
//-----------------------------------------------------------------------------


#ifndef __P_SAVEG__
#define __P_SAVEG__


#ifdef __GNUG__
#pragma interface
#endif

#include <stddef.h>


// Persistent storage/archiving.
// These are the load / save game routines.
void P_ArchivePlayers (void);
void P_UnArchivePlayers (void);
void P_ArchiveWorld (void);
void P_UnArchiveWorld (void);
void P_ArchiveThinkers (void);
void P_UnArchiveThinkers (void);
void P_ArchiveSpecials (void);
void P_UnArchiveSpecials (void);

extern byte*		save_p; 

// DOOM-0255: the loaded file's extent, and the bound check every read in the
// load path passes through first. G_DoLoadGame owns save_end (it is the only
// place the length is known) and uses these for the fixed-size header it reads
// before handing over to the P_UnArchive* functions above.
extern byte*		save_end;

void P_SaveNeed (size_t count, const char* what);
void P_SaveNeedAligned (size_t count, const char* what);

// DOOM-0374: the same pair for the write side. G_DoSaveGame owns save_max (it
// makes the allocation) and uses these for the fixed-size header and the
// trailing marker it writes around the P_Archive* functions above.
extern byte*		save_max;

void P_SaveRoom (size_t count, const char* what);
void P_SaveRoomAligned (size_t count, const char* what);


#endif
//-----------------------------------------------------------------------------
//
// $Log:$
//
//-----------------------------------------------------------------------------
