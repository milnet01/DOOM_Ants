# DOOM-0060 — Game-select menu: choose DOOM 1 or DOOM 2, switch without relaunching by hand

**Status:** **draft** 2026-07-03 — awaiting user confirmation of the two *Assumed
decisions* below, then **/cold-eyes** (house rule 14) before implementation. Not
yet implemented.
**Roadmap:** DOOM-0060 (📋, `phase-1-build-modernise-share`). Stays 📋 until shipped.
**Kind:** feature.
**Depends on:** the existing `-iwad <file>` selector (d_main.c:702–724, already in
the fork) and the classic menu system (m_menu.c). No dependency on the 3D/RT work.

## Contents

- [Goal](#goal)
- [Assumed decisions (pending user confirmation)](#assumed-decisions-pending-user-confirmation)
- [Background — why a relauncher, not a live WAD swap](#background--why-a-relauncher-not-a-live-wad-swap)
- [Approach](#approach)
- [Boot & switch flow](#boot--switch-flow)
- [The relaunch mechanism (the load-bearing correctness point)](#the-relaunch-mechanism-the-load-bearing-correctness-point)
- [Alternatives considered](#alternatives-considered)
- [Verified assumptions](#verified-assumptions)
- [Open implementation questions (resolve at plan time)](#open-implementation-questions-resolve-at-plan-time)
- [Components / affected files](#components--affected-files)
- [Verification](#verification)
- [Out of scope (YAGNI)](#out-of-scope-yagni)
- [Cold-eyes loop log](#cold-eyes-loop-log)

## Goal

When both DOOM 1 and DOOM 2 data files live in the same folder, let the player
**choose which game to play from inside the engine**, and **switch to the other
game mid-session** without quitting to a shell and re-running with different
arguments.

Concretely: on startup, if both games are found, a **Game Select** screen offers
"DOOM" and "DOOM II". If only one is found, boot straight into it (today's
behaviour, unchanged). Once in a game, an in-menu **Return to Game Select** option
brings the chooser back so the other game can be picked. This is the convenience
that lets a tester exercise a feature (e.g. the switch glow, the see-through
grates) in *both* games without hand-editing a launch command.

## Assumed decisions (pending user confirmation)

Two design choices were put to the user; both are set here to the recommended
default and are **cheap to flip** — they change only which assets draw the picker
and where one menu item sits.

- **A1 — Picker style: DOOM-styled, smart relaunch (recommended).** Boot loads
  whichever game auto-detection finds first (so palette + menu font + a live video
  system exist), and if *both* games are present, the picker is drawn as an
  ordinary DOOM menu over the title screen. Picking the **already-loaded** game
  continues **instantly** (no reload); picking the **other** game relaunches the
  engine. *Alternative (not chosen): a plain built-in-font picker drawn before any
  WAD loads, always relaunching — more neutral-looking, always reloads.*
- **A2 — "Return to Game Select" lives in the Options menu, next to "End Game"
  (recommended).** Groups the two "leave this game" actions and leaves the classic
  top-level menu untouched. Only shown when both games are present. *Alternative:
  put it on the top-level ESC menu near "Quit Game".*

If the user picks the alternatives, the spec deltas are localised: A1-alt replaces
the [relaunch mechanism](#the-relaunch-mechanism-the-load-bearing-correctness-point)'s
"instant if same game" clause with "always relaunch" and adds a pre-WAD text
renderer to [Components](#components--affected-files); A2-alt moves one
`menuitem_t` from `OptionsMenu[]` to `MainMenu[]`.

## Background — why a relauncher, not a live WAD swap

The roadmap already settled this (user-chosen): **each game runs in a fresh engine
boot**, not an in-process IWAD swap. The reason is that WAD data is wired into
process-global state that has no teardown path — the lump directory, the colormap
and playpal, the sprite tables, the zone allocator, and (in this fork) the Vulkan
**bindless material array** and acceleration structures are all built once at load
and never torn down. Swapping IWADs in place would mean unwinding all of that
safely; a fresh `exec` sidesteps it entirely for a fraction of the code and risk.

The cost of the relauncher is a brief black reload when you switch games. That is
acceptable and expected. It is **avoided in the common case** by decision A1: when
the picker offers the game that is already loaded, choosing it just closes the
picker — no reload.

## Approach

Three pieces, each small and reusing existing machinery.

**1. Two-family IWAD detection (d_main.c).** A new `D_DetectIwads()` scans the same
directory and candidate names `IdentifyVersion()` already uses (d_main.c:673–818)
and records, independently of which one got loaded:
- a **DOOM 1** family path if any of `doom.wad` / `doomu.wad` / `doom1.wad` exists
  (registered / retail / shareware);
- a **DOOM 2** family path if any of `doom2.wad` / `doom2f.wad` / `plutonia.wad` /
  `tnt.wad` exists (commercial).

"Both present" ⇔ a DOOM 1 path **and** a DOOM 2 path were found. The picker offers
exactly two entries, "DOOM" → the DOOM 1 path, "DOOM II" → the DOOM 2 path. TNT and
Plutonia are treated as "DOOM II" for v1 (see [Out of scope](#out-of-scope-yagni)).
The engine already knows which *family is currently loaded* from `gamemode`
(`commercial` ⇒ DOOM 2, else DOOM 1), so the picker can tell "this one" from "the
other".

**2. In-process picker menu (m_menu.c).** A new `GameSelectDef` menu with two
items reuses the entire existing menu pipeline — `M_WriteText`/`hu_font` drawing,
`M_Responder` keyboard+gamepad input (the fork's controller support comes along for
free), and the title screen behind it. It is opened:
- **at boot**, from `D_DoomMain` just before `D_StartTitle()` (d_main.c:1302),
  **iff** both families are present **and** no explicit `-iwad` was passed on the
  command line (an explicit `-iwad` means "the caller already chose", so skip);
- **mid-game**, from the new Options → "Return to Game Select" item (decision A2).

Selecting an entry:
- **same family as loaded** → `M_ClearMenus()` and proceed (boot: title screen;
  mid-game: back to play). **No relaunch.**
- **other family** → confirm with the existing `M_StartMessage` yes/no prompt
  (mid-game abandons the current game, exactly like "End Game"), then **relaunch**
  `exe -iwad <other-path>`.

Because the relaunch carries an explicit `-iwad`, the child process skips the boot
picker and lands straight in the chosen game. "Return to Game Select" is the way
back to the chooser; it does not need a relaunch of its own (the menu opens
in-process over the running game).

**3. Options menu item (m_menu.c).** One `menuitem_t` — "Return to Game Select" —
added to `OptionsMenu[]` next to `M_ENDGAM` (m_menu.c:372), calling into the same
`GameSelectDef` open path. It is hidden (drawn disabled / skipped) when only one
family is present, so single-game installs see no new clutter.

## Boot & switch flow

```
D_DoomMain
  IdentifyVersion()        # loads the auto-detected (or -iwad) IWAD as today
  D_DetectIwads()          # NEW: record DOOM1 path, DOOM2 path (both may be set)
  ... existing init (W_InitMultipleFiles, R_Init, menu, video) ...
  if bothPresent && no -iwad on cmdline:
      D_StartTitle()       # title screen up
      M_OpenGameSelect()   # NEW: picker menu on top, drawn with loaded assets
  else:
      D_StartTitle()       # unchanged single-game boot
  D_DoomLoop()             # never returns

Pick "the other game"  ->  confirm  ->  M_SaveDefaults()
                                          I_ShutdownSubsystems()   # video+audio
                                          relaunch exe -iwad <other>
Pick "this game"       ->  M_ClearMenus()  (instant, no reload)
```

## The relaunch mechanism (the load-bearing correctness point)

`exec` **replaces the process image immediately** and does **not** run C `atexit`
handlers, so the engine must shut down cleanly *by hand* before relaunching, or the
GPU/window/audio device leaks into the child:

1. `M_SaveDefaults()` — persist config to `~/.doomrc` (shared across both games,
   d_main.c:699), so renderer choice, volumes, etc. carry over.
2. Explicitly tear down the display and audio (the reverse of the boot init:
   SDL video + the Vulkan device if the 3D back-end is up, and the sound/music
   device) so the child gets a free window and audio device.
3. Resolve the current executable's real path (Linux: `readlink("/proc/self/exe")`;
   Windows: `GetModuleFileNameA`), **not** `myargv[0]` (which may be a bare name or
   relative path that won't re-exec reliably).
4. Relaunch:
   - **POSIX:** `execv(exePath, {exePath, "-iwad", otherPath, NULL})` — replaces the
     process in place.
   - **Windows:** `_spawnv(_P_NOWAIT, exePath, argv)` then a clean `exit(0)` — the
     old process releases its window/device as it exits and the child takes over.
     (`_execv` on Windows spawns-and-returns rather than replacing in place, so the
     spawn-then-exit form is the portable choice.)

The argv passed is intentionally minimal — just `-iwad <path>`. Per-resolution and
per-device settings ride in the shared config, not the command line, so they do not
need forwarding. Original one-shot flags (`-warp`, `-skill`, a `-file` PWAD) are
**not** forwarded: a game-switch starts that game fresh at its title screen. (This
is called out as a deliberate limitation, not an oversight.)

## Alternatives considered

- **In-process IWAD swap.** Rejected by the roadmap and above — no teardown path
  for WAD-global state incl. the bindless material array; large, risky.
- **Pre-WAD built-in-font picker (A1 alternative).** Works before any IWAD loads
  and is IWAD-neutral, but needs a bespoke text renderer that doesn't exist, looks
  un-DOOM, and always reloads even when picking the default. Kept as the A1
  fallback only.
- **Show both games' real `TITLEPIC` side by side.** Would require loading two
  IWADs' palettes/graphics at once — the exact lump-name collision (both define
  `TITLEPIC`, `PLAYPAL`, …) the relauncher exists to avoid. Rejected.
- **A tiny external launcher process/script.** Moves the chooser out of the engine;
  extra artifact to ship and keep in lockstep with the packaged builds, and can't
  offer "Return to Game Select" from inside a running game. Rejected.

## Verified assumptions

Each backed by a read of current source (house rule 13):

- **`-iwad <file>` exists and infers `gamemode` from the filename, returning before
  auto-detect.** d_main.c:702–724 (`commercial`/`shareware`/`retail`/`registered`
  by substring; `D_AddFile(iwad); return;`).
- **Auto-detect order and candidate paths.** d_main.c:673–818 — `doom2f`, `doom2`,
  `plutonia`, `tnt`, `doomu`, `doom`, `doom1`, searched under `$DOOMWADDIR` (default
  `.`). This is the exact set `D_DetectIwads()` reuses.
- **The config path is shared across games.** d_main.c:699 sets
  `basedefault = "$HOME/.doomrc"` unconditionally, before the `-iwad` branch — so a
  relaunch preserves settings.
- **The IWAD is chosen early and the title screen starts at the tail of
  `D_DoomMain`.** `IdentifyVersion()` at d_main.c:924; `D_StartTitle()` at
  d_main.c:1302 (the `else` of the autostart/loadgame branch). This is the hook
  point for the boot picker.
- **`D_StartTitle()` just resets the demo sequence and advances the attract loop.**
  d_main.c:609–614 — nothing there conflicts with opening a menu on top afterwards.
- **The menu system is the reusable substrate.** `MainMenu[]`/`OptionsMenu[]` and
  `menu_t`/`menuitem_t` at m_menu.c:266–389; `M_EndGame` already returns to the
  title via `D_StartTitle()` (m_menu.c:1137–1153); the Options menu's item 0 is
  `M_ENDGAM` (m_menu.c:372) — the anchor for A2.
- **`gamestate_t` and the `D_Display` state switch exist.** doomdef.h:141–145;
  `D_Display` at d_main.c:197. (Relevant only if A1-alt needs a new gamestate;
  the recommended A1 avoids one by reusing the menu overlay.)

## Open implementation questions (resolve at plan time)

These are implementation-level, not design-level; noted so cold-eyes can check the
plan rather than the spec re-deriving them:

- Exact names of the display/audio teardown calls to invoke before `exec` (the
  boot init sequence in `D_DoomMain`/`I_InitGraphics`/`I_InitSound` must be read and
  reversed). The spec asserts *that* teardown is required; the plan pins *which*
  calls.
- Whether opening the picker at boot should freeze the attract-demo timer so the
  title page stays put behind it (cosmetic; a one-liner if needed).
- Whether to persist "last game played" so a cold boot defaults the picker cursor
  to it (nice-to-have; see Out of scope).

## Components / affected files

- **d_main.c** — `D_DetectIwads()` (new), stored DOOM1/DOOM2 paths + `bothPresent`
  flag (new globals or a small struct), the boot-time picker hook before
  d_main.c:1302, and the relaunch helper `D_RelaunchWithIwad(path)`.
- **d_main.h** (or the relevant shared header) — declarations for the picker-open
  and relaunch entry points the menu calls.
- **m_menu.c** — `GameSelectDef` menu + its two handlers and draw routine; the
  Options "Return to Game Select" item (A2) with show/hide on `bothPresent`.
- **i_video.c / i_sound.c (or i_system.c)** — expose/confirm a clean
  display+audio shutdown usable before `exec` (may already exist via the quit
  path; reuse if so).
- **No shader, WAD, or renderer-backend changes.**

## Verification

Manual, in both games (this is a UX/boot feature; the logic is I/O- and
process-level, so an automated unit test has little to bite on — a small unit test
over `D_DetectIwads()`'s family-classification of a list of filenames is the one
worthwhile automated piece, added under `tests/`):

1. **Both present, cold boot** (no `-iwad`): Game Select appears over the title;
   arrow/gamepad navigation works; "DOOM II" (loaded) continues instantly; restart
   and pick "DOOM" → engine relaunches into DOOM 1.
2. **Only one present:** no picker, boots straight in (regression: unchanged).
3. **Explicit `-iwad doom2.wad`:** no picker even with both present.
4. **Mid-game switch:** in DOOM 2, Options → Return to Game Select → "DOOM" →
   confirm → relaunches into DOOM 1; verify renderer/volume settings carried over
   (proves shared-config persistence).
5. **Switch back:** in DOOM 1, Return to Game Select → "DOOM II" → back in DOOM 2.
6. **Windows parity** (Charl's box): steps 1 and 4 relaunch cleanly (no orphaned
   window / lost audio device) via the spawn-then-exit path.
7. `tests/` unit: `D_DetectIwads` classifies each candidate filename into the right
   family and sets `bothPresent` correctly for representative directory listings.

## Out of scope (YAGNI)

- **TNT / Plutonia as their own picker entries.** v1 folds them under "DOOM II"
  (they *are* commercial DOOM 2 mapsets). A four-way picker is a later nicety.
- **Remembering the last game across a full quit/relaunch** (cold boot always
  auto-detects). Optional config key later.
- **Real `TITLEPIC` thumbnails of each game** in the picker (needs dual-WAD load).
- **In-process IWAD hot-swap** (the whole reason for the relauncher).
- **Forwarding one-shot launch flags** across a switch (`-warp`, `-file`, …).

## Cold-eyes loop log

*(To be filled as the /cold-eyes loops run — house rule 14. Loop 2+ runs cold; an
issue not raised again is the proof the fix held.)*

- Loop 1: _pending_.
