# Changelog

All notable changes to DOOM_Ants are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Completed
`ROADMAP.md` items graduate into this file under the release they ship in.

## [Unreleased]

### Added

- **Keep the classic 2.5D renderer selectable alongside the 3D renderer.** (DOOM-0026)
  When the new 3D renderer arrives, you'll still be able to switch back to the original DOOM look from the main menu - both renderers ship in the same build.

- **Render the classic view at 640x400 (hi-res).** (DOOM-0027)
  The picture is now drawn at double the internal detail (640x400 instead of 320x200), so walls, floors and monsters look crisp instead of blocky when the window is enlarged - the classic DOOM look is unchanged, just sharper. Built from a design spec hardened through 6 cold-eyes review loops; the 320x200 UI art (status bar, menus, HUD, intermission, finale) is integer-doubled to match. (Code-complete and building clean; pending an in-game play-test.)

### Changed

- **Mark I_Error as _Noreturn.** (DOOM-0023)
  Tells the compiler that the fatal-error function never returns, so it can optimise better and reason correctly about the code that runs after a fatal-error guard.

### Fixed

- **Fix undefined order-of-evaluation in sndserv's strupr.** (DOOM-0029)
  Tidied a tiny string-uppercasing routine in the standalone sound server that relied on undefined C behaviour, so it now works reliably on any compiler.

- **Fix an off-by-one out-of-bounds read in the menu/finale font renderer.** (DOOM-0028)
  A stray character in on-screen text could make the game read one slot past the end of the font table; now it's skipped cleanly like any other non-font character.

- **Stop leaking the candidate IWAD path strings in IdentifyVersion.** (DOOM-0025)
  Tidies up the data-file search at startup so the small scratch strings it builds while hunting for your DOOM .wad are no longer left dangling in memory.

- **Harden the config-file parser against an over-long line.** (DOOM-0024)
  A corrupt or hand-edited config file with a very long line can no longer overflow an internal buffer and crash the game.

- **Print pointers with %p instead of %lx in s_sound debug output.** (DOOM-0022)
  A cosmetic fix to the debug logging so memory addresses print correctly on 64-bit builds.

- **Fix off-by-one ammo-type bounds check in P_GiveAmmo.** (DOOM-0021)
  Corrects a boundary test on ammo types so an edge value can't read past the end of the ammo array.

- **Add the missing mobjinfo bounds guard in P_RespawnSpecials.** (DOOM-0020)
  Hardens the item-respawn code against an out-of-bounds read if a queued item has an unrecognised type.

- **Fix level-load reset clearing pointer size instead of the mouse/joy button arrays.** (DOOM-0019)
  On each new level the game tried to wipe the mouse and joystick button state but only cleared a sliver of it, so a button could appear stuck for a moment after a level loads.

- **Fix undefined behaviour in the event-queue ring increment.** (DOOM-0018)
  Fixes a hidden flaw where the input-event counter was updated in a way modern compilers are allowed to mishandle - it could silently drop or scramble keypresses on a new compiler.

## [0.1.0] - 2026-06-12

First playable release: id Software's 1997 DOOM engine building and running on
modern 64-bit Linux through SDL2, now with music, a fixed save system, and
quality-of-life input and windowing improvements. A Linux (x86-64) build is
attached; the Windows build and a fully self-contained package come later.

### Added
- **Play the in-WAD music as clean General MIDI.** (DOOM-0016)
  Turns on DOOM's soundtrack — title screen, every level, the intermission and end screens — rendered as smooth, modern General MIDI. Sound effects are untouched.

- **Add a -iwad switch and detect Ultimate Doom by content.** (DOOM-0015)
  Pick which game to run with -iwad <file>, so DOOM 1 and DOOM 2 can share one folder. A doom.wad that is Ultimate Doom now correctly offers all four episodes.

- **Make the game window larger, resizable, and fullscreen-capable.** (DOOM-0014)
  Opens at a bigger size, can be resized by dragging, and runs fullscreen with -fullscreen. (The picture is bigger; the internal detail is still the original 320x200 — true high-resolution rendering is a separate Phase 2 job.)

- **Add WASD movement keys alongside the arrow keys.** (DOOM-0013)
  Lets you move with the modern W/A/S/D keys — W/S walk, A/D step sideways — not just the arrow keys.

- Project documentation and standards tree (`CLAUDE.md`, `README.md`,
  `docs/standards/`).
- `ROADMAP.md` with the three-phase plan: Foundations, Build & Modernise,
  and The Spin (the 3D / ray-tracing renderer overhaul).

### Changed

- **Replace legacy X11 video & sound with SDL2.** (DOOM-0004)
  Swap the ancient display/sound code for a modern, cross-platform layer.

### Fixed

- **Fix savegame crash by giving saves a real heap buffer.** (DOOM-0017)
  Saving a game no longer corrupts memory or crashes — the save data now gets its own properly-sized space instead of being squeezed into the video screen memory.

- **Get linuxdoom-1.10 compiling on modern 64-bit Linux.** (DOOM-0003)
  Fix the 1997 code so today's compiler can build it.

[Unreleased]: https://github.com/milnet01/DOOM_Ants/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/milnet01/DOOM_Ants/releases/tag/v0.1.0

---

DOOM_Ants is a GPL-v2 derivative of id Software's DOOM source code
(released 1997-12-23).
