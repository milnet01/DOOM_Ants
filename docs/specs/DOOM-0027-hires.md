# DOOM-0027 — Raise the classic renderer's internal resolution

**Status:** Ready to implement — 6 `/cold-eyes` loops to clean; compile-time fixed-2× approach signed off by user (2026-06-12). The `V_DrawPatch` physical-coordinate class is exhaustively closed (Component F); the standing residual is Component E's mechanical position-arithmetic re-sweep.
**Roadmap:** 📋 planned (Phase 2 — "the spin"), `ROADMAP.md` DOOM-0027.
**Kind:** enhancement
**Depends on:** DOOM-0014 (SDL2 windowing — bigger/resizable window), shipped. DOOM-0004 (SDL2 video layer), shipped.

> **Design decision (signed off by user, 2026-06-12):** this spec adopts a
> compile-time fixed 2× over the roadmap's runtime-variable recommendation. See
> *Alternative considered — runtime variables*. The roadmap body (`ROADMAP.md:175`)
> is to be trimmed to point here, landed alongside the implementation.

## Contents

- **Goal** · **Background** — why a naïve resize breaks (the id "futile" warning)
- **Approach** — two coordinate spaces + a per-buffer-width scaler; three
  alternatives considered; verified assumptions; memory budget
- **Components** — A `doomdef.h` constants · B `v_video.c` scaler · C status bar ·
  D window size · E UI positioning audit · F raw self-blit paths
- **Verification** · **Out of scope** · **Cold-eyes loop log**

## Goal

Make the classic view render at **higher internal detail** so it looks sharp
instead of blocky when the window is enlarged — while keeping it looking and
feeling exactly like the original DOOM.

For a player: today the game draws its picture at 320×200 (about 64,000 pixels)
and the window just stretches that up, so big windows look chunky. After this
change the picture is drawn at **640×400** (about 256,000 pixels — four times
the detail), so walls, floors and monsters are crisp at a large window size. The
art style, the colours, the "DOOM look" are unchanged — it's the same game, just
not blurry-blocky when blown up.

This is the item DOOM-0014 explicitly deferred — its release note adds, in a
parenthetical: *"The picture is bigger; the internal detail is still the original
320x200 — true high-resolution rendering is a separate Phase 2 job."*
(`CHANGELOG.md:52`, `ROADMAP.md:65`). This spec is that Phase 2 job.

This spec covers a **fixed 2× (640×400)** internal resolution. Picking the
resolution at runtime, or going to 3×/4×, is explicitly deferred (see *Out of
scope*).

## Background — why it doesn't "just work"

The id authors left a warning sign on this exact road. At `doomdef.h:102-105`:

> *"It is educational but futile to change this scaling e.g. to 2. Drawing of
> status bar, menues [sic] etc. is tied to the scale implied by the graphics."*

— sitting right above `#define SCREEN_MUL 1` (`doomdef.h:106`) and
`#define SCREENWIDTH 320` / `#define SCREENHEIGHT 200` (`doomdef.h:112,114`).

That warning is the whole problem in one sentence. Three facts drive the design:

1. **The user-interface art is drawn pixel-for-pixel, no scaling.** The status
   bar, the menus, the heads-up text, the intermission "tally" screens and the
   end-of-game text are all bitmap graphics (DOOM calls them *patches*) authored
   for a 320-wide screen. They are blitted 1:1 — `V_DrawPatch` writes straight
   into the screen buffer at `screens[scrn] + y*SCREENWIDTH + x` and walks one
   `SCREENWIDTH` per row (`v_video.c:239,251,257`). If we simply make the buffer
   640 wide, those 320-wide graphics draw into the **top-left quarter** and the
   rest of the bar/menu is blank. That is precisely the "futile" the comment
   warns about.

2. **Every `v_video.c` primitive hardcodes `SCREENWIDTH` as the row stride for
   *all* screen buffers** (`v_video.c:187-188,239,304,428,466`). There are five
   buffers, `screens[0..4]` (`v_video.c:44`). Four are the full-frame buffers,
   carved at physical stride (`V_Init`, `:489-492`); the fifth, `screens[4]`, is
   the status-bar scratch, allocated at only `ST_WIDTH*ST_HEIGHT` = 320×32
   (`st_stuff.c:1470`). Today this is invisible because `SCREENWIDTH` == the
   scratch width == 320. The moment `SCREENWIDTH` ≠ 320, indexing the 320-wide
   scratch with a 640 stride overruns it — *before* any scaling is even
   considered. So "make the buffer bigger" cannot be a single global constant; the
   primitives must know each buffer's own width.

3. **The 3D view, by contrast, already scales for free.** Everything that draws
   the actual world is written in terms of `SCREENWIDTH`/`SCREENHEIGHT`, and the
   renderer's scratch buffers are *already sized for hi-res*: `r_draw.c:48-49`
   defines `MAXWIDTH 1120`, `MAXHEIGHT 832`, and `ylookup`/`columnofs` use those
   (`r_draw.c:70-71`). 640×400 fits inside that headroom with room to spare. The
   per-column/row arrays (`floorclip[SCREENWIDTH]`, `yslope[SCREENHEIGHT]`,
   `clipbot[SCREENWIDTH]`, … at `r_plane.c:69-93`, `r_things.c:81-82,845-846`)
   are sized by the constants, so they grow automatically at compile time.

So the job is **not** "rewrite the renderer". The renderer is ready. The job is:
let the 3D view render at 640×400, and teach the UI layer to blow its
320×200-authored art up to match — concentrated in the `v_video.c` primitives, so
the dozens of UI call sites keep their existing logical coordinates.

## Approach

Introduce **two coordinate spaces**, and make the `v_video.c` primitives scale
between them based on **which buffer** each draw targets:

- **Logical space — 320×200.** New constants `ORIGWIDTH 320`, `ORIGHEIGHT 200`
  in `doomdef.h`. *All UI code keeps positioning its art in this space*, exactly
  as the original code already does (a menu item at x=97, the status bar 32 rows
  tall, etc.). Nothing in the UI learns a new coordinate system.

- **Physical space — 640×400.** `SCREENWIDTH`/`SCREENHEIGHT` become the *render
  buffer* size (`#define SCREENWIDTH (ORIGWIDTH*HIRES)` …). The 3D world renderer
  and the full-frame buffers use these and render at genuine 640×400 detail.

- **Per-buffer width is the pivot.** Each of the five `screens[]` carries a known
  width: `screens[0..3]` = `SCREENWIDTH` (640, physical), `screens[4]` =
  `ORIGWIDTH` (320, the logical bar scratch — *kept at 320 on purpose*). A small
  `int screenwidth[5]` (set in `V_Init`, with `screenwidth[4]` set where
  `st_stuff.c` allocates the scratch) replaces the hardcoded `SCREENWIDTH` stride
  inside the primitives.

- **`v_video.c` scales a logical request to the destination buffer's width.**
  `V_DrawPatch`/`V_DrawPatchFlipped` take logical coordinates and a 320-authored
  patch, and write it scaled by `screenwidth[dest]/ORIGWIDTH` — i.e. ×2 (`HIRES`)
  into a 640-wide frame buffer, but ×1 into the 320-wide bar scratch.
  `V_CopyRect` takes all-logical args and scales *position and size* on each side
  by that buffer's own factor (Component B). The payoff:
  the status bar is *assembled at native 320×32 into the scratch with its code
  unchanged*, then `V_CopyRect`'d to the physical screen with an automatic ×2 —
  the bar comes out doubled and full-width with **no coordinate edits in the bar
  code at all** (see Component C). The same logical coordinates that already
  exist everywhere keep working.

This keeps the change **concentrated**: the scaling logic lives in `v_video.c`;
the UI subsystems keep their logical coordinates and only need (a) dimension
*macros* repointed from `SCREENWIDTH`/`SCREENHEIGHT` to `ORIGWIDTH`/`ORIGHEIGHT`,
and (b) a sweep for spots that used `SCREENWIDTH` as a *position* (Component E).
The 3D renderer, the SDL output, the automap and the screen-melt keep using
`SCREENWIDTH`/`SCREENHEIGHT` and get hi-res for free.

Why **integer 2×** specifically, and not an arbitrary factor: 640×400 has the
**identical aspect ratio** to 320×200 (`INV_ASPECT_RATIO 0.625`, `doomdef.h:107`
— i.e. 200/320), so DOOM's signature non-square-pixel "stretch" is preserved
bit-for-bit; there is no aspect-correction maths to add. A pure doubling also
makes the scaler a trivial 2×2 block copy with no sampling artefacts.

### Alternative considered — make the resolution a *runtime* variable (the approach the roadmap recommends)

**This spec deliberately overrides the roadmap on the central design decision.**
The DOOM-0027 roadmap body (`ROADMAP.md:175`) does not merely float runtime
variables — it calls Crispy Doom's runtime-variable approach the *proven path*:
*"make SCREENWIDTH/SCREENHEIGHT runtime variables instead of compile-time
#defines."* This spec rejects that for v1 in favour of compile-time constants.
The reversal is the reason this spec needs explicit sign-off (see the note under
*Status*); on acceptance, the roadmap body must be trimmed to "Design: see
`docs/specs/DOOM-0027-hires.md`" so the rejected method does not survive as a
confidently-wrong instruction (`documentation.md`: fix a doc the moment a change
makes it wrong).

Rejected for v1 because it is **substantially** more invasive for no extra
player benefit here:

- About fifteen file-scope arrays are declared with the resolution as their size
  — `short floorclip[SCREENWIDTH];` (`r_plane.c:69`), `clipbot[SCREENWIDTH]`
  (`r_things.c:845`), and so on. In C you cannot size a file-scope array with a
  runtime `int`; every one of these would have to become a heap allocation with
  matching alloc/free plumbing. That is exactly the churn the "futile" comment
  was about, multiplied across the renderer.
- The player goal is "less blocky", which a fixed 640×400 fully delivers.
  Choosing the number at runtime is a convenience feature, not the goal.

Keeping the resolution a **compile-time constant** lets all those static arrays
keep compiling unchanged and confines the work to the UI scaler. Runtime
selectability can be revisited as its own later item if it's ever wanted; the
two-space split and per-buffer-width machinery designed here are the foundation
it would build on, so nothing here is throwaway.

The runtime-variable approach is the one **Crispy Doom** takes (its medium-res
mode — reference implementation at `github.com/fabiangreffrath/crispy-doom`); the
two-coordinate-space idea (`ORIGWIDTH`/`ORIGHEIGHT` vs `SCREENWIDTH`/
`SCREENHEIGHT`) and the per-buffer-width pattern are borrowed from it directly,
just resolved at compile time instead of runtime.

### Alternative considered — render the 3D view to a separate hi-res buffer and composite

Keep the whole UI buffer at 320×200 untouched, render only the 3D view into a
second, larger buffer, and composite the two at output time. Rejected: it is
*more* complex, not less. The 3D view renderer writes into `screens[0]`, the
same buffer the UI then draws over (status bar along the bottom, menu/HUD on
top); splitting them means two buffers, two coordinate systems and a
compositing step, versus this spec's single shared 640×400 buffer.

### Alternative considered — make `screens[4]` physical and scale only in `V_DrawPatch`

Enlarge the bar scratch to 640×64 and have `V_DrawPatch` *always* scale by
`HIRES`. Rejected: the bar is assembled by drawing patches *into* the scratch
(`st_stuff.c:504,507`; `st_lib.c:125,235,287`) and *then* `V_CopyRect`'ing it to
the screen — so "always scale" double-scales (once drawing in, once copying out),
forcing a special "don't scale this copy" flag and doubling every bar-element
coordinate at its call site. Keying the scale to the destination buffer's width
(the chosen approach) makes the scratch a 1× target and the copy-out a 2× target
*automatically*, with zero coordinate churn in the bar code.

### Verified assumptions

Each checked against current source, not memory:

- **Renderer arrays fit 640×400.** `MAXWIDTH 1120`, `MAXHEIGHT 832`
  (`r_draw.c:48-49`); 640 ≤ 1120 and 400 ≤ 832. ✓
- **Full-frame buffers auto-size.** `V_Init` allocates `SCREENWIDTH*SCREENHEIGHT*4`
  and carves `screens[0..3]` from it (`v_video.c:489-492`). ✓
- **SDL output already follows the constants.** `i_video.c` creates its texture
  at `SCREENWIDTH,SCREENHEIGHT` (`:335-337`), sets logical size to the same
  (`:333`), and its blit loop iterates `SCREENWIDTH`/`SCREENHEIGHT` (`:229-234`).
  Bumping the constants makes the output path render 640×400 with **no edit** to
  the blit. ✓
- **Screen-melt auto-scales.** `f_wipe.c` takes `width`/`height` as parameters;
  its callers pass `SCREENWIDTH,SCREENHEIGHT` (`d_main.c:228,329`). The melt and
  its `V_DrawBlock` restore (`f_wipe.c:257`) operate on *physical* full-frame
  snapshots — raw, no logical scaling. Follows the constants. ✓
- **The UI really is drawn 1:1 today** (the thing we must change). `V_DrawPatch`
  blits with no scale factor (`v_video.c:239,251,257`). ✓
- **`dirtybox` is dead.** `V_MarkRect` writes it (`v_video.c:149-150`); grep finds
  no reader outside `v_video.c`, and the SDL2 path re-blits the whole frame every
  frame. Its coordinate space is a don't-care (Component B). ✓
- **`SCREEN_MUL`'s only live use is `ST_HEIGHT`** (`st_stuff.h:32`); its two other
  appearances (`doomdef.h:113,115`) are in comments. Safe to retire. ✓

**Memory budget.** The resolution change quadruples the per-pixel buffers (figures
in decimal KB/MB; all 8-bit-paletted except the SDL texture):

| Allocation | 320×200 | 640×400 | Δ |
|------------|---------|---------|---|
| `screens[0..3]` 8-bit (`v_video.c:489`) | 256 KB | 1.0 MB | +768 KB |
| SDL texture, 32-bit ARGB (`i_video.c:335`) | 256 KB | 1.0 MB | +768 KB |
| renderer per-column/row arrays (`floorclip` etc.) | small, ×~2 | — | a few KB |
| `screens[4]` status-bar scratch (`st_stuff.c:1470`) | 10 KB | 10 KB | 0 (stays logical 320×32) |
| `ylookup`/`columnofs` (`r_draw.c:70-71`) | fixed `MAXHEIGHT`/`MAXWIDTH` | same | 0 (pre-allocated) |

Total added resident memory ≈ **1.5 MB** — negligible on any machine that runs a
modern desktop. The full-frame `screens[0..3]` block comes from `I_AllocLow`, a
plain `malloc` (`v_video.c:489` → `i_system.c:151`), independent of the DOOM
zone-heap size, so its ~1 MB never strains the zone. (`screens[4]`, the bar
scratch, is a small `Z_Malloc` from the zone — `st_stuff.c:1470` — but it stays
320×32 = 10 KB, unchanged by this work.)

## Components / affected files

**A. `doomdef.h` — the constants (the pivot of the change).**
Define the resolution as a single named multiplier so the factor lives in one
place (and a future 3×/runtime item has one knob to turn):

```c
#define ORIGWIDTH    320              // logical UI canvas, unchanged
#define ORIGHEIGHT   200
#define HIRES        2                // internal-resolution multiplier
#define SCREENWIDTH  (ORIGWIDTH*HIRES)   // physical render buffer = 640
#define SCREENHEIGHT (ORIGHEIGHT*HIRES)  // physical render buffer = 400
```

Every doubling factor elsewhere in this spec (the scaler, the status-bar physical
height, the automap bar reservation) is written in terms of `HIRES`, never a bare
`2`. Replace the obsolete "futile to change" comment block (`doomdef.h:102-105`)
with a short note pointing at this spec, since the thing it warned about is now
handled.

**`SCREEN_MUL` is retired.** It is `1` today (`doomdef.h:106`) and its only live
use is `ST_HEIGHT 32*SCREEN_MUL` (`st_stuff.h:32`), which Component C rewrites in
logical terms. Remove the `SCREEN_MUL` define and that one use in the same
change (leaving a `*1` factor dangling would rot). `BASE_WIDTH` (`:100`) and
`INV_ASPECT_RATIO` (`:107`) are unreferenced by this change and left untouched.

**B. `v_video.c` — the scaler (the bulk of the new logic).**
Add `int screenwidth[5]` and initialise **all five entries in `V_Init`** —
`screenwidth[0..3] = SCREENWIDTH` and `screenwidth[4] = ORIGWIDTH` (the scratch
width is the compile-time constant `ORIGWIDTH`, so `V_Init` can set it even though
`ST_Init`/`st_stuff.c:1470` allocates the scratch *pointer* later). Initialising
all five in one place keeps the array fully valid before any draw and avoids a
cross-module write or an init-order race (`ST_Init` runs after `V_Init`, so a
draw to `screens[4]` before `ST_Init` would otherwise read `screenwidth[4]==0` and
silently scale to nothing). Replace the hardcoded `SCREENWIDTH` stride inside the
primitives with `screenwidth[scrn]`, and make the three logical-coordinate
primitives scale to the destination buffer's width:

| Function | Location | Change |
|----------|----------|--------|
| `V_DrawPatch` | `:204` | index by `screenwidth[dest]`; scale a 320-patch by `screenwidth[dest]/ORIGWIDTH` (→ ×2 into a frame buffer, ×1 into the 320 scratch) |
| `V_DrawPatchFlipped` | `:271` | same, mirrored |
| `V_CopyRect` | def `:158`, blit `:187-188` | **all eight args are logical** — see coordinate rule below |
| `V_DrawBlock` | def `:405`, blit `:428` | **raw, not a logical primitive** — index by `screenwidth[dest]`; copies a caller-supplied *physical* block 1:1 (only caller: `f_wipe.c:257`, physical dims). No logical scaling. |

**`V_CopyRect` coordinate rule (load-bearing — the whole bar pipeline rests on it).**
All the coordinate/size arguments of the real signature
`V_CopyRect(srcx, srcy, srcscrn, width, height, destx, desty, destscrn)`
(`v_video.c:158-166` — note the screen indices are interleaved, not trailing) are
**logical** (320-space). Each side then scales by *its own buffer's* factor: the source rectangle is read from `screens[src]` at
`(srcx, srcy)` size `(width, height)`, each multiplied by
`screenwidth[src]/ORIGWIDTH`; the destination is written to `screens[dest]` at
`(destx, desty)` size `(width, height)`, each multiplied by
`screenwidth[dest]/ORIGWIDTH`. A 320-scratch→640-screen copy thus reads at ×1 and
writes at ×2 — *positions included*, which is what places the bar correctly
(traced in Component C). **This changes the copy *body*, not just the position
arithmetic:** today `V_CopyRect` does a `memcpy` per row at `SCREENWIDTH` stride
(`v_video.c:187-194`) — fine when src and dest are the same width, but a ×1-read /
×2-write cannot go through one `memcpy`. It becomes a pixel-expanding blit: walk
the source at its stride, and for each source pixel write a `(dest/src)`×`(dest/src)`
block at the destination stride (a 2×2 block for 320→640). When src and dest
factors are equal (any physical→physical copy, if one is ever added) it degenerates
to the original 1:1 `memcpy`.

`V_DrawPatchDirect` (`:337`) forwards to `V_DrawPatch` (`:343`) and inherits the
scaling for free (its original VGA-planar body is **commented out**, `:345-395`,
verifiably dead under SDL2 — leave it).

Bounds checks must be evaluated in the space each primitive's args live in —
per primitive, not blanket:

- **`V_DrawPatch`/`V_DrawPatchFlipped`/`V_CopyRect`** (logical args): the guards
  (`x+SHORT(patch->width) > SCREENWIDTH` at `v_video.c:223`; `:290`; the src+dest
  pair at `V_CopyRect` `:173-178`) compare the *logical* args against `ORIGWIDTH`/
  `ORIGHEIGHT`. For `V_CopyRect` *both* the src and dest rects are logical (a
  buffer's logical width is `ORIGWIDTH` regardless of its physical width), so both
  compare against `ORIGWIDTH` — the physical fit is guaranteed by the scaling.
- **`V_DrawBlock`** (physical args, `:417`): stays a physical `SCREENWIDTH` guard.

Getting this split right is the core correctness task. (The `screenwidth[scrn]`
substitution for `V_DrawBlock` is a no-op for its one live caller — `f_wipe.c:257`
targets `screens[0]`, whose width *is* `SCREENWIDTH` — but it is done for
uniformity so no primitive keeps a hardcoded stride.)

> **`V_MarkRect` / `dirtybox` — don't-care, pass through.** The primitives call
> `V_MarkRect` (`:185,236,301,426`), which accumulates `dirtybox` (`:46,149-150`).
> `dirtybox` is **written but never read** (no consumer outside `v_video.c`; the
> SDL2 output re-blits the whole frame every frame). Pass the logical args through
> unchanged; do not spend effort scaling them. (Noted because it looks like it
> should matter and it doesn't.)

**C. `st_stuff.h` / `st_stuff.c` / `st_lib.c` — status bar.**
Because the bar is assembled into the 320-wide scratch (`screens[4]`, kept at
`ORIGWIDTH` via `screenwidth[4]`) and `V_CopyRect`'d to the physical screen,
**the bar element code keeps every one of its logical coordinates unchanged** —
`V_DrawPatch(ST_X,0,BG,…)` (`st_stuff.c:504,507`) draws ×1 into the 320 scratch,
and the copy-out `V_CopyRect(ST_X,0,BG,ST_WIDTH,ST_HEIGHT,ST_X,ST_Y,FG)`
(`st_stuff.c:509`) and the per-digit copies (`st_lib.c:125,235,287`) come out ×2
automatically. **Worked trace:** that copy-out reads the bar from `screens[4]`
(scale 1) at `(0,0)` size 320×32, and writes to `screens[0]` (scale 2) at
`(ST_X·2, ST_Y·2) = (0, 168·2) = (0, 336)` size 640×64 — flush along the bottom of
the 400-row frame. The bar-internal positions (`ST_X`, `ST_FX`, `ST_X2`, and the
digit/face coordinates in `st_lib.c`) all stay logical and unchanged — they draw
×1 into the 320 scratch. The only edits are the dimension/position macros (`st_stuff.h`
for `ST_HEIGHT`/`ST_WIDTH`/`ST_Y`; `st_stuff.c` for `ST_MAPTITLEX`), repointed
from physical to *logical* space — exact before → after:

| Macro | Today | After |
|-------|-------|-------|
| `ST_HEIGHT` | `32*SCREEN_MUL` | `32` (logical; `SCREEN_MUL` retired — Component A) |
| `ST_WIDTH` | `SCREENWIDTH` | `ORIGWIDTH` |
| `ST_Y` | `(SCREENHEIGHT - ST_HEIGHT)` | `(ORIGHEIGHT - ST_HEIGHT)` → 168 logical |
| `ST_MAPTITLEX` | `(SCREENWIDTH - ST_MAPWIDTH*ST_CHATFONTWIDTH)` (`st_stuff.c:262-263`) | `(ORIGWIDTH - …)` |

The scratch allocation `screens[4] = Z_Malloc(ST_WIDTH*ST_HEIGHT, …)`
(`st_stuff.c:1470`) needs **no edit** — with the macros repointed it is
`ORIGWIDTH*32` = 320×32, exactly as today (correct by construction). The bar thus
occupies the bottom `HIRES*ST_HEIGHT` = 64 physical rows — the figure the
automap's and renderer's bar reservations (Component F) must match.

**D. `i_video.c` — window size.**
Default window scale is `scale = 4` (`i_video.c:47`), i.e. a 320×4 = 1280-wide
window today. With the internal buffer now 640, default scale should become `2`
so the *physical window stays the same size* (640×2 = 1280) — same window,
sharper picture. Update the adjacent comment "Integer scale of the 320x200
image" (`i_video.c:46`) to read 640×400 in the same edit, so it doesn't rot.
The `-scale N` / `-2` / `-3` switches (`:296-304`) keep working unchanged — they
multiply the (now larger) logical size, and `SDL_RenderSetLogicalSize`
(`:333`, set to `SCREENWIDTH,SCREENHEIGHT`) handles any integer scale over the
640×400 logical size with nearest-neighbour upscale. The texture/logical-size/
blit (Verified assumptions) need no edit beyond following the constants.

**E. UI positioning audit — art drawn through the scaler but positioned with the
wrong constant.**
`m_menu.c`, `wi_stuff.c`, `hu_lib.c`/`hu_stuff.c` and `f_finale.c` draw their art
through `V_DrawPatch`/`V_DrawPatchDirect`/`V_CopyRect`, so the *art itself* scales
for free once B lands. (The HUD and menu character draws go via
`V_DrawPatchDirect` — `hu_lib.c:122,137`, `m_menu.c:1335` etc. — which forwards to
`V_DrawPatch`; grep that symbol when sweeping.) The trap is that they compute the
art's **position** using `SCREENWIDTH`/`SCREENHEIGHT` as if those were the logical
canvas — which they no longer are. Every such use must be reclassified.

**The classification rule:** if a value is a position or extent *in the 320×200
art canvas*, it is logical → `ORIGWIDTH`/`ORIGHEIGHT`. If it is a stride or byte
offset *into the physical pixel buffer*, it stays physical → `SCREENWIDTH`/
`SCREENHEIGHT`. Done by reading each site, never blanket find-replace.

**Worked example (the pattern to follow):** `wi_stuff.c:1451`
`WI_drawPercent(SCREENWIDTH - SP_STATSX, …)` right-aligns the kills percentage,
`SP_STATSX` (=50) being a logical inset. It must become `ORIGWIDTH - SP_STATSX`
(= x 270 logical, scaled to physical 540). Left as-is it computes 640−50 = 590
*logical*, which scales to physical 1180 — off-screen. **The most common form of
this is patch-centering, `(SCREENWIDTH - patch->width)/2` → `(ORIGWIDTH -
patch->width)/2`** — treat every centering divide that way.

**Enumerated landmines (from a source sweep — repoint logical→`ORIG*`):**

| File:line | Code | Fix |
|-----------|------|-----|
| `m_menu.c:1333` | `cx+w > SCREENWIDTH` (menu text wrap) | `ORIGWIDTH` |
| `wi_stuff.c:88` | `SP_TIMEY (SCREENHEIGHT-32)` | `(ORIGHEIGHT-32)` |
| `wi_stuff.c:426,432,444,450` | `(SCREENWIDTH - SHORT(p->width))/2` (level-name / Finished / Entering centering) | `(ORIGWIDTH - …)/2` |
| `wi_stuff.c:477,479` | `right < SCREENWIDTH && bottom < SCREENHEIGHT` (`WI_drawOnLnode` "you-are-here" fit test; `lnodes` coords are logical) | `ORIGWIDTH`/`ORIGHEIGHT` |
| `wi_stuff.c:1451,1454,1457` | `SCREENWIDTH - SP_STATSX` | `ORIGWIDTH - …` |
| `wi_stuff.c:1460,1464` | `SCREENWIDTH/2 ± SP_TIMEX` | `ORIGWIDTH/2 ± …` |
| `wi_stuff.c:1465` | `SCREENWIDTH - SP_TIMEX` (full-width inset, **not** half) | `ORIGWIDTH - …` |
| `hu_lib.c:120,128,135` | `x+w > SCREENWIDTH` (text bounds) | `ORIGWIDTH` |
| `f_finale.c:321` | `cx+w > SCREENWIDTH` (text wrap) | `ORIGWIDTH` |
| `f_finale.c:677,693` | `(SCREENWIDTH-13*8)/2`, `(SCREENHEIGHT-8*8)/2` ("THE END" card centering) | `(ORIGWIDTH-…)/2`, `(ORIGHEIGHT-…)/2` |

(`wi_stuff.c:408` `memcpy(screens[0], screens[1], SCREENWIDTH*SCREENHEIGHT)` is a
full-buffer copy — genuinely physical, stays. That is the rule working both ways.)

**Scope of this table.** It enumerates the *position-arithmetic* class —
`SCREENWIDTH`/`SCREENHEIGHT` used inside a bound or a centering divide. That class
is the one to re-sweep at implementation (read each `SCREEN*` in a UI source
file). The *other* class — a patch drawn through `V_DrawPatch` with physical
coordinates — is **closed**, not open: Component F's per-site sweep classified all
~90 `V_DrawPatch*` call sites and found exactly three physical ones. (`m_misc.c:71`
`M_DrawText` — which has the same `x+w > SCREENWIDTH` bound at `:91` — is **dead
code**, no callers besides its `m_misc.h:53` declaration, so it is deliberately
excluded.)

**F. Raw self-blitting paths that bypass the scaler — the real trap.**
These write pixels into `screens[0]` with their *own* loops, so Component B's
scaling never reaches them. They must be handled explicitly:

- **`f_finale.c:610` `F_DrawPatchCol` + `:659-670` `F_BunnyScroll`** — the
  "THE END" bunny scene (DOOM 1 / Ultimate Doom). `F_DrawPatchCol` blits a patch
  column 1:1 (`dest += SCREENWIDTH`, `:634`); the scroll math hardcodes the patch
  width `320` (`:659-661,667-670`) while iterating `x < SCREENWIDTH` (`:665`,
  now physical). Drawn unchanged at 640 it fills the **left half only**.
  **Contract (decided): render the *scroll* wholly in 320 space, then 2×-blit it;
  the END cards stay direct.** `F_BunnyScroll` does two things — the column-scroll
  loop (`:665-670`, via `F_DrawPatchCol`), then it draws the "END0…END6" cards on
  top via `V_DrawPatch(…,0,…)` to `screens[0]` (`:677-678,693`). Handle them
  differently:
    1. **The scroll → private scratch.** Add a private `static byte` 320×200
       scratch *local to `f_finale.c`* (not a `screens[]` slot). Point only the
       scroll loop / `F_DrawPatchCol` at it: the `dest += SCREENWIDTH` stride
       (`:634`) and the `x < SCREENWIDTH` bound (`:665`) become `ORIGWIDTH`, at
       which point the scene is wholly logical and the hardcoded `320` constants
       (`:659-661,667-670`) are already correct. The scroll overwrites every
       column every frame, so the scratch needs **no per-frame clear**.
    2. **2×-blit the scratch into `screens[0]`** with a small dedicated loop (each
       source pixel → a 2×2 block).
    3. **The END cards stay `V_DrawPatch(…,0,…)` to `screens[0]`**, drawn *after*
       the scratch-blit (so they land on top), and scale for free via Component B
       — their `(SCREENWIDTH-13*8)/2` / `(SCREENHEIGHT-8*8)/2` centering is the
       Component E fix (→ `ORIG*`), not a Component F concern. Ordering matters:
       blit-then-cards, or the cards are clobbered.
  **Why a private scratch, not a registered `screens[5]`:** the `screens[]` and
  `screenwidth[]` arrays are hard-sized at 5 (`v_video.c:44`), and six *live*
  `(unsigned)scrn>4` RANGECHECK guards (`v_video.c:179,180,226,293,420,460`; a
  seventh at `:362` is inside the dead commented-out `V_DrawPatchDirect` body)
  assume that bound — growing the array to feed `V_CopyRect` would be a global
  change to satisfy one local screen. The ~8-line doubling loop duplicates
  `V_CopyRect`'s doubling but keeps the finale's entire blast radius inside
  `f_finale.c`.
- **`f_finale.c:279-287` `F_TextWrite` flat-tile background** — `memcpy`s a 64×64
  flat across `SCREENWIDTH/64` columns × `SCREENHEIGHT` rows. This **auto-fills**
  the physical buffer (it is a repeating texture, not positioned art) and renders
  at native 1× density — consistent with how the 3D view draws floor/ceiling
  flats. **No fix needed**; noted as a deliberate density choice (the flat stays
  fine-grained while the patches over it are doubled — matches vanilla intent).
- **`hu_lib.c:157-165` HUD-line erase** — `lh = SHORT(l->f[0]->height)+1`
  (`:157`) is a *logical* font height, and the loop `for (y=l->y,
  yoffset=y*SCREENWIDTH; y<l->y+lh; …)` (`:158`) walks logical rows while the
  stride `SCREENWIDTH` is physical. **Contract:** the chars are drawn scaled
  (×`HIRES`) via `V_DrawPatchDirect`, so the erase must cover the physical rows
  they occupy — multiply both the start `l->y` and the height `lh` by `HIRES`
  (`for (y=HIRES*l->y; y<HIRES*(l->y+lh); …)`). The full-width / border run
  lengths (`SCREENWIDTH`, `viewwindowx`, `viewwidth`, `:161-165`) are already
  physical and stay. Miss this and the erase under-covers by half → text
  ghosting.
- **`am_map.c:223` `finit_height = SCREENHEIGHT - 32`** — the `32` is the bar
  height in physical pixels and the bar is now `HIRES*ST_HEIGHT` = 64 tall, so
  this must become `SCREENHEIGHT - HIRES*ST_HEIGHT`, else the automap overlaps the
  bar. One-line fix. (The map-drawing transforms themselves auto-scale via
  `f_w`/`f_h` seeded from `finit_width = SCREENWIDTH`, `am_map.c:222`; the map
  draws into `fb = screens[0]`, `:464`.)
- **`r_draw.c:52` `#define SBARHEIGHT 32`** — the renderer's *own* copy of the
  physical bar height, used to reserve the bottom strip: `viewwindowy = (SCREENHEIGHT
  - SBARHEIGHT - height)>>1` (`:716`), the back-screen fill bound
  `y < SCREENHEIGHT-SBARHEIGHT` (`:759`), and the border centring `top =
  ((SCREENHEIGHT-SBARHEIGHT)-viewheight)/2` (`:854`). This is the exact twin of the
  `am_map.c:223` literal — the bar is now 64 physical rows, so `SBARHEIGHT` must
  become `HIRES*32` (= `HIRES*ST_HEIGHT`), else the reduced 3D view draws 32 rows
  *under* the bar. (`r_draw.c:875` `V_MarkRect(0,0,SCREENWIDTH,SCREENHEIGHT-SBARHEIGHT)`
  feeds the dead `dirtybox`; harmless but update for consistency.)
- **Physical-coordinate `V_DrawPatch` sites — the complete set (3).** A patch
  drawn through `V_DrawPatch` whose x/y come from *physical* render geometry
  (rather than logical 320-space) collides with Component B's "args are logical"
  rule — the already-physical coordinate gets doubled and the art lands off-place.
  An exhaustive sweep of all ~90 `V_DrawPatch`/`V_DrawPatchDirect`/`V_CopyRect`
  call sites across the engine found **exactly three** such sites (every other
  site passes logical coords and is handled by Component B/C/E):
    1. **`r_draw.c:777-808` `R_FillBackScreen` reduced-view border** — tiles the
       `brdr_*` edge patches around a shrunk viewport via eight `V_DrawPatch(
       viewwindowx+x, …, 1, patch)` calls stepping `x += 8` across
       `scaledviewwidth`. `viewwindowx`/`viewwindowy`/`scaledviewwidth`/`viewheight`
       are physical (`viewwindowx=(SCREENWIDTH-width)>>1`, `:706`).
    2. **`am_map.c:1320` automap mark numbers** — `V_DrawPatch(fx, fy, FB,
       marknums[i])` where `fx=CXMTOF(...)`, `fy=CYMTOF(...)` are physical
       map-frame pixel coords (from `f_w`/`f_h`). The *map lines* self-blit raw
       into `fb` and auto-scale; only the dropped-mark digits route through
       `V_DrawPatch`.
    3. **`d_main.c:311` pause pic** — `V_DrawPatchDirect(viewwindowx +
       (scaledviewwidth-68)/2, y, 0, "M_PAUSE")`, `y=viewwindowy+4` (physical) —
       the `68` is the patch's logical width, the rest is physical view geometry.
  **Shared contract:** divide the physical view-geometry terms by `HIRES` at each
  call site so the patch passes through the standard scaled `V_DrawPatch` in
  logical space (e.g. border: loop over `scaledviewwidth/HIRES` step 8 at
  `viewwindowx/HIRES`; the 8-px `brdr_*` tile → 16-px physical). The geometry is
  `HIRES`-aligned by construction (it derives from the `HIRES`-multiple
  `SCREENWIDTH`/`SCREENHEIGHT`). **This set is now closed** — the per-site sweep
  is the proof, superseding Component E's "individually classify" caveat for the
  `V_DrawPatch` class specifically.

**No change needed (follow the constants):** the 3D *world* renderer
(`r_main`/`r_bsp`/`r_segs`/`r_plane`/`r_things`, and `r_draw.c`'s column/span
drawers — **but not** `r_draw.c`'s `SBARHEIGHT` and `R_FillBackScreen` border,
carved out just above), the full-frame buffers (`v_video.c` `V_Init`), `f_wipe.c`,
the screenshot path (`m_misc.c:537-538` `WritePCXfile(linear, SCREENWIDTH,
SCREENHEIGHT)` reads `screens[2]` and writes a PCX at the physical size — yields a
640×400 screenshot, correct by following the constants), the devparm FPS dots
(`i_video.c:221-223` `screens[0][(SCREENHEIGHT-1)*SCREENWIDTH + i]` — raw self-blit
at physical constants, correct as-is), and the rest of the SDL output in
`i_video.c` (blit/texture/logical-size). Listed so the reader knows they were
considered and deliberately excluded.

## Verification

- **Sharper world, same look:** boot a level at a large window; walls/floors/
  monsters are visibly crisper than before, colours and style unchanged. The 3D
  view is genuinely 640×400, not a stretched 320×200.
- **UI fills the screen correctly:** the status bar spans the full width and sits
  flush at the bottom; the menu, the heads-up message line, the automap overlay,
  the between-level tally screen and the end-game text are all complete and
  correctly placed — no art stuck in a corner, no blank half-bar (the specific
  failure the id comment warned about).
- **Finale & intermission specifically:** play to a level-end (intermission tally,
  centred level names) and to the game end (the bunny "THE END" scroll and the
  cast call) — both render full-width and correctly, since those are the
  raw-blit/centring paths most likely to break (Components E, F).
- **Screen-melt intact:** the wipe between screens (e.g. starting a level) melts
  over the whole frame.
- **Window size unchanged by default:** with no `-scale` switch the window opens
  the same physical size as before; `-scale N`, `-2`, `-3` still work.
- **Aspect preserved:** the picture keeps DOOM's classic proportions (no new
  letterboxing or squashing) — confirmed by the identical 0.625 ratio.
- **Performance (measured, with a pass threshold):** run the engine's built-in
  benchmark `linuxxdoom -timedemo demo1` before and after on the same machine.
  `-timedemo` (`G_TimeDemo`, `g_game.c:1639`) plays the demo flat-out; the result
  *"timed N gametics in M realtics"* is printed by `G_CheckDemoStatus` via
  `I_Error` on exit (`g_game.c:1668`), giving `N*35/M` FPS. **Pass = after-figure
  ≥ 35 FPS (i.e. faster than real time) AND ≥ 0.5 × the before-figure.** Record
  both numbers in the commit message. If the after-figure falls below that, the
  change is **blocked pending profiling** (the 8-bit column/span drawers in
  `r_draw.c` are the suspect) — not accepted with a known regression.
- **Build:** `make` compiles and links clean, no new warnings.

## Out of scope (YAGNI)

- **Runtime-selectable resolution** and **factors above 2×** (3×/4×, arbitrary
  widths) — deliberately deferred; this item ships a fixed 640×400. The two-space
  design and per-buffer-width machinery here are the foundation a later runtime
  item would extend.
- **True-colour / 32-bit rendering** — the engine stays 8-bit paletted; only the
  pixel count changes.
- **Aspect-ratio correction** (square-pixel / widescreen modes) — out of scope;
  the classic 4:3-displayed-16:10 stretch is intentionally preserved.
- **Anything in the 3D-renderer-overhaul (Phase 2 "the spin")** — this is the
  *classic* renderer at higher res, not the ray-traced renderer (DOOM-0026,
  planned, is the item that will keep the two renderers selectable).

## Cold-eyes loop log

- **2026-06-12 — 6 loops to clean; compile-time approach signed off by user.**
  Three independent cold reviewers per loop (accuracy / implementability /
  cross-doc), each briefed only against current source — no prior-loop context.
  - **L1:** accuracy nits (line numbers; the `V_DrawPatchDirect` note invented a
    non-existent "sibling"); structure (memory budget mid-list); cross-doc (status
    line omitted roadmap status; the runtime-vs-compile-time reversal was framed
    dishonestly as "floated"). All fixed; honest roadmap-override framing begun.
  - **L2:** the core mechanism was wrong — "`V_DrawPatch` always scales ×HIRES"
    breaks because the bar scratch `screens[4]` is indexed with the global
    `SCREENWIDTH` stride. Reworked to a **per-buffer-width** scaler (screens[0..3]
    = 640, screens[4] = 320). Component E's landmine table had missed the
    highest-volume class (`(SCREENWIDTH-w)/2` centring). Added FPS-measure method
    + memory budget.
  - **L3:** `V_CopyRect` position-scaling was unspecified (the bar wouldn't land at
    physical y=336); the finale bunny-scratch had no `screens[]` slot. Added the
    coordinate rule + worked trace; chose a private `f_finale.c` scratch over
    growing the global array. Fixed the "menus"→"menues [sic]" misquote.
  - **L4:** `V_CopyRect`'s body must become a pixel-doubling blit (not a `memcpy`
    stride-patch); `screenwidth[]` init moved wholesale into `V_Init` (init-order
    race); the finale END cards must draw *after* the scroll-blit (ordering
    pinned). Zone-heap framing scoped to `screens[0..3]`.
  - **L5:** the blanket "`r_draw`: no change needed" was wrong —
    `R_FillBackScreen`'s reduced-view border draws via `V_DrawPatch` at *physical*
    coords, and `SBARHEIGHT 32` under-reserves the now-64px bar (twin of the
    automap landmine). Carved both out of the exclusion; added contracts.
  - **L6:** the implementability lane found two more `V_DrawPatch`-with-physical-
    coords sites of the L5 class (`am_map.c:1320` mark numbers; `d_main.c:311`
    pause pic). Rather than chase them one loop at a time, did the **definitive
    sweep**: classified all ~90 `V_DrawPatch*`/`V_CopyRect` call sites by
    coordinate source — exactly **three** are physical (the two above + the L5
    `r_draw` border), all now under one Component F contract; every other site is
    logical. Accuracy and cross-doc lanes returned clean (only line-range/quote
    nits, fixed). `m_misc.c:71 M_DrawText` noted dead.
  - **Convergence:** accuracy and cross-doc lanes were clean from L3 on; the core
    design (per-buffer-width scaler, status-bar pipeline, finale split) is solid
    and citation-accurate. The implementability lane surfaced additional affected
    code paths through L5–L6 — a signal the change is genuinely wide-reaching (~12
    files) — until the L6 exhaustive `V_DrawPatch` sweep **closed** that class
    definitively (it is no longer "the known set"). User signed off the
    compile-time fixed-2× approach (over the roadmap's runtime variant). Spec is
    implementation-ready; the only standing residual is Component E's
    position-arithmetic re-sweep, which is mechanical given the classification
    rule.
