# DOOM-0147 (Parts B & C) — Authentic widescreen for the classic renderer

**Status:** Part B — **implemented** 2026-06-30 (commit `8fb4d66`; builds clean
Linux + Windows; awaiting visual confirmation on a true-widescreen display — see
Implementation notes). Part C — **cold-eyes converged** 2026-07-01 (3 loops; see the
Part C cold-eyes loop log at the end of this doc), cleared for implementation; not
yet implemented.
**Roadmap:** DOOM-0147 (Part A shipped 16e076b). Part C, **once shipped**, completes
DOOM-0147's 4:3-vs-widescreen aspect choice — the roadmap entry stays 🚧 until
then. The static-screen side-gaps on true-widescreen displays are a **separate**
open item, **DOOM-0151** — closing DOOM-0147 does not close it.
**Kind:** fix / enhancement.
**Depends on:** DOOM-0027 (shipped) — reuses its `ORIGWIDTH`/`SCREENWIDTH`/`HIRES` split.

## Contents

- [Goal](#goal)
- [Background — why it doesn't "just work"](#background--why-it-doesnt-just-work)
- [Approach](#approach)
- [Alternatives considered](#alternatives-considered)
- [Verified assumptions](#verified-assumptions)
- [Components / affected files](#components--affected-files)
- [Verification](#verification)
- [Out of scope (YAGNI)](#out-of-scope-yagni)
- [Cold-eyes loop log](#cold-eyes-loop-log)
- [Part C — aspect toggle + fill-to-screen (persisted)](#part-c--aspect-toggle--fill-to-screen-persisted)

## Goal

Make the **Classic** (software) renderer fill the whole monitor on displays
**wider than 4:3** — 16:9, 16:10, ultrawide — by showing **more of the level to
the left and right**, not by stretching the 4:3 picture sideways. Vertically the
view is identical to the 1993 original, so it still *feels* like DOOM. This is
the well-known **"Hor+"** (horizontal-plus) technique.

Concretely, on a friend's 16:9 laptop the player sees further to the sides
instead of black pillars.

**Honest scope limit — displays *narrower* than 4:3 (e.g. 5:4).** Hor+ only adds
width, so it can only help screens that are *wider* than 4:3. The user's 5:4
(1280×1024) monitor is slightly *taller* than 4:3, so this feature leaves it at
authentic 4:3 with the thin top/bottom bars from Part A intact — there is no
spare width to fill. Removing those specific bars needs either a small optional
fill-stretch (a Part C toggle) or vertical-plus rendering (out of scope, changes
the classic feel). This is a real limitation of the technique, not a bug.

This is **Part B** of DOOM-0147. Part A (present the 4:3 buffer at authentic 4:3,
commit 16e076b) already shipped — roadmap-tracked only, with no standalone spec,
as it was a small present-time change; the "Part B" title here does not imply a
missing sibling doc. Part C (a menu toggle between *4:3* and *Widescreen*, plus
a fill-to-screen stretch, persisted) follows this — see the Part C section for its
current scope (the "Fill Screen" toggle is a first-class all-display control, not
the 5:4-only option this line originally imagined).

## Background — why it doesn't "just work"

DOOM's software renderer draws the world one vertical column at a time across the
view width. Two facts make widescreen non-trivial:

1. **A wider buffer alone does the wrong thing.** The projection sets a fixed
   90° horizontal field of view (`FIELDOFVIEW 2048`, `r_main.c:48`) that is
   spread across the whole view width. `focallength` is derived from
   `centerxfrac` (= `viewwidth/2 << FRACBITS`) at `r_main.c:557-558`. If you
   simply widen `viewwidth`, the *same* 90° of world is painted across *more*
   columns — i.e. the picture is **zoomed/stretched horizontally**, the exact
   thing we are trying to avoid. To get *more* world at correct proportions, the
   focal length must stay tied to a **fixed 4:3-reference half-width** while the
   actual `viewwidth` grows. The extra columns then extend the FOV at the
   original per-column angular resolution. This is precisely what Crispy Doom
   does (`focallength = FixedDiv(centerxfrac_nonwide, finetangent[…])`, see
   [Verified assumptions](#verified-assumptions)).

2. **The UI art is 320 wide.** The status bar (`STBAR`), title screen
   (`TITLEPIC`), intermission, and menus are all 320-pixel-wide graphics
   authored for a 4:3 canvas. Widen the world view and these no longer span it.
   Every faithful port (Crispy, Woof, Doom Retro) keeps them **centered** in the
   wide frame via one global horizontal offset — Crispy calls it
   `WIDESCREENDELTA` — added to every UI patch draw. Genuinely-wider UI art is an
   optional later nicety (and dovetails with the Ultra renderer's HD-asset plan);
   the baseline simply centers the originals with gameplay showing behind.

The good news, from the DOOM-0027 hi-res work
([`docs/specs/DOOM-0027-hires.md`](DOOM-0027-hires.md)): the code already splits
a **logical** UI canvas (`ORIGWIDTH`/`ORIGHEIGHT` = 320×200) from the
**physical** render buffer (`SCREENWIDTH`/`SCREENHEIGHT` = `ORIGWIDTH*HIRES` =
640×400). Almost all UI centering was already repointed `SCREENWIDTH → ORIGWIDTH`.
So the 4:3 assumption is now **concentrated**, not scattered — it lives in
`ORIGWIDTH`'s value, the present-time logical-size call, and the projection
setup, rather than in dozens of UI sites.

## Approach

Follow the Crispy/Woof model, mapped onto our `ORIGWIDTH`/`SCREENWIDTH` split.

Introduce three concepts:

- **`NONWIDEWIDTH = 320`** — the immutable 4:3 reference. Drives the projection
  (focal length, vertical scale) and the UI-centering offset. *Never changes.*
- **Active logical width** — a **runtime** value in `[NONWIDEWIDTH,
  MAXWIDTH/HIRES]`, chosen at startup from the real display aspect ratio.
  `activeWidth = round(NONWIDEWIDTH × displayAspect ÷ (4/3)) = round(240 ×
  displayAspect)`, then clamped to `[320, 640]`. Worked examples: 4:3 → 320 (a
  no-op, identical to today); 16:10 → 384; 16:9 → 427; 21:9 → 560; 24:9 → 640
  (the logical cap = `MAXWIDTH/HIRES`; `MAXWIDTH` itself is the *physical*
  constant, see change #2). **Displays narrower than 4:3 clamp *up* to 320** (5:4 computes
  300 → clamped 320): they render authentic 4:3 and letterbox, because Hor+ has
  no width to remove. The lower clamp at `NONWIDEWIDTH` is what guarantees a
  non-negative `WIDESCREENDELTA` and keeps the UI on-screen.
- **`WIDESCREENDELTA = (activeLogicalWidth − NONWIDEWIDTH) / 2`** — the
  horizontal offset (in our logical `ORIGWIDTH` space) added to every UI patch
  draw so 320-wide art stays centered. This is our-space form of Crispy's
  `(SCREENWIDTH − NONWIDEWIDTH)/2`: Crispy's `SCREENWIDTH` is *its* logical
  width, which maps to our `activeLogicalWidth`, not to our physical
  `SCREENWIDTH` (= `ORIGWIDTH*HIRES`; `HIRES = 2`, `doomdef.h:109`). The lower
  clamp keeps it ≥ 0.

### The five mechanical changes

1. **Projection (the one delicate change), `r_main.c`.** In
   `R_ExecuteSetViewSize` / `R_InitTextureMapping`, compute a **non-wide
   reference half-width** from the *view height* (the 4:3-equivalent width for
   the current view size) and substitute it into the three projection sites,
   each of which today uses the geometric (wide) half-width:
   - `focallength` — currently `FixedDiv(centerxfrac, finetangent[…])`
     (`r_main.c:557-558`): replace `centerxfrac`.
   - `projection` — currently `= centerxfrac` (`r_main.c:705`): replace
     `centerxfrac`.
   - `yslope[]` numerator — currently `(viewwidth<<detailshift)/2*FRACUNIT`
     (`r_main.c:743`): replace the `viewwidth/2` term (note: it is *not* spelled
     `centerxfrac` in this loop — the implementer substitutes the `viewwidth/2`
     expression, not a `centerxfrac` symbol).

   `centerx`/`centerxfrac` stay the geometric center of the wide view (used to
   place columns). Result: vertical FOV and per-column angle are byte-identical
   to vanilla; the extra columns extend the horizontal FOV. When the active
   width == `NONWIDEWIDTH` (4:3) the reference equals the geometric center, so
   the math reduces exactly to today's — a zero-diff path for 4:3.

2. **Buffer + present width.** Make the active logical width feed
   `viewwidth`/`SCREENWIDTH` so the physical buffer and the 3D view are wider.
   Allocate `screens[0..3]` at the compile-time physical **max** width (so static
   sizing is safe) but render only the active width. **Reuse the existing
   `MAXWIDTH` constant** (`r_draw.c:48`, currently `1120`, which already sizes
   `columnofs[MAXWIDTH]` at `r_draw.c:72`) as that cap — do **not** introduce a
   second `MAXWIDTH`. It must be **enlarged** to fit the widest physical buffer:
   24:9 → logical 640 → physical `640*HIRES = 1280`, so `MAXWIDTH` goes `1120 →
   ≥1280` (and `screens[]` must allocate `MAXWIDTH*SCREENHEIGHT`, not the current
   `SCREENWIDTH*SCREENHEIGHT` — see `v_video.c:534`). Update the present-time
   call, currently `SDL_RenderSetLogicalSize(renderer, SCREENWIDTH,
   SCREENWIDTH*3/4)` (`i_video.c:666`, the 4:3 lock set by Part A), to a logical
   area matching the active display aspect, so the wide buffer fills the window
   with no letterbox/pillarbox. *(This `*3/4` form is the pre-Part-B before-state;
   Part B implemented it as `SDL_RenderSetLogicalSize(renderer, SCREENWIDTH,
   SCREENHEIGHT*6/5)` at `i_video.c:668`, which is the current line Part C builds
   on.)*

   **Row-stride sites to track (don't miss these).** Several places stride by the
   compile-time `SCREENWIDTH` macro and must instead use the active physical
   width once the buffer can be wider than `ORIGWIDTH*HIRES`: `ylookup[]`
   construction (`r_draw.c:721`, `*SCREENWIDTH`), the `I_FinishUpdate` expand-blit
   stride (`i_video.c:513`), and the view-centering `viewwindowx =
   (SCREENWIDTH-width)>>1` (`r_draw.c:707`). The cleanest route is to make the
   active physical width the value carried in `SCREENWIDTH` at runtime (or a new
   `viewwindow_physwidth`); whichever, these three strides must key off it, not a
   frozen 640.

3. **UI centering, `v_video.c`.** Add `WIDESCREENDELTA` to the `x` coordinate in
   the two base column blitters **only** — `V_DrawPatch` (`:223`) and
   `V_DrawPatchFlipped` (`:298`) — so all 320-logical UI art centers in the wide
   frame. Do **not** add it to `V_DrawPatchDirect` (`:372`): it *forwards* to
   `V_DrawPatch` (`:378`), so offsetting both would double-shift the UI. (This
   tree has no translucent/TL patch variant; Crispy's `V_DrawPatchTL` has no
   equivalent here.) Mirrors Crispy's `x += WIDESCREENDELTA;`.

4. **Status bar, `st_stuff.c`.** The bar is assembled 320-wide in `screens[4]`
   then `V_CopyRect`'d to the bottom of `screens[0]` (`st_stuff.c:509`,
   `V_CopyRect(ST_X, 0, BG, ST_WIDTH, ST_HEIGHT, ST_X, ST_Y, FG)`). Center that copy via
   `WIDESCREENDELTA` so the original 320-wide `STBAR` sits centered, with the
   border flat (already drawn full-width by `R_FillBackScreen`) filling the
   sides. Widescreen `STBAR` filler art is **out of scope** (see below).

5. **Weapon sprite, `r_main.c` + `r_things.c`.** A wider FOV must **not** shrink
   or grow the gun. Two distinct concerns:
   - **Scale (the fix):** `pspritescale`/`pspriteiscale` (`r_main.c:731-732`) are
     `viewwidth/ORIGWIDTH`-relative, so a wider `viewwidth` would enlarge the gun.
     Pin these to `NONWIDEWIDTH` (not the active width) so the gun keeps its 4:3
     size. The 3D Vulkan path does the analogous `(4.0/3.0)/aspect` correction at
     `r_mesh.c:1109`, a useful cross-check.
   - **Horizontal anchor (leave as-is — verify, don't change):** `R_DrawPSprite`
     computes `tx = psp->sx - 160*FRACUNIT` then `x1 = (centerxfrac +
     FixedMul(tx,pspritescale)) >> FRACBITS` (`r_things.c:676,679`). `centerxfrac`
     is the *wide* screen center, which is exactly where a centered gun
     (`sx≈160`) should sit — screen-centered in any aspect. With the scale pinned
     above, the off-center term `tx*pspritescale` is also 4:3-correct. So the
     anchor stays `centerxfrac` deliberately; only the *scale* changes. Confirm in
     Verification step 5 that the gun neither moves nor resizes.

The active width is computed **at runtime** from the display so it adapts to
whatever monitor the build runs on — the user's 5:4, the friend's 16:9, a 4:3
CRT — with no per-machine rebuild. Part C exposes a *4:3 ↔ Widescreen* toggle
that simply forces the active width to `NONWIDEWIDTH` (4:3) or the display-derived
value (Widescreen), persisted via `m_misc.c`.

## Alternatives considered

- **Stretch the 4:3 buffer to fill (Part A's rejected "Fill").** Trivial
  (logical size = window), but distorts geometry — Doomguy gets fat. Explicitly
  what the user rejected ("not to stretch it but … authentic widescreen").
  Possible as an optional fallback in Part C, not the default.

- **Naively widen `ORIGWIDTH` and let `focallength` follow `centerxfrac`** (the
  literal reading of the roadmap's one-line note). Rejected: as shown in
  Background §1, this zooms/stretches horizontally rather than extending FOV. The
  `NONWIDEWIDTH` reference is the load-bearing correction the roadmap note
  omits.

- **Compile-time fixed widescreen width (one ratio baked in).** Simpler (no
  startup aspect query), but forces one ratio for all users — bars return on any
  other monitor. Since users have varied displays (5:4, 16:9, …), the active
  width must be runtime. We still keep a compile-time **max** purely for static
  buffer allocation.

**On recording this choice (ADR).** The runtime-vs-compile-time width decision is
captured here in the spec, not in a separate `docs/decisions/` ADR. Per
`docs/standards/documentation.md` (§ "Specs"), an ADR is reserved for a *hard
architectural choice* — language, API, protocol, or storage format. Runtime-vs-
compile-time width is a design tradeoff in **none** of those categories: it adds
no new axis beyond DOOM-0027's `ORIGWIDTH`/`SCREENWIDTH` split, which it reuses.
So a spec-level record is the right home (DOOM-0027 set the same precedent for its
resolution choice, in-spec with user sign-off). ADR `0001` remains the home for
the one genuinely cross-cutting choice (renderer language/API).

- **Render the 3D view to a separate wide buffer and composite under a 4:3 UI.**
  Cleaner separation, but duplicates the present/scale path and was already
  rejected for the analogous hi-res work (DOOM-0027 spec, "separate hi-res
  buffer" alternative). Reusing the single-buffer pipeline is less code.

## Verified assumptions

Every claim below was checked against current source (this repo) or the Crispy
Doom reference implementation — not recalled.

- **World-geometry projection consumers follow `viewwidth` automatically.**
  `r_segs.c:449` (walls), `r_things.c:495/536` (world sprites), `r_plane.c:212-213`
  (floors/ceilings) all key off `centerx`/`centerxfrac`/`projection`, themselves
  set from `viewwidth` in `R_ExecuteSetViewSize` (`r_main.c:701-705`). So for the
  *world*, the **only** site that must be reworked for correct FOV is the
  focal-length/`yslope` derivation — everything downstream self-adjusts.
  **Exception — the weapon sprite does NOT auto-follow:** `R_DrawPSprite`
  (`r_things.c:676-679`) uses `pspritescale`, which is `viewwidth`-relative and so
  *would* enlarge the gun with a wider view — change #5 pins it to `NONWIDEWIDTH`.
  (Code-map, 2026-06-30.)
- **The reference technique.** Crispy Doom `src/doom/r_main.c`:
  `focallength = FixedDiv(centerxfrac_nonwide, finetangent[FINEANGLES/4+FIELDOFVIEW/2]);`
  and `R_ExecuteSetViewSize` sets `scaledviewwidth = viewheight*SCREENWIDTH/
  (SCREENHEIGHT-(ST_HEIGHT<<hires))` for the widescreen case — i.e. width derived
  from height × screen aspect, focal length from the *non-wide* center. **These
  are Crispy's own symbol names, not ours** (external source, not in this tree):
  Crispy's `ST_HEIGHT<<hires` is the status-bar height in physical pixels, whose
  in-tree equivalent is `SBARHEIGHT` (defined `r_draw.c:53`, `= HIRES*32`); Crispy's
  `SCREENWIDTH`/`SCREENHEIGHT` are its logical canvas. Treat the formula as the
  *shape* of the derivation (width = viewheight × screen aspect), to be
  re-expressed in our symbols, not copied verbatim. Confirmed by fetching the
  raw source 2026-06-30; not re-verifiable in-repo.
- **The UI offset.** Crispy `src/v_video.c` adds `x += WIDESCREENDELTA;` at the
  top of `V_DrawPatch`, `V_DrawPatchFlipped`, and the TL patch variants;
  `WIDESCREENDELTA = (SCREENWIDTH − NONWIDEWIDTH)/2`. Confirmed by fetching the
  raw source 2026-06-30.
- **Buffers auto-size.** `screens[0..3]` are one `I_AllocLow(SCREENWIDTH*
  SCREENHEIGHT*4)` (`v_video.c:534`) carved in four (`:538`); the `I_FinishUpdate`
  blit loop (`i_video.c:481-520`) and the SDL texture-create (`:668-670`) follow
  `SCREENWIDTH/HEIGHT`; `f_wipe.c` is fully `(width,height)`-parameterized; `WI_slamBackground`
  copies a full frame. None of these embed a 4:3/320 constant. (Code-map.)
- **`screens[4]` is deliberately `ORIGWIDTH`-wide** — allocated
  `Z_Malloc(ST_WIDTH*ST_HEIGHT,…)` at `st_stuff.c:1470`, where `ST_WIDTH` is
  `#define`d `= ORIGWIDTH` at `st_stuff.h:34`. The status-bar scratch; the bar
  art is a 320-wide patch. (Code-map.)
- **`INV_ASPECT_RATIO 0.625`** (`doomdef.h:110`) is currently unreferenced in the
  classic renderer (DOOM-0027 spec); not relied on here.

## Components / affected files

| File | Change |
|------|--------|
| `doomdef.h` | Add `NONWIDEWIDTH` (320). Document that the *active* logical width is runtime in `[320, MAXWIDTH/HIRES]` (= `[320, 640]` once `MAXWIDTH` is enlarged). |
| `r_draw.c` | **Enlarge the existing `MAXWIDTH`** (`:48`, `1120` → `≥1280`) so `columnofs[MAXWIDTH]` and the physical buffers fit a 24:9 view. Do **not** add a second `MAXWIDTH`. |
| `r_main.c` | `R_ExecuteSetViewSize` / `R_InitTextureMapping`: non-wide reference half-width for `focallength`, `projection`, `yslope` numerator; pin `pspritescale`/`pspriteiscale` to `NONWIDEWIDTH` (psprite x-anchor in `r_things.c:676-679` stays `centerxfrac` — verify, don't change). **The one delicate change.** |
| `i_video.c` | Active-width-aware `SDL_RenderSetLogicalSize`; startup display-aspect query → active logical width. |
| `v_video.c` | Allocate `screens[0..3]` at the physical max (`MAXWIDTH*SCREENHEIGHT`, not `SCREENWIDTH*SCREENHEIGHT`); add `WIDESCREENDELTA` to patch-draw `x`; expose `WIDESCREENDELTA` + active width. |
| `st_stuff.c` | Center the status-bar `V_CopyRect` by `WIDESCREENDELTA`. |
| `r_draw.c` | Re-verify `viewwindowx = (SCREENWIDTH−width)>>1` centering with a wider buffer (expected auto-correct; confirm). |
| `m_misc.c` (Part C) | Persist the aspect choice. |
| `m_menu.c` (Part C) | *4:3 ↔ Widescreen* option. |

Sites confirmed **not** to need changes (auto-scale): `f_wipe.c`, `WI_slamBackground`,
the back-screen flat fill, screenshots, devparm dots, and all UI *centering* math
(already `ORIGWIDTH`-relative). Source: code-map 2026-06-30.

## Verification

The renderer can't be eyeballed in the dev environment (no display/GPU here);
each step ends in a Windows build dropped to the test share for the user to
confirm. Step → verify check:

1. Projection change with active width forced to `NONWIDEWIDTH` → **byte-identical
   frame** vs current 4:3 build (proves the refactor is a no-op at 4:3). Verify:
   capture a fixed-camera frame via the engine's screenshot path (`M_ScreenShot`,
   `m_misc.c`) on both builds at a demo-fixed viewpoint and confirm a zero pixel
   diff (not an eyeball "looks the same").
2. Run on the user's 5:4 monitor → active width clamps to 320, so it **renders
   authentic 4:3 with the same thin top/bottom bars as Part A** (no regression,
   no distortion). Verify: user screenshot matches the Part-A 5:4 build; straight
   walls stay straight. (5:4 cannot gain from Hor+ — see Goal's scope limit.)
3. Run on a 16:9 (or 16:10) display → **more level visible left/right** than 4:3,
   no stretch, no side pillars; title/menu/status-bar centered. Verify: friend's
   16:9 laptop screenshot shows wider sightlines than the 4:3 build.
4. Reduced Screen Size (blocks < 10) in widescreen → ornamental border still
   frames the view correctly (guards against the DOOM-0055-class hi-res
   border/visplane bugs). Verify: no `R_MapPlane`/`V_DrawPatch` log spam, no
   crash.
5. Weapon sprite unchanged in size/anchor between 4:3 and widescreen. Verify:
   gun sits at the same screen position and scale.

## Out of scope (YAGNI)

- **Widescreen UI artwork** (a wider `STBAR`, widescreen `TITLEPIC`/`INTERPIC`).
  Baseline centers the 320-wide originals. Wider art is a later nicety and
  belongs with the Ultra renderer's HD-asset sourcing.
- **Vertical+ or arbitrary FOV slider.** This feature is Hor+ at the vanilla 90°
  reference only.
- **Widescreen for the Vulkan 3D path.** Solid/Ultra already render the full
  window (`r_mesh.c` does its own aspect correction). This spec is the *software*
  renderer only.
- **Aspect choice persistence + menu toggle** — that's Part C, a follow-up.

## Implementation notes (2026-06-30)

Four things surfaced during implementation that the cold-eyes loops (a *docs* review)
could not have caught — they needed an implementer reading the hot render path:

1. **`SCREENWIDTH` runtime-isation was wider than "3 stride sites."** Change #2 chose
   to make `SCREENWIDTH` a runtime value; in practice that also required repointing
   **~16 compile-time array declarations** (`floorclip`/`ceilingclip`/`distscale`
   `r_plane.{c,h}`; `negonearray`/`screenheightarray`/`clipbot`/`cliptop`
   `r_things.{c,h}`; `xtoviewangle` `r_main.c`/`r_state.h`; visplane `top`/`bottom`
   `r_defs.h`; `MAXOPENINGS`) to the compile-time `MAXWIDTH` cap (promoted from
   `r_draw.c` to `doomdef.h`, `1120 → 1280`). Two file-scope *static initialisers*
   also broke and were converted to runtime assignment: `am_map.c` `finit_width` (set
   in `AM_LevelInit`) and the spectre-fuzz table `r_draw.c` `fuzzoffset[]` (now holds
   `±1` row-step signs, scaled by `SCREENWIDTH` at use). All hot-loop strides
   (`dest += SCREENWIDTH`, `dest[SCREENWIDTH*n]`) "just worked" once the symbol became
   a runtime int — only declarations and static initialisers needed edits.

2. **UI scale factor `f = dsw/ORIGWIDTH` was latently wrong for wide buffers.** In
   `V_DrawPatch`/`V_DrawPatchFlipped`/`V_CopyRect` the per-buffer scale was derived as
   `screenwidth/ORIGWIDTH`, which equals `HIRES` only by integer-truncation luck at
   ≤16:9 and inflates to 3–4× at 21:9+. Pinned to `HIRES` for the full-screen buffers
   (1 for the `ORIGWIDTH`-wide scratch). Zero-diff at 4:3.

3. **DOOM-0148 (HUD always on, Screen Size capped at block 10) defeated widescreen
   in-game.** Block 10 hardcoded `scaledviewwidth = setblocks*32*HIRES` (= 640, 4:3),
   so the gameplay view never widened. Block 10 now uses the full `SCREENWIDTH`
   (zero-diff at 4:3, where `SCREENWIDTH == 640`). The spec's claim that
   `R_FillBackScreen` would fill the status-bar sides was wrong at full width (it
   early-returns) — instead `ST_refreshBackground` blacks out the strips either side
   of the centred 320-wide bar.

4. **Static 320-wide full-screen art (title / intermission / menu background /
   finale) shows side gaps on a true-widescreen display.** This is the spec's
   already-out-of-scope "widescreen UI artwork" item, now visible in practice: the
   originals are centred (via `WIDESCREENDELTA`) with un-painted sides. Tracked as a
   follow-up (a black-clear of those pages, or genuine widescreen art with the Ultra
   HD-asset work). The user's 5:4 monitor is unaffected (it renders authentic 4:3).

Files touched: `doomdef.h`, `r_draw.c`, `r_plane.{c,h}`, `r_things.{c,h}`, `r_main.c`,
`r_state.h`, `r_defs.h`, `i_video.{c,h}`, `d_main.c`, `am_map.c`, `v_video.c`,
`st_stuff.c`. Builds clean on both targets; 4:3/5:4 is a provable zero-diff no-op.

## Cold-eyes loop log

**Loop 1 (2026-06-30)** — 2 cold reviewers (code-accuracy lane; cross-doc lane).
All in-repo code citations verified accurate (lane 1: zero CRITICAL). Findings
verified and fixed:
- **CRITICAL (author-caught during synthesis)** — Goal claimed widescreen would
  remove the 5:4 monitor's top/bottom bars. Wrong: 5:4 is *narrower* than 4:3, so
  Hor+ (width-only) cannot fill it; it clamps to 320 and stays 4:3 + letterbox.
  Goal, active-width formula, and Verification step 2 corrected to state the
  scope limit honestly.
- **HIGH** — `yslope[]` numerator is `viewwidth/2` (`r_main.c:743`), not
  `centerxfrac`; projection guidance now names each site's real current
  expression so the implementer doesn't hunt for a missing symbol.
- **MEDIUM** — `MAXWIDTH`/clamp had no pinned value → set to 640 logical (24:9
  cap), active width `[320, 640]` with formula `round(240 × displayAspect)`.
- **MEDIUM** — Crispy's `scaledviewwidth` formula uses Crispy symbols
  (`ST_HEIGHT`); annotated with the in-tree equivalent (`SBARHEIGHT`) and marked
  external/non-re-verifiable.
- **MEDIUM** — runtime-vs-compile-time width choice now records why no ADR
  (follows DOOM-0027 precedent).
- **LOW** — Verification step 1 strengthened from "looks identical" to a zero
  pixel-diff; `WIDESCREENDELTA` left-operand disambiguated (our `activeLogicalWidth`
  vs Crispy's `SCREENWIDTH`); Part A's spec-less status noted in Goal.
- **Cross-doc fix (DOOM-0027)** — its stale `INV_ASPECT_RATIO` citation
  (`:107` → `:110`) corrected in the same pass.

**Loop 2 (2026-06-30)** — 2 cold reviewers re-read the edited spec. Found and
fixed:
- **CRITICAL** — proposed `MAXWIDTH` (640) collides with the existing
  `#define MAXWIDTH 1120` (`r_draw.c:48`) that sizes `columnofs[]`. Reworked to
  *reuse and enlarge* that constant (`1120 → ≥1280`) instead of adding a clashing
  one; clarified logical cap = `MAXWIDTH/HIRES`.
- **HIGH** — change #5 named only `pspritescale`; the weapon-sprite x-anchor
  (`r_things.c:676,679`, `160`/`centerxfrac`) was unaddressed. Split into
  scale (pin to `NONWIDEWIDTH`) vs anchor (stays `centerxfrac` — correct,
  verify-don't-change); carved the psprite scale out of the "auto-follows"
  assumption.
- **MEDIUM** — `SBARHEIGHT` repointed to its definition (`r_draw.c:53`, not the
  use site `:716`); `ST_WIDTH` repointed to `st_stuff.h:34`; alloc range
  tightened to `v_video.c:524/528`.
- **Cross-doc** — DOOM-0027's drifted **primary-table** `v_video.c` cites
  corrected (`V_DrawPatch :204→:223`, `V_DrawPatchFlipped :271→:298`, `V_CopyRect
  :158→:162`); DOOM-0027 still has residual inline cites (DOOM-0150 tracks the
  full refresh — DOOM-0027 is **not** yet fully clean). ROADMAP DOOM-0147 entry
  annotated that this spec supersedes its inline sketch.
- **LOW** — Verification step 1 names the capture path (`M_ScreenShot`).

**Loop 3 (2026-06-30)** — 2 cold reviewers. No CRITICAL (all code citations
verified accurate). Found and fixed:
- **HIGH** — change #3 listed "the TL variants" of `V_DrawPatch`, which don't
  exist in this tree (Crispy-ism). Removed; noted no TL variant here.
- **HIGH** — missing spec metadata header (Status/Roadmap/Kind/Depends-on) that
  DOOM-0027 has. Added.
- **MEDIUM (correctness)** — change #3 listed `V_DrawPatchDirect` as a separate
  centering site, but it *forwards* to `V_DrawPatch` (`v_video.c:378`), so
  offsetting both would double-shift the UI. Scoped the edit to the two base
  blitters only.
- **MEDIUM** — no-ADR justification re-anchored to documentation.md's category
  list (language/API/protocol/storage) directly, not just DOOM-0027 precedent.
- **LOW** — added the row-stride sites (`ylookup` `r_draw.c:721`, blit
  `i_video.c:513`, `viewwindowx` `r_draw.c:707`) to change #2 so the widened-buffer
  stride change isn't missed; fixed DOOM-0027's `SBARHEIGHT` cite (`:52`/`32` →
  `:53`/`HIRES*32`).

**Loop 4 (2026-06-30)** — 2 cold reviewers. **No CRITICAL, no blocking HIGH**;
every in-scope code citation re-verified accurate. Remaining findings were pure
polish, fixed:
- **HIGH (cosmetic)** — change #2 now quotes the current
  `SDL_RenderSetLogicalSize(renderer, SCREENWIDTH, SCREENWIDTH*3/4)` form before
  describing the replacement, so the before-state is visible.
- **MEDIUM** — Loop-2 log line clarified to say only DOOM-0027's *primary table*
  was corrected; residual inline cites remain (DOOM-0150 tracks them) — DOOM-0027
  is not implied fully clean.
- **MEDIUM** — DOOM-0027's `V_DrawPatchDirect` forward cite (`:337/:343` →
  `:372/:378`) corrected.
- Assessed-and-kept: the no-ADR rationale is already anchored to
  `documentation.md`'s category list with DOOM-0027 precedent as a one-clause
  secondary note (not a full restatement) — left as-is.

**Loop 5 (2026-06-30) — converged.** 2 cold reviewers independently re-verified
all 30+ code citations against current source ("unusually clean… none would cause
an implementer to build the wrong thing"). **Zero CRITICAL / HIGH / design
findings.** Only cosmetic citation polish remained, applied this pass:
- relabelled `i_video.c:668-670` as the texture-create (not the blit loop);
- pinpointed the status-bar copy site (`st_stuff.c:509`);
- added the `HIRES` cite (`doomdef.h:109`); fixed a one-word typo.
Accepted/tracked (not blockers): ROADMAP DOOM-0147's append-only inline sketch is
explicitly superseded by its line-1249 annotation; DOOM-0027's residual inline
cites are tracked by DOOM-0150.

**Outcome:** 5 loops; 2 CRITICALs + 1 correctness bug caught and fixed; converged
to citation-clean. Spec is cleared for implementation (house rule 14 satisfied).

---

## Part C — aspect toggle + fill-to-screen (persisted)

**Status:** Part C — **cold-eyes converged** 2026-07-01 (3 loops; see the Part C
cold-eyes loop log below), cleared for implementation; not yet implemented.
**Depends on:** Part B (implemented `8fb4d66`) — reuses its runtime `SCREENWIDTH` /
`WIDESCREENDELTA` and the `I_InitWidescreen` startup hook.

Terminology: **Fill Screen** is the menu label, `fillstretch` is the backing
global, this section's heading calls it "fill-to-screen", and Part B's notes call
the same idea "fill-stretch" — one concept, several spellings.

### Goal (Part C)

Give the player two persisted display preferences, reachable from the in-game
**Renderer** sub-menu (Options → Renderer):

1. **Widescreen — On / Off.** On (default) keeps the Part B Hor+ behaviour: on a
   display wider than 4:3 the Classic renderer shows more of the level to the
   sides. Off forces authentic 4:3 (pillar-boxed on a wide display) for players
   who prefer the original framing.
2. **Fill Screen — On / Off.** Off (default) presents the rendered image at its
   correct aspect, letter-/pillar-boxing with black bars where the monitor's
   shape doesn't match. On stretches the image to fill the whole monitor,
   removing the bars at the cost of a small geometric distortion.

Both settings persist to `~/.doomrc` (via `M_SaveDefaults` / `M_LoadDefaults`) so
they survive a restart.

**Why two settings.** They target different displays and are independent:
- *Widescreen* only does anything on a display **wider** than 4:3 — the knob for
  16:9 / ultrawide players.
- *Fill Screen* is the knob for displays **narrower** than 4:3 (e.g. the
  maintainer's 5:4 1280×1024) where Hor+ has no spare width to add: it removes the
  thin top/bottom bars Part A leaves, by stretching. On a wide display it also
  removes any residual bars. It is the only Part C control that changes anything
  visible on a 5:4 monitor; the Widescreen toggle there is cosmetic (4:3 either
  way, since Hor+ already clamps a 5:4 display to 320 logical).

### Approach (Part C)

Two new persisted `int` globals, **defined** in `i_video.c` next to the
`SCREENWIDTH` / `WIDESCREENDELTA` definitions (`i_video.c:685`) and **declared
`extern` in `doomdef.h`** next to those globals' own externs (`doomdef.h:121` — the
established convention; `i_video.h` holds only function declarations). The new
`I_SetAspect` function is declared in `i_video.h` alongside `I_InitWidescreen`.
Both globals are registered in the `m_misc.c` `defaults[]` table so they load from
/ save to `~/.doomrc`:

| global | default | meaning |
|--------|---------|---------|
| `widescreen`  | `1` | `0` ⇒ force 4:3 even on a wide display |
| `fillstretch` | `0` | `1` ⇒ stretch the present to fill the monitor |

#### Widescreen (applied at startup — "restart to apply")

`I_InitWidescreen` (`i_video.c:698`) already computes the active logical width
from the desktop aspect. One change: when `widescreen == 0`, skip the aspect
result and pin `logical = NONWIDEWIDTH` (→ `SCREENWIDTH = 640`,
`WIDESCREENDELTA = 0`) — the exact Part A 4:3 state.

Because `SCREENWIDTH` sizes the screen buffers (`v_video.c` `V_Init`, `:534`),
the SDL streaming texture (`i_video.c:670`) and the present logical size — **all
allocated once at startup** — the Widescreen toggle is honoured at the **next
launch**, not live. The menu draws a "restart to apply" note. This is the
conservative choice: a live toggle would have to re-allocate `screens[]`,
recreate the texture, and re-run the whole `R_ExecuteSetViewSize` /
`R_InitTextureMapping` view-setup chain, destabilising the just-shipped Part B for
a niche control. Listed under Out of scope.

**Ordering fix (load-before-init).** Today `d_main.c` runs
`I_InitWidescreen()` (`:1127`) → `V_Init()` (`:1130`) → `M_LoadDefaults()`
(`:1133`), so the persisted `widescreen` value is not yet loaded when
`I_InitWidescreen` decides the width and `V_Init` allocates the buffers to it.
`M_LoadDefaults()` is moved **above** `I_InitWidescreen()`. It is a pure
config-file → globals read (`m_misc.c:361`) with no dependency on the video / zone
/ WAD systems — `basedefault` is already set (`d_main.c:689`), and vanilla DOOM
loads defaults before `V_Init` for exactly this reason — so the move is safe.

#### Fill Screen (applied live)

The present path sets the SDL logical size once at window creation (inside
`CreateSoftwareWindow`, `i_video.c:639` — the
`SDL_RenderSetLogicalSize(renderer, SCREENWIDTH, SCREENHEIGHT*6/5)` call at
`:668`). Part C replaces that single call with a new `I_SetAspect(void)` that
branches on `fillstretch`:

- `fillstretch == 0` → `SDL_RenderSetLogicalSize(renderer, SCREENWIDTH, SCREENHEIGHT*6/5)`
  — authentic aspect with the 1.2× vertical pixel correction; SDL adds black bars
  where the window shape differs (the Part B present, unchanged).
- `fillstretch == 1` → `SDL_RenderSetLogicalSize(renderer, 0, 0)`. Passing `(0,0)`
  clears the logical resolution and resets SDL's viewport to the full output and
  scale to `1.0` (verified SDL2 behaviour), so the per-frame
  `SDL_RenderCopy(renderer, texture, NULL, NULL)` (`i_video.c:518`) stretches the
  texture across the whole window.

**Trade-off (be explicit).** Fill bypasses **both** the black bars **and** the
6/5 (1.2×) vertical-aspect correction: the texture is the raw
`SCREENWIDTH × SCREENHEIGHT` (640×400) render buffer, not the 4:3-corrected
640×480 logical area, so on a 5:4 monitor the image gains a little extra vertical
stretch on top of filling the bars. This is the accepted cost of "fill the
screen"; players who want correct proportions leave it Off.

`I_SetAspect` guards `if (!renderer) return;` and is called both at window
creation (in place of the old line) and from the menu handler, so flipping Fill
Screen updates the picture immediately — no restart, no reallocation (it only
recomputes SDL's viewport/scale, which is legal at any time).

#### Menu (Renderer sub-menu)

Two text rows are added to `RendererMenu[]` and the `renderer_e` enum
(`m_menu.c:397/407`), drawn by `M_DrawRendererMenu` (`:1055`) with `M_WriteText`
(no menu-art lumps needed), placed **before** the Brightness thermo so the slider
stays last:

```
rm_renderer, rm_upscaler, rm_renderscale, rm_debugviews,
rm_widescreen, rm_fillstretch, rm_brightness, rm_end
```

- `M_ChangeWidescreen(choice)` → `widescreen = widescreen ? 0 : 1;` (value drawn
  On/Off; a "restart to apply" note is drawn under the row).
- `M_ChangeFillScreen(choice)` → `fillstretch = fillstretch ? 0 : 1; I_SetAspect();`
  (live).

Both follow the existing `M_ChangeDebugViews` On/Off idiom (`m_menu.c:1320`,
`x = x ? 0 : 1`; the nearby `M_ChangeUpscaler` uses a `(x+1)%2` form instead —
either works, the proposal mirrors DebugViews). `m_menu.c` sees the `widescreen` /
`fillstretch` externs via `doomdef.h` (already included) and gains
`#include "i_video.h"` (or a local `extern void I_SetAspect(void);`) for the live
re-apply.

### Components / affected files (Part C)

- `i_video.c` — define `widescreen` / `fillstretch`; `I_InitWidescreen` honours
  `widescreen`; new `I_SetAspect`; `CreateSoftwareWindow` calls it in place of the
  hard-coded logical-size line.
- `doomdef.h` — declare the `widescreen` / `fillstretch` externs (next to the
  `SCREENWIDTH` / `WIDESCREENDELTA` externs at `:121`).
- `i_video.h` — declare `I_SetAspect`.
- `m_misc.c` — two `defaults[]` rows (`{"widescreen",&widescreen,1}`,
  `{"fillstretch",&fillstretch,0}`).
- `m_menu.c` — enum + array rows, two `M_DrawRendererMenu` labels + restart note,
  two handlers, two forward decls, `#include "i_video.h"`.
- `d_main.c` — move `M_LoadDefaults()` above `I_InitWidescreen()`.

### Verification (Part C)

1. **Default = zero behavioural change from Part B.** With a fresh config
   (`widescreen=1`, `fillstretch=0`): `I_InitWidescreen` takes the same aspect
   path as Part B, and `I_SetAspect` issues the identical
   `SDL_RenderSetLogicalSize(renderer, SCREENWIDTH, SCREENHEIGHT*6/5)`. On the 5:4
   dev monitor this stays the provable 4:3 no-op.
2. **`widescreen=0`** on a 16:9 display → `SCREENWIDTH=640`, pillar-boxed 4:3
   (matches Part A); persists across restart; the menu shows the restart note.
3. **`fillstretch=1`** → the picture fills the whole monitor with no black bars,
   live the instant it is toggled and again after a restart (loaded from config).
4. **Persistence** — toggling either writes `widescreen` / `fillstretch` to
   `~/.doomrc` on quit (`M_SaveDefaults`, `m_misc.c:329`) and reloads next launch.
5. **Menu layout** — after the two new rows, the Brightness thermo is still the
   last Renderer row (the slider must stay at the bottom).
6. **Builds clean** on Linux + Windows; no new warnings.

### Out of scope (YAGNI) (Part C)

- **Live Widescreen toggle** (re-alloc buffers + recreate texture + re-run view
  setup mid-session). Revisit only if explicitly requested.
- **Aspect-correct fill** (pixel-perfect integer scaling, per-axis choice). Fill
  is a deliberate stretch; players who want correctness leave it Off.
- **Static-screen side-gaps** (title / intermission / menu / finale full-screen
  320-wide art showing un-painted sides on a true-widescreen display). Tracked
  **separately as DOOM-0151** — closing DOOM-0147 does **not** close it.
- **A separate Video menu.** Two rows fit the Renderer sub-menu, which already
  hosts the other present-time controls (upscaler, render scale).

### Cold-eyes loop log (Part C)

**Loop 1 (2026-07-01)** — 2 cold reviewers (code-accuracy lane; cross-doc lane).
**Zero CRITICAL; zero HIGH from the code lane** — every Part C code citation
verified against current source. Findings verified and fixed:
- **HIGH (clarity)** — "Part C completes the item" risked implying the still-open
  DOOM-0151 side-gap work was also done. Clarified that Part C completes
  DOOM-0147's aspect-choice scope only, with an explicit DOOM-0151 cross-ref in
  the header and Out-of-scope list.
- **HIGH (accuracy)** — Fill Screen described the distortion as merely the
  window-aspect mismatch; corrected to state `(0,0)` drops **both** the bars and
  the 6/5 vertical-aspect correction (raw 640×400 texture, not the 640×480
  corrected area).
- **MEDIUM (accuracy)** — the spec claimed `SCREENWIDTH` / `WIDESCREENDELTA`
  externs live in `i_video.h`; they are in `doomdef.h:121`. Corrected: the new
  globals' externs go in `doomdef.h` (the real convention), `I_SetAspect` in
  `i_video.h`.
- **MEDIUM (accuracy)** — cited `M_ChangeUpscaler` + `M_ChangeDebugViews` as one
  On/Off pattern, but they use different idioms (`(x+1)%2` vs `x?0:1`); re-cited
  `M_ChangeDebugViews` (`:1320`) as the matching template.
- **MEDIUM (currency)** — present-call drift: Part B change #2's historical
  `SCREENWIDTH*3/4` (`:666`) vs the current `SCREENHEIGHT*6/5` (`:668`); annotated
  the Part B line as the superseded before-state.
- **MEDIUM (structure)** — Part C opened a second `# H1`; demoted to `##` with
  `###` / `####` sub-sections and added a Contents entry.
- **MEDIUM** — `CreateSoftwareWindow` pinned to its definition (`i_video.c:639`;
  the logical-size call is at `:668`).
- **LOW** — tied the "Fill Screen" (menu) / `fillstretch` (global) / "fill-stretch"
  (Part B notes) terms together with a one-line glossary.
- **Open question resolved** — `SDL_RenderSetLogicalSize(renderer, 0, 0)` confirmed
  against SDL2 (clears the logical resolution, resets the viewport to full output +
  scale `1.0`); noted inline as the relied-on behaviour.

**Loop 2 (2026-07-01)** — 2 cold reviewers re-read the edited section. Code-accuracy
lane returned **zero** findings (all Part C citations verified). Cross-doc lane
found self-consistency issues introduced by the Loop-1 log, fixed this pass:
- **HIGH** — top-of-doc Status still said "before implementation" while the Loop-1
  log already recorded fixed findings; reconciled both Status lines to "in cold-eyes
  review" with a pointer to this log.
- **HIGH** — "Part C completes the item" read as present-tense closure while the
  roadmap is still 🚧; softened to "once shipped, completes …".
- **MEDIUM** — Part B's "optional 5:4 fill-stretch" framing under-described Part C's
  first-class Fill Screen; added a forward pointer at Part B's mention.
- **MEDIUM** — single-sourced the Part B commit hash (`8fb4d66`) into Part B's Status.
- **MEDIUM** — added Verification step 5 (Brightness thermo stays the last Renderer
  row). **LOW** — folded "fill-to-screen" into the terminology gloss.

**Loop 3 (2026-07-01)** — 2 cold reviewers. Code-accuracy lane: **zero CRITICAL /
HIGH**; one stale cross-cite caught and fixed. Cross-doc lane: the only HIGH was the
log's own in-progress state (resolved by this convergence entry).
- **MEDIUM (accuracy)** — Part B's `screens[]` alloc cite drifted (`v_video.c:524/528`
  → current `:534/538`); corrected at both active Part B sites (the Loop-2 historical
  log line is left as a point-in-time record).
- **LOW** — "final scope" → "current scope" (the scope is settled, but the review was
  still open when written).
- **Assessed and kept (dismissed with reason):** the `(Part C)` heading suffixes are
  **deliberate** — without them the Part C `### Goal` / `### Verification` headings
  would collide with Part B's identically-named anchors. The `renderer_e` "enum" name
  is faithfully copied from the source (an unnamed enum with a stray tag), not a doc
  error. Terminology is acknowledged in the gloss rather than flattened, since
  `fillstretch` (code) must differ from "Fill Screen" (UI label).

**Outcome:** 3 loops; the code-accuracy lane reached zero findings (an implementer
builds from accurate citations), cross-doc relationships (DOOM-0147 🚧 vs DOOM-0151
📋) verified consistent, and the residual cross-doc findings decayed to the log's own
not-yet-converged state — resolved here. **Spec is cleared for implementation (house
rule 14 satisfied).**
