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
//	Gamma correction LUT stuff.
//	Functions to draw patches (by post) directly to screen.
//	Functions to blit a block to the screen.
//
//-----------------------------------------------------------------------------


static const char
rcsid[] __attribute__((used)) = "$Id: v_video.c,v 1.5 1997/02/03 22:45:13 b1 Exp $";


#include "i_system.h"
#include "r_local.h"

#include "doomdef.h"
#include "doomdata.h"

#include "m_bbox.h"
#include "m_swap.h"

#include "v_video.h"


// Each screen is [screenwidth[i]*<height>].  screens[0..3] are the physical
// full-frame buffers (width SCREENWIDTH); screens[4] is the status-bar scratch,
// kept at the logical ORIGWIDTH.  The drawing primitives scale a logical request
// to each buffer's own width (DOOM-0027).
byte*				screens[5];
int				screenwidth[5];

int				dirtybox[4];



// Now where did these came from?
byte gammatable[5][256] =
{
    {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
     17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
     33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,
     49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,
     65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,
     81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,
     97,98,99,100,101,102,103,104,105,106,107,108,109,110,111,112,
     113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,
     128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
     144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
     160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
     176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
     192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
     208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
     224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
     240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255},

    {2,4,5,7,8,10,11,12,14,15,16,18,19,20,21,23,24,25,26,27,29,30,31,
     32,33,34,36,37,38,39,40,41,42,44,45,46,47,48,49,50,51,52,54,55,
     56,57,58,59,60,61,62,63,64,65,66,67,69,70,71,72,73,74,75,76,77,
     78,79,80,81,82,83,84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,
     99,100,101,102,103,104,105,106,107,108,109,110,111,112,113,114,
     115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,129,
     130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,
     146,147,148,148,149,150,151,152,153,154,155,156,157,158,159,160,
     161,162,163,163,164,165,166,167,168,169,170,171,172,173,174,175,
     175,176,177,178,179,180,181,182,183,184,185,186,186,187,188,189,
     190,191,192,193,194,195,196,196,197,198,199,200,201,202,203,204,
     205,205,206,207,208,209,210,211,212,213,214,214,215,216,217,218,
     219,220,221,222,222,223,224,225,226,227,228,229,230,230,231,232,
     233,234,235,236,237,237,238,239,240,241,242,243,244,245,245,246,
     247,248,249,250,251,252,252,253,254,255},

    {4,7,9,11,13,15,17,19,21,22,24,26,27,29,30,32,33,35,36,38,39,40,42,
     43,45,46,47,48,50,51,52,54,55,56,57,59,60,61,62,63,65,66,67,68,69,
     70,72,73,74,75,76,77,78,79,80,82,83,84,85,86,87,88,89,90,91,92,93,
     94,95,96,97,98,100,101,102,103,104,105,106,107,108,109,110,111,112,
     113,114,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,
     129,130,131,132,133,133,134,135,136,137,138,139,140,141,142,143,144,
     144,145,146,147,148,149,150,151,152,153,153,154,155,156,157,158,159,
     160,160,161,162,163,164,165,166,166,167,168,169,170,171,172,172,173,
     174,175,176,177,178,178,179,180,181,182,183,183,184,185,186,187,188,
     188,189,190,191,192,193,193,194,195,196,197,197,198,199,200,201,201,
     202,203,204,205,206,206,207,208,209,210,210,211,212,213,213,214,215,
     216,217,217,218,219,220,221,221,222,223,224,224,225,226,227,228,228,
     229,230,231,231,232,233,234,235,235,236,237,238,238,239,240,241,241,
     242,243,244,244,245,246,247,247,248,249,250,251,251,252,253,254,254,
     255},

    {8,12,16,19,22,24,27,29,31,34,36,38,40,41,43,45,47,49,50,52,53,55,
     57,58,60,61,63,64,65,67,68,70,71,72,74,75,76,77,79,80,81,82,84,85,
     86,87,88,90,91,92,93,94,95,96,98,99,100,101,102,103,104,105,106,107,
     108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,
     125,126,127,128,129,130,131,132,133,134,135,135,136,137,138,139,140,
     141,142,143,143,144,145,146,147,148,149,150,150,151,152,153,154,155,
     155,156,157,158,159,160,160,161,162,163,164,165,165,166,167,168,169,
     169,170,171,172,173,173,174,175,176,176,177,178,179,180,180,181,182,
     183,183,184,185,186,186,187,188,189,189,190,191,192,192,193,194,195,
     195,196,197,197,198,199,200,200,201,202,202,203,204,205,205,206,207,
     207,208,209,210,210,211,212,212,213,214,214,215,216,216,217,218,219,
     219,220,221,221,222,223,223,224,225,225,226,227,227,228,229,229,230,
     231,231,232,233,233,234,235,235,236,237,237,238,238,239,240,240,241,
     242,242,243,244,244,245,246,246,247,247,248,249,249,250,251,251,252,
     253,253,254,254,255},

    {16,23,28,32,36,39,42,45,48,50,53,55,57,60,62,64,66,68,69,71,73,75,76,
     78,80,81,83,84,86,87,89,90,92,93,94,96,97,98,100,101,102,103,105,106,
     107,108,109,110,112,113,114,115,116,117,118,119,120,121,122,123,124,
     125,126,128,128,129,130,131,132,133,134,135,136,137,138,139,140,141,
     142,143,143,144,145,146,147,148,149,150,150,151,152,153,154,155,155,
     156,157,158,159,159,160,161,162,163,163,164,165,166,166,167,168,169,
     169,170,171,172,172,173,174,175,175,176,177,177,178,179,180,180,181,
     182,182,183,184,184,185,186,187,187,188,189,189,190,191,191,192,193,
     193,194,195,195,196,196,197,198,198,199,200,200,201,202,202,203,203,
     204,205,205,206,207,207,208,208,209,210,210,211,211,212,213,213,214,
     214,215,216,216,217,217,218,219,219,220,220,221,221,222,223,223,224,
     224,225,225,226,227,227,228,228,229,229,230,230,231,232,232,233,233,
     234,234,235,235,236,236,237,237,238,239,239,240,240,241,241,242,242,
     243,243,244,244,245,245,246,246,247,247,248,248,249,249,250,250,251,
     251,252,252,253,254,254,255,255}
};



int	usegamma;
			 
//
// V_MarkRect 
// 
void
V_MarkRect
( int		x,
  int		y,
  int		width,
  int		height ) 
{ 
    M_AddToBox (dirtybox, x, y); 
    M_AddToBox (dirtybox, x+width-1, y+height-1); 
} 
 

//
// V_CopyRect 
// 
void
V_CopyRect
( int		srcx,
  int		srcy,
  int		srcscrn,
  int		width,
  int		height,
  int		destx,
  int		desty,
  int		destscrn ) 
{ 
    byte*	src;
    byte*	dest;
    int		ssw, dsw;	// per-buffer strides
    int		sf, df;		// per-buffer scale factors
    int		slw, dlw;	// per-buffer LOGICAL widths (stride / scale)
    int		lx, ly, rx, ry;

    // All coordinates/dimensions are logical (320x200 space); each side scales
    // by its own buffer's factor (DOOM-0027). Validate the screen indices first so
    // the screenwidth[] lookups below are in-bounds.
#ifdef RANGECHECK
    if ((unsigned)srcscrn>4 || (unsigned)destscrn>4)
	I_Error ("Bad V_CopyRect (screen index)");
#endif

    ssw = screenwidth[srcscrn];
    dsw = screenwidth[destscrn];
    // DOOM-0147: scale is HIRES for full-screen buffers, 1 for the ORIGWIDTH-wide
    // status-bar scratch. (A widescreen buffer must not inflate the factor past
    // HIRES; dsw/ORIGWIDTH only stayed == HIRES by luck at 4:3.)
    sf = (ssw == ORIGWIDTH) ? 1 : HIRES;
    df = (dsw == ORIGWIDTH) ? 1 : HIRES;
    // Each buffer's own logical width. A widescreen full-screen buffer is
    // SCREENWIDTH/HIRES logical columns wide (> ORIGWIDTH), so a copy may land past
    // 320 -- e.g. the status bar, drawn to a WIDESCREENDELTA-shifted x (DOOM-0147).
    // At 4:3 both reduce to ORIGWIDTH, so the bound is identical (zero-diff).
    slw = ssw / sf;
    dlw = dsw / df;

#ifdef RANGECHECK
    if (srcx<0
	|| srcx+width > slw
	|| srcy<0
	|| srcy+height>ORIGHEIGHT
	|| destx<0
	|| destx+width > dlw
	|| desty<0
	|| desty+height>ORIGHEIGHT)
    {
	I_Error ("Bad V_CopyRect");
    }
#endif
    V_MarkRect (destx, desty, width, height);

    src = screens[srcscrn] + (srcy*sf)*ssw + (srcx*sf);
    dest = screens[destscrn] + (desty*df)*dsw + (destx*df);

    for (ly=0 ; ly<height ; ly++)
    {
	for (lx=0 ; lx<width ; lx++)
	{
	    byte px = src[ly*sf*ssw + lx*sf];	// top-left of the sf-block
	    byte* d = dest + (ly*df)*dsw + (lx*df);
	    for (ry=0 ; ry<df ; ry++, d+=dsw)	// write a df x df block
		for (rx=0 ; rx<df ; rx++)
		    d[rx] = px;
	}
    }
}


//
// V_ExtendSides  (DOOM-0151)
//
// Fill the widescreen side strips either side of a centred 320-wide full-screen
// image by clamping (repeating) the image's edge column outward, so the black bars
// on a wide display become a seamless continuation of the art. Reads/writes only
// this buffer's own pixels -- no external art, nothing redistributed. No-op on the
// ORIGWIDTH scratch and at 4:3, where WIDESCREENDELTA is 0 (zero-diff).
//
void V_ExtendSides (int scrn)
{
    int		y, x, dsw, left, right;
    byte*	row;

    if ((unsigned)scrn > 4)
	return;
    dsw = screenwidth[scrn];
    if (dsw == ORIGWIDTH || WIDESCREENDELTA <= 0)
	return;

    left  = WIDESCREENDELTA * HIRES;		// first physical column of the art
    right = left + ORIGWIDTH * HIRES;		// one past the art's last column

    for (y=0 ; y<SCREENHEIGHT ; y++)
    {
	row = screens[scrn] + y*dsw;
	byte lpx = row[left];			// art's left edge pixel
	byte rpx = row[right-1];			// art's right edge pixel
	for (x=0 ; x<left ; x++)
	    row[x] = lpx;
	for (x=right ; x<dsw ; x++)
	    row[x] = rpx;
    }
}


//
// V_PostInBounds
// DOOM-0254: a patch column is a chain of posts whose topdelta and length come
// straight out of the WAD, and those two bytes drive the destination pointer of
// every blitter below. Vanilla trusted them, so a crafted patch could walk the
// blit past the end of the destination buffer. A post that does not fit inside
// the patch's own declared height is malformed; callers stop drawing that
// column instead of writing out of bounds.
//
boolean V_PostInBounds (const column_t* column, int height)
{
    return (int)column->topdelta + (int)column->length <= height;
}


//
// V_DrawPatchGeneral
// Masks a column based masked pic to the screen. When trans is non-NULL every
// source pixel is remapped through it (a 256-entry palette-translation table),
// e.g. to recolour the monochrome HUD font (DOOM-0158 centred secret message).
// trans == NULL is the plain path, pixel-identical to the original V_DrawPatch.
//
static void
V_DrawPatchGeneral
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch,
  const byte*	trans )
{

    int		count;
    int		col; 
    column_t*	column; 
    byte*	desttop;
    byte*	dest;
    byte*	source; 
    int		w; 
	 
    int		dsw = screenwidth[scrn];	// dest stride
    // DOOM-0147: HIRES for full-screen buffers, 1 for the ORIGWIDTH-wide scratch.
    int		f = (dsw == ORIGWIDTH) ? 1 : HIRES;
    // Re-centre 320-wide UI art in a widescreen frame (0 on the scratch and at 4:3).
    int		wsdelta = (dsw == ORIGWIDTH) ? 0 : WIDESCREENDELTA;
    int		rx, ry;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);
#ifdef RANGECHECK
    if (x<0
	||x+SHORT(patch->width) >ORIGWIDTH
	|| y<0
	|| y+SHORT(patch->height)>ORIGHEIGHT
	|| (unsigned)scrn>4)
    {
      // DOOM-0137/0171: RANGECHECK rejects patches drawn outside the 320x200
      // logical screen (view-border bezel at startup; widescreen/4K status-bar
      // fill). They are ignored and the frame renders fine, so the underlying
      // constraint is cosmetic -- but the two fprintfs per patch flood the log.
      // Rate-limit to a few lines then suppress; a real border/tiling geometry
      // fix stays a separate task.
      static int nbadpatch = 0;
      if (nbadpatch < 3)
      {
        fprintf( stderr, "Patch at %d,%d exceeds LFB\n", x,y );
        // No I_Error abort - what is up with TNT.WAD?
        fprintf( stderr, "V_DrawPatch: bad patch (ignored)\n");
        if (++nbadpatch == 3)
          fprintf( stderr, "V_DrawPatch: further out-of-bounds patch warnings suppressed\n");
      }
      return;
    }
#endif

    if (!scrn)
	V_MarkRect (x, y, SHORT(patch->width), SHORT(patch->height));

    x += wsdelta;				// DOOM-0147 widescreen UI centring
    col = 0;
    desttop = screens[scrn] + (y*f)*dsw + (x*f);	// physical top-left

    w = SHORT(patch->width);

    for ( ; col<w ; col++, desttop+=f)
    {
	column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));

	// step through the posts in a column
	while (column->topdelta != 0xff )
	{
	    // Malformed post (overruns the patch's own height): stop this column
	    // rather than blit past the end of the screen buffer.
	    if (!V_PostInBounds (column, SHORT(patch->height)))
		break;

	    source = (byte *)column + 3;
	    dest = desttop + column->topdelta*f*dsw;
	    count = column->length;

	    while (count--)
	    {
		byte px = *source++;		// one source pixel -> f x f block
		if (trans)
		    px = trans[px];		// palette recolour (e.g. gold font)
		byte* d = dest;
		for (ry=0 ; ry<f ; ry++, d+=dsw)
		    for (rx=0 ; rx<f ; rx++)
			d[rx] = px;
		dest += f*dsw;
	    }
	    column = (column_t *)(  (byte *)column + column->length
				    + 4 );
	}
    }
}

//
// V_DrawPatchScaled
// As V_DrawPatchGeneral's plain path, but every SOURCE pixel expands to a
// (HIRES*scale) square instead of HIRES -- i.e. the patch is blitted at an
// integer `scale` multiple of its normal size. DOOM-0206: the Classic main
// menu draws all its items in the red HUD font at scale 2 ("medium"), so the
// oversized big-red graphic lumps and the tiny "Game Select" text meet at one
// uniform size. Coordinates stay in 320x200 virtual space; widescreen centring
// and the HIRES buffer factor are honoured exactly like V_DrawPatch.
//
void
V_DrawPatchScaled
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch,
  int		scale )
{
    int		count;
    int		col;
    column_t*	column;
    byte*	desttop;
    byte*	dest;
    byte*	source;
    int		w;

    int		dsw = screenwidth[scrn];
    int		f = (dsw == ORIGWIDTH) ? 1 : HIRES;
    int		wsdelta = (dsw == ORIGWIDTH) ? 0 : WIDESCREENDELTA;
    int		fs, rx, ry;

    if (scale < 1) scale = 1;
    fs = f * scale;				// physical pixels per source pixel

    y -= SHORT(patch->topoffset) * scale;
    x -= SHORT(patch->leftoffset) * scale;

    if (!scrn)
	V_MarkRect (x, y, SHORT(patch->width)*scale, SHORT(patch->height)*scale);

    x += wsdelta;
    col = 0;
    desttop = screens[scrn] + (y*f)*dsw + (x*f);

    w = SHORT(patch->width);

    for ( ; col<w ; col++, desttop += fs)
    {
	column = (column_t *)((byte *)patch + LONG(patch->columnofs[col]));

	// step through the posts in a column
	while (column->topdelta != 0xff )
	{
	    // Malformed post (overruns the patch's own height): stop this column
	    // rather than blit past the end of the screen buffer.
	    if (!V_PostInBounds (column, SHORT(patch->height)))
		break;

	    source = (byte *)column + 3;
	    dest = desttop + column->topdelta*fs*dsw;
	    count = column->length;

	    while (count--)
	    {
		byte px = *source++;		// one source pixel -> fs x fs block
		byte* d = dest;
		for (ry=0 ; ry<fs ; ry++, d+=dsw)
		    for (rx=0 ; rx<fs ; rx++)
			d[rx] = px;
		dest += fs*dsw;
	    }
	    column = (column_t *)(  (byte *)column + column->length
				    + 4 );
	}
    }
}

//
// V_DrawPatch / V_DrawPatchTranslated
// Thin wrappers over V_DrawPatchGeneral: plain draw vs palette-remapped draw.
//
void V_DrawPatch (int x, int y, int scrn, patch_t* patch)
{
    V_DrawPatchGeneral (x, y, scrn, patch, NULL);
}

void V_DrawPatchTranslated (int x, int y, int scrn, patch_t* patch, const byte* trans)
{
    V_DrawPatchGeneral (x, y, scrn, patch, trans);
}

//
// V_DrawPatchFlipped
// Masks a column based masked pic to the screen.
// Flips horizontally, e.g. to mirror face.
//
void
V_DrawPatchFlipped
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch ) 
{ 

    int		count;
    int		col; 
    column_t*	column; 
    byte*	desttop;
    byte*	dest;
    byte*	source; 
    int		w; 
	 
    int		dsw = screenwidth[scrn];	// dest stride
    // DOOM-0147: HIRES for full-screen buffers, 1 for the ORIGWIDTH-wide scratch.
    int		f = (dsw == ORIGWIDTH) ? 1 : HIRES;
    int		wsdelta = (dsw == ORIGWIDTH) ? 0 : WIDESCREENDELTA;	// UI centring
    int		rx, ry;

    y -= SHORT(patch->topoffset);
    x -= SHORT(patch->leftoffset);
#ifdef RANGECHECK
    if (x<0
	||x+SHORT(patch->width) >ORIGWIDTH
	|| y<0
	|| y+SHORT(patch->height)>ORIGHEIGHT
	|| (unsigned)scrn>4)
    {
      fprintf( stderr, "Patch origin %d,%d exceeds LFB\n", x,y );
      I_Error ("Bad V_DrawPatch in V_DrawPatchFlipped");
    }
#endif

    if (!scrn)
	V_MarkRect (x, y, SHORT(patch->width), SHORT(patch->height));

    x += wsdelta;				// DOOM-0147 widescreen UI centring
    col = 0;
    desttop = screens[scrn] + (y*f)*dsw + (x*f);

    w = SHORT(patch->width);

    for ( ; col<w ; col++, desttop+=f)
    {
	column = (column_t *)((byte *)patch + LONG(patch->columnofs[w-1-col]));

	// step through the posts in a column
	while (column->topdelta != 0xff )
	{
	    // Malformed post (overruns the patch's own height): stop this column
	    // rather than blit past the end of the screen buffer.
	    if (!V_PostInBounds (column, SHORT(patch->height)))
		break;

	    source = (byte *)column + 3;
	    dest = desttop + column->topdelta*f*dsw;
	    count = column->length;

	    while (count--)
	    {
		byte px = *source++;
		byte* d = dest;
		for (ry=0 ; ry<f ; ry++, d+=dsw)
		    for (rx=0 ; rx<f ; rx++)
			d[rx] = px;
		dest += f*dsw;
	    }
	    column = (column_t *)(  (byte *)column + column->length
				    + 4 );
	}
    }
}
 


//
// V_DrawPatchDirect
// Draws directly to the screen on the pc. 
//
void
V_DrawPatchDirect
( int		x,
  int		y,
  int		scrn,
  patch_t*	patch ) 
{
    V_DrawPatch (x,y,scrn, patch); 

    /*
    int		count;
    int		col; 
    column_t*	column; 
    byte*	desttop;
    byte*	dest;
    byte*	source; 
    int		w; 
	 
    y -= SHORT(patch->topoffset); 
    x -= SHORT(patch->leftoffset); 

#ifdef RANGECHECK 
    if (x<0
	||x+SHORT(patch->width) >SCREENWIDTH
	|| y<0
	|| y+SHORT(patch->height)>SCREENHEIGHT 
	|| (unsigned)scrn>4)
    {
	I_Error ("Bad V_DrawPatchDirect");
    }
#endif 
 
    //	V_MarkRect (x, y, SHORT(patch->width), SHORT(patch->height)); 
    desttop = destscreen + y*SCREENWIDTH/4 + (x>>2); 
	 
    w = SHORT(patch->width); 
    for ( col = 0 ; col<w ; col++) 
    { 
	outp (SC_INDEX+1,1<<(x&3)); 
	column = (column_t *)((byte *)patch + LONG(patch->columnofs[col])); 
 
	// step through the posts in a column 
	 
	while (column->topdelta != 0xff ) 
	{ 
	    source = (byte *)column + 3; 
	    dest = desttop + column->topdelta*SCREENWIDTH/4; 
	    count = column->length; 
 
	    while (count--) 
	    { 
		*dest = *source++; 
		dest += SCREENWIDTH/4; 
	    } 
	    column = (column_t *)(  (byte *)column + column->length 
				    + 4 ); 
	} 
	if ( ((++x)&3) == 0 ) 
	    desttop++;	// go to next byte, not next plane 
    }*/ 
} 
 


//
// V_DrawBlock
// Draw a linear block of pixels into the view buffer.
//
void
V_DrawBlock
( int		x,
  int		y,
  int		scrn,
  int		width,
  int		height,
  byte*		src ) 
{ 
    byte*	dest; 
	 
#ifdef RANGECHECK 
    if (x<0
	||x+width >SCREENWIDTH
	|| y<0
	|| y+height>SCREENHEIGHT 
	|| (unsigned)scrn>4 )
    {
	I_Error ("Bad V_DrawBlock");
    }
#endif 
 
    V_MarkRect (x, y, width, height);

    dest = screens[scrn] + y*screenwidth[scrn]+x;

    while (height--)
    {
	memcpy (dest, src, width);
	src += width;
	dest += screenwidth[scrn];
    }
}
 


//
// V_GetBlock
// Gets a linear block of pixels from the view buffer.
//
void
V_GetBlock
( int		x,
  int		y,
  int		scrn,
  int		width,
  int		height,
  byte*		dest ) 
{ 
    byte*	src; 
	 
#ifdef RANGECHECK 
    if (x<0
	||x+width >SCREENWIDTH
	|| y<0
	|| y+height>SCREENHEIGHT 
	|| (unsigned)scrn>4 )
    {
	I_Error ("Bad V_DrawBlock");
    }
#endif 
 
    src = screens[scrn] + y*screenwidth[scrn]+x;

    while (height--)
    {
	memcpy (dest, src, width);
	src += screenwidth[scrn];
	dest += width;
    }
}




//
// V_Init
// 
void V_Init (void) 
{ 
    int		i;
    byte*	base;
		
    // stick these in low dos memory on PCs

    base = I_AllocLow (SCREENWIDTH*SCREENHEIGHT*4);

    for (i=0 ; i<4 ; i++)
    {
	screens[i] = base + i*SCREENWIDTH*SCREENHEIGHT;
	screenwidth[i] = SCREENWIDTH;
    }
    // screens[4] (the status-bar scratch) is allocated by ST_Init, which runs
    // after V_Init; its width is the compile-time logical ORIGWIDTH, so set it
    // here so the array is fully valid before any draw (DOOM-0027).
    screenwidth[4] = ORIGWIDTH;
}
