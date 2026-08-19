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
//
// $Log:$
//
// DESCRIPTION:
//	Main loop menu stuff.
//	Default Config File.
//	PCX Screenshots.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] __attribute__((used)) = "$Id: m_misc.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>	// PATH_MAX (M_SaveDefaults' temp-file path)
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN	// exclude <rpcndr.h>, which typedefs `boolean`
				// (unsigned char) and clashes with doomtype.h's
				// `typedef enum {false,true} boolean`
#include <windows.h>		// MoveFileExA (DOOM-0353 config replace)
#endif


#include "doomdef.h"

#include "z_zone.h"

#include "m_swap.h"
#include "m_argv.h"

#include "w_wad.h"

#include "i_system.h"
#include "i_video.h"
#include "v_video.h"

#include "hu_stuff.h"

// State.
#include "doomstat.h"

// Data.
#include "dstrings.h"

#include "m_misc.h"

// Renderer back-end selection (DOOM-0026) — persisted via defaults[] below.
#include "r_backend.h"

//
// M_DrawText
// Returns the final X coordinate
// HU_Init must have been called to init the font
//
extern patch_t*		hu_font[HU_FONTSIZE];

int
M_DrawText
( int		x,
  int		y,
  boolean	direct,
  char*		string )
{
    int 	c;
    int		w;

    while (*string)
    {
	c = toupper(*string) - HU_FONTSTART;
	string++;
	if (c < 0 || c >= HU_FONTSIZE)
	{
	    x += 4;
	    continue;
	}
		
	w = SHORT (hu_font[c]->width);
	if (x+w > SCREENWIDTH)
	    break;
	if (direct)
	    V_DrawPatchDirect(x, y, 0, hu_font[c]);
	else
	    V_DrawPatch(x, y, 0, hu_font[c]);
	x+=w;
    }

    return x;
}




//
// M_WriteFile
//
#ifndef O_BINARY
#define O_BINARY 0
#endif

boolean
M_WriteFile
( char const*	name,
  void*		source,
  int		length )
{
    int		handle;
    int		count;
	
    handle = open ( name, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);

    if (handle == -1)
	return false;

    count = write (handle, source, length);
    close (handle);
	
    if (count < length)
	return false;
		
    return true;
}


//
// M_ReadFile
//
int
M_ReadFile
( char const*	name,
  byte**	buffer )
{
    int	handle, count, length;
    struct stat	fileinfo;
    byte		*buf;
	
    handle = open (name, O_RDONLY | O_BINARY, 0666);
    if (handle == -1)
	I_Error ("Couldn't read file %s", name);
    if (fstat (handle,&fileinfo) == -1)
	I_Error ("Couldn't read file %s", name);
    length = fileinfo.st_size;
    buf = Z_Malloc (length, PU_STATIC, NULL);
    count = read (handle, buf, length);
    close (handle);
	
    if (count < length)
	I_Error ("Couldn't read file %s", name);
		
    *buffer = buf;
    return length;
}


//
// DEFAULTS
//
int		usemouse;
int		usejoystick;

extern int	key_right;
extern int	key_left;
extern int	key_up;
extern int	key_down;

extern int	key_strafeleft;
extern int	key_straferight;

extern int	key_fire;
extern int	key_use;
extern int	key_strafe;
extern int	key_speed;

extern int	mousebfire;
extern int	mousebstrafe;
extern int	mousebforward;

extern int	joybfire;
extern int	joybstrafe;
extern int	joybuse;
extern int	joybspeed;

extern int	viewwidth;
extern int	viewheight;

extern int	mouseSensitivity;
extern int	showMessages;
extern int	fpsCorner;
extern int	rb_upscaler;	// DOOM-0009 6-d temporal upscaler (0 Off, 1 TAAU)
extern int	rb_renderscale;	// DOOM-0009 6-d render scale percent (100/75/67/50)
extern int	rb_exposure;	// DOOM-0096 Ultra/denoiser brightness slider (0..15)
extern int	rb_flashlight;	// DOOM-0044 player flashlight on/off (persisted)
extern int	rb_ssao;	// DOOM-0170 L2b raster SSAO (contact shadows) on/off (persisted)
extern int	rb_rtdebug;	// DOOM-0116 Ultra path-tracer view (6 = denoised; persisted)
extern int	rb_rtdebug_menu;// DOOM-0135 Debug Views toggle (0 = ~ is RT on/off; 1 = ~ cycles diagnostics)
extern int	rb_profile;	// DOOM-0090 per-pass GPU profiler toggle (0 Off; `\` key)
extern int	rb_detile;	// DOOM-0181 Ultra RT de-tile quality (0 off/1 2-tap/2 4-tap; `]` key)
extern int	rb_filth;	// DOOM-0187 Ultra RT dirt-stain filth master (1 on/0 off; `[` key)
extern int	rb_wet;		// DOOM-0183 Ultra RT wet-liquid layers (sheen/ripple/puddle; 1 on/0 off; `'` key)
extern int	rb_fog;		// DOOM-0011 RT volumetric fog strength (0 off / 1 Low / 2 Med / 3 High; `;` key)
extern int	rb_bloom;	// DOOM-0331 bloom strength (0 off / 1 Low / 2 Med / 3 High; menu row, no hotkey)

extern int	detailLevel;

extern int	screenblocks;

extern int	showMessages;

// machine-independent sound params
extern	int	numChannels;


// UNIX hack, to be removed.
#ifdef SNDSERV
extern char*	sndserver_filename;
extern int	mb_used;
#endif

#ifdef LINUX
char*		mousetype;
char*		mousedev;
#endif

extern char*	chat_macros[];



typedef struct
{
    char*	name;
    int*	location;
    intptr_t	defaultvalue;		// holds an int, or a char* for string defaults
    int		scantranslate;		// PC scan code hack
    int		untranslated;		// lousy hack
} default_t;

default_t	defaults[] =
{
    {"mouse_sensitivity",&mouseSensitivity, 5},
    {"sfx_volume",&snd_SfxVolume, 15},
    {"music_volume",&snd_MusicVolume, 8},
    {"show_messages",&showMessages, 1},
    {"fps_corner",&fpsCorner, 0},
    {"renderer",&rendermode, RB_CLASSIC},
    {"upscaler",&rb_upscaler, 1},		// DOOM-0090: default Ultra to TAAU @ 50%
    {"render_scale",&rb_renderscale, 50},	// so it boots playable, not 8 FPS at native
    {"rt_brightness",&rb_exposure, 10},
    {"flashlight",&rb_flashlight, 0},
    {"ssao",&rb_ssao, 1},			// DOOM-0170 L2b: raster contact shadows, default on
    {"rt_view",&rb_rtdebug, 6},
    {"rt_debug_views",&rb_rtdebug_menu, 0},
    {"widescreen",&widescreen, 1},		// DOOM-0147 Part C: 0 = force 4:3 (restart)
    {"fillstretch",&fillstretch, 0},		// DOOM-0147 Part C: 1 = stretch to fill (live)
    {"rt_profile",&rb_profile, 0},
    {"rt_detile",&rb_detile, 2},		// DOOM-0181: de-tile quality, default 4-tap
    {"rt_filth",&rb_filth, 1},		// DOOM-0187: dirt-stain filth, default on
    {"rt_wet",&rb_wet, 1},		// DOOM-0183: wet-liquid layers, default on
    {"rt_fog",&rb_fog, 1},		// DOOM-0011: volumetric fog strength 0..3, default Low
    {"rt_bloom",&rb_bloom, 2},		// DOOM-0331: bloom strength 0..3, default Medium


#ifdef NORMALUNIX
    {"key_right",&key_right, KEY_RIGHTARROW},
    {"key_left",&key_left, KEY_LEFTARROW},
    {"key_up",&key_up, KEY_UPARROW},
    {"key_down",&key_down, KEY_DOWNARROW},
    {"key_strafeleft",&key_strafeleft, ','},
    {"key_straferight",&key_straferight, '.'},

    {"key_fire",&key_fire, KEY_RCTRL},
    {"key_use",&key_use, ' '},
    {"key_strafe",&key_strafe, KEY_RALT},
    {"key_speed",&key_speed, KEY_RSHIFT},

// UNIX hack, to be removed. 
#ifdef SNDSERV
    {"sndserver", (int *) &sndserver_filename, (intptr_t) "sndserver"},
    {"mb_used", &mb_used, 2},
#endif
    
#endif

#ifdef LINUX
    {"mousedev", (int*)&mousedev, (intptr_t)"/dev/ttyS0"},
    {"mousetype", (int*)&mousetype, (intptr_t)"microsoft"},
#endif

    {"use_mouse",&usemouse, 1},
    {"mouseb_fire",&mousebfire,0},
    {"mouseb_strafe",&mousebstrafe,1},
    {"mouseb_forward",&mousebforward,2},

    {"use_joystick",&usejoystick, 0},
    {"joyb_fire",&joybfire,0},
    {"joyb_strafe",&joybstrafe,1},
    {"joyb_use",&joybuse,3},
    {"joyb_speed",&joybspeed,2},

    /* DOOM-0285: 9 is vanilla's bordered view -- a shrunken play area inside a
     * decorative wall-texture frame. Nobody wants that here (user, 2026-07-30):
     * the play area is always full width. 10 is the largest the menu allows
     * (DOOM-0148 keeps the status bar; 11 would hide it), so 10 is "borderless". */
    {"screenblocks",&screenblocks, 10},
    {"detaillevel",&detailLevel, 0},

    {"snd_channels",&numChannels, 3},



    {"usegamma",&usegamma, 0},

    {"chatmacro0", (int *) &chat_macros[0], (intptr_t) HUSTR_CHATMACRO0 },
    {"chatmacro1", (int *) &chat_macros[1], (intptr_t) HUSTR_CHATMACRO1 },
    {"chatmacro2", (int *) &chat_macros[2], (intptr_t) HUSTR_CHATMACRO2 },
    {"chatmacro3", (int *) &chat_macros[3], (intptr_t) HUSTR_CHATMACRO3 },
    {"chatmacro4", (int *) &chat_macros[4], (intptr_t) HUSTR_CHATMACRO4 },
    {"chatmacro5", (int *) &chat_macros[5], (intptr_t) HUSTR_CHATMACRO5 },
    {"chatmacro6", (int *) &chat_macros[6], (intptr_t) HUSTR_CHATMACRO6 },
    {"chatmacro7", (int *) &chat_macros[7], (intptr_t) HUSTR_CHATMACRO7 },
    {"chatmacro8", (int *) &chat_macros[8], (intptr_t) HUSTR_CHATMACRO8 },
    {"chatmacro9", (int *) &chat_macros[9], (intptr_t) HUSTR_CHATMACRO9 }

};

int	numdefaults;
char*	defaultfile;


//
// M_SaveDefaults
//
void M_SaveDefaults (void)
{
    int		i;
    int		v;
    FILE*	f;
    char	tmpfile[PATH_MAX];

    // DOOM-0254: vanilla truncated the real config first and wrote into it, so
    // a crash or a full disk mid-write left the user with a half-written (or
    // empty) ~/.doomrc. Write a sibling temp file and rename it into place —
    // rename(2) is atomic within a directory.
    if ((size_t)snprintf (tmpfile, sizeof(tmpfile), "%s.tmp", defaultfile)
	>= sizeof(tmpfile))
    {
	fprintf (stderr, "M_SaveDefaults: config path too long, not saved\n");
	return;
    }

    f = fopen (tmpfile, "w");
    if (!f)
    {
	fprintf (stderr, "M_SaveDefaults: can't write %s: %s\n",
		 tmpfile, strerror (errno));
	return;
    }

    for (i=0 ; i<numdefaults ; i++)
    {
	if (defaults[i].defaultvalue > -0xfff
	    && defaults[i].defaultvalue < 0xfff)
	{
	    v = *defaults[i].location;
	    fprintf (f,"%s\t\t%i\n",defaults[i].name,v);
	} else {
	    fprintf (f,"%s\t\t\"%s\"\n",defaults[i].name,
		     * (char **) (defaults[i].location));
	}
    }

    if (fclose (f) != 0)
    {
	fprintf (stderr, "M_SaveDefaults: write to %s failed: %s\n",
		 tmpfile, strerror (errno));
	remove (tmpfile);
	return;
    }

    // DOOM-0353: rename(2) silently replaces an existing destination on POSIX,
    // which is what makes the temp-file dance above atomic. Windows' rename()
    // does NOT — it fails with EEXIST once a config exists — so every settings
    // change was dropped from the second launch onward. MoveFileExA with
    // MOVEFILE_REPLACE_EXISTING is the Win32 equivalent, and keeps the
    // replace-in-one-step property; remove()-then-rename() would not.
#ifdef _WIN32
    if (!MoveFileExA (tmpfile, defaultfile, MOVEFILE_REPLACE_EXISTING))
    {
	fprintf (stderr, "M_SaveDefaults: can't replace %s: Win32 error %lu\n",
		 defaultfile, (unsigned long) GetLastError ());
	remove (tmpfile);
    }
#else
    if (rename (tmpfile, defaultfile) != 0)
    {
	fprintf (stderr, "M_SaveDefaults: can't replace %s: %s\n",
		 defaultfile, strerror (errno));
	remove (tmpfile);
    }
#endif
}


//
// M_LoadDefaults
//
extern byte	scantokey[128];

void M_LoadDefaults (void)
{
    int		i;
    int		len;
    FILE*	f;
    char	def[80];
    char	strparm[100];
    char*	newstring;
    int		parm;
    boolean	isstring;
    
    // set everything to base values
    numdefaults = sizeof(defaults)/sizeof(defaults[0]);
    for (i=0 ; i<numdefaults ; i++)
    {
	// String defaults carry a char* in defaultvalue (it falls outside
	// the small-int range); write it full-width so the pointer survives
	// on 64-bit. Numeric defaults write through the int* as before.
	if (defaults[i].defaultvalue > -0xfff
	    && defaults[i].defaultvalue < 0xfff)
	    *defaults[i].location = defaults[i].defaultvalue;
	else
	    *(char **)defaults[i].location = (char *)defaults[i].defaultvalue;
    }
    
    // check for a custom default file
    i = M_CheckParm ("-config");
    if (i && i<myargc-1)
    {
	defaultfile = myargv[i+1];
	printf ("	default file: %s\n",defaultfile);
    }
    else
	defaultfile = basedefault;
    
    // read the file in, overriding any set defaults
    f = fopen (defaultfile, "r");
    if (f)
    {
	while (!feof(f))
	{
	    isstring = false;
	    if (fscanf (f, "%79s %99[^\n]\n", def, strparm) == 2)
	    {
		if (strparm[0] == '"')
		{
		    // get a string default
		    isstring = true;
		    len = strlen(strparm);
		    newstring = (char *) malloc(len);
		    if (!newstring)
			I_Error ("M_LoadDefaults: out of memory for string default");
		    strparm[len-1] = 0;
		    strcpy(newstring, strparm+1);
		}
		else if (strparm[0] == '0' && strparm[1] == 'x')
		    sscanf(strparm+2, "%x", (unsigned int *)&parm);
		else
		    sscanf(strparm, "%i", &parm);
		for (i=0 ; i<numdefaults ; i++)
		    if (!strcmp(def, defaults[i].name))
		    {
			if (!isstring)
			    *defaults[i].location = parm;
			else
			    *(char **)defaults[i].location =
				newstring;
			break;
		    }
	    }
	}

	fclose (f);
    }

    // DOOM-0254: ~/.doomrc is a plain text file the user (or anything else on
    // the machine) can edit, and these four are used as array indices or as an
    // allocation size — usegamma indexes gammatable[5] during I_SetPalette,
    // screenblocks drives R_SetViewSize, detailLevel picks a column function,
    // snd_channels sizes the channel table. Clamp them once, here, rather than
    // at every use site. DOOM-0253 tracks sweeping the rest of the table.
    if (usegamma < 0 || usegamma > 4)
	usegamma = 0;
    if (screenblocks < 3)
	screenblocks = 3;
    if (screenblocks > 11)
	screenblocks = 11;
    if (detailLevel < 0 || detailLevel > 1)
	detailLevel = 0;
    if (numChannels < 1 || numChannels > 32)
	numChannels = 3;
}


//
// SCREEN SHOTS
//


typedef struct
{
    char		manufacturer;
    char		version;
    char		encoding;
    char		bits_per_pixel;

    unsigned short	xmin;
    unsigned short	ymin;
    unsigned short	xmax;
    unsigned short	ymax;
    
    unsigned short	hres;
    unsigned short	vres;

    unsigned char	palette[48];
    
    char		reserved;
    char		color_planes;
    unsigned short	bytes_per_line;
    unsigned short	palette_type;
    
    char		filler[58];
    unsigned char	data;		// unbounded
} pcx_t;


//
// WritePCXfile
//
void
WritePCXfile
( char*		filename,
  byte*		data,
  int		width,
  int		height,
  byte*		palette )
{
    int		i;
    int		length;
    pcx_t*	pcx;
    byte*	pack;
	
    pcx = Z_Malloc (width*height*2+1000, PU_STATIC, NULL);

    pcx->manufacturer = 0x0a;		// PCX id
    pcx->version = 5;			// 256 color
    pcx->encoding = 1;			// uncompressed
    pcx->bits_per_pixel = 8;		// 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = SHORT(width-1);
    pcx->ymax = SHORT(height-1);
    pcx->hres = SHORT(width);
    pcx->vres = SHORT(height);
    memset (pcx->palette,0,sizeof(pcx->palette));
    pcx->color_planes = 1;		// chunky image
    pcx->bytes_per_line = SHORT(width);
    pcx->palette_type = SHORT(2);	// not a grey scale
    memset (pcx->filler,0,sizeof(pcx->filler));


    // pack the image
    pack = &pcx->data;
	
    for (i=0 ; i<width*height ; i++)
    {
	if ( (*data & 0xc0) != 0xc0)
	    *pack++ = *data++;
	else
	{
	    *pack++ = 0xc1;
	    *pack++ = *data++;
	}
    }
    
    // write the palette
    *pack++ = 0x0c;	// palette ID byte
    for (i=0 ; i<768 ; i++)
	*pack++ = *palette++;
    
    // write output file
    length = pack - (byte *)pcx;
    M_WriteFile (filename, pcx, length);

    Z_Free (pcx);
}


//
// M_ScreenShot
//
void M_ScreenShot (void)
{
    int		i;
    byte*	linear;
    char	lbmname[12];
    
    // munge planar buffer to linear
    linear = screens[2];
    I_ReadScreen (linear);
    
    // find a file name to save it to
    strcpy(lbmname,"DOOM00.pcx");
		
    for (i=0 ; i<=99 ; i++)
    {
	lbmname[4] = i/10 + '0';
	lbmname[5] = i%10 + '0';
	if (access(lbmname,0) == -1)
	    break;	// file doesn't exist
    }
    if (i==100)
	I_Error ("M_ScreenShot: Couldn't create a PCX");
    
    // save the pcx file
    WritePCXfile (lbmname, linear,
		  SCREENWIDTH, SCREENHEIGHT,
		  W_CacheLumpName ("PLAYPAL",PU_CACHE));
	
    players[consoleplayer].message = "screen shot";
}


