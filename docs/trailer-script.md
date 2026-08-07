# DOOM_Ants trailer — shot script

**Status:** draft (2026-08-07), expected to be edited. Owned by the user; this
file is the shooting script, not a contract.
**Tracked by:** ROADMAP DOOM-0339.
**Structure from:** the user, 2026-08-07. Title-card wording polished here; the
*order and intent* are theirs and should not be rearranged without asking.

The idea in one line: **walk the viewer up the ladder**, from the 1993 game
exactly as it shipped to the ray-traced HD version, one step at a time, so each
step's contribution is visible on its own rather than as one big before/after.

---

## 1. The cut

Six title cards, six gameplay blocks. Read top to bottom; this is the edit order.

| # | Type | On screen | Tier | View | Length |
|---|---|---|---|---|---|
| 1 | Title | *"DOOM and DOOM II — exactly as they shipped in 1993."* | — | — | ~2.5 s |
| 2 | Gameplay | | **Classic** | 4:3, as released | 5 s |
| 3 | Title | *"Now in widescreen."* | — | — | ~2 s |
| 4 | Gameplay | | **Classic** | widescreen | 5 s |
| 5 | Title | *"The same art, rebuilt in true 3D."* | — | — | ~2.5 s |
| 6 | Gameplay | | **Solid** | Original (raster) | 5 s |
| 7 | Title | *"Now lit by ray tracing."* | — | — | ~2 s |
| 8 | Gameplay | | **Solid** | ray-traced | 5 s |
| 9 | Title | *"Or with the art remade in high definition."* | — | — | ~2.5 s |
| 10 | Gameplay | | **Ultra** | HD (raster) — see §4 | 5 s |
| 11 | Title | *"Ray-traced, with everything turned on."* | — | — | ~2.5 s |
| 12 | Montage | | **Ultra** | ray-traced | 6 × 3 s = 18 s |

**Runtime:** 43 s of gameplay + ~14 s of titles ≈ **57 s**. A good length — long
enough to show the ladder, short enough that nobody scrubs.

### Title-card wording — the user's intent, and the polish

Both columns say the same thing. The right column is shorter because a title card
is read in two seconds, not studied; DOOM's own tone is terse, and flowery copy
would fight the game. **Pick per row — mixing is fine.**

| # | As briefed | Suggested |
|---|---|---|
| 1 | "Experience the original DOOM 1 + 2 as it was released in 1993" | "DOOM and DOOM II — exactly as they shipped in 1993." |
| 3 | "now in widescreen" | "Now in widescreen." |
| 5 | "the old graphics converted to 3D" | "The same art, rebuilt in true 3D." |
| 7 | "now with ray tracing" | "Now lit by ray tracing." |
| 9 | "with modernised graphics" | "Or with the art remade in high definition." |
| 11 | "enhanced with ray tracing and extra visual effects" | "Ray-traced, with everything turned on." |

Two wording notes worth keeping, because they are about accuracy rather than taste:

- **Card 5 says "the same art", not "the old art".** Solid *enhances* DOOM's own
  textures (upscaled, with PBR and parallax on top); it does not replace them.
  `CLAUDE.md`'s tier table is explicit that Solid enhances and Ultra substitutes,
  and card 9's "remade" is what marks that difference. Calling Solid's art "old"
  undersells the one tier whose whole pitch is that it still looks like DOOM.
- **Card 9 opens with "Or".** Ultra is not a step *above* Solid, it is a
  different choice — a player who wants the game to still look like DOOM picks
  Solid on purpose. "Or" keeps the ladder honest instead of implying Solid is a
  waypoint.

---

## 2. How each shot is captured

All shots via `demoreel --gpu` (a headless `cage` compositor, so the card is
reachable — `Xvfb` cannot run the Vulkan tiers at all). Every run needs a **temp
config**: the engine rewrites `~/.doomrc` on exit, so never point it at the live
file.

| Shot | `renderer` | `rt_view` | `widescreen` | Notes |
|---|---|---|---|---|
| 2 | `0` (Classic) | — | `0` | 4:3. `widescreen 0` is read at startup, so it needs its own run |
| 4 | `0` (Classic) | — | `1` | same scene as shot 2 if possible, so the change is the only difference |
| 6 | `2` (Solid) | `0` | `1` | |
| 8 | `2` (Solid) | `6` | `1` | |
| 10 | `1` (Ultra) | `0` | `1` | needs `DOOMASSETDIR` — see the trap below |
| 12 | `1` (Ultra) | `6` | `1` | six runs, one per level |

Enum values, so nobody has to re-derive them: `renderer` is `0` Classic,
**`1` Ultra**, `2` Solid (`rendermode_t` order, frozen for config lockstep — the
middle two are *not* alphabetical). `rt_view` is `0` raster, `6` denoised
ray-traced; `1`–`4` are debug views and must not be shot.

### Three traps, all of which have already cost a session

1. **Ultra silently renders 1994 paletted art** unless `DOOMASSETDIR` is
   exported. The recording is not blank, it does not error, and it passes
   demoreel's uniform-frame guard — it just shows the wrong game. **Confirm
   `DOOM-0042: HD load done - 18 material(s), 75 image(s), 213.9 MB` in the app's
   log before trusting any Ultra take.** demoreel discards the app's stdout, so
   wrap the command: `sh -c 'exec <doom> ... > app.log 2>&1'`.
2. **~2–3 s of black lead-in** on the Vulkan tiers while the BLAS builds and the
   GI bakes (measured: black at t=1 s and t=2 s, picture from t=3 s). Record
   longer than the shot needs and trim the head, or the cut opens on black.
3. **The attract-mode demos cannot be used.** `doom.wad`'s DEMO1/2/3 lumps fail
   this engine's version check (`*demo_p++ != VERSION`) and the title sequence
   shows black. Use `-warp E M` into a level.

### Getting movement into a 5-second shot

A static camera makes a dull trailer, and `-noinput` gives exactly that. Two
routes, in preference order:

1. **Record a demo once, replay it for every take.** `-record` with this build
   produces a version-matched lump `-playdemo` will accept, so the same run of
   play can be shot in all six tier/view combinations — which is what makes the
   ladder a genuine comparison rather than six different moments. This is the
   right answer and it is the one that makes shots 2 and 4 line up.
2. **Scripted input** via demoreel's `-a 'key …'` actions. Works under X, and
   whether it reaches the `--gpu` compositor is an open question (§5).

---

## 3. Assembly is ours, not demoreel's

demoreel records **one app, silently, with no overlays** — its charter rules out
titles, editing and audio permanently, and that is the right call for it. So
everything below is a script in this project's `scripts/`, built on `ffmpeg`:

- **Title cards** — generated (solid background + text), not screen-recorded.
- **Concatenation** — twelve clips in the order above.
- **Audio** — the engine's own output, captured via a PipeWire null sink in
  parallel with the picture and muxed at the end (DOOM-0339 carries the shape).
  Nothing recorded so far has sound.
- **Trimming** — the black lead-in from trap 2, per clip.

---

## 4. Ultra's non-ray-traced view is mis-labelled, and this trailer needs it fixed

Shot 10 shows Ultra with ray tracing off. The menu currently calls that view
**"Original (raster)"** for both 3D tiers (`m_menu.c`'s mode-name array). That is
correct for Solid, whose raster view really is DOOM's own art — and wrong for
Ultra, whose raster view shows the HD replacement art. A viewer who reads that
label learns the opposite of what card 9 just told them.

Filed as **DOOM-0340**. Not a blocker for shooting, but it should land before the
trailer is published, since the trailer draws attention to exactly this row.

---

## 5. Open questions

1. **Which levels, and which moments?** Shot 12 wants six, each showing
   something different — the goo room, a lava map, an outdoor/sky view, a
   tight corridor for the flashlight, a lamp-lit room, one big open space. Also
   applies to shots 2–10: one recorded demo through a scene that has something to
   look at beats six unrelated spawn points. **User to choose.**
2. **Fog and exposure.** The one capture shot so far is washed out grey — at the
   current `rt_fog` default E1M1's start hides the ray tracing rather than
   showing it. Needs a tuning pass before any real take. **Claude to shoot
   options, user to pick.**
3. **Does `-a` scripted input reach the `--gpu` compositor?** Decides whether
   route 2 above is available. Untested.
4. **Does the bloom work (DOOM-0331) land first?** It changes the look materially
   and shot 12 is the one that would show it. Shooting before it means
   reshooting after. **User to decide.**
5. **Where does the widescreen comparison come from?** Shots 2 and 4 are most
   convincing as the *same* scene, which needs one demo replayed under two
   `widescreen` values. Confirm the demo plays back identically at both aspect
   ratios.
