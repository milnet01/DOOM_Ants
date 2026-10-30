// d_ledger.c — DOOM-0502: the engine's side of a frameledger benchmark run.
//
//   -frameledger FILE           write the run to FILE
//   -frameledger-frames N       stop and quit after N recorded frames
//   -frameledger-scene NAME     the header's scene; defaults to the first level
//   -frameledger-display NAME   the header's display: onscreen (the default),
//                               headless (a private display) or offscreen
//
// run_ledger.c owns the file. This owns what only the running engine knows: the
// flags, the clock, the machine readings, and which level a frame belongs to.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "frameledger.h"

#include "doomdef.h"
#include "doomstat.h"
#include "d_ledger.h"
#include "i_system.h"
#include "m_argv.h"
#include "r_backend.h"
#include "rb_vulkan.h"
#include "run_ledger.h"

#include "build_stamp.h"

extern char* wadfiles[];

// A frame's rows are a few hundred bytes, and the buffer writes itself out at half
// full, so this outruns any run.
#define LEDGER_BUFFER	(16 * 1024 * 1024)

// Rendered frames each run draws in the level before its first recorded frame: the
// HD load lands in the first of them, and the denoiser's history fills in about a
// second of the rest. A frame that load lengthened would fail the budget alone.
#define LEDGER_SETTLE	120

static const char*	ledger_path;
static long long	ledger_frames;
static const char*	ledger_scene;
static const char*	ledger_display = "onscreen";
static double		load_start = -1;
static double		gpu_busy_start = -1;
static int		settled;
static FILE*		ledger_file;
static boolean		ledger_failed;

// CPU load as a share of the machine; below 0 where it cannot be read. Linux only:
// the format's Windows reading is a different measure, and absent means unknown.
static double D_ReadLoad (void)
{
    double load = -1;
    FILE* f = fopen ("/proc/loadavg", "r");
    if (f)
    {
	if (fscanf (f, "%lf", &load) != 1)
	    load = -1;
	fclose (f);
    }
    return load < 0 ? -1 : load / SDL_GetCPUCount ();
}

// The busiest GPU's busy share; below 0 where the driver does not say. amdgpu has
// this file; a card that lacks it simply is not counted.
static double D_ReadGpuBusy (void)
{
    double busiest = -1;
    int card;
    for (card = 0; card < 4; card++)
    {
	char path[64];
	int pct;
	FILE* f;
	snprintf (path, sizeof path, "/sys/class/drm/card%d/device/gpu_busy_percent", card);
	f = fopen (path, "r");
	if (!f)
	    continue;
	if (fscanf (f, "%d", &pct) == 1 && pct > busiest)
	    busiest = pct;
	fclose (f);
    }
    return busiest;
}

static double D_LedgerNowMs (void)
{
    return (double)SDL_GetPerformanceCounter () * 1000.0
	 / (double)SDL_GetPerformanceFrequency ();
}

void D_LedgerInit (void)
{
    int p = M_CheckParm ("-frameledger");
    if (!p || p + 1 >= myargc)
	return;
    ledger_path = myargv[p + 1];

    p = M_CheckParm ("-frameledger-frames");
    if (p && p + 1 < myargc)
	ledger_frames = atoll (myargv[p + 1]);
    p = M_CheckParm ("-frameledger-scene");
    if (p && p + 1 < myargc)
	ledger_scene = myargv[p + 1];
    p = M_CheckParm ("-frameledger-display");
    if (p && p + 1 < myargc)
	ledger_display = myargv[p + 1];

    // Before the engine loads anything, so they measure the machine, not this run.
    load_start = D_ReadLoad ();
    gpu_busy_start = D_ReadGpuBusy ();
}

// The header's inputs: every WAD, by name, hash and size, so two runs can prove
// they ran the same game data.
static void D_BuildInputs (char* out, size_t n)
{
    size_t used = 0;
    int i;
    used += snprintf (out + used, n - used, "[");
    for (i = 0; wadfiles[i] && used + 256 < n; i++)
    {
	char hash[72], name[128];
	long long bytes;
	const char* base = strrchr (wadfiles[i], '/');
	base = base ? base + 1 : wadfiles[i];
	if (!RL_HashFile (wadfiles[i], hash, &bytes))
	    continue;
	fled_json_escape (name, sizeof name, base);
	used += snprintf (out + used, n - used, "%s{\"name\":\"%s\",\"hash\":\"%s\",\"bytes\":%lld}",
			  used > 1 ? "," : "", name, hash, bytes);
    }
    snprintf (out + used, n - used, "]");
}

static void D_LevelName (char* out, size_t n)
{
    if (gamemode == commercial)
	snprintf (out, n, "map%02d", gamemap);
    else
	snprintf (out, n, "e%dm%d", gameepisode, gamemap);
}

static void D_LedgerOpen (void)
{
    static char		inputs[2048];
    static char		run_id[FLED_RUN_ID_MAX], started[32], scene[32];
    rl_facts_t		f;
    time_t		now = time (NULL);

    memset (&f, 0, sizeof f);
    strftime (started, sizeof started, "%Y-%m-%dT%H:%M:%SZ", gmtime (&now));
    snprintf (run_id, sizeof run_id, "doom-%s-%llu", started,
	      (unsigned long long)(SDL_GetPerformanceCounter () % 1000000));
    D_LevelName (scene, sizeof scene);
    D_BuildInputs (inputs, sizeof inputs);

    f.run_id = run_id;
    f.commit = BUILD_COMMIT;
    f.dirty = BUILD_DIRTY;
#ifdef DOOM_DEV
    f.build_type = "dev";
#else
    f.build_type = "release";
#endif
    f.display = ledger_display;
    f.scene = ledger_scene ? ledger_scene : scene;
    f.settling_frames = LEDGER_SETTLE;
    f.settled_frames = settled;
    f.os = SDL_GetPlatform ();
    f.started_utc = started;
    f.inputs_json = inputs;
    f.load_start = load_start;
    f.gpu_busy_start = gpu_busy_start;

    if (rendermode == RB_CLASSIC)
    {
	// No GPU work and no API to name: every frame is CPU work, drawn in software.
	f.device = f.renderer = f.driver = "none";
	f.api = "sdl2-software";
	f.tier = "classic";
	f.width = SCREENWIDTH;
	f.height = SCREENHEIGHT;
	f.render_scale_pct = 100;
    }
    else
    {
	f.tier = rendermode == RB_RT3D ? "ultra" : "solid";
	RB_Vulkan_LedgerFacts (&f);
    }

    ledger_file = fopen (ledger_path, "wb");
    if (!ledger_file || !RL_Open (ledger_file, &f, LEDGER_BUFFER, ledger_frames))
    {
	printf ("-frameledger: could not start writing %s; this run writes no file.\n",
		ledger_path);
	ledger_failed = true;
	return;
    }
    printf ("-frameledger: recording to %s after %d settling frames.\n", ledger_path, settled);
}

void D_LedgerFrameBegin (void)
{
    if (!ledger_path || ledger_failed)
	return;

    if (!RL_Active ())
    {
	if (gamestate != GS_LEVEL)
	    return;
	if (settled < LEDGER_SETTLE)
	{
	    settled++;
	    return;
	}
	D_LedgerOpen ();
	if (!RL_Active ())
	    return;
    }

    if (RL_FrameBegin (D_LedgerNowMs ()))
    {
	printf ("-frameledger: %lld frames recorded.\n", ledger_frames);
	I_Quit ();
    }
}

// After the tics ran, since a tic is what changes the level. What the frame has
// done by now is the game's own CPU work, the only part of it the renderer's
// timers cannot see. On the tic-locked path (Classic, a demo) it also holds the
// wait for the next tic, so cpu/game and frame/cpu_span mean little there.
void D_LedgerAfterTics (int tics, double tic_ms)
{
    char level[32], segment[48];

    if (!RL_Active ())
	return;
    if (gamestate == GS_LEVEL)
    {
	D_LevelName (level, sizeof level);
	snprintf (segment, sizeof segment, "level:%s", level);
    }
    else
	snprintf (segment, sizeof segment, "screen:%d", (int)gamestate);
    RL_SetSegment (segment);
    if (tics > 0)
	RL_Event ("tic", (double)tics);
    RL_CpuPass ("game", tic_ms);
}

double D_LedgerClockMs (void)
{
    return ledger_path ? D_LedgerNowMs () : 0.0;
}

void D_LedgerFinish (void)
{
    if (!RL_Active ())
	return;
    RL_Finish (D_LedgerNowMs (), D_ReadLoad (), D_ReadGpuBusy ());
    fclose (ledger_file);
    ledger_file = NULL;
}
