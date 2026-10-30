// bot_play.c — DOOM-0529: a player for recording demo runs, with nobody at the controls.
//
//   -botplay SEED    fill each ticcmd from the game's state; use with -record NAME
//   -bottics N       end the run after N tics (default 700, 20 seconds)
//
// Every BOT_WINDOW tics it prints a [botw] line of what the camera had in those tics, so a
// harness can pick the stretch of a long run worth filming.
//
// What it writes is an ordinary demo, so -timedemo replays it on any renderer. It runs
// one tic per frame and draws nothing, so a run takes moments, and a harness can try many
// seeds and keep the best. It reads the world and never calls P_Random, so a run's
// random numbers are the game's own. Developer builds only.
//
// Each tic ends in one of three intentions, nearest danger first: fight what it can see,
// fetch a pickup it can see, or go somewhere it has not been.
#ifdef DOOM_DEV

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomstat.h"
#include "d_event.h"
#include "d_ticcmd.h"
#include "g_game.h"
#include "m_argv.h"
#include "m_bbox.h"
#include "p_local.h"
#include "r_state.h"
#include "tables.h"
#include "bot_play.h"
#include "bot_steer.h"

extern boolean	singletics;
extern boolean	nodrawers;
extern fixed_t	forwardmove[2];
extern fixed_t	sidemove[2];

#define BOT_DEFAULT_TICS	700
#define BOT_ANG1		(ANG45 / 45)
#define BOT_MIN(a, b)		((a) < (b) ? (a) : (b))
#define BOT_MAX(a, b)		((a) > (b) ? (a) : (b))
#define BOT_SIGHT		(1600*FRACUNIT)	// farther than this is not worth aiming at
#define BOT_ITEM_REACH		(500*FRACUNIT)
#define BOT_STUCK_TICS		20
#define BOT_BLOCK_TICS		350		// how long a way that stuck it stays closed
#define BOT_MAX_SECTORS		4096
#define BOT_MAX_LINES		16384
#define BOT_WALL_MARGIN		72.0		// walks no nearer a wall than this if it can
#define BOT_WALL_FILLS		110.0		// a wall this near fills the screen
#define BOT_FIGHT_ANGLE		(BOT_ANG1 * 15)	// the centre third of the picture, either way
#define BOT_LOOK_HOLD		14		// tics a look lasts, so it does not twitch
#define BOT_WINDOW		35
#define BOT_RAY_MAX		2048.0

boolean		bot_active;

static unsigned	rng;
static int	bot_tics = BOT_DEFAULT_TICS;
static int	tics_run;
static int	damage_seen;
static int	last_health;
static double	walked;
static fixed_t	last_x, last_y;
static int	stuck_tics, stuck_total;
static int	shots;
static int	sway_tics, sway_dir = 1;
static int	unstick_tics, unstick_dir;
static int	aim_phase;
static int	look_tics;
static angle_t	look_off;
static int	win_wall, win_fight, win_sky, win_liquid, win_lit;

static byte	visited[BOT_MAX_SECTORS];
static int	sectors_seen;
static int	blocked_until[BOT_MAX_LINES];
static short	parent_line[BOT_MAX_SECTORS];
static short	bfs_queue[BOT_MAX_SECTORS];

// Pickups given up on. Standing on a medikit at full health takes nothing, and the thing
// stays there, so without this the run would stand on it to the end.
#define BOT_MAX_GIVEN_UP	64
static const mobj_t*	given_up[BOT_MAX_GIVEN_UP];
static int		num_given_up;
static const mobj_t*	chasing;
static int		chase_tics;

static unsigned BOT_Rand (void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static double BOT_Rad (angle_t a)
{
    return (double)a * (2.0 * 3.14159265358979 / 4294967296.0);
}

void BOT_Init (void)
{
    int p = M_CheckParm ("-botplay");
    if (!p || p + 1 >= myargc)
	return;
    rng = (unsigned)atoi (myargv[p + 1]) * 2654435761u + 1u;
    p = M_CheckParm ("-bottics");
    if (p && p + 1 < myargc)
	bot_tics = atoi (myargv[p + 1]);
    aim_phase = (int)(BOT_Rand () % 256);
    bot_active = true;
    singletics = true;
    nodrawers = true;
}

// A door a player opens with the use key, and can: the key it needs is held.
static boolean BOT_DoorOpens (const line_t* ln, const player_t* pl)
{
    switch (ln->special)
    {
      case 1: case 31: case 117: case 118:
	return true;
      case 26: case 32:
	return pl->cards[it_bluecard] || pl->cards[it_blueskull];
      case 27: case 34:
	return pl->cards[it_yellowcard] || pl->cards[it_yellowskull];
      case 28: case 33:
	return pl->cards[it_redcard] || pl->cards[it_redskull];
    }
    return false;
}

// A line that would end the level, walked over or pressed: the run must not.
static boolean BOT_LineExits (const line_t* ln)
{
    return ln->special == 11 || ln->special == 51 || ln->special == 52
	|| ln->special == 124 || ln->special == 197 || ln->special == 198;
}

static boolean BOT_FloorHurts (const sector_t* sec)
{
    return sec->special == 4 || sec->special == 5 || sec->special == 7
	|| sec->special == 11 || sec->special == 16;
}

// Can a player get from one side of this line to the other?
static boolean BOT_Crossable (int lnum, const sector_t* from, const sector_t* to,
			      const player_t* pl)
{
    const line_t* ln = &lines[lnum];
    fixed_t headroom = BOT_MIN (from->ceilingheight, to->ceilingheight)
		     - BOT_MAX (from->floorheight, to->floorheight);

    if (lnum < BOT_MAX_LINES && blocked_until[lnum] > tics_run)
	return false;
    if (ln->flags & ML_BLOCKING)
	return false;
    if (BOT_LineExits (ln) || BOT_FloorHurts (to))
	return false;
    if (headroom < 56*FRACUNIT)
	return BOT_DoorOpens (ln, pl);
    return to->floorheight - from->floorheight <= 24*FRACUNIT;
}

// Breadth first over sectors from where the player stands, to the nearest one not yet
// walked in. Returns the first line to cross, or -1 when nowhere new is reachable.
static int BOT_NextLine (const player_t* pl)
{
    const sector_t* start = pl->mo->subsector->sector;
    int head = 0, tail = 0, i;
    int startnum = (int)(start - sectors);

    if (numsectors > BOT_MAX_SECTORS)
	return -1;
    for (i = 0; i < numsectors; i++)
	parent_line[i] = -1;
    parent_line[startnum] = -2;
    bfs_queue[tail++] = (short)startnum;

    while (head < tail)
    {
	const int snum = bfs_queue[head++];
	const sector_t* sec = &sectors[snum];

	if (!visited[snum])
	{
	    // Walk back to the first step of the route.
	    int cur = snum;
	    int first = -1;
	    while (parent_line[cur] != -2)
	    {
		const line_t* ln = &lines[parent_line[cur]];
		first = parent_line[cur];
		cur = (int)((ln->frontsector - sectors) == cur
			    ? ln->backsector - sectors : ln->frontsector - sectors);
	    }
	    return first;
	}
	for (i = 0; i < sec->linecount; i++)
	{
	    const line_t* ln = sec->lines[i];
	    const sector_t* other;
	    int onum;

	    if (!ln->backsector || !ln->frontsector)
		continue;
	    other = ln->frontsector == sec ? ln->backsector : ln->frontsector;
	    onum = (int)(other - sectors);
	    if (parent_line[onum] != -1)
		continue;
	    if (!BOT_Crossable ((int)(ln - lines), sec, other, pl))
		continue;
	    parent_line[onum] = (short)(ln - lines);
	    bfs_queue[tail++] = (short)onum;
	}
    }
    return -1;
}

static boolean BOT_GivenUp (const mobj_t* mo)
{
    int i;
    for (i = 0; i < num_given_up; i++)
	if (given_up[i] == mo)
	    return true;
    return false;
}

static void BOT_GiveUp (const mobj_t* mo)
{
    if (num_given_up < BOT_MAX_GIVEN_UP)
	given_up[num_given_up++] = mo;
}

// The nearest living monster it can see, or else the nearest pickup in reach.
static void BOT_Look (const player_t* pl, mobj_t** enemy, mobj_t** item)
{
    thinker_t* th;
    fixed_t best_enemy = BOT_SIGHT, best_item = BOT_ITEM_REACH;

    *enemy = *item = NULL;
    for (th = thinkercap.next; th != &thinkercap; th = th->next)
    {
	mobj_t* mo;
	fixed_t dist;

	if (th->function.acp1 != (actionf_p1)P_MobjThinker)
	    continue;
	mo = (mobj_t*)th;
	if (mo == pl->mo)
	    continue;
	dist = P_AproxDistance (mo->x - pl->mo->x, mo->y - pl->mo->y);
	if ((mo->flags & MF_COUNTKILL) && mo->health > 0)
	{
	    if (dist < best_enemy && P_CheckSight (pl->mo, mo))
	    {
		best_enemy = dist;
		*enemy = mo;
	    }
	}
	else if ((mo->flags & MF_SPECIAL) && dist < best_item && !BOT_GivenUp (mo)
		 && mo->z <= pl->mo->z + 24*FRACUNIT && P_CheckSight (pl->mo, mo))
	{
	    best_item = dist;
	    *item = mo;
	}
    }
}

// A line the camera and the player cannot pass: a solid wall, a closed door it cannot open,
// or a step too tall. A door it can open is not a wall, or it would never walk up to one.
static boolean BOT_LineWalls (const line_t* ln, const player_t* pl)
{
    const sector_t* f = ln->frontsector;
    const sector_t* b = ln->backsector;
    if (!f || !b)
	return true;
    if (BOT_MIN (f->ceilingheight, b->ceilingheight)
	- BOT_MAX (f->floorheight, b->floorheight) < 56*FRACUNIT)
	return !BOT_DoorOpens (ln, pl);
    return (ln->flags & ML_BLOCKING) != 0;
}

// How far it is to the nearest wall along an angle, up to BOT_RAY_MAX.
static double BOT_Clearance (const player_t* pl, double rad)
{
    const double ox = pl->mo->x / (double)FRACUNIT, oy = pl->mo->y / (double)FRACUNIT;
    const double dx = cos (rad), dy = sin (rad);
    double best = BOT_RAY_MAX;
    int i;
    for (i = 0; i < numlines; i++)
    {
	const line_t* ln = &lines[i];
	double t;
	if (!BOT_LineWalls (ln, pl))
	    continue;
	t = BOT_RayToSegment (ox, oy, dx, dy,
			      ln->v1->x / (double)FRACUNIT, ln->v1->y / (double)FRACUNIT,
			      ln->v2->x / (double)FRACUNIT, ln->v2->y / (double)FRACUNIT);
	if (t >= 0.0 && t < best)
	    best = t;
    }
    return best;
}

// Where to look on the way to a goal: at it, unless a wall is in the way of it.
static angle_t BOT_LookFor (const player_t* pl, angle_t goal)
{
    if (--look_tics <= 0)
    {
	const angle_t step = BOT_ANG1 * 30;
	double dist[12];
	int i;
	for (i = 0; i < 12; i++)
	    dist[i] = BOT_Clearance (pl, BOT_Rad (goal + (angle_t)i * step));
	look_off = (angle_t)BOT_BestLook (dist, 12) * step;
	look_tics = BOT_LOOK_HOLD;
    }
    return goal + look_off;
}

// Tally what the camera has this tic, and print each window.
static void BOT_Tally (const player_t* pl, const mobj_t* enemy)
{
    const mobj_t* mo = pl->mo;
    const sector_t* sec = mo->subsector->sector;
    const fixed_t ahead = 200 * FRACUNIT;
    const unsigned fine = mo->angle >> ANGLETOFINESHIFT;
    const sector_t* front = R_PointInSubsector (mo->x + FixedMul (ahead, finecosine[fine]),
						mo->y + FixedMul (ahead, finesine[fine]))->sector;

    if (BOT_Clearance (pl, BOT_Rad (mo->angle)) < BOT_WALL_FILLS)
	win_wall++;
    if (enemy)
    {
	const angle_t off = R_PointToAngle2 (mo->x, mo->y, enemy->x, enemy->y) - mo->angle;
	if (off < BOT_FIGHT_ANGLE || off > (angle_t)0 - BOT_FIGHT_ANGLE)
	    win_fight++;
    }
    if (sec->ceilingpic == skyflatnum || front->ceilingpic == skyflatnum)
	win_sky++;
    if (BOT_FloorHurts (sec) || BOT_FloorHurts (front))
	win_liquid++;
    if (sec->lightlevel >= 190 && sec->ceilingpic != skyflatnum)
	win_lit++;

    if (tics_run % BOT_WINDOW == 0)
    {
	printf ("[botw] tic=%d wall=%d fight=%d sky=%d liquid=%d lit=%d health=%d kills=%d"
		" pos=%d,%d\n", tics_run, win_wall, win_fight, win_sky, win_liquid, win_lit,
		pl->health, pl->killcount, mo->x >> FRACBITS, mo->y >> FRACBITS);
	win_wall = win_fight = win_sky = win_liquid = win_lit = 0;
    }
}

// Hold the best gun it has for the job, as a player reaching for a number key would.
static void BOT_ChooseWeapon (const player_t* pl, fixed_t dist, ticcmd_t* cmd)
{
    weapontype_t want = pl->readyweapon;

    if (pl->weaponowned[wp_chaingun] && pl->ammo[am_clip] > 0 && dist > 300*FRACUNIT)
	want = wp_chaingun;
    else if (pl->weaponowned[wp_shotgun] && pl->ammo[am_shell] > 0)
	want = wp_shotgun;
    else if (pl->weaponowned[wp_chaingun] && pl->ammo[am_clip] > 0)
	want = wp_chaingun;

    if (want != pl->readyweapon && pl->pendingweapon == wp_nochange)
    {
	cmd->buttons |= BT_CHANGE;
	cmd->buttons |= want << BT_WEAPONSHIFT;
    }
}

void BOT_BuildTiccmd (ticcmd_t* cmd)
{
    player_t*	pl = &players[consoleplayer];
    mobj_t*	mo = pl->mo;
    mobj_t*	enemy;
    mobj_t*	item;
    angle_t	face;
    double	move_rad, speed = 0.0;
    int		forward, side;
    boolean	have_move = false;

    if (gamestate != GS_LEVEL || !mo)
	return;

    if (tics_run == 0)
    {
	last_x = mo->x;
	last_y = mo->y;
	last_health = pl->health;
    }

    if (pl->playerstate != PST_LIVE || ++tics_run > bot_tics)
    {
	BOT_Report ("run");
	G_CheckDemoStatus ();
	return;
    }

    // What the run has done so far.
    {
	const int snum = (int)(mo->subsector->sector - sectors);
	const double step = hypot ((mo->x - last_x) / (double)FRACUNIT,
				   (mo->y - last_y) / (double)FRACUNIT);
	if (snum < BOT_MAX_SECTORS && !visited[snum])
	{
	    visited[snum] = 1;
	    sectors_seen++;
	}
	walked += step;
	stuck_tics = step < 1.0 ? stuck_tics + 1 : 0;
	if (pl->health < last_health)
	    damage_seen += last_health - pl->health;
	last_health = pl->health;
	last_x = mo->x;
	last_y = mo->y;
    }

    BOT_Look (pl, &enemy, &item);
    BOT_Tally (pl, enemy);
    forward = side = 0;
    face = mo->angle;

    if (enemy)
    {
	// Fight: face it, drift side to side, keep a distance that suits the gun.
	const fixed_t dist = P_AproxDistance (enemy->x - mo->x, enemy->y - mo->y);
	const angle_t to = R_PointToAngle2 (mo->x, mo->y, enemy->x, enemy->y);
	const double to_rad = BOT_Rad (to);
	const double sway = (double)sway_dir * 3.14159265358979 / 2.0;
	double along = 0.0;

	// A hand's aim wanders a little around the target.
	aim_phase++;
	face = to + (angle_t)(int)(sin (aim_phase * 0.11) * sin (aim_phase * 0.037)
				   * (double)(BOT_ANG1 * 2));

	if (--sway_tics <= 0)
	{
	    sway_dir = -sway_dir;
	    sway_tics = 20 + (int)(BOT_Rand () % 40);
	}
	if (dist > 500*FRACUNIT)
	    along = 0.7;
	else if (dist < 170*FRACUNIT)
	    along = -0.8;
	move_rad = atan2 (sin (to_rad) * along + sin (to_rad + sway),
			  cos (to_rad) * along + cos (to_rad + sway));
	speed = 1.0;
	have_move = true;

	BOT_ChooseWeapon (pl, dist, cmd);

	// Fire when the gun is on it, as a player would, not on the first frame it is seen.
	if (labs ((long)(int)(to - mo->angle)) < (long)(BOT_ANG1 * 4))
	{
	    cmd->buttons |= BT_ATTACK;
	    shots++;
	}
    }
    else if (item)
    {
	const angle_t to = R_PointToAngle2 (mo->x, mo->y, item->x, item->y);

	// On it and it is still there, or too long getting there: not to be had.
	chase_tics = item == chasing ? chase_tics + 1 : 0;
	chasing = item;
	if (P_AproxDistance (item->x - mo->x, item->y - mo->y) < 24*FRACUNIT
	    || chase_tics > 105 || stuck_tics >= BOT_STUCK_TICS)
	    BOT_GiveUp (item);
	face = BOT_LookFor (pl, to);
	move_rad = BOT_Rad (to);
	speed = 1.0;
	have_move = true;
    }
    else
    {
	// Go somewhere new: at the middle of the next line on the route.
	const int lnum = BOT_NextLine (pl);
	if (lnum >= 0)
	{
	    const line_t* ln = &lines[lnum];
	    const fixed_t mx = ln->v1->x / 2 + ln->v2->x / 2;
	    const fixed_t my = ln->v1->y / 2 + ln->v2->y / 2;
	    const angle_t to = R_PointToAngle2 (mo->x, mo->y, mx, my);
	    const fixed_t dist = P_AproxDistance (mx - mo->x, my - mo->y);

	    face = BOT_LookFor (pl, to);
	    move_rad = BOT_Rad (to);
	    speed = 1.0;
	    have_move = true;

	    // At a door it can open: press use, as often as a player would.
	    if (ln->special && dist < 90*FRACUNIT && BOT_DoorOpens (ln, pl)
		&& (tics_run % 12) == 0)
		cmd->buttons |= BT_USE;

	    // Stuck on the way there: close this way for a while and try another.
	    if (stuck_tics >= BOT_STUCK_TICS && lnum < BOT_MAX_LINES)
	    {
		blocked_until[lnum] = tics_run + BOT_BLOCK_TICS;
		stuck_total++;
		stuck_tics = 0;
		unstick_tics = 15;
		unstick_dir = (BOT_Rand () & 1) ? 1 : -1;
	    }
	}
	else
	    face = mo->angle + BOT_ANG1 * 3;	// nowhere new: look around
    }

    // Keep off walls: a run hugging one shows nothing else.
    if (have_move)
    {
	double dist[8];
	int i;
	for (i = 0; i < 8; i++)
	    dist[i] = BOT_Clearance (pl, i * 2.0 * 3.14159265358979 / 8);
	move_rad = BOT_RepelFrom (move_rad, dist, 8, BOT_WALL_MARGIN);
    }

    // Backing off a wall takes priority over the plan: a hand does this without thinking.
    if (unstick_tics > 0)
    {
	unstick_tics--;
	move_rad = BOT_Rad (mo->angle) + 3.14159265358979 + unstick_dir * 1.2;
	speed = 1.0;
	have_move = true;
    }

    cmd->angleturn += (short)BOT_TurnFor ((int)(face - mo->angle) >> 16);
    if (have_move)
    {
	BOT_MoveFor (move_rad, BOT_Rad (mo->angle), speed,
		     forwardmove[1], sidemove[1], &forward, &side);
	cmd->forwardmove += (signed char)forward;
	cmd->sidemove += (signed char)side;
    }
}

// One line a harness reads, and the check a replay must match: the same run reaches the
// same position, health, kills and random-number index.
void BOT_Report (const char* what)
{
    const player_t* pl = &players[consoleplayer];
    extern int prndindex;

    if (!pl->mo)
	return;
    printf ("[bot] %s tics=%d alive=%d health=%d kills=%d/%d sectors=%d walked=%d "
	    "stuck=%d shots=%d hurt=%d state=%d,%d,%d,%d,%d\n",
	    what, tics_run, pl->playerstate == PST_LIVE, pl->health, pl->killcount,
	    totalkills, sectors_seen, (int)walked, stuck_total, shots, damage_seen,
	    pl->mo->x, pl->mo->y, pl->health, pl->killcount, prndindex);
    fflush (stdout);
}

#endif // DOOM_DEV
