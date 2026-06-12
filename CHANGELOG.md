# Changelog

All notable changes to DOOM_Ants are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Completed
`ROADMAP.md` items graduate into this file under the release they ship in.

## [Unreleased]

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

---

DOOM_Ants is a GPL-v2 derivative of id Software's DOOM source code
(released 1997-12-23).

### Changed

- **Replace legacy X11 video & sound with SDL2.** (DOOM-0004)
  Swap the ancient display/sound code for a modern, cross-platform layer.

### Fixed

- **Fix savegame crash by giving saves a real heap buffer.** (DOOM-0017)
  Saving a game no longer corrupts memory or crashes — the save data now gets its own properly-sized space instead of being squeezed into the video screen memory.

- **Get linuxdoom-1.10 compiling on modern 64-bit Linux.** (DOOM-0003)
  Fix the 1997 code so today's compiler can build it.
