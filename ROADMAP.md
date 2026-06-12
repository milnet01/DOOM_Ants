# Roadmap

The plan for DOOM_Ants, in three phases. Every actionable item carries a
permanent `[DOOM-NNNN]` ID (append-only — never renumbered, never reused).
Status: 📋 planned · 🚧 in progress · ✅ shipped · 💭 considered.

See `docs/standards/roadmap-format.md` for the format. Shipped items graduate
into `CHANGELOG.md`.

## Phase 0 — Foundations

Documentation, standards, and getting the project published. The groundwork
that everything else builds on.

- 🚧 [DOOM-0001] **Establish the documentation & standards tree.**
  **Layman:** Set up the project's rulebooks and roadmap so the work stays organised and unambiguous.
  Kind: doc.
  Source: in-session-2026-06-11.
- 📋 [DOOM-0002] **Publish DOOM_Ants as a public GitHub repository.**
  **Layman:** Put the project online, publicly, so it can be shared and downloaded.
  Kind: chore.
  Source: in-session-2026-06-11.

## Phase 1 — Build, Modernise & Share

Get the 1997 engine compiling, running, and playable on a modern machine —
without changing how it plays. The SDL2 layer makes this cross-platform, so
this phase also delivers a **Windows** build and downloadable releases to share
with friends.

- ✅ [DOOM-0003] **Get linuxdoom-1.10 compiling on modern 64-bit Linux.**
  **Layman:** Fix the 1997 code so today's compiler can build it.
  Kind: fix.
  Source: in-session-2026-06-11.
  Resolved (2026-06-12): builds under gcc 15 / C23 via -std=gnu11; m_misc.c config-table and am_map.c implicit-int repairs.
- ✅ [DOOM-0004] **Replace legacy X11 video & sound with SDL2.**
  **Layman:** Swap the ancient display/sound code for a modern, cross-platform layer.
  Kind: refactor.
  Source: in-session-2026-06-11.
  Resolved (2026-06-12): SDL2 video (ARGB texture, integer scale) + in-process SDL audio mixer replace X11/sndserver; single self-contained binary.
- ✅ [DOOM-0005] **Boot with a shareware WAD and confirm gameplay.**
  **Layman:** Actually run it with DOOM's data and check it plays like the original.
  Kind: test.
  Source: in-session-2026-06-11.
  Progress (2026-06-12): boots Ultimate Doom / DOOM II with SDL2 video+audio; renders a warped-in level for 25s+ with no crash (verified headless via SDL dummy drivers). Four 64-bit pointer fixes landed (r_data maptexture/array sizing, p_setup line list, colormap/translation alignment). Remaining: a human playtest on a real display to confirm input feel and visuals; shareware doom1.wad not yet exercised (tested with retail IWADs).
  Resolved (2026-06-12): user playtested DOOM II on a real display - boots, renders, controls respond, and SDL sound effects play. Plays like the original.
- 📋 [DOOM-0006] **Add a Windows build target.**
  **Layman:** Produce a version that runs on Windows so friends can play it.
  Kind: feature.
  Source: in-session-2026-06-11.
- 📋 [DOOM-0007] **Publish downloadable Linux & Windows builds via GitHub Releases.**
  **Layman:** Put ready-to-run downloads online so anyone can grab a copy.
  Kind: release.
  Source: in-session-2026-06-11.

- ✅ [DOOM-0013] **Add WASD movement keys alongside the arrow keys.**
  **Layman:** Lets you move with the modern W/A/S/D keys — W/S walk, A/D step sideways — not just the arrow keys.
  Kind: feature.
  Source: user-request-2026-06-12.

- ✅ [DOOM-0014] **Make the game window larger, resizable, and fullscreen-capable.**
  **Layman:** Opens at a bigger size, can be resized by dragging, and runs fullscreen with -fullscreen. (The picture is bigger; the internal detail is still the original 320x200 — true high-resolution rendering is a separate Phase 2 job.)
  Kind: enhancement.
  Source: user-request-2026-06-12.

- ✅ [DOOM-0015] **Add a -iwad switch and detect Ultimate Doom by content.**
  **Layman:** Pick which game to run with -iwad <file>, so DOOM 1 and DOOM 2 can share one folder. A doom.wad that is Ultimate Doom now correctly offers all four episodes.
  Kind: feature.
  Source: user-request-2026-06-12.

- ✅ [DOOM-0016] **Play the in-WAD music as clean General MIDI.**
  Music has been silent since the SDL2 port (DOOM-0004) — the I_*Song / I_*Music functions in i_sound.c are empty stubs. Wire them to SDL2_mixer on a SECOND audio device at 44.1 kHz (separate from the 11025 Hz effects mixer, which stays untouched), rendering MIDI via FluidSynth + the FluidR3_GM soundfont. DOOM music lumps are MUS format, converted in-memory to MIDI by a ported (GPL v2) Chocolate DOOM mus2mid. No game code changes — only i_sound.c music functions + a new mus2mid.c/.h + a 3-line Makefile change. Spec: docs/specs/DOOM-0016-music.md.
  **Layman:** Turns on DOOM's soundtrack — title screen, every level, the intermission and end screens — rendered as smooth, modern General MIDI. Sound effects are untouched.
  Kind: feature.
  Source: user-request-2026-06-12.
  Resolved (2026-06-12): SDL2_mixer second device (44.1 kHz) + FluidSynth/FluidR3_GM; new mus2mid.c MUS->MIDI converter; effects mixer untouched. Found + fixed that stock linuxdoom never called I_InitMusic. Verified: clean build, real D_E1M1/D_INTRO lumps convert to valid MIDI and load via Mix_LoadMUS_RW. Audible listen-test pending on hardware.

- ✅ [DOOM-0017] **Fix savegame crash by giving saves a real heap buffer.**
  Stock linuxdoom parked the savegame buffer inside the video screen buffer (screens[1]+0x4000). The four screens are one 256000-byte block, so that left only ~175 KiB of real backing - less than SAVEGAMESIZE (0x2c000=180 KiB) - and the overrun check ran only AFTER writing. On 64-bit the archived structs are larger (mobj_t is 224 bytes), so a busy level's save overran the screen buffers into the heap and crashed. Resolved (2026-06-12): G_DoSaveGame now Z_Mallocs a dedicated SAVEGAMESIZE buffer (mirroring the load path's M_ReadFile/Z_Free) and frees it after writing; SAVEGAMESIZE raised 0x2c000 -> 0x80000 (512 KiB) for 64-bit headroom. Build-verified; in-game save/load playtest pending.
  **Layman:** Saving a game no longer corrupts memory or crashes — the save data now gets its own properly-sized space instead of being squeezed into the video screen memory.
  Kind: fix.
  Source: user-request-2026-06-12.

## Phase 2 — The Spin

The creative overhaul: evolve the renderer toward true 3D with ray tracing and
modern lighting, holding 60 FPS, while keeping the original DOOM feel. These are
parked ideas (💭 considered) until we commit to and design each one.

- 💭 [DOOM-0008] **Convert the renderer to true 3D.**
  **Layman:** Replace DOOM's fake-3D trick with a real 3D engine.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0009] **Add hardware ray tracing (path tracing where feasible).**
  **Layman:** Use the graphics card to trace real light rays for accurate reflections and shadows.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0010] **Add dynamic lighting.**
  **Layman:** Let lights move and react — muzzle flashes, flickering lamps — lighting the scene live.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0011] **Add volumetric lighting (god rays).**
  **Layman:** Visible shafts of light through smoke and doorways.
  Kind: feature.
  Source: in-session-2026-06-11.
- 💭 [DOOM-0012] **Hold a 60 FPS performance floor.**
  **Layman:** Keep it running smoothly — never below 60 frames per second.
  Kind: perf.
  Source: in-session-2026-06-11.
