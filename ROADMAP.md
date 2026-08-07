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

- ✅ [DOOM-0264] **Documentation standard: cite code by symbol, not by line number.**
  Adds a "Citing code from docs" section to docs/standards/documentation.md: the
  symbol name or a quoted line is the locator; a line number is at most an
  approximate hint carried alongside it; never edit at a raw line number; re-anchor
  bare numbers as you pass them. Evidence for the rule: DOOM-0011's spec carried
  ~50 file:line citations, two unrelated commits shifted r_vulkan.cpp by 4-6 lines
  and pathtrace.comp by 2, and all 50 needed re-checking - three had drifted onto
  unrelated code. The DOOM-0011 spec's own citation block now cites the standard
  rather than standing alone.
  **Layman:** Docs now point at code by name (a function or constant) instead of by line number, so they stop going wrong every time the code above them shifts.
  Kind: doc.
  Source: user-request-2026-07-26.

- ✅ [DOOM-0265] **Documentation standard requires plain, concise language.**
  Expanded `docs/standards/documentation.md` § Writing rules: say it once,
  cut ornament but never precision, one idea per sentence, a table cell
  holds a line not a paragraph, plus a read-aloud test. Adds a table
  mapping Karpathy's four principles (no silent assumptions, simplicity
  first, surgical changes, explicit verification) onto documents.
  Earned by DOOM-0011's cold-eyes loop 8, where the spec lane found zero
  correctness defects and six readability ones.
  **Layman:** Docs must be written simply and short — muddled sentences hide muddled ideas, so plain wording is how you find the mistakes.
  Kind: doc.
  Source: user-request-2026-07-26.

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

- ✅ [DOOM-0060] **Game-select boot menu: choose DOOM 1 or DOOM 2, switch back without relaunching by hand.**
  Approach (user-chosen): a relauncher, NOT an in-process IWAD swap — each game runs in a fresh engine boot, sidestepping the teardown of the WAD-global state (textures incl. the new bindless material array, sprites, zone memory). Requirements:
  - Auto-detect IWADs at startup. Exactly one present (DOOM 1 doom1.wad/doom.wad OR doom2.wad) -> skip the selector, boot straight into it. Both present -> show the Game Select screen.
  - Game Select screen styled with the same look and feel as the games' title screens.
  - In-game menu option 'Return to Game Select' that re-enters the chooser so the other game can be picked.
  Open design wrinkles for the spec: the selector needs art/palette/font but runs before an IWAD is chosen (load one to draw it, or peek both wads for each TITLEPIC?); the re-exec mechanism (exec the binary with -iwad vs in-process reset); how 'Return to Game Select' surfaces mid-game. Needs a spec + /cold-eyes before implementation (house rule 14).
  **Layman:** On startup, if both games are installed you get a menu to pick one; finish playing, return to that menu, and pick the other. If only one game is found it boots straight in.
  Kind: feature.
  Source: user-request-2026-06-25.
  Spec drafted 2026-07-03: docs/specs/DOOM-0060-game-select.md. Design: (1) D_DetectIwads() scans the existing IdentifyVersion candidate set for a DOOM1-family (doom/doomu/doom1) and DOOM2-family (doom2/doom2f/plutonia/tnt) path; (2) an in-process picker MENU (new GameSelectDef in m_menu.c, reuses all menu/gamepad/font machinery) opened just before D_StartTitle (d_main.c:1302) iff both present AND no explicit -iwad, and from Options -> Return to Game Select mid-game; (3) picking the loaded game continues instantly (no reload), picking the other relaunches exe -iwad <other>. Relaunch = M_SaveDefaults + explicit video/audio teardown (exec skips atexit) + resolve real exe path (/proc/self/exe | GetModuleFileNameA) + execv (POSIX) / _spawnv+exit (Windows). Config ~/.doomrc is shared across both games (d_main.c:699) so settings carry over. Two design decisions A1 DOOM-styled smart-relaunch picker (vs plain pre-WAD always-relaunch); A2 Return-to-Game-Select in Options next to End Game (vs top-level menu). [STATUS + MECHANISM of this 2026-07-03 draft note are SUPERSEDED by the 2026-07-04 update below -- decisions confirmed, spec cold-eyed; corrected: picker opens AFTER D_StartTitle (not before), POSIX relaunch needs no A/V teardown, DOOM1 preference is retail-first.] NB Feature-1 (quit to main menu) was found already-existing as Options->End Game (user agreed), so no separate item.
  Update 2026-07-04 (SUPERSEDES the 2026-07-03 note's status): both design decisions CONFIRMED by user ("go with your recommendations") -- A1 DOOM-styled smart-relaunch picker, A2 Return-to-Game-Select in Options. Spec has been taken through /cold-eyes to convergence (no CRITICAL/HIGH; see the spec's Cold-eyes loop log for the authoritative loop-by-loop record). Key mechanism points (authoritative over the 2026-07-03 sketch above): picker = M_OpenGameSelect() called AFTER the D_StartTitle() at d_main.c:1302 (draws over the title; inherits the ga_loadgame/autostart guard so -warp/-loadgame/net bypass it). POSIX relaunch = M_SaveDefaults then execv, NO manual A/V teardown (kernel reclaims window/GPU/audio on image replace); Windows = I_Quit-body-minus-exit teardown then _spawnv+exit (parent lingers). exec/spawn failure -> I_Error, never a silent exit(0). Confirm keys on usergame (game in progress). Explicit-choice skip = re-check -iwad/-shdev/-regdev/-comdev via M_CheckParm. Family precedence: DOOM1 doomu>doom>doom1 (retail-first, matches IdentifyVersion access() order); DOOM2 doom2>doom2f>plutonia>tnt. Pure D_IwadFamily() classifier gives the unit test a seam. Ready for implementation pending final user spec skim.
  Implemented 2026-07-04 (awaiting user play-test). New iwad_detect.h (pure D_IwadFamily classifier + IWAD_CANDIDATES preference list, unit-testable). d_main.c: D_DetectIwads (scans DOOMWADDIR, records DOOM1/DOOM2 rep paths + bothPresent), D_BothGamesPresent, D_IwadPathForFamily, D_RelaunchWithIwad (POSIX readlink /proc/self/exe + execv after M_SaveDefaults; Windows GetModuleFileNameA + I_QuitTeardown + _spawnv + exit; exec/spawn failure -> I_Error, never silent exit); boot hook after D_StartTitle (d_main.c) rides the ga_loadgame/autostart else-branch and skips when -iwad/-shdev/-regdev/-comdev present. i_system.c: I_Quit body factored into I_QuitTeardown (reused by the Windows relaunch). m_menu.c: GameSelectDef chooser (DOOM / DOOM II, text-drawn), M_OpenGameSelect, M_GameSelectChoose (same family -> M_ClearMenus continue; other -> confirm-if-usergame then relaunch), M_ReturnToGameSelect. A2 CHANGE: the "Return to Game Select" item went on the MAIN menu ("Game Select"), not Options -- Options is full (10 rows already reach screen-bottom; 11th would clip). Spliced into MainMenu after Quit via M_Init when D_BothGamesPresent (mirrors the commercial reshuffle idiom), drawn as text by M_DrawMainMenu; single-game installs see no new item. New tests/game_select_test.cpp: classification (paths/case/negatives/NULL) + preference order + bothPresent -- make test green. Full engine builds clean 0 warnings; headless boot verified on both the both-games picker path and the -iwad skip path (no crash). NOT play-tested: the visual picker render/nav + the REAL execv/relaunch into the other game (can't headless). TO CLOSE: user runs with both doom.wad+doom2.wad present -> picker appears -> pick other game relaunches; Options... no, main menu Game Select mid-game switches; then flip in-progress->shipped + changelog. Charl verifies Windows _spawnv relaunch. Commit pending.
  Added 2026-07-04 (user request): "remember last game." The engine records the loaded IWAD path in $HOME/.doom_ants_lastgame (D_WriteLastGame in D_DoomMain, after IdentifyVersion) and on a no-iwad boot defaults to it instead of the doom2-first auto-detect (D_ReadLastGame in IdentifyVersion, checked after -iwad/-shdev/-regdev/-comdev and before the doom2f auto-detect chain; a stored path that no longer exists self-heals to auto-detect). Shared gamemode inference factored into D_InferGamemode (reused by the -iwad path). Went with Option A: the boot chooser STILL shows, now pre-selecting the loaded=last game (M_OpenGameSelect sets GameSelectDef.lastOn) so one select press continues -- user was asked A vs B but away; A is conservative (keeps the approved startup chooser). One-line switch to Option B (skip chooser when remembered) if the user prefers. Verified headless: no-memory->doom2+remembers; memory=doom.wad->DOOM1; memory=doom2->DOOM2; missing-path->doom2 fallback + self-heal. Builds clean, tests green. ALSO fixed the desktop launcher (~/.local/share/applications/doom-ants.desktop): was hardcoding -iwad doom.wad (correctly skipped the chooser) AND KDE couldn't run the `env DOOMWADDIR=...` Exec form (icon did nothing) -> now Exec=/mnt/.../DOOM_Ants/run-doom-ants.sh wrapper (sets DOOMWADDIR, keeps cwd for savegames), kbuildsycoca6 refreshed. Engine boot on the real display (renderer=1 Vulkan RT) confirmed working (swapchain up, music ready) -- earlier "nothing happens" was purely the KDE launcher, not the engine.
  Shipped -- user-confirmed 2026-07-04: "the switcher works perfectly and it loads between each game very fast, no issue there." Both the startup chooser and the remember-last-game default work; relaunch between DOOM 1 and DOOM 2 is fast. Desktop-launcher wrapper (run-doom-ants.sh) in place.

- ✅ [DOOM-0070] **Fix unbounded -record demo-name buffer overflow in G_RecordDemo.**
  Found 2026-06-26 (ants-audit static analysis, verified). G_RecordDemo (g_game.c:1547) did strcpy(demoname, name) + strcat(demoname, ".lmp") into demoname[32] (g_game.c:132); `name` is the raw -record command-line argument (d_main.c:1204), unbounded. A -record name longer than 27 chars overflows the global buffer (corrupts adjacent globals, potential control-flow hijack). Same class as DOOM-0024 (config-parser hardening). Resolved: replaced with snprintf(demoname, sizeof(demoname), "%s.lmp", name) — bounds the copy, always NUL-terminates, builds the whole name in one call. Built clean. The other 91 audit string-function findings are faithful-1997 copies of string literals / known-bounded buffers (false-positives); the SNDSERV popen "command injection" CRITICAL is dead code (not compiled in the SDL2 build).
  **Layman:** A long demo filename passed on the command line could overflow a fixed buffer and corrupt memory; now it's safely truncated.
  Kind: security.
  Source: ants-audit-2026-06-26.

- 💭 [DOOM-0085] **Build a multiplayer system: restore networked co-op and deathmatch over a modern transport.**
  Original DOOM shipped co-op + deathmatch via a peer-to-peer tick-synchronised (lockstep) model in d_net.c, talking to the network through the DoomCom seam in i_net.c — originally backed by IPX LAN (ipx/) or serial/modem (sersrc/). Those transports are dead on modern 64-bit Linux and sit outside the SDL2 build path (cf. DOOM-0035). Goal: replace only the transport (the i_net.c / DoomCom I/O layer) with modern UDP/IP sockets, keeping d_net.c's game-sync logic intact where possible. Baseline scope: faithful 2-4 player co-op + deathmatch over LAN/internet.
  Open questions (settle in a spec when this leaves Considered): (a) topology — keep classic P2P lockstep, or move to client-server with input delay/prediction for internet latency; (b) player count beyond the original 4; (c) direct-connect + NAT traversal vs a discovery/matchmaking layer; (d) the simulation is render-tier-independent (INV-10), so lockstep determinism must hold identically in Classic/Solid/Ultra and per-frame AS/refit work must never enter the netgame tick. Needs a design spec + /cold-eyes before implementation (house rule 14). The legacy DOS drivers (ipx/, sersrc/) stay as historical reference, not a build target.
  **Layman:** Bring back DOOM's classic multiplayer — co-op and deathmatch — but over today's internet/LAN instead of the dead 1990s LAN-cable and modem links. Parked for now; we'll design it properly when we reach it.
  Kind: feature.
  Source: user-request-2026-06-28.

- 💭 [DOOM-0087] **Provide a map-authoring workflow / level editor for custom multiplayer WADs.**
  Motivation: the stock single-player maps aren't built for deathmatch/co-op, so DOOM-0085 (multiplayer) wants purpose-made MP maps — which needs a way to author custom WADs. Recommended approach (simpler path first): DOOM_Ants runs standard DOOM-format WADs, and the vanilla engine already supports multiplayer maps — deathmatch player starts (g_game.c deathmatchstarts) and the multiplayer-only Thing flag (p_mobj.c:743, options & 16 skipped outside a netgame). So DM/co-op maps are authorable today with a mature FOSS editor (Ultimate Doom Builder, SLADE, or Eureka) exporting a normal WAD this engine loads. Realistic scope: (a) verify the engine cleanly loads + plays third-party editor output (DM starts + MP thing flags); (b) document the authoring workflow; only (c) build bespoke in-engine tooling if MP needs map data the standard format/editors can't express. A from-scratch editor duplicates excellent existing tools — last resort (house rule: reuse before rewriting).
  Open: whether the 3D/RT tiers need map-side authoring data existing editors can't carry — e.g. RT light/emissive entities (cf. DOOM-0043/0084) or new linedef specials — which would be the one strong reason to build custom tooling. Pairs with DOOM-0085.
  **Layman:** To play multiplayer on maps designed for it (the originals weren't), we need a way to make our own maps. Excellent DOOM map editors already exist, so the likely plan is to use one and make sure our engine loads its maps — building a brand-new editor only if our 3D lighting needs something those tools can't do.
  Kind: feature.
  Source: user-request-2026-06-28.

- ✅ [DOOM-0097] **M_QuickLoad sprintf can overflow tempstring[80] (-Wformat-overflow).**
  gcc -Wformat-overflow flags m_menu.c:790 M_QuickLoad: sprintf(tempstring, QLPROMPT, savegamestrings[quickSaveSlot]) can write 60-83 bytes into tempstring[80] (QLPROMPT is the \"do you want to quickload...'%s'?\" prompt + a 24-char savegame name). Pre-existing original-DOOM code, surfaced while building DOOM-0096. Fix: bound it with snprintf(tempstring, sizeof(tempstring), ...). M_QuickSave (the symmetric quicksave prompt) likely shares the pattern; check both. Source: in-session-2026-06-28 (build warning).
  **Layman:** A harmless-looking quick-load confirmation message could, with a long savegame name, write past the end of a fixed text buffer — a latent crash/corruption risk flagged by the compiler.
  Kind: fix.
  Source: in-session-2026-06-28.
  Resolved (2026-06-28): root cause was tempstring[80] being too small for the longest quicksave/quickload prompt (~59 fixed chars + a 24-char savegame name = up to 83 bytes). Bumped the shared buffer to [128] and switched both M_QuickSave (QSPROMPT) and M_QuickLoad (QLPROMPT) from sprintf to snprintf(sizeof) for defense-in-depth. Clean build — both -Wformat-overflow and -Wformat-truncation gone.

- ✅ [DOOM-0158] **Announce a found secret with an on-screen message + a distinct sound (all renderers).**
  When the player steps into a secret sector, show a brief HUD message and play
  a distinct chime — in Classic, Solid AND Ultra (a HUD/gameplay layer, renderer
  -independent). Hook P_PlayerInSpecialSector, sector special 9 (p_spec.c:1048
  -1052: player->secretcount++; sector->special = 0) — add a player->message
  (the standard HU_ print path) and an S_StartSound there. Vanilla DOOM only
  revealed secrets in the end-of-level tally; this is a QoL addition matching
  modern ports / DOOM 2016's secret chime. Pick a sound that does not collide
  with an existing sfx (confirm the cue with the user); message text TBD (e.g.
  "A secret is revealed!"). Secret detection runs in the shared game tick so it
  fires in all tiers, but the HUD message must render OVER the 3D view — the 3D
  backends replicate SW-renderer side effects separately
  ([[3d-backends-bypass-sw-renderer-sideeffects]]) and overlay compositing is
  DOOM-0050-adjacent, so verify the popup shows in Solid/Ultra too.
  **Layman:** When you find a hidden secret area, a short message pops up on screen and a sound plays to let you know — right now you only find out from the end-of-level stats.
  Kind: feature.
  Source: user-request-2026-07-01.
  Progress 2026-07-01: implemented. p_spec.c case 9 (secret sector) now sets player->message = SECRETMESSAGE and calls S_StartSound(NULL, sfx_itmbk) after the secretcount++/special=0. New SECRETMESSAGE macro ("A secret is revealed!") in d_englsh.h + French parity ("UN SECRET EST REVELE!") in d_french.h; p_spec.c now includes dstrings.h. Sound sfx_itmbk (item-respawn whoosh) chosen with user — effectively no collision in normal solo play. Global sound (NULL origin) + standard HU message path, so it fires in the shared game tick across Classic/Solid/Ultra. Full build links clean. PENDING: in-game verify the popup renders over the 3D view in Solid AND Ultra (DOOM-0050-adjacent overlay compositing) and the chime plays — needs on-HW check with user.
  Progress 2026-07-02: user-confirmed the message + chime work in-game; then requested the text be yellow and centred (was default gray, top-left). Reworked the popup: it no longer rides plr->message (top-left gray widget). New HU_TriggerSecret()/HU_drawSecret() in hu_stuff.c draw SECRETMESSAGE centred (both axes, logical 320x200) in a gold-recoloured HUD font, gated by a secret_counter (HU_MSGTIMEOUT tics), decremented in HU_Ticker, cleared in HU_Start. DOOM has no yellow letter font, so HU_buildSecretGold() builds a 256-entry palette-translation table once from PLAYPAL via luminance-preserving nearest-colour match to gold (no hardcoded indices; mirrors R_InitTranslationTables intent). Added V_DrawPatchTranslated (v_video.c refactored to a shared V_DrawPatchGeneral helper; V_DrawPatch unchanged for all callers). p_spec.c now calls HU_TriggerSecret() (dropped the dstrings.h include, added hu_stuff.h). Draws into screens[0] like HU_DrawFPS so it composites in Classic/Solid/Ultra. Full build links clean. PENDING user in-game re-verify of the new look; offered to nudge position (dead-centre may be intrusive) or brighten the yellow.
  Resolved 2026-07-02 (user-confirmed in-game): stepping into a secret sector shows "A secret is revealed!" centred on screen in a bright pure-yellow HUD font plus the sfx_itmbk chime, in Classic/Solid/Ultra. Final tuning per user: dead-centre position (confirmed perfect) and pure-yellow recolour with a luminance lift (confirmed "perfect"). Implementation: HU_TriggerSecret/HU_drawSecret + HU_buildSecretGold (PLAYPAL nearest-colour recolour, no hardcoded indices) in hu_stuff.c; V_DrawPatchTranslated in v_video.c (shared V_DrawPatchGeneral helper, V_DrawPatch unchanged); p_spec.c case 9 calls HU_TriggerSecret(). Commits 74caf28, ce33b5f, d80e840.

- ✅ [DOOM-0165] **Title-screen music sometimes silent on the very first launch, plays on the next.**
  Found 2026-07-04 (user, Ultra renderer=1, launched via the desktop icon). On the FIRST launch after a rebuild the title-screen music did not play AND there was a ~1s black screen; on the immediate re-launch both were fine (music played, no black screen). NOT caused by DOOM-0060 (that launch used -iwad so the game-select code path never ran). Two likely-separate cold-start effects: (a) the ~1s black screen is Vulkan pipeline/shader compilation on the first run after `make clean` regenerated the .spv shaders -- the driver recompiles then caches, so it is one-time-per-rebuild and expected (candidate polish: show a "compiling shaders" splash instead of black -- would also help Charl's first-ever launch); (b) the missing music is an intermittent audio cold-start -- SDL_mixer / FluidSynth soundfont may not be ready when the title music is first triggered, or the first-frame shader stall delays/drops the music start. Repro is intermittent. Investigate: does music start reliably if the audio device/soundfont init is awaited before D_StartTitle, or if the music-start is retried once after the first tic? Config at report: music_volume 5, snd_channels 3. See [[doom-ants-audio-architecture]] (SFX+music share one SDL_mixer device).
  **Layman:** Sometimes when you first open the game there's no menu music, but if you quit and open it again the music plays fine. Track down why the first launch occasionally starts silent.
  Kind: investigate.
  Source: user-testing-2026-07-04.
  Investigated + fix implemented 2026-07-04 (awaiting user cold-launch confirmation). Root cause traced: the audio device + soundfont are opened synchronously (I_InitSound -> I_InitMusic) BEFORE the title, and the game logic ALWAYS issues the title music-start (D_DoAdvanceDemo case 0 -> S_StartMusic -> I_RegisterSong + I_PlaySong) -- verified this fires even across the first-frame Vulkan shader-compile stall (that stall is on the main render thread, after the music-start tic, and cannot drop music already handed to the SDL audio thread). So the intermittent silence originates INSIDE SDL_mixer/FluidSynth's cold soundfont warm-up on the very first launch (a warm relaunch always works = warm page cache). Also found a fork-introduced latent bug: S_ChangeMusic marked mus_playing=music even when I_RegisterSong returned 0 (original DMX never failed; the SDL_mixer port can), making a failed cold-start "sticky" so the engine believed silence was playing and never retried. Fix (retry + harden): (1) reintroduced a REAL I_QrySongPlaying (returns Mix_PlayingMusic) [i_sound.h/.c] + log any Mix_PlayMusic failure; (2) s_sound.c: new S_StartMusicInfo helper is the one place music registers+plays+verifies -- on a start that did not take hold it releases the lump, leaves mus_playing NULL, and schedules a short wall-clock retry (MUS_RETRY_DELAY_MS=400, MUS_RETRY_MAX=4 via I_GetTimeMS, frame-rate independent), which mirrors the manual relaunch that warms the mixer; S_ChangeMusic arms the budget then delegates; S_StopMusic cancels any pending retry; the retry is driven from S_UpdateSounds. Builds clean 0 warnings; make test green; headless Classic boot: music ready, success path taken, ZERO false retries over 8s. Cannot repro the cold-start miss headlessly. TO CLOSE: user does several cold launches (first launch after a rebuild / `make clean`) via the desktop icon -> title music plays reliably (or, if it still misses once, the ~400ms retry recovers it audibly); then flip in-progress->shipped + changelog. Note the separate ~1s black screen (shader compile) is expected + one-time-per-rebuild; a "compiling shaders" splash is a distinct future polish, not this item.
  Resolved (2026-07-12, user-confirmed on RX 6600): title-screen music now plays reliably on the first cold launch.

- ✅ [DOOM-0166] **Fix the DOOM-0060 game-select Windows cross-build (windows.h `boolean` clash).**
  DOOM-0060's Windows relaunch path (`D_RelaunchWithIwad`) added `#include <windows.h>` to d_main.c for GetModuleFileNameA/_spawnv, but that pulls in `<rpcndr.h>`, which `typedef`s `boolean` to `unsigned char` — colliding with doomtype.h's `typedef enum {false,true} boolean` and breaking `make windows` with "conflicting types for 'boolean'". The path had never been cross-compiled (mingw deps were unstaged when DOOM-0060 landed), so the break was latent. Fix: `#define WIN32_LEAN_AND_MEAN` before `#include <windows.h>` (d_main.c:48), which excludes the RPC headers while keeping GetModuleFileNameA. Change is entirely inside `#ifdef _WIN32`, so the Linux build/tests are byte-unaffected (verified: 0 warnings, make test green). New Windows binary built + verified to contain the game-select code (Game Select / DOOM II / D_LastGameFile strings) and deployed to the user's Windows test share alongside both WADs. NB the actual _spawnv relaunch behaviour on Windows still awaits DOOM-0060's play-test.
  **Layman:** The DOOM 1 / DOOM 2 chooser could not be compiled for Windows at all — this fixes that so it can be tested on Windows.
  Kind: fix.
  Source: in-session-2026-07-04 (Windows deploy for user play-test).

- 🚧 [DOOM-0202] **Offscreen screenshot / golden-image visual-regression harness (-shotverify).**
  Most of this project's renderer work is verified by launching and eyeballing; the only automated visual net is -rtverify (a NUMERIC direct-light rel-MSE + white-furnace proof, not a look check). Two pain points motivate this: (1) no automated catch for look regressions (a wrong composite, a broken tonemap, a mis-placed sprite) short of manual play-test; (2) headless self-verify is fragile — 2026-07-18 the xdotool/import recipe broke because SDL opened a native Wayland surface with no X window to capture (see [[doom-ants-launch-screenshot-harness]]). Proposal: a -shotverify mode that, like -rtverify, renders a deterministic set of fixed camera views (per tier: Classic / Solid / Ultra) to PNG on disk via an OFFSCREEN render target (no window/desktop dependency) and exit()s; a committed set of golden PNGs + a perceptual diff (e.g. per-pixel or SSIM threshold) flags regressions. Reuses the -rtverify determinism + first-present harness. Enables both CC and CI to self-verify the LOOK with zero window/input tooling. Suggestion only — scope the golden-image storage (git-LFS vs small refs) in design. Pairs with the CI suggestion below.
  **Layman:** A way for the engine to render a few fixed scenes straight to image files (no window needed) and automatically compare them against saved 'known-good' pictures — so a graphics change that accidentally breaks the look gets caught automatically instead of only by someone staring at the screen.
  Kind: test.
  Source: in-session-2026-07-18 (CC suggestion, for user review).
  Approved by user 2026-07-18 for implementation.
  First cut IMPLEMENTED + self-verified headlessly 2026-07-18. `-shotverify [path]` renders the Ultra RT view for kShotWarmup(45) frames (SVGF settle on the static spawn view), copies the final display image (finalImage, R8G8B8A8_UNORM, display-res, already TRANSFER_SRC at the swapchain blit) into a host-visible buffer, writes a PNG (vendored stb_image_write.h v1.16, implemented in rb_image.c per ADR 0002), and exits; a watchdog bails if the RT view never becomes ready. Debug label (DENOISED/PROFILER) suppressed during capture so goldens are clean. VERIFIED on RX 6600: -warp 1 1 Ultra wrote a correct 3840x2160 PNG (colours right — no channel swap; HD materials + denoise clean), build + `make test` green, -rtverify still PASS. Crucially this RESTORES headless visual self-verify (SDL now opens a Wayland surface xdotool/import can't drive — see [[doom-ants-launch-screenshot-harness]]); -shotverify needs no window to drive, only the offscreen copy. Harness gotcha: the game launch must be a foreground `timeout N ./linuxxdoom ... 2>&1 | grep` pipeline — file-redirect / trailing-command / `&` / foreground-`sleep` forms get killed by the harness with no output. REMAINING (still open under this ID): (1) raster/Solid + Classic tier capture (raster renders straight into the swapchain with no TRANSFER_SRC image — read back the swapchain image or add a capture target); (2) the golden-image DIFF + reference store (perceptual/SSIM threshold) that makes it a true regression gate; (3) fixed-resolution offscreen render so goldens don't depend on window size; (4) CI wiring (DOOM-0203).
  Progress (2026-07-18): golden-image DIFF gate IMPLEMENTED — remaining-item (2). New `-shotcompare <ref.png>` parm arms the same Ultra-RT capture, downscales it to a canonical git-friendly size (longest edge 640, ~220 KB PNG — reuses rb_image.c's box filter + PNG loader, zero new deps), then bootstraps the golden if <ref> is missing (writes it, exit 0) or compares via mean-abs-error over RGB vs kGoldenMAE=3.0 (exit 0 PASS / 3 FAIL / 1 i/o). Self-verified on RX 6600: same scene PASS mae=0.000 (RT capture is bit-exact run-to-run, as -rtverify relies on → zero false-positive noise), different level (E1M2 vs E1M1 golden) FAIL mae=11.156 exit 3, plain -shotverify still writes full-res 3840x2160 (unchanged), -rtverify still PASS (rel-MSE 0.08%). First golden committed: linuxdoom-1.10/tests/goldens/e1m1_ultra_rt.png (+ README with the bless/re-bless workflow). Storage fork resolved as the roadmap's "small refs" option (downscaled PNGs in-tree, no LFS). STILL OPEN under this ID: (1) raster/Solid + Classic tier capture; (3) window-size-independent fixed-res render (partly mooted for goldens by the canonical downscale, but full-res -shotverify output is still display-sized); (4) CI wiring for the golden gate — needs a real GPU, so it was deliberately left OUT of DOOM-0203's ✅ headless gate and stays on the optional self-hosted runner. Which additional views to bless as goldens is the user's call — the bootstrap flag makes adding one a one-liner.
  Progress (2026-07-26, debt sweep): the DOOM-0208 canonical-config pin was
  missing `rb_fog`. DOOM-0011 shipped rt_fog (m_misc.c default 1) AFTER that
  pin was written, so the volumetric-fog level leaked in from the user's
  ~/.doomrc — exactly the config-dependence the pin exists to close. Fixed
  (r_vulkan.cpp: `rb_fog = 1;` added to the pin). This does NOT by itself
  explain the logged mae=17.622: the golden was blessed before fog shipped,
  so a fog-on capture vs a fog-off golden is expected to differ. Re-blessing
  is still owed and still deliberate — do not blind-re-bless.

- ✅ [DOOM-0203] **Minimal CI on push: build + make test + headless smoke.**
  There is no automated build/test gate today — breakage is only caught locally. The repo is a PUBLIC GitHub repo (free Linux Actions minutes per [[push-freely-public-repo]]), so a lightweight workflow is essentially free. Proposal (first cut, CPU-only): on push/PR, run make + make test + a software-renderer headless boot smoke (SDL dummy drivers, warp a level, no crash — the recipe already used for early headless checks). Caveat: the GPU paths (Vulkan RT -rtverify, and the -shotverify Ultra views above) need a GPU runner — GitHub's hosted runners have none, so RT/visual gates would be a self-hosted-runner (the user's RX 6600 box) stretch goal, kept OPTIONAL and manual-dispatch to avoid burning the user's machine on every push. Respect the push-cadence rules (§6): CI is a gate, not a reason to auto-push. Suggestion only.
  **Layman:** Set up an automatic check that compiles the game and runs the tests every time code is pushed, so a change that fails to build or breaks a test is caught immediately — the repo is public, so this is free.
  Kind: chore.
  Source: in-session-2026-07-18 (CC suggestion, for user review).
  Approved by user 2026-07-18 for implementation.
  CORRECTION (2026-07-18): the suggestion's premise "there is no automated build/test gate today" was WRONG — I asserted it without checking .github/workflows/ (a verify-before-stating miss). The CORE of this item ALREADY EXISTS: .github/workflows/build.yml (added 2026-07-03) runs `make -C linuxdoom-1.10` + `make ... test` on every push/PR to master (docs-paths ignored, concurrency-cancel, actions/checkout@v7, deps from packaging/ci-deps.txt, mirrored locally by packaging/ci-local.sh). It has been GREEN on every recent push, incl. today's DOOM-0202 push (run 29639869154, 46s, success). So "minimal CI: build + make test" is DONE. Remaining (optional, genuinely not yet present): (1) a headless RUNTIME smoke — the runner has no WAD/display, but a free WAD (Freedoom, GPL-compatible, ships demo lumps) + SDL_VIDEODRIVER=dummy/SDL_AUDIODRIVER=dummy + the engine's existing -timedemo/-nodraw could boot+play+exit to catch boot/runtime regressions the compile gate misses (needs checking Freedoom demo-version compat first — a mismatch returns to title rather than quitting, per DOOM-0034); (2) the self-hosted-GPU gate running -rtverify + the new -shotverify (DOOM-0202) on the user's RX 6600. Narrowing this item to those two optional extras; the compile+test gate it mainly asked for is already live.
  Resolved (2026-07-23, 0bdcf7b): headless runtime smoke (extra 1) shipped, completing the item's headline scope (build + test + headless smoke) — the build+test gate already existed (build.yml). New engine flag `-bootsmoke [N]` (d_main.c) boots the game, simulates N tics (default 105 ~= 3 s) through the normal software-render loop, then exits 0 via I_Quit; pins RB_CLASSIC so a stray software Vulkan ICD can't pull it onto the GPU path, and needs no demo lump. Deliberately NOT -timedemo (verified via source: a successful timedemo exits 255 via I_Error timing report; a version-mismatched Freedoom demo exits 0 without rendering — both wrong for a gate). build.yml gains a smoke step: install Freedoom, run under SDL dummy video/audio, -warp 1 1 -bootsmoke 105, assert the success line + clean exit (timeout + pipefail backstop hang/crash). ci-local.sh mirrors it best-effort (native, local IWAD, skips when none). Verified: make + make test green; direct headless run exit 0; full ci-local --native (clean git-archive checkout) PASSED build+test+smoke. Remaining = extra (2), the OPTIONAL self-hosted-GPU gate running -rtverify + -shotverify on the user's RX 6600 — deferred (needs the user to register the runner; manual-dispatch by design). File it separately if/when wanted.

- ✅ [DOOM-0208] **-shotcompare E1M1 golden is stale (mae≈17.6 at base and HEAD) — re-bless/investigate the drift.**
  During DOOM-0206 branch verification, `-shotcompare tests/goldens/e1m1_ultra_rt.png` FAILs with mae≈17.622 (threshold 3.0). Isolated to a PRE-EXISTING issue, NOT a DOOM-0206 regression: an isolated build at base 1da136c (before any DOOM-0206 code) produces the SAME mae=17.622, and the DOOM-0206 branch is deterministic run-to-run (A/B/C all 17.622) — base==branch, so the menu work does not touch the RT render. -rtverify still PASSES (the statistical RT-correctness gate) and make test is green. The golden was blessed at 2d7d570 (prior session) and no longer matches the current render. Likely causes to check: a persisted ~/.doomrc value drift (e.g. rt_brightness=13), a driver/dep update, or the KDE/Wayland desktop-overlay capture artefact (cf. DOOM-0173). Also noticed: the very first -shotcompare in a shell read mae=5.672 then stabilised at 17.622 — points to config being normalised/written on exit affecting the next read (worth confirming the gate doesn't self-perturb via ~/.doomrc writes). Action: investigate the drift cause, then re-bless the golden deliberately (do NOT blind-re-bless — that would mask whatever drifted). DOOM-0202 follow-up.
  **Layman:** The automatic "did the picture change?" screenshot test is failing because its reference image is out of date — not because of a real bug. Needs a fresh reference photo and a look at what drifted.
  Kind: investigate.
  Source: in-session-2026-07-19 (DOOM-0206 wrap-up verification).
  Also affects -rtverify (same gate-instability family): during the same DOOM-0206 wrap-up, -rtverify direct-light rel-MSE read 0.0796% PASS early in the session (×many, all subagent + direct runs) but later read a deterministic 3.4943% FAIL (×3), lit-px 64000→63987. PROVEN not DOOM-0206: an isolated rebuild at base 1da136c gives the identical 3.4943%/63987 in the same environment (base==HEAD for BOTH the render and rtverify), so the menu work does not touch the RT path. The rel-MSE is CONSTANT across every ~/.doomrc toggle tried (render_scale 100/50, upscaler 0/1, rt_filth/rt_wet 0/1) — so it is NOT those configs. No disk GI-bake cache exists (the bake is a runtime GPU compute with atomic scatter). Leading hypothesis: GI-bake / RT-render GPU float-accumulation nondeterminism that is stable within a "GPU-state epoch" but shifts between epochs (early-session vs after dozens of heavy RT renders) — which would make BOTH gates (shotcompare mae + rtverify rel-MSE) epoch-sensitive. Action for DOOM-0208: make the RT gates robust to this (seed/determinism audit of the bake's atomic scatter, or widen/normalise the bars), and re-bless the golden once the render is stable. INV-5 for DOOM-0206 holds (base==HEAD).
  Resolved (2026-07-23): root cause was the golden-image gate inheriting live ~/.doomrc. Reproduced on this RX 6600 box: -shotcompare deterministic FAIL mae=5.672 (the 17.6 in the original note tracked a since-changed config state), while -rtverify now reads 0.0796% PASS (its 3.4943% FAIL was a transient environmental blip, not present now). Proved the drift = rb_exposure: live rt_brightness=13 vs the default 10 the golden was blessed at — forcing 10 dropped mae to 2.857. Fix (r_vulkan.cpp, shot-mode arm block): pin a canonical, config-independent RT configuration (shipped defaults — rb_rtdebug=6, rb_rtdebug_menu=0, rb_profile=0, rb_upscaler=1, rb_renderscale=50, rb_exposure=10, rb_detile=2, rb_filth=1, rb_wet=1, rb_flashlight=0) whenever -shotverify/-shotcompare is armed, so play-test tweaks (brightness, flashlight, effect toggles) can no longer poison the gate. Deliberately re-blessed the golden under canonical config: fresh compare now mae=0.000 bit-exact ×3, and proven config-independent — an extreme poisoned live config (brightness=15, flashlight ON, wet OFF) still scores 0.000. make + make test green.

- 📋 [DOOM-0235] **In-game auto-update: check for a new release, download, install, and relaunch.**
  Flow: on launch (or via a menu item) query the latest GitHub release (tag/version + asset URLs) over HTTPS; if newer than the built-in version, prompt the user; on accept, download the platform asset with a progress UI, verify it (checksum/signature), then hand off to a small updater/relaunch step (the running game can't overwrite itself in place -- spawn a helper that waits for exit, swaps files, relaunches). Per-platform: Linux AppImage self-replace; Windows installer/exe swap via helper. Respect §6 (no silent network calls without opt-in); make the update check user-toggleable. Reuse the existing version line + CHANGELOG for the 'what's new' text. Depends on packaging (installer item below) for the Windows path.
  **Layman:** The game checks if a newer version exists and, if you agree, downloads it, closes, installs, and reopens itself -- no manual re-download.
  Kind: feature.
  Source: user-request 2026-07-23.

- 📋 [DOOM-0236] **Windows: ship a signed installer to clear SmartScreen (signing, not file format, is the fix).**
  IMPORTANT: an .msi by itself does NOT bypass SmartScreen -- the warning is driven by Authenticode code-signing + download reputation, not the file extension; an unsigned .msi warns exactly like the unsigned .exe. Real fix: sign the binary (and installer) with an Authenticode cert. An OV cert still needs reputation to build up; an EV cert clears SmartScreen immediately but is pricier and needs a hardware token / cloud HSM. Scope: (1) evaluate cert options (cost/EV-vs-OV, cloud signing e.g. Azure Trusted Signing); (2) add a WiX/MSI (or NSIS) installer target alongside the current .exe with Start-menu shortcut + uninstaller (also a natural home for the auto-update helper); (3) wire signing into the release pipeline. Offering the .msi is still worthwhile for UX/uninstall, but must be paired with signing to actually remove the warning.
  **Layman:** Make the Windows download stop showing the blue 'unrecognised app' warning by code-signing it, and offer a proper installer.
  Kind: package.
  Source: user-request 2026-07-23.

- 📋 [DOOM-0240] **Add the missing regression tests for the netgame + WAD-bounds hardening fixes.**
  docs/standards/testing.md cited the mus2mid, netgame and bounds fixes from the 2026-07-23 pass as the model for "every bug fix gets a regression test", but only tests/mus2mid_test.cpp exists. The standard has been corrected to name this gap; this item closes it. All three are pure input validation and CPU-testable: w_wad.c:208/286 (crafted numlumps/infotableofs vs real file size), i_net.c:235 (numtics > BACKUPTICS), d_net.c:277 (netconsole >= MAXPLAYERS), d_main.c:1356 (-warp 3 argv NULL-deref).
  **Layman:** Three security fixes shipped without the automatic tests that prove they stay fixed — write those tests.
  Kind: test.
  Source: debt-sweep-2026-07-26.
  Progress (2026-07-26, /test-audit): the item's claim that all four are "pure input validation and CPU-testable" was checked per function and is only partly right — sizing the work before it starts. Cheap in this harness (single TU + a stub or two, mus2mid_test is the model): w_wad.c:206-219 W_AddFile and the twin W_Reload check at :284-296 — w_wad.c pulls only i_system.h/z_zone.h, I_Error is a plain extern (stub it), Z_Malloc/Z_Free are trivially faked; and s_sound.c:293-296 (the >=NUMSFX bound), a pure comparison against a compile-time constant. Moderate: i_net.c:235 PacketGet — the check is inline in a function that calls recvfrom(), so it needs a socketpair() fake or a small extract-the-bound-check refactor first. NOT cheap: d_net.c:277 GetPackets (needs a fake transport plus nodeingame/playeringame engine state) and d_main.c:1356 -warp (the guard sits inside the ~1000-line D_DoomMain; testing it means extracting the argv parse into its own function). Suggest taking the three cheap ones first — they are most of the security value for a fraction of the work.

- 📋 [DOOM-0241] **Write INV conformance tests for the shipped renderer specs now the CI-has-no-WAD gate is gone.**
  Only nee_sampling_test.cpp references an INV. DOOM-0026 (spec:290-319) and DOOM-0008 (spec:426-471) are both shipped and both defer their conformance test to "gated on CI having a WAD" — that gate no longer exists: build.yml installs freedoom and boots the engine headless (DOOM-0203). CPU-testable today: DOOM-0026 INV-1/INV-3, DOOM-0008 INV-3/INV-5, DOOM-0181 INV-9 (push-constant layout), the rb_fog 0..3 clamp.
  **Layman:** Several finished features list rules they must obey, but nothing automatically checks them.
  Kind: test.
  Source: debt-sweep-2026-07-26.

- ✅ [DOOM-0242] **Stop calling the function under test inside assert() — 15 sites compile away under NDEBUG.**
  cppcheck assertWithSideEffect on 15 sites across mus2mid_test.cpp, rb_image_test.cpp, rb_materials_test.cpp and rb_text_test.cpp, e.g. assert(mus2mid(...) == 0). No -DNDEBUG is set today (Makefile TEST_CXXFLAGS), so the suite is correct as it stands — but anyone adding NDEBUG turns every one of these into a silent no-op that still reports PASS. Fix: assign to a local, then assert the local.
  **Layman:** Some tests would silently do nothing if the build settings changed — make them robust.
  Kind: test.
  Source: debt-sweep-2026-07-26.
  Resolved (2026-07-26, /test-audit): fixed by retiring assert() from the suite entirely rather than by hoisting each call. New tests/check_util.h provides check()/check_eq_int()/check_summary() -- ordinary functions, so nothing compiles away and a failure no longer aborts the binary. mus2mid_test, rb_image_test, rb_materials_test and rb_text_test converted; the other three already used the idiom and now share the header instead of each carrying a copy. Verified: building mus2mid_test with -DNDEBUG BEFORE the fix printed "all passed" with zero checks executed; after it, the same -DNDEBUG build catches a mutation of allocate_midichannel (exit 1). docs/standards/testing.md updated -- the convention was still "main() runs the cases with assert".

- ✅ [DOOM-0243] **rb_text_test passes vacuously when no system TTF is present — use the bundled Oxanium instead.**
  tests/rb_text_test.cpp:46-49 prints "skipped" and returns 0 when it cannot open a DejaVu TTF, so on any image lacking fonts-dejavu-core the file asserts only the NULL case. CI works around it by installing fonts-dejavu-core (ci-deps.txt:16). The repo already ships assets/Oxanium-SemiBold.ttf.h — bake from that instead and make an absent font a hard failure, which also drops the CI dependency.
  **Layman:** A font test quietly skips itself on machines without a particular font, so it can pass without testing anything.
  Kind: test.
  Source: debt-sweep-2026-07-26.
  Resolved (2026-07-26, /test-audit): rb_text_test now bakes the bundled assets/Oxanium-SemiBold.ttf (the same embedded array r_vulkan.cpp bakes at init) instead of hunting three system DejaVu paths, so the skip-to-green path is gone -- a missing font is now impossible, not silent. The Makefile gives every test binary an order-only $(ASSET_HDRS) prerequisite, so the generated header exists even on a fresh tree (verified by deleting it and re-running make test). fonts-dejavu-core dropped from packaging/ci-deps.txt. Coverage added while there: vertical metrics (ascent/descent/line_gap), the atlas-doubling retry at 96px, and exact -- not +/-0.5px -- equality on rb_text_measure. That tolerance mattered: 22 of Oxanium's 93 adjacent glyph pairs differ by under 0.5px and the digits are identical width, so an off-by-one glyph index on a numeric string was invisible under it.

- ✅ [DOOM-0244] **game_select_test mirrors D_DetectIwads' loop instead of calling it, so it cannot catch a regression.**
  tests/game_select_test.cpp:36-52 defines select_reps() as a hand-copied "Mirror of D_DetectIwads' pure selection loop". Changing the real loop in d_main.c cannot fail this test. Fix: extract the pure selection loop into iwad_detect.h (which already exists) and have both the engine and the test call it.
  **Layman:** A test re-implements the code it is supposed to check, so breaking the real code doesn't fail the test.
  Kind: test.
  Source: debt-sweep-2026-07-26.
  Resolved (2026-07-26, /test-audit): the preference-scan loop moved out of D_DetectIwads into iwad_select_reps() in iwad_detect.h, parameterised by an "is it installed?" predicate. d_main.c passes one that builds <dir>/<name> and calls access(); game_select_test passes one that checks an in-memory set, so the test now drives the REAL loop and stays hermetic. Verified by mutation: dropping the "family already found" guard in iwad_select_reps now fails the test with 4 preference-order failures (before the change, the same mutation could not fail it). Engine rebuilt with zero warnings; headless boot smoke green.

- 📋 [DOOM-0245] **Bump the staged Vulkan-Headers pin from vulkan-sdk-1.4.350.0 to 1.4.350.1.**
  mingw-deps/README.md:19-20,25-27 pins vulkan-sdk-1.4.350.0; upstream latest is vulkan-sdk-1.4.350.1. Not recorded in the dependencies.md Version Exception Ledger (which reads "_(none currently)_"), and dependencies.md repeats the stale number in its inventory. Deliberately NOT bumped in the debt sweep: it changes the Windows cross-build and could not be verified from this Linux session. Bump the two curl URLs and the dependencies.md line together, then run packaging/windows-build.sh.
  **Layman:** A bundled Windows-build dependency is one patch release behind.
  Kind: chore.
  Source: debt-sweep-2026-07-26.

- 📋 [DOOM-0246] **packaging/README.md hardcodes 0.5.0 in its usage example.**
  packaging/README.md:11 embeds the literal 0.5.0. It is not part of the three-place version lockstep (releases.md), so nothing keeps it current. Replace with <ver>.
  **Layman:** A copy-paste example in the packaging docs will read as out of date after the next release.
  Kind: doc-fix.
  Source: debt-sweep-2026-07-26.

- 📋 [DOOM-0247] **Anchor the bare `doom` .gitignore rule so it can't swallow a future directory.**
  .gitignore:9 is a bare `doom`, which matches any file OR directory named doom at any depth — a future docs/doom/ would vanish silently. Anchor it to /linuxdoom-1.10/doom. Nothing is currently shadowed (verified with git ls-files + git status --ignored).
  **Layman:** An ignore rule is broader than intended and could hide a folder someone adds later.
  Kind: chore.
  Source: debt-sweep-2026-07-26.

- 📋 [DOOM-0252] **Bound the savegame indices P_UnArchiveThinkers turns straight back into pointers.**
  p_saveg.c:303/307/311 rebuild mobj->state, mobj->player and mobj->info by indexing states[], players[] and mobjinfo[] with values memcpy'd raw out of the .dsg file, with no range check: mobj->state = &states[(int)(intptr_t)mobj->state]; mobj->player = &players[idx-1]; mobj->info = &mobjinfo[mobj->type]. A crafted or corrupted savegame therefore forms an out-of-bounds pointer that is dereferenced immediately (mobj->info->..., mobj->player->mo = mobj). Savegames are a named trust boundary in docs/standards/security.md, and the only one the 2026-07-23 hardening pass never reached -- git log on p_saveg.c shows no security commits, only the 2026-06-29 -Wall cast cleanup. Fix: validate against NUMSTATES / MAXPLAYERS / NUMMOBJTYPES before dereferencing and I_Error on violation, mirroring the existing default-case guard at p_saveg.c:319. Reproduce first (the load path is pure array indexing, so a test can supply small fake states[]/mobjinfo[] and a crafted byte buffer).
  **Layman:** A damaged or hand-edited save file can make the game read memory it doesn't own, because the load code trusts the numbers in the file.
  Kind: security.
  Source: test-audit-2026-07-26 lane-E.
  Progress (2026-07-26): DOOM-0254 bounded the SPECIALS' sector indices (new P_SectorFromSave covers ceiling/door/floor/plat/flash/strobe/glow). P_UnArchiveThinkers' index->pointer casts are still unbounded, and the buffer-extent gap is DOOM-0255.

- 📋 [DOOM-0253] **Audit whether any other ~/.doomrc int is used as an array index without a clamp.**
  DOOM-0216 clamped msgValueNames[showMessages] and fpsPosNames[fpsCorner] at their m_menu.c use sites, but m_misc.c's M_LoadDefaults loop itself still writes any int-shaped config value straight into its target with no range validation (m_misc.c:407-430) -- so the mitigation is per-known-site, not systemic. Sweep the defaults table for every entry whose value indexes an array or selects a mode, and either clamp on read in M_LoadDefaults or confirm each use site already clamps. Scope is small (the table is short); the point is to find out whether DOOM-0216 was the only one.
  **Layman:** Hand-editing the config file to a silly number crashed the game once already; check whether any other setting can still do that.
  Kind: investigate.
  Source: test-audit-2026-07-26 lane-E.
  Progress (2026-07-26): DOOM-0254 clamped the four ~/.doomrc ints that index arrays or size allocations (usegamma, screenblocks, detaillevel, snd_channels) in M_LoadDefaults. The remaining table entries have not been swept.

- ✅ [DOOM-0254] **Harden every untrusted-input boundary the 2026-07-26 audit + indie-review sweep found.**
  Corroborated by 5 review lanes. Fixed: p_setup.c validates every WAD-derived
  index (seg vertex/linedef/sidedef, subsector seg range, sidedef sector, linedef
  sidenum, node children, BLOCKMAP header + dimensions); r_data.c bounds PNAMES
  count, texture-directory offsets, patch indices and texture headers; v_video.c +
  f_finale.c reject patch posts that overrun the patch height (new
  V_PostInBounds); wi_stuff.c replaces a never-compiled `#ifdef RANGECHECKING`
  guard with real clamps; hu_stuff.c bounds shiftxform[]; p_mobj.c bounds
  mapthing type below; r_bsp.c sizes solidsegs[] for widescreen + guards the
  insert; z_zone.c rejects bad allocation sizes; p_saveg.c validates the specials'
  sector indices; d_main.c bounds the response-file arrays, snprintf's the demo
  name and clamps -skill; m_misc.c clamps usegamma/screenblocks/detaillevel/
  snd_channels from ~/.doomrc and writes the config atomically; m_menu.c
  NUL-terminates savegame strings; i_net.c bounds the -net host list; i_video.c
  clamps -scale; g_game.c length-checks the demo header.
  Verified: build clean, 7 test suites pass, 8 maps across doom.wad + doom2.wad
  boot-smoke clean.
  **Layman:** A hostile or broken WAD, savegame, config file or command line can no longer make the game read or write memory it does not own.
  Kind: security.
  Source: audit+indie-review-2026-07-26.
  Also in this sweep: p_switch.c now bounds the alphSwitchList scan by the table's own element count instead of MAXSWITCHES (the cppcheck arrayIndexOutOfBounds error — the terminator entry made it unreachable in practice, but the loop bound was two-thirds larger than the table).
  Follow-up (2026-08-03, code-quality-review sweep): the claim above that
  p_setup.c "validates every WAD-derived index" was two sites short, and the
  p_saveg.c entry covered only the specials' sector indices. Closed now:
  P_LoadLineDefs routed its v1/v2 through P_WadIndex (they were the one pair
  the original pass missed, while P_LoadSegs guarded its own vertices), and
  p_saveg.c gained a P_SaveIndex twin of that helper now bounding the psprite
  state, mobj state, mobj type and mobj player indices -- the last of which is
  stored 1-based, so a savegame holding 0 indexed players[-1]. security.md's
  untrusted-input table already listed .dsg files; the code had not caught up.

- 📋 [DOOM-0255] **Thread a buffer-end bound through every p_saveg Unarchive* function.**
  G_DoLoadGame discards M_ReadFile's length and no Unarchive* tracks an end pointer, so strcmp/memcpy walk past the allocation on a short .dsg. DOOM-0254 bounded the sector INDICES; the buffer extent itself is still unchecked. Extends DOOM-0252.
  **Layman:** A truncated or hand-edited savegame can still read past the end of the loaded file.
  Kind: security.
  Source: indie-review-2026-07-26 game-save-net.

- 📋 [DOOM-0256] **d_net.c NetbufferChecksum() returns 0 under NORMALUNIX, so packet integrity is unchecked.**
  The shipped Linux build defines NORMALUNIX, which compiles the checksum out; HGetPacket's integrity test is a no-op. Either implement an endian-safe checksum or record the vanilla-compatibility decision in the security standard.
  **Layman:** Network games do not actually verify that packets arrived intact.
  Kind: security.
  Source: indie-review-2026-07-26 game-save-net.

- 📋 [DOOM-0257] **BuildHdSet aborts on GPU allocation failure instead of falling back to the paletted set.**
  r_vulkan.cpp:5956-5959 uses fatal Check()/I_Error, contradicting the documented contract that g.hdSet always ends valid with InitHdDefault's paletted fallback.
  **Layman:** If the HD texture upload runs out of video memory the game quits instead of dropping to the classic textures.
  Kind: fix.
  Source: indie-review-2026-07-26 vk-frame.

- 📋 [DOOM-0258] **Animated-texture retex dirties the whole static point-light cache every frame.**
  RB_UPD_RETEX sets staticLightsDirty unconditionally (r_vulkan.cpp:6656, 8027), re-running the O(subsectors x emitters) cull the DOOM-0170 split removed. Only dirty when the retexed face is emissive.
  **Layman:** Scrolling/animated wall textures make the renderer redo light work it already did.
  Kind: perf.
  Source: indie-review-2026-07-26 vk-frame.

- 📋 [DOOM-0259] **AppImage build downloads continuous-tag tooling and executes it without verifying it.**
  packaging/build-appimage.sh:33-36 fetches appimagetool from a rolling `continuous` tag and execs it. Pin a release tag and verify a recorded sha256 before running it.
  **Layman:** The Linux packaging step runs a program it just downloaded, with no check that it is the expected one.
  Kind: security.
  Source: indie-review-2026-07-26 build-ci-packaging.

- 📋 [DOOM-0260] **packaging/release.sh duplicates the Windows cross-build block from windows-build.sh.**
  release.sh:80-109 repeats windows-build.sh:30-59 inline; call the script instead so one edit covers both paths.
  **Layman:** Two copies of the same build steps can drift apart.
  Kind: refactor.
  Source: indie-review-2026-07-26 build-ci-packaging.

- 📋 [DOOM-0261] **Decide whether to migrate SDL2 -> SDL3, or record the hold.**
  SDL3 is the current major and SDL2 is maintenance-only; SDL2_mixer 2.8.2 and the staged Windows deps are all SDL2-series. The dependencies standard requires latest-stable or a logged reason, and today there is neither a migration nor a ledger entry.
  **Layman:** The game uses the previous generation of the SDL library; we should decide whether to move.
  Kind: chore.
  Source: indie-review-2026-07-26 build-ci-packaging.

- 📋 [DOOM-0262] **Extract a shared helper for the near-identical text and cursor pipeline builders.**
  r_vulkan.cpp:4994-5078 and 5187-5240 build the same simple pipeline shape twice.
  **Layman:** About ninety lines of near-identical setup code exist twice.
  Kind: refactor.
  Source: indie-review-2026-07-26 vk-frame.

- 🚧 [DOOM-0268] **Place the player anywhere on any map and capture it headlessly.**
  User request 2026-07-27, after DOOM-0267 cost two wrong fixes: every diagnosis had to be
  reasoned from source because the bug only reproduced at one spot that only a human could walk
  to, and every candidate fix needed a play-test round-trip to disprove.
  **Part 1 SHIPPED (`8542d2b`): `-warpto X Y [ANGLE]`** in `G_WarpToSpot` (`g_game.c`), called
  from `G_DoLoadLevel` right after `P_SetupLevel`. X/Y are map units exactly as a map editor or
  the `/` diagnostic prints them; ANGLE is optional degrees (0 = east, counter-clockwise, the
  Thing convention); Z is taken from the destination sector's floor. It relinks the mobj via
  `P_UnsetThingPosition`/`P_SetThingPosition` rather than assigning x/y, so the blockmap and
  sector lists stay correct. It found DOOM-0267's real cause on its first use.
  **Part 2 OUTSTANDING and the reason this is only half a harness: headless GPU capture.**
  `-shotverify <path>` already renders, captures and writes a PNG, but it cannot be driven from
  a non-interactive shell: Vulkan hangs at window/swapchain creation under `SDL_VIDEODRIVER=x11`,
  `wayland` AND `offscreen` alike (verified 2026-07-27, and verified identical on an unmodified
  build, so it is the environment and not any local change). Classic runs fine headless via
  `-bootsmoke` + the dummy driver; only the Vulkan tiers are blocked.
  Fix to scope: a **true headless render path that never creates a surface or swapchain**.
  Vulkan does not need either to render — the engine already draws into offscreen targets
  (DOOM-0170 L2a) and `-shotverify` already copies from one. So the work is to let
  `RB_Vulkan_Init` skip SDL window + surface + swapchain when a `-headless` parm is present,
  render N warm-up frames into the existing offscreen target, capture, and exit. That would make
  Solid and Ultra as scriptable as Classic already is, and let a renderer change be checked
  against a golden image without a person at the keyboard.
  Then: pair `-warpto` with `-shotverify` per tier to build a small library of
  known-tricky viewpoints (this secret, the zigzag slits of DOOM-0270, a lift mid-travel) as
  regression goldens — which is what DOOM-0202's `-shotcompare` gate wants and currently only
  has one of.
  **Layman:** A way to drop the player at any spot on any map from the command line and take a screenshot without anyone playing, so a graphics bug that only shows up in one corner of one room can be reproduced and checked automatically instead of needing someone to walk there.
  Kind: test.
  Source: user-request-2026-07-27.
  Progress (2026-08-03): Part 2's blocking claim needs qualifying, and the
  qualification has a cost nobody had noticed.

  `-shotverify` ran fine from a non-interactive shell today — a dozen
  times, including a four-capture A/B matrix at 3840x2160 in Ultra RT for
  DOOM-0296. So "cannot be driven from a non-interactive shell" is too
  strong. What made it work is that the shell had DISPLAY=:0 and
  WAYLAND_DISPLAY=wayland-0: a graphical session was present, so window and
  swapchain creation succeeded. The 2026-07-27 verification stands for a
  shell with NO display; Part 2's actual scope is unchanged.

  THE COST, which is the part worth recording: because it still creates a
  window, every capture STEALS FOCUS from whatever is on screen. Running
  two of them while the user was mid-play-test ended their session — twice
  in one day, on the very item the captures were meant to support. So
  `-shotverify` is scriptable-with-a-display but NOT headless, and the
  difference is not academic: it cannot be used at all while a human is
  playing.

  That sharpens the case for the `-headless` path Part 2 already scopes. It
  is not only about CI or a display-less shell; it is what lets a
  measurement run BESIDE a play-test instead of interrupting it. Until it
  exists, the working rule is: never run the engine while the user is
  playing.

- 📋 [DOOM-0284] **Surround sound on setups that support it, plus binaural audio for headphones.**
  DOOM today pans SFX with Mix_SetPanning -- a stereo left/right balance
  and nothing more, so a monster directly behind you sounds identical to
  one directly in front. Two wants, one menu: (a) real multi-channel
  output where the device offers it (SDL_mixer opens the device with a
  channel count; the engine would need to place each sound in the
  horizontal plane rather than on a L/R axis), and (b) an HRTF binaural
  path for headphones, which is where the elevation and front/back cues
  actually come from. Add a Sound-options row for the choice
  (Stereo / Surround / Headphones-binaural), with auto-detect as the
  default. Depends on the audio architecture already settled: SFX play as
  SDL_mixer chunks on the SAME device as music -- never a second device
  or a custom mixer, which is silent on Windows.
  **Layman:** Proper 5.1/7.1 sound on a surround setup, and headphone audio that puts sounds above, behind and around you.
  Kind: feature.
  Source: user-request-2026-07-30.
  Note (2026-08-05, upstream review): GZDoom's answer to both this and
  DOOM-0201 is one thing -- it does not use SDL_mixer for effects at all.
  src/common/audio/sound/oalsound.cpp is an OpenAL Soft backend that
  already carries everything both bullets ask for: ALC_SOFT_HRTF behind an
  snd_hrtf cvar with on / off / "don't care" (:598, :627-635), ALC_EXT_EFX
  environmental reverb including the underwater case (snd_efx,
  snd_waterreverb), AL_SOURCE_SPATIALIZE_SOFT and AL_SOURCE_RELATIVE for
  per-source 3-D placement (:250-264), distance rolloff, and
  AL_SOFT_source_latency for accurate playback position.

  So the real decision is not "which panning curve" but whether to swap the
  effects backend from SDL_mixer to OpenAL Soft. That is a big change and
  it collides head-on with [[doom-ants-audio-architecture]], whose whole
  lesson was that a second device / custom mixer went SILENT on Windows --
  though note OpenAL is a different proposition from that: it is the only
  device, not a second one alongside SDL_mixer's.

  Worth scoping properly rather than deciding here. If the swap is judged
  too big, DOOM-0201's cheap-wins framing (better panning + a smoother
  distance curve on the existing device) still stands on its own, and this
  bullet's surround/binaural half is the part that genuinely needs OpenAL.

- ✅ [DOOM-0285] **Never frame the play area with a border -- default to the full-width view.**
  The shipped screenblocks default was vanilla's 9: a reduced play area
  inside a tiled wall-texture frame. The user's position is absolute --
  "I will never ever want a border around the play area. It must be full
  screen." Default moved to 10, which is already the largest the menu
  allows (DOOM-0148 clamps to 10 so the status bar stays; 11 is the
  HUD-less fullscreen view). Spotted by the user in a screenshot of a
  throwaway -config test run, where the omitted key fell back to this
  default -- so it was the shipped default that was wrong, not the test.
  Resolved (2026-07-30): m_misc.c defaults table.
  **Layman:** A fresh install no longer draws the old decorative frame around a shrunken picture; the game fills the screen.
  Kind: fix.
  Source: user-request-2026-07-30.

- ✅ [DOOM-0287] **The -shotverify capture is not tic-deterministic, so a cold-cache run blesses a different golden.**
  Measured 2026-07-30 while setting up a DOOM-0011 A/B. Four consecutive
  warm `-shotcompare` runs of one unchanged build returned mae=9.247,
  9.247, 9.248, 9.247 against a golden blessed by that same build --
  i.e. the runs are bit-identical to each other and all differ from the
  golden by the same 9.25. The golden's run was the FIRST after a shader
  rebuild, so its Vulkan pipeline cache was cold and startup was slower.
  Two warm full-res captures of the same build differ by only 1.63.

  Cause: the capture fires after a fixed count of RENDERED FRAMES
  (kShotWarmup, r_vulkan.cpp:8724-8731) but the game's tic clock is
  wall-clock driven, so a slower startup means more tics have run by the
  capture frame and every animated flat / flickering sector / sprite
  frame lands on a different phase. DOOM-0202 already pinned the ripple
  clock (r_vulkan.cpp:7912) and the config (DOOM-0208, :8709) for exactly
  this reason; the game clock itself was missed.

  Consequence: the gate's 3.0 MAE threshold is smaller than the 9.25 a
  cold bless injects, so a golden blessed on a cold cache fails every
  warm run afterwards and vice versa. It also silently corrupts any
  image A/B run across a rebuild -- the rebuild itself invalidates the
  pipeline cache, so the "changed" build's first capture is always the
  cold one.

  Fix direction: under rb_shotverify, drive the game clock from the
  rendered-frame counter instead of wall-clock (a fixed tics-per-frame),
  so leveltime at the capture frame is a constant. Workaround until
  then: after any rebuild, discard the first capture and use the second.
  **Layman:** The automatic screenshot test can pass or fail depending on how fast the game loaded, not on whether anything actually changed.
  Kind: fix.
  Source: in-session-2026-07-30.
  CORRECTION (2026-07-30, same session): the tic-clock diagnosis above is
  WRONG and the fix direction it names would not fix this. A temporary
  diagnostic printing gametic / leveltime / svgfFrame / shotFrame at the
  capture site was added and run repeatedly.

  What the counters actually show: gametic at capture does drift by +/-1
  between runs (45 / 46 / 45 on one build, 47 on another) -- but three
  runs whose gametic differed scored 0.003 MAE against each other, i.e.
  a tic of drift is worth nothing. Meanwhile two runs with an IDENTICAL
  gametic (47 and 47), identical svgfFrame (46) and identical shotFrame
  scored 2.41 apart at High fog and 6.87 at Low. So the varying counter
  is not the varying output, and pinning the game clock would leave this
  in place.

  Signature of the real defect: the difference lands on the LIT SURFACES
  -- light panels and bright walls blow out, while open air, floors and
  the fog volume are unchanged. That is a global-illumination difference,
  not an animation-phase one. It is also INTERMITTENT rather than
  bimodal-per-build: consecutive runs of one unchanged build are usually
  bit-identical (0.002-0.007), and then one run in three or four lands
  ~2.4-10 MAE away. Prime suspect is the GI bake / SVGF temporal history
  -- something whose state at the capture frame depends on how many
  frames ran before the bake had fed the accumulator -- but that has NOT
  been verified and the counters above rule out the obvious candidates.

  Impact is unchanged and now better quantified: the gate's threshold is
  3.0 and the rogue run swings up to 10, so DOOM-0202 can fail on an
  unchanged build. It also sets a floor on what any image A/B can
  resolve: the DOOM-0011 step-count A/B measured a 0.15 signal against
  this 2.4-6.9 noise, and only survived because the signal was small in
  a direction that did not matter.

  Method note worth keeping: capture THREE runs per build, not two. Two
  runs cannot tell you which of them is the outlier; three can.
  Resolved (2026-07-30, b23d609): CAUSE FOUND BY THE USER, and it was
  neither of the two theories logged above. They were moving the mouse
  while a capture ran. The window grabs the pointer and the capture
  renders ~45 warm-up frames before taking one, so a nudge in that window
  turns the player and changes the image.

  It fits every observation the game-clock theory could not: the whole
  frame shifts at once rather than one animated thing changing; the
  residual lands hardest on high-contrast light panels (a yaw shift moves
  sharp edges furthest); gametic, svgfFrame and shotFrame are identical
  across runs that differ; and it is intermittent precisely because it
  depends on whether a person happened to touch the mouse. Confirmed
  structurally too: rb_shotverify appeared in NO .c file, so the input
  path had no knowledge of the capture modes at all.

  Fix: I_InputDisabled() in i_video.c, true for -shotverify /
  -shotcompare (never valid with input live) and for a new -noinput flag
  any run can pass. Gated at three sites -- I_GetEvent drops key/mouse/
  wheel events (SDL_QUIT and window events still pass, so a run that
  never captures stays closeable); I_PollGamepad returns early, because
  the pad is read by STATE not events and an off-centre stick would
  otherwise still steer; and both SDL_SetRelativeMouseMode calls leave
  the pointer alone. -noinput is separate because a PROFILING run is not
  a capture run -- it plays normally for tens of seconds -- and still
  seized the mouse from whoever was at the desk.

  Evidence is the code path, not a measurement: mouse input cannot be
  injected under Wayland, so the fix could not be A/B'd directly. Three
  consecutive captures agree to 0.002-0.004/255, which is consistent but
  not proof, since runs sometimes agreed before. -rtverify PASS, 7/7
  tests.

  Method lesson, and it is the expensive one: two theories were built and
  one was disproved with a diagnostic, but BOTH searched inside the
  program. The variable was a person at the desk, outside anything the
  process could observe. When a defect is intermittent and correlates
  with nothing measurable inside the system, ask whoever is present what
  they were doing -- the user answered in one sentence what the
  instrumentation could not.

  Follow-on for the DOOM-0011 measurements: every image A/B taken this
  session carries this noise, so the numbers stand only where the signal
  was far outside it. The kFogSteps 40-vs-24 result (signal 0.153) is
  safe by a wide margin; anything under ~3 MAE from earlier sessions is
  worth re-taking now that captures are input-proof.
  CONFIRMED ON HARDWARE (2026-07-30, user): "I did test moving the mouse
  at the start of the window and it no longer moved the camera." That
  closes the one gap in the evidence above -- mouse input cannot be
  injected under Wayland, so the fix could only be argued from the code
  path here. It is now verified by the instrument that was missing, which
  is the same instrument that DIAGNOSED it: a person at the keyboard.

- 📋 [DOOM-0288] **Derive useful camera coordinates from the map data so a viewpoint can be chosen without walking to it.**
  The second half of DOOM-0268. `-warpto X Y [ANGLE]` can already put the
  player anywhere, but nothing tells you WHICH X Y to ask for -- the
  coordinates still have to come from a person who walked there, or from
  reasoning over the WAD by hand. That is the remaining round-trip.

  Wanted: a diagnostic that reads the loaded level and prints candidate
  viewpoints with their coordinates and a suggested facing, so a session
  can pick one and capture it. Useful classes to surface, since each one
  is a different feature under test:
    - player starts and teleport destinations
    - open-sky sectors (the fog / sky-shaft work needs these by name)
    - liquid sectors (nukage/lava -- DOOM-0183)
    - the largest rooms, and sectors with the longest sight lines
    - doors and lifts (a mid-travel capture)
    - sectors with light-flicker specials
  Each entry wants a spot that is actually STANDABLE and a facing that
  looks INTO the room rather than at the nearest wall -- a raw sector
  centroid is often inside a pillar or facing a corner, which is why
  picking coordinates by hand keeps failing.

  Pairs with DOOM-0268's headless capture to give the golden-image
  library DOOM-0202 wants and currently has one entry of.
  **Layman:** A way to ask the game "where are the interesting spots on this map?" and get coordinates back, so a screenshot can be taken anywhere without a person walking there first.
  Kind: test.
  Source: user-request-2026-07-30.

- ✅ [DOOM-0324] **Build the Windows cross-target in CI, so it cannot rot again.**
  .github/workflows/build.yml has ONE job — "Linux build + tests" on
  ubuntu-latest. Nothing compiles the mingw target, so the Windows build
  is exercised only by packaging/release.sh, i.e. once per release.

  Found cutting 0.6.0: TWO independent Windows-only compile errors had
  accumulated across the 193 commits since v0.5.0, both from the
  DOOM-0294 developer-capture work and both invisible on Linux —
  g_game.c used int64_t without stdint.h (glibc supplies it
  transitively, mingw does not), and rb_image.c called the POSIX
  two-argument mkdir where Windows takes one. Neither is subtle;
  nothing was looking.

  A whole-tree -fsyntax-only sweep under x86_64-w64-mingw32-gcc caught
  both in seconds and needs no linking, no staged SDL2/Vulkan import
  libs and no artifact upload — so the cheap version of this is a
  syntax-check job, not a full cross-build. Add the link stage only if
  a link-stage break ever appears.

  Note d_main.c:1230 already carried the correct #ifdef _WIN32 mkdir
  split, so the pattern to follow was in the tree the whole time.
  **Layman:** The Windows version is only built when we cut a release, so it can stay broken for months without anyone noticing.
  Kind: chore.
  Lanes: ci, packaging.
  Source: in-session-2026-08-05 (0.6.0 release).
  Progress (2026-08-05): the local half is DONE — packaging/windows-smoke.sh
  now does the whole-tree mingw -fsyntax-only sweep AND boots the resulting
  .exe under Wine on a private Xvfb display, asserting the -bootsmoke line.
  Verified on this machine: the sweep is clean, and the Windows build really
  runs (Wine even passes Vulkan through to the RX 6600). It found DOOM-0325
  on its first run. Remaining work here is only the CI job — call the script
  with --syntax-only on ubuntu-latest (no wine/Xvfb needed, seconds, and it
  catches the exact class that broke 0.6.0), or install wine + Xvfb and run
  it in full against the freedoom IWAD CI already installs.
  Resolved (2026-08-05): build.yml now carries a second job,
  "Windows cross-compile check", running packaging/windows-smoke.sh
  --syntax-only on ubuntu-latest in parallel with the Linux job. Three
  things had to exist first, none of them obvious from a warm tree:
  packaging/mingw-deps.sh (stages the upstream SDL2 / SDL2_mixer / Vulkan
  headers, and is now the single source of truth for those three version
  pins -- mingw-deps/README.md defers to it); a `generated` phony Makefile
  target, because r_vulkan.cpp #includes the shader/font headers that do
  not exist on a fresh clone; and preconditions in windows-smoke.sh that
  run both. Verified on a clean-checkout copy with mingw-deps/ and every
  generated header stripped: 8 seconds, exit 0. Then verified NEGATIVELY
  -- deleting g_game.c's #include <stdint.h> (one of the two faults that
  broke 0.6.0) turns the gate red with the exact error. The link half is
  still deliberately absent, per the original bullet; the Wine boot stays
  local while DOOM-0325 stands.

- 📋 [DOOM-0325] **The Windows build hangs on exit, inside I_ShutdownMusic.**
  Reproduce in one command: packaging/windows-smoke.sh --no-build
  (exit code 3 = booted fine, never exited).

  The engine completes its work and prints "bootsmoke: 35 tics simulated
  OK, exiting.", then I_Quit -> I_QuitTeardown never returns. Temporary
  breadcrumbs through the teardown pinned it exactly:

    TD: enter        printed
    TD: net done     printed   (D_QuitNetGame)
    TD: sound done   printed   (I_ShutdownSound)
    TD: music done   NEVER     (I_ShutdownMusic hangs)

  so the hang is inside i_sound.c I_ShutdownMusic, one of Mix_HaltMusic,
  the I_UnRegisterSong loop, Mix_CloseAudio or Mix_Quit. Not yet narrowed
  past the function; another breadcrumb round would pin the exact call.

  NOT Wine: `wine cmd /c exit` returns in 2 s in the same prefix, and the
  NATIVE Linux build runs the identical smoke and exits rc=0 in 2 s
  against the Windows build's 90 s timeout. Windows-only, and it
  reproduces with -nosound as well, so it is not the sfx side keeping the
  device open.

  Player-visible: quitting the game on Windows leaves the process alive.
  Also note d_main.c's DOOM-0060 game-select relaunch calls the very same
  I_QuitTeardown before re-exec'ing precisely so the old process releases
  the audio device first -- so on Windows that relaunch likely hangs too.
  Worth checking as part of the fix.
  **Layman:** On Windows the game finishes what it was doing but the program never actually closes — it has to be forced shut.
  Kind: fix.
  Lanes: audio, platform.
  Source: in-session-2026-08-05 (found by the new packaging/windows-smoke.sh).
  Root cause found (2026-08-05) — NOT our code. Breadcrumbs narrowed the
  hang to the FIRST call in I_ShutdownMusic: Mix_HaltMusic. Mix_GetMusicType
  and Mix_PlayingMusic both return first, and both take the same audio lock,
  so the lock is free; the hang is inside SDL2_mixer's backend stop. Music
  type is MUS_MID (4) and the staged SDL2_mixer 2.8.2 DLL carries only
  NATIVEMIDI + TIMIDITY (no FluidSynth), so playback is on NATIVEMIDI --
  Windows winmm midiStreamStop/midiStreamClose.

  Proved with a 40-line standalone program containing NO DOOM code: SDL_Init
  + Mix_OpenAudioDevice + Mix_LoadMUS + Mix_PlayMusic + Mix_HaltMusic,
  cross-compiled against the same mingw-deps/prefix and run in the same Wine
  sandbox. It hangs at Mix_HaltMusic exactly as the engine does. The SAME
  binary handed a WAV instead of a MIDI runs halt/free/close/quit/SDL_Quit
  and exits rc=0. So the defect is SDL2_mixer's native-MIDI stop under Wine's
  winmm, not DOOM_Ants and not SDL2_mixer generally.

  Still UNKNOWN and blocking: whether real Windows is affected at all. Wine's
  winmm MIDI here has only ALSA 'Midi Through' to talk to. Native MIDI is
  SDL2_mixer's normal Windows path, so a hang there would be a widely-hit
  upstream bug -- which makes a Wine-only fault the likelier reading. Needs
  Charl to quit the game on real Windows and check Task Manager before any
  engine-side change is justified; there is no root-cause fix to make in this
  repo if Windows is clean.

  Corrections to this bullet's original body: (a) there is NO -nosound flag in
  this engine (no M_CheckParm for it anywhere), so "reproduces with -nosound"
  tested nothing; (b) "NOT Wine" is too strong -- it ruled out Wine being
  slow, not Wine's MIDI driver being broken.
  How to settle it on a real Windows PC (2 minutes, no dev tools): unzip a
  release build, run doom_ants.exe, let the title music play so the MIDI
  device is actually open, quit from the menu, then open Task Manager ->
  Details and look for doom_ants.exe. Gone = Windows is clean and this is
  Wine-only, close as not-a-bug. Still listed = real, and the fix belongs
  upstream in SDL2_mixer's native_midi_win32 (or in switching the Windows
  build off native MIDI). Music must have played: the deadlock needs a live
  MUS_MID song, and a run with music off will exit cleanly either way.
  Progress (2026-08-05): upstream HAS the answer, and it is
  architectural rather than a shutdown-ordering patch. Read from source,
  not recall.

  Our shipped SDL2_mixer is 2.8.2 and its DLL carries BOTH backends --
  `strings mingw-deps/prefix/bin/SDL2_mixer.dll` shows NATIVEMIDI plus
  midiOutPrepareHeader / midiOutSetVolume / midiOutUnprepareHeader (the
  Win32 midiStream API) AND TIMIDITY, and it honours the SDL_NATIVE_MUSIC
  env var. So the native path is what plays MIDI on Windows today, and a
  software path is already compiled in.

  GZDoom/ZMusic never take that path by default. `DEF_MIDIDEV -5`
  (src/common/audio/music/music_midi_base.cpp:44) and ZMusic maps -5 ->
  MDEV_FLUIDSYNTH (libraries/ZMusic/source/musicformats/music_midi.cpp
  :227-236). CreateWinMIDIDevice is reached only when snd_mididevice >= 0,
  i.e. when a user explicitly picks a hardware device (same file :282-284).
  The Windows MIDI mapper is opt-in upstream, not the default.

  And where they DO use it they engineer around this exact hazard:
  ZMusic's WinMIDIDevice::Stop() signals its own player thread's ExitEvent
  and WaitForSingleObject(..., INFINITE) BEFORE midiStreamStop /
  midiOutReset, so teardown never depends on the MIDI callback. rheit/zdoom
  's older Stop() (src/sound/music_win_mididevice.cpp:211-219) has no join
  at all -- the hazard was found and fixed between the two ports.

  Two routes for us, both small, and they are not exclusive:
    (a) Set SDL_NATIVE_MUSIC=0 before Mix_OpenAudio on Windows so SDL_mixer
        uses its Timidity backend. Needs a SoundFont/patch set to be
        audible -- verify before shipping or the fix trades a hang for
        silence, and the smoke test would not catch that.
    (b) Ship our own softsynth path as upstream does, which also delivers
        identical music on every platform instead of whatever GM set the
        player's Windows happens to have.
  Route (a) is the cheap experiment and settles whether native MIDI is
  really the deadlock: if SDL_NATIVE_MUSIC=0 makes windows-smoke.sh exit 0,
  the diagnosis is confirmed with no engine change at all.
  Correction, same day (2026-08-05), and it retracts the note above's
  route (a) AND casts doubt on this bullet's own recorded diagnosis.

  Ran it: `SDL_NATIVE_MUSIC=0 packaging/windows-smoke.sh --no-build`
  still hangs, identically to the control run immediately before it. The
  reason is that the switch was already in that position.

  SDL_mixer's music.c (SDL2 branch) gates native MIDI on
  `SDL_GetHintBoolean("SDL_NATIVE_MUSIC", SDL_FALSE)` -- **default FALSE**,
  so native MIDI is OPT-IN and is tried only after FluidSynth and Timidity.
  We never set that hint (no SDL_SetHint for it anywhere in
  linuxdoom-1.10), and the shipped DLL has TIMIDITY compiled in but NOT
  FluidSynth. So the Windows build has been decoding MIDI with **Timidity,
  in-process**, and has never touched Win32 midiStream at all.

  Which means "SDL2_mixer's native-MIDI stop deadlocks" is very probably a
  MISATTRIBUTION -- it is stated in this bullet, in the header of
  packaging/windows-smoke.sh (~line 28) and in the FAIL message the script
  prints on every run, so it greets every future session as fact. The
  standalone no-DOOM-code repro the earlier session did still stands as
  evidence the hang is inside SDL_mixer; only the named backend is wrong.

  That re-points the next diagnostic rather than removing it. I_ShutdownMusic
  now has four candidates and one of them owes nothing to MIDI:
  Mix_HaltMusic, the I_UnRegisterSong loop, **Mix_CloseAudio** (plain SDL
  audio-device teardown under Wine) and Mix_Quit. The breadcrumb round this
  bullet already schedules is still the right next step -- it just should
  not assume the answer is MIDI-shaped. Fix the script's two claims in the
  same commit as the fix, not before: replacing a wrong cause with a vague
  one buys nothing.

  GZDoom's default is still worth copying on its own merits (see the note
  above): a bundled in-process softsynth means every player hears the same
  music instead of whatever GM set their OS ships.

- 📋 [DOOM-0326] **Bump the staged Vulkan headers to 1.4.357.0.**
  packaging/mingw-deps.sh pins VULKAN_VER=1.4.350.0; Khronos has shipped
  vulkan-sdk-1.4.357.0. Checked at the same time and deliberately NOT
  bumped, to keep a dependency change out of a CI-gate commit.

  The other two pins in that file were verified CURRENT on 2026-08-05 and
  need no action: SDL2 2.32.10 and SDL2_mixer 2.8.2 are both the latest
  within the SDL2 line. Note the engine is SDL2, not SDL3 — the GitHub
  releases list is dominated by 3.x, so "latest release" there is the
  wrong answer. Filter to release-2.*.

  The fix is one variable in packaging/mingw-deps.sh, then
  `packaging/mingw-deps.sh --force` and a full `packaging/windows-smoke.sh`
  run to confirm the tree still compiles and links against the new
  headers. mingw-deps.sh is the single source of truth for all three, so
  there is nowhere else to edit.
  **Layman:** The Windows build uses a slightly old copy of the Vulkan graphics headers; a newer one is out.
  Kind: chore.
  Source: in-session-2026-08-05 (DOOM-0324 version-pin check).

- ✅ [DOOM-0327] **Honour -nosound and -nomusic on the command line.**
  Neither flag exists: there is no M_CheckParm("-nosound") or
  ("-nomusic") anywhere in the engine, so both are silently ignored and
  audio always initialises. Every DOOM port in the family (Chocolate,
  Crispy, PrBoom+) honours them, and a DOOM-0325 note in this file was
  written believing -nosound had been tested -- it had not, because the
  flag does nothing.

  Wanted: -nosound skips I_InitSound entirely; -nomusic keeps effects but
  skips the MIDI half (Mix_Init(MIX_INIT_MID) and everything downstream).
  Small and self-contained in i_sound.c + d_main.c.

  Second payoff: packaging/windows-smoke.sh could then boot with -nomusic
  and reach exit 0 under Wine, since DOOM-0325's deadlock needs a live
  MUS_MID song. That would turn the full smoke back into a usable
  pass/fail gate without hiding the hang -- keep a music-on run for
  whenever DOOM-0325 is settled.
  **Layman:** Add the standard switches that let you start the game with no sound or no music.
  Kind: feature.
  Source: in-session-2026-08-05 (found while diagnosing DOOM-0325).
  Resolved (2026-08-05): both flags land as `nosound` / `nomusic` in
  doomstat.h, read in D_DoomMain beside -nomonsters and before I_Init.
  -nosound returns from I_InitSound before SDL_InitSubSystem, so no device
  is opened and sound_ok / music_initialised both stay false; -nosound
  implies -nomusic, so music paths test nomusic alone.

  Two consequences the flags forced, both in this change:
    - s_sound.c needed guards too. With music off, I_RegisterSong returns 0
      and DOOM-0165's cold-start path reads that as a failed start: 4
      retries over 1.6 s plus a log line each, on EVERY music change. The
      sfx side would likewise print "not pre-cached - wtf?" once per sound.
      Guards in S_StartMusicInfo and S_StartSoundAtVolume.
    - I_ShutdownMusic only closed the shared device when music had come up,
      so -nomusic (and, already, a missing MIDI decoder) left it open
      through I_QuitTeardown -- which DOOM-0060's relaunch needs to release
      for the child process. It now closes on sound_ok and calls Mix_Quit
      only where Mix_Init ran.

  Verified headless (-bootsmoke 35, dummy video, doom.wad): baseline logs
  "music ready" + "pre-cached all sound data"; -nomusic logs "music
  disabled by -nomusic" and still pre-caches effects; -nosound logs
  "sound disabled by -nosound" and nothing else. All exit 0. The music
  guard was confirmed by breaking it once -- with it disabled the same
  -nomusic run logs 4 "did not start" retry lines, with it in place, zero.
  make + make test (7 suites) green, Windows --syntax-only PASS.

  NOT verified by execution: the -nosound sfx guard. A headless run never
  requests a sound effect -- a probe print in S_StartSoundAtVolume counted
  zero calls across 10 s on MAP01/MAP07/MAP12 with the player standing
  still, and input cannot be injected under Wayland. The branch is two
  lines above an unchanged body, so a play-test with -nosound would settle
  it. The second payoff (booting packaging/windows-smoke.sh with -nomusic)
  is deliberately NOT taken here -- it is a policy call about DOOM-0325's
  visibility, filed as DOOM-0329.

- 💭 [DOOM-0329] **Decide whether the Windows smoke should boot with -nomusic.**
  DOOM-0327 gives packaging/windows-smoke.sh the option: a -nomusic boot
  never registers a MIDI song, so DOOM-0325's Mix_HaltMusic deadlock cannot
  fire and the full script would reach exit 0 instead of its designed exit
  3. That turns the full run back into a usable pass/fail gate.

  The reason it was not just done: exit 3 is the only thing currently
  making DOOM-0325 visible, and a green smoke would quietly retire that
  signal. If it is taken, keep a music-on run alongside -- either a second
  invocation or a flag -- so the deadlock still has somewhere to show up,
  and revert to music-on once DOOM-0325 is settled.

  Needs a call from the user, not a code decision.
  **Layman:** Choose whether the Windows test run should start the game with music off, so it can finish cleanly.
  Kind: chore.
  Source: in-session-2026-08-05 (split out of DOOM-0327).
  Resolved (2026-08-05): NO -- the user's call. The full windows-smoke.sh
  run keeps booting with music on and keeps exiting 3. Rationale, in the
  user's terms: exit 3 is the only thing keeping DOOM-0325 visible, and a
  green smoke would quietly retire that signal. The cost of leaving it is
  nil today, because the run CI actually performs is --syntax-only, which
  exits 0. Revisit only if DOOM-0325 is settled or if the full run is ever
  promoted into CI.

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
- 🚧 [DOOM-0009] **Add hardware path tracing (Monte-Carlo GI + ray-traced shadows).**
  **Layman:** Use the graphics card to trace real light rays for accurate lighting, bounced light, and shadows.
  Kind: feature.
  Source: in-session-2026-06-11.
  Direction confirmed (2026-06-25, user): ray tracing is a SETTINGS TOGGLE (on/off), orthogonal to the art set -- it applies to BOTH the original paletted art and the HD art set (DOOM-0042). Maps onto the existing tiers as the Solid (RT off) <-> Ultra (RT on) distinction within the 3D renderer, surfaced as a clear "Ray Tracing: On/Off" option; the material/lighting pipeline stays art-set-agnostic so either art theme can be traced or not. Sequencing: tracer-first on the original art (proves the lighting), HD art (DOOM-0042) layered on its material pipeline later. User has a PBR asset library at '/mnt/Games/3D Engine Assets/' (HDRIs, PBR textures, glTF models) -- the Outdoor HDRIs are directly useful for the tracer's sky/environment lighting regardless of art set; PBR textures + models feed DOOM-0042. New sourced assets go in that folder keeping its categorisation, GPL-compatible (CC0/free) only. Design spec: docs/specs/DOOM-0009-path-tracer.md.
  Direction refinement (2026-06-27, supersedes the 2026-06-25 line above): ray tracing is NOT the Solid<->Ultra distinction. Solid and Ultra are two distinct tiers (Solid = original 1993 art + original-style lighting; Ultra = HD PBR art (DOOM-0042) + modern lighting (DOOM-0010/0011) + scene lights (DOOM-0043) + flashlight (DOOM-0044)), and ray tracing is an independent On/Off toggle available INSIDE BOTH 3D tiers. So the looks are: Classic, Solid-RT-off, Solid-RT-on, Ultra-RT-off, Ultra-RT-on. The art set is bound to the tier (not a free-floating toggle); INV-9's art-set-agnostic material mechanism is unchanged. This path tracer (DOOM-0009) delivers the shared RT-on back-end for both tiers; the tier-defining art/lighting features ship under their own IDs. See docs/specs/DOOM-0009-path-tracer.md §2 (revised 2026-06-27) and the DOOM-0042 annotation.
  Progress (2026-06-27): build step 1 (bindless materials) shipped 2026-06-25; steps 2a (RT device features) + 2b (static BLAS+TLAS) landed; design spec revised 2026-06-27 to the tier×RT model. Flipped Considered→In-progress to match the shipped/in-flight implementation.
  Progress (2026-06-27): build step 2c done — "first rays on screen". New shaders/pathtrace.comp (VK_KHR_ray_query compute megakernel) casts one primary ray/pixel against the per-level TLAS; writes a storage image the present path blits to the swapchain. Two diagnostic modes via the `~` debug toggle (rb_rtdebug, mirrors rb_wireframe): 1=intersection/normal visualization, 2=white-furnace energy check (cosine-sampled unit-radiance env, albedo 1, no occlusion — must read flat 1.0). Fully additive + RT-gated, so the Solid raster path is byte-for-byte unchanged (INV-10). Verified clean under VK_LAYER_KHRONOS_validation with the toggle forced on (~8s frames, 0 VUID). Pending: user's on-screen visual check of both modes. Next: build step 3 (NEE direct lighting).
  Progress (2026-06-27): build step 3 (direct lighting) COMPLETE. 3a textured traced view; 3b emitter extraction (E1M1: 56 NEE emitters); 3c-1 first NEE direct light (user-visually-verified — ceiling/door light pooling, ray-traced shadows, green-slime glow); 3c-2 power-importance light sampling + exposure (user-verified brighter/cleaner at 4 spp); 3c-3 unbiasedness proof (commit cc17e91 — `make nee-test`, all green: selection freq==pdf within 6σ, estimator within 0.5%, matched-integrand var~0). INV-6's GPU image rel-MSE vs a brute-force Cornell reference is deferred to step 4 (needs the complete integrator + a high-precision off-screen target). Next: step 4 — indirect GI (sector-keyed irradiance-cache bake) replacing the flat ambient fill.
  Progress (2026-06-27): build step 4 (static GI bake) COMPLETE. 4d closes INV-6 — a new megakernel verify path (pathtrace.comp mode 5) + host RB_RtVerify (`-rtverify` parm) converge the direct integrator two independent unclamped ways (power-NEE vs an all-lights brute-force reference) at a 320x200 off-screen rgba32f accumulator and assert rel-MSE <= 0.5%, plus a white-furnace throughput check. Verified E1M1 (RX 6600, validation on): rel-MSE 0.18% PASS, furnace deviation 0.000000 PASS, verify + display paths validation-clean (INV-10 held). Commit 2994674. Next: build step 5 (dynamic delta — muzzle-flash analytic light + ray-traced shadows + moving-sector AS refit).
  Progress (2026-06-28): build step 5 (dynamic delta) implemented in two parts. (1) Moving-sector AS refit (commit 4d9fe26) — resolves the step-5 hard-gate open question: the single ~2k-tri world BLAS is built ALLOW_UPDATE and refit in place (BLAS UPDATE -> AS barrier -> TLAS UPDATE) only when RB_UpdateMeshHeights reports a moving door/lift (latched dirty flag), so traced shadows track open doors; the coarse whole-BLAS refit measures under budget so instance-splitting is unnecessary. (2) Muzzle-flash dynamic light (commit 99d39b2) — pathtrace.comp mode 4 adds a positional point light at the gun barrel (eye + forward offset, so it swings as the player turns) with a hard ray-traced shadow, gated on player->extralight, composited over baked static. Both validation-clean (forced-on mode-4 tests on E1M1/RX 6600; test hooks reverted). On-screen muzzle look + the rotation-tracking verify need eyes-on (fire with `~` trace active). Flash colour/intensity/offset are inline INV-7 backfill pending a Workbench pass. Next: build step 6 (denoiser — half-res indirect + A-SVGF, then FSR2).
  Step 6 (denoiser) direction (2026-06-28, user): implement build sub-steps 6a (temporal accumulation) + 6b (spatial A-SVGF a-trous) together this session; user verifies on the RX 6600 before continuing. Upscale sub-step 6d: ship BOTH a lean custom temporal upscaler (TAAU) AND AMD FidelityFX FSR2, selectable from the settings menu (user choice). The "A" in A-SVGF (adaptive temporal-gradient anti-ghosting) rides with the later muzzle-flash ghosting verify, per spec §7 ordering (6b spatial now, flash check at 6d).
  Progress (2026-06-28): build step 6 (denoiser) sub-steps 6a + 6b landed together (the temporal -> a-trous -> composite chain only yields an image as a whole). New pathtrace.comp mode 6 writes a demodulated SVGF G-buffer (gpos worldPos+matId, gnorm, galbedo, gillum = noisy illum with albedo factored out + emission excluded) instead of a tonemapped pixel. Three new compute passes (shaders/svgf_temporal.comp, svgf_atrous.comp, svgf_composite.comp): 6a temporal accumulation reprojects last frame via stored world position + prev-camera basis, disocclusion+normal validated, EMA blend, luminance moments -> variance; 6b edge-aware 5x5 a-trous (5 iters, hole step 1..16, normal/plane/variance edge-stops, SVGF colour-feedback on iter 0); composite re-modulates albedo + re-adds crisp emission + tonemaps (same exposure/PBR-Neutral/sRGB operator as mode 4) into rtImage. Host (r_vulkan.cpp): one shared svgf descriptor set (set 2 on the trace pipeline, set 0 on the denoiser passes), 12 swapchain-sized images (recreated on resize, histories cleared so first frame finds no stale reprojection), parity ping-pong + prev-camera snapshot. `~` toggle now cycles ...->mode 4 (noisy)->mode 6 (denoised), so the two are directly comparable. Build clean (glslc + g++, 0 warnings); cannot run/validate here (no GPU/WAD) -> PENDING USER ON-HARDWARE VERIFY on the RX 6600: (1) validation-clean over a multi-second mode-6 run (INV-8); (2) noise melts when still, no edge smearing when moving (6a); (3) single-frame/fast-motion clean without over-blurring textures (6b). Tuning knobs are inline + flagged (EMA alpha floor 0.1, histLen cap 32, disocclusion 4px world-span, a-trous sigN/sigZ/sigL). NOT yet done: the adaptive "A" (temporal-gradient anti-ghosting) rides with the muzzle-flash ghosting verify; half-res indirect (6c); upscale 6d (both TAAU + FSR2, menu-selectable).
  Progress (2026-06-28, step 6 fix): user HW test showed mode 6 ~identical to the noisy mode 4 (denoiser a no-op). Root cause (systematic debug + confirmed in source): the SVGF shaders index storage-image ARRAYS by a runtime parity (gpos[cur], atrous[ping], hcol[prev]), which requires the shaderStorageImageArrayDynamicIndexing device feature -- it was NOT enabled (only the sampled-image descriptor-indexing for bindless materials was). Without it the index collapses to 0 on RADV, degenerating BOTH the temporal and a-trous ping-pong at once (exactly the symptom). Fix: enable shaderStorageImageArrayDynamicIndexing in PickPhysicalAndDevice (gated on support; warn if absent). Build clean; PENDING user re-test on the RX 6600. Web-research pass (SVGF/A-SVGF best practices) confirmed the rest of the design is sound: albedo demodulation, feeding the FIRST a-trous iteration (not the final) back as history to avoid growing-blur, disocclusion+normal reuse (not colour clamping), firefly clamp -- all already implemented correctly. Also added (user request): an on-screen top-centre mode title (new label.comp + CreateLabelPipeline, reuses svgfDsLayout binding 7, embedded 5x7 font, stamps the active ~ mode name into rtImage before the blit since the trace path skips the HUD) + a stdout "RT debug mode N: <name>" print on each ~ switch. Makefile test infra also reworked (incremental + parallel + -O2 + auto header-deps; `make test` aggregate).
  Progress (2026-06-28, step 6 ROOT CAUSE + HW-verified): the storage-image-array feature enable was necessary (the a-trous/temporal ping-pong indexes image arrays at runtime) but NOT sufficient -- mode 6 stayed a no-op. Systematic debug: a forced-blur diagnostic (all a-trous edge-stops = 1) proved the whole pipeline (dispatch + array indexing + composite read + barriers) sound, isolating the bug to the variance/edge-stop math. Actual root cause: the mode-6 feed used a frozen, pixel-only seed (px.x + px.y*w + 1), so every frame produced the IDENTICAL noise -> (a) temporal accumulation averaged identical values = no convergence, (b) temporal variance = 0 -> the a-trous luminance edge-stop collapsed to identity. A frozen seed defeats SVGF wholesale. Fix: fold the frame counter (host g.svgfFrame via pc.misc3.x) into the feed seed through pcgHash so each frame is an independent sample. User-verified on the RX 6600: mode 6 is now clearly cleaner than mode 4 with edges/textures crisp and grain melting to a clean image when still. 6a + 6b COMPLETE. Diagnostic scaffolding removed. Still pending: adaptive "A" anti-ghosting (with the muzzle-flash verify), 6c half-res indirect, 6d upscale (TAAU + FSR2).
  Progress (2026-06-28): build step 6 adaptive anti-ghosting (the "A" in
  A-SVGF) implemented in svgf_temporal.comp — a noise-aware temporal-
  luminance gradient collapses history weight on a sudden light change
  (muzzle flash on/off) so stale values can't trail, while ordinary
  Monte-Carlo noise leaves history intact. Shader-only, builds clean;
  visual acceptance pending a playtest. Also recorded the §4.4 player-
  selectable upscaler decision (TAAU / FSR 2 / FSR 3.1; frame-gen split
  out to DOOM-0088). Next: 6-c half-res indirect, then 6-d upscalers.
  Progress (2026-06-28): build step 6c half-res lighting implemented.
  Finding: the spec's "half-res indirect" target was already removed by the
  step-4 baked-GI cache (no live indirect bounce); the live cost is the
  direct/NEE shadow rays. User chose to half-res that. Shader-only (no host
  change): pathtrace.comp mode 6 casts the NEE + flash shadow rays for one
  even/even pixel per 2×2 block (gillum.a flags samples), G-buffer + albedo
  stay full-res; svgf_temporal.comp joint-bilateral-upsamples the other
  three from the grid samples, edge-guided by the full-res G-buffer, before
  the à-trous. Spec §4.4/§7 reconciled + cold-eyes'd. Builds clean; visual
  acceptance pending playtest. Next: 6-d upscalers.
  Progress (2026-06-28): build step 6-d (upscale) PHASE 1 landed — the in-engine TAAU plumbing the spec §4.4 names first (FSR 2 / FSR 3.1 are later phases on the SAME contract). Three parts: (a) pathtrace.comp jitters the primary ray by a Halton(2,3) sub-pixel offset (carried in camPos.w/camDir.w; 0 on the non-upscaled path, so it is a no-op there); (b) svgf_composite.comp writes a render-res motion-vector image (rg16f, set-2 binding 8) by reprojecting each pixel's world point through the prev-frame camera — the FSR-ready MV contract, reusing the prev-camera basis already in its push constants; (c) new shaders/taau.comp reconstructs the full DISPLAY image from the render-res denoised colour + MV + this frame's jitter (bilinear un-jittered current sample, motion-reprojected history, 3x3 neighbourhood colour-clamp anti-ghosting, history-weighted blend) into a display-res output the present path blits. Host (r_vulkan.cpp): SV_MOTION added to the SVGF descriptor set; a dedicated TAAU descriptor set + compute pipeline + display-res history[2]/output images; a labelTaauDs so the debug mode label stamps on the upscaled image. When active the trace + SVGF dispatch into a render-scale sub-rectangle of the (display-sized) storage images and TAAU upscales — no image-resize on scale change. Gated to the mode-6 denoised path with Upscaler=TAAU; modes 1-5 and mode-6 with Upscaler=Off are byte-identical (default Off). Menu: a new Renderer sub-menu hung off the Options "Renderer" row (the main Options menu is full at 320x200) — Renderer / Upscaler (Off, TAAU) / Render Scale (100/75/67/50%); persisted via m_misc.c (upscaler, render_scale). Build clean (glslc + g++, 0 warnings); cannot run/validate here (no GPU/WAD) -> PENDING USER ON-HARDWARE VERIFY on the RX 6600: (1) validation-clean mode-6 run with Upscaler=TAAU at 100% then 75/67/50%; (2) no jitter shimmer when still; (3) upscaled image holds edges/detail without obvious smearing when moving; (4) Off path unchanged. NOT yet done: jitter-aware variance clipping + depth/disocclusion confidence (tuning rides the step-7 perf pass); FSR 2 / FSR 3.1 backends (later 6-d phases); an optional linear/HDR upscale tap point if an FSR backend prefers it.
  Research note (2026-06-28, verified — docs/research/DOOM-0009-rt-denoiser-upscaler-bestpractices.md): findings that shape the 6-d FSR phases. (1) FSR 2's input contract is strict: three mandatory render-res buffers — colour + depth + motion vectors — plus an optional reactive mask + exposure for best quality, and the order is denoise-then-upscale (which the TAAU plumbing already follows). Convention is load-bearing: colour + depth are JITTERED, motion vectors are NOT jittered by default. Our TAAU motion vectors are computed geometrically (worldPos -> prev camera) and are already un-jittered -> FSR2-correct as-is. GAPS to close in 6-d phase 2: we do NOT yet produce a render-res DEPTH image (the G-buffer stores worldPos, not depth) — FSR2 needs one; and we need a reactive mask (sparse for DOOM — mostly opaque; the mask is for alpha/particle content that doesn't write depth/MVs, NOT for opaque emissive geometry) + an exposure input. (2) AMD FSR Ray Regeneration (the ML denoiser) is UNAVAILABLE on this stack — it requires RDNA4 (RX 9000+), DX12 + SM 6.6, Windows 11, and is DX12-exclusive (no Vulkan). So the custom SVGF/A-SVGF + TAAU->FSR2 path is THE route; do not pursue RR on the RX 6600. (Time-sensitive: FSR SDK support evolves — RDNA3 upscaling landed in SDK 2.3, RX 6000 upscaling slated ~2027 — re-check before an FSR milestone.) (3) Validated, no change needed: the inline ray-query compute megakernel choice (the wavefront-beats-megakernel claim was refuted 0-3 for low-divergence matte art), and the SVGF demodulate-before/remodulate-after + A-SVGF adaptive-alpha design are all confirmed canonical. Note: A-SVGF's adaptive alpha targets LIGHTING-change ghosting; geometric disocclusion is still the inherited depth/normal/mesh-id reprojection test's job. The dedicated perf + AS + lighting + security follow-ups are now DOOM-0090..0093.
- 💭 [DOOM-0010] **Add dynamic lighting.**
  **Layman:** Let lights move and react — muzzle flashes, flickering lamps — lighting the scene live.
  Kind: feature.
  Source: in-session-2026-06-11.
  Scope clarification (2026-06-27, user): the muzzle flash must light up the room in BOTH 3D tiers (Solid AND Ultra), with ray tracing ON or OFF. RT off → a raster-lit flash (the room brightens, no cast shadows); RT on → the DOOM-0009 path tracer additionally casts the flash's ray-traced shadows + bounces. So muzzle-flash illumination is tier- and RT-agnostic (it is NOT an Ultra-only or RT-only effect); only the shadow quality scales with the RT toggle. Distinct from the pitch-black-room handling (DOOM-0043, Ultra-only): a flash lights any room momentarily regardless of tier.
- 🚧 [DOOM-0011] **Add volumetric lighting (god rays).**
  **Layman:** Visible shafts of light through smoke and doorways.
  Kind: feature.
  Source: in-session-2026-06-11.
  Spec written 2026-07-23: docs/specs/DOOM-0011-volumetric-lighting.md — RT single-scattering volumetrics (god-ray shafts + coloured height/area fog). Scope widened per user 2026-07-23: gated on RT ENGAGED (rb_rtdebug 4/6), so it covers BOTH Solid-RT and Ultra-RT, not Ultra alone. Sky + big-static emitters only (no dynamic/muzzle/flashlight); adds the engine's first directional "sun" vector for shafts; half-res + dithered + denoised; cheap&smooth present-total gate (originally ≤5%; raised to ≤15% by the 2026-07-25 amendment); rb_fog 0..3 dial + `;` hotkey + both menus. Reuses misc6.z/.w (the last 2 free RT push lanes). Companion item DOOM-0238 = the FAKED rasterised-view version (Solid/Ultra "Original"), user chose "RT first, fake follows" + "match RT as closely as possible" — deferred. Cold-eyes CONVERGED after 4 loops (rule 14): loop1 HIGH1/MED5, loop2 MED3, loop3 HIGH1/MED2, loop4 MED1 — all verified & fixed (full log in the spec's Cold-eyes section); reviewer verdict "genuinely tight". NEXT: user sign-off on the spec → writing-plans → implement (L1-L6). Flipping 💭→🚧 (spec landed).
  Progress (2026-07-25): L1 (e7753b3, uniform-haze skeleton) and L1b
  (1345c92, open-sky placement gate + sky-backdrop aerial fog) are
  IMPLEMENTED and user-play-tested — "looking fantastic... covers the
  mountains... outside and not inside". The 2026-07-24 open-sky amendment
  cold-eyes-converged in 3 loops (log in the spec).
  Progress (2026-07-26): cold-eyes loops 4 and 5 run on the
  2026-07-25 SH2 amendment; all verified findings fixed and committed
  (d2c0eb4, 18aef66, pushed). NOT yet converged — the /cold-eyes
  --max-loops cap of 5 is reached and loop 5 still returned 3 CRITICALs,
  so a loop 6 is owed before L1c is implementable.
  Progress (2026-07-26, cont.): cold-eyes loop 6 run (2 Sonnet lanes,
  citations out of scope, ~304k) — 2 CRITICAL, 5 HIGH, 3 MEDIUM, 2 LOW,
  0 unverified; 15 fixes in 2420117. Four of the seven worst findings
  were defects in loop 5's own sigma-split fix (undefined gooMult, dead
  densMul, wisp with no owning task, an invented kGooDensityMul) — the
  third consecutive loop where the previous loop's fixes seeded the next
  loop's findings. Two further ripples were caught only by the fix
  ledger's standing greps, not by either lane. Real new findings: the
  L1c budget reservation never subtracted L1d's own 1% seep tap; §4.5
  promised composable profiles but gave no mediumTint combination rule
  (now: densities add, tints multiply); INV-4/5/10/11 gained falsifiers;
  INV-12 now states the two guards that make it true.

  Then, on the user's call, the plan's largest gap was closed rather
  than looping again: **Tasks L1c and L1d are WRITTEN** (108fe9e, 8 steps
  each). All twelve invariants are now pinned to tasks. Writing them
  made a dozen statements stale (banner, file table, L2/L4 cross-refs,
  self-review coverage) — all chased. Two further stale topic sentences
  surfaced, both the same defect: a fix changed a passage's conclusion
  and left its opening line asserting the old one (§4.3a "the graph is
  over SECTORS" vs step 1's portals; §5 "need their OWN descriptor set"
  vs its own set-0 resolution).

  STATUS: cold-eyes has NOT converged (6 loops, cap of 5 passed — each
  further loop is an explicit user call), and L1c/L1d have not had a
  cold read. NEXT: implement L1c then L1d. Full audit trail in
  docs/specs/DOOM-0011-fix-ledger.md.

  Loop 4 (6 lanes): CRITICAL 15 / HIGH 24 / MEDIUM 34 / LOW 34 / INFO 8.
  Two classes dominated. (1) The seep's traversal was broken twice over:
  vanilla DOOM has no minisegs (P_LoadSegs gives every seg a linedef), so
  the specified SUBSECTOR graph leaves every multi-leaf room disconnected
  and `d` cannot propagate inward from a doorway at all; and resolving `d`
  per graph node rather than per grid cell reproduces exactly the abrupt
  room-boundary step the section rejects as option (C). Also unhandled:
  self-referencing sectors, levels with no sky, the unreachable sentinel
  (R16F +inf under a zero bilinear weight yields NaN), map-extent
  overflow, and the sampler address mode. (2) EVERY file:line citation had
  rotted again — r_vulkan.cpp by +4..+6 and pathtrace.comp by +2, because
  both files grew under DOOM-0254/0263 AFTER loop 3 re-anchored them. 50
  citations re-verified against source and corrected; three landed on
  unrelated constructs. Plus: L2's sky term never said whether it replaces
  or adds to L1's shipped flat sky ambient (it must replace — the other
  reading double-counts); kSunDir was called "new" though already declared
  at pt_common.glsl:42; L1 shipped a plain bilinear, not the "bilateral
  upsample" §7 credited it with; L1b's spot-check reused the whole-feature
  15% gate; L1c bound two thresholds (8% and 15%) to one decision; the
  menu list was one edit short of compiling. The implementation plan
  carried 9 CRITICALs of its own.

  Loop 5 (2 narrowed lanes, citations excluded): CRITICAL 3 / HIGH 2 /
  MEDIUM 2. Two of the three CRITICALs were defects in LOOP 4's OWN FIXES
  — the argument for re-reading cold. (i) Moving the graph to sectors
  dodged the miniseg problem but a sector-indexed Dijkstra settles one
  distance per sector, i.e. the per-node value the next step forbids; the
  search state must be the PORTAL. (ii) "Border cells are dMax by
  construction" was asserted, not made true. (iii) In the plan, L4 was
  charged with replacing the sigma-times-skyExposure form and never did,
  so roofed goo/hell rooms would keep ~5% of intended density while the
  play-test passed weakly on a thin green tint.

  LESSON worth carrying: line-number citations in a 1340-line spec against
  a 9000-line moving source re-rot on every code change — this is the
  third loop-pair to "fix citation drift". Loops 2/3 fixed it; DOOM-0254
  and DOOM-0263 then shifted the lines again. Consider anchoring to symbol
  names + quoted code rather than bare line numbers.

  NEXT: loop 6 (user decision — the cap was hit), then write the L1c/L1d
  tasks the implementation plan still lacks, then implement L1c.

  2026-07-25 amendment (user, with Silent Hill 2 named as the art target and
  reference screenshots): (1) SH2 look — new spec §4.3b, near-white colourless
  kFogColor replacing the cool-blue SKY_COLOR as the CLEAR-profile base, base
  density ~2x, kFogSteps 24 -> ~40, and two octaves of drifting 3-D noise
  modulating density (researched how SH2 actually does it: distance fog plus
  two combined layers; the two-octave design is an analogy, not a derivation);
  (2) outdoor-proximity seep — §4.3a's binary indoor gate becomes a graded fade
  driven by a load-time distance field flood-filled through CONNECTED OPEN
  SPACE only; (3) coloured fog "only where it makes sense" — the near-white
  base is the clear profile, §4.5's mediumTint still multiplies it, so hell
  reads red and goo green.

  Perf gate RAISED 5% -> 15% present-total (user decision; the PS2-comparison
  caveat and the 15%-vs-60-FPS-floor conflict are both recorded in §6).
  docs/standards/performance.md updated to match.

  Build order gains L1c (SH2 look) + L1d (seep). INV-9 amended, INV-11/INV-12
  added, Q16-Q22 added. Two new sampled images (startup-generated 3-D noise
  volume + per-level 2-D distance field) which need their OWN descriptor set —
  neither set 1 nor set 3 can be appended to (both end in a variable-count
  bindless array). No new push lane (drift reuses misc6.x).

  Cold-eyes loop 1 (3 lanes, 2026-07-25): CRITICAL 1 / HIGH 10 / MEDIUM 15 /
  LOW 12. Headline: skyExposure multiplied areaMult, which would have zeroed
  goo/hell/torch fog in every roofed room — i.e. it would have broken the
  Hell-colour wrinkle by construction. Also: the seep's connectivity test was
  "not one-sided", but a closed DOOM door IS two-sided, so fog would have
  poured through every shut door; the profiler key is `\` not `` ` ``; neither
  descriptor set is appendable; kWispWeight2's "SH2 90/128" justification was
  wrong twice over. Side-effects: docs/standards/renderer.md misc6 ledger was
  stale for already-shipped code (rb_fog on .z) — fixed; DOOM-0183's spec still
  claimed .z/.w reserved — fixed.

  NEXT: further cold-eyes loops to convergence, then bring
  DOOM-0011-implementation-plan.md up to date (it has no L1c/L1d tasks, still
  carries the ≤5% gate, and its L2 task uses the custom-index-2 sky test that
  the 2026-07-24 amendment already corrected), then implement L1c.
  Progress (2026-07-30): **L1c IMPLEMENTED** (b3ca70d) — near-white
  kFogColor at all three in-scatter sites, and two octaves of drifting 3-D
  value noise multiplying density. Build + tests green, -rtverify PASS
  (rel-MSE 0.0796 %, white furnace 0.000000). AWAITING USER PLAY-TEST.

  Measured (RX 6600, E1M1, Ultra RT, HD art loaded, dial the only
  variable): fog off 42 fps / 19.07 ms GPU, fog on 39-40 fps / 20.72 ms =
  **+8.6 % GPU, ~+6 % frame time** for the WHOLE feature — inside the 8 %
  ceiling, up from the +4 % DOOM-0276 left. The blocked banner on L1c in
  the plan was stale: §6 recorded that +4 % on 2026-07-27.

  **Three of the spec's starting values did not survive contact**, each
  judged on a captured frame:
    - kFogBaseDensity 0.0033 -> 0.0066 REVERTED. The doubled layer
      saturates, and a saturated medium cannot show billows at all —
      thickness and structure pull against each other. User's call on the
      day was billows over bulk.
    - kWispFreq1 1/512 -> 1/192. At 512 one noise cell spanned the whole
      view and the march integrated it to a flat wash.
    - An odd, mean-preserving S-curve on the noise + a 2.5x vertical
      squash, and kWispAmp 0.6 -> 1.0. Ray integration averages ~10
      samples back toward the mean: measured, the raw signal reached the
      pixel as a 6 % swing (MAE 4.0 vs wisps-off). After: MAE 13.0, peak
      128/255.

  Method worth keeping: **an A/B against the same build with kWispAmp = 0,
  scored as MAE, turns "can you see it?" into a number.** A still frame
  could not answer it and neither could argument.

  Also found + fixed in the same pass: the shot modes inherited DOOM-0183's
  WALL-CLOCK ripple time, which the wisps now ride — so every -shotcompare
  capture was a different image and the golden gate was quietly meaningless.
  Pinned under rb_shotverify.

  STILL OPEN on L1c: kFogSteps 24 -> 40 was kept on the banding hypothesis
  and has NOT been falsified (the plan says revert it and bank the budget
  if 24 reads clean with wisps on — that wants a moving picture). L1c/L1d
  have still had no cold read.
  Progress (2026-07-31): L2b (DOOM-0289) shipped and play-tested --
  the fog is back inside budget at +3.2% and 42 fps, and the user
  signed off both it and L1c's look ("damn, I do love the fog").
  L1c's Step 7 play-test is therefore also satisfied: near-white,
  colourless, billows drifting. Remaining owed on L1c is unchanged --
  kFogSteps 40 -> 24 has still not been falsified, and neither L1c nor
  L1d has had a cold read.

  The same play-test set Task L3's weighting, which the task text did
  not previously fix: the user asked for a light near the fog to affect
  it "a little bit outside but a lot more inside". That ordering is
  physical rather than arbitrary -- outdoor air is already carrying the
  sky's in-scatter, so a torch is a small addend against a large term,
  while roofed air (once DOOM-0292 gates the sky share on sky exposure)
  has almost nothing else lighting it and the same torch dominates. So
  L3's torch gain needs NO indoor weighting at all -- see the
  correction appended below (2026-08-01). This line first said to invert
  DOOM-0292's curve, which double-counts. Still build DOOM-0292 first.
  CORRECTION (2026-08-01) to the note above, from a design discussion
  with the user. It asked for a formula that makes a placed light read
  faintly outdoors and strongly indoors. There is no such formula to
  write: LIGHT ADDS, and once DOOM-0292 makes the sky's contribution
  honest under a roof, the ratio falls out of plain addition. Outdoors
  the sky term is large and a torch moves it a few percent; indoors the
  sky term is now small and the same torch dominates. Same number, no
  indoor multiplier, no inverse curve.

  So the earlier instruction to scale L3's torch gain by DOOM-0292's
  curve inverted is WRONG and must not be built -- the contrast is
  already paid for by the sky term being gated, and boosting the torch
  on top would count it twice. An explicit indoor-boost knob is legal as
  TASTE, defaulting to off; it is not the mechanism.

  What L3 does still need a real formula for, and what the Formula
  Workbench (/mnt/Games/Scripts/Linux/3D_Engine/) is the right tool for:
  the falloff SHAPE. Brightness / distance^2 x phase x transmittance
  blows up when a fog sample sits inside a light, so the softening curve
  is a design choice, not a physics fact. Sun/Ramamoorthi/Narasimhan/
  Nayar, SIGGRAPH 2005, "A Practical Analytic Single Scattering Model
  for Real Time Rendering" integrates single scattering from a point
  light in closed form and reduces it to a small lookup table -- the
  reference to reach for if the per-sample torch loop measures expensive.

  THE REAL RISK IN L3 IS NOT THE FORMULA, IT IS VISIBILITY. The task
  text says nearest-few static emitters with NO occlusion test, so a
  torch lights fog through a wall. That was tolerable while roofed air
  was uniformly bright; after DOOM-0292 a leak through a wall is the
  BRIGHTEST thing in a dark room. A ray per light per sample is exactly
  what DOOM-0289 just deleted, so the cheap proxy to try first is
  sector-scoped light lists -- only let an emitter touch air in its own
  sector, reusing the per-subsector nearest-N lists the raster path
  already builds (RebuildStaticPointLightCache).

  User's framing question, worth keeping because the answer is not
  obvious: does consolidating lighting features into one formula make it
  cheaper? Half. Consolidating the DATA is where every win in this
  feature came from -- one field now answers four questions. But on a
  GPU a general formula holds every case's live values in registers for
  every pixel, cutting occupancy, and this project has the measurement:
  DOOM-0289 found L2's ray cost 2.40 ms/frame with fog switched OFF.
  DOOM-0129 does the opposite deliberately -- one compiled pipeline
  variant per view mode so unused branches are dead-stripped. The rule:
  consolidate the thinking, specialise the code, and prize the kind of
  consolidation that lets you SKIP work (the "light budget" idea) over
  the kind that merely removes duplication.
  STANDING CONSTRAINT set by the user 2026-08-01, and it applies to
  every remaining fog task (L3, L4, DOOM-0292, DOOM-0293) rather than to
  any one of them:

    "Whatever we can do cheaply without losing the visual bang, let's do
     it. For consistency though, I was hoping we apply it once and it
     looks right across all maps in both games."

  Two rules fall out, and the second is the one that constrains design
  rather than effort:

  1. Cheap is preferred, but not at the cost of the effect. The order to
     evaluate options in is: does it read? then, what does it cost?
     -- not the reverse. DOOM-0289 is the model (the cost went to ~0 and
     the picture was held identical to 0.05 MAE).

  2. PREFER DERIVED OVER HAND-PLACED -- a DEFAULT, not a ban (clarified
     2026-08-01, below). Derived = computed from the WAD at load. This is
     already how the feature works and it should stay that way: the seep
     distance, the open-sky mask, the sun clearance and fogFloorZ are all
     computed from the map's own geometry, which is exactly why they hold
     on maps nobody has looked at. It also rules out the industry's usual
     answer for localised fog -- hand-placed volumes -- a poor DEFAULT for 32 + 36
     maps across two IWADs, let alone custom WADs.

     The corollary for CONSTANTS: prefer ones expressed relative to
     something the map supplies (fogFloorZ, a sector's own floor, the
     level's highest liquid surface) over absolute world numbers, and
     distrust any dial that had to be re-tuned to make a second map look
     right -- that is the signal that the quantity underneath it is the
     wrong one.

     The corollary for TESTING: the -shotverify gate set must span BOTH
     IWADs, since doom2.wad carries flats, sector shapes and outdoor
     scales that doom.wad never exercises (DOOM-0293's flat inventory is
     the first case found -- 16 SLIME flats that exist only in DOOM 2).
  CLARIFICATION (2026-08-01, same day, user rephrasing the constraint
  above because the note had hardened it into a ban it never was):

    "I would like it to be possible to do something once and it applies
     everywhere. But in games I know that isn't always possible as we
     would probably need significantly more powerful hardware to apply
     real physics to the world. But we don't have that hardware and
     people who will be playing this game probably don't have that
     hardware either. So, let's not throw out the hand placed items but
     only where it is feasible and makes sense to do, let's try the
     approach of apply one to everywhere."

  So the rule is a PREFERENCE ORDER, not a prohibition:

    1. Derive it from the map if that is feasible and gives the look.
    2. If it is not feasible -- or a derived version would cost more than
       the hardware has -- hand-place it, deliberately, and say so.

  Hand-authored data is a legitimate engineering answer here, not a
  failure. The reason to reach for derivation FIRST is coverage across
  32 + 36 stock maps plus custom WADs, and the reason not to insist on it
  is that a fully derived answer sometimes means simulating what the
  hardware cannot afford. The user is explicit that the target machine is
  not a workstation.

  Practical read for the fog tasks: keep deriving where it is already
  working (seep, open-sky mask, sun clearance, fogFloorZ, and
  DOOM-0293's liquid map -- all cheap because DOOM is flat-mapped and
  the answers are 2-D). Do NOT contort a design to avoid a hand-placed
  list where one is genuinely the cheap correct answer -- a per-map
  override table, a hand-tuned constant for one Hell level under L4's
  area profiles, or a placed volume for a set-piece would all be
  acceptable if the derived route is expensive or does not read. What is
  NOT acceptable is a derived mechanism that only works because someone
  quietly tuned it against one map (see the corollary above: distrust a
  dial that had to be re-tuned to make a second map look right).
  Progress (2026-08-01): **Task L3 IMPLEMENTED** (7f78f02) -- static
  emitters now light the fog. Step 1 (height pooling) shipped early on
  2026-07-27; this is Step 2, and it did NOT take the shape the plan drew.

  The plan's sketch scanned every static emitter per march sample and took
  the nearest few with NO occlusion test. Both halves were replaced:

    - Occlusion is not optional after DOOM-0292. The plan wrote the
      no-occlusion form as "Q2 start cheap", but gating the sky's ambient
      share means a torch leaking through a wall is now the brightest
      thing in a dark room. The user flagged this as L3's real risk on
      2026-08-01 and was right.
    - The per-sample scan goes with it. WHICH cells of air can see WHICH
      emitters is baked once at level load, onto the seep grid DOOM-0276/
      0289 already established, using DOOM's own line-of-sight test
      (P_CheckSight's BSP walk, factored out as P_CheckSightTrace so it
      takes points rather than mobjs). Runtime: one buffer read per fog
      sample, no rays. Set 0 gains binding 6; no new push lane (INV-5
      holds, the struct is still 240 bytes).

  The user's suggested proxy -- sector-scoped lists off the raster path's
  RebuildStaticPointLightCache -- was considered and NOT taken, for a
  reason worth keeping: DOOM sector boundaries ARE walls, so scoping to a
  sector pops hard at every one, and that cache is keyed by SUBSECTOR,
  which a fog sample in mid-air has no cheap way to look up. The seep grid
  already answers "what is at this XY". Same idea, better-indexed home.

  No indoor weighting was built, per the 2026-08-01 correction.

  Two measurement lessons, both expensive:

    1. SET THE GAIN AGAINST THE MEDIAN LIGHT, NOT THE BRIGHTEST. E1M1's
       clustered intensities run 0 / 7987 med / 83741 -- one nukage
       cluster is 17x an ordinary wall panel. Tuned against the maximum,
       an A/B against the same build with the gain at 0 scored MAE
       0.001/255: the noise floor, i.e. the whole feature invisible. A
       deliberate 1000x gain then proved it had been wired all along. The
       method is L1c's own -- A/B vs the same build with the constant at
       0, scored as MAE.
    2. A DEFAULT VIEWPOINT IS NOT A TEST. The first three A/Bs were shot
       at the E1M1 spawn, which faces the courtyard and no light at all;
       the effect measured as nothing there while working 200 units away.
       -warpto (DOOM-0268), aimed at a light read out of the bake's own
       log, is what made the measurement honest.

  Measured, RX 6600, Ultra RT, HD art loaded:
    - Nukage courtyard, L3 on vs gain 0: MAE 12.7/255, 71% of pixels
      moved, frame mean 157.3 -> 170.0. The pool glows green into the air.
    - megakernel 10.11 -> 11.16 ms, 58 -> 55 fps. Fog is then ~9% of
      present-total, inside the 15% gate, but L3 is now its most
      expensive piece -- DOOM-0295.
    - Bake 4.4 ms / 7788 sight tests / 467 of 1085 air cells lit on E1M1;
      1.3 ms / 48 lights on doom2 MAP01. Both IWADs exercised per the
      standing constraint, and their medians agree closely (7987 vs 9900)
      -- the evidence one global gain can serve both.
    - make test 7/7; -rtverify PASS 0.0796% unchanged on doom.wad. It
      FAILS on doom2.wad at 3.4943%, PROVEN pre-existing by a stashed
      rebuild -- DOOM-0297.

  AWAITING USER PLAY-TEST, and it carries DOOM-0292's open question with
  it: whether kIndoorSkyLight stays at 0.45 now torches give light back
  locally. Sheets rendered at 0.45 / 0.25 / 0.0 with L3 on. Also unjudged
  on hardware: whether the strong glow off nukage/lava is wanted at that
  strength, those clusters being ~10x a wall panel by construction.

  Plan Steps 3 and 5 are done (build/tests, commit). Step 4 (play-test)
  and the acceptance row are the user's -- including "dynamic/muzzle/
  flashlight do NOT scatter", which holds by construction: the bake reads
  only [0, staticN). Known limit: DOOM-0296 (a door opening mid-play does
  not re-bake).
  LOOK DECISION (user, 2026-08-01), after comparing our fog side by side
  with the PCSX2 Silent Hill 2 reference captures: "Up close Silent Hill 2
  fog is lighter than what we have in DOOM but I prefer what we have in
  DOOM."

  So the near-field density is SETTLED and stays where it is. Do not thin
  kFogBaseDensity, kFogPoolHeight or kFloorFogDensity toward the
  reference; the difference was seen, and ours was chosen.

  **This scopes the whole SH2 reference, which is the part worth carrying
  forward.** Silent Hill 2 is the art target for the fog's MOTION and its
  COLOUR -- near-white, colourless, billows that dissipate and reform in
  place (DOOM-0300 measured that at 82-84% large-scale structure and ~0-1%
  translation, across two samples). It is NOT the target for the density
  curve. A future session that captures the reference and "converges on
  it" wholesale would thin the near field and undo a decision the user
  made with both images in front of them.

  Two earlier decisions this ratifies rather than changes:
    - L1c tried kFogBaseDensity 0.0033 -> 0.0066 (the "roughly twice as
      thick" half of the 2026-07-25 amendment) and REVERTED it, because a
      saturated medium cannot show billows at all. Thickness and structure
      pull against each other; the user chose billows then and has now
      chosen the resulting near-field density explicitly.
    - The user's 2026-07-27 "outside I want the fog much, much thicker and
      higher" was delivered through kFogPoolHeight 18 -> 112, deliberately
      NOT through kFogBaseDensity. Same separation, same reason.

  DOC DEBT this exposes, flagged rather than fixed here because editing
  the spec pulls in the rule-14 gate: docs/specs/DOOM-0011-volumetric-
  lighting.md still states, in the §6 brightness passage, that "L1c then
  plans to move the tone to a near-white kFogColor and roughly DOUBLE
  kFogBaseDensity", and warns against lowering density because "the ~2x
  raise and the wisps depend on it". That raise was tried and reverted on
  2026-07-30. The passage now describes a plan that does not exist and
  argues from a constant that was never adopted. Correct it -- and fold in
  this decision -- next time that spec is opened for the gate.
  Progress (2026-08-03): L4 AREA PROFILES IMPLEMENTED — goo-green +
  hell-red, built to DOOM-0310 §4.1/§4.4/§4.6 rather than to the plan's
  three stale steps. Build + tests green; -rtverify PASS (INV-6 rel-MSE
  0.2059% vs the 0.50% bar, white furnace exact). AWAITING USER LOOK
  SIGN-OFF; nothing is blocked on it.

  Shape: `rb_view_t.hazeDensity` (r_mesh.h) <- r_backend.c's per-level
  rule beside `view.skytexnum` -> `g.lastView.hazeDensity` bit-cast into
  `pc.misc6[3]` -> `uintBitsToFloat` in marchFog. Goo is primary-hit
  keyed through a NEW `FogHit.ctrlFlags` carrying `MatCtrl.flags`.
  `kAreaDensity` = 0.0020 (pt_common.glsl), `kHazeDensityDefault` =
  0.0010f (r_backend.c) — both first guesses, Q7/Q20 dials.

  CORRECTION to this bullet's own L4 pre-flight: it said `mc` is NOT in
  scope at either FogHit call site. It IS — `MatCtrl mc = ctrl[id];` at
  pathtrace.comp:1436 (mode 4) and :1620 (mode 6), both used again at
  :1546 / :1675, above the two sites. The widening was a one-token edit,
  not the budgeted plumbing job. Everything else in the pre-flight held.

  The fire-sky disjunct of §4.5's hell rule is deliberately NOT built:
  vanilla picks skytexture from gameepisode/gamemap in G_DoLoadLevel, so
  it can never fire where the two implemented disjuncts do not.

  MEASURED, pre-L4 worktree vs HEAD, same view, same config, quiet box:
  - E1M1 spawn (no profile in view): MAE 0.004 against a same-build
    control of 0.003 — inert, i.e. §7's byte-identity row as closely as
    this harness can put it. NOTE the harness is NOT frame-deterministic:
    the same build captured twice differs (md5 differs, MAE 0.003), so
    literal byte-identity is unobservable here and the claim rests on the
    algebra (`x + 0.0 == x`, `kFogColor * vec3(1.0)`) plus this bound.
  - E1M1 roofed nukage (sector 53): MAE 1.42, 28.6% of px, shift
    (-1.21, +0.67, -2.25) = green-ward, under a roof.
  - E1M1 OPEN-SKY nukage (sector 0): MAE 6.89, shift (-8.42, +0.29,
    -11.93); 0.000% of px above 250 on both builds and mean luma FELL, so
    §7's wash-out risk did not materialise at kAreaDensity 0.0020.
  - E3M1: MAE 12.1 over 100% of px, shift (+3.99, -11.67, -20.76) =
    red-ward. E1M1 gains nothing. §4.5's falsifier passes both ways.
  - CONSTRUCTION CHECK (§7, "not by eye"): with kIndoorFogScale forced to
    0.0, toggling kAreaDensity still moves 29.2% of px at MAE 1.61 — the
    profile term is provably OUTSIDE the skyExposure gate.
  - Cost: megakernel, GPU per-pass profiler, rt_fog High, two runs each.
    Goo room 15.177/15.178 vs 15.187/15.197 ms; E3M1 6.405/6.413 vs
    6.410/6.402 ms. Delta within +-0.02 ms, under the 0.1 ms budget.

  Reusable: `-warpto <x> <y> [deg]` + a WAD-lump parse for liquid sectors
  beats cheat-nav for reaching a fixture headlessly. E1M1 carries BOTH goo
  fixtures — sector 0 is open-sky nukage, sector 53 roofed. E3M1's liquid
  is BLOOD3, which is not in FlagLiquidFlats' lut, so E3M1 tests hell
  cleanly with no goo contamination. rt_profile is read from ~/.doomrc
  AFTER the C default, so defaulting rb_profile=1 does nothing — pass a
  temp `-config` with rt_profile 1 instead.

  Still owed by the user, none blocking: Q7/Q20 (kGooTint/kHellTint and
  the two densities re-judged in play), Q32 (does the sky backdrop's
  closed form take the hell addend and mediumTint, or is a coloured
  skyline seam accepted), Q31 (DOOM-0300's drift speed).
  USER PLAY-TEST 2026-08-04 -- three of the four open look calls answered.
  Q31 (DOOM-0300's drift speed) SIGNED OFF: "I like the fog... this one I
  think you can sign off." The 15x raise stands as a judged value rather than
  a measured guess. CLOSED.
    Carried forward as a SEPARATE observation, because it is not the drift
    speed and must not be filed as if Q31 were still open: "it is hard to see
    the drift as the fog wisps are not as visible as in Silent Hill 2". That
    is wisp AMPLITUDE / contrast (kWispAmp and the S-curve shaping), not
    velocity -- the reference look is the one §1 names, so this is a real gap
    against the stated art direction rather than a preference. Part 1
    (DOOM-0310 §4.6) owns the wisps; raise it there when the fog work resumes.
  Q7 (goo/hell fog DENSITIES) and Q20 (kGooTint/kHellTint COLOURS) CANNOT BE
  JUDGED YET, and the user said why: "As with DOOM-0183, the goo (nukage)
  isn't really highlighting anything." Both questions ask the user to judge a
  tint and a density on a pool that is not visibly lighting its surroundings
  -- so the answer would be measuring the wrong thing. Blocked on DOOM-0316's
  constant split; re-ask with before/after captures once it lands, NOT in the
  abstract. This is the same root cause as DOOM-0183's failing half and it is
  now confirmed from two independent directions (the headless ladder and the
  user's eye).
  Q32 (does the sky backdrop take the hell haze + tint, or is a coloured
  skyline seam accepted) STILL OPEN, and the user asked which levels to judge
  it on. Answered from `r_backend.c` rather than recalled -- `view.hazeDensity`
  is non-zero for `((gamemode == registered || retail) && gameepisode >= 3) ||
  (gamemode == commercial && gamemap >= 20)`:
    DOOM 1  Episode 3 "Inferno"  E3M1-E3M9   (and Episode 4, Thy Flesh Consumed)
    DOOM 2  MAP20 onward         MAP20-MAP32
  E3M1 is the standing fixture for hell in the headless harness, because its
  liquid is BLOOD3, which is NOT in FlagLiquidFlats' lut and so tests hell
  with zero goo contamination.
  Q32 ANSWERED BY THE USER 2026-08-04, with two screenshots of an open hell
  landscape (red sky, mountains, hazed ground). Verdict, verbatim: "The fog is
  showing as white here (the sky is already red) and the fog needs to be
  heavily tinted. It shouldn't be so bright under a red sky. And the fog
  should be a lot thicker."
  So Q32 closes toward TINT THE BACKDROP: the alternative it offered --
  declare the hell profile march-only and accept a coloured skyline seam -- is
  rejected, because the seam is precisely what the user photographed. In
  image 1 there is a bright near-white band along the horizon, brightest at
  centre, sitting under a correctly red sky. That is the signature of fog
  folded onto the sky backdrop WITHOUT mediumTint, which is the exact question
  Q32 asks. Three sub-answers, all in the user's words: the backdrop takes the
  tint; the in-scatter is too BRIGHT for a red-sky scene; and the density is
  too LOW.
  ⚠ NOT YET ACTIONABLE, and the reason is a fork that must be settled before
  any constant moves. The user's HUD reads 86 fps. Ultra's ray-traced view
  measures ~40 fps on this hardware at 100% scale (this session: megakernel
  15.8 ms, 40-41 fps at render_scale 50), so 86 fps points at a RASTERISED
  view -- and by INV-7 the volumetric fog exists ONLY in the RT megakernel
  (rb_rtdebug in {4,6}). If the captures are raster, then what is washing the
  scene out is NOT this feature at all and no amount of kHellTint or
  kAreaDensity will touch it; the work would belong to DOOM-0170's raster
  path or to the sky backdrop's own fade. Asked of the user rather than
  assumed.
  Measurement attempted and DISCARDED as confounded, recorded so it is not
  repeated: E3M1 captured in Ultra RT at rt_fog 2 vs rt_fog 0 and the hue of
  the difference taken, giving R/G = 0.74 on the ground -- green-dominant,
  which would suggest the goo tint on a level whose liquid (BLOOD3) is not
  even in the lut. The subtraction is not pure in-scatter: fog also attenuates
  the surface behind it, so on reddish-brown hell ground the transmittance
  term removes red and biases the delta green. The technique worked in the
  E1M1 goo room and does NOT transfer here. The honest test is to force
  hazeDensity to 0 on the SAME level and diff that, which isolates kHellTint
  exactly. E3M1's spawn also faces a wall and is useless as an outdoor hell
  fixture -- an open-landscape vantage is needed (E3M6, E4M2, or DOOM II
  MAP20+).
  Q32 NARROWED BY MEASUREMENT 2026-08-04, and the earlier reading of the
  user's report was wrong in a way worth recording. Their captures were the
  E3M1 SURFACE (the level opens underground; a lift takes the player up --
  which is also why the spawn-point capture was useless, it faces a wall).
  Reached headlessly by finding an open-sky sector in the WAD directly --
  parse E3M1's SECTORS for `ceilingpic == F_SKY1`, take a vertex on a linedef
  whose sidedef faces one, and `-warpto` there. Spot used: -600 576, angle
  300. Worth reusing: it turns "I cannot get to the place the user
  photographed" into a lookup.
  THE MARCH IS CORRECTLY TINTED. Measured on that capture, mean RGB:
    upper sky     (153.3, 79.6, 79.6)   R/G 1.93
    horizon band  (136.8, 78.4, 67.1)   R/G 1.75
    ground        ( 85.8, 37.0, 25.2)   R/G 2.32
  and E3M6's open landscape independently gives R/G 1.99-2.91. So kHellTint
  IS reaching marchFog and "the fog is showing as white" is NOT a tint failure
  in the march. User confirmed on the capture: "at least the fog is red."
  WHAT IS ACTUALLY WHITE is the SKY BACKDROP -- the untinted pale band along
  the horizon in the user's first screenshot, brightest at centre. That is
  this question's subject exactly: the backdrop's closed form takes neither
  mediumTint nor the profile density addend. Where a view is dominated by
  backdrop rather than by world geometry, the scene reads washed-out and
  neutral even though the march around it is red. So Q32 CLOSES TOWARD
  TINTING THE BACKDROP, on evidence rather than on preference -- the
  alternative it offered (accept a coloured skyline seam) is what the user
  photographed and rejected.
  TWO SEPARATE LOOK CALLS SURVIVE, and neither is answered by the tint work:
  "it shouldn't be so bright under a red sky" (in-scatter brightness --
  kSkyShaftStrength / kSkyAmbientFrac in a red-sky scene) and "the fog should
  be a lot thicker" (density -- kFogBaseDensity, or the hell profile's own
  haze addend). Do not fold these into Q32; tinting the backdrop will change
  neither.
  Method note, so the confounded attempt is not repeated: the hue-of-a-
  difference test (fog on minus fog off) is invalid here. Fog attenuates the
  surface behind it as well as adding in-scatter, so on reddish-brown hell
  ground the transmittance term removes red and biases the delta green -- it
  returned R/G 0.74, suggesting a goo tint on a level with no goo. Measure the
  ABSOLUTE hue of the fogged frame instead, which is what the figures above
  are.
  Note (2026-08-05, upstream review): the user's instinct was right --
  **nobody upstream does volumetric fog.** Grepped GZDoom's and UZDoom's
  entire src/ and wadsrc/ trees for volumetric / godray / light shaft /
  raymarch: **zero matches in either.** Their fog is per-sector DISTANCE
  fog, a colour blended by depth, driven by the map's colormap and MAPINFO
  (GetFogDensity, src/rendering/hwrenderer/scene/hw_lighting.cpp:168, and
  fogboundary.fp for the sector seam). No march, no in-scatter, no shafts.

  So there is nothing to take for the march itself, and this feature is
  genuinely ahead of the field rather than catching up. One idea IS worth
  borrowing though, and it lands directly on DOOM-0330's fix: GZDoom keys
  fog PROPERTIES per sector, read from the WAD, rather than from one global
  model. That is precedent for making our area profile a per-cell property
  of the map instead of a property of what the ray happens to hit.
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

- 🚧 [DOOM-0042] **Add a second, high-fidelity art set (DOOM 3 / sci-fi-horror look) selectable alongside the classic art.**
  Two art options for the 3D renderer: (1) DOOM's original art converted to 3D (the DOOM-0008 path, already in progress); (2) a replacement HD art set inspired by DOOM 3 and sci-fi horror generally. LEVEL LAYOUT IS UNCHANGED — same map geometry/segs/sectors and UVs; only the textures, flats, sprites and their materials are swapped. Implies a material-source abstraction (a 'theme' the renderer selects) layered on the bindless material pipeline, plus real PBR maps (albedo/normal/roughness/metallic/emissive) for the HD set rather than flat paletted albedo. LICENSING CONSTRAINT: id Software's DOOM 3 assets are proprietary and CANNOT be shipped in this GPL-v2 repo. Sourcing options to decide with the user: freely/CC0-licensed HD texture+sprite packs, community packs with compatible licences, AI-generated art, or an optional separately-distributed asset pack the user supplies locally. Open decision: which sourcing route. Depends on the bindless material seam and the path tracer (DOOM-0009).
  **Layman:** A second look for the 3D game: a modern, scary, DOOM-3-style art set you can switch to — same levels, new graphics.
  Kind: feature.
  Source: user-request-2026-06-24.
  Decision (2026-06-24): art-sourcing route = curate free / CC0-licensed HD packs (GPL-v2-distributable). Implication: bulk surface textures are well-covered by CC0 PBR libraries (e.g. ambientCG, Poly Haven), but DOOM-specific sprite/monster coverage under a free licence is patchy — expect to fill gaps (AI-generated or hand-authored originals) and to wrangle disparate packs into one coherent sci-fi-horror look. Curate against DOOM's texture names/sizes via a materials sidecar (see the Q2RTX materials.csv pattern in docs/research/3d-renderer-approaches.md).
  Direction refinement (2026-06-27): the HD art set is now BOUND TO THE ULTRA TIER, not an independent toggle. Classic and Solid keep the original 1993 art; Ultra is the tier that carries the new PBR art + modern lighting (god rays/volumetrics/fog, DOOM-0010/0011) + scene lights (DOOM-0043) + flashlight (DOOM-0044). This supersedes the earlier "art set is orthogonal to everything, all four {Classic-art,HD-art}×{RT off,on} combos valid" framing in DOOM-0009 spec §2. Ray tracing remains an independent On/Off toggle WITHIN both Solid and Ultra. INV-9's art-set-agnostic mechanism is unchanged (the renderer still selects materials via the bindless interface); only the product binding changes — the tier picks the art set, the player no longer picks art separately. DOOM-0009 spec §2 to be updated to match.
  Requirement (2026-06-28, user): the HD PBR map set must include HEIGHT / DISPLACEMENT maps, and the Ultra material/shading pipeline must support PARALLAX OCCLUSION MAPPING (POM) for surfaces with real relief — brick, rough stone, metal grates/panels, tech detailing. Clarification: a height map IS POM's required input (a grayscale displacement field the shader ray-marches against the view direction for recessed depth + self-occlusion), so "add POM" and "add height maps" are the same asset requirement, not two. Sourcing is already covered — the CC0 libraries this item curates (ambientCG, Poly Haven) ship height/displacement alongside albedo/normal/roughness/AO, so the full map set per material becomes: albedo, normal, roughness, metallic, emissive, AO, height/displacement. Map only the surfaces that benefit (skip flat panels/decals) to bound cost. Open decision (which sourcing route) RESOLVED — see the 2026-06-24 Decision line below (CC0 curation + hybrid pipeline). PATH-TRACER DESIGN NOTE (decide at implementation, do not over-spec now): classic POM is a primary-ray raster trick evaluated in the hit shader; in the Ultra path tracer (DOOM-0009) that gives parallax in the directly-shaded view but the displaced relief is INVISIBLE to ray-traced shadows + the GI/probe bake unless we go further — options are (a) cheap primary-hit POM only (relief shades but casts no self-shadow), (b) height-field self-occlusion sampled during NEE shadow rays, or (c) true geometric displacement / displacement-mapped BLAS (most accurate, most BVH cost). Lives in the material seam shared with DOOM-0010/0011 (modern lighting). Tessellation/real displacement is the heavier alternative if POM artefacts at grazing angles prove unacceptable.
  Spec drafted 2026-07-14: docs/specs/DOOM-0042-ultra-hd-pbr-materials.md. Design approved by user via brainstorming. Confirms/consolidates prior decisions (CC0 sourcing ambientCG/Poly Haven; Ultra-tier binding; height maps + POM) and adds two refinements: (1) HYBRID asset pipeline — hand-picked CC0 "hero" materials for high-traffic surfaces + an offline derive-generator (scripts/pbr_derive.py) producing PBR maps from the original WAD art for the long tail; (2) sequencing — Ultra RAY-TRACED view first (Ultra's default rb_rtdebug=6), Ultra raster sub-view (~) HD is a fast-follow. POM = option (a) primary-hit only (relief shades, no RT self-shadow yet). Tier hook = rendermode==RB_RT3D. First slice = E1M1. Enemies stay billboards (3D monster models remain the separate far-out item). Sourced assets go in /mnt/Games/3D Engine Assets/ (CC0/free only). Dependency edge (cold-eyes 2026-07-14): DOOM-0042 DEPENDS ON DOOM-0103 for the GGX specular lobe — 0042 supplies the roughness/metallic maps + material plumbing, 0103 owns the BRDF (F0 / VNDF sampling / MIS). The "wet metal" specular look and its verification are gated on DOOM-0103; until it lands, roughness/metallic ride the diffuse term only (albedo/normal/AO/POM still upgrade).
  Cold-eyes converged 2026-07-14: /cold-eyes run to convergence over 12 loops (3 lanes/loop — citation accuracy, cross-doc integrity, spec-as-contract), every actionable finding fixed, log in the spec. Spec + ADR 0002 (materials.csv sidecar + stb_image loader) APPROVED and signed off by Claude per the user's standing delegation. Notable outcomes: GGX specular lobe is DOOM-0103's (0042 supplies roughness/metallic maps + plumbing, depends on 0103 for the wet-metal look); v1 HD = walls + flats only (HD sprite albedo is a fast-follow, sprites paletted in v1); POM = kPomHeightScale 0.06, offset-bounded (not [0,1]-clamped, preserves REPEAT tiling). NEXT: implement the E1M1 slice (writing-plans).

- ✅ [DOOM-0043] **Place scene lights and an ambient floor so path-traced rooms are never unintentionally pitch black.**
  Once ray tracing computes real light transport, sectors with no bright surfaces go near-black. Build on the DOOM-0008 spec's derived emission (sector lightlevel glow, known bright/lamp/computer textures as emitters, sky sun) and add deliberate light placement where it makes sense (lamps, computer banks, exit signs, fire) plus a small ambient/sky floor so navigation stays playable. Distinct from DOOM-0010 (moving/coloured dynamic lights) and DOOM-0009 (the integrator itself) — this is the lighting-design/brightness pass that keeps the world readable. Pairs with the player flashlight (next item) for genuinely dark areas.
  **Layman:** Make sure rooms aren't pitch dark once real lighting is on — add lights where it makes sense and a gentle base glow.
  Kind: feature.
  Source: user-request-2026-06-24.
  Scope decision (2026-06-27, user): this is ULTRA-ONLY. Solid intentionally leaves unlit rooms pitch black for now — the player flashlight (DOOM-0044) is the intended answer for dark Solid rooms, not an ambient floor. Whether to soften pitch-black Solid rooms gets revisited during playtest (using cheats to traverse levels), not decided now. So DOOM-0043's deliberate scene lights + gentle ambient/sky floor apply to the Ultra tier; Solid (RT on or off) ships no ambient floor at this stage.
  Follow-up (2026-06-27, user): the Ultra ambient-floor / scene-light treatment is ALSO provisional — the user may decide to leave Ultra rooms pitch black too. Decision deferred to playtest: play the levels first (cheats OK to traverse), then call whether Ultra keeps the gentle ambient floor + placed lights or goes fully dark like Solid. So BOTH tiers' dark-room handling is a play-it-first call; nothing here is final until playtest.
  Resolved (2026-06-29): Ultra ambient floor implemented in the path-tracer megakernel (pathtrace.comp modes 4 + 6). A gentle ambient term seeded by the DOOM sector lightlevel (per-vertex, 0..1) lifts rooms the mapper marked bright even when they hold no emissive lamp texture, via max(GI, sectorLight*AMBIENT_SECTOR_SCALE) so already-lit rooms are untouched (no double-count) and genuinely dark sectors stay dark for the flashlight (DOOM-0044) to matter. Ultra-only by construction — only the megakernel runs this; Solid is the raster path. Default AMBIENT_SECTOR_SCALE=0.25 is the INV-7 inline playtest knob. Scope note: the "deliberate scene lights" half (lamps/computer banks/exit signs as emitters) was already delivered by DOOM-0008's texture-derived emission (ComputeMaterialEmissive); free-standing light OBJECTS (barrels/torches) remain DOOM-0084, lit switches DOOM-0082, slime glow DOOM-0083. This whole feature stays provisional per the roadmap's play-it-first decision: after playtest the user may tune the scale or revert Ultra to fully dark like Solid.
  Playtest (2026-06-29, user): at AMBIENT_SECTOR_SCALE=0.25 rooms read bright enough (no longer too dark), BUT the flat ambient fill softens shadows/contrast — it lifts the shadowed side of surfaces, not just the lit side. User chose to leave it as-is for now and re-judge on later levels before tuning. Tuning options when revisited: (a) lower the scale to trade brightness back for contrast; (b) make the floor less flat — e.g. scale it by an occlusion/AO term or fold it into the GI bake so genuinely shadowed pockets stay dark while open lit rooms still read; (c) the standing revert-to-fully-dark option per this item's play-it-first decision.

- ✅ [DOOM-0044] **Add a player flashlight toggled by a key, lighting the path-traced scene with ray-traced shadows.**
  A camera-mounted spotlight (headlamp) the player toggles with a configurable, config-persisted key. Fed to the path tracer as a dynamic analytic light so it casts real ray-traced shadows and bounces, sampled by next-event estimation. Follows view position/angle each frame (no BLAS change — a light parameter, not geometry). Builds on the DOOM-0009 integrator and the dynamic-light path (DOOM-0010 seed). Makes the dark sci-fi-horror areas (previous item) tense rather than unplayable.
  **Layman:** Give the player a flashlight they can switch on and off with a button.
  Kind: feature.
  Source: user-request-2026-06-24.
  Scope clarification (2026-06-27): the flashlight is a feature of BOTH 3D tiers (Solid and Ultra), not Ultra/path-traced only. With ray tracing OFF it is an ordinary raster spotlight (cone + falloff, no cast shadows); with ray tracing ON it is fed to the integrator as a dynamic analytic light and casts real ray-traced shadows + bounces (the behaviour already described below). Only Classic has no flashlight. So the toggle key + view-tracking light are tier-agnostic; only the shadow quality scales with the RT On/Off switch.
  Input mapping (user decision 2026-06-27): the flashlight toggle maps to L1 (SDL_CONTROLLER_BUTTON_LEFTSHOULDER) on the gamepad. Keyboard key is implementer's choice (pick a free, intuitive one — e.g. F). Wire on a press edge like the existing rb_wireframe/Share-button pattern in i_video.c.
  Progress (2026-06-29): implemented + build-clean (commit 887483c); on-hardware playtest pending before shipped. The eye + view dir already reach both renderers, so the light needed only an on/off bit. Ultra (pathtrace.comp modes 4 + 6): an eye-mounted spotlight aimed along camDir with a hard ray-traced shadow + 1/dist^2 falloff + a cone (cos 0.82->0.92), built on the muzzle-flash dynamic-light pattern; forwarded in the spare misc2.w push slot (no RT layout change). Solid (mesh.frag): additive cone + distance falloff + facing term, no cast shadows; added a vWorldPos varying + one push float (raster push range 23->24). Input (i_video.c): keyboard F (press edge, gated on !menuactive so it still types in menu/save-name entry) + gamepad L1; L1 dropped from the run binding (R1 + left trigger still run). Config (m_misc.c): "flashlight" persists on/off. Colour/intensity/cone are INV-7 inline tunables. EYES-ON VERIFY on the RX 6600: (1) Ultra — F/L1 lights a cone ahead that casts real shadows off walls/things and tracks the view as you turn; (2) Solid — F/L1 brightens a cone ahead (no shadows); (3) toggle persists across a restart; (4) tune brightness/cone width if the beam reads too hot or too narrow. Flip to shipped + changelog once confirmed.
  Playtest fix (2026-06-29, commit 55bb080): user reported the Ultra flashlight's lit patch lingering ~1s after toggle-off. Root cause was in the SVGF temporal denoiser (svgf_temporal.comp), not the flashlight: its adaptive anti-ghosting reset history only PARTIALLY on a sudden luma drop, and the bright residue inflated the variance estimate (m2 - m1^2), pushing 2*sigma above the delta on subsequent frames so detection stalled and the patch faded at the slow EMA floor. Fix normalises the temporal gradient by max(m1,l) (symmetric darken/brighten) and lets a ~half-magnitude change saturate the reset -> a genuine lighting edge clears history in one frame; the 2*sigma noise guard is kept so stable surfaces still converge. Side benefit: resolves the previously-pending A-SVGF muzzle-flash trailing verify (the "A" anti-ghosting was implemented but under-tuned for held/toggled dynamic lights). EYES-ON: confirm the toggle-off is now instant and that still scenes/GI haven't re-noised.
  Resolved (2026-06-29): user-confirmed on the RX 6600. Flashlight toggles with F / gamepad L1, persists across restart, lights a cone ahead — ray-traced shadows in Ultra (pathtrace.comp modes 4+6), a raster cone in Solid (mesh.frag). Playtest fixes folded in: SVGF temporal anti-ghosting so the toggle-off no longer lingers (commit 55bb080; "significantly better", residual parked as DOOM-0099), and near-field 1/(dist^2+R^2) softening so the beam no longer blows to a white blob point-blank (5010df7). Same session bumped the muzzle flash to out-shine the headlamp (6242fa6, FLASH_INTENSITY 3e6) — "lights up the surrounding area like it should". Flashlight/muzzle colour, intensity, cone + softening radius remain INV-7 inline tunables pending a Workbench pass.

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

- ✅ [DOOM-0050] **Fix 2D HUD/menu overlay ghosting over the status bar in 3D modes.**
  Found 2026-06-25 (user testing, Sound Volume menu). In Solid/Ultra the compositor reads the whole paletted screens[0] each frame, but only the 3D view rectangle is cleared to RB_OVERLAY_KEY (r_backend.c). The status-bar region is drawn by ST_Drawer with refresh=false (st_stuff.c ST_diffDraw), which repaints only changed widgets, not the background -- so stale menu pixels drawn over the status bar last frame are never erased and composite as ghosting. Fix: force a full status-bar refresh (ST_doRefresh) each frame while in a 3D mode so the bar background repaints and erases stale overlay pixels; the border redraw already covers windowed sizes (borderdrawcount on menuactive). Watch also for any vertical displacement of the menu title (verify the overlay samples the full 320x200 logical screens[0] under DOOM-0027 hires).
  **Layman:** Menus smear over the bottom HUD bar in 3D mode.
  Kind: fix.
  Source: in-session-2026-06-25.
  Implemented 2026-06-25 (commit 7d19c64): force a full status-bar refresh each frame in 3D modes so stale overlay pixels are cleared. Builds clean. Pending on-screen re-test of the Sound Volume menu over the HUD.
  Progress (2026-08-04, user): the artefact that prompted the re-test was
  the user's KDE MAGNIFYING LENS, not the engine -- "we found that it was my
  magnifying lens". Same class as DOOM-0173, where the "mystery box" turned
  out to be a desktop overlay that appeared on the bare desktop too.
  This does NOT retract the fix. The mechanism diagnosed in 2026-06-25 was
  real and read from source: ST_Drawer runs with refresh=false (ST_diffDraw)
  so it repaints only changed widgets, while the compositor clears just the
  3D view rect to RB_OVERLAY_KEY -- stale overlay pixels over the status bar
  were genuinely never erased. Commit 7d19c64 forces a full status-bar
  refresh each frame in 3D modes. What the magnifier explains is the
  SYMPTOM the user was looking at when asking for the re-test, which is why
  the re-test kept seeming to fail.
  WHAT IS ACTUALLY OUTSTANDING is therefore narrower than the bullet above
  implies: not "does the smear still happen" but a plain confirmation that
  the menu-over-HUD region is clean in Solid/Ultra with the magnifier off.
  Note this cannot be captured headlessly today -- opening a menu needs a
  keypress and Wayland will not let input be injected into the client, so
  -inspect/-freeze/-devshot (DOOM-0294, 2026-08-04) reach every WORLD view
  but no menu. Either a human looks with the lens off, or the developer
  build gains a way to open a menu from argv. The latter is the general fix
  and would also unblock DOOM-0205's on-screen check.
  Resolved (2026-08-04): the outstanding item was never a fix, it was a LOOK, and DOOM-0318 made that look capturable the same day. The bullet's own words: "not 'does the smear still happen' but a plain confirmation that the menu-over-HUD region is clean in Solid/Ultra with the magnifier off", blocked because no capture could open a menu.
  Confirmed clean in both 3D tiers, magnifier not involved (these are headless captures, so the user's lens cannot contaminate them -- which is what made the original report misleading):
    dev-shots/N-effects-solid.png  Solid, Render Effects over the status bar
    dev-shots/M-effects-ultra.png  Ultra RT, same menu
    dev-shots/O-video-ultra.png    Ultra RT, the TALL DOOM-0206 Video menu -- the strongest fixture, because it runs the full height down to the status-bar boundary and ends in a scroll chevron
  In all three the status bar is crisp and undimmed, the menu sits above it, and the boundary shows no ghosting, smear or double-draw.
  The fix itself shipped earlier as part of DOOM-0206's menu redesign, whose CHANGELOG entry already states it: "a layout that no longer overlaps the bottom status bar". No separate CHANGELOG entry, since a second one would describe the same change twice.

- ✅ [DOOM-0047] **Verify sound-effect audibility vs music balance in the SDL2 build.**
  Found 2026-06-25 (user testing). SFX are fully wired (i_sound.c: addsfx -> I_MixSound -> SDL callback; s_sound.c S_StartSound -> I_StartSound; no stub), but the user can't tell they play. SFX are software-mixed at 11025 Hz; music plays via SDL2_mixer at 44100 Hz on a separate device, so music may dominate. Isolation test first: set Music Volume to 0 and confirm SFX are audible. If SFX are present but quiet, options: raise the default sfx_volume (m_misc.c, currently 8/15) or rebalance the mix; do not rewrite the mixer blind. Confirm by ear before changing audio.
  **Layman:** Hard to tell if gun/door/monster sounds are playing under the music.
  Kind: investigate.
  Source: in-session-2026-06-25.
  Resolved-pending-ear-check 2026-06-25 (commit 863447f): user confirmed SFX play but are drowned by music. Capped music scale at 80/128 (~63%) instead of full -- safer than boosting SFX (software mixer would clip). Tunable; awaiting the user's confirmation of the new balance.
  Resolved (2026-06-25, commit 863447f, user-confirmed): capped the music scale at 80/128 (~63%) so the louder separate-device music no longer drowns the 11025 Hz software SFX mixer. User reports "the sound effects and music are much better now." Tunable if needed."
  Rebalance follow-up 2026-06-30 (user: music still drowns SFX even with the music slider low). Confirmed SFX cannot be amplified to compensate — a per-channel volume >127 is a hard I_Error in I_UpdateSoundParams (i_sound.c:343) — so the rebalance is music-side only. Lowered the music ceiling 80/128 -> 48/128 (~38% at max) in I_SetMusicVolume (i_sound.c:464): scales music down at EVERY slider position, so it works on the user's existing config (runtime), not just fresh installs. Also raised the default sfx_volume 8 -> 15 (m_misc.c:249) so new configs are SFX-forward (does NOT affect an existing config — user should max the in-game SFX slider). Built Linux+Windows, deployed exe to the test share. Tunable: if music now too quiet raise the 48; if still dominant drop toward 32. A fuller fix (upsample the 11025 Hz SFX to 44100 to close the device-loudness gap) remains out of scope.
  Fix 2026-07-01 (Windows audio, user-reported): music blared at full volume until the volume slider was first touched, and that max-volume music drowned the SFX ("SFX too soft on Windows only; Linux perfect"). Root cause: I_PlaySong called Mix_PlayMusic but never re-applied the music volume; SDL2_mixer's MIDI backend (on Windows) starts a freshly-played track at full volume and ignores a Mix_VolumeMusic set before playback began. Fixed by calling I_SetMusicVolume(snd_MusicVolume) right after Mix_PlayMusic (i_sound.c I_PlaySong), so every track honors the saved/menu level from the first note. Explains why Linux was fine (its MIDI backend honored the pre-set volume) and Windows wasn't. The SFX-too-soft symptom was a consequence of the blaring music, so this single fix addresses both. Builds clean Linux+Windows; exe redeployed. User to confirm on Windows.
  SFX-quiet-on-Windows fix 2026-07-01 (after the music-volume fix earlier today, SFX were STILL barely audible on Windows at max slider; Linux perfect). Root cause via web research: SDL's Windows WASAPI backend resamples an 11025 Hz audio device badly (near-silent effects) -- a known SDL issue (libsdl-org/SDL#1491, "poor quality playing 11025hz"). The software SFX mixer ran at SAMPLERATE 11025; music (SDL2_mixer, 44100) was unaffected, which is why only SFX suffered and only on Windows. Fix (i_sound.c): raise the SFX mixer output to SAMPLERATE 44100 (common native rate, matches the music device, so WASAPI does no awkward resampling); added SFXRATE (11025, the DOOM sound-lump source rate) and scaled the pitch steptable by SFXRATE/SAMPLERATE so sounds play at the SAME pitch. Bumped SAMPLECOUNT 512->1024 (~23 ms buffer) for underrun margin at the higher rate. Mixing amplitude (vol_lookup) untouched, so Linux is bit-identical; Windows should now be full-volume. Stale 11025 comments updated. Builds clean Linux+Windows; audio device inits OK at 44100; exe redeployed. User to confirm on Windows.
  SFX-silent-on-Windows, take 2 (2026-07-01): the 44100 Hz change did NOT fix it -- effects were still near-silent on Windows (even menu blips), while Linux stayed perfect. Real root cause: the engine opened TWO audio devices -- effects on the legacy SDL_OpenAudio device, music on the separate SDL2_mixer device -- and on Windows the second (effects) device barely output. Since MUSIC (SDL2_mixer) works on Windows, that device is the known-good path. Fix (i_sound.c): stop opening a second device; mix the effects INTO the SDL2_mixer output via Mix_SetPostMix. I_MixSound -> I_MixSoundInto(out, frames) now ADDS effects into the post-mix stream (music) and clamps, decoupled from SAMPLECOUNT. I_InitMusic queries the device rate (Mix_QuerySpec; ALLOW_FREQUENCY_CHANGE may pick 48 kHz), sets sfx_out_freq, and registers the hook (sfx_via_postmix). The step table targets sfx_out_freq so pitch is right at any device rate. If the music device fails to open, a standalone SDL_OpenAudio effects device is opened as a fallback (effects still play, no music). Shutdown unregisters the post-mix hook or closes the fallback. Effects amplitude (vol_lookup) unchanged -> Linux stays perfect; Windows effects now ride the working device. Builds clean Linux+Windows; headless run confirms the post-mix path initialises and a level runs without crashing; exe redeployed. User to confirm on Windows.
  SFX-silent-on-Windows, take 3 (2026-07-01, the fix): the post-mix approach made it WORSE (total silence; even menu blips). Root cause is architectural -- the hand-rolled software mixer feeding an SDL audio device does not work reliably on Windows/WASAPI. Per web research + Chocolate Doom's i_sdlsound.c, the robust approach is to play effects as SDL2_mixer CHUNKS on the SAME device as the music (music works on Windows, so that device is known-good). Rewrote i_sound.c: I_CacheSfxChunk converts each DOOM lump (8-bit U8 mono @ its header rate) to S16 stereo @ the mixer's rate via SDL_BuildAudioCVT/SDL_ConvertAudio (non-fatal W_CheckNumForName so a DOOM2 sound missing under DOOM1 is skipped, not I_Error). I_StartSound = Mix_PlayChannel(-1,...) returning the channel as the handle; I_UpdateSoundParams/I_StopSound/I_SoundIsPlaying = Mix_SetPanning/Mix_HaltChannel/Mix_Playing. I_SetChanVolPan replicates the old addsfx L/R split and maps DOOM's 0-15 vol to SDL panning x2 -- i.e. the SAME ~12% ceiling the old mixer used, so Linux loudness is unchanged. I_InitMusic now opens the ONE shared device FIRST (before the MIDI check, so effects work even without MIDI), queries its rate (mixer_freq), allocates channels, sets sound_ok. The old software-mixer code (getsfx/addsfx/I_MixSoundInto/callbacks/globals) is left dead for now (non-static or __attribute__((unused)); a follow-up cleanup will delete it). Pitch variation is not applied to chunks (minor). Builds clean Linux+Windows; headless: device opens, chunks cache, a level runs with no crash. exe redeployed. User to confirm Windows (audible) AND Linux (unchanged).
  Volume rebalance 2026-07-01 (after chunk rewrite): user reported effects working on Windows but far too soft vs music, and menu blips (up/down/select) inaudible while "back" worked -- symptom of the ~12% ceiling I'd matched to old Linux (centred sounds landed ~8.6%). Also the user has long run Linux with SFX maxed + music near-off to balance. Fix: I_SetChanVolPan now separates loudness from pan -- Mix_Volume(channel, vol*128/15) gives a full 0..100% range so a maxed slider balances the music (which peaks 48/128), and Mix_SetPanning carries only L/R (2*(255-sep) / 2*sep), FULL in both channels at centre so centred sounds (all menu blips, player weapon) play at full volume. Much louder than before on BOTH platforms; the SFX slider is now meaningful end-to-end. This is the requested Linux rebalance too. Builds clean Linux+Windows; exe redeployed. User to confirm balance on both; if simultaneous loud effects clip, cap the ceiling below 128. Menu-blip audibility expected fixed by the boost; if any specific blip is still silent it's a separate caching bug to chase.

- ✅ [DOOM-0048] **Decouple render rate from the 35 Hz game tic (currently present-locked at 35 FPS).**
  Found 2026-06-25 (FPS counter reads ~35). D_Display is presenting one frame per 35 Hz game tic, so the renderer is tic-locked and can't exceed 35 FPS regardless of GPU headroom. The FPS counter (DOOM-0046) is reporting the true present rate. To hit the Phase-2 60 FPS floor the present/interpolation must be decoupled from the fixed game tic (render interpolation between tics, uncapped or vsync present). Part of the 60-FPS-floor performance work (DOOM-0011/0012); recorded now as the concrete sub-task surfaced by testing.
  **Layman:** The game draws only 35 frames a second; the FPS counter correctly shows ~35.
  Kind: perf.
  Source: in-session-2026-06-25.
  Progress (2026-06-29): camera/view-only render interpolation implemented (awaiting play-test). The single-player + 3D path (Solid/Ultra) now runs a non-blocking tic advance (NetUpdate + run-all-due-tics, capped at 1s) and renders interpolated in-between frames paced by vsync; the simulation stays locked to 35 Hz so demos/physics are bit-exact (render-only). r_backend.c: new RB_InterpReset/Snapshot/SetFrac/Disable + lerp in Vulkan_RenderPlayerView (fixed_t pos, signed short-arc angle wrap, 128-unit teleport-snap guard). d_main.c D_DoomLoop: uncapped branch gated on !singletics && !netgame && !demoplayback && !demorecording && rendermode != RB_CLASSIC; net/demo/Classic keep the classic tic-locked TryRunTics. Sub-tic fraction from I_GetTimeMS reset at each tic. Scope is camera-only: monsters/projectiles still step at 35 Hz (full-entity interpolation is the deferred follow-up DOOM-0048b). Compiles clean. NOT yet pushed/changelogged: this is a core-loop runtime change that can only be verified by playing (no WAD in repo). PLAY-TEST CHECKLIST: (1) game speed unchanged in Solid/Ultra; (2) FPS counter exceeds 35; (3) walking/turning smooth, no judder; (4) no camera smear on teleport/level load; (5) Classic mode still tic-locked & unchanged; (6) input latency acceptable.
  Resolved (2026-06-29): play-tested in Ultra above the 35 Hz floor (the DOOM-0090 perf work got 50% TAAU to 35-46 fps) — user confirms camera motion is "definitely smooth", which is the benefit interpolation only delivers once the render rate exceeds the 35 Hz sim tic. Simulation stays 35 Hz; demos/netplay/Classic unaffected (RB_InterpDisable). Shipped in c57d12c.

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

- ✅ [DOOM-0057] **Reconcile DOOM-0008's internally-split moving-sector AS wording.**
  Surfaced by the DOOM-0009 /cold-eyes loop. DOOM-0008 contradicts itself on moving-sector acceleration-structure updates: §Approach (~line 138) says doors/lifts "update TLAS instance transforms, not BLAS geometry", while §Geometry (~line 215) says "per-sector BLAS updates (refit, not rebuild)". DOOM-0009 §3 resolves the physics (rigid motion → instance transform; non-rigid wall-height change → BLAS refit) and supersedes the over-broad "no refit" claim. Action: align DOOM-0008's two passages to DOOM-0009 §3's resolution so the sibling specs stop disagreeing. Pure doc edit.
  **Layman:** Fix two sentences in an old design doc that disagree with each other about how moving doors are drawn in the ray-traced renderer.
  Kind: doc-fix.
  Source: cold-eyes DOOM-0009 2026-06-25.
  Resolved (2026-07-04): DOOM-0008 §Approach reworded to match §Geometry and DOOM-0009 §3 — rigid movers → TLAS instance transform, moving sectors → per-sector BLAS refit. DOOM-0009 §3's now-stale 'DOOM-0008 is split' parenthetical also updated. Verified clean by a 5-loop cold-eyes pass (both DOOM-0008 and DOOM-0009 lanes independently confirmed the two passages now agree).

- 📋 [DOOM-0058] **Replace the per-material manual sub-allocator with VMA.**
  DOOM-0009 build step 1 backs all N bindless material images with ONE device allocation, binding each at a manually-aligned offset (r_vulkan.cpp UploadAtlas). That keeps the allocation count at 1 (clear of the driver's per-allocation limit on large WADs) but is a minimal hand-rolled sub-allocator. Swap it for the Vulkan Memory Allocator (VMA) when the AS/buffer allocations of later build steps arrive, so all GPU memory goes through one battle-tested allocator. No behaviour change; robustness + less bespoke code.
  **Layman:** Tidy up how the game reserves video memory for textures so it scales to big mods.
  Kind: refactor.
  Source: in-session-2026-06-25 DOOM-0009 build step 1 increment 2.

- ✅ [DOOM-0059] **Gate the 3D render tiers on descriptor-indexing support at probe time.**
  The bindless materials path (DOOM-0009 build step 1) requires four Vulkan 1.2 descriptor-indexing features. They are checked in PickPhysicalAndDevice (device creation, after the user has already switched to Solid/Ultra), so a GPU lacking them now I_Errors at init. Effectively unreachable on real hardware (any Vulkan-1.2 driver has them), but the clean fix is to fold the check into RB_Vulkan_Available / the tier probe so an unsupported GPU never offers the 3D tiers and the menu silently stays on Classic — no abort.
  **Layman:** On a very old GPU, keep the menu on Classic instead of crashing when 3D is picked.
  Kind: enhancement.
  Source: in-session-2026-06-25 DOOM-0009 build step 1 increment 2.
  Shipped 2026-07-01. RB_VulkanProbe (r_vulkan.cpp) now queries the same four
  Vulkan 1.2 descriptor-indexing features PickPhysicalAndDevice requires
  (runtimeDescriptorArray / shaderSampledImageArrayNonUniformIndexing /
  descriptorBindingVariableDescriptorCount / descriptorBindingPartiallyBound)
  and skips any device lacking them, so a GPU without bindless support is
  reported Classic-only and the menu never offers Solid/Ultra — no init-time
  I_Error. The device-creation check is kept as a backstop (comment updated).
  Confined to the probe; orthogonal to DOOM-0147's r_backend.c availability
  rework. Builds clean (g++ -Wall, 0 warnings). On-HW verify still open: only
  testable on a Vulkan-1.2 GPU that genuinely lacks descriptor indexing
  (effectively none exist), so verified by code parity with the device path.

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

- ✅ [DOOM-0073] **3D renderer defensive-hardening bundle (indie-review deferred items).**
  Deferred indie-review findings (2026-06-26) that are defensive-only (not reachable with the shipped/stock WADs) — bundled for one hardening pass. (1) clip_poly POLYMAX=64 silently truncates a carved cap to a malformed polygon -> emit_cap_poly fans garbage triangles; a Python replay proved stock E1M1 stays at <=8 verts, but a very complex custom sector could exceed 64 -> add a guard that skips/asserts rather than rendering wrong (r_mesh.c clip_poly/emit_subsector_caps). (2) RB_UpdateMeshHeights indexes flattranslation[pic]/texturetranslation[base] from WAD-loaded shorts every frame with no bound check -> a corrupt map is a repeated OOB read; add a load-time invariant or per-use clamp (r_mesh.c ~496/505). (3) RB_BuildSprites uses sprframe->lump[rot] then spritewidth[lump] with no lump>=0 check; R_InstallSpriteLump validates at load (same as vanilla) so unreachable, but a cheap guard documents the invariant (r_mesh.c ~786). (4) RB_Vulkan_SetOverlay assumes screens[0] never resizes mid-session (true today: V_Init allocates once) -> add a size-change guard so a future runtime-resolution change can't overrun the overlay staging buffer (r_vulkan.cpp ~1605/1731). (5) (fixed_t)(view->x*FRACUNIT) overflows int32 at the +/-32768 map edge -> wrong sprite-rotation angle for a frame; compute the angle without round-tripping through fixed (r_mesh.c ~753). (6) carve_caps recurses passing a ~520-byte poly_t by value (twice/level) -> pass const poly_t* to cut stack/copies and bound a degenerate-BSP stack blow (r_mesh.c ~344). (7) several Vulkan enumerate calls + vkCreateDebugUtilsMessengerEXT drop their VkResult -> Check-wrap the load-bearing ones (esp. vkGetSwapchainImagesKHR). All MEDIUM/LOW, none reachable in normal play.
  **Layman:** A set of small safety nets in the 3D renderer for unusual or corrupt level data — none affect normal play; grouped for a later hardening pass.
  Kind: refactor.
  Source: indie-review-2026-06-26.
  Resolved (2026-07-02): all 7 deferred defensive items landed across two commits (1550bdc r_mesh.c, fed04ad r_vulkan.cpp). (1) clip_poly flags POLYMAX truncation and drops the cell (n=0) instead of fanning a malformed poly; (2) RB_UpdateMeshHeights bounds flattranslation[pic]/texturetranslation[base] to numflats/numtextures; (3) RB_BuildSprites guards lump<0; (4) RB_Vulkan_SetOverlay records the built size and refuses a mid-session resize rather than overrun the fixed staging buffer; (5) sprite rotation angle now from atan2 float deltas (no int32 overflow at the +/-32768 map edge), math-equivalent to R_PointToAngle2 for the 8-way bucket; (6) carve_caps recurses by const poly_t* with one reused child buffer (was ~520B by value/level); (7) Check-wrapped the load-bearing vkGetSwapchainImagesKHR + vkEnumeratePhysicalDevices and capture+warn on the non-fatal debug messenger (graceful-degrade enumerations left unwrapped by design). Builds clean (-Wall, zero warnings). Only item 5 changes the normal hot path (sprite rotation) and is verified math-equivalent; the rest are no-ops unless fed corrupt/pathological data. Not runtime-eyes-on verified (guards are unreachable with stock WADs).

- ✅ [DOOM-0074] **3D renderer has no CPU/GPU frame overlap (single frame in flight).**
  Found 2026-06-26 (indie-review, r_vulkan.cpp). The swapchain requests minImageCount+1 images (typically 3) and uses FIFO present, but there is only ONE command buffer + one inFlight fence + one image-available/render-finished semaphore pair, and every RB_Vulkan_Present blocks on that fence at the top. So the CPU always waits for the previous frame's full GPU completion before recording the next -- zero CPU/GPU overlap, throughput capped at GPU-bound latency. Correctness-safe (the fence serializes the persistently-mapped vertex buffer writes), but a real ceiling for the project's 60 FPS floor. Fix: an N-deep ring of {cmd, imageAvailable, renderFinished, fence} indexed by a frame counter, with per-frame vertex-buffer regions (or fence-gated reuse). Defer until perf work; note the mapped-buffer write must stay fence-guarded per frame-slot.
  **Layman:** The 3D renderer waits for the graphics card to fully finish each frame before starting the next, leaving performance on the table toward the 60 FPS goal.
  Kind: perf.
  Source: indie-review-2026-06-26.
  Resolved 2026-07-17 (branch DOOM-0074-frames-in-flight): implemented CPU/GPU overlap via "build-ahead" rather than a full N-frames-in-flight ring. The expensive per-frame CPU build (moving-sector re-height ~3ms + NEE emitter refill/point-light cull ~2.4ms + billboards) is factored into BuildFrameInputs() and, in steady-state raster (Solid), run BEFORE the top-of-frame fence wait so the CPU prepares the next frame while the GPU still renders the current one. Only the 3 buffers the build writes and the raster GPU reads (vbuf vertex input, spriteVbuf vertex input, lightBuf point-light SSBO) are double-buffered per in-flight slot (g.<name> aliases re-pointed at slot[frameSlot] each frame; all downstream bind/BDA/re-height sites unchanged). The GPU stays single-frame-in-flight: ONE command buffer, ONE fence, ONE semaphore set, and all render targets / BLAS-TLAS / SVGF denoiser history are single-copy and untouched — so RT/Ultra is unaffected. A traced or just-toggled frame builds after the fence (serialized); a render-mode (~ key) toggle drains once (vkDeviceWaitIdle) so single-copy RT resources are never in flight. Hardware-verified on the RX 6600 (E1M1): A/B with build-ahead forced off = 70 fps (fenceWait 7.61ms serial after GPU), on = 161 fps (fenceWait 0.03ms, GPU fully hidden under the 5.4ms build) — clears the 60 FPS floor with headroom. RT path unchanged: -rtverify INV-6 rel-MSE 0.0987% PASS, RT run steady at ~45 fps (GPU-bound megakernel, unchanged). No visual corruption (screenshots), no crash across 8 raster<->RT toggles, make + make test green. Full RT-mode overlap (double-buffering BLAS/TLAS/emitter/sprite buffers + render targets) deferred as a separate item.

- 💭 [DOOM-0075] **3D sky pans ~4x too fast and tiles 4x too often vs Classic.**
  Found 2026-06-26 (indie-review, mesh.frag). The sky fragment path maps the ~90deg horizontal FOV (atan(ndcX) over -1..1) to ONE full sky-texture width: col = ang/(PI*0.5)*sz.x. Classic DOOM maps a full 360deg of yaw to one texture repeat (ANGLETOSKYSHIFT: 90deg -> 1/4 texture). So our sky pans 4x faster and tiles 4x as often as the original. Falls under mesh.frag's explicit bring-up-shader constant exemption, hence deferred not silently changed. Fix: col = ang/(2.0*PI)*sz.x (360deg -> one texture width), matching DOOM's viewangletox sky shift. Verify against Classic by turning on the spot in E1M1.
  **Layman:** In the 3D renderers the sky/mountains scroll about four times too fast as you turn, compared to Classic DOOM.
  Kind: fix.
  Source: indie-review-2026-06-26.
  Investigated 2026-07-01 — the diagnosis is inverted; recommend closing as
  working-as-intended. Verified against this repo's own Classic renderer:
  r_sky.h:35 ANGLETOSKYSHIFT=22, sky texture 256 wide (r_sky.h:34),
  r_plane.c:427 angle=(viewangle+xtoviewangle[x])>>22 then R_GetColumn masks
  to width. A full circle 2^32 >>22 = 1024 column-steps per 360deg, masked to
  256 => Classic repeats the sky 4x per 360deg, i.e. 90deg of yaw -> exactly
  one texture width. That is the well-known DOOM sky (it tiles 4x per turn),
  NOT one panorama per 360deg. The current shader col=ang/(PI*0.5)*sz.x already
  reproduces this: 90deg -> one width, 4 repeats per 360deg, and the 90deg FOV
  (atan over ndcX -1..1 = PI/2) shows exactly one width across the screen — so
  pan rate AND tiling already match Classic. The proposed col=ang/(2*PI)*sz.x
  would pan 4x too SLOW and show a single 360deg panorama — a regression, not a
  fix. No code change made. If confirmed, flip to a closed/considered state.
  Closed 2026-07-01 as working-as-intended (user-confirmed). The sky already
  matches Classic — see the 2026-07-01 investigation note above; the proposed
  ang/(2*PI) change would be a regression. No further action.

- ✅ [DOOM-0076] **Distant surfaces render black in the 3D back-ends where Classic shows them lit.**
  Found 2026-06-26 (user testing, E1M1 first courtyard, Solid/Ultra). Confirmed by a same-position Classic-vs-Ultra toggle: Classic renders the distant far-side structures (perimeter walls / building tops just below the sky) fully lit; Ultra shows a horizontal BLACK strip across that band. Distinct from DOOM-0069 (which fixed the near-overhang ceilings — the big black band is gone). The post-DOOM-0069 lighting is shade = vLight * distLight with distLight = clamp(1 - vDist/3000, 0.35, 1.0): the 0.35 floor means distance alone cannot drive a normally-lit surface to pure black (0.35*vLight*albedo > 0), so the cause is something else — candidates: (a) a specific surface getting vLight≈0 or a black/wrong texel (atlas/UV), (b) an upper-wall between the sky courtyard (ceilingpic F_SKY1, height 216) and a lower non-sky sector that is mis-lit, (c) the two sky-ceiling sectors at different heights (216 vs 24 in E1M1) interacting badly, or (d) a no-backface-cull back-face of a distant wall. Next step: a surface-type color-code debug shader (ceiling=red / floor=green / wall=blue, lighting bypassed) to ID the black surface in ONE screenshot, then fix the root cause. NOTE: pre-existing before DOOM-0069 (independent of the directional-light removal); only became the most-visible artifact once the near ceilings were fixed.
  **Layman:** In the 3D renderers, the far structures across an outdoor area go solid black near the top, while Classic shows them normally lit.
  Kind: fix.
  Source: in-session-2026-06-26.
  Diagnosis narrowed by two debug probes (2026-06-26), both now reverted. (1) Surface color-code (ceiling=red/floor=green/wall=blue, lighting bypassed): the black region is NOT red/green/blue -> it is NOT wall/floor/ceiling mesh geometry. This REFUTES the sky-hack-upper-wall hypothesis (and the research aimed at that layer). (2) Yellow 3D-clear: the black stays BLACK (does not turn yellow) and NO yellow appears anywhere -> the 3D colour buffer is fully covered by sky + geometry (no 3D hole), and the black is the 2D OVERLAY (screens[0]) drawn opaquely over the 3D. So root cause is COMPOSITING, not geometry/lighting/sky: a rectangular region of screens[0] inside the displayed view holds index-0 (black) and is NOT the transparent key (RB_OVERLAY_KEY=251), so overlay.frag composites it over the 3D instead of discarding. The 3D view footprint is keyed each frame by Vulkan_RenderPlayerView's clear-to-key loop (r_backend.c:127), which fills only viewwidth x viewheight at (viewwindowx,viewwindowy) in screens[0]; overlay.vert/frag map screens[0] 1:1 to the full framebuffer. Both the black region AND the sky opening show hard screen-axis-aligned rectangular edges (a keying artifact, not world geometry). Suspect: the clear-to-key rect does not cover the full displayed 3D view -- likely a HIRES coordinate-space mismatch in viewwindowx/viewwindowy/viewwidth/viewheight (R_ExecuteSetViewSize, r_main.c:682-722; note the existing "blocks==10 gave a 320x168 view marooned in 640x400" HIRES comment) -- leaving a sub-region that keeps the stale black from RB_SetMode's memset(screens[0],0). NEXT: dump runtime viewwidth/viewheight/viewwindowx/viewwindowy/setblocks vs SCREENWIDTH/SCREENHEIGHT, find the uncovered band, and either extend the clear-to-key to the full overlay-visible area or fix the view-rect coordinate space.
  Resolved (2026-06-26, user-confirmed "huge improvement"). CORRECTION to the earlier same-day note: the two prior probes' "it's the 2D overlay" conclusion was WRONG. Re-probed this session with a clean toggleable shader: (1) tint sky yellow -> the big black region turned yellow = it IS the sky layer; (2) add geometry tint cyan -> zero black remained = the rest is mesh geometry, not overlay. Root cause = sky vertical mapping in mesh.frag: `row = clamp(vScreenUV.y*2.0*sz.y, 0, sz.y-1)` squashed the whole panorama into the top half (the *2.0) AND clamped everything below screen-centre to the texture's dark bottom row, painting a black band across distant outdoor views wherever the floor did not reach the horizon. Fix: map the sky at DOOM's fixed scale -- `row = 100.0 + (vScreenUV.y-0.5)*200.0` (skytexturemid row 100 pinned to the horizon at screen centre, one 320x200 logical pixel per texel) and REPEAT-wrap instead of clamp (below-horizon is always covered by floor). Mirrors classic r_sky (dc_texturemid=skytexturemid=100<<FRACBITS, dc_iscale=FRACUNIT). Confirmed by a wireframe debug pass (the residual was visibly geometry, not sky/overlay). Residual extra upper-wall geometry above outdoor fences (user-highlighted) split to a separate bullet.

- ✅ [DOOM-0077] **Add a 3D wireframe debug view toggled by the gamepad Share button.**
  Built while diagnosing DOOM-0076. r_vulkan.cpp gains a polygonMode-LINE variant of the world pipeline (g.wirePipeline), built only when the GPU advertises fillModeNonSolid (near-universal on desktop; the toggle no-ops otherwise). A C-linkage global rb_wireframe (default off) selects fill vs wire for the world+sprite draw; the sky stays a filled backdrop so wire triangles read against it. i_video.c maps the PS4 Share button (SDL_CONTROLLER_BUTTON_BACK) to flip rb_wireframe on its press edge; Start alone now opens the menu (Share no longer doubles as Escape). No effect in Classic (no Vulkan path). Builds clean, no warnings; proven in-session (the residual DOOM-0076 geometry was visible as wire triangles against the sky).
  **Layman:** A debug view that draws the 3D world as wireframe outlines, to see exactly what the renderer is (and isn't) building.
  Kind: feature.
  Source: in-session-2026-06-26.

- ✅ [DOOM-0078] **Remove extra dark upper-wall geometry above outdoor fences in Solid/Ultra (not in Classic).**
  Found 2026-06-26 (user testing, E1M1 courtyard), surfaced once the DOOM-0076 sky fix removed the larger black band. User highlighted several dark upper-wall segments sitting above mid-distance wooden fences/structures that Classic does not render. Confirmed it is mesh GEOMETRY (the cyan-tint probe + wireframe view both showed triangles there), not sky/overlay/lighting. The mesh upper-wall sky-hack in r_mesh.c (RB_BuildLevelMesh ~403) only skips an upper wall when BOTH front and back ceilings are skyflatnum; some outdoor-fence configuration is producing an upper quad that classic suppresses. NEXT: with the new wireframe toggle, identify the exact segs at that spot, dump front/back ceilingpic + heights + toptexture for those linedefs, and compare against classic r_segs.c upper-texture/sky handling (worldtop=worldhigh hack and the markceiling path) to find the missing skip condition. Likely a one-line guard in emit_wall's upper-step call site.
  **Layman:** In the 3D renderers, some short dark wall sections appear on top of distant outdoor fences/structures that the original game does not draw.
  Kind: fix.
  Source: in-session-2026-06-26.
  Resolved (2026-06-26): misdiagnosis -- NOT extra geometry. A 3D per-emit-path colour probe (upper=red/lower=green/mid=blue) plus a level-load dump showed the suspect dark quads above E1M1's outdoor fences are upper-step walls; every one has front ceilingpic=sky, back ceiling solid and lower, and a real toptexture (e.g. tex 70/14 on lines 29,30,115,117,119,159,212,272,476,479,480). Cross-checked classic r_segs.c: R_StoreWallRange sets toptexture whenever worldhigh<worldtop (after the both-ceilings-sky worldtop=worldhigh hack) and R_RenderSegLoop draws it when toptexture!=0 -- so classic draws these same uppers. RB_BuildLevelMesh's both-sky skip already matches the only suppression rule, so the mesh is faithful; no geometry change needed. User confirmed (clean build) the walls are textured, just dim with distance. Residual dimness is the placeholder distance-diminishing in mesh.frag (clamp to 0.35), owned by DOOM-0009's path-traced lighting -- not a geometry bug. Probe + dump reverted; tree clean.

- ✅ [DOOM-0079] **Fix unsafe per-frame reuse of the Vulkan present (renderFinished) semaphore.**
  Surfaced the moment VK_LAYER_KHRONOS_validation was installed (to satisfy DOOM-0009 INV-8): vkQueueSubmit signalled a single shared renderFinished semaphore while a prior vkQueuePresentKHR could still hold it (VUID-vkQueueSubmit-pSignalSemaphores-00067) — the per-frame fence tracks only the submit, not the presentation engine's consume. A latent Stage-1 (DOOM-0008) present-path defect, invisible before the layer went in.
  Resolved (2026-06-26): switched to one renderFinished semaphore per swapchain image, indexed by the acquired image index in both submit and present (the layer's own recommended fix). Image idx is only re-acquired after its previous present completed, so renderFinished[idx] is guaranteed unsignalled on reuse. New CreateRenderFinishedSemaphores() helper sizes the set to the swapchain image count and is re-run on swapchain recreate. Verified: 0 validation lines over a multi-second RT3D run on the RX 6600/RADV (was several per frame).
  **Layman:** A behind-the-scenes timing-signal bug in the 3D renderer that the new GPU error-checker caught; harmless-looking but technically incorrect, now fixed.
  Kind: fix.
  Source: in-session-2026-06-26.

- 💭 [DOOM-0080] **Replace the 2D billboard enemy sprites with real 3D monster models in the Solid/Ultra renderers.**
  Classic DOOM draws monsters as flat camera-facing sprites (8 rotation angles per animation frame). In the 3D tiers these read as cardboard cutouts up close and under ray-traced lighting. Goal: substitute real 3D models with skeletal animation mapped to DOOM's existing state/animation machine (spawn, walk, attack, pain, death frames), lit and shadowed by the path tracer like the rest of the scene.
  
  HARD and FAR-OUT — parked deliberately late. Open problems: (1) Sourcing — id's monster models/sprites are proprietary and CANNOT ship in this GPL-v2 repo, same constraint as DOOM-0042's art set; routes are CC0/free model packs, commissioned/hand-authored originals, or AI-assisted modelling, none of which has good DOOM-monster coverage today. (2) Rigging + animation retargeting onto the 2D sprite-frame timing so behaviour stays vanilla. (3) Per-tier: only the 3D renderers swap models; Classic keeps the original sprites (parallel-asset rule, like DOOM-0042). Pairs with DOOM-0042 (HD art set) and depends on the DOOM-0009 path tracer being in place. Approach to be researched with the user when the tier reaches it — not committed in detail yet (hence Considered, not Planned).
  **Layman:** Way down the line: turn the flat cardboard-cutout monsters into proper 3D models that look right from every angle.
  Kind: feature.
  Source: user-request-2026-06-27.
  Scope BROADENED + tier-bound (user 2026-07-14): not just enemies — in ULTRA, replace ALL sprites with 3D models: enemies/monsters, barrels, ALL pickups (health/armour/ammo/weapons/keys/powerups), decorations/scenery, and projectiles. Bind to the Ultra tier (parallel-asset rule, mirroring DOOM-0042's Ultra HD-art binding): Classic + Solid keep the original 2D billboards; Ultra swaps in 3D models. Still 💭/HARD/FAR-OUT and unspecced — the gating blocker is unchanged and now WIDER: freely-licensed (CC0/GPL-safe) 3D models with good coverage of DOOM's SPECIFIC roster (imp/demon/cacodemon/baron/… + barrels/medkits/etc.) barely exist, so expect heavy hand-authoring / AI-assisted modelling. Depends on DOOM-0042's material pipeline (models need PBR materials too) + the DOOM-0009 path tracer, and on per-object transforms in the TLAS (today all sprites share one camera-facing billboard BLAS — see DOOM-0100). Needs its own brainstorm → spec → cold-eyes cycle later; NOT part of the DOOM-0042 texture spec (which correctly keeps enemies as billboards). New sourced models go in /mnt/Games/3D Engine Assets/Models/ (CC0/free only) per ASSET_CATEGORIES.md.
  Asset-source scout (2026-07-20): dengine.net/addons (Doomsday Engine
  resource packs) evaluated as a model source and rejected — do NOT
  re-investigate. Every notable pack is unusable for a GPL project on two
  counts: (1) licensing — the one pack with a real licence (DHMP, the DOOM
  High-Res Model Project) is CC BY-NC-SA (NonCommercial + a non-GPL
  ShareAlike, both GPL-incompatible); the rest (jDRP, Abbspack, jDUI, the
  music/SFX packs, Hexen/Heretic packs) state NO licence = all-rights-
  reserved, or "fair-use for non-profit" (not a grant). (2) They are
  derivative works of id/Raven/Bethesda DOOM art (fan recreations of the
  actual monsters), so the underlying IP is Bethesda's regardless of the
  repacker's stamp — and Bethesda enforces DOOM IP. Formats: Doomsday uses
  DMD/MD5 models bound via DED text defs (engine-specific), PNG/DDS
  textures. Bottom line: the model scarcity that blocks this item is real;
  this source does not relieve it. Clean path stays original sculpts under a
  licence we control (CC0 / CC-BY / GPL-compatible) — mirrors the DOOM-0042
  CC0-hero-texture approach. See also DOOM-0042 (HD materials): dengine's
  DHTP texture pack is the least-bad there but still a per-author
  attribution patchwork over DOOM-shaped art — not worth vendoring.

- ✅ [DOOM-0081] **Polish three pre-existing DOOM-0009 spec nits surfaced by the 2026-06-27 cold-eyes re-review.**
  Pre-existing §3/§4 items in docs/specs/DOOM-0009-path-tracer.md, outside the 2026-06-27 §2 tier-model revision's scope, deferred rather than reopened: (1) build step 5 (muzzle-flash dynamic delta) never names where the muzzle's WORLD position comes from — the verify ("shadow direction tracks the muzzle as the player rotates") is unbuildable without it; name the engine source (likely player->mo position + weapon state) or add it to §9 open questions. (2) §4.3 calls the REJECT-driven NEE candidate set "exact" — REJECT is conservative sector-to-sector visibility, not point-exact occlusion; reword "exact"→"conservative cull" to match the research doc's own caveat. (3) §3/§4 cite `DOOM-0008 §"The path tracer"` as a bare label; anchor it to docs/specs/DOOM-0008-3d-renderer.md + heading like the ADR-0001 citations. All three are LOW/MEDIUM polish; none blocks implementation.
  **Layman:** Three small wording/accuracy tidy-ups in the path-tracer design doc, found during review but not urgent.
  Kind: doc-fix.
  Source: cold-eyes-2026-06-27 (DOOM-0009 §2 revision, lane A residuals).
  Progress (2026-06-28): the 2026-06-28 §4.4 upscaler-decision cold-eyes
  pass surfaced more pre-existing DOOM-0009 spec drift to bundle here: (1) build-
  step statuses are stale — §7 marks only step 1 shipped while steps 2-6 (BLAS/
  TLAS, NEE, GI bake, dynamic delta, SVGF 6a/6b) are implemented on disk; (2) §2's
  tier×RT contract prose is restated 3-4x (lines ~40-116) — candidate for a
  collapse; (3) "A-SVGF" (spec) vs the shipped svgf_* shader names — add a one-line
  note that the adaptive anti-ghosting ("A") lands in step 6, or reconcile the name;
  (4) INV-6's "raise spp if 4096 shows visible noise" escape clause is subjective.
  The §5 formulas/ "does not exist yet" staleness was fixed inline this session.
  Resolved (2026-07-04): five of six nits fixed in docs/specs/DOOM-0009-path-tracer.md — (1) §4.1 muzzle world-position source named (camPos+camDir·MUZZLE_FORWARD−drop, per pathtrace.comp muzzleFlashDelta; extralight the gate); (2) §4.3 'exact'→'conservatively-culled' REJECT wording; (3) §3 DOOM-0008 §'The path tracer' citation anchored to the full doc path; (4) §7+§9 build-step statuses audited to on-disk reality (steps 1-5 shipped, step 6 TAAU-only/FSR pending, step 7 DOOM-0090; MIS/RR N/A); (5) §4.4 A-SVGF→svgf_* shipped-name note; (6) INV-6 subjective 'visible noise' → objective 8192-spp doubling-convergence test. The remaining sub-item — §2 tier×RT prose collapse — deferred to DOOM-0168 (too large for a surgical doc-fix; bundled with TOC + §4.4 sentence-density). 5-loop cold-eyes to clean; 0 CRITICAL/HIGH.

- ✅ [DOOM-0082] **Activated switches/buttons emit a faint coloured glow (red buttons glow red).**
  Folds into the DOOM-0009 path-tracer emitter work (build step 3b/3c). The
  per-material emissive precompute auto-derives a switch's lit-variant texture
  colour (red buttons -> red Le), and the mesh already re-reads each surface's
  live texture per frame (DOOM-0066), so the glow should engage on activation.
  Two pieces to verify when 3c lands: (1) the lit button texture actually crosses
  the emissive brightness threshold (data-dependent — may need a switch-texture
  allow-list or a lowered threshold for the "faint" case); (2) the emitter set
  tracks the live switch texture swap (event- or per-tic refresh) so a pressed
  button becomes an emitter. Intensity is a tunable ("faint").
  **Layman:** When you press a lit button or switch, it gives off a soft glow in its own colour — a red button casts a faint red light — instead of staying flat.
  Kind: feature.
  Source: user-request-2026-06-27.
  Progress 2026-07-01: the live-swap emitter refresh (the missing mechanism) is
  implemented. RB_UpdateMeshHeights (r_mesh.c) now returns RB_UPD_RETEX when a
  wall/flat live texnum changes — a switch press, a button REVERT, or an animated
  texture — and the RT back-end rebuilds the static NEE emitter set from the LIVE
  vertex buffer (new BuildStaticEmitterSet on g.vbufMapped, r_vulkan.cpp) so a
  now-lit switch face enters the light set and a reverted one drops out. The scan
  was extracted into BuildStaticEmitterSet (level-load reads baked verts; the
  refresh reads live). NO hardcoded switch list — the glow colour is auto-derived
  from each texture's bright texels (ComputeMaterialEmissive, VALUE/max-channel so
  saturated red/green is caught), which matches the user's switch taxonomy
  (2026-07-01): plain up/down LEVERS have no lit region -> no glow (correct); red
  button + indicator lamp -> red glow; green button -> green glow; DOOM2 green+red
  -> both; timed buttons revert through the same retex path so the glow switches
  off. Builds clean (0 warnings), nee_sampling_test green. PENDING on-HW verify on
  the RX 6600: confirm lit SW2 textures actually glow, and TUNE — a small/"faint"
  lit region may fall below kBrightLum=0.5 / kEmitterMinLum=0.02, needing a lowered
  threshold or a switch allow-list. Bonus: animated slime/panels (DOOM-0083) now
  refresh their emitters via the same retex path.
  HW result 2026-07-01 (user): a pressed switch LIGHTS UP (SW2 texture shows) but
  does NOT cast light onto its surroundings. Diagnosis: the live-swap emitter
  refresh works, but the lit switch's material Le stays below kEmitterMinLum — its
  bright indicator is a few texels and ComputeMaterialEmissive AREA-AVERAGES, so it
  never enters the NEE set as a caster. Shared root cause with the new colour-cast
  item (over-permissive/area-averaged emissive derivation): fixing that derivation
  (peak near-fullbright gate instead of area-average) should make the switch cast
  AND stop coloured walls from flooding the room. Tune together on the RX 6600.
  Progress 2026-07-03b: implemented the peak-region emitter gate the 2026-07-01 HW diagnosis called for. Extracted the per-material emissive derivation out of r_vulkan.cpp into a testable header (emissive_derive.h, mirrors nee_sampling.h). QUALIFICATION now gates on a genuine near-fullbright region (kEmitterPeakLum=0.9, min bright-texel fraction) instead of the area-AVERAGED Le magnitude: a switch's small fullbright indicator now qualifies (previously drowned below kEmitterMinLum -> no glow), while a uniformly-tinted wall with no fullbright texel is now rejected (stops the colour flood). Le colour/intensity derivation unchanged, so lamps that already worked keep their brightness (test asserts lamp Le==40 unchanged; switch Le red 0.195 = faint). New tests/emissive_derive_test.cpp proves switch-qualifies-faint / tinted-wall-rejected / lamp-strong / dark+speck-rejected / hue-correct / sub-rect stride — make test green, full engine builds clean 0 warnings. Committed locally, NOT pushed: the peak threshold + faint intensity are on-HW dials and real-WAD content may need tuning (some dim large surfaces may drop from the emitter set) -> user visually verifies a pressed switch casts red onto the wall/floor in Ultra RT (~ key), tunes kEmitterPeakLum / kEmissiveScale, then push + flip. Same mechanism now also helps DOOM-0083 (slime) and the sprite-eye false-negative.
  User verify 2026-07-04 (post peak-gate build): a pressed switch/button still does NOT visibly cast light on its surroundings -- "it would have to be significantly brighter to cast light. So, for now, let's leave it as is." So the peak-region gate correctly makes the lit switch an EMITTER (it no longer drops out of the NEE set), but its area-averaged Le is far too faint to read as a room light. User has DEFERRED further work -- do NOT tune now. When revisited: the switch's bright indicator is a small fraction of the tile, so even qualifying, kEmissiveScale*(bright fraction) is tiny; would need either a per-emitter minimum-brightness floor for small-but-genuine emitters, or a much higher kEmissiveScale for the switch class, tuned on-HW. Parked at user request; item stays open (partial: emitter membership fixed, visible cast not achieved).
  Resolved (2026-07-12, user-confirmed): activated switches/buttons emit their coloured glow. In the Ultra RT view the glow does not visibly spill onto the surrounding environment, but the switch emission is dim enough that the user is happy to sign off as-is (revisit if a brighter switch light is wanted later).

- 📋 [DOOM-0083] **Green slime/nukage emits a faint green glow onto its surroundings.**
  Same DOOM-0009 path-tracer emitter mechanism as [DOOM-0082]: the per-material emissive precompute (build step 3b) auto-derives a green Le from the slime/nukage flats' bright green texels, so those surfaces become NEE area emitters and cast a faint green tint on neighbours. Two data-dependent checks when 3c lands: (1) the nukage/slime flats (NUKAGE1-3, and bright-green floor flats) actually cross the emissive luminance threshold (may need a per-flat allow-list or lowered threshold for the "faint" case, since slime is darker than a lamp); (2) animated slime flats cycle via flattranslation each tic (DOOM-0066) — the emitter set should track the live flat so the glow animates. Intensity is the same "faint" tunable as DOOM-0082. Distinct from DOOM-0082 (switches/buttons) in surface class (environmental animated floor flats) but shares the implementation.
  **Layman:** Glowing green slime pools cast a soft green light on the nearby floor and walls, instead of looking flat.
  Kind: feature.
  Source: user-request-2026-06-27.
  Progress (2026-07-17): implemented as DOOM-0183 L2 (forced-constant green Le on NUKAGE1-3 via ComputeMaterialEmissive -> enters the NEE emitter set + self-glows). Code committed/pushed (ecf9a6c); graduates to shipped when DOOM-0183 clears its hardware play-test.

- 📋 [DOOM-0084] **Make free-standing light objects (floor lamps, torches, burning barrels) emit light in the path tracer.**
  DOOM-0009's NEE emitter list (BuildEmitterList) is extracted only from the
  static walls+flats BLAS, so Thing/sprite objects are excluded. The brightest
  *visible* lights in many rooms — TLMP/TLP2 floor lamps, candelabra, burning
  barrels — are map Things rendered as billboards (per spec §line123, sprites
  live in the per-frame TLAS, not the static BLAS). To light them: tag the
  emissive sprite Things, derive a per-Thing Le (same area-weighted bright-texel
  mean as ComputeMaterialEmissive), and add them as point/area emitters in the
  NEE light set each frame (or via the per-frame TLAS instances). Pairs with
  DOOM-0010 (dynamic/flicker) and DOOM-0043 (scene lights). Sector-light ambient
  fill already lifts these areas; this gives them real local pooling + shadows.
  **Layman:** The tall lamp stands, candles and burning barrels you see around the map are separate objects, not part of the walls or floor — so the new lighting doesn't treat them as lights yet. This makes them glow and cast light like the ceiling lights already do.
  Kind: enhancement.
  Source: user-observation-2026-06-27 (3c-1 visual verify: lamp stands not lit).

- 💭 [DOOM-0086] **Increase the resolution of the title screens and in-game menu graphics.**
  DOOM's title/intermission screens (TITLEPIC, INTERPIC, the M_DOOM logo) and menu graphics (M_* patches, the menu/HUD fonts) are 320x200-era paletted lumps. With the hi-res framebuffer (DOOM-0027, 640x400) and the 3D tiers they are point-upscaled and visibly blocky. Goal: supply higher-resolution replacement art for the title/intermission screens and menu/HUD patches, drawn at native display resolution instead of upscaled from the 320x200 lumps. Likely a parallel-asset set selectable alongside the originals (same rule as DOOM-0042's HD art and DOOM-0026's Classic toggle), so the authentic low-res look is preserved.
  Open: sourcing the art (hand-authored / AI-upscaled originals — id's art cannot ship, cf. DOOM-0042), and the draw path (load hi-res patches + a higher-res font, or render menu text via a scalable font). Distinct from DOOM-0045 (menu compositing in 3D) and DOOM-0053/0055 (hi-res ghosting/smearing fixes), which concern where/whether the existing low-res art draws, not its resolution.
  **Layman:** The DOOM logo, title/intermission pictures and menu text are still tiny 1993-resolution images, so they look blocky on a big modern screen. This swaps in sharp, high-resolution versions while keeping the option of the original look.
  Kind: enhancement.
  Source: user-request-2026-06-28.

- 📋 [DOOM-0088] **FSR 3 frame generation (interpolated frames), gated behind DOOM-0048.**
  Deferred out of DOOM-0009 by the 2026-06-28 §4.4 decision. FSR 3 frame
  generation is invasive (proxy swapchain, HUD/UI composition handling, frame
  pacing) and has no clean seam while the engine is present-locked to the 35 Hz
  tic. HARD-GATED behind DOOM-0048 (decouple render rate). The RX 6600 (RDNA2)
  supports it (vendor-agnostic, shader-based). Distinct from the upscaler axis
  (TAAU / FSR 2 / FSR 3.1) which ships in DOOM-0009 step 6.
  **Layman:** An optional mode that inserts AI-guessed in-between frames to make motion look smoother — kept separate from the sharpness upscaler, and only after the engine can render faster than the 35-times-a-second game clock.
  Kind: feature.
  Source: in-session-2026-06-28 (DOOM-0009 §4.4 upscaler decision).

- ✅ [DOOM-0089] **Smooth (pixel-art-aware) upscaling of the 2D title/HUD/menu overlay in the 3D renderers.**
  The 2D overlay (screens[0]: the title screens, menus, HUD, intermission/finale) was point-sampled (VK_FILTER_NEAREST) when composited over the 3D scene, so it looked blocky/stair-stepped while the traced world behind it was crisp. overlay.frag now does a sharp-bilinear (pixel-art-aware) upscale: it decodes the 2x2 neighbourhood of palette indices to RGB and blends IN RGB (bilinearly blending raw palette indices would interpolate to unrelated colours), with two guards that keep it correct — (1) the nearest texel alone decides the transparent-key discard, so the 2D/3D boundary stays exactly as crisp as point sampling, and (2) key (magenta 251) taps are excluded from the colour blend so transparency never bleeds into a smoothed edge. The fractional sample coords are compressed into a ~1-display-pixel band via fwidth(), so texel interiors stay crisp and only the seams between texels are anti-aliased — sharper than plain bilinear, far smoother than NEAREST. Self-contained (one fragment shader; no host/pipeline change). NOTE: this cleans the SCALING of the original 320x200 art; genuinely higher-detail title/menu art is the separate HD art set (DOOM-0042), and higher-internal-resolution text rendering is a possible later step. Build clean (glslc + g++, 0 warnings); cannot run/validate here (no GPU/WAD) -> PENDING USER ON-HARDWARE VERIFY: title screen + main menu look smooth (no blocky stair-steps) with crisp transparent edges over the 3D view, and the classic look is preserved (not blurry).
  **Layman:** The menus and title screens now look smooth instead of blocky when shown over the 3D view.
  Kind: enhancement.
  Source: in-session-2026-06-28.

- 🚧 [DOOM-0090] **Profile and reduce the path-tracer megakernel's occupancy / VGPR pressure on the RX 6600.**
  From the 2026-06-28 verified research (docs/research/DOOM-0009-rt-denoiser-upscaler-bestpractices.md §1a). RDNA2 caps at 16 wavefronts/SIMD with only ~1024 VGPRs, so the megakernel's single hottest register hot-spot throttles the whole kernel — the occupancy penalty is STRONGER on the RX 6600 than the RDNA3 examples in the literature. Actions: profile occupancy with Radeon GPU Profiler; ensure exactly one live rayQueryEXT in scope (AMD guidance); demote rarely-used hot paths out of the megakernel's max-VGPR footprint; keep the kernel BOUNDED (the megakernel-vs-wavefront refutation in the research was scene-specific — the megakernel choice is validated for DOOM's low-divergence matte art, but an unbounded one is the risk). Feeds DOOM-0009 step-7 perf pass. Cited: AMD GPUOpen RDNA performance guide + occupancy-explained; Laine et al. 2013.
  **Layman:** Make the ray-tracing GPU program use fewer registers so more of it runs at once — the single biggest speed lever on this card.
  Kind: perf.
  Source: research-2026-06-28.
  Progress (2026-06-29): instrumentation half landed + pushed (e720b4e). Added an opt-in per-pass GPU profiler (the `\` key, persisted as rt_profile) that timestamps the four path-tracer stages — sprite-AS rebuild / megakernel trace / denoiser chain + TAAU / label + blit — and prints per-frame averages to the terminal once a second. Read back stall-free under single-frame-in-flight. Next: read the breakdown on the RX 6600 at 50% render scale and reduce the proven hotspot (megakernel VGPR/occupancy needs RGP; denoiser a-trous iteration count is in-code tunable).
  Progress (2026-06-29): profiled with the new rt_profile timer on the RX 6600 @ 50% scale. Megakernel is dominant and scene-scaling: ~16.7 ms facing a wall (36 fps) vs 80-110 ms facing a glowing-prop room (8-9 fps); denoise+taau secondary (7-37 ms); sprites/blit negligible (<0.5 ms). Root cause: the omni sprite-light NEE loop cast one shadow ray per in-view emissive sprite, per pixel, with no cull -> O(sprites)/pixel. First optimization shipped (7ac4a13): sampleEmitter now skips a sprite's shadow ray when its unshadowed contribution is below OMNI_CULL_VALUE (omni path only; static + INV-6 verify unchanged). Awaiting re-profile to quantify. Remaining levers if still short of the 60-fps floor: stochastic cap on omni count (bound worst case), reduce static NEE_SAMPLES (6), trim SVGF a-trous iterations (5), and the RGP-based VGPR/occupancy pass.
  Validated (2026-06-29): apples-to-apples at a CONSTANT 50% render scale, the same light-heavy library view went from ~20s fps (pre-cull) to 35-42 fps (post-cull, 7ac4a13) on the RX 6600 — roughly 1.6-1.9x. (NB: the earlier 8-9 fps figure was at 100% scale, so an 8-9 -> 40 comparison conflates the resolution drop with the optimization and is NOT the cull's doing.) Post-cull breakdown @50%: megakernel ~14 ms, denoise+taau ~8.5 ms, sprites/blit <0.6 ms; omni count 22-36 of ~104-118 total emitters. Clean same-scale 100% re-profile confirms the cull: megakernel ~80-110 ms -> ~54-65 ms (~1.5x), frame 8-9 -> 10-11 fps; denoise+taau unchanged at ~36 ms (the cull doesn't touch it). The ~1.5x megakernel ratio holds at both 50% and 100%. KEY FINDING: at 100% the SVGF denoiser is a ~36 ms slab (~37% of frame) and is now the second hotspot; it scales ~linearly with render pixels (~8.5 ms at 50%). Next levers (all visible quality tradeoffs, tune with user): trim SVGF a-trous 5->3/4 (cuts the denoiser slab); reduce static NEE_SAMPLES 6->4; the RGP VGPR/occupancy pass for the residual megakernel cost.
  Follow-ons shipped (b85d640): (1) default the upscaler to TAAU @ 50% render scale (was off/100%) so Ultra boots playable (~35-42 fps) instead of ~8 fps at native — defaults only affect fresh configs, saved settings untouched, still adjustable in Options->Renderer. (2) trimmed SVGF a-trous 5->4 (dropped the coarsest, least-visible pass) to shave the denoiser slab. Deliberately did NOT cut NEE samples too (fewer samples + lighter denoiser fight each other). Pending play-test: confirm fresh-config Ultra boots at 50%, and that the 5->4 denoiser change shows no visible quality regression + re-profile denoise+taau.
  Validated (2026-06-29): the 5->4 a-trous trim is quality-free — user reports no visible change, denoise+taau dropped ~8.5 -> ~7.5 ms @50%, frame now ~37-46 fps in the library view. Quick-win phase done. Current @50% budget: megakernel ~15 ms (now the clear #1), denoise ~7.5 ms, fixed <0.7 ms. Hitting a locked 60 (~16.6 ms budget) needs a structural change, not micro-trims: ReSTIR DI (DOOM-0092 research) to cut the megakernel's many-light NEE cost; RGP VGPR/occupancy pass (needs AMD profiler on-HW); or FSR3 frame-gen (DOOM-0088, gated on DOOM-0048). Stopping perf micro-opt here unless asked.

- 🚧 [DOOM-0091] **Compact the BLAS and rebuild the TLAS on the compute queue (RT acceleration-structure best practice).**
  From the 2026-06-28 verified research (§1b). BLAS compaction (build with VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR -> query compacted size -> compact) typically saves 20-50% AS memory. Rebuild the small TLAS each frame on the COMPUTE queue (AMD guidance) rather than the graphics queue. The current transform-only door/lift refit (ALLOW_UPDATE) is validated and stays — but note the hard limit: transitioning a primitive active<->inactive cannot be done by update and forces a full rebuild (relevant if a sector ever culls geometry in/out). Counterpoint to weigh on-HW (Intel): a budgeted full per-frame TLAS rebuild can be more frame-time-stable than relying on updates. Cited: AMD GPUOpen RDNA guide; Khronos Vulkan RT best-practices; Vulkan AS spec.
  **Layman:** Shrink the ray-tracing data structures (20-50% less GPU memory) and rebuild them on a cheaper queue.
  Kind: perf.
  Source: research-2026-06-28.
  Progress (2026-06-29): BLAS compaction shipped. The static world BLAS is now built with ALLOW_COMPACTION_BIT, its compacted size queried (VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR), and copy-compacted into a right-sized AS at level load (r_vulkan.cpp BuildAccelerationStructures). ALLOW_UPDATE is retained so moving-sector refits still run on the compacted AS (verified safe per NVIDIA RTXMU / nvpro: compaction + update are not mutually exclusive). The per-frame sprite BLAS stays non-compacted (a query round-trip would stall the frame). Build prints "BLAS (N tris, X->Y KiB compacted)" so the win is observable at runtime. DEFERRED: "rebuild the TLAS on the compute queue" — the engine creates a single graphics+present queue (queueCount=1, r_vulkan.cpp PickPhysicalAndDevice), so moving the per-frame TLAS rebuild off the graphics queue needs a second queue + per-frame cross-queue ownership transfers/semaphores (medium/high risk); the research body itself flags that a budgeted per-frame rebuild on the graphics queue can be more frame-time-stable. Re-evaluate the compute-queue half only if profiling (DOOM-0090) shows the on-graphics-queue TLAS build is a bottleneck. Item kept in-progress for that remaining half.
  Note (2026-08-05, upstream review): do NOT go looking at GZDoom or
  UZDoom for this. Both ship the same 279-line
  src/common/rendering/vulkan/renderer/vk_raytrace.cpp, and it builds its
  acceleration structures with PREFER_FAST_TRACE and nothing else -- no
  compaction query pool, no compute-queue build, no update/refit path. We
  are ahead of them here, so this item has to be designed against the
  Vulkan spec and vendor guidance rather than copied.

- ✅ [DOOM-0092] **Research: ReSTIR DI/GI cost on RDNA2 and the static SH-L1 bake vs a dynamic DDGI probe field.**
  Coverage GAP from the 2026-06-28 research (§4): no external claims on this axis survived verification within budget, so it needs its own pass. Open questions: (1) the measured register-pressure / occupancy + frame-time cost of adding ReSTIR DI (then GI) to the inline-ray-query megakernel ON the RX 6600 specifically -- the spec's own §4.4 already flags ReSTIR as RDNA2's worst register case, so this must be MEASURED, not assumed (it interacts directly with the occupancy item above); (2) whether per-subsector SH-L1 probes go stale when doors/lifts change local visibility, and whether a dynamic DDGI/irradiance field is worth its cost; (3) an external correctness check on the NEE + power-importance + MIS variance. Decide before any Stage-3 / DOOM-0012 ReSTIR work.
  **Layman:** Investigate whether smarter light sampling and dynamic light-probes are worth it on this card before committing to them.
  Kind: research.
  Source: research-2026-06-28.
  Resolved (2026-06-29): research complete — docs/research/DOOM-0092-restir-cost-benefit.md (4 cold-eyes loops to clean). DECISION: defer full ReSTIR; build the cheap ladder first — REJECT cull (DOOM-0119) -> RIS without reservoirs (DOOM-0120). Full ReSTIR is reconsidered only if RIS still misses 60 fps AND the RGP capture (DOOM-0090, guide added at docs/research/DOOM-0090-rgp-capture-guide.md) shows occupancy headroom by the doc's numeric VGPR/wave gate. The premise flipped: this session MEASURED ~104-118 emitters in a light-heavy room (not 'a few lights'), but the cheap end of the resampling family captures the win at a fraction of ReSTIR's RDNA2 register cost. ReSTIR GI stays deferred (indirect is baked). Q2: keep the static sector-keyed bake + event-driven lazy re-bake on door/lift moves (DOOM-0121); NO continuous DDGI field (leaks + per-frame cost). Q3: 'MIS' is N/A (shipped integrator is pure-Lambert NEE-only) and INV-6 proves only the STATIC selection unbiased — the omni path is unverified (verify runs omniStart==emitCount). Spawned: DOOM-0119..0125 (REJECT, RIS, lazy re-bake, INV-6 omni gap, omni-cull bias quant, spec-MIS doc-fix, perf-doc citation sweep).

- 📋 [DOOM-0093] **Harden the path tracer against untrusted WAD data and GPU memory-safety / device-loss risks.**
  Coverage GAP from the 2026-06-28 research (§5): no claims survived verification within budget, but the axis matters because WADs are UNTRUSTED input that drives emitter-list extraction and acceleration-structure builds. Needs a dedicated pass: (1) GPU memory-safety / out-of-bounds with buffer_reference + bindless descriptor indexing (bounds checks, robustBufferAccess, descriptor-indexing partial-bound hazards); (2) a systematic NaN/inf hardening pass across the tracer (we clamp in places; make it principled -- and mine the Vulkan robustness guide + NVIDIA driver-level RT validation); (3) defensive AS-build limits against degenerate / huge geometry from a crafted WAD causing DoS / device-loss (TDR). Tie to the validation-clean invariant (INV-8).
  **Layman:** Make sure a malformed or malicious level file can't crash the graphics driver or read out-of-bounds GPU memory.
  Kind: security.
  Source: research-2026-06-28.
  Note (2026-07-26, debt sweep): 11 source comments across w_wad.c, d_net.c,
  i_net.c, i_sound.c, p_spec.c, d_main.c and mus2mid.{c,h} cite "DOOM-0093"
  for the 2026-07-23 CPU-side hardening pass, which actually shipped under
  DOOM-0212..DOOM-0220. The citations are self-consistent (DOOM-0093 was the
  umbrella research item that motivated the pass), so they were left as-is
  rather than re-pointed at 11 sites. THIS item's own scope is unchanged and
  still 📋: it is the GPU-side axis — buffer_reference/bindless bounds,
  NaN/inf hardening, AS-build limits against a crafted WAD. A reader who
  follows a comment here should look to DOOM-0212..0220 for the CPU fix.

- ✅ [DOOM-0094] **Draw the 2D presentation layer (HUD, menu, FPS, weapon) over the path-traced view.**
  The path-tracer present path (RecordRtTrace) writes only the traced WORLD to the swapchain (compute -> blit) and skips the entire 2D + sprite presentation the raster path draws: the 2D overlay screens[0] (HUD + in-game menu + on-screen messages + the DOOM-0046 FPS counter are ALL composited from screens[0]) and the weapon viewmodel billboard (RB_BuildPSprites). So the `~` trace modes show no HUD, menu, FPS or gun/hand — it reads as a diagnostic, not a playable mode. Plan: after the trace blit, run a render pass over the swapchain (loadOp=LOAD on colour to keep the trace, loadOp=CLEAR on depth) that draws (1) the weapon billboard via the existing world pipeline (depth-cleared so it sits on top, as the player weapon always does) and (2) the overlay compositor (existing overlayPipeline) — reusing g.framebuffers (a LOAD-variant render pass is format-compatible). Enable the screens[0] staging copy + RB_BuildPSprites for rtActive (currently gated !rtActive). SCOPE: the weapon (a screen-space psprite) is in; WORLD sprites (monsters/items) are NOT — they need correct depth occlusion against the traced world, which means putting them in the TLAS (DOOM-0084) or a depth-aware billboard pass, tracked separately. Relates to but distinct from DOOM-0050 (overlay ghosting in the raster 3D modes).
  **Layman:** In the ray-traced view you currently see only the world — no menu, HUD, FPS counter or your gun. Bring those back so it's a real playable view, not a bare diagnostic.
  Kind: fix.
  Source: user-request-2026-06-28.
  Progress (2026-06-28): implemented in r_vulkan.cpp. Added a LOAD-variant render pass (g.rtOverlayPass: colour loadOp=LOAD to keep the traced blit, depth cleared), format-compatible with g.framebuffers + the world/overlay pipelines. After RecordRtTrace, RecordRtOverlay() draws (1) the weapon viewmodel psprite via the world pipeline and (2) the existing overlay compositor. Enabled RB_BuildPSprites + the screens[0] staging copy for rtActive (weapon only; sky + world sprites intentionally excluded per scope). Extracted UploadOverlayImage() shared by both present paths (raster path emits identical commands -> INV-10 preserved). Builds clean (-Wall, 0 warnings). PENDING: visual confirmation on the RX 6600 that HUD/menu/FPS/weapon show in the ~ Ultra view; flip to shipped after that.
  Resolved (2026-06-28): verified in-game on the RX 6600 — the ~ path-traced view now shows the HUD, the weapon (hand+gun), and the FPS counter. World sprites (enemies/pickups/barrels) are correctly absent: out of scope here, tracked under DOOM-0084 (they need TLAS depth occlusion). FPS ~27 at TAAU 75% is a perf data point for DOOM-0090, not a defect.

- 📋 [DOOM-0095] **Per-weapon muzzle-flash colour (and position) in the path tracer — BFG green, plasma blue.**
  DOOM-0009 step 5 ships a single fixed muzzle flash: colour FLASH_COLOR=(1.0,0.85,0.6) warm + a fixed barrel offset (MUZZLE_FORWARD=24, MUZZLE_DROP=8), identical for every weapon, because the shader is only told the engine's `extralight` scalar (firing brightness) via misc2.z — the weapon TYPE (player->readyweapon) never reaches the renderer. Enhancement: forward readyweapon into the RT push constants and select the flash colour per weapon — warm yellow (pistol/shotgun/SSG/chaingun/rocket), BLUE (plasma rifle), GREEN (BFG9000); fist/chainsaw emit no flash (already gated by extralight). Optional: small per-weapon intensity/position (BFG's large slow flash vs the chaingun stutter; the BFG could also tint via its own emissive). Small, localised: one host push-constant field + a colour lookup in pathtrace.comp's mode-4 and mode-6 flash blocks (keep the two in lockstep). Tunable colours are INV-7 backfill candidates (a Vestige Workbench pass) like the other scene-light constants."
  **Layman:** Make each gun's muzzle flash the right colour — the BFG glows green, the plasma rifle blue, the rest warm yellow — instead of every gun flashing the same warm colour.
  Kind: enhancement.
  Source: user-request-2026-06-28.

- ✅ [DOOM-0096] **User-adjustable brightness slider for the path-traced (Ultra/denoiser) view.**
  The denoiser composite tonemap (svgf_composite.comp) hard-coded EXPOSURE_EV = -2.25, which read a little dark. Make it user-adjustable: a Brightness thermometer slider in the Renderer menu backed by a new rb_exposure tunable (0..15, persisted to the config as rt_brightness). The host maps the slider to a photographic EV [-4.0, -0.25] (pos 7 == the old -2.25) and passes it to the composite via the spare misc3.x push-constant slot (bit-cast float); the shader replaces its const with uintBitsToFloat(pc.misc3.x). Default pos 10 (~ -1.5 EV), one step brighter than before, per user request. Scope: the denoiser/Ultra view (svgf_composite, mode 6); the debug ~ modes 1-4 (pathtrace.comp) keep their fixed exposure. Source: user-request-2026-06-28.
  **Layman:** The ray-traced view looked a little dark. Add a Brightness slider to the Renderer settings menu so you can dial it to taste; it now defaults a touch brighter.
  Kind: feature.
  Source: user-request-2026-06-28.
  Resolved (2026-06-28): shipped + user-tested. Brightness slider added to the Renderer menu (rb_exposure 0..15 -> EV [-4.0,-0.25] via svgf_composite misc3.x; persisted as rt_brightness; default pos 10 ~ -1.5 EV). User confirms the default view is now slightly brighter and the slider works; effect is subtle and the view is still somewhat dark, but they chose to leave the range as-is — the player flashlight (DOOM-0044) is the intended fix for dark corners. Range/mapping is a one-line change if a punchier slider is wanted later.

- 📋 [DOOM-0098] **Make the Ultra ambient floor shadow-aware so it stops flattening contrast.**
  Refines DOOM-0043. The current ambient floor (max(GI, sectorLight*AMBIENT_SECTOR_SCALE) in pathtrace.comp modes 4 + 6) is added FLAT to every surface in a sector, so within a lit room it lifts the shadowed side of a surface as much as the lit side — softening shadows/contrast (user playtest 2026-06-29: "not too dark now but it kind of kills the shadows"). The per-sector keying is correct (bright-marked rooms read, dark sectors stay dark); the problem is the within-room flatness, which per-area targeting cannot fix because the shadow lives inside the same bright room that needs the glow.
  Approaches to evaluate (cheapest first):
    (a) Occlusion/AO-modulate the floor: scale the ambient term by a cheap ambient-occlusion / short shadow-ray term so genuinely open surfaces get the fill but shadowed pockets keep their darkness. Preserves contrast while still rescuing dark rooms.
    (b) Fold the floor into the GI bake (bake.comp) instead of adding it at the camera: treat the sector-light ambient as a small uniform emitter that participates in the bounce, so occlusion falls out of the existing GI solution naturally (no extra per-pixel ray) — but changes bake semantics and re-bake cost.
    (c) Hemisphere/normal-aware fill (e.g. a faint sky-direction bias) so up-facing surfaces get more than tucked-under faces — partial contrast recovery, cheaper than a full AO term.
  Defer until later levels are playtested (per DOOM-0043's play-it-first decision) and likely sequence AFTER the flashlight (DOOM-0044), which is the proper answer for genuinely dark corners and may reduce how hard the ambient floor has to work. AMBIENT_SECTOR_SCALE stays the master-strength knob regardless of approach.
  **Layman:** Refine the gentle room glow so it only fills genuinely dark/shadowed spots instead of washing out every shadow.
  Kind: enhancement.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0099] **Fully eliminate the residual Ultra flashlight/muzzle toggle-off ghost (denoiser history).**
  Follow-up to DOOM-0044. The SVGF temporal anti-ghosting fix (commit 55bb080) made the flashlight toggle-off fade "significantly better" per playtest, but the user notes it still isn't a perfect instant cut in every case; they chose to leave it for now. Residual likely sources: (1) the a-trous SPATIAL pass + SVGF colour-feedback still carry a blurred remnant of the lit value into the next frame's history even after the temporal pass resets newHist=1 (the spatial filter pulls from neighbours that may still hold the lit colour in their fed-back history for a frame); (2) the half-res lighting reconstruction (6c) bilateral-upsamples from grid samples, so a 2x2 block clears only as fast as its grid pixel; (3) the gradient still needs a >2*sigma delta, so a low-contrast surface (already dim) resets more weakly. Candidate fixes (cheapest first): (a) also gate the colour-feedback / clamp the history colour to the current neighbourhood AABB (SVGF "history colour clamping") so a reset pixel can't re-pull a stale lit colour; (b) the principled fix -- composite the deterministic analytic dynamic lights (flashlight + muzzle) CRISP in svgf_composite.comp (recompute from the G-buffer, or carry them in a separate non-denoised channel) so they never enter temporal history at all, which also keeps their hard shadow edges sharp instead of denoised. Option (b) is the real answer and supersedes further temporal tuning; size it against the composite pass gaining a TLAS bind or an extra image. Not urgent (deferred by user).
  **Layman:** When you switch the flashlight off, the lit patch is now much quicker to clear but still doesn't vanish instantly in every case — finish the job.
  Kind: enhancement.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0100] **Render world sprites (monsters, items, barrels) in the path-traced Ultra view.**
  The megakernel (pathtrace.comp) traces primary rays against the static walls+flats BLAS only, so world Things (drawn as billboards in Solid via RB_BuildSprites) never appear in Ultra. DOOM-0094 shipped the weapon + 2D overlay over the trace but explicitly DEFERRED world sprites: they need correct depth occlusion against the traced world, 'tracked separately'. Approach: add each visible Thing as a per-frame TLAS instance -- a camera-facing billboard quad with the paletted sprite material, alpha-tested on palette index 0 -- so primary rays hit them with real depth occlusion and they receive path-traced lighting/shadows. Shares the per-frame TLAS instance plumbing with DOOM-0084 (emissive Things) and the projectile-light item below; build them together. Distinct from DOOM-0080 (replace billboards with real 3D models, far-future). Cap/cull off-screen Things.
  **Layman:** In the ray-traced (Ultra) view the monsters, pickups and barrels are currently invisible — only walls, floors, the weapon and HUD show. Make all those objects appear in the ray-traced view too.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0101] **Enemy hitscan attacks cast a muzzle-flash light in the path tracer, like the player's.**
  The player muzzle flash (DOOM-0009 step 5, hardened in the DOOM-0044 session) is a dynamic analytic light at the player barrel, gated on extralight via misc2.z, with a ray-traced shadow. Enemy hitscan attackers -- former human (A_PosAttack), shotgun guy (A_SPosAttack), chaingunner (A_CPosAttack), spider mastermind -- flash on their firing frames but emit no light. Spawn a brief flash (warm, ~A_Light1 brightness) at the firing enemy's position + facing on the attack tic, with a ray-traced shadow like the player's. Requires generalising the single-dynamic-light path to N dynamic lights (today only one player-flash slot exists via misc2.z) -- the concrete first increment of DOOM-0010 (dynamic lighting). Pairs with the projectile-light item (same multi-light mechanism).
  **Layman:** When a zombie, shotgun guy or chaingunner fires at you, their gun should briefly light up the room — the same way your own gun's flash now does.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0102] **Demon projectiles (fireballs, plasma, BFG) cast moving light + shadows in the path tracer.**
  Projectile mobjs -- imp/baron MT_TROOPSHOT/MT_BRUISERSHOT, caco MT_HEADSHOT, revenant MT_TRACER, mancubus MT_FATSHOT, arachnotron MT_ARACHPLAZ, plasma MT_PLASMA, BFG MT_BFG -- are moving emissive Things. Each should be a moving dynamic point/area light updated to the mobj position every frame, casting ray-traced shadows + bounces, coloured per type (imp/baron fire warm orange; plasma blue; BFG green -- reuse the DOOM-0095 colour idea). Per frame: gather active projectiles, derive Le + colour, feed them into the NEE dynamic-light set; cap to the brightest N for perf. Builds on the multi-dynamic-light generalisation (DOOM-0010) shared with the enemy-flash item, and on emissive Things (DOOM-0084); their visible sprites come via the world-sprite-drawing item above.
  **Layman:** The glowing fireballs imps and barons throw — and plasma and BFG balls — should light up the walls and floor as they fly past and cast moving shadows, instead of being flat self-lit sprites that don't affect the scene.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0103] **Specular / glossy highlights (GGX) on metal, wet floors and nukage in the path tracer.**
  Add a GGX/VNDF specular lobe gated to surfaces that warrant it (metal/computer textures, liquid flats), per DOOM-0009 spec 4.4 ('specular gated to measured need'). Roughness from a per-material heuristic (liquids/metal = glossy, matte wall/floor art = diffuse). Aesthetic north star: Quake RTX — a full RT overhaul that still reads as the original — informed by DOOM 3 / 2016 / Eternal / Dark Ages. DOOM-0042 (HD PBR material set) DEPENDS ON this lobe: it supplies the roughness/metallic maps, this item owns the BRDF (F0 / VNDF / MIS) — 0042's "wet metal" specular look is gated on DOOM-0103 landing. Tune strength with the user to keep the DOOM feel.
  **Layman:** Shiny surfaces — metal, wet floors, slime — get realistic highlights like modern DOOM, without losing the classic look.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0104] **Ray-traced reflections (glossy / near-mirror) on polished floors, water and metal.**
  A secondary reflection ray off surfaces flagged reflective, glossy-importance-sampled by roughness (near-mirror for water/polished metal, blurry for semi-gloss). Builds on the existing megakernel + per-frame TLAS. Stage-3-class cost — gate behind the perf pass / DOOM-0012. Strength tunable so it enhances rather than overwhelms the DOOM look (Quake-RTX restraint).
  **Layman:** Polished floors and water actually reflect the room and your surroundings, like Quake RTX.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0105] **Ray-traced ambient occlusion / contact shadows for crisper depth.**
  The static GI bake already gives some implicit AO; add short-range RTAO (or extend the bake) for crisper contact darkening — especially under dynamic sprites (monsters/items/barrels, DOOM-0100) that the static bake doesn't cover. Cheap relative to reflections; high perceived-quality return.
  **Layman:** Corners, crevices and where objects meet the floor get subtle realistic darkening, adding depth.
  Kind: enhancement.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0106] **Volumetric lighting — light shafts and atmospheric fog in the path tracer.**
  Ray-marched single-scattering through fog/dust along the view ray, sampling the NEE light set (lamps, muzzle flash, flashlight, projectile lights) for in-scatter. Big atmosphere win, heavier cost — gate behind perf / DOOM-0012. DOOM 3 / Eternal reference. Pairs well with the flashlight (DOOM-0044) for a visible cone.
  **Layman:** Light beams through doorways and atmospheric haze, like DOOM 3's flashlight beams and Eternal's god-rays.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0107] **Sprites: unit-quad BLAS + per-instance transforms instead of a per-frame triangle-soup rebuild.**
  DOOM-0100 shipped world sprites as a triangle-soup BLAS rebuilt every frame (simplest reuse of RB_BuildSprites). The DOOM-0008/0009 spec's design is one unit-quad BLAS reused via per-instance billboard transforms (TLAS instance update, no per-frame BLAS build) — cheaper. Switch to it if the per-frame sprite-BLAS build measures costly in the perf pass. Same on-screen result.
  **Layman:** A faster way to feed the on-screen monsters/items to the ray tracer each frame.
  Kind: perf.
  Source: in-session-2026-06-29.

- 📋 [DOOM-0108] **Sprites cast alpha-tested shadows in the Ultra view.**
  DOOM-0100 makes sprites primary-ray-visible but mask-excludes them from shadow/NEE rays (cull mask 0x01 = world only) to avoid per-shadow-ray alpha-test cost (spec 8 lean posture). Add alpha-tested sprite silhouettes to shadow rays (the spec's flat-card-shadow intent) once the perf budget allows — likely gated behind the half-res trace / DOOM-0012.
  **Layman:** Monsters, barrels and lamps should cast shadows in the ray-traced view, not just receive light.
  Kind: feature.
  Source: in-session-2026-06-29.

- 📋 [DOOM-0109] **Per-object motion vectors for moving sprites (reduce denoiser ghosting).**
  The SVGF/TAAU motion vectors (svgf_composite) are camera-only, so a moving sprite (walking monster, flying projectile) reprojects to the wrong history texel and can ghost/smear. Derive per-sprite motion from the mobj's previous-tic position and write it into the motion buffer for sprite pixels. Pairs with DOOM-0100/0102.
  **Layman:** Stop fast-moving monsters from smearing slightly in the denoised ray-traced view.
  Kind: enhancement.
  Source: in-session-2026-06-29.

- 📋 [DOOM-0110] **Light the weapon/hand from local path-traced light (coloured tint, not just white sector brightness).**
  The player weapon is a screen-space psprite drawn over the traced world (DOOM-0094), shaded by sector lightlevel + extralight only — so it brightens white and never picks up coloured RT light (user saw the gun brighten white under red ceiling lights). Sample the path-traced irradiance at the player/muzzle position (a small light probe: the GI cache + nearby dynamic/NEE lights) and tint the psprite by it. Keeps the weapon consistent with the lit world.
  **Layman:** Your gun and hand should pick up the colour of nearby lights — go red under a red light, blue under a blue one — instead of just brightening white.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0111] **Exploding barrels emit a transient light flash in the path tracer.**
  An exploding barrel (MT_BARREL death -> A_Explode) currently emits no light. Spawn a brief, bright warm-orange dynamic point light at the barrel position for the explosion frames (like the muzzle flash, gated on the death/explosion tics), with a ray-traced shadow. Builds on the multi-dynamic-light foundation (DOOM-0010); colour per the projectile-light palette (DOOM-0102).
  **Layman:** When a barrel blows up, the explosion should briefly light up the room.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0112] **Glowing collectibles (health/armor potions, spheres) emit a coloured glow + light.**
  DOOM-0084 gates sprite emission on FF_FULLBRIGHT, which correctly excludes ammo but also excludes glowing collectibles that aren't fullbright-framed (the blue health-bonus potion BON1, armor bonus BON2, soul/mega sphere, invuln/blur sphere). Add a small allowlist of 'glowing collectible' mobjtypes that emit even without FF_FULLBRIGHT, coloured by their sprite (blue potion -> blue light). Confirm the exact set with the user.
  **Layman:** Pickups that are meant to glow — the blue health bottle, soul/mega spheres — should give off coloured light, while plain ammo stays dark.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0113] **Sprite area-lights are flat camera-facing cards, so they light ~2 sides instead of all around.**
  DOOM-0084 samples each emissive sprite's two billboard triangles as the NEE area light. The billboard is a flat quad oriented at the camera, so even with cosL forced to 1 the emitting AREA is a plane: surfaces edge-on to that plane (e.g. a wall in the corner next to the bottle) receive almost nothing. The proper fix is a true positional/point light per emissive Thing (the DOOM-0010/0101/0102 dynamic-light foundation): sample a small sphere/point at the Thing centre so it radiates uniformly. Until then sprite lights are ~2-sided. Confirmed on-screen: blue bonus bottle lights two walls but not the corner between them.
  **Layman:** A glowing bottle or lamp lights the floor and the walls it faces, but a wall tucked in the corner beside it stays dark — because the light is emitted from a flat card facing the player, not from a little glowing ball. Make these lights radiate evenly in every direction.
  Kind: enhancement.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0114] **Some allowlisted glowing pickups (key skulls, armor bonus) don't visibly cast light.**
  DOOM-0084/0112: sprite_glows() allowlists the key skulls (BSKU/RSKU/YSKU) and armor/health bonuses (BON1/BON2), and bottles now light correctly — but skulls and the armor bonus show no surrounding light. Likely causes to check: (a) their sprite texture's computed Le (ComputeMaterialEmissive area-weighted bright-texel mean) falls below the emitter-min gate, so they never enter the emitter list; (b) the billboard is so small its area term makes the contribution negligible even when direct-sampled; (c) armor genuinely lit but drowned by adjacent sky/skylight. Instrument per-sprite Le + emitter inclusion to disambiguate, then raise the gate/boost or floor the area for tiny pickups.
  **Layman:** The blue bottles glow and light the room now, but the floating key-skulls and the green armor bonus still don't throw any light (the armor may also just be washed out by bright skylight near it). Find out why those particular pickups stay dark and fix them.
  Kind: fix.
  Source: user-request-2026-06-29.
  User re-confirmed 2026-07-04: the ARMOUR pickup (armor bonus BON2 / green) and key skulls still cast no surrounding glow, while the health/bonus bottle now DOES cast light (resolved -> matches "bottles now light correctly"). Strong likelihood this is cause (a): the old AREA-AVERAGED emitter-min gate (kEmitterMinLum) drowned these small-bright-region sprites so they never entered the NEE emitter set. The DOOM-0082 fix (commit fc93658, peak-region gate in emissive_derive.h, NOT yet in the user's build) replaces exactly that area-average qualification with a near-fullbright peak-region gate -> a small bright armor/skull region should now qualify. NEXT: rebuild with fc93658 and re-test the armor bonus + key skulls in Ultra RT; if they now cast, DOOM-0114 cause (a) is resolved by DOOM-0082 and only tuning remains; if still dark, fall to cause (b) tiny-billboard area term (floor the area for small pickups) or (c) drowned by skylight. Green armor -> green glow expected (peak gate + VALUE/max-channel derivation is hue-correct).
  Update 2026-07-04: DOOM-0157 (shipped, commit 01f0e6a) gave these same pickups (skull keys, armour bonus) a guaranteed faint self-emission Le via the allowFaint fallback in emissive_derive.h, and the user signed off that they now read fine in-game. That resolves the VISIBILITY half — the eyes now self-illuminate in a dark room. This item's original framing was specifically "don't visibly CAST light onto surroundings"; the faint Le is deliberately dim (self-glow, not room-pooling), so a pickup POOLING light on nearby walls is still not delivered and would need a stronger Le / dedicated NEE emitter weight for these lumps. Kept open (planned) for that room-cast half only; the user has not asked for it and may never — de-prioritised. See [[DOOM-0157]].
  Aesthetic detail (user 2026-07-17, from RT DOOM 1+2 videos): once the bonus armour (BON2, green) casts light, its 'eyes' need a LOT more green glow than a subtle emit — the exaggerated RT-DOOM look. Track the intensity dial-up under DOOM-0193 (glow dial-up pass); this item still owns the root fix (why it casts no light at all today).

- 📋 [DOOM-0115] **Fix pre-existing Vulkan validation errors (clear-color usage, renderpass dependency mismatch).**
  INV-8 (validation-clean). Two distinct families in the terminal log: (1) vkCmdClearColorImage on an image created with only VK_IMAGE_USAGE_STORAGE_BIT, missing VK_IMAGE_USAGE_TRANSFER_DST_BIT (add the usage flag at image creation, or clear via a compute/shader path); (2) vkCmdBeginRenderPass/vkCmdDraw pDependencies srcStageMask/srcAccessMask/dstAccessMask incompatible between the render pass used to create the framebuffer/pipeline and the one begun (the two render passes' subpass dependencies must match for compatibility). Pre-existing, not from the sprite work. Reconcile the renderpass dependency definitions and add the transfer-dst usage.
  **Layman:** The graphics debug layer is logging a few rule violations every frame. They aren't causing visible problems today, but they're real correctness bugs that can break on other drivers — clean them up.
  Kind: fix.
  Source: in-session-2026-06-29.

- ✅ [DOOM-0116] **Persist the Ultra path-tracer view across sessions and default it to the denoised (SVGF) view.**
  The `~` path-tracer view selector (rb_rtdebug) defaulted to 0 (raster "Original") and was not persisted, so Ultra booted into the raster-looking view every launch until the user pressed `~` to reach mode 6 (denoised SVGF). Now persisted via m_misc.c defaults[] as "rt_view" (default 6) and clamped on load in RB_Init to a valid cycle value ({0,1,2,3,4,6}; 5 is the headless verify path). So a fresh Ultra user sees the denoised path-traced view immediately, and any `~` choice survives a restart. rendermode (Classic/Solid/Ultra) already persisted. Implemented 2026-06-29; awaiting play-test (held local with DOOM-0048 — needs a WAD to confirm Ultra shows the denoised view on boot). Implemented in-session.
  **Layman:** Ultra now starts in the proper ray-traced (denoised) look and remembers your view choice between play sessions.
  Kind: feature.
  Source: user-request-2026-06-29.
  Resolved (2026-06-29): play-tested — Ultra boots straight into the denoised (SVGF) view (the on-screen "DENOISED" label confirms) and the `~` view choice persists across sessions via the rt_view config var. Shipped in 4d8f619 (pushed e720b4e).

- 📋 [DOOM-0117] **Controls settings page that lists every keyboard key and controller-button mapping.**
  Add an Options -> Controls page that displays the full current binding set: keyboard keys (key_right/left/up/down, key_fire, key_use, key_strafe, key_speed, strafe left/right, etc. from m_misc.c defaults[]) and the controller/gamepad mappings (joyb_*, plus the gamepad actions like the L1 flashlight from DOOM-0044). Read-only display; rebinding is the follow-up DOOM-0118. Prerequisite for it (the page is where rebinding happens).
  **Layman:** A single menu page that shows you, at a glance, what every key and gamepad button does.
  Kind: feature.
  Source: user-request-2026-06-29.

- 📋 [DOOM-0118] **Let the player rebind keyboard keys and controller buttons from the Controls page, persisted to config.**
  Build on the Controls settings page (DOOM-0117): select a binding, capture the next key/button press, write it back to the live key_*/joyb_* globals and persist via m_misc.c defaults[] (M_SaveDefaults). Handle conflict detection (a key already bound elsewhere) and a reset-to-defaults option. Covers both keyboard and gamepad. The defaults[] table already persists every key_*/mouseb_*/joyb_* value, so this is mostly the capture UI + write-back + save.
  **Layman:** Change any key or gamepad button to whatever you prefer, and it sticks between sessions.
  Kind: feature.
  Source: user-request-2026-06-29.

- ✅ [DOOM-0119] **REJECT-lump light culling for NEE (cheap-ladder step 1).**
  Per docs/research/DOOM-0092-restir-cost-benefit.md §1.4. Use the WAD REJECT sector->sector visibility lump to skip, per shading point, every emissive sector invisible from the current sector -- an exact, free, register-free candidate-set cull. Cheapest first lever against the ~100-emitter worst case; precedes RIS. Fall back to a shadow ray where REJECT is conservative.
  **Layman:** Use DOOM's built-in room-visibility table to skip lights that can't possibly reach a surface, so the path tracer stops wasting work on them.
  Kind: implement.
  Source: research-2026-06-29 DOOM-0092 §1.4.
  Design locked (2026-06-29, scoped via Explore map). Scope this first cut to the OMNI SPRITE loop only (the O(N)/pixel cliff; sprite sector is unambiguous = thing's subsector). Static wall emitters use the CDF (not the cliff) and their centroid sits on a linedef (ambiguous sector) -> defer to a later item. Mechanism (all build-time/per-frame baked, zero GPU BSP): (1) hit sector = subSec[triSs[prim]] — triSs already exists (r_vulkan.cpp BuildProbes), add a subSec buffer (subsector->sector); sprite hits carry triSs=0xFFFFFFFF -> bypass cull. (2) emitter sector: nee_merge_emitters does NOT reorder (static [0,staticN) then dynamic [staticN,n) in push order), so build a parallel dynSec[] in BuildDynamicEmitters (sector via R_PointInSubsector(centroid)); emitSec buffer = static sentinel 0xFFFFFFFF + dyn sectors, host-visible like g.emitBuf, refilled per frame in FinalizeEmitters. (3) upload REJECT matrix once per level + numsectors. Shader: gate the omni loop in pt_common.glsl shadeSurface — skip emitter k when rejectmatrix[(hitSec*numsectors+emitSec)>>3] & (1<<((..)&7)) AND both sectors known, BEFORE sampleEmitter (saves the unshadowed eval + the shadow ray; strictly better than OMNI_CULL_VALUE). New RB_ accessors in r_mesh.c/.h: RB_NumSectors, RB_RejectMatrix(+size), RB_SectorAtPoint(x,y), RB_BuildSubsectorSectors(out,n). Push-constant budget OK: append 3 uint64 addrs (subSec/emitSec/reject) -> struct 176->200B (device limit 256, comment r_vulkan.cpp:1723); numsectors into misc4[2] (reserved). Update RtPushConstants struct + static_assert(200) + pcr.size + the GLSL layout in BOTH pathtrace.comp and pt_common.glsl (verify 152B partial range unaffected — verify path doesn't cull). Verify: build green; INV-6 still passes (verify estimators run omniStart==emitCount, never see the cull); play-test E1M1 glowing room @50% with the `\` profiler — megakernel down in multi-sector scenes, no rooms gone dark. Caveat: a broken/over-aggressive REJECT lump in custom WADs could cull visible lights (darkening); fine for id maps; untrusted-WAD hardening tracked under DOOM-0073.
  Progress (2026-06-29): implementation complete, builds green (static_assert RtPushConstants==200, BakePush==80 both pass; glslc clean), unit tests green (nee_sampling_test incl. DOOM-0084 merge-order). 5 files: r_mesh.c/.h (RB_NumSectors/RB_RejectMatrix/RB_SectorAtPoint/RB_BuildSubsectorSectors); r_vulkan.cpp (subSec+reject per-level buffers in BuildProbes; host-visible emitSec mirrored to emitBuf, filled by FinalizeEmitters lock-step with nee_merge; push-constant 176->200B, numsectors in misc4.z); pt_common.glsl (SubSec/EmitSec/Reject buffer_refs; REJECT gate in shadeSurface omni loop, before sampleEmitter); pathtrace.comp (hitSec = subSec[triSs[prim]], both call sites); bake.comp (null cull args, omni loop empty so dead). Awaiting user play-test: E1M1 glowing room @50% with the \ profiler -- megakernel down in multi-sector scenes, no rooms gone dark. Flip to shipped on a clean play-test.
  Resolved (2026-06-29): play-test pass on E1M1-class light-heavy scene @50% scale. Cull active (log: "REJECT cull active (88 sectors, 968-byte matrix)"); scene ran 82 static + up to 48 sprite emitters. Same-scale (50%) result vs DOOM-0090 baseline (~37-46 fps): megakernel now 8.99-15.54 ms, fps 36-51 -- ceiling raised to 51 fps / ~9 ms megakernel on views where cross-room lights are culled; floor (~36-38 fps) unchanged on views facing the whole light cluster (little to cull), exactly as designed. Validation layer clean for the change: zero push-constant / device-address / buffer_reference / descriptor / OOB errors (the 200B push range + 3 new device-address buffers + REJECT indexing all verified by the layer). No dark-room regression reported. Commit 2873180.
  Follow-up (2026-08-03, code-quality-review sweep): the cull accepted any
  non-empty REJECT lump, but both readers address it as bit
  (secA * numSectors + secE) -- the CPU cull in BuildRasterPointLights and the
  megakernel over the uploaded copy. A PWAD's REJECT length is not tied to its
  sector count, so a well-formed 5000-sector map shipping an 8-byte REJECT read
  megabytes past the allocation on both sides. The gate now requires
  ceil(numSec^2 / 8) bytes and otherwise leaves the cull off (the same path a
  REJECT-less level takes), printing why rather than losing frame rate silently.

- ✅ [DOOM-0120] **RIS light resampling without reservoirs (cheap-ladder step 2).**
  Per docs/research/DOOM-0092-restir-cost-benefit.md §1.4. Resampled Importance Sampling: pick M omni candidates, resample to 1 by contribution weight, cast ONE shadow ray -- NO cross-frame/neighbour reservoir (the part that hurts RDNA2 registers). Replaces the current O(N) omni loop with O(1) shadow rays + O(M) cheap weight evals. Build after REJECT cull. Full ReSTIR is only reconsidered if RIS still misses 60 fps AND the DOOM-0090 RGP capture shows occupancy headroom.
  **Layman:** Pick a few candidate lights, keep the best one, and cast a single shadow ray instead of looping every light -- much cheaper on this AMD card than full ReSTIR.
  Kind: implement.
  Source: research-2026-06-29 DOOM-0092 §1.4.
  Progress (2026-06-29): implemented as a pure shader change (pt_common.glsl) — the REJECT/emitter buffers from DOOM-0119 already feed the candidate set, so no C++/push-constant/buffer changes. Split sampleEmitter into emitterContribUnshadowed + occluded (seed usage byte-identical, so static-NEE and INV-6 verify paths are unchanged). The omni sprite loop now builds a single-frame weighted reservoir over each surviving candidate's per-light-clamped unshadowed reflected radiance (VALUE/max-channel target), then casts ONE shadow ray on the survivor and reweights by wsum/selTgt. Provably unbiased w.r.t. the old per-light-clamped sum E = Σ_k min(rad_k, FIREFLY_MAX)·V_k. No cross-frame/neighbour reservoir (keeps RDNA2 registers free); TAAU+SVGF absorb the single ray's visibility noise over time. Replaces O(sprites)/pixel shadow rays with O(1) ray + O(M) cheap weight evals. OMNI_CULL_VALUE removed (RIS supersedes the dim-sprite cull). Build + make test green. Awaiting 50%-scale play-test on the RX 6600 to quantify the megakernel drop in a glowing-prop room.
  Resolved (2026-06-29): play-tested on the RX 6600 @ 50% scale (terminal_output.log, 67 profiled frames). Headline result is STRUCTURAL: megakernel time is now decoupled from sprite count — 22 sprites avg 11.8 ms, 38 sprites avg 11.3 ms, 40 sprites avg 12.2 ms (~flat, ~0.02 ms/sprite vs the original O(sprites) loop that hit 80-110 ms / 8-9 fps facing a glowing room before any omni opt). Overall 36-57 fps / megakernel 9.0-14.9 ms. Versus the DOOM-0119-only baseline (36-51 fps / 8.99-15.54 ms) the ceiling rose 51->57 fps and worst-case megakernel fell 15.5->14.9 ms; the average gain is modest because after the REJECT cull the omni loop was already a minority of cost — the bottleneck has shifted to the STATIC wall NEE (NEE_SAMPLES=6) + SVGF denoiser, which is the next lever. Validation: ZERO push-constant/device-address/buffer_reference/descriptor/OOB errors (the shader wiring is clean); the warnings in the log are the pre-existing DOOM-0126/0127 plus a newly-filed AS-refit issue DOOM-0128 (all C++/AS-side, unrelated to this shader-only change). No rooms went dark.

- 📋 [DOOM-0121] **Event-driven lazy per-sector irradiance re-bake on door/lift moves.**
  Per docs/research/DOOM-0092-restir-cost-benefit.md §2. Keep the static sector-keyed irradiance bake; on a door/lift visibility change, mark the <=2 affected sectors dirty and re-fill only their cache entries (trigger = the existing sector-move thinker). Avoids a continuously-traced dynamic DDGI field (costs every frame + thin-wall leaks). Storage granularity (per-vertex vs SH-L1 probe) stays the DOOM-0009 spec Sec.9 open item.
  **Layman:** When a door or lift opens, only the couple of rooms it affects get their baked lighting refreshed -- cheaper than constantly recomputing the whole level's bounce light.
  Kind: implement.
  Source: research-2026-06-29 DOOM-0092 Q2.

- ✅ [DOOM-0122] **INV-6 verify path runs with omniStart==emitCount, leaving the omni NEE loop unchecked.**
  Per docs/research/DOOM-0092-restir-cost-benefit.md §3. pathtrace.comp:427/429 call directNEEVerify/directAllLights with omniStart==emitCount (both args pc.misc2.x), so the post-DOOM-0084 omni direct-sampling branch in sampleEmitter is never exercised on the verify path. INV-6 currently proves only the STATIC power-importance selection unbiased. Fix: pass the real omniStart so the omni loop is checked against the brute-force reference; refresh the now-stale 'Mirrors shadeSurface's emitter pick' comment at pt_common.glsl:254-256.
  **Layman:** The self-test that proves the lighting math is unbiased doesn't actually cover the newer glowing-sprite lighting path -- a gap worth closing.
  Kind: review-fix.
  Source: research-2026-06-29 DOOM-0092 Q3.
  Progress (2026-07-17): implemented — the two verify estimators now receive the real omniStart (pc.misc4.y, the split shadeSurface uses in production) instead of omniStart==emitCount, so the post-DOOM-0084 omni sprite-light NEE loop is exercised against the brute-force reference. Refreshed the stale "Mirrors shadeSurface's emitter pick" comment in pt_common.glsl. Safe: OMNI_CULL_VALUE (the one-sided omni bias) was removed by DOOM-0120's RIS reservoir, and both estimators forward the same omniStart into the same sampleEmitter, so they still differ only in selection and the unbiased importance-sampling identity holds. Shaders recompile clean; nee_sampling_test green. Commit 3b8982d. Graduates to shipped after an -rtverify hardware run confirms INV-6 stays within tolerance with the omni path now covered.
  Progress (2026-07-17, cont.): completed the host half -- RB_RtVerify now sets pc.misc4.y = g.staticWgt.size() (the real static|omni split, matching the display path at r_vulkan.cpp:6494), so the committed shader read of misc4.y (3b8982d) is correct; also refreshed the stale struct comment and added omni-coverage reporting to the [rtverify] line (2ed8227). Verified on the RX 6600: -rtverify INV-6 rel-MSE PASS at E1M1 0.08% / E3M4 0.32% / E4M1 0.07%, white-furnace 0.000000 -- no regression (at these scenes staticN==emitCount, so the split is a no-op identical to the prior omniStart==emitCount path). HOWEVER the omni loop is now correctly WIRED but still NOT EXERCISED: every -rtverify run reports 0 omni emitters because the verify fires on the first present, before RecordRtTrace populates this frame's dynamic sprite emitters -- so only the level-load static set exists. Split that blocker to DOOM-0195; keeping DOOM-0122 in-progress until the omni loop is actually checked. Separately found a pre-existing INV-6 fail at E3M1 (1.61%), logged as DOOM-0196 (unrelated: DOOM-0122's change is a no-op there).
  Resolved (2026-08-04): verified complete in the tree, not on recall. Both halves are live -- r_vulkan.cpp sets pc.misc4[1] = g.staticWgt.size() (the real static|omni split, matching the display path) and the verify run prints how much of the omni sprite-light NEE loop it covered. Nothing was left outstanding on the bullet; it had simply never been flipped. CHANGELOG entry added under Fixed.

- ✅ [DOOM-0123] **Quantify the omni-cull (OMNI_CULL_VALUE) one-sided bias against the brute-force reference.**
  Per docs/research/DOOM-0092-restir-cost-benefit.md §3 follow-up #2. The OMNI_CULL_VALUE=0.0025 cull drops a sprite's shadow ray when its UNSHADOWED contribution is below threshold -- a deliberate one-signed bias (shadowing only shrinks the term), bounded by threshold x culled-count. Measure it once vs directAllLights to record the bound instead of assuming it negligible.
  **Layman:** Measure exactly how much the shadow-ray-skipping shortcut darkens the image, so we have a recorded number rather than an assumption.
  Kind: test.
  Source: research-2026-06-29 DOOM-0092 Q3.
  Resolved (2026-06-29): moot — DOOM-0120 removed OMNI_CULL_VALUE entirely. RIS keeps every surviving sprite as a weighted candidate (a dim one almost never wins the reservoir draw) instead of hard-dropping its shadow ray, so the one-sided contribution-cull bias this item would have quantified no longer exists.

- ✅ [DOOM-0124] **Correct DOOM-0009 spec's stale 'NEE + MIS' wording to NEE-only.**
  Per docs/research/DOOM-0092-restir-cost-benefit.md §3. docs/specs/DOOM-0009-path-tracer.md §4.4 (~line 219) says 'NEE + multiple importance sampling (power heuristic)' and §7 build-step 3 (~line 313) says 'NEE + MIS + RR', but the shipped integrator is pure-Lambert NEE-only with a cosine-hemisphere bounce -- no BSDF-light sampling, so MIS is moot. Correct to 'NEE-only (MIS N/A for pure-Lambert)', unless a future specular path is planned (then mark MIS as future, not present).
  **Layman:** The design doc still says the renderer uses a lighting technique (MIS) it doesn't actually use -- update it to match the real code.
  Kind: doc-fix.
  Source: research-2026-06-29 DOOM-0092 Q3.
  Resolved (2026-07-04): folded into the DOOM-0057/0081 reconciliation. DOOM-0009 §4.4, §7 status header, and §7 step 3 reworded from 'NEE + MIS' / 'MIS+RR not yet wired' to 'MIS is N/A (shipped integrator is pure-Lambert NEE-only)' + 'Russian-roulette moot (single live bounce)', per the DOOM-0092 decision; DOOM-0008 stage-table row + integrator prose likewise reconciled (supersession banner). Verified mis_power_heuristic/rr_survival have no call sites. Cold-eyes loops 4 + 5 confirmed all mentions agree with each other and DOOM-0092.

- 📋 [DOOM-0125] **Sweep dangling sub-section citations into DOOM-0009-performance.md (flat-list doc).**
  Per the DOOM-0092 cold-eyes loops. docs/research/DOOM-0009-performance.md uses flat numbered lists under ## 2 / ## 3 (no Sec.2.x/3.x sub-anchors). The DOOM-0009 spec (and the original DOOM-0092 draft) cite broken anchors like 'perf Sec.2.5'/'Sec.2.7'. Sweep all docs citing the perf doc and rewrite to 'Sec.2 item N' / 'Sec.3 idea N'.
  **Layman:** Several docs point at section numbers that don't exist in the performance research file -- fix the broken internal references.
  Kind: doc-fix.
  Source: research-2026-06-29 DOOM-0092 cold-eyes.

- 📋 [DOOM-0126] **Vulkan validation: vkCmdClearColorImage on a STORAGE-only image (missing TRANSFER_DST_BIT).**
  Seen in the user's terminal_output.log line 40: "vkCmdClearColorImage(): image was created with usage VK_IMAGE_USAGE_STORAGE_BIT (missing VK_IMAGE_USAGE_TRANSFER_DST_BIT)". Pre-existing (NOT from DOOM-0119 -- no image-usage changes in that diff). Fix: add VK_IMAGE_USAGE_TRANSFER_DST_BIT to the offending image's usage flags at creation (likely an RT/SVGF storage image that is also vkCmdClearColorImage'd), or clear it via a compute store instead of vkCmdClearColorImage. Low urgency (works on RADV) but it pollutes the validation log and is spec-incorrect.
  **Layman:** A startup warning from the graphics driver's checker: an image is cleared without being marked as clearable. Harmless today but worth fixing so the validation log is clean.
  Kind: fix.
  Source: in-session-2026-06-29 DOOM-0119 play-test log.

- 📋 [DOOM-0127] **Vulkan validation: renderpass dependency stage/access-mask incompatibility on overlay/blit framebuffers.**
  Seen in terminal_output.log lines ~226-291 (capped at the 10x duplicate limit): vkCmdBeginRenderPass reports pDependencies[0] srcStageMask/srcAccessMask/dstAccessMask incompatible between VkRenderPass 0xd and the one baked into the VkFramebuffer (0xc) -- VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT vs EARLY/LATE_FRAGMENT_TESTS, TRANSFER_WRITE vs 0, etc. Pre-existing (NOT from DOOM-0119 -- no renderpass changes in that diff). The framebuffer was created against a renderpass whose subpass dependencies don't match the renderpass used at begin time; they must be render-pass-compatible (same dependency stage/access masks). Fix: align the subpass dependency masks between the renderpass used to create the framebuffer and the one used in vkCmdBeginRenderPass (or reuse the same VkRenderPass object). Likely in the 2D overlay/blit path.
  **Layman:** Another graphics-checker warning: two render steps describe their hand-off slightly differently. Cosmetic for now; cleaning it keeps the log trustworthy so real bugs stand out.
  Kind: fix.
  Source: in-session-2026-06-29 DOOM-0119 play-test log.

- 📋 [DOOM-0128] **Vulkan validation: acceleration-structure UPDATE refit undersized / flag-mismatched on a compacted AS.**
  Seen in terminal_output.log: vkCmdBuildAccelerationStructuresKHR reports three related VUIDs — (1) dstAccelerationStructure was created with size 306304 but an UPDATE build requires a minimum size of 353536 (the refit target grew past the originally-allocated/compacted size); (2) mode is VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR but pInfos[0].flags (ALLOW_UPDATE|PREFER_FAST_TRACE) must equal the flags the AS was originally built with (VUID-...-03759); (3) updating a COMPACTED acceleration structure (VUID-...-10126). Pre-existing and NOT from DOOM-0120 (a shader-only change — no AS build/update code touched). Root cause: the dynamic-geometry AS (sprite/thing TLAS or BLAS) is refit in UPDATE mode against a structure that was compacted to a tighter size, so when the primitive count rises the refit no longer fits and the build flags no longer match. Fix options: do not UPDATE a compacted AS (keep an uncompacted, ALLOW_UPDATE-flagged copy for refitting, compact only static geometry), or size the updatable AS to the worst-case prim count and rebuild (not update) when the count exceeds the allocation. Low urgency (RADV tolerates it) but spec-incorrect and it can corrupt the AS on stricter drivers.
  **Layman:** A graphics-checker warning: when the world's ray-tracing structure is quickly refreshed (instead of rebuilt) as moving things come into view, it sometimes no longer fits or its settings don't line up. Harmless on this AMD card today, but worth fixing so the refresh path is correct and the log stays clean.
  Kind: fix.
  Source: in-session-2026-06-29 DOOM-0120 play-test log.

- ✅ [DOOM-0129] **Specialise the path-tracer megakernel per view-mode (Vulkan spec-constant) so glslc strips the unused debug modes, cutting VGPR/occupancy on RDNA2.**
  mode is a runtime push-constant (pathtrace.comp:219, `const uint mode = pc.misc.x;`) branched on by `if (mode == 2u)` etc., so glslc cannot dead-strip modes 1/2/3 (white-furnace/textured) and 5 (verify: directNEEVerify/directAllLights inlined from pt_common.glsl). Only mode 4 (noisy NEE) and mode 6 (denoised, the default) ship in play. Make `mode` a `layout(constant_id=0) const uint`, build one .spv variant per shipped mode (Makefile already does per-file glslc — add a -D/spec-constant rule), bind the matching pipeline. Provably image-identical (same executed path); the only change is which dead code is present. RDNA2 occupancy is a VGPR step function, so even modest live-state reduction can bump the occupancy tier. Targets the #1 cost (megakernel ~15 ms @50%). Companion to DOOM-0090 (occupancy/VGPR pass); LOSSLESS, low risk, medium effort.
  **Layman:** The renderer is one giant shader that carries five unused diagnostic view-modes at once; compiling a trimmed copy per real view-mode frees GPU registers and runs faster with a pixel-identical picture.
  Kind: perf.
  Source: in-session-2026-06-29 megakernel lossless-perf analysis.
  Resolved (2026-06-29): mode is now `layout(constant_id=0) const uint mode = 6u` in pathtrace.comp; host builds one specialised pipeline per view-mode via RtPipelineForMode() (lazy cache, mode 6 pre-built at init), bound per-frame from rb_rtdebug. SPIR-V confirmed to carry OpDecorate SpecId 0 + OpSpecConstantOp IEqual for every mode test, so the driver folds the constant and dead-strips the unused debug modes. Provably image-identical (same executed path); occupancy/FPS gain to be quantified on the RX 6600 at 50% scale. Builds clean. Companion to DOOM-0090.

- ✅ [DOOM-0130] **Extract shared muzzle-flash + flashlight shadow-ray helpers (byte-identical between megakernel mode 4 and mode 6) to shrink shader code/live-state.**
  The muzzle-flash and flashlight shadow-ray blocks are duplicated between mode 4 (pathtrace.comp ~357-414) and mode 6 (~526-575), differing only by the `albedo *` factor (mode 6 is demodulated). Fold into shared inline helpers (as DOOM-0120 did for occluded()/emitterContribUnshadowed()). Identical output; reduces code size + live state, compounding the DOOM-0129 occupancy win. LOSSLESS, low risk, low effort.
  **Layman:** Two big chunks of lighting code are copy-pasted between the renderer's two real view-modes; merging them into one helper shrinks the shader and helps it run faster, with no visual change.
  Kind: refactor.
  Source: in-session-2026-06-29 megakernel lossless-perf analysis.
  Resolved (2026-06-29): muzzle-flash + flashlight shadow-ray blocks (previously duplicated byte-for-byte between megakernel mode 4 and mode 6, differing only by the albedo factor) extracted into muzzleFlashDelta()/flashlightDelta() in pathtrace.comp. albedo is applied inside in the original left-to-right order, so the display path (mode 4, real albedo) is bit-identical to the pre-refactor arithmetic and the demodulated feed (mode 6) passes vec3(1.0) — also bit-exact since 1.0*x == x in IEEE. ~110 lines of duplication removed. Builds clean. LOSSLESS pure refactor.

- ✅ [DOOM-0131] **Fold RefitAS into the frame command buffer to remove the mid-frame one-time submit that bubbles the GPU on door/lift frames.**
  RefitAS uses its own BeginOneTime/EndOneTime submit (r_vulkan.cpp:1565-1567), a separate command-buffer submission outside the frame's g.cmd, on moving-geometry frames (g.blasDirty). That synchronous mid-frame submit can bubble the GPU while geometry animates. Record the BLAS UPDATE -> AS barrier -> (TLAS) into the frame's g.cmd instead (as BuildSpriteTlas already does for per-frame builds), removing the extra submit with identical output. Intermittent (animation-only) latency/hitch fix. Relates to DOOM-0091 (AS best-practice) and DOOM-0128 (refit sizing). LOSSLESS, low-moderate risk.
  **Layman:** When a door or lift moves, the renderer fires a separate GPU job mid-frame that can briefly stall it; folding that into the main frame removes the hitch (identical image).
  Kind: perf.
  Source: in-session-2026-06-29 megakernel lossless-perf analysis.
  Resolved (2026-06-29): RefitAS() split into RecordRefitAS(VkCommandBuffer) — the BLAS in-place update is now recorded into the frame command buffer (g.cmd) inside RecordRtTrace, ahead of the TLAS rebuild that reads it, ordered by an AS write->read barrier. The old standalone BeginOneTime/EndOneTime submit (which waited the queue idle, bubbling the GPU on every door/lift frame) is removed. Memory model unchanged (host-coherent vbuf written before submit, fence-guarded); only the refit timing changes. blasDirty still latches under raster so off-screen moves are caught on the first traced frame. Builds clean.

- 📋 [DOOM-0132] **Anisotropic texture filtering (with mipmaps) for grazing-angle surfaces, gated on a true-colour material path.**
  Goal: sharpen textures viewed at oblique/grazing angles (DOOM's long floors and corridor walls receding into the distance), where point-sampling without mipmaps currently shimmers/moires.
  
  BLOCKER (verified): the material sampler is VK_FILTER_NEAREST + VK_SAMPLER_MIPMAP_MODE_NEAREST (r_vulkan.cpp:2870-2874, "paletted art: point sampling"). The material textures store PALETTE INDICES that are resolved through paletteTex; linear/anisotropic filtering of indices is invalid (averaging index 50 and 200 yields a garbage colour, not a blend of the two colours). So AF cannot be bolted onto the current paletted sampler.
  
  Requires, in order:
  1. A true-colour material path: pre-expand the paletted material atlas to RGB(A) once (palette + any COLORMAP/sector-light handling resolved up front), so the sampled texels are colours that CAN be filtered. This is the load-bearing prerequisite and probably its own item.
  2. Mipmap generation for the material atlas (AF builds on mipmapping).
  3. Enable the samplerAnisotropy device feature + a sampler with maxAnisotropy (clamp to VkPhysicalDeviceLimits.maxSamplerAnisotropy, e.g. up to 16x), linear min/mag + linear mipmap mode.
  4. Raster (Solid) path gets hardware AF for free once 1-3 land (fragment shader has implicit derivatives).
  5. Ultra (path tracer, pathtrace.comp) has NO implicit derivatives in compute — AF needs explicit gradients via textureGrad with ray differentials (track the ray's footprint). Larger effort; can land after the raster path.
  
  Aesthetic tension (per RT north star): AF + linear filtering smooths DOOM's deliberately crunchy pixels. Keep it a tunable level (Off / 2x / 4x / 8x / 16x) so the user can trade shimmer-reduction against the retro look; tune with the user. Off should remain byte-identical to today's NEAREST path.
  
  Kind: enhancement.
  **Layman:** Floors and walls seen edge-on currently shimmer and smear; anisotropic filtering keeps them crisp into the distance — but it needs a true-colour texture path first and should stay tunable so DOOM's chunky-pixel look survives.
  Kind: enhancement.
  Source: user-request-2026-06-29.

- ✅ [DOOM-0133] **Fix vkCmdClearColorImage validation error: storage images cleared without VK_IMAGE_USAGE_TRANSFER_DST_BIT.**
  At startup two STORAGE_BIT images are cleared with vkCmdClearColorImage, which requires VK_IMAGE_USAGE_TRANSFER_DST_BIT in the image's usage flags (VUID-vkCmdClearColorImage-image-00002). Symptom in the log: two images (e.g. 0x7e..., 0x81...) flagged immediately after swapchain creation. Fix: add VK_IMAGE_USAGE_TRANSFER_DST_BIT to those images' VkImageCreateInfo.usage (candidates: the SVGF G-buffer / rtAccum storage images), or clear them via a compute store / render-pass clear instead. Pre-existing (not from DOOM-0129/0130/0131). Validation-only; no functional impact observed.
  Kind: fix.
  **Layman:** A harmless-but-incorrect graphics warning at startup: two images are wiped using a method their creation flags don't permit. The game runs fine, but it should be made spec-correct.
  Kind: fix.
  Source: terminal-log-2026-06-29.
  Resolved (2026-06-29): the TAAU history images (taImg, non-TA_OUT) were created STORAGE-only but are cleared at init/resize via vkCmdClearColorImage, which requires TRANSFER_DST. Added VK_IMAGE_USAGE_TRANSFER_DST_BIT to the non-output taImg usage (r_vulkan.cpp CreateTaauTargets). The svImg path already had it. Builds clean; confirm the two startup vkCmdClearColorImage validation errors are gone with a validation-layer run.

- 📋 [DOOM-0134] **Fix render-pass/framebuffer incompatibility validation errors (srcStageMask/Access mismatch between two render passes).**
  vkCmdBeginRenderPass / vkCmdDraw report pDependencies[0] srcStageMask, srcAccessMask and dstAccessMask incompatible between VkRenderPass 0xd... and VkRenderPass 0xc... (the framebuffer was created against 0xc but begun/used with 0xd) — VUID-VkRenderPassBeginInfo-renderPass-00904 / VUID-vkCmdDraw-renderPass-02684. The two passes' subpass dependencies differ (ALL_TRANSFER vs EARLY_FRAGMENT_TESTS|COLOR_ATTACHMENT_OUTPUT). Fix: make the render pass used at BeginRenderPass / pipeline creation render-pass-compatible with the one the framebuffer was created from (align the subpass dependency stage/access masks, or create the framebuffer against the matching pass). Pre-existing raster-path issue (not from DOOM-0129/0130/0131). Validation-only.
  Kind: fix.
  **Layman:** Another startup/raster-path graphics warning: a drawing surface was set up with one recipe but used with a slightly different one. Cosmetic to the validation layer, but worth making consistent.
  Kind: fix.
  Source: terminal-log-2026-06-29.

- ✅ [DOOM-0135] **Menu "Debug Views" toggle gating the ~ path-tracer diagnostic cycle; ~ becomes plain RT on/off when debug is off.**
  Current wiring (verified): rb_rtdebug (the ~ key, values 0=raster .. 6=denoised, 5=headless-verify-only) is the ONLY thing that switches the Vulkan backend between the flat raster view and the path-traced view. rb_rtdebug is written in exactly 3 places: r_vulkan.cpp:575 (default 6), r_backend.c:310-311 (init clamp), i_video.c:330-332 (the ~ cycle). The menu "Renderer" selector (Classic/Solid/Ultra via RB_SetMode) does NOT touch rb_rtdebug, and Solid (RB_RASTER3D) + Ultra (RB_RT3D) share the same Vulkan_Present/Vulkan_Init — only their Available() probe differs. So today picking "Solid" does not disable the tracer; the ~ key does. There is ONE shared RendererDef menu (m_menu.c:413), so a new item appears for both Solid and Ultra automatically.
  
  Plan:
  - New persisted int rb_rtdebug_menu (default 0 = off), m_misc.c default + extern in r_vulkan.cpp beside rb_rtdebug.
  - Menu: add rm_debugviews item to the RendererMenu[] enum/array + draw "Debug Views: On/Off" in M_DrawRendererMenu; handler toggles the flag. On turning it OFF, snap rb_rtdebug out of any diagnostic value (1-4) to 6 so the view doesn't stick on white-furnace etc.
  - ~ handler (i_video.c): if debug on -> existing full cycle (0,1,2,3,4,6, skip 5); if debug off -> toggle rb_rtdebug 6<->0 (RT on/off).
  
  OPEN QUESTION (blocks final design): should selecting Solid vs Ultra in the Renderer menu also drive RT off/on (Solid forces rb_rtdebug=0, Ultra forces 6), so the menu label matches what's shown? Today they don't, which is likely the source of the confusion. Resolve with the user before implementing.
  Kind: feature.
  **Layman:** Add a Debug Views on/off switch to the Renderer menu. With it off (the normal case), the ~ key simply turns ray tracing on or off; with it on, ~ cycles through the developer diagnostic views (normals, white-furnace, etc.) as it does today.
  Kind: feature.
  Source: user-request-2026-06-29.
  Resolved (2026-06-29): added rb_rtdebug_menu (persisted "rt_debug_views", default 0) and a "Debug Views: On/Off" item in the shared Renderer menu (m_menu.c rm_debugviews, drawn between Render Scale and Brightness so it doesn't collide with the brightness thermo). ~ handler (i_video.c) now branches: debug off -> toggle rb_rtdebug 6<->0 (plain RT on/off); debug on -> full diagnostic cycle as before. Toggling debug off snaps rb_rtdebug out of diagnostic values 1-4 to 6. Per user decision the Solid/Ultra menu label is NOT linked to RT on/off (the ~ key remains the only RT switch). Builds clean; in-game menu/keyboard check pending on user. The open question in the original bullet (Solid forces raster) was answered: no.

- ✅ [DOOM-0136] **Map the RT-view toggle to the PS4/PS5 touchpad — clicking its right half mirrors the ~ key.**
  Resolved (2026-06-29): extracted the ~-key RT-view logic into a shared I_ToggleRtView() (i_video.c) so the keyboard and gamepad share one path. In I_PollGamepad the whole-pad click (SDL_CONTROLLER_BUTTON_TOUCHPAD) is edge-detected; on the press edge SDL_GameControllerGetTouchpadFinger(gamepad,0,0,...) reads finger 0's normalised position and the toggle fires only when a finger is down on the right half (x >= 0.5). SDL 2.32.70 on this box, well past the 2.0.14 touchpad-API floor. No-op on pads without a touchpad (the button never reports). Honours the DOOM-0135 Debug Views toggle (RT on/off vs diagnostic cycle). Builds clean; in-game controller check pending on user.\nKind: feature.
  **Layman:** On a PlayStation controller you can now press the right side of the touchpad to turn ray tracing on/off (or cycle debug views when that mode is on), just like the ~ key on the keyboard.
  Kind: feature.
  Source: user-request-2026-06-29.

- ✅ [DOOM-0137] **Stop the startup flood of "V_DrawPatch: bad patch (ignored) / exceeds LFB" border-bezel warnings.**
  Symptom (verified in a profiling run): ~80 lines of \"Patch at X,Y exceeds LFB\" + \"V_DrawPatch: bad patch (ignored)\" at startup, at coords like 0,-3 / 8,-3 ... / 320,168 (the viewport border bezel, drawn ~3px outside the play area at 8px steps).\n\nRoot: V_DrawPatch (v_video.c:245-253) rejects + warns when a patch's x/y (+ width/height) fall outside the 320x200 logical screen or scrn>4, then returns. The view-border bezel pieces are being drawn at out-of-bounds coordinates (negative / >=320), so each is skipped with a warning. Pre-existing (the id ChangeLog notes the same TNT.WAD case); surfaced loudly here, likely because the 3D backend's screen/viewport sizing (scaledviewwidth / viewwindowx/y) differs from the software path when the border is first drawn.\n\nFix options: (a) correct the border-draw geometry so the bezel patches land in-bounds (proper root fix); (b) if the bezel genuinely has no place at the current view size, skip drawing those pieces rather than calling V_DrawPatch and getting rejected; (c) at minimum, demote the per-patch fprintf to a single rate-limited/once message so the log isn't flooded. Cosmetic / log-noise only; no visible rendering defect reported.\nKind: fix.
  **Layman:** At startup the log spits out dozens of harmless 'bad patch' warnings while drawing the screen border. The picture is fine; it's just noise that should be silenced or the border draw fixed.
  Kind: fix.
  Source: terminal-log-2026-06-29.
  Resolved (2026-07-17): rate-limited the V_DrawPatch RANGECHECK reject to the first 3 occurrences plus a one-line suppression note, so the startup border-bezel flood is gone. Log-hygiene only; no render change. The underlying constraint (UI art drawn past the 320-wide LFB) is named in the code comment and left as a separate border/tiling geometry task. Commit f026e8f.

- ✅ [DOOM-0138] **EV_DoDonut two-sided check is a no-op: (!flags) & ML_TWOSIDED is always 0 (precedence bug).**
  p_spec.c EV_DoDonut (~line 1187): `if ((!s2->lines[i]->flags & ML_TWOSIDED) || ...)`. `!` binds tighter than `&`, so this is `((!flags) & ML_TWOSIDED)` -- !flags is 0 or 1, and 0&4 == 1&4 == 0, so the term is ALWAYS false. The intended test is `!(flags & ML_TWOSIDED)` (skip non-two-sided lines). This is a known vanilla 1997 bug; some source ports fix it, some keep it for demo compat. During the -Wall sweep (DOOM-0140) the expression was parenthesised to `((!flags) & ML_TWOSIDED)` to PRESERVE the shipped (always-false) behaviour and silence -Wparentheses. Decision needed: fix to `!(flags & ML_TWOSIDED)` (changes donut behaviour on some maps; no demo-compat concern in this fork) or keep vanilla. Kind: fix.
  **Layman:** A long-standing original-DOOM bug in the 'donut' sector effect: a check meant to skip one-sided lines never actually fires. Harmless on most maps but can misbehave on hand-crafted donuts. Left as-is for now (so vanilla maps play identically); flagged for a real fix.
  Kind: fix.
  Source: warning-sweep-2026-06-29.
  Resolved (2026-07-17): fixed the operator-precedence no-op `(!flags) & ML_TWOSIDED` to the intended `!(flags & ML_TWOSIDED)`, so EV_DoDonut skips one-sided lines as designed (and no longer risks a NULL-backsector deref on a malformed donut). Well-formed donuts select the same line, so stock maps are unchanged. Builds clean (no -Wparentheses). Commit c38a062. Verified by inspection + clean build; a crafted donut WAD would be needed to exercise the malformed edge case, which is not present in the stock IWADs.

- ✅ [DOOM-0139] **Sky-texture selection compares gamemode against gamemission enums (pack_tnt/pack_plut).**
  g_game.c G_DoLoadLevel (~line 464): `(gamemode == commercial) || (gamemode == pack_tnt) || (gamemode == pack_plut)`. pack_tnt/pack_plut are GameMission_t, not GameMode_t. Numerically pack_tnt==2==commercial and pack_plut==3==retail (doomdef.h), so the expression actually evaluates to `gamemode==commercial || gamemode==retail` -- i.e. the TNT/Plutonia clauses collapse to 'commercial or retail', and Ultimate Doom (retail) hits the SKY3 branch. Intended check is almost certainly `gamemission == pack_tnt` / `gamemission == pack_plut`. During the -Wall sweep (DOOM-0140) the constants were cast to GameMode_t to PRESERVE shipped behaviour and silence -Wenum-compare. Decision needed: switch to gamemission (correct Final DOOM skies, changes retail sky) or keep vanilla. Kind: fix.
  **Layman:** The code that picks the sky for Final DOOM (TNT/Plutonia) accidentally checks the wrong setting, so the special sky logic doesn't trigger the way it was meant to. Cosmetic, vanilla behaviour preserved for now.
  Kind: fix.
  Source: warning-sweep-2026-06-29.
  Resolved (2026-07-17): switched the Final DOOM sky test from gamemode to the correct gamemission field, so retail (Ultimate Doom) no longer falls into the commercial map-range branch. Verified in-game on doom.wad (retail) with a temporary probe: E1M1->SKY1(59), E2M1->SKY2(240), E3M1->SKY3(241), E4M1->SKY4(242) -- all distinct episode skies; pre-fix all four would have been SKY1. Commit 08545f5.

- ✅ [DOOM-0140] **Eliminate all -Wall compiler warnings (behaviour-preserving sweep).**
  Goal: a clean `make` with zero warnings under -Wall, no behaviour change. Started at 98 warnings (mostly id's 1997 code). Part 1 (committed): Z_ChangeTag macro indentation (-41) + dead unused locals + SNDINTR-gate i_sound flag (98->43). Part 2 (in progress): 64-bit pointer/int casts via intptr_t/uintptr_t (p_saveg PADSAVEP + load-side index casts, z_zone owner check, d_main statcopy); sprintf target buffers enlarged (f_finale, p_setup, wi_stuff); strcmp signedness cast (g_game); r_things sort sentinel made static (dangling-pointer false positive). Two latent VANILLA bugs found and PRESERVED (silenced only): DOOM-0138 (EV_DoDonut precedence) and DOOM-0139 (sky gamemode/gamemission enum) -- both surfaced for a separate decide-and-fix. Also runtime: Vulkan validation (DOOM-0133/0134) and the bad-patch flood (DOOM-0137) are tracked separately. Kind: chore.
  **Layman:** Clean up all the compiler's complaints so the build is warning-free, without changing how the game plays.
  Kind: chore.
  Source: user-request-2026-06-29.
  Resolved (2026-06-29): `make clean && make` now reports 0 warnings/errors under -Wall (was 98). Part 1 (commit DOOM-0140 pt1): Z_ChangeTag macro indentation + dead locals + i_sound flag SNDINTR-gate. Part 2: intptr_t/uintptr_t for all pointer<->int casts (p_saveg PADSAVEP + 10 load-side index casts, z_zone owner check, d_main statcopy, d_net 2 offset hacks); socklen_t for i_net recvfrom; sprintf buffers enlarged (f_finale 16, p_setup 16, wi_stuff 40); strcmp signedness cast (g_game); r_things sort sentinel made static. Behaviour preserved throughout. Two latent vanilla bugs found, silenced-not-fixed, tracked as DOOM-0138 (donut precedence) + DOOM-0139 (sky enum). Runtime warnings remain separate: DOOM-0133/0134 (Vulkan validation), DOOM-0137 (bad-patch flood). Builds clean; gameplay unchanged (needs a play sanity-check).

- ✅ [DOOM-0141] **Render the DOOM sky in the ray-traced view (no more see-through floating geometry).**
  Root cause: the RT mesh builder SKIPS sky ceilings/sky-border walls (r_mesh.c:362, :446-448), so primary rays escape through the gap and either hit distant geometry (it "floats") or miss into the flat SKY_COLOR fill (pt_common.glsl:30). Classic DOOM fakes a solid sky backdrop that occludes everything behind it (r_plane.c); the tracer has no analogue. A full sky panorama already exists for the raster path (mesh.frag FLAG_SKY + RB_BuildSky) but is disabled under RT (r_vulkan.cpp:4951).
  
  Fix (RT-only, isolates the change from the working raster/Solid path): emit sky surfaces into a SEPARATE mesh + static BLAS on a 3rd TLAS instance (customIndex 2, mask 0x04) that only PRIMARY rays see — shadow rays + the GI bake cull to 0x01 so they never hit it (no false shadows, GI unchanged, raster untouched).
    Stage 1: emit sky geometry + sky BLAS/instance; tracer treats a sky hit like a miss (flat SKY_COLOR) but now OCCLUDING -> floating geometry gone.
    Stage 2: shade the sky hit (and miss) as the real cylindrical sky-texture panorama by ray/screen yaw, mirroring mesh.frag; composite outputs sky raw (no tonemap) so Ultra matches Classic's mountains.
  Files: r_mesh.c/.h (separate sky vert list), r_vulkan.cpp (sky buffer/BLAS/instance + skytexnum in misc4.w), pathtrace.comp (sky hit/miss panorama), svgf_composite.comp (raw sky out).
  **Layman:** In the Ultra/Solid ray-traced view the sky was a hole, so distant buildings floated in mid-air and the sky showed as flat blue instead of the mountains you see in Classic. This makes the sky a solid backdrop again.
  Kind: fix.
  Source: user-report-2026-06-29 (level 2 screenshot: geometry floating against flat-blue sky).
  Resolved (2026-07-12, user-confirmed): the DOOM sky now renders as a solid backdrop in the ray-traced view (sky BLAS/instance seen by primary rays only; Stage 2 panorama shading) so the sky-hole no longer lets distant geometry float, and the raster view got the same world-space sky-dome occluder in DOOM-0162 (GPU-verified 2026-07-03). The wall/sky seam this introduced was fixed with the below-horizon haze fade in DOOM-0143 (799a850, signed off 2026-07-02). Remaining leftover is tracked separately as DOOM-0142 (a mid-ground occluder wall rendered too short in the 3D mesh on level 2's vista — a shared-mesh geometry bug, explicitly not a sky issue).

- 📋 [DOOM-0142] **Mid-ground occluder wall missing in the 3D mesh (geometry visible over a wall Classic blocks).**
  Confirmed a SHARED-mesh bug (identical in Solid raster + Ultra RT), pre-existing, NOT sky-related and NOT fixed by DOOM-0141. A/B at the same spot: Classic (image #5) shows a tall solid brown wall occluding the mid-ground (sky above it only); Solid/Ultra (images #6/#7) show that wall too short/absent, revealing distant techbase buildings + a rocky/nukage band behind it (the apparent 'floating geometry'). The missing chunk is the UPPER ~half of the wall (screen ~52-78%). Leading suspects in r_mesh.c wall emission: (a) emit_wall drops any upper/lower step whose texture is '-' (texnum<=0, r_mesh.c:208) -> a height-step wall the map leaves untextured but classic still occludes; (b) a sector floor/ceiling height baked wrong; (c) classic's per-column sky/visplane occlusion hiding geometry that true-3D reveals. Needs the exact WAD/level + the in-engine normals debug view (or a walk-into-it collision test) to pin which. Likely also affects other open-vista maps.
  **Layman:** On level 2's outdoor vista, the 3D renderers (Solid + Ultra) let you see over/through a wall that the Classic renderer correctly uses to block the view, so distant buildings look like they float.
  Kind: fix.
  Source: user-report-2026-06-29 (level 2; Classic vs Solid vs Ultra A/B screenshots).
  Research (2026-06-30, Doom source-port community): this is the canonical "2.5D BSP world -> real 3D mesh" failure mode. Vanilla occlusion is IMPLICIT (BSP draw-order + solidsegs clip list skip hidden strips); a 3D mesh fed to a GPU loses it, so geometry the original never drew shows through ("floating"). Strongly corroborates suspect (a): in vanilla a wall step with NO texture ("-") still OCCLUDES (solid to the renderer though it draws nothing); our emit_wall dropping texnum<=0 steps (r_mesh.c:208) deletes exactly that occluder. Fix pattern other true-3D ports/converters use: emit an INVISIBLE-BUT-SOLID surface for missing-texture steps (write depth, draw no color) instead of dropping them, and let the depth buffer hide what's behind. Two related cases to handle while here (so we don't trade one artifact for another): (1) self-referencing sectors (both sidedefs reference the same sector -> vanilla treats as "not there": invisible bridges/deep water/fake rooms) must be detected and special-cased, NOT walled off -- GZDoom's softpoly true-3D renderer was partly discontinued over this; (2) 2-sided MIDDLE textures (cages, hanging bars) are INTENTIONALLY mid-air and must stay alpha-tested see-through quads, not become solid. Net fix direction for DOOM-0142: occlude-don't-draw for "-" steps; verify it doesn't resurrect HOM or block self-ref sectors. Sources: DoomWiki Hall_of_mirrors / Making_a_self-referencing_sector; ZDoom forums softpoly self-ref thread.

- ✅ [DOOM-0143] **Seam (black line + white sliver) where the RT sky cap meets a wall top.**
  Introduced by DOOM-0141's sky ceiling caps. The sky cap (a horizontal polygon at the sky sector's ceiling height) does not meet the wall's top edge cleanly -- a hairline gap (black line, background showing) and a grazing-angle sliver of the cap shading bright sky (white triangle). Likely the DOOM-0065 cap-overshoot trim (clip_poly to each seg's front half-plane) leaving the sky cap edge a hair short of the wall, and/or a T-junction at the shared edge. Fix: align the sky cap edge to the wall top (small overshoot or shared-edge snap), or depth-bias. Minor/cosmetic; the occlusion itself is correct.
  **Layman:** With the new ray-traced sky, a thin dark line and a small bright sliver can show right where a wall meets the sky.
  Kind: fix.
  Source: user-report-2026-06-29 (image #9, Ultra, after DOOM-0141).
  Resolved 2026-07-02 (799a850). Root cause was NOT a cap T-junction. Verified: the RT world mesh and raster draw share g.vbuf (identical wall heights), and shipping mode 6 returns skyPanorama on both miss and sky-hit -- so a geometry crack shows sky, not black. The real cause: skyPanorama maps the sky's vertical texel by screen-Y with a fixed scale (row = 100 + (suv.y-0.5)*200); below the horizon `row` runs past the 128px sky texture and the sampler's vertical REPEAT wraps it back to the bright top-of-sky rows -- a bright strip where the RT backdrop fills below eye level (the window). Clamping to the base row instead only trades bright for dark (the DOOM-0076 black band). Fix: clamp the vertical sample (kill the bright wrap) then fade the below-horizon strip into a soft neutral haze (user's fog suggestion) so the mountains dissolve into mist at the wall line instead of showing either seam; fog rises with screen depth so it self-aligns to the wall. RT-only (skyPanorama); mesh.frag untouched so DOOM-0076 can't regress. User signed off (black only visible if actively looking for it). Tunable fog tone/onset constants left for future dialing.

- ✅ [DOOM-0144] **Split the rt_profile denoise+TAAU bucket into temporal/a-trous/composite/TAAU sub-timings.**
  The rt_profile line (\\ key) lumped the whole denoiser into one 'denoise+taau' number. Added GPU timestamps 5/6/7 (the timer pool already had 8 slots, only 0-4 used) at the temporal/a-trous/composite boundaries inside RecordRtTrace's denoise block; non-mode-6 frames write the spare slots collapsed so the 8-query readback stays available (no VK_NOT_READY). profMs grew 4->8; the print now reads 'denoise+taau X (temporal a, atrous b, composite c, taau d)'. Instrumentation only, no behaviour change. NEXT SESSION: capture a fresh rt_profile line on E1M2 to see whether a-trous (4 iters @ render res) or TAAU (@ 4K display res) dominates, then optimise the fat one losslessly (e.g. TAAU is the only pass at full 4K).
  **Layman:** The on-screen/log performance readout now shows exactly which part of the denoiser is slowest, so we can target the right thing next session.
  Kind: perf.
  Source: in-session-2026-06-29 (profiler showed denoise+taau is a flat ~7.2ms ~38% of frame; need the sub-breakdown to optimise).

- 📋 [DOOM-0145] **Windows 0.2.0 build: 3D view renders as a small centered box with garbled (uninitialized) borders.**
  Reported on the shipped 0.2.0 Windows build. Symptoms: (1) fullscreen window, but the actual game view is a small centered rectangle with garbled imagery (fragments of the DOOM II title art + scattered text) filling the surrounding area; (2) the 2D menu + status-bar overlay render cleanly at full width ON TOP of the garbage (confirmed by screenshot of the in-game ESC menu); (3) audio "struggling" (unspecified — crackle vs missing music vs none).
  
  Leading hypothesis (display): in a 3D mode (Solid/Ultra) the scene is traced into a render-scale sub-rectangle (default render_scale=50%, m_misc.c:255) that TAAU upscales to the display. If TAAU is not upscaling to fill the swapchain on this GPU, the present (which uses full g.extent, r_vulkan.cpp:4947) shows the 50% box + never-cleared swapchain borders = the garbage. Default renderer is RB_CLASSIC (m_misc.c:253), so this only bites if the friend selected Solid/Ultra, OR a config persisted a 3D mode. Alt hypotheses to rule out: a Classic-path SDL present/pitch bug specific to Windows; the DOOM-0050 2D-overlay-over-3D family.
  
  Disambiguating facts needed (cannot reproduce without the friend's box): render mode selected; GPU + Windows version; whether switching to Classic and/or setting Render Scale 100% clears it; whether -windowed changes it. Likely real fixes once pinned: (a) clear the full swapchain image each frame so uncovered borders are black not garbage; (b) ensure TAAU upscales to full display extent (or blit the render sub-rect scaled to g.extent when TAAU off). Audio is a separate sub-investigation (SDL_OpenAudio 11025 Hz legacy device, i_sound.c:825).
  **Layman:** On Windows, the game view shows up as a small box in the middle of the screen with garbled junk around it; sound also struggles.
  Kind: fix.
  Source: user-report-2026-06-30 (Windows friend; original verbal report + in-game menu screenshot).
  Confirmed by friend's in-game photo (2026-06-30): the 3D scene renders CORRECTLY but only into a ~50%-size centered sub-rectangle (= render_scale=50%); the status bar draws full-width and correct; the uncovered border region shows stale/uninitialized framebuffer content (leftover menu/skill-select text: NEW GAME, NIGHTMARE!, etc.). So this is NOT a crash or RT-capability failure — the engine works; it's a present/upscale bug. Code: r_vulkan.cpp:4454 gates the render sub-rectangle on taauActive = (rb_rtdebug==6 && rb_upscaler==1 && taauPipeline!=NULL); at render_scale<100 the rendered region is not being scaled to fill g.extent on this GPU, and the borders are never cleared. Immediate user workaround (any one): Options->Renderer-> Render Scale=100%, OR Upscaler=Off, OR Renderer=Classic. Real fix: (a) always blit/scale the rendered region to the full display extent (never present the sub-rect 1:1); (b) clear the swapchain image to black each frame so uncovered areas aren't garbage. Still need: friend's GPU model + audio specifics.
  GTX 1050 / Win10 test (2026-06-30, screenshots (1)&(2)): in SOLID renderer the view is FULL-SCREEN and clean at 35-59 FPS — no small box, no garbled borders. This isolates the bug: it is NOT Solid mode and NOT a generic Windows present bug. It is specific to a 3D path running at render_scale<100% where the upscale-to-display is not filling the swapchain (i.e. the Ultra/TAAU path: taauActive at r_vulkan.cpp:4454 traces into a 50% sub-rect; if its output is not blitted scaled-to-g.extent, you get the friend's small-box+garbage). Confirms the fix: (a) always scale the rendered sub-region to the full display extent on present; (b) clear the swapchain to black each frame. Practical guidance for the friend NOW: use Solid (works); Ultra needs RT hardware the GTX 1050 lacks anyway (ties to DOOM-0059/DOOM-0026 capability gating). Still want: one Ultra-mode screenshot on the GTX 1050 to confirm whether Ultra runs/garbles/gates-off there.
  Friend's machine identified (2026-06-30): GTX 2060 (RT-capable, has RT cores) on a 4K laptop. So the small-box repro = Ultra at render_scale 50% on a 4K display = a 1920x1080 render box shown un-upscaled in the centre of a 3840x2160 screen, surrounded by uncleared garbage. Matches the present/upscale theory exactly. This is the one machine that reproduces DOOM-0145 (RX 6600 upscales fine; GTX 1050 has no Ultra). Verification of the fix will need the GTX 2060 box. NOTE: separate from the GTX 1050 "not full screen / border" report, which is the in-game Screen Size (screenblocks) ornamental border + Classic's 4:3 letterbox on a 16:9 display — tracked separately.

- 📋 [DOOM-0146] **Ultra must be selectable on ALL Vulkan-capable machines; gate only the ray-casting on RT hardware, not the menu option.**
  Today backends[RB_RT3D] ("Ultra").Available = Vulkan_RT_Available = RB_Vulkan_Available(1) (r_backend.c:83,230), so on a non-RT GPU (e.g. GTX 1050 Pascal) Ultra is hidden from the Options->Renderer cycle and the ~/\\ RT keys no-op. User's design intent: Ultra must ALWAYS be offered wherever plain Vulkan runs, because HD graphics/assets are coming to the Ultra view that do NOT require ray tracing and must reach all users. The ONLY per-machine RT gate is whether actual rays are cast.
  
  Change: point Ultra's Available() at RB_Vulkan_Available(0) (same as Solid) so it's listed whenever Vulkan works; add a runtime RT-capability guard INSIDE the Vulkan backend so selecting Ultra on a non-RT card renders the 3D view WITHOUT ray tracing (raster + future HD assets), never attempting ray-query/megakernel (which would crash or garbage). The menu/label should still convey RT on vs unavailable. Solid & Ultra already share one Vulkan backend differing only by rb_rtdebug RT on/off ([[solid-ultra-same-renderer]]), so "Ultra without RT" ~= Solid today until HD assets land. Cross-ref DOOM-0059 (descriptor-indexing probe gate) and DOOM-0145 (reduced-scale present). Needs on-HW verify: Ultra appears + renders (no RT) on the GTX 1050; still casts rays on RT cards (RX 6600 / GTX 2060).
  **Layman:** The Ultra graphics mode should appear for everyone, not just people with ray-tracing cards — because the upcoming HD visuals for Ultra are for all users. Only the ray-traced lighting itself should switch off on cards that can't do it.
  Kind: enhancement.
  Source: user-request-2026-06-30 (Windows GTX 1050 has no Ultra option; design intent clarified).
  CORRECTION to the body's framing (user clarified 2026-06-30, matches memory render-mode-menu-names): the three renderers are Classic (90s software) / Solid (original art rasterized 3D) / Ultra (original art REPLACED with HD models, 3D). Ray tracing is an ORTHOGONAL on/off capability available in BOTH Solid AND Ultra when the GPU supports it — it is NOT the Solid-vs-Ultra distinction (that is purely the art set). So my earlier 'Ultra-without-RT ~= Solid' line is wrong: Solid can also run RT. Correct gating: Classic always; Solid AND Ultra both available whenever plain Vulkan works (RB_Vulkan_Available(0) for BOTH — Solid is already correct at r_backend.c:235; only Ultra at :230 must change from Vulkan_RT_Available/RB_Vulkan_Available(1) to the (0) check). Ray Tracing becomes a separate On/Off control, enableable only on RT-capable hardware, applied to whichever 3D renderer (Solid or Ultra) is active. This is the DOOM-0009 build-step-1 'two controls: Renderer + Ray Tracing' split that isn't fully implemented yet (the enum RB_RASTER3D/RB_RT3D still couples mode to RT). On the GTX 1050 the expected result is: BOTH Solid and Ultra appear and render without RT; the RT toggle stays disabled/greyed. Cross-ref DOOM-0009 §2 menu rework.

- ✅ [DOOM-0147] **Classic presents at 16:10 (square-pixel 320x200), not authentic 4:3 — letterboxes on 4:3 monitors; add a 4:3-vs-fill aspect choice.**
  i_video.c:660 calls SDL_RenderSetLogicalSize(renderer, SCREENWIDTH=320, SCREENHEIGHT=200), so SDL preserves a 1.6:1 (16:10, square-pixel) aspect when scaling the software framebuffer to the window. DOOM's authentic display is 4:3 (VGA stretched the 200 lines ~1.2x vertically). On a 4:3 monitor the 1.6 image is letterboxed -> black bars top+bottom (confirmed screenshot 6, Classic, max screen size) and the geometry is vertically squished ~1.2x vs original. Same path drives the title/intermission screens (screenshot 3 'title not full screen'), so they bar too. Solid/Ultra are unaffected (Vulkan draws to the full window extent).
  
  Fix: present Classic at 4:3. Options: SDL_RenderSetLogicalSize(320,240) and let RenderCopy stretch the 320x200 texture to the 320x240 logical area (1.2x vertical = authentic 4:3), letterboxing 4:3 to the window (fills a 4:3 monitor; pillarbox on widescreen). PLUS add a player aspect option (per user request 2026-06-30): {4:3 authentic, Fill/Stretch-to-window}. 4:3 = correct proportions, may bar on non-4:3 monitors; Fill = edge-to-edge on any monitor, distorts off-4:3. Persist via m_misc.c; expose in the Options/Renderer menu. NOTE 'no HUD' in the user's max-screen-size shots is expected (screenblocks 11 hides the status bar) — dropping one notch restores it; not part of this fix. Verify on the 4:3 Windows monitor (no bars in 4:3 mode) and a 16:9 display (correct pillarbox vs fill).
  **Layman:** In Classic mode the old-style picture has black bars and isn't shaped quite right; we should make it fill the screen the authentic way, and let players choose 4:3 (original) or stretch-to-fill.
  Kind: fix.
  Source: user-report-2026-06-30 (Windows 4:3 monitor; screenshots 3 & 6 show black bars top+bottom in Classic + title screen).
  Scope refinement (user 2026-06-30): prefer AUTHENTIC WIDESCREEN over stretch. Three aspect choices, not two: (1) 4:3 authentic (pillarbox on widescreen, fills a 4:3 monitor); (2) Widescreen — extend the horizontal field of view so MORE of the level renders left/right at correct proportions (Crispy/GZDoom-style), NOT a horizontal squash; (3) optional Fill/Stretch fallback (distorts off-4:3) — include only if cheap. The 4:3-bar fix (logical-size 4:3 present) and the aspect option remain as described; authentic widescreen is the larger sub-part: widen ORIGWIDTH beyond the 4:3 ratio (320 -> ~426 for 16:9) and recompute the view projection. Engine is already parameterised for this — SCREENWIDTH = ORIGWIDTH*HIRES (doomdef.h:112) from the DOOM-0027 hi-res work, and the projection keys off centerxfrac/SCREENWIDTH (r_main.c:557, R_InitTextureMapping), so the buffer can grow without a rewrite. Effort: authentic widescreen touches the SW renderer FOV setup, so it is a proper feature, bigger than the present-time toggle. Cross-ref DOOM-0027 (hi-res buffer parameterisation).
  Part A shipped 2026-06-30 (16e076b): authentic 4:3 present. Confirmed via screenshot 9 (fills width; thin top/bottom bars remain because the test monitor is 5:4 / 1280x1024, slightly taller than 4:3 — expected for true 4:3). Remaining: Part B (aspect-correct render at the display's actual shape -> fills 5:4 AND 16:9 with no bars, no stretch) and Part C (menu toggle + persist).
  Part B design is now in docs/specs/DOOM-0147-widescreen.md (cold-eyes reviewed). It SUPERSEDES the inline one-line sketch in this entry: the literal "widen ORIGWIDTH (320->426) and let projection follow centerxfrac" reading is incomplete — the load-bearing correction is a NONWIDEWIDTH=320 reference for focallength/projection/yslope (so extra columns extend FOV instead of zooming), and the physical cap reuses+enlarges the existing r_draw.c MAXWIDTH (1120->>=1280), NOT a new constant. Also: the inline "i_video.c:660 ... SCREENHEIGHT=200" cite above is pre-Part-A; the current present call is i_video.c:666 (SCREENWIDTH, SCREENWIDTH*3/4). See the spec for the authoritative approach + affected-files list.
  Part B (authentic Hor+ widescreen for Classic) IMPLEMENTED 2026-06-30 per docs/specs/DOOM-0147-widescreen.md. SCREENWIDTH is now runtime (chosen from display aspect in I_InitWidescreen, d_main before V_Init); ~16 view-width arrays repointed to a promoted MAXWIDTH (doomdef.h, 1120->1280); projection focal length / world-scale / yslope / pspritescale reference a non-wide 4:3 half-width so widescreen columns EXTEND the FOV (no stretch); UI art centred via WIDESCREENDELTA in V_DrawPatch/Flipped + status-bar copy; present aspect = SCREENWIDTH x SCREENHEIGHT*6/5. Discovered-in-impl (see spec Implementation notes): V_* scale factor pinned to HIRES (was dsw/ORIGWIDTH, wrong at 21:9+); block 10 made full-width so widescreen works with the DOOM-0148 HUD-always-on cap, status-bar sides blacked out. 4:3/5:4 is a provable zero-diff no-op (user's 1280x1024 unaffected). Builds clean Linux+Windows; Windows exe redeployed to test share. Awaiting visual confirmation on a true-widescreen (16:9) display. Part C (menu 4:3<->Widescreen toggle + optional 5:4 fill-stretch + persist) still open; item keeps 🚧.
  Part C IMPLEMENTED 2026-07-01: two persisted display prefs on the Renderer sub-menu. Widescreen On/Off forces 4:3 even on a wide display (sizes SCREENWIDTH at startup, so it shows "(restart)" and applies next launch). Fill Screen On/Off is a live present-time stretch to fill the monitor, removing letter/pillar bars at the cost of the 4:3 vertical (6/5) correction. New globals widescreen=1 / fillstretch=0 (i_video.c, extern doomdef.h) + defaults rows in m_misc.c; new I_SetAspect() applies the present aspect (SCREENHEIGHT*6/5, or 0,0 to disable logical scaling); M_LoadDefaults moved above I_InitWidescreen so the widescreen pref is loaded before SCREENWIDTH is sized. Spec docs/specs/DOOM-0147-widescreen.md Part C cold-eyes-converged (3 loops). Builds clean Linux+Windows; doom_ants.exe redeployed to the test share. Item stays 🚧 ONLY pending the user's visual confirmation on a true-widescreen (16:9) display — all implementation (Parts A/B/C) is now complete. DOOM-0151 (static-screen side gaps) remains a separate open item; closing this does not close it.
  CRASH FIX 2026-07-01 (found in first real-widescreen testing): entering a level with the HUD visible (Screen Size <= block 10) on a true-widescreen display aborted with I_Error "Bad V_CopyRect". Root cause: V_CopyRect range-checked the destination x against a fixed ORIGWIDTH (320), but the status-bar refresh (st_stuff) copies to a WIDESCREENDELTA-shifted x into a widescreen buffer whose logical width is SCREENWIDTH/HIRES (> 320), so destx+width exceeded 320 and tripped the guard. Fixed by range-checking each side against its own buffer's logical width (stride/scale); provably identical at 4:3 (both reduce to ORIGWIDTH). Reproduced headless under gdb (forced 16:9 aspect + -warp + block 10) before/after the fix. This is why widescreen appeared to crash "even with Widescreen off" -- Off only applies on restart, so widescreen was still active at runtime. v_video.c V_CopyRect. Builds clean Linux+Windows; exe redeployed.
  Widescreen render fixes 2026-07-01 (found in real 16:9 play, Classic renderer): (1) FLOOR/CEILING SWAM relative to walls -- r_plane.c R_ClearPlanes computed basexscale/baseyscale by dividing by centerxfrac (the WIDE geometric centre), but the Hor+ projection focal length references centerxfrac_nonwide (the 4:3 half-width); the flat texel step therefore didn't match the column-to-angle mapping, so flats drifted as the view moved. Fixed to divide by centerxfrac_nonwide (exported via r_main.h). Zero-diff at 4:3 (the two are equal). (2) GREY SQUARES OVER THE HUD hiding info -- st_lib.c STlib_drawNum/updateMultIcon/updateBinIcon draw the widget with V_DrawPatch (auto +WIDESCREENDELTA on the wide FG) but erased the previous value with V_CopyRect at an UN-shifted dest x, so the grey background-restore rect landed left of the digit and covered adjacent HUD fields. Added +WIDESCREENDELTA to the three erase dest-x (source stays the 320-wide BG scratch). Relies on the earlier V_CopyRect logical-width range-check fix. Builds clean Linux+Windows; exe redeployed.
  Auto-detect 2026-07-01 (user-reported: Windows 5:4 showed Widescreen "On" though the screen isn't widescreen). I_InitWidescreen now forces widescreen=0 when the detected desktop aspect is 4:3-or-narrower (aspect <= 4/3 + eps), or if the display query fails. Effect was already 4:3 there (Hor+ clamps a narrow display to NONWIDEWIDTH), but the menu misleadingly read "On"; now it honestly reads "Off". Displays wider than 4:3 keep the saved preference (default on). i_video.c I_InitWidescreen. Zero effect on true-widescreen displays. Builds clean Linux+Windows; exe redeployed.
  Resolved (2026-08-04): all three aspect choices are shipped and the item has been quiet for five weeks with nothing outstanding stated -- Part A (authentic 4:3), Part B (authentic Hor+ widescreen per docs/specs/DOOM-0147-widescreen.md), Part C (the two persisted prefs), plus the V_CopyRect crash fix, the two widescreen render fixes (swimming flats, HUD erase rects) and the aspect auto-detect, every one of them driven by real testing on the Windows/5:4 machine.
  Verified today rather than closed on the notes, and the verification found a real defect -- just not in this item. Captured Classic on the same fixture in all three modes:
    widescreen 1              3840x2160, aspect 1.778, no bars, and genuinely Hor+ (more level visible left and right than the 4:3 shot, not a horizontal stretch)
    widescreen 0              content exactly 4:3 (2880x2160 = 1.333)
    widescreen 0 + fill       3840x2160, fills the output
  The 4:3 shot initially appeared left-aligned with a 960 px bar on one side only, which looked like a pillarbox-centring bug in THIS item. It was not: `I_SetAspect` uses `SDL_RenderSetLogicalSize`, which centres by SDL's documented behaviour, and the asymmetry was the capture path reading the viewport into an output-sized buffer. Proved by A/B rather than by reading the docs -- the artefact vanishes with `fillstretch 1`, i.e. it tracks the logical size and not anything DOOM draws. Filed and fixed as DOOM-0320.

- ✅ [DOOM-0148] **Keep the HUD always on in-game: cap the Screen Size slider at full-view-with-status-bar (screenblocks 10), never the HUD-less fullscreen (11).**
  DOOM maps screenSize 0..8 -> screenblocks 3..11 (m_menu.c). screenblocks 11 is the fullscreen view with NO status bar; 10 is full-width view WITH the status bar. Fix caps the slider at screenSize 7 (screenblocks 10) so the HUD can never be slid away: M_SizeDisplay case 1 guard 8->7; M_DrawDisplay thermo cells 9->8 so the maxed slider reads full; M_Init clamps a stale screenblocks 11 config down to 10 on load. Applies to all in-game renderers (status bar is composited for blocks<11 regardless of backend; confirmed via screenshots 9/10 where dropping one notch restored the HUD in both Classic and Solid). Title screen unaffected (no HUD there by design).
  **Layman:** Stop the screen-size slider from going high enough to hide the heads-up display during play — the HUD (health, ammo, face) now always stays on in-game (it's still absent on the title screen, which is correct).
  Kind: fix.
  Source: user-request-2026-06-30.
  Resolved 2026-06-30 (a93a6b3): slider capped at screenblocks 10; HUD can no longer be slid away in-game. Built + deployed to the Windows test share.

- 📋 [DOOM-0149] **User-editable higher render resolution for Solid/Ultra (native + supersampled); RT cost scales with resolution, managed via settings.**
  Decision (user 2026-06-30): keep Classic at the faithful 2x (640x400, Crispy-parity, DOOM-0027) and put high resolution where the community puts it — the hardware path. Solid/Ultra (Vulkan) today render at a render_scale fraction of display res (rb_renderscale presets {100,75,67,50}, default 50%) then TAAU-upscale to the display. This item = expose user-editable higher-resolution / quality settings: (1) render at full native (100%) cleanly; (2) optional supersampling above native (DSR-style) for crisper edges; (3) make the resolution/scale + upscaler a first-class settings group. RT (megakernel + SVGF denoiser + shadow rays) and raster cost scale with pixel count, so higher res trades FPS — surfaced honestly as a player setting per the user ("that is what user editable settings are for"). Cross-ref: DOOM-0009 (path tracer), render_scale/TAAU plumbing in r_vulkan.cpp (rb_renderscale, taauActive), and the Classic-vs-Solid/Ultra resolution division researched 2026-06-30 (Crispy caps software at 640x400; ports use OpenGL/Vulkan for 1080p/4K). Distinct from DOOM-0147 (widescreen = field of view, orthogonal to resolution).
  **Layman:** Add sharper, higher-resolution options for the Solid and Ultra 3D views that players can turn up or down. Higher looks crisper but costs frame rate when ray tracing is on — which is exactly why it's a setting the player controls. Classic stays faithful (unchanged); this is the 3D path's job, matching how every other source port does high-res (hardware rendering, not software).
  Kind: enhancement.
  Source: user-request-2026-06-30.

- 📋 [DOOM-0150] **Refresh DOOM-0027 hi-res spec's drifted v_video.c line citations (its own implementation shifted them).**
  Surfaced during DOOM-0147 cold-eyes. docs/specs/DOOM-0027-hires.md cites v_video.c symbols at pre-implementation lines that drifted when DOOM-0027 itself landed: primary table cites already corrected 2026-06-30 (V_DrawPatch :204->:223, V_DrawPatchFlipped :271->:298, V_CopyRect def :158->:162; INV_ASPECT_RATIO :107->:110). REMAINING to verify+fix: the inline refs elsewhere in that doc (V_CopyRect blit :187-188; the :158/:158-166/:173-178/:464 prose refs; V_DrawBlock def :405/blit :428; the :489-492-style alloc cites) — not re-verified yet, so left for a dedicated pass rather than guessed. Low priority (the spec is shipped/frozen; this is doc hygiene). Cross-ref: DOOM-0147-widescreen.md cites the same functions at current lines.
  **Layman:** An older design document points at line numbers in a code file that have since moved, so anyone cross-reading it lands in the wrong spot. Tidy the line references so the doc matches the current code.
  Kind: doc-fix.
  Source: cold-eyes-2026-06-30 (DOOM-0147 spec review, lane 2).

- ✅ [DOOM-0151] **Fill widescreen side-gaps on static 320-wide Classic screens (title/intermission/menu/finale).**
  Follow-up from DOOM-0147 Part B. The in-game view and the status bar are widescreen-correct, but full-screen 320-wide static art (TITLEPIC, WIMAP/INTERPIC, the menu background, finale bunny/cast) is centred via WIDESCREENDELTA with UN-painted sides — on a true-widescreen display those side strips show stale pixels. (The user's 5:4 monitor renders authentic 4:3, so it is unaffected; this only shows on 16:9+.) Cheapest fix: black-clear screens[0] before drawing each such page (D_PageDrawer, WI_drawBackground/WI_slamBackground, M_Drawer background, F_* finale) so the sides are clean black, matching the status-bar side treatment already added. Nicer (later, with the Ultra HD-asset work): genuine widescreen versions of these screens. Verify on a 16:9 display: no garbage strips beside the centred art. Out of scope for Part B by the spec's "widescreen UI artwork" exclusion; logged here so it isn't lost.
  **Layman:** On a wide monitor the old fixed-size pictures (title, menu background, end screens) sit centred with blank strips on either side; tidy those strips so they look intentional.
  Kind: enhancement.
  Source: in-session-2026-06-30 (discovered implementing DOOM-0147 Part B).
  Progress 2026-07-01: user opted (after a licensing review) for a NO-external-art approach to avoid any copyright exposure -- WidePix/Nash is free-but-copyrighted (can't bundle under GPL) and Bethesda's are copyrighted; a truly-uncopyrighted *DOOM* widescreen title can't exist (it derives from id art). Implemented V_ExtendSides (v_video.c/.h): on a widescreen buffer it clamps (repeats) the centred 320-wide image's edge columns outward to fill the side strips, using only the buffer's own already-drawn pixels -- nothing external or redistributed. No-op on the ORIGWIDTH scratch and at 4:3 (WIDESCREENDELTA==0, zero-diff). Hooked into D_PageDrawer, so the TITLEPIC/CREDIT/HELP pages and the menu background (which now forces TITLEPIC, DOOM-0152) fill the screen. Verified headless (forced 16:9) that the title page renders without crashing. REMAINING: apply to the intermission (WI_*) and finale (F_*) backgrounds once the user confirms the edge-clamp look reads well on a real widescreen display; if they prefer a gradient/vignette instead, swap V_ExtendSides' fill. Builds clean Linux+Windows; exe redeployed.
  Progress 2026-07-01: status-bar side strips now filled too (user request "add the same grey box to fill the black sides of the HUD"). st_stuff.c ST_refreshBackground previously memset the widescreen side strips to black; now it edge-clamps the centred bar's own grey edge outward per row (memset(row, row[left], left) / row[right-1]) -- same treatment as the title-screen V_ExtendSides, so the HUD sides read as a continuation of the bar. Skipped at 4:3 (WIDESCREENDELTA==0). Title/menu edge-extend approved by the user ("perfect solution"). Still remaining for full DOOM-0151: intermission (WI_*) and finale (F_*) backgrounds.
  Resolved 2026-07-01: the intermission ("<level> FINISHED" / "ENTERING <level>") backgrounds now edge-extend too -- wi_stuff.c WI_loadData calls V_ExtendSides(1) after drawing the 320-wide WIMAP into screens[1], so WI_slamBackground's full-width copy carries filled sides (previously stale green pixels showed beside the map). Completes the user-requested scope (title, menu, HUD, and level-close/intermission screens all fill on widescreen; the finale story-text screen already tiles its flat full-width). User confirmed the Classic widescreen result is "perfect." KNOWN MINOR REMAINDER (rare, once-per-episode-end, not edge-extended): the DOOM1 boss screen (BOSSBACK, f_finale.c:588) and the bunny scroll (PFUB), plus the DOOM2 cast -- fold in later if noticed; not blocking. Builds clean Linux+Windows; exe redeployed.

- ✅ [DOOM-0152] **Show the plain TITLEPIC behind the menu so menu text is readable.**
  Reported 2026-07-01 with a screenshot: the main menu drawn over the attract-loop CREDIT page was red-on-red and illegible. Fix (d_main.c): D_PageDrawer draws TITLEPIC whenever menuactive (instead of the current attract page), and D_PageTicker freezes the attract cycle while the menu is up so it can't slide to CREDIT/HELP or a demo behind the menu. Closing the menu resumes the normal attract loop. Builds clean Linux+Windows; exe redeployed. Note: on a true-widescreen display TITLEPIC is still the 320-wide art centred with black side strips (that side-fill is DOOM-0151).
  **Layman:** Opening the menu over the scrolling credits screen put red menu text on top of red credits text — unreadable. Now the menu always shows the plain DOOM title behind it.
  Kind: fix.
  Source: user-request-2026-07-01.

- ✅ [DOOM-0153] **Mouse scroll wheel cycles weapons.**
  User request 2026-07-01. Handle SDL_MOUSEWHEEL in i_video.c and translate wheel up/down into a next/previous-weapon change (skip weapons the player doesn't own / has no ammo for, and honor the shotgun/SSG + chainsaw/fist slot-sharing like the number keys do). Feed it through the ticcmd BT_CHANGE / weapon-select bits the number-key path already uses (g_game.c G_BuildTiccmd / the weapon_keys handling), so demos and netplay stay consistent. Renderer-independent (input layer). Verify: wheel up/down cycles owned weapons in-game.
  **Layman:** Let the mouse wheel switch weapons (scroll up/down for next/previous), like modern shooters.
  Kind: feature.
  Source: user-request-2026-07-01.
  Implemented 2026-07-01. Weapon cycling on both mouse wheel and gamepad D-pad, unified through a single g_game.c accumulator `weaponcycle` consumed in G_BuildTiccmd: it steps to the next/previous OWNED number-key slot (0-6) from the current weapon and emits the normal BT_CHANGE weapon bits, so it behaves exactly like tapping a number key (chainsaw shares the fist slot, super shotgun the shotgun slot) and is demo/net safe. One step consumed per tic (rate-limited). Input: new ev_mousewheel event (d_event.h) posted from SDL_MOUSEWHEEL (i_video.c, respects wheel.direction FLIPPED); G_Responder accrues it only in GS_LEVEL & !menuactive. GAMEPAD REMAP (user request, same session): the D-pad is now a weapon/flashlight pad IN GAMEPLAY -- LEFT/RIGHT = prev/next weapon, UP = flashlight (moved off L1); L1 is a RUN button again. The D-pad still navigates menus (its turn/forward contribution is gated behind menuactive; sticks drive movement in play). Flashlight/weapon D-pad edges gated on gamestate==GS_LEVEL && !menuactive. Files: d_event.h, i_video.c (I_PollGamepad + SDL_MOUSEWHEEL), g_game.c (weaponcycle, G_WeaponSlot/G_SlotOwned/G_CycleWeaponSlot, G_BuildTiccmd, G_Responder). Builds clean Linux+Windows; headless level-run shows no crash; exe redeployed. v1 cycles owned weapons (matches number-key semantics); skipping empty guns is a possible later refinement. Memory [[flashlight-input-mapping]] updated.

- ✅ [DOOM-0154] **Mouse turns only; vertical mouse motion no longer walks the player.**
  User request 2026-07-01. Removed `forward += mousey;` in g_game.c G_BuildTiccmd so the mouse Y axis no longer drives forward/back. Mouse X still turns (or strafes while the strafe modifier is held); mousex/mousey are still zeroed each tic. Keyboard/WASD handle movement.
  **Layman:** The mouse now only turns left/right. Pushing the mouse forward/back no longer moves the player — the keyboard (arrows / WASD) handles movement.
  Kind: fix.
  Source: user-request-2026-07-01.

- 📋 [DOOM-0155] **Redo the Options menu: uniform text size, no HUD overlap (or tinted backing).**
  User request 2026-07-01. The Options menu (and its Renderer sub-menu) mixes big menu-art headings (M_OPTTTL etc.) with small M_WriteText rows (Renderer/FPS/Widescreen/Fill Screen/Brightness), so text sizes are inconsistent; taller menus also run down over the status bar (block 10 HUD-always-on, DOOM-0148) and become illegible red-on-HUD. Fix: normalise to one text style/size and lay the menu out so it doesn't collide with the HUD; if overlap is unavoidable at 320x200, draw a semi-transparent darkened panel behind the menu items (a translucency blit into screens[0], or a per-row dark box) so the text reads clearly over the HUD/world. Applies to Classic (software) UI drawing (m_menu.c / v_video.c); consider a reusable V_ dim-rect helper. Verify: every menu row same size; no illegible overlap with the status bar.
  **Layman:** The Options menu has mismatched text sizes and some entries sit over the HUD, which is hard to read. Give it consistent text and keep it clear of the HUD; where overlap is unavoidable, put a slightly darkened see-through panel behind the menu text so it stays legible.
  Kind: ux.
  Source: user-request-2026-07-01.

- ✅ [DOOM-0156] **Restore per-sound pitch variation lost in the SDL_mixer chunk rewrite.**
  User request 2026-07-01. The DOOM-0047 chunk rewrite plays each effect via Mix_PlayChannel with no pitch shift (the old software mixer used steptable[pitch] and S_StartSoundAtVolume randomises pitch via M_Random for most sounds). Options: (a) build a few pre-shifted Mix_Chunk variants per sound and pick by pitch bucket (Chocolate Doom's snd_pitchshift approach; memory cost), or (b) resample on the fly. Low priority / cosmetic -- do after the base effects volume/balance is confirmed good. Cross-ref DOOM-0047.
  **Layman:** Classic DOOM slightly randomises the pitch of repeated sound effects so they don't sound identical every time. The new SDL_mixer effects path plays them at a fixed pitch; add the subtle variation back.
  Kind: enhancement.
  Source: user-request-2026-07-01.
  Implemented 2026-07-01 alongside the dead-code cleanup. i_sound.c now stores each sound's raw 8-bit lump (I_CacheSfx keeps it cached) and builds S16 chunks LAZILY per pitch bucket (I_BuildPitched): bucket = DOOM pitch>>4 (16 buckets), representative pitch = bucket*16, and the 8-bit source is resampled as if at rate*pitch/128 to shift playback pitch. I_StartSound picks the bucket from the (randomised) pitch and plays that cached chunk, so repeated sounds vary subtly again. Bounded memory (<=16 variants per played sound, built on demand). ALSO did the promised cleanup: removed the dead software mixer (getsfx, addsfx, I_MixSoundInto, I_SFXPostMix, I_SDLAudioCallback and the mixbuffer/channels/steptable/vol_lookup globals) -- i_sound.c is ~370 lines lighter. Builds clean Linux+Windows (zero warnings); headless: device opens, sounds cache, a level runs with no crash. exe redeployed. Cross-ref DOOM-0047.

- ✅ [DOOM-0157] **Armour pickups' green glowing eyes emit a faint green glow in the path tracer.**
  The armour pickups (green/blue armor — the sprites with a face/helmet whose
  eyes glow green) are sprite Things but are NOT flagged RB_MESH_EMISSIVE, so
  BuildDynamicEmitters (r_vulkan.cpp:3817, DOOM-0084) skips them and they never
  enter the NEE emitter set -> the glowing eyes cast no light in Ultra. Make the
  armour pickups emissive so their bright green eye texels pool a faint green
  light onto their surroundings, per the RTX-aesthetic north star
  [[rt-aesthetic-north-star]]. Two pieces: (1) flag the armour-pickup sprites
  RB_MESH_EMISSIVE where the sprite->mesh emissive flag is set (r_mesh.c ~1071);
  (2) the eyes are only a few bright texels, so the area-averaged Le from
  ComputeMaterialEmissive will likely fall below kEmitterMinLum — this probably
  needs a per-sprite Le or a lowered/faint threshold rather than the pure
  auto-derive (same data-dependent tuning as DOOM-0082/0083). Shares the
  DOOM-0084 sprite-emitter mechanism. OPEN QUESTION for user: green armor only,
  or both green + blue armor (and any other glowing-eye pickups)? Needs on-HW
  verify on the RX 6600.
  **Layman:** The armour pickups have little green glowing eyes; those should cast a soft green glow when you are near them, instead of looking flat — they do not glow right now.
  Kind: feature.
  Source: user-request-2026-07-01.
  User clarified 2026-07-04 (post DOOM-0082 peak-gate build): the armour/skull pickups still don't glow. The PRIMARY want is VISIBILITY -- "if the environment is very dark, you should be able to SEE the skulls because of the glowing green eyes" -- i.e. the pickup's own eyes should self-illuminate (be bright in a dark room), separate from casting room light. Precise diagnosis (code-traced this session): (1) the self-emission mechanism EXISTS -- pathtrace.comp:468 addEmis = isSprite ? (FLAG_EMISSIVE set) : true, so an emissive-tagged sprite shows its bright texels on the primary ray. (2) the armour BONUS (SPR_BON2) + key skulls ARE tagged (sprite_glows, r_mesh.c:1016-1025, RB_MESH_EMISSIVE at 1119). BUT the big green/blue armour (SPR_ARM1/SPR_ARM2) are NOT in sprite_glows -> if that's the sprite, it needs adding. (3) ROOT for the tagged ones: ComputeMaterialEmissive gives them Le=0 -- the DOOM-0082 peak-region gate requires a near-fullbright region of >= kEmitterMinBrightTexels(8) texels (or kEmitterMinBrightFrac), and a pickup's glowing eyes are only a few texels -> excluded -> Le 0 -> nothing to self-illuminate. FIX DIRECTION (needs on-HW visual iteration -- can't verify headless): give allow-listed glowing-collectible sprites a GUARANTEED faint self-emission Le even when the generic peak-gate would zero them (e.g. a per-glow-sprite lowered bright-texel floor, or derive Le from the brightest texels regardless of count for sprite_glows members), so the eyes read as glowing in the dark. Note the sphere (SPR_SOUL/MEGA) works because it's large + bright -> passes the gate; the problem is specifically SMALL bright regions. Distinct from DOOM-0082 (switches, deferred) only in surface class. Awaiting user go-ahead to take a swing they can test.
  Fix implemented 2026-07-04 (awaiting on-HW play-test — can't verify RT self-emission headless). Root cause (confirmed by tracing pt_common.glsl:213 shadeSurface: self-emission = matEmis[id] * emissiveMask(albedo)): the glow-collectible sprite lumps derive material Le=0 because their few bright eye texels fall below the DOOM-0082 near-fullbright peak-region gate (kEmitterMinBrightTexels=8), so matEmis[id]=0 zeroes the self-glow even though the eye texels pass emissiveMask. Fix (3 files, surgical): (1) emissive_derive.h derive_material_le gains an allowFaint param — when the strict peak gate fails but the tile still holds a genuine bright speck, it emits a FAINT Le from those bright texels instead of 0 (the same bright-texel-averaged-over-tile formula, naturally dim for a few-texel glow); (2) r_mesh.c RB_SpriteLumpGlows() maps each sprite ATLAS LUMP back to its spritenum and reports sprite_glows membership (cached bitmap), bridging the spritenum-keyed allow-list to the lump-keyed material array; (3) r_vulkan.cpp ComputeMaterialEmissive passes allowFaint=true only for those sprite lumps — walls/flats and non-collectible sprites keep the strict gate, and the already-working sphere (SOUL/MEGA) is untouched because it still passes the strict gate first (faint is only a fallback). Test: tests/emissive_derive_test.cpp §5 proves a 4-green-texel skull tile derives Le=0 under the strict gate but a faint green Le (0.083) with allowFaint, and a fully-dark tile stays 0 even with allowFaint. Build + all tests green. NEXT: user play-tests in Ultra RT in a dark room — the skull eyes / armour-bonus gleam should now self-illuminate; tune kEmissiveScale / kBrightLum if too faint or too bright. Only skull keys (BSKU/RSKU/YSKU) + armour bonus (BON2) are in scope (already in sprite_glows); big armour vests (ARM1/ARM2) intentionally NOT added — user described glowing EYES, which vests lack.
  Resolved (2026-07-04): user play-tested the faint-self-glow fix (commit 01f0e6a) in Ultra RT and confirmed the glowing-collectible pickups now read fine ("they seem fine"). Signed off; user will re-flag if a specific case looks wrong later. No further tuning needed at this brightness (kEmissiveScale=40 / kBrightLum=0.5).

- 💭 [DOOM-0159] **Ultra mutes DOOM's vibrant art colours (door emblems, esp. yellow) vs Classic's flat display.**
  User A/B screenshots 2026-07-01 (the "Z" emblem door, Gimp-highlighted): Classic
  shows vibrant teal rings + a punchy yellow Z; Ultra renders it muted (dim teal,
  olive yellow). ASSESSMENT (code-traced): this is EXPECTED PBR behaviour, not a
  broken bug. Classic paints the texture's palette colours directly, dimmed only by
  sector lightlevel (hue preserved) -> art stays vibrant. Ultra lights the door as a
  real surface: final = albedo * illum (svgf_composite.comp:88). The room is dim and
  its bounced light is slightly green, so the yellow albedo is both dimmed AND hue-
  shifted toward olive -> muted. Physically correct; the cost is DOOM's punchy flat-
  lit art vibrancy, which cuts against [[rt-aesthetic-north-star]]. User is OK to
  leave it if correct -> logged as an OPTIONAL aesthetic tune, NOT a defect. Two
  levers if we later choose to restore vibrancy: (1) desaturate the GI/indirect
  colour-bleed a touch so coloured room light stops shifting hues (a mild scene-wide
  green tint is real but secondary — do NOT re-open the earlier "walls are area
  lights / whole-scene cast" theory, that was my over-read and is retracted);
  (2) give emblem/switch/light textures a mild self-emissive lift so near-fullbright
  DOOM art reads vibrant under low room light (ties into the emissive-derivation
  tuning shared with DOOM-0082/0083/0157). Decide priority with user.
  **Layman:** In Ultra the door emblem looks more muted/olive than Classic's bright yellow. That is because Ultra lights the door with the room's actual (dim, slightly green) light, while Classic just shows the art's colours directly. It is technically correct, but it loses DOOM's punchy look — optional to tune back toward vibrant if wanted.
  Kind: enhancement.
  Source: user-request-2026-07-01 (Classic vs Ultra door screenshots).

- ✅ [DOOM-0160] **Answer the Quit (and other yes/no) confirmation prompts with the gamepad.**
  User request 2026-07-02: opening the menu and choosing Quit Game works on the controller, but the follow-up "press Y to quit" prompt could only be answered on the keyboard. Root cause: M_Responder's messageToPrint input filter only passes ' '/'n'/'y'/ESCAPE to the message routine, and the gamepad's select button (A/Cross) arrives as KEY_ENTER -- which was dropped, so no controller button could say "yes". Fix (m_menu.c): in the messageToPrint branch, translate KEY_ENTER (the gamepad A/Cross select press, also the keyboard Enter) into 'y' for input-needing prompts, so the same button that selected "Quit Game" now confirms it. Contained to active yes/no prompts (Quit, End Game, quicksave/quickload overwrite); Escape/B still cancels. Chose the existing select button over binding a new one so there is nothing new to learn and it needs no i_video.c change. Builds clean. Verify: with a controller, Menu -> Quit Game -> press A/Cross quits; Escape/B backs out.
  **Layman:** You can now confirm "Quit Game" with the controller instead of having to reach for the keyboard's Y key — the same button you use to pick the menu item confirms the prompt.
  Kind: enhancement.
  Source: user-request-2026-07-02.
  Verified 2026-07-02: user tested in-game with their controller -- "Fantastic, it works." The ✕/Cross (SDL BUTTON_A) face button now confirms the Quit prompt; Circle/Escape backs out.

- ✅ [DOOM-0161] **Name the gamepad confirm button in the Quit prompt, per controller family.**
  Follow-up to DOOM-0160. With a gamepad connected, the Quit confirmation now adds a second line "(or the &lt;BUTTON&gt; button)" beneath the classic "(press y to quit)", naming the face button that actually confirms. The label tracks the controller family rather than a hardcoded name: SDL already classifies the pad (SDL_GameControllerGetType), so I_ControllerConfirmLabel (i_video.c) maps it -- PlayStation (PS3/4/5) -> "CROSS", Nintendo Switch Pro -> "B", Xbox and everything else -> "A" (the sane default, since generic PC pads are overwhelmingly XInput/Xbox-style with A on the bottom). No per-device database and no online layout scraping needed -- SDL's own classification does it. The engine's confirm is always SDL BUTTON_A (the bottom face button), so the label always matches the button that works. Returns NULL when no pad is connected, so keyboard-only players keep the unchanged single-line prompt. Label is uppercase to suit DOOM's uppercase-only menu font; the PlayStation glyph can't be drawn so the official name "CROSS" is spelled out. m_menu.c M_QuitDOOM builds the string; declared in i_video.h. Builds clean (no warnings). Verify: with an Xbox pad the prompt reads "...(or the A button)", with a PlayStation pad "...(or the CROSS button)", and with no pad the classic prompt.
  **Layman:** The "quit?" box now tells controller players which button to press to confirm, and shows the right name for their pad — "A" on an Xbox pad, "CROSS" on a PlayStation pad.
  Kind: enhancement.
  Source: user-request-2026-07-02.
  Updated 2026-07-02 (user request): PlayStation label changed from "CROSS" to "X" -- only Sony calls it Cross; gamers call it X. I_ControllerConfirmLabel now returns "X" for PS3/4/5.
  Fixed 2026-07-02: adding the second prompt line exposed a latent bug in M_Drawer's message line-splitter -- "(press y to quit)" vanished, leaving only "(or the X button)" (user screenshot). Root cause: the loop advances `start` past the '\n' it splits on, then tested `i == strlen(messageString+start)` (the ALREADY-advanced remainder) to detect the final line; when a real line's post-advance remainder length equalled the split index it wrongly ran the final-line branch, overwriting+skipping the line. Both prompt lines are 17 chars with the '\n' at index 17, so it triggered. Replaced the stale length compare with an explicit `split` boolean. Verified with a standalone simulation of the exact loop (old drops the Y line, fixed keeps both). This also hardens every other multi-line message against the same equal-length coincidence.

- ✅ [DOOM-0162] **Draw the DOOM-0141 sky occluder in the raster view too, so distant geometry stops floating there.**
  The DOOM-0141 sky backdrop mesh (emit_sky_wall/emit_sky_cap -> levelMesh->sky -> g.skyMeshBuf) is RT-only: it rides the sky BLAS and occludes distant geometry for the tracer, but the raster path never draws it. Raster instead draws a depth-OFF full-screen sky quad behind everything (r_vulkan.cpp:5321), which cannot occlude, so distant buildings drawn later hang in front of it (the DOOM-0142 "floating buildings" seen only in the raster ~ toggle). Plan: (1) add VK_BUFFER_USAGE_VERTEX_BUFFER_BIT to skyMeshBuf's usage (r_vulkan.cpp:4478) so the existing occluder buffer is rasterisable at zero extra memory; (2) draw g.skyMeshBuf depth-ON in the raster pass -- the verts carry RB_MESH_SKY so mesh.frag's FLAG_SKY branch paints the panorama, depth-test occludes the buildings and is occluded by nearer walls (watch backface culling -- may need a no-cull sky draw since sky tris can face away); (3) port the DOOM-0143 below-horizon fog fade from pathtrace.comp skyPanorama into mesh.frag's FLAG_SKY path so the raster window shows the same clean haze instead of the DOOM-0076 wrap seam. Iterate visually like DOOM-0143.
  **Layman:** Make the raster 3D view hide far-off buildings behind the sky the same way the ray-traced view already does, so they stop looking like they float.
  Kind: feature.
  Source: user-request-2026-07-02.
  Resolved (2026-07-03, user GPU-verified): the raster view now occludes distant geometry with sky, matching the ray-traced view. Implemented in three parts. (1) VK_BUFFER_USAGE_VERTEX_BUFFER_BIT added to the DOOM-0141 sky occluder buffer (g.skyMeshBuf) so it is rasterisable; drawn depth-ON with the world pipeline (cull-none) after the world, before sprites (r_vulkan.cpp ~5351). (2) THE load-bearing fix: the occluder verts were flagged RB_MESH_SKY, which the vertex shader treats as already-in-NDC (skip the MVP) -- so the world-space occluder was flung off-screen and occluded nothing (why the first candidate failed the user's re-test). Added a dedicated RB_MESH_SKYDOME (0x40) flag for the world-space sky dome: emit_sky_wall/emit_sky_cap now emit it, mesh.vert MVP-projects it (RT unaffected -- the trace ignores the flag, keying sky off the TLAS instance index), and mesh.frag paints FLAG_SKY|FLAG_SKYDOME as the panorama. vScreenUV is now derived from clip-space xy and marked `noperspective` in both shaders, so the projected dome samples the exact same sky the flat backdrop quad would at each pixel. (3) The DOOM-0143 below-horizon fog fade was ported from pathtrace.comp into mesh.frag's sky path, so the raster window shows the same clean haze. Scoped to the raster else-branch (r_vulkan.cpp:5261), so the RT view is untouched. Found during testing and roadmapped separately: DOOM-0163 (RT masked mid-walls opaque), DOOM-0164 (sky world-lock / mountain drift).

- ✅ [DOOM-0163] **Alpha-test two-sided masked mid-walls (grates/fences) in the ray-traced view so you can see through them.**
  Found 2026-07-03 (user testing, DOOM 1, Solid/Ultra `~` toggle). A two-sided masked mid-wall (the diamond grate/fence) renders correctly see-through in the RASTER view (mesh.frag alpha-tests FLAG_MASKED on palette index 0, DOOM-0065) but is fully OPAQUE in the ray-traced view -- rays stop at the grate quad instead of passing through its index-0 gaps. Root cause: the world BLAS treats masked mid-wall triangles as opaque geometry (no per-hit alpha test), so primary + shadow + GI rays all block on them. Fix direction: give masked (and check: are world-sprite/thing hits already handled?) surfaces a VK any-hit / opacity path in the path tracer that samples the material's palette index and ignores the hit when index 0 (mirrors the raster FLAG_MASKED discard). Options: (a) any-hit shader that reads the bindless material + discards index 0; (b) Vulkan opacity micromaps if the texel mask is static per material. Must apply to primary rays (see-through), shadow rays (grate casts a patterned shadow, not a solid one), and ideally the GI bake. Scope: the RT geometry material seam (r_vulkan.cpp BLAS build + pathtrace.comp hit handling). Distinct from DOOM-0065 which fixed the raster path only.
  **Layman:** In the ray-traced view, see-through grates and fences look like solid walls; make them see-through like they are in the classic and raster views.
  Kind: fix.
  Source: user-testing-2026-07-03.
  Progress (2026-07-03): implemented. Root fix = make the world BLAS NON-opaque (r_vulkan.cpp world BLAS build @1320 + RecordRefitAS @1704, geom.flags OPAQUE_BIT -> 0) so the primary ray yields world-triangle candidates, then alpha-test them in pathtrace.comp's existing candidate loop via a new worldCandidateOpaque(): an ordinary wall/flat commits on the first candidate (one FLAG check), a two-sided masked mid-wall (FLAG_MASKED, added to pt_common.glsl as 0x2) commits only where its paletted texel != index 0 -- mirroring the raster mesh.frag FLAG_MASKED discard. Masked walls index their material by texnum direct, matching the mode-3/4/6 decode. Verified via enumerating every rayQueryInitializeEXT: ONLY the primary ray uses gl_RayFlagsNoneEXT; muzzle/flashlight/NEE-occluded/GI-bake all force gl_RayFlagsOpaqueEXT, so they are immune to the BLAS flag and keep their fast opaque path -- meaning this touches only the single coherent primary ray (no perf regression on the shadow/secondary fleet the DOOM-0009 perf research flagged) and grates still cast SOLID shadows for now (patterned-shadow follow-up parallels the deferred DOOM-0108 sprite-shadow posture). AS setup is byte-identical to the proven-working non-opaque sprite BLAS (flags=0 + FACING_CULL_DISABLE instance). Builds clean (r_vulkan.o + both compute .spv regenerated, no warnings). AWAITING user visual play-test: in Ultra with the RT view (rt_view=6, press ~), look at the diamond grate/fence -- should be see-through to the geometry behind it, like Classic/Solid; opaque walls unchanged.
  Resolved (2026-07-12, user-confirmed): two-sided masked mid-walls (grates/fences) are now see-through in the ray-traced view, matching Classic/Solid.

- 📋 [DOOM-0164] **World-lock the sky so distant mountains stop drifting when the player turns (low priority, later).**
  Found 2026-07-03 (user testing, DOOM 1 E1M1 outdoor, Solid/Ultra). The sky panorama drifts relative to the world as the player yaws -- distant "mountains" slide instead of holding a fixed world bearing. Root cause: the sky is a SCREEN-space lookup keyed on view yaw (mesh.frag / pathtrace.comp skyPanorama map screen-x -> ray-yaw offset via atan, panned by pc.yaw), so it is pinned to the camera, not to world directions. Classic DOOM has the same 1:1 yaw-locked sky (it never truly world-locks either), but with the DOOM-0141/0162 world-space sky dome now carrying the panorama the mismatch is more noticeable: the dome geometry is world-locked while the texture painted on it pans with yaw, so the sky appears to slide across the fixed dome. Fix direction (bigger change, deferred by user 2026-07-03): map the sky by WORLD azimuth/elevation of the view ray (or the dome surface direction) instead of screen x/yaw, so a given sky texel always sits over the same world compass bearing -- mountains then hold still as you turn. Decide raster (mesh.frag) + RT (pathtrace.comp) together so both tiers match. LOW PRIORITY: user is fine with the current behaviour; investigate later.
  **Layman:** When you turn left or right outdoors, the far mountains slide a little instead of sitting still like real scenery; pin the sky to fixed compass directions so it stays put. Minor -- parked for later.
  Kind: fix.
  Source: user-testing-2026-07-03.

- 📋 [DOOM-0167] **Reconcile DOOM-0008 spec drift against the shipped renderer seam + DOOM-0009 §2 settings model.**
  Surfaced by the 2026-07-04 cold-eyes loop while reconciling DOOM-0057/0081; deferred out of that bundle to keep those edits surgical. Pre-existing DOOM-0008 (docs/specs/DOOM-0008-3d-renderer.md) drift, all doc-side only (no code change): (1) HIGH — the C/C++ seam is described as a single `extern "C" const renderer_backend_t* R_VulkanBackend(void)` accessor (~lines 129, 399, 403), but the shipped seam is a `renderer_backend_t backends[RB_NUMMODES]` table populated with the `RB_Vulkan_*` function set (`RB_Vulkan_Available/Init/SetResolution/RenderView/SetOverlay/Present/Shutdown`) plus the `RB_VulkanProbe` probe (r_backend.c:69,221,297; r_backend.h:57) — no `R_VulkanBackend` symbol exists. (2) MEDIUM — the settings model (Renderer:3D single option; RB_RASTER3D/RB_RT3D as auto-detected HW tiers, ~lines 23/70/172-187) is superseded by DOOM-0009 §2's 2026-06-27 tier×RT model (Classic/Solid/Ultra tier + orthogonal RT On/Off); DOOM-0008 carries no forward-pointer, so it reads as the current contract. Add a 'settings model superseded by DOOM-0009 §2' banner. (3) MEDIUM — the single config key `renderer`↔`rendermode` persistence (~lines 186-187) is stale: DOOM-0009 §2 records tier + RT toggle + upscaler each persist as separate m_misc.c defaults. (4) LOW — `R_SetupFrame` cited r_main.c:830-863; actual definition is r_main.c:867 (symbol correct, line-range drift). (5) LOW/open — verify whether `RB_RenderPlayerView`/`RB_Present` (~lines 364,366) are real generic-dispatcher names or should be the `RB_Vulkan_*` variants.
  **Layman:** An older design doc (DOOM-0008) describes some renderer plumbing and menu behaviour that has since changed; bring its wording back in line with the code and the newer path-tracer doc.
  Kind: doc-fix.
  Source: cold-eyes-2026-07-04 (DOOM-0008/0009 review, pre-existing drift outside DOOM-0057/0081 scope).

- 📋 [DOOM-0168] **DOOM-0009 path-tracer spec readability pass (collapse §2 tier×RT restatement, add TOC, split §4.4 run-ons).**
  Deferred structural/readability nits in docs/specs/DOOM-0009-path-tracer.md, surfaced (again) by the 2026-07-04 cold-eyes loop and held out of DOOM-0081 as too large for a surgical doc-fix: (1) §2's tier×RT 'ray tracing is orthogonal to the tier' contract is restated 3-4x across the intro bullets, the 'orthogonal' paragraph, the 'Scope' paragraph and the 'In Stage 2 specifically' paragraph (~lines 40-116) — collapse to one canonical statement without losing the menu-rework / INV-9 / scope nuances (this is the un-done DOOM-0081 item 5). (2) No TOC/Contents anchor list on a ~470-line doc (sibling DOOM-0008 has one) — add one. (3) §4.4's integrator paragraph is one dense >40-word nested run-on — split into a bulleted pipeline. All clarity/structure only; no contract change. Do these as one pass, then a cold-eyes confirm.
  **Layman:** Tidy the path-tracer design doc so it is quicker to read: remove a few paragraphs that repeat the same point, add a table of contents, and break up a couple of very long sentences. No change to what it specifies.
  Kind: doc-fix.
  Source: cold-eyes-2026-07-04 (DOOM-0009 review, deferred structural nits incl. DOOM-0081 item 5).

- ✅ [DOOM-0169] **Render tier drives the ray-tracing default so Solid is the fast raster original view.**
  Root cause (Explore trace 2026-07-04): on an RT-capable GPU, selecting the Solid tier (RB_RASTER3D) ran the FULL mode-6 path-trace megakernel + SVGF denoise + TAAU — byte-for-byte identical GPU cost to Ultra — because the tier menu (rendermode) never touched rb_rtdebug (default 6); only the `~` key did (r_vulkan.cpp:5111 rtActive gate; m_menu.c:1409 / r_backend.c:342 tier switch). The cheap raster mesh.vert/frag path (the classic look, far above 60 FPS on the RX 6600) only appeared at rb_rtdebug==0. This is exactly the OPEN QUESTION DOOM-0135 (shipped) flagged for user resolution — user confirmed 2026-07-04: art set bound to tier, RT an on/off toggle inside each tier (matches DOOM-0009 §2). Fix: RB_ApplyTierRt() in r_backend.c drives rb_rtdebug from the tier on RB_Init + RB_SetMode (RB_RASTER3D->0 raster, RB_RT3D->6 traced), skipped while Debug Views (rb_rtdebug_menu) is on so the diagnostic cycle keeps control; `~` still toggles RT within a tier. Per-frame mutual exclusion already holds (megakernel/denoise/TAAU/TLAS/BLAS all inside RecordRtTrace, only run when rtActive; raster pass skipped when rtActive). FOLLOW-UP (this item or a split): the synchronous level-load GI bake (RunGiBake) still runs in raster mode — wasted GPU + a likely level-load hitch; gate it to raster-skip with a lazy re-bake when RT is toggled on in Solid (also serves the frame-time-consistency goal). Then verify even frame times on the raster Solid path.
  **Layman:** Picking the "Solid" graphics mode now gives the fast, classic-looking view instead of secretly running the heavy ray-traced renderer — so it's smooth. "Ultra" is the ray-traced one. You can still flip ray-tracing on/off within a mode with the ~ key.
  Kind: fix.
  Source: user-request-2026-07-04 (Solid not buttery-smooth on RX 6600).
  Resolved (2026-07-12, user-confirmed): render tier drives the ray-tracing default so Solid is the fast raster original view. User follow-up preference: persist the last render-mode choice across launches (captured as DOOM-0175).

- 🚧 [DOOM-0170] **Raster "performance mode" — modern lighting/shadows/reflections when ray tracing is OFF (both Solid and Ultra).**
  User model (PS5 quality/performance modes): RT-on = quality mode (path tracer, unchanged); RT-off (rb_rtdebug==0) = performance mode — the SAME lighting ideas done the cheap raster way, and it must beat Classic. Tier stays "which art" (Solid=classic, Ultra=HD); RT on/off is the quality/performance toggle INSIDE each tier. Mutual-exclusion holds (RT-off never runs the tracer; RT-on never runs this stack).
  Spec written: docs/specs/DOOM-0170-raster-performance-mode.md. /cold-eyes 4-loop polish-converged 2026-07-09 (0 CRITICAL/HIGH; also added a reciprocal supersession pointer to DOOM-0009 §2). Implementation-ready, layered build: L1 lighting (point lights + baked-probe bounce) first, then L2 shadows (SSAO + key-light shadow map + blob), then L3 scoped SSR — each rebuilt + play-tested on the RX 6600 before the next.
  Progress (2026-07-09): L1 lighting landed. L1a baked-probe indirect bounce (mesh.frag reads probes via gl_PrimitiveID→triSs→subsector, buffer_reference-via-push; geometryShader feature enabled) shipped f748452. L1b dynamic point lights this commit: per-subsector nearest-N (N=16) cull from the NEE emitter list runs on the CPU each raster frame (BuildRasterPointLights, reusing probe centroids + DOOM-0119 REJECT matrix), uploaded to a host-visible SSBO mesh.frag loops per fragment (Le · 1/(1+(d/RADIUS)²) · N·L, no cast shadows). Also wired BuildDynamicEmitters into the raster frame branch so emissive sprites (torches/lamps/barrels) reach the emitter list in Solid (they only ran on traced frames before). Push block now 124 B (probes+triSs+lights addrs + probeCount). Spec §4.1/§6 reconciled to shipped reality (Le-as-intensity; raw luminance·area weight is unrecoverable post-merge). Build + make test green. NEXT: user play-test on RX 6600 (torch pools light? FPS ≥ 60? tune RADIUS/STRENGTH), then L2 shadows.
  L1 SIGNED OFF (2026-07-09, user play-test accepted). Final L1 tuning after 3 rounds: point lights blew to white (raw NEE Le is radiometric — sprite maxLe 17.43 = ~1.45×12 boost; big surfaces enter as many triangles summed 16-wide), fixed by using Le as COLOUR only (normalize by max channel) + Reinhard roll-off ceiling. Then too subtle → split into two dials: POINT_LIGHT_GAIN (3.0, single-light pop) + POINT_LIGHT_STRENGTH (0.7, cluster ceiling). User's own insight: dim the flat sector light so additive lights have headroom → BASE_SECTOR_DIM (0.75, §9 Q1 rebalance). Final look: atmospheric, lit, no white-out, ~60fps@4K. Values remain tunable (§9 Q1) once L2/L3 land. Commits 175dfba (impl) + d9ff5b8 (bound) + fe0d5bd (dials+dim). NEXT: L2 shadows.
  L2a step 1 SHIPPED + self-verified (55f7e52): off-screen scene target (scenePass) + composite pass. World now renders to g.sceneImage → composite fullscreen samples to swapchain → HUD on top. 8-bit passthrough (identical look), zero validation errors, 61fps@4K. NEW SELF-TEST CAPABILITY (user-suggested): build → launch E1M1 windowed in Solid (renderer=2 in ~/.doomrc) via `linuxxdoom -iwad wads/doom.wad -warp 1 1 -windowed` → spectacle screenshot → read image + grep /tmp/doomlaunch.log for [validation] errors. Removes the blind-refactor risk (caught + fixed a renderpass dependencyCount-compatibility bug in one cycle). TWO PERF LEVERS UNLOCKED by the off-screen target (both serve the buttery-smooth goal): (1) raster currently renders at FULL 4K — render_scale (config=50) only ever affected the RT path (RecordRtTrace), never raster; now I can size g.sceneImage to render_scale and upscale in the composite = ~4× raster perf at 50%. (2) render only the view area above the HUD (~16% fewer px, user idea). NEXT: L2a step 2 (size scene to render_scale + HDR float + tonemap via existing pbr_neutral_tonemap.glsl), then L2b SSAO / L2c flashlight shadow map / L2d blob, each self-verified before handing over.
  L2a step 2 SHIPPED (26e4b0c, self-verified E1M1: 0 validation errors, 78 FPS @ 50% scale). (1) Raster now honours render_scale: world draws into the [0,uvScale] corner of the full-size scene target (scaled viewport, clamp 25..100), composite upscales it via a vec2 push-constant + a dedicated linear+clamp sampler; read per-frame like the tracer (menu takes effect, no swapchain rebuild); 100% = uvScale(1,1) = byte-identical. Whole-target clear avoids a linear edge-tap seam. (2) Present mode prefers MAILBOX (fall back FIFO): FPS no longer blocked at vsync — runs flat-out, lowest latency, tear-free (60 FPS is a FLOOR not a ceiling per user; 78 observed). FINDING: 4K->1080p only moved 61->78 FPS, so Solid is CPU-bound (per-frame emitter merge + light cull + sprite rebuild from L1b), NOT pixel-bound — dirty-flagging that CPU work is the real throughput lever (L1b sprites rarely change). NEXT: L2a step 3 HDR float scene target + tonemap (pbr_neutral_tonemap.glsl) → L2b SSAO → L2c flashlight shadow map → L2d blob. Also logged DOOM-0172 (Solid-tier art upscaling, user idea, orthogonal).
  Progress (2026-07-10): L2a step 3 shipped (4e168d1) — HDR float scene canvas (R16G16B16A16_SFLOAT) + PBR-Neutral tone-map in composite.frag, so raster highlights (muzzle flash, explosions, stacked lights) roll off softly instead of clipping to white. Same tone operator as the RT denoiser (svgf_composite) → Solid/Ultra tone-matched; identity below the 0.76 knee so the DOOM palette/midtones are untouched (no dimming). World/sky/wire pipelines now build against the float scenePass; added rtWeaponPipeline (8-bit twin) for the RT weapon overlay. Self-test E1M1 Solid: 0 validation errors, not dimmed, 75/62 FPS (avg/low above 60 floor). Un-play-tested by user: soft-highlight look on live muzzle flash/explosions + confirm Ultra weapon overlay still draws. NEXT: L2b shadows (SSAO contact shadows → flashlight/key-light shadow map → blob shadows).
  Progress (2026-07-12): L2c — flashlight cast-shadow map SHIPPED (commit e1516e0, pushed). Spec §4.4 key-light shadow map, first of the L2 shadows layer. Each torch-on frame renders the world (walls/flats + monster/item billboards; psprite+sky clipped out in shadow.vert) depth-only from the flashlight viewpoint into a fixed 2048² D32 map (new shadow.vert/frag), then mesh.frag does a 3×3 PCF compare gating ONLY the additive flashlight term (base sector light untouched). Torch off = pass skipped (zero cost), parked depth image keeps the set-1 descriptor valid. lightVP reaches mesh.frag via a new set-1 descriptor (shadow sampler + UBO) since the push block is at the 124 B floor; single-frame-in-flight so the UBO is one persistently-mapped buffer. Shadow map is window/render-scale independent (built once in CreateShadowResources; needed CreateCommandsAndSync moved ahead of it so BeginOneTime has a command pool). Ultra RT unaffected (tracer does its own shadows; weapon overlay binds the parked set). Self-verified: make + make test green, E1M1 Solid 0 validation errors torch on & off, scene renders. Shadow bias/look + billboard-caster quality pending user play-test (drive into a dark area, aim torch at a pillar/monster). NEXT in shadows layer: L2d blob shadows (simple grounding oval), then L2b SSAO (needs the DIRECT/INDIRECT MRT split per §4.3, biggest of the three; also lays the G-buffer for L3 SSR).
  L2d blob shadows SHIPPED (5a79f98, pushed, self-verified E1M1 Solid: 0 validation errors, make test green). §4.5: RB_BuildBlobs emits one horizontal quad per solid/pickup Thing at floorz (skips player body, MF_MISSILE, non-solid corpses); blob.vert/blob.frag draw a soft radial dark oval faded by sector light; dedicated alpha-blend pipeline (SRC_ALPHA/1-SRC_ALPHA, depth-test on/write off) draws from g.spriteVbuf after the world, before the billboards. Single HDR target for now; moves to DIRECT when L2b adds the MRT split. Cyan debug pass confirmed correct centring/size/on-floor, sprite on top, no z-fight. Un-play-tested: strength/size feel (BLOB_STRENGTH 0.55, radius 1.35x, tunable). NEXT: L2b SSAO (contact shadows) — needs the DIRECT/INDIRECT MRT split (§4.3) + sampleable depth = biggest of the three, lays the G-buffer for L3 SSR.
  Progress (2026-07-12): L2b SSAO shipped in two steps. L2b-1 (f70d1e8) split the raster scene into two HDR targets — AMBIENT (sector light + baked bounce) and DIRECT (flashlight + point lights + sprite/sky colour) — a visible no-op that lays the MRT groundwork. L2b-2 (e2d6ffa) added the half-res SSAO pass: mesh.frag packs forward-distance linear depth into DIRECT.a (raster camera is yaw-only, so it equals clip.w), ssao.frag reconstructs view positions from it with just the fixed 90° FOV + aspect (no depth buffer / matrix / NORMAL target — normals from screen-space derivatives), 16-tap hemisphere + IGN rotation, composite blurs and applies DIRECT + AO×AMBIENT. New rb_ssao gate (persisted "ssao", default on). Deviations from spec text (fold into §3/§4.3 at L2 wrap-up): AO darkens AMBIENT = sector+bounce not bounce-alone (bounce too faint to show); normals from depth not a NORMAL target (deferred to L3 which needs the shine bit); blob darkens ambient-only via per-output alpha (independentBlend off). Self-test: 0 validation, make+test green, cranked-intensity capture confirmed occlusion in ceiling recesses / pillar-floor junctions / corners, not on flat lit surfaces or sprites. Tuning dials kSsao* (radius 64, intensity 1.6, power 1.5, bias 1.5) — play-test. NEXT: L3 scoped SSR (wet-floor reflections). Un-play-tested: L2b look/strength on live monsters; RT weapon overlay visual on Ultra (uses the new SINGLE_TARGET mesh.frag variant; reasoned INV-4-preserving).
  Progress (2026-07-12, play-test fix ec9a2ab): user play-tested L2b on RX 6600 — floor-to-wall AO and blob shadows under enemies were invisible (though the raw-AO debug view confirmed the occlusion WAS being computed on the floor). Root cause: both darkened only AMBIENT, but DOOM's emitter-heavy floors get a large DIRECT share (L1b point lights) left untouched, so total floor brightness barely moved. Fix: composite now applies AO to DIRECT too, weighted (AO_DIRECT_WEIGHT=0.6): final = direct*mix(1,ao,0.6) + ambient*ao — contact shadows read on lit floors while flashlight/lamp beams + weapon/sprites stay bright. Blobs now darken BOTH targets (opaque grounding shadow blocks all light). Self-verified E1M1: clear grounding at pillar bases / wall-floor junctions / ceiling recesses. Deliberate deviation from §4.3 ("AO never touches DIRECT") — ambient-only is invisible on DOOM content; fold into §4.3 at L2 wrap-up. Flashlight cast-shadow (L2c): user couldn't confirm (room too bright) — additive flashlight adds little in a lit room so its shadow is invisible; needs a DARK room with the torch ON aimed at a pillar/monster to verify.
  Progress (2026-07-14): L2b SSAO play-test tune (ff757af) — sprites (monsters/items) now excluded from SSAO by negating their packed depth in mesh.frag (ssao.frag skips negative samples as both receiver and occluder), killing the black halo around monsters; RADIUS 64->40, BIAS 1.5->2.0, INTENSITY 1.6->1.3, composite AO_DIRECT_WEIGHT 0.6->0.5 to fix corner-streaks + over-dark floors. User confirmed floor AO + blob shadows now read; flashlight cast-shadow (L2c) STILL not visible to user across 2 play-tests — separate investigation pending (is the shadow pass producing a visible term at all?). Also bfe2ffe: gated the RT on-screen mode/PROFILER label overlay on rb_rtdebug_menu so "DENOISED"/"PROFILER" text no longer shows in normal Ultra play (only in the Debug Views diagnostic cycle). PERF (INV-2 breach): user reports mid-50s FPS (avg/low ~50/45) at 50% render scale — below the 60 floor. Math check: the 2x HDR MRT buffers are only ~1.8 GB/s on a 224 GB/s card, so NOT bandwidth-bound; cost is ALU (mesh.frag per-fragment GI+16-light loop, SSAO taps, and the 2048^2 flashlight shadow full-scene depth re-render when torch on). NEXT: add per-pass raster GPU timing (extend the `\` rb_profile which only covers RT passes today) to cut the proven hotspot, rather than guess. Candidate safe trims: SSAO 16->8 taps, flashlight shadow 2048->1024.
  Progress (2026-07-14b): flashlight cast-shadow ROOT CAUSE found + fixed (dfdb0d7). It was co-located with the camera (leye=eye, lfwd=view dir) — geometrically the one setup that casts NO visible shadow (every shadow hides directly behind its caster). Fix: offset the torch up+right (view-space, 28/22 units, Doom 3-style) in BOTH the shadow-map viewpoint AND the mesh.frag cone (matched constants) so parallax swings shadows into view. Awaiting user play-test (dark room, torch on, aim at pillar/monster). Also extended the `\` GPU profiler to the Solid raster passes (8230035): new [raster_profile] line. FIRST RX 6600 MEASUREMENT (E1M1, 50% scale, torch on) OVERTURNS earlier guesses: per-pass GPU = shadow 0.42 / scene 5.2 / ssao 1.8 / composite 0.45 / hud 0.18 ms = ~8ms GPU total. The flashlight shadow is CHEAP (0.42ms), NOT the hog I feared. GPU hotspots: scene MRT pass (5.2ms, the per-fragment GI+16-light loop) then SSAO (1.8ms). BUT ~8ms GPU vs ~19.6ms frame (51 fps) => the frame is CPU/serialization-bound (single-frame-in-flight serializes CPU prep + GPU exec), NOT GPU-bound. So GPU cuts alone won't reach 60. NEXT: add a CPU-side timer to find the ~11ms (candidates: per-frame BuildRasterPointLights + BuildDynamicEmitters + NEE CDF rebuild), and/or 2-frames-in-flight to overlap CPU+GPU. Cheap GPU wins still available (SSAO 16->8 taps ~ -0.9ms) but secondary to the CPU gap.
  Progress (2026-07-14c): CPU-side frame profiler added (df26c48) — [cpu_profile] fenceWait/build/record/submit/present-total + [cpu_build] sprites/lights/reheight, same `\` toggle as the GPU line. PINPOINTED the ~11ms CPU gap (E1M1, 50%, torch on): present-total 18.9ms ≈ whole 19ms frame; build 11.0 = sprites 0.03 + LIGHTS 8.0 + REHEIGHT 3.0; fenceWait 7.5 (≈ 7.5ms GPU). Frame = 11ms CPU build THEN 7.5ms blocked on GPU, SERIALIZED (single-frame-in-flight). Root cause: BuildRasterPointLights O(subsectors×emitters) per-subsector nearest-N cull runs EVERY frame but is camera-independent (only depends on emitter positions = static for walls/torches); RB_UpdateMeshHeights rescans the whole mesh each frame even when no door/lift moves. Both recompute near-identical results on a static scene. FIX (next, needs play-test): dirty-flag both — rebuild point-lights only when the emitter set changes (split static-once vs dynamic-merge), skip re-height when no sector is actively moving. Two levers, both quantified: (a) cut the 11ms build (long pole + pure waste) → ~90fps ceiling; (b) 2-frames-in-flight to overlap the 11ms CPU with the 7.5ms GPU. (a) first (bigger + enables b). Sprite builds are already cheap (0.03ms). Await user go-ahead on the dirty-flag fix.
  Progress (2026-07-14): L2 perf — static point-light cull cached (commit, pushed). The CPU profiler's "lights 8ms" pole was the per-subsector nearest-N cull re-running the UNPRUNED static-emitter pass every frame (static wall/flat lights carry the no-reject sentinel -> tested vs every subsector, but never move). Split into RebuildStaticPointLightCache (static nearest-N computed once, on g.staticLightsDirty set by BuildStaticEmitterSet) + a per-frame dynamic-sprite merge on top; provably the same nearest-N of the union, moving fireball-lights still update. Merge kept in local RAM, single sequential write to the write-combined light buffer (reading back mapped GPU mem made the 1st cut 20ms — classic trap). Measured E1M1/50%/flashlight: build 11.0->6.1ms (lights 8.0->2.8), frame 18.9->~14.7ms (52->65 fps). Frame now GPU-bound (fenceWait 8.3ms). Reheight (3ms) left alone — that per-vertex scan also drives animated-texture cycling (DOOM-0066), can't blanket-skip. Next lever if more fps wanted: 2-frames-in-flight to overlap the 6.1ms CPU with the 8.3ms GPU. Awaiting user play-test (lighting look unchanged + moving lights).
  User play-test SIGN-OFF (2026-07-14): flashlight now casts shadows correctly (L2c Doom-3 offset dfdb0d7); AO (L2b), blob shadows (L2d), and the enemy black-halo fix (ff757af) all "look like they're supposed to"; no visual regression from the L2 perf light-cache split — so the 52->65fps fix is confirmed lighting-identical on hardware. L2b/L2c/L2d + the perf fix are user-confirmed. Remaining for DOOM-0170: L3 scoped SSR, then L2 wrap-up (fold §3/§4.3 spec deviations into docs/specs/DOOM-0170-raster-performance-mode.md + /cold-eyes). Optional perf lever still open: 2-frames-in-flight to overlap the 6.1ms CPU build with the 8.3ms GPU (frame is now GPU-bound).

  Today mesh.frag is only "classic DOOM lighting in 3D" (sector light x distance + flashlight cone) — no dynamic point lights, shadows, reflections, or bounce. This feature grows the raster path into a short multi-pass pipeline.

  Design forks (user-decided 2026-07-09): (1) build EVERYTHING (lights+bounce+SSAO+key-light shadow map+blob shadows+scoped SSR) — implement in verifiable layers; (2) reflections = scoped SSR on liquids/metal only (nukage/water/blood/polished), half-res, blurred sheen, else matte; (3) shadows = SSAO contact + ONE dominant-light shadow map (flashlight/outdoor sun; monsters+items render into it) + cheap blob shadow under each monster/barrel.

  Pipeline (per frame, RT-off): 1 key-light shadow map; 2 main pass (mesh.frag: albedo x sector-light + nearest ~12-16 point lights from the existing NEE emitter list + baked SH-L1 probe bounce + shadowed key light -> HDR colour + normals); 3 SSAO half-res; 4 scoped SSR half-res; 5 composite+tonemap -> swapchain, HUD on top; blob shadows.

  Reuse: the per-frame emitter list (BuildStaticEmitterSet/BuildDynamicEmitters) and the one-time GI bake (RunGiBake SH-L1 probes) already exist — raster just isn't fed them yet. Non-RT GPUs: SSAO/SSR/shadows/blobs are pure raster (no RT); probe bounce falls back to flat ambient when no bake (no crash). 60 FPS floor is hard: half-res screen passes, one shadow map, capped point lights, scales with Render Scale. Each effect individually toggleable to isolate hardware issues.

  Layered build: L1 lighting (point lights + probe bounce), L2 shadows (SSAO+shadow map+blob), L3 scoped SSR — each rebuilt + play-tested on the RX 6600 before the next. Spec: docs/specs. Needs /cold-eyes per house rule 14 before implementation.
  **Layman:** With ray tracing off, the classic view still gets modern shadows, lighting and reflections done the fast raster way — like a console's "Performance Mode" next to the ray-traced "Quality Mode".
  Kind: feature.
  Source: user-request-2026-07-09.

- ✅ [DOOM-0171] **4K/widescreen HUD spams "V_DrawPatch: bad patch (ignored)" — status-bar patches exceed the 320-wide LFB.**
  Seen in the run-doom-ants.sh console at 3840x2160: hundreds of "Patch at X,-3 exceeds LFB / V_DrawPatch: bad patch (ignored)" lines while compositing the status bar / HUD border. Pre-existing (not from DOOM-0170); the patches are ignored so it renders, but it's log spam and suggests the widescreen status-bar fill (cf. DOOM-0151 edge-extend) draws patches past the 320-wide low-res framebuffer bounds. Low priority; investigate the V_DrawPatch bounds/tiling for widescreen at high res.
  **Layman:** On a 4K widescreen display the game floods the terminal with harmless "bad patch" warnings while drawing the bottom status bar — cosmetic log noise, but it hints the status bar isn't tiled correctly across the ultra-wide screen.
  Kind: fix.
  Source: observed-in-session-2026-07-09 (RX 6600, 3840x2160 play-test).
  Resolved (2026-07-17): same shared root as DOOM-0137 — the function-level rate-limit caps the 4K/widescreen status-bar bad-patch flood globally too. Patches were already ignored (frame renders fine), so there was no visible defect; the log-spam symptom is eliminated. Commit f026e8f.

- 📋 [DOOM-0172] **Solid-tier art upscaling — smooth/enhance the low-res paletted textures & sprites like a PS1/2 emulator.**
  User ask: game art (wall/flat textures, pickups, enemy sprites) is still very low-res even as the render resolution rises; can we upscale it like PS1/2 emulators do, for the Solid renderer ONLY (Ultra gets its own hand-authored HD art). Two complementary routes, cheapest first:
  (a) Bilinear texture filtering — flip the world sampler mag/min from NEAREST to LINEAR for Solid (near-zero cost; softens the pixel blockiness — the classic "texture filtering" toggle). Should be an option since it trades the crunchy 1993 look for smoothness.
  (b) Algorithmic upscale at atlas-build time — run each paletted texture/sprite through an edge-directed upscaler (xBRZ / HQ2x / scale2x) to 2×/4× when building the material atlas. This is exactly the PS1/2-emulator "upres" the user means: keeps art sharp but higher-res, no external/copyrighted art. More work (integrate an upscaler over the palette-indexed source, then paletremap → RGBA atlas), but self-contained.
  Neural (ESRGAN) is out of scope (heavy, offline). Scope: Solid tier only; leave Classic (software) and Ultra (HD art) paths untouched. Likely a new layer after the DOOM-0170 lighting/shadow stack (or parallel — it is orthogonal to lighting). Decide (a)-only vs (a)+(b) and the filtering toggle UX with the user before building.
  **Layman:** Make DOOM's chunky textures and monster sprites look sharper and less blocky in the Solid view, the way a PS1/PS2 emulator can "upres" an old game — without needing new hand-drawn art.
  Kind: feature.
  Source: user-request-2026-07-09.

- ✅ [DOOM-0173] **Investigate the mysterious dark bordered rectangle overlaid on the Solid (raster) view.**
  Found while self-testing DOOM-0170 L2c. A semi-transparent dark, thin-bordered rectangle (~1/4 screen) is composited over the Solid/raster 3D view every frame. It moves/repositions frame-to-frame and in at least one capture overlapped the status bar (hid the AMMO/HEALTH numbers). CONFIRMED PRE-EXISTING, not from L2c: it appears identically in a clean HEAD build (commit bcab63f) with my changes stashed, and it shows with the flashlight both on and off (so unrelated to the shadow map, which is an off-screen target never composited). Repro: DOOMWADDIR=wads ./linux/linuxxdoom -iwad wads/doom.wad -warp 1 1, renderer=2 (Solid), screenshot mid-demo (see /tmp/doom_head.png, /tmp/doom_l2c_off.png). Drawn very late (over the HUD), so likely an overlay/composite-stage element — candidates: a leftover debug/preview inset (render target, GI/probe, RT accumulation), a light/emitter debug box, or a composite/overlay bug. May be an intentional debug view left enabled — confirm with user before removing. Not investigated in depth to avoid rabbit-holing the L2c work.
  **Layman:** There's a floating dark box drawn on top of the 3D picture in the fast "Solid" graphics mode — sometimes it even covers part of the health/ammo bar. Figure out what draws it and whether it should be there.
  Kind: investigate.
  Source: in-session-2026-07-12 (found during DOOM-0170 L2c self-test).
  Investigated 2026-07-12 (deep dive). CORRECTED DIAGNOSIS — my first "Solid composite overlay" guess was WRONG. Hard facts now established: (1) The box appears IDENTICALLY in the Classic 1997 software renderer AND Solid — captured E1M1/E1M2/E1M3/E2M1 in renderer=0 (verified software: no RB_Vulkan_BuildLevel in log; Classic_Present = I_FinishUpdate SDL blit of screens[0]) and renderer=2. So it is a shared 2D element in screens[0], NOT a Vulkan/composite artifact and NOT from L2c/L2a. (2) It is on EVERY map (not map geometry). (3) It is a HOLLOW black rectangle OUTLINE (~1/4 screen) at a FIXED screen position (right-of-centre, ~logical 78x63 at ~(146,76)); the scene shows through it continuously (proven with a mesh.frag geometry-tint build: interior renders as normal tinted walls/floor, the border is untinted black => drawn AFTER the mesh, in screens[0]). My earlier "moves/semi-transparent/FLAG_FLAT" reads were the moving scene behind a fixed hollow frame, plus a separate real dark-floor patch on E1M1 that I conflated. NOT YET FOUND: the exact drawer — source grep of all screens[0] writers (HU_DrawFPS, HU_drawSecret, ST_Drawer, M_Drawer, I_FinishUpdate, R_FillBackScreen/R_DrawViewBorder) turned up nothing that draws a centred box; R_DrawViewBorder confirmed no-op at screenblocks 10. NEXT STEP: git-bisect the introducing commit (fast repro: warp 1 2, renderer 2 or 0, box sits dead-centre at spawn), or instrument screens[0] to bisect which D_Display drawer paints it.
  RESOLVED 2026-07-12 — NOT A DOOM DEFECT. Root cause: the "box" is a hollow black rectangle drawn by the DESKTOP/COMPOSITOR (KDE Wayland), on top of whatever is on screen — proven by screenshotting the plain desktop with NO game running (spectacle desktop.png): the identical black rectangle outline appears over the browser at a fixed screen position. It shows in DOOM on every map, in BOTH the Classic software renderer and the Solid/Ultra Vulkan renderers, and SURVIVES disabling every game draw path (screens[0] wiped to a flat colour before present; composite AND overlay vkCmdDraw both commented out -> box still present over the bare slate clear) — because nothing in linuxdoom draws it. No engine code change is warranted. USER ACTION (external to this repo): it is a screen-space overlay from the KDE/Wayland desktop or a running app — likely a screen-recording/screencast region indicator (pipewire screencast is running), a stuck Spectacle rectangular-region selection, an annotation/spotlight tool, or a transparent always-on-top window. Close the screen-record/share tool or the stray window and it should disappear. All temporary debug instrumentation (mesh.frag geometry tint, r_draw.c border prints, r_mesh.c cap dumps, r_things.c sprite dump, d_main.c screens[0] wipe, r_vulkan.cpp draw disables, magenta clear) has been reverted; working tree clean.

- 📋 [DOOM-0175] **Remember the player's last render-mode choice (Classic/Solid/Ultra + RT) across launches.**
  Split out of DOOM-0169. DOOM-0169 made the render tier drive the ray-tracing default (Solid = fast raster) at startup; the user would additionally like the last-chosen render mode to persist across sessions so re-launching returns to their preferred view. Likely a small addition: write the current tier/RT selection to the config (~/.doomrc, alongside renderer=/rt_view=) whenever it changes and honour it on load, rather than always applying the tier-derived default. Verify: pick Ultra, quit, relaunch -> starts in Ultra; same for Solid/Classic.
  **Layman:** The game should start up in whatever view mode you last used, instead of resetting to a default each time you launch.
  Kind: enhancement.
  Source: user-request-2026-07-12.

- 📋 [DOOM-0176] **Fix render-pass/framebuffer subpass-dependency incompatibility validation errors in the raster path.**
  Validation (RADV, VUID-VkRenderPassBeginInfo-renderPass-00904 + VUID-vkCmdDraw-renderPass-02684): a VkRenderPass used to BEGIN a pass (0xf..) is incompatible with the VkRenderPass the framebuffer/pipeline was created against (0xc..) on the subpass dependency srcStageMask/srcAccessMask/dstAccessMask (VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT + TRANSFER_WRITE vs EARLY_FRAGMENT_TESTS|COLOR_ATTACHMENT_OUTPUT). ~20 messages/frame. NOT from DOOM-0042 (that change is compute-only, touches no render pass) — pre-existing raster, likely the DOOM-0170 L2b MRT/SSAO composite passes whose subpass dependency carries an ALL_TRANSFER src. Fix: align the offending render pass's subpass-dependency stage/access masks between the framebuffer-creation pass and the begun/pipeline pass. Benign on RADV (renders correctly) but spec-noncompliant.
  **Layman:** The graphics card's debug checker complains that two rendering steps disagree about timing; harmless on this GPU but should be cleaned up.
  Kind: fix.
  Source: observed-2026-07-14 (DOOM-0042 E1M1 Ultra play-test log).

- 📋 [DOOM-0177] **Fix a 2-object (1 VkBuffer + 1 VkDeviceMemory) leak at vkDestroyDevice shutdown.**
  Validation at vkDestroyDevice: 'VkDevice has 2 leaked objects' — one VkDeviceMemory + one VkBuffer (adjacent handles = a buffer+its memory allocated together). NOT the DOOM-0042 HD resources: FreeHdMaterials() frees every HD buffer/memory (hdCtrlBuf/hdCtrlMem/hdMemory + images/views/pool) and is called at shutdown (r_vulkan.cpp:7540) before device destroy — audited create/destroy-balanced. Candidate: g.overlayStaging + g.overlayStagingMem (r_vulkan.cpp:7553-7554, only freed if non-null) or another subsystem's staging buffer. Confirm pre-existing with a Solid-only run (never enter Ultra, so no HD built) — if it still leaks 2, it is not HD-related. Shutdown-only; no gameplay impact.
  **Layman:** When the game exits, it forgets to hand back one small chunk of graphics memory. No effect while playing; tidy-up on quit.
  Kind: fix.
  Source: observed-2026-07-14 (DOOM-0042 E1M1 Ultra play-test log).

- 📋 [DOOM-0178] **Per-texel HD emissive maps in the shipped denoised RT path (mode 6).**
  DOOM-0042 T13 adds HD emissive (hdEmissive*kEmissiveScale) on the mode-4
  display path only. The shipped denoised path (mode 6) demodulates albedo and
  re-adds emission in svgf_composite.comp via the PALETTED per-material Le table
  (matEmis) x an albedo-brightness mask — there is no HD descriptor set in the
  composite, and galbedo.a is already taken by the emission-enable flag, so a
  dedicated per-texel HD emissive map has no free channel there. v1 ships NO
  emissive maps so this is currently DORMANT (no regression). Fix options: (a) a
  new emissive G-buffer image the composite reads raw (un-denoised, not albedo-
  modulated); (b) bind the HD set into svgf_composite and sample maps[5] there.
  Prereq for any HD hero that carries a real emissive map.
  **Layman:** Glowing HD textures (e.g. a lit computer panel) currently only glow in the debug ray-traced view, not the normal denoised game view. Add a way for them to glow correctly in the real view too.
  Kind: enhancement.
  Source: in-session-2026-07-14 (DOOM-0042 T13).

- ✅ [DOOM-0179] **World-position grime/blemish overlay so HD surfaces don't tile uniformly.**
  Follow-on to DOOM-0042 (do AFTER the base hero textures land + sign off; user
  chose base-first). Approach: sample a low-frequency grunge/dirt map (the user's
  3D Engine Assets/Textures/Grunge/ library, CC0) by WORLD position (world XZ for
  flats, world XY/Z projected for walls) rather than tile UV, so the grime drifts
  across a whole surface and does NOT repeat with the base tile. Blend into albedo
  (multiply/overlay) + optionally perturb roughness; a strength dial (menu or const,
  tune with user). Breaks the repetition the user flagged. Ultra RT path first
  (pathtrace.comp), same set-3 material infra as DOOM-0042. Consider a per-surface
  hash tint/offset as a cheaper complementary trick. NB: user picked a PRE-WEATHERED
  base for the heroes, so tune overlay strength to ADD variation, not double-grunge.
  **Layman:** DOOM paints one texture across many walls, so an HD texture repeats visibly and every wall looks identical. This adds a drifting layer of dirt, wear, and scorch marks placed by where each surface sits in the world — so no two walls look the same, and the tiling stops being obvious.
  Kind: feature.
  Source: user-request-2026-07-14 (DOOM-0042 T17 play-test discussion).
  Progress (2026-07-14, 58bb59c): implemented for the Ultra RT view. A first-party, procedurally-authored, seamless CC0 grunge map (scripts/make_grunge.py -> assets/ultra/overlays/grunge.png) is sampled by WORLD position (dominant-axis projection) and multiplied over usePBR HD surfaces in pathtrace.comp modes 4/6, breaking the base tiling. New misc5 push-constant slot carries the overlay's bindless id; loaded as one extra bindless image in EnsureHdMaterials. Centred blend = net-neutral exposure; kGrimeStrength (0.32) + kGrimeWorldScale (1/384) are shader-const playtest knobs. Paletted/Classic untouched. Also fixed a latent -rtverify (mode 5) push-constant drift the change surfaced. AWAITING user play-test to tune strength/scale, then flip to shipped + CHANGELOG.
  Resolved (2026-07-17): shipped as the filth layer of DOOM-0181. The world-position grunge overlay (misc5.x, world-projected) became the grounding term of the DOOM-0181 stain system; graduates ✅ per its ship-gate (DOOM-0181 accepted). See docs/specs/DOOM-0181-detile-grime.md §4.3.

- ✅ [DOOM-0180] **Thin bright diagonal seam on ceilings in the Ultra RT view.**
  A thin, bright, diagonal line appears on dark ceiling flats in E1M1 Ultra (green
  room; also faintly in the wood-panel room). NOT from DOOM-0042 HD materials —
  ceilings are not heroed and run the paletted path (usePBR=0, byte-identical). Most
  likely a pre-existing T-junction / triangulation crack in the RT flat mesh where a
  sliver of a brighter neighbouring surface or the sky backdrop leaks through the
  sub-pixel gap between two coplanar ceiling triangles. Investigate: (1) repro on a
  pre-DOOM-0042 build to confirm pre-existing; (2) inspect the flat-triangulation in
  r_mesh (T-junction handling at sector/subsector edges); (3) check if the bright
  value is the sky backdrop or an adjacent lit flat. Low priority (hairline, not
  gameplay-affecting) but a visible RT-view polish item.
  **Layman:** A faint bright line shows up along some ceilings in the ray-traced view, like a hairline crack. Harmless, but it should be tracked down and sealed.
  Kind: investigate.
  Source: observed-2026-07-14 (DOOM-0042 T17 E1M1 Ultra play-test, images #8/#9).
  Diagnosed 2026-08-04 (headless, E1M1 -warpto 3274 -3353 200 -- the
  roofed nukage room, seam along the pit ledge rather than a ceiling).
  ROOT CAUSE CLASS CONFIRMED: the seam is a genuine HOLE in the shared
  Vulkan world mesh -- a ray miss, not a shading artefact. Evidence, each
  by construction with a control that moved:
  - HITS debug view (rt_view 1, rt_debug_views 1) renders the seam BLACK.
    Black appears in exactly two places in that frame: the window opening
    (looking outside) and the seam. Black = no hit.
  - TEXTURED view (rt_view 3, unlit albedo) also renders it black, while
    the shipping view renders it neutral white ~ (236,238,237). That pair
    is the signature of a miss: DOOM-0143 established mode 6 returns
    skyPanorama on a miss, so a crack shows sky, not black.
  - Present in BOTH 3D tiers (Ultra RT and Solid raster) and ABSENT in
    Classic (user-confirmed + own capture via the new Classic -devshot
    path). Classic never builds the 3D mesh, which places the fault in the
    mesh rather than in either shading path.
  - Independent of HD art (identical with DOOMASSETDIR unset, a control
    that moved 74.9% of the frame), of fog, and of the wet-liquid layer.
  RULES OUT the original guess: not "coplanar ceiling triangles", not a
  missing texture (emit_wall already drops the "-" side, r_mesh.c:566),
  not atlas bleed (the paletted atlas samples NEAREST with no padding,
  r_mesh.c:1541-1543).
  WAS STILL OPEN -- why the gap exists. The suspect recorded here was the
  BSP carve clipping the cap a hair short of the linedef (r_mesh.c:588);
  that was WRONG, and the carve is correct. The answer was one level
  lower, in the map data itself. Do not re-follow the carve.
  Resolved (2026-08-05): DOOM stores every vertex as a 16-bit INTEGER, so
  when the node builder SPLITS a linedef the split vertex is rounded to
  whole units. On an axis-aligned linedef that is exact; on a DIAGONAL one
  it lands off the true line. Worse, the two sides of a linedef are split
  independently at different points, so the front segs and the back segs
  trace two DIFFERENT polylines between the same endpoints -- and r_mesh.c
  built each wall quad on one side's seg while clipping the neighbouring
  floor/ceiling caps to the other's. The two therefore missed each other
  by that rounding, leaving the hole.
  Measured straight from doom.wad, E1M1: 6 of its 180 two-sided linedefs
  trace mismatched polylines, worst 0.97 units -- linedef 193, the pit
  ledge, worst at (3215.8,-3404.6), 78 units from the 2026-08-04 camera
  that photographed the seam (its back-side split vertex v442 sits 1.0165
  units off the line; the front side's is 0.0549). ALL SIX are diagonal;
  not one of the map's 381 axis-aligned linedefs mismatches. That is why
  the seam was always diagonal, which the original 2026-07-14 report said
  and nothing since had explained.
  Fix: seg_project.h (RB_ProjectOnLine) + r_mesh.c seg_line_xy project
  every seg endpoint onto its linedef's exact line, used by emit_wall,
  emit_sky_wall AND the emit_subsector_caps half-plane clip, so both sides
  build on one shared line. emit_subsector_caps caps the floor and the
  ceiling from the SAME carved polygon, so the ceiling seam of the
  original report closes by the same edit as the ledge.
  Verified headless: E1M1 -warpto 3274 -3353 200, Ultra RT, fog off,
  -inspect -freeze. Seam gone by eye; signal max 72.3/255 against a
  same-build control noise floor of max 4.3, and 0.3% of pixels moved with
  every one of them at the ledge. -rtverify PASS and UNCHANGED by the fix
  (rel-MSE 0.2059% pre / 0.2058% post, bar 0.50%; white furnace 0.000000).
  make + make test green.
  The CEILING seam of the original 2026-07-14 report is photographed shut
  as well, not merely argued from the shared polygon: same camera at yaw
  120 and 150, the thin bright diagonal hairline on the dark ceiling is
  present pre-fix and absent post-fix. Those two views carry more harness
  noise than the ledge one (a same-build control moves up to 88/255 at yaw
  120), so the seam pixels were isolated as "the fix moved it AND the
  control did not" -- 480 px at yaw 120, 900 px at yaw 150, every one of
  them in the top eighth of the frame. Quote that pairing, not the raw
  signal, for any A/B at these angles; ab_diff.py now prints it as its
  EFFECT row so it does not have to be rebuilt by hand.
  Tooling kept, not thrown away: scripts/wad_seg_probe.py is the probe that
  found this, and it found it from the WAD alone -- before any engine edit
  or any captured frame. Start there on the next "there is a crack in the
  world" report; a map-data answer costs seconds. Its header carries the
  full account, and `wad_seg_probe.py wads/doom.wad E1M1 -- 3274 -3353`
  reproduces every number quoted above.
  Regression lock: tests/seg_project_test.cpp carries E1M1's real numbers
  and asserts BOTH halves -- that the stored vertex really is off the line
  by the measured amount, and that projecting puts it back -- so it would
  also notice if a future WAD-loading change moved the input. Confirmed to
  fail (8 checks) with RB_ProjectOnLine stubbed to the old behaviour.
  Not swept: RB_BuildProbes and RB_BuildSeepField also read seg vertices,
  but only to average them into a centroid, where a sub-unit shift cannot
  matter. Left alone deliberately.

- ✅ [DOOM-0181] **Stochastic per-tile de-tiling for HD surfaces so walls/floors stop looking copy-pasted.**
  **Layman:** Stops HD walls and floors in the ray-traced view looking like the same tile pasted over and over — each repeat is secretly nudged/mirrored and keyed to its world position, so one wall stops cloning itself and different walls stop cloning each other.
  Kind: feature.
  Source: in-session-2026-07-16 (user: E1M1 walls "extremely tiled"; pairs with DOOM-0179 grime).
  Spec docs/specs/DOOM-0181-detile-grime.md written + /cold-eyes reviewed (2026-07-16): 6 loops, shared-cache reviewers. Design stable/unchallenged from loop 1; loops fixed data-flow, perf-baseline (RT-on vs DOOM-0170 raster), and precision. Confirming loop 6 caught a mirrorProb probability inversion (hash.z > vs < mirrorProb). Locked, implementation-ready. Next: writing-plans → subagent-driven build (L1 de-tile albedo → L5 perf/dial).
  Resolved (2026-07-17): shipped. L1–L5 built (de-tile albedo/normal/AO/height+POM, runtime `]` dial 0/1/2) + L4 filth redesigned into a distinct multi-coloured dirt-stain system (grungeFbm 3-scale + hard smoothstep + stainColour sampling a real CC0 dirt texture dirt.png/misc5.z), goo puddles on floors, liquid-skip guard, applied to all non-sprite world surfaces. User play-test accepted. Spec docs/specs/DOOM-0181-detile-grime.md reconciled to as-built + re-run through /cold-eyes (3 loops). One perf follow-up open: the post-L5 filth stain fetch cost is not yet isolated on the profiler (spec §6/§10 Q4) — perf levers documented ready.

- 📋 [DOOM-0182] **Extend DOOM-0042 HD materials to the Ultra raster sub-view (HD in all Ultra views, not just ray-traced).**
  Today HD (usePBR) materials are sampled ONLY in the RT path (pathtrace.comp);
  the raster fragment shader (mesh.frag) samples the paletted materialTex[], so
  Ultra with RT off (rb_rtdebug 0) shows base textures. This was the DOOM-0042
  "RT-view-first" sequencing; the raster HD pass was always a planned fast-follow.
  Scope: bind the HD descriptor set (set 3) into the raster pipeline, add a
  usePBR albedo/normal/AO sampling branch in mesh.frag mirroring the RT path, and
  (for parity) port the DOOM-0181 de-tiling + filth wrappers so the raster view
  matches the RT look. Keep Classic + paletted byte-identical. Depends on the
  DOOMASSETDIR launcher fix (2026-07-16) so HD actually loads.
  **Layman:** Make the fancy high-res wall textures show up in Ultra's fast (non-ray-traced) view too, so switching ray-tracing off doesn't drop you back to the blocky originals.
  Kind: feature.
  Source: user-request-2026-07-16.

- 🚧 [DOOM-0183] **Reflective, glowing, liquid goo (nukage) in the Ultra RT view.**
  Grew out of the DOOM-0181 grime/stain iteration (see [[doom-0181-detile-grime]]). The stain system now paints green-goo puddles on floors; the user wants the goo (puddles AND the source nukage flat) to read as a wet liquid. Needs a design pass first (user chose "design properly"). Scope to settle in design:
  - Light-green EMISSIVE glow on goo — the RT emissive channel already exists (pathtrace.comp self-radiance), so this part is cheap.
  - Reflectivity — the path tracer currently does DIFFUSE bounces only (no glossy/specular reflection rays). True reflective goo needs a new glossy-reflection capability (also unlocks reflective metal/floors later); relates to the deferred scoped-SSR idea. This is the feature-sized part.
  - A liquid-property replacement texture for the nukage (wet/animated normal, ripples).
  - Contextual placement: goo puddles belong in/near goo sectors, never on the nukage liquid itself (it is the source) — needs the shader to know which sectors contain liquid. Current cheap guard only skips already-green albedo.
  Related sub-item to fold in or split: dirt/stains that fall over an EMISSIVE surface (a light/panel) should tint & dim the emitted light, not just the albedo (user note 2026-07-16).
  **Layman:** Make the green toxic sludge — both the puddles on the floor and the main pool it comes from — look like real glowing, reflective liquid instead of a flat green colour.
  Kind: feature.
  Source: user-request-2026-07-16.
  Design pass done (2026-07-17): spec docs/specs/DOOM-0183-glowing-wet-liquid.md written + cold-eyes clean (3 loops). Scope settled with the user: "cheap wins first" — green nukage gets glow + cast-light + wet direct-light sheen + procedural ripples; LAVA gets glow + orange cast-light (user add); true mirror reflections stay DOOM-0103; water/blood deferred. Liquid identity via a MatCtrl.flags bit from the flat name (NUKAGE1-3, LAVA1-4); cast-light = forced-constant Le through the existing NEE emitter path (delivers DOOM-0083). Build order L1 liquid bit -> L2 glow/cast-light -> L3 sheen -> L4 ripples -> L5 puddle-wet -> L6 toggle+perf. Cheaper-RT research from this pass captured separately as DOOM-0188..0192; RT-glow dial-up as DOOM-0193.
  Progress (2026-07-17): L1-L6 implemented + committed/pushed (ecf9a6c). L1 liquid bit (MatCtrl.flags bit3/bit4 by flat name — NUKAGE1-3/LAVA1-4 — via FlagLiquidFlats); L2 forced-constant Le glow+cast-light via ForceLiquidEmissive (delivers DOOM-0083); L3 direct-light Blinn-Phong sheen (flashlight+muzzle, no reflection ray); L4 procedural ripple normal on nukage + new misc6 push-const lane for time (steady_clock seconds); L5 goo-puddle wet via gooWet mask exposed from applyGrime; L6 rb_wet toggle (' key, rt_wet config). misc6 appended after the 216-byte tail (std430-padded to offset 224, size 240) so the 184-byte -rtverify prefix is byte-identical (INV-6); static_assert+pcr.size 216->240. Fixed the stale misc2.z/.w comment (Q8). `make` + `make test` green. Tuning consts (kNukageLe/kLavaLe/kWetSheen*/kRipple*/kPuddle*) are placeholders. REMAINING (human-run L6 gate): on-hardware play-test of the look on the E1M1 goo room + a lava map, -rtverify green, and the <=5% perf measurement (rb_wet off vs on). Then flip to shipped + graduate DOOM-0083 + CHANGELOG. Known v1 note: mode-6 (denoised) sheen/puddle-glow are albedo-tinted by demodulation — reads as a green wet glint on the green liquid (on-theme); a neutral wet surface would need a full-res specular channel (DOOM-0103 follow-up).
  Verify progress (2026-07-18): build green; -rtverify PASS (direct-light rel-MSE 0.1317% vs 0.50% bar, white-furnace 0.000000) — Ultra RT correctness holds with the liquid changes. No-liquid perf baseline unchanged: E1M1 spawn Ultra = 45 fps, megakernel 10.5ms, present-total 22ms (matches pre-0183 ~45fps GPU-bound), so the CPU-baked glow/cast-light (permanent Le in matEmissive+NEE+GI-bake) adds no general-frame regression. STILL OPEN (needs someone at the screen): (a) the per-liquid-pixel wet-shader cost (sheen §4.4 / ripples §4.5 / goo-wet §4.6) with nukage IN VIEW — rt_wet-gated, so A/B live with the ' key near a nukage pool and watch [rt_profile] megakernel for the ≤5% budget; (b) the subjective look. Headless self-drive is unavailable this session: SDL now opens a NATIVE WAYLAND surface (no X window for the pid, so xdotool/import can't target it) and this box has no grim/ydotool/wtype to inject Wayland input — the old X11 xdotool recipe in [[doom-ants-launch-screenshot-harness]] no longer applies. User play-test will close both.
  USER PLAY-TEST 2026-08-04 -- HALF SIGNED OFF, HALF NOT, and the failing
  half has a measured root cause from the same day.
  "The colour is now uniform across the whole surface but it still doesn't
  look like it is emitting green on to the fog or the surrounding
  environment. This must also apply to barrels."
  SIGNED OFF: the uniform surface. That is DOOM-0302's emisWeight() fix
  confirmed by eye -- the pool reads as one sheet of glowing sludge rather
  than the field of bright patches the 2026-08-02 report described.
  NOT SIGNED OFF: the cast light, on BOTH the fog and nearby surfaces. This
  is not a new discovery and it is not a tuning miss -- DOOM-0316's headless
  ladder measured the cause hours earlier, and the user's eye independently
  reproduced it, which is the strongest corroboration available:
  kNukageLe serves TWO consumers that saturate in the wrong order. Mean green
  in the E1M1 roofed goo room, Ultra RT, rt_fog 2, render_scale 100:
    Le x    pool surface   %clipped   glow above the pool edge
    1x       133.01           0.0%       57.67   (shipped)
    5x       235.40           0.0%       86.86
    10x      243.87          94.7%      112.55
    20x      247.27          94.7%      148.23
  The surface CLIPS between 5x and 10x -- at 10x, 94.7% of it is flat blown-out
  green, the exact slab DOOM-0302 was tuned to remove -- while the cast light
  does not read until ~20x. So no value of kNukageLe satisfies both, and
  raising it is not the fix. The surrounding-surfaces half has the same cause:
  at 20x the walls near the pool gained ~32 in green with fog OFF, so the pool
  DOES light the room via NEE, just far too faintly at the shipped constant.
  THEREFORE this item's remaining half is blocked on DOOM-0316, which owns the
  constant split (a separate surface Le and fog/NEE Le) plus the position-keyed
  liquid-proximity field. Not re-scoped into it: the deliverable is still this
  bullet's, but the mechanism it needs is designed there. Do NOT attempt a
  kNukageLe re-tune to close this -- it is measured not to work.
  BARRELS ARE NEW SCOPE and are filed separately (see the barrel bullet in
  this phase). They are not liquid and carry no LIQUID_* MatCtrl bit, and
  sprite emitters are derived from artwork brightness via DOOM-0084's
  peak-gated derive rather than from any name list -- a DOOM barrel is not
  bright enough to pass that gate, so it emits nothing today. The fix shape is
  the sprite analogue of ForceLiquidEmissive: a name-keyed forced Le on BAR1.

- 📋 [DOOM-0184] **Glowing fireball / projectile that casts light (Ultra RT).**
  User: "I really like this fireball, can we replicate it?" (ref: Ultimate Doom RTX mod). A self-lit projectile sprite with a warm emissive core + a point light travelling with it so nearby walls/floor light up as it passes. Relates to the dynamic-light trio DOOM-0010/0101/0102 and the emissive sprite path (DOOM-0084). See [[rt-aesthetic-north-star]].
  **Layman:** Make fireballs and other glowing shots light up the room as they fly, like a little moving torch.
  Kind: feature.
  Source: user-request-2026-07-16.

- 📋 [DOOM-0185] **Coloured glow around key-locked doors (red/blue/yellow).**
  User: "I like the red glow of the door requiring the red key card" (ref: Ultimate Doom RTX mod). Key-coloured emissive on the locked-door texture + a coloured light so the corridor glows. A gameplay-readability win too. Relates to dynamic lights DOOM-0010/0101/0102 and HD emissive.
  **Layman:** Make the doors that need a coloured key glow in that colour, so you can spot them and they cast coloured light.
  Kind: feature.
  Source: user-request-2026-07-16.

- 📋 [DOOM-0186] **Extend HD up-res + POM/PBR to ALL wall/floor/ceiling textures, not just the hero set.**
  User observed the Ultimate Doom RTX mod up-ressed the ORIGINAL textures and added POM/PBR across the board (so the whole level looks HD, not just curated spots). Extends DOOM-0042 (v1 = walls+flats hero set only) to the full texture set — likely via the pbr_derive.py auto-derive path (WAD -> upscaled + normal/rough/AO/height) applied to every texture, with the VRAM budget (DOOM-0042 T5) managing the count. This also fixes DOOM-0181's "grime only shows on HD surfaces" reach. Big but high-impact.
  **Layman:** Give every wall, floor and ceiling the high-detail 3D-looking treatment, not only the handful we've done so far.
  Kind: feature.
  Source: user-request-2026-07-16.

- ✅ [DOOM-0187] **Profile + isolate the DOOM-0181 filth stain-fetch cost; apply a perf lever if it bites.**
  DOOM-0181's filth stain system was added AFTER the L5 de-tile perf pass and has never been isolated on the profiler (spec docs/specs/DOOM-0181-detile-grime.md §6, §10 Q4). Cost, as-built: applyGrime runs on EVERY non-sprite world hit; grungeFbm = 3 overlay fetches always, stainColour = +2-3 fetches only where a stain forms (gated behind the smoothstep threshold), 0 on liquids. Steady ~3 fetches/primary-hit, spiking ~5-6 in stains; paid once per pixel (GI is baked). Task: `\` profiler present-total over a ~10s green-goo-room walk with de-tiling off vs the shipped filth, at 50% render scale + flashlight; record the ms in §6. Perf levers already documented ready (spec §6): (1) grungeFbm 3->2 world scales; (2) a filth quality/off dial on the free misc5.w lane (no look change when on — cleanest); (3) distance/LOD gate on the fine-grain stain fetch. Lever 2 is the safe first move if a real cost shows. Needs the live profiler (user at the keyboard; RT view can't be driven headless).
  **Layman:** The new dirt/stain layer adds a few texture look-ups to every wall and floor pixel in the ray-traced view. We shipped it because it looks right, but we never actually measured how much it costs. This is a reminder to run the built-in speed profiler over the goo room and, only if it's actually slowing things down, turn on one of the ready-made cheaper settings.
  Kind: perf.
  Source: in-session-2026-07-17 (DOOM-0181 ship-gate; user asked to keep performance in mind).
  Resolved (2026-07-17): profiled the green-goo room (Ultra RT, 50% scale, flashlight, RX 6600). De-tile 4-tap = 0.70ms / 3.0% present-total (inside the L5 ≤5% gate); filth stains = ~0.40ms on the ray-trace megakernel and ≈0 (noise) on the whole GPU-bound frame — isolated via a new misc5.w on/off toggle. Below profiler noise, so NO lever was pulled (filth default stays on). Added the misc5.w filth dial (`[` key, rb_filth) as the isolation instrument + a standing perf/quality option. The room's ~40fps is the megakernel (~12ms) + denoiser (~7ms), NOT DOOM-0181 (a small rider on both). Spec §6 + §10 Q4 updated with the captured numbers; toggle built + make test green.

- 📋 [DOOM-0188] **Trace the ray-traced lighting at reduced resolution (¼/½) and bilateral-upscale it.**
  Cheaper-RT research lever #1. Our megakernel traces at/near screen res; id's final-gather traces 1 ray/pixel at 1/2 or 1/4 res with a normal/depth bilateral upscale (reuses the TAAU we already have, DOOM-0090). Up to ~16x fewer primary GI rays. Expected payoff: the largest single cut to the ~12ms trace in the goo room, for modest quality softening. Effort: low-medium. Pairs with denoise-at-trace-res (see the A-SVGF item). Ref: Sousa, 'FAST AS HELL: idTech 8 GI', SIGGRAPH 2025.
  **Layman:** Do the expensive lighting math on a smaller copy of the picture, then smartly scale it back up — the single biggest, lowest-risk speed-up id used in DOOM: The Dark Ages (they never trace at full screen resolution).
  Kind: perf.
  Source: research-2026-07-17 (DOOM-0183 cheaper-RT survey).

- 📋 [DOOM-0189] **World-space radiance cache — reuse lit results across frames; rays become visibility-only.**
  Cheaper-RT research lever #2 — highest ceiling, biggest rewrite. Amortize indirect shading across frames via a hash-grid / DDGI-style probe cache; terminate paths after ~1 bounce into the cache (shorter paths) and feed the denoiser pre-smoothed input (cuts BOTH the ~12ms trace and the ~7ms denoise). This is the structural difference between 'path tracer + denoiser' (us) and 'cache-fed GI that only traces visibility' (id). Study refs: AMD GI-1.0 / Capsaicin (Sponza 4.2ms@1080p RX6900XT), NVIDIA SHaRC (RTXGI 2.0), DDGI (Majercik JCGT), Metro Exodus EE infinite-bounce DDGI. Needs a new cache + update pass. Effort: medium-high. Its own brainstorm->spec->cold-eyes cycle when tackled.
  **Layman:** Remember lighting results in a reusable 3D grid so we don't recompute them from scratch every frame. This is THE core trick behind DOOM: The Dark Ages hitting 60fps — their whole lighting step costs ~2ms versus our ~19ms.
  Kind: perf.
  Source: research-2026-07-17 (DOOM-0183 cheaper-RT survey).

- 📋 [DOOM-0190] **Overlap ray tracing with the rest of the frame (async compute + 2 frames in flight).**
  Cheaper-RT research lever #4. id bought ~0.4-0.5ms on base consoles purely by running GI async with raster and said they couldn't hit target without it. Our CPU profiler (DOOM-0187 session) shows a serialized 'CPU build -> fence-wait on GPU' frame; the MEMORY note already scopes '2 frames in flight -> ~110fps'. Hides GPU RT cost behind other work rather than reducing it; no quality change. Effort: medium. Relates to DOOM-0170 (raster perf mode) and the general frame-pacing work.
  **Layman:** Let the graphics card do the ray tracing at the same time as its other work, instead of one-then-the-other. Our own profiler shows the frame currently runs the CPU build and then sits waiting on the GPU — that stall is recoverable for free (no visual change).
  Kind: perf.
  Source: research-2026-07-17 (DOOM-0183 cheaper-RT survey) + in-session profiler notes.

- 📋 [DOOM-0191] **Upgrade the denoiser in place — adaptive A-SVGF + blue-noise, run at trace resolution.**
  Cheaper-RT research lever #5. Keep our SVGF architecture; add A-SVGF gradient-driven adaptive temporal accumulation (kills ghosting/lag while keeping effective sample count), blue-noise ray directions (filter-friendly residual noise, fewer rays for the same floor), and run the denoise at the 1/4-1/2 trace res. Cheapest quality-per-effort denoise lever. Refs: A-SVGF (KIT adaptive temporal filtering), NVIDIA NRD (ReBLUR/ReLAX) as an alternative drop-in. Effort: low-medium. The DOOM-0009 step-6 note already flagged 'A-SVGF then FSR2'.
  **Layman:** Make the noise-cleanup step smarter and run it on the smaller (lower-res) image — trims the ~7ms denoiser and reduces smearing/ghosting when things move.
  Kind: perf.
  Source: research-2026-07-17 (DOOM-0183 cheaper-RT survey).

- 💭 [DOOM-0192] **ReSTIR direct lighting for many-emitter scenes (far-future; only if emissive load explodes).**
  Cheaper-RT research lever #3, parked far-future by design. ReSTIR DI (reservoir resampling) samples many emissive triangles at ~1 shadow ray/pixel (San Miguel: 11k emissive tris <8ms). The upgrade path IF plain NEE gets noisy under heavy emissive liquid (DOOM-0183 glowing lava/nukage) or a future many-light scene. Cross-vendor compute (reservoir buffers + temporal/spatial reuse on top of existing NEE). Measure first — do NOT build pre-emptively; a handful of lava pools do not need it. Best paired with the radiance cache for the diffuse bounce (mirrors Cyberpunk Overdrive). Refs: ReSTIR GI/RTXDI (NVIDIA).
  **Layman:** A technique to light a room from thousands of tiny lights very cheaply. Only worth it if we ever have huge amounts of glowing surfaces (e.g. vast lava fields lighting everything). For a few glowing pools today it is overkill — plain lighting handles that fine.
  Kind: perf.
  Source: research-2026-07-17 (DOOM-0183 cheaper-RT survey).

- 📋 [DOOM-0193] **Dial up the exaggerated RT emissive glows (barrels, bonus-armour eyes, switches).**
  Aesthetic north-star: RT-DOOM lighting reads deliberately exaggerated (user, watching RT DOOM 1+2 videos 2026-07-17). Tuning pass on hardware with the user once the underlying glows land: (1) EXPLOSIVE BARRELS -> dial up (home: DOOM-0084 free-standing light objects). (2) BONUS ARMOUR (BON2, green): the 'eyes' need a LOT more green glow (home: DOOM-0114 - annotated there). (3) SWITCHES -> up a little (DOOM-0082 shipped; tune kEmitterPeakLum / kEmissiveScale). (4) HEALTH BONUS bottles (BON1, blue) -> confirmed already good, do not touch. Pure intensity tuning, no new mechanism.
  **Layman:** Ray-traced DOOM videos push the glows harder than we currently do. Turn up the glow on the explosive barrels, make the green bonus-armour's 'eyes' glow MUCH more green, and nudge switch glow up a touch. The blue health bottles already look right — leave them.
  Kind: enhancement.
  Source: user-request-2026-07-17.

- ✅ [DOOM-0194] **Fix the stale misc5 push-constant comment in pathtrace.comp.**
  The `uvec4 misc5` comment in the pathtrace.comp push-constant block reads "x = world-grime overlay bindless id ... y,z,w reserved", but y/z/w are all in use: y = de-tile quality (DOOM-0181), z = dirt-colour texture id (DOOM-0181), w = filth master toggle (DOOM-0187). Same class of stale-comment trap DOOM-0183 Q8 fixed for misc2.z/.w. One-line comment correction; no behaviour change. Left un-fixed during DOOM-0183 to stay in-lane (rule 11).
  **Layman:** A code comment describing one of the GPU data slots is out of date and could mislead the next person editing that file.
  Kind: doc-fix.
  Source: in-session-2026-07-17 (found during DOOM-0183 misc6 work).
  Resolved (2026-07-17): corrected the misc5 push-constant comment (y = de-tile quality DOOM-0181, z = dirt-colour texture id DOOM-0181, w = filth master toggle DOOM-0187). Comment-only, no behaviour change. Commit a54d562.

- 📋 [DOOM-0195] **-rtverify self-test never exercises the omni NEE loop (fires before dynamic sprite emitters exist).**
  DOOM-0122 wired the -rtverify verify path to pass the real omniStart (misc4.y = g.staticWgt.size()), so the omni sprite-light branch WOULD be covered. But on hardware every -rtverify run reports 0 omni emitters, at every scene tried (E1M1/E3M1/E3M4/E4M1). Root cause: RB_RtVerify triggers in the present path ABOVE vkAcquireNextImageKHR / RecordRtTrace (r_vulkan.cpp:7069), on the FIRST present -- but dynamic sprite emitters are populated per-frame inside RecordRtTrace (FinalizeEmitters(&emit,&wgt,&dynSec), r_vulkan.cpp:5499), which has not run yet. So g.emitCount holds only the level-load STATIC set (BuildProbes -> FinalizeEmitters(nullptr,nullptr)), giving omniStart == emitCount always. To actually exercise the omni loop the verify must run after >=1 full frame (so dynamic emitters populate) OR synthesize a dynamic emitter for the verify camera. NOTE: running post-frame conflicts with RB_RtVerify's assumption that the display image is still UNDEFINED (it parks it UNDEFINED->GENERAL); that barrier needs rework if the trigger moves. Blocks DOOM-0122 graduating to shipped.
  **Layman:** The lighting self-test can't actually check the glowing-sprite lighting path yet, because it runs a moment too early -- before the game has worked out which glowing props are on screen.
  Kind: test.
  Source: in-session-2026-07-17 (found while completing DOOM-0122 on the RX 6600).

- 📋 [DOOM-0196] **-rtverify INV-6 fails at E3M1 (rel-MSE 1.61%, bar 0.5%) -- bias or under-convergence?**
  Running -rtverify -warp 3 1 (Ultimate Doom, retail) reports INV-6 direct-light rel-MSE = 1.6088% over 64000 lit px -- FAIL (bar 0.50%). White-furnace passes (0.000000). E1M1 (0.08%), E3M4 (0.32%), E4M1 (0.07%) all pass, so it is E3M1-scene-specific. PRE-EXISTING and unrelated to DOOM-0122: at E3M1 staticN==emitCount==71, so DOOM-0122's omniStart change is a no-op there (byte-identical to the prior omniStart==emitCount behaviour). Distinguish bias from variance: re-run with far more spp (raise the runEstimator dispatch counts) -- if rel-MSE shrinks toward 0 it is under-convergence at that high-variance view; if it plateaus above the bar it is a real one-sided bias in the static power-NEE estimator worth root-causing. Low priority (self-test only; display path unaffected).
  **Layman:** The lighting self-test, run at the start of the Hell Keep level, comes out a bit further off than the pass mark. Might just be that particular view needing more samples to settle, or a real math bias worth chasing.
  Kind: investigate.
  Source: in-session-2026-07-17 (found while completing DOOM-0122 on the RX 6600).

- ✅ [DOOM-0197] **Extend build-ahead frame overlap to the RT/Ultra path.**
  DOOM-0074 shipped CPU/GPU build-ahead for the raster (Solid) path only; a traced frame still serializes (builds after the fence). RT is currently GPU-bound on the megakernel (~10.6ms) + SVGF denoise (~6.9ms), so the CPU build (~3ms reheight) overlapping would only trim a little today, but it becomes worth it once DOOM-0090 (megakernel occupancy) lands. Doing it needs more than the raster cut: the RT frame's per-frame GPU-read resources — sprWorldBuf (sprite BLAS input), tlasInstBuf, the sprite BLAS + world-BLAS refit, and the emitter buffers (emitBuf/emitSecBuf, GPU-read by the megakernel) — must be double-buffered per in-flight slot, and the BLAS/TLAS refit must target the active slot's vbuf. The SVGF denoiser history is intentionally temporal (reprojected each frame) so it must NOT be naively double-buffered — the reprojection already tolerates the 1-frame latency. Sequence AFTER DOOM-0090. Defer until then.
  **Layman:** Give the ray-traced Ultra view the same CPU/GPU overlap speedup the Solid view just got.
  Kind: perf.
  Source: in-session-2026-07-17 DOOM-0074 follow-up.
  Resolved 2026-07-27: Ultra RT went 41 -> 48 FPS (present-total 24.1 -> 20.5 ms)
  on the RX 6600 at E1M1, 50% render scale, WITH the HD material set loaded.

  Sequenced BEFORE DOOM-0090, against this bullet's own "defer until then". The
  deferral reasoned from a frame where the fog cost +8.4 ms; DOOM-0276 removed
  7.4 ms of that, so the ~3.6 ms CPU build stopped being noise and became 15% of
  the frame. The user asked for it next.

  The bullet's scope estimate was too pessimistic, and measuring first is what
  showed it. It listed sprWorldBuf, tlasInstBuf, the sprite BLAS, the world-BLAS
  refit and emitBuf/emitSecBuf as all needing per-slot copies. In fact:
  - The AS work (tlasInstBuf, sprite BLAS, world-BLAS refit) is written during
  RECORDING, which already happens after the fence, so it never needed slotting
  at all. Every site re-derives BufferAddress() at record time, so slot aliasing
  is transparent to it.
  - The traced CPU build measures 3.57 ms and the moving-sector RE-HEIGHT is 3.10
  of it -- 87%. Re-height writes g.vbufMapped, which DOOM-0074 ALREADY
  double-buffers. So the large majority of the win needed no new double-buffering
  whatsoever.

  Shipped the seam rather than the fleet: BuildFrameReheight() split out of
  BuildFrameInputs and run before the fence on traced frames; the sprite + emitter
  halves (~0.45 ms, writing the still-single-copy sprWorldBuf / emitBuf /
  emitSecBuf) stay behind it. Raster is untouched -- it still runs the whole build
  ahead. A just-toggled frame still runs everything after the fence.

  Safety is DOOM-0074's argument unchanged: slot[frameSlot] was last read two
  frames ago and that frame completed before the previous one was submitted. The
  megakernel reads vertices through pc.vertsAddr = BufferAddress(g.vbuf), which
  points at the OTHER slot while this one is written.

  One deliberate behaviour change, called out in a comment: the re-height now runs
  BEFORE the emitter rebuild instead of after, so a switch that changed texture is
  picked up by BuildStaticEmitterSet on the same frame rather than the next. One
  frame earlier, reading current heights instead of stale ones -- strictly better,
  but it makes the old "last frame RB_UpdateMeshHeights saw..." comment untrue in
  RT, so that is now stated where it matters.

  Also fixed while here: the profiler's own arithmetic. The pre-fence re-height sat
  outside BuildFrameInputs' timing span, so [cpu_build] printed a total SMALLER
  than one of its own parts and ~3 ms of present-total belonged to no bucket. The
  re-height now adds to the build total only when it is called standalone.

  Gates, all with HD art loaded: -shotcompare vs a worktree build of 8b41786,
  mae 0.006/255 against a same-build noise floor of 0.002 -- the denoiser's own
  residue. -rtverify PASS, rel-MSE 0.0988%, matching the historical HD figure
  recorded on DOOM-0074. make + make test green.

  METHOD WARNING, and it cost a full set of measurements: HD materials resolve
  relative to cwd, so a run that does not export DOOMASSETDIR silently renders
  Ultra with PALETTED art. The first pass of these numbers (43 -> 53 FPS) was taken
  that way and had to be discarded. The user caught it from a screenshot; no gate
  did. Logged as DOOM-0283.

  Remaining: ~0.45 ms still behind the fence (sprites + emitter refill), which
  needs sprWorldBuf / emitBuf / emitSecBuf slotted -- and those are read by
  RunGiBake and RB_RtVerify too, so it is a wider change for an eighth of the
  prize. Left undone deliberately.

- 📋 [DOOM-0198] **Vulkan validation: renderpass/framebuffer subpass-dependency incompatibility in the raster overlay pass.**
  With Vulkan validation layers on, the Solid raster path logs VUID-VkRenderPassBeginInfo-renderPass-00904 / VUID-vkCmdDraw-renderPass-02684: a render pass is begun with a framebuffer (and pipeline) created for a subpass-dependency-INCOMPATIBLE render pass — srcAccessMask TRANSFER_WRITE vs 0, srcStageMask ALL_TRANSFER vs EARLY_FRAGMENT/COLOR_ATTACHMENT_OUTPUT (renderPass 0xf vs 0xc). Pre-existing (confirmed: the DOOM-0074 diff touches no render-pass/framebuffer/pipeline creation code; the warning is a static setup mismatch, not a runtime one). Likely the overlay/composite or rtOverlay LOAD-variant pass whose pDependencies differ from the base renderPass its framebuffer/pipeline were built against, while still being format-compatible. Renders correctly (image is right), but the passes should be made subpass-dependency-compatible (or the framebuffer/pipeline rebuilt against the matching pass) to clear the warning. Low priority — cosmetic validation noise, no visible effect.
  **Layman:** The graphics debug layer flags a mismatch in how one of the 2D-overlay render steps is set up; harmless in practice but should be cleaned up.
  Kind: fix.
  Source: in-session-2026-07-17 (surfaced during DOOM-0074 hardware verify).

- 📋 [DOOM-0199] **In-game Video / RT settings menu to surface the hidden renderer toggles.**
  The RT/raster renderer has grown a pile of runtime toggles that today are ALL undocumented single-key shortcuts or ~/.doomrc hand-edits, undiscoverable to a normal player: render tier (Classic/Solid/Ultra), rt_view raster-vs-denoised (~), de-tile ] , filth [ , wet-liquid ' , SSAO, render-scale, flashlight (F). Proposal: a new 'Video' / 'Ultra (RT)' submenu under the existing Options menu (reuse the m_menu.c machinery + gamepad nav already used by DOOM-0060's game-select picker), with on/off rows + a render-scale and FOV slider, persisted through the normal default-config path. Value: makes the whole DOOM-0042/0181/0183/L2 feature set actually reachable and configurable by the user, and gives a single discoverable home for future toggles. Suggestion only — scope/needs design + a /cold-eyes spec pass before implementing.
  **Layman:** Add a proper Options screen for the new graphics features, so you can turn them on/off from a menu instead of memorising secret keyboard keys or hand-editing a config file.
  Kind: ux.
  Source: in-session-2026-07-18 (CC suggestion, for user review).
  Approved by user 2026-07-18 for implementation.

- 📋 [DOOM-0200] **Optional mouse / free look (vertical aim) for the Solid and Ultra 3D views.**
  Classic DOOM has no vertical aim (auto-aim only). With the true-3D Solid/Ultra renderers a look-up/down freelook becomes meaningful and is a common modern-source-port comfort feature. Proposal: an OPTIONAL, default-OFF freelook (pitch) bound to the mouse (and a right-stick option on gamepad), gated by a menu toggle (pairs with the settings-menu suggestion above). Must respect the aesthetic north-star ([[rt-aesthetic-north-star]]): keep the classic feel — so default off, and auto-aim stays authoritative for weapon targeting even when the camera pitches. Open questions to settle in design: does pitch affect projectile/hitscan aim or camera-only; sprite-billboard behaviour under pitch (relates to DOOM-0100/0080). Suggestion only.
  **Layman:** Now that the world is truly 3D, optionally let the player look up and down with the mouse — but off by default so classic DOOM still feels like classic DOOM.
  Kind: feature.
  Source: in-session-2026-07-18 (CC suggestion, for user review).
  Approved by user 2026-07-18 for implementation.

- 📋 [DOOM-0201] **Distance-attenuated, camera-relative 3D positional audio in the 3D views.**
  Audio today is SDL_mixer with stereo panning + a rough distance rolloff driven by the classic 2D S_sound.c model (see [[doom-ants-audio-architecture]]). With a true-3D camera we can drive panning + attenuation from the actual 3D listener orientation (yaw, and pitch if freelook lands) and 3D source position for a more convincing spatial mix — still via Mix_SetPanning / Mix_Volume on the same device (never a second device / custom mixer — that regressed on Windows before). Cheap-wins framing: better directional panning + smoother distance curve first; HRTF / occlusion (sound muffled through walls) is a bigger, later step. Suggestion only — verify how much of S_UpdateSounds already carries the needed geometry before scoping.
  **Layman:** Make sounds feel like they come from the right direction and fade with distance in the 3D modes — footsteps behind you sound behind you.
  Kind: feature.
  Source: in-session-2026-07-18 (CC suggestion, for user review).
  Approved by user 2026-07-18 for implementation.

- 📋 [DOOM-0204] **Palette-lock brightness match so dark DOOM textures don't blow out to a bright HD hero.**
  Found during DOOM-0042 T17 coverage batch 2. scripts/stage_hero.py
  tint_to_doom() does `tinted = HD_luma * (doom_mean / doom_lum)` — it
  preserves the HD SOURCE luminance and only matches DOOM hue. For a bright
  CC0 source over a very dark DOOM texture the hero blows out (CEIL5_2 =
  near-black in DOOM came out bright cream-yellow, rejected from the batch;
  left paletted). Fix: optional brightness match — scale the tinted result so
  its mean luma ~= DOOM mean luma (e.g. multiply by doom_lum / hd_mean_luma,
  clamp to avoid crushing). Would let dark ceilings/floors take an HD hero
  instead of falling back to paletted. Does not touch the 17 already-staged
  heroes (already on disk). Re-test CEIL5_2 (concrete_wall_003) once added.
  **Layman:** When we recolour a fancy HD texture to match a DOOM texture's colour, very dark DOOM textures (like the near-black CEIL5_2 ceiling) come out far too bright, because the recolour copies the DOOM hue but keeps the HD photo's brightness. Add an option to also match the DOOM texture's brightness so dark surfaces stay dark.
  Kind: enhancement.
  Source: in-session-2026-07-18.

- ✅ [DOOM-0205] **Render Effects submenu — visible on/off state for every render toggle.**
  Motivated by a false 'regression' report: the user saw the dirt (filth) layer 'gone' and nukage 'not glowing', but the real cause was rt_filth=0 / rt_wet=0 persisted in ~/.doomrc with no visible indication of the toggle state. The hotkeys ([ filth, ] de-tile, ' wet, F flashlight, \ profiler) were unlabelled and invisible. This adds a Render Effects submenu off the existing Renderer menu (m_menu.c), one row per toggle showing its live value (On/Off, or de-tile Off/2-tap/4-tap). Each row flips the same rb_* var its hotkey does — the same var bound in m_misc.c defaults[] — so menu, hotkey and persisted config stay in lockstep. Implemented in the classic bitmap-font menu style (works in all renderer tiers); the visual restyle for 3D modes is DOOM-0206. Build + make test green; engine boots + -shotcompare gate PASS. Menu navigation is Wayland-input-gated so on-screen visual check is the user's (which serves the screenshot-proof goal).
  **Layman:** A settings screen (Options → Renderer → Render Effects) that lists each graphics toggle — flashlight, SSAO, de-tile, filth/grime, wet liquid, profiler — and shows whether it's on or off, so you can see (and screenshot) exactly what's enabled instead of guessing at hidden hotkeys.
  Kind: enhancement.
  Source: user-request-2026-07-18.
  Resolved (2026-08-04): implemented in m_menu.c (the submenu entry, its per-toggle rows, M_DrawEffectsMenu and the handler) and now CONFIRMED ON SCREEN, which was the missing half -- the code had been present for a fortnight with nothing able to photograph it.
  dev-shots/M-effects-ultra.png (Ultra RT) and dev-shots/N-effects-solid.png (Solid) show all seven rows with their live state beside each: Flashlight OFF, SSAO ON, De-tile 4-TAP, Filth/Grime ON, Wet Liquid ON, Volumetric Fog MED, Profiler OFF. That is exactly the bullet's ask -- see what is enabled instead of guessing at hidden hotkeys -- and it is the direct answer to the false regression report that motivated the item, where rt_filth=0 / rt_wet=0 sat invisible in ~/.doomrc.
  The same toggles also appear in the consolidated DOOM-0206 Video menu (dev-shots/O-video-ultra.png) under an EFFECTS group, so the state is reachable from either route. CHANGELOG entry added under Added.

- ✅ [DOOM-0206] **Redesign the in-game menu for Solid/Ultra (cleaner font, HUD-aware layout); keep Classic as-is.**
  User request (2026-07-18): the latest Steam DOOM 1+2 re-release has a completely different, cleaner menu; for Solid/Ultra the user is happy for a redesign to make it more usable. Font + colour are the implementer's choice (user withdrew the 'Arial' suggestion: 'use a font that you think best suits the game' + 'a colour that you think is best for readability'). Two concrete asks: (1) a cleaner menu look for the 3D tiers only — Classic keeps the authentic bitmap menu; (2) the in-game menu must NOT overlap the bottom HUD/status bar (currently reads as messy/unprofessional). NEEDS DESIGN (brainstorm) before implementation — the load-bearing decision is FONT RENDERING: the 1997 engine only has bitmap fonts (the small STCFN set + big red menu-graphic lumps); a crisp scalable 'modern' look implies either a bundled higher-res bitmap font atlas or a real glyph renderer in the Vulkan backend. Scope the font approach, the per-tier menu switch (Classic vs 3D), and HUD-aware placement in a spec/brainstorm first. Depends on / supersedes the classic-style rendering of DOOM-0205 for the 3D tiers (content/handlers reused).
  **Layman:** Give the 3D modes (Solid and Ultra) a nicer, more modern-looking menu — cleaner text and a layout that doesn't sit on top of the bottom status bar (HUD), which looks messy. The original Classic renderer keeps its authentic 1997 menu.
  Kind: feature.
  Source: user-request-2026-07-18.
  Progress 2026-07-19: L1–L6 all IMPLEMENTED, reviewed, and pushed (subagent-driven, 7 tasks). Crisp display-res menu for Solid/Ultra (vendored stb_truetype + bundled Oxanium OFL font, glyph pipeline in r_vulkan.cpp), consolidated Video menu with all render toggles + a new Ray Tracing row (6↔0) + tier routing, itemOn-derived HUD-safe scrolling, dimmed backdrop behind the crisp menu, and the two shared Classic fixes (HUD-safe whole-menu shift + uniform Options row font, titles/banners kept). Spec cold-eyed to convergence (6 further loops) incl. the user's title-exemption decision. Review loop caught 6 real bugs pre-merge (text-queue GPU-freeze, 2 scroll-clamp overflows, INV-7 value-column gap, a cross-task dim-over-menu compositing bug). Build + make test GREEN. NOT yet shipped: every menu VISUAL is unverified on hardware (headless capture is black + Wayland blocks input injection here) — awaiting user play-test sign-off before flipping to shipped + changelog. Note: -rtverify/-shotcompare gates currently red for a PRE-EXISTING reason (base==HEAD, DOOM-0208), not this work.
  Resolved 2026-07-21 (v2): shipped. v2 extended the crisp display-res skin from just the Video menu to every row-list menu in the 3D tiers (generic M_DrawCrispMenu + crispMenus[] registry), added the real M_SKULL1 cursor and real M_DOOM logo (RGBA cursor pipeline reusing the text pipeline layout), spread/sized/brightened the skull to the text, and brightened the Game Select screen skull to match. Final piece: the Classic-tier main menu, which mixed big-red graphic lumps with a tiny HUD-font "Game Select" row, now draws all items in one uniform paletted HUD font at 2x ("medium") via the new V_DrawPatchScaled + M_WriteTextScaled (Classic is the 1997 software renderer and cannot reach the Vulkan crisp font — the HUD font is the only full software alphabet). User signed off: Solid/Ultra menus "all great"; chose to keep the Classic medium size. Commits across the session culminating in a206f85.

- 📋 [DOOM-0207] **Player casts a ray-traced shadow in the Ultra renderer.**
  The player has no visible body/shadow in Ultra RT. Give the player a shadow-casting proxy (capsule or the player sprite billboard) in the TLAS so RT shadow rays hit it and the player casts shadows from point lights / flashlight / sun, matching how enemies and props already occlude light. Scope: Ultra (RB_RT3D) only. Relates to the flashlight offset work (a co-located light casts no visible shadow — DOOM-0.. flashlight fix) and enemy/prop shadow casting already in the TLAS.
  **Layman:** In the ray-traced view, the player should throw a shadow on the floor and walls like everything else does — right now they don't.
  Kind: feature.
  Source: user-request-2026-07-19.

- 📋 [DOOM-0209] **Decorative light-source sprites (candelabra, candle, torches) don't emit light in the Solid/Ultra renderers.**
  Found in play-test (user, 2026-07-20, Image #9): the two gold candelabras
  flanking a doorway have lit flames but cast NO light in the 3D tiers — the
  room around them isn't lit by them. Classic DOOM's decorative light-source
  Things (candelabra, candle, the tall/short techno floor lamps, the red/green
  torches, burning barrel, etc.) are drawn as fullbright flame sprites and read
  as light sources, but our RT/Solid emitter system derives emitters from
  WALL/FLAT materials only (emis::derive_material_le — the DOOM-0082 peak-region
  fullbright gate on textures/flats), so these sprite props never enter the
  emitter set and stay dark.

  Fix direction: feed decorative light-source sprite Things into the emitter
  system — likely a curated set of DOOM thing types (Candelabra, Candle, the
  lamp/torch/fire decorations) mapped to a warm point-light Le (colour + faint
  intensity), placed at the flame's world position (top of the sprite, not the
  base). Reuse the static-emitter cache path (these don't move) — see the
  DOOM-0084 static/sprite emitter split + DOOM-0119 cull. Tune intensity/colour
  with the user (candles = faint, torches = stronger). Relates to DOOM-0082
  (switch/lamp emitters), DOOM-0083 (forced-Le liquids/lava). Needs its own
  brainstorm -> spec -> cold-eyes cycle before implementing.
  **Layman:** Lit props like candelabras and torches glow in real life but stay dark in the 3D renderer — they should cast a warm light like the real DOOM light sources they are.
  Kind: enhancement.
  Source: user-request-2026-07-20.

- 📋 [DOOM-0211] **Classic-tier menu font looks blocky — give Classic a nicer uniform menu font.**
  **SCOPE WIDENED by user 2026-07-26:** *"I think we need to make the Classic renderer rather use
  the new menu we have. The Classic menu just doesn't look right the way it is. Let's just make
  them all use the same menu."* So the goal is no longer "a nicer font for Classic" — it is **one
  menu shared by all three tiers**, which means option (a) below, not (b) or (c). The layout is
  already shared (DOOM-0206 gave every tier the same rows, sizes and HUD-safe placement); the only
  thing that still differs is the glyph rendering.
  **The load-bearing constraint, verified 2026-07-26:** `Classic_Present()` is literally
  `{ I_FinishUpdate(); }` (`r_backend.c`) — the software renderer hands its 8-bit framebuffer
  straight to SDL and never enters the Vulkan backend, where the crisp text lives (`FlushMenuText`,
  `g.textVerts`). Sharing one menu therefore means routing Classic's finished frame through the
  Vulkan present path: upload the paletted buffer as a texture, blit it, then draw the menu with
  the same code the other tiers use. The game view's pixels are unchanged by that (it is the same
  buffer, same palette) — only the overlay changes.
  **The risk to weigh before building: Classic is the no-Vulkan fallback tier.** It currently runs
  on a machine with no working Vulkan, and the headless test path depends on that. Routing it
  through Vulkan would give the fallback a dependency on the thing it falls back FROM. Recommended
  shape: share the menu whenever Vulkan is up, and keep the bitmap menu strictly as the
  no-Vulkan path — accepting that the two skins then still both exist, but only one is ever seen
  on a working install. Also re-bless `-shotcompare` if any golden captures a menu.
  **Needs a short spec before implementation** (house rule: any design doc goes through
  `/cold-eyes` first). Kind changes from enhancement to feature at that point.
  Follow-up to DOOM-0206. The Classic main menu draws all items in the paletted HUD font at 2x (V_DrawPatchScaled / M_WriteTextScaled); nearest-neighbour doubling of the small STCFN bitmap font reads as blocky. User accepted it for now (2026-07-21) but wants a nicer look later. Root constraint: Classic = Classic_Present -> I_FinishUpdate (the 1997 software renderer), which never enters the Vulkan backend, so the crisp Oxanium font used by Solid/Ultra (FlushMenuText) is unavailable there. Options to scope: (a) route the Classic menu overlay through the Vulkan crisp-text path; (b) bundle/bake a higher-res paletted bitmap menu font for the software path; (c) tune the scale/spacing (1x for crispness vs 2x for size) as a cheap partial. Needs a small brainstorm before implementing.
  **Layman:** In the original Classic mode, the menu items all share one size now, but the font looks chunky/blocky. Make it look nicer while staying consistent.
  Kind: enhancement.
  Source: user-request-2026-07-21.

- ✅ [DOOM-0212] **Harden W_AddFile/W_Reload: validate the WAD directory against the real file size.**
  w_wad.c: numlumps*sizeof(filelump_t) truncated into int and read() returns ignored -> a crafted numlumps overflowed the size and yielded an OOB/uninitialised lump directory. Now numlumps/infotableofs are validated against filelength() and the directory read is length-checked. Verified: real doom.wad still loads.
  **Layman:** A hand-made/corrupt WAD could previously make DOOM read random memory while loading; now a bad directory is rejected cleanly.
  Kind: security.
  Source: indie-review+audit 2026-07-23 (wad-data-misc lane, HIGH).

- ✅ [DOOM-0213] **Bound mus2mid MUS->MIDI reads by the real lump length, not the header's own claim.**
  mus2mid() took no length and trusted scorestart+scorelength from the header. Threaded W_LumpLength through I_RegisterSong; muslen is now min(header, real) with an 8-byte minimum. Regression test tests/mus2mid_test.cpp added.
  **Layman:** A crafted in-WAD music track could make DOOM read up to ~64KB past the end of the lump; now reads are capped at the true lump size.
  Kind: security.
  Source: indie-review 2026-07-23 (wad-data-misc + platform-io, HIGH, corroborated).

- ✅ [DOOM-0214] **Clamp netgame packet numtics against BACKUPTICS in i_net PacketGet.**
  i_net.c: numtics is an attacker byte (0-255) copied into cmds[BACKUPTICS=12] before checksum validation -> heap/stack OOB. Now drops the packet when numtics > BACKUPTICS.
  **Layman:** A malformed network packet could overflow an internal array before any validation; now oversized packets are dropped.
  Kind: security.
  Source: indie-review 2026-07-23 (platform-io, HIGH).

- ✅ [DOOM-0215] **Clamp netconsole (packet player field) to MAXPLAYERS in d_net GetPackets.**
  d_net.c: netconsole = player & ~PL_DRONE (0-127) indexed MAXPLAYERS-sized arrays (playeringame, nodeforplayer, netcmds) -> OOB write. Now skips packets naming a player >= MAXPLAYERS.
  **Layman:** A malformed network packet could write out of bounds using a bogus player number; now such packets are skipped.
  Kind: security.
  Source: indie-review 2026-07-23 (platform-io, HIGH).

- ✅ [DOOM-0216] **Clamp menu value-name indices (showMessages/fpsCorner) against a hand-edited config.**
  m_menu.c: msgValueNames[2]/fpsPosNames[4] were indexed by config ints M_LoadDefaults never clamps -> OOB char* deref on menu open. Clamped inline, matching the existing detileNames idiom.
  **Layman:** Editing ~/.doomrc to an out-of-range value could crash the game when opening the menu; the value is now clamped.
  Kind: fix.
  Source: indie-review 2026-07-23 (menu-hud, MEDIUM).

- ✅ [DOOM-0217] **Drain the GPU before RB_Vulkan_BuildLevel frees/recreates live buffers.**
  r_vulkan.cpp:7107 RB_Vulkan_BuildLevel destroyed vertex/sky/emitter buffers with no vkDeviceWaitIdle, unlike the mode-switch/recreate/shutdown paths -> latent GPU use-after-free of g.vbuf. Added a drain at function entry.
  **Layman:** Loading a new level could, in rare timing, free graphics memory the GPU was still using; now the GPU is drained first.
  Kind: fix.
  Source: indie-review 2026-07-23 (vulkan-rt-render, MEDIUM).

- ✅ [DOOM-0218] **Guard RB_BuildPSprites against a negative sprite lump index.**
  r_mesh.c:1292 indexed spritewidth/sprite_h/... with an unguarded lump that can be -1, while the twin RB_BuildSprites guards it. Added the matching `if (lump < 0) continue;`.
  **Layman:** A missing weapon-sprite frame could read out of bounds; now it's skipped, matching the world-sprite path.
  Kind: fix.
  Source: indie-review 2026-07-23 (vulkan-mesh-assets, MEDIUM).

- ✅ [DOOM-0219] **Fix -warp E/M bounds so `-warp 3` on a non-commercial IWAD can't NULL-deref.**
  d_main.c: the episode+map form read myargv[p+2] under only a p<myargc-1 guard -> argv[argc]==NULL deref. Split into p<myargc-1 (commercial) and p<myargc-2 (E/M). Verified: `-warp 3` no longer crashes.
  **Layman:** Typing `doom -warp 3` on DOOM 1 crashed at startup; now it's handled gracefully.
  Kind: fix.
  Source: indie-review 2026-07-23 (platform-io, MEDIUM).

- ✅ [DOOM-0220] **Fix three low-severity bounds nits: donut NULL-deref, sfx off-by-one, basedefault snprintf.**
  p_spec.c:1192 EV_DoDonut derefs a possibly-NULL getNextSector result (now guarded like every other caller); s_sound.c:294 `sfx_id > NUMSFX` -> `>= NUMSFX` (S_sfx[NUMSFX] is one past); d_main.c basedefault sprintf($HOME) -> snprintf.
  **Layman:** Three small safety tidy-ups: a malformed WAD donut, a sound-index edge, and a long $HOME path can no longer misbehave.
  Kind: fix.
  Source: indie-review+audit 2026-07-23 (playsim/platform-io, LOW).

- 📋 [DOOM-0221] **Bound UploadAtlas WAD-derived material count / tile dimensions before GPU allocation.**
  r_vulkan.cpp:4604 UploadAtlas feeds WAD material count n and per-tile w/h into vector resizes, staging sizes, descriptor counts and VkImage extents with no bounds check -> bad_alloc / I_Error abort (DoS, not corruption; all arithmetic is 64-bit). DOOM-0093-adjacent.
  **Layman:** A corrupt WAD can make the Ultra renderer try a huge allocation and abort; validate sizes for a clean error instead.
  Kind: security.
  Source: indie-review 2026-07-23 (vulkan-rt-core, MEDIUM).

- 📋 [DOOM-0222] **Bound G_ReadDemoTiccmd against the demo lump end (attract-mode demo from a PWAD).**
  g_game.c:1577 G_ReadDemoTiccmd never bounds demo_p against the lump end; a marker-less DEMO lump auto-played in attract mode reads past the buffer.
  **Layman:** A malformed demo embedded in a custom WAD (auto-played on the title screen) can run off the end of its buffer.
  Kind: fix.
  Source: indie-review 2026-07-23 (wad-data-misc, MEDIUM).
  Progress (2026-07-26): DOOM-0254 added a 13-byte demo-lump length check before G_DoPlayDemo parses the header. The per-tic G_ReadDemoTiccmd bound this item names is still open.

- 📋 [DOOM-0223] **Hoist emitSecMapped out of the BuildRasterPointLights double loop (avoid WC readback).**
  r_vulkan.cpp:6548 re-reads write-combined emitSecMapped[e] inside the per-subsector x per-emitter loop though it's loop-invariant; DOOM-0170 warns against WC readback. Hoist es[staticN..emitN) to RAM once.
  **Layman:** A small performance nit in the light build loop; reads slow write-combined GPU memory repeatedly.
  Kind: perf.
  Source: indie-review 2026-07-23 (vulkan-rt-render, LOW).

- 📋 [DOOM-0224] **Push all 31 push-constant floats in RecordRtOverlay (probe/tri/light lane undefined).**
  r_vulkan.cpp:7836 pushes only 24 of the layout's 31 floats for the RT weapon draw; the raster draw pushes all 31. Safe only if the psprite shader never reads the probe lane -- push the full 31 zeroed.
  **Layman:** A defensive fix in the RT weapon overlay; some GPU constants are left undefined but happen to be unread today.
  Kind: fix.
  Source: indie-review 2026-07-23 (vulkan-rt-render, LOW).

- 📋 [DOOM-0225] **Prefer a present-capable device that also has bindless in PickPhysicalAndDevice.**
  r_vulkan.cpp:1181 picks the first present-capable device then aborts if it lacks bindless, even when another present device (which RB_VulkanProbe accepts) has it.
  **Layman:** On rare multi-GPU systems the renderer may pick a device that lacks a needed feature and abort.
  Kind: fix.
  Source: indie-review 2026-07-23 (vulkan-rt-core, LOW).

- 📋 [DOOM-0226] **Avoid swapchain format fallback to formats[0] that may double-encode sRGB.**
  r_vulkan.cpp:1357 swapchain format fallback to formats[0] could accept an _SRGB target the palette path double-encodes.
  **Layman:** A cosmetic colour-accuracy edge on unusual GPUs; unreachable on the target hardware.
  Kind: fix.
  Source: indie-review 2026-07-23 (vulkan-rt-core, LOW).

- 📋 [DOOM-0227] **Key R_InitLightTables zlight off centerxfrac_nonwide for consistent widescreen distance-light.**
  r_main.c:633 R_InitLightTables keys zlight off wide SCREENWIDTH/2 while DOOM-0147 moved world projection to centerxfrac_nonwide. Cosmetic, widescreen-only, zero-diff at 4:3.
  **Layman:** On a true-widescreen display, floor/ceiling distance-darkening fades at a slightly different rate than walls.
  Kind: fix.
  Source: indie-review 2026-07-23 (sw-renderer, LOW).

- 📋 [DOOM-0228] **Bound r_mesh blit_tile against crafted flat/patch lump sizes (4096 assumption + post offsets).**
  r_mesh.c blit_tile assumes flats are exactly 4096 bytes and trusts patch post offsets/lengths -- vanilla-parity but inconsistent with the file's own hardened paths. DOOM-0093-adjacent.
  **Layman:** A crafted WAD with odd-sized graphics could make the mesh builder read out of bounds.
  Kind: security.
  Source: indie-review 2026-07-23 (vulkan-mesh-assets, LOW).

- 📋 [DOOM-0229] **Widen rb_image box-filter accumulator to avoid overflow on pathological image sizes.**
  rb_image.c:58 box-filter downscale uses unsigned acc[4], overflowing for >16M source texels per output texel.
  **Layman:** A cosmetic overflow only on absurdly large source images (never real material art).
  Kind: fix.
  Source: indie-review 2026-07-23 (vulkan-mesh-assets, LOW).

- ✅ [DOOM-0230] **Add RANGECHECK x/y to V_DrawPatchScaled and clip Y in M_WriteTextScaled.**
  v_video.c:375 V_DrawPatchScaled (new for DOOM-0206) omits the RANGECHECK every sibling primitive has; caller M_WriteTextScaled clips X but not Y -> latent OOB write.
  **Layman:** A defensive bounds guard on the new crisp-menu text drawing; safe with today's fixed content.
  Kind: fix.
  Source: indie-review 2026-07-23 (menu-hud, LOW).
  Progress (2026-07-26): DOOM-0254 added per-post extent validation (V_PostInBounds) to V_DrawPatch/Scaled/Flipped and F_DrawPatchCol, which closes the OOB-WRITE half. The x/y RANGECHECK on V_DrawPatchScaled and the Y clip in M_WriteTextScaled named here remain open (they interact with DOOM-0231's scale-2 label overflow).
  Resolved 2026-08-03 (code-quality-review sweep, re-found independently by the
  classic-renderer lane). Both halves are in: V_DrawPatchScaled gained the x/y
  RANGECHECK its siblings carry -- rejecting the patch and rate-limiting the
  report, matching V_DrawPatchGeneral's posture rather than aborting -- and
  M_WriteTextScaled now breaks on a row that would fall outside ORIGHEIGHT, the
  Y clip to go with the X clip it already had. RANGECHECK is defined
  (doomdef.h:75), so both guards are live in the shipping build, not just in a
  debug one. DOOM-0231's scale-2 label overflow is untouched and stays open; the
  interaction noted in the 2026-07-26 progress line is now one-sided, since an
  overflowing label is dropped with a stderr note instead of writing out of
  range. Build clean, 7 test suites pass.

- 📋 [DOOM-0231] **Keep Classic main-menu scale-2 labels within ORIGWIDTH (no mid-word truncation).**
  Classic main-menu labels drawn at scale 2 from x=97 can overrun ORIGWIDTH and truncate mid-word (Game Select / Load Game).
  **Layman:** Cosmetic: some Classic menu labels can run off the edge and truncate.
  Kind: ux.
  Source: indie-review 2026-07-23 (menu-hud, LOW).

- 📋 [DOOM-0232] **Harden d_main -wart bounds and FindResponseFile (ftell/fread/one-past-end).**
  d_main.c: -wart reads myargv[p+1]/[p+2] under only `if(p)`; FindResponseFile has unchecked ftell/fread, a one-past-end write, and fixed moreargs[20]/MAXARGVS[100]. Dev-only, hence LOW.
  **Layman:** Developer-only command-line paths that can misbehave with too few args or an odd response file.
  Kind: fix.
  Source: indie-review 2026-07-23 (platform-io, LOW).
  Progress (2026-07-26): DOOM-0254 bounded FindResponseFile's moreargs[20] and MAXARGVS argv rebuild, and snprintf'd the -playdemo name. The ftell/fread one-past-end handling and the -wart bounds named in this item are still open.

- 📋 [DOOM-0233] **Give I_StartSound an opaque handle -> channel map (avoid SDL channel aliasing).**
  i_sound.c returns the Mix_PlayChannel number as the DOOM handle; a recycled channel can let a stale handle re-pan/halt the wrong effect. Add a handle->channel indirection (Chocolate DOOM pattern).
  **Layman:** A rare audio glitch where a finished sound's controls could affect a different sound reusing its channel.
  Kind: fix.
  Source: indie-review 2026-07-23 (platform-io, LOW).

- 📋 [DOOM-0234] **Add a crafted-WAD / malformed-packet fuzz + regression harness for the parse boundaries.**
  Follow-up to the W_AddFile/mus2mid/i_net/d_net hardening: build crafted-WAD fixtures + a packet-parse harness so the untrusted-input guards get regression coverage beyond the mus2mid unit test.
  **Layman:** Automated tests that throw deliberately-broken WADs and packets at the game to prove the new guards hold.
  Kind: test.
  Source: indie-review 2026-07-23 (follow-up to DOOM-0212..0215).

- ✅ [DOOM-0237] **Cold-eyes docs sweep (2026-07-23): fix doc-vs-code drift across specs + standards.**
  Full /cold-eyes sweep of all 41 project docs (4 contracts, 12 standards, 3 ADRs, 13 specs, 5 research, 2 plans), 7 topic lanes, 2 loops (two-tier: haiku breadth -> Opus verify). 9 verified fixes, all doc-only, committed 7d10f81 + 4b977a4: renderer.md misc3/misc4 push-constant table corrected vs pathtrace.comp; DOOM-0008 renderer-seam description (extern-C entry points adapted by r_backend.c, not "one accessor"); DOOM-0027 currency note that DOOM-0147 made SCREENWIDTH runtime; DOOM-0206 status line + §4.1/§L2 v1->v2 supersession back-pointers; DOOM-0181 plan marked as-built/done; CLAUDE.md Windows scope + "stages" wording; packaging/README stale version example. 3 false positives logged to .ants_review_falsepos.jsonl (push-cadence vs global rule, cross-doc dedup vs private global file, misc5 stale-roadmap echo). Remaining substantive item — DOOM-0027 inline v_video.c line-citation drift — already tracked as DOOM-0150 (deferred doc-hygiene), not re-opened.
  **Layman:** A full "fresh eyes" review of the project's design docs and house rules, fixing places where a doc had drifted out of sync with the code or with another doc.
  Kind: doc-fix.
  Source: cold-eyes-2026-07-23.

- 📋 [DOOM-0238] **Faked volumetric lighting (god-rays + fog) in the rasterised "Original" view.**
  Companion to DOOM-0011 (RT volumetrics). The rasterised path (Solid/Ultra with RT
  off — the "Original" view) cannot march light through the air, so volumetrics must
  be FAKED in screen space. User direction 2026-07-23: "match the RT look as closely
  as raster allows" (the most ambitious of the three options offered) — i.e. both
  screen-space light shafts (radial/crepuscular scatter from the sky + bright
  on-screen sources) AND coloured height/area fog (green goo rooms, hell haze, floor
  pooling), chasing the DOOM-0011 RT look rather than settling for flat depth fog.
  Sequenced AFTER DOOM-0011 ships (user chose "RT first, fake follows"). Needs its
  own design spec → /cold-eyes → plan → implement. Distinct technique from DOOM-0011
  (screen-space post-process in the raster composite, not a traced march), so it does
  NOT share the RT push-constant lanes / SVGF path — it hooks the raster pipeline
  (DOOM-0170) instead. Scope note: Classic (1997 software renderer) is excluded, as
  in DOOM-0011.
  **Layman:** Give the non-ray-traced "Original" view (Solid and Ultra with ray tracing off) the same shafts-of-light-and-fog look as the ray-traced view, faked with cheap screen tricks since the rasteriser can't trace light through air.
  Kind: feature.
  Source: user-request-2026-07-23.

- 💭 [DOOM-0239] **Player-reactive fog swirl (the Silent Hill 2 "James influence" term).**
  Split out of the DOOM-0011 2026-07-25 Silent Hill fog amendment. SH2
  reverse-engineering (elishacloud/Silent-Hill-2-Enhancements #246) shows the
  fog carries a player "influence" value of 200.0, reduced to 10.0 in the
  Forest area — the fog is displaced around James as he moves.

  Deferred by the user 2026-07-25 with a good reason: DOOM is FIRST-PERSON,
  so there is no on-screen body for the fog to curl around and the player
  would never see the effect on themselves. Would only pay off if it were
  applied to MONSTERS instead (fog parting around a charging imp), which is
  a different and larger feature (per-mobj displacement in the fog march).

  Revisit only after DOOM-0011 L1c/L1d ship and the wisps have been judged
  on hardware.
  **Layman:** Fog that curls and thins around the player as they walk, the way it does around James in Silent Hill 2.
  Kind: feature.
  Source: in-session-2026-07-25.

- 📋 [DOOM-0248] **cppcheck cannot parse r_vulkan.cpp, so the largest file in the project gets ZERO static-analysis coverage.**
  The 2026-07-26 full audit reported parse_failures for r_vulkan.cpp (plus stb_image.h / stb_image_write.h). A file cppcheck cannot parse is absent from the findings, which reads as "clean" — the most dangerous kind of false negative, on the file holding the entire Vulkan renderer. The same C++23-frontend limitation makes the editor's clangd red on this file. Fix: feed cppcheck the real include paths / a compile_commands.json (the Makefile knows the SDL2 + Vulkan flags), or move it to clang-tidy which already resolves the compile DB.
  **Layman:** The automatic bug-scanner silently skips our biggest source file — it reports clean because it never looked.
  Kind: audit-fix.
  Source: debt-sweep-2026-07-26.
  Finding (2026-07-26): re-ran cppcheck by hand with -I/usr/include/SDL2. The missing include path is NOT the whole story -- it then fails with `internalAstError: AST broken, 'toDst' doesn't have a parent` at r_vulkan.cpp:4752 (a vkCmdPipelineBarrier call with a std::vector .data() argument). So cppcheck's C++ frontend cannot parse this TU regardless of includes; routing the file to clang-tidy is the realistic path to coverage.

- 📋 [DOOM-0249] **Decide a policy for the ~1000 cppcheck style findings on id Software's 1997 tree.**
  The full audit returned 2291 findings; ~1000 are cppcheck style on the original engine (316 staticLinkage, 219 variableScope, 93+68 constPointer, 64 unusedFunction). None were fixed in the debt sweep: mass-editing id's code violates the surgical-edit rule and would bury real signal in churn. Needs a deliberate policy — an .audit_allowlist.json baseline suppressing these rules on the pre-fork files so NEW findings stand out, versus a one-off cleanup sweep. Prefer the baseline.
  **Layman:** The bug-scanner raises a thousand minor style complaints about the original 1997 code, drowning out real problems.
  Kind: chore.
  Source: debt-sweep-2026-07-26.

- 📋 [DOOM-0250] **Review the 28 unbounded strcpy/strcat sites semgrep flags.**
  semgrep insecure-use-string-copy-fn/strcat across d_main.c(5), m_menu.c(5), rb_materials.h(4), g_game.c(3), hu_stuff.c(2), m_misc.c(2), w_wad.c(2), wi_stuff.c(2), i_sound.c, d_net.c, r_data.c, sndserv/wadread.c. Most copy fixed-size lump names between fixed-size buffers and are safe by construction, but rb_materials.h is OUR code parsing an on-disk sidecar CSV — that one is attacker-adjacent (DOOM-0042 materials.csv) and should be checked first. Triage each; convert the genuinely unbounded ones to snprintf.
  **Layman:** Old-style text-copying calls that don't check length — mostly harmless here, but worth a proper look.
  Kind: security.
  Source: debt-sweep-2026-07-26.

- 📋 [DOOM-0251] **pt_common.glsl carries forward-staged fog helpers that are dead until DOOM-0011 L2-L4.**
  fogPhaseHG() is defined but never called, and kFogAnisotropy, kFogPoolHeight, kSunDir, kGooTint, kHellTint and kTorchShaftStrength have zero references (pt_common.glsl:41-46,52-56). Verified by grep across shaders/. The comments tag them L2/L3/L4, so this is deliberate staging rather than an oversight — left in place. Fold each into the layer that uses it as DOOM-0011 lands, or drop them if the design moves on.
  **Layman:** Some shader code was written early for a feature that isn't built yet, so it currently does nothing.
  Kind: chore.
  Source: debt-sweep-2026-07-26.

- ✅ [DOOM-0263] **Fix the correctness and doc-drift findings from the 2026-07-26 audit + indie-review sweep.**
  CRITICAL: bake.comp traced its GI sample rays with cull mask 0xFF, so a ray
  that hit the sky backdrop (instance mask 0x04) or a sprite billboard (0x02) was
  committed and then decoded through pc.verts (the WORLD mesh buffer) — reading
  unrelated vertex data instead of taking the sky-radiance miss path, biasing the
  irradiance bake on every sky-exposed probe. Mask is now 0x01 (world only),
  matching the shadow-ray convention.
  Also: am_map.c's bounds scan used else-if, so a map whose vertexes are stored in
  descending order left the maxima at -MAXINT (garbage automap zoom range) and an
  empty VERTEXES lump underflowed the subtraction; r_draw.c's full-view fast path
  compared scaledviewwidth against a stale literal 320, so the view border was
  refilled on every frame since DOOM-0027; I_GetTime/I_GetTimeMS now use
  CLOCK_MONOTONIC, so an NTP or manual clock step no longer jumps game pacing;
  a rejected soundfont is now reported instead of silently yielding inaudible
  music; -shotcompare no longer overwrites a golden PNG that exists but fails to
  decode. Doc drift fixed in ADR 0001 (VMA/vulkan.hpp never adopted),
  docs/standards/renderer.md (misc3.w is SVGF frame parity, not reserved),
  DOOM-0016 (I_QrySongPlaying revived by DOOM-0165), DOOM-0042 (status said "no
  code written yet"), DOOM-0008 (R_VulkanBackend()/"placeholders"),
  docs/RELEASE_README.txt (still described 0.1.0 as the first playable release),
  plus two stale renderer comments (blob.frag's vertex stage and its DIRECT-alpha
  behaviour).
  **Layman:** The baked global-illumination pass was reading the sky as if it were a wall; several smaller bugs and six stale documentation claims are fixed too.
  Kind: fix.
  Source: audit+indie-review-2026-07-26.

- ✅ [DOOM-0266] **Add a Volumetric fog row to the Render Effects submenu.**
  The dial shipped ahead of its menu row. `rb_fog` (0 off / 1 Low / 2 Med /
  3 High) is live in `r_vulkan.cpp`, persisted as `rt_fog` in `m_misc.c`'s
  defaults, and cycled by the `;` key in `i_video.c` — but `EffectsMenu[]`
  in `m_menu.c` has six rows (flashlight, SSAO, de-tile, filth, wet,
  profiler) and no fog row, so the only ways to turn fog off are an
  unlabelled hotkey or hand-editing ~/.doomrc.
  Shipped 2026-07-27 (346d4b8). "Volumetric Fog" row with
  Off/Low/Med/High in BOTH the Effects submenu and the crisp Video menu,
  driving the same rb_fog the `;` hotkey and the rt_fog config row use —
  so menu, hotkey and ~/.doomrc stay in lockstep by construction rather
  than by discipline. Seven edits per the DOOM-0011 plan's L6 Step 2
  (enum in effects_e + videoitem_e, a row in both menuitem arrays, the
  M_DrawEffectsMenu label/value pair, the videoLabels[] entry, the
  M_VideoCrispValue case, M_ChangeFog + the fogNames[4][6] table, and the
  forward declaration without which the arrays do not compile). Answers
  the user's "is the fog setting in the menus yet?" — it was not.

  That is precisely the failure DOOM-0205 was created to fix: a toggle
  whose state is invisible, which last time produced a false "regression"
  report. Fog now makes it worse than the others, because it defaults to
  **on** (`rt_fog` default 1) rather than off.

  Scope: one row cloning the `ef_detile` 0..N pattern exactly (`ef_fog`
  entry, `M_ChangeFog` handler with `% 4`, a `fogNames[4][5]` label table
  matching `i_video.c`'s `fog_name[4]` = OFF/Low/Med/High, and the
  forward declaration — the menu edit set the DOOM-0011 plan warns is
  easy to leave one short). RT-engaged tiers only, like every other row.

  Relationship to DOOM-0011: its **L6** task already owns this work as
  part of the full volumetrics ship. Split out because the dial is
  user-facing *today* and the menu row should not wait on L1c/L1d/L2-L5.
  Whichever lands first, the other must not duplicate the row — if this
  ships alone, mark L6's menu step done rather than re-adding it.
  **Layman:** The fog has an on/off dial that works, but it's only reachable by a hidden key or by hand-editing a config file — this puts it in the settings screen next to the other graphics toggles, showing Off/Low/Med/High.
  Kind: ux.
  Source: user-request-2026-07-26.

- ✅ [DOOM-0267] **Solid/Ultra draw a solid wall where Classic shows an open secret (E1M1).**
  Found by user play-test 2026-07-26. Two screenshots at identical player state (98% health /
  8% armour / 38 bullets), Solid vs Classic: Classic draws an open doorway into the next room,
  Solid draws an unbroken wall. Ultra shows the same closed wall elsewhere in the same secret.
  Evidence: ~/Pictures/ClaudePaste/paste_20260726_212923_362_c067f0b7.png (Solid),
  paste_20260726_212936_805_83d2decf.png (Classic).
  **DOOM-0068 predicted this exact failure and deferred it:** "backface culling is off
  (VK_CULL_MODE_NONE), so the rare line textured on BOTH sides whose floors cross could show a
  phantom inverted quad; not observed, acceptable edge case, revisit if reported." Now reported.
  Mechanism (traced, not guessed). The 3D wall pipeline sets `rs.cullMode = VK_CULL_MODE_NONE`
  ("both wall faces visible; winding-agnostic"), so every wall quad is visible from BOTH sides.
  Classic is one-sided by construction: `R_StoreWallRange` re-derives per frame that a two-sided
  line needs NO lower texture when `worldlow <= worldbottom`, so from the far side the step is
  not drawn and you see through it. `RB_BuildLevelMesh` instead emits that step ONCE at load,
  from whichever sidedef carries the texture, and then shows it from both sides forever.
  DOOM-0068's `<=` lower gate widened the exposure: every flush two-sided line carrying a bottom
  texture now gets a zero-height placeholder that `RB_UpdateMeshHeights` grows with no sign
  check, so a quad can grow on the WRONG side of its line and stand up as a phantom wall. E1M1
  has 45 such lines. Concrete candidate at the reported spot: line 460 joins sector 87 (the
  tag-2 secret lift, special 9) to sector 58 at equal floor heights, with BROWNGRN as the bottom
  texture on sector 87's side only — so while the lift is down the mesh holds a real ~152-unit
  quad that Classic draws from the lift side and omits from the other.
  Candidate fixes to weigh (needs a decision, then a targeted test): (a) restore backface
  culling for wall quads and emit consistent winding — closest to Classic's semantics, but
  DOOM-0068 chose CULL_NONE deliberately, so check what breaks; (b) make the re-height pass
  sign-aware, collapsing a quad to zero height when its planes cross instead of letting it
  invert — the narrower change, and it directly matches `emit_wall`'s own load-time
  `if (topz < bottomz) return;` guard which has no runtime equivalent; (c) emit the step from
  both sidedefs and let each be one-sided. **(b) is the cheapest and most targeted — start
  there,** since the load-time and run-time paths disagreeing is a defect in its own right.
  Verify against Classic at the same spot in all three tiers, and re-run `-shotcompare` +
  `-rtverify`. Related: DOOM-0052, DOOM-0068 (the `<=` gate), DOOM-0142 (the opposite artifact,
  holes where a wall is missing) — do not conflate.
  **Layman:** In the 3D renderers a hidden passage in E1M1 looks like a plain wall, but the original renderer shows an open doorway. The 3D view is drawing the back of a wall panel that should only be visible from the other side.
  Kind: fix.
  Source: user-play-test-2026-07-26.
  Resolved 2026-07-27 (`8542d2b`), user-confirmed in Solid and Ultra. Cause: E1M1 line 458, the secret room's own doorway, carries its BROWNGRN midtexture on sidedef 628 (outside) only; sidedef 629 (inside) has none. Classic draws a two-sided midtexture PER SIDEDEF, so from inside it draws nothing and the doorway is open. RB_BuildLevelMesh emitted one quad over the whole 104..176 opening and, under VK_CULL_MODE_NONE, showed it from both sides. Fix: sidedef faces are now one-sided in both renderers, tested against the stored normal (winding-agnostic) -- mesh.frag discards a back-facing fragment, and pathtrace.comp skips a back-facing candidate in worldCandidateOpaque, which only the primary ray reaches, so occlusion/GI/light transport are untouched. MASKED mid-walls are deliberately INCLUDED: a real grate carries a midtexture on both sidedefs, so the seg walk emits two quads, one per side. Cosmetic only -- the user could always walk through -- but it reads as a dead end to anyone new. Two earlier fixes were wrong because they were reasoned from source rather than measured; the `/` diagnostic plus DOOM-0268's -warpto found the real line in one step.

- ✅ [DOOM-0270] **Zigzag wall slits render as criss-cross gaps in Solid, diagonal in Classic.**
  Found by user play-test 2026-07-27, screenshots at the same spot: Classic draws the wall's
  diagonal slots leaning one way; Solid draws them **crossed both ways**, a lattice the original
  never shows. Evidence: ~/Pictures/ClaudePaste/paste_20260727_075959_715_78b48b40.png (Classic)
  and paste_20260727_080036_021_a3695423.png (Solid).
  **Likely already fixed by DOOM-0267 (`8542d2b`) — verify before investigating further.** That
  change made sidedef faces one-sided, including two-sided MIDtextures. A criss-cross reads
  exactly like seeing each diagonal slot's face from the front *and* the mirrored face of the
  same recess from behind, which is the symptom DOOM-0267 removes. If it persists after that
  fix, the next suspects are (a) the mid quad's REPEAT tiling: `r_mesh.c` emits a mid over the
  full `max(floors)..min(ceilings)` opening with no clamp to `textureheight[]`, while Classic
  draws a two-sided midtexture exactly ONCE with no vertical tiling — so a short slot texture
  would repeat down the opening in 3D and not in Classic; and (b) the alpha test: Classic skips
  palette-index-0 posts per column, the mesh alpha-tests per texel.
  Re-test with `-warpto` (DOOM-0268) once headless capture lands, so this gets a golden.
  **Layman:** A wall with diagonal slots in it looks wrong in the 3D renderers — the slots appear crossed both ways instead of leaning one way like the original.
  Kind: fix.
  Source: user-play-test-2026-07-27.
  Resolved 2026-07-27 by DOOM-0267 (`8542d2b`), user-confirmed: "that is now working as it should". The criss-cross was exactly what the prediction said -- each diagonal recess drawn from the front AND its far face from behind, because sidedef faces were visible both ways. No separate fix needed; the REPEAT-tiling and alpha-test suspects listed here were not reached.

- 📋 [DOOM-0271] **Outdoor floor flats still read as an obvious repeating grid despite de-tiling.**
  **Layman:** The ground outside shows the same square tile over and over in a visible grid — the anti-repetition trick that fixed the walls is not doing its job on the floor.
  Kind: fix.
  Lanes: renderer, shaders.
  Source: user-play-test-2026-07-27.
  Reported alongside the DOOM-0011 ground-cloud work: with the walls
  now hazed, the eye lands on the floor and its repetition is obvious.
  Evidence: user screenshot, E1M1 outdoor courtyard, Ultra.
  Leading hypothesis, UNVERIFIED — kDetileWorldCell is 64.0
  (pathtrace.comp, "96->64" in its own comment), and a DOOM flat is
  exactly 64x64 world units. The de-tile variation grid is therefore
  EXACTLY commensurate with the flat's tiling period on every floor and
  ceiling, so each cell's stochastic offset lands on the same phase of
  the texture and the repetition survives. Walls escape this because
  wall textures are 64-256 wide and 128 tall, so the cell rarely lines
  up. Cheapest test: make the cell non-commensurate (80, 96 or 112) and
  look. That changes walls too, which the user has already approved, so
  measure the wall look before and after.
  Second hypothesis, also unverified and cheaper to rule out: de-tiling
  may simply be OFF in the user's config. rb_detile is the `]` key
  (off / 2-tap / 4-tap) and ~/.doomrc has silently held a toggle at 0
  before (see the menu/shotcompare config gotcha). Confirm the toggle
  state before touching any constant.
  Depends on nothing; DOOM-0181 shipped the de-tiler this refines.
  Update 2026-07-27: the user's VIDEO menu screenshot RULES OUT the
  config hypothesis — De-tile reads "4-tap", so de-tiling is on at full
  quality and the grid survives it. That leaves the commensurate-cell
  hypothesis as the only one standing: kDetileWorldCell = 64.0 is exactly
  a DOOM flat's 64x64 world period, so on every floor and ceiling each
  variation cell lands on the same phase of the texture. Cheapest test is
  still one constant (try 80, 96 or 112, none of which divides 64) plus a
  before/after look at the WALLS, which the user has already approved and
  which this would also change.

- ✅ [DOOM-0272] **Split the fog into two layers: a general aerial layer plus a short-range floor fog.**
  **Layman:** Add a second, thicker mist that hugs the floor and only shows up near you — so you wade through it without the far end of the level turning white.
  Kind: feature.
  Lanes: renderer, shaders.
  Source: user-request-2026-07-27.
  Design pinned by the user 2026-07-27, after signing off the single-layer
  fog: "for outside you will have the general fog and the floor fog.
  Outside, the floor fog can probably be thicker." And for interiors:
  rooms exposed to outdoors via a window or door get the floor fog,
  "with a much smaller distance to the camera setting than the general
  fog".
  Progress (2026-07-27): the OUTDOOR half is implemented as task L1e
  (6e3234b) — kFloorFogDensity/Pool/Range + floorFogDensity() in
  pt_common.glsl, a third addend in marchFog's sigma on the
  skyExposure-gated side, and a matching second closed form in
  skyFogOpticalDepth (without it the skyline steps ~37% against the walls
  beneath it). The sky form was checked against a numeric integral: 0.00%
  error, continuous through rd.z = 0. make + make test green, -rtverify
  PASS. Awaiting hardware play-test.
  The INDOOR half still waits on DOOM-0011 L1d (the seep), exactly as this
  bullet said it would: roofed air scales by kIndoorFogScale, so a sealed
  room shows 5% of the bank rather than a bank at your feet.
  Spec §4.3c went through a one-lane /cold-eyes first (1c059ed): 0C/2H/3M,
  all fixed — no build task, no constant values, a code block that
  disagreed with its own prose, the sky gap above, and an unamended INV-9.
  Progress (2026-07-27, second): the INDOOR half is in too, and it cost
  no new code. Both layers share the skyExposure gate, and DOOM-0011 L1d
  (f62f468) replaced that gate's flat indoor floor with the graded seep —
  so a room with a window now gets the floor fog at up to kSeepMax (0.5)
  of its outdoor strength, and a sealed room still gets kIndoorFogScale
  (0.05), exactly as §4.3c predicted when it put both layers on the same
  gate. Awaiting the play-test that covers both halves.
  The load-bearing new idea is that SECOND term's range. Today's fog is a
  pure medium: opacity only ever grows with distance, which is why
  thickening it to make your feet misty also turns the far ground white.
  The floor layer instead FADES OUT with distance from the camera - not
  physical, but exactly the game trick that makes ground mist readable.
  It is free to evaluate: marchFog already knows t along the ray.

  Sketch (spec DOOM-0011 4.3c owns the real version):
    sigma_floor(p) = kFloorFogDensity
                   * exp(-(p.z - baseZ) / kFloorFogPool)   // hugs the floor
                   * exp(-t / kFloorFogRange)              // NEAR the camera only
    sigma = sigma_general + sigma_floor
  kFloorFogPool << kFogPoolHeight (112) and kFloorFogRange << kFogMaxDist
  (2048). Outdoors gets a higher kFloorFogDensity than indoors.

  Depends on DOOM-0011 L1d (the outdoor-proximity seep) for the INDOOR
  half: nothing in the tree can yet tell "room with a window" from "room
  buried three doors deep", and the user was explicit at L1b that sealed
  interiors stay clear. The OUTDOOR half has no such dependency and can
  land first.

  Gate: this is a design change to a converged multi-file spec, so
  CLAUDE.md rule 14 puts /cold-eyes between the amendment and the code.

  Resolved (2026-07-27): shipped as L1e (6e3234b outdoor half, f62f468 seep) and
  signed off on hardware - "All looks good to me, I went through doorways and it
  all looks just fine and yes the fog does dissipate the further away from an
  opening to the outside." Both layers ride the same skyExposure gate, so the
  indoor half needed no extra code once L1d's seep graded that gate. The user's
  sentence also closes L1d Step 7's last untested clause (that the seep THINS
  with depth rather than being on/off) and DOOM-0276's one accepted cost (the
  doorway threshold, where the half-cell grid error lives). Known gap found in
  the same play-test, tracked separately as DOOM-0281: a wall that OPENS during
  play does not re-flood the seep field, so fog does not roll into a room that
  was sealed at level load.

- 📋 [DOOM-0273] **Solid tier: upscale the ORIGINAL textures and give them PBR/POM, keeping the 1993 art.**
  **Layman:** Same DOOM pictures you know, just sharper, with real bumpiness and depth — as opposed to Ultra, which swaps the art out entirely.
  Kind: feature.
  Lanes: renderer, assets.
  Source: user-request-2026-07-27.
  User redefinition of the three tiers, 2026-07-27 (see CLAUDE.md's
  "Render tiers" section, which this item implements the middle row of).
  Solid keeps the ORIGINAL 1993/97 art but upscaled, with PBR/POM and the
  rest added on top; Ultra swaps the art out for HD replacements. The
  user acknowledged this reverses an earlier position: "probably a
  contradiction to what I said earlier but we grow as we go along."

  Most of the machinery already exists and only needs re-pointing.
  DOOM-0042's materials.csv sidecar already has TWO row kinds: `hero`
  (curated CC0 replacement art) and `derive` (maps generated FROM the WAD
  texture by scripts/pbr_derive.py). That derive path IS this request -
  original art, upscaled, with normal/roughness/AO/height derived from
  it. So the tier split falls out as: derive rows -> Solid, hero rows ->
  Ultra. Check that assumption against the shipped loader before
  planning; it is inferred from the sidecar's shape, not yet verified in
  code.

  Related, do not duplicate: DOOM-0172 (Solid-tier art UPSCALING, the
  PS1/2-emulator-style smoothing - the resolution half of this item) and
  DOOM-0238 (faked god-rays/fog in the rasterised view - the "bells and
  whistles, cheaply" half). This item is the material half. Consider
  whether the three should become one bundle.

- 📋 [DOOM-0274] **Apply the widescreen toggle live, with no restart.**
  **Layman:** Switching widescreen on or off should take effect immediately instead of asking you to quit and relaunch.
  Kind: enhancement.
  Lanes: video, renderer.
  Source: user-request-2026-07-27.
  The menu currently admits it: the row reads "On (restart)".

  Why the restart exists (i_video.c, I_InitWidescreen): it sets
  SCREENWIDTH and WIDESCREENDELTA and MUST run before V_Init/R_Init,
  because both the screen buffers and the software renderer's projection
  tables are sized from SCREENWIDTH. Changing it live therefore means
  reallocating screens[], rebuilding R_Init's projection/view tables,
  resizing the SDL window + texture, and re-laying-out the status bar and
  HUD. Bounded, but it is a video-pipeline reinit, not a flag flip.

  Worth checking first whether DOOM-0051's mid-game renderer switching
  already tore down and rebuilt enough of this to reuse - that work
  solved a structurally similar "reinit the view without restarting"
  problem and is shipped.

  Related: DOOM-0147 (the Classic 4:3-vs-fill aspect work that introduced
  this flag).

- 📋 [DOOM-0275] **Every debug hotkey should report its new state on screen, not only to the terminal.**
  Found when the user pressed a key mid-play-test and had no way to tell
  whether it had registered: "When I press / there is no feedback to let me
  know that the log is on or not."

  The fog (`;`) and GPU profiler (`\`) keys were fixed on the spot, since
  the DOOM-0011 perf A/B depends on knowing which state you are in. The
  rest still printf to stdout only: `~` RT view cycle, `]` de-tile, `[`
  filth, `'` wet, and the DOOM-0267 `/` geometry dump.

  Use I_DebugKeyMessage in i_video.c — it prints AND posts the same line
  to players[consoleplayer].message, so it shows on the HUD during play.

  Related, and the reason this bit: `/` is NOT a profiler key at all — it
  is the temporary DOOM-0267 geometry dump. A user reaching for the
  profiler and getting silence from an unrelated key is the failure mode.
  Worth auditing the whole hotkey set for keys that look like they should
  do something and do something else.
  **Layman:** When you press one of the developer keys, say what happened on screen instead of in a terminal window you cannot see while playing.
  Kind: ux.
  Source: user-report-2026-07-27.

- ✅ [DOOM-0276] **Replace the fog march's per-sample up-ray with a seep-field lookup.**
  Measured 2026-07-27 (spec DOOM-0011 6): the fog costs +8.38 ms /
  +34.7% present-total, and 7.93 ms of that is inside the megakernel. The
  pole is the per-sample open-sky test - one ray query straight up for
  each of kFogSteps = 24 samples, per fog pixel.

  It does not need a ray. Vanilla DOOM is flat-mapped: one floor and one
  ceiling per XY, never a room above a room. The engine's own lookup is
  the proof - R_PointInSubsector takes (x, y) only and returns exactly one
  sector, hence one ceilingpic. So "is there sky above this point" is a
  pure function of XY, and the up-ray is doing 3-D work on a 2-D question.

  DOOM-0011 L1d already builds exactly that map: RB_BuildSeepField's cells
  are 0 where the sector's ceilingpic == skyflatnum. Swap the ray for the
  field tap the shader already performs for the seep, and the openSky test
  becomes free rather than the dominant cost.

  Risk to judge on screen, not here: the field is 64-unit cells, so the
  sky/roof boundary blurs by up to a cell and the mist wall at a doorway
  threshold could soften. Spec 6 warns that coarsening the open-sky test
  also coarsens the seep that branches on it - but this is per-cell, not
  the per-surface RB_MESH_OUTDOOR whole-view fallback that warning is
  about.

  Expected: most of the 7.93 ms, at no cost in look beyond that edge.

  Resolved (2026-07-27): the up-ray is gone. The seep field grew a second
  channel - .r is the L1d distance, .g a per-cell open-sky mask (1.0 where the
  cell's sector has ceilingpic == skyflatnum) - so the march's ONE existing
  bilinear tap now answers both questions and the march carries no rays at all.
  The mask needed its own channel rather than an epsilon on .r: a roofed cell
  one step inside a doorway also has a near-zero distance, so d < eps would
  have put the full outdoor bank inside the first room behind every door.

  Measured on the RX 6600, E1M1, 50% render scale, three runs per configuration
  against a worktree build of 8522b23: fog costs +8.37 ms (+35.4%) before and
  +0.98 ms (+4.2%) after; the fog-on frame goes 32.03 -> 24.53 ms
  present-total, 31.2 -> 40.8 FPS. Fog-OFF is the control and is unchanged to
  the hundredth of a millisecond on both builds, which is what makes the
  comparison trustworthy - the expected saving landed, and the thing that
  could not have moved did not.

  Look checked before the number was believed: -shotverify at the same spawn
  view, MAE 2.93/255 against 1.09/255 between two runs of the SAME build, so
  ~1.8 of real change - the roofline moving onto the 64-unit grid, which is the
  one cost this carries. -rtverify PASS. Fixed a latent L1d bug the review
  turned up on the way: the field's grid was sized with a truncating divide, so
  the void ring got bilinear weight on real air along the +X/+Y edges (E1M1
  74x47 -> 75x47 once corrected). Spec DOOM-0011 4.3a amendment + 6's second
  boxed notice; fix ledger batch 21.

  Still owed: a user play-test of a DOORWAY threshold, which is where the
  half-cell error lives.
  **Layman:** Stop asking the graphics card to fire a test ray straight up 24 times per pixel, when a small map we already build answers the same question for free.
  Kind: perf.
  Lanes: renderer, shaders.
  Source: in-session-2026-07-27.

- 📋 [DOOM-0277] **Pace Ultra's ray-traced view to an even 30 FPS instead of a variable one.**
  User decision 2026-07-27, after the fog measurement: "for Ultra with ray
  tracing, we can make it 30 FPS but we need to ensure proper frame
  pacing." Accepting 30 is the right call; the pacing is what makes 30
  feel like 30 rather than like a bad 40.

  Today there is NO pacing of any kind. The swapchain prefers MAILBOX
  (r_vulkan.cpp:1439) with FIFO only as a fallback, and there is no frame
  limiter anywhere in the tree - so frames present the instant they are
  ready, at whatever irregular cadence the scene produces. On a 60 Hz
  display a ~31 FPS stream lands unevenly across refreshes, which reads as
  judder even when the average is fine.

  A paced 30 means presenting on a fixed cadence - every second refresh on
  a 60 Hz panel - which requires the frame to reliably FIT in 33.3 ms.

  It currently does not: measured mean is 32.53 ms with a 36.01 ms max, so
  a naive lock would miss its slot constantly and alternate 33/50 ms,
  which is worse than no lock. THE PACING WORK IS BLOCKED ON HEADROOM, not
  the other way round - land the perf levers first.

  Also needed: read the display's actual refresh rate rather than assuming
  60, and decide what happens on a 120/144 Hz panel (present every 4th).
  **Layman:** Lock the ray-traced mode to a steady 30 frames a second, evenly spaced, so it feels smooth instead of stuttery even though it is not fast.
  Kind: perf.
  Lanes: renderer.
  Source: user-request-2026-07-27.

- 📋 [DOOM-0278] **Motion blur for the 3-D views - camera-velocity first, per-object only once monsters are models.**
  User asked (2026-07-27) for "perhaps some per object motion blur" as
  part of accepting 30 FPS for Ultra RT. Splitting it, because the two
  halves have very different costs here.

  CAMERA-VELOCITY BLUR IS NEARLY FREE AND IS WHERE THE BENEFIT IS. The
  SVGF temporal pass already reprojects every pixel's world hit point into
  the PREVIOUS frame's camera (svgf_temporal.comp:102) - the screen-space
  delta that falls out of that IS the camera motion vector. And in DOOM
  the dominant motion is the camera: players turn fast and often.

  PER-OBJECT BLUR NEEDS DATA THAT DOES NOT EXIST. The reprojection above
  is world-position based and assumes the world is static; moving things
  are handled by REJECTING the temporal match, not by tracking them. There
  are no per-object velocities anywhere in the tree, so this means a new
  velocity G-buffer plus per-thing previous transforms. And in Ultra the
  monsters are still billboard sprites - blurring a flat card earns little
  - so this should wait for DOOM-0080 (sprites to 3-D models).

  Caution for a fast shooter: heavy blur reads as input lag. Ship it as a
  menu toggle with a short shutter and a conservative default; judge it
  with the user at a paced 30, since a smooth 30 may need less of it than
  a juddering one.
  **Layman:** Blur the picture slightly as you turn, so 30 frames a second looks smoother than it is.
  Kind: feature.
  Lanes: renderer, shaders.
  Source: user-request-2026-07-27.

- 📋 [DOOM-0279] **Get Ultra's ray-traced view back to 60 FPS - and give the remaining effects a budget to fit in.**
  User, 2026-07-27: "please log a roadmap entry to try and get this back
  to 60 FPS even though I probably have to resign myself to the fact that
  it may not be possible on my hardware" - and, in the same breath, "keep
  in mind we got more effects to add to this view."

  THE HONEST ARITHMETIC, measured on the RX 6600 in E1M1 (Ultra RT, 50%
  render scale, fog High): present-total 32.53 ms = fenceWait 28.54 (the
  GPU wall: megakernel 21.21 + denoise/taau 7.01 + blit 0.29) + CPU build
  3.61 + record/submit. 60 FPS needs 16.7 ms. So even deleting the fog
  ENTIRELY leaves 24.15 ms - about 41 FPS. 60 is not a tuning problem on
  this hardware; it needs the path tracer roughly halved, which is what
  DOOM-0188 (quarter-res GI), DOOM-0189 (radiance cache), DOOM-0190 (async
  + 2 frames in flight) and DOOM-0191 (A-SVGF) are for. Lowering the
  render scale below 50% is the one lever that reaches 60 TODAY, at a cost
  in sharpness.

  TWO LEVERS THAT COST NOTHING IN LOOK, AND SHOULD LAND FIRST:
  - DOOM-0276 - the fog's per-sample up-ray becomes a field lookup
    (expect most of 7.93 ms).
  - DOOM-0197 - extend the raster path's build-ahead overlap to RT. The
    CPU build is 3.61 ms and it is SERIALISED in front of a 28.54 ms GPU
    wait, so overlapping it is worth ~3.6 ms and touches no pixel. The
    same change took the raster path from 70 to 161 FPS.

  Together those two plausibly take 32.5 ms toward ~21 ms.

  THE POINT OF THAT HEADROOM IS THE EFFECTS STILL QUEUED, not a higher
  number. Still to land in this view: DOOM-0011 L2 (sky shafts - a SECOND
  ray per fog sample), L3 (torch shafts), L4 (colour profiles), plus
  DOOM-0103 reflections and whatever follows. Accepting 30 FPS at 32.5 ms
  leaves 0.8 ms of slack under the 33.3 ms frame, which is not a budget -
  it is a cliff, and the next effect goes over it.

  So the deliverable here is not only speed: it is a PER-EFFECT ms BUDGET
  for the RT view, in the manner of DOOM-0011 spec 6's table, so each new
  effect is measured against a share rather than against "does it still
  feel alright".
  **Layman:** The ray-traced mode runs at about 30 frames a second on this card; this is the long-term push to double that, and to stop each new effect quietly eating the difference.
  Kind: perf.
  Lanes: renderer, shaders.
  Source: user-request-2026-07-27.

- 📋 [DOOM-0280] **The DOOM-0060 game chooser can pick a different IWAD across identical runs.**
  Found while A/B-ing DOOM-0276. Two launches with the SAME config file, same
  DOOMWADDIR (holding both doom.wad and doom2.wad), same env and no -iwad, and
  no input at all: one started E1M1 (bounds x[-704,3758] y[-4856,-2080], 2227
  tris) and the other started DOOM 2's MAP01 (bounds x[-1224,1892] y[-888,2608],
  1610 tris). Nothing in the two invocations differed but the working directory
  of the binary.

  Why it matters beyond tidiness: it silently invalidates measurements. The first
  before/after comparison for DOOM-0276 looked like the change had made the
  renderer 4 ms FASTER with fog OFF, which is impossible - fog off skips the
  march entirely. The two runs were rendering different games. Any perf or
  -shotcompare work that does not pass -iwad explicitly is exposed to this.

  Not yet diagnosed. The chooser is presumably waiting on input and falling
  through to a default; the suspicion is that the fall-through depends on timing
  or on the wad-scan order rather than on a stable preference. Worth checking
  whether a remembered preference is meant to be persisted to the config and is
  not being.

  Workaround meanwhile: pass -iwad explicitly whenever a run has to be
  reproducible.
  **Layman:** With both DOOM 1 and DOOM 2 in the wads folder and no key pressed, the game sometimes starts one and sometimes the other - it should always make the same choice.
  Kind: fix.
  Lanes: startup.
  Source: in-session-2026-07-27 (hit while measuring DOOM-0276).

- ✅ [DOOM-0281] **Re-flood the seep field when a wall or door opens, so fog rolls into a newly-opened room.**
  User, 2026-07-27, with a screenshot of a normally-closed E1M1 wall standing
  open onto the courtyard: "if a wall opens like in this screenshot, the fog
  doesn't roll in though. Usually this wall is closed."
  Progress (2026-07-27): IMPLEMENTED and verified on the RX 6600; only the LOOK
  is left, which needs a person at the keyboard.
  Progress (2026-07-27, second pass): the user reported the fog STILL not coming in
  through an opened wall. The mechanism was not at fault -- their own play log
  (/tmp/doom-ants-run.log) shows the re-flood firing repeatedly through the
  session, the field going 835 -> 819 -> 761 -> 721 -> 715 sealed cells as walls
  opened. E1M1 throughout (2227 tris, one level all session).

  The fault was entirely in two constants, and the arithmetic says so without a
  play-test. skyExposure's roofed branch is
  mix(kIndoorFogScale=0.05, kSeepMax=0.5, exp(-d/kSeepFalloff=192)) against 1.0
  outdoors, which gives 50% of outdoor density standing IN the opening, 21% at 192
  units in, 11% at 384, and ~7% by 600 -- indistinguishable from a sealed room. A
  player standing back in a room, which is where players stand, could not have seen
  anything no matter how well the re-flood worked.

  Worse, kSeepMax = 0.5 is a 2x density STEP at every threshold: fog visibly HALVES
  the instant it crosses an opening, which is the opposite of seeping through one.
  Air standing in a doorway is outdoor air.

  Re-tuned: kSeepMax 0.5 -> 0.9, kSeepFalloff 192 -> 384 (and RB_SEEP_FALLOFF in
  r_mesh.h, which the header requires to match). New grade: 90% at the opening, 57%
  at 192, 36% at 384, 17% at 768.

  INV-12 survives BY CONSTRUCTION, not by luck: dMax is defined as 8 x kSeepFalloff
  in both pt_common.glsl and r_mesh.h, so the sealed sentinel scales with the
  falloff and a sealed room stays exactly 8 e-folds out -- 0.0503, i.e. the
  kIndoorFogScale floor -- whatever the falloff becomes. Verified in the field
  build: an unreachable cell's `best` is DMAX + dist, clamped back to DMAX.

  Side effect, benign: the E1M1 cell split moves 920/1770/835 to 920/2020/585. The
  250 cells that changed bucket were never sealed -- they were SATURATING the old
  1536 sentinel, and now carry a real distance between 1536 and 3072, which grades
  to 0.05-0.07. Below the floor's own visibility.

  -rtverify PASS, unchanged. Spawn-frame capture shows the far half of the E1M1
  start room now carrying visible mist off the courtyard windows with the near
  floor still clear.

  NOTE this overrides a signed-off look (2026-07-27: "the fog does dissipate the
  further away from an opening"), on the strength of a FRESHER complaint about the
  same feature. If it now overshoots, kSeepMax is the single dial -- pull it toward
  0.7 before touching the falloff, since the falloff is what carries the depth the
  user is asking for.

  Built as the bullet's three-part shape, and the shape held:
  - Detector. RB_SeepOpeningsChanged (r_mesh.c) caches one open/shut bit per
    linedef at flood time and diffs it. Gated on the RB_UPD_MOVED that
    RB_UpdateMeshHeights already returns, because connectivity cannot change
    without a plane moving -- so a still map never pays for it at all. NOT hooked
    into the playsim: no p_doors/p_floor/p_plats edit, the renderer asks the map.
  - Re-flood, latched like blasDirty and consumed in RecordRtTrace, so a door
    opened while in Solid is still caught on the way back to Ultra.
  - Upload. vkCmdCopyBufferToImage into the EXISTING image, from a persistent
    mapped staging buffer, with a SHADER_READ->TRANSFER_DST->SHADER_READ barrier
    pair in the frame's own command buffer. No destroy, no device wait, no hitch.
    Safe with one staging copy because the RT path records after waiting
    g.inFlight (single-frame-in-flight).
  - Ease. Exponential, tau 0.32 s (~95% in 1 s), so the mist DRIFTS in rather than
    popping. The inverse falls out for free: a door that shuts eases the seep back
    out. If a re-flood produces a field identical to the current one -- a crusher
    cycling, a door between two already-sealed rooms -- the ease is skipped
    entirely, so those cost one flood and nothing else.

  Measured, not budgeted -- which is the whole lesson of Q22:
  - Detector scan: 0.0039 ms, and only on frames where a plane moved.
  - Re-flood: 0.6-0.7 ms, once per opening flip (NOT per frame of door motion).
  - Spawn frame: -shotcompare vs a worktree build of 462503c, mae 0.000/255 on a
    same-build noise floor of 0.000. Bit-identical -- the change is inert until
    something moves.
  - -rtverify PASS, unchanged to 4 significant figures (rel-MSE 0.0796%,
    white-furnace deviation 0.000000). No new Vulkan validation messages; zero
    mentioning the copy or an image layout.

  The proof the field moves the RIGHT way, which is what the print now carries:
  E1M1 spawns with 835 sealed cells; opening one door drops it to 761 (74 cells
  behind that door stop being sealed) and shutting it returns exactly 835.

  Verification method worth reusing: the built-in attract demos are version-
  mismatched against this WAD (109 vs 110) and xdotool cannot inject input under
  Wayland, so neither could drive a door. A ~15-line throwaway hook in
  P_UpdateSpecials called EV_VerticalDoor on tics 105 and 280 with no player
  input -- deterministic, and removed before the commit.

  REMAINING: the user's eyes on whether tau = 0.32 s reads as mist rolling in
  rather than a fade. That is the one thing no automated check can answer.

  Cause, verified: RB_BuildSeepField (r_mesh.c) runs P_LineOpening on every
  two-sided seg and skips any with openrange <= 0 -- a shut door is two-sided but
  has no opening, which is deliberate and is what INV-12 rests on. The field is
  then built exactly once, in RB_Vulkan_BuildLevel (r_vulkan.cpp:7356), from
  those spawn-state openings. Nothing re-floods it when a door or lift moves, so
  the room behind a wall that opens in play keeps the "sealed" sentinel and the
  indoor fog stays at the flat kIndoorFogScale floor. NOT a DOOM-0276 regression:
  the open-sky mask that task added is per-cell ceilingpic, which a moving door
  does not change; the stale part is the seep DISTANCE, and it was equally stale
  before.

  This is spec DOOM-0011 Q22, logged 2026-07-25, which deferred the decision with
  "judge at L1d whether the difference is even noticeable in play". It is.

  Q22 rejected re-flooding as "far too costly", but it reasoned from the BUDGET
  (<= 20 ms per level load) rather than from a measurement. The fill actually
  costs 0.6 ms on E1M1 -- 33x under that budget, and a fortieth of a 24 ms frame.
  So the CPU side is not the obstacle.

  The real work is the GPU re-upload. UploadSeepField destroys and recreates the
  VkImage, which is only safe today because the level-load path has already
  drained the device. Mid-play it needs either a device wait (a visible hitch) or
  -- better, and not much harder -- a plain vkCmdCopyBufferToImage into the
  EXISTING image with barriers, since the grid dimensions cannot change within a
  level, so nothing needs reallocating.

  Shape of the fix:
    - set a dirty flag when a door/lift/platform finishes moving AND its
      openrange crosses zero (only a connectivity change matters, so most sector
      movement triggers nothing);
    - re-flood on that flag, throttled to at most one rebuild every N frames;
    - upload into the existing image rather than recreating it.

  Second half, and it is what the user's word "roll" is asking for: a rebuild
  makes the fog POP in over one frame. Easing the field toward its new values
  over ~1 s would make the mist visibly drift in through the new opening, which
  is the effect worth having. Cheap to do -- keep the previous field and lerp,
  or lerp d per cell on the CPU during the throttled rebuild.

  Note the inverse case too: a door that CLOSES should stop the seep. Same
  mechanism, no extra work, but worth putting in the acceptance check.
  **Layman:** When a wall slides open onto the outdoors, mist should start drifting into the room. Right now the game worked out where mist can reach when the level loaded, and never revisits it, so a room that was sealed at the time stays clear even after it opens.
  Kind: enhancement.
  Lanes: renderer, shaders.
  Source: user-play-test-2026-07-27.
  USER PLAY-TEST 2026-08-04 -- the observable behaviour is confirmed and the
  item is closed on it. Their words: "fog rolls in where walls are removed",
  with no counter-example found -- "I haven't found a place where this
  applies yet... unless you know of a specific place that I can test, this one
  we will leave as is for now."
  That is the acceptance this bullet was waiting on. The mechanism half had
  already been verified on the RX 6600 on 2026-07-27 (the re-flood firing
  repeatedly through a live session, the field going 835 -> 819 -> 761 -> 721
  -> 715 sealed cells as walls opened, all on E1M1), and the constants that
  made it invisible to a player standing back in a room were re-tuned in the
  same pass (kSeepMax 0.5 -> 0.9, kSeepFalloff 192 -> 384, mirrored in
  RB_SEEP_FALLOFF as the header requires). What was missing was only a human
  confirming the effect reads in play, and it does.
  No specific fixture is owed back to the user: E1M1 is where the mechanism
  was instrumented and the effect is general to any opening, so there is no
  better place to send them. Closing rather than leaving open for a fixture
  that would not sharpen the answer.
  INV-12 survives by construction, not by luck -- dMax is 8 x kSeepFalloff in
  BOTH pt_common.glsl and r_mesh.h, so the sealed-room sentinel scales with
  the falloff and a sealed room stays exactly 8 e-folds out whatever the
  falloff becomes. -rtverify PASS, unchanged.

- ✅ [DOOM-0282] **A wall changes colour — goes blue — when the camera turns a few degrees.**
  User, 2026-07-27, with a matched screenshot pair from a crate room with BLUE
  liquid pooled on the floor: "notice this wall now ... notice that the wall now
  turns blue by me just turning a few degrees." Camera position unchanged; only
  the yaw differs between the two frames.
  Resolved 2026-07-27 -- but by SIDE EFFECT, and the distinction is worth keeping.

  User re-tested deliberately after the DOOM-0281 seep re-tune (8b41786): "I stood
  at varying distances from the wall, rotated the camera left and right but no
  change to the wall. This was with fog on." That is a better test than the one
  that found it -- varying distance as well as yaw, and with the suspected
  subsystem enabled rather than disabled.

  The only thing that changed between the report and the re-test is kSeepMax
  0.5 -> 0.9 and kSeepFalloff 192 -> 384. That clears the LIQUID hypotheses this
  bullet led with (the DOOM-0183 sheen, RIS resampling, SVGF disocclusion) -- none
  of them are touched by a fog constant -- and points at the fog itself.

  Most likely mechanism, INFERRED AND NOT PROVEN: at kSeepFalloff 192 the seep
  field crossed its whole range over about three 64-unit grid cells. The march
  samples that field along the view ray, so a few degrees of yaw moved the samples
  across a steep gradient and swung the in-scattered fog on that wall. Doubling
  the falloff halves the spatial rate, and the wall stops flipping. It also fits
  the tint being CLEAN rather than grainy, which is what argued against the
  stochastic candidates in the first place.

  The honest status: it does not reproduce, and the one change that plausibly
  explains it is the one that was made. That is not the same as having found it.

  What would bring it back, and is the thing to watch: any future TIGHTENING of
  kSeepFalloff, or a steep seep gradient arriving some other way -- a small room
  newly opened next to a sealed one, where the field steps by its full range
  across one or two cells. If a wall ever swings colour on yaw again, come here
  first and check the seep gradient before suspecting the liquid.

  The two frames were registered against each other to confirm it is the same
  surface and not a different wall coming into view: the pillar moves ~80 px right
  between them (camera yawed left), and the panel that reads grey at x~920-1170 in
  the first frame reads blue at x~1000-1250 in the second. Same wall, two colours,
  same spot on the floor.

  NOT YET DIAGNOSED, and the render mode is not yet known -- which matters more
  than usual here, because the suspects share nothing between the two paths:

  - If RAY-TRACED: the blue floor is a liquid, and DOOM-0183 gives liquids a
  forced-constant Le, so the nukage is a real light in the NEE list. Candidates,
  in rough order: (a) the DOOM-0183 sheen leaking onto a NON-liquid surface --
  a specular sheen is view-dependent BY CONSTRUCTION, so a mis-set LIQUID_NUKAGE
  bit or a material-slot collision would produce exactly this; (b) DOOM-0120 RIS
  light resampling picking the nukage as the sample from one angle and not the
  other; (c) SVGF disocclusion on turn handing the wall a poorly-converged
  history. (a) fits best because the tint is CLEAN, not noisy -- (b) and (c)
  would both read as grain.
  - If RASTERISED: none of the above exists. The per-subsector nearest-N
  point-light list is keyed on POSITION, not on yaw, so a pure rotation changing
  a wall's lighting would itself be the bug.

  First step is to establish the mode, then diff the two frames' shading inputs
  for that wall rather than guessing. A view-dependent term on a diffuse wall is
  the shape to look for either way: turning the camera must not change what a
  matte surface's colour is.
  **Layman:** A wall in a room with blue liquid on the floor turns blue when you turn on the spot, and back again when you turn away. Standing still and just looking around should never change what colour a wall is.
  Kind: fix.
  Lanes: renderer, shaders.
  Source: user-play-test-2026-07-27.

- 📋 [DOOM-0283] **Ultra falls back to paletted art silently when the HD assets are not found.**
  EnsureHdMaterials resolves the HD set relative to the CURRENT WORKING DIRECTORY
  ("assets/ultra/") unless DOOMASSETDIR overrides it. cwd is linuxdoom-1.10/ (the
  launcher keeps it there so savegames land where they always have), and
  assets/ultra/ lives one level up at the repo root -- so any run that does not
  export DOOMASSETDIR gets paletted art in Ultra. run-doom-ants.sh sets it and
  carries a comment warning about exactly this; nothing else does.

  The only signal is a stdout line -- "DOOM-0042: no assets/ultra/materials.csv -
  Ultra uses paletted art." -- which is invisible while the game owns the display.
  That is the same failure shape as the debug hotkeys in DOOM-0275: an affordance
  whose only output channel is one the user cannot see during the activity it
  describes.

  Cost of it being silent, measured rather than imagined: an entire session of
  DOOM-0197 perf measurements and -shotcompare golden captures were taken paletted
  while believing they were Ultra HD, and the numbers had to be thrown away and
  retaken (41->48 fps HD, vs 43->53 paletted -- different enough to matter). The
  user spotted it from a screenshot; nothing in the harness did.

  Two things worth doing, the first much more important:
  - Say so ON SCREEN. The Ultra row in the render-mode menu could read "Ultra (HD
  art not found - using original art)", or a one-line startup notice via
  I_DebugKeyMessage's channel. A player who picked Ultra and got Solid's art has
  no way to tell today.
  - Consider resolving the default path relative to the EXECUTABLE or searching
  "../assets/ultra/" as a fallback, so the common layout works without an env
  var. Keep DOOMASSETDIR as the override.
  **Layman:** If the high-definition texture pack cannot be found, Ultra quietly falls back to the original artwork and looks like Solid. Nothing on screen says so, so it reads as the setting not working.
  Kind: enhancement.
  Lanes: renderer.
  Source: in-session-2026-07-27.

- 📋 [DOOM-0286] **Upres the HUD / status bar and the first-person hand and gun to at least 1080p.**
  The status bar, its number/face graphics and the first-person weapon
  sprites are still drawn from the 320x200 paletted patches, so on a 4K
  display they are the coarsest thing on screen while the world behind
  them is HD. Target at least 1080p-native for all of them.

  Scope named by the user (2026-07-30): the status bar AND the hand +
  gun. Three surfaces, and they are NOT the same problem:
    - status-bar background + digits + mugshot (st_stuff.c / st_lib.c,
      V_DrawPatch into the 320x200 backbuffer)
    - the first-person weapon sprites (p_pspr.c -> R_DrawPSprite, and the
      3D backends' own overlay path)
    - anything else drawn through the same 320x200 patch pipeline that
      sits on top of the HD view

  Open questions for the spec: upscale the existing art at load time (a
  filter -- cheap, no new assets, no licence question) versus replacing
  it with HD art (DOOM-0042's route, Ultra-only, needs art); and whether
  this is shared across all three tiers or Solid/Ultra only. Classic must
  keep its exact 1993 look either way.

  Relates to DOOM-0042 (HD art set) and DOOM-0050 (2D overlay ghosting
  over the status bar in 3D modes).
  **Layman:** The health/ammo bar at the bottom and the gun in your hands stay chunky and pixelated even at 4K — sharpen them to match the rest of the picture.
  Kind: enhancement.
  Source: user-request-2026-07-30.
  Scope widened by the user, 2026-07-30 (same day, mid-session). The hand
  and gun should ideally become REAL 3-D MODELS, lit and reflected by the
  scene rather than flat sprites pasted over it -- including the
  super-shotgun's reload animation, which is the case that most obviously
  wants real geometry. "It doesn't have to be full on, it can be faked."

  Per tier, as stated:
    - ULTRA: models + lighting + reflections, in BOTH the rasterised and
      the ray-traced view. Note this is the tier rule in CLAUDE.md working
      as written -- the feature is gated on the ART (HD weapon models),
      not on the view, so raster gets a cheap fake of the same look and
      RT gets the real thing.
    - SOLID: upres the existing sprites (no model swap), and fake the
      lighting / reflections on them if it can be made to hold up. The
      user's words: "if lighting / reflections can be faked there, that
      would be great as well."
    - CLASSIC: unchanged, as before.

  This splits the item's three surfaces further apart than the body above
  assumes, and the spec must not treat them as one job:
    - the status bar stays a 2-D upres problem in every tier;
    - the hand/gun becomes an ASSET + RENDERER problem in Ultra (geometry,
      materials, a weapon-space transform, and a per-frame animation
      driven by the existing p_pspr state) and stays a 2-D upres problem
      in Solid.
  So "upscale vs replace with HD art" is no longer one open question with
  one answer -- it is answered differently per tier, and the Ultra answer
  now reaches past art into the renderer.

  Sequencing note, not yet decided: this overlaps DOOM-0080 (ALL sprites
  -> 3-D models in Ultra), which memory records as a far-out project
  because free DOOM-roster models are scarce. The weapon set is a much
  smaller and much better-supplied subset than the monster roster -- one
  first-person model per weapon, always seen from one angle -- so it may
  be tractable well before DOOM-0080 is. Decide when this is specced
  whether the weapons are a slice of DOOM-0080 or a separate, earlier
  item.

- ✅ [DOOM-0289] **Bake the sun's fixed direction into a load-time clearance field and delete L2's per-sample ray.**
  DOOM-0011 L2 shipped correct and far over budget (544ae84). Measured in
  E1M1's courtyard, default rt_fog=1, render scale 50%:
    pre-L2, fog off   43 fps, megakernel 12.7 ms
    pre-L2, fog Low   40 fps, megakernel 13.4 ms   (the fog itself: 0.7 ms)
    L2,     fog Low   25 fps, megakernel 27.0 ms   (the sun ray: 13.6 ms)
  The ray alone is 19x the entire rest of the fog and costs 15 fps, against
  a 15 % budget. At rt_fog=3 it is 26.5 ms of megakernel vs 15.4 off.

  This is DOOM-0276 repeating. That item deleted this same feature's OTHER
  per-sample ray -- the straight-up open-sky test, then 7.9 ms of an 8.4 ms
  feature -- by answering it from a load-time 2-D field instead, and took
  the fog from +35 % of frame time to +4 %. The sun ray is the same shape
  of question and admits the same answer, for the same reason: DOOM is
  flat-mapped, and kSunDir is a compile-time CONSTANT.

  Proposed mechanism -- a sun-clearance field, one R16F channel on the grid
  the seep field already uses. Because the sun direction never changes, the
  question "can the sun be seen from (x,y,z)?" collapses to a THRESHOLD ON
  Z: march each cell's 2-D projection along the sun's horizontal heading,
  carry the running maximum of (obstruction top height - horizontal distance
  travelled * tan(elevation)), and store it. Then the shader's test is
    sunVisible = (p.z >= texture(uSunClearance, worldToSeepUV(p.xy)).r)
  -- one bilinear tap, the cost of the seep tap already in the loop, and
  the 13.6 ms goes to roughly nothing.

  Known consequences to settle when specced, not hand-waved:
    - Same trade DOOM-0276 accepted: the shaft edge follows the 64-unit
      grid rather than the exact wall, so it lands within half a cell.
      Softer beam edges are arguably a FEATURE here.
    - Ties the sun to a fixed direction per level. It already is one
      (kSunDir is a const), but this makes it structural -- a future
      moving sun would need a rebuild per direction, so if a day/night
      cycle is ever wanted, decide before this lands.
    - Needs the same re-flood hook DOOM-0281 added for the seep field: a
      door or lift that opens changes what the sun can reach.
    - Doors/lifts that move mid-frame are the interesting case; the seep
      field's existing dirty-flag path is the precedent.

  Blocks the L6 perf gate, and blocks L3 (torch shafts) from being
  measured honestly -- L3 adds its own per-sample work on top of this one.
  **Layman:** The sunbeams currently cost nearly half the frame rate; precompute the answer once when the level loads instead of asking the graphics card millions of times per frame.
  Kind: perf.
  Source: in-session-2026-07-30.
  Design notes (2026-07-30, before the spec is written). User chose the
  full fix over a stopgap, so this goes through rule 14's gate first.

  THE WRINKLE THE HEADLINE SKETCH ABOVE GETS WRONG. "Store the minimum z
  at which the sun clears every obstruction" is only valid in OPEN-SKY
  air, where rising can only ever help. In ROOFED air it is false in both
  directions: rising clears a wall but also runs you into the ceiling. So
  sun visibility is not a single threshold on z there -- and roofed air
  near a doorway is exactly where the best shafts are, so this cannot be
  waved through.

  Stated properly. March the 2-D projection from the cell along the sun's
  fixed horizontal heading u = normalize(kSunDir.xy), with slope
  m = kSunDir.z / |kSunDir.xy| (= 2.357 at the shipped kSunDir, ~67 deg
  elevation). A ray starting at height z is at h(s) = z + m*s after
  horizontal distance s. Each cell c crossed at distance s contributes:
    - floor:   need z >  floor(c) - m*s          (a lower bound)
    - ceiling, non-sky: need z <  ceil(c)  - m*s (an upper bound)
    - ceiling, SKY:     z >= ceil(c) - m*s ESCAPES -- sky reached, stop
  So the admissible z is an intersection of bounds terminated by an
  escape, i.e. an INTERVAL, not a threshold.

  PROPOSED FIELD: two more channels carrying that interval per cell --
  zLo (lowest z that still escapes) and zHi (highest z before a solid
  ceiling stops it). Shader test becomes
    sunVisible = (p.z >= tap.b && p.z <= tap.a)
  which is still ONE bilinear tap, and the seep tap is ALREADY in the
  march loop (RG16F today: .r = seep distance, .g = open-sky mask). Widen
  that image to RGBA16F and the whole 13.6 ms goes to roughly nothing --
  no new image, no new sampler, no new descriptor, no second tap.

  KNOWN APPROXIMATION, to be argued explicitly in the spec rather than
  discovered later: an interval cannot represent TWO separated openings
  on one path (two windows at different heights on the same heading). The
  error is one-sided -- it reports visible in the gap between them -- so
  it over-lights rather than leaving a hole, and it is rare. Quantify or
  bound it in the spec; do not leave it implied.

  Also to settle in the spec, none of it new work but all of it easy to
  forget: bilinear interpolation of zLo/zHi across cells softens the
  shaft edge (probably a feature, matching DOOM-0276's accepted half-cell
  error); DOOM-0281's re-flood hook must extend to these channels or an
  opening door leaves stale shafts; and the whole scheme is structurally
  tied to kSunDir being a compile-time constant, so a moving sun would
  need a rebuild per direction -- decide before this lands if a day/night
  cycle is ever wanted.

  Build cost should be small and in line with the seep field's measured
  0.6 ms: ~3.5 k cells on E1M1 x a march of order 100 cells is ~350 k
  steps, and it reuses the seep field's existing grid, transform UBO and
  upload path (vkCmdCopyBufferToImage into the existing image).
  SPECCED AND GATED (2026-07-30). The design is now in
  docs/specs/DOOM-0011-volumetric-lighting.md §4.4 (the 2026-07-30
  amendment) with build steps as Task L2b in
  docs/specs/DOOM-0011-implementation-plan.md. Rule 14's /cold-eyes gate
  ran 3 loops x 3 lanes and converged by cap with ZERO findings deferred:
  loop 1 C3/H5/M7/L5, loop 2 C2/H5/M10/L9, loop 3 C4/H4/M10/L8, every
  verified finding fixed in its own loop. Commits 1a75192 (draft),
  e19cb63 / 1e091a4 / 19ec1a0 (the three loops).

  The roofed-air correction this item recorded survived review intact, and
  the review then found five more things the sketch got wrong. Worth
  carrying into the build:
    - The march must NOT stop at the first sky cell, and "first" has to
      mean first with a NON-EMPTY window -- a later sky sector with a
      higher ceiling raises the escape threshold. Stopping early reports
      an open courtyard as unlit below head height.
    - The stored zHi is clamped to the cell's own ceiling to bound the
      field's dynamic range, and in an OPEN-SKY cell that ceiling is a sky
      plane rather than a barrier. The shader needs `p.z <= zHi ||
      openSky` or a horizontal seam appears across outdoor fog.
    - The padded void ring blocks for the seep and must ESCAPE for the
      clearance -- and `solid = (cz <= fz)` means the natural `fz = cz = 0`
      ring write silently makes it solid, carving an unlit band along
      every +X/+Y map edge.
    - DOOM-0281's flip detector is necessary but NOT sufficient: the
      clearance is keyed on plane HEIGHTS, so a lift moving between two
      open heights changes what it shadows without flipping anything. The
      trigger widens, and the refresh splits in two so the height-only
      case skips the Dijkstra.
    - The step size is cell/(|u.x|+|u.y|) = 45.3 at the shipped 45-degree
      heading, not the cell size -- so 107 units of rise per cell entered,
      not 151. Several bounds were quoted against the wrong figure.

  Also fixed in passing, and pre-existing rather than new: §4.4(a) still
  specified that L2's directional term REPLACES the flat sky ambient, and
  "roofed air in-scatters no sky light at all". What shipped is an
  ambient/directional SPLIT (kSkyAmbientFrac = 0.65) because the
  replacement measured a 3.4x darkening on hardware. An implementer
  reconciling L2b against the spec would have removed the ambient share
  the signed-off look depends on.

  Two questions were opened and one closed by the user the day it was
  asked: Q27 (a moving sun) is CLOSED -- "DOOM 1 + 2 doesn't feature a day
  / night cycle. So, that's fine." -- which makes INV-3's fixed sun
  structural rather than a v1 simplification. Q28 (RB_SUN_NEVER, the shaft
  edge) and Q30 (the clearance-rebuild cadence) are hardware tunes owned
  by L2b; Q29 asks whether the "escaped z is never re-shadowed"
  approximation ever shows.

  NOT IMPLEMENTED -- the next session builds Task L2b. Note for it: the
  review's own trend says this spec is at 2.7k lines and past the gate's
  design point, so a further amendment should split the document first.
  Resolved 2026-07-31 (d5f1ce7). Built as spec §4.4's 2026-07-30
  amendment and plan Task L2b: the sun's visibility is a load-time
  clearance INTERVAL on two new channels of the seep field (.b = zLo,
  .a = zHi, the field widened to RGBA16F), read by the same bilinear tap
  that already answered open-sky. The per-sample rayQueryEXT is deleted.

  Measured on the RX 6600, E1M1 courtyard, 50% scale, three runs a cell,
  -noinput throughout: fog High +11.05 ms / +44.4% -> +0.71 ms / +3.2%
  against a 15% bar; fog Low (the shipped default) +43.7% -> +4.7%;
  28 -> 42 fps. Level-load fill 0.8-0.9 ms; a clearance rebuild after a
  plane moves is 0.12 ms typical, which closes Q30 with a number -- no
  rate limit needed. -shotverify MAE 0.051-0.079/255 across E1M1/E1M3/E1M6
  against a 3.0 bar; -rtverify PASS; make test 7/7.

  Two findings worth keeping. (1) The fog-OFF control moved, 14.41 ->
  12.22 ms, which the plan says must not happen -- so it was chased rather
  than waved through: a third worktree at pre-L2 measures 12.01 ms, i.e.
  L2's ray cost 2.40 ms/frame WITH FOG SWITCHED OFF. rb_fog is a push
  constant, not a spec constant, so a ray inside the fog branch still
  costs registers on every pixel. "It is gated, so it is free when off" is
  false in this shader. (2) -noinput did not exist at the before-commit
  (it landed in b23d609), so the old build grabbed the pointer and the
  user's mouse turned its camera mid-run; any A/B against a pre-b23d609
  commit must cherry-pick it into the worktree first.

  Two corrections to the plan, both forced by measurement: the void
  test's tie-break nudge is 1 world unit, not the plan's quarter-cell
  (the subsector is resolved at the UN-nudged point and BSP leaves are
  far smaller than a 64-unit cell -- 16 units falsely called 588 of
  E1M1's 920 open-sky cells void); and rb_cellgeom_t must carry `isvoid`
  separately from `solid`, or the clearance-only rebuild re-derives the
  height clause and erases the void verdict.

  User play-test sign-off 2026-07-31: "very happy with the result ...
  damn, I do love the fog." Three look observations came out of the same
  session -- DOOM-0292 (roofed fog lit as if the sky reached it),
  DOOM-0293 (liquid pools want their own fog) and the local-light
  in-scatter, which is Task L3's existing job.

- ✅ [DOOM-0292] **Roofed fog is lit as if the sky reached it -- gate the sky ambient on sky exposure.**
  User, on the DOOM-0289 play-test: "fog is generally very white when
  outside as the sky / sun are lighting it up but under roof that won't
  be the case. So, the fog should be a little darker inside."

  Correct, and it is a real gap in L2's model rather than a taste dial.
  marchFog builds the sample's in-scatter as

    Ls = kFogColor * kSkyShaftStrength
       * (kSkyAmbientFrac + (1 - kSkyAmbientFrac) * sunLit)

  and only the DIRECTIONAL share is gated on visibility. The ambient
  share (kSkyAmbientFrac = 0.65) is applied at full strength everywhere,
  so a sealed room's air receives the same sky in-scatter as a courtyard
  in shadow. L1d already grades roofed air's DENSITY by distance to
  outdoor air; nothing grades its LIGHT.

  The signal is already in hand and costs nothing: the same seep tap the
  sample takes for openSky/density carries fld.r, distance-to-outdoor-air
  through open space. Gating the ambient share on the same exp(-fld.r /
  kSeepFalloff) curve keeps the doorway continuous (air one step inside a
  door still sees most of the sky) and takes a sealed room to a floor --
  so this is one mix and one new const, no extra tap and no ray.

  Deliberately NOT a straight reuse of skyExposure: that already
  multiplies density, and multiplying brightness by the same 0.05 floor
  would take indoor fog to black rather than to "a little darker". The
  light floor is its own, gentler constant.

  Pairs with Task L3 (torch in-scatter): darkening the sky share is what
  makes the torches worth adding, and L3 is what stops a deep room from
  reading flat once its sky light is gone. Land this first, then L3 gives
  the light back where a light actually is.
  **Layman:** Fog indoors is as bright and white as fog outside in the sun. Under a roof there is no sky lighting it, so it should be darker.
  Kind: fix.
  Lanes: shaders, fog.
  Source: user-play-test-2026-07-31.
  Progress (2026-07-31): IMPLEMENTED (43c6413), awaiting the user
  play-test. Built as described above -- one mix on the existing seep
  tap, kIndoorSkyLight = 0.45, the directional share deliberately left
  ungated so a shaft through a roof light keeps its contrast.

  Verified: E1M1's roofed spawn falls 47.33 -> 37.36 mean (-21%), E1M6
  71.09 -> 62.81, E1M3 -1.5 (a view already mostly open air). Rebuilt
  with the constant at 1.0 and re-shot the same views -- MAE 0.0024 and
  0.0036 /255 against the pre-change build, i.e. the same-build noise
  floor, so the dial is the only thing that moved and outdoor air is
  untouched by construction. Megakernel 12.9 -> 13.0 ms, 42 fps both.
  make test 7/7, -rtverify PASS.

  Left for the play-test, because a still frame cannot settle it: whether
  0.45 is the right amount of darker. It is a floor on LIGHT, not on
  density, so it can go to 0.0 once L3 gives torches something to say --
  the number is the dial to bring to that session, not a value to defend.
  USER SIGN-OFF 2026-08-04: "Definitely, colour looks excellent." That closes
  the one thing the bullet left open -- whether kIndoorSkyLight = 0.45 is the
  right amount darker for roofed air, which a still frame could not settle.
  The value is now a signed-off look, not a first guess.
  Everything else had already been verified before this: the ambient share is
  gated on the seep grade (pathtrace.comp, `skyLight = mix(kIndoorSkyLight,
  1.0, seepT)`), the outdoor branch is untouched by construction (re-shot with
  the constant at 1.0 -> MAE 0.0024 and 0.0036 /255 against the pre-change
  build, i.e. the same-build noise floor), megakernel 12.9 -> 13.0 ms, make
  test 7/7, -rtverify PASS.
  Note the constant stays a dial rather than a defended value: it is a floor
  on LIGHT, not on density, so it can still go toward 0 if L3's torches end up
  giving roofed air enough to say on their own.

- 📋 [DOOM-0293] **Liquid pools should carry their own fog -- a per-cell liquid mask on the field.**
  User, on the DOOM-0289 play-test: "any liquid (not puddles on the
  floor but actual pools should have more fog too please." I.e. the
  sector-sized nukage/lava/water flats, not DOOM-0181's wet-floor
  grime.

  DOOM-0183 already tags liquid, but on the MATERIAL, at the point a
  surface is shaded -- and the fog march never touches a surface. What
  the march needs is a function of (x, y): is there liquid under this
  sample, and at what height is its surface. That is exactly the shape
  DOOM-0276 and DOOM-0289 both answered from the seep grid.

  The cost is that the grid is now FULL: R16G16B16A16_SFLOAT carrying
  seep distance, the open-sky mask, and DOOM-0289's zLo/zHi. So this one
  needs a second image (RG16F: .r = liquid coverage 0..1, .g = the
  liquid surface z) and a second bilinear tap per fog sample -- the first
  of these three fog items that is not free. Bilinear coverage is a
  feature, not a cost: it gives the bank a soft edge at the pool's rim
  instead of a 64-unit staircase.

  Why the surface z has to travel with it: a pool sits BELOW the floor
  around it, and L1e's floor layer is referenced to the camera's floor
  indoors -- so without the pool's own height the steam would float at
  the wrong altitude, which is the two-clouds defect the L3 correction
  already fixed once.

  Design change, so it wants the spec amendment and the rule-14 gate
  before it is built -- and the review's own note on DOOM-0289 stands:
  the spec is past 2.7k lines and should be SPLIT before it takes
  another amendment.
  **Layman:** Nukage and lava pools should steam -- thicker fog sitting over the liquid itself, not over the whole room.
  Kind: enhancement.
  Lanes: shaders, fog, r_mesh.
  Source: user-play-test-2026-07-31.
  Design refinement (2026-08-01), from a discussion with the user plus
  a read of how the industry does it. Three changes to the sketch above,
  each making it cheaper than "a second image and a second tap" sounds:

  1. ONE CHANNEL, NOT TWO. The bullet above says RG16F (coverage +
     surface z). It only needs the STEAM LAYER'S TOP Z per cell, with
     non-liquid cells storing a finite sentinel far BELOW any floor --
     the same finite-sentinel idiom RB_SEEP_DMAX and RB_SUN_NEVER
     already use, and for the same NaN reason. Bilinear then sinks the
     layer smoothly to nothing across a pool's rim, so the soft edge
     comes free from the sampler instead of costing a channel.

  2. MOST SAMPLES SKIP THE TAP. Compute the level's highest liquid
     surface once at load and push it as one scalar; any fog sample
     above it plus a few steam heights never reads the image, and a
     level with no liquid at all pays nothing. The march bunches its
     samples near the camera (Q26's quadratic warp) and the steam layer
     is shallow, so this kills the majority of the reads in practice.

  3. IT MAY NEED NO NEW TERM. L1e's floor layer is already a
     short-range, height-pooled addend. Pool steam may be that layer
     with its baseZ and density driven by the liquid channel, which is
     a far smaller change than a fourth kind of fog. Try that before
     writing a new one (reuse before rewriting).

  Rejected on inspection: folding the liquid signal into the existing
  RGBA by making the open-sky mask signed (+1 sky / -1 liquid). Bilinear
  between +1 and -1 crosses zero, so a cell between a sky cell and a
  liquid cell would read "neither" -- an invisible coupling between two
  unrelated facts, for one channel's saving.

  ART, not code: fog forms over water when the water is WARMER than the
  air, so the user's "cold liquids" framing inverts the physics -- which
  does not matter for the look but does split it in two. Nukage and lava
  are plausibly out-gassing, so they want RISING wisps; water wants a
  flat, still, low mist. DOOM-0183's material bits already distinguish
  nukage from lava (RB_FLAG_LIQUID_NUKAGE / _LAVA, rb_materials.h), so
  the split is cheap -- but note only NUKAGE1-3 and LAVA1-4 are tagged
  by name today (FlagLiquidFlats, r_vulkan.cpp). Water (FWATER1-4) and
  DOOM 2's blood/slime flats are NOT, so they would steam not at all
  until added.

  Synergy worth taking: DOOM-0183 already makes lava glow and cast
  light. Give it steam and the pool lights its own steam -- the emitter
  and the medium are the same object, for no extra work.

  Industry check (Unreal's local fog volumes; particle-injected
  density): everyone bakes localised fog into a volume rather than
  querying scene geometry per sample, which is the direction this bullet
  already takes. Nothing found that beats a per-cell number for an
  engine that already marches a grid.
  Liquid-flat inventory, taken 2026-08-01 from the two IWADs and from
  the engine's own animated-flat table (p_spec.c animdefs, ~:105-115) --
  because the user's standing constraint is that this must be applied
  ONCE and look right across every map in BOTH games, which makes "which
  flats count" a question to answer from data, not from memory.

  Tagged today (FlagLiquidFlats, r_vulkan.cpp): NUKAGE1-3, LAVA1-4.
  Present in doom.wad AND doom2.wad but NOT tagged: FWATER1-4, BLOOD1-3.
  Present in doom2.wad only, NOT tagged: SLIME01-16.

  The engine's animdefs is the better source than a hand-list, since it
  is what vanilla itself treats as a flowing surface: NUKAGE1-3,
  FWATER1-4, LAVA1-4, BLOOD1-3, SLIME01-04, SLIME05-08, SLIME09-12.
  Two cautions before copying it wholesale. RROCK05-08 animates and is
  ROCK, so animation alone does not mean liquid. And SLIME09-12 (and the
  un-animated SLIME13-16) need an eyeball -- some of that range is
  corroded metal, not sludge. Check them on screen before tagging.

  A second, independent signal worth combining rather than choosing
  between: DOOM's DAMAGING FLOOR specials (p_spec.c ~:1025-1039 --
  nukage damage, hellslime, super hellslime). Anything that hurts you to
  stand in is liquid. It misses water, which is why it is a corroborate
  and not a replacement -- but flat-name OR damaging-special covers both
  IWADs and degrades sanely on a custom WAD that uses its own flat
  names, which no hand-list can. That combination is what makes "apply
  it once and it is right everywhere" achievable rather than aspirational.

- ✅ [DOOM-0294] **Developer view: jump to any level and stop the monsters noticing you, so the game can actually be tested.**
  User, 2026-08-01: "I need a developer view in the game that allows me
  to jump to any stage, turn on invincibility (or rather make the enemies
  not see me) so that I can test the game more thoroughly."

  This is a TESTING instrument, and it is the bottleneck on every look
  task in this project. Every renderer feature since DOOM-0011 started
  has been signed off from one or two spawn views, because reaching a
  specific room means playing there -- and the assistant cannot help:
  xdotool cannot inject input under Wayland, and the attract demos are
  version-mismatched against wads/doom.wad (109 vs 110) so they abort
  before a level loads. The workaround used so far is a throwaway hook in
  P_UpdateSpecials, compiled in and deleted before commit, which is not
  something the user can drive.

  Three parts, all small, and every one of them has its mechanism already
  in the tree:

  1. LEVEL JUMP. The cheat path exists (cheat_clev, st_stuff.c:453, into
     G_DeferedInitNew) but is typed blind and, per DOOM-0287's session,
     does not reliably register. Wanted as a MENU: pick episode+map for
     DOOM 1 or map 1-32 for DOOM 2. Which game is loaded is already known
     -- DOOM-0060 built the game-select chooser -- so the row set can
     follow it rather than being asked for.

  2. MONSTERS DO NOT NOTICE YOU. The user's own correction is the right
     design: not invincibility, which leaves them shooting and shoving.
     Add CF_NOTARGET = 8 to the flags in d_player.h:71-75 (CF_NOCLIP=1,
     CF_GODMODE=2, CF_NOMOMENTUM=4 -- 8 is free) and test it in
     P_LookForPlayers (p_enemy.c:499), which is the single choke point:
     A_Look (:623), A_Chase (:705, :756) and the respawn path (:1979) all
     route through it. CAUTION, and it is the whole difference between
     this working and half-working: that only stops NEW acquisition. A
     monster already awake keeps its target, so the toggle must also
     clear existing ones, or every room you have already walked into
     stays hostile.

  3. GET TO A PLACE, not just a level. -warpto X Y ANGLE already exists
     on the command line and is what the profiling recipe uses. Exposing
     it (plus a "print where I am now", which cheat_mypos, st_stuff.c:460,
     already computes) closes the loop with DOOM-0288's map-coordinate
     discovery and the skyspots.py prototype: find a spot from the WAD,
     jump to it, look at it. That is the whole test loop for a look task.

  Menu placement: follow the pattern already proven four times over --
  GameSelectDef, RendererDef, EffectsDef, VideoDef in m_menu.c are all
  project-added menus. READ DOOM-0206 FIRST: the menu redesign spec is
  written and gated but not implemented, so a Developer menu added now
  should either follow its conventions or be explicitly listed as a
  consumer of them, rather than becoming a fifth thing the redesign has
  to absorb without knowing about it.

  Gating: this is a developer tool in a GPL fork, not a shipped cheat
  menu -- decide whether it hides behind a -devmode command-line flag or
  a config key. Do not make it reachable by accident in normal play.
  **Layman:** An in-game menu for testing: pick any level and jump straight to it, and switch off the monsters' attention so you can walk a map and look at it without fighting through it.
  Kind: feature.
  Source: user-request-2026-08-01.
  Progress (2026-08-01): IMPLEMENTED, awaiting the user play-test.
  Built as three small pieces, all in the existing mechanisms.

  1. CF_NOTARGET = 8 (d_player.h) gated in P_LookForPlayers, plus
  P_ForgetPlayerTargets (p_enemy.c) which walks the thinker list and
  drops every monster whose target is a player. A THIRD gate the item
  did not name turned out to be needed: A_Look reads
  sector->soundtarget directly and never calls P_LookForPlayers, so
  P_NoiseAlert would still wake a room on the noise a NOTARGET player
  makes -- gated there too, or the toggle only works in quiet rooms.

  2. A Developer menu (m_menu.c) reached from a "Developer" row spliced
  onto Options by `-devmode`, using the Game Select row's own trick (the
  template sits past the default numitems, so an ordinary launch does
  not merely disable the row, it never draws it and the cursor cannot
  land on it). A command-line flag rather than a config key on purpose:
  a remembered "on" is a trapdoor that stays open.

  3. ONE "Level" row, not an episode row plus a map row -- the level
  list is flattened to a single index, so the row set is the same shape
  in both games and neither carries a dead row. It opens on the level
  you are standing in. "Print Position" prints the current spot AS the
  -warpto line that reproduces it (DOOM-0268), to stdout and the HUD.

  DOOM-0206 was read first as the item asked, and the item's claim about
  it was STALE: it says the menu redesign is "written and gated but not
  implemented", but DOOM-0206 shipped 2026-07-21. So the Developer menu
  is a consumer of the shipped crisp registry rather than a fifth menu
  the redesign would have to absorb -- one crispMenus[] entry plus a
  classic-tier draw, holding INV-1/2/4/7.

  Verified headlessly against both IWADs with a temporary in-engine
  harness (removed before commit; `grep TEMP-DEVTEST` is clean):
  - gate: Options is 9 rows with -devmode, 8 without.
  - level list: retail 36 (E1M1..E4M9), commercial 32 (MAP01..MAP32),
    wrap in both directions, opens on the current level (E3M5, MAP07).
  - targets: E3M5 with all 63 monsters forced onto the player -> 0 after
    the toggle; doom2 MAP07 12 -> 0.
  - acquisition, with the player planted next to a monster so sight is
    not the variable: acquiredWithFlag=0, acquiredWithoutFlag=1. The
    CONTROL is the point -- run from a spawn point both legs read 0,
    because nothing can see you there, and that proves nothing at all.
  - round-trip: the printed `-warpto -544 640 180` fed back in placed
    the player at (-544,640) angle=128 (=180 deg), i.e. the line is
    consumable verbatim.
  make test 7/7, -rtverify PASS (rel-MSE 0.0796%, white furnace
  0.000000) -- no renderer file touched.

  Left for the play-test, because headless capture cannot reach it: the
  crisp menu's LOOK in Solid/Ultra (same standing limitation DOOM-0206
  shipped under -- Wayland blocks input injection, so no automated run
  can open a menu). Also worth confirming in play: that walking into a
  room you already cleared stays quiet, which is the half CF_NOTARGET
  alone would not give.

  Not built, and deliberately: shooting still provokes retaliation
  (P_DamageMobj sets target from the damage source, which is the player
  noticing THEM). Say so if it is unwanted; it is a two-line gate.
  Progress (2026-08-01, second pass): fleshed out on the user's ask,
  and the GATING DECISION CHANGED -- read this before the note above,
  which describes the superseded `-devmode` runtime flag.

  User: "When we publish a release on GitHub I don't want the developer
  menu part of it. So, whatever approach allows us to have the developer
  mode but gamers do not, go with that option."

  So the gate moved from runtime to COMPILE time: `make DEV=1` defines
  DOOM_DEV and compiles the Developer menu and its tools in; a plain
  `make` leaves them out of the binary entirely. Default OFF is
  fail-safe rather than a preference -- every release path already runs
  a plain make (packaging/linux-build.sh, build-appimage.sh,
  windows-build.sh, release.sh, .github/workflows/build.yml), so a
  published build is clean without any of them having to remember
  anything, and shipping the tools can never be the consequence of one
  forgotten flag. run-doom-ants.sh (the panel icon) builds DEV=1; the
  -devmode parm is gone.

  VERIFIED, not asserted: a plain-make binary greps 0 for every
  developer string ("Monsters Notice You", "Jump to Level", "Give Keys
  & Weapons", "Developer") where the DEV=1 one greps 1, and carries no
  developer symbols. A DEV toggle also forces a rebuild -- a -D is
  invisible to the .d files, so a stamp file carrying the current
  setting is a prerequisite of every object.

  The menu is now 16 rows: Mode (Play/Inspect); WORLD (monsters notice
  you, freeze monsters, no clip, invulnerable, give keys & weapons);
  LEVEL (level, skill, jump, print position); VIEW (RT view, video &
  effects, back).

  Three design points worth keeping:
  - MODE IS DERIVED, not stored -- computed from the switches below it,
  so it cannot claim Play while one is still on. Play = everything off
  (test a level as a player meets it, which the user asked for);
  Inspect = notarget + invulnerable. No Clip is deliberately not in the
  preset: it changes how moving feels, so it is worth choosing.
  - FREEZE IS MONSTERS + THEIR MISSILES ONLY. The user's objection to a
  whole-world freeze was right -- "how will I move around the world if
  doors don't work anymore?" Doors, lifts and animated textures are
  excluded by construction: the guard fires only on P_MobjThinker,
  which a door thinker can never equal.
  - THE RENDER TOGGLES ARE NOT COPIED. They stay in the Video menu
  (shipped features a player also gets); Developer links to it. Two
  copies would be two things to keep in step.

  The Classic tier needed its own scroll: 16 rows x LINEHEIGHT is 256px
  against ~144px above the status bar, and the generic HUD-safe shift
  assumes every row is drawn, so it would push the top off-screen.
  M_DrawDevMenu takes the placement over and windows on itemOn the same
  way the crisp renderer does (INV-4 holds). Writing currentMenu->y is
  safe because M_Drawer restores it, and the skull then lands right for
  free.

  Verified headlessly on both IWADs with a temporary harness (removed;
  grep TEMP-DEVTEST clean): mode derivation across Play -> Inspect ->
  +noclip -> Play (cheats 0 -> 10 -> 11 -> 0); each switch; give (blue
  card, BFG, ammo 200); skill/level wrap; RT view stepping 0,1,2,3,4,6
  with Debug Views on for the four diagnostics only; the scroll window
  (itemOn 0/5/10/15 -> scrollTop 0/1/6/7, clamped at 16-9);
  print-position. make test 7/7 in BOTH configurations.

  FREEZE: the first version of that test was BLIND, and why is worth
  recording -- 10 thinker calls happened to land the monster's tic
  counter back on its start value, so both legs read "unchanged" and
  the control agreed with the experiment. Printing the reading per step
  fixed it: frozen 7 7 7 7, running 6 5 4 3. A control that cannot move
  is not a control.

  STILL OWED: (1) the in-game screenshot the user asked for. The
  existing -shotverify copy lives inside the RT path and exits after
  writing; a general one wants the swapchain image (both 3D paths leave
  it in PRESENT_SRC), and the swapchain is created without TRANSFER_SRC
  usage, so that flag must be added where the surface supports it. NOT
  started. (2) the play-test of the menu's look, unchanged.
  Progress (2026-08-01, third pass): the in-game SCREENSHOT is done, so
  the "still owed" item in the note above is closed except for the
  play-test.

  F12, or Developer > Capture Screenshot, writes the frame to
  dev-shots/shot-NNNN.png at full display resolution. Both triggers set
  a countdown in presents rather than capturing immediately -- the delay
  is what lets the menu close first, so the picture is of the game and
  not of the menu that asked for it.

  WHERE it copies from is the design decision. -shotverify's existing
  copy is inside RecordRtTrace, i.e. the ray-traced path only, and it
  exits after writing. This one copies the SWAPCHAIN image instead --
  the frame the screen is about to show -- which both recording branches
  leave in PRESENT_SRC by the time the command buffer closes. One block
  therefore covers the raster and ray-traced views alike, needs no
  knowledge of either, and picks up the HUD and everything else drawn
  over the scene. The swapchain had to gain TRANSFER_SRC usage to be
  readable; that is asked for only when the surface reports it
  (supportedUsageFlags), because requesting an unsupported usage fails
  swapchain creation outright -- the whole renderer lost to gain a
  screenshot. Without it the capture refuses with a message.

  Classic routes to DOOM's own G_ScreenShot (.pcx) instead: the 1997
  software renderer never builds an image for Vulkan to present. Worth
  noting because the first version of this got it wrong in comments --
  that path is normally reachable only as F1 under -devparm, which is no
  use to someone who did not launch with the flag, so F12 now covers all
  three tiers.

  VERIFIED on hardware, by looking at the files rather than by trusting
  the exit code: Solid raster and Ultra RT both wrote correct 3840x2160
  PNGs of E1M1 (colours right -- the swapchain is BGRA and is swizzled;
  alpha forced opaque, since a presented image's alpha means nothing and
  left alone the PNG opens transparent in some viewers), and Classic
  wrote a correct DOOM00.pcx.

  THE VALIDATION CHECK IS WORTH RECORDING, because the first reading of
  it was worthless. A plain run reported zero validation messages, which
  proves nothing on its own -- an inactive layer and a clean frame look
  identical. Re-running with best-practices enabled produced 31 messages
  (so the messenger demonstrably reports) and ONE of them was against
  this change: the PRESENT_SRC -> TRANSFER_SRC barrier named
  COLOR_ATTACHMENT_WRITE|TRANSFER_WRITE as its source access, where an
  image already in PRESENT_SRC has no pending access to flush and the
  expected mask is 0. Fixed; the source STAGE still names both, since
  that is the execution dependency that matters. Re-run: capture-related
  messages 0, control still live at 31.

  Both configurations build warning-free and make test is 7/7 in each;
  the release binary still greps 0 for every developer string, now
  including the new ones. dev-shots/ is gitignored.

  STILL OWED: the play-test of the menu and the capture on hardware,
  driven by hand.
  Progress (2026-08-01, fourth pass): USER PLAY-TESTED the menu and the
  screenshot -- "works perfectly" on both -- with two defects, both now
  fixed. The play-test owed on the earlier notes is therefore satisfied
  except for confirming these two.

  1. INSPECT MODE ENDED IF YOU HURT A MONSTER INDIRECTLY. User: "I had
  it on inspect and so the enemies didn't notice me but I blew up a
  barrel that damaged a monster and then that monster became aware of
  me." Correct, and it is a THIRD acquisition path that neither of the
  first two gates covers: P_DamageMobj sets target = source, which needs
  no line of sight (P_LookForPlayers) and no noise (A_Look's
  soundtarget). Gated the same way in p_inter.c. The monster still takes
  the damage and still flinches; it just does not decide the player is
  its enemy. The barrel case falls out for free: a barrel shot by a
  NOTARGET player now records no owner either, so its blast has nobody
  to blame (P_RadiusAttack already handles a null source).

  Earlier notes called retaliation a deliberate non-feature -- "shooting
  still provokes retaliation... say so if it is unwanted". The user has
  said so, so that line is superseded.

  2. THE MODE RESET ON EVERY LEVEL JUMP. G_PlayerReborn memsets the
  player, which is what a pistol start IS, so the cheat bits went with
  it. The three developer bits (NOTARGET / NOCLIP / GODMODE) are now
  saved across that wipe: Inspect is a property of the testing session,
  not of the player. In Play mode the bits are zero, so nothing carries.
  Freeze already persisted (a plain global).

  Both verified headlessly with live controls, which is what makes the
  results mean anything:
  - hurt in Inspect -> target=none; the same damage with Inspect off ->
  target=PLAYER. The control moves.
  - a barrel damaged by a NOTARGET player -> owner=none.
  - across a real G_DeferedInitNew level jump, cheats 14 -> 10: the two
  developer bits survived and CF_NOMOMENTUM (a NON-developer bit,
  included precisely as the control) did NOT. That distinguishes
  "the right bits persist" from "the wipe stopped working".

  Both configurations build warning-free; make test 7/7 in each; the
  release binary still greps 0 for every developer string.

  NOTE for whoever reads the diff: the first cut of the G_PlayerReborn
  edit also stripped trailing whitespace from ten untouched 1997 lines
  (rule 11 -- stay in your lane). Redone as 15 pure insertions.
  Verified headlessly (2026-08-01), closing the play-test the user was
  owed on 948b0d1's two fixes. No playing needed: a throwaway P_Ticker
  harness (armed by -l3devtest, reverted after) drove both cases and
  printed the result, run twice -- once in Inspect mode and once as a
  CONTROL in Play mode.

    1. A BARREL BLAST MUST NOT WAKE THE MONSTER IT DAMAGES. Spawned a
       barrel beside the toughest live monster on E1M2, had the player
       shoot it, and read the monster's target after the blast:

          INSPECT   barrel owner = NULL     monster target = NULL
          CONTROL   barrel owner = PLAYER   monster target = PLAYER

       Both hops move with the mode, which is the proof: the barrel
       records no owner, so its blast carries source = NULL, and
       P_DamageMobj's acquisition block is gated on `source &&`.

    2. THE MODE SURVIVES A LEVEL JUMP. G_DeferedInitNew to E1M2, then
       read the cheat word on the new level: 0x8 (NOTARGET set) in
       Inspect, 0x0 in the control. It survives the G_PlayerReborn memset.

  Two traps the harness hit first, worth recording because either one
  produces a CONFIDENT FALSE PASS -- both arms agreeing, which reads as
  "the fix works" when it means the test never ran:

    - A LETHAL HIT PROVES NOTHING. P_DamageMobj reaches P_KillMobj and
      RETURNS at p_inter.c:116, before the target-setting block at :149.
      The first attempt killed both the barrel outright and the monster
      with the blast, so neither arm ever executed the code under test
      and both printed target=NULL. The damage has to be survivable at
      each step: a non-lethal 5 to the barrel first (that is what records
      the owner), and the monster far enough out that (128 - dist) wounds
      rather than kills.
    - AN UNSEEN BARREL DOES NOT EXPLODE ON ANYTHING. P_SpawnMobj does no
      collision check, so a barrel placed blind can land where
      PIT_RadiusAttack's own P_CheckSight then drops the blast -- the
      monster took zero damage and, again, both arms agreed. Fixed by
      testing 8 compass points with P_CheckSightTrace (DOOM-0011 L3's new
      helper) and spawning at the first one the monster can see.

  The general lesson for this project's headless self-verification: when
  both arms of an A/B agree, suspect the harness before believing the
  result. A control that cannot move is not a control.
  Progress (2026-08-04): the developer view gained the three things that
  were stopping it being driven from a script, all DOOM_DEV-gated.
  - `-inspect` (g_game.c G_DevInspectFromArgv, called from G_DoLoadLevel
    beside G_WarpToSpot) applies the menu's Inspect preset from argv:
    CF_NOTARGET | CF_GODMODE + P_ForgetPlayerTargets, exactly the pair
    M_DevMode sets. `-freeze` sets dev_freezemonsters. Until now that
    preset was reachable ONLY through the menu, and a menu is what an
    automated run cannot reach (no Wayland input injection).
    Why it mattered, measured: an A/B of the DOOM-0183 wet layer taken in
    a live level reported 15.05% of pixels moved against a 0.15% control.
    With the world held still the same A/B reads 13.83% against a 0.00%
    control (max channel delta 1). The earlier figure was partly a monster
    walking through frame and the nukage damage counter ticking down --
    motion indistinguishable from the effect under test.
  - `-devshot N` now works in the CLASSIC tier too (i_video.c
    I_DevShotClassic, reading the SDL backbuffer after RenderCopy and
    before RenderPresent). It was previously Vulkan-only, so in Classic
    the flag was a SILENT no-op -- which is worse than an error, because a
    harness then picks up whatever PNG was already on disk. That is not
    hypothetical: it produced a wrong "Classic shows the same seam"
    reading during the DOOM-0180 investigation before the harness was
    made to fail on a missing file. F12's existing Classic .pcx route
    (G_ScreenShot) is unchanged; this is the scriptable path and writes
    the same dev-shots/shot-NNNN.png the other tiers write.
    The first cut segfaulted: SDL_RenderGetViewport returns LOGICAL units
    (320x200 under Classic's SDL_RenderSetLogicalSize) while
    SDL_RenderReadPixels(NULL) reads the whole output target in pixels, so
    sizing the buffer from the viewport overran it by the square of the
    scale factor. Fixed to SDL_GetRendererOutputSize.
  - The dev-shot naming loop is now shared (rb_image.c rb_devshot_path),
    called by both present paths, so all three tiers write one scheme into
    one directory.
  Build green DEV and release; make test 7/7.
  Resolved (2026-08-04): -inspect / -freeze / -devshot are shipped and pushed, DEV and release builds clean, make test 7/7, and the flags were exercised roughly twenty times in this session's DOOM-0316 measurement work -- which is the strongest acceptance the item could ask for. Already carried four CHANGELOG entries; only the roadmap flip was owed. NOTE the scope boundary confirmed today: -devshot reaches every WORLD view but cannot open a menu, and it is NOT headless (verified 2026-08-04 -- with DISPLAY and WAYLAND_DISPLAY both unset SDL still opens a real window and the run hangs rather than exiting). Headless capture stays DOOM-0268's; menu capture stays blocked, see DOOM-0050.

- 📋 [DOOM-0295] **L3's torch in-scatter costs 1.05 ms of megakernel -- find out where.**
  Measured on an RX 6600 at the E1M1 nukage courtyard, Ultra RT with HD
  art, the gain constant the only variable: megakernel 10.11 -> 11.16 ms,
  58 -> 55 fps. That puts the whole fog feature at roughly 9% of
  present-total (inside DOOM-0011's 15% gate) but makes L3 the single
  most expensive part of it, ahead of everything DOOM-0289 saved.

  What it is NOT: the window's squared term. That was assumed to be a
  pow() the driver would not fold, rewritten as a multiply, and measured
  at 11.18 ms either way -- RADV folds it already. The assumption is
  recorded because it is the obvious first guess and it is wrong.

  Remaining candidates, cheapest to test first: (a) fogPhaseHG's
  pow(x, 1.5), evaluated 24 samples x 2 lights per fog pixel, against a
  Schlick phase approximation which is a divide; (b) register pressure --
  this is a megakernel and DOOM-0289 already measured L2's ray costing
  2.40 ms with fog switched OFF, so occupancy is a known lever here and a
  table lookup costing 1 ms smells like it; (c) kFogLightsPerCell 2 -> 1,
  which is the quality trade of last resort (a room with two lamps loses
  one) and should not be taken before (a) and (b) are measured.

  Do NOT take the cost out of the effect first. The standing constraint
  is "does it read? then, what does it cost?" -- and it took a full
  re-tune to make this read at all.
  **Layman:** The new torchlight-in-fog effect costs about 3 frames per second. Worth a look to see if it can be cheaper.
  Kind: perf.
  Lanes: shaders, fog, perf.
  Source: in-session-2026-08-01.
  Progress (2026-08-02, 7f68bbe): candidate (a) CONFIRMED but it is only
  26% of the cost. pow(x, 1.5) needed no Schlick approximation -- it is
  exactly (x^-0.5)^3, so an inversesqrt cubed is the SAME curve, no
  re-tune. glslc -O does not fold a 1.5 exponent (33 Pow survive);
  SPIR-V after: 4 Pow + 4 OpFDiv gone, 4 InverseSqrt added.

  Re-measured today, 3 runs per build, 48 pooled samples each, medians,
  idle machine, E1M1 nukage courtyard (-warpto 1866 -3221 45), Ultra RT
  with HD art, 50% scale:

    torch disabled (floor)   13.38 ms   46 fps
    before                   14.93 ms   41 fps
    after                    14.53 ms   43 fps

  NOTE the baseline correction: L3 costs 1.55 ms at this spot, not the
  1.05 ms in the headline -- more torches in view. The 0.40 ms recovered
  is 26% of it.

  STILL OPEN: the remaining 1.15 ms. Candidate (b) register pressure is
  untested -- get VGPR/occupancy from RADV_DEBUG=shaderstats and compare
  against the floor build. Candidate (c) kFogLightsPerCell 2 -> 1 remains
  the quality trade of last resort.

  Tooling note for whoever picks this up: -shotverify PINS a canonical
  config and cannot be used for effect A/Bs; use -devshot N (DOOM-0303).
  Progress (2026-08-02): candidate (b), register pressure, is FALSIFIED.
  Measured, not argued: RADV_DEBUG=shaderstats on the RX 6600 (Mesa
  26.1.5), -rtverify -warp 1 1 on doom.wad, the only variable
  kTorchShaftStrength 0.047 vs 0.0 -- a const zero folds the whole torch
  loop away, so that is the same floor build the 13.38 ms came from.

  The megakernel (the 5133-instruction compute shader):

                        L3 on   L3 off
    VGPRs                  96       96
    Spilled VGPRs           0        0
    Subgroups per SIMD      8        8
    Code size           28300    27660
    Instructions         5133     5009
    VALU                 3232     3132
    VMEM                  159      155

  Occupancy is bit-identical with the feature on and off, so occupancy
  cannot be the mechanism. L3 costs zero registers and zero spills.

  Nor is 8 waves/SIMD a register ceiling: 96 VGPRs would allow 10
  (1024/96). The cap is the 8192-byte LDS the pipeline reports, and the
  only two shaders in the whole build reporting 8192 are the two using
  rayQueryEXT (pathtrace.comp, bake.comp) -- every non-ray-query compute
  shader reports 0. 8192 = 128 bytes x an 8x8 group, i.e. a per-invocation
  BVH traversal stack. That last step is a CORRELATION across this
  build's 26 pipelines, not a read of RADV's source; if it holds, the
  ceiling is the driver's and not ours to move. Flagged, not claimed.

  What the 1.15 ms actually is: dynamic issue inside the march. The torch
  loop IS unrolled (2 lights -> +4 VMEM statically) and the 24-sample
  march is NOT, so every fog pixel pays the static delta 24 times --
  roughly 2400 extra VALU and 96 extra vector loads.

  So the remaining levers are structural or quality, never occupancy:
    (c) kFogLightsPerCell 2 -> 1. Still the last resort.
    (d) evaluate the torch term every OTHER march sample and lerp. The
        term is smooth in t, so this is ~0.5 ms for little visible cost.
    (e) cache the cell's light list across samples -- kills the 96 loads,
        leaves the VALU, so it is the smaller half.
    (f) a closed-form line integral per light in place of the 24-sample
        sum: the same substitution DOOM-0276 and DOOM-0289 made. Biggest
        win by far, but density varies along the ray (pooling, wisps,
        seep), so it is an approximation with a look consequence and
        wants a spec before code.
  Progress (2026-08-02, second): lever (d) shipped -- the torch term is now
  integrated at HALF the march's rate, once per pair of samples at the
  pair's midpoint, spent on both.

  Same instrument and spot as the falsification above (RX 6600, Mesa
  26.1.5, E1M1 nukage courtyard, Ultra RT + HD art, 50% scale, 3 runs x
  21 samples, medians, idle machine):

    rt_fog 3 High   15.29 -> 14.96 ms   floor 14.14   L3 1.15 -> 0.83 ms
    rt_fog 1 Low    15.31 -> 14.96 ms                 0.35 ms recovered
    (Low is the SHIPPED DEFAULT, and the saving is slightly LARGER there
     -- thin fog never trips the trans < 0.003 early-out, so more samples
     run and there are more evaluations to halve. Measured because a cold
     reviewer asked, not assumed.)

  The 1.15 ms independently REPRODUCES the figure this bullet recorded on
  2026-08-01, even though both absolute numbers sit ~0.76 ms higher today
  -- so the two sessions' machines differ but the delta does not.

  Recovered 28%, not the 50% halving implies, and that gap is now written
  into the spec: the floor deletes the whole term, while a rate halving
  keeps the Ls addend on every sample and the carried register. Anyone
  reusing the trick should predict 28%.

  Look: MAE 0.055/255, worst pixel 10/255, over the 99.6% of frame a
  same-build control pair holds stable (control MAE 0.0041). That is 13x
  the noise floor and 55x UNDER -shotcompare's kGoldenMAE bar of 3.0.
  Confined to near-floor fog, mottled not banded. -rtverify INV-6
  identical either side (0.1091%, bar 0.50%) -- measured on both builds,
  not assumed. make test green.

  Also fixed, because a cold reviewer found it: an odd kFogSteps would
  have sampled past tMax on the unpaired final index. The guard is in the
  code rather than in a comment, so L1c's scheduled 24 -> ~40 retune
  cannot break it. Costs nothing (14.97 -> 14.96, inside run agreement).

  REMAINING: 0.83 ms. L3 is now 0.83 of a 14.96 ms megakernel. Levers left
  are (e) cache the cell's light list across samples -- kills the loads,
  leaves the VALU -- and (f) a closed-form per-light line integral, which
  is the big one but changes the look and wants a spec. (c)
  kFogLightsPerCell 2 -> 1 remains the last resort and is still untaken.

  Spec amended (DOOM-0011 §4.4(b) + INV-1) and taken through /cold-eyes:
  2 loops, 2 lanes each, 26 findings, all verified and fixed. Loop 2 was
  majority collateral from loop 1's own fixes, so the run stopped there by
  Phase 5's rule rather than spending a third read on its own wake. The
  review's best catch was structural and NOT about this change: §4.4(b)
  still specifies a torch-selection scheme L3 never shipped, at four
  sites, one of them a live directive. Filed as DOOM-0304.

- ✅ [DOOM-0296] **The fog-light grid is baked at level load, so a door that opens later admits no torchlight.**
  DOOM-0011 L3 bakes which cells of air can see which static emitters
  once, at level load. A door that opens mid-play changes that answer and
  nothing re-runs the bake.

  The error is one-directional and that is why it shipped: doors are shut
  when a level loads, so the failure mode is a torch that does NOT light
  air it could, never one that lights air through a wall. A miss reads as
  nothing happening; a leak reads as the brightest thing in a dark room.

  The fix has a shape already, because DOOM-0281 solved the same problem
  for the seep field: re-flood when an opening flips, and ease rather
  than snap. The open question is cost -- the seep re-flood is a Dijkstra
  over portals, while this bake is 7788 BSP sight tests (4.4 ms on E1M1),
  which is affordable at load and probably not mid-frame. Likely answer
  is to re-bake only the cells within reach of a light whose visibility
  could have changed, keyed off RB_SeepOpeningsChanged's existing signal.

  Sequence after DOOM-0295, since a cheaper per-sample term may change
  what the re-bake has to produce.
  **Layman:** Open a door and the torch behind it lights the room, but not the fog in it, until you reload the level.
  Kind: fix.
  Lanes: shaders, fog.
  Source: in-session-2026-08-01.
  Progress (2026-08-02): IMPLEMENTED (9cd8add) and verified on the
  RX 6600; only the LOOK is left, which needs a person at the keyboard.
  Spec DOOM-0011 4.4 amendment + INV-14 + 7's L3b row through three
  cold-eyes loops (2 -> 1 -> 0 CRITICAL); build steps as plan Task L3b.

  Four claims in the headline above are SUPERSEDED, all by precedent this
  bullet was itself citing:
    - "ease rather than snap" -> it SNAPS. DOOM-0289's own line: the seep
      eases because mist rolls in; light is height-keyed and snaps. The
      settle timer also lands the bake after the door has stopped, so there
      is no partial state to fade through -- and a slot holds a light's
      IDENTITY beside its weight, so two different lights in one slot
      cannot be interpolated anyway.
    - "keyed off RB_SeepOpeningsChanged" -> keyed off RB_UPD_MOVED.
      P_CheckSightTrace's P_CrossSubsector narrows its slope range from
      opentop/openbottom, so the answer moves CONTINUOUSLY with plane
      height. On the flip signal a lift rising in front of a torch would
      leave the grid lighting air THROUGH it -- a leak, the one direction
      this defect never takes for a door.
    - "re-bake only the cells within reach" -> the whole grid re-bakes. An
      opened door reveals a torch to every cell within that torch's reach,
      not to cells near the door, so a correct scope is a reach-radius
      sweep per opening: not obviously cheaper, much easier to get wrong.
    - "7788 sight tests (4.4 ms)" is stale. It predates DOOM-0302
      (63d5a2d), which cut kNukageLe 0.35/1.30/0.15 -> 0.05/0.19/0.02 and
      kLavaLe 2.20/0.75/0.12 -> 0.55/0.19/0.03; reach is
      sqrtf(lum / RB_FOG_LIGHT_CUTOFF), so dimmer liquids reach less far
      and fewer cells have any candidate. 6320 tests now.

  Measured on E1M1 (idle, renderer 1 / rt_fog 2 / render_scale 50):
    still map     one L3 line, no second; -shotcompare mae 0.003 against a
                  golden written by the PRE-change build (same-build
                  control 0.004, i.e. at the noise floor)
    door          449 -> 454 lit, 1085 -> 1090 air, and back on close
    lift, no flip 449 -> 353 lit, and back -- the only fixture that catches
                  a flip-keyed trigger, which emits nothing here
    real door     exactly one bake per plane stop, not ~80
    bake+upload   4.1 ms E1M1 / 2.9 ms MAP01 against a <= 6 ms gate. The
                  upload is NOT separable from run variance (bake-alone
                  spanned 3.6-4.7 ms over seven runs of one build), so do
                  not quote it as a delta.
    make test 7/7; -rtverify doom.wad PASS 0.1091%, furnace PASS.

  Owed before shipped:
    - The user's play-test. Two look calls: a torch's fog appearing ~0.15 s
      after a door finishes (snap, not roll-in -- that supersession is mine
      and is theirs to veto); and whether a repeating re-bake reads badly
      on a map with a cycling mover. A perpetual platform waits 3 s at each
      end, so it SETTLES and bakes twice per cycle indefinitely without
      ever touching the cap -- the commoner case, and the one to watch. If
      it reads badly, suppress re-bakes while such a mover runs rather than
      shortening the cap.
    - Plan Step 8's visual A/B was NOT run: it needs the throwaway
      plane-driving hook (removed after Step 4) and a door that reveals a
      torch. The lit-count moves above are the numeric proof of effect.

  Found, not fixed: the timer is map-GLOBAL. RB_UPD_MOVED says SOME plane
  moved, not which, so a lift cycling in an unvisited corner defers every
  door's re-bake to the 4 s cap while it runs. Accepted (a per-sector timer
  needs a dirty set the engine does not keep), but it is why the cap is not
  the rare path it looks like.
  Progress (2026-08-02, second pass): the user's first play-test shot was
  INCONCLUSIVE, and correctly so -- E1M1 courtyard, nothing had moved, and
  the room was already brightly lit. The feature only changes cells whose
  visibility to a torch changed BECAUSE a plane moved, so walking into a
  lit room can never show it.

  So rather than have them hunt for a level, every shut door in both IWADs
  was scanned: open it, refresh the cell cache, re-bake, diff the lit
  count, restore. Throwaway `-fogscan` diagnostic, reverted -- the answer
  is below, so the tool is not needed again.

  BEST PLAY-TEST LOCATION, by a wide margin:
    doom2.wad MAP01, door sector 13 at (-863, 597).  base 158 lit -> +74,
    a +47% increase in torch-lit fog cells from one door.
    Stand at (-700, 597) and face west: verified open, heavily-fogged
    outdoor ground right beside it.
    ./linux/linuxxdoom -iwad wads/doom2.wad -warp 1 -warpto -700 597

  Runners-up: MAP09 sector 105 (-1048, 508) +47 of 570; MAP15 sector 208
  (-704, -2640) +37 of 639; MAP06 sector 34 (1820, 1696) +33 of 246.

  ⚠ THE SCAN ALSO SAYS SOMETHING THE FEATURE'S OWN FRAMING DID NOT, and it
  should temper what this item claims. In DOOM 1 the effect is SMALL: the
  best door on any E1Mx map moves 21 cells (E1M5 sector 64 at (432, -32)),
  and E1M1's own best is 5. Vanilla DOOM rarely puts a torch behind a shut
  door within that torch's reach, so on DOOM 1 this is a correctness fix
  whose visible payoff is marginal. DOOM 2 is where it reads. Worth saying
  plainly rather than letting a play-test discover it as disappointment.

  Two more facts the scan turned up, neither chased:
    - MAP02 and MAP07 bake ZERO lit cells at spawn. Either they genuinely
      have no static emitter within reach of any air cell, or the cluster
      step is dropping their emitters. Unverified, and worth one look --
      a map where L3 does nothing at all is not obviously intended.
    - The LIFT case is the bigger visual mover on E1M1 (449 -> 353, 96
      cells) than any door there. If the door demo underwhelms, a lift
      rising in front of a torch is the stronger fixture.
  Progress (2026-08-03, third pass): the play-test came back INCONCLUSIVE
  a second time, and the two attempts together shift where the burden
  sits.

  Attempt 2 used this bullet's own "BEST PLAY-TEST LOCATION" —
  `-warpto -700 597` on doom2 MAP01. That coordinate is BAD and this
  bullet's reading of it was WRONG -- see the retraction at the end of this
  note. (Retracted claim: that the spot puts the player on the map's rim.)
  The user reported it as "a big hole in the geometry ... and it
  is outside as well", which is a fair description of the view. The
  coordinate came out of the `-fogscan` diagnostic, whose criterion was
  lit-cell delta per door — never that the resulting view was playable.
  A scan that ranks by cell count cannot rank by visibility.

  Attempt 3 was a normal MAP01 start. The user walked the map and found
  nowhere the effect reads, and suggested hell levels as more likely.
  Worth answering with the scan data already in this bullet rather than
  another play-test: the scan covered every shut door in BOTH IWADs, and
  no hell map placed — MAP01's +74 beats MAP09 (+47), MAP15 (+37) and
  MAP06 (+33). Hell maps carry more fire, but their emitters are largely
  not behind shut doors. So MAP01 is the best case, and if the effect is
  invisible there it is invisible everywhere.

  WHAT THIS MEANS FOR THE ITEM. Two play-tests have now failed to see a
  change that the counters say is real, which is exactly the gap Plan Step
  8's visual A/B was supposed to close and never did (it needs the
  throwaway plane-driving hook, removed after Step 4). The lit-count
  numbers prove the MECHANISM; they have never been evidence of
  VISIBILITY, and this bullet has been treating them as though they were.
  Next action is mine, not the user's: run the A/B headlessly at MAP01
  sector 13 with the hook restored, diff the frames, and record the
  answer. A verdict of "correct but not visible" is a perfectly good
  outcome to ship — it just has to be stated instead of left as a play-test
  the user keeps failing.

  Do NOT send the user to another location before that A/B exists.
  RETRACTION (2026-08-03, same day, before any of it was acted on). The
  paragraph above claiming `-warpto -700 597` puts the player on the map's
  rim is FALSE. Checked against doom2.wad's MAP01 lumps directly rather
  than inferred from a screenshot:

    sector 11  floor 8, ceil 264, floortex GRASS1, ceiltex F_SKY1, light 224

  That is MAP01's outdoor grass courtyard — a legitimate room, correctly
  rendered, and exactly what the capture showed (grass underfoot, sky
  above, the courtyard's boundary wall in the distance). The engine's own
  `-warpto` print had said `sector=11 floor=8 ceil=264` all along; I read
  an open outdoor area as "outside the map" because I was expecting a
  corridor and a door.

  The coordinate stands. What does NOT stand is the claim that it is a
  good demo spot, and the reason is unchanged: the scan ranked doors by
  lit-cell delta, which is not the same question as "can a player see
  this happen from a sensible vantage".

  The user's "big hole in the geometry ... see underneath the geometry"
  is therefore still UNEXPLAINED and must not be written off as this
  retracted mis-reading. Their capture shows the courtyard's wall band
  with ground visible below its base; the innocent explanation is the
  56-unit floor step between sector 11 (floor 8) and sectors 12/13
  (floor 64), but that is a hypothesis and nobody has confirmed it. Ask
  the user to point at the spot, or find it, before closing it.

  METHOD NOTE worth keeping: three of this session's wrong turns —
  "outside the map", "the wall is glowing because of the HD art", "the
  step-count raise is still open" — were each an inference from an image
  or a recollection when the authoritative data was one cheap read away
  (the WAD's SECTORS lump, a paletted-art capture, the shader's own
  comment). Read the source of truth first; it was cheaper than the
  inference in all three cases.
  THE VISUAL A/B FINALLY RAN (2026-08-03), and the answer is that on MAP01
  there is nothing to see. Plan Step 8 owed this since the feature shipped;
  it is done, and it supersedes "owed: the user's play-test".

  Method: a throwaway `-fogdoor` hook opened every shut door sector BEFORE
  RB_BuildLevel, so the fog-light bake sees what the doors were hiding
  without needing a tic loop -shotverify has no time for. Four captures per
  viewpoint: {doors shut, doors open} x {rt_fog 3 High, rt_fog 0 off}, the
  fog-off pair being the control that subtracts the geometry change.
  Ultra RT, doom2 MAP01, 3840x2160. Hook REVERTED.

    viewpoint                   fog ON      fog OFF     fog's own share
    outdoor courtyard           0.0075      0.0075      0.0000
    (-warpto -700 597 180)      peak 2      peak 2
    player start                0.0155      0.0096      0.0059
    (-warpto -96 784 90)        peak 85     peak 8

  MAE per 255. The -shotcompare bar for "a look change happened" is 3.0
  (kGoldenMAE) and same-build noise is ~0.004. So the fog's contribution to
  opening EVERY door in the map is at the noise floor -- roughly 500x under
  the gate. At the courtyard it is bit-for-bit the same change with fog on
  and off, i.e. exactly zero. The peak-85 pixels at the player start say a
  handful of pixels do change; the eye will not find them.

  AND THE HEADLINE NUMBER IN THIS BULLET DOES NOT REPRODUCE. MAP01 has only
  FOUR shut door sectors, and opening all four together moves the bake
  158 -> 173 lit cells, i.e. +15. This bullet claims +74 from door sector 13
  ALONE ("a +47% increase"). Two different methods -- the reverted -fogscan
  opened one door and re-baked at runtime; this opens them at load -- so the
  gap may be method, not defect. Either way the +74 is not a number to quote
  again without re-deriving it, and the "BEST PLAY-TEST LOCATION" section
  above rests entirely on it. Cells "with a candidate" also FELL 420 -> 403
  while lit rose, which nobody has explained.

  VERDICT TO PUT TO THE USER: DOOM-0296 is correct, cheap and invisible.
  The mechanism is proven (the counters move, the re-bake fires once per
  settled move, the cost is 2.6-4.1 ms against a 6 ms gate) and the visible
  payoff on MAP01 is nil. That is a perfectly shippable outcome for a
  correctness fix -- a torch that could light air now does -- but it should
  ship as that, not as a feature anyone will notice. Do not send the user
  hunting for it again; two play-tests failed because there was nothing
  there to find.
  Resolved (2026-08-03, 9cd8add; A/B and verdict d018164). Shipped as a
  CORRECTNESS fix with no visible payoff on the stock maps, which is the
  honest description and is what the changelog entry says.

  Closed on the measurement rather than on a play-test, deliberately. Two
  play-tests failed to see it because the visual A/B (finally run today)
  puts the fog's own contribution at ~500x under the -shotcompare bar and
  at the same order as same-build noise. A third play-test would have
  failed the same way. The user signed off on shipping it as-is.

  NOT closed, and carried forward rather than buried: this bullet's
  "+74 lit cells from door sector 13, a +47% increase" does not reproduce
  (all four of MAP01's shut door sectors together give +15), and the
  "with a candidate" count FALLS 420 -> 403 while lit rises. Two methods
  were involved so the gap may be method rather than defect, but the
  number should not be quoted again without re-deriving it. If a future
  session wants a fog-light demo location, re-derive from scratch; do not
  trust the BEST PLAY-TEST LOCATION section above.

- ✅ [DOOM-0297] **-rtverify passes on doom.wad and deterministically fails on doom2.wad, same build.**
  Found while gating DOOM-0011 L3 across both IWADs, which the standing
  2026-08-01 constraint requires. On one build, at -warp 1 1:

    doom.wad   INV-6 direct-light rel-MSE 0.0796%  PASS (bar 0.50%)
    doom2.wad  INV-6 direct-light rel-MSE 3.4943%  FAIL, 63987 lit px

  PROVEN pre-existing, not L3: git stash to the untouched tree, rebuild,
  re-run -- identical 3.4943% to four decimal places. So the two IWADs
  disagree reproducibly rather than the number drifting.

  This matters beyond one number, because it is the SAME 3.4943%/63987
  pair DOOM-0208 recorded on 2026-07-23 and closed as "a transient
  environmental blip, not present now". It was never transient. It was
  doom2.wad. That note should be corrected rather than left as a
  falsified explanation on the record.

  Leading hypothesis: the bar, not the renderer. MAP01's emitter set is
  smaller and its brightest lights dominate more (48 clustered lights
  spanning 1987..57320 vs doom.wad's 83 spanning 0..83741), so a
  power-sampled NEE estimator at 16384 spp may genuinely converge slower
  against the brute-force reference there. Check that before assuming a
  defect: raise spp and see whether the number falls.

  Until it is settled, -rtverify is a doom.wad-only gate, and any claim
  that it passes should say which IWAD it passed on.
  **Layman:** The renderer's automated self-check passes on DOOM 1 and fails on DOOM 2. Probably the check, not the renderer.
  Kind: investigate.
  Lanes: rt, test.
  Source: in-session-2026-08-01.
  Progress (2026-08-02): ANSWERED, and the leading hypothesis was right --
  it is the bar, not the renderer. Measured by scaling each estimator's
  dispatch count independently through a throwaway `-rtdisp <nee> <brute>`
  parm in a worktree (not merged), machine idle:

    doom2 MAP01   nee 16384 / brute  4096  ->  3.4993%   (the shipped gate)
                  nee 16384 / brute 16384  ->  3.1607%
                  nee 16384 / brute 65536  ->  2.9124%
                  nee 65536 / brute 65536  ->  0.7245%
    doom.wad E1M1 nee 16384 / brute  4096  ->  0.1091%
                  nee 16384 / brute 65536  ->  0.1032%

  Raising the brute-force REFERENCE 16x moved the number by 0.59 pp; raising
  the NEE side 4x collapsed it from 2.9124 to 0.7245 -- a ratio of 4.02
  against 4x the samples. That is textbook 1/N Monte-Carlo variance with no
  bias floor, and it fits both arms: modelling rel-MSE as
  (nee term)/N_nee + (brute term)/N_brute gives a brute term of ~0.63 at
  4096 spp and a NEE term of ~2.87 at 16384 spp, which predicts 0.757 at
  65536/65536 against 0.7245 observed.

  So the two IWADs do not disagree about the renderer. They disagree about
  how fast the estimator converges: MAP01's 48 clustered emitters spanning
  1987..57320 leave the power-sampled NEE estimator ~26x noisier at equal
  spp than E1M1's 83 spanning 0..83741. rel-MSE charges that residual
  variance to the score, and a fixed 0.50% bar therefore measures the map as
  much as the integrator.

  What this means for the gate, in preference order:
    (a) The unbiasedness claim INV-6 exists to make is proven by the error
        falling as 1/N toward zero, which it demonstrably does -- not by a
        fixed threshold. A gate that samples two spp counts and checks the
        SLOPE is valid on any map and costs one extra run.
    (b) Cheaper stopgap: raise the doom2 gate's spp. Extrapolating 1/N, the
        0.50% bar needs roughly 1.5-2x the 65536 spp already measured, i.e.
        about 8x the shipped gate's runtime -- affordable for a headless
        gate, not for anything interactive.
    (c) Do NOT relax the bar per-IWAD without doing (a) first; that hides a
        real bias if one ever appears.

  DOOM-0208's 2026-07-23 note should be corrected on the record: the
  3.4943% / 63987 pair it closed as "a transient environmental blip" was
  never transient and was never environmental. It was doom2.wad, and it is
  estimator variance.

  Still true and unchanged: -rtverify is a doom.wad-only gate until (a) or
  (b) lands, and any claim that it passes should name the IWAD.
  Progress (2026-08-02, second pass): THE 1/N SLOPE GATE RECOMMENDED ABOVE
  WAS BUILT ON PAPER, MEASURED, AND DOES NOT SURVIVE. Recording the
  falsification because the recommendation was already given to the user
  and would otherwise be implemented as written.

  The derivation was sound. Model rel-MSE as E = a/N_nee + c/N_brute +
  bias. Quadrupling BOTH counts quarters both variance terms, so the two
  unknowns cancel and the bias falls out of two runs:
      bias = (4*E_4x - E_1x) / 3
  Cheaper than the three runs first assumed, and valid on any map.

  Measured (idle, renderer 1 / rt_fog 2, -rtdisp <nee> <brute> dispatches):
      doom.wad   E(256,64) 0.1091%  E(1024,256) 0.0374%  fall 2.92x
      doom2.wad  E(256,64) 3.4993%  E(1024,256) 0.7330%  fall 4.77x
      bias:  doom.wad +0.0135%   doom2.wad -0.1891%

  Two things kill it as a gate. A NEGATIVE bias is unphysical, so -0.189%
  is measurement slop rather than a reading -- and its magnitude is a third
  of the 0.50% bar it was meant to replace, i.e. the new gate is noisier
  than the old one. And the fall is 2.92x on one IWAD and 4.77x on the
  other where pure variance predicts 4.00x both times, so a "did it fall
  4x" test has no bar that passes both without being meaningless.

  Cross-check confirming it is the model and not one bad run: the earlier
  three-point solve (a=746.8, c=40.07, bias=-0.0439) predicts
  E(1024,256) = 0.8419% and the direct measurement is 0.7330%. ~0.11 pp of
  model error, which is the same order as the bias being extrapolated.

  WHAT THE DATA DOES SUPPORT, unambiguously, is the thing the gate was
  wanted for: doom2's failure is VARIANCE, NOT BIAS. 3.4993 -> 0.7330 ->
  0.7245 as samples rise, falling hard with no floor anywhere near 3.5%.
  That was the precondition this bullet set for touching the bar, and it is
  now met.

  REVISED RECOMMENDATION, and it is the option the first pass explicitly
  withheld until the slope was checked: make the gate's sample count and
  bar PER-IWAD, on the evidence above. It costs nothing at runtime and it
  is now justified rather than assumed. Concretely, one of:
    (i)  keep doom.wad at the shipped cost and the 0.50% bar (it passes at
         0.1091%), and gate doom2 at 4x cost with a ~1.0% bar (0.7330%);
    (ii) leave doom2 out of the gate entirely and say so in the runner,
         rather than letting a red result sit there being re-diagnosed
         every few months -- which is how DOOM-0208 happened.
  (i) costs a 4x run on one IWAD and keeps both covered; (ii) is free and
  covers less. Needs the user's call before implementing, because it
  changes INV-6's acceptance (docs/specs/DOOM-0009-path-tracer.md) and so
  pulls in the rule-14 spec gate either way.

  NOT a defect either way: the renderer is fine, and this bullet's own
  "probably the check, not the renderer" was right.
  SHIPPED 2026-08-02 (7adcd0a). -rtverify now picks its SAMPLE COUNT from
  `gamemode` and leaves the bar alone. Both IWADs pass the same 0.50% bar:

    doom.wad   0.1091%  NEE  16384 spp / reference  4096 spp   PASS
    doom2.wad  0.3665%  NEE 262144 spp / reference 16384 spp   PASS
    white furnace 0.000000 both; make test 7/7.

  This is NOT the per-IWAD BAR the user approved -- it is that option with
  the part worth disliking removed, and it is strictly stronger. Holding
  the reference fixed and quadrupling NEE takes doom2 from 0.7330% to
  0.3665%, which clears the EXISTING bar, so nothing was relaxed. That
  matters beyond tidiness: raising spp can only TIGHTEN a variance-limited
  gate, so plutonia, tnt and any PWAD loaded over doom2 inheriting the
  higher count is harmless -- whereas an inherited BAR would have hidden a
  real defect. Both cold-eyes lanes had flagged exactly that hole in the
  bar-relaxing draft.

  THE EVIDENCE HAD TO BE RE-MEASURED BEFORE IT COULD BE CLAIMED, and this
  is the reusable lesson. The first draft argued "falling with no floor"
  from three points of which only ONE moved the NEE count; the third
  quadrupled the REFERENCE and moved 0.7330 -> 0.7245, which is exactly
  what a bias floor at 0.72% would look like. Two independent lanes caught
  it. A third NEE level at fixed reference settled it at 0.3665%.

  NOT CLAIMED, and INV-6 now says so: that the residual is zero. The 4x NEE
  step gave a 2.0x fall against a predicted 4.0x, and extrapolations of the
  constant term scatter by ~+-0.2% rel-MSE, so any true bias is bounded by
  about that rather than shown absent. The gate does not need zero --
  0.3665% clears 0.50% with 27% headroom against +-16% measured run-to-run
  scatter. Raising the reference as well only reaches 0.3419%, which is why
  the extra 4x is not spent.

  ALSO RECORDED IN INV-6 so it is not re-proposed: the bias-extrapolation
  gate, derived, measured and falsified. Model E = a/N_nee + c/N_ref + b^2
  -- the constant is b^2, not b, because rel-MSE is a SQUARED metric, so a
  negative b^2 is what is unphysical (a cold-eyes lane corrected that
  algebra). Quadrupling both counts gives b^2 = (4*E_4x - E_1x)/3, which
  measured +0.0135% on doom.wad and -0.1891% on doom2 -- impossible, and
  slop of the same order as the bar it would replace.

  DOOM-0208's 2026-07-23 note stays corrected: the 3.4943% / 63987 pair it
  closed as "a transient environmental blip" was never transient and never
  environmental. It was doom2.wad, under-sampled.

  CAVEAT worth knowing before quoting any of these numbers: the score is a
  property of a map AND a camera as well as the integrator -- RB_RtVerify
  builds its view from g.lastView at the first ready present. The rows are
  defined only at the invocations INV-6 quotes (-warp 1 1 for doom.wad,
  -warp 1 for doom2.wad). Running -rtverify elsewhere is a diagnostic, not
  this gate. The result line now prints its configuration for that reason.

  Cost: doom2's gate is ~13x the dispatches it was. Headless only, and it
  is the price of a gate that is valid on both IWADs.

- 📋 [DOOM-0298] **New liquid surfaces: bubbles in nukage, splashes on lava, and animation that does not visibly repeat.**
  User, 2026-08-01, on the DOOM-0011 L3 nukage glow: "when we create new
  surfaces for liquids (water / nukage / lava) please add bubbles to
  nukage and splashes to the lava. All 3 should have animated surfaces
  like they currently do in the old graphics, however, it should look
  more realistic and not so repeated."

  Three liquids, three asks, and the third one is the hard one.

    1. NUKAGE gets bubbles -- rising, surfacing, popping.
    2. LAVA gets splashes -- spitting, the surface breaking.
    3. WATER, nukage and lava all keep a MOVING surface. The 1993 flats
       already animate (NUKAGE1-3, LAVA1-4, a 3-4 frame loop on
       flattranslation), so this is not a new behaviour -- it is the same
       behaviour done convincingly.

  "NOT SO REPEATED" IS THE LOAD-BEARING PHRASE, and it names a defect
  this project has already solved once. DOOM's animated flats repeat in
  BOTH axes at once: spatially, one 64x64 flat tiles across the whole
  pool; and temporally, every tile plays the same 3-4 frame loop in
  lockstep, so a large pool pulses as one surface. Replacing the art
  alone fixes neither -- a prettier tile tiled the same way is still a
  grid, which is exactly the "very very tiled" report that produced
  DOOM-0181.

  So the mechanism is likely to matter more than the texture:
    - SPATIAL repetition: DOOM-0181's world-keyed stochastic de-tiling
      (IQ 4-corner blend) already exists and already runs on HD walls and
      flats. It should be the starting point here rather than a new idea.
    - TEMPORAL repetition: de-correlate the phase per world position, so
      neighbouring patches of a pool are at different points in the cycle
      and the surface never pulses as a unit. DOOM-0183 L4's procedural
      ripple normal already runs off a world position and a clock, so the
      hook exists.
    - BUBBLES and SPLASHES are EVENTS, not a loop, and that is what will
      sell them. A loop of a bubble is still a loop. Cheapest derived
      route: hash the world cell to a per-cell phase and spawn a bubble
      on that cell's own schedule, so the pool is covered in independent
      events without a particle system or any hand placement.

  Relationship to what already shipped, because this is easy to
  mis-scope as duplicate work:
    - DOOM-0183 shipped the nukage's procedural RIPPLE normal (L4), its
      glow and cast-light. Ripples are not bubbles; that item is about
      the liquid's material, this one is about its SURFACE MOTION and its
      art.
    - DOOM-0042 owns the HD art programme; new liquid art belongs in that
      pipeline (materials.csv sidecar, palette-locked) rather than beside
      it.
    - DOOM-0011 L3 now lights the air above these pools, and DOOM-0293
      will give them their own fog. Both READ the liquid's identity from
      the same flat-name flags DOOM-0183 established, so a new liquid
      surface must keep those flags meaning what they mean or it silently
      turns the glow and the fog off.
    - Water has no liquid flag at all yet (DOOM-0183 deliberately
      deferred water and blood), so item 3 needs that flag added first.

  Needs a design pass before implementation -- /write-spec, then the
  rule-14 gate. Sequence after DOOM-0293, so the liquid identity data
  this depends on has settled.
  **Layman:** Toxic sludge should bubble and lava should spit and splash, and both should keep moving the way they do in the original game -- just convincingly, without the same little loop playing over and over across the whole pool.
  Kind: feature.
  Lanes: shaders, assets, liquid.
  Source: user-request-2026-08-01.
  Scope narrowed by the user, 2026-08-01, same session: **ULTRA ONLY.**

  That is consistent with the tier rule rather than an exception to it.
  CLAUDE.md's standing warning is not to gate a feature on Ultra because
  it is EXPENSIVE -- gate it on the ray-traced view, or ship a cheap
  approximation for Solid. This one qualifies on the other clause:
  "Ultra-only is correct only for things that need the HD art itself",
  and new liquid surfaces are exactly that. Ultra SUBSTITUTES for DOOM's
  art; Solid enhances it, so a replaced nukage surface is not a Solid
  feature by definition.

  One question to put to the user at design time rather than decide
  quietly: the MECHANISM and the ART separate cleanly here. Bubbles and
  splashes are new art and are Ultra's. But de-correlating the animation
  phase per world position, so a big pool stops pulsing in lockstep,
  would work just as well over the ORIGINAL 1993 flats -- and "enhance
  DOOM's own art" is precisely Solid's brief. Worth asking whether the
  anti-repetition half should follow into Solid once the Ultra version
  has proven itself. Not assumed either way here.

- 📋 [DOOM-0299] **Replace the barrel explosion with a modern one (Ultra only).**
  User, 2026-08-01: "please replace the barrel explosions with better /
  modern explosions in the Ultra view."

  What is there today is DOOM's MT_BARREL death sequence: a handful of
  billboard sprite frames (S_BEXP...), camera-facing, paletted, and the
  same every time. A-Explode fires on one of those frames and does the
  damage; the LOOK and the gameplay are already separate, which is what
  makes this safe to replace.

  Ultra only, and that is the tier rule applying rather than bending:
  this is art SUBSTITUTION, which is Ultra's definition, not an effect
  held back because it is expensive. Solid keeps DOOM's own explosion
  sprites -- enhanced, per the tier table, not replaced.

  What the engine can already give it, none of which existed when this
  was last considered:
    - Emissive materials feeding the NEE emitter set, so the blast can
      genuinely LIGHT the room rather than being a bright sprite (the
      same mechanism DOOM-0183 used for lava and DOOM-0011 L3 now reads
      for fog).
    - DOOM-0011's fog march, so the flash can light the smoke and the
      air around it -- an explosion in fog is most of the effect.
    - DOOM-0184 (glowing projectiles that cast light) is the same family
      and should probably be designed with this rather than after it.

  Open at design time: whether this is better art on a billboard, a
  particle system, or a small volumetric puff -- and whether the smoke
  should persist. The engine has no particle system today, so "modern
  explosion" may be a bigger dependency than it sounds; scope that
  before promising a look. Relates to DOOM-0080 (all sprites -> 3D
  models in Ultra), which is the general form of this problem.

  Needs a design pass -- /write-spec, then the rule-14 gate.
  **Layman:** Blowing up a barrel should look like a real explosion -- fire, smoke, light thrown across the room -- instead of the original's few flat frames.
  Kind: feature.
  Lanes: shaders, assets, sprites.
  Source: user-request-2026-08-01.

- ✅ [DOOM-0300] **The torch glow sits still while the fog behind it drifts -- give the light path the billows too.**
  User, 2026-08-01, on the DOOM-0011 L3 screenshots: "I love the glow of
  the nukage pools both indoors and outdoors. I also like that it isn't a
  uniform colour, however, it is static. This is where the Silent Hill 2
  fog system will help."

  The observation is right and the expected remedy is not, which is why
  this is its own item. **L1c's SH2 wisps are already shipped and already
  multiply this term, and they do not move it.** Measured, E1M1 nukage
  courtyard, by pinning the ripple clock (rb_shotverify's rippleSec) at
  8 s, 20 s and 32 s and diffing:

      pool crop, drift between t=8 s and t=32 s
        L3 ON    MAE 6.075 / 255
        CONTROL  MAE 6.236 / 255   (torch gain 0 -- the sky fog alone)

      the torch layer itself, same time, on vs control
        t=8 s    MAE 35.790, peak 127
        t=32 s   MAE 37.167, peak 134

  So L3 adds a very large layer (MAE ~36) that contributes NO extra
  motion -- turning it on leaves the drift statistically where it was,
  and its own magnitude moves about 4% across 24 seconds. A big still
  layer over a moving one is exactly what "it is static" describes.

  Why, and it is not a wiring bug. The glow's brightness IS multiplied by
  the wisp-modulated sigma, but its ENVELOPE -- Le / d^2 x phase around a
  fixed lamp -- is what the eye tracks, and that is static by
  construction. The glow is significant within roughly 64-128 units of
  its light, while kWispFreq1's noise cell spans 192, so the entire glow
  sits inside about one cell and brightens or dims as a unit instead of
  developing internal structure. Octave 2 (2.5x, weight 0.7) is the only
  part with a comparable scale.

  The missing physics is the fix, and it is the term single scattering is
  supposed to have: transmittance along the LIGHT path. Today the torch
  reaches the sample unattenuated, so no billow can ever pass in front of
  it. Attenuating by exp(-tau) from lamp to sample -- approximated by one
  wisp tap at the midpoint of that segment, not a march -- makes drifting
  billows visibly roll through the glow and cut it, which is the SH2 look
  applied to the light rather than to the medium alone.

  Cost is the reason this is not already done: one 3-D noise tap plus an
  exp per light per sample, on a term that already measures 1.05 ms.
  Sequence AFTER DOOM-0295 finds out where that 1.05 ms goes, because the
  answer may change what this can afford. If it turns out to be register
  pressure rather than ALU, one extra tap is nearly free.
  **Layman:** The new glow around lights looks lovely but it does not move, so it reads as a painted patch floating in front of fog that is drifting past it.
  Kind: enhancement.
  Lanes: shaders, fog.
  Source: user-play-test-2026-08-01.
  CORRECTION + refinement (2026-08-01, same day), after the user proposed
  the mechanism themselves: "The nukage pool gives off a uniform glow from
  the whole pool. Then, as the wisps of fog move around and dissipate and
  new ones are created (the Silent Hill 2 look) it will make it look like
  there is movement as some bit of fog will be thicker and some thinner."

  That mechanism is REAL and already partly working. The bullet above says
  the wisps contribute "NO extra motion" -- that was measured with
  whole-frame MAE, where L3's large static envelope swamps the signal, and
  with the glow layer's MEAN, which cannot see a pattern move at all. Two
  sharper measurements, isolating the glow layer as (L3 on - L3 off) at a
  fixed time:

    DOES THE MEDIUM MODULATE THE GLOW AT ALL? Same frame, kWispAmp 1 vs 0:
        glow layer mean 35.78 (wisps on) vs 34.76 (wisps off)
        difference MAE 4.474, peak 27
    -> yes: the wisps swing the glow by about 12.5%.

    DOES IT MOVE? Glow layer at three pinned ripple times, per pixel:
        t= 8 s vs t=20 s   MAE 2.191  peak 18  81.0% of pixels
        t= 8 s vs t=32 s   MAE 3.037  peak 22  88.6%
        t=20 s vs t=32 s   MAE 2.493  peak 16  90.6%
    -> also yes, but only ~6-8% of the glow's own magnitude, spread over
       tens of seconds. It is not frozen; it is slow and shallow, against
       a static envelope of mean ~36. Next to fog that is visibly
       billowing it therefore READS as static, which is exactly what the
       user reported.

  So the corrected diagnosis: the user's route is sound and the term is
  already there -- it is under-driven, not absent.

  THE CHEAPEST LEVER IS FREE, AND IT IS NOT AMPLITUDE. kWispAmp is already
  1.0, i.e. density swinging 0x..2x, and cannot go further without going
  negative. The gap between the 12.5% the wisps make spatially and the 7%
  they make temporally is DRIFT SPEED: kWispVel1 is (8, 3, 1) world units
  per second, so in 12 seconds the pattern moves 96 units -- half of
  octave 1's 192-unit cell. Raising that velocity costs literally nothing
  (it is a constant inside a noise lookup that is already being sampled)
  and turns the same 12.5% spatial swing into a much faster temporal one.
  Try that BEFORE the light-path transmittance term above, which costs a
  tap and an exp per light per sample.

  The user also named the other half -- wisps that "dissipate and new ones
  are created". The shipped noise only TRANSLATES; nothing appears or
  fades. Evolution needs the sample point to move through the volume on
  the z axis as well (kWispVel1.z is 1.0, nearly nothing) or a second
  time-varying seed. Also nearly free, and probably what sells
  "dissipating" over "sliding past".

  Revised order: (1) wisp velocity + z evolution, free, try first;
  (2) light-path transmittance, only if (1) does not read.
  MEASURED AGAINST THE REAL THING (2026-08-01). The user loaded a Silent
  Hill 2 save state in PCSX2 -- the forest path, James idle, fixed camera
  -- and 20 screenshots were taken 0.5 s apart with
  /mnt/Games/Scripts/Linux/screenshot-burst.sh -W "Silent Hill 2".
  Reference frames: ~/Pictures/sh2-fog-study/.

  Three numbers, and the third one overturns how our wisps are built.

    1. IT IS NOT GRAIN. SH2 ships a noise/grain filter, so raw frame
       difference would flatter it. Consecutive frames (0.5 s apart):
       raw MAE 7.083, and after a 12 px Gaussian blur still 5.974 --
       **84% of the change is large-scale structure**, i.e. real
       billowing, not per-pixel shimmer.

    2. IT DECORRELATES IN ABOUT 1.5 SECONDS. Blurred MAE against frame 1:
           +0.5 s  5.173      +2.0 s  8.169      +6.0 s  8.996
           +1.0 s  7.321      +3.0 s  7.246      +9.5 s  9.000
           +1.5 s  8.789      +4.0 s  8.035
       It climbs to ~8.8 by 1.5 s and then sits at 8-9 for the remaining
       eight seconds. That is a saturated random walk: the fog restructures
       completely in a second and a half and never returns.

       OURS TAKES TENS OF SECONDS. kWispFreq1 is 1/192 and kWispVel1 is
       8 units/s, so one noise cell takes 192/8 = 24 s to pass. That is a
       ~15x mismatch in timescale, and it is the whole reason the glow
       reads as static: the modulation is there (12.5%, measured above) but
       it arrives an order of magnitude too slowly to see.

    3. **IT DOES NOT TRANSLATE. IT CHURNS IN PLACE.** This is the finding
       that matters, because our implementation does the opposite. Each
       consecutive pair was matched against its neighbour over a +/-12 px
       search for the best whole-frame shift:
           pair 1  best shift (+0,+0)   0% explained by translation
           pair 2  best shift (-2,+2)   0%
           pair 3  best shift (+0,+2)   0%
           pair 4  best shift (+0,+0)   0%
           pair 5  best shift (+4,-4)   1%
           pair 6  best shift (-2,-2)   1%
           pair 7  best shift (+0,-2)   1%
       Mean improvement from allowing ANY translation: 0%. There is no
       wind direction. The fog dissipates and reforms where it stands --
       precisely the user's own description ("wisps move around and
       dissipate and new ones are created"), now with a number on it.

       Our wisps are pure TRANSLATION: kWispVel1 = (8, 3, 1) and
       kWispVel2 = (-3, 4, 0.3) slide the noise volume past the world.
       Sliding faster would give SH2's rate with a wind SH2 does not have.

  WHAT TO BUILD, revised again and now evidence-led. Do not simply raise
  kWispVel1. The two octaves already drift in OPPOSED directions, and two
  counter-moving patterns interfere: their beat dissipates and reforms
  without net translation, which is the churn we want and is already the
  shape of the code. Raise BOTH octave speeds by roughly 15x, keep them
  opposed, and keep the net near zero. Cost: two vec3 constants in a
  lookup that is already being sampled. Free.

  Then verify with the same instrument rather than by eye: capture our own
  fog at pinned ripple times, blur, and check (a) the decorrelation
  half-life is ~1.5 s and (b) the best-shift search still explains ~0%.
  A DOOM capture that scores a large translation component has the wrong
  mechanism no matter how good the still looks.

  Caveat on the absolute MAE values: SH2's scene, exposure and resolution
  are not ours, so 8-9/255 is not a target to hit. The TIMESCALE and the
  zero-translation result are the transferable facts.
  CORRECTION to the timings in the note above, plus a second SH2 sample
  and a look decision (2026-08-01, same session).

  **THE ELAPSED TIMES WERE WRONG BY ~4.3x, AND THE INSTRUMENT LIED ABOUT
  IT.** The bursts were requested at 0.5 s spacing and the analysis
  labelled them that way, but spectacle has no burst mode: every shot is a
  fresh process launch plus a portal round-trip, measured at ~1.75 s on
  this box. Real spacing was 2.17-2.25 s. So "decorrelates in ~1.5 s"
  should read: the FIRST step, at +2.2 s, is already at the plateau, and
  this instrument cannot bound it any tighter than that.

  Corrected, sample 2, true elapsed against frame 1 (blurred):
      + 2.2 s  5.643      +10.9 s  5.471      +30.4 s  6.328
      + 4.4 s  6.115      +19.5 s  6.923      +41.2 s  6.258
      + 6.5 s  6.403
  Flat from the first sample onward -- a saturated walk, no loop, no trend.

  What survives the correction, which is everything that mattered: SH2's
  fog fully restructures in UNDER ~2.2 s while ours needs 24 s to cross
  one noise cell, so the mismatch is still an order of magnitude and the
  conclusion is unchanged. What does NOT survive is any attempt to set our
  constant to a precise SH2 figure -- there is no measured figure yet,
  only an upper bound. Resolving it needs video (spectacle -R s, then
  ffmpeg -i clip.mp4 -vf fps=10), not stills. screenshot-burst.sh now
  measures its own cadence from the file mtimes and says so, rather than
  repeating back the interval it was asked for.

  SECOND SAMPLE, a different angle in the same forest, confirms both
  structural findings independently:
      large-scale share  82%  (sample 1: 84%)
      translation        1%   (sample 1: 0%)  -- best shifts (+0,+0),
                              (+2,+6), (+0,-2), (-2,+0), (-2,+6), (+0,+2)
  Two different scenes agreeing is the evidence that "churns in place, no
  wind" is a property of SH2's fog and not of one camera angle.

  **LOOK DECISION (user, 2026-08-01): NO FILM GRAIN.** SH2 lays a grain
  filter over everything and we are not copying it. The measurement says
  that costs almost nothing: 82-84% of the frame-to-frame life in SH2's
  fog is large-scale billowing that survives a 12 px blur, so the grain is
  under a fifth of the effect and none of the part that reads as weather.
  Do not add a grain pass to approximate the reference; it would be the
  cheap 18% while leaving the 82% unbuilt.
  DIRECTION DECISION (user, 2026-08-01): "For our purposes, the wisps
  don't have to be going in any specific direction. Let's make it choose a
  direction at random."

  Accepted, and it composes with the measurement rather than overriding
  it, provided one thing is kept: RANDOMISE THE PAIR'S ORIENTATION, NOT
  EACH OCTAVE INDEPENDENTLY. kWispVel1 and kWispVel2 already move in
  opposed directions, and that opposition is what makes their beat
  dissipate and reform in place -- the ~0-1% translation SH2 measures.
  Rotate both by the same random angle and the churn survives with an
  arbitrary orientation; roll two independent vectors and the pair stops
  cancelling, which reintroduces exactly the net wind the reference does
  not have. So: one random angle per level, applied to both.

  **IT MUST BE SEEDED, NOT ACTUALLY RANDOM.** DOOM-0202's -shotcompare
  golden gate and DOOM-0208's canonical config depend on the RT frame
  being bit-exact run to run -- that is why rb_shotverify already pins the
  ripple clock to a constant (r_vulkan.cpp, `if (rb_shotverify == 1)
  rippleSec = 8.0f`). A genuinely random direction would make every golden
  capture a different image and quietly turn the gate into a no-op, which
  is the same defect DOOM-0183's wall-clock ripple time caused once
  already. Seed it from something stable and per-map -- the episode/map
  number, or the level name -- so the same map always looks the same and
  different maps differ. That also delivers the variety the user is
  actually asking for, since the alternative (one global constant
  direction) is what makes every level's fog blow the same way.

  Still the main lever, unchanged: SPEED. Direction is variety; the reason
  the glow reads as static is that the pattern needs ~24 s to cross one
  noise cell against SH2's under ~2.2 s. Randomising the angle without
  raising the speed changes nothing visible.
  IMPLEMENTED 2026-08-01, verified with the instrument this bullet
  specified rather than by eye.

  WHAT SHIPPED. kWispVel1 = 15 x (8, 3, 1) and kWispVel2 = -kWispVel1
  (pt_common.glsl): the 15x speed the measurement called for, with the two
  octaves now EXACTLY opposed so their sum is identically zero. Plus a
  per-level heading -- rb_mesh_t::wispAngle, seeded in RB_BuildLevelMesh
  from (gameepisode, gamemap) through an avalanche mix, pushed as a
  bit-cast float, applied to BOTH octaves in wisp(). Rotation is linear, so
  the opposition (and therefore the zero net wind) survives any angle; that
  is why the pair is rotated as a unit rather than rolled independently.

  NO NEW PUSH LANE NEEDED. misc6 is full, but its 16-byte alignment had
  already forced two pad words and only the first (fogFloorZ) was spent.
  wispAngle takes the second, so the range stays 240 bytes and -rtverify's
  184-byte prefix is untouched. Both pad words are now gone -- renderer.md
  updated, since the next value really does need a misc7.

  THE MEASUREMENTS. The analysis script was validated against the SH2
  reference frames FIRST and reproduced both published findings (MAE flat
  from the first sample; translation 0% on every consecutive pair), so it
  is the same instrument, not a new one that happens to agree. Same-build
  noise floor at one pinned time: blurred MAE 0.0087/255.

  E1M1 courtyard (-warpto 1866 -3221 45), Ultra RT, rt_fog 3, 3840x2160,
  12 px blur, ripple clock pinned via a new -rippletime flag.

  blurred MAE against the t=8 s reference
    elapsed   BEFORE   AFTER
     0.25 s    0.213   5.416
     1.00 s    0.831  11.556
     1.50 s    1.217  18.732
     3.00 s    2.251  17.115
     9.00 s    4.400  21.054

  (a) DECORRELATION: PASS. After reaches its plateau by ~1.5 s then sits at
  15-21 for the remaining 7.5 s -- a saturated walk, SH2's own shape,
  inside SH2's under-2.2 s bound. Before never plateaus: still climbing
  linearly at 9 s, which is exactly the 24 s cell crossing diagnosed above
  and exactly why the glow read as static. At 1.5 s the change is 15.4x
  larger, the 15x arriving where predicted.

  (b) TRANSLATION: PASS. Best-shift over +/-12 px, consecutive pairs: mean
  0.4% explained, worst pair 2%. SH2 measures 0-1%. So it churns in place
  and no net wind was introduced -- the failure this bullet warned of did
  not occur. Before scores 0.0%, as expected: it barely moves.

  FREE, as predicted. Paired A/B, two passes: BEFORE 45/44 fps, AFTER 44/44
  -- inside run-to-run noise. Caveat: two unrelated processes were pegging
  a core each, so 44 is not a clean absolute; the A/B is paired under
  identical conditions, which is what the comparison needs. -rtverify PASSES,
  make test green.

  NEW TOOL, reusable: `-rippletime <sec>` overrides rb_shotverify's pinned
  ripple clock, so a time-varying effect can be sampled at chosen instants
  without a rebuild per sample. Same family as -warpto/-shotverify, a no-op
  unless passed. DOOM-0295 can use it.

  REMAINING GATE: play-test. The numbers say SH2-rate and wind-free;
  whether the courtyard now READS as billowing, and whether 15x is too fast
  rather than merely fast enough, is a look judgement. Speed is one
  constant if it wants backing off.

  NOT DONE: SH2's true timescale is still only an upper bound (~2.2 s) --
  pinning it needs a screen recording and PCSX2 was not running.
  SHIPPED 2026-08-02 -- user play-test sign-off: "I have checked it
  already and it looks much, much better."

  That clears the remaining gate as this bullet framed it. Both halves of
  the look judgement it asked for are answered: the courtyard DOES now read
  as billowing, and **15x is not too fast** -- the speed constant stays
  where it is and does not want backing off.

  One loose end is now MOOT rather than outstanding. The bullet recorded
  that SH2's true timescale was still only an upper bound (~2.2 s), pinnable
  only with a screen recording that PCSX2 was not up to make. It existed to
  settle whether our rate was right; the eye has now settled that directly,
  so the measurement has nothing left to decide. Not filed as follow-up.

  Still true and reusable from this item: `-rippletime <sec>` overrides the
  pinned ripple clock so a time-varying effect can be sampled at chosen
  instants without a rebuild per sample.

- 📋 [DOOM-0301] **The game should be able to play itself, so footage can be captured without a human at the keyboard.**
  User, 2026-08-01, with the reason stated plainly, which is what should
  shape the design: "I realise I need to publish videos on YouTube to try
  and get more eyes on my GitHub projects in the hopes of getting
  donations. And thus I need easy ways of creating DOOM 1 + 2 videos."

  So the goal is NOT a competent DOOM bot as an end in itself. It is
  FOOTAGE: long, unattended, watchable, across both IWADs, that shows off
  what this fork actually does differently -- the lighting, the fog, the
  HD art. Judge every design choice against that, not against how well it
  plays. A bot that dies on MAP07 after four minutes fails; one that
  strolls a level looking at things for twenty minutes succeeds.

  That reframing matters because it makes the hard version optional.
  DOOM has no navmesh and no pathfinding, so "plays DOOM well" is a real
  research project. Three routes, cheapest first:

    (a) A CAMERA, not a player. Fly a spectator through the level on a
        route derived from the map -- the DOOM-0011 seep field already
        holds a per-cell grid of where the open air is, and DOOM-0294's
        developer view already has no-clip and free movement. Shows the
        renderer off, needs no combat AI, and is the most likely thing to
        produce a usable "look at this lighting" video. Probably the
        first thing to build.
    (b) A WALKER, reusing the monster AI. This is the reuse answer and it
        is a good one: P_Move / P_TryWalk / P_NewChaseDir already
        implement "step toward something, slide along walls, refuse to
        walk off a ledge", and they run on any mobj. Point the player at
        the level's exit switch (or at successive key pickups) and let
        the same code walk it, firing at whatever ends up in front. The
        hard part is not movement, it is knowing WHERE to go -- doors,
        keys and lifts are a dependency graph the engine never builds.
    (c) A real bot. Only if (a) and (b) prove insufficient.

  Prior art in-tree, both of which need checking before anything is
  written: DOOM's own DEMO system records and replays exact input
  deterministically (-record / -playdemo / -timedemo), which would give
  perfect unattended playback of a route a human walked ONCE -- that may
  be most of the answer for a launch trailer. But this fork's attract
  demos are already known version-mismatched (see
  [[doom-ants-launch-screenshot-harness]]), so the demo format's health
  here is an open question, not an assumption.

  Ties to capture work rather than standing alone:
    - It should be drivable from the command line and run unattended, so
      it composes with a recorder. Same posture as -warpto, -shotverify
      and -rtverify: the flag IS the interface.
    - It wants a HUD-less / weapon-less presentation option for clean
      footage, and probably a slow cinematic turn rate -- a bot's
      instant snap-turns look like a bot.
    - It would also close a real testing gap. Every fog and lighting
      judgement in this project so far has been made on STILL FRAMES,
      and DOOM-0300 exists precisely because a still cannot show whether
      something moves. A self-playing camera is the instrument that
      question needs.

  Needs a design pass -- /write-spec, then the rule-14 gate. Sequence is
  the user's call; it is not blocked by any of the fog work.
  **Layman:** A mode where DOOM plays itself -- walks the level, fights, finds the exit -- so hours of video can be recorded for YouTube without anyone having to sit and play it.
  Kind: feature.
  Lanes: playsim, tooling.
  Source: user-request-2026-08-01.

- ✅ [DOOM-0302] **A nukage pool glowed only in patches, because the per-texel emissive mask was applied to liquids too.**
  User report 2026-08-02, with four F12 captures: "it looks like there is
  point lights placed at random" in the pools, "just parts of the pools
  glowing". Ultra RT, confirmed by the Video menu in the fourth shot.

  ROOT CAUSE: shadeSurface multiplied the material's Le by
  emissiveMask(albedo) = smoothstep(0.30, 0.60, max channel of this
  texel). That is DOOM-0084, and it is right for a lamp -- it glows from
  its lit top, not its dark metal stand. It is wrong for a liquid.
  NUKAGE's flat is deeply mottled, so its dark blobs fell UNDER the 0.30
  threshold and emitted nothing at all while the bright ridges emitted
  full Le. The pool therefore glowed in a field of patches that read
  exactly as scattered point lights. Liquids are the one material whose
  Le is not derived from the texture but FORCED CONSTANT by name
  (ForceLiquidEmissive, INV-7), so there is no dark-stand problem to
  solve there.

  FIX: emisWeight(mc, albedo) returns 1.0 for a liquid and the DOOM-0084
  mask otherwise. The decision moved to pathtrace.comp, the only place
  that can see MatCtrl.flags; galbedo.a now carries the full emission
  WEIGHT rather than a 0/1 enable, so svgf_composite.comp just
  multiplies. That also deletes a duplicated smoothstep that two shaders
  had to keep in step by hand.

  A PROPORTIONAL variant was built and captured first -- smoothstep(0,
  HI), no dead zone, so dark sludge emits less but never nothing. It did
  NOT fix the report: tracking the albedo at all reproduces the same
  blob field, just dimmer. The patchiness IS the texture, so only a
  constant removes it. Recorded because it is the more conservative fix
  and it looks right until captured.

  RE-TUNE: with the whole surface emitting, the old constants measured
  51x brighter in linear green at the E1M1 courtyard and blew out to a
  flat white-green slab -- the old numbers were really the peak of a few
  ridges, not the pool's brightness. kNukageLe 0.35/1.30/0.15 ->
  0.05/0.19/0.02, kLavaLe 2.20/0.75/0.12 -> 0.55/0.19/0.03.

  Gates: make test green; -rtverify PASS on doom.wad (rel-MSE 0.1091% vs
  0.50% bar, white furnace 0.000000). AWAITING the user's eye on the
  brightness -- the pool is uniform and clearly self-lit, but a flat
  emitter necessarily flattens the surface texture, and that trade is a
  look call, not a measurement.
  **Layman:** Nukage pools looked like they had random glowing spots in them. Now the whole pool glows evenly.
  Kind: fix.
  Lanes: shaders, rt.
  Source: user-report-2026-08-02.
  SHIPPED 2026-08-02 -- user play-test sign-off: "I have checked it
  already and it looks much, much better."

  That answers the specific question this bullet was held open on. The
  trade it names -- a flat emitter necessarily flattens the pool's surface
  texture, and whether that reads worse than the patchiness it replaced --
  is resolved in favour of the uniform emitter. **kNukageLe 0.05/0.19/0.02
  and kLavaLe 0.55/0.19/0.03 stand as tuned**; no further re-tune is owed.

  Gates were already green before the play-test: make test, and -rtverify
  PASS on doom.wad (rel-MSE 0.1091% vs the 0.50% bar, white furnace
  0.000000). Note for anyone re-reading that number: DOOM-0297 has since
  established it is a doom.wad-only gate, and that doom2's failure is
  estimator variance rather than a defect.

- ✅ [DOOM-0303] **-devshot N takes a developer screenshot headlessly, without -shotverify's canonical config pin.**
  Found while investigating the liquid-glow report. -shotverify PINS a
  canonical RT config (rb_fog, rb_wet, rb_flashlight, rb_renderscale,
  ...) so the DOOM-0202 golden gate is reproducible regardless of
  ~/.doomrc. That is correct for a gate and exactly wrong for a look
  investigation: FIVE captures taken through it to A/B rt_fog 0 vs 2 and
  rt_wet 0 vs 1 came back identical to within the 0.0087/255 noise
  floor, because every one of them rendered the same pinned frame. The
  toggles under test were the ones being overwritten.

  -devshot N arms the existing DOOM-0294 rb_devshot capture after N
  presents, so a shot can be taken under -noinput (F12 cannot be pressed
  there, and xdotool cannot inject into a Wayland client). It pins
  nothing. Re-running the same A/B with it moved the frame by MAE 140 of
  255, which is what a fog toggle should do.

  Lesson worth keeping: a capture harness that silently normalises the
  variable under test reports a clean, confident, meaningless result.
  **Layman:** A way to screenshot the game automatically for testing, showing your real settings.
  Kind: test.
  Lanes: rt, test.
  Source: in-session-2026-08-02.

- ✅ [DOOM-0304] **The spec still describes a torch-selection scheme L3 never shipped.**
  DOOM-0011 §4.4(b) specifies torch selection as: iterate the static
  emitter slice `k in [0, omniStart)`, pick the NEAREST FEW by centroid
  distance, and multiply each by `mediumTint`. The shipped
  `torchInscatter` does none of those. It reads a per-cell,
  brightest-first list of at most `kFogLightsPerCell` (= 2) lights,
  baked at level load onto the seep grid and indexed by
  `fogLightCell(p.xy)`, and it applies no medium tint -- deliberately,
  since a torch carries the emitter's own Le colour.

  Found by the two cold lanes gating DOOM-0295's half-rate amendment,
  which is why the fix is filed rather than folded in: documenting the
  shipped scheme is a section, and a perf amendment is the wrong place
  to smuggle one.

  FOUR sites carry the stale mechanism, not one -- the second lane's
  contribution, and the reason this is worth an item rather than an
  edit:
    - §4.4(b)'s bullets (now marked SUPERSEDED inline, pointing here)
    - the layer table's L3 row ("iterate static emitters k<omniStart
      (nearest-few, no occlusion first)")
    - Q2 ("nearest-few emitters with no occlusion ray")
    - Q23, which is worse than stale: it is a LIVE DIRECTIVE telling an
      implementer to measure the scan and amend §4.4(b) to match. The
      shipped code took a third option Q23 does not contemplate. Closed
      2026-08-02 against the shipped answer.

  Also stale in the same family, and cheap to fold into the same pass:
  `DOOM-0011-implementation-plan.md` carries `kTorchShaftStrength = 1.0`
  (:216) and the superseded nearest-few code sketch (:1881-1905). That
  is the plan, already executed, so it is stale rather than harmful.

  Do NOT re-review to rediscover any of this -- it is written down here
  and in the fix ledger's 2026-08-02 section. Fold it in directly.
  **Layman:** A design document describes an older way the torch-in-fog lighting was going to work, not the way it actually works.
  Kind: doc.
  Lanes: docs, fog.
  Source: cold-eyes-2026-08-02 (DOOM-0295 amendment gate).
  Resolved (2026-08-03, 8febcc7 + 76fb0b7). §4.4(b) now documents the
  shipped scheme; §7's L3 row, Q2 and the superseded-mechanism note follow
  it; the plan gets stale-markers only.

  The count in this bullet was WRONG and is corrected in the doc: FIVE
  sites carried the superseded mechanism, not four. §6's "drop the emitter
  occlusion ray" perf lever and INV-2's `k < omniStart` falsifier were both
  missed by the original scan and found by the rule-14 gate.

  The gate also caught two errors in the fix itself, which is the argument
  for running it on a fold-in that "only" transcribes shipped behaviour:
  "ranked brightest-first" (the bake ranks by unoccluded CONTRIBUTION, the
  same windowed curve torchInscatter evaluates -- and the cell-boundary
  continuity argument depends on that match), and kFogSteps written as
  "still open" when the shader records the raise as falsified and reverted.

  One thing this bullet asked for was NOT done, deliberately. Folding in
  "the shipped scheme" tempted a claim that the torch term is untinted by
  design. It is untinted because L4 has not shipped -- mediumTint does not
  exist in the shaders. §4.4(b) now carries the tint as an OPEN DECISION
  for the user rather than settling it. Both cold lanes independently got
  this backwards and would have deleted the L4 contract.

  The rest of the gate's verified tail is DOOM-0308, with a recommendation
  to split the spec.

- 📋 [DOOM-0305] **The fog-light re-bake's settle timer is map-global, so one cycling lift defers every door.**
  Found while building DOOM-0296, recorded rather than fixed there.

  `RB_UPD_MOVED` reports that SOME sector plane moved this frame, not which
  one. DOOM-0296's `g.fogLightStill` is a single global reset by that
  signal, so a lift cycling in an unvisited corner of the map keeps
  resetting it and no door ever reaches `kFogLightSettle`. Every re-bake
  then falls through to the `kFogLightMaxWait` cap instead: the torchlight
  arrives up to 4 s after the door finishes rather than 0.15 s, on any map
  with a mover running somewhere.

  Two things make this less bad than it sounds, which is why it shipped:
  the cap bounds it, and vanilla DOOM 1/2 maps rarely have a plane in
  motion at an arbitrary moment. It gets worse on a PWAD with ambient
  machinery.

  The fix needs a per-sector dirty set the engine does not currently keep.
  `RB_UpdateMeshHeights` walks vertices and could report WHICH sectors
  moved at little extra cost; the re-bake would then hold a timer keyed to
  the nearest moved sector, or simply ignore movement outside any light's
  reach of a cell it would change. The second is probably cheaper and is
  the same reach bound DOOM-0296's D4 already computes.

  Sequence after the DOOM-0296 play-test, which may make it moot: if the
  snap reads fine at cap latency, this is not worth a dirty set.
  **Layman:** If any platform anywhere on the map is moving, opening a door makes the mist wait a few seconds to catch the torchlight instead of a fraction of one.
  Kind: enhancement.
  Source: in-session-2026-08-02.

- 📋 [DOOM-0306] **Two halves of INV-6 were specced and never built: the Cornell reference scene and the reference-convergence self-check.**
  Found by the cold-eyes pass on DOOM-0297's INV-6 amendment, which asked
  what else in that invariant is asserted rather than implemented. Two
  things, both stated in the base paragraph since the spec was written:

  1. **The authored Cornell-style test scene.** INV-6 says the bar is
     measured "on the white-furnace + a reference Cornell-style DOOM room
     (a small test scene this spec's implementer authors)". No such scene
     exists. `RB_RtVerify` has always measured a real game map at whatever
     camera the first ready present holds. That is why the score turned out
     to depend on the IWAD at all (DOOM-0297) -- an authored scene would
     have been IWAD-independent by construction, which is the property the
     clause was there to provide.

  2. **The reference-convergence self-check.** INV-6 says the reference
     "counts as converged only when doubling it to 8192 spp shifts the
     image by < 0.5% rel-MSE". `RB_RtVerify` runs exactly three estimators
     and no doubling pass, and `8192` appears nowhere in the engine. So the
     reference has never been checked for convergence by the mechanism its
     own invariant names -- it has only ever been assumed converged.

  Neither is urgent: DOOM-0297 established the shipped gate passes on both
  IWADs with headroom, and (2)'s absence is why (1) matters less than it
  looks -- with a per-gamemode reference spp, "doubling it to 8192" is not
  even arithmetically meaningful any more and would have to be restated as
  doubling that row's own count.

  Worth doing because the two together are the reason a red gate took
  months to diagnose (DOOM-0208 closed the doom2 failure as "a transient
  environmental blip"; it was neither). An IWAD-independent authored scene
  would make the gate's number mean one thing regardless of what is loaded,
  and a real convergence check would stop the reference being the silent
  unknown in every comparison.

  Sequence: after any further DOOM-0297 follow-up. Doing (2) first is
  cheaper and answers more -- it is one extra estimator run and a
  comparison, against authoring a whole scene for (1).
  **Layman:** The renderer's accuracy self-test is missing two checks its own design document says it has.
  Kind: test.
  Source: cold-eyes-2026-08-02.

- ✅ [DOOM-0307] **An ordinary wall texture emits light because its pale panels clear the emitter gate.**
  User report 2026-08-03, three F12 captures from a doom2 MAP01
  play-test: "the perimeter wall looks like it is glowing". It does. The
  wall's pale rectangular panels read as blown-out white while the
  mottled green parts around them stay dark, so the wall reads as a bank
  of lights rather than as masonry.

  SAME SIGNATURE AS DOOM-0302, one layer earlier. There the per-texel
  emissive mask made a nukage pool glow in patches; here the *material*
  itself clears the emitter gate. `derive_material_le`
  (`emissive_derive.h:86`) admits any tile whose near-fullbright texel
  count clears `kEmitterPeakLum` / `kEmitterMinBrightFrac`, and it rates
  brightness by VALUE (max channel) precisely so a saturated light is not
  missed. A wall texture with large pale panels satisfies that as
  readily as a lamp does, and nothing downstream asks whether the thing
  is *supposed* to be a light. 1042 of 1962 materials are currently
  flagged emissive.

  REPRODUCED HEADLESSLY, and the two obvious explanations are both
  excluded:
    fog off      the panels are exactly as blown out at `rt_fog 0`, so it
                 is not the volumetrics being milky
    paletted art identical again with `DOOMASSETDIR` unset (`HD load done
                 - 0 material(s)`), so DOOM-0042's HD art is not the
                 source and DOOM-0178 is not implicated

    ./linux/linuxxdoom -iwad ../wads/doom2.wad -warp 1 \
        -warpto -700 597 180 -config <fog-off cfg> -shotverify out.png

  NOT YET ESTABLISHED, and the fix shape depends on it: how many of the
  428 wall textures clear the gate. The 1042 figure is dominated by the
  1381 sprites and says nothing on its own. A per-class count is one
  temporary print in `ComputeMaterialEmissive`, and is the first step —
  if most walls qualify then the gate is wrong for walls as a class, and
  if a handful do then it is a per-texture allow/deny question like
  DOOM-0157's sprite_glows list.

  Sequence: it is a look defect on a shipped path, so it outranks the
  DOOM-0011 tail. Related but distinct: DOOM-0084 (the mask this reuses),
  DOOM-0193 (dial UP the intended glows — do not confuse the two).
  **Layman:** A plain outdoor wall glows white in patches, as if it were a light rather than a wall.
  Kind: fix.
  Lanes: shaders, rt.
  Source: user-play-test-2026-08-03.
  Measured (2026-08-03), and it is a CLASS problem, not a handful:

    doom2.wad   82/428 walls, 20/153 flats, 940/1381 sprites emit
    doom.wad    60/287 walls, 19/111 flats, 479/764 sprites emit

  Roughly one wall texture in five is a light source. Temporary probe in
  UploadAtlas printed the emissive wall ids with their summed Le; ids
  mapped to names offline from the WAD's TEXTURE1 lump (the back end is
  DOOM-header-free, so it cannot name them itself). Probe REVERTED.

  The gate does not merely over-fire — it cannot tell the two apart. The
  same threshold admits both of these:

    legitimately a light      FIREWALL / FIREWALA / FIREWALB (11-12),
                              DOORYEL2 (7.97), SW1CMT / SW2CMT (12.86)
    plainly not a light       CEMENT1..CEMENT9 (7.5-17.1) -- nine plain
                              grey concrete walls, and the likeliest
                              match for the wall in the user's shots;
                              ZZWOLF2/3/4/6/7/13 (5.2-16.3) and
                              ZDOORB1 / ZDOORF1 (38.47, the two BRIGHTEST
                              emitters in the IWAD) -- Wolfenstein walls
                              and doors; SKINFACE / SKINLOW / SKSNAKE1 /
                              SKSPINE1 / SKSPINE2 (7.1-14.6); AASHITTY

  SKY2 (19.76) is its own question and should not be lumped in: the sky
  arguably SHOULD be a light source, but it is already the sun via
  kSunDir, so it may well be counted twice today.

  What this rules out: a per-texture deny-list. Nine CEMENTs, six
  ZZWOLFs and five skin walls is not a list, it is a missing distinction
  -- "pale" is being read as "bright". Two shapes worth weighing before
  building either: (a) require SATURATION or hue, so a near-neutral pale
  panel fails where a coloured or fiery one passes; (b) require the
  bright region to be a small fraction of the tile, so a lamp's lit face
  passes and a whole pale wall does not -- note kEmitterMinBrightFrac is
  currently a MINIMUM, so adding a maximum is a small change to an
  existing gate rather than a new one.

  Do NOT re-measure to rediscover the above; the counts and the names are
  here.
  Measured (2026-08-03), offline against the WAD lumps -- no engine run. A
  throwaway Python probe reusing scripts/pbr_derive.py's Wad reader
  reproduces the gate EXACTLY (82/428 doom2, 60/287 doom), so it is a
  faithful stand-in for emis::derive_material_le.

  RESULT: NO TEXEL STATISTIC SEPARATES THE TWO CLASSES. Do not spend
  another pass looking for one.

  Root cause, from the palette dump: every peak texel is merely the TOP OF
  SOME PALETTE RAMP. value() = max channel, and DOOM's ramps almost all
  top out at 255 in one channel, so "brightest tan" scores identically to
  "pure red fire". Two pairs prove colour can never work --
    FIREWALL peaks are #176(255,0,0) + #175(255,31,31); AASHITTY's peaks
    are the SAME entries.
    SW1CMT peaks are #209(255,235,219) + #52 + #226; CEMENT1's are the
    SAME entries (SW1CMT is a switch drawn on cement).

  Candidates measured and FALSIFIED, each with LIGHT/not ranges fully
  overlapping: peak fraction, bright fraction, mean saturation, hue, tile
  luminance, peak luminance, global contrast (peakLum/tileLum), LOCAL ring
  contrast at r=3 (the standard brightmap heuristic), connected-component
  count, largest-component fraction, tile area.

  Exhaustive threshold search over 76 hand-labelled doom2 walls (27 light,
  49 not): calling everything "not a light" costs 27 errors; the best
  SINGLE-feature threshold costs 17; the best two-feature AND-rule costs
  13 -- and it reaches 13 only by rejecting FIREWALL, FIREBLU1, DOORYEL2,
  TEKLITE and LITEBLU1, i.e. by throwing away the textures everyone agrees
  ARE lights. An automatic discriminator tops out at ~83% while getting
  the headline case backwards.

  WHAT DOES WORK -- the engine's own tables, no new hand-authored list.
  Keeping the peak gate as a NECESSARY condition and additionally
  requiring the texture to be named by animdefs[] (p_spec.c:103,
  istexture entries, alpha/numeric frame runs expanded) or by
  alphSwitchList (p_switch.c:48) admits 27 of the 82 in doom2 and 21 of
  the 60 in doom, and drops every reported false positive: all nine
  CEMENTs, every ZZWOLF, ZDOORB1/F1, SKINFACE/SKINLOW/SKSNAKE1/SKSPINE1/2,
  AASHITTY, METAL6/7, BIGBRIK3, ZZZFACE3/4.

  It also drops a short residue of real lights the two tables do not name:
  LITEBLU1, LITEBLU2, LITERED, TEKLITE, DOORYEL2, DOORBLU2, SILVER2 (and
  arguably BRICKLIT, TEKGREN5, REDWALL1). Eight to eleven names -- the
  DOOM-0157 sprite_glows shape, and an order of magnitude shorter than the
  94-name deny-list the bullet already ruled out. Imperfections it keeps:
  BLODRIP1-4 (blood drips, animated but not lights) and ROCKRED1.

  Probes are throwaway, in the session scratchpad, NOT committed.
  Implemented 2026-08-03 (build green, all 7 test binaries pass). AWAITING
  PLAY-TEST -- not flipped.

  The fix is a curated wall-light list, because the measurement above says no
  texel statistic can do it. Re-checked against the ART (every candidate
  rendered from the WAD to a contact sheet and looked at), which corrected
  two things this bullet had wrong:

    * SW1CMT / SW2CMT (12.86) are NOT lights. That Le is the CEMENT wall
      BEHIND the switch -- which is why it sat beside CEMENT1's 15.32. The
      switch plate itself is unlit.
    * The tables-only shape floated earlier would have DROPPED ~19 real
      lights: METAL6/7, BIGBRIK3, BRONZE4, TEKGREN5 and SPCDOOR1/2 all carry
      a genuine yellow lit strip (METAL6 and METAL7 are the same strip at top
      and at bottom -- hence their identical texel counts), and BRICKLIT,
      BSTONE3, CRACKLE2/4, EXITDOOR, SILVER2, TEKBRON2, TEKWALL1/4/6 all
      carry a real fixture too.

  SECOND DEFECT, found on the way and fixed in the same change: the gate was
  wrong in BOTH directions. DOOM's own light panels -- LITE3, LITE5,
  LITEBLU3/4, LITESTON, TEKLITE2, COMPSTA1/2, the FIRELAVA / FIREMAG family
  -- peak at only ~0.53 linear, never cleared kEmitterPeakLum = 0.9, and
  emitted NOTHING. The lamps were dark while the walls glowed.

  MECHANISM (3 files, mirrors DOOM-0157's sprite_glows exactly):
    r_mesh.c   wall_light_tex[59] + ensure_wall_light_map + RB_WallTexEmits.
               Names absent from the loaded IWAD are skipped, so one table
               serves DOOM and DOOM II. No per-map authoring.
    r_mesh.h   RB_WallTexEmits declaration.
    r_vulkan.cpp ComputeMaterialEmissive: an unlisted WALL skips derivation
               (Le stays 0); a listed one passes allowFaint, so the gate is
               BYPASSED for walls rather than narrowed -- the list, not the
               texels, is the answer. Flats and sprites untouched.

  MEASURED before/after, from the model that reproduces the shipped gate
  exactly:
    doom2   82 -> 47 emitters; 53 stop, 18 previously-dark fixtures start
    doom    60 -> 37 emitters; 41 stop, 18 start
  Everything reported is gone: ZDOORB1/F1 (38.47, the worst), all nine
  CEMENTs, every ZZWOLF, SKINFACE/SKINLOW/SKSNAKE1/SKSPINE1/2, AASHITTY,
  SP_FACE2, ZZZFACE3/4, the BLODRIPs and the unlit SW1 skull switches.

  SKY2/SKY3/SKY4 also stop, and that is a no-op, not a change: their Le was
  DEAD DATA. Verified two ways -- no stock linedef in either IWAD uses a SKY*
  texture (scanned every map's SIDEDEFS: 377 and 245 distinct wall textures
  in use, zero SKY*), and a sky flat never becomes world geometry
  (emit_sky_cap writes the SEPARATE sky[] array, r_mesh.h:124, on a
  primary-ray-only mask, while BuildStaticEmitterSet walks levelMesh only).
  So the sun was always the only sky light; the double-count this bullet
  worried about does not exist.

  FOR THE USER'S EYE on play-test: LITE3 lands at Le 33.1 and LITE5 at 24.3
  -- brighter than FIREWALL (11.25), because those textures are MOSTLY light
  panel. Physically defensible for a bank of fluorescents, but it is the same
  magnitude that made CEMENT look wrong, so it is the first thing to judge.
  REDWALL1 (19.93, doom only) is the other one to watch.
  Resolved (2026-08-03): VISUALLY VERIFIED in-engine, both directions, by
  capture rather than by play-test.

  Method: same view, two builds -- the shipped binary and a worktree built
  at the parent commit -- via
    ./linux/linuxxdoom -iwad ../wads/doom2.wad -warp 1 -warpto -700 597 180
        -config <renderer 1, rt_fog 0> -noinput -shotverify <out>.png
  `DOOMASSETDIR` left unset (`HD load done - 0 material(s)`), matching the
  paletted path the defect was reproduced on.

  NOISE FLOOR IS EXACTLY ZERO. Two captures of the same build came back
  byte-identical, so every changed pixel is signal, not sampling. (The
  control proves determinism at the file level only -- it cannot validate a
  decoder, so the pixel counts below were computed twice, with a hand-rolled
  PNG reader and with PIL/numpy, and agree exactly. ImageMagick's
  `compare -metric AE` disagreed by 20x and is the outlier.)

    courtyard (the reported view)   53.79% of pixels changed, mean |d| 9.43,
                                    max 119; 10.66% of pixels moved by >16
    LITE5 alcove                    67.18% changed, mean |d| 7.28, max 154

  THE DEFECT IS GONE. In the before capture the perimeter wall's pale panels
  are clipped to pure white with no texture visible at all -- a bank of
  backlit panels, which is what the user photographed. After, the same wall
  reads as masonry: mottled stone, tan weathering, green mould and the panel
  borders all legible. It is the CEMENT family, as this bullet predicted.

  THE SECOND HALF WORKS TOO, and this is the half that had no evidence
  before. MAP01 uses LITE5 (2 linedefs, 191 and 195). Head-on at
  `-warpto 780 504 0`: before, the fixture is a dull GREY ladder lit only by
  ambient -- it was not a light. After, it reads as a lit strip and throws a
  modest glow onto the brown column beside it.

  The Le = 24.3 worry recorded above did NOT materialise: the bright region
  stays confined to the fixture's own bands and the surrounding wall gains
  only a small lift, because the fixture subtends little of the frame. No
  blowout. LITE3 (33.1) is unexercised on MAP01 -- it first appears on MAP04,
  alongside LITEBLU4 and TEKLITE2 -- so if any dial-down is ever wanted, that
  is the map to look at. 17 of the 32 maps contain a newly-emitting fixture.

  Build green, all 7 test binaries pass. Follow-up filed as DOOM-0309 (the HD
  material generator still uses the gate this change replaced).

- 📋 [DOOM-0308] **The DOOM-0011 spec's verified cold-eyes tail, filed rather than looped — and the case for splitting the document.**
  Two independent cold lanes read docs/specs/DOOM-0011-volumetric-
  lighting.md (3381 lines) against the shipped renderer. Sixteen findings
  were verified and fixed in that pass. These are the REST -- every one
  verified against current source, none fixed.

  DO NOT RE-REVIEW TO REDISCOVER THESE. A fresh loop costs a full
  multi-agent dispatch to regenerate what is written here. Fold them in
  directly.

  TWO NEED A DECISION, NOT AN EDIT -- they are why this is filed rather
  than finished:

  1. THREE INCOMPATIBLE DENSITY FORMULAS, one of which declares itself
     canonical. 4.3b's sigma_final block (~:806) says "This is the single
     authoritative statement -- 4.3a and INV-9 point here rather than
     restating it", and it carries NO floor-fog term. 4.3c (~:1000) gives
     sigma = (sigma_general + sigma_floor) * fogStrengthScale *
     skyExposure. INV-9 (~:2915) gives (skySigma + floorSigma) *
     skyExposure + areaSigma. They disagree on the addends AND on what
     skyExposure multiplies -- and 4.3a:419 calls that second point
     "load-bearing and was got wrong in the first draft". Every layer's
     density is built from one of these. Reconciling them is a structural
     rewrite of the section, not a patch.

  2. DOES A TORCH SHAFT TAKE THE MEDIUM'S COLOUR? 4.4(b) now carries this
     as an explicit open decision (banner added 2026-08-03). The code has
     no tint because L4 has not shipped -- mediumTint does not exist in
     the shaders and kGooTint/kHellTint are declared unread. Meanwhile
     4.3, 4.5 and 7's L4 acceptance row all specify the tint ("a torch
     shaft in a goo room is warm-through-green"). L4 cannot be accepted
     until the user picks. NOTE both review lanes read this as stale text
     to delete; that was wrong, and deleting it would have silently
     dropped a design decision.

  MECHANICAL, HIGH VALUE (an implementer is blocked or misled):

  3. 5 never lists L3's FogLights storage buffer (set 0 binding 6, stride
     RB_FOG_LIGHTS_PER_CELL x 8 floats, ~225 KB on E1M1), while :2102
     still says "No new SSBOs, light/emitter buffers" and 4.4:1916 asserts
     "no new resource appears in 5". The whole RB_FOG_LIGHT_* family is
     also undefined in the document though INV-14 uses it: CLUSTER 64,
     TESTZ 24, SUBS 2, PROBES 4, CUTOFF 0.04f, MAXREACH 512. The bake is
     not buildable from the spec as it stands.
  4. 7's shipped markers are stale for L1c, L1d, L1e, L2b and L3 -- all
     shipped per the body, none marked in the build order. A reader of 7
     alone concludes L1c onward is unbuilt.
  5. 4.3b's wisp constants are stale against 5 and the shader: kWispAmp
     0.6 -> ships 1.0 (so the bound is 0x..2x, not 0.4x..1.6x); octave-1
     scale 1/512 -> 1/192. Three derived figures rot with them -- the
     "1/f1 = 512x too fast" drift claim, Q21's "tiling period is 13107
     units" (should be ~4915), and "the finer octave drifts slower"
     (kWispVel2 = -kWispVel1 since DOOM-0300).
  6. Q30 is closed at :2545 ("No rate limit is needed, and that is a
     measurement rather than a budget") but still listed OPEN in 10, and
     4.4:1645 and :1883 issue binding instructions to "whoever closes
     Q30" about a cadence that will never exist.

  MECHANICAL, MEDIUM:

  7. 4.4(b)'s bake write-up omits contract detail an implementer must
     otherwise guess: the sight test runs at tz = floor + TESTZ (24),
     clamped to mid-column when the ceiling is low; emitter triangles are
     snapped to a 64-unit lattice with a power-weighted centroid and
     intensity sum(Le*area) -- which is what `lum` in
     reach = sqrt(lum/cutoff) actually measures; cells with no air are
     skipped (RB_SeepCellAir) and clusters with lum <= 0 dropped.
  8. No invariant pins L3's central guarantee -- selection is baked per
     cell at load, the march does no emitter scan and no per-sample
     occlusion ray. 6:2630 says "a per-sample ray is never affordable in
     this march", but nothing forbids a later layer adding one for
     torches.
  9. 6 has no measured box for L3; its 0.83 ms lives only in 4.4's
     amendment, and 6:2368's ">= 6% reserved for L2-L5" is never
     reconciled against it. 6:2473 sets the precedent that a layer is not
     done until its number is in 6.
  10. Q24 is still posed as live for the reverted density raise.
  11. :1019's "16% at 512 units" for the aerial layer contradicts 4.3's
      own shipped table (61% at eye height, 512 u) -- and the "3x the
      aerial layer" framing for kFloorFogDensity rests on that stale pair.
  12. Two bake-cost pairs disagree 22 lines apart: :1812 says 3.6 ms /
      6320 tests on E1M1 and 3.4 ms / 5228 on MAP01; :1834 says 4.1 ms
      E1M1 and 2.9 ms MAP01. MAP01 falls, unexplained.

  LOW: the :3 status header still says "L1 + L1b implemented"; Q16's
  kSeepMax 0.5 / kSeepFalloff 192 against shipped 0.9 / 384; 14.97 vs
  14.96 ms for one figure; 284 ms vs INV-14's 290 ms for the same 79
  grids; :1136 reads as if DOOM-0289 cost 13.6 ms (that was L2's ray);
  :881 and 6 item 2 still call the deleted up-ray "the march's dominant
  cost"; two bullets both numbered Q23 and Q24a precedes Q24; "one indexed
  read" is one index plus up to four vec4 loads; "same 64-unit cell" is
  not guaranteed (the cell doubles until the grid fits 256x256).

  AND THE STRUCTURAL POINT, which is the real recommendation: 3381 lines
  produced ~30 verified findings on ONE loop, and the great majority had
  nothing to do with the change being gated. The failure mode is legible
  in the findings themselves -- amendments that supersede earlier text in
  place, so a top-down reader hits the abandoned contract first and the
  retraction a hundred lines later. Splitting is the cheaper fix than
  looping: the natural seams are 4.3/4.3a-c (density + the fields),
  4.4 (light sources + the bakes), and 4.6/4.6a (resolve + composite).
  Per the standing rule, past loop 3 the tail is filed rather than looped
  -- this stopped at loop 1 because the tail is dominated by pre-existing
  debt a second cold read would only re-find.
  **Layman:** A list of already-checked errors in the fog design document, written down so nobody has to find them again.
  Kind: doc.
  Lanes: docs, fog.
  Source: cold-eyes-2026-08-03 (DOOM-0304 fold-in gate, 2 lanes).
  Progress (2026-08-03): item 2 of the two decisions is CLOSED by the
  user — **a torch shaft is NOT tinted by the medium.** It keeps its
  emitter's own Le, so a flame reads warm through green air; the room's
  colour comes from the fog around it. Folded into all five sites in the
  same pass (§4.4(b) now states it as a contract on L4, and §4.3, §4.5's
  colour formula, §4.5's compose paragraph and §7's L4 acceptance row all
  follow it). **L4 applies mediumTint to the SKY term only.**

  Item 1 (the three incompatible density formulas) is still open and is
  now the only decision left on this list.

  The user has also chosen to **SPLIT the spec three ways** — density +
  the fields (§4.3/§4.3a-c), light sources + the bakes (§4.4), resolve +
  composite (§4.6/§4.6a). Not started. Sequence it AFTER DOOM-0307, which
  is a visible defect on a shipped path. Note the split is the better
  moment to fix item 1: reconciling three density formulas is a rewrite of
  the section that would become its own document anyway.
  Item 1 (the three incompatible density formulas) is CLOSED, 2026-08-03 --
  and it turned out to be a measurement, not a judgement call. The shipped
  shader decides it.

  `pathtrace.comp` marchFog:

      float sigma = (fogDensity(p, baseZ, poolH) + floorFogDensity(p, baseZ, t))
                    * strength * skyExposure * wisp(p, rippleTime());

  Two addends; strength, skyExposure and wisp all multiply the whole sum.
  No area term, because L4 has not shipped. Against that:

    4.3b's self-declared authoritative sigma_final -- STALE. It predates
    L1e, so it has no floor addend at all. Its OTHER structure is right,
    and is the half that matters: the sky-sourced term is gated by
    skyExposure while the area-profile sum is NOT, which is 4.3a's
    load-bearing rule ("skyExposure gates the SKY-SOURCED haze only --
    never the area profiles", the thing 4.3a:419 says was got wrong in the
    first draft).
    4.3c's sigma -- structurally right, but written before L1c and so
    missing the `wisp` multiplier, and it has no area term.
    INV-9 -- wrong twice: no fogStrengthScale at all, and it predates the
    wisp. Its `+ areaSigma` placement OUTSIDE the gate is right.

  So no one of the three was correct, and each was right about something
  the others got wrong. The single statement that is simultaneously true
  of the shipped code and correct for L4:

      sigma(p,t) = ( (sigma_aerial(p) + sigma_floor(p,t)) * skyExposure
                   + SUM_profiles areaDensity(profile) * areaMult(profile) )
                   * wisp(p,t)
                   * fogStrengthScale

  With L4 unshipped the sum is empty and this reduces EXACTLY to the
  shipped line -- the three surviving factors commute, so it is the same
  expression, not an approximation of it.

  WHY THIS UNBLOCKS L4, which was the point of doing 4.3 first: the open
  question was never "how thick is goo" but WHERE the area term attaches.
  It attaches outside the skyExposure gate and inside wisp and the dial.
  That placement is forced, not chosen -- gate it and a goo room under a
  roof would be driven to kIndoorFogScale (5%) and the profile would
  barely register, which is precisely the failure 4.3a:419 records from
  the first draft. Pair it with the user's 2026-08-03 decision that a
  torch shaft is NOT tinted by the medium (mediumTint applies to the sky
  term only) and L4's contract is fully pinned.

  This lands in the split's part 1 (DOOM-0310) as the section's single
  authoritative sigma, with the other two statements deleted rather than
  annotated -- leaving them as superseded text in place is the exact
  failure mode this bullet identified.
  Progress (2026-08-03): ITEM 1 IS CLOSED AND LANDED, and the mechanical tail with it. The three incompatible density formulas are reconciled to one statement in docs/specs/DOOM-0310-fog-density-fields.md §4.1, decided by the shipped marchFog rather than by judgement, with the two superseded statements DELETED rather than annotated -- the failure mode this bullet identified.

  It took THREE attempts, which is worth recording because it validates the bullet's own structural argument. Loop 1 of DOOM-0310's gate found the parent's §4.5 still carrying a partial sigma (omitting the floor addend, naming a heightPool factor §4.1 does not have). Loop 2 then found that the SPLIT'S OWN POINTER BLOCK, written into the parent's §4.3 during the extraction, was carrying a FOURTH partial sigma -- and it dropped the ray-distance/drift-clock distinction that loop 1 had just added a table to prevent. Both are now pointers. A partial restatement is worse than a link precisely because it reads as authoritative and is never updated when the real one moves.

  FOLDED IN DIRECTLY, no re-review, each re-verified against pt_common.glsl at HEAD: item 5 (the wisp constants and all three derived figures), item 10 (Q24), item 11 (the 16%-at-512 contradiction), and the density/field LOW items -- Q16's kSeepMax/kSeepFalloff against the shipped 0.9/384, the ":881 up-ray is the march's dominant cost" claim (the up-ray is deleted), the cell-size assumption (64 units INITIALLY; it doubles until the grid fits RB_SEEP_MAXDIM), and the Q-numbering hygiene.

  Item 11 turned out to matter more than LOW: the stale figure was not merely inconsistent, it was oversizing an obligation part 3 has to build to. The floor layer's 37% / tau 0.46 assume a baseZ the shipped code does not use outdoors; a sky pixel is open-sky by definition, so the sky-seam addend's size is the outdoor pair (~16% / 0.17), not the roofed one. The parent's live INV-10 has been corrected.

  Items 3, 4, 6, 7, 8, 9, and 12 belong to parts 2 (DOOM-0011 §4.4, light sources and the bakes) and 3 (§4.6/§4.6a) and are NOT yet folded in -- they are the FogLights SSBO missing from §5, the RB_FOG_LIGHT_* family being undefined, §7's stale shipped markers, Q30's closed-but-listed-open state, the bake's contract detail, the missing L3 invariant and measured box, and the two disagreeing bake-cost pairs. They stay filed here and travel with those parts when their ids are allocated. DO NOT re-review to rediscover them.

  Also found while doing this, and not on the original list: four shipped things the parent never documented (kWispSquashZ, the wisp S-curve and its load-bearing clamp, DOOM-0300's heading rotation, kIndoorSkyLight); the seep field's worst-case size stated as 256 KB when RGBA16F makes it 512 KiB; and Q21 claimed as cited from pt_common.glsl when it appears in no source file at all.

  New code-side item filed: the shipped shader comments carry three figures this document corrects (pt_common.glsl's "+/-60 % swing" and "16% at 512 units"; pathtrace.comp's "would drift 512x too fast", now 192x) plus a sigma comment calling the floor layer "a THIRD addend" where the expression has two. Not fixed under a docs review.

- 📋 [DOOM-0309] **The HD material generator still uses the emitter gate DOOM-0307 just proved cannot classify a wall.**
  Found 2026-08-03 while fixing DOOM-0307. That bullet's measurement shows
  the near-fullbright peak gate cannot tell a light from pale art, in
  either direction. DOOM-0307 replaced it for the PALETTED wall path
  (r_vulkan.cpp ComputeMaterialEmissive now asks r_mesh.c's
  RB_WallTexEmits). The HD path was NOT touched and still uses it.

  `scripts/pbr_derive.py:307` sets `peak = 0.9  # emissive_derive.h
  kEmitterPeakLum` and derives each HD material's emissive map from it.
  docs/specs/DOOM-0042-ultra-hd-pbr-materials.md:78 states the intent as
  "so lit computer panels glow but ordinary walls stay dark" -- which is
  exactly the claim DOOM-0307 measured and falsified. The same nine
  CEMENTs and every ZZWOLF will carry an emissive map.

  NOT the same defect DOOM-0307 reported, and do not conflate them: that
  one was reproduced with `DOOMASSETDIR` unset (`HD load done - 0
  material(s)`), so the paletted Le was the cause of the glowing wall the
  user photographed. This is a second, latent instance in the offline
  generator, and it bites only Ultra with HD art staged.

  Cheapest shape: have the generator read the same list. It is C data
  (r_mesh.c wall_light_tex), so either export it or move the list to a
  shared data file both sides read -- worth deciding rather than
  duplicating 59 names into Python, since a list that exists twice will
  drift.

  Depends on DOOM-0307. Related: DOOM-0042 (the HD pipeline),
  DOOM-0084 (the per-texel mask), DOOM-0193 (dial UP intended glows).
  **Layman:** The high-definition art pipeline decides what glows with the same broken test we just replaced for the normal art.
  Kind: fix.
  Lanes: shaders, assets.
  Source: in-session-2026-08-03 (found while fixing DOOM-0307).

- ✅ [DOOM-0310] **Split part 1 of 3 — fog density and the fields, extracted from the DOOM-0011 spec.**
  First of the three-way split the user approved on DOOM-0308: density +
  the fields (§4.3/§4.3a-c). Parts 2 and 3 are light sources + the bakes
  (§4.4) and resolve + composite (§4.6/§4.6a).

  Why this part first: it forces DOOM-0308's item 1, the only decision
  still open on that list -- three incompatible sigma formulas, one of
  which declares itself authoritative and omits the floor-fog term. Every
  layer's density is built from one of them, and L4 (goo/hell profiles)
  adds a term to exactly these formulas, so L4 is blocked until this part
  lands.

  The parent spec is 3383 lines and produced ~30 verified findings on ONE
  cold-eyes loop, most unrelated to the change being gated. The failure
  mode DOOM-0308 identified: amendments supersede earlier text in place,
  so a top-down reader meets the abandoned contract first and its
  retraction a hundred lines later. All three sigma statements are that
  shape.

  Each part runs the rule-14 gate from loop 1 on its own bytes -- the
  parent's loops were run against a document that will no longer exist.
  **Layman:** The fog design document is being split into three smaller ones; this is the part about how thick the fog is and where it sits.
  Kind: doc.
  Lanes: docs, fog.
  Source: user-request-2026-08-03 (DOOM-0308's structural recommendation).
  Progress (2026-08-03): DOCUMENT EXTRACTED AND GATED — docs/specs/DOOM-0310-fog-density-fields.md (1240 lines, from the parent's 825-line §4.3/§4.3a-c). Rule-14 gate ran from loop 1 on its own bytes; the parent's 23 loops were NOT inherited. **Converged-by-cap at 3 loops, zero findings deferred** (3 lanes, then 2, then 2). Draft defects 27 -> 14 -> 7, CRITICALs 3 -> 2 -> 0; collateral 0 -> 10 -> 9, outnumbering draft defects at loop 3, which is the documented signal to stop.

  Item 1 of DOOM-0308 is CLOSED here: sigma is stated ONCE, reconciled against the shipped marchFog rather than by judgement, and the superseded statements are DELETED not annotated. It needed doing three times, which is itself the finding -- the extraction's own pointer block in the parent turned out to be carrying a FOURTH partial sigma, and loop 1's fix for the parent's §4.5 did not catch it.

  The split's premise now has no mechanical guard, recorded as a `nothing` row: if a further sigma statement appears, the answer is a /doc-lint check greping the split's prose for a sigma composition outside §4.1.

  FOLDED IN from DOOM-0308 (verified against pt_common.glsl at HEAD, not re-reviewed): the wisp constants (kWispAmp 0.6 -> 1.0, so the density bound is 0x..2x; kWispFreq1 1/512 -> 1/192) and the three figures derived from them; kSeepMax 0.5 -> 0.9 and kSeepFalloff 192 -> 384 (closing Q16 via the DOOM-0281 re-tune); and §4.3c's "16% at 512 units", which contradicted §4.3's own shipped table (61%).

  FOUR shipped things the parent never recorded are now documented: kWispSquashZ, the odd S-curve contrast shaper and its load-bearing clamp (the cubic folds beyond +/-1), DOOM-0300's per-level heading rotation, and kIndoorSkyLight -- the seep field's SECOND consumer.

  CORRECTED, and it changes what part 3 must build: the floor layer's headline 37% / tau 0.46 were computed against a baseZ the shipped code does not use outdoors. Both branches are now derived (roofed 37%, outdoors 16%) and the parent's live INV-10 was oversizing the sky-seam addend by ~2.7x. Sky pixels are open-sky by definition, so the outdoor pair is the size.

  L4 IS NOW BUILDABLE FROM THIS DOCUMENT. Pinned: the area term attaches OUTSIDE the skyExposure gate and INSIDE wisp and the dial (forced, not chosen -- gate it and a sealed goo room drops to kIndoorFogScale); the exact GLSL line, distributing `strength` onto the new term so an empty profile sum stays bit-identical; areaSigma defined; kAreaDensity named (0.0020, does not exist yet); the goo addend is primary-hit-keyed (a DECLARED relaxation of the no-hit-dependence rule, since DOOM-0011 §4.5 requires it) while hell is a per-level misc6.w constant; densities ADD but tints MULTIPLY for goo-on-hell; mediumTint's site named (both sky shares, before kSkyShaftStrength); a >= 0.1 ms budget with the instrument corrected (the per-pass profiler, NOT shaderstats, which reports occupancy); and five acceptance rows including the one nothing covered before -- an OPEN-SKY goo pool, where the ungated addend stacks on air already at full density and nothing clamps sigma.

  Q9 and Q21 closed rather than carried: Q9 is answered by the shipped composite (the sky is decoded to linear before the fold, so one kFogColor triple is right on both branches and INV-4 is correct); Q21 closes on BOTH axes via the composite period (24576 u / 9830 u against a 2048 u clamp) instead of resting on map geometry. Q7, Q24b and Q32 newly tabled -- Q32 is a genuine user look call (does the sky closed form take L4's addend and tint, or is a coloured skyline seam accepted?).

  Parts 2 (DOOM-0011 §4.4) and 3 (§4.6/§4.6a) still need ids. The parent keeps the shared invariants plus a citation map for its remaining references to the moved sections; INV-9/11/12 moved with their ids unchanged, and INV-10 carries a tombstone so the sequence stays legible.
  L4 PRE-FLIGHT (2026-08-03) — read before writing any L4 code. There is a fully reviewed L4 task in docs/specs/DOOM-0011-implementation-plan.md (search "## Task L4"), and THREE of its instructions are now stale. Following it verbatim produces a defect or a broken build:

  1. ITS SIGMA LINE PREDATES L1e. The plan writes `(skySigma + areaSigma) * pool * wisp(...) * strength` with no floor addend and `pool` as a separate factor. The shipped line has two addends and no separate pool. USE DOOM-0310 section 4.1's L4 line instead -- it distributes `strength` onto the new term so an empty profile sum stays bit-identical.

  2. IT TINTS THE TORCH TERM. The plan says "Multiply the sky term AND each torch term by mediumTint". SUPERSEDED by the user's 2026-08-03 decision: a torch shaft is NOT tinted by the medium. Sky term only (DOOM-0310 section 4.6 sites it: kFogColor * mediumTint across both sky shares, before kSkyShaftStrength).

  3. IT SAYS DELETE fogDensity(). "fogDensity() loses its last caller here -- delete it." FALSE against shipped code: the sigma line still calls fogDensity(p, baseZ, poolH). L3 shipped differently from what the plan anticipated. Do not delete it, and do not extract fogHeightPool() -- that extraction never happened and is not needed.

  STILL GOOD in that plan, and already cold-eyes-reviewed (loop 9 of the parent's campaign caught two CRITICALs in it, both folded in): Step 1 (rb_view_t.hazeDensity), Step 2 (the hell-detection rule in r_backend.c beside view.skytexnum), Step 3 (the misc6.w bit-cast), and the FogHit widening.

  THE TWO CRITICALS THAT PLAN ALREADY FIXED -- do not reintroduce either:
  - The goo test MUST read a NEW `FogHit.ctrlFlags` field carrying `MatCtrl.flags`. Testing `h.matFlags` for RB_FLAG_LIQUID_NUKAGE is testing an unrelated per-VERTEX bit: no goo room would ever render green and NOTHING WOULD FAIL TO COMPILE.
  - The misc6.w write must read `g.lastView.hazeDensity`. RecordRtTrace() takes no rb_view_t parameter, so a bare `view` does not compile.

  VERIFIED AT HEAD 2026-08-03 (line numbers are hints; the symbols are authoritative):
  - `struct FogHit { vec3 hitP; vec3 gnormal; uint matFlags; }` at pathtrace.comp:925 -- no ctrlFlags yet.
  - Both call sites are `FogHit fh = FogHit(hitP, n, uint(flags));` at pathtrace.comp:1550 and :1685.
  - GOTCHA THE PLAN GETS WRONG: it says `mc` is "already in scope at both; grep to confirm". IT IS NOT. `MatCtrl mc = ctrl[id];` does not appear at either site; `isNukage(mc)` is at :559 and `mc` is live at :1502 and :1725. The widening needs the MatCtrl fetched (or the flags threaded) at the two FogHit sites -- budget for that rather than assuming a one-token edit.
  - `pc.misc6[3] = 0u;  // hell-haze density, bit-cast float (wired at L4)` at r_vulkan.cpp:8641, with the ripple bit-cast pattern to mirror at :8630.
  - `rb_view_t` ends `float angle; float extralight; int skytexnum;` in r_mesh.h (~:395-402).
  - `view.skytexnum = skytexture;` at r_backend.c:181, `view.extralight` at :177.
  - LIQUID_NUKAGE = 8u at pathtrace.comp:548 (mirrors RB_FLAG_LIQUID_NUKAGE, rb_materials.h:17).
  - kAreaDensity does NOT exist in the tree; L4 declares it (start 0.0020).

  Selection contract is DOOM-0011 section 4.5: goo = primary-hit LIQUID_NUKAGE, areaMult 1.0, density kAreaDensity; hell = per-level flag (registered/retail AND gameepisode>=3) OR (commercial AND gamemap>=20) OR a fire sky, density runtime on misc6.w. Densities ADD, tints MULTIPLY. Do NOT write `gamemode != commercial` -- it admits shareware and indetermined.
  Resolved (2026-08-04): docs/specs/DOOM-0310-fog-density-fields.md exists, carries the eleven-section shape, and its own cold-eyes log records convergence-by-cap at 3 loops with zero findings deferred (draft defects 27 -> 14 -> 7, CRITICALs 3 -> 2 -> 0). The rule-14 gate ran from loop 1 on its own bytes. No CHANGELOG entry: the corpus has no precedent for a spec-split doc item appearing there, and this changes no shipped behaviour.

- 📋 [DOOM-0311] **Validate the render push-constants on the CPU instead of catching their NaNs in the shaders.**
  Three shader inputs are divided by without being checked at the boundary:
  ssao.frag divides by pc.aspect, taau.comp divides by the display
  dimensions, and svgf_composite.comp bit-casts pc.misc3.x to an exposure
  value. Each is recoverable today only because a downstream NaN guard
  happens to catch it (svgf_composite.comp:195 and taau.comp:109), which is
  recovery-by-clamping rather than prevention, and a new consumer of the
  same push-constant would not inherit the guard. Cheap fix: assert/clamp
  aspect, extent and exposure once where the push-constant block is filled.
  No known reachable trigger -- the host always supplies real values today --
  so this is hardening, not a live defect.
  **Layman:** The graphics code currently cleans up bad numbers after they have already reached the graphics card; better to reject them before sending.
  Kind: security.
  Source: code-quality-review-2026-08-03 (shaders-raster lane, MEDIUM).

- 📋 [DOOM-0312] **Keep the unbuilt DOS-era drivers out of the static-analysis sweeps.**
  ipx/, sersrc/ and sndserv/ are retained as historical reference per
  DOOM-0085 and are referenced by no build target, but every audit sweep
  still parses them and reports findings in them -- including a genuine
  sprintf overflow in sndserv/wadread.c:244 and unchecked read() calls,
  none of which ship. The findings are real about the code and irrelevant
  to the product, which is the worst combination for a review budget.
  Either add them to the audit scope exclusions or move them under a
  clearly-marked historical/ directory, and note the choice where the
  next sweep will see it.
  **Layman:** Three old folders that are kept only for history get scanned by the code-checking tools every time, producing warnings nobody will ever act on.
  Kind: chore.
  Source: code-quality-review-2026-08-03 (legacy-drivers lane).

- 📋 [DOOM-0313] **Give -devshot a camera, so the visual features can be verified where they actually happen.**
  -devshot (DOOM-0303) removed the keypress problem, but not the position
  one: -warp drops you at the map's spawn and Wayland blocks synthetic
  input, so a capture can only ever frame the start room. Five shipped
  promises could not be verified end to end in the 2026-08-03 feature
  review for exactly that reason -- floor fog pooling outdoors (DOOM-0272),
  a torch's glow being cut by drifting fog (DOOM-0300), nukage glowing
  evenly (DOOM-0302), fog re-flooding a room whose wall opens (DOOM-0281),
  and per-map wisp drift (DOOM-0300). Each is observable; none is
  reachable from a spawn point. Wanted: a dev-only way to place the camera
  before the capture arms -- `-devcam x y z angle`, or reusing the
  Developer menu's existing warp/pos rows from the command line. The door
  case additionally needs the throwaway EV_VerticalDoor hook in
  P_UpdateSpecials that DOOM-0281's own verification used. Cheap, and it
  converts five permanent cannot-verifies into a repeatable gate.
  **Layman:** Screenshots can only be taken from wherever the player starts, so the fog, torch and nukage effects cannot be photographed at the places they are meant to show up.
  Kind: test.
  Source: feature-review-2026-08-03.

- 📋 [DOOM-0314] **Four fog-shader comments still quote figures the constants beside them contradict.**
  Found by every lane of DOOM-0310's cold-eyes gate, and left alone because a
  docs review does not edit code. Each is a comment sitting next to the very
  constant that falsifies it, which is the worst place for one:

  - pt_common.glsl, the kFogBaseDensity block: "The wisps are a +/-60 % swing
    in density". kWispAmp ships at 1.0, so the swing is 0x..2x. The 60 %
    figure is the spec's old 0.6.
  - pt_common.glsl, the kFloorFogRange comment: "against the aerial layer's
    16% at 512 units". That predates the outdoor pool rising to 112; the
    shipped value is 61 %. DOOM-0310 section 4.2 derives it.
  - pathtrace.comp, the wisp header: velocity outside the frequency scale
    "would drift 512x too fast". kWispFreq1 is 1/192, so it is 192x.
  - pathtrace.comp, the sigma line: calls the floor layer "a THIRD addend"
    where the shipped expression has two (the third counted a future
    area-profile term L4 has not added). Harmless, but it reads as a miscount.

  None changes behaviour. They matter because the fog constants get tuned by
  reading these comments -- the 16 % one especially, since it is the figure a
  re-tune would compare against, and DOOM-0310 had to correct that same number
  in the spec after it had already propagated into a stale comparison.

  Cheapest shape: fix all four in one pass while the file is open for
  something else. Do NOT re-derive the numbers -- DOOM-0310 sections 4.2, 4.3
  and 4.6 carry them with the commands that produce them.

  Related: DOOM-0310 (the spec that found them), DOOM-0308 (the filed tail),
  DOOM-0300 (which set the wisp speed and freq).
  **Layman:** Some notes written next to the fog code describe old numbers, so the next person to read them gets the wrong idea.
  Kind: doc-fix.
  Lanes: shaders, docs.
  Source: cold-eyes-2026-08-03 (DOOM-0310's gate, surfaced by all five lanes).

- 📋 [DOOM-0315] **Make `-shotverify` frame-deterministic, or stop writing byte-identity acceptance rows.**
  Found while discharging DOOM-0310 §7's byte-identity row for L4. Captured
  E1M1's spawn view twice from ONE build, same args, `-noinput`: the PNGs
  differ (different md5, MAE 0.003, max channel delta 4, 0.83% of pixels).
  So the capture is not reproducible frame-to-frame, and any acceptance row
  phrased as "byte-identical to today" cannot be executed as written — the
  instrument's own noise is larger than the thing being asserted.

  `-shotverify` already pins the things anyone thought of: `rippleSec = 8.0`
  (r_vulkan.cpp, `rb_shotverify == 1`) and DOOM-0208's config pin (rt_view,
  render scale, exposure, detile/filth/wet, flashlight, rt_fog). What is NOT
  pinned is which frame gets captured, so the SVGF temporal accumulation and
  TAAU have run a timing-dependent number of frames when the shot is taken.
  That is the likely mechanism and it is a HYPOTHESIS, not a diagnosis — the
  capture-trigger path has not been read.

  Cheapest plausible fix: capture on a fixed frame INDEX rather than after a
  wall-clock delay, so the denoiser history is always the same depth. Then a
  same-build double capture is the regression test for this item itself.

  Why it matters beyond tidiness: DOOM-0202's `-shotcompare` gate leans on the
  same capture, and its `kGoldenMAE` of 3.0 is ~1000x this noise floor, so the
  gate is not currently at risk — but a tighter bar could not be adopted, and
  L4 had to substitute "within the same-build control" for a contract that said
  "byte-identical". Until this lands, a byte-identity claim in a spec should be
  written as an ALGEBRAIC one (the shipped association is preserved; `x + 0.0
  == x`) with a measured noise-floor bound as its evidence, which is what
  DOOM-0011's L4 note does.
  **Layman:** Two screenshots of the exact same scene from the exact same build don't come out identical, which makes a whole class of "nothing changed" test impossible to run.
  Kind: test.
  Source: in-session-2026-08-03 (DOOM-0011 L4 verification).

- 📋 [DOOM-0316] **Split part 2 of 3 — fog light sources and the bakes, extracted from the DOOM-0011 spec.**
  Second of the three-way split the user approved on DOOM-0308: §4.4 light
  sources and shafts, plus the bakes. Part 1 is DOOM-0310 (density + the
  fields, shipped); part 3 is resolve + composite (§4.6/§4.6a).

  SCOPE GREW on 2026-08-04 from a user request, and the request turns out
  to be exactly this part's subject: "dial up the fog around nukage and
  water a bit more as a default, just to make it that much moodier (horror
  ambience). That way we can show the nukage pools glowing. Also, wherever
  there is nukage spilled on the floor, please make that glow as well."

  MEASURED STATE, so the spec starts from the tree and not from recall
  (headless ladder, E1M1 -warpto 3274 -3353 200, the roofed nukage room,
  render_scale 100, Ultra RT):
  - rt_fog 1 (Low, the shipped default) is very close to rt_fog 0 in this
    room. The fog only reads at 2 and 3.
  - The fog over a goo pool is NEUTRAL GREY, not green, at every strength.
  - The pool casts NO light into the air above it.

  ROOT CAUSE of the grey, and it is structural rather than a dial. In
  marchFog (pathtrace.comp:1277) mediumTint multiplies the SKY term ALONE:
      Ls = kFogColor * mediumTint * kSkyShaftStrength
         * (kSkyAmbientFrac * skyLight + (1-kSkyAmbientFrac) * sunLit)
  and skyLight = mix(kIndoorSkyLight, 1.0, seepT). In a roofed room seepT
  is near 0, so the sky share is near kIndoorSkyLight and the goo tint is
  multiplying a nearly-zero number. DOOM-0292 then gates the ambient share
  on sky exposure as well, which is correct on its own terms and pushes the
  same term further down. The two changes are not in conflict about what is
  right; they jointly mean a coloured MEDIUM cannot show indoors, which is
  where goo rooms are.

  THE DESIGN QUESTION this part has to settle, therefore, is not "what is
  kGooTint" but WHERE a liquid's colour enters the model. The promising
  answer, and the one consistent with what is already decided: make liquid
  surfaces FOG LIGHT SOURCES carrying their own Le, so a pool lights the
  mist above it the way a torch does. That reuses the existing untinted
  torch addend (pathtrace.comp:1280) and the user's own 2026-08-03 ruling
  that an emitter keeps its own colour and reads THROUGH the medium rather
  than being repainted by it -- so the green would come from the goo, not
  from a tint applied to sky light that is not there.

  OPEN, for the spec to answer:
  - Do liquid flats enter the L3 fog-light bake as area emitters, and at
    what cost? The bake ranks by unoccluded contribution and is capped by
    kFogLightsPerCell; a large pool is not a point light and the ranking
    assumes one.
  - Spilled nukage on the floor is the DOOM-0181 stain/puddle layer, not a
    liquid flat, so it carries no LIQUID_NUKAGE MatCtrl bit. Does it get
    one, or its own weaker emitter class? DOOM-0302 already made
    emisWeight() own the per-texel liquid mask.
  - kAreaDensity (0.0020) and the shipped rt_fog default (1) are both first
    guesses. Whether "moodier by default" is a density raise, a default
    raise, or both is a look call -- take a ladder to it, do not pick.
  - Does water get the same treatment as nukage? The user named both, and
    water has no glow of its own.

  Dependencies: DOOM-0310 (part 1) shipped. This part gates DOOM-0011 L5/L6.
  Runs the rule-14 gate from loop 1 on its own bytes via /write-spec.
  **Layman:** The part of the fog design document about what lights the fog up — and making the glowing green sludge actually light the mist above it.
  Kind: doc.
  Lanes: docs, fog, shaders.
  Source: in-session-2026-08-04.
  USER DECISIONS 2026-08-04, closing two of this part's open questions
  before the spec is drafted:
  - WATER GETS FOG TREATMENT ONLY, NOT GLOW. So the area-density profile
    and the emissive-source work are separate mechanisms with separate
    membership: nukage (and lava) are fog LIGHT SOURCES and also carry an
    area density; water carries an area density and emits nothing. Do not
    fold them into one "liquid" profile -- the spec needs a per-liquid
    table with density and Le as independent columns, or water inherits a
    glow nobody asked for.
  - THE NEW DEFAULT FOG STRENGTH IS MEDIUM (rt_fog 2), chosen off the
    headless ladder: Low is close to Off in the roofed nukage room, Medium
    reads as atmosphere without obscuring the room, High is heavy for a
    permanent default. Qualified by the user: "unless we can claw back
    sufficient performance without affecting visuals" -- i.e. High is not
    rejected on looks, it is rejected on cost, so this default is worth
    revisiting if DOOM-0090 / DOOM-0091 free up budget. Record the reason
    with the constant so a later session does not re-litigate the look.
    Note the ladder was shot at render_scale 100; confirm Medium still
    reads at the 50 the game boots on before pinning it.
  CORRECTION 2026-08-04, before the spec was drafted: LIQUIDS ARE ALREADY
  FOG LIGHT SOURCES. The earlier framing on this bullet -- "make liquid
  surfaces fog light sources" -- proposed a mechanism that ships today, and
  building to it would have re-implemented a working path.
  The chain, verified in source rather than recalled:
  `ForceLiquidEmissive` (r_vulkan.cpp) forces kNukageLe {0.05,0.19,0.02}
  and kLavaLe {0.55,0.19,0.03} onto the NUKAGE1-3 / LAVA1-4 flats by name;
  its own comment states that a material with Le>0 enters the NEE emitter
  set via BuildStaticEmitterSet; and `BuildFogLightGrid` clusters
  `g.staticWgt`, which is that set. So a nukage pool is already a candidate
  fog light.
  Measured on E1M1 (Ultra, rt_fog 2, render_scale 50, -noinput -inspect):
    DOOM-0011 L3 fog lights -- 174 emitter tris -> 107 clustered lights
    (intensity 0 / 6104 med / 67782), 1085 air / 758 with a candidate /
    435 lit of 3525 cells, 7792 sight tests (5.0 ms, bake+upload).
  Note `L.lum` is an AREA-WEIGHTED accumulation, not per-material Le --
  `L.lum = max(L.r, L.gr, L.b)` at r_vulkan.cpp with only the centroid
  divided by area -- so reach = sqrt(lum / RB_FOG_LIGHT_CUTOFF) puts a
  median light at ~390 units, not the ~2 units a per-material reading of Le
  would give. Do not repeat that arithmetic error; it was made and caught
  in this session.
  SO THE REAL QUESTION FOR THE SPEC NARROWS to why a pool that IS a
  candidate produces no visible glow, and there are three live suspects,
  none yet discriminated:
    (a) DOOM-0302 re-tuned kNukageLe DOWN ~51x in linear green (0.35/1.30/
        0.15 -> 0.05/0.19/0.02) to fix a blown-out surface once emisWeight
        made the whole flat emit. That fix was right for the SURFACE and
        its effect on the pool's FOG contribution was never considered --
        reach and ranking both derive from the same Le. One constant is
        serving two consumers with opposite needs, which is the shape of
        the bug and is probably the answer.
    (b) kFogLightsPerCell = 2. A pool competes for two slots against wall
        lights ranked by lum*win^2/(d^2+kTorchSoftR2); a large dim pool can
        lose to a small bright lamp even where the pool is what the player
        is looking at.
    (c) kTorchShaftStrength = 0.047 scales the whole emitter side.
  Discriminate before designing: the bake already prints per-cell candidate
  counts, and an A/B raising kNukageLe alone answers (a) directly.
  Extraction seam, measured 2026-08-04 so it is not re-derived: this part's
  source is docs/specs/DOOM-0011-volumetric-lighting.md **lines 378-1189**
  (`### 4.4 Light sources & shafts` through the end of the DOOM-0296
  amendment, stopping before `### 4.5 Area profiles`, which is the
  umbrella's). That span carries §4.4(a) sky shafts, §4.4(b) torch shafts,
  the DOOM-0295 half-rate amendment, the DOOM-0289 sun-clearance amendment
  (the interval derivation, the storage contract, the build, the re-flood,
  the three bounded approximations) and the DOOM-0296 fog-light re-bake.
  Follow docs/specs/DOOM-0310-fog-density-fields.md for shape: same eleven
  sections, invariants RENUMBERED FROM 1 with a §2 mapping table back to the
  umbrella's ids, and a hand-written `0-split` provenance row opening the
  cold-eyes log (no reviewer dispatched, so no severity counts). Per §2 of
  part 1 this part owns umbrella INV-2, INV-3, INV-13, INV-14 and the seep
  field's `.b`/`.a` channels; part 1 owns `.r`/`.g` and its build, sampler
  state, world->UV transform, cell-size rule and void ring, which this part
  inherits unchanged.
  Format standard: the project has NO docs/standards/spec*.md, so
  ~/.claude/skills/_shared/spec-format.md governs (verified, not recalled).
  DISCRIMINATED 2026-08-04, headless A/B on kNukageLe alone. Suspect (a) is
  confirmed and, more usefully, is confirmed NOT fixable by a dial: the two
  consumers saturate in the wrong order.
  Ladder, E1M1 -warpto 3274 -3353 200, Ultra RT, rt_fog 2, render_scale 100,
  -noinput -inspect -freeze -devshot 90, one binary rebuilt per rung.
  Mean GREEN (0-255, sRGB frame) in two boxes; control = two runs of the same
  binary, which moved the pool box by 0.01 and the wall box by 0.25, so
  everything below is far above the noise floor:
    Le x    pool surface   %clipped   glow band above pool edge
    1x       133.01           0.0%       57.67   (shipped default)
    2x       173.96           0.0%       66.94
    5x       235.40           0.0%       86.86
    10x      243.87          94.7%      112.55
    20x      247.27          94.7%      148.23
  The pool surface CLIPS between 5x and 10x -- at 10x, 94.7% of the surface
  box is at G>=250, i.e. the flat white-green slab DOOM-0302 was tuned to
  remove. The fog glow does not read until ~20x. There is no value of
  kNukageLe that gives a visible fog glow and a textured surface, so the spec
  must SPLIT the constant (surface Le vs fog-emitter Le) or give liquid
  emitters their own fog-side gain. A single re-tune is not an option.
  The fog response is real and correctly localised, not a global lift.
  Isolating it against rt_fog 0 at the same Le:
    glow band just above the pool: fog adds 34.8 at 1x, 104.1 at 20x
    same wall, well above the pool: fog adds  5.3 at 1x,  10.6 at 20x
  so the added radiance falls off with height away from the pool, which is
  the signature of a working local fog light rather than an ambient raise.
  Suspect (b) kFogLightsPerCell is NOT the limiter, and the bake print says so
  directly: 174 emitter tris -> 107 clustered lights at EVERY rung, with cells
  carrying a candidate at 758 / 782 / 782 for 1x / 5x / 20x. The pool is
  already a candidate everywhere it should be; only its magnitude was short.
  Worth noting for the ranking discussion: max cluster intensity holds at
  67782 through 5x and only jumps to 244781 at 20x, so below ~10x the pool is
  not the brightest emitter in its own room.
  Suspect (c) kTorchShaftStrength (0.047) survives as the other half of the
  same story -- it scales the whole emitter side of marchFog, so it is the
  reason the required Le is ~20x rather than ~2x. Raising it globally would
  brighten torch shafts too, so the per-liquid fog gain is the narrower knob.
  Tree restored to the shipped constants and rebuilt; no source change landed.
  ROOT CAUSE CORRECTED 2026-08-04, by measurement, BEFORE the spec was
  drafted. The bullet's earlier root cause -- "in a roofed room seepT is near
  0, so the sky share is near kIndoorSkyLight and the goo tint is multiplying
  a nearly-zero number" -- is WRONG, and building to it would have chased a
  magnitude problem that does not exist. kIndoorSkyLight is 0.45
  (pt_common.glsl), not ~0, and kSkyAmbientFrac is 0.65, so the tinted sky
  share is ~0.29 of full, not a rounding error.
  THE REAL CAUSE IS THE KEYING, NOT THE MAGNITUDE. mediumTint and areaMult
  are selected from the PRIMARY HIT's material (`h.ctrlFlags & LIQUID_NUKAGE`,
  pathtrace.comp), so a ray that crosses a goo room and lands on a WALL is
  never tinted and never thickened, however strong the constants.
  Proof, and it is unambiguous: kGooTint was set to pure red (1,0,0) for one
  throwaway build and the frame diffed against the shipped build. Exactly
  4.04% of pixels moved (threshold 20, noise-floor max 18), and their bounding
  box is the pool's own visible surface -- the two nukage polygons. Walls,
  ceiling and all the air above the pool: unchanged. Capture retained as
  dev-shots/MASK-gootint.png.
  Corroborating hue measurement, fog-on minus fog-off at the shipped Le, mean
  delta over the glow band just above the pool edge: dRGB (31.7, 34.8, 40.2),
  G/R = 1.10 -- neutral, as kFogColor (0.55,0.56,0.56) is. At 20x kNukageLe
  the same band reads dRGB (61.9, 104.1, 59.7), G/R = 1.68. So the ONLY route
  by which liquid colour currently reaches the air is the untinted EMITTER
  addend carrying the pool's own Le -- never mediumTint.
  THIS FALSIFIES A DECLARED RELAXATION IN PART 1. DOOM-0310 §3 tables the
  goo profile's primary-hit keying as bounded because "every pixel of a
  profiled room keys the same way, so the error shows only at a doorway edge".
  Measured in the E1M1 roofed goo room it is 4.04% of the frame that keys as
  goo -- the error is the other 96%, not a doorway edge. Part 1's §3 table and
  its INV-9 carve-out both need amending; that is a cross-part finding this
  part must file rather than absorb.
  AND IT BLOCKS THE WATER HALF OF THE REQUEST OUTRIGHT. The user's already-
  recorded decision is that water gets fog density and no glow. Density is
  areaSigma, which is behind the same primary-hit gate, and water carries no
  emitter to route around it -- so "dial up the fog around water" is not
  reachable by any tuning of the shipped mechanism.
  USER DECISION 2026-08-04, taken on two captures rather than in the abstract
  (dev-shots/B-20x-fog2.png = pool-as-lamp, dev-shots/H-goo-unkeyed.png = the
  medium itself green, a deliberately over-applied throwaway probe): BOTH
  MECHANISMS, with membership per liquid.
  - A per-cell LIQUID-PROXIMITY FIELD decides "is this air near liquid",
    keyed on position rather than on the primary hit, and drives BOTH the
    density raise and the tint. This is what water rides; it is also what
    makes the tint survive a ray that lands on a wall. The seep field is the
    precedent to copy -- same grid, same cell, same build pass -- and the
    channel budget is the first thing the spec must settle, since RGBA16F is
    already fully spoken for (.r/.g part 1, .b/.a INV-13).
  - The EMITTER path keeps the glow and stays untinted, per the user's
    2026-08-03 ruling. It needs the surface/fog Le split the ladder above
    forces.
  - Water: density only, no glow, no tint of its own.
  So the spec owns a per-liquid table with density, tint and Le as
  INDEPENDENT columns, exactly as the earlier decision note required, plus
  the field that makes the first two reachable at all.

- 📋 [DOOM-0317] **Split part 3 of 3 — fog resolve and composite, extracted from the DOOM-0011 spec.**
  Third of the three-way split the user approved on DOOM-0308: §4.6 half-res,
  denoise and composite, plus §4.6a fogging the sky backdrop (aerial
  perspective). Part 1 is DOOM-0310 (density + fields, shipped); part 2 is
  light sources + bakes.

  Written after part 2, because the composite consumes whatever the light
  model produces and part 2 may add a source (liquid emitters are under
  consideration there). Extracting it first would gate it on a contract that
  is still moving.

  Carries DOOM-0011 Q32 (the sky backdrop's fog treatment), which is one of
  the two look calls L4 still owes.

  Runs the rule-14 gate from loop 1 on its own bytes via /write-spec.
  **Layman:** The last part of the fog design document: how the fog is blended into the finished picture, including the sky behind it.
  Kind: doc.
  Lanes: docs, fog, shaders.
  Source: in-session-2026-08-04.

- ✅ [DOOM-0318] **Open a menu from argv in the developer build, so menus can be captured headlessly.**
  DOOM-0294 gave the developer build `-inspect` / `-freeze` / `-devshot`, and
  those reach every WORLD view in all three tiers. They cannot reach a MENU:
  opening one needs a keypress, and Wayland will not let input be injected
  into the client, so no automated capture can photograph a menu at all.

  That single gap is what blocks TWO items, which is the whole argument for
  doing it rather than looking twice by hand:
  - DOOM-0050 needs the menu-over-status-bar region seen in Solid/Ultra with
    the user's magnifier off. Its own note says so: "Either a human looks
    with the lens off, or the developer build gains a way to open a menu
    from argv. The latter is the general fix and would also unblock
    DOOM-0205's on-screen check."
  - DOOM-0205's Render Effects submenu is implemented in `m_menu.c` (the
    submenu, its per-toggle rows, the draw routine and the handler are all
    present) but has never been confirmed on screen.

  Scope, deliberately small: `-devmenu <name>` sets `menuactive`,
  `currentMenu` and `itemOn` directly, exactly as `M_StartControlPanel`
  already does, from the same level-load call site DOOM-0294's
  `G_DevInspectFromArgv` uses. DOOM_DEV only. No new menu, no new state, no
  change to how any menu draws or responds -- if a menu is wrong on screen
  this makes it visible, it does not fix it.

  Verified before writing: `m_menu.c` already includes `m_argv.h`, and the
  `menu_t` definitions (`MainDef`, `OptionsDef`, `RendererDef`, `EffectsDef`,
  `VideoDef`, `SoundDef`, `DeveloperDef`) all precede the file's tail, so the
  lookup table needs no forward declarations. `M_DevMenu` is already taken by
  the submenu's own item callback, hence `M_DevMenuFromArgv`, mirroring
  `G_DevInspectFromArgv`'s name.

  NOT in scope: driving a menu (moving the cursor, toggling a row). One frame
  of a named menu is what both blocked items need; anything more is a second
  item if it is ever wanted.
  **Layman:** Let an automated screenshot open a settings screen, so menu bugs can be checked without a person clicking through to one.
  Kind: test.
  Lanes: renderer, tests.
  Source: in-session-2026-08-04.
  Resolved (2026-08-04): implemented and proven in the same session it was
  filed. `M_DevMenuFromArgv` (m_menu.c) sets `menuactive` / `currentMenu` /
  `itemOn` from a name lookup, declared in m_menu.h under DOOM_DEV and called
  from G_DoLoadLevel beside `G_DevInspectFromArgv` (g_game.c). Seven names:
  main, options, renderer, effects, video, sound, developer. Added
  `#include <strings.h>` for `strcasecmp` rather than leaning on a transitive
  include; `m_argv.c` is the precedent for using it.
  Verified: DEV build clean, RELEASE build clean (the whole point of the
  DOOM_DEV guards), `make test` 7/7. Three captures prove it end to end and
  they are the acceptance:
    dev-shots/M-effects-ultra.png  Render Effects, Ultra RT
    dev-shots/N-effects-solid.png  Render Effects, Solid
    dev-shots/O-video-ultra.png    the tall DOOM-0206 Video menu, Ultra RT
  each logging `-devmenu: opened '<name>'`. Closes the gap DOOM-0294 left and
  unblocked DOOM-0050 and DOOM-0205 in the same sitting, which was the whole
  argument for building it rather than looking twice by hand.
  Scope held: no menu was added, changed or re-laid-out. Driving a menu
  (moving the cursor, toggling a row) remains deliberately out of scope --
  one frame of a named menu is what the blocked items needed.

- 📋 [DOOM-0319] **Toxic barrels should cast their green glow onto the fog and the room, like the nukage does.**
  User, on the DOOM-0183 play-test: the nukage's cast light "must also apply
  to barrels".

  Barrels emit NOTHING today, and the reason is structural rather than a dial
  being low. Verified 2026-08-04 rather than recalled:
  - Sprite emitters are derived from the ARTWORK's brightness -- DOOM-0084's
    peak-gated derive over the sprite's own texels. There is no name list for
    sprites, unlike flats.
  - The barrel is `SPR_BAR1` (`info.c`), a mid-green prop with no bright
    region, so it does not clear the peak gate and never enters the emitter
    set. Being green is not being bright.
  - `ForceLiquidEmissive` (r_vulkan.cpp) already solves exactly this problem
    for FLATS: it overwrites the derived Le by flat name for NUKAGE1-3 and
    LAVA1-4, precisely because a derive keyed on artwork cannot know that
    sludge glows. A barrel is the same case wearing a sprite.

  So the fix shape is the sprite analogue of that function: a name-keyed
  forced Le on `BAR1`, entering the emitter set the same way, with no new
  light type.

  DEPENDS ON DOOM-0316, and should not be attempted before it. DOOM-0316
  measured that a liquid's single Le constant cannot serve both its own
  surface and its cast light -- the surface clips between 5x and 10x while the
  cast light needs ~20x -- and is splitting the constant in two. A barrel
  inherits that problem exactly: pick one Le and either the barrel is a
  blown-out white-green blob or it lights nothing. Build this on the split
  once it exists.

  Open, for whoever builds it:
  - Which barrel states? `S_BAR1`/`S_BAR2` are the idle pair; `S_BEXP*` is the
    explosion, which is already a bright sprite and may already emit. Do NOT
    force Le on the explosion frames without checking -- doubling an emitter
    that already derives one is the DOOM-0011 double-count class.
  - Does the glow follow the barrel when it is moved or destroyed? Barrels are
    `mobj`s, so they ride the DYNAMIC `[omniStart, emitCount)` slice, not the
    static set -- which also means they are excluded from the fog by INV-2 and
    would need that decided explicitly rather than assumed.
  - How bright, relative to a nukage pool? A room of barrels must not out-light
    the pool they were filled from.
  **Layman:** The green waste barrels should light up the mist and the floor around them, instead of being flat green props.
  Kind: feature.
  Lanes: renderer, shaders.
  Source: user-play-test-2026-08-04.
  SCOPE BROADENED 2026-08-04 by the user, and the item is now "everything
  green glows", not just barrels. Their words: the glow "needs to come from
  barrels, pools and the areas it spilled on the floor (the dirt grime layer
  you added for me but it should be only the green that glows)", plus "the
  bonus armour pickups that look like skulls with green glowing eyes needs to
  cast light as well".
  So four emitter sources, of which one already half-exists:
  1. POOLS -- the nukage flats. Already forced-emissive by name
     (ForceLiquidEmissive); the problem is magnitude, owned by DOOM-0316.
  2. SPILLED GOO on the floor -- DOOM-0181's stain/grime layer. NOTE this is
     NOT unbuilt: `kPuddleGlow` (0.35) and `kPuddleSheenScale` already exist
     in pathtrace.comp and add a faint additive glow on goo puddles via
     `gooWet`, in both the mode-4 and mode-6 blocks. So the mechanism is
     there and is simply too weak to read, which is the same story as the
     pools. What it does NOT do is enter the emitter set, so it lights
     nothing but itself.
     ⚠ THE CONSTRAINT THE USER STATED EXPLICITLY: only the GREEN stains glow.
     The grime layer also paints rust and dirt, and those must stay dark. The
     existing `gooWet` selector already distinguishes green goo from the rest
     (it is the same floor + goo-selector test `stainColour` uses), so the
     discrimination exists -- do not widen it to the whole filth layer.
  3. BARRELS -- the original subject of this bullet. Static in practice, which
     the user confirmed, so they can be treated as static emitters and the
     INV-2 conflict below dissolves.
  4. ARMOUR BONUS PICKUPS -- SETTLED 2026-08-04. The user photographed the
     pickup they meant and it is `SPR_BON2`, the armour bonus (confirmed in
     source: `p_inter.c:391` `case SPR_BON2:` awards `armorpoints++`). It
     reads as a skull because it is a domed helmet with two green lights at
     its base. `SPR_CEYE` (Evil Eye) and the skull keys are NOT the subject
     and must not be given a forced Le on this bullet's account -- that was
     the risk the old ⚠ warned about, and it is now closed.
     ⚠ CORRECTION, measured later the same day and now filed as DOOM-0323:
     `SPR_BON2` IS in `sprite_glows()` (r_mesh.c:1768) but it does NOT
     receive a faint Le -- all four of its animation frames derive Le
     exactly {0,0,0}, because every frame peaks below `kBrightLum` (0.5) and
     DOOM-0157's escape hatch is guarded on a non-zero bright-texel sum. The
     mechanism is wired up and dead. Read DOOM-0323 before doing anything
     here; it carries the per-frame measurements and the threshold data.
     The user has also since asked for the light to PULSE with the sprite's
     fade in/out, which rules out a ForceLiquidEmissive-style constant --
     see DOOM-0323's corollary. Magnitude still inherits DOOM-0316.
  INV-2 RESOLVED IN PRINCIPLE by the user, 2026-08-04: they accepted that
  barrels are static and said "we should be able to do something for them".
  That is the (b) route this bullet sketched -- keep non-moving props in the
  STATIC emitter set so they are legal fog light sources, rather than
  relaxing INV-2 to let dynamic lights scatter, which is the expensive option
  INV-2 exists to prevent. The armour bonuses are the same case: they do not
  move until picked up. The design owes an answer for what happens at the
  moment a barrel explodes or a pickup is taken -- the emitter must leave the
  static set, which is a rebuild trigger, and DOOM-0296's `RB_UPD_*` dirty
  machinery is the precedent to copy rather than a new mechanism.
  STILL DEPENDS ON DOOM-0316 for the same reason as before: every one of the
  four sources needs a Le that lights its surroundings without blowing out its
  own surface, and that split is DOOM-0316's.
  USER REQUEST 2026-08-04, on seeing the E3M1 surface capture: "That level
  looks like it is a pool of blood, so, if it is blood, then it needs to be
  treated as a liquid." Correct -- the floor there is BLOOD1-3, and the flats
  exist in both IWADs (verified by reading the WAD directory: BLOOD1, BLOOD2,
  BLOOD3 present in doom.wad and doom2.wad).
  So the per-liquid table gains a fourth row. Membership so far, with the
  columns deliberately independent as the earlier decision requires:
    nukage  density yes,  tint green,  glow yes
    lava    density yes,  tint --   ,  glow yes
    water   density yes,  tint --   ,  glow NO   (user, earlier)
    blood   density yes,  tint red? ,  glow ?    (NEW -- both undecided)
  Blood's glow is NOT implied by this request and must not be assumed: the
  user asked for it to be "treated as a liquid", which covers the wet/ripple
  surface treatment and the fog density; whether a blood pool GLOWS is a
  separate look call, and real blood does not. Ask before giving it an Le.
  ⚠ THIS BREAKS THE PROJECT'S HELL TEST FIXTURE, and that is the thing to
  handle before any code changes. Both DOOM-0011 and DOOM-0310 rely on E3M1
  being a "hell with zero goo contamination" fixture, and the stated reason is
  precisely that its liquid is BLOOD3, which FlagLiquidFlats' lut does not
  contain -- so the hell haze can be judged without a liquid profile stacking
  on it. The moment blood enters the lut that property is gone and every
  hell-vs-goo comparison in those two specs silently starts measuring the
  product of two tints.
  Options for whoever builds this, none free:
    (a) find a hell level with NO liquid at all and re-pin the fixture there;
    (b) keep a build-time switch that excludes blood, used only by the fixture;
    (c) accept the contamination and always compare hell against hell.
  (a) is cleanest if such a level exists -- check before assuming; much of
  Episode 3 is blood-floored. Whichever is chosen, the memory note and both
  specs' fixture lines need updating in the same change, or a later session
  will trust a fixture that no longer holds.

- ✅ [DOOM-0320] **Classic -devshot captures are left-aligned with a false black bar whenever Fill Screen is off.**
  Found while verifying DOOM-0147, and it is a defect in the MEASUREMENT
  HARNESS rather than in the game -- which is the worse of the two places for
  it, because it silently corrupts the evidence other items are judged on.

  MEASURED, E1M1 -warpto 3274 -3353 200, Classic, output 3840x2160:
    widescreen 0, fillstretch 0 -> content occupies columns 0..2879,
                                   left bar 0 px, right bar 960 px
    widescreen 0, fillstretch 1 -> content occupies columns 0..3839, no bars
    widescreen 1                -> content occupies columns 0..3839, no bars
  The 4:3 content is itself exactly right (2880x2160 = 1.333); it is the
  PLACEMENT that is wrong. A correct pillarbox would be 480 px on each side.

  CAUSE. `I_DevShotClassic` (i_video.c) sizes its buffer from
  `SDL_GetRendererOutputSize` (full output, 3840) and then calls
  `SDL_RenderReadPixels(renderer, NULL, ...)`, which reads the current
  VIEWPORT -- and `I_SetAspect` sets `SDL_RenderSetLogicalSize(SCREENWIDTH,
  SCREENHEIGHT*6/5)` whenever `fillstretch` is off, which makes the viewport
  narrower than the output. So 2880 columns of a 3840-wide buffer are filled
  and the remainder is never written, reading as a black bar on the right.
  With `fillstretch` on, logical scaling is disabled, viewport == output, and
  the same code path is correct -- which is exactly the A/B above.

  THE GAME IS NOT AFFECTED. `SDL_RenderSetLogicalSize` centres the scaled
  output in the window by SDL's own documented behaviour, so a player sees a
  correctly centred picture with even bars. Verified by the fillstretch A/B
  rather than assumed: the defect appears and disappears with the logical
  size, not with anything DOOM draws.

  The existing code comment is half-right and that is how this survived: it
  warns that `SDL_RenderGetViewport` returns LOGICAL units and that sizing the
  buffer from them "overruns it by the square of the scale factor", which is
  true. It then concludes the buffer must be sized from the output, which is
  the wrong half -- the right answer is the viewport in PIXELS,
  `SDL_RenderGetViewport` multiplied by `SDL_RenderGetScale`. That also stays
  correct when `fillstretch` is on, where viewport == output and scale == 1,
  so the fix is a no-op on the path that works today.

  Blast radius: every Classic `-devshot` taken since DOOM-0294 landed
  (2026-08-04) with Fill Screen off. Solid and Ultra are NOT affected -- they
  present through Vulkan and never touch `SDL_RenderSetLogicalSize`. No
  conclusion in this session rests on a Classic capture; the DOOM-0316
  measurements are all Ultra.
  **Layman:** Screenshots of the original 1993 view came out shoved to one side with a black stripe that is not really on the screen.
  Kind: fix.
  Lanes: renderer, tests.
  Source: in-session-2026-08-04.
  Resolved (2026-08-04, d22b9dc): `I_DevShotClassic` now sizes its buffer from `SDL_RenderGetViewport` multiplied by `SDL_RenderGetScale` -- the viewport in PIXELS -- instead of from `SDL_GetRendererOutputSize`.
  Verified across all three Classic modes on the same fixture (E1M1 -warpto 3274 -3353 200, 2160p output):
    4:3, logical scaling ON   2880x2160, aspect 1.333, bars L0 R0   (was 3840 wide with a false 960 px right bar)
    fill-stretch              3840x2160, aspect 1.778, bars L0 R0   (unchanged)
    widescreen                3840x2158, aspect 1.779, bars L0 R0   (unchanged)
  The two already-correct paths are byte-unaffected by construction, not merely by observation: with Fill Screen on, `I_SetAspect` passes (0,0) to disable logical scaling, so viewport == output and scale == 1 and the product reduces to the old expression.
  Release build clean, make test 7/7. Captures retained: dev-shots/S-classic-43-fixed.png, T-classic-fill-fixed.png, U-classic-ws-fixed.png; the pre-fix evidence is Q-classic-43.png.

- 📋 [DOOM-0321] **Volumetric fog blows out to pure white across long open sight lines, losing its tint entirely.**
  User play-test 2026-08-04, Ultra + ray tracing on, render_scale 50,
  rt_fog Med, on an open hell landscape: "Something is definitely causing
  the white blown out colour." Their captures show the lower half of the
  frame going to near-pure white below a hard horizontal boundary, with the
  red sky and mountains above rendering correctly.

  THE DIAGNOSIS TO TEST FIRST, because it reconciles two observations that
  look contradictory and would otherwise send the next session hunting a
  tint bug that does not exist. Measured earlier the same day on the E3M1
  SURFACE, mean RGB of the fogged frame: sky (153,80,80) R/G 1.93, horizon
  (137,78,67) R/G 1.75, ground (86,37,25) R/G 2.32 -- clearly red-tinted --
  and E3M6 gave R/G 1.99-2.91. So kHellTint IS reaching marchFog. Yet the
  user's open-expanse captures are white. Both are true, and the reconciling
  mechanism is SATURATION KILLING THE HUE.

  In-scatter accumulates along the ray. Per unit of path the hell fog's
  colour is kFogColor * kHellTint = (0.495, 0.196, 0.168) -- red-dominant.
  But accumulate that over a long enough path and the channels clip in
  order: red saturates first, then green, then blue, and once all three are
  pinned the result is WHITE regardless of how strongly it was tinted. A
  tint only survives while the brightest channel is below the ceiling. Short
  paths (a walled courtyard, the E3M1 blood room) stay red; a sight line
  across open ground runs far enough to saturate. That predicts exactly the
  observed pattern -- red near geometry, white across the open -- and the
  hard horizontal boundary is where the ground plane starts offering
  unbounded path length.

  How to test it, cheaply and without guessing: capture the same open view
  at rt_fog 1 / 2 / 3. If this is saturation, the white REGION grows with
  strength while its colour stays pinned at white, and at rt_fog 1 the same
  pixels should read red rather than pale. If instead the colour is wrong at
  every strength, the cause is elsewhere and this diagnosis is dropped.

  RELATED BUT NOT THE SAME as the user's other two fog notes from the same
  session ("it shouldn't be so bright under a red sky", "the fog should be a
  lot thicker"), which are dials on DOOM-0011. This one is a defect: no dial
  setting should produce a white sheet over the floor. Note the two pull in
  OPPOSITE directions -- raising density to make the fog thicker makes this
  saturation worse -- so this must be fixed BEFORE the density is re-tuned,
  or the tuning will be done against a broken ceiling.

  Candidate fixes, none chosen: clamp the accumulated in-scatter below the
  saturation point; apply the tint AFTER accumulation so hue survives
  clipping; or bound the effective path length the way kFogSkyDist already
  bounds the sky term's.
  **Layman:** Looking across a big open hell area, the mist goes blank white instead of staying red — and it hides the whole floor.
  Kind: fix.
  Lanes: shaders, fog.
  Source: user-play-test-2026-08-04.
  Progress (2026-08-04): REPRODUCED HEADLESSLY, so this no longer depends
  on the user's screenshots. Fixture `-warp 3 1 -warpto 192 -1400 90` on
  E3M1's opening courtyard, Ultra + rt_view 6 + rt_fog 2: everything below
  the horizon goes to near-white and the ASHWALL perimeter, the building
  and the trees all disappear into it, exactly as captured. The same
  fixture at rt_fog 0 renders the scene normally, so the blow-out is the
  fog term and nothing else.
  Note when re-measuring: fog exists ONLY in the RT view. Solid raster
  (renderer 2, rt_view 0) is byte-identical at rt_fog 0 and rt_fog 2 --
  verified, two captures compared -- so never A/B the fog dials in Solid
  and conclude anything.
  The white also swallows the DOOM-0322 vantage, which is why that defect
  had to be diagnosed with fog off.

- 📋 [DOOM-0322] **A tall wall renders black in Solid and Ultra where Classic draws its texture.**
  User play-test 2026-08-04, same open hell landscape, reported as "the
  geometry is being cut off at a certain height" and captured in all three
  tiers -- which is what makes it diagnosable, because the tier that differs
  is the one that is right.

  WHAT THE CAPTURES SHOW. In CLASSIC the band above the brick building is a
  dark grey rocky wall with visible texture, continuous up to the mountains.
  In SOLID and in ULTRA the same band is SOLID BLACK across the full width
  of the view, with the building, trees and ground below it drawn normally
  and the sky and mountains above it drawn normally. So the defect is
  confined to one horizontal span of wall, in both 3D tiers, and is absent
  from the software renderer.

  ⚠ "CUT OFF" IS THE SYMPTOM, NOT NECESSARILY THE CAUSE, and the distinction
  decides where to look. Black could be (a) geometry genuinely missing, so
  the ray/raster hits nothing and returns the clear colour, or (b) geometry
  present but receiving no light. These have completely different fixes and
  the captures alone cannot separate them. The engine already has the tool
  to tell them apart: the HITS debug view renders a ray MISS as black
  (rt_debug_views 1), so if the band is still black under HITS the geometry
  is absent, and if it is not, the geometry is there and unlit. That is the
  first thing to run, before any code is read -- it is the same test that
  proved DOOM-0180's ceiling seam was a hole in the mesh rather than a
  shading artefact.

  LIKELY RELATED TO DOOM-0180, which is the other confirmed hole in the
  shared Vulkan world mesh (a bright seam on ceilings, present in Ultra and
  Solid, absent in Classic, root cause still unknown and suspected to be a
  T-junction from the BSP carve in r_mesh.c). Same tier signature, same
  mesh, same "3D backends only" pattern. Check whether one cause explains
  both before fixing either separately -- a shared root cause is likely
  enough that fixing them independently risks two patches for one defect.

  Both 3D tiers share RB_BuildMesh, so a mesh fix lands in both at once;
  that shared path is also why the two tiers agreeing is evidence for the
  mesh rather than for either renderer.

  No capture of this exists in dev-shots yet -- the user's screenshots are
  the only record, and the vantage is an open hell landscape reached after
  E3M1's opening lift. Pin a -warpto fixture for it before investigating, so
  the before/after is repeatable.
  **Layman:** In the 3D views, the top part of a big wall goes solid black instead of showing its stone texture — the old view draws it correctly.
  Kind: fix.
  Lanes: renderer, shaders.
  Source: user-play-test-2026-08-04.
  EVIDENCE, and it is the ONLY record of this vantage -- no dev-shots capture
  of it exists, because the spot was reached by the user walking there and no
  -warpto fixture is pinned yet.
  15 user screenshots, /home/ants/Pictures/ClaudePaste/paste_20260804_17*.png
  (timestamps 170704 to 173828). The three-tier comparison is inside that set:
  Solid and Ultra show the black band; Classic shows the same band as a dark
  grey rocky wall with visible texture. Several frames include the Video menu
  open, which is how the tier and settings are confirmed rather than inferred
  -- Renderer Ultra / Solid / Classic, Ray Tracing On / Off, render_scale 50,
  rt_fog Med, TAAU, Fill Screen On.
  Same set is the evidence for DOOM-0321 (the white blow-out); the two defects
  were reported from the same session and the same landscape.
  FIRST STEP when picking this up, before reading any code: pin a -warpto
  fixture for the vantage so before/after is repeatable. The DOOM-0011 note
  carries the method -- parse the map's SECTORS lump for `ceilingpic ==
  F_SKY1`, take a vertex on a linedef whose sidedef faces one, warp there. The
  E3M1 surface spot already found that way is `-warp 3 1 -warpto -600 576 300`,
  but that is the blood room and is NOT this landscape; the user reached this
  one by playing on from there.
  ANSWERED 2026-08-04 by the HITS capture, and the answer is (a): the
  geometry is GENUINELY MISSING, not present-and-unlit. Do not re-run that
  test.

  FIXTURE PINNED (this bullet's first step, now done):
  `-warp 3 1 -warpto 192 -980 90` on E3M1. That is the map's opening
  courtyard (sector 31, floor SFLR6_1, sky ceiling at z=56, ASHWALL
  perimeter), looking north through the x=32..352 opening at the SP_HOT1
  building with its BIGDOOR7 door and SW1GARG plaques. Matches the user's
  framing; distance was fitted by making the ASHWALL band subtend the same
  pixel height as in their captures. Their earlier vantage was further out.

  WHAT THE THREE CAPTURES SHOW, same fixture, fog off, no menu:
  Classic draws the brick building rising ~200 units above the courtyard
  wall. Solid draws its base only, up to z=56, and pure SKY above. HITS
  (rt_debug_views 1, rt_view 1) is BLACK over that whole region = ray MISS
  = no geometry there at all.

  CORRECTION TO THIS BULLET'S OWN DESCRIPTION. "Solid black band" was
  wrong -- it came from reading the user's screenshots, and two of the
  frames read as Classic are actually Solid (the top-right counter is
  cur/avg FPS: Classic runs ~35 at 4K native, Solid ~360 at 50% scale).
  The region is not black, it is SKY. The user's own words -- "the geometry
  is being cut off at a certain height" -- were the accurate description
  all along, and the height is exactly the VIEWER's sector sky ceiling.

  ROOT CAUSE, verified in source: r_mesh.c:547-553. For a two-sided seg
  where both ceilings are sky and front > back, DOOM-0141 emits
  `emit_sky_wall(seg, back->ceilingheight, front->ceilingheight)` -- an
  opaque sky-textured quad filling the height gap, explicitly so "the
  tracer occludes geometry beyond instead of seeing through the gap"
  (r_mesh.c:155-158, :550-552). Segs are emitted per side, so the seg on
  the FAR sector's side (front = sec32 ceil 192, back = sec31 ceil 56)
  raises a sky quad from z=56 to z=192 standing between the camera and the
  building. Sectors 32/33/34 do this across the full width of the opening.
  Vanilla does the opposite: with both ceilings sky it skips the upper wall
  and does NOT raise ceilingclip, so far geometry above the near ceiling
  shows through. That is the classic sky hack, and the Classic capture
  proves it. DOOM-0141 needed the sky to occlude the sky HOLE; it
  over-applied that to the sky-hack GAP, which vanilla leaves transparent.

  RELATIONSHIP TO THE OTHER TWO, both checked as this bullet asks:
  NOT a shared cause with DOOM-0180 -- that is a bright seam on ceilings,
  suspected T-junction from the BSP carve, a different mechanism. Fix them
  separately.
  DOOM-0142 IS the same subsystem with OPPOSITE polarity: there the 3D mesh
  occludes too LITTLE (a wall Classic blocks with is missing), here it
  occludes too MUCH. emit_sky_wall is itself an over-correction of the
  DOOM-0142 class. Design the fix for both at once or they will fight.

  FIX NOT STARTED, and it needs a design decision rather than a one-liner:
  the sky-hack gap must occlude nothing while the sky HOLE that DOOM-0141
  closed must keep occluding. Deleting the emit_sky_wall call would
  regress DOOM-0141's floating geometry. Spec it before coding.

- 📋 [DOOM-0323] **The armour bonus's pulsing green eyes derive Le=0, so DOOM-0157's faint path never fires for the one sprite it was written for.**
  User 2026-08-04, having identified the pickup as SPR_BON2 from a
  photograph: "it's green eyes keep fading in and out, so I am hoping that
  when it fades in, that it lights up the surrounding area."

  THE PULSE IS REAL AND IS IN THE ARTWORK. info.c walks S_BON2 through
  frames 0,1,2,3,2,1 at 6 tics each -- a ping-pong over four lumps, 36 tics
  (~1.03 s) per cycle. Measured max-channel peak of each lump against
  PLAYPAL, decoded to linear the same way emissive_derive.h does:
    BON2A0 0.227   BON2B0 0.227   BON2C0 0.292   BON2D0 0.429
  An 89% swing dimmest to brightest. The user's description is exactly right.

  THE DEFECT. emissive_derive.h's kBrightLum is 0.5, and the bright-texel
  sum only accumulates texels with value() > kBrightLum. EVERY armour-bonus
  frame peaks below 0.5, so sr+sg+sb == 0 on all four. derive_material_le's
  allowFaint escape hatch is guarded by `!allowFaint || (sr+sg+sb) <= 0.0`,
  so it cannot fire when the sum is zero -- and the header comment at
  emissive_derive.h:79-81 names "the armour bonus's gleam" as the very case
  allowFaint exists for. DOOM-0157 wired the sprite up (SPR_BON2 is in
  sprite_glows(), r_mesh.c:1768, and reaches the derive via
  RB_SpriteLumpGlows) but the threshold below it makes the whole path dead
  for this sprite. Verified by reimplementing the engine's own formula over
  the WAD lumps: all four frames yield Le exactly {0,0,0}.
  Contrast SPR_CEYE (the Evil Eye), which clears the strict PEAK gate on its
  own with Le=(0.714,0.192,0.094) -- which is why that one glows and this one
  does not, and why the two were easy to confuse.

  FIX SHAPE, and the measured numbers that pick it. Lower the bright
  threshold FOR allowFaint TILES ONLY. It must not move globally: kBrightLum
  = 0.5 is what stops a uniformly tinted wall averaging up and flooding a
  room with colour (emissive_derive.h:34-35), which is the exact failure the
  peak gate was introduced to end.
  Modelled Le.g per frame A..D at candidate thresholds:
    thr 0.40 -> 0.000 0.000 0.000 0.143   only frame D lights: a BLINK
    thr 0.25 -> 0.000 0.000 0.097 0.337   frames C and D: a genuine RAMP
  0.25 is the one that delivers what the user asked for, because the
  animation then reads 0, 0, 0.097, 0.337, 0.097, 0 across the cycle -- up
  and back down -- rather than a single-frame flash. Colour comes out
  correctly green-dominant, (0.066, 0.337, 0.046).

  WHY THE PULSE NEEDS NO NEW MECHANISM. Le is derived per ATLAS LUMP
  (ComputeMaterialEmissive, r_vulkan.cpp:5291) and each animation frame is
  its own lump, so once the frames derive non-zero the modulation is
  automatic. ⚠ COROLLARY, and it rules out the obvious shortcut: do NOT fix
  this the way ForceLiquidEmissive fixes nukage, by overwriting Le with a
  name-keyed CONSTANT. A constant is identical across the four lumps and
  would flatten the pulse to a steady glow -- the one property the user
  actually asked for. Scale or re-derive; never replace.

  MAGNITUDE IS A SEPARATE QUESTION AND IT IS DOOM-0316'S. Le.g 0.337 makes
  the eyes self-illuminate; whether it also throws light far enough to read
  on the floor and in the fog is the same one-constant-cannot-serve-both
  split DOOM-0316 measured for liquids. So this bullet's THRESHOLD half is
  independent and shippable on its own; its CAST-LIGHT half inherits
  DOOM-0316. Splitting it that way avoids blocking a verified one-line-class
  defect behind a spec that has not been started.
  Ordering note: this shares DOOM-0319's shape (a green prop that is green
  without being bright), so whoever builds the barrel forced-Le should check
  whether one threshold change serves both before adding a second mechanism.
  **Layman:** The little armour helmets have green eyes that pulse brighter and dimmer, but they never light up the room around them — the code meant to make them glow can't actually trigger on them.
  Kind: fix.
  Lanes: renderer, shaders.
  Source: user-request-2026-08-04.

- 📋 [DOOM-0330] **A distant toxic pool ignores the fog, and never tints the air above it.**
  User feedback taking the DOOM-0011 look call (2026-08-05), given
  alongside sign-off on the fog DENSITY: "the green pool now seems to
  override the fog even at distance ... there should be fog mostly
  obscuring the pool except for the green glow in the fog itself."

  Diagnosed by measurement, not by reading. BOTH halves are broken, and
  the first one is the one the user photographed: the pool stays a flat,
  uniform green all the way to its far edge while the mountains and the
  wall behind it fade correctly into grey.

  1. Fog DOES reach the pool -- the fold is present and correct in both
     composite paths (`L = L * fog.a + fog.rgb` after the albedo
     re-multiply, svgf_composite.comp:186-193; pathtrace.comp:1605 for
     mode 4). What fails is that the effect is INVISIBLE and, worse,
     FLAT WITH DISTANCE. Measured on the E1M1 open-sky pool
     (-warpto 1831 -3254, 50% scale, fog off vs Low, same build and
     view): pool pixels change by mean 39.4/255 with fog on, wall pixels
     in the same rows change by 109.6 -- and across the pool's whole
     visible span the colour holds at RGB ~(115, 207, 87), varying by
     under 2 levels from near edge to far.

     **CAUSE ESTABLISHED 2026-08-05 (second session), by a build A/B, and
     it is the SAME defect as half 2 below.** `kGooTint` multiplies the
     in-scatter of every pixel whose PRIMARY HIT is nukage
     (`mediumTint = kGooTint`, pathtrace.comp:1125-1128), so a pool pixel's
     whole march -- including air nowhere near the goo -- returns GREEN
     `fog.rgb`. The pool does not fail to fade; it fades into green fog
     rather than grey, at a hue close enough to its own that the fade is
     invisible. That reconciles the contradiction recorded below exactly:
     in-scatter IS added while the pool APPEARS un-attenuated, because what
     replaces the surface is the same colour as the surface.

     Two one-token probe builds, same fixture, same config:

     - `mediumTint` forced to `vec3(1.0)` for goo -> the pool washes out
       with the rest of the scene and its far edge fades further than its
       near edge. Mid-pool mean RGB (132,173,120) -> (174,181,170); the
       green cast g-r collapses 41 -> 7 and the pool lands on the
       surrounding fog's own brightness (~180).
     - `areaMult` forced to 0.0 (the goo profile's extra density, the other
       half of the same `if`) -> (127,169,114), g-r 42. **No effect.** The
       density is not the mechanism; the tint is all of it.

     This also falsifies the third suspect the previous session named:
     `fetchFog`'s position-guided upsample (svgf_composite.comp:92-120) is
     untouched by both probes and the defect still vanishes, so the fetch
     is not implicated. Do not re-open it on the grazing-incidence argument.

     Fixture (reproduces the user's photo; the 08-05 one stood IN the pool
     and was too short to show a gradient): `-warpto 1400 -3300 0` on E1M1,
     the courtyard's west edge looking east across sector 0 -- near edge
     ~120 units, far edge ~730, wall behind ~1340.

     **NEW TRAP, and it cost this session two captures: `-shotverify` PINS
     `rb_fog = 1`** along with brightness / flashlight / every effect toggle
     (DOOM-0208's golden pin, r_vulkan.cpp:9466-9478). A fog A/B taken
     through `-shotverify` photographs the SAME frame twice -- measured mean
     abs diff 0.0001 between an `rt_fog 2` and an `rt_fog 0` config. Use
     `-devshot N` (pins nothing) for any look investigation; r_vulkan.cpp
     :9914-9918 says so in as many words.

     Superseded: the two candidates below were falsified against the source
     by the previous session, and are kept so they are not re-proposed:

     - *"The emission is too bright to veil, so the tonemap compresses
       the attenuation away."* FALSE. `kNukageLe` is
       `{0.05f, 0.19f, 0.02f}` (r_vulkan.cpp:5256). DOOM-0302 scaled
       these DOWN by ~51x, not up -- its comment says the old constants
       "measured 51x brighter ... and blew out to a flat white-green
       slab". 0.19 linear green sits well inside
       `pbrNeutralToneMapping`'s responsive range, so shoulder
       compression cannot be the mechanism.
     - *"The pool surface is simply not fogged."* FALSE. Fog moves pool
       pixels by 39.4/255, so the fold is reaching them.

     Both readings were sound; the inference drawn from them was not.
     Transmittance was never ~1 on the pool -- it falls exactly as it does
     on the rim beside it. What the composited image could not show is that
     the in-scatter replacing the surface is the SAME hue as the surface,
     which is why inferring transmittance from a composite is the step that
     made two wrong causes look plausible. The build A/B above measures the
     term directly and needs no fog-buffer debug view; the `fog.a` rt_view
     mode the previous session scheduled is NOT needed for this defect.

  2. The AIR above the pool is never tinted, and that is the defect. L4's
     area profile is keyed on `FogHit.ctrlFlags`, which carries the
     MatCtrl.flags of the PRIMARY HIT (pathtrace.comp:1603). So the goo
     profile engages only when the ray's own hit is liquid. Looking ACROSS
     a pool at a wall, ctrlFlags is the wall's, the goo term is zero, and
     the march returns neutral `kFogColor` -- grey haze over green water.
     The profile is a property of what you look AT rather than of the air
     the ray travels THROUGH.

  Liquids are already in the fog-light bake, so the plumbing exists:
  BuildStaticEmitterSet admits any material with Le > 0 including flats
  (r_vulkan.cpp:7355-7360), and BuildFogLightGrid clusters that same set.
  Measured on E1M1: 174 emitter tris -> 107 clustered lights. What is
  missing is that outdoors `skyExposure` is 1.0, so the neutral sky
  ambient dominates and swamps whatever green in-scatter the emitters do
  contribute.

  ONE fix closes both halves, because both halves ARE §4.5's declared
  relaxation -- "the room reads goo-foggy when you are looking at or across
  the goo" -- read back out in each direction. Keyed on the primary hit, a
  pool pixel tints its ENTIRE march green (half 1, air nowhere near goo)
  while a wall-across-the-pool pixel tints NONE of it (half 2, air directly
  over the goo). Make `areaMult` and `mediumTint` a function of the sample
  position `p` instead of the hit: a per-cell goo-ness grid baked at level
  load, tapped per march sample, exactly as `uSeepField` already answers
  "is there sky above this XY" for the same loop. DOOM is flat-mapped, so
  goo-ness is a pure function of XY and decidable before the first frame --
  the same substitution DOOM-0276 and DOOM-0289 made. Then `mediumTint =
  mix(vec3(1), kGooTint, goo(p.xy))` and `areaMult = goo(p.xy)`, and both
  halves fall out. Belongs with the DOOM-0011 spec's section 4.4
  lights+bakes split; needs a spec before it is built (§4.1's invariant is
  being restored, not bent, so the relaxation text goes too).

  Density itself is SIGNED OFF as-is; do not thin it while fixing this.
  The -shotcompare golden re-bless (DOOM-0202) is deliberately HELD until
  this lands: re-blessing now would bake the flat-green pool into the
  reference image the gate compares every future change against.
  **Layman:** A green toxic pool stays the same flat green however far away it is, instead of fading into the fog like everything else does -- and the haze above it should glow green, but stays plain grey.
  Kind: enhancement.
  Source: user-look-call-2026-08-05 (DOOM-0011 L4/L5 sign-off).
  Reproduction detail (2026-08-05), easy to get wrong: the user's
  screenshot was taken at their LIVE config, which is `rt_fog 2` (Med) with
  `flashlight 1` -- NOT the shipped `rt_fog 1` default the 2026-08-05
  measurements used. Low and Med differ by mean 18.4/255, so a session that
  reproduces at Low is looking at a slightly different image than the one
  that was reported. Check ~/.doomrc before concluding anything about
  "their" view; it is the user's own file and is rewritten every time they
  play.

  Where this sits in the in-progress set: this IS the outstanding look
  defect on DOOM-0183 (reflective/glowing liquid goo, still in progress),
  and the -shotcompare golden re-bless that DOOM-0202 is waiting on is held
  behind it. So DOOM-0330, DOOM-0183 and DOOM-0202 all close on one thread.

  Evidence pack (side-by-side fog off / Low / High at three E1M1 fixtures,
  plus the measured perf and correctness gates):
  https://claude.ai/code/artifact/8949a67e-e6a3-4b18-ad32-5e3feee49227
  The source captures were written to a session scratchpad and are gone;
  the page is the durable copy.
  USER LOOK CALL on the probe (2026-08-05), and it settles the target
  behaviour by observation rather than by argument.

  Shown the `PROBE_NO_GOO` capture -- the goo profile disabled outright,
  both tint and extra density -- the user reported: "There is still no glow
  from the goo but the fog is looking better now that it shows over the
  pool." So half 1 is confirmed fixed by removing the tint, and half 2 is
  confirmed still open, by the same eye that reported the defect. The two
  halves are now independently verified as separate symptoms of the one
  cause.

  The user had also read the shipped frame as "fog over every surface
  EXCEPT the pools", which is worth recording because the measurement says
  something more specific and both readings are right about what they see.
  The pool is fogged, and by the correct AMOUNT: with the profile off it
  lands within ~5 levels of the stone rim beside it at the same distance
  (pool far 172,182,169 vs rim far 177,180,172). What is pinned in the
  shipped build is the GREEN CHANNEL ALONE -- 182 at the near edge and 182
  at the far edge, unchanged over the pool's whole span -- because
  kGooTint's green is 0.85, so the green in-scatter added almost exactly
  replaces the green the surface loses. Red and blue do fade across the
  same span (109 -> 150 and 96 -> 144). The eye reads hue, so it reads "no
  fog", which is precisely what was reported.

  TARGET BEHAVIOUR, now agreed and matching the user's original words
  ("there should be fog mostly obscuring the pool except for the green glow
  in the fog itself"). Once goo-ness is a per-cell property of the AIR:
    - Looking across the pool at the far wall, the ray passes over goo, so
      THAT fog picks up the green cast -- the glow the user is still
      missing.
    - Looking at the pool itself from a distance, the ray spends most of
      its length in ordinary air and only its last stretch over goo, so the
      pool is mostly obscured by grey fog with a green cast near the
      surface -- rather than by a wall of green.

  FALLBACK, validated: if the per-cell grid proves too expensive, simply
  deleting the tint is already a strict improvement on the shipped build --
  the user said so looking at it. It loses the glow, which is a feature we
  never had, rather than regressing one we did.
  Progress (2026-08-05): HALF SHIPPED as 16b073a -- the tint is gone, the
  density stays, and the user signed the result off on hardware ("Yay, it
  is fixed") from the E1M1 start-room window. Mid-pool at distance
  150,182,144 -> 180,188,178, landing on the rim beside it (178,182,173).
  make test green, -rtverify PASS (0.2059%, furnace 0.000000). What remains
  is the GLOW, and the user's brief for it is below.

  USER DESIGN BRIEF for the remaining half (2026-08-05, verbatim intent):
    - The ask is that "the goo casts a light onto the surrounding area
      (which would include fog)" -- so this is CAST LIGHT, not a medium
      tint painted on. That is a different mechanism from what L4 shipped
      and it is why the L4 approach could never satisfy it.
    - MUTED, explicitly: "Many of the DOOM with ray tracing videos I have
      watched really accentuate the glow of the pools. I want a more muted
      view but I still want it to cast light." So the reference renders are
      an ANTI-target on intensity. Tune down, not up.
    - OUTDOORS the glow lands on "the fog and the pool walls" -- the
      surrounding geometry and the air, not the whole sky-lit courtyard.
    - INDOORS, "especially in darker rooms", it should behave as "normal
      bounce light which in turn lights a room a little bit" -- i.e. GI,
      not a special case. The indoor/outdoor difference is one of degree
      and should fall out of the existing sky-vs-seep weighting rather than
      being written in as a branch.
    - Explicit permission to stop: "if we can't get that right, then let's
      move on as it looks great as it currently is." The shipped half is an
      acceptable resting point. Do NOT ship a bad glow to close the item.

  WHAT THIS MEANS FOR THE DESIGN, and it re-points it: framing this as "put
  kGooTint back, per cell" is probably WRONG, or at least incomplete. A
  medium tint multiplies the SKY term (marchFog's Ls line), so it makes air
  greener wherever sky light already reaches -- which is brightest outdoors
  and nearly absent in the dark indoor rooms where the user wants the
  strongest effect. It has the gradient backwards.

  The machinery the brief actually asks for mostly EXISTS and is already
  diagnosed in this bullet: liquids are admitted to BuildStaticEmitterSet
  as emitters (any material with Le > 0, including flats, r_vulkan.cpp
  :7355-7360) and BuildFogLightGrid clusters that same set -- measured 174
  emitter tris -> 107 clustered lights on E1M1. torchInscatter already
  in-scatters those emitters into the fog per march sample, ungated and
  UNTINTED by design (user decision 2026-08-03: a shaft keeps its emitter's
  own Le). So a nukage pool should ALREADY be lighting the fog green.
  Verify why it does not read: this bullet's own earlier note says outdoors
  skyExposure is 1.0 so the neutral sky ambient swamps it, and
  kFogLightsPerCell is only 2 -- a pool clustered into 107 lights may be
  losing its slots to brighter torches. Both are measurable before any
  design work. Start there rather than at a new grid.

  Same question for the surfaces: DOOM-0083/DOOM-0183 gave liquids a
  forced-constant Le, so the pool walls and a dark room's GI should already
  receive some. Measure what they get before adding a mechanism.

  SEQUENCING NOTE: the user has separately approved bloom (DOOM-0331) and
  assumed it would deliver this glow. It will not, and that was explained
  -- bloom spreads light already on screen, and at distance the fogged pool
  has nothing bright left to spread. Bloom helps the CLOSE-range case (a
  bright pool bleeding a halo into the air above it) and is complementary,
  not a substitute. Do not let bloom's arrival be mistaken for closing
  this.
  Progress (2026-08-05, second half): the GLOW is implemented as df4a30b,
  awaiting the user's look call on intensity.

  BOTH suspects this bullet named are FALSIFIED, by a temporary dump of the
  clustered fog lights and the packed per-cell slots at the E1M1 pool
  (1831,-3254):
  - kFogLightsPerCell = 2 is NOT starving the pool. The nukage clusters hold
    BOTH slots of every cell over and around it, at vis 1.00. No torch is
    competing.
  - The clusters are NOT dim. 23 of them; the brightest carries intensity
    12239 against a MEDIAN light of 6104, with reach at the 512 cap.
  - skyExposure is not swamping it either: the term is simply small.

  CAUSE: a point light is the wrong stand-in for a POOL, and the error is
  one-directional. Air a few tens of units above a broad flat sheet is inside
  its near field, where the real falloff is ~1/d, not the 1/d^2 the clustered
  point light applies -- a torch's air sits at its peak (kTorchSoftR2 caps
  it), a pool's air is already 100-300 units from every centroid. And only 2
  of the 23 clusters are summed, when the real pool contributes over its
  whole solid angle.

  Confirmed by a build A/B on the whole torch term at the user's fixture
  (-warpto 1400 -3300 0, rt_fog 3, Ultra RT): 1x = no glow, 3x = a faint cast
  on the air just over the pool, 5x = a clear green rise into the fog, 10x =
  the accentuated look the user named as the ANTI-target. The mechanism was
  working the whole time; only magnitude was wrong.

  FIX: RB_FOG_LIGHT_GOO_GAIN (4.0), applied per emitter TRIANGLE on the way
  into the fog-light grid ONLY. The surface's own Le, NEE and the GI bake all
  keep the unscaled value. Scoped deliberately -- raising kTorchShaftStrength
  would have brightened every torch shaft in the game (it is calibrated
  against a median light), and raising kNukageLe would have blown out the
  pool surface, which DOOM-0302 already re-tuned. Liquid triangles are tagged
  in BuildStaticEmitterSet from the ids ForceLiquidEmissive already resolves
  by name, so there is no second name table to disagree.

  Verified scoped, not merely claimed: -rtverify direct-light rel-MSE is
  0.2059%, unchanged to the digit, and the brightest NON-goo cluster is
  identical at 67782. make test green, furnace 0.000000. Bake 4.5 -> 4.8 ms
  against the spec's 6 ms gate; no shader change, so no per-frame cost.

  Look, captured headlessly: outdoors the air over the pool takes a muted
  green cast that falls off with distance while the mountains and the side
  wall stay grey; indoors (-warpto 3274 -3353 200, the roofed nukage room) a
  soft glow hugs the pool edge and the far floor without blowing the room
  out, which is the "lights a room a little bit" half of the brief.

  STILL OPEN: (a) the user's look call on the gain -- 3.0 and 5.0 are the
  bracketing values and both were captured; (b) the SURFACES half of the
  brief (pool walls, dark-room GI) is untouched here and still owes the
  measurement this bullet asks for; (c) the -shotcompare golden (DOOM-0202)
  is deliberately NOT re-blessed yet.
  Look call (2026-08-05): the user kept RB_FOG_LIGHT_GOO_GAIN at 4.0, chosen
  from the headless captures rather than on hardware. 3.0 and 5.0 were the
  alternatives offered and both were captured. A hardware play-test is still
  owed before this half is called shipped.
  Instrument note, so the next session re-measures in minutes rather than
  rebuilding it: the falsification above came from a TEMPORARY env-gated dump
  at the tail of BuildFogLightGrid (r_vulkan.cpp), removed before commit.
  `RB_FOGPROBE="x,y,radius"` printed (a) every clustered light within the
  radius as pos/intensity/lum/reach, (b) the PACKED per-cell slots there --
  which light won each of the kFogLightsPerCell slots, with its baked vis --
  and (c) a count of nukage- vs lava-coloured clusters, identified by Le
  COLOUR RATIO, which works precisely because ForceLiquidEmissive makes a
  liquid's Le a forced constant. Rebuild that rather than reasoning about the
  grid from the source: the aggregate line the bake already prints cannot
  show slot competition, which was the whole question.
  SURFACES HALF MEASURED 2026-08-05 (third session), and the answer is
  DO NOT BUILD A MECHANISM -- it already works. This bullet asked for the
  measurement before any design, and it discharges STILL-OPEN item (b).

  Method: a temporary `RB_NOLIQUIDLE` env gate zeroing the Le that
  ForceLiquidEmissive writes (removed before commit, as RB_FOGPROBE was).
  That kills the surface Le, the NEE emitter and the GI bake in one move.
  Captured Ultra RT @50% with `-inspect -freeze` so the world holds still,
  and rt_fog 0 via a temp -config so ONLY surface light is in play (the
  df4a30b fog-grid gain cannot contribute). Each fixture: Le on, Le off,
  and a same-build control repeat.

  The control is what makes the small numbers readable: `-inspect -freeze`
  drives the noise floor to mean 0.01/255 with 0.0% of pixels moving. Take
  the freeze away and a walking zombie alone moves ~15% of the frame.
  Use this pair on every look A/B from now on.

  Roofed nukage room (-warpto 3274 -3353 200), delta/255 and mean RGB:
    goo surface (control)   63.46   (3.9,21.4,0.0) -> (62.1,137.4,16.2)
    pit ledge / pool wall    4.70   (41.9,73.5,8.8) -> (46.2,82.1,9.9)
    ceiling                  4.53   (15.2,12.9,7.3) -> (18.2,22.8,7.9)
    floor beside pool        3.25   (47.4,43.7,4.4) -> (50.4,49.6,5.2)
    far wall above pool      2.10 | left wall, further   0.79
  23.9% of the frame moves. The ceiling's green channel rises 12.9 -> 22.8,
  +77% -- a dark room taking ordinary green bounce off the pool, which is
  the indoor half of the brief stated almost verbatim.

  Open-sky pool (-warpto 1400 -3300 0):
    goo surface (control)   63.17
    pit bank, front rim     37.97   green 48.2 -> 116.8
    pit bank right edge      6.51   green 50.2 -> 62.1
    pit bank left edge       2.45 | courtyard floor ~3m back  3.02
    far wall behind pool     0.61   (nothing, correctly)
  So the outdoor "pool walls" ARE lit, strongly at contact and falling off
  with distance. The earlier worry that sky ambient swamps it is true only
  for surfaces far from the pool, where it SHOULD be.

  CONCLUSION: DOOM-0083/DOOM-0183's forced-constant Le already delivers
  both surface asks -- lit pool walls outdoors, ordinary bounce in a dark
  room -- through NEE and the GI bake, with no new mechanism and no new
  cost. Nothing to add here; adding one would double-count light the
  tracer is already carrying. Raising kNukageLe to make it read louder is
  still refused for the reason recorded above (it blows out the pool
  surface DOOM-0302 tuned).

  CAVEAT, stated rather than hidden: measured with fog OFF to isolate the
  surface term. At the user's shipping rt_fog 3 the fog attenuates the
  distant end of this on top of what is measured here; it does not remove
  the near-field wall lighting, which is where all the signal is.

  What remains on this bullet is unchanged: the user's HARDWARE look at
  the glow (gain 4.0, bracket 3.0/5.0), and the -shotcompare golden
  re-bless (DOOM-0202) which is still held behind it.

- 📋 [DOOM-0331] **Bloom on the HDR views, so emissive things read as bright rather than merely light-coloured.**
  Found reviewing GZDoom at the user's request. GZDoom ships
  bloomextract.fp / bloomcombine.fp / blur.fp as post-process passes;
  DOOM_Ants ships NONE of that -- grepped, and there is no bloom, no
  auto-exposure, no FXAA and no depth-of-field anywhere in the shaders,
  r_vulkan.cpp or the roadmap (DOOM-0278 motion blur is the only
  post-process item filed).

  Why this one first, of everything upstream has that we do not: we
  already did the expensive half. DOOM-0011/L2a put HDR float render
  targets and a PBR-Neutral tonemap in place, and DOOM-0084/0302/0183 fill
  the scene with genuine emitters -- lamps, muzzle flashes, glowing
  nukage, lava. Bloom is the pass that turns "this texel is 4.0 linear"
  into something the eye reads as a light source. Without it every
  emitter is capped by the tonemap at roughly paper-white and no brighter,
  which is exactly why a lamp and a white wall can end up looking alike.

  Applies to Solid and Ultra, both views (INV: nothing on Classic). Belongs
  after the tonemap, before the HUD.

  Cheapest-wins shape, matching GZDoom's: bright-pass extract at half or
  quarter res, a separable blur pyramid, one additive combine. Costs one
  small dial (threshold + intensity) and should sit under the Render
  Effects submenu (DOOM-0205) like every other toggle.

  Measure against the 60 fps floor before shipping; a quarter-res
  three-level pyramid is normally well under a millisecond, but Solid's
  smoothness is a protected property (CLAUDE.md).
  **Layman:** Make lamps, fireballs and glowing goo bleed a little light into the air around them, the way bright things do in a photo — the single cheapest thing that makes the lighting read as "real".
  Kind: feature.
  Source: upstream-review-2026-08-05 (gzdoom wadsrc/static/shaders/pp).

- 📋 [DOOM-0332] **1-D shadow maps for point lights in the rasterised view, exploiting DOOM's flat map.**
  Found reviewing GZDoom at the user's request; feeds DOOM-0170's raster
  "performance mode", where today the only real shadow caster is the
  flashlight (DOOM-0170 L2c) plus blob shadows (L2d).

  GZDoom's trick, from hw_shadowmap.cpp's design comment: because DOOM is
  flat-mapped, a point light's shadow is a 1-D problem, not a cube map.
  Each light gets ONE ROW of a single 1024x1024 R32F texture -- so 1024
  lights in one texture -- and the row is split into four 256-texel
  quadrants for +Y / +X / -Y / -X, the 2-D analogue of a cube face. The
  whole thing is generated by ONE full-screen fragment pass that ray-tests
  a GPU-uploaded AABB tree over the map's line segments; the lighting
  shader then picks its quadrant from the direction to the light and does
  a single texture fetch.

  Worth taking here specifically because it is the substitution this
  project already runs on: DOOM-0276 (the up-ray), DOOM-0289 (the sun ray)
  and the seep field all replaced 3-D per-sample rays with a 2-D lookup on
  exactly the same reasoning -- vanilla DOOM is flat-mapped, so the
  question is decidable from XY alone. This is that argument applied to
  shadows, and the BSP line data it needs is already parsed.

  Known limits, inherited: a 2-D occluder set cannot shadow over or under
  anything, so a light above a low wall still shadows as though the wall
  were infinite. Acceptable in the rasterised view, whose whole premise is
  cheap fakes that hold up (CLAUDE.md's tier table); Ultra's ray-traced
  view keeps real geometry shadows and must not regress.

  Scope note: this is a Solid/Ultra RASTER-view feature. Per the tier
  rules, do not gate it on Ultra.
  **Layman:** Let every lamp and fireball in the rasterised (non-ray-traced) view cast a real shadow, cheaply, by taking advantage of the fact that DOOM's world is really a 2-D floor plan.
  Kind: feature.
  Source: upstream-review-2026-08-05 (gzdoom src/common/rendering/hwrenderer/data/hw_shadowmap.cpp).

- 💭 [DOOM-0333] **Voxel models as the route to 3-D monsters, instead of hand-made or commissioned meshes.**
  Found reviewing GZDoom at the user's request. DOOM-0080 (every sprite ->
  3-D in Ultra) has been parked on one blocker: freely-licensed models for
  the DOOM roster are scarce. Voxels are a second supply route, and the
  engine side is smaller than it sounds.

  GZDoom does the whole job in two files, ~900 lines total:
    - voxels.cpp -- R_LoadKVX reads Build-engine .KVX (slab-compressed
      voxel columns), remaps the palette against the game's own, and reads
      the mip chain.
    - models_voxel.cpp -- MakeSlabPolys / AddFace walk the slabs and emit
      QUADS per exposed voxel face, with an FVoxelMap neighbour check that
      culls interior faces so only the skin is meshed.

  That second half is the part that matters here: it produces ordinary
  TRIANGLES, which is exactly what our BLAS wants, so a voxel actor would
  ride the existing sprite-BLAS path rather than needing a new one. It
  also side-steps the thing that makes DOOM-0080 hard aesthetically --
  voxel models keep the original sprite's palette and silhouette by
  construction, so they still read as DOOM, which a modern re-modelled
  imp would not (see [[rt-aesthetic-north-star]]).

  Filed as CONSIDERED, not planned, because the engine work is the easy
  half and two questions are unanswered and are NOT code questions:
    1. Does a freely-licensed voxel set covering the DOOM roster actually
       exist, and on terms compatible with GPL v2 redistribution? Well-known
       voxel packs exist for DOOM but their licences must be READ, not
       assumed -- docs/standards/assets.md governs.
    2. Per-frame cost: a voxel actor is thousands of triangles against a
       sprite's two, times every monster on screen, and it moves -- so it
       lands on the TLAS refit path every frame. Measure before committing.

  Settle question 1 first. If the answer is no, this closes and DOOM-0080
  stays parked on its original blocker.
  **Layman:** Turning DOOM's flat cardboard-cutout monsters into real 3-D objects has been blocked by there being no free 3-D models. Voxels — little Lego-brick models — are a route around that, and the code to convert them into something we can ray-trace is a known, small problem.
  Kind: research.
  Source: upstream-review-2026-08-05 (gzdoom src/common/models/voxels.cpp + models_voxel.cpp).
  USER DECISION 2026-08-05, and it re-homes this item to a tier: voxels
  are "not the look I am going for" for Ultra, but "if there is no license
  issue, we can use it for the Solid renderer along with the other visual
  improvements we will be making there."

  That is a better fit than the bullet originally proposed, and it follows
  from CLAUDE.md's own tier rule rather than being an exception to it.
  Solid ENHANCES DOOM's own art; Ultra SUBSTITUTES for it. A voxel model is
  built from the original sprite -- same palette, same silhouette -- so it
  is the sprite enhanced, not replaced. It belongs on the same shelf as the
  upscaled-with-PBR wall textures. Ultra keeps its own answer: DOOM-0042's
  HD art, and hand-made or commissioned meshes if DOOM-0080 ever gets them.

  Note this is an ART-tier split, NOT an effects gate, so it does not
  collide with the rule that a feature must never be gated on Ultra for
  being expensive -- the tier table's own axis is exactly which art the
  tier draws.

  Sequencing unchanged: the licence question is still the gate, and it is
  now the ONLY gate on starting, since the target tier is settled. Read the
  licence before any code (docs/standards/assets.md). If it fails, this
  closes and nothing is lost.

- 📋 [DOOM-0334] **Eye adaptation: the view adjusts when moving between dark and bright, for the tension it buys.**
  User approved 2026-08-05 on the upstream review: "eyes adjusting from
  dark to light and vice versa, sure, can add some tension to certain
  scenes." So this is filed for the TENSION, not for exposure correctness
  -- which decides the tuning when the two disagree.

  GZDoom ships the reference machinery as three post-process passes
  (exposureextract / exposureaverage / exposurecombine,
  wadsrc/static/shaders/pp). We have the harder half already: HDR float
  targets and a PBR-Neutral tonemap (DOOM-0011 L2a), plus rb_exposure fed
  from the Brightness slider through misc3.x (DOOM-0096).

  Shape: measure the frame's average luminance by successive downsample to
  1x1, smooth it over time with separate attack and decay rates, and drive
  the existing exposure term with it. Two rates, not one -- the dazzle on
  walking out and the blindness on walking in are different lengths in real
  eyes, and the asymmetry IS the effect.

  DECISIONS THIS OWES BEFORE IT IS BUILT, and they are the whole risk:
    - It must not fight the art direction. DOOM's own lighting is authored;
      an auto-exposure that flattens every room to mid-grey deletes the
      contrast DOOM-0292 and DOOM-0011 L2 exist to create. Clamp the range
      hard and default the effect subtle.
    - It must not fight the player's Brightness slider. rb_exposure is a
      user setting; adaptation should ride ON it as an offset, not replace
      it, or the slider stops doing what it says.
    - Off by default, or on? Decide with the user at look-call time, not in
      the spec.
    - Tier: raster and ray-traced views alike (it is a tonemap-stage
      effect, not an art feature), never Classic.

  Sequencing: this reads the same HDR frame bloom does and lands in the
  same part of the pipeline, so it wants to come AFTER DOOM-0331 and reuse
  its downsample chain rather than building a second one.
  **Layman:** Step out of a dark corridor into daylight and the view is dazzling for a second before settling — and stepping back in, you are briefly blind. It makes dark places feel more dangerous.
  Kind: feature.
  Source: user-decision-2026-08-05 (upstream review follow-up).

- 📋 [DOOM-0335] **-devshot captures escape .gitignore when the game is launched from the repo root.**
  Found while capturing the DOOM-0330 glow A/Bs (2026-08-05).

  .gitignore:68 ignores `linuxdoom-1.10/dev-shots/`, but rb_devshot_path
  creates `dev-shots/` relative to the PROCESS WORKING DIRECTORY, not to the
  binary. Both run-doom-ants.sh and every headless capture recipe in the
  memory notes launch from the REPO ROOT, so the directory that actually
  fills up is `<repo>/dev-shots/` -- which no rule covers. `git status` then
  reports it untracked forever, and a careless `git add -A` would commit
  multi-megabyte PNGs.

  Verified: `git check-ignore -v dev-shots/` returns nothing, and 7 captures
  sat there untracked at the end of the session.

  Fix is one line -- make the rule root-relative-anywhere (`dev-shots/`
  rather than `linuxdoom-1.10/dev-shots/`), which covers both launch
  directories. Kept as a bullet rather than done inline because it is
  unrelated to the DOOM-0330 commits it was found during, and CLAUDE.md
  rule 11 says stay in the lane of the request.
  **Layman:** Developer screenshots pile up as untracked files in the project folder instead of being ignored, because the ignore rule guesses the wrong folder.
  Kind: fix.
  Source: in-session-2026-08-05.

- 📋 [DOOM-0336] **Controller rumble, capability-detected, with the DualSense extras where the pad has them.**
  User request 2026-08-05. The engine already opens a pad through SDL2's
  GameController API (i_video.c, `gamepad`) and already classifies its
  family (`SDL_GameControllerGetType`, used by DOOM-0161's confirm-button
  label), so both halves of this have a foothold. There is currently NO
  haptics code anywhere -- grepped: not one rumble/haptic call in the tree.

  DETECT, don't assume. SDL 2.32.70 on this box exposes the capability
  queries directly, so no per-device database is needed and no pad is
  driven with an effect it does not have:
    - `SDL_GameControllerHasRumble` -> the two body motors
      (`SDL_GameControllerRumble`, low + high frequency, duration_ms)
    - `SDL_GameControllerHasRumbleTriggers` -> the trigger motors
      (`SDL_GameControllerRumbleTriggers`)
    - `SDL_GameControllerHasLED` -> the light bar (`SDL_GameControllerSetLED`)
  A pad answering false to all three keeps today's behaviour exactly.

  WHERE IT FIRES is the design work, and it is small: weapon discharge
  (scaled by weapon -- pistol tap vs rocket thump), taking damage, a nearby
  barrel/rocket explosion, and arguably the pain state. The natural seam is
  the same one the sound code already uses, so the trigger points come free
  rather than needing new plumbing. Wants a Rumble strength dial (off /
  low / normal) in the options menu alongside the other toggles, and it
  must default to something a player would not call intrusive -- DOOM fires
  a LOT.

  PS5 / DualSense specifically, and the honest split:
    - Body rumble and the light bar are plain SDL2 calls and will just
      work through the queries above. Tinting the bar with health is a
      cheap, obvious win.
    - ADAPTIVE TRIGGERS (the variable resistance) are NOT in SDL2's public
      API. The only route is `SDL_GameControllerSendEffect` with a raw
      DualSense HID output report, which depends on SDL's hidapi DualSense
      driver being the one bound (it is not, when the kernel's hid-playstation
      driver claims the pad first). So treat trigger resistance as a
      SEPARATE, unverified stretch goal behind a capability probe -- do not
      let it block the ordinary rumble, and MEASURE that it actually reaches
      the hardware before claiming it.

  Testing note: this cannot be verified headlessly or by screenshot. It
  needs a pad in hand, and the Windows half needs Charl (see the Windows
  tester note) since SDL's DualSense path differs per platform.
  **Layman:** The gamepad should shake when you fire, take a hit, or an explosion goes off — and on a PS5 pad, use its fancier motors and light bar too.
  Kind: feature.
  Source: user-request-2026-08-05.

- 📋 [DOOM-0337] **Fix the composite.frag comment that calls the tone operator identity below its knee.**
  `composite.frag`'s step-3 comment says the Khronos PBR-Neutral operator
  "is identity below its ~0.76 knee, so DOOM's palette and midtones are
  untouched". It is not identity: `pbrNeutralToneMapping` subtracts a flat
  0.04 for any input at or above 0.08 (with a soft toe below that), so
  0.50 linear leaves as 0.4600 and 0.76 as 0.7200. Verified by running the
  shipped formula's own constants (`formulas/pbr_neutral_tonemap.glsl`).

  Comment-only defect -- the operator itself is the verbatim Khronos
  reference and is correct; nothing renders wrong. It matters because the
  claim is load-bearing for reasoning about thresholds: DOOM-0331's bright
  pass had to re-derive the real curve after inheriting this sentence, and
  the same trap is waiting for the next post-process feature.

  Fix: correct the comment in `composite.frag`, and check whether
  `svgf_composite.comp` and `pathtrace.comp` carry the same wording.
  **Layman:** A code comment says the brightness curve leaves dark and mid tones alone. It doesn't quite — it darkens everything a touch. Harmless today, but the comment would mislead the next person who trusts it.
  Kind: doc-fix.
  Source: in-session-2026-08-07 (found while writing the DOOM-0331 bloom spec).

- 📋 [DOOM-0338] **Clamp rb_fog on use; a hand-edited rt_fog reads past its range.**
  `rb_fog` is persisted as `rt_fog` in `~/.doomrc` and pushed to the
  megakernel as `pc.misc6.z` with **no range clamp anywhere**. Verified:
  grepping `rb_fog` in `r_vulkan.cpp` finds no clamp, no `% 4`, no
  bounds test. The only guard is display-side, in `m_menu.c`
  (`fogNames[(rb_fog>=0 && rb_fog<=3) ? rb_fog : 0]`), which protects the
  menu label and not the value handed to the shader.

  Compare `rb_renderscale`, which IS clamped at both of its use sites
  (`rb_renderscale < 25 ? 25 : rb_renderscale > 100 ? 100 : ...`). That
  is the pattern `rb_fog` should follow.

  Not reachable through the menu (`M_ChangeFog` is `(rb_fog + 1) % 4`),
  so this is a hand-edited-config / corrupt-config path only -- hence
  `fix` rather than `security`. Worth closing because the fog strength
  indexes density tables on the shader side.

  Found while specifying DOOM-0331's own `bloom` dial, which needed a
  clamp precedent to cite and could not use this one.
  **Layman:** The fog setting in the config file isn't checked for a sensible value. Typing a silly number by hand could make the game read memory it shouldn't. Nobody would hit it by accident — the menu can only produce 0 to 3 — but it costs one line to close.
  Kind: fix.
  Source: in-session-2026-08-07 (cold-eyes loop 1 on the DOOM-0331 bloom spec).
