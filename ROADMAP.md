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

- ✅ [DOOM-0056] **Let the controller Circle (B) button close an open menu.**
  User request 2026-06-25. The gamepad opened/closed the menu via Start/Options (KEY_ESCAPE toggle). Now the B button (PlayStation Circle, the common 'back' button) also acts as Escape WHILE a menu is open -- backing out a level / closing at the top -- matching the PS back convention. In play (no menu) B keeps its strafe/menu-back role (joyb bit1); the menu case is gated on menuactive so B can only close, never open. i_video.c I_PollGamepad. Builds clean; pending user verify."
  **Layman:** Circle on the controller now backs out / closes the menu, like Start does.
  Kind: enhancement.
  Source: user-request-2026-06-25.
  Resolved (2026-06-25, commit 83dd303, user-confirmed). Controller Circle (B) now backs out one menu level and closes only at the top: B->KEY_BACKSPACE while a menu is open, and KEY_BACKSPACE closes when the menu has no prevMenu. Start/Options still toggles. User confirms it works as a back-then-close button.

- 📋 [DOOM-0060] **Game-select boot menu: choose DOOM 1 or DOOM 2, switch back without relaunching by hand.**
  Approach (user-chosen): a relauncher, NOT an in-process IWAD swap — each game runs in a fresh engine boot, sidestepping the teardown of the WAD-global state (textures incl. the new bindless material array, sprites, zone memory). Requirements:
  - Auto-detect IWADs at startup. Exactly one present (DOOM 1 doom1.wad/doom.wad OR doom2.wad) -> skip the selector, boot straight into it. Both present -> show the Game Select screen.
  - Game Select screen styled with the same look and feel as the games' title screens.
  - In-game menu option 'Return to Game Select' that re-enters the chooser so the other game can be picked.
  Open design wrinkles for the spec: the selector needs art/palette/font but runs before an IWAD is chosen (load one to draw it, or peek both wads for each TITLEPIC?); the re-exec mechanism (exec the binary with -iwad vs in-process reset); how 'Return to Game Select' surfaces mid-game. Needs a spec + /cold-eyes before implementation (house rule 14).
  **Layman:** On startup, if both games are installed you get a menu to pick one; finish playing, return to that menu, and pick the other. If only one game is found it boots straight in.
  Kind: feature.
  Source: user-request-2026-06-25.

- ✅ [DOOM-0070] **Fix unbounded -record demo-name buffer overflow in G_RecordDemo.**
  Found 2026-06-26 (ants-audit static analysis, verified). G_RecordDemo (g_game.c:1547) did strcpy(demoname, name) + strcat(demoname, ".lmp") into demoname[32] (g_game.c:132); `name` is the raw -record command-line argument (d_main.c:1204), unbounded. A -record name longer than 27 chars overflows the global buffer (corrupts adjacent globals, potential control-flow hijack). Same class as DOOM-0024 (config-parser hardening). Resolved: replaced with snprintf(demoname, sizeof(demoname), "%s.lmp", name) — bounds the copy, always NUL-terminates, builds the whole name in one call. Built clean. The other 91 audit string-function findings are faithful-1997 copies of string literals / known-bounded buffers (false-positives); the SNDSERV popen "command injection" CRITICAL is dead code (not compiled in the SDL2 build).
  **Layman:** A long demo filename passed on the command line could overflow a fixed buffer and corrupt memory; now it's safely truncated.
  Kind: security.
  Source: ants-audit-2026-06-26.

## Phase 2 — The Spin

The creative overhaul: evolve the renderer toward true 3D with hardware
ray/path tracing and modern lighting, holding 60 FPS, while keeping the original
DOOM feel. DOOM-0008 (the foundation) is now in design/build (🚧); the rest are
parked ideas (💭 considered) until we commit to and design each one.

- ✅ [DOOM-0008] **Convert the renderer to true 3D.**
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
  Stage 1 shipped (2026-06-25, user-confirmed). The Vulkan 3D raster renderer is complete and playable: level meshes, per-texel paletted materials (atlas), world sprites + weapon, sky, muzzle-flash brighten, the Classic/Solid/Ultra menu, the 2D HUD/menu compositor, BSP floor caps, moving-sector animation (DOOM-0049), door-jamb geometry (DOOM-0052), and clean mid-game switching (DOOM-0051). Tested on DOOM 1 and DOOM 2 first levels. Stage 2 = the path tracer (DOOM-0009); the bindless-material migration and acceleration structure land there per docs/specs/DOOM-0009-path-tracer.md.
- 💭 [DOOM-0009] **Add hardware path tracing (Monte-Carlo GI + ray-traced shadows).**
  **Layman:** Use the graphics card to trace real light rays for accurate lighting, bounced light, and shadows.
  Kind: feature.
  Source: in-session-2026-06-11.
  Direction confirmed (2026-06-25, user): ray tracing is a SETTINGS TOGGLE (on/off), orthogonal to the art set -- it applies to BOTH the original paletted art and the HD art set (DOOM-0042). Maps onto the existing tiers as the Solid (RT off) <-> Ultra (RT on) distinction within the 3D renderer, surfaced as a clear "Ray Tracing: On/Off" option; the material/lighting pipeline stays art-set-agnostic so either art theme can be traced or not. Sequencing: tracer-first on the original art (proves the lighting), HD art (DOOM-0042) layered on its material pipeline later. User has a PBR asset library at '/mnt/Games/3D Engine Assets/' (HDRIs, PBR textures, glTF models) -- the Outdoor HDRIs are directly useful for the tracer's sky/environment lighting regardless of art set; PBR textures + models feed DOOM-0042. New sourced assets go in that folder keeping its categorisation, GPL-compatible (CC0/free) only. Design spec: docs/specs/DOOM-0009-path-tracer.md.
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

- ✅ [DOOM-0045] **Composite the HUD/menu over the 3D view before a level is loaded (title screen).**
  The DOOM-0008 2D HUD/menu compositor only engages once a level is built, because it reuses the level texture atlas's PLAYPAL LUT + descriptor set (UploadAtlas). So at the title/demo screen in Solid/Ultra mode -- before any level -- the 2D overlay (title pic, main menu) is not composited and the view shows the slate clear, which means a cold launch straight into a 3D tier shows no menu until a demo/level loads. Decouple the palette LUT + overlay image/descriptor from the level atlas (build the PLAYPAL LUT at Vulkan init from the WAD-global palette) so the overlay composites from the first frame. Found 2026-06-24 while implementing the compositor; the in-level case (the user-facing goal: HUD + menu reachable during play, including switching back to Classic) works.
  **Layman:** In 3D mode the menu only appears once you're in a level; on the opening title screen it's currently invisible. Make it show there too.
  Kind: fix.
  Source: in-session-2026-06-24.
  Implemented 2026-06-25 (commit 9d826d1): palette LUT + descriptor set built at Vulkan init from WAD-global PLAYPAL, decoupled from the level atlas; overlay composites from the first frame. Builds clean. Pending on-screen verify that the title pic + main menu show in Ultra/Solid before a level loads.
  Resolved (2026-06-25, commit 9d826d1, user-confirmed): booting straight into Ultra shows the full main menu (New Game / Load Game / episode / difficulty) composited over the empty 3D view -- the title/demo screen 2D overlay now renders from the first frame instead of a blank slate, because the PLAYPAL LUT + descriptor set are built at Vulkan init rather than only after a level's atlas. User confirmed the menu is visible at boot in 3D. (No renderer chooser at the title screen by design; the renderer is selected in Options in-game.)

- ✅ [DOOM-0046] **Add an optional on-screen FPS counter with selectable corner placement.**
  An Options toggle ("Show FPS: On/Off") plus a position setting ("FPS position: Top-Left / Top-Centre / Top-Right"), both config-persisted via m_misc.c's defaults[] table. Measure frame rate from I_GetTime/the frame loop (a rolling average over the last N frames reads steadier than an instantaneous tics delta) and draw the number with the small HUD font (HUlib / the STCFN* glyphs, as the messages widget does) into the 320x200 screens[0] each frame, anchored to the chosen top corner with a few-pixel margin. Drawing into screens[0] means it shows in every renderer for free: Classic blits it, and the DOOM-0008 3D compositor already composites that buffer over the 3D view (so no Vulkan-side work needed). Keep it out of screenshots/intermission only if it looks intrusive -- otherwise always-on when toggled. Ties into the Phase-2 "60 FPS floor" goal as the user-facing way to watch it.
  **Layman:** Add a setting to show a frames-per-second counter on screen, and let the player pick whether it sits in the top-left, top-middle, or top-right corner.
  Kind: feature.
  Source: user-request-2026-06-24.
  Resolved (2026-06-25, commit a5baf13, user-confirmed on RX 6600): Options "FPS:" item cycles Off/Top-Left/Top-Centre/Top-Right, persisted via the fps_corner config default; HU_DrawFPS draws a half-second rolling average with the small HUD font into screens[0] (shows under every renderer); new I_GetTimeMS wall-clock helper. User saw it on screen. It read ~35 because the renderer is currently present-locked to the 35Hz game tic -- the counter is correct; the tic-lock is tracked as DOOM-0048. Collapsed the roadmap's two settings to one 4-state cycle to avoid a menu overflow (same outcome)."
  Follow-up fix (2026-06-25, commit dfd8932): centre/right placement used SCREENWIDTH (physical hi-res = ORIGWIDTH*HIRES) but the HUD font draws in logical 320-wide space, so top-centre was cut off at the right edge and top-right ran off screen. Fixed to anchor on ORIGWIDTH. Pending re-test of the centre/right corners.

- ✅ [DOOM-0049] **Animate moving sectors (doors, lifts, floors, crushers) in the 3D view.**
  Found 2026-06-25 (user testing, Ultra). The 3D level mesh (r_mesh.c RB_BuildLevelMesh) is built once at level load with sector floor/ceiling heights baked into vertex z, and never updated; game logic (T_VerticalDoor, T_PlatRaise, T_MovePlane) still moves sectors, so doors/lifts open in gameplay but stay frozen on screen. Cheapest correct fix without regenerating geometry: tag each mesh vertex with its (sector index, plane=floor|ceiling|none), upload a per-frame per-sector height-delta buffer (height - initial), and offset z in mesh.vert by the selected plane's delta. Snapshot initial floor/ceiling heights at RB_Vulkan_BuildLevel. Walls between two sectors: top verts track one sector's ceiling, bottom verts the other's floor -- tag at emit time in emit_wall/emit_cap. Sprites/psprite/sky verts = plane none (no offset). Scrolling textures already animate (sidedef offset, unaffected).
  **Layman:** Doors and lifts don't visually move in 3D mode yet.
  Kind: fix.
  Source: in-session-2026-06-25.
  Implemented 2026-06-25 (commit 977c662): per-vertex (sector, plane) tags + RB_UpdateMeshHeights rewrites moving-plane z's into a kept-mapped vertex buffer each frame. User testing confirmed doors are functional (sound + walk-through) but frozen; this animates them. Builds clean. Pending on-screen verify. Known follow-ups: texture v not re-pegged (slight stretch on moving faces); zero-height-at-build walls (some lift sides) not emitted.
  Resolved (2026-06-25, user-confirmed). Doors/lifts animate in the 3D view via per-vertex (sector,plane) tags + RB_UpdateMeshHeights. User confirms doors animate correctly and no longer stretch/shrink.

- 🚧 [DOOM-0050] **Fix 2D HUD/menu overlay ghosting over the status bar in 3D modes.**
  Found 2026-06-25 (user testing, Sound Volume menu). In Solid/Ultra the compositor reads the whole paletted screens[0] each frame, but only the 3D view rectangle is cleared to RB_OVERLAY_KEY (r_backend.c). The status-bar region is drawn by ST_Drawer with refresh=false (st_stuff.c ST_diffDraw), which repaints only changed widgets, not the background -- so stale menu pixels drawn over the status bar last frame are never erased and composite as ghosting. Fix: force a full status-bar refresh (ST_doRefresh) each frame while in a 3D mode so the bar background repaints and erases stale overlay pixels; the border redraw already covers windowed sizes (borderdrawcount on menuactive). Watch also for any vertical displacement of the menu title (verify the overlay samples the full 320x200 logical screens[0] under DOOM-0027 hires).
  **Layman:** Menus smear over the bottom HUD bar in 3D mode.
  Kind: fix.
  Source: in-session-2026-06-25.
  Implemented 2026-06-25 (commit 7d19c64): force a full status-bar refresh each frame in 3D modes so stale overlay pixels are cleared. Builds clean. Pending on-screen re-test of the Sound Volume menu over the HUD.

- ✅ [DOOM-0047] **Verify sound-effect audibility vs music balance in the SDL2 build.**
  Found 2026-06-25 (user testing). SFX are fully wired (i_sound.c: addsfx -> I_MixSound -> SDL callback; s_sound.c S_StartSound -> I_StartSound; no stub), but the user can't tell they play. SFX are software-mixed at 11025 Hz; music plays via SDL2_mixer at 44100 Hz on a separate device, so music may dominate. Isolation test first: set Music Volume to 0 and confirm SFX are audible. If SFX are present but quiet, options: raise the default sfx_volume (m_misc.c, currently 8/15) or rebalance the mix; do not rewrite the mixer blind. Confirm by ear before changing audio.
  **Layman:** Hard to tell if gun/door/monster sounds are playing under the music.
  Kind: investigate.
  Source: in-session-2026-06-25.
  Resolved-pending-ear-check 2026-06-25 (commit 863447f): user confirmed SFX play but are drowned by music. Capped music scale at 80/128 (~63%) instead of full -- safer than boosting SFX (software mixer would clip). Tunable; awaiting the user's confirmation of the new balance.
  Resolved (2026-06-25, commit 863447f, user-confirmed): capped the music scale at 80/128 (~63%) so the louder separate-device music no longer drowns the 11025 Hz software SFX mixer. User reports "the sound effects and music are much better now." Tunable if needed."

- 📋 [DOOM-0048] **Decouple render rate from the 35 Hz game tic (currently present-locked at 35 FPS).**
  Found 2026-06-25 (FPS counter reads ~35). D_Display is presenting one frame per 35 Hz game tic, so the renderer is tic-locked and can't exceed 35 FPS regardless of GPU headroom. The FPS counter (DOOM-0046) is reporting the true present rate. To hit the Phase-2 60 FPS floor the present/interpolation must be decoupled from the fixed game tic (render interpolation between tics, uncapped or vsync present). Part of the 60-FPS-floor performance work (DOOM-0011/0012); recorded now as the concrete sub-task surfaced by testing.
  **Layman:** The game draws only 35 frames a second; the FPS counter correctly shows ~35.
  Kind: perf.
  Source: in-session-2026-06-25.

- ✅ [DOOM-0051] **Fix mid-game renderer switching (blank 3D world, Classic ghosting).**
  Found 2026-06-25 (user testing). Switching renderer mid-game via the Options menu broke: into Solid/Ultra the world was blank (HUD only), and into Classic the view showed magenta smears + leftover menu text. Root cause: RB_SetMode (r_backend.c) shut down + re-initialised the back-end but never rebuilt the level, so a Vulkan target had no mesh/atlas; and the shared paletted screens[0] carried the 3D compositor's RB_OVERLAY_KEY (magenta index 251) + stale menu pixels across the hand-off. Fix (commit pending): after Init, call active->BuildLevel() when a level is loaded (mirrors RB_Init's autostart path; Classic's BuildLevel is NULL so it no-ops); then memset screens[0]=0, set setsizeneeded, and ST_Start() to force a clean full view/border/HUD redraw. Pending on-screen verify of all six switch directions.
  **Layman:** Switching renderers during play broke the view; now rebuilt cleanly.
  Kind: fix.
  Source: in-session-2026-06-25.
  Resolved (2026-06-25, user-confirmed). Mid-game renderer switching works in all directions: into Solid/Ultra rebuilds the level (no blank view); into Classic no longer crashes (that was the DOOM-0055 hi-res visplane bug, now fixed) and the screen is wiped clean of the 3D key colour.

- ✅ [DOOM-0052] **Close black gaps in floor/wall geometry near doorways in the 3D view.**
  Found 2026-06-25 (user testing, screenshots). Black triangular gaps appear in the floor/walls around some doorways in Solid/Ultra -- void showing through where a cap or wall triangle is missing. Likely interacts with the DOOM-0049 moving-sector re-height (a moving plane exposes an edge that was zero-height / not emitted at build, per the known DOOM-0049 limitation) and/or a BSP floor-cap carve gap at door-sector boundaries. Investigate against r_mesh.c emit_wall/emit_subsector_caps and the DOOM-0049 zero-height-wall note; reproduce on a specific doorway. Distinct from the (accepted) texture stretch on moving faces.
  **Layman:** Some doorways show black wedges where floor/wall should be.
  Kind: fix.
  Source: in-session-2026-06-25.
  Candidate fix 2026-06-25 (commit 0b82c32): the door/lift jamb walls were dropped by emit_wall's zero-height guard (a closed door collapses its own walls to zero), so the doorway side was a void hole in 3D. Now emit zero-height walls (degenerate, drawn only once DOOM-0049 grows them). Pending on-screen verify that the door-side void is gone. Follow-up: jamb texture stretches when grown (zero-span V); the bottom-centre floor wedge near the camera is still open and may be a separate carve/projection issue."
  Resolved (2026-06-25, commit 0b82c32, user-confirmed earlier in Ultra). Zero-height door/lift jamb walls are now emitted so the doorway sides are no longer a void hole.

- ✅ [DOOM-0053] **Fix in-game menu text ghosting in the Classic renderer (hi-res).**
  Found 2026-06-25 (user testing, after an Ultra->Classic mid-game switch). Navigating the in-game Esc menu in Classic leaves previous pages' text un-erased -- main-menu and Options text accumulate over the view (SAVE GAME / READ THIS! / QUIT GAME / MOUSE SENSITIVITY / SOUND VOLUME all at once). Investigation (Explore agent): the in-game menu-erase relies on R_RenderPlayerView redrawing the view window each frame to overwrite menu pixels, plus R_DrawViewBorder for the border; R_DrawViewBorder early-returns at fullscreen (scaledviewwidth==SCREENWIDTH), so the view re-fill is the only eraser for the view region. Under DOOM-0027 hi-res, scaledviewwidth/viewwindow are physical units while the d_main.c:294 gate compares to the literal 320 -- verify the view actually re-renders and fully fills its window after a switch to Classic (viewactive / setsizeneeded / buffer re-init). Needs a careful, iteratively-verified fix (candidate: clear the view-window region of screens[0] before R_RenderPlayerView, or correct the fullscreen erase gate). Solid/Ultra switching is clean (DOOM-0051); Classic is the remaining mode.
  **Layman:** In Classic mode, menu pages smear over each other as you navigate.
  Kind: fix.
  Source: in-session-2026-06-25.
  Resolved (2026-06-25, commit 30090e7, user-confirmed): the in-level Esc menu in Classic no longer smears pages together. While a menu is open in a level in Classic, D_Display now drives the full view/border/background redraw a screen-size change triggers, plus a forced status-bar repaint, so each page lands on a clean frame; scoped to Classic+GS_LEVEL+menuactive. NB: a separate Classic hi-res floor/visplane smear during gameplay (no menu) is tracked as DOOM-0055 -- not this menu fix."
  Reopened (2026-06-25): the 30090e7 fix (force a full view-size/border redraw while a menu is open in Classic) CAUSED A CRASH -- it drove DOOM-0027's latent hi-res view-border + visplane path, which on a windowed screen size spams 'Patch ... exceeds LFB' and crashes (R_MapPlane: 321, 221 at 255) on switching to Classic. Reverted in the DOOM-0055 commit. The menu ghosting returns but is cosmetic; its real fix is the underlying hi-res software-renderer bug (DOOM-0055), not a forced redraw. Do NOT re-add a forced setsizeneeded."
  Resolved (2026-06-25, commit 148e121, user-confirmed). The view region was fixed by the DOOM-0055 visplane widening; the status-bar region (where 'SOUND VOLUME' lingered) is fixed by forcing a full HUD repaint while a menu is or was just open (menuactive || menuactivestate). HUD-only, no view-resize path. User confirms no menu text left on the HUD after closing.

- 💭 [DOOM-0054] **Integrate RetroAchievements (unlock achievements while playing).**
  User request 2026-06-25. Integrate RetroAchievements: hash the loaded IWAD/PWAD to identify the game, log in to the RA service, and unlock achievements by watching game state (players[], mobjs, level/secret/kill counters) against RA's trigger definitions; support hardcore mode + leaderboards. Use rcheevos -- RA's official client library (MIT, GPL-v2-compatible) -- for the protocol, hashing, and trigger evaluation, so we don't reimplement the network/achievement logic. Design points to settle: account/network handling and offline behaviour, the memory-inspection hooks into DOOM's game state, an opt-in toggle, and keeping it out of the core game loop's way. Sizable networked feature; likely Phase 3+. Captured now per request.
  **Layman:** Earn RetroAchievements in DOOM_Ants like an emulator does.
  Kind: feature.
  Source: user-request-2026-06-25.
  Route confirmed (2026-06-25): go the RetroAchievements path (rcheevos client + RA recognition + author the set on RA's site), chosen over a lighter self-contained in-game achievement system. Reminder of the real-work split: the per-achievement badge image (RA auto-dims it for the locked state), title, and description are authored in RA's web editor — not stored in this repo; the engine work is the rcheevos integration, and promotion is gated by the RA team recognising us as a client. Stays Phase 3+ behind DOOM-0009 (tracer first).

- ✅ [DOOM-0055] **Fix Classic hi-res floor/visplane smearing and a stray floating floor polygon.**
  Found 2026-06-25 (user testing, Classic gameplay, no menu open). The floor (visplanes) leaves smear/ghost stripes during movement and a stray floor polygon appears in the air. Almost certainly a DOOM-0027 hi-res software-renderer bug: the visplane fill or the screens[0] view fill doesn't cover every physical pixel at hi-res (integer y-scaling gaps), so stale pixels persist between frames -- likely the SAME root cause as the Classic menu ghosting (DOOM-0053), which the full-redraw workaround masked for menus only. Distinct from the 3D back-ends (they recomposite each frame). Investigate r_plane.c R_DrawPlanes / R_MakeSpans and the hi-res y-mapping; the proper fix is a complete per-pixel fill rather than a per-frame clear. Lower priority than the 3D-path bugs since Classic is the legacy renderer.
  **Layman:** In Classic the floor smears as you move and a stray bit of floor floats.
  Kind: fix.
  Source: in-session-2026-06-25.
  Crash root-caused 2026-06-25 (terminal log). Switching to Classic at a WINDOWED screen size (default screenblocks 9) drives the hi-res software renderer's view-border + visplane code and: (1) R_DrawViewBorder draws border patches at out-of-bounds logical coords (hundreds of 'Patch at -3,Y / 320,Y / X,-3 exceeds LFB' -> V_DrawPatch ignores them), and (2) R_MapPlane fatally I_Errors on a corrupted visplane span ('R_MapPlane: 321, 221 at 255' -- a stale spanstart[y] from a wider view width). RANGECHECK (doomdef.h:75) is ON, so it exits via I_Error rather than segfaulting -- looked like a silent close because not run from a terminal. So DOOM-0055 is really THREE coupled DOOM-0027 hi-res bugs: floor visplane under-fill (smear), R_DrawViewBorder out-of-bounds border coords, and the visplane spanstart corruption (crash). The forced-redraw in DOOM-0053/0051 exposed them; reverting removed the trigger. Real fix: correct the hi-res coordinate handling in r_draw.c R_DrawViewBorder and r_plane.c R_MakeSpans/spanstart, and the visplane bottom-row fill. Repro: Classic at screenblocks<10, move around. Fullscreen (size 11) likely avoids the border path."
  Resolved (2026-06-25, commit 67d4399 + clean rebuild, user-confirmed). Root cause: visplane_t.top/bottom were `byte` (max 255) but hold physical screen rows 0..399 at hi-res (the 1997 struct comment: "the rub for all dynamic resize/change of resolution"). Widened to unsigned int with a 0xffffffffu sentinel and UNSIGNED R_MakeSpans params (signed read the all-ones sentinel as -1 and inverted the span logic), per Crispy Doom's hi-res recipe (found via web research). Plus a sky-loop sentinel skip and the step-1 R_MapPlane guard. CRITICAL build note: the Makefile has no header dependency tracking, so the fix only took effect after `make clean` -- an incremental build linked mixed visplane_t layouts and kept the floor broken (see memory doom-ants-makefile-no-header-deps). User confirms floor fills cleanly; doors animate without stretch. The earlier crash (step 1) and the menu-text-on-floor are gone."

- 📋 [DOOM-0057] **Reconcile DOOM-0008's internally-split moving-sector AS wording.**
  Surfaced by the DOOM-0009 /cold-eyes loop. DOOM-0008 contradicts itself on moving-sector acceleration-structure updates: §Approach (~line 138) says doors/lifts "update TLAS instance transforms, not BLAS geometry", while §Geometry (~line 215) says "per-sector BLAS updates (refit, not rebuild)". DOOM-0009 §3 resolves the physics (rigid motion → instance transform; non-rigid wall-height change → BLAS refit) and supersedes the over-broad "no refit" claim. Action: align DOOM-0008's two passages to DOOM-0009 §3's resolution so the sibling specs stop disagreeing. Pure doc edit.
  **Layman:** Fix two sentences in an old design doc that disagree with each other about how moving doors are drawn in the ray-traced renderer.
  Kind: doc-fix.
  Source: cold-eyes DOOM-0009 2026-06-25.

- 📋 [DOOM-0058] **Replace the per-material manual sub-allocator with VMA.**
  DOOM-0009 build step 1 backs all N bindless material images with ONE device allocation, binding each at a manually-aligned offset (r_vulkan.cpp UploadAtlas). That keeps the allocation count at 1 (clear of the driver's per-allocation limit on large WADs) but is a minimal hand-rolled sub-allocator. Swap it for the Vulkan Memory Allocator (VMA) when the AS/buffer allocations of later build steps arrive, so all GPU memory goes through one battle-tested allocator. No behaviour change; robustness + less bespoke code.
  **Layman:** Tidy up how the game reserves video memory for textures so it scales to big mods.
  Kind: refactor.
  Source: in-session-2026-06-25 DOOM-0009 build step 1 increment 2.

- 📋 [DOOM-0059] **Gate the 3D render tiers on descriptor-indexing support at probe time.**
  The bindless materials path (DOOM-0009 build step 1) requires four Vulkan 1.2 descriptor-indexing features. They are checked in PickPhysicalAndDevice (device creation, after the user has already switched to Solid/Ultra), so a GPU lacking them now I_Errors at init. Effectively unreachable on real hardware (any Vulkan-1.2 driver has them), but the clean fix is to fold the check into RB_Vulkan_Available / the tier probe so an unsupported GPU never offers the 3D tiers and the menu silently stays on Classic — no abort.
  **Layman:** On a very old GPU, keep the menu on Classic instead of crashing when 3D is picked.
  Kind: enhancement.
  Source: in-session-2026-06-25 DOOM-0009 build step 1 increment 2.

- ✅ [DOOM-0061] **Port DOOM wall texture pegging into the 3D mesh (fixes vertically-misaligned uppers and switches).**
  Found 2026-06-25 (user testing, DOOM 1, Solid/Ultra): a wall switch (red-button panel) visible in Classic did not show in the 3D back-ends. Root cause: r_mesh.c emit_wall had NO texture pegging -- it top-aligned every wall (vtop = rowoffset only). DOOM picks the texture's vertical origin per wall kind and per the linedef ML_DONTPEGTOP/ML_DONTPEGBOTTOM flags (r_segs.c:448-605): default upper textures are bottom-pegged, and DONTPEGBOTTOM switch/step faces peg to the ceiling. Ignoring this slid those textures' art out of the visible band. Fix: emit_wall gains a pegkind (PEG_ONESIDED/UPPER/LOWER/MID) and computes vtop via the faithful r_segs rules using textureheight[] + linedef flags; the four call sites pass their kind. Default lower/one-sided/mid V origins are unchanged (only +rowoffset cases), so the only behaviour change is the previously-wrong uppers and DONTPEG faces now align. Built clean. Needs user GPU re-test (the switch should reappear and uppers should match Classic).
  **Layman:** In the 3D renderers a wall switch's button graphic was sliding out of view; now textures line up vertically like Classic.
  Kind: fix.
  Source: in-session-2026-06-25.

- ✅ [DOOM-0062] **Draw the upper wall above an outdoor doorway in the 3D mesh (sky-ceiling guard fix).**
  Found 2026-06-25 (user testing, DOOM 1 E1M1 outdoor area, Solid/Ultra): the wood lintel above the exit-building doorway was missing -- a see-through hole where Classic shows solid wall. Root cause: r_mesh.c RB_BuildLevelMesh guarded the upper-step emit with `front->ceilingpic != skyflatnum`, skipping the top texture whenever the FRONT sector ceiling is sky. Outdoors that is always true, so any doorway cut into an open-air wall lost its upper. DOOM only suppresses the upper when BOTH ceilings are sky (the outdoor height-change hack, r_segs.c:530). Fix: skip only when front AND back ceilings are skyflatnum; otherwise emit the top texture (emit_wall still early-returns on a "-"/absent toptexture, so genuine sky gaps still show through). Built clean. Needs user GPU re-test.
  **Layman:** In the 3D renderers the wall above DOOM's outdoor exit door was see-through; now the lintel renders like Classic.
  Kind: fix.
  Source: in-session-2026-06-25.

- ✅ [DOOM-0063] **Map the controller Triangle (Y) button to toggle the automap.**
  User request: a controller button to show/hide the automap (PS4 Triangle). i_video.c I_PollGamepad previously mapped both Square (X) and Triangle (Y) to "use" (bit3). Dropped Y from the use mask (X still opens doors) and posts the engine's automap key (KEY_TAB == AM_STARTKEY/AM_ENDKEY) as an edge via I_PostKeyEdge, so one press opens the map and the next closes it -- same open/close-toggle contract as Start->Escape. Built clean.
  **Layman:** Press Triangle on the PS4 pad to open the map; press it again to close it.
  Kind: feature.
  Source: user-request-2026-06-25.

- ✅ [DOOM-0064] **Automap renders blank/frozen in the 3D back-ends (Solid/Ultra).**
  Found 2026-06-25 (user testing, DOOM 1, Triangle toggles map via DOOM-0063). Symptom: in Solid/Ultra the automap is blank when opened; what Classic last drew lingers (stale) until you move, after which it shows nothing until you switch back to Classic, which redraws it correctly. Classic is always correct.
  
  Static analysis (this session) traced the WHOLE path and found it SHOULD work, which is the puzzle: (1) D_Display calls AM_Drawer() every frame whenever automapactive, in all render modes (d_main.c ~244); (2) AM_Drawer writes into fb = screens[0] at full hi-res (am_map.c: f_w=SCREENWIDTH, f_h=SCREENHEIGHT-HIRES*32, PUTDOT into screens[0]); (3) map background = BACKGROUND = BLACK = index 0, which is NOT the overlay key (RB_OVERLAY_KEY=251), so it is NOT keyed out; (4) when automapactive, RB_RenderPlayerView is skipped (d_main.c ~283) so the view-rect clear-to-key in r_backend.c does NOT run; (5) nothing between AM_Drawer and the overlay copy overwrites screens[0] in 3D (I_FinishUpdate/Classic-only; I_UpdateNoBlit empty); (6) Vulkan_Present re-uploads screens[0] and draws the overlay every frame with depth test+write OFF (skyDs, r_vulkan.cpp ~1143), full-screen, keying only 251. The HUD/status bar use this exact overlay path and update fine. So by every readable line the map should composite. Conclusion: the cause is a runtime detail not visible in static reading -- did NOT ship a speculative fix (cannot GPU-verify here).
  
  Next step / discriminating probe needed: with the map open in Solid, observe whether the view area shows (a) solid black, (b) the dark-slate clear colour, or (c) a frozen 3D scene -- each points at a different cause (a=map bg present but lines missing/scale, b=overlay not sampling the map region, c=overlay not covering / present ordering). Cheapest instrument: a one-frame printf in AM_Drawer (confirm it runs in 3D) + dump screens[0] center byte. Candidate fixes to evaluate after the probe: treat automap-active as a pure-2D present (skip the stale mesh draw); verify AM_Start's fb=screens[0] is the same buffer the Vulkan overlay copies. Web research (Chocolate Doom blits the whole software buffer; GZDoom draws 2D via shaders over 3D) confirmed our keyed-overlay approach is a legitimate architecture, no documented match for this exact bug.
  **Layman:** The in-game map is blank in the 3D renderers and only updates in Classic; needs a runtime probe to finish diagnosing.
  Kind: fix.
  Source: in-session-2026-06-25.
  Root-caused and fixed 2026-06-25. The discriminator: the player arrow drew but walls did not -- so AM_Drawer + the overlay both work; the issue was that AM_drawWalls only draws linedefs flagged ML_MAPPED (am_map.c:1128), and that flag is set in exactly one place: the SOFTWARE seg renderer's R_StoreWallRange (r_segs.c:398). The 3D back-ends render the Vulkan mesh and never run it, so no wall was ever marked seen (only the always-drawn player showed; a Classic visit revealed walls because the flag persists on the linedef; new areas explored in 3D stayed blank). Fix: a faithful mark-only software BSP pass. Added rb_mapmarkonly (r_main.c) + R_MarkAutomapLines(player) which runs R_SetupFrame + R_ClearClipSegs/DrawSegs + R_RenderBSPNode; R_StoreWallRange marks ML_MAPPED then returns before drawing (ds_p not yet advanced -> no leak), R_Subsector skips the visplane/sprite setup in mark mode. Called from Vulkan_RenderPlayerView each play frame (skipped while the map is open, matching Classic). Same frustum + solid-seg occlusion as a real frame, so it reveals exactly what the player sees, no pixels drawn. Built clean. Needs user GPU re-test (open map in Solid/Ultra; walls should now reveal as you explore, like Classic). Perf note: full BSP visibility walk per play frame -- cheap (no draws), but could throttle later if needed.

- ✅ [DOOM-0065] **Stray distant floor/ceiling planes in the 3D outdoor view.**
  Found 2026-06-25 (user testing, DOOM 1 E1M1 courtyard, Solid/Ultra): a dark horizontal plane appears in the distance across the sky gap, absent in Classic. emit_subsector_caps already skips sky floors AND sky ceilings (r_mesh.c:275/278), so it is not raw sky caps. Leading hypothesis: carve_caps builds each subsector's floor/ceiling cell by clipping a map-sized quad (B=32768) with ANCESTOR BSP PARTITION lines only (r_mesh.c:287-303), not by the subsector's own segs. For a leaf not fully bounded by partitions (or bounded partly by non-partition one-sided walls), the carved convex cell can extend well past the real room toward the +/-32768 edge, emitting an oversized flat that pokes into the distance/sky. The in-code comment claims partitions alone reproduce the exact outline; that holds for fully partition-bounded leaves but not all. Robust fix (as GZDoom/Eternity do): after the partition carve, additionally clip each leaf cell by its own segs (keep the front/right half-plane of each seg) so the cell is bounded to the true subsector. Pre-existing since DOOM-0008; surfaced now during outdoor scrutiny. NOTE: regression-risky (touches all floor/ceiling rendering) -- needs a debug build that dumps which triangles fall in the suspect region to confirm before changing the carve. Also re-check whether DOOM-0062's new outdoor upper walls contribute (rule out vs the cap hypothesis).
  **Layman:** In the 3D renderers a dark slab appears across the distance outdoors where Classic shows open sky.
  Kind: fix.
  Source: in-session-2026-06-25.
  Visual confirmed 2026-06-25 (user-highlighted screenshot): two THIN horizontal slabs seen nearly edge-on, floating in the mid-distance across the sky gap between buildings, at roughly perimeter-wall-top height. Edge-on + thin + floating = floor/ceiling CAPS (flats) of sector(s) beyond the courtyard wall whose carved cell reaches into the gap, not vertical walls -- so this is the carve-cell hypothesis (cells under-bounded by partition-only clipping), not DOOM-0062's uppers. Confirms the fix direction: clip each leaf cell by its own segs after the partition carve.
  Resolved (2026-06-25): emit_subsector_caps now trims each carved floor/ceiling cell by its one-sided segs' front half-planes. Root cause: the partition-only BSP carve (clip a map-box by ancestor partitions) over-shoots past one-sided walls into the void because a seg is not a partition, so the cell balloons to the 32768 map edge; indoors it hides behind the wall, but outdoors the stray cap floated in the distance. Clipping by one-sided segs only (two-sided shared edges left untouched to avoid inter-cap cracks) removes the overshoot without cutting valid interior, since the sector sits on the front of its own walls. Awaiting user GPU re-test of the E1M1 outdoor view.
  Follow-up (2026-06-26): the one-sided-only clip was an under-fix -- a cap can also overshoot past a TWO-SIDED wall and float in front of the sky (user E1M1 Ultra shot: grey plane where Classic shows mountains). Extended emit_subsector_caps to clip the carved cell by EVERY seg's front half-plane, not just one-sided ones. Justified by Classic never overshooting (visplane spans are clipped to each seg's screen extent), and safe because every seg is an edge of the convex BSP leaf so the sector lies wholly on its front; two-sided neighbours clip to the same shared line from both sides so caps still meet exactly. Awaiting user GPU re-test.

- ✅ [DOOM-0066] **Reflect runtime wall/flat texture changes in the 3D mesh (switches, animated textures).**
  Found 2026-06-25 (user testing, Solid/Ultra): pressing a switch does not change its face from SW1 (off) to SW2 (on) as it does in Classic. Root cause: RB_BuildLevelMesh bakes each surface's texture id (texnum) into the vertex once at level load; the static mesh is never rebuilt. Two runtime mechanisms change a surface's picture and neither is reflected in 3D: (1) SWITCHES -- P_ChangeSwitchTexture swaps sides[].toptexture/midtexture/bottomtexture directly; (2) ANIMATED textures/flats (SLADRIP, blinking computer panels, flowing slime/lava) -- the SW renderer indexes texturetranslation[]/flattranslation[] which cycle each tic, while the sidedef/sector pic stays the base. The switch ACTION still fires (door/lift triggers -- gameplay is render-independent); only the visual is stale. Fix direction: a per-frame material-id patch mirroring DOOM-0049's RB_UpdateMeshHeights -- tag wall verts with their sidedef index (and flat verts already carry vsector) so texnum can be re-derived each frame from the current sidedef texture / the active animation frame (apply texturetranslation/flattranslation), updating the host-coherent vertex buffer in place. Scope to confirm: vertex format gains a sidedef index; the bindless material array (DOOM-0009) already has one image per material so the id swap is cheap. Verify the switch's action (door opens) still works in 3D -- expected yes.
  **Layman:** In the 3D renderers a pressed switch doesn't light up and animated surfaces (screens, slime) don't animate, because the 3D world bakes each surface's picture in once.
  Kind: feature.
  Source: in-session-2026-06-25.
  Resolved (2026-06-25): RB_UpdateMeshHeights now re-derives each vertex's live texture id every frame — texturetranslation[sides[vtexside].slot] for walls, flattranslation[sector pic] for flats — mirroring exactly what the SW renderer samples. Wall verts gained vtexside/vtexslot tags; flats reuse vsector/vplane. Switches (SW1->SW2) and animated walls/flats now show in Solid/Ultra.

- ✅ [DOOM-0067] **Stop door/lift textures stretching and squashing as they move in the 3D mesh.**
  Found 2026-06-25 (user testing, Solid/Ultra): doors stretched their texture as they opened and squashed it as they closed; lifts the same. Root cause: RB_UpdateMeshHeights (r_mesh.c, the DOOM-0049 per-frame moving-sector updater) rewrote only each moving vertex's z (world height) and never its v (texture row). The build-time wall mapping holds v + z = const along the quad (1 texel per world unit), so when z moved but v stayed, the fixed texture span was scaled across the changed quad height -> stretch when growing, squash when shrinking. DOOM instead keeps the texture fixed in world space and lets the moving edge slide over it. Pre-existing since DOOM-0049; DOOM-0061's correct pegging made it obvious. Fix: in RB_UpdateMeshHeights, after setting the new z, shift the vertex's v by (build_z - new_z) for WALL verts so v + z stays constant (texture stays pegged). Flats (RB_MESH_FLAT) project on world XY, so their v must not follow z -- guarded out. Always recomputed from the immutable build-time v/z baseline (no drift). Built clean. Needs user GPU re-test (open/close a door and ride a lift in Solid/Ultra).
  **Layman:** In the 3D renderers a moving door/lift stretched its texture open and squashed it closed; now the texture stays put as the surface slides, like Classic.
  Kind: fix.
  Source: in-session-2026-06-25.
  Correction (same session, 2026-06-25): the first fix (shift v so v+z stays constant) was WRONG -- it pegged the texture to FIXED WORLD SPACE, so the door appeared to consume/reveal a stationary texture (user: 'disappearing and reappearing') instead of carrying the texture with the leaf. DOOM pegs a wall's texture to ONE edge; on a door/lift that edge moves and the texture slides with it. Reimplemented: rb_vertex_t gains vtexsec/vtexplane/vtexoff (the sector plane the texture is anchored to + texel offset to row 0), set per peg-mode in emit_wall (top-pegged -> top edge; bottom-pegged -> bottom edge; DONTPEGBOTTOM lower -> front ceiling). RB_UpdateMeshHeights now re-derives v = (anchor_height + vtexoff) - z each frame, so the moving anchor edge keeps a constant v (texture pinned to it) while the far edge tracks at 1:1 -- the door's texture slides up into the ceiling opening and back down, no stretch. Static walls recompute to their exact build v (no parity regression). r_mesh.h struct changed -> make clean && make (done). Needs user GPU re-test.

- ✅ [DOOM-0068] **Build flush lift/step shaft walls in the 3D mesh so they appear when the lift travels.**
  Found 2026-06-25 (user testing, Ultra, riding a lift): a shaft wall that Classic draws was missing in 3D, leaving a see-through gap. Root cause: RB_BuildLevelMesh's lower-step gate emitted a wall only when front->floorheight < back->floorheight (strict). A lift built FLUSH with its neighbour (floors equal) failed that test, so the shaft wall was never created; when the lift travelled and the gap opened there was no wall to grow (the static mesh decides which walls exist once, at load; Classic re-derives per frame). Distinct from DOOM-0052 (which kept already-emitted walls from vanishing at zero height) -- here the wall was never emitted. Fix: change the gate to <= so a flush line emits its lower wall as a zero-height quad that the DOOM-0049 height update grows as the lift moves. emit_wall drops the untextured (\"-\") side, so only the textured shaft face -- which by construction grows valid and never inverts -- is added; non-moving flush lines emit an invisible zero-height quad (harmless). NOTE: backface culling is off (VK_CULL_MODE_NONE), so the rare line textured on BOTH sides whose floors cross could show a phantom inverted quad; not observed, acceptable edge case, revisit if reported. Upper gate left strict (lifts move floors; doors build closed so already emit). Built clean. Needs user GPU re-test.
  **Layman:** In the 3D renderers a lift shaft wall was missing (you could see through it) until the lift moved; now it's there like Classic.
  Kind: fix.
  Source: in-session-2026-06-25.

- ✅ [DOOM-0069] **Over-dark ceilings/overhangs in the 3D back-ends (black band across the top in Solid/Ultra).**
  Found 2026-06-26 (user testing, DOOM 1 E1M1 courtyard overhang, Solid/Ultra). Symptom: the ceiling/overhang occupying the top of the view renders as a near-pure-black band; Classic renders the same surface dark-grey but textured/visible. Initially mistaken for DOOM-0065 cap overshoot, then for a sky-coverage gap — both ruled out:
  (1) Cap geometry is correct: a Python replication of carve_caps + the all-segs clip on E1M1's prebuilt NODES/SSECTORS/SEGS gives max 8-vertex cells, zero POLYMAX(64) overflows, zero caps with extent >4000u — the caps are tight, not overshooting.
  (2) Not the sky: the sky is a full-screen NDC quad drawn first with depth test+write OFF (r_vulkan.cpp:1124-1125, 1827-1831); the world overdraws it. mesh.frag's sky path fills every screen pixel with a sky texel (no black branch), so black at the top must be world geometry overdrawing the sky, i.e. the ceiling cap itself.
  Leading root cause: mesh.frag applies a NON-DOOM directional key-light term — L=(0.3,0.4,0.85), diff=max(dot(n,L),0), shade=vLight*distLight*(0.55+0.45*diff) (mesh.frag:113-114,124). Ceiling caps face straight down (normal (0,0,-1)) so diff=0 -> shade drops to 0.55x; floors (normal +z) sit at ~0.93x. That ~40% ceiling-only darkening, compounded by the distance term (distLight clamps to 0.35) and low sector light, pushes dim ceilings toward black. Classic uses flat sector light + colormap distance only — no normal-based shading. Fix direction: make the raster lighting DOOM-faithful (drop the directional diff term; keep sector light + distance diminishing) so ceilings match floors/walls at the same sector light. Caveat: the fullbright-ceiling probe (force ceiling vLight=1.0) reportedly showed no change, but that test is suspect — the user ran the stale June-17 AppImage via the desktop icon at least once; re-verify on linux/linuxxdoom before/after the lighting change. Touches all 3D shading -> regression-check floors/walls/sprites stay correct.
  **Layman:** In the 3D renderers the ceiling/overhang above an outdoor view reads as a solid black band where Classic shows it dark-grey but clearly textured.
  Kind: fix.
  Source: in-session-2026-06-26.
  Resolved (2026-06-26): dropped the non-canonical Lambert key light from mesh.frag. Web research (id r_segs.c, DoomWiki "Fake contrast", GZDoom gl_maplightmode, ryanthomson.net colormap writeup) confirmed classic DOOM shades every surface by sector light + distance ONLY — no normal/directional term — so a floor and ceiling at equal sector light and distance are equally bright. The (0.55 + 0.45*dot(N,L)) term dropped down-facing ceiling caps to ~0.55x (floors sat ~0.93x), pushing dim ceilings to black — the courtyard-overhang black band. Now shade = vLight * distLight (distance diminishing kept; it is faithful). Removed the now-dead n/L/diff. The one legit orientation effect, wall fake-contrast (walls-only ±1 light level on axis-aligned segs), is left as an optional future per-vertex pass, not a per-fragment key light. Built clean (mesh.frag.spv regenerated). Awaiting user GPU re-test (E1M1 courtyard overhang should read dark-grey/textured like Classic, not black).

- ✅ [DOOM-0071] **Guard the Vulkan surface-format query against a zero count / dropped result.**
  Found 2026-06-26 (indie-review, r_vulkan.cpp lane, verified). CreateSwapchain called vkGetPhysicalDeviceSurfaceFormatsKHR twice WITHOUT checking either VkResult (the sibling caps query is Check-wrapped) and then dereferenced formats[0]. A driver reporting zero formats (spec-illegal but seen on broken/headless drivers) makes the std::vector empty and formats[0] undefined behaviour. Resolved: Check-wrap both queries and I_Error if fn==0 before indexing — matching the file's existing Check/I_Error idiom. Built clean.
  **Layman:** On an unusual graphics driver the renderer could read invalid memory while picking a display format at startup; now it checks properly and fails loudly instead.
  Kind: review-fix.
  Source: indie-review-2026-06-26.

- ✅ [DOOM-0072] **Clamp atlas tile width so a crafted-WAD wide texture can't overrun the atlas.**
  Found 2026-06-26 (indie-review, r_mesh.c lane, verified). The shelf packer (RB_BuildAtlas) assumes every tile fits the 2048-wide atlas: when x+w>ATLAS_WIDTH it resets x=0 but keeps w, so a tile wider than 2048 is placed at x=0 with w>2048 and blit_tile writes dst[(oy+row)*2048 + (ox+col)] with ox+col running past the row into following rows / past the buffer — a heap overflow driven by WAD data (malicious PWADs are a known DOOM attack surface). Stock textures are <=256 wide so it never fires normally. Resolved: clamp *w to ATLAS_WIDTH in tile_size (the single place the width flows from, used by rect/x-advance/blit consistently) — an over-wide tile is cropped, not overflowed. Built clean.
  **Layman:** A specially-crafted level file with an unusually wide texture could corrupt memory while building the 3D texture sheet; the width is now capped so it crops instead.
  Kind: security.
  Source: indie-review-2026-06-26.

- 📋 [DOOM-0073] **3D renderer defensive-hardening bundle (indie-review deferred items).**
  Deferred indie-review findings (2026-06-26) that are defensive-only (not reachable with the shipped/stock WADs) — bundled for one hardening pass. (1) clip_poly POLYMAX=64 silently truncates a carved cap to a malformed polygon -> emit_cap_poly fans garbage triangles; a Python replay proved stock E1M1 stays at <=8 verts, but a very complex custom sector could exceed 64 -> add a guard that skips/asserts rather than rendering wrong (r_mesh.c clip_poly/emit_subsector_caps). (2) RB_UpdateMeshHeights indexes flattranslation[pic]/texturetranslation[base] from WAD-loaded shorts every frame with no bound check -> a corrupt map is a repeated OOB read; add a load-time invariant or per-use clamp (r_mesh.c ~496/505). (3) RB_BuildSprites uses sprframe->lump[rot] then spritewidth[lump] with no lump>=0 check; R_InstallSpriteLump validates at load (same as vanilla) so unreachable, but a cheap guard documents the invariant (r_mesh.c ~786). (4) RB_Vulkan_SetOverlay assumes screens[0] never resizes mid-session (true today: V_Init allocates once) -> add a size-change guard so a future runtime-resolution change can't overrun the overlay staging buffer (r_vulkan.cpp ~1605/1731). (5) (fixed_t)(view->x*FRACUNIT) overflows int32 at the +/-32768 map edge -> wrong sprite-rotation angle for a frame; compute the angle without round-tripping through fixed (r_mesh.c ~753). (6) carve_caps recurses passing a ~520-byte poly_t by value (twice/level) -> pass const poly_t* to cut stack/copies and bound a degenerate-BSP stack blow (r_mesh.c ~344). (7) several Vulkan enumerate calls + vkCreateDebugUtilsMessengerEXT drop their VkResult -> Check-wrap the load-bearing ones (esp. vkGetSwapchainImagesKHR). All MEDIUM/LOW, none reachable in normal play.
  **Layman:** A set of small safety nets in the 3D renderer for unusual or corrupt level data — none affect normal play; grouped for a later hardening pass.
  Kind: refactor.
  Source: indie-review-2026-06-26.

- 📋 [DOOM-0074] **3D renderer has no CPU/GPU frame overlap (single frame in flight).**
  Found 2026-06-26 (indie-review, r_vulkan.cpp). The swapchain requests minImageCount+1 images (typically 3) and uses FIFO present, but there is only ONE command buffer + one inFlight fence + one image-available/render-finished semaphore pair, and every RB_Vulkan_Present blocks on that fence at the top. So the CPU always waits for the previous frame's full GPU completion before recording the next -- zero CPU/GPU overlap, throughput capped at GPU-bound latency. Correctness-safe (the fence serializes the persistently-mapped vertex buffer writes), but a real ceiling for the project's 60 FPS floor. Fix: an N-deep ring of {cmd, imageAvailable, renderFinished, fence} indexed by a frame counter, with per-frame vertex-buffer regions (or fence-gated reuse). Defer until perf work; note the mapped-buffer write must stay fence-guarded per frame-slot.
  **Layman:** The 3D renderer waits for the graphics card to fully finish each frame before starting the next, leaving performance on the table toward the 60 FPS goal.
  Kind: perf.
  Source: indie-review-2026-06-26.

- 📋 [DOOM-0075] **3D sky pans ~4x too fast and tiles 4x too often vs Classic.**
  Found 2026-06-26 (indie-review, mesh.frag). The sky fragment path maps the ~90deg horizontal FOV (atan(ndcX) over -1..1) to ONE full sky-texture width: col = ang/(PI*0.5)*sz.x. Classic DOOM maps a full 360deg of yaw to one texture repeat (ANGLETOSKYSHIFT: 90deg -> 1/4 texture). So our sky pans 4x faster and tiles 4x as often as the original. Falls under mesh.frag's explicit bring-up-shader constant exemption, hence deferred not silently changed. Fix: col = ang/(2.0*PI)*sz.x (360deg -> one texture width), matching DOOM's viewangletox sky shift. Verify against Classic by turning on the spot in E1M1.
  **Layman:** In the 3D renderers the sky/mountains scroll about four times too fast as you turn, compared to Classic DOOM.
  Kind: fix.
  Source: indie-review-2026-06-26.

- 🚧 [DOOM-0076] **Distant surfaces render black in the 3D back-ends where Classic shows them lit.**
  Found 2026-06-26 (user testing, E1M1 first courtyard, Solid/Ultra). Confirmed by a same-position Classic-vs-Ultra toggle: Classic renders the distant far-side structures (perimeter walls / building tops just below the sky) fully lit; Ultra shows a horizontal BLACK strip across that band. Distinct from DOOM-0069 (which fixed the near-overhang ceilings — the big black band is gone). The post-DOOM-0069 lighting is shade = vLight * distLight with distLight = clamp(1 - vDist/3000, 0.35, 1.0): the 0.35 floor means distance alone cannot drive a normally-lit surface to pure black (0.35*vLight*albedo > 0), so the cause is something else — candidates: (a) a specific surface getting vLight≈0 or a black/wrong texel (atlas/UV), (b) an upper-wall between the sky courtyard (ceilingpic F_SKY1, height 216) and a lower non-sky sector that is mis-lit, (c) the two sky-ceiling sectors at different heights (216 vs 24 in E1M1) interacting badly, or (d) a no-backface-cull back-face of a distant wall. Next step: a surface-type color-code debug shader (ceiling=red / floor=green / wall=blue, lighting bypassed) to ID the black surface in ONE screenshot, then fix the root cause. NOTE: pre-existing before DOOM-0069 (independent of the directional-light removal); only became the most-visible artifact once the near ceilings were fixed.
  **Layman:** In the 3D renderers, the far structures across an outdoor area go solid black near the top, while Classic shows them normally lit.
  Kind: fix.
  Source: in-session-2026-06-26.
  Diagnosis narrowed by two debug probes (2026-06-26), both now reverted. (1) Surface color-code (ceiling=red/floor=green/wall=blue, lighting bypassed): the black region is NOT red/green/blue -> it is NOT wall/floor/ceiling mesh geometry. This REFUTES the sky-hack-upper-wall hypothesis (and the research aimed at that layer). (2) Yellow 3D-clear: the black stays BLACK (does not turn yellow) and NO yellow appears anywhere -> the 3D colour buffer is fully covered by sky + geometry (no 3D hole), and the black is the 2D OVERLAY (screens[0]) drawn opaquely over the 3D. So root cause is COMPOSITING, not geometry/lighting/sky: a rectangular region of screens[0] inside the displayed view holds index-0 (black) and is NOT the transparent key (RB_OVERLAY_KEY=251), so overlay.frag composites it over the 3D instead of discarding. The 3D view footprint is keyed each frame by Vulkan_RenderPlayerView's clear-to-key loop (r_backend.c:127), which fills only viewwidth x viewheight at (viewwindowx,viewwindowy) in screens[0]; overlay.vert/frag map screens[0] 1:1 to the full framebuffer. Both the black region AND the sky opening show hard screen-axis-aligned rectangular edges (a keying artifact, not world geometry). Suspect: the clear-to-key rect does not cover the full displayed 3D view -- likely a HIRES coordinate-space mismatch in viewwindowx/viewwindowy/viewwidth/viewheight (R_ExecuteSetViewSize, r_main.c:682-722; note the existing "blocks==10 gave a 320x168 view marooned in 640x400" HIRES comment) -- leaving a sub-region that keeps the stale black from RB_SetMode's memset(screens[0],0). NEXT: dump runtime viewwidth/viewheight/viewwindowx/viewwindowy/setblocks vs SCREENWIDTH/SCREENHEIGHT, find the uncovered band, and either extend the clear-to-key to the full overlay-visible area or fix the view-rect coordinate space.
