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
//	DOOM selection menu, options, episode etc.
//	Sliders and icons. Kinda widget stuff.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] __attribute__((used)) = "$Id: m_menu.c,v 1.7 1997/02/03 22:45:10 b1 Exp $";

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>


#include "doomdef.h"
#include "dstrings.h"

#include "d_main.h"
#include "iwad_detect.h"   // DOOM-0060 game-select: IWAD_DOOM1/DOOM2 family codes

#include "i_system.h"
#include "i_video.h"
#include "z_zone.h"
#include "v_video.h"
#include "w_wad.h"

#include "r_local.h"


#include "hu_stuff.h"

#include "g_game.h"

#include "m_argv.h"
#include "m_swap.h"

#include "s_sound.h"

#include "doomstat.h"

// Data.
#include "sounds.h"

#include "m_menu.h"

#include "r_backend.h"   // renderer back-end toggle (DOOM-0026)
#include "st_stuff.h"    // ST_Y: 320x200 status-bar top, the classic HUD-safe bound (DOOM-0206 L6)



extern patch_t*		hu_font[HU_FONTSIZE];
extern boolean		message_dontfuckwithme;

extern boolean		chat_on;		// in heads-up code

//
// defaulted values
//
int			mouseSensitivity;       // has default

// Show messages has default, 0 = off, 1 = on
int			showMessages;

// FPS counter (DOOM-0046), has default. 0 = off, 1 = top-left,
// 2 = top-centre, 3 = top-right. Drawn by HU_DrawFPS.
int			fpsCorner;


// Blocky mode, has default, 0 = high, 1 = normal
int			detailLevel;		
int			screenblocks;		// has default

// temp for screenblocks (0-9)
int			screenSize;		

// -1 = no quicksave slot picked!
int			quickSaveSlot;          

 // 1 = message to be printed
int			messageToPrint;
// ...and here is the message string!
char*			messageString;		

// message x & y
int			messx;			
int			messy;
int			messageLastMenuActive;

// timed message = no input from user
boolean			messageNeedsInput;     

void    (*messageRoutine)(int response);

#define SAVESTRINGSIZE 	24

char gammamsg[5][26] =
{
    GAMMALVL0,
    GAMMALVL1,
    GAMMALVL2,
    GAMMALVL3,
    GAMMALVL4
};

// we are going to be entering a savegame string
int			saveStringEnter;              
int             	saveSlot;	// which slot to save in
int			saveCharIndex;	// which char we're editing
// old save description before edit
char			saveOldString[SAVESTRINGSIZE];  

boolean			inhelpscreens;
boolean			menuactive;

#define SKULLXOFF		-32
#define LINEHEIGHT		16

extern boolean		sendpause;
char			savegamestrings[10][SAVESTRINGSIZE];

char	endstring[160];


//
// MENU TYPEDEFS
//
typedef struct
{
    // 0 = no cursor here, 1 = ok, 2 = arrows ok
    short	status;
    
    char	name[10];
    
    // choice = menu item #.
    // if status = 2,
    //   choice=0:leftarrow,1:rightarrow
    void	(*routine)(int choice);
    
    // hotkey in menu
    char	alphaKey;			
} menuitem_t;



typedef struct menu_s
{
    short		numitems;	// # of menu items
    struct menu_s*	prevMenu;	// previous menu
    menuitem_t*		menuitems;	// menu items
    void		(*routine)();	// draw routine
    short		x;
    short		y;		// x,y of menu
    short		lastOn;		// last item user was on in menu
} menu_t;

short		itemOn;			// menu item skull is on
short		skullAnimCounter;	// skull animation counter
short		whichSkull;		// which skull to draw

// graphic name of skulls
// warning: initializer-string for array of chars is too long
char    skullName[2][/*8*/9] = {"M_SKULL1","M_SKULL2"};

// current menudef
menu_t*	currentMenu;                          

//
// PROTOTYPES
//
void M_NewGame(int choice);
void M_Episode(int choice);
void M_ChooseSkill(int choice);
void M_LoadGame(int choice);
void M_SaveGame(int choice);
void M_Options(int choice);
void M_EndGame(int choice);
void M_ReadThis(int choice);
void M_ReadThis2(int choice);
void M_QuitDOOM(int choice);

void M_ChangeMessages(int choice);
void M_ChangeSensitivity(int choice);
void M_SfxVol(int choice);
void M_MusicVol(int choice);
void M_ChangeDetail(int choice);
void M_ChangeRenderer(int choice);
void M_RendererMenu(int choice);
void M_DrawRendererMenu(void);
void M_ChangeFPS(int choice);
void M_ChangeUpscaler(int choice);
void M_ChangeRenderScale(int choice);
void M_ChangeDebugViews(int choice);
void M_ChangeBrightness(int choice);
void M_ChangeWidescreen(int choice);
void M_ChangeFillScreen(int choice);
// DOOM-0205: Render Effects submenu (visible on/off state for the render toggles).
void M_UltraEffects(int choice);
void M_DrawEffectsMenu(void);
void M_ChangeFlashlight(int choice);
void M_ChangeSSAO(int choice);
void M_ChangeDetile(int choice);
void M_ChangeFilth(int choice);
void M_ChangeWet(int choice);
void M_ChangeProfiler(int choice);
// DOOM-0206 (L3): consolidated crisp Video menu (3D tiers) + its new Ray Tracing row.
void M_ChangeRayTracing(int choice);
void M_VideoBack(int choice);
void M_DrawVideoMenu(void);
void M_SizeDisplay(int choice);
void M_StartGame(int choice);
void M_Sound(int choice);

void M_FinishReadThis(int choice);
void M_LoadSelect(int choice);
void M_SaveSelect(int choice);
void M_ReadSaveStrings(void);
void M_QuickSave(void);
void M_QuickLoad(void);

void M_DrawMainMenu(void);
void M_DrawReadThis1(void);
void M_DrawReadThis2(void);
void M_DrawNewGame(void);
void M_DrawEpisode(void);
void M_DrawOptions(void);
void M_DrawSound(void);
void M_DrawLoad(void);
void M_DrawSave(void);

void M_DrawSaveLoadBorder(int x,int y);
void M_SetupNextMenu(menu_t *menudef);
void M_DrawThermo(int x,int y,int thermWidth,int thermDot);
void M_DrawEmptyCell(menu_t *menu,int item);
void M_DrawSelCell(menu_t *menu,int item);
void M_WriteText(int x, int y, char *string);
void M_WriteTextScaled(int x, int y, char *string, int scale);
int  M_StringWidth(char *string);
int  M_StringHeight(char *string);
void M_StartControlPanel(void);
void M_StartMessage(char *string,void *routine,boolean input);
void M_StopMessage(void);
void M_ClearMenus (void);

// DOOM-0060 game-select chooser.
void M_DrawGameSelect(void);
void M_GameSelectChoose(int choice);
void M_ReturnToGameSelect(int choice);




//
// DOOM MENU
//
enum
{
    newgame = 0,
    options,
    loadgame,
    savegame,
    readthis,
    quitdoom,
    main_end
} main_e;

menuitem_t MainMenu[]=
{
    {1,"M_NGAME",M_NewGame,'n'},
    {1,"M_OPTION",M_Options,'o'},
    {1,"M_LOADG",M_LoadGame,'l'},
    {1,"M_SAVEG",M_SaveGame,'s'},
    // Another hickup with Special edition.
    {1,"M_RDTHIS",M_ReadThis,'r'},
    {1,"M_QUITG",M_QuitDOOM,'q'},
    // DOOM-0060: "Game Select" — no menu-art lump, so it draws as text
    // (M_DrawMainMenu) and is spliced in by M_Init only when both games are
    // installed. Lives at index main_end; hidden by the default numitems.
    {1,"",M_ReturnToGameSelect,'g'}
};

menu_t  MainDef =
{
    main_end,
    NULL,
    MainMenu,
    M_DrawMainMenu,
    97,64,
    0
};


//
// EPISODE SELECT
//
enum
{
    ep1,
    ep2,
    ep3,
    ep4,
    ep_end
} episodes_e;

menuitem_t EpisodeMenu[]=
{
    {1,"M_EPI1", M_Episode,'k'},
    {1,"M_EPI2", M_Episode,'t'},
    {1,"M_EPI3", M_Episode,'i'},
    {1,"M_EPI4", M_Episode,'t'}
};

menu_t  EpiDef =
{
    ep_end,		// # of menu items
    &MainDef,		// previous menu
    EpisodeMenu,	// menuitem_t ->
    M_DrawEpisode,	// drawing routine ->
    48,63,              // x,y
    ep1			// lastOn
};

//
// NEW GAME
//
enum
{
    killthings,
    toorough,
    hurtme,
    violence,
    nightmare,
    newg_end
} newgame_e;

menuitem_t NewGameMenu[]=
{
    {1,"M_JKILL",	M_ChooseSkill, 'i'},
    {1,"M_ROUGH",	M_ChooseSkill, 'h'},
    {1,"M_HURT",	M_ChooseSkill, 'h'},
    {1,"M_ULTRA",	M_ChooseSkill, 'u'},
    {1,"M_NMARE",	M_ChooseSkill, 'n'}
};

menu_t  NewDef =
{
    newg_end,		// # of menu items
    &EpiDef,		// previous menu
    NewGameMenu,	// menuitem_t ->
    M_DrawNewGame,	// drawing routine ->
    48,63,              // x,y
    hurtme		// lastOn
};



//
// OPTIONS MENU
//
// DOOM-0206 (v2 §4.6): two play-test content fixes. Graphic Detail is removed
// (a confirmed no-op on every backend -- M_ChangeDetail only prints "n.a."; the
// F5 keybind still calls it, so the handler + detailLevel var/config stay, only
// the row is gone). The FPS row is de-duplicated: it lived here AND in the Video
// menu -- it now lives only in the Video menu (3D tiers) and the Classic-only
// RendererDef (see rm_fps), so Options no longer carries it. The "Video" row
// stays -- it is the sole entry into the render menu (M_RendererMenu).
enum
{
    endgame,
    messages,
    renderer,
    scrnsize,
    option_empty1,
    mousesens,
    option_empty2,
    soundvol,
    opt_end
} options_e;

menuitem_t OptionsMenu[]=
{
    {1,"M_ENDGAM",	M_EndGame,'e'},
    {1,"M_MESSG",	M_ChangeMessages,'m'},
    {1,"",		M_RendererMenu,'r'},
    {2,"M_SCRNSZ",	M_SizeDisplay,'s'},
    {-1,"",0},
    {2,"M_MSENS",	M_ChangeSensitivity,'m'},
    {-1,"",0},
    {1,"M_SVOL",	M_Sound,'s'}
};

menu_t  OptionsDef =
{
    opt_end,
    &MainDef,
    OptionsMenu,
    M_DrawOptions,
    60,37,
    0
};

//
// GAME SELECT (DOOM-0060): choose DOOM 1 or DOOM 2 when both are installed.
// Shown at boot over the title screen, and re-entered via the main-menu "Game
// Select" item. The two rows have no menu-art lumps, so M_DrawGameSelect writes
// them as text. See docs/specs/DOOM-0060-game-select.md.
//
enum
{
    gs_doom,        // DOOM 1
    gs_doom2,       // DOOM 2
    gs_end
} gameselect_e;

menuitem_t GameSelectMenu[]=
{
    {1,"",M_GameSelectChoose,'d'},
    {1,"",M_GameSelectChoose,'i'}
};

menu_t  GameSelectDef =
{
    gs_end,
    &MainDef,           // Back/Esc falls out to the main menu (or closes at boot)
    GameSelectMenu,
    M_DrawGameSelect,
    100,80,
    gs_doom
};

//
// RENDERER SUB-MENU (DOOM-0008/0009): the 3D back-end + path-tracer settings,
// grouped off the Options "Renderer" row so the upscaler controls have room (the
// main Options menu is full at 320x200). Back returns to Options.
//
enum
{
    rm_renderer,
    rm_upscaler,
    rm_renderscale,
    rm_debugviews,
    rm_widescreen,
    rm_fillstretch,
    rm_effects,
    rm_fps,		// DOOM-0206 (v2): FPS counter moved here from Options (Classic only)
    rm_brightness,
    rm_end
} renderer_e;

menuitem_t RendererMenu[]=
{
    {1,"",	M_ChangeRenderer,'r'},
    {1,"",	M_ChangeUpscaler,'u'},
    {1,"",	M_ChangeRenderScale,'c'},
    {1,"",	M_ChangeDebugViews,'d'},
    {1,"",	M_ChangeWidescreen,'w'},
    {1,"",	M_ChangeFillScreen,'f'},
    {1,"",	M_UltraEffects,'e'},
    {1,"",	M_ChangeFPS,'p'},
    {2,"",	M_ChangeBrightness,'b'}
};

menu_t  RendererDef =
{
    rm_end,
    &OptionsDef,
    RendererMenu,
    M_DrawRendererMenu,
    60,60,
    0
};

//
// DOOM-0205: RENDER EFFECTS SUB-MENU. One place that shows the live on/off (or
// value) of every render toggle that was otherwise only reachable by an unlabelled
// hotkey (F flashlight, [ filth, ] de-tile, ' wet, ...), so the state is visible
// and screenshot-able. Reached from the Renderer menu's "Ultra Effects" row; back
// returns there. Each row's variable is the same one bound in m_misc.c defaults[],
// so a change persists to ~/.doomrc on exit like every other menu setting.
//
enum
{
    ef_flashlight,
    ef_ssao,
    ef_detile,
    ef_filth,
    ef_wet,
    ef_profiler,
    ef_end
} effects_e;

menuitem_t EffectsMenu[]=
{
    {1,"",	M_ChangeFlashlight,'f'},
    {1,"",	M_ChangeSSAO,'s'},
    {1,"",	M_ChangeDetile,'d'},
    {1,"",	M_ChangeFilth,'g'},
    {1,"",	M_ChangeWet,'w'},
    {1,"",	M_ChangeProfiler,'p'}
};

menu_t  EffectsDef =
{
    ef_end,
    &RendererDef,
    EffectsMenu,
    M_DrawEffectsMenu,
    60,50,
    0
};

//
// DOOM-0206 (L3): the consolidated crisp VIDEO menu for the 3D tiers (Solid/Ultra).
// A single column (the DOOM cursor is one-dimensional) grouping every render toggle
// plus the new Ray Tracing row, drawn crisp (display-pixel font) by M_DrawVideoMenu —
// which owns the dim, all rows, values, headings AND the skull, so M_Drawer skips its
// generic patch/skull loop for this menu (M_MenuIsCrisp). Reuses the DOOM-0205
// M_Change* handlers, so menu / hotkey / ~/.doomrc stay in lockstep. Entry is
// tier-conditional (M_RendererMenu routes Classic -> RendererDef, 3D -> VideoDef);
// INV-1 keeps VideoDef off the Classic tier. Rows follow spec §4.5's mockup order.
// Spacer rows are status -1 (skipped by cursor movement); Back is status 1.
//
enum
{
    vid_renderer,       // tier selector (Classic/Solid/Ultra) — re-routes on Classic<->3D
    vid_raytracing,     // NEW: RT on/off within the 3D tiers
    vid_upscaler,
    vid_renderscale,
    vid_brightness,     // status 2 slider
    vid_eff_head,       // "— Effects —" spacer
    vid_flashlight,
    vid_ssao,
    vid_detile,
    vid_filth,          // "Dirt & Grime"
    vid_wet,            // "Wet Liquid"
    vid_disp_head,      // "— Display —" spacer
    vid_widescreen,
    vid_fillscreen,
    vid_fps,            // "FPS Counter"
    vid_dev_head,       // "— Developer —" spacer
    vid_debugviews,
    vid_profiler,
    vid_back,
    vid_end
} videoitem_e;

menuitem_t VideoMenu[]=
{
    {1,"",	M_ChangeRenderer,'r'},
    {1,"",	M_ChangeRayTracing,'t'},
    {1,"",	M_ChangeUpscaler,'u'},
    {1,"",	M_ChangeRenderScale,'c'},
    {2,"",	M_ChangeBrightness,'b'},
    {-1,"",0},
    {1,"",	M_ChangeFlashlight,'f'},
    {1,"",	M_ChangeSSAO,'s'},
    {1,"",	M_ChangeDetile,'d'},
    {1,"",	M_ChangeFilth,'g'},
    {1,"",	M_ChangeWet,'w'},
    {-1,"",0},
    {1,"",	M_ChangeWidescreen,'i'},
    {1,"",	M_ChangeFillScreen,'l'},
    {1,"",	M_ChangeFPS,'p'},
    {-1,"",0},
    {1,"",	M_ChangeDebugViews,'v'},
    {1,"",	M_ChangeProfiler,'o'},
    {1,"",	M_VideoBack,'k'}
};

menu_t  VideoDef =
{
    vid_end,
    &OptionsDef,
    VideoMenu,
    M_DrawVideoMenu,
    60,40,
    0
};

//
// Read This! MENU 1 & 2
//
enum
{
    rdthsempty1,
    read1_end
} read_e;

menuitem_t ReadMenu1[] =
{
    {1,"",M_ReadThis2,0}
};

menu_t  ReadDef1 =
{
    read1_end,
    &MainDef,
    ReadMenu1,
    M_DrawReadThis1,
    280,185,
    0
};

enum
{
    rdthsempty2,
    read2_end
} read_e2;

menuitem_t ReadMenu2[]=
{
    {1,"",M_FinishReadThis,0}
};

menu_t  ReadDef2 =
{
    read2_end,
    &ReadDef1,
    ReadMenu2,
    M_DrawReadThis2,
    330,175,
    0
};

//
// SOUND VOLUME MENU
//
enum
{
    sfx_vol,
    sfx_empty1,
    music_vol,
    sfx_empty2,
    sound_end
} sound_e;

menuitem_t SoundMenu[]=
{
    {2,"M_SFXVOL",M_SfxVol,'s'},
    {-1,"",0},
    {2,"M_MUSVOL",M_MusicVol,'m'},
    {-1,"",0}
};

menu_t  SoundDef =
{
    sound_end,
    &OptionsDef,
    SoundMenu,
    M_DrawSound,
    80,64,
    0
};

//
// LOAD GAME MENU
//
enum
{
    load1,
    load2,
    load3,
    load4,
    load5,
    load6,
    load_end
} load_e;

menuitem_t LoadMenu[]=
{
    {1,"", M_LoadSelect,'1'},
    {1,"", M_LoadSelect,'2'},
    {1,"", M_LoadSelect,'3'},
    {1,"", M_LoadSelect,'4'},
    {1,"", M_LoadSelect,'5'},
    {1,"", M_LoadSelect,'6'}
};

menu_t  LoadDef =
{
    load_end,
    &MainDef,
    LoadMenu,
    M_DrawLoad,
    80,54,
    0
};

//
// SAVE GAME MENU
//
menuitem_t SaveMenu[]=
{
    {1,"", M_SaveSelect,'1'},
    {1,"", M_SaveSelect,'2'},
    {1,"", M_SaveSelect,'3'},
    {1,"", M_SaveSelect,'4'},
    {1,"", M_SaveSelect,'5'},
    {1,"", M_SaveSelect,'6'}
};

menu_t  SaveDef =
{
    load_end,
    &MainDef,
    SaveMenu,
    M_DrawSave,
    80,54,
    0
};


//
// M_ReadSaveStrings
//  read the strings from the savegame files
//
void M_ReadSaveStrings(void)
{
    int             handle;
    int             i;
    char    name[256];
	
    for (i = 0;i < load_end;i++)
    {
	if (M_CheckParm("-cdrom"))
	    sprintf(name,"c:\\doomdata\\"SAVEGAMENAME"%d.dsg",i);
	else
	    sprintf(name,SAVEGAMENAME"%d.dsg",i);

	handle = open (name, O_RDONLY | 0, 0666);
	if (handle == -1)
	{
	    strcpy(&savegamestrings[i][0],EMPTYSTRING);
	    LoadMenu[i].status = 0;
	    continue;
	}
	// DOOM-0254: a savegame is user data. A short read left the previous
	// slot's text in place, and a save string with no terminator ran into
	// the next entry on every strcpy/draw downstream.
	if (read (handle, &savegamestrings[i], SAVESTRINGSIZE) != SAVESTRINGSIZE)
	{
	    close (handle);
	    strcpy(&savegamestrings[i][0],EMPTYSTRING);
	    LoadMenu[i].status = 0;
	    continue;
	}
	close (handle);
	savegamestrings[i][SAVESTRINGSIZE-1] = 0;
	LoadMenu[i].status = 1;
    }
}


//
// M_LoadGame & Cie.
//
void M_DrawLoad(void)
{
    int             i;
	
    V_DrawPatchDirect (72,28,0,W_CacheLumpName("M_LOADG",PU_CACHE));
    for (i = 0;i < load_end; i++)
    {
	M_DrawSaveLoadBorder(LoadDef.x,LoadDef.y+LINEHEIGHT*i);
	M_WriteText(LoadDef.x,LoadDef.y+LINEHEIGHT*i,savegamestrings[i]);
    }
}



//
// Draw border for the savegame description
//
void M_DrawSaveLoadBorder(int x,int y)
{
    int             i;
	
    V_DrawPatchDirect (x-8,y+7,0,W_CacheLumpName("M_LSLEFT",PU_CACHE));
	
    for (i = 0;i < 24;i++)
    {
	V_DrawPatchDirect (x,y+7,0,W_CacheLumpName("M_LSCNTR",PU_CACHE));
	x += 8;
    }

    V_DrawPatchDirect (x,y+7,0,W_CacheLumpName("M_LSRGHT",PU_CACHE));
}



//
// User wants to load this game
//
void M_LoadSelect(int choice)
{
    char    name[256];
	
    if (M_CheckParm("-cdrom"))
	sprintf(name,"c:\\doomdata\\"SAVEGAMENAME"%d.dsg",choice);
    else
	sprintf(name,SAVEGAMENAME"%d.dsg",choice);
    G_LoadGame (name);
    M_ClearMenus ();
}

//
// Selected from DOOM menu
//
void M_LoadGame (int choice)
{
    if (netgame)
    {
	M_StartMessage(LOADNET,NULL,false);
	return;
    }
	
    M_SetupNextMenu(&LoadDef);
    M_ReadSaveStrings();
}


//
//  M_SaveGame & Cie.
//
void M_DrawSave(void)
{
    int             i;
	
    V_DrawPatchDirect (72,28,0,W_CacheLumpName("M_SAVEG",PU_CACHE));
    for (i = 0;i < load_end; i++)
    {
	M_DrawSaveLoadBorder(LoadDef.x,LoadDef.y+LINEHEIGHT*i);
	M_WriteText(LoadDef.x,LoadDef.y+LINEHEIGHT*i,savegamestrings[i]);
    }
	
    if (saveStringEnter)
    {
	i = M_StringWidth(savegamestrings[saveSlot]);
	M_WriteText(LoadDef.x + i,LoadDef.y+LINEHEIGHT*saveSlot,"_");
    }
}

//
// M_Responder calls this when user is finished
//
void M_DoSave(int slot)
{
    G_SaveGame (slot,savegamestrings[slot]);
    M_ClearMenus ();

    // PICK QUICKSAVE SLOT YET?
    if (quickSaveSlot == -2)
	quickSaveSlot = slot;
}

//
// User wants to save. Start string input for M_Responder
//
void M_SaveSelect(int choice)
{
    // we are going to be intercepting all chars
    saveStringEnter = 1;
    
    saveSlot = choice;
    strcpy(saveOldString,savegamestrings[choice]);
    if (!strcmp(savegamestrings[choice],EMPTYSTRING))
	savegamestrings[choice][0] = 0;
    saveCharIndex = strlen(savegamestrings[choice]);
}

//
// Selected from DOOM menu
//
void M_SaveGame (int choice)
{
    if (!usergame)
    {
	M_StartMessage(SAVEDEAD,NULL,false);
	return;
    }
	
    if (gamestate != GS_LEVEL)
	return;
	
    M_SetupNextMenu(&SaveDef);
    M_ReadSaveStrings();
}



//
//      M_QuickSave
//
char    tempstring[128];   // DOOM-0097: hold the longest quicksave/load prompt
                           // (~59 fixed chars + a 24-char savegame name = up to 83)

void M_QuickSaveResponse(int ch)
{
    if (ch == 'y')
    {
	M_DoSave(quickSaveSlot);
	S_StartSound(NULL,sfx_swtchx);
    }
}

void M_QuickSave(void)
{
    if (!usergame)
    {
	S_StartSound(NULL,sfx_oof);
	return;
    }

    if (gamestate != GS_LEVEL)
	return;
	
    if (quickSaveSlot < 0)
    {
	M_StartControlPanel();
	M_ReadSaveStrings();
	M_SetupNextMenu(&SaveDef);
	quickSaveSlot = -2;	// means to pick a slot now
	return;
    }
    snprintf(tempstring,sizeof(tempstring),QSPROMPT,savegamestrings[quickSaveSlot]);
    M_StartMessage(tempstring,M_QuickSaveResponse,true);
}



//
// M_QuickLoad
//
void M_QuickLoadResponse(int ch)
{
    if (ch == 'y')
    {
	M_LoadSelect(quickSaveSlot);
	S_StartSound(NULL,sfx_swtchx);
    }
}


void M_QuickLoad(void)
{
    if (netgame)
    {
	M_StartMessage(QLOADNET,NULL,false);
	return;
    }
	
    if (quickSaveSlot < 0)
    {
	M_StartMessage(QSAVESPOT,NULL,false);
	return;
    }
    snprintf(tempstring,sizeof(tempstring),QLPROMPT,savegamestrings[quickSaveSlot]);
    M_StartMessage(tempstring,M_QuickLoadResponse,true);
}




//
// Read This Menus
// Had a "quick hack to fix romero bug"
//
void M_DrawReadThis1(void)
{
    inhelpscreens = true;
    switch ( gamemode )
    {
      case commercial:
	V_DrawPatchDirect (0,0,0,W_CacheLumpName("HELP",PU_CACHE));
	break;
      case shareware:
      case registered:
      case retail:
	V_DrawPatchDirect (0,0,0,W_CacheLumpName("HELP1",PU_CACHE));
	break;
      default:
	break;
    }
    return;
}



//
// Read This Menus - optional second page.
//
void M_DrawReadThis2(void)
{
    inhelpscreens = true;
    switch ( gamemode )
    {
      case retail:
      case commercial:
	// This hack keeps us from having to change menus.
	V_DrawPatchDirect (0,0,0,W_CacheLumpName("CREDIT",PU_CACHE));
	break;
      case shareware:
      case registered:
	V_DrawPatchDirect (0,0,0,W_CacheLumpName("HELP2",PU_CACHE));
	break;
      default:
	break;
    }
    return;
}


//
// Change Sfx & Music volumes
//
void M_DrawSound(void)
{
    V_DrawPatchDirect (60,38,0,W_CacheLumpName("M_SVOL",PU_CACHE));

    M_DrawThermo(SoundDef.x,SoundDef.y+LINEHEIGHT*(sfx_vol+1),
		 16,snd_SfxVolume);

    M_DrawThermo(SoundDef.x,SoundDef.y+LINEHEIGHT*(music_vol+1),
		 16,snd_MusicVolume);
}

void M_Sound(int choice)
{
    M_SetupNextMenu(&SoundDef);
}

void M_SfxVol(int choice)
{
    switch(choice)
    {
      case 0:
	if (snd_SfxVolume)
	    snd_SfxVolume--;
	break;
      case 1:
	if (snd_SfxVolume < 15)
	    snd_SfxVolume++;
	break;
    }
	
    S_SetSfxVolume(snd_SfxVolume /* *8 */);
}

void M_MusicVol(int choice)
{
    switch(choice)
    {
      case 0:
	if (snd_MusicVolume)
	    snd_MusicVolume--;
	break;
      case 1:
	if (snd_MusicVolume < 15)
	    snd_MusicVolume++;
	break;
    }
	
    S_SetMusicVolume(snd_MusicVolume /* *8 */);
}




//
// M_DrawMainMenu
//
// Display-string labels for the main-menu rows, in item order. Also feeds the
// crisp 3D-tier registry (crispMenus[]). Sized main_end+1: MainDef gets a +1 for
// the spliced "Game Select" row (DOOM-0060), drawn only when M_Init bumps
// MainDef.numitems. M_Init keeps this in item order across the DOOM-II reshuffle.
static const char* mainLabels[main_end + 1] =
{
    "New Game", "Options", "Load Game", "Save Game", "Read This!", "Quit Game",
    "Game Select"
};

void M_DrawMainMenu(void)
{
    int i;

    V_DrawPatchDirect (94,2,0,W_CacheLumpName("M_DOOM",PU_CACHE));

    // DOOM-0206: draw every item in the red HUD font at one uniform "medium" size
    // (scale 2), rather than the mixed big-red graphic lumps + tiny "Game Select"
    // text. The generic patch loop in M_Drawer is suppressed for MainDef (like
    // OptionsDef) so the big-red lumps don't draw underneath. mainLabels[] tracks
    // the item order (incl. the DOOM-II reshuffle and the spliced Game Select row),
    // so this covers whichever rows are currently visible.
    for (i = 0; i < MainDef.numitems; i++)
	M_WriteTextScaled(MainDef.x, MainDef.y + i*LINEHEIGHT,
			  (char *)mainLabels[i], 2);
}




//
// M_NewGame
//
void M_DrawNewGame(void)
{
    V_DrawPatchDirect (96,14,0,W_CacheLumpName("M_NEWG",PU_CACHE));
    V_DrawPatchDirect (54,38,0,W_CacheLumpName("M_SKILL",PU_CACHE));
}

void M_NewGame(int choice)
{
    if (netgame && !demoplayback)
    {
	M_StartMessage(NEWGAME,NULL,false);
	return;
    }
	
    if ( gamemode == commercial )
	M_SetupNextMenu(&NewDef);
    else
	M_SetupNextMenu(&EpiDef);
}


//
//      M_Episode
//
int     epi;

void M_DrawEpisode(void)
{
    V_DrawPatchDirect (54,38,0,W_CacheLumpName("M_EPISOD",PU_CACHE));
}

void M_VerifyNightmare(int ch)
{
    if (ch != 'y')
	return;
		
    G_DeferedInitNew(nightmare,epi+1,1);
    M_ClearMenus ();
}

void M_ChooseSkill(int choice)
{
    if (choice == nightmare)
    {
	M_StartMessage(NIGHTMARE,M_VerifyNightmare,true);
	return;
    }
	
    G_DeferedInitNew(choice,epi+1,1);
    M_ClearMenus ();
}

void M_Episode(int choice)
{
    if ( (gamemode == shareware)
	 && choice)
    {
	M_StartMessage(SWSTRING,NULL,false);
	M_SetupNextMenu(&ReadDef1);
	return;
    }

    // Yet another hack...
    if ( (gamemode == registered)
	 && (choice > 2))
    {
      fprintf( stderr,
	       "M_Episode: 4th episode requires UltimateDOOM\n");
      choice = 0;
    }
	 
    epi = choice;
    M_SetupNextMenu(&NewDef);
}



//
// M_Options
//
// DOOM-0206 (L6 review fix): hu_font display strings for the Detail/Messages
// value columns (INV-7: value columns share the uniform row size too), drawn as
// text instead of the oversized big-red M_GD*/M_MSG* graphic lumps. Indices:
// showMessages 0=Off/1=On (the original patch truth). (detailValueNames removed
// with the Graphic Detail row -- DOOM-0206 v2 §4.6; detailLevel var itself stays.)
static const char* msgValueNames[2]	= {"Off","On"};
char	fpsPosNames[4][11]	= {"Off","Top-Left","Top-Centre","Top-Right"};
// Temporal upscaler (DOOM-0009 6-d): selector text + the render-scale presets.
// rb_upscaler / rb_renderscale live in r_vulkan.cpp (read by the path tracer).
extern int	rb_upscaler;
extern int	rb_renderscale;
extern int	rb_exposure;
extern int	rb_rtdebug_menu;	// DOOM-0135 Debug Views toggle
char	upscalerNames[2][8]	= {"Off","TAAU"};
int	renderScalePresets[4]	= {100,75,67,50};
char	renderScaleNames[4][6]	= {"100%","75%","67%","50%"};
// DOOM-0205: render-effect toggles surfaced in the Render Effects submenu. Each is the
// same var bound in m_misc.c defaults[] (so a menu change persists to ~/.doomrc).
extern int	rb_flashlight;		// F key      (rt config: flashlight)
extern int	rb_ssao;		// SSAO       (rt config: ssao)
extern int	rb_detile;		// ] key 0/1/2 (rt config: rt_detile)
extern int	rb_filth;		// [ key      (rt config: rt_filth)
extern int	rb_wet;			// ' key      (rt config: rt_wet)
extern int	rb_profile;		// profiler   (rt config: rt_profile)
char	detileNames[3][7]	= {"Off","2-tap","4-tap"};
// DOOM-0206 (L1b/L2): menu-text batch API + HUD-safe bound, in r_vulkan.cpp.
extern int	rb_menu_text_active;		// gates the text/dim flush in the present path
extern void	rb_menu_dim(void);		// queues the play-view dim quad (0..rb_menu_safe_bottom())
extern int	rb_menu_safe_bottom(void);	// display-pixel Y below which nothing may draw (INV-2)
extern void	rb_text_begin(void);		// clears the queued-quad vector (call first, once/frame)
// DOOM-0206 (L3): the rest of the crisp-text API the Video menu draws with — all display pixels.
extern void	rb_text_draw(const char* s, int x, int y, float scale, unsigned rgba);
extern int	rb_text_width(const char* s, float scale);
extern int	rb_text_line_height(float scale);
extern void	rb_menu_fill(int x, int y, int w, int h, unsigned rgba);   // solid quad (slider bar)
// DOOM-0206 (v2): the crisp menu cursor (original skull sprite baked into the atlas).
extern int	rb_menu_cursor_ready(void);		// 1 if the crisp skull baked (else fall back)
extern int	rb_menu_cursor_width(int h);		// drawn width for a target height (aspect-kept)
extern void	rb_menu_draw_cursor(int x, int y, int h);   // draws the DOOM-coloured skull
// DOOM-0206: the real M_DOOM logo lump drawn bright in the crisp layer (main menu only).
extern int	rb_menu_logo_ready(void);		// 1 if the M_DOOM logo baked (else fall back to text)
extern int	rb_menu_logo_width(int h);		// drawn width for a target height (aspect-kept)
extern void	rb_menu_draw_logo(int x, int y, int h);     // draws the M_DOOM logo bright
extern int	rb_display_width(void);
extern int	rb_display_height(void);
extern int	rb_rtdebug;			// r_vulkan.cpp: RT view/debug mode (6 = RT on, 0 = off)

// DOOM-0206 v2: decode the real menu skull (M_SKULL1) to a brightened RGBA buffer for
// the crisp cursor. Cached; returns NULL on failure. w*h*4 bytes, straight alpha (0 =
// transparent where the patch has no post). r_vulkan uploads it as the cursor texture.
// Plain C (this TU is compiled as C); r_vulkan.cpp declares it extern "C" to link.
// Column walk mirrors V_DrawPatch (v_video.c): post source = column+3, next post =
// column + column->length + 4.
// Decode a WAD patch lump to a straight-alpha RGBA buffer via PLAYPAL, optionally
// brightening every colour by brightPct/100 (clamped). brightPct=100 = no brighten
// (the lump keeps its own colours). Caller owns the returned malloc'd buffer.
// Returns NULL on failure. Column walk mirrors V_DrawPatch (v_video.c).
static unsigned char* M_DecodePatchRGBA(const char* lump, int brightPct, int* out_w, int* out_h)
{
    patch_t* p = (patch_t*)W_CacheLumpName((char*)lump, PU_CACHE);   // legacy API takes char*
    if (!p) return NULL;
    int w = SHORT(p->width), h = SHORT(p->height);
    if (w <= 0 || h <= 0) return NULL;
    const byte* pal = (const byte*)W_CacheLumpName("PLAYPAL", PU_CACHE);
    if (!pal) return NULL;
    unsigned char* rgba = (unsigned char*)calloc((size_t)w * h, 4);   // transparent
    if (!rgba) return NULL;
    for (int x = 0; x < w; x++)
    {
	const column_t* col = (const column_t*)((const byte*)p + LONG(p->columnofs[x]));
	while (col->topdelta != 0xff)
	{
	    const byte* src = (const byte*)col + 3;
	    for (int i = 0; i < col->length; i++)
	    {
		int y = col->topdelta + i;
		if (y < 0 || y >= h) continue;
		int idx = src[i];
		unsigned char* d = rgba + ((size_t)y * w + x) * 4;
		for (int k = 0; k < 3; k++)
		{
		    int v = pal[idx * 3 + k] * brightPct / 100;
		    d[k] = (unsigned char)(v > 255 ? 255 : v);
		}
		d[3] = 255;
	    }
	    col = (const column_t*)((const byte*)col + col->length + 4);
	}
    }
    *out_w = w; *out_h = h;
    return rgba;
}

const unsigned char* M_CursorSkullRGBA(int* out_w, int* out_h)
{
    static unsigned char* cache = NULL;
    static int cw = 0, ch = 0;
    if (cache) { *out_w = cw; *out_h = ch; return cache; }
    // Brighten ~1.35x so the skull reads bright over the dim menu.
    unsigned char* rgba = M_DecodePatchRGBA("M_SKULL1", 135, &cw, &ch);
    if (!rgba) return NULL;
    cache = rgba;
    *out_w = cw; *out_h = ch;
    return cache;
}

// DOOM-0206: decode the real M_DOOM logo lump to RGBA for the crisp main-menu title.
// brightPct=100 — the logo already carries its own DOOM colours, so no brighten.
// Cached like the skull; r_vulkan uploads it as a second RGBA menu sprite.
const unsigned char* M_MenuLogoRGBA(int* out_w, int* out_h)
{
    static unsigned char* cache = NULL;
    static int cw = 0, ch = 0;
    if (cache) { *out_w = cw; *out_h = ch; return cache; }
    unsigned char* rgba = M_DecodePatchRGBA("M_DOOM", 100, &cw, &ch);
    if (!rgba) return NULL;
    cache = rgba;
    *out_w = cw; *out_h = ch;
    return cache;
}


// DOOM-0206 (L6): Options row labels as hu_font display strings (INV-7: one
// uniform row size). Under Classic these replace the oversized big-red graphic-
// lump labels (M_ENDGAM, M_MESSG, M_SCRNSZ, M_MSENS, M_SVOL) the generic patch
// loop would draw -- that loop is suppressed for OptionsDef in M_Drawer. Under
// the 3D tiers the same table feeds the crisp renderer (M_DrawCrispMenu). Spacer
// rows (option_empty*) carry "". menuitem_t.name holds the lump name, not a
// display string, so the strings must live here. "Video" (renderer row) shows the
// active tier name beside it; its value is supplied by the value provider / the
// classic value draw below.
static const char* optionsLabels[opt_end] =
{
    "End Game",           // endgame
    "Messages",           // messages
    "Video",              // renderer   (value = active tier name)
    "Screen Size",        // scrnsize   (status 2: thermo on the row below)
    "",                   // option_empty1
    "Mouse Sensitivity",  // mousesens  (status 2: thermo on the row below)
    "",                   // option_empty2
    "Sound Volume"        // soundvol
};

void M_DrawOptions(void)
{
    int i;

    // Title banner kept as art (INV-7 exempt).
    V_DrawPatchDirect (108,15,0,W_CacheLumpName("M_OPTTTL",PU_CACHE));

    // Uniform hu_font row labels (INV-7) -- one size for every Options row.
    for (i = 0 ; i < opt_end ; i++)
	if (optionsLabels[i][0])
	    M_WriteText(OptionsDef.x,OptionsDef.y+LINEHEIGHT*i,(char *)optionsLabels[i]);

    // DOOM-0206 (L6 review fix): value columns as hu_font text, not big-red
    // patches (INV-7 uniform row size).
    M_WriteText(OptionsDef.x + 120,OptionsDef.y+LINEHEIGHT*messages,
		(char *)msgValueNames[(showMessages >= 0 && showMessages <= 1) ? showMessages : 0]);

    // DOOM-0206 (L3): the "Video" row (label from optionsLabels[renderer]) opens
    // the crisp VideoDef in the 3D tiers, the classic RendererDef in Classic
    // (M_RendererMenu branches on rendermode). The value shows the active tier.
    // (v2 §4.6: the FPS row that used to sit here was removed -- it now lives in
    // the Video menu / RendererDef only.)
    M_WriteText(OptionsDef.x + 88,OptionsDef.y+LINEHEIGHT*renderer,
		(char *)RB_ModeName(rendermode));

    M_DrawThermo(OptionsDef.x,OptionsDef.y+LINEHEIGHT*(mousesens+1),
		 10,mouseSensitivity);
	
    M_DrawThermo(OptionsDef.x,OptionsDef.y+LINEHEIGHT*(scrnsize+1),
		 8,screenSize);
}

void M_Options(int choice)
{
    M_SetupNextMenu(&OptionsDef);
}

//
// Renderer sub-menu: the 3D back-end + path-tracer upscaler (DOOM-0009 6-d).
//
void M_RendererMenu(int choice)
{
    choice = 0;
    // DOOM-0206 (L3): tier-conditional entry. Classic keeps the original bitmap
    // RendererDef (INV-1: VideoDef is never shown under RB_CLASSIC); the 3D tiers
    // get the consolidated crisp VideoDef.
    M_SetupNextMenu(rendermode==RB_CLASSIC ? &RendererDef : &VideoDef);
}

void M_DrawRendererMenu(void)
{
    int rsi, k;

    // DOOM-0206 (v2): no standalone "RENDERER" header. With the added FPS row this
    // Classic menu is 9 rows tall (Brightness's thermo occupies a 10th), and a header
    // drawn 20px ABOVE RendererDef.y would push the total above the in-level HUD-safe
    // band (M_ClassicMenuShift shifts the rows up to clear ST_Y=168, which would then
    // clip the header off the top). The menu is reached via the labelled "Video" row,
    // so the header is redundant -- dropping it keeps every row HUD-safe (INV-2).

    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_renderer,"Renderer:");
    M_WriteText(RendererDef.x + 120,RendererDef.y+LINEHEIGHT*rm_renderer,
		(char *)RB_ModeName(rendermode));

    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_upscaler,"Upscaler:");
    M_WriteText(RendererDef.x + 120,RendererDef.y+LINEHEIGHT*rm_upscaler,
		upscalerNames[(rb_upscaler==1)?1:0]);

    rsi = 0;
    for (k=0 ; k<4 ; k++) if (renderScalePresets[k]==rb_renderscale) rsi=k;
    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_renderscale,"Render Scale:");
    M_WriteText(RendererDef.x + 120,RendererDef.y+LINEHEIGHT*rm_renderscale,
		renderScaleNames[rsi]);

    // DOOM-0135: Debug Views toggle. Off = ~ is a plain RT on/off switch; On = ~
    // cycles the path-tracer diagnostic views (HITS/normals, white furnace, ...).
    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_debugviews,"Debug Views:");
    M_WriteText(RendererDef.x + 120,RendererDef.y+LINEHEIGHT*rm_debugviews,
		rb_rtdebug_menu ? "On" : "Off");

    // DOOM-0147 Part C: Widescreen on/off (sizes SCREENWIDTH at startup, so the
    // value carries a "(restart)" tag) + Fill Screen on/off (live present stretch).
    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_widescreen,"Widescreen:");
    M_WriteText(RendererDef.x + 120,RendererDef.y+LINEHEIGHT*rm_widescreen,
		widescreen ? "On (restart)" : "Off (restart)");

    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_fillstretch,"Fill Screen:");
    M_WriteText(RendererDef.x + 120,RendererDef.y+LINEHEIGHT*rm_fillstretch,
		fillstretch ? "On" : "Off");

    // DOOM-0205: entry to the Render Effects submenu (flashlight, SSAO, de-tile,
    // filth, wet, profiler — each with a visible on/off).
    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_effects,"Render Effects...");

    // DOOM-0206 (v2 §4.6): FPS counter -- de-duplicated out of the Options menu.
    // The 3D tiers reach it via the Video menu; Classic reaches it here (the only
    // FPS access point once it left Options). Same fpsCorner var / M_ChangeFPS.
    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_fps,"FPS Counter:");
    M_WriteText(RendererDef.x + 120,RendererDef.y+LINEHEIGHT*rm_fps,
		fpsPosNames[(fpsCorner >= 0 && fpsCorner <= 3) ? fpsCorner : 0]);

    // DOOM-0096: Ultra/denoiser brightness. Label on its own line with a thermometer
    // slider below it (rb_exposure 0..15), the standard DOOM slider layout.
    M_WriteText(RendererDef.x,RendererDef.y+LINEHEIGHT*rm_brightness,"Brightness:");
    M_DrawThermo(RendererDef.x,RendererDef.y+LINEHEIGHT*(rm_brightness+1),
		16,rb_exposure);
}

//
// DOOM-0205: draw the Render Effects submenu — one row per toggle, label on the
// left and its live state (On/Off, or the de-tile quality name) on the right, so a
// glance (or a screenshot) shows exactly what is enabled. Values mirror the same
// vars the hotkeys ([ ] ' F ...) flip.
//
void M_DrawEffectsMenu(void)
{
    M_WriteText(EffectsDef.x + 30,EffectsDef.y - 20,"RENDER EFFECTS");

    M_WriteText(EffectsDef.x,EffectsDef.y+LINEHEIGHT*ef_flashlight,"Flashlight:");
    M_WriteText(EffectsDef.x + 130,EffectsDef.y+LINEHEIGHT*ef_flashlight,
		rb_flashlight ? "On" : "Off");

    M_WriteText(EffectsDef.x,EffectsDef.y+LINEHEIGHT*ef_ssao,"SSAO:");
    M_WriteText(EffectsDef.x + 130,EffectsDef.y+LINEHEIGHT*ef_ssao,
		rb_ssao ? "On" : "Off");

    M_WriteText(EffectsDef.x,EffectsDef.y+LINEHEIGHT*ef_detile,"De-tile:");
    M_WriteText(EffectsDef.x + 130,EffectsDef.y+LINEHEIGHT*ef_detile,
		detileNames[(rb_detile>=0 && rb_detile<=2) ? rb_detile : 0]);

    M_WriteText(EffectsDef.x,EffectsDef.y+LINEHEIGHT*ef_filth,"Filth/grime:");
    M_WriteText(EffectsDef.x + 130,EffectsDef.y+LINEHEIGHT*ef_filth,
		rb_filth ? "On" : "Off");

    M_WriteText(EffectsDef.x,EffectsDef.y+LINEHEIGHT*ef_wet,"Wet liquid:");
    M_WriteText(EffectsDef.x + 130,EffectsDef.y+LINEHEIGHT*ef_wet,
		rb_wet ? "On" : "Off");

    M_WriteText(EffectsDef.x,EffectsDef.y+LINEHEIGHT*ef_profiler,"Profiler:");
    M_WriteText(EffectsDef.x + 130,EffectsDef.y+LINEHEIGHT*ef_profiler,
		rb_profile ? "On" : "Off");
}

//
// DOOM-0206 (L3): draw the consolidated crisp Video menu — display-pixel font (rb_text_*),
// ONE glyph size for every row (INV-7; headings get CAPS + colour, never a bigger size).
// Self-contained: queues the dim backdrop, the title, every label + right-aligned value, the
// Brightness slider bar, and the skull cursor. M_Drawer routes here via M_MenuIsCrisp and then
// skips its generic patch/skull loop. The font is a fixed size (INV-7); when the list is taller
// than the HUD-safe band an itemOn-derived scroll window with up/down arrows keeps the selection
// visible and every element above the bound (INV-2), see the scroll block below (L4/L5).
//
static const char* videoLabels[vid_end] =
{
    "Renderer", "Ray Tracing", "Upscaler", "Render Scale", "Brightness",
    "-  EFFECTS  -",
    "Flashlight", "SSAO", "De-tile", "Dirt & Grime", "Wet Liquid",
    "-  DISPLAY  -",
    "Widescreen", "Fill Screen", "FPS Counter",
    "-  DEVELOPER  -",
    "Debug Views", "Profiler",
    "Back"
};

// DOOM-0206 (v2 §4.6): crisp display-string labels for the row-list menus that
// become crisp under the 3D tiers. menuitem_t.name holds a graphic-lump name, not
// a display string, so the crisp renderer reads its text from these tables.
// (optionsLabels[] and videoLabels[] above serve the same role for their menus.)
// Sized to their menu's item count. mainLabels[] lives further up (it also feeds
// the Classic main menu's uniform-font draw in M_DrawMainMenu).
static const char* episodeLabels[ep_end] =
{
    "Knee-Deep in the Dead", "The Shores of Hell", "Inferno", "Thy Flesh Consumed"
};
static const char* skillLabels[newg_end] =
{
    "I'm too young to die.", "Hey, not too rough.", "Hurt me plenty.",
    "Ultra-Violence.", "Nightmare!"
};
static const char* soundLabels[sound_end] =
{
    "Sound Volume", "", "Music Volume", ""
};

// DOOM-0206 (v2 §4.6): the crisp renderer is generic (M_DrawCrispMenu) and drives
// every row-list menu in the 3D tiers from a registry. Each row's VALUE (right-
// aligned string, a slider fraction, or "centre this label, no value") comes from
// a per-menu value provider; a NULL provider means a label-only menu (main /
// episode / skill) whose rows are plain left-aligned labels.
typedef struct
{
    const char* str;       // right-aligned value string ("" => none)
    int         slider;    // nonzero => draw a fill bar instead of a value string
    int         num, den;  // slider fill fraction num/den (den>=1)
    int         centered;  // nonzero => draw the label centred, no value (e.g. "Back")
    unsigned    valcol;    // 0 => default value colour; else an RGBA override (greyed RT)
} crispval_t;

typedef void (*crispValFn)(int i, crispval_t* cv);

static void M_VideoCrispValue(int i, crispval_t* cv)
{
    int rsi, k;
    switch (i)
    {
      case vid_renderer:   cv->str = (const char*)RB_ModeName(rendermode); break;
      case vid_raytracing:
	cv->str = (rb_rtdebug == 6) ? "On" : "Off";
	if (rb_rtdebug_menu) cv->valcol = 0x707070FFu;   // greyed while Debug Views owns rb_rtdebug
	break;
      case vid_upscaler:   cv->str = upscalerNames[(rb_upscaler == 1) ? 1 : 0]; break;
      case vid_renderscale:
	rsi = 0;
	for (k = 0 ; k < 4 ; k++)
	    if (renderScalePresets[k] == rb_renderscale) rsi = k;
	cv->str = renderScaleNames[rsi]; break;
      case vid_brightness: cv->slider = 1; cv->num = rb_exposure; cv->den = 15; break;
      case vid_flashlight: cv->str = rb_flashlight ? "On" : "Off"; break;
      case vid_ssao:       cv->str = rb_ssao ? "On" : "Off"; break;
      case vid_detile:
	cv->str = detileNames[(rb_detile >= 0 && rb_detile <= 2) ? rb_detile : 0]; break;
      case vid_filth:      cv->str = rb_filth ? "On" : "Off"; break;
      case vid_wet:        cv->str = rb_wet ? "On" : "Off"; break;
      case vid_widescreen: cv->str = widescreen ? "On (restart)" : "Off (restart)"; break;
      case vid_fillscreen: cv->str = fillstretch ? "On" : "Off"; break;
      case vid_fps:        cv->str = fpsPosNames[(fpsCorner >= 0 && fpsCorner <= 3) ? fpsCorner : 0]; break;
      case vid_debugviews: cv->str = rb_rtdebug_menu ? "On" : "Off"; break;
      case vid_profiler:   cv->str = rb_profile ? "On" : "Off"; break;
      case vid_back:       cv->centered = 1; break;
      default: break;
    }
}

static void M_OptionsCrispValue(int i, crispval_t* cv)
{
    switch (i)
    {
      case messages:  cv->str = msgValueNames[(showMessages >= 0 && showMessages <= 1) ? showMessages : 0]; break;
      case renderer:  cv->str = (const char*)RB_ModeName(rendermode); break;
      // Sliders share the classic thermo scales: Screen Size 0..7 (M_SizeDisplay),
      // Mouse Sensitivity 0..9 (thermWidth 10). See the classic M_DrawThermo calls.
      case scrnsize:  cv->slider = 1; cv->num = screenSize;       cv->den = 7; break;
      case mousesens: cv->slider = 1; cv->num = mouseSensitivity; cv->den = 9; break;
      default: break;   // endgame, soundvol = action rows (no value); spacers handled by st==-1
    }
}

static void M_SoundCrispValue(int i, crispval_t* cv)
{
    switch (i)
    {
      case sfx_vol:   cv->slider = 1; cv->num = snd_SfxVolume;   cv->den = 15; break;
      case music_vol: cv->slider = 1; cv->num = snd_MusicVolume; cv->den = 15; break;
      default: break;   // spacers handled by st==-1
    }
}

typedef struct
{
    menu_t*      menu;
    const char*  title;
    const char** labels;
    crispValFn   valueFn;   // NULL => label-only menu
} crispmenu_t;

// The registry IS the crisp whitelist: only menus listed here draw crisp under a
// 3D tier (M_MenuIsCrisp == membership). Everything else — the bespoke fullscreen
// help screens (ReadDef1/2), the DOOM 1-vs-2 chooser (GameSelectDef), and (for now)
// Load/Save with their editable slots — falls through to the classic bitmap path,
// exactly as under Classic. RendererDef/EffectsDef are never current in 3D
// (M_RendererMenu routes 3D -> VideoDef), so they need no entry.
static const crispmenu_t crispMenus[] =
{
    { &MainDef,    "DOOM",           mainLabels,    NULL },
    { &EpiDef,     "WHICH EPISODE?", episodeLabels, NULL },
    { &NewDef,     "CHOOSE SKILL",   skillLabels,   NULL },
    { &OptionsDef, "OPTIONS",        optionsLabels, M_OptionsCrispValue },
    { &SoundDef,   "SOUND VOLUME",   soundLabels,   M_SoundCrispValue },
    { &VideoDef,   "VIDEO",          videoLabels,   M_VideoCrispValue }
};

static const crispmenu_t* M_FindCrispMenu(menu_t* m)
{
    unsigned k;
    for (k = 0 ; k < sizeof(crispMenus) / sizeof(crispMenus[0]) ; k++)
	if (crispMenus[k].menu == m)
	    return &crispMenus[k];
    return NULL;
}

//
// DOOM-0206 (v2 §4.6): draw ANY registered row-list menu crisp — display-pixel font
// (rb_text_*), ONE glyph size for every row (INV-7; headings get CAPS + colour, never a
// bigger size). Generalised from the L3 Video-menu draw: the title, labels and per-row
// values now come from the registry (M_FindCrispMenu) rather than being hard-coded to
// VideoDef. Self-contained: queues the dim backdrop, the title, every label + right-aligned
// value (or slider bar), and the skull cursor. M_Drawer routes here via M_MenuIsCrisp and
// skips its generic patch/skull loop. The font is a fixed size (INV-7); when the list is
// taller than the HUD-safe band an itemOn-derived scroll window with up/down arrows keeps the
// selection visible and every element above the bound (INV-2), see the scroll block (L4/L5).
//
static void M_DrawCrispMenu(menu_t* menu)
{
    const unsigned labelCol = 0xE0E0E0FFu;
    const unsigned valCol   = 0xB0B0B0FFu;
    const unsigned selCol   = 0xFFF060FFu;   // highlight the cursor row (alongside the skull)
    const unsigned headCol  = 0xE0A040FFu;   // section headings (CAPS + gold, same size)

    const crispmenu_t* d = M_FindCrispMenu(menu);
    int dispW  = rb_display_width();
    int dispH  = rb_display_height();
    int bottom = rb_menu_safe_bottom();
    int nItems = menu->numitems;
    int rowH, rowStride, rowsTopY, titleY, marginX, valRightX, colX, labelOnly;
    int availH, maxRows, winRows, scrollTop, arrowRoom, v;
    float scale = 1.7f;   // INV-7: one comfortable, fixed glyph size for the whole menu

    // Menu backdrop (dim to the HUD-safe bound). Nothing else draws if the font never baked
    // or the menu isn't registered (defensive; M_MenuIsCrisp only lets registered menus here).
    rb_menu_dim();
    rowH = rb_text_line_height(scale);
    if (rowH < 1 || !d)
	return;

    marginX   = dispW / 4;           // value menus: label column (narrower than v1 -> less "too wide")
    valRightX = dispW - dispW / 4;   // value menus: right-aligned value column
    titleY    = rowH;
    rowsTopY  = titleY + rowH * 2;   // title + one blank row of breathing space

    // A label-only menu (main / episode / skill — no value column) reads unbalanced
    // hugging the left margin, so centre its label column as a block: every row shares
    // the left edge that centres the WIDEST label (a clean aligned column sitting in the
    // screen centre, not ragged per-row centring). Menus with a value column keep the
    // left-label / right-value span (marginX .. valRightX), which already fills the width.
    labelOnly = (d->valueFn == NULL);
    colX = marginX;
    if (labelOnly)
    {
	int maxW = 0, j;
	for (j = 0 ; j < nItems ; j++)
	{
	    int w = rb_text_width(d->labels[j], scale);
	    if (w > maxW) maxW = w;
	}
	colX = (dispW - maxW) / 2;
	if (colX < 0) colX = 0;
    }

    // Row pitch. Label-only menus (few rows: main / episode / skill) get extra
    // vertical breathing space so rows aren't cramped and the skull cursor clears its
    // neighbours; value menus (e.g. the 19-row Video list) stay tight so the whole
    // list still fits above the HUD without scrolling.
    rowStride = labelOnly ? rowH + rowH / 2 : rowH;

    // itemOn-derived scroll window (INV-4: derived from itemOn only; no M_Responder change).
    // maxRows = rows that fit between the title and the HUD-safe bound. If the whole list fits,
    // draw it all with no arrows; else centre a window on the cursor and reserve the bottom slot
    // for the down-arrow (the up-arrow sits in the gap above the list, so it costs no row).
    //
    // DOOM-0206 L5 review fix: the window is live-resizable with no minimum height, so at
    // runtime `bottom` can land arbitrarily close to `rowsTopY` (rowH is baked at startup and
    // NOT re-measured). Budget the down-arrow's row BEFORE computing winRows, and only ever
    // draw it when a row was genuinely reserved for it — never force winRows up past what
    // maxRows can actually afford (that was the overflow: forcing a row+arrow into a space
    // that only fit one of them). maxRows==0 means not even one row fits above the HUD-safe
    // bound: draw nothing rather than overflow it.
    availH  = bottom - rowsTopY;
    maxRows = availH / rowStride;
    if (maxRows < 0) maxRows = 0;
    if (maxRows >= nItems)
    {
	scrollTop = 0;
	winRows   = nItems;
	arrowRoom = 0;
    }
    else
    {
	arrowRoom = (maxRows >= 2) ? 1 : 0;   // room for a row AND the arrow needs maxRows>=2
	winRows   = maxRows - arrowRoom;
	if (winRows > 0)
	{
	    scrollTop = itemOn - winRows / 2;
	    if (scrollTop > nItems - winRows) scrollTop = nItems - winRows;
	    if (scrollTop < 0) scrollTop = 0;
	}
	else
	{
	    scrollTop = 0;   // nothing fits (maxRows==0): draw no rows, no arrows
	}
    }

    // Title — centred, CAPS, same glyph size as the rows (INV-7). Replaces the bitmap
    // banner (M_DOOM/M_OPTTTL/...) in the 3D tiers, so there is no red-on-red overlap.
    // Main menu only: draw the real M_DOOM logo lump bright in the crisp layer instead
    // of the "DOOM" crisp text; every other menu keeps its crisp text title.
    if (menu == &MainDef && rb_menu_logo_ready())
    {
	int lh = rowH * 2;                         // logo occupies the title band (title..rowsTopY)
	int lw = rb_menu_logo_width(lh);
	rb_menu_draw_logo((dispW - lw) / 2, titleY, lh);
    }
    else
    {
	int tw = rb_text_width(d->title, scale);
	rb_text_draw(d->title, (dispW - tw) / 2, titleY, scale, labelCol);
    }

    // Up/down scroll arrows (same glyph size, INV-7), both inside the HUD-safe bound by
    // construction: up in the gap above the list, down in the reserved bottom slot.
    if (scrollTop > 0)
    {
	int aw = rb_text_width("^", scale);
	rb_text_draw("^", (dispW - aw) / 2, rowsTopY - rowH, scale, headCol);
    }
    if (arrowRoom && scrollTop + winRows < nItems)
    {
	int aw = rb_text_width("v", scale);
	rb_text_draw("v", (dispW - aw) / 2, rowsTopY + winRows * rowStride, scale, headCol);
    }

    for (v = 0 ; v < winRows ; v++)
    {
	int i   = scrollTop + v;
	int y   = rowsTopY + v * rowStride;
	int st  = menu->menuitems[i].status;
	int sel = (i == itemOn);
	const char* label = d->labels[i];
	unsigned lc = sel ? selCol : labelCol;
	crispval_t cv;

	cv.str = ""; cv.slider = 0; cv.num = 0; cv.den = 1; cv.centered = 0; cv.valcol = 0;
	if (d->valueFn)
	    d->valueFn(i, &cv);

	if (st == -1)
	{
	    // Section heading: CAPS + colour, no value (INV-7 keeps the size).
	    rb_text_draw(label, colX, y, scale, headCol);
	}
	else if (cv.centered)
	{
	    // Centred label, no value (e.g. the Video menu's "Back" row).
	    int w = rb_text_width(label, scale);
	    rb_text_draw(label, (dispW - w) / 2, y, scale, lc);
	}
	else if (cv.slider)
	{
	    // Slider: label left, a crisp bar filling with num/den (Brightness, Screen
	    // Size, Mouse Sensitivity, Sfx/Music Volume — each supplies its own scale).
	    int barW = dispW / 5;
	    int barX = valRightX - barW;
	    int barH = rowH / 3;
	    int barY, fillW, den = (cv.den < 1) ? 1 : cv.den;
	    if (barH < 2) barH = 2;
	    barY = y + (rowH - barH) / 2;
	    if (barY + barH > bottom) barY = bottom - barH;   // INV-2 clamp
	    rb_text_draw(label, colX, y, scale, lc);
	    rb_menu_fill(barX, barY, barW, barH, 0x303030FFu);            // track
	    fillW = barW * cv.num / den;
	    if (fillW < 0)    fillW = 0;
	    if (fillW > barW) fillW = barW;
	    if (fillW > 0)
		rb_menu_fill(barX, barY, fillW, barH, sel ? selCol : 0xC0C0C0FFu);
	}
	else
	{
	    // Label left + optional right-aligned value string. A label-only menu (NULL
	    // provider) leaves cv.str "" and just draws the left label.
	    rb_text_draw(label, colX, y, scale, lc);
	    if (cv.str && cv.str[0])
	    {
		unsigned vc = cv.valcol ? cv.valcol : valCol;
		int w = rb_text_width(cv.str, scale);
		rb_text_draw(cv.str, valRightX - w, y, scale, vc);
	    }
	}
    }

    // Skull cursor. The crisp rows are display-pixel; V_DrawPatchDirect draws in the 320x200
    // virtual canvas that the paletted 2D overlay maps to full display (same convention as
    // rb_menu_safe_bottom), so map the selected row's display-Y centre back to virtual Y (skull
    // art ~16 tall -> -8 to centre it) and its X to just left of the label column. Chunky
    // beside the crisp text -- the user asked to keep the skull, not to make it crisp. Under
    // scrolling the cursor's VISUAL row is (itemOn - scrollTop), which is inside [0, winRows)
    // because scrollTop is clamped from itemOn -- but only when winRows > 0. When winRows == 0
    // (maxRows==0: not even one row fits above the HUD-safe bound) no row was drawn for the
    // cursor to sit on, so the skull must not draw either (L5 review fix 2: it used to draw
    // unconditionally and could land well below the HUD-safe bound).
    if (winRows > 0)
    {
	int rowTopY = rowsTopY + (itemOn - scrollTop) * rowStride;

	if (rb_menu_cursor_ready())
	{
	    // Crisp skull cursor: display-res, bright, sized to one text row. Sit it fully
	    // left of the (maybe centred) label column with a small gap; the selCol tint ties
	    // it to the highlighted row. Drawn in the crisp layer (over the dim, hence bright).
	    int ch = rowH;
	    int cw = rb_menu_cursor_width(ch);
	    int cx = colX - cw - rowH / 3;
	    if (cx < 0) cx = 0;
	    rb_menu_draw_cursor(cx, rowTopY, ch);
	}
	else
	{
	    // Fallback (crisp cursor sprite didn't bake): the original paletted skull in the
	    // 320x200 overlay. Chunkier + dimmer, but keeps a cursor. Map the row centre back to
	    // the virtual canvas, centre by the skull's real height, sit it left of the column.
	    patch_t* skp = (patch_t*)W_CacheLumpName(skullName[whichSkull], PU_CACHE);
	    int rowCenter = rowTopY + rowH / 2;
	    int vy = (int)((double)rowCenter * (double)ORIGHEIGHT / (double)dispH) - SHORT(skp->height) / 2;
	    int vx = (int)((double)colX * (double)ORIGWIDTH / (double)dispW) - SHORT(skp->width) - 4;
	    V_DrawPatchDirect(vx, vy, 0, skp);
	}
    }
}

// VideoDef.routine still points here (defensive). The crisp path calls
// M_DrawCrispMenu(currentMenu) directly from M_Drawer, never routine(), so this
// forwarder only runs if VideoDef were ever drawn via the classic path — which it
// isn't (VideoDef is 3D-only). Kept so the routine pointer stays valid.
void M_DrawVideoMenu(void)
{
    M_DrawCrispMenu(&VideoDef);
}



//
//      Toggle messages on/off
//
void M_ChangeMessages(int choice)
{
    // warning: unused parameter `int choice'
    choice = 0;
    showMessages = 1 - showMessages;
	
    if (!showMessages)
	players[consoleplayer].message = MSGOFF;
    else
	players[consoleplayer].message = MSGON ;

    message_dontfuckwithme = true;
}


//
// M_EndGame
//
void M_EndGameResponse(int ch)
{
    if (ch != 'y')
	return;
		
    currentMenu->lastOn = itemOn;
    M_ClearMenus ();
    D_StartTitle ();
}

void M_EndGame(int choice)
{
    choice = 0;
    if (!usergame)
    {
	S_StartSound(NULL,sfx_oof);
	return;
    }
	
    if (netgame)
    {
	M_StartMessage(NETEND,NULL,false);
	return;
    }
	
    M_StartMessage(ENDGAME,M_EndGameResponse,true);
}


//
// M_GameSelect (DOOM-0060)
//
// GAMESWITCHPROMPT is only shown mid-game, where switching abandons progress
// (like End Game). PRESSYN comes from dstrings.h.
#define GAMESWITCHPROMPT	"end this game and switch\nto the other game?\n\n"PRESSYN

// The IWAD path to relaunch into once a mid-game switch is confirmed.
static const char*	gameSelectSwitchPath;

void M_GameSelectResponse(int ch)
{
    if (ch != 'y')
	return;
    D_RelaunchWithIwad(gameSelectSwitchPath);   // never returns (re-execs)
}

// choice: gs_doom (DOOM 1) or gs_doom2 (DOOM 2).
void M_GameSelectChoose(int choice)
{
    int	want   = (choice == gs_doom2) ? IWAD_DOOM2 : IWAD_DOOM1;
    int	loaded = (gamemode == commercial) ? IWAD_DOOM2 : IWAD_DOOM1;

    if (want == loaded)
    {
	// Already playing this game -> just dismiss the chooser and carry on.
	M_ClearMenus();
	return;
    }

    // Switching to the other game means a relaunch. Confirm only when a game is
    // in progress (there is progress to abandon); at boot / on the title screen
    // the pick itself is the confirmation.
    if (usergame)
    {
	gameSelectSwitchPath = D_IwadPathForFamily(want);
	M_StartMessage(GAMESWITCHPROMPT, M_GameSelectResponse, true);
    }
    else
    {
	D_RelaunchWithIwad(D_IwadPathForFamily(want));   // never returns
    }
}

void M_DrawGameSelect(void)
{
    M_WriteText(GameSelectDef.x - 28, GameSelectDef.y - 24, "SELECT GAME");
    M_WriteText(GameSelectDef.x, GameSelectDef.y + LINEHEIGHT*gs_doom,  "DOOM");
    M_WriteText(GameSelectDef.x, GameSelectDef.y + LINEHEIGHT*gs_doom2, "DOOM II");
}

// Open the chooser. Called at boot (menuactive still 0) and from the main-menu
// "Game Select" item (menuactive already 1); setting it again is harmless.
void M_OpenGameSelect(void)
{
    // DOOM-0060 remember-last-game: start the cursor on the currently-loaded game
    // (which is the last one played, since the engine defaults to it), so a single
    // select press just continues it.
    GameSelectDef.lastOn = (gamemode == commercial) ? gs_doom2 : gs_doom;
    menuactive = 1;
    M_SetupNextMenu(&GameSelectDef);
}

void M_ReturnToGameSelect(int choice)
{
    choice = 0;
    M_OpenGameSelect();
}


//
// M_ReadThis
//
void M_ReadThis(int choice)
{
    choice = 0;
    M_SetupNextMenu(&ReadDef1);
}

void M_ReadThis2(int choice)
{
    choice = 0;
    M_SetupNextMenu(&ReadDef2);
}

void M_FinishReadThis(int choice)
{
    choice = 0;
    M_SetupNextMenu(&MainDef);
}




//
// M_QuitDOOM
//
int     quitsounds[8] =
{
    sfx_pldeth,
    sfx_dmpain,
    sfx_popain,
    sfx_slop,
    sfx_telept,
    sfx_posit1,
    sfx_posit3,
    sfx_sgtatk
};

int     quitsounds2[8] =
{
    sfx_vilact,
    sfx_getpow,
    sfx_boscub,
    sfx_slop,
    sfx_skeswg,
    sfx_kntdth,
    sfx_bspact,
    sfx_sgtatk
};



void M_QuitResponse(int ch)
{
    if (ch != 'y')
	return;
    if (!netgame)
    {
	if (gamemode == commercial)
	    S_StartSound(NULL,quitsounds2[(gametic>>2)&7]);
	else
	    S_StartSound(NULL,quitsounds[(gametic>>2)&7]);
	I_WaitVBL(105);
    }
    I_Quit ();
}




void M_QuitDOOM(int choice)
{
  // We pick index 0 which is language sensitive,
  //  or one at random, between 1 and maximum number.
  char* msg = (language != english)
      ? endmsg[0]
      : endmsg[ (gametic%(NUM_QUITMESSAGES-2))+1 ];

  // DOOM-0161: when a gamepad is connected, add a second prompt line naming its
  // confirm button (A on Xbox, CROSS on PlayStation, ...), so a controller
  // player sees the button they should press -- not just the keyboard Y. The
  // label follows the controller family (I_ControllerConfirmLabel); NULL means
  // keyboard only, so we keep the classic single-line prompt.
  const char* pad = I_ControllerConfirmLabel();
  if (pad)
    sprintf(endstring,"%s\n\n"DOSY"\n(or the %s button)", msg, pad);
  else
    sprintf(endstring,"%s\n\n"DOSY, msg);

  M_StartMessage(endstring,M_QuitResponse,true);
}




void M_ChangeSensitivity(int choice)
{
    switch(choice)
    {
      case 0:
	if (mouseSensitivity)
	    mouseSensitivity--;
	break;
      case 1:
	if (mouseSensitivity < 9)
	    mouseSensitivity++;
	break;
    }
}




void M_ChangeDetail(int choice)
{
    choice = 0;
    detailLevel = 1 - detailLevel;

    // FIXME - does not work. Remove anyway?
    fprintf( stderr, "M_ChangeDetail: low detail mode n.a.\n");

    return;
    
    /*R_SetViewSize (screenblocks, detailLevel);

    if (!detailLevel)
	players[consoleplayer].message = DETAILHI;
    else
	players[consoleplayer].message = DETAILLO;*/
}

//
// Cycle the renderer back-end (DOOM-0026). Advances to the next available mode
// in fidelity order (Classic -> Solid -> Ultra), skipping any this machine
// can't run. If nothing else is available (no Vulkan device), say so.
//
void M_ChangeRenderer(int choice)
{
    rendermode_t next;
    int prevClassic, nextClassic;

    choice = 0;
    next = RB_NextAvailableMode(rendermode);

    if (next == rendermode)
    {
	// No other back-end available — nothing to switch to yet.
	S_StartSound(NULL, sfx_oof);
	players[consoleplayer].message = "3D renderer not yet available";
	message_dontfuckwithme = true;
	return;
    }

    prevClassic = (rendermode == RB_CLASSIC);
    RB_SetMode(next);

    // DOOM-0206 (L3): the menu skin is tied to the tier. A tier change that crosses
    // the Classic<->3D boundary re-routes currentMenu so the classic bitmap skin
    // (RendererDef) and the crisp skin (VideoDef) each only ever show under their own
    // tiers. A Solid<->Ultra change keeps VideoDef (no needless cursor jump). itemOn
    // is pinned to the destination's tier row so the cursor stays put across the swap
    // (M_SetupNextMenu would restore lastOn, not necessarily that row).
    nextClassic = (next == RB_CLASSIC);
    if (prevClassic != nextClassic &&
	(currentMenu == &VideoDef || currentMenu == &RendererDef))
    {
	if (nextClassic) { currentMenu = &RendererDef; itemOn = rm_renderer; }
	else             { currentMenu = &VideoDef;    itemOn = vid_renderer; }
    }
}

//
// DOOM-0206 (L3): the new Ray Tracing on/off row (both 3D tiers). On <=> rb_rtdebug==6,
// Off <=> rb_rtdebug==0. No-op when Debug Views (rb_rtdebug_menu) owns rb_rtdebug — that
// developer mode cycles it through the diagnostic set via the `~` key, so this row goes
// visually greyed and its handler returns early (INV-4 forbids touching M_Responder, so
// the row stays status 1 and Enter still fires here — hence the guard). Not persistence-
// stable in Solid: RB_ApplyTierRt reclaims the tier default (0 Solid / 6 Ultra) at boot
// and on every tier switch unless Debug Views is on (spec §4.5).
//
void M_ChangeRayTracing(int choice)
{
    choice = 0;
    if (rb_rtdebug_menu)
	return;
    rb_rtdebug = (rb_rtdebug == 6) ? 0 : 6;
}

//
// DOOM-0206 (L3): Back row on the Video menu — return to Options (Esc does the same via
// the classic prevMenu path).
//
void M_VideoBack(int choice)
{
    choice = 0;
    M_SetupNextMenu(&OptionsDef);
}


//
// Cycle the FPS counter: Off -> Top-Left -> Top-Centre -> Top-Right (DOOM-0046)
//
//
// Cycle the temporal upscaler: Off -> TAAU (DOOM-0009 build step 6-d). FSR 2 /
// FSR 3.1 are later phases on the same plumbing; only TAAU ships here.
//
void M_ChangeUpscaler(int choice)
{
    choice = 0;
    rb_upscaler = (rb_upscaler + 1) % 2;
}

//
// DOOM-0205: Render Effects submenu. The entry point (opens the submenu) plus one
// toggle handler per effect. Each flips the same var its hotkey does, so the menu,
// the hotkey and the persisted ~/.doomrc all stay in lockstep.
//
void M_UltraEffects(int choice)
{
    choice = 0;
    M_SetupNextMenu(&EffectsDef);
}

void M_ChangeFlashlight(int choice)
{
    choice = 0;
    rb_flashlight = rb_flashlight ? 0 : 1;
}

void M_ChangeSSAO(int choice)
{
    choice = 0;
    rb_ssao = rb_ssao ? 0 : 1;
}

void M_ChangeDetile(int choice)
{
    choice = 0;
    rb_detile = (rb_detile + 1) % 3;   // Off -> 2-tap -> 4-tap
}

void M_ChangeFilth(int choice)
{
    choice = 0;
    rb_filth = rb_filth ? 0 : 1;
}

void M_ChangeWet(int choice)
{
    choice = 0;
    rb_wet = rb_wet ? 0 : 1;
}

void M_ChangeProfiler(int choice)
{
    choice = 0;
    rb_profile = rb_profile ? 0 : 1;
}

//
// Cycle the path tracer's render scale: 100% -> 75% -> 67% -> 50% (DOOM-0009 6-d).
// Takes effect on the Ultra denoised view when an upscaler is active.
//
void M_ChangeRenderScale(int choice)
{
    int i, k = 0;
    choice = 0;
    for (i=0 ; i<4 ; i++) if (renderScalePresets[i]==rb_renderscale) k=i;
    rb_renderscale = renderScalePresets[(k + 1) % 4];
}

//
// Toggle the path-tracer Debug Views (DOOM-0135). Off = the `~` key is a plain RT
// on/off switch; On = `~` cycles the diagnostic views. Turning it off snaps the
// view out of any diagnostic mode (1-4) to the denoised view, so the picture never
// sticks on e.g. white-furnace once the cycle key is no longer reachable.
//
void M_ChangeDebugViews(int choice)
{
    choice = 0;
    rb_rtdebug_menu = rb_rtdebug_menu ? 0 : 1;
    if (!rb_rtdebug_menu && rb_rtdebug >= 1 && rb_rtdebug <= 4)
	rb_rtdebug = 6;
}

//
// Brightness slider for the Ultra/denoiser view (DOOM-0096): left/right nudge the
// exposure EV fed to the path-tracer composite tonemap. rb_exposure 0..15 maps in
// r_vulkan.cpp to EV [-4.0, -0.25] (pos 7 == the old fixed -2.25 default).
//
void M_ChangeBrightness(int choice)
{
    switch (choice)
    {
      case 0:
	if (rb_exposure > 0)  rb_exposure--;
	break;
      case 1:
	if (rb_exposure < 15) rb_exposure++;
	break;
    }
}

//
// DOOM-0147 Part C: Widescreen on/off. SCREENWIDTH (and the screen buffers + SDL
// texture sized from it) is fixed once at startup, so the change only takes effect
// on the next launch -- the menu value shows "(restart)". Persisted to ~/.doomrc.
//
void M_ChangeWidescreen(int choice)
{
    choice = 0;
    widescreen = widescreen ? 0 : 1;
}

//
// DOOM-0147 Part C: Fill Screen on/off -- stretch the present to fill the whole
// monitor vs. authentic 4:3 with black bars. Applied live via I_SetAspect (present
// time only, no reallocation). Persisted to ~/.doomrc.
//
void M_ChangeFillScreen(int choice)
{
    choice = 0;
    fillstretch = fillstretch ? 0 : 1;
    I_SetAspect();
}

void M_ChangeFPS(int choice)
{
    choice = 0;
    fpsCorner = (fpsCorner + 1) % 4;
}




void M_SizeDisplay(int choice)
{
    switch(choice)
    {
      case 0:
	if (screenSize > 0)
	{
	    screenblocks--;
	    screenSize--;
	}
	break;
      case 1:
	// Stop at screenSize 7 -> screenblocks 10: one short of the HUD-less
	// fullscreen view (11), so the status bar can't be slid away (DOOM-0148).
	if (screenSize < 7)
	{
	    screenblocks++;
	    screenSize++;
	}
	break;
    }
	

    R_SetViewSize (screenblocks, detailLevel);
}




//
//      Menu Functions
//
void
M_DrawThermo
( int	x,
  int	y,
  int	thermWidth,
  int	thermDot )
{
    int		xx;
    int		i;

    xx = x;
    V_DrawPatchDirect (xx,y,0,W_CacheLumpName("M_THERML",PU_CACHE));
    xx += 8;
    for (i=0;i<thermWidth;i++)
    {
	V_DrawPatchDirect (xx,y,0,W_CacheLumpName("M_THERMM",PU_CACHE));
	xx += 8;
    }
    V_DrawPatchDirect (xx,y,0,W_CacheLumpName("M_THERMR",PU_CACHE));

    V_DrawPatchDirect ((x+8) + thermDot*8,y,
		       0,W_CacheLumpName("M_THERMO",PU_CACHE));
}



void
M_DrawEmptyCell
( menu_t*	menu,
  int		item )
{
    V_DrawPatchDirect (menu->x - 10,        menu->y+item*LINEHEIGHT - 1, 0,
		       W_CacheLumpName("M_CELL1",PU_CACHE));
}

void
M_DrawSelCell
( menu_t*	menu,
  int		item )
{
    V_DrawPatchDirect (menu->x - 10,        menu->y+item*LINEHEIGHT - 1, 0,
		       W_CacheLumpName("M_CELL2",PU_CACHE));
}


void
M_StartMessage
( char*		string,
  void*		routine,
  boolean	input )
{
    messageLastMenuActive = menuactive;
    messageToPrint = 1;
    messageString = string;
    messageRoutine = routine;
    messageNeedsInput = input;
    menuactive = true;
    return;
}



void M_StopMessage(void)
{
    menuactive = messageLastMenuActive;
    messageToPrint = 0;
}



//
// Find string width from hu_font chars
//
int M_StringWidth(char* string)
{
    int             i;
    int             w = 0;
    int             c;
	
    for (i = 0;i < strlen(string);i++)
    {
	c = toupper(string[i]) - HU_FONTSTART;
	if (c < 0 || c >= HU_FONTSIZE)
	    w += 4;
	else
	    w += SHORT (hu_font[c]->width);
    }
		
    return w;
}



//
//      Find string height from hu_font chars
//
int M_StringHeight(char* string)
{
    int             i;
    int             h;
    int             height = SHORT(hu_font[0]->height);
	
    h = height;
    for (i = 0;i < strlen(string);i++)
	if (string[i] == '\n')
	    h += height;
		
    return h;
}


//
//      Write a string using the hu_font
//
void
M_WriteText
( int		x,
  int		y,
  char*		string)
{
    int		w;
    char*	ch;
    int		c;
    int		cx;
    int		cy;
		

    ch = string;
    cx = x;
    cy = y;
	
    while(1)
    {
	c = *ch++;
	if (!c)
	    break;
	if (c == '\n')
	{
	    cx = x;
	    cy += 12;
	    continue;
	}
		
	c = toupper(c) - HU_FONTSTART;
	if (c < 0 || c>= HU_FONTSIZE)
	{
	    cx += 4;
	    continue;
	}
		
	w = SHORT (hu_font[c]->width);
	if (cx+w > ORIGWIDTH)
	    break;
	V_DrawPatchDirect(cx, cy, 0, hu_font[c]);
	cx+=w;
    }
}


//
// M_WriteTextScaled
// DOOM-0206: M_WriteText, but each red HUD-font glyph is blitted at an integer
// `scale` multiple (V_DrawPatchScaled) and the pen advances by the scaled width.
// Used by the Classic main menu to render every item at one uniform "medium" size.
//
void
M_WriteTextScaled
( int		x,
  int		y,
  char*		string,
  int		scale )
{
    int		w;
    char*	ch;
    int		c;
    int		cx;
    int		cy;

    ch = string;
    cx = x;
    cy = y;

    while(1)
    {
	c = *ch++;
	if (!c)
	    break;
	if (c == '\n')
	{
	    cx = x;
	    cy += 12*scale;
	    continue;
	}

	c = toupper(c) - HU_FONTSTART;
	if (c < 0 || c>= HU_FONTSIZE)
	{
	    cx += 4*scale;
	    continue;
	}

	w = SHORT (hu_font[c]->width);
	if (cx+w*scale > ORIGWIDTH)
	    break;
	V_DrawPatchScaled(cx, cy, 0, hu_font[c], scale);
	cx += w*scale;
    }
}



//
// CONTROL PANEL
//

//
// M_Responder
//
boolean M_Responder (event_t* ev)
{
    int             ch;
    int             i;
    static  int     joywait = 0;
    static  int     mousewait = 0;
    static  int     mousey = 0;
    static  int     lasty = 0;
    static  int     mousex = 0;
    static  int     lastx = 0;
	
    ch = -1;
	
    if (ev->type == ev_joystick && joywait < I_GetTime())
    {
	if (ev->data3 == -1)
	{
	    ch = KEY_UPARROW;
	    joywait = I_GetTime() + 5;
	}
	else if (ev->data3 == 1)
	{
	    ch = KEY_DOWNARROW;
	    joywait = I_GetTime() + 5;
	}
		
	if (ev->data2 == -1)
	{
	    ch = KEY_LEFTARROW;
	    joywait = I_GetTime() + 2;
	}
	else if (ev->data2 == 1)
	{
	    ch = KEY_RIGHTARROW;
	    joywait = I_GetTime() + 2;
	}
		
	if (ev->data1&1)
	{
	    ch = KEY_ENTER;
	    joywait = I_GetTime() + 5;
	}
	if (ev->data1&2)
	{
	    ch = KEY_BACKSPACE;
	    joywait = I_GetTime() + 5;
	}
    }
    else
    {
	if (ev->type == ev_mouse && mousewait < I_GetTime())
	{
	    mousey += ev->data3;
	    if (mousey < lasty-30)
	    {
		ch = KEY_DOWNARROW;
		mousewait = I_GetTime() + 5;
		mousey = lasty -= 30;
	    }
	    else if (mousey > lasty+30)
	    {
		ch = KEY_UPARROW;
		mousewait = I_GetTime() + 5;
		mousey = lasty += 30;
	    }
		
	    mousex += ev->data2;
	    if (mousex < lastx-30)
	    {
		ch = KEY_LEFTARROW;
		mousewait = I_GetTime() + 5;
		mousex = lastx -= 30;
	    }
	    else if (mousex > lastx+30)
	    {
		ch = KEY_RIGHTARROW;
		mousewait = I_GetTime() + 5;
		mousex = lastx += 30;
	    }
		
	    if (ev->data1&1)
	    {
		ch = KEY_ENTER;
		mousewait = I_GetTime() + 15;
	    }
			
	    if (ev->data1&2)
	    {
		ch = KEY_BACKSPACE;
		mousewait = I_GetTime() + 15;
	    }
	}
	else
	    if (ev->type == ev_keydown)
	    {
		ch = ev->data1;
	    }
    }
    
    if (ch == -1)
	return false;

    
    // Save Game string input
    if (saveStringEnter)
    {
	switch(ch)
	{
	  case KEY_BACKSPACE:
	    if (saveCharIndex > 0)
	    {
		saveCharIndex--;
		savegamestrings[saveSlot][saveCharIndex] = 0;
	    }
	    break;
				
	  case KEY_ESCAPE:
	    saveStringEnter = 0;
	    strcpy(&savegamestrings[saveSlot][0],saveOldString);
	    break;
				
	  case KEY_ENTER:
	    saveStringEnter = 0;
	    if (savegamestrings[saveSlot][0])
		M_DoSave(saveSlot);
	    break;
				
	  default:
	    ch = toupper(ch);
	    if (ch != 32)
		if (ch-HU_FONTSTART < 0 || ch-HU_FONTSTART >= HU_FONTSIZE)
		    break;
	    if (ch >= 32 && ch <= 127 &&
		saveCharIndex < SAVESTRINGSIZE-1 &&
		M_StringWidth(savegamestrings[saveSlot]) <
		(SAVESTRINGSIZE-2)*8)
	    {
		savegamestrings[saveSlot][saveCharIndex++] = ch;
		savegamestrings[saveSlot][saveCharIndex] = 0;
	    }
	    break;
	}
	return true;
    }
    
    // Take care of any messages that need input
    if (messageToPrint)
    {
	// DOOM-0160: let the gamepad answer yes/no prompts (e.g. the Quit
	// confirmation). The controller's select button -- the same A/Cross
	// press that picked "Quit Game" from the menu -- arrives here as
	// KEY_ENTER; treat it (and the keyboard Enter) as an affirmative so a
	// controller-only player is no longer forced to reach for the keyboard Y.
	if (messageNeedsInput == true && ch == KEY_ENTER)
	    ch = 'y';

	if (messageNeedsInput == true &&
	    !(ch == ' ' || ch == 'n' || ch == 'y' || ch == KEY_ESCAPE))
	    return false;
		
	menuactive = messageLastMenuActive;
	messageToPrint = 0;
	if (messageRoutine)
	    messageRoutine(ch);
			
	menuactive = false;
	S_StartSound(NULL,sfx_swtchx);
	return true;
    }
	
    if (devparm && ch == KEY_F1)
    {
	G_ScreenShot ();
	return true;
    }
		
    
    // F-Keys
    if (!menuactive)
	switch(ch)
	{
	  case KEY_MINUS:         // Screen size down
	    if (automapactive || chat_on)
		return false;
	    M_SizeDisplay(0);
	    S_StartSound(NULL,sfx_stnmov);
	    return true;
				
	  case KEY_EQUALS:        // Screen size up
	    if (automapactive || chat_on)
		return false;
	    M_SizeDisplay(1);
	    S_StartSound(NULL,sfx_stnmov);
	    return true;
				
	  case KEY_F1:            // Help key
	    M_StartControlPanel ();

	    if ( gamemode == retail )
	      currentMenu = &ReadDef2;
	    else
	      currentMenu = &ReadDef1;
	    
	    itemOn = 0;
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
				
	  case KEY_F2:            // Save
	    M_StartControlPanel();
	    S_StartSound(NULL,sfx_swtchn);
	    M_SaveGame(0);
	    return true;
				
	  case KEY_F3:            // Load
	    M_StartControlPanel();
	    S_StartSound(NULL,sfx_swtchn);
	    M_LoadGame(0);
	    return true;
				
	  case KEY_F4:            // Sound Volume
	    M_StartControlPanel ();
	    currentMenu = &SoundDef;
	    itemOn = sfx_vol;
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
				
	  case KEY_F5:            // Detail toggle
	    M_ChangeDetail(0);
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
				
	  case KEY_F6:            // Quicksave
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuickSave();
	    return true;
				
	  case KEY_F7:            // End game
	    S_StartSound(NULL,sfx_swtchn);
	    M_EndGame(0);
	    return true;
				
	  case KEY_F8:            // Toggle messages
	    M_ChangeMessages(0);
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
				
	  case KEY_F9:            // Quickload
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuickLoad();
	    return true;
				
	  case KEY_F10:           // Quit DOOM
	    S_StartSound(NULL,sfx_swtchn);
	    M_QuitDOOM(0);
	    return true;
				
	  case KEY_F11:           // gamma toggle
	    usegamma++;
	    if (usegamma > 4)
		usegamma = 0;
	    players[consoleplayer].message = gammamsg[usegamma];
	    I_SetPalette (W_CacheLumpName ("PLAYPAL",PU_CACHE));
	    return true;
				
	}

    
    // Pop-up menu?
    if (!menuactive)
    {
	if (ch == KEY_ESCAPE)
	{
	    M_StartControlPanel ();
	    S_StartSound(NULL,sfx_swtchn);
	    return true;
	}
	return false;
    }

    
    // Keys usable within menu
    switch (ch)
    {
      case KEY_DOWNARROW:
	do
	{
	    if (itemOn+1 > currentMenu->numitems-1)
		itemOn = 0;
	    else itemOn++;
	    S_StartSound(NULL,sfx_pstop);
	} while(currentMenu->menuitems[itemOn].status==-1);
	return true;
		
      case KEY_UPARROW:
	do
	{
	    if (!itemOn)
		itemOn = currentMenu->numitems-1;
	    else itemOn--;
	    S_StartSound(NULL,sfx_pstop);
	} while(currentMenu->menuitems[itemOn].status==-1);
	return true;

      case KEY_LEFTARROW:
	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status == 2)
	{
	    S_StartSound(NULL,sfx_stnmov);
	    currentMenu->menuitems[itemOn].routine(0);
	}
	return true;
		
      case KEY_RIGHTARROW:
	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status == 2)
	{
	    S_StartSound(NULL,sfx_stnmov);
	    currentMenu->menuitems[itemOn].routine(1);
	}
	return true;

      case KEY_ENTER:
	if (currentMenu->menuitems[itemOn].routine &&
	    currentMenu->menuitems[itemOn].status)
	{
	    currentMenu->lastOn = itemOn;
	    if (currentMenu->menuitems[itemOn].status == 2)
	    {
		currentMenu->menuitems[itemOn].routine(1);      // right arrow
		S_StartSound(NULL,sfx_stnmov);
	    }
	    else
	    {
		currentMenu->menuitems[itemOn].routine(itemOn);
		S_StartSound(NULL,sfx_pistol);
	    }
	}
	return true;
		
      case KEY_ESCAPE:
	currentMenu->lastOn = itemOn;
	M_ClearMenus ();
	S_StartSound(NULL,sfx_swtchx);
	return true;
		
      case KEY_BACKSPACE:
	currentMenu->lastOn = itemOn;
	if (currentMenu->prevMenu)
	{
	    currentMenu = currentMenu->prevMenu;
	    itemOn = currentMenu->lastOn;
	    S_StartSound(NULL,sfx_swtchn);
	}
	else
	{
	    // No menu to go back to (top level): "back" closes the panel, so the
	    // controller Circle button works as back-then-close (DOOM-0056).
	    M_ClearMenus ();
	    S_StartSound(NULL,sfx_swtchx);
	}
	return true;
	
      default:
	for (i = itemOn+1;i < currentMenu->numitems;i++)
	    if (currentMenu->menuitems[i].alphaKey == ch)
	    {
		itemOn = i;
		S_StartSound(NULL,sfx_pstop);
		return true;
	    }
	for (i = 0;i <= itemOn;i++)
	    if (currentMenu->menuitems[i].alphaKey == ch)
	    {
		itemOn = i;
		S_StartSound(NULL,sfx_pstop);
		return true;
	    }
	break;
	
    }

    return false;
}



//
// M_StartControlPanel
//
void M_StartControlPanel (void)
{
    // intro might call this repeatedly
    if (menuactive)
	return;
    
    menuactive = 1;
    currentMenu = &MainDef;         // JDC
    itemOn = currentMenu->lastOn;   // JDC
}


//
// M_Drawer
// Called after the view has been rendered,
// but before it has been blitted.
//
//
// DOOM-0206 (L3): true when the active menu draws with the crisp display-pixel skin (which
// owns its own dim + rows + skull). Only VideoDef does, and it is only ever reached in the 3D
// tiers (M_RendererMenu routes Classic -> RendererDef), so this is inherently 3D-only.
//
static int M_MenuIsCrisp(void)
{
    // DOOM-0206 (v2 §4.6): crisp under the 3D tiers for any REGISTERED row-list menu.
    // The registry (crispMenus[]) is the whitelist — bespoke fullscreen menus (the
    // help screens ReadDef1/2, the DOOM 1-vs-2 chooser GameSelectDef) and Load/Save
    // with their editable slots aren't in it, so they fall through to the classic
    // bitmap path exactly as under Classic. INV-1: never crisp under RB_CLASSIC.
    return rendermode != RB_CLASSIC && M_FindCrispMenu(currentMenu) != NULL;
}

//
// DOOM-0206 (L6): HUD-safe shift for the 320x200 classic menus (INV-2). Every
// classic menu element -- the generic patch rows, the M_Draw* routine's
// M_WriteText / M_DrawThermo output, and the skull -- derives its Y from
// currentMenu->y, so a single bias on that field slides the whole menu up as a
// unit (M_Drawer applies it, then restores). This returns how far up to bias.
//
// The overrun the spec cites lives in the routine-drawn menus: a status==2 item
// draws its slider on the row BELOW its label (M_DrawThermo at LINEHEIGHT*(idx+1)),
// so it occupies TWO rows -- e.g. after the v2 FPS row RendererDef's Brightness
// thermo (rm_brightness=8) lands at y+16*9=204 and Options' Sound Volume row
// (soundvol=7) at y+16*7=172, both below the 168 status-bar top in-game. Budget
// the extra row per thermo or a slider can still land in the HUD band.
//
// The bound is ST_Y (=ORIGHEIGHT-ST_HEIGHT=168) whenever the status bar is drawn
// (in a level: DOOM-0148 always draws it), else the full ORIGHEIGHT (200). Every
// classic menu's ROWS fit above the bound once shifted (RendererDef, the tallest at
// 9 rows + a thermo = 160px, clears a ~166px in-level band), so plain shift-to-fit
// suffices -- no per-menu scroll window is needed (spec 7 L6's fallback path is
// unreachable for the current menus). NOTE: this budgets the menuitem ROWS only; a
// routine that draws a header ABOVE currentMenu->y (as M_DrawRendererMenu once did)
// is NOT in the budget and would clip off the top once shifted -- so such headers
// were dropped from the tall menus (see M_DrawRendererMenu).
//
static int M_ClassicMenuShift(void)
{
    int i, maxRow = 0, bottom, bound, shift;

    for (i = 0 ; i < currentMenu->numitems ; i++)
    {
	int row = i;
	if (currentMenu->menuitems[i].status == 2)
	    row = i + 1;                 // thermo draws on the row below its label
	if (row > maxRow) maxRow = row;
    }
    bottom = currentMenu->y + (maxRow + 1) * LINEHEIGHT;   // 320x200 bottom edge
    bound  = (gamestate == GS_LEVEL) ? ST_Y : ORIGHEIGHT;
    shift  = bottom - bound;
    if (shift <= 0)
	return 0;
    return shift + 2;                    // a small margin below the exact bound
}

void M_Drawer (void)
{
    static short	x;
    static short	y;
    short		i;
    short		max;
    char		string[40];
    int			start;
    short		savedMenuY;	// DOOM-0206 (L6): restore .y after the HUD-safe bias
    int			hudShift;

    inhelpscreens = false;

    // DOOM-0206 (final review fix, Finding 2): default the gate off on every call. The present
    // path's FlushMenuText only resets it to 0 after actually flushing a queued dim/text batch,
    // but that flush is skipped whenever the swapchain present early-returns on
    // VK_ERROR_OUT_OF_DATE_KHR (window resize/minimize) -- so a menu that was open on such a
    // skipped frame could leave the gate armed for one extra frame even after closing. Clearing
    // it here, before any early return or draw decision, means only a frame that actually draws
    // the crisp VideoDef skin (below) re-arms it -- every other path (message prompt, no menu,
    // classic-path bitmap menu) leaves it off.
    rb_menu_text_active = 0;

    // Horiz. & Vertically center string and print it.
    if (messageToPrint)
    {
	start = 0;
	y = 100 - M_StringHeight(messageString)/2;
	while(*(messageString+start))
	{
	    boolean	split = false;		// DOOM-0161: did this line end at a '\n'?
	    for (i = 0;i < strlen(messageString+start);i++)
		if (*(messageString+start+i) == '\n')
		{
		    memset(string,0,40);
		    strncpy(string,messageString+start,i);
		    start += i+1;
		    split = true;
		    break;
		}

	    // Final line (no trailing '\n'). The original test here was
	    //   i == strlen(messageString+start)
	    // but `start` is advanced inside the loop above, so that stale compare
	    // could spuriously fire on a real line whose remainder-after-advance
	    // happened to equal i -- silently dropping a line. It bit two equal-
	    // length prompt lines (e.g. "(press y to quit)" / "(or the X button)").
	    // Track whether we split on a '\n' instead.
	    if (!split)
	    {
		strcpy(string,messageString+start);
		start += i;
	    }
				
	    x = 160 - M_StringWidth(string)/2;
	    M_WriteText(x,y,string);
	    y += SHORT(hu_font[0]->height);
	}
	return;
    }

    if (!menuactive)
	return;

    // DOOM-0206 (v2 §4.6): a crisp 3D-tier menu draws itself entirely in display pixels --
    // dim, title, rows, values, sliders AND skull -- via M_DrawCrispMenu, which doesn't align
    // with the generic 320x200 patch/skull loop below. So for a crisp menu we draw it and
    // return, skipping that loop. We call M_DrawCrispMenu(currentMenu) directly, NOT
    // currentMenu->routine() -- routine is the CLASSIC bitmap draw (M_DrawOptions/M_DrawMainMenu/
    // ...) which would paint the red banners/lumps. rb_text_begin() MUST run first: it clears the
    // host quad vector so each frame starts empty (else it appends onto every prior frame's quads
    // forever -- FlushMenuText's GPU buffer has a fixed cap). rb_menu_text_active is reset to 0 by
    // FlushMenuText after it draws, so the frame after the menu closes (M_Drawer doesn't run) sees
    // the gate off and skips the draw -- the overlay disappears the instant the menu closes.
    if (M_MenuIsCrisp())
    {
	rb_text_begin();
	rb_menu_text_active = 1;
	M_DrawCrispMenu(currentMenu);   // dim + title + rows + values + skull, all display-pixel
	return;
    }

    // DOOM-0206 (final review fix, Finding 1): classic bitmap menus (Options/Main/Load/Save/
    // Sound/...) draw straight into screens[0] a few lines below, via the generic patch loop.
    // A dim quad, by contrast, is queued into g.textVerts and flushed by FlushMenuText AFTER
    // screens[0] is composited in the present path -- so queuing one here would land ON TOP of
    // the classic menu (darkening it), not behind it, which is the opposite of "dim behind the
    // classic menus". Only the crisp VideoDef skin (handled above, which queues its own dim
    // before its own text in the SAME batch, so the ordering works out) gets a dim. Classic-path
    // menus under Solid/Ultra therefore stay undimmed here -- exactly like Classic -- and the
    // gate (cleared at the top of this function) stays 0, so FlushMenuText's early-return skips
    // the draw entirely. Their HUD-safe shift below still applies under every tier.

    // DOOM-0206 (L6): slide the whole classic menu up so no element overruns the
    // status-bar band (INV-2). Bias currentMenu->y -- which every element (the
    // routine's M_WriteText/M_DrawThermo rows, the generic patch loop, and the
    // skull) reads -- for the duration of the draw, then restore it.
    hudShift   = M_ClassicMenuShift();
    savedMenuY = currentMenu->y;
    currentMenu->y -= hudShift;

    if (currentMenu->routine)
	currentMenu->routine();         // call Draw routine

    // DRAW MENU
    x = currentMenu->x;
    y = currentMenu->y;
    max = currentMenu->numitems;

    for (i=0;i<max;i++)
    {
	// DOOM-0206 (L6): Options draws its own uniform hu_font labels
	// (M_DrawOptions / optionsLabels[]), so suppress the oversized big-red
	// graphic lumps the generic loop would otherwise draw for it (INV-7).
	// The Classic main menu (M_DrawMainMenu) does the same via M_WriteTextScaled,
	// so MainDef is suppressed here too.
	if (currentMenu != &OptionsDef && currentMenu != &MainDef
	    && currentMenu->menuitems[i].name[0])
	    V_DrawPatchDirect (x,y,0,
			       W_CacheLumpName(currentMenu->menuitems[i].name ,PU_CACHE));
	y += LINEHEIGHT;
    }


    // DRAW SKULL
    // DOOM-0206 (v2): the Game Select screen is a carved-out classic-path menu, so its skull is
    // the paletted M_SKULL at plain palette brightness -- dull beside the crisp menus' brightened
    // skull. In the 3D tiers, draw the brightened crisp skull (the same M_SKULL1 lump, same size)
    // in its place. Map the classic skull's 320x200 virtual placement to display pixels; height
    // comes from the patch so the size matches the classic skull (user: brighten, keep the size).
    if (currentMenu == &GameSelectDef && rendermode != RB_CLASSIC && rb_menu_cursor_ready())
    {
	patch_t* sk = (patch_t*)W_CacheLumpName(skullName[whichSkull],PU_CACHE);
	int dW = rb_display_width(), dH = rb_display_height();
	int vx = x + SKULLXOFF, vy = currentMenu->y - 5 + itemOn*LINEHEIGHT;
	rb_text_begin();
	rb_menu_text_active = 1;
	rb_menu_draw_cursor(vx * dW / ORIGWIDTH, vy * dH / ORIGHEIGHT,
			    SHORT(sk->height) * dH / ORIGHEIGHT);
    }
    else
    {
	V_DrawPatchDirect(x + SKULLXOFF,currentMenu->y - 5 + itemOn*LINEHEIGHT, 0,
			  W_CacheLumpName(skullName[whichSkull],PU_CACHE));
    }

    currentMenu->y = savedMenuY;        // restore the un-shifted layout

}


//
// M_ClearMenus
//
void M_ClearMenus (void)
{
    menuactive = 0;
    // if (!netgame && usergame && paused)
    //       sendpause = true;
}




//
// M_SetupNextMenu
//
void M_SetupNextMenu(menu_t *menudef)
{
    currentMenu = menudef;
    itemOn = currentMenu->lastOn;
}


//
// M_Ticker
//
void M_Ticker (void)
{
    if (--skullAnimCounter <= 0)
    {
	whichSkull ^= 1;
	skullAnimCounter = 8;
    }
}


//
// M_Init
//
void M_Init (void)
{
    currentMenu = &MainDef;
    menuactive = 0;
    itemOn = currentMenu->lastOn;
    whichSkull = 0;
    skullAnimCounter = 10;
    // HUD stays on in-game (DOOM-0148): screenblocks 11 is the fullscreen view
    // that hides the status bar. The largest size we allow is 10 (full view
    // WITH HUD), so clamp a config that saved 11 from an earlier build.
    if (screenblocks > 10)
	screenblocks = 10;
    screenSize = screenblocks - 3;
    messageToPrint = 0;
    messageString = NULL;
    messageLastMenuActive = menuactive;
    quickSaveSlot = -1;

    // Here we could catch other version dependencies,
    //  like HELP1/2, and four episodes.

  
    switch ( gamemode )
    {
      case commercial:
	// This is used because DOOM 2 had only one HELP
        //  page. I use CREDIT as second page now, but
	//  kept this hack for educational purposes.
	MainMenu[readthis] = MainMenu[quitdoom];
	mainLabels[readthis] = mainLabels[quitdoom];  // DOOM-0206: keep the crisp label in step with the item
	MainDef.numitems--;
	MainDef.y += 8;
	NewDef.prevMenu = &MainDef;
	ReadDef1.routine = M_DrawReadThis1;
	ReadDef1.x = 330;
	ReadDef1.y = 165;
	ReadMenu1[0].routine = M_FinishReadThis;
	break;
      case shareware:
	// Episode 2 and 3 are handled,
	//  branching to an ad screen.
      case registered:
	// We need to remove the fourth episode.
	EpiDef.numitems--;
	break;
      case retail:
	// We are fine.
      default:
	break;
    }

    // DOOM-0060: when both games are installed, add "Game Select" to the main
    // menu after Quit. Done here so it follows the commercial reshuffle above,
    // which may have pulled Quit up to a lower index. The item template lives at
    // MainMenu[main_end]; copy it to the slot right after the last visible item.
    if (D_BothGamesPresent())
    {
	MainMenu[MainDef.numitems] = MainMenu[main_end];
	mainLabels[MainDef.numitems] = mainLabels[main_end];  // DOOM-0206: crisp "Game Select" label follows the item slot
	MainDef.numitems++;
	MainDef.y -= 8;         // one more row -> recenter (mirrors the +=8 above)
    }
}

