# Roadmap

The plan for DOOM_Ants, in three phases. Every actionable item carries a
permanent `[DOOM-NNNN]` ID (append-only — never renumbered, never reused).
Status: 📋 planned · 🚧 in progress · ✅ shipped · 💭 considered.

See `docs/standards/roadmap-format.md` for the format. Shipped items graduate
into `CHANGELOG.md`.

## Phase 0 — Foundations

Documentation, standards, and getting the project published. The groundwork
that everything else builds on.

- ✅ [DOOM-0001] **Establish the documentation & standards tree.**
  **Layman:** Set up the project's rulebooks and roadmap so the work stays organised and unambiguous.
  Kind: doc.
  Source: in-session-2026-06-11.
  Resolved (2026-06-15): documentation & standards tree is in place — README.TXT, CLAUDE.md, ROADMAP.md, CHANGELOG.md, docs/specs/, and the four house-rule standards (docs/standards/coding.md, commits.md, documentation.md, roadmap-format.md), all with substantive content. Tree is established; further standards get added on demand per their own "add only when a real decision forces it" rule.
- ✅ [DOOM-0002] **Publish DOOM_Ants as a public GitHub repository.**
  **Layman:** Put the project online, publicly, so it can be shared and downloaded.
  Kind: chore.
  Source: in-session-2026-06-11.
  Resolved (2026-06-12): already live and public at https://github.com/milnet01/DOOM_Ants (visibility PUBLIC, non-empty, described). Origin remote is set; today's commits are pending a push.

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
- ✅ [DOOM-0006] **Add a Windows build target.**
  **Layman:** Produce a version that runs on Windows so friends can play it.
  Kind: feature.
  Source: in-session-2026-06-11.
  Deferred (2026-06-12, user): the Windows build only matters once the Phase 2 3D version exists, so it is parked until then. Survey notes for when we pick it up: openSUSE Tumbleweed ships mingw64-cross-gcc but NO mingw SDL2 / SDL2_mixer / fluidsynth packages (would need source cross-builds of those, incl. the FluidR3_GM soundfont bundled and pointed at via $DOOM_SOUNDFONT). Remaining POSIX porting surface: i_net.c (BSD sockets/ioctl), i_system.c (gettimeofday, usleep), unistd/sys-time includes in w_wad.c/m_misc.c/d_main.c/r_data.c/m_menu.c.
  Resolved (2026-06-17): added a `make windows` mingw-w64 cross-build target. mingw-w64 supplies DOOM's POSIX shims, so the only _WIN32-guarded divergences were i_net.c (BSD sockets -> Winsock2), i_main.c (SDL_MAIN_HANDLED), d_main.c ($HOME->%USERPROFILE%, one-arg mkdir), doomtype.h/m_bbox.h (portable MAXINT vs glibc <values.h>), and w_wad.c (don't redefine mingw strupr/filelength; conditional O_BINARY). SDL2 2.32.10 / SDL2_mixer 2.8.2 / Vulkan-Headers 1.4.350.0 mingw dev libs staged under mingw-deps/ (git-ignored; README documents fetch). Builds mingw/doom_ants.exe (PE32+ x86-64); verified under Wine through engine init to the expected no-WAD exit; native Linux build unchanged. Commit eac0cd4.
- ✅ [DOOM-0007] **Publish downloadable Linux & Windows builds via GitHub Releases.**
  **Layman:** Put ready-to-run downloads online so anyone can grab a copy.
  Kind: release.
  Source: in-session-2026-06-11.
  Progress (2026-06-12): Linux half shipped — v0.1.0 GitHub Release with doom_ants-0.1.0-linux-x86_64.tar.gz (binary + README + LICENSE; runtime deps documented). https://github.com/milnet01/DOOM_Ants/releases/tag/v0.1.0 . Windows build half stays open until DOOM-0006 lands (Phase 2). A fully self-contained Linux package (AppImage) is a possible follow-up.
  Resolved (2026-06-17): both platforms now downloadable. Linux = v0.1.0 stable (doom_ants-0.1.0-linux-x86_64.tar.gz). Windows = v0.2.0-pre.1 preview (doom_ants-0.2.0-pre.1-windows-x86_64.zip: doom_ants.exe + SDL2/SDL2_mixer/winpthread DLLs + README + LICENSE), published as a pre-release because the Phase-2 3D renderer (DOOM-0008) is still in progress. Follow-up options remain: a fully self-contained Linux AppImage, and promoting the snapshot to a finalised versioned release once Phase 2 lands.
  Follow-up delivered (2026-06-17): the self-contained Linux AppImage is done and attached to the v0.2.0-pre.1 release (doom_ants-0.2.0-pre.1-x86_64.AppImage, 9.5 MB). Bundles SDL2/SDL2_mixer/FluidSynth + a compact FluidR3_GS General-MIDI soundfont, so it runs (with music) on a fresh distro with no dependency install — only a WAD needed. Reproducible via packaging/build-appimage.sh (linuxdeploy + appimagetool; committed desktop file + icon). Inherits the build host's glibc floor, so it targets reasonably current distros. Both downloads (Windows zip + Linux AppImage) are now one-download-and-done.

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

- ✅ [DOOM-0018] **Fix undefined behaviour in the event-queue ring increment.**
  cppcheck (unknownEvaluationOrder) + PVS-Studio both flag `eventhead = (++eventhead)&(MAXEVENTS-1)` in D_PostEvent/D_ProcessEvents (d_main.c) and the CheckAbort loop (d_net.c): the variable is modified twice with no sequence point between - textbook C undefined behaviour that gcc 15 / C23 may miscompile. Fix: `x = (x + 1) & (MAXEVENTS-1)` (Chocolate Doom rewrote D_PostEvent the same way). Three sites.
  **Layman:** Fixes a hidden flaw where the input-event counter was updated in a way modern compilers are allowed to mishandle - it could silently drop or scramble keypresses on a new compiler.
  Kind: fix.
  Source: audit-2026-06-12.
  Resolved (2026-06-12): rewrote the three ring-increment sites as `x = (x + 1) & (MAXEVENTS-1)` (D_PostEvent + D_ProcessEvents in d_main.c, CheckAbort loop in d_net.c). cppcheck unknownEvaluationOrder cleared; clean build. Confirmed against PVS-Studio's DOOM analysis + Chocolate Doom's D_PostEvent rewrite.

- ✅ [DOOM-0019] **Fix level-load reset clearing pointer size instead of the mouse/joy button arrays.**
  g_game.c: `mousebuttons`/`joybuttons` are pointers (`boolean* = &array[1]`), so `memset(mousebuttons,0,sizeof(mousebuttons))` clears only sizeof(pointer)=8 bytes, not the array. Documented by PVS-Studio. Fix: memset the underlying `mousearray`/`joyarray` by their real size.
  **Layman:** On each new level the game tried to wipe the mouse and joystick button state but only cleared a sliver of it, so a button could appear stuck for a moment after a level loads.
  Kind: fix.
  Source: audit-2026-06-12.
  Resolved (2026-06-12): memset now clears the underlying mousearray[4]/joyarray[5] by real size instead of sizeof(pointer). cppcheck pointerSize cleared; clean build.

- ✅ [DOOM-0020] **Add the missing mobjinfo bounds guard in P_RespawnSpecials.**
  p_mobj.c P_RespawnSpecials: the doomednum-match loop can leave `i == NUMMOBJTYPES` with no guard before `mobjinfo[i]` is read (line ~620). P_SpawnMapThing has the equivalent `if (i==NUMMOBJTYPES) I_Error(...)` guard; mirror it here. Latent (unreachable with well-formed maps) but a real OOB read.
  **Layman:** Hardens the item-respawn code against an out-of-bounds read if a queued item has an unrecognised type.
  Kind: fix.
  Source: audit-2026-06-12.
  Resolved (2026-06-12): added `if (i==NUMMOBJTYPES) I_Error(...)` guard before the mobjinfo[i] access in P_RespawnSpecials, mirroring P_SpawnMapThing. Latent OOB read closed. (cppcheck still reports the access as a phantom OOB because it does not model I_Error's _Noreturn — logged to .ants_review_falsepos.jsonl.)

- ✅ [DOOM-0021] **Fix off-by-one ammo-type bounds check in P_GiveAmmo.**
  p_inter.c P_GiveAmmo: `if (ammo < 0 || ammo > NUMAMMO)` lets ammo==NUMAMMO(4) through to `player->ammo[4]` (array size 4, valid 0-3). Fix: `>= NUMAMMO`. Unreachable in normal play but a genuine guard off-by-one.
  **Layman:** Corrects a boundary test on ammo types so an edge value can't read past the end of the ammo array.
  Kind: fix.
  Source: audit-2026-06-12.
  Resolved (2026-06-12): changed the P_GiveAmmo guard from `ammo > NUMAMMO` to `ammo >= NUMAMMO`, so ammo==NUMAMMO(4) is caught before the OOB ammo[4] access. Clean build.

- ✅ [DOOM-0022] **Print pointers with %p instead of %lx in s_sound debug output.**
  s_sound.c saw-channel debug fprintf()s pass `mobj_t*`/`sfxinfo_t*`/`void*` to `%lx` (expects unsigned long) - harmless on LP64 but technically wrong varargs type, flagged by cppcheck (invalidPrintfArgType_uint). Fix: use `%p`.
  **Layman:** A cosmetic fix to the debug logging so memory addresses print correctly on 64-bit builds.
  Kind: fix.
  Source: audit-2026-06-12.
  Resolved (2026-06-12): the saw-channel debug fprintf()s now use %p for pointers instead of 0x%lx. gcc -Wformat warnings cleared; cppcheck invalidPrintfArgType cleared.

- ✅ [DOOM-0023] **Mark I_Error as _Noreturn.**
  i_system.c/.h: `I_Error` always exits but isn't declared `_Noreturn`, so cppcheck reports false-positive out-of-bounds/return-path warnings after I_Error guards (e.g. p_mobj.c:764). Add C11 `_Noreturn` to the declaration + definition; helps gcc 15 flow analysis.
  **Layman:** Tells the compiler that the fatal-error function never returns, which lets it optimise better and silences a batch of bogus static-analysis warnings.
  Kind: refactor.
  Source: audit-2026-06-12.
  Resolved (2026-06-12): I_Error declared `_Noreturn` in i_system.h + i_system.c (it ends in exit(-1)). Benefits gcc 15 flow analysis/optimisation. NB cppcheck does NOT honour the C11 _Noreturn keyword for its OOB analysis (only its library .cfg noreturn list), so the guarded-access false positives it reports remain — logged to the ledger rather than silenced.

- ✅ [DOOM-0024] **Harden the config-file parser against an over-long line.**
  M_LoadDefaults read config lines with an unbounded %[^\n] conversion into strparm[100], so any line longer than 99 chars smashed the stack. Bounded it to %99[^\n]. Also corrected the hex branch, which read %x into a signed int (now through an unsigned int* alias). gcc build clean; cppcheck invalidscanf + invalidScanfArgType findings cleared.
  **Layman:** A corrupt or hand-edited config file with a very long line can no longer overflow an internal buffer and crash the game.
  Kind: fix.
  Source: in-session-2026-06-12 (cppcheck audit).

- ✅ [DOOM-0025] **Stop leaking the candidate IWAD path strings in IdentifyVersion.**
  IdentifyVersion malloc'd seven candidate IWAD path strings up front and returned on the first access() match without freeing the rest (cppcheck flagged ~12 memleak paths, including the new -iwad early return). D_AddFile copies its argument, so the buffers are pure local scratch - converted them to fixed PATH_MAX stack buffers written with snprintf, removing the leak by construction. A path longer than PATH_MAX can't name a real file, so snprintf truncation is harmless. gcc build clean; all memleak findings cleared.
  **Layman:** Tidies up the data-file search at startup so the small scratch strings it builds while hunting for your DOOM .wad are no longer left dangling in memory.
  Kind: fix.
  Source: in-session-2026-06-12 (cppcheck audit + user request).

- ✅ [DOOM-0028] **Fix an off-by-one out-of-bounds read in the menu/finale font renderer.**
  cppcheck arrayIndexOutOfBoundsCond. hu_font[HU_FONTSIZE] has valid indices 0..HU_FONTSIZE-1 (HU_FONTSIZE = '_'-'!'+1 = 63), but the range guard read `if (c < 0 || c > HU_FONTSIZE)` — letting c == HU_FONTSIZE through to hu_font[63], one patch_t* past the array. Reachable: a backtick '`' maps via toupper to index 63. Changed the guard to `c >= HU_FONTSIZE` at all four sites (f_finale.c x3 in F_TextWrite / F_CastDrawer measure+draw, m_misc.c M_WriteText). Out-of-range chars are now skipped with the existing `cx += 4` spacing, identical to every other non-font char. Builds clean (linux/linuxxdoom links).
  **Layman:** A stray character in on-screen text could make the game read one slot past the end of the font table; now it's skipped cleanly like any other non-font character.
  Kind: audit-fix.
  Source: audit-2026-06-15 cppcheck.

- ✅ [DOOM-0029] **Fix undefined order-of-evaluation in sndserv's strupr.**
  cppcheck unknownEvaluationOrder. sndserv/wadread.c strupr() did `*s++ = toupper(*s)`, where the post-increment store target and the toupper read of *s are unsequenced side effects on the same pointer — undefined behaviour. Rewrote to the engine's own correct idiom (w_wad.c strupr): `while (*s) { *s = toupper(*s); s++; }`. Compiles clean.
  **Layman:** Tidied a tiny string-uppercasing routine in the standalone sound server that relied on undefined C behaviour, so it now works reliably on any compiler.
  Kind: audit-fix.
  Source: audit-2026-06-15 cppcheck.

- ✅ [DOOM-0030] **Fix the crash when a demo lump's version doesn't match the engine.**
  G_DoPlayDemo (g_game.c:1603-1607) rejects a version-mismatched demo with a stderr message + gameaction=ga_nothing + early return, but leaves no level/player initialised; the engine then ticks P_PlayerThink on a NULL player->mo (crash at p_user.c:245, via P_Ticker -> G_Ticker -> D_DoomLoop). Repro: `-timedemo demo1` on doom.wad whose demo lumps are a different VERSION byte -> SIGSEGV. Found 2026-06-16 while smoke-testing the DOOM-0008 raster path (unrelated to that change; pre-existing). Investigate why the tick loop runs a player thinker after the demo aborts and make a version mismatch fail gracefully (skip the demo / fall back to the title screen), not crash.
  **Layman:** Playing back a demo recorded by a different DOOM version (e.g. -timedemo on a WAD whose demos don't match this engine) currently crashes the game instead of just skipping the demo; this should fail gracefully.
  Kind: fix.
  Source: in-session-2026-06-16.
  Resolved (2026-06-17): G_DoPlayDemo's version-mismatch abort now mirrors G_CheckDemoStatus's teardown — releases the lump, I_Quit on an explicit -playdemo/-timedemo, else sets GS_DEMOSCREEN + D_AdvanceDemo back to the title screen, so the same-tick G_Ticker dispatch never runs P_Ticker/P_PlayerThink on a NULL player->mo. Commit 22e149b.

- ✅ [DOOM-0031] **Free the save buffer when a savegame's version doesn't match the engine.**
  G_DoLoadGame (g_game.c:1222) reads the whole save file into a Z_Malloc(PU_STATIC) buffer via M_ReadFile, then bailed with a bare `return; // bad version` on a version-string mismatch, leaking savebuffer on every failed load. The normal completion path frees it with Z_Free(savebuffer); the mismatch path now does the same. Same bad-input-cleanup class as the demo-version crash (DOOM-0030) and the IdentifyVersion path leak (DOOM-0025). Found 2026-06-17 during the audit sweep that accompanied DOOM-0030.
  Resolved (2026-06-17): freed savebuffer before the early return; compiles clean. Commit 9e87bcd.
  **Layman:** Loading a save file made by a different DOOM version used to quietly waste a chunk of memory every attempt; now it cleans up after itself.
  Kind: fix.
  Source: in-session-2026-06-17 (cppcheck sibling of DOOM-0030).

- ✅ [DOOM-0032] **Guard unchecked heap allocations against out-of-memory null-deref.**
  Eight raw malloc sites used their result without a NULL check, so an out-of-memory condition segfaulted instead of failing cleanly. Added if(!p) I_Error guards matching the engine's own idiom (already used in r_mesh.c): i_system.c I_ZoneBase (the zone-heap base) and I_AllocLow; i_net.c doomcom; m_misc.c M_LoadDefaults string default; d_main.c D_AddFile + FindResponseFile (file buffer and rebuilt argv); sndserv/wadread.c lumpinfo (uses derror). Already-guarded reallocs (w_wad.c, mus2mid.c, r_mesh.c) left as-is.
  **Layman:** If the game can't get memory it now exits with a clear message instead of crashing.
  Kind: fix.
  Source: audit-2026-06-17 cppcheck nullPointerOutOfMemory.

- ✅ [DOOM-0033] **Pass a literal format string to printf for the dev/CD-ROM banners.**
  d_main.c:903/907 called printf(D_DEVSTR) and printf(D_CDROM), passing a macro as the format string. The macros are constant today (no % conversions) so this was not exploitable, but it is a format-string anti-pattern (-Wformat-security). Changed to printf("%s", D_DEVSTR/D_CDROM).
  **Layman:** Tidies up two startup messages so they print safely.
  Kind: fix.
  Source: audit-2026-06-17 grep format-string.

- ✅ [DOOM-0034] **Replace obsolete alloca() calls in r_data.c and w_wad.c.**
  cppcheck flags alloca() at r_data.c:326,455,768,790,825 and w_wad.c:199,256. alloca can overflow the stack on a large request. Replace with a C99 VLA or a checked heap allocation where the size is unbounded.
  **Layman:** Swap a risky old memory trick for a safer modern one.
  Kind: refactor.
  Source: audit-2026-06-17 cppcheck allocaCalled.
  Resolved (2026-06-17): replaced all 7 alloca() sites. Bounded 1-byte arrays (patchcount, flatpresent, texturepresent, spritepresent) became C99 VLAs (auto-freed on return, incl. R_GenerateLookup's early return). The untrusted multiplied sizes (patchlookup = nummappatches*4 from PNAMES; fileinfo = numlumps*16 in W_AddFile/W_Reload) became checked malloc()+I_Error-on-null+free, so a hostile lump count now fails gracefully instead of smashing the stack. Swapped <alloca.h> for <stdlib.h> in r_data.c, dropped it from w_wad.c. cppcheck allocaCalled now clears; engine builds clean.

- ✅ [DOOM-0035] **Fix signed/unsigned printf/scanf format-specifier mismatches in the serial/IPX drivers.**
  sersrc/DOOMNET.C:107 (%lu vs signed long), sersrc/PORT.C:83 (%x vs signed int* in scanf), ipx/DOOMNET.C:59 (%lu vs signed long). Legacy DOS multiplayer drivers; low priority as they are not part of the SDL2 build path.
  **Layman:** Correct some number-formatting mismatches in the old modem/LAN code.
  Kind: fix.
  Source: audit-2026-06-17 cppcheck invalidPrintfArgType.
  Resolved (2026-06-17): flatadr long -> unsigned long in sersrc/DOOMNET.C and ipx/DOOMNET.C (matches the %lu sprintf; the value is a flattened seg:off address, always >= 0). uart int -> unsigned int in sersrc/PORT.C (matches the 0x%x sscanf at :83 and the latent 0x%x printf at :89, same root cause). cppcheck invalidPrintfArgType/invalidScanfArgType clear. DOS-only drivers, not in the SDL2 build path; verified via cppcheck.

- ✅ [DOOM-0036] **Fix the sndserv standalone build (soundsrv.c missing <string.h>).**
  sndserv/soundsrv.c uses strlen/strcmp without including <string.h>, so its standalone Makefile fails (implicit-declaration error under modern gcc). Pre-existing breakage, unrelated to the alloc-guard bundle; the standalone sndserv is legacy (superseded by SDL2 audio, DOOM-0004). Add the missing include.
  **Layman:** Make the optional standalone sound server compile again.
  Kind: fix.
  Source: audit-2026-06-17 build-check.
  Resolved (2026-06-17): added #include <string.h> to sndserv/soundsrv.c (uses strlen at :325-337 and strcmp at :348). soundsrv.c now compiles clean under modern gcc with no implicit-declaration error.

- 📋 [DOOM-0038] **Add game controller (gamepad) support.**
  Wire SDL2's GameController API (SDL_GameController* / SDL_CONTROLLERAXISMOTION
  + SDL_CONTROLLERBUTTONDOWN events, already available since the DOOM-0004 SDL2
  port) into the DOOM input path so a gamepad can drive movement, turning, fire,
  use and menu navigation. Analog sticks map to move/strafe and turn; buttons map
  to the existing DOOM actions with a sensible default layout. Consider an analog
  look/turn sensitivity and an optional in-menu rebind later.
  **Layman:** Let people play with a game controller, not just keyboard and mouse.
  Kind: feature.
  Source: user-request-2026-06-17.

- ✅ [DOOM-0039] **Launch in fullscreen by default, with -windowed to opt out.**
  i_video.c: both the Classic and Vulkan window-creation paths now default to SDL_WINDOW_FULLSCREEN_DESKTOP via a shared I_WantFullscreen() helper; -windowed/-w opts out, -fullscreen/-f still accepted. Single helper so the two paths can't drift. Verified: Linux + Windows builds compile and link clean.
  **Layman:** The game now opens fullscreen straight away; pass -windowed if you'd rather have a window.
  Kind: ux.
  Source: user-request-2026-06-17.

- ✅ [DOOM-0040] **Print clear WAD-placement guidance when no IWAD is found.**
  d_main.c IdentifyVersion: when no IWAD is located, print the searched folder and tell the user to drop doom1.wad/doom.wad/doom2.wad there or pass -iwad. Helps the single-file Linux AppImage (no companion readme). Paired packaging change: the AppImage apprun hook sets DOOMWADDIR to the .AppImage's own directory, so a WAD dropped beside it is found regardless of launch CWD. Verified: AppImage run from a different CWD reports the AppImage's own folder as the search dir.
  **Layman:** If no game data file is found, the game now tells you exactly where to put a WAD instead of a cryptic error.
  Kind: enhancement.
  Source: user-request-2026-06-17.

## Phase 2 — The Spin

The creative overhaul: evolve the renderer toward true 3D with hardware
ray/path tracing and modern lighting, holding 60 FPS, while keeping the original
DOOM feel. DOOM-0008 (the foundation) is now in design/build (🚧); the rest are
parked ideas (💭 considered) until we commit to and design each one.

- 🚧 [DOOM-0008] **Convert the renderer to true 3D.**
  **Layman:** Replace DOOM's fake-3D trick with a real 3D engine.
  Kind: feature.
  Source: in-session-2026-06-11.
  Design: docs/specs/DOOM-0008-3d-renderer.md (`/cold-eyes` reviewed 2026-06-16) — a
  real-time Vulkan **path tracer** (global illumination + ray-traced shadows) on
  DOOM's original art, behind the DOOM-0026 seam, with auto-detected tiers
  (RT-capable → full path tracing; non-RT → raster-3D; no Vulkan → Classic). Built
  in stages: **Stage 1 = DOOM-0008** (3D meshes, materials, acceleration
  structure, sprites, UI compositing, selectable "Renderer: 3D"); **Stage 2 =
  DOOM-0009** (the Monte-Carlo integrator + muzzle-flash dynamic shadows); **Stage
  3 = DOOM-0010/0011/0012** (full dynamic lights, volumetrics, and the performance
  work to the 60 FPS floor). Shading curves authored/validated in the Vestige
  Formula Workbench. ADR: docs/decisions/0001-renderer-language-and-api.md.
  Progress (2026-06-16): Stage 1 raster primary visibility up. The level mesh is now GPU-uploaded (vertex buffer) and drawn in a depth-buffered render pass from the player's camera (view-projection from viewx/viewy/viewz/viewangle, 90deg horizontal FOV), with a bring-up shader (sector light + Lambert; neutral grey until materials). Added the GLSL->SPIR-V->embedded-header build rule (glslc + xxd) to the Makefile, an rb_view_t POD camera across the seam, and a graphics pipeline (cull-none, depth-test). Smoke-tested on doom.wad E1M1 via -warp: RT3D tier on the RX 6600, 1201 tris/3603 verts uploaded, 3D geometry renders (screenshot-confirmed), no Vulkan/validation errors over a multi-second run. Next: materials/textures (palette albedo), then sprites + UI composite, then the path tracer (DOOM-0009). NB: validation layer not installed on the dev box this run, so INV-8 unexercised here.
- 💭 [DOOM-0009] **Add hardware path tracing (Monte-Carlo GI + ray-traced shadows).**
  **Layman:** Use the graphics card to trace real light rays for accurate lighting, bounced light, and shadows.
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

- ✅ [DOOM-0026] **Keep the classic 2.5D renderer selectable alongside the 3D renderer.**
  Design constraint on the Phase 2 true-3D / ray-traced renderer (DOOM-0008..0012): it must sit ALONGSIDE the original software renderer, not replace it. Add a runtime renderer switch exposed in the main menu (e.g. a 'Renderer: Classic / 3D' option), persisted in the config. Implies abstracting the render path behind a small back-end interface so both can be selected without a rebuild. Default to Classic for exact parity. This shapes how DOOM-0008 is architected, so it is recorded now rather than retrofitted later.
  **Layman:** When the new 3D renderer arrives, you'll still be able to switch back to the original DOOM look from the main menu - both renderers ship in the same build.
  Kind: feature.
  Source: user-request-2026-06-12.
  Design: docs/specs/DOOM-0026-renderer-backend.md (function-pointer back-end seam at the world/UI boundary; Classic + future Vulkan path-traced 3D; auto-detected tiers). Decisions in docs/decisions/0001-renderer-language-and-api.md. Implementation of the Classic seam follows this session.
  Resolved (2026-06-15): the renderer back-end seam shipped — r_backend.h/.c with a function-pointer interface at the world/UI boundary, the Classic back-end wrapping the existing software renderer unchanged, RB_Init auto-detect/clamp, D_Display routed through RB_RenderPlayerView/RB_Present, a "renderer" config default, and a "Renderer:" options-menu item (3D shown unavailable until DOOM-0008). Classic output byte-identical; builds clean; smoke-tested on doom1.wad (E1M1 renders via the seam, unavailable-mode config falls back to Classic). The 3D back-end (DOOM-0008..0012) remains future work.

- ✅ [DOOM-0027] **Raise the classic renderer's internal resolution to 640x400.**
  Design: docs/specs/DOOM-0027-hires.md (6 /cold-eyes loops). User signed off a **compile-time fixed 2x** over the runtime-variable approach this entry originally sketched: a two-coordinate-space split (logical ORIGWIDTH/ORIGHEIGHT 320x200 vs physical SCREENWIDTH/SCREENHEIGHT 640x400) with a per-buffer-width scaler in v_video.c, so UI code keeps its logical coordinates. Builds clean across the ~12 touched files (doomdef, v_video, st_stuff, i_video, m_menu, wi_stuff, hu_lib, f_finale, am_map, r_draw, d_main). Runtime play-test pending (needs a WAD).
  **Layman:** Render the original-style view at higher internal detail (640x400) so it looks sharp instead of blocky when the window is enlarged - the look stays classic DOOM, just crisper.
  Kind: enhancement.
  Source: user-request-2026-06-12.

- ✅ [DOOM-0037] **Initialise VulkanState::viewProj in the 3D renderer back-end.**
  cppcheck: r_vulkan.cpp:210 member VulkanState::viewProj has no initializer. Part of the in-progress DOOM-0008 renderer. Give it a default (identity) initialiser so a frame drawn before the first camera update is well-defined.
  **Layman:** Make sure a camera matrix in the new 3D renderer always starts with a known value.
  Kind: fix.
  Source: audit-2026-06-17 cppcheck uninitMemberVarNoCtor.
  Resolved (2026-06-17): gave VulkanState::viewProj a default identity matrix ({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}) at r_vulkan.cpp:210 so a frame drawn before the first RB_Vulkan_RenderView camera update is well-defined. Verified: g++ -std=c++17 builds r_vulkan.o and links linuxxdoom cleanly; cppcheck uninitMemberVarNoCtor no longer reported.
