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
//	Archiving: SaveGame I/O.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] __attribute__((used)) = "$Id: p_tick.c,v 1.4 1997/02/03 16:47:55 b1 Exp $";

#include "i_system.h"
#include <stdint.h>
#include "z_zone.h"
#include "p_local.h"
#include "save_bounds.h"

// State.
#include "doomstat.h"
#include "r_state.h"
#include "r_main.h"	// NUMCOLORMAPS, for the fixedcolormap bound

byte*		save_p;

// One past the last byte of the loaded savegame. G_DoLoadGame sets it from
// M_ReadFile's length, which vanilla discarded; the save path bounds itself
// against save_max below instead.
byte*		save_end;

// One past the last byte the save buffer can hold. G_DoSaveGame sets it from the
// allocation it makes; the load path does not use it.
byte*		save_max;



//
// P_SaveNeed / P_SaveNeedAligned — DOOM-0255.
//
// Every read in the load path goes through one of these first, so the file's own
// extent bounds the cursor rather than the structures the file claims to hold.
// Together they keep the invariant the checks depend on: save_p never moves past
// save_end, because nothing advances it that has not been asked for here.
//
// The two forms exist because vanilla pads some reads and not others, and where
// it pads is part of the on-disk layout. Padding where vanilla did not would
// shift the cursor and make every existing .dsg unreadable.
//
void P_SaveNeed (size_t count, const char* what)
{
    if (!SaveFits ((size_t)(save_end - save_p), 0, count))
	I_Error ("P_UnArchive: savegame ends %d byte(s) before its %s",
		 (int)(count - (size_t)(save_end - save_p)), what);
}

void P_SaveNeedAligned (size_t count, const char* what)
{
    size_t	pad = SavePadBytes (save_p);

    // pad is at most 3 and count is a sizeof, so the sum cannot wrap.
    P_SaveNeed (pad + count, what);
    save_p += pad;
}


//
// P_SaveRoom / P_SaveRoomAligned — DOOM-0374.
//
// The write side's mirror of P_SaveNeed, keeping the same invariant: save_p
// never moves past save_max, because nothing advances it that has not been asked
// for here. Vanilla's only check ran in G_DoSaveGame once the whole archive had
// been written, so it reported an overrun of the Z_Malloc block that had already
// happened — and the damage stays inside mainzone, where testing.md's savegame
// section says ASAN cannot see it.
//
// This is reachable in ordinary play rather than only under attack: the shipped
// IWADs archive well inside SAVEGAMESIZE, but a large modern PWAD's thinkers
// alone run past it. Refusing is the answer rather than growing the buffer,
// because the whole zone heap is a few megabytes and holds the level too.
//
void P_SaveRoom (size_t count, const char* what)
{
    if (!SaveFits ((size_t)(save_max - save_p), 0, count))
	I_Error ("P_Archive: savegame buffer is %d byte(s) short of its %s",
		 (int)(count - (size_t)(save_max - save_p)), what);
}

void P_SaveRoomAligned (size_t count, const char* what)
{
    size_t	pad = SavePadBytes (save_p);

    // pad is at most 3 and count is a sizeof, so the sum cannot wrap.
    P_SaveRoom (pad + count, what);
    save_p += pad;
}


//
// A savegame is untrusted input for the same reason a PWAD is: it is a file the
// engine is handed, not one it wrote this session, and players do trade them.
// Vanilla stored array indices in pointer fields and cast them straight back on
// load, so a truncated or edited .dsg dereferences whatever integer it contains.
// Same posture and same shape as P_WadIndex in p_setup.c: refuse the save rather
// than read past the array.
//
static int P_SaveIndex (int value, int count, const char* what)
{
    if (value < 0 || value >= count)
	I_Error ("P_UnArchive: bad %s index %d (game has %d)", what, value, count);
    return value;
}



//
// P_ArchivePlayers
//
void P_ArchivePlayers (void)
{
    int		i;
    int		j;
    player_t*	dest;
		
    for (i=0 ; i<MAXPLAYERS ; i++)
    {
	if (!playeringame[i])
	    continue;
	
	P_SaveRoomAligned (sizeof(player_t), "players");

	dest = (player_t *)save_p;
	memcpy (dest,&players[i],sizeof(player_t));
	save_p += sizeof(player_t);
	for (j=0 ; j<NUMPSPRITES ; j++)
	{
	    if (dest->psprites[j].state)
	    {
		dest->psprites[j].state 
		    = (state_t *)(dest->psprites[j].state-states);
	    }
	}
    }
}



//
// P_UnArchivePlayers
//
void P_UnArchivePlayers (void)
{
    int		i;
    int		j;
	
    for (i=0 ; i<MAXPLAYERS ; i++)
    {
	if (!playeringame[i])
	    continue;

	P_SaveNeedAligned (sizeof(player_t), "players");

	memcpy (&players[i],save_p, sizeof(player_t));
	save_p += sizeof(player_t);
	
	// will be set when unarc thinker
	players[i].mo = NULL;	
	players[i].message = NULL;
	players[i].attacker = NULL;

	// DOOM-0373: these three index fixed tables, and two of them reach a
	// write. weaponinfo[] is NUMWEAPONS entries and the ammo index it
	// yields is used to decrement player->ammo[NUMAMMO]; fixedcolormap
	// becomes a raw pointer offset into colormaps in R_SetupFrame, read
	// once per rendered pixel. wp_nochange is a legitimate pendingweapon.
	players[i].readyweapon =
	    P_SaveIndex (players[i].readyweapon, NUMWEAPONS, "readyweapon");

	if (players[i].pendingweapon != wp_nochange)
	    players[i].pendingweapon =
		P_SaveIndex (players[i].pendingweapon, NUMWEAPONS,
			     "pendingweapon");

	// 0 means "none" and NUMCOLORMAPS is the inverse map, so both ends
	// of the range are valid.
	players[i].fixedcolormap =
	    P_SaveIndex (players[i].fixedcolormap, NUMCOLORMAPS + 1,
			 "fixedcolormap");

	for (j=0 ; j<NUMPSPRITES ; j++)
	{
	    if (players[i]. psprites[j].state)
	    {
		players[i]. psprites[j].state
		    = &states[ P_SaveIndex ((int)(intptr_t)players[i].psprites[j].state,
					    NUMSTATES, "psprite state") ];
	    }
	}
    }
}


//
// P_WorldBytes
//
// The world block is a flat run of fixed-size shorts whose extent is fully
// determined by the level currently loaded, so both directions measure it up
// front and check once instead of per element — their loops then walk a raw
// short* exactly as vanilla did. Sides are counted the way those loops visit
// them, so a one-sided linedef contributes its 3 shorts and no side record.
//
static size_t P_WorldBytes (void)
{
    int			i;
    int			j;
    line_t*		li;
    size_t		need;

    need = (size_t)numsectors * 7;
    for (i=0, li = lines ; i<numlines ; i++,li++)
    {
	need += 3;
	for (j=0 ; j<2 ; j++)
	    if (li->sidenum[j] != -1)
		need += 5;
    }
    return need * sizeof(short);
}


//
// P_ArchiveWorld
//
void P_ArchiveWorld (void)
{
    int			i;
    int			j;
    sector_t*		sec;
    line_t*		li;
    side_t*		si;
    short*		put;

    P_SaveRoom (P_WorldBytes (), "world state");

    put = (short *)save_p;
    
    // do sectors
    for (i=0, sec = sectors ; i<numsectors ; i++,sec++)
    {
	*put++ = sec->floorheight >> FRACBITS;
	*put++ = sec->ceilingheight >> FRACBITS;
	*put++ = sec->floorpic;
	*put++ = sec->ceilingpic;
	*put++ = sec->lightlevel;
	*put++ = sec->special;		// needed?
	*put++ = sec->tag;		// needed?
    }

    
    // do lines
    for (i=0, li = lines ; i<numlines ; i++,li++)
    {
	*put++ = li->flags;
	*put++ = li->special;
	*put++ = li->tag;
	for (j=0 ; j<2 ; j++)
	{
	    if (li->sidenum[j] == -1)
		continue;
	    
	    si = &sides[li->sidenum[j]];

	    *put++ = si->textureoffset >> FRACBITS;
	    *put++ = si->rowoffset >> FRACBITS;
	    *put++ = si->toptexture;
	    *put++ = si->bottomtexture;
	    *put++ = si->midtexture;	
	}
    }
	
    save_p = (byte *)put;
}



//
// P_UnArchiveWorld
//
void P_UnArchiveWorld (void)
{
    int			i;
    int			j;
    sector_t*		sec;
    line_t*		li;
    side_t*		si;
    short*		get;

    P_SaveNeed (P_WorldBytes (), "world state");

    get = (short *)save_p;

    // do sectors
    for (i=0, sec = sectors ; i<numsectors ; i++,sec++)
    {
	sec->floorheight = *get++ << FRACBITS;
	sec->ceilingheight = *get++ << FRACBITS;
	// DOOM-0373: raw shorts from the file, and r_plane.c indexes
	// flattranslation[] with them unguarded.
	sec->floorpic = P_SaveIndex (*get++, numflats, "sector floorpic");
	sec->ceilingpic = P_SaveIndex (*get++, numflats, "sector ceilingpic");
	sec->lightlevel = *get++;
	sec->special = *get++;		// needed?
	sec->tag = *get++;		// needed?
	sec->specialdata = 0;
	sec->soundtarget = 0;
    }
    
    // do lines
    for (i=0, li = lines ; i<numlines ; i++,li++)
    {
	li->flags = *get++;
	li->special = *get++;
	li->tag = *get++;
	for (j=0 ; j<2 ; j++)
	{
	    if (li->sidenum[j] == -1)
		continue;
	    si = &sides[li->sidenum[j]];
	    si->textureoffset = *get++ << FRACBITS;
	    si->rowoffset = *get++ << FRACBITS;
	    // DOOM-0373: raw shorts from the file, and r_segs.c indexes
	    // texturetranslation[] with them unguarded.
	    si->toptexture =
		P_SaveIndex (*get++, numtextures, "side toptexture");
	    si->bottomtexture =
		P_SaveIndex (*get++, numtextures, "side bottomtexture");
	    si->midtexture =
		P_SaveIndex (*get++, numtextures, "side midtexture");
	}
    }
    save_p = (byte *)get;	
}





//
// Thinkers
//
typedef enum
{
    tc_end,
    tc_mobj

} thinkerclass_t;



//
// P_ArchiveThinkers
//
void P_ArchiveThinkers (void)
{
    thinker_t*		th;
    mobj_t*		mobj;
	
    // save off the current thinkers
    for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
    {
	if (th->function.acp1 == (actionf_p1)P_MobjThinker)
	{
	    P_SaveRoom (1, "thinker tag");
	    *save_p++ = tc_mobj;
	    P_SaveRoomAligned (sizeof(*mobj), "thinker");
	    mobj = (mobj_t *)save_p;
	    memcpy (mobj, th, sizeof(*mobj));
	    save_p += sizeof(*mobj);
	    mobj->state = (state_t *)(mobj->state - states);
	    
	    if (mobj->player)
		mobj->player = (player_t *)((mobj->player-players) + 1);
	    continue;
	}
		
	// I_Error ("P_ArchiveThinkers: Unknown thinker function");
    }

    // add a terminating marker
    P_SaveRoom (1, "thinker end marker");
    *save_p++ = tc_end;	
}



//
// P_UnArchiveThinkers
//
void P_UnArchiveThinkers (void)
{
    byte		tclass;
    thinker_t*		currentthinker;
    thinker_t*		next;
    mobj_t*		mobj;
    
    // remove all the current thinkers
    currentthinker = thinkercap.next;
    while (currentthinker != &thinkercap)
    {
	next = currentthinker->next;
	
	if (currentthinker->function.acp1 == (actionf_p1)P_MobjThinker)
	    P_RemoveMobj ((mobj_t *)currentthinker);
	else
	    Z_Free (currentthinker);

	currentthinker = next;
    }
    P_InitThinkers ();
	
    // read in saved thinkers
    while (1)
    {
	P_SaveNeed (1, "thinker tag");
	tclass = *save_p++;
	switch (tclass)
	{
	  case tc_end:
	    return; 	// end of list

	  case tc_mobj:
	    P_SaveNeedAligned (sizeof(*mobj), "thinker");
	    mobj = Z_Malloc (sizeof(*mobj), PU_LEVEL, NULL);
	    memcpy (mobj, save_p, sizeof(*mobj));
	    save_p += sizeof(*mobj);
	    mobj->state = &states[P_SaveIndex ((int)(intptr_t)mobj->state,
					       NUMSTATES, "mobj state")];
	    mobj->target = NULL;
	    if (mobj->player)
	    {
		// Stored 1-based so that 0 can mean "not a player" in the field
		// above; index the array only after the -1 lands in range.
		mobj->player = &players[P_SaveIndex ((int)(intptr_t)mobj->player - 1,
						     MAXPLAYERS, "mobj player")];
		mobj->player->mo = mobj;
	    }
	    P_SetThingPosition (mobj);
	    mobj->info = &mobjinfo[P_SaveIndex (mobj->type, NUMMOBJTYPES,
						"mobj type")];
	    mobj->floorz = mobj->subsector->sector->floorheight;
	    mobj->ceilingz = mobj->subsector->sector->ceilingheight;
	    mobj->thinker.function.acp1 = (actionf_p1)P_MobjThinker;
	    P_AddThinker (&mobj->thinker);
	    break;
			
	  default:
	    I_Error ("Unknown tclass %i in savegame",tclass);
	}
	
    }

}


//
// P_ArchiveSpecials
//
enum
{
    tc_ceiling,
    tc_door,
    tc_floor,
    tc_plat,
    tc_flash,
    tc_strobe,
    tc_glow,
    tc_endspecials

} specials_e;	



//
// Things to handle:
//
// T_MoveCeiling, (ceiling_t: sector_t * swizzle), - active list
// T_VerticalDoor, (vldoor_t: sector_t * swizzle),
// T_MoveFloor, (floormove_t: sector_t * swizzle),
// T_LightFlash, (lightflash_t: sector_t * swizzle),
// T_StrobeFlash, (strobe_t: sector_t *),
// T_Glow, (glow_t: sector_t *),
// T_PlatRaise, (plat_t: sector_t *), - active list
//
void P_ArchiveSpecials (void)
{
    thinker_t*		th;
    ceiling_t*		ceiling;
    vldoor_t*		door;
    floormove_t*	floor;
    plat_t*		plat;
    lightflash_t*	flash;
    strobe_t*		strobe;
    glow_t*		glow;
    int			i;
	
    // save off the current thinkers
    for (th = thinkercap.next ; th != &thinkercap ; th=th->next)
    {
	if (th->function.acv == (actionf_v)NULL)
	{
	    for (i = 0; i < MAXCEILINGS;i++)
		if (activeceilings[i] == (ceiling_t *)th)
		    break;
	    
	    if (i<MAXCEILINGS)
	    {
		P_SaveRoom (1, "special tag");
		*save_p++ = tc_ceiling;
		P_SaveRoomAligned (sizeof(*ceiling), "ceiling");
		ceiling = (ceiling_t *)save_p;
		memcpy (ceiling, th, sizeof(*ceiling));
		save_p += sizeof(*ceiling);
		ceiling->sector = (sector_t *)(ceiling->sector - sectors);
	    }
	    continue;
	}
			
	if (th->function.acp1 == (actionf_p1)T_MoveCeiling)
	{
	    P_SaveRoom (1, "special tag");
	    *save_p++ = tc_ceiling;
	    P_SaveRoomAligned (sizeof(*ceiling), "ceiling");
	    ceiling = (ceiling_t *)save_p;
	    memcpy (ceiling, th, sizeof(*ceiling));
	    save_p += sizeof(*ceiling);
	    ceiling->sector = (sector_t *)(ceiling->sector - sectors);
	    continue;
	}
			
	if (th->function.acp1 == (actionf_p1)T_VerticalDoor)
	{
	    P_SaveRoom (1, "special tag");
	    *save_p++ = tc_door;
	    P_SaveRoomAligned (sizeof(*door), "door");
	    door = (vldoor_t *)save_p;
	    memcpy (door, th, sizeof(*door));
	    save_p += sizeof(*door);
	    door->sector = (sector_t *)(door->sector - sectors);
	    continue;
	}
			
	if (th->function.acp1 == (actionf_p1)T_MoveFloor)
	{
	    P_SaveRoom (1, "special tag");
	    *save_p++ = tc_floor;
	    P_SaveRoomAligned (sizeof(*floor), "floor");
	    floor = (floormove_t *)save_p;
	    memcpy (floor, th, sizeof(*floor));
	    save_p += sizeof(*floor);
	    floor->sector = (sector_t *)(floor->sector - sectors);
	    continue;
	}
			
	if (th->function.acp1 == (actionf_p1)T_PlatRaise)
	{
	    P_SaveRoom (1, "special tag");
	    *save_p++ = tc_plat;
	    P_SaveRoomAligned (sizeof(*plat), "platform");
	    plat = (plat_t *)save_p;
	    memcpy (plat, th, sizeof(*plat));
	    save_p += sizeof(*plat);
	    plat->sector = (sector_t *)(plat->sector - sectors);
	    continue;
	}
			
	if (th->function.acp1 == (actionf_p1)T_LightFlash)
	{
	    P_SaveRoom (1, "special tag");
	    *save_p++ = tc_flash;
	    P_SaveRoomAligned (sizeof(*flash), "light flash");
	    flash = (lightflash_t *)save_p;
	    memcpy (flash, th, sizeof(*flash));
	    save_p += sizeof(*flash);
	    flash->sector = (sector_t *)(flash->sector - sectors);
	    continue;
	}
			
	if (th->function.acp1 == (actionf_p1)T_StrobeFlash)
	{
	    P_SaveRoom (1, "special tag");
	    *save_p++ = tc_strobe;
	    P_SaveRoomAligned (sizeof(*strobe), "strobe");
	    strobe = (strobe_t *)save_p;
	    memcpy (strobe, th, sizeof(*strobe));
	    save_p += sizeof(*strobe);
	    strobe->sector = (sector_t *)(strobe->sector - sectors);
	    continue;
	}
			
	if (th->function.acp1 == (actionf_p1)T_Glow)
	{
	    P_SaveRoom (1, "special tag");
	    *save_p++ = tc_glow;
	    P_SaveRoomAligned (sizeof(*glow), "glow");
	    glow = (glow_t *)save_p;
	    memcpy (glow, th, sizeof(*glow));
	    save_p += sizeof(*glow);
	    glow->sector = (sector_t *)(glow->sector - sectors);
	    continue;
	}
    }
	
    // add a terminating marker
    P_SaveRoom (1, "special end marker");
    *save_p++ = tc_endspecials;	

}


//
// P_UnArchiveSpecials
//
//
// P_SectorFromSave
// DOOM-0254: a savegame stores each special's sector as a bare index and the
// loader turns it straight back into a pointer. The file is user data (and may
// have been written by a different level), so range-check before dereferencing.
//
static sector_t* P_SectorFromSave (const void* stored)
{
    int	index = (int)(intptr_t)stored;

    if (index < 0 || index >= numsectors)
	I_Error ("P_UnArchiveSpecials: sector index %d out of range (level has "
		 "%d)", index, numsectors);
    return &sectors[index];
}


void P_UnArchiveSpecials (void)
{
    byte		tclass;
    ceiling_t*		ceiling;
    vldoor_t*		door;
    floormove_t*	floor;
    plat_t*		plat;
    lightflash_t*	flash;
    strobe_t*		strobe;
    glow_t*		glow;
	
	
    // read in saved thinkers
    while (1)
    {
	P_SaveNeed (1, "special tag");
	tclass = *save_p++;
	switch (tclass)
	{
	  case tc_endspecials:
	    return;	// end of list

	  case tc_ceiling:
	    P_SaveNeedAligned (sizeof(*ceiling), "ceiling");
	    ceiling = Z_Malloc (sizeof(*ceiling), PU_LEVEL, NULL);
	    memcpy (ceiling, save_p, sizeof(*ceiling));
	    save_p += sizeof(*ceiling);
	    ceiling->sector = P_SectorFromSave (ceiling->sector);
	    ceiling->sector->specialdata = ceiling;

	    if (ceiling->thinker.function.acp1)
		ceiling->thinker.function.acp1 = (actionf_p1)T_MoveCeiling;

	    P_AddThinker (&ceiling->thinker);
	    P_AddActiveCeiling(ceiling);
	    break;
				
	  case tc_door:
	    P_SaveNeedAligned (sizeof(*door), "door");
	    door = Z_Malloc (sizeof(*door), PU_LEVEL, NULL);
	    memcpy (door, save_p, sizeof(*door));
	    save_p += sizeof(*door);
	    door->sector = P_SectorFromSave (door->sector);
	    door->sector->specialdata = door;
	    door->thinker.function.acp1 = (actionf_p1)T_VerticalDoor;
	    P_AddThinker (&door->thinker);
	    break;
				
	  case tc_floor:
	    P_SaveNeedAligned (sizeof(*floor), "floor");
	    floor = Z_Malloc (sizeof(*floor), PU_LEVEL, NULL);
	    memcpy (floor, save_p, sizeof(*floor));
	    save_p += sizeof(*floor);
	    floor->sector = P_SectorFromSave (floor->sector);
	    floor->sector->specialdata = floor;
	    floor->thinker.function.acp1 = (actionf_p1)T_MoveFloor;
	    P_AddThinker (&floor->thinker);
	    break;
				
	  case tc_plat:
	    P_SaveNeedAligned (sizeof(*plat), "platform");
	    plat = Z_Malloc (sizeof(*plat), PU_LEVEL, NULL);
	    memcpy (plat, save_p, sizeof(*plat));
	    save_p += sizeof(*plat);
	    plat->sector = P_SectorFromSave (plat->sector);
	    plat->sector->specialdata = plat;

	    if (plat->thinker.function.acp1)
		plat->thinker.function.acp1 = (actionf_p1)T_PlatRaise;

	    P_AddThinker (&plat->thinker);
	    P_AddActivePlat(plat);
	    break;
				
	  case tc_flash:
	    P_SaveNeedAligned (sizeof(*flash), "light flash");
	    flash = Z_Malloc (sizeof(*flash), PU_LEVEL, NULL);
	    memcpy (flash, save_p, sizeof(*flash));
	    save_p += sizeof(*flash);
	    flash->sector = P_SectorFromSave (flash->sector);
	    flash->thinker.function.acp1 = (actionf_p1)T_LightFlash;
	    P_AddThinker (&flash->thinker);
	    break;
				
	  case tc_strobe:
	    P_SaveNeedAligned (sizeof(*strobe), "strobe");
	    strobe = Z_Malloc (sizeof(*strobe), PU_LEVEL, NULL);
	    memcpy (strobe, save_p, sizeof(*strobe));
	    save_p += sizeof(*strobe);
	    strobe->sector = P_SectorFromSave (strobe->sector);
	    strobe->thinker.function.acp1 = (actionf_p1)T_StrobeFlash;
	    P_AddThinker (&strobe->thinker);
	    break;
				
	  case tc_glow:
	    P_SaveNeedAligned (sizeof(*glow), "glow");
	    glow = Z_Malloc (sizeof(*glow), PU_LEVEL, NULL);
	    memcpy (glow, save_p, sizeof(*glow));
	    save_p += sizeof(*glow);
	    glow->sector = P_SectorFromSave (glow->sector);
	    glow->thinker.function.acp1 = (actionf_p1)T_Glow;
	    P_AddThinker (&glow->thinker);
	    break;
				
	  default:
	    I_Error ("P_UnarchiveSpecials:Unknown tclass %i "
		     "in savegame",tclass);
	}
	
    }

}

