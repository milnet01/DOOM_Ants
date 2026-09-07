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
//	Handles WAD file header, directory, lump I/O.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] __attribute__((used)) = "$Id: w_wad.c,v 1.5 1997/02/03 16:47:57 b1 Exp $";


#ifdef NORMALUNIX
#include <ctype.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifndef O_BINARY		// mingw already defines O_BINARY (0x8000); only the
#define O_BINARY		0	// Unix builds need the no-op fallback (DOOM-0006)
#endif
#endif

#ifdef _WIN32
#include <io.h>		// mingw's filelength(); strupr() is in <string.h>
#endif

#include "doomtype.h"
#include "m_swap.h"
#include "i_system.h"
#include "z_zone.h"

#ifdef __GNUG__
#pragma implementation "w_wad.h"
#endif
#include "wad_bounds.h"
#include "level_bounds.h"
#include "w_wad.h"






//
// GLOBALS
//

// Location of each lump on disk.
lumpinfo_t*		lumpinfo;		
int			numlumps;

void**			lumpcache;


#define strcmpi	strcasecmp

// mingw already provides strupr() and filelength() (the latter via <io.h>),
// so define our own only off-Windows to avoid the clash (DOOM-0006).
#ifndef _WIN32
void strupr (char* s)
{
    while (*s) { *s = toupper(*s); s++; }
}

// DOOM-0384: st_size is 64-bit and the int return truncated it, so a file over
// 2 GiB reported a length two orders of magnitude wrong and every bound computed
// from it was wrong with it. long matches the type mingw's own filelength()
// returns, so the declaration agrees on both platforms; it is 64-bit here and
// still 32-bit on Windows, where the shim is mingw's rather than ours.
long filelength (int handle)
{
    struct stat	fileinfo;

    if (fstat (handle,&fileinfo) == -1)
	I_Error ("Error fstating");

    return (long)fileinfo.st_size;
}
#endif


//
// W_CheckLumpExtent — DOOM-0384.
//
// DOOM-0093 bounded the directory's EXTENT and stopped there; every filepos and
// size inside it was stored raw, and security.md is explicit that a self-declared
// size is never trusted without bounding it against the real buffer. A lump
// claiming a huge size reaches W_CacheLumpNum's Z_Malloc and aborts the game on
// any downloaded PWAD; a negative position makes W_ReadLump's lseek fail
// silently, after which read() takes its bytes from wherever the descriptor
// already happened to be.
//
// A zero size stays legal: marker lumps (MAP01, S_START, F_END) are empty by
// design and every real WAD is full of them.
//
static void
W_CheckLumpExtent
( long		pos,
  long		size,
  long		filelen,
  const char*	what,
  const char*	filename,
  int		lump )
{
    if (!WadLumpFits (pos, size, filelen))
	I_Error ("%s: %s lump %d runs outside the file "
		 "(offset %ld, size %ld, file is %ld bytes)",
		 what, filename, lump, pos, size, filelen);
}


void
ExtractFileBase
( char*		path,
  char*		dest )
{
    char*	src;
    int		length;

    src = path + strlen(path) - 1;
    
    // back up until a \ or the start
    while (src != path
	   && *(src-1) != '\\'
	   && *(src-1) != '/')
    {
	src--;
    }
    
    // copy up to eight characters
    memset (dest,0,8);
    length = 0;
    
    while (*src && *src != '.')
    {
	if (++length == 9)
	    I_Error ("Filename base of %s >8 chars",path);

	*dest++ = toupper((int)*src++);
    }
}





//
// LUMP BASED ROUTINES.
//

//
// W_AddFile
// All files are optional, but at least one file must be
//  found (PWAD, if all required lumps are present).
// Files with a .wad extension are wadlink files
//  with multiple lumps.
// Other files are single lumps with the base filename
//  for the lump name.
//
// If filename starts with a tilde, the file is handled
//  specially to allow map reloads.
// But: the reload feature is a fragile hack...

int			reloadlump;
char*			reloadname;
// DOOM-0400: how many lumps the reload file contributed at startup. lumpinfo and
// lumpcache were sized for exactly that many, and W_Reload re-reads the count
// from the file every level load -- so without this the file growing between the
// two writes past both allocations.
static int		reloadcount;


void W_AddFile (char *filename)
{
    wadinfo_t		header;
    lumpinfo_t*		lump_p;
    unsigned		i;
    int			handle;
    int			length;
    int			startlump;
    filelump_t*		fileinfo;
    filelump_t*		fileinfo_heap = NULL;	// malloc'd base (WAD branch); freed at end, NULL-safe for the single-lump path
    filelump_t		singleinfo;
    int			storehandle;
    long		filelen;
    
    // open the file and add to directory

    // handle reload indicator.
    if (filename[0] == '~')
    {
	filename++;
	reloadname = filename;
	reloadlump = numlumps;
	reloadcount = 0;		// set below, once the count is known
    }
		
    if ( (handle = open (filename,O_RDONLY | O_BINARY)) == -1)
    {
	printf (" couldn't open %s\n",filename);
	return;
    }

    printf (" adding %s\n",filename);
    startlump = numlumps;

    // DOOM-0384: measured once and reused, so the directory check, the
    // single-lump path and the per-lump bounds below all agree on one number.
    filelen = filelength (handle);

    // DOOM-0400: filename+strlen(filename)-3 reads BEFORE the buffer for a name
    // shorter than three characters, which "-file x" supplies.
    if (strlen(filename) < 3 || strcmpi (filename+strlen(filename)-3 , "wad" ) )
    {
	// single lump file
	fileinfo = &singleinfo;
	singleinfo.filepos = 0;
	// filelump_t's size field is 32-bit, so a larger file would be stored
	// truncated and every later read of it would be short.
	if (filelen > (long)MAXINT)
	    I_Error ("W_AddFile: %s is too large to add as a single lump "
		     "(%ld bytes)", filename, filelen);
	singleinfo.size = LONG((int)filelen);
	ExtractFileBase (filename, singleinfo.name);
	numlumps++;
    }
    else 
    {
	// WAD file
	// DOOM-0384: vanilla discarded this return, so a file shorter than the
	// header left it uninitialised and the strncmp below read stack garbage.
	if (read (handle, &header, sizeof(header)) != (int)sizeof(header))
	    I_Error ("W_AddFile: %s is too short to hold a WAD header", filename);
	if (strncmp(header.identification,"IWAD",4))
	{
	    // Homebrew levels?
	    if (strncmp(header.identification,"PWAD",4))
	    {
		I_Error ("Wad file %s doesn't have IWAD "
			 "or PWAD id\n", filename);
	    }
	    
	    // ???modifiedgame = true;		
	}
	header.numlumps = LONG(header.numlumps);
	header.infotableofs = LONG(header.infotableofs);
	// DOOM-0093: validate the directory against the real file size before trusting
	// numlumps/infotableofs -- a crafted numlumps overflows the int `length`
	// (numlumps*16) and the fill loop reads an OOB / uninitialised directory.
	{
	    if (header.numlumps < 0
		|| (long)header.numlumps > filelen / (long)sizeof(filelump_t)
		|| header.infotableofs < 0
		|| (long)header.infotableofs
		   + (long)header.numlumps * (long)sizeof(filelump_t) > filelen)
		I_Error ("W_AddFile: %s has a corrupt lump directory", filename);
	}
	length = header.numlumps*sizeof(filelump_t);
	// alloca() is obsolete and numlumps is attacker-controlled, so a huge
	// request must fail gracefully: use a checked heap allocation.
	fileinfo = fileinfo_heap = malloc (length);
	if (!fileinfo)
	    I_Error ("W_AddFile: couldn't malloc %i bytes for %s",
		     length, filename);
	lseek (handle, header.infotableofs, SEEK_SET);
	if (read (handle, fileinfo, length) != length)
	    I_Error ("W_AddFile: %s has a truncated lump directory", filename);
	numlumps += header.numlumps;
    }

    
    // Fill in lumpinfo
    lumpinfo = realloc (lumpinfo, numlumps*sizeof(lumpinfo_t));

    if (!lumpinfo)
	I_Error ("Couldn't realloc lumpinfo");

    lump_p = &lumpinfo[startlump];
	
    storehandle = reloadname ? -1 : handle;
	
    for (i=startlump ; i<numlumps ; i++,lump_p++, fileinfo++)
    {
	lump_p->handle = storehandle;
	lump_p->position = LONG(fileinfo->filepos);
	lump_p->size = LONG(fileinfo->size);
	// i is the global lump number; report the index within THIS file, which
	// is the only one that means anything to whoever holds the file.
	W_CheckLumpExtent (lump_p->position, lump_p->size, filelen,
			   "W_AddFile", filename, i - startlump);
	strncpy (lump_p->name, fileinfo->name, 8);
    }

    free (fileinfo_heap);	// no-op when NULL (single-lump path)

    // DOOM-0400: only for the file that just registered itself as reloadable --
    // a later -file leaves reloadname pointing at the earlier one.
    if (reloadname == filename)
	reloadcount = numlumps - reloadlump;

    if (reloadname)
	close (handle);
}




//
// W_Reload
// Flushes any of the reloadable lumps in memory
//  and reloads the directory.
//
void W_Reload (void)
{
    wadinfo_t		header;
    int			lumpcount;
    lumpinfo_t*		lump_p;
    unsigned		i;
    int			handle;
    int			length;
    long		filelen;
    filelump_t*		fileinfo;
    filelump_t*		fileinfo_heap;

    if (!reloadname)
	return;
		
    if ( (handle = open (reloadname,O_RDONLY | O_BINARY)) == -1)
	I_Error ("W_Reload: couldn't open %s",reloadname);

    if (read (handle, &header, sizeof(header)) != (int)sizeof(header))
	I_Error ("W_Reload: %s is too short to hold a WAD header", reloadname);
    lumpcount = LONG(header.numlumps);
    header.infotableofs = LONG(header.infotableofs);
    // DOOM-0093: same directory validation as W_AddFile (reload path).
    filelen = filelength(handle);
    {
	if (lumpcount < 0
	    || (long)lumpcount > filelen / (long)sizeof(filelump_t)
	    || header.infotableofs < 0
	    || (long)header.infotableofs
	       + (long)lumpcount * (long)sizeof(filelump_t) > filelen)
	    I_Error ("W_Reload: %s has a corrupt lump directory", reloadname);
    }
    // DOOM-0400: lumpinfo and lumpcache were sized at startup for the lumps this
    // file held THEN, and the loop below indexes both by the count read from the
    // file NOW. Growing the file between the two is a heap overflow in lumpinfo
    // and a Z_Free walk past the end of lumpcache. The arrays cannot grow here,
    // so a changed count is refused rather than clamped -- a clamp would load
    // half a directory and call it a reload.
    if (lumpcount != reloadcount)
	I_Error ("W_Reload: %s now holds %d lump(s), not the %d it had at "
		 "startup -- restart to pick that up",
		 reloadname, lumpcount, reloadcount);

    length = lumpcount*sizeof(filelump_t);
    // alloca() is obsolete; use a checked heap allocation (lumpcount is
    // read straight from the file header, so a huge value must fail safely).
    fileinfo = fileinfo_heap = malloc (length);
    if (!fileinfo)
	I_Error ("W_Reload: couldn't malloc %i bytes", length);
    lseek (handle, header.infotableofs, SEEK_SET);
    if (read (handle, fileinfo, length) != length)
	I_Error ("W_Reload: %s has a truncated lump directory", reloadname);
    
    // Fill in lumpinfo
    lump_p = &lumpinfo[reloadlump];
	
    for (i=reloadlump ;
	 i<reloadlump+lumpcount ;
	 i++,lump_p++, fileinfo++)
    {
	if (lumpcache[i])
	    Z_Free (lumpcache[i]);

	lump_p->position = LONG(fileinfo->filepos);
	lump_p->size = LONG(fileinfo->size);
	W_CheckLumpExtent (lump_p->position, lump_p->size, filelen,
			   "W_Reload", reloadname, (int)(i - reloadlump));
    }

    free (fileinfo_heap);

    close (handle);
}



//
// W_InitMultipleFiles
// Pass a null terminated list of files to use.
// All files are optional, but at least one file
//  must be found.
// Files with a .wad extension are idlink files
//  with multiple lumps.
// Other files are single lumps with the base filename
//  for the lump name.
// Lump names can appear multiple times.
// The name searcher looks backwards, so a later file
//  does override all earlier ones.
//
void W_InitMultipleFiles (char** filenames)
{	
    int		size;
    
    // open all the files, load headers, and count lumps
    numlumps = 0;

    // will be realloced as lumps are added
    lumpinfo = malloc(1);	

    for ( ; *filenames ; filenames++)
	W_AddFile (*filenames);

    if (!numlumps)
	I_Error ("W_InitFiles: no files found");
    
    // set up caching
    // DOOM-0400: numlumps comes from WAD headers and the product is computed in
    // size_t, so it is the assignment to an int that truncates -- a huge lump
    // count would allocate a small array that every W_CacheLumpNum then indexes
    // by the real count. level_bounds.h owns the arithmetic and its own test.
    if (!LevelAllocFits (numlumps, sizeof(*lumpcache)))
	I_Error ("W_InitMultipleFiles: %i lumps is too many to cache", numlumps);

    size = numlumps * sizeof(*lumpcache);
    lumpcache = malloc (size);
    
    if (!lumpcache)
	I_Error ("Couldn't allocate lumpcache");

    memset (lumpcache,0, size);
}




//
// W_InitFile
// Just initialize from a single file.
//
void W_InitFile (char* filename)
{
    char*	names[2];

    names[0] = filename;
    names[1] = NULL;
    W_InitMultipleFiles (names);
}



//
// W_NumLumps
//
int W_NumLumps (void)
{
    return numlumps;
}



//
// W_CheckNumForName
// Returns -1 if name not found.
//

int W_CheckNumForName (char* name)
{
    union {
	char	s[9];
	int	x[2];
	
    } name8;
    
    int		v1;
    int		v2;
    lumpinfo_t*	lump_p;

    // make the name into two integers for easy compares
    strncpy (name8.s,name,8);

    // in case the name was a fill 8 chars
    name8.s[8] = 0;

    // case insensitive
    strupr (name8.s);		

    v1 = name8.x[0];
    v2 = name8.x[1];


    // scan backwards so patch lump files take precedence
    lump_p = lumpinfo + numlumps;

    while (lump_p-- != lumpinfo)
    {
	if ( *(int *)lump_p->name == v1
	     && *(int *)&lump_p->name[4] == v2)
	{
	    return lump_p - lumpinfo;
	}
    }

    // TFB. Not found.
    return -1;
}




//
// W_GetNumForName
// Calls W_CheckNumForName, but bombs out if not found.
//
int W_GetNumForName (char* name)
{
    int	i;

    i = W_CheckNumForName (name);
    
    if (i == -1)
      I_Error ("W_GetNumForName: %s not found!", name);
      
    return i;
}


//
// W_LumpLength
// Returns the buffer size needed to load the given lump.
//
int W_LumpLength (int lump)
{
    if (lump >= numlumps)
	I_Error ("W_LumpLength: %i >= numlumps",lump);

    return lumpinfo[lump].size;
}



//
// W_ReadLump
// Loads the lump into the given buffer,
//  which must be >= W_LumpLength().
//
void
W_ReadLump
( int		lump,
  void*		dest )
{
    int		c;
    lumpinfo_t*	l;
    int		handle;
	
    // DOOM-0400: this is public API, so it validates its own arguments rather
    // than trusting every caller to. Vanilla bounded the top only.
    if (lump < 0 || lump >= numlumps)
	I_Error ("W_ReadLump: lump %i is outside 0..%i",lump,numlumps-1);

    l = lumpinfo+lump;

    // A negative size reaches read() as a huge size_t, which fails with EINVAL
    // and returns -1 -- and `-1 < -1` is false, so the check below reported
    // SUCCESS with dest untouched. Refuse it before the read, not after.
    if (l->size < 0)
	I_Error ("W_ReadLump: lump %i declares a negative size %i",
		 lump, l->size);
	
    // ??? I_BeginRead ();
	
    if (l->handle == -1)
    {
	// reloadable file, so use open / read / close
	if ( (handle = open (reloadname,O_RDONLY | O_BINARY)) == -1)
	    I_Error ("W_ReadLump: couldn't open %s",reloadname);
    }
    else
	handle = l->handle;
		
    lseek (handle, l->position, SEEK_SET);
    c = read (handle, dest, l->size);

    // c is -1 on failure, so compare it as a signed count rather than against
    // the size alone.
    if (c < 0 || c < l->size)
	I_Error ("W_ReadLump: only read %i of %i on lump %i",
		 c,l->size,lump);	

    if (l->handle == -1)
	close (handle);
		
    // ??? I_EndRead ();
}




//
// W_CacheLumpNum
//
void*
W_CacheLumpNum
( int		lump,
  int		tag )
{
    if ((unsigned)lump >= numlumps)
	I_Error ("W_CacheLumpNum: %i >= numlumps",lump);
		
    if (!lumpcache[lump])
    {
	// read the lump in
	
	//printf ("cache miss on lump %i\n",lump);
	Z_Malloc (W_LumpLength (lump), tag, &lumpcache[lump]);
	W_ReadLump (lump, lumpcache[lump]);
    }
    else
    {
	//printf ("cache hit on lump %i\n",lump);
	Z_ChangeTag (lumpcache[lump],tag);
    }
	
    return lumpcache[lump];
}



//
// W_CacheLumpName
//
void*
W_CacheLumpName
( char*		name,
  int		tag )
{
    return W_CacheLumpNum (W_GetNumForName(name), tag);
}


//
// W_Profile
//
int		info[2500][10];
int		profilecount;

void W_Profile (void)
{
    int		i;
    memblock_t*	block;
    void*	ptr;
    char	ch;
    FILE*	f;
    int		j;
    char	name[9];
	
	
    for (i=0 ; i<numlumps ; i++)
    {	
	ptr = lumpcache[i];
	if (!ptr)
	{
	    ch = ' ';
	    continue;
	}
	else
	{
	    block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));
	    if (block->tag < PU_PURGELEVEL)
		ch = 'S';
	    else
		ch = 'P';
	}
	// DOOM-0400: info is int[2500][10] and both indices were unbounded --
	// numlumps comes from the WAD directory and profilecount only grows.
	// The function is dead (its one call site in p_setup.c is commented
	// out), so this is a trap laid for whoever uncomments it rather than a
	// live defect. Bounded here rather than deleted: it is id's code, and
	// the trap is what needed removing.
	if (i < (int)(sizeof(info)/sizeof(info[0]))
	    && profilecount < (int)(sizeof(info[0])/sizeof(info[0][0])))
	    info[i][profilecount] = ch;
    }

    if (profilecount < (int)(sizeof(info[0])/sizeof(info[0][0])))
	profilecount++;
	
    f = fopen ("waddump.txt","w");
    name[8] = 0;

    for (i=0 ; i<numlumps ; i++)
    {
	memcpy (name,lumpinfo[i].name,8);

	for (j=0 ; j<8 ; j++)
	    if (!name[j])
		break;

	for ( ; j<8 ; j++)
	    name[j] = ' ';

	fprintf (f,"%s ",name);

	for (j=0 ; j<profilecount ; j++)
	    fprintf (f,"    %c",info[i][j]);

	fprintf (f,"\n");
    }
    fclose (f);
}


