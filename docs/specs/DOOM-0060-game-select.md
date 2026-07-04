# DOOM-0060 — Game-select menu: choose DOOM 1 or DOOM 2, switch without relaunching by hand

**Status:** **cold-eyes converged** 2026-07-04 (5 loops — see the [Cold-eyes loop
log](#cold-eyes-loop-log); CRITICAL/HIGH/MEDIUM all cleared by loop 4, LOW-precision
only thereafter). Both decisions below confirmed by the user ("go with your
recommendations"). Cleared for implementation pending a final user spec skim. **Not
yet implemented.**
**Roadmap:** DOOM-0060 (📋, `phase-1-build-modernise-share`). Stays 📋 until shipped.
**Kind:** feature.
**Depends on:** the existing `-iwad <file>` selector (d_main.c:702–724, already in
the fork) and the classic menu system (m_menu.c). No dependency on the 3D/RT work.

## Contents

- [Goal](#goal)
- [Confirmed decisions](#confirmed-decisions)
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

## Confirmed decisions

Two design choices were put to the user and **confirmed 2026-07-04** ("go with your
recommendations"). Both are the recommended defaults below.

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
directory and candidate names `IdentifyVersion()` already uses (the `access()`
sequence at d_main.c:767–818, candidate paths built at d_main.c:673–690) and
records **one representative path per family**, independently of which one got
loaded. When several WADs of a family coexist, the **first** by this fixed
preference order wins (the order is part of the [unit-test acceptance](#verification)):
- **DOOM 1** family — preference `doomu.wad` → `doom.wad` → `doom1.wad`
  (retail → registered → shareware — best/most-complete version first; retail's
  episode 4 "Thy Flesh Consumed" is a superset. This matches `IdentifyVersion`'s own
  `access()` order, which checks `doomu` before `doom`, d_main.c:799–811).
- **DOOM 2** family — preference `doom2.wad` → `doom2f.wad` → `plutonia.wad` →
  `tnt.wad` (English retail commercial first; `doom2f` is the French `doom2` — note
  this deliberately diverges from `IdentifyVersion`'s raw order, which checks
  `doom2f` first).

The per-candidate classification is a **pure helper** `D_IwadFamily(const char*
filename)` → {DOOM 1, DOOM 2, none} that `D_DetectIwads()` applies to each candidate;
factoring it out this way gives [Verification](#verification) step 8 a WAD-free,
directory-free seam to unit-test. **House-rule-3 note:** the candidate path buffers in
`IdentifyVersion` (declared d_main.c:656–663, built 673–690) are function-local stack
arrays, so `D_DetectIwads()` cannot literally reuse them — it re-builds the
`$DOOMWADDIR/<name>` paths. Extracting a shared path-builder is optional; the duplication is small and
bounded to the candidate-name list.

"Both present" ⇔ a DOOM 1 path **and** a DOOM 2 path were found. The picker offers
exactly two entries, "DOOM" → the DOOM 1 path, "DOOM II" → the DOOM 2 path. TNT and
Plutonia are folded under "DOOM II" for v1 (see [Out of scope](#out-of-scope-yagni)).
The engine already knows which *family is currently loaded* from `gamemode`
(`commercial` ⇒ DOOM 2, else DOOM 1), so the picker can tell "this one" from "the
other". With **only one** family present the picker never shows (boot straight in,
today's behaviour). With **zero** IWADs present nothing changes either — that is the
existing no-IWAD path (`IdentifyVersion` leaves `gamemode = indetermined` and prints
the "where to put the WAD" guidance, d_main.c:820+), and `bothPresent` is false, so no
picker.

**Family-vs-path limitation (v1).** "Continue instantly / relaunch the other" keys
on the loaded **family** (`gamemode`), not on the specific loaded path. So if the
loaded commercial WAD differs from the recorded DOOM 2 representative (e.g. the
engine loaded `tnt.wad` but `doom2.wad` is also on disk), picking "DOOM II"
continues the *loaded* `tnt.wad` instantly; the recorded `doom2.wad` is reached only
by first switching to DOOM 1 and back. Reaching a *specific* commercial WAD from the
menu is out of scope for v1 (consistent with folding TNT/Plutonia under "DOOM II").

**2. In-process picker menu (m_menu.c).** A new `GameSelectDef` menu with two
items reuses the entire existing menu pipeline — `M_WriteText`/`hu_font` drawing,
`M_Responder` keyboard+gamepad input (the fork's controller support comes along for
free), and the title screen behind it. It is opened:
- **at boot**, by wrapping the existing `D_StartTitle()` call at d_main.c:1302 as
  `D_StartTitle(); M_OpenGameSelect();` — the picker opens **after** the title loop
  starts, so it draws **on top of** the title screen. This placement inherits the
  correct guard for free: that `D_StartTitle()` call is only reached when the engine
  would otherwise sit on the title screen — i.e. `gameaction != ga_loadgame` **and**
  not (`autostart` or `netgame`) (the `else` branch at d_main.c:1297–1304). So
  `-warp` (autostart), `-loadgame` (`ga_loadgame`), and net play jump straight into a
  game via that branch and bypass the picker. `-playdemo`/`-timedemo` also bypass it,
  but earlier and by a different route — they call `D_DoomLoop()` directly
  (d_main.c:1276 / 1283) and never reach line 1297 at all. Either way the hook is not
  reached. `M_OpenGameSelect()` then additionally no-ops unless both families are
  present **and** no explicit game was chosen on the command line.
  "Explicit choice" = any of `-iwad`, `-shdev`, `-regdev`, `-comdev` (each makes
  `IdentifyVersion()` early-return with a specific game, d_main.c:702–765). `gamemode`
  alone cannot tell an auto-detected DOOM 2 from a `-iwad doom2.wad` one, so the hook
  detects "explicit choice" by re-checking those four flags with `M_CheckParm`, not by
  inspecting `gamemode`.
- **from the Options menu** (decision A2), reachable both from the title screen and
  mid-game.

Selecting an entry:
- **same family as loaded** → `M_ClearMenus()` and proceed (title/attract or back to
  play). **No relaunch.**
- **other family** → **relaunch** `exe -iwad <other-path>`. The relaunch is confirmed
  with the existing `M_StartMessage` yes/no prompt **iff a game is in progress**
  (`usergame` is true — the same guard `M_EndGame` uses, m_menu.c:1140), since only
  then is there progress to abandon. With no game in progress (the boot picker, or
  Options opened from the title screen), the relaunch is **unconfirmed** — the pick
  itself is the confirmation.

Because the relaunch carries an explicit `-iwad`, the child process skips the boot
picker and lands straight in the chosen game. "Return to Game Select" is the way
back to the chooser; it does not need a relaunch of its own (the menu opens
in-process over the running game).

**3. Options menu item (m_menu.c).** A "Return to Game Select" entry next to
`M_ENDGAM` (m_menu.c:372), calling into the same `GameSelectDef` open path. This is
not literally "one line": the classic menu is a fixed-size table sized by an enum,
so the edit touches three coupled places — the `options_e` enum (m_menu.c:355–368),
the `OptionsMenu[]` array (m_menu.c:370–382), and the fixed row y-spacing in
`M_DrawOptions`/`OptionsDef`. **Single-game installs must see no new item** (not a
disabled row). The classic engine has no per-item hidden flag, so the concrete
mechanism is: when only one family is present, register the *original*
`OptionsMenu`/`OptionsDef` (item count `opt_end`); when both are present, register a
one-larger variant that includes the extra item. Selecting the mechanism at menu-init
time avoids adding a hidden-item concept to `M_Responder`/`M_Drawer`.

The Options item is gated on `bothPresent` **only** — deliberately *not* on the
explicit-choice skip that suppresses the boot picker. The two gates answer different
questions: the boot picker asks "which game do you want to start?" (skip it if the
launcher already said, via `-iwad`), whereas "Return to Game Select" asks "switch to
the other game now?" — still useful after an explicit-`-iwad` launch. So launching
`-iwad tnt.wad` with `doom.wad` also on disk shows no boot picker but *does* keep the
Options switch available. This asymmetry is intended.

## Boot & switch flow

```
D_DoomMain
  IdentifyVersion()        # loads the auto-detected (or -iwad) IWAD as today
  D_DetectIwads()          # NEW: record DOOM1 path, DOOM2 path (both may be set)
  ... existing init (W_InitMultipleFiles, R_Init, menu, video) ...
  if gameaction != ga_loadgame:          # existing d_main.c:1297-1304 structure
      if autostart || netgame:
          G_InitNew(...)                 # -warp / net: straight into a game, no picker
      else:
          D_StartTitle()                 # title loop up (unchanged)
          M_OpenGameSelect()             # NEW: no-op unless bothPresent && no explicit
                                         #      game chosen; else picker on top of title
  D_DoomLoop()                           # never returns

Pick "the other game"  ->  [confirm iff usergame]  ->  D_RelaunchWithIwad(other)
                             # POSIX:   M_SaveDefaults(), then execv  (kernel reclaims A/V)
                             # Windows: I_Quit teardown minus exit(), then _spawnv + exit
                             # (see "The relaunch mechanism" for the full per-platform rule)
Pick "this game"       ->  M_ClearMenus()  (instant, no reload)
```

## The relaunch mechanism (the load-bearing correctness point)

`D_RelaunchWithIwad(otherPath)`. Two facts drive the design: `exec` does **not**
run C `atexit` handlers (so config must be saved by hand), and the teardown
requirement differs by platform (below). The engine already has the exact teardown
sequence to reuse — `I_Quit` (i_system.c:140–148) runs
`D_QuitNetGame → I_ShutdownSound → I_ShutdownMusic → M_SaveDefaults → I_ShutdownGraphics → exit(0)`.
The Windows relaunch is "the `I_Quit` body verbatim minus the final `exit(0)`, then
spawn"; POSIX needs only `M_SaveDefaults` before `execv` (see Step 3).

**Step 1 — resolve the executable path (both platforms).** Linux:
`readlink("/proc/self/exe")`; Windows: `GetModuleFileNameA`. **Not** `myargv[0]`
(may be a bare name found via `$PATH`, or a relative path that won't re-exec after a
`chdir`). If resolution fails or truncates, fall back to `myargv[0]`; if that is also
unusable, `I_Error("game-select: cannot locate the engine executable to relaunch")`
— never proceed to tear down subsystems when there is nothing to exec.

**Step 2 — argv.** `{exePath, "-iwad", otherPath, NULL}`. Intentionally minimal:
per-resolution/per-device settings ride in the shared config (`~/.doomrc`,
d_main.c:699), not the command line. Original one-shot flags (`-warp`, `-skill`, a
`-file` PWAD) are **not** forwarded — a game-switch starts that game fresh at its
title screen (deliberate limitation, not an oversight).

**Step 3 — relaunch, per platform:**

- **POSIX:** `M_SaveDefaults()` (persist config; `atexit` won't run), then
  `execv(exePath, argv)`. Manual A/V teardown is **not** required: `execv` replaces
  the process image, and the kernel reclaims the window, GPU context, and audio
  device with the old image, so the child boots on a clean slate. `execv` **returns
  only on failure**; on return, `I_Error("game-select: relaunch failed: %s",
  strerror(errno))` — do **not** fall through into `D_DoomLoop` with the config
  half-changed.
- **Windows:** the parent process **lingers** after `_spawnv`, so it must release the
  window and audio device *before* the child grabs them. Run the `I_Quit` teardown
  sequence **minus** `exit(0)` (which frees graphics + audio and saves defaults),
  **then** `_spawnv(_P_NOWAIT, exePath, argv)`. On success `exit(0)`. On a **negative
  return** (spawn failed — bad path, `ETXTBSY`, AV lock), `I_Error(...)` — **never
  the unconditional `exit(0)`**, which would silently quit the running game reporting
  success. (`_execv` on Windows terminates the parent immediately on success, racing
  device release against the child; the spawn-then-exit form sequences the handoff.)

**Windows device-handoff ordering (residual risk).** Even with release-before-spawn,
`_P_NOWAIT` runs parent and child concurrently, so the child could momentarily reach
audio-device open before the parent's `exit(0)` completes. The child's audio-init
path must therefore tolerate a transient device-busy. Whether the current
`I_InitSound` path already degrades gracefully on a busy device is **unverified** and
is an [open implementation question](#open-implementation-questions-resolve-at-plan-time)
to confirm at plan time; [Verification](#verification) step 6 exercises this on Windows.

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
- **Auto-detect order and candidate paths.** Candidate paths built at
  d_main.c:673–690; the `access()` check order at d_main.c:767–818 — `doom2f`,
  `doom2`, `plutonia`, `tnt`, `doomu`, `doom`, `doom1`, searched under `$DOOMWADDIR`
  (default `.`). This is the exact candidate set `D_DetectIwads()` reuses (with the
  family preference order defined in [Approach §1](#approach), which is *not* the
  raw `access()` order — the picker prefers English `doom2` over French `doom2f`).
- **The config path is shared across games.** d_main.c:699 sets
  `basedefault = "$HOME/.doomrc"` unconditionally, before the `-iwad` branch — so a
  relaunch preserves settings.
- **The IWAD is chosen early and the title screen starts at the tail of
  `D_DoomMain`.** `IdentifyVersion()` at d_main.c:924; `D_StartTitle()` at
  d_main.c:1302 (the `else` of the autostart/loadgame branch). This is the hook
  point for the boot picker.
- **`D_StartTitle()` just resets the demo sequence and advances the attract loop.**
  d_main.c:609–614 — nothing there conflicts with opening a menu on top afterwards.
- **The menu system is the reusable substrate.** The `menuitem_t`/`menu_t` typedefs
  are at m_menu.c:146–173; the `MainMenu[]`/`OptionsMenu[]` tables and their `menu_t`
  defs at m_menu.c:266–392. `M_EndGame` returns to the title via `D_StartTitle()`
  called from `M_EndGameResponse` (m_menu.c:1127–1135), and guards on `usergame`
  (m_menu.c:1140); the Options menu's item 0 is `M_ENDGAM` (m_menu.c:372) — the
  anchor for A2.
- **`gamestate_t` and the `D_Display` state switch exist.** doomdef.h:139–145;
  `D_Display` at d_main.c:197. (Relevant only if A1-alt needs a new gamestate;
  the recommended A1 avoids one by reusing the menu overlay.)

## Open implementation questions (resolve at plan time)

These are implementation-level, not design-level; noted so cold-eyes can check the
plan rather than the spec re-deriving them:

- **Windows audio-busy tolerance.** The relaunch's Windows path can momentarily have
  parent and child both wanting the audio device (see the relaunch section). Whether
  `I_InitSound` already degrades gracefully on a transiently-busy device, or needs a
  short retry, is unverified — confirm at plan time.
- Whether opening the picker at boot should freeze the attract-demo timer so the
  title page stays put behind it (cosmetic; a one-liner if needed).
- Whether to persist "last game played" so a cold boot defaults the picker cursor
  to it (nice-to-have; see Out of scope).

- **Recorded IWAD path unreadable at switch time.** The DOOM 1 / DOOM 2 paths are
  recorded once at boot; if the "other" file is moved/deleted before a switch, the
  relaunch's `-iwad` will fail. Falls into the relaunch-failure branch (`I_Error`, not
  a silent `exit`); [Verification](#verification) step 7 covers the failure branch
  generally. Re-`access()`-checking the path just before relaunch is a cheap optional
  guard for a friendlier message.

*(Resolved in this spec, no longer open: the pre-relaunch teardown call names — it is
the `I_Quit` body minus `exit(0)`, i_system.c:140–148, starting with `D_QuitNetGame`;
and the exec-path fallback — `readlink`/`GetModuleFileNameA`, else `myargv[0]`, else
`I_Error`.)*

## Components / affected files

- **d_main.c** — `D_IwadFamily(filename)` (new, pure classifier), `D_DetectIwads()`
  (new, scans + records the DOOM1/DOOM2 representative paths + `bothPresent` flag as
  new globals or a small struct), the boot-time picker hook — `M_OpenGameSelect()`
  called immediately **after** the `D_StartTitle()` at d_main.c:1302 — and the
  relaunch helper `D_RelaunchWithIwad(path)`.
- **d_main.h** (or the relevant shared header) — declarations for the picker-open
  and relaunch entry points the menu calls.
- **m_menu.c** — `GameSelectDef` menu + its two handlers and draw routine; the
  Options "Return to Game Select" item (A2) registered only in the both-present
  Options variant (per [Approach §3](#approach) — enum + array + row-spacing edits,
  no runtime hidden-item flag).
- **i_system.c** — the Windows pre-relaunch teardown reuses the `I_Quit` **body**
  (i_system.c:140–148, incl. its leading `D_QuitNetGame`) minus the final `exit(0)`;
  factor that body out into a shared helper so `D_RelaunchWithIwad` and `I_Quit` don't
  duplicate it. POSIX needs only `M_SaveDefaults` (no A/V teardown). No new shutdown
  code needed.
- **No shader, WAD, or renderer-backend changes.**

## Verification

Manual, in both games (this is a UX/boot feature; the logic is I/O- and
process-level, so an automated unit test has little to bite on — a small unit test
over the pure `D_IwadFamily()` classifier is the one worthwhile automated piece,
added under `tests/`):

1. **Both present, cold boot** (no `-iwad`): Game Select appears over the title;
   arrow/gamepad navigation works; "DOOM II" (loaded) continues instantly; restart
   and pick "DOOM" → engine relaunches into DOOM 1.
2. **Only one present:** no picker, boots straight in (regression: unchanged).
3. **Explicit choice skips the picker:** `-iwad doom2.wad` (and each of `-shdev` /
   `-regdev` / `-comdev`) boots straight in even with both present.
4. **Mid-game switch:** in DOOM 2, Options → Return to Game Select → "DOOM" →
   confirm → relaunches into DOOM 1; verify renderer/volume settings carried over
   (proves shared-config persistence).
5. **Switch back:** in DOOM 1, Return to Game Select → "DOOM II" → back in DOOM 2.
6. **Windows parity** (Charl's box): steps 1 and 4 relaunch cleanly (no orphaned
   window / lost audio device) via the spawn-then-exit path.
7. **Relaunch-failure path:** with a deliberately unresolvable exe path (or a
   spawn forced to fail), the engine surfaces `I_Error` and does **not** silently
   `exit(0)` — confirms the failure branch, not just the happy path.
8. `tests/` unit: (a) `D_IwadFamily()` classifies each candidate filename into the
   right family (and rejects non-IWAD names); (b) `D_DetectIwads` applies the family
   **preference order** when several of a family are present (`doomu.wad` beats
   `doom.wad` beats `doom1.wad`; `doom2.wad` beats `doom2f`/`tnt`/`plutonia`); and
   (c) sets `bothPresent` correctly for representative directory listings.

## Out of scope (YAGNI)

- **TNT / Plutonia as their own picker entries.** v1 folds them under "DOOM II"
  (they *are* commercial DOOM 2 mapsets). A four-way picker is a later nicety.
- **Remembering the last game across a full quit/relaunch** (cold boot always
  auto-detects). Optional config key later.
- **Real `TITLEPIC` thumbnails of each game** in the picker (needs dual-WAD load).
- **In-process IWAD hot-swap** (the whole reason for the relauncher).
- **Forwarding one-shot launch flags** across a switch (`-warp`, `-file`, …).
- **French `doom2f.wad` localisation across a switch.** A relaunch always uses the
  `-iwad` path, and `IdentifyVersion`'s `-iwad` branch sets `gamemode = commercial`
  but not `language = french` (French is only set on the auto-detect `doom2f` branch,
  d_main.c:767–773). So switching *into* `doom2f.wad` via the picker loses the French
  language selection until the next cold (no-`-iwad`) boot. Accepted v1 limitation;
  fixable later by having the relaunch also infer/pass language.

## Cold-eyes loop log

*(Loop 2+ runs cold — reviewers are re-briefed with the doc + cited code only, never
a list of prior findings; entries are count-summaries so a re-read stays independent.)*

- **Loop 1 (2026-07-04, 2 reviewers):** 2 HIGH, 5 MEDIUM, several LOW verified and
  fixed (relaunch failure/teardown correctness, family-precedence + explicit-choice
  skip conditions, boot-vs-mid-game confirm, a fabricated symbol, and citation slips).
  Convergent across both reviewers. All fixed in-place; re-running.
- **Loop 2 (2026-07-04, 2 reviewers):** 0 CRITICAL, 0 HIGH; 3 MEDIUM + a few LOW
  verified and fixed (incomplete `I_Quit` sequence, flow-diagram vs POSIX-teardown
  conflict, confirm-rule keyed on `usergame`, ROADMAP drift — superseding note
  appended, two line-slips, zero-IWAD case). Convergent. Re-running.
- **Loop 3 (2026-07-04, 2 reviewers):** 0 CRITICAL, 0 HIGH; 4 MEDIUM + a few LOW
  verified and fixed (deeper integration issues: boot-hook call order + skip set now
  tied to the real `ga_loadgame`/autostart structure; DOOM 1 preference corrected to
  retail-first; a pure `D_IwadFamily()` classifier named for the unit test; the
  residual ROADMAP old-text contradiction edited in-place; French-`doom2f` and
  stack-local-path-buffer limitations noted). Convergent. All fixed; re-running.
- **Loop 4 (2026-07-04, 2 reviewers):** 0 findings in the **spec** — both reviewers
  clean on it. The only verified findings were drift in the ROADMAP bullet's own
  progress notes (stale DOOM 1 order + loop count from earlier passes) + one LOW spec
  citation split; ROADMAP note rewritten drift-resistant (points here for the loop
  record). Re-running to confirm.
- **Loop 5 (2026-07-04, 2 reviewers):** 0 CRITICAL, 0 HIGH, 0 MEDIUM. Two LOW
  precision nits (a `-playdemo`/`-timedemo` bypass mis-attributed to the else-branch
  guard rather than their early `D_DoomLoop()` return; a boot-picker-vs-Options
  gating asymmetry left undocumented) verified and fixed; ROADMAP↔spec fully
  reconciled. **Converged** — reached the `--max-loops` cap of 5 with severity having
  strictly decreased each pass (HIGH+MED → MED → MED → ROADMAP-only → LOW-only) and
  every code citation independently re-verified line-for-line across passes.
