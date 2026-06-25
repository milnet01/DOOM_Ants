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

- ✅ [DOOM-0038] **Add game controller (gamepad) support.**
  Wire SDL2's GameController API (SDL_GameController* / SDL_CONTROLLERAXISMOTION
  + SDL_CONTROLLERBUTTONDOWN events, already available since the DOOM-0004 SDL2
  port) into the DOOM input path so a gamepad can drive movement, turning, fire,
  use and menu navigation. Analog sticks map to move/strafe and turn; buttons map
  to the existing DOOM actions with a sensible default layout. Consider an analog
  look/turn sensitivity and an optional in-menu rebind later.
  **Layman:** Let people play with a game controller, not just keyboard and mouse.
  Kind: feature.
  Source: user-request-2026-06-17.
  Resolved (2026-06-20): Wired SDL2's GameController API into i_video.c. The classic ev_joystick path is reused for the four action buttons + turn + forward + menu navigation (including the menu's auto-repeat throttle), so the engine's own G_Responder/M_Responder dual game/menu routing is reused unchanged. The two things the two-axis ev_joystick contract can't carry -- an independent strafe axis and a menu-open button -- are synthesised as the engine's existing strafe keys (key_straferight/left) and KEY_ESCAPE, so no engine-file changes were needed; the whole feature lives in i_video.c. Default layout: left stick move/strafe, right stick turn, d-pad move + menu-nav, A/RT fire, B strafe-mod (menu back), X/Y use, LB/RB/LT run, Start/Back menu. Hot-plug via SDL_CONTROLLERDEVICEADDED/REMOVED. Builds clean (gcc -Wall, no warnings) and runs (E1M1 startup smoke-tested, no regression). Runtime gamepad-input test pending a physical controller + interactive session; in-menu rebinding and analog sensitivity deferred to future work.

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
  Progress (2026-06-20): Materials slice -- per-surface palette albedo. r_mesh.c now computes each surface's average DOOM colour (flats: raw 64x64 palette indices; walls: an R_GetColumn column grid) through PLAYPAL and carries it as a new per-vertex rb_vertex_t.{r,g,b} attribute. The Vulkan pipeline binds it at location 3 (attrs[4]) and the bring-up fragment shader uses it in place of the neutral-grey stand-in; the fixed-direction Lambert term is still placeholder (INV-7 exemption noted in mesh.frag). Verified on doom.wad E1M1, RT3D tier on the RX 6600: 1701 triangles built with albedo, no crash / no Vulkan error over a multi-second run, and a screenshot confirms real per-surface colours (tan STARTAN walls, grey ceilings, blue floor) instead of uniform grey. Next: per-texel texture sampling (image atlas + descriptor sets), then sprites + UI composite, then the path tracer (DOOM-0009).
  Progress (2026-06-24): Per-texel texture sampling. Every wall texture + flat is packed as raw 8-bit palette indices into one R8 atlas (r_mesh.c RB_BuildAtlas), addressed by a unified id (walls, then flats); the per-surface average-albedo stand-in is removed and vertices now carry texel UV + texture id + flags. r_vulkan.cpp uploads the atlas + a 256x1 PLAYPAL LUT as device-local sampled images (staging copy + layout transitions) and a std430 storage buffer of per-id rects, bound via a new descriptor set; the mesh fragment shader looks up each surface's rect, tiles the UV within it (fract wrap, nearest, no inter-tile bleed), reads the index, and decodes through PLAYPAL. Also fixed an sRGB double-encode (present through a B8G8R8A8_UNORM swapchain, not _SRGB) that washed the world out and lifted the dark background to a glowing grey. Verified on doom.wad E1M1, RT3D tier (RX 6600): atlas 2048x2176 (287 walls + 111 flats), 1701 tris, walls/flats textured at correct DOOM brightness, no Vulkan errors over a multi-second run (screenshot-confirmed). NB: the atlas is a raster-pass choice; for the path tracer's hit shaders a bindless array-of-textures is the better fit (migrate at DOOM-0009 -- see docs/research/3d-renderer-approaches.md). Next: sprites (things + weapon) + UI composite, then sky, then the path tracer (DOOM-0009). Sky and sprites are why parts of the scene are still empty/dark.
  Progress (2026-06-24): world sprites now render as 3D billboards. Every sprite lump is packed into the paletted atlas (posted patches decoded to R8, gaps = palette index 0 = transparent); the rect buffer gained numFlat so the shader addresses walls|flats|sprites, and sprite surfaces alpha-test-discard index 0. RB_BuildSprites walks the sector thing lists each frame and emits camera-facing billboard quads (cylindrical: right = camera right, up = world +z), sized from the sprite patch, with the 8-way rotation/flip picked as R_ProjectSprite does; they upload to a persistent per-frame vertex buffer and draw after the level mesh through the same pipeline (depth + alpha test resolve occlusion, no sprite sort). Verified on E1M1 (RT3D tier): atlas 2048x4504 (287 walls + 111 flats + 764 sprites); barrels/decor upright, scaled, depth-occluded, clean edges; no Vulkan errors. Next: player weapon screen overlay, then sky, then the path tracer (DOOM-0009). Known follow-ups: no frustum/visibility cull yet (all things billboarded each frame; depth buffer hides the off-screen/occluded ones); index-0 transparency also drops genuine black sprite texels (standard GPU-DOOM tradeoff); two-sided masked mid-walls still opaque.
  Progress (2026-06-24): Player weapon overlay. RB_BuildPSprites
  (r_mesh.c) emits the player's psprites (weapon + muzzle flash) as a
  screen-space quad: R_DrawPSprite's HUD placement (psp->sx/sy bob +
  sprite offsets) in DOOM's 320x200 space, mapped to Vulkan NDC over the
  whole frame, z=0 so the depth test (LESS) keeps it on top of the world.
  Verts share the per-frame sprite buffer + draw call, flagged
  RB_MESH_PSPRITE|RB_MESH_SPRITE; mesh.vert skips the view-projection for
  PSPRITE verts (already NDC), the sprite path's paletted atlas + index-0
  alpha test give the cut-out. Verified on MAP01: pistol + fist render
  bottom-centre, alpha-tested, sector-lit, occluding the world; clean
  build, no Vulkan validation errors. Follow-ups: weapon stretches to the
  full 16:9 frame (no 4:3/aspect correction yet, same as the world view);
  invuln fixedcolormap + invisibility shadow not applied (sector light +
  FF_FULLBRIGHT only). Next: sky rendering for sky-flat surfaces.
  Progress (2026-06-24): muzzle-flash extralight wired into the 3D view.
  Classic DOOM brightens the whole screen for a few tics while a gun fires
  (A_Light1/2 set player->extralight); the Vulkan mesh path ignored it, so
  firing added no flicker. rb_view_t gained a [0,1] extralight term, set on
  the C side from player->extralight (one light-segment = 1<<LIGHTSEGSHIFT =
  16 of the 0..255 units, matching the software renderer); it rides the push
  constant beside the MVP and mesh.vert adds it to every surface's shade,
  clamped (fullbright stays 1). Walls, floors, monsters, and the weapon
  flicker brighter together -- the original feel, not the real localized
  flash light (that is DOOM-0009 Stage 2's muzzle-flash dynamic shadows).
  Verified end-to-end on E1M1 (RT3D tier, RX 6600) by temporarily forcing
  the term: the whole lit scene lifts uniformly while the dark void/clear
  stays dark; clean build, no Vulkan validation errors. Next: sky rendering.
  Progress (2026-06-24): sky rendering for the 3D view. Sky-flat
  ceilings were skipped in the mesh, leaving the dark clear colour
  showing through; now a full-screen backdrop draws behind the world
  (a second depth-off pipeline) and the fragment shader maps each pixel
  to a sky texel by view yaw + screen x (cylindrical, 90 deg of view per
  texture width, atan for the perspective FOV) and screen y (128px sky
  over the top half, horizon at screen centre). Fullbright, so the muzzle
  flash never brightens it; column sign matches DOOM's viewangle +
  xtoviewangle so it pans the correct way on turning. Sky texnum rides
  across the seam in rb_view_t (RB_BuildSky), DOOM globals stay C-side.
  Verified on DOOM II MAP01 (RT3D, RX 6600): sky shows through the
  entryway opening, occluded by walls/ceiling, no validation errors.
  Follow-up: vertical mapping is a top-half linear clamp (below-horizon
  smear is hidden by floors but would show through a rare F_SKY1 floor);
  refine to DOOM's skytexturemid placement if a sky-floor map needs it.
  Next: still on the DOOM-0008 raster bring-up (menu wiring of the
  Classic/Solid/Ultra modes is the user-facing piece).
  Progress (2026-06-24): renderer menu wiring. The three tiers are now
  player-facing as Classic / Solid / Ultra (RB_CLASSIC / RB_RASTER3D /
  RB_RT3D) and selectable live from the Options "Renderer:" item. The
  rendermode_t enum values stay frozen (config + r_vulkan tier lockstep),
  so the menu's ascending-fidelity cycle (Classic -> Solid -> Ultra) lives
  in a separate cycleOrder[] behind RB_NextAvailableMode(), skipping any
  tier this machine can't run. Closed the half-wired switch: switching INTO
  a Vulkan mode already recreated the SDL window as a Vulkan window, but
  nothing rebuilt the 2D software window on the way back; added
  I_ReinitGraphicsForClassic() (counterpart to I_ShutdownGraphicsForVulkan,
  2D window/renderer/texture creation factored into CreateSoftwareWindow),
  called from Classic_Init, a no-op on the normal Classic boot path.
  Verified on DOOM II MAP01 (RX 6600, RT3D): live-switched Ultra -> Classic
  mid-level via a temporary probe; the software view renders with the full
  HUD after the handoff, clean build, no Vulkan validation errors. Next:
  polish the 3D look (DOOM-0008 raster bring-up follow-ups).
  Progress (2026-06-24): 3D-look polish pass (several commits). (1) Distance
  light falloff in the world shader -- camera pos rides the push constant,
  the fragment dims far surfaces toward a floor so the view reads with
  depth instead of flat sector light (weapon psprite excluded). (2)
  Two-sided masked mid-walls (grates/fences) now render see-through:
  R_RenderTextureToAtlas composites wall patches into the atlas like
  R_GenerateComposite so masked gaps stay index 0, and the shader
  alpha-tests FLAG_MASKED. (3) Floor/ceiling black gaps closed: emit_cap
  now triangulates the convex hull of each subsector's seg endpoints
  (Andrew monotone chain), spanning the invisible BSP-partition edges that
  were showing the sky backdrop through the floor. (4) Sprites/weapon no
  longer punch see-through holes on genuinely-black texels: opaque index-0
  is remapped to the palette's darkest non-black index (index 0 stays the
  transparent key). (5) Weapon overlay keeps DOOM's 4:3 proportions on
  widescreen (x scaled by (4:3)/aspect about screen centre). Weapon
  horizontal placement kept DOOM-authentic per user (sprite centred, aim
  point dead-centre; pistol barrel sits a hair left by the original art).
  All verified on DOOM II MAP01/MAP02 (RX 6600, RT3D), clean builds, no
  Vulkan validation errors. Next: UI compositing -- draw the 2D overlay
  (status bar, messages, menu) over the 3D frame; it is currently skipped,
  so the HUD and the menu are invisible in 3D.
  Progress (2026-06-24): 2D HUD/menu compositor. The 3D back-ends render
  the world through Vulkan, so the engine's paletted screens[0] -- where
  the status bar, messages, menu, and intermission/finale draw -- was never
  shown (HUD invisible; the menu unreachable, so you couldn't even switch
  back to Classic from inside 3D). Now composited as a full-screen layer
  over the rendered scene: a vertexless pass (overlay.vert/.frag) samples
  screens[0] through the same PLAYPAL LUT the world uses and discards the
  transparent key. The 3D view's footprint in screens[0] is cleared to that
  key each frame (RB_OVERLAY_KEY = palette index 251, pure magenta, unused
  by any HUD/menu/font art, verified against the lumps), so the scene shows
  through there while every 2D element painted on top composites over the
  world. A persistently-mapped staging buffer streams the overlay into a
  device-local R8 image each frame (upload recorded before the render pass,
  draw issued last, depth off; descriptor binding 3). Verified on DOOM II
  MAP01 (RX 6600, Ultra) at screen sizes 9/10/11: status bar + brick border
  composite correctly, the view region keys out cleanly, no validation
  errors. (Title-screen-before-any-level case still shows the slate clear
  in 3D because the overlay reuses the level atlas's palette/descriptor --
  see found-issue below.)
  Progress (2026-06-24): floor/ceiling caps rebuilt from the BSP. The
  convex-hull-of-seg-endpoints cap (above) still under-covered subsectors
  whose true corners sit on BSP partition-line intersections (no seg
  there), and a numlines < 3 guard dropped every subsector with fewer than
  three segs outright -- both showing the sky backdrop through the floor (a
  tan gradient where a flat belongs). Replaced with the canonical carve:
  start from a map-sized quad and clip it by each ancestor partition line
  down the node tree (children[0] front/right, children[1] back/left, per
  R_PointOnSide), so every leaf gets its exact convex cell with no gaps.
  MAP01 1206 -> 1420 triangles; floor renders solid, no sky bleed-through
  (RX 6600, Ultra, no validation errors).
  Progress (2026-06-24): weapon drawn in the view window. RB_BuildPSprites
  mapped the weapon's 200-tall HUD canvas onto the whole frame, so the hand
  landed at the screen bottom where the composited status bar painted over
  it ("just the gun, no hand" at screen size <= 10). Now mapped onto the
  active view's vertical span [viewwindowy, viewwindowy+viewheight] so the
  weapon anchors to the view bottom and clears the status bar, as the classic
  renderer does; full-screen (size 11) is unchanged. With this, screen size
  10 (full-width view, status bar shown, no brick border) gives HUD + full
  gun/hand + solid floor -- user-confirmed "perfect". The horizontal 4:3
  mapping is untouched.
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

- ✅ [DOOM-0041] **Fix DOOM-0027 hi-res scaling regressions: small view window and mispositioned weapon sprite.**
  First runtime play-test of DOOM-0027 (its roadmap note flagged the play-test as pending) surfaced two regressions, both from 1997-era 320x200 constants left unscaled when the view buffer became 640x400. (1) R_ExecuteSetViewSize computed scaledviewwidth=setblocks*32 and viewheight=setblocks*168/10 in logical units, so the default screen size produced a 320x168 view marooned in the 640x400 buffer (a small bordered square) -- fixed by scaling both by HIRES. (2) pspritescale divided by the physical SCREENWIDTH(640) while the weapon's sx/BASEYCENTER are authored in 320x200 logical space, halving the scale to FRACUNIT so the gun shrank and its anchor sat mid-view -- fixed by dividing by ORIGWIDTH(320). Both in r_main.c. Verified by before/after in-game screenshots on doom.wad E1M1 (Classic renderer, fullscreen): the view now fills the screen and the pistol sits correctly at the view bottom. Not from DOOM-0038/0008 (those don't touch the Classic path).
  **Layman:** In the new sharper (640x400) mode the 3D view was a small square in the middle of the screen and the gun drifted up to the centre; both now sit and scale correctly.
  Kind: fix.
  Source: user-report-2026-06-20.

- 💭 [DOOM-0042] **Add a second, high-fidelity art set (DOOM 3 / sci-fi-horror look) selectable alongside the classic art.**
  Two art options for the 3D renderer: (1) DOOM's original art converted to 3D (the DOOM-0008 path, already in progress); (2) a replacement HD art set inspired by DOOM 3 and sci-fi horror generally. LEVEL LAYOUT IS UNCHANGED — same map geometry/segs/sectors and UVs; only the textures, flats, sprites and their materials are swapped. Implies a material-source abstraction (a 'theme' the renderer selects) layered on the bindless material pipeline, plus real PBR maps (albedo/normal/roughness/metallic/emissive) for the HD set rather than flat paletted albedo. LICENSING CONSTRAINT: id Software's DOOM 3 assets are proprietary and CANNOT be shipped in this GPL-v2 repo. Sourcing options to decide with the user: freely/CC0-licensed HD texture+sprite packs, community packs with compatible licences, AI-generated art, or an optional separately-distributed asset pack the user supplies locally. Open decision: which sourcing route. Depends on the bindless material seam and the path tracer (DOOM-0009).
  **Layman:** A second look for the 3D game: a modern, scary, DOOM-3-style art set you can switch to — same levels, new graphics.
  Kind: feature.
  Source: user-request-2026-06-24.
  Decision (2026-06-24): art-sourcing route = curate free / CC0-licensed HD packs (GPL-v2-distributable). Implication: bulk surface textures are well-covered by CC0 PBR libraries (e.g. ambientCG, Poly Haven), but DOOM-specific sprite/monster coverage under a free licence is patchy — expect to fill gaps (AI-generated or hand-authored originals) and to wrangle disparate packs into one coherent sci-fi-horror look. Curate against DOOM's texture names/sizes via a materials sidecar (see the Q2RTX materials.csv pattern in docs/research/3d-renderer-approaches.md).

- 📋 [DOOM-0043] **Place scene lights and an ambient floor so path-traced rooms are never unintentionally pitch black.**
  Once ray tracing computes real light transport, sectors with no bright surfaces go near-black. Build on the DOOM-0008 spec's derived emission (sector lightlevel glow, known bright/lamp/computer textures as emitters, sky sun) and add deliberate light placement where it makes sense (lamps, computer banks, exit signs, fire) plus a small ambient/sky floor so navigation stays playable. Distinct from DOOM-0010 (moving/coloured dynamic lights) and DOOM-0009 (the integrator itself) — this is the lighting-design/brightness pass that keeps the world readable. Pairs with the player flashlight (next item) for genuinely dark areas.
  **Layman:** Make sure rooms aren't pitch dark once real lighting is on — add lights where it makes sense and a gentle base glow.
  Kind: feature.
  Source: user-request-2026-06-24.

- 📋 [DOOM-0044] **Add a player flashlight toggled by a key, lighting the path-traced scene with ray-traced shadows.**
  A camera-mounted spotlight (headlamp) the player toggles with a configurable, config-persisted key. Fed to the path tracer as a dynamic analytic light so it casts real ray-traced shadows and bounces, sampled by next-event estimation. Follows view position/angle each frame (no BLAS change — a light parameter, not geometry). Builds on the DOOM-0009 integrator and the dynamic-light path (DOOM-0010 seed). Makes the dark sci-fi-horror areas (previous item) tense rather than unplayable.
  **Layman:** Give the player a flashlight they can switch on and off with a button.
  Kind: feature.
  Source: user-request-2026-06-24.

- 🚧 [DOOM-0045] **Composite the HUD/menu over the 3D view before a level is loaded (title screen).**
  The DOOM-0008 2D HUD/menu compositor only engages once a level is built, because it reuses the level texture atlas's PLAYPAL LUT + descriptor set (UploadAtlas). So at the title/demo screen in Solid/Ultra mode -- before any level -- the 2D overlay (title pic, main menu) is not composited and the view shows the slate clear, which means a cold launch straight into a 3D tier shows no menu until a demo/level loads. Decouple the palette LUT + overlay image/descriptor from the level atlas (build the PLAYPAL LUT at Vulkan init from the WAD-global palette) so the overlay composites from the first frame. Found 2026-06-24 while implementing the compositor; the in-level case (the user-facing goal: HUD + menu reachable during play, including switching back to Classic) works.
  **Layman:** In 3D mode the menu only appears once you're in a level; on the opening title screen it's currently invisible. Make it show there too.
  Kind: fix.
  Source: in-session-2026-06-24.
  Implemented 2026-06-25 (commit 9d826d1): palette LUT + descriptor set built at Vulkan init from WAD-global PLAYPAL, decoupled from the level atlas; overlay composites from the first frame. Builds clean. Pending on-screen verify that the title pic + main menu show in Ultra/Solid before a level loads.

- ✅ [DOOM-0046] **Add an optional on-screen FPS counter with selectable corner placement.**
  An Options toggle ("Show FPS: On/Off") plus a position setting ("FPS position: Top-Left / Top-Centre / Top-Right"), both config-persisted via m_misc.c's defaults[] table. Measure frame rate from I_GetTime/the frame loop (a rolling average over the last N frames reads steadier than an instantaneous tics delta) and draw the number with the small HUD font (HUlib / the STCFN* glyphs, as the messages widget does) into the 320x200 screens[0] each frame, anchored to the chosen top corner with a few-pixel margin. Drawing into screens[0] means it shows in every renderer for free: Classic blits it, and the DOOM-0008 3D compositor already composites that buffer over the 3D view (so no Vulkan-side work needed). Keep it out of screenshots/intermission only if it looks intrusive -- otherwise always-on when toggled. Ties into the Phase-2 "60 FPS floor" goal as the user-facing way to watch it.
  **Layman:** Add a setting to show a frames-per-second counter on screen, and let the player pick whether it sits in the top-left, top-middle, or top-right corner.
  Kind: feature.
  Source: user-request-2026-06-24.
  Resolved (2026-06-25, commit a5baf13, user-confirmed on RX 6600): Options "FPS:" item cycles Off/Top-Left/Top-Centre/Top-Right, persisted via the fps_corner config default; HU_DrawFPS draws a half-second rolling average with the small HUD font into screens[0] (shows under every renderer); new I_GetTimeMS wall-clock helper. User saw it on screen. It read ~35 because the renderer is currently present-locked to the 35Hz game tic -- the counter is correct; the tic-lock is tracked as DOOM-0048. Collapsed the roadmap's two settings to one 4-state cycle to avoid a menu overflow (same outcome)."

- 📋 [DOOM-0049] **Animate moving sectors (doors, lifts, floors, crushers) in the 3D view.**
  Found 2026-06-25 (user testing, Ultra). The 3D level mesh (r_mesh.c RB_BuildLevelMesh) is built once at level load with sector floor/ceiling heights baked into vertex z, and never updated; game logic (T_VerticalDoor, T_PlatRaise, T_MovePlane) still moves sectors, so doors/lifts open in gameplay but stay frozen on screen. Cheapest correct fix without regenerating geometry: tag each mesh vertex with its (sector index, plane=floor|ceiling|none), upload a per-frame per-sector height-delta buffer (height - initial), and offset z in mesh.vert by the selected plane's delta. Snapshot initial floor/ceiling heights at RB_Vulkan_BuildLevel. Walls between two sectors: top verts track one sector's ceiling, bottom verts the other's floor -- tag at emit time in emit_wall/emit_cap. Sprites/psprite/sky verts = plane none (no offset). Scrolling textures already animate (sidedef offset, unaffected).
  **Layman:** Doors and lifts don't visually move in 3D mode yet.
  Kind: fix.
  Source: in-session-2026-06-25.

- 🚧 [DOOM-0050] **Fix 2D HUD/menu overlay ghosting over the status bar in 3D modes.**
  Found 2026-06-25 (user testing, Sound Volume menu). In Solid/Ultra the compositor reads the whole paletted screens[0] each frame, but only the 3D view rectangle is cleared to RB_OVERLAY_KEY (r_backend.c). The status-bar region is drawn by ST_Drawer with refresh=false (st_stuff.c ST_diffDraw), which repaints only changed widgets, not the background -- so stale menu pixels drawn over the status bar last frame are never erased and composite as ghosting. Fix: force a full status-bar refresh (ST_doRefresh) each frame while in a 3D mode so the bar background repaints and erases stale overlay pixels; the border redraw already covers windowed sizes (borderdrawcount on menuactive). Watch also for any vertical displacement of the menu title (verify the overlay samples the full 320x200 logical screens[0] under DOOM-0027 hires).
  **Layman:** Menus smear over the bottom HUD bar in 3D mode.
  Kind: fix.
  Source: in-session-2026-06-25.
  Implemented 2026-06-25 (commit 7d19c64): force a full status-bar refresh each frame in 3D modes so stale overlay pixels are cleared. Builds clean. Pending on-screen re-test of the Sound Volume menu over the HUD.

- 📋 [DOOM-0047] **Verify sound-effect audibility vs music balance in the SDL2 build.**
  Found 2026-06-25 (user testing). SFX are fully wired (i_sound.c: addsfx -> I_MixSound -> SDL callback; s_sound.c S_StartSound -> I_StartSound; no stub), but the user can't tell they play. SFX are software-mixed at 11025 Hz; music plays via SDL2_mixer at 44100 Hz on a separate device, so music may dominate. Isolation test first: set Music Volume to 0 and confirm SFX are audible. If SFX are present but quiet, options: raise the default sfx_volume (m_misc.c, currently 8/15) or rebalance the mix; do not rewrite the mixer blind. Confirm by ear before changing audio.
  **Layman:** Hard to tell if gun/door/monster sounds are playing under the music.
  Kind: investigate.
  Source: in-session-2026-06-25.

- 📋 [DOOM-0048] **Decouple render rate from the 35 Hz game tic (currently present-locked at 35 FPS).**
  Found 2026-06-25 (FPS counter reads ~35). D_Display is presenting one frame per 35 Hz game tic, so the renderer is tic-locked and can't exceed 35 FPS regardless of GPU headroom. The FPS counter (DOOM-0046) is reporting the true present rate. To hit the Phase-2 60 FPS floor the present/interpolation must be decoupled from the fixed game tic (render interpolation between tics, uncapped or vsync present). Part of the 60-FPS-floor performance work (DOOM-0011/0012); recorded now as the concrete sub-task surfaced by testing.
  **Layman:** The game draws only 35 frames a second; the FPS counter correctly shows ~35.
  Kind: perf.
  Source: in-session-2026-06-25.
