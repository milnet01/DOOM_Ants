// render_scale.h — DOOM-0596 / DOOM-0622: the render scale, as pure decisions
// on plain values.
//
// Two things live here. The Video menu's Render Scale row lists real sizes
// (1920x1080, not only "50%"), and the renderer must draw at exactly the size
// the row named, so both read RsRenderSize. And the Dynamic choice moves the
// scale from the measured GPU time, which is DrsStep.
//
// They are here, rather than in m_menu.c and r_vulkan.cpp, so
// tests/render_scale_test.cpp can hold them with no menu and no GPU -- the same
// reason gfx_settings.h exists.
#ifndef RENDER_SCALE_H
#define RENDER_SCALE_H

#include <math.h>

// The config key render_scale is a whole percentage of the output's height.
// A hand-edited value is held to RS_CLAMP_MIN..100, as it always was. The row
// and the Dynamic choice go no lower than RS_MIN_PCT: a third of 4K is 720p.
#define RS_CLAMP_MIN	25
#define RS_MIN_PCT	33
#define RS_MAX_PCT	100
#define RS_MAX_SIZES	10

// The heights the row offers below the output's own, by the output's shape.
static const int rsWideHeights[] = { 2160, 1800, 1440, 1080, 900, 720, 540, 360, 0 };
static const int rsTallHeights[] = { 1200, 960, 768, 600, 480, 0 };

// 4:3 and 5:4 outputs take the second list; 16:10, 16:9 and wider the first.
static inline const int* RsHeights (int dispW, int dispH)
{
    return dispW * 2 < dispH * 3 ? rsTallHeights : rsWideHeights;
}

static inline int RsPctOfHeight (int h, int dispH)
{
    return (h * 100 + dispH / 2) / dispH;
}

// The row's fixed sizes for this output, largest first, as percentages. The
// output's own size is always the first. An output too small or too oddly
// shaped for two standard sizes below it gets the plain fractions instead.
// Returns how many it wrote; pcts holds RS_MAX_SIZES.
static inline int RsSizes (int dispW, int dispH, int* pcts)
{
    const int*	hs = RsHeights (dispW, dispH);
    int		n = 0, i, pct;

    pcts[n++] = RS_MAX_PCT;
    for (i = 0 ; hs[i] && n < RS_MAX_SIZES ; i++)
    {
	if (hs[i] >= dispH)
	    continue;
	pct = RsPctOfHeight (hs[i], dispH);
	if (pct >= RS_MIN_PCT && pct < pcts[n - 1])
	    pcts[n++] = pct;
    }
    if (n < 3)
    {
	pcts[1] = 75; pcts[2] = 67; pcts[3] = 50;
	n = 4;
    }
    return n;
}

// The size the world is drawn at for a percentage. Where the percentage is one
// of the row's standard sizes, it is that size exactly: 83% of 3840x2160 is
// 3200x1800, which no whole percentage multiplies out to. Otherwise each side
// is scaled, rounding down, as it was before the row named sizes.
static inline void RsRenderSize (int dispW, int dispH, int pct, int* w, int* h)
{
    const int*	hs = RsHeights (dispW, dispH);
    int		i;

    if (pct < RS_CLAMP_MIN) pct = RS_CLAMP_MIN;
    if (pct > RS_MAX_PCT)   pct = RS_MAX_PCT;

    *w = dispW * pct / 100;
    *h = dispH * pct / 100;
    for (i = 0 ; hs[i] ; i++)
	if (hs[i] < dispH && RsPctOfHeight (hs[i], dispH) == pct)
	{
	    *h = hs[i];
	    *w = (hs[i] * dispW + dispH / 2) / dispH;
	    break;
	}
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

// The row's next choice: Dynamic, then each fixed size from the largest down,
// then Dynamic again. A percentage the list does not hold (a hand-edited 60)
// moves to the next size below it. Dynamic leaves the percentage alone.
static inline void RsNextChoice (int dispW, int dispH, int* dynamic, int* pct)
{
    int	pcts[RS_MAX_SIZES];
    int	n = RsSizes (dispW, dispH, pcts), i;

    if (*dynamic)
    {
	*dynamic = 0;
	*pct = pcts[0];
	return;
    }
    for (i = 0 ; i < n ; i++)
	if (pcts[i] < *pct)
	{
	    *pct = pcts[i];
	    return;
	}
    *dynamic = 1;
}

// ---- The Dynamic choice (DOOM-0596) ----
//
// Each frame the renderer hands DrsStep the GPU time of the last frame it
// measured and the view's frame budget (docs/standards/performance.md: 1000/60
// ms rasterised, 1000/30 traced). It answers with the percentage to draw at.
//
// The model is that GPU time follows the pixel count, so the time at a new
// percentage is the old time times the square of the ratio. Some of a frame
// costs the same at any size, so a drop may take two moves to land. A rise
// goes a short step at a time, because each move disturbs the picture.
//
// The time it steers by is the FASTEST of the last few frames. Now and then
// a frame, or four or five in a row, takes three or four times as long for
// reasons no scale would fix (measured on a shared machine: 16 ms frames with
// bursts of 50-70 ms ones). Steering by an average dropped the scale for each.
//
// A size that has just proved too slow is capped out of reach for a while, and
// for twice as long each time a retry fails. Without the cap the scale flaps
// between two sizes for as long as the scene sits on the line.

#define DRS_AIM		0.85f	// aim this far under the budget: a late frame costs a whole refresh
#define DRS_RISE_BELOW	0.75f	// room to rise: under this share of the aim...
#define DRS_RISE_FRAMES	30	// ...for this many frames in a row
#define DRS_RISE_TO	0.92f	// a rise is sized to land here, as a share of the aim
#define DRS_RISE_DIV	12	// ...and is at most a twelfth of the scale
#define DRS_CAP_FRAMES	600	// how long a size that proved too slow stays capped...
#define DRS_CAP_MAX	4800	// ...doubling up to this while retries keep failing
#define DRS_HOLD_FRAMES	6	// after a move, wait for frames drawn at the new size
#define DRS_MIN_STEP	3	// never move less than this
#define DRS_WINDOW	8	// frames the fastest is taken over: longer than a hitch

typedef struct
{
    int		pct;		// the percentage in effect
    float	last[DRS_WINDOW]; // the newest GPU times at pct
    int		n;		// how many of them are filled
    float	ms;		// the fastest of them; 0 = too few yet
    int		calm;		// frames in a row with room to rise
    int		hold;		// frames left before the next move
    int		cap;		// no rise goes above this; 0 = no cap
    int		capAge;		// frames the cap has stood
    int		capFor;		// frames it stands for; 0 = the next cap starts short
    int		fromPct;	// the percentage before the last rise
    int		sinceRise;	// frames since that rise, counted to DRS_RISE_FRAMES + 1
} drs_t;

static inline void DrsReset (drs_t* d, int pct)
{
    d->pct  = pct < RS_MIN_PCT ? RS_MIN_PCT : (pct > RS_MAX_PCT ? RS_MAX_PCT : pct);
    d->n    = 0;
    d->ms   = 0.0f;
    d->calm = 0;
    d->hold = 0;
    d->cap  = 0;
    d->capAge = 0;
    d->capFor = 0;
    d->fromPct   = d->pct;
    d->sinceRise = DRS_RISE_FRAMES + 1;
}

static inline drs_t DrsStart (int pct)
{
    drs_t	d;

    DrsReset (&d, pct);
    return d;
}

static inline void DrsMove (drs_t* d, int pct)
{
    d->pct  = pct;
    d->n    = 0;
    d->ms   = 0.0f;
    d->calm = 0;
    d->hold = DRS_HOLD_FRAMES;
}

static inline int DrsStep (drs_t* d, float gpuMs, float budgetMs)
{
    float	aim = budgetMs * DRS_AIM;
    int		want, top, i;

    if (d->pct < RS_MIN_PCT || d->pct > RS_MAX_PCT)
	DrsReset (d, d->pct);
    if (!(gpuMs > 0.0f) || !(aim > 0.0f))
	return d->pct;

    if (d->cap && ++d->capAge >= d->capFor)
	d->cap = 0;
    if (d->sinceRise <= DRS_RISE_FRAMES && ++d->sinceRise > DRS_RISE_FRAMES && !d->cap)
	d->capFor = 0;		// the last rise held: the next cap starts short again

    // The frames just after a move were drawn, or at least measured, at the
    // old size. Leave them out.
    if (d->hold > 0)
    {
	d->hold--;
	return d->pct;
    }
    for (i = DRS_WINDOW - 1 ; i > 0 ; i--)
	d->last[i] = d->last[i - 1];
    d->last[0] = gpuMs;
    if (++d->n < DRS_WINDOW)
	return d->pct;
    d->n  = DRS_WINDOW;
    d->ms = d->last[0];
    for (i = 1 ; i < DRS_WINDOW ; i++)
	if (d->last[i] < d->ms)
	    d->ms = d->last[i];

    if (d->ms > aim)
    {
	// Too slow, every frame of the window: drop to where the model says
	// the aim is met.
	want = (int) ((float) d->pct * sqrtf (aim / d->ms));
	if (want > d->pct - DRS_MIN_STEP) want = d->pct - DRS_MIN_STEP;
	if (want < RS_MIN_PCT)            want = RS_MIN_PCT;

	if (d->sinceRise <= DRS_RISE_FRAMES)
	{
	    // The rise just made was the mistake: back under where it started.
	    d->cap    = d->fromPct;
	    d->capFor = d->capFor ? d->capFor * 2 : DRS_CAP_FRAMES;
	    if (d->capFor > DRS_CAP_MAX) d->capFor = DRS_CAP_MAX;
	}
	else
	{
	    // The scene got heavier: this size is out of reach for now.
	    d->cap    = d->pct - 1;
	    d->capFor = DRS_CAP_FRAMES;
	}
	d->capAge    = 0;
	d->sinceRise = DRS_RISE_FRAMES + 1;

	if (want != d->pct)
	    DrsMove (d, want);
	else
	    d->calm = 0;
    }
    else if (d->ms < aim * DRS_RISE_BELOW)
    {
	// Room to spare: rise only once it has lasted, and only by a step the
	// model says still lands under the aim.
	if (++d->calm >= DRS_RISE_FRAMES)
	{
	    want = (int) ((float) d->pct * sqrtf (aim * DRS_RISE_TO / d->ms));
	    top  = d->pct + d->pct / DRS_RISE_DIV;
	    if (top < d->pct + DRS_MIN_STEP) top = d->pct + DRS_MIN_STEP;
	    if (want > top)                  want = top;
	    if (d->cap && want > d->cap)     want = d->cap;
	    if (want > RS_MAX_PCT)           want = RS_MAX_PCT;
	    if (want >= d->pct + DRS_MIN_STEP || (want == RS_MAX_PCT && want > d->pct))
	    {
		d->fromPct   = d->pct;
		d->sinceRise = 0;
		DrsMove (d, want);
	    }
	    else
		d->calm = 0;
	}
    }
    else
	d->calm = 0;

    return d->pct;
}

#endif // RENDER_SCALE_H
