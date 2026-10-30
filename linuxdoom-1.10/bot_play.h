// bot_play.h — DOOM-0529: a player for recording demo runs. Developer builds only.
#ifndef BOT_PLAY_H
#define BOT_PLAY_H

#ifdef DOOM_DEV

#include "d_ticcmd.h"
#include "doomtype.h"

extern boolean bot_active;

void BOT_Init (void);			// reads -botplay and -bottics
void BOT_BuildTiccmd (ticcmd_t* cmd);	// adds this tic's move to cmd
void BOT_Report (const char* what);	// prints the run's one summary line

#endif

#endif
