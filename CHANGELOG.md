# Changelog

All notable changes to DOOM_Ants are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Completed
`ROADMAP.md` items graduate into this file under the release they ship in.

## [Unreleased]

### Fixed

- **Fix three low-severity bounds nits: donut NULL-deref, sfx off-by-one, basedefault snprintf.** (DOOM-0220)
  Three small safety tidy-ups: a malformed WAD donut, a sound-index edge, and a long $HOME path can no longer misbehave.

- **Fix -warp E/M bounds so `-warp 3` on a non-commercial IWAD can't NULL-deref.** (DOOM-0219)
  Typing `doom -warp 3` on DOOM 1 crashed at startup; now it's handled gracefully.

- **Guard RB_BuildPSprites against a negative sprite lump index.** (DOOM-0218)
  A missing weapon-sprite frame could read out of bounds; now it's skipped, matching the world-sprite path.

- **Drain the GPU before RB_Vulkan_BuildLevel frees/recreates live buffers.** (DOOM-0217)
  Loading a new level could, in rare timing, free graphics memory the GPU was still using; now the GPU is drained first.

- **Clamp menu value-name indices (showMessages/fpsCorner) against a hand-edited config.** (DOOM-0216)
  Editing ~/.doomrc to an out-of-range value could crash the game when opening the menu; the value is now clamped.

### Security

- **Clamp netconsole (packet player field) to MAXPLAYERS in d_net GetPackets.** (DOOM-0215)
  A malformed network packet could write out of bounds using a bogus player number; now such packets are skipped.

- **Clamp netgame packet numtics against BACKUPTICS in i_net PacketGet.** (DOOM-0214)
  A malformed network packet could overflow an internal array before any validation; now oversized packets are dropped.

- **Bound mus2mid MUS->MIDI reads by the real lump length, not the header's own claim.** (DOOM-0213)
  A crafted in-WAD music track could make DOOM read up to ~64KB past the end of the lump; now reads are capped at the true lump size.

- **Harden W_AddFile/W_Reload: validate the WAD directory against the real file size.** (DOOM-0212)
  A hand-made/corrupt WAD could previously make DOOM read random memory while loading; now a bad directory is rejected cleanly.

## [0.5.0] - 2026-07-22

### Added

- **A cleaner, sharper in-game menu for the 3D modes, plus a consistent Classic main menu.** (DOOM-0206)
  In the Solid and Ultra 3D modes, every menu now uses a crisp modern font
  drawn at full display resolution over a dimmed backdrop, with a layout that
  no longer overlaps the bottom status bar. The menu cursor is the real DOOM
  skull and the main-menu title shows the real DOOM logo, both sharpened and
  brightened. In the original Classic (1997 software) mode, the main-menu
  items now all share one consistent size and font, instead of mixing large
  graphic words with a tiny "Game Select" row.

- **De-tiled, grimy surfaces in the Ultra ray-traced view (DOOM-0181, DOOM-0179)** (DOOM-0181)
  HD walls and floors no longer read as the same tile pasted over and over:
  each repeat is stochastically offset/mirrored and keyed to its world
  position (Inigo-Quilez 4-corner blend), breaking both within-surface and
  between-surface tiling. Toggle/quality via the `]` key (off / 2-tap / 4-tap).
  On top, a filth layer makes a monster-overrun base look filthy: distinct,
  hard-edged, multi-coloured dirt stains (sampling a real first-party CC0 dirt
  texture) with crevice pooling and green-goo puddles on floors — applied to
  every non-sprite world surface, never on sprites or on the flowing green
  liquid itself. Ultra RT view only; Classic and Solid are byte-identical.

### Changed

- **Solid (raster) 3D view now overlaps CPU frame-build with GPU rendering, roughly doubling its frame rate (DOOM-0074).**
  The per-frame CPU work (moving-sector re-height, point-light cull, sprite
  billboards) now runs ahead of the GPU fence in steady-state raster, so the
  CPU prepares the next frame while the graphics card finishes the current one
  instead of waiting for it. On the RX 6600 (E1M1) this took the Solid view
  from ~70 to ~161 fps. The ray-traced Ultra view is unchanged (it stays
  single-frame-in-flight; extending the overlap there is tracked separately).

### Fixed

- **Fixed a long-standing operator-precedence bug in the "donut" sector effect that could misbehave (or crash) on hand-crafted donut sectors (DOOM-0138).**

- **Ultimate Doom now shows the correct per-episode sky (SKY1-SKY4) instead of always the E1 sky (DOOM-0139).**
  The sky picker compared mission-pack values against the game-mode field;
  because they overlap numerically, retail (Ultimate Doom) was wrongly
  forced onto the Doom II map-range sky (always SKY1). Fixed to test the
  mission field, so E2/E3/E4 get SKY2/SKY3/SKY4 as intended.

- **Silenced the flood of "V_DrawPatch: bad patch (ignored)" warnings at startup and on 4K/widescreen HUD compositing (DOOM-0137, DOOM-0171).**
  The view-border bezel and widescreen status-bar fill legitimately draw
  UI art past the 320-wide low-res framebuffer; those patches were already
  ignored (the frame renders correctly), but each printed two warning lines
  — dozens at startup, hundreds at 4K. The diagnostic is now capped at the
  first few occurrences with a suppression note. Log-hygiene only.

## [0.4.0] - 2026-07-16

### Added

- **Activated switches/buttons emit a faint coloured glow (red buttons glow red).** (DOOM-0082)
  When you press a lit button or switch, it gives off a soft glow in its own colour — a red button casts a faint red light — instead of staying flat.

- **Armour pickups' green glowing eyes emit a faint green glow in the path tracer.** (DOOM-0157)
  The armour pickups have little green glowing eyes; those should cast a soft green glow when you are near them, instead of looking flat — they do not glow right now.

- **Game-select boot menu: choose DOOM 1 or DOOM 2, switch back without relaunching by hand.** (DOOM-0060)
  When both DOOM 1 and DOOM 2 are installed, a startup chooser lets you pick which to play, and you can switch games mid-session from the main menu without relaunching by hand. The app remembers the last game you played and defaults to it next launch. If only one game is installed it boots straight in.

- **Name the gamepad confirm button in the Quit prompt, per controller family.** (DOOM-0161)
  The "quit?" box now tells controller players which button to press to confirm, and shows the right name for their pad — "A" on an Xbox pad, "X" on a PlayStation pad.

- **Answer the Quit (and other yes/no) confirmation prompts with the gamepad.** (DOOM-0160)
  You can now confirm "Quit Game" with the controller instead of having to reach for the keyboard's Y key — the same button you use to pick the menu item confirms the prompt.

- **Announce a found secret with an on-screen message + a distinct sound (all renderers).** (DOOM-0158)
  Stepping into a secret area now shows "A secret is revealed!" centred on screen in bright yellow and plays a distinct chime, in all three renderers (Classic, Solid, Ultra) — previously secrets were only revealed in the end-of-level tally.

### Changed

- **Render tier drives the ray-tracing default so Solid is the fast raster original view.** (DOOM-0169)
  Picking the "Solid" graphics mode now gives the fast, classic-looking view instead of secretly running the heavy ray-traced renderer — so it's smooth. "Ultra" is the ray-traced one. You can still flip ray-tracing on/off within a mode with the ~ key.

- **3D renderer defensive-hardening bundle (indie-review deferred items).** (DOOM-0073)
  Defensive hardening of the 3D renderer against corrupt or pathological (non-stock) level data — cap-carve buffer-overflow guard, per-frame texture-index bounds, sprite-rotation int overflow fix at the far map edge, a mid-session overlay-resize guard, and checked results on the load-bearing Vulkan setup calls. No change to normal play.

- **3D renderer tiers are now gated on descriptor-indexing support at probe time (DOOM-0059)**
  On a GPU that lacks the Vulkan 1.2 descriptor-indexing features the bindless
  material path needs, the menu now stays on Classic instead of erroring out
  when a 3D mode is selected. The probe checks the same four features device
  creation requires, so an unsupported GPU is never offered Solid/Ultra.

### Fixed

- **Render the DOOM sky in the ray-traced view (no more see-through floating geometry).** (DOOM-0141)
  In the Ultra/Solid ray-traced view the sky was a hole, so distant buildings floated in mid-air and the sky showed as flat blue instead of the mountains you see in Classic. This makes the sky a solid backdrop again.

- **Alpha-test two-sided masked mid-walls (grates/fences) in the ray-traced view so you can see through them.** (DOOM-0163)
  In the ray-traced view, see-through grates and fences look like solid walls; make them see-through like they are in the classic and raster views.

- **Title-screen music sometimes silent on the very first launch, plays on the next.** (DOOM-0165)
  Sometimes when you first open the game there's no menu music, but if you quit and open it again the music plays fine. Track down why the first launch occasionally starts silent.

- **Fix the DOOM-0060 game-select Windows cross-build (windows.h `boolean` clash).** (DOOM-0166)
  The DOOM 1 / DOOM 2 chooser could not be compiled for Windows at all — this fixes that so it can be tested on Windows.

- **Draw the DOOM-0141 sky occluder in the raster view too, so distant geometry stops floating there.** (DOOM-0162)
  The raster 3D view (Solid tier / ~ toggle) now hides distant geometry behind the sky the same way the ray-traced view does, so far-off buildings no longer appear to float. The DOOM-0141 sky occluder mesh is drawn depth-tested in the raster pass via a new world-space sky-dome flag, and the DOOM-0143 below-horizon fog fade is mirrored into the raster sky shader.

- **Seam (black line + white sliver) where the RT sky cap meets a wall top.** (DOOM-0143)
  With the new ray-traced sky, a thin dark line and a small bright sliver can show right where a wall meets the sky.

## [0.3.0] - 2026-07-01

### Added

- **Restored the subtle pitch variation on repeated sound effects** (DOOM-0156)
  Repeated sounds (gunshots, etc.) again vary slightly in pitch like classic DOOM, instead of sounding identical every time. Built on demand so it costs little memory.

- **Switch weapons with the mouse wheel and the gamepad D-pad** (DOOM-0153)
  Scroll the mouse wheel up/down, or press the controller D-pad left/right, to cycle to the next/previous weapon you own. Works like tapping the number keys (so it's safe in demos and multiplayer).

- **Widescreen and Fill Screen display toggles in the Renderer menu (DOOM-0147 Part C)** (DOOM-0147)
  Two persisted options (Options -> Renderer): "Widescreen" turns the authentic Hor+ widescreen on or off (Off forces classic 4:3 even on a wide monitor; takes effect on restart), and "Fill Screen" stretches the picture to fill the whole monitor, removing the black bars on displays whose shape isn't 4:3 (applies instantly). Both are saved to ~/.doomrc. Defaults reproduce the prior behaviour exactly.

- **Authentic widescreen (Hor+) for the Classic software renderer (DOOM-0147 Part B)** (DOOM-0147)
  On a display wider than 4:3 the Classic renderer now shows MORE of the level
  to the left and right instead of stretching the 4:3 picture. The view width is
  chosen at startup from the real display aspect; the projection keeps DOOM's
  vanilla vertical proportions and per-column angle, so it still feels like the
  original. UI art and the status bar stay centred. Displays at 4:3 or narrower
  (e.g. 5:4 1280x1024) render authentic 4:3 unchanged — a provable no-op there.
  Internally SCREENWIDTH became a runtime value (view-width arrays now sized at
  the MAXWIDTH cap). Builds clean on Linux and Windows; awaiting visual
  confirmation on a 16:9 display. Part C (a 4:3<->Widescreen menu toggle) follows.

### Changed

- **Sound effects are much louder / the SFX slider now spans its full range** (DOOM-0047)
  Effects were far too quiet relative to music (the old mixer capped them at ~12%). The SFX volume slider now scales effects from silent to full, and centred sounds (including all menu blips) play at full volume, so effects balance the music properly. Adjust the two sliders to taste.

- **Mouse now turns only — it no longer moves the player forward/back** (DOOM-0154)
  Moving the mouse up/down no longer walks the player; the mouse only turns left/right. Use the arrow keys or WASD to move.

- **Gamepad remap: flashlight on D-pad Up, L1 is Run again** (DOOM-0153)
  The controller D-pad is now the weapon/flashlight pad in-game (left/right change weapons, up toggles the flashlight), and L1 goes back to being a Run button. The D-pad still navigates menus.

- **Widescreen now auto-detects the display and defaults off on non-widescreen screens** (DOOM-0147)
  On a 4:3 or 5:4 monitor (where widescreen has no effect), the Widescreen option now correctly reads Off instead of a misleading On. Displays wider than 4:3 keep the saved preference (on by default).

- **Widescreen intermission (level-end) screens fill the sides instead of showing stale pixels** (DOOM-0151)
  The "level finished" and "entering" map screens now extend their background to the edges on a widescreen display, matching the title and HUD. No change at 4:3.

- **Widescreen HUD sides now continue the grey status bar instead of black bars** (DOOM-0151)
  The strips either side of the centred status bar are filled by extending the bar's own edge, matching the title-screen treatment. No change at 4:3.

- **Widescreen title/menu screens fill the sides instead of showing black bars** (DOOM-0151)
  On a display wider than 4:3, the title, credits, help and menu-background screens now extend their own edges outward to fill the black side strips, so they look intentional rather than pillar-boxed. Uses only the game's own on-screen pixels (no added art). No change at 4:3. Intermission and finale screens still to follow.

- **The menu now shows the plain DOOM title behind it, not the attract-loop credits**
  Opening the menu while the credits/help screen was showing put red menu text over red credit text and was hard to read. The menu now always draws the plain TITLEPIC behind it and pauses the title/demo cycle until you close the menu.

- **Rebalanced audio so sound effects are no longer drowned out by music: the music volume ceiling is lowered (quieter at every slider position) and new installs default to full sound-effects volume.** (DOOM-0047)

### Fixed

- **Sound effects on Windows: play them through SDL2_mixer like the music** (DOOM-0047)
  Rewrote the effects engine to play each sound as an SDL2_mixer chunk on the same audio device as the music (the approach Chocolate Doom uses), replacing a hand-rolled mixer that was near-silent on Windows. Effects loudness is calibrated to match the previous Linux behaviour. No change expected on Linux.

- **Sound effects silent on Windows (take 2): mix effects into the music device** (DOOM-0047)
  The game opened two separate audio devices — one for effects, one for music — and on Windows the effects device barely produced output. Effects now play through the same (working) device as the music, so they're audible on Windows. Music and Linux behaviour are unchanged.

- **Sound effects nearly silent on Windows** (DOOM-0047)
  The effects mixer ran at 11025 Hz, which SDL's Windows (WASAPI) audio backend resamples badly to near-silence. The mixer now outputs at 44100 Hz (the common native rate, matching the music), so effects play at full volume on Windows. Pitch and Linux behaviour are unchanged.

- **Music played at full volume until the volume slider was touched (and drowned the SFX on Windows)** (DOOM-0047)
  The saved music volume wasn't re-applied when a track actually started, so SDL2_mixer's MIDI backend (on Windows) played it at maximum until you nudged the slider -- which also made the sound effects seem far too quiet. The volume is now applied every time music starts.

- **Grey squares covered parts of the HUD in widescreen** (DOOM-0147)
  Status-bar numbers/icons were drawn shifted for widescreen but their background was cleared at the old position, leaving grey rectangles over adjacent readouts (ammo, arms). The clear now follows the shifted position.

- **Floor and ceiling textures swam relative to walls in widescreen (Classic renderer)** (DOOM-0147)
  On a widescreen display the flat (floor/ceiling) texture scale didn't match the widened field of view, so floors appeared to slide as you moved. Now locked to the same 4:3 reference the rest of the view uses. No change at 4:3.

- **Crash ("Bad V_CopyRect") when starting a level in widescreen with the HUD on** (DOOM-0147)
  The status-bar draw copied to a shifted position on a widescreen screen, which tripped an over-strict internal bounds check and aborted the game the moment a level loaded. The check now measures against the real (wider) screen, so widescreen play works with the HUD visible. No effect at 4:3.

- **The Screen Size slider can no longer be raised high enough to hide the in-game HUD; the status bar now always stays on during play.** (DOOM-0148)

- **Classic renderer now presents at authentic 4:3 instead of a stretched 16:10, so the picture has correct proportions and fills a 4:3 monitor (title and menu screens too).** (DOOM-0147)

## [0.2.0] - 2026-06-30

### Added

- **Player flashlight (headlamp), toggled by F or gamepad L1 (DOOM-0044)**
  A camera-mounted spotlight aimed along the view, toggled with F or gamepad
  L1 and persisted in the config. In Ultra (path-traced) it casts real
  ray-traced shadows; in Solid (raster 3D) it lights a cone with distance
  falloff. Same session: the path tracer's dynamic lights gained a near-field
  softening so they no longer blow out at point-blank range, the SVGF temporal
  denoiser's anti-ghosting was fixed so a light switching off no longer
  lingers, and the muzzle flash was brightened to light the surrounding room.

- **Ultra ambient floor: path-traced rooms marked bright by the level designer no longer go pitch black (DOOM-0043)**
  The path tracer now adds a gentle ambient term scaled by each surface's DOOM
  sector lightlevel, so a sector the mapper lit brightly reads as lit even when it
  holds no emissive lamp texture, while dark sectors stay dark. Applied as a floor
  under the baked GI (max, not add) so already-lit rooms are unchanged. Ultra-only
  (Solid keeps its pitch-black rooms — the flashlight is its answer). Brightness is
  an inline tunable pending playtest.

- **User-adjustable brightness slider for the path-traced (Ultra/denoiser) view.** (DOOM-0096)
  The ray-traced view looked a little dark. Add a Brightness slider to the Renderer settings menu so you can dial it to taste; it now defaults a touch brighter.

- **Temporal upscaler (TAAU) for the Ultra path tracer — DOOM-0009 build step 6-d phase 1** (DOOM-0009)
  The denoised path-tracer view can now render below display resolution and reconstruct a full-resolution image by accumulating sub-pixel-jittered frames (a custom temporal anti-aliasing upsampler). Adds the jitter, motion-vector, and menu plumbing the later FSR 2 / FSR 3.1 backends will share. New Options -> Renderer sub-menu: Renderer, Upscaler (Off / TAAU), Render Scale (100/75/67/50%); defaults to Off, so the existing image is unchanged until enabled.

- **Add a 3D wireframe debug view, toggled by the gamepad Share button.** (DOOM-0077)
  Draws the world and sprites as wireframe over a filled sky in Solid/Ultra to show what the renderer builds; no effect in Classic. Start alone now opens the menu.

- **Reflect runtime wall/flat texture changes in the 3D mesh (switches, animated textures).** (DOOM-0066)
  In the 3D renderers a pressed switch doesn't light up and animated surfaces (screens, slime) don't animate, because the 3D world bakes each surface's picture in once.

- **Map the controller Triangle (Y) button to toggle the automap.** (DOOM-0063)
  Press Triangle on the PS4 pad to open the map; press it again to close it.

- **Let the controller Circle (B) button close an open menu.** (DOOM-0056)
  Circle on the controller now backs out / closes the menu, like Start does.

- **Convert the renderer to true 3D.** (DOOM-0008)
  Replace DOOM's fake-3D trick with a real 3D engine.

- **Optional on-screen FPS counter with selectable corner placement (DOOM-0046)**
  An Options "FPS:" item cycles Off / Top-Left / Top-Centre / Top-Right,
  persisted in the config. Drawn with the small HUD font into the 320x200
  screen buffer, so it appears under every renderer (Classic, Solid, Ultra).
  Measured as a half-second rolling average via a new wall-clock millisecond
  timer.

- **Add game controller (gamepad) support.** (DOOM-0038)
  Plug in a controller and play: left stick moves and strafes, right stick turns, the triggers and face buttons fire, open doors and run, and Start opens the menu. Reuses DOOM's original joystick handling so the menus and controls just work; controllers can be plugged in and out while playing.

- **Add a Windows build target.** (DOOM-0006)
  Produce a version that runs on Windows so friends can play it.

- **Keep the classic 2.5D renderer selectable alongside the 3D renderer.** (DOOM-0026)
  When the new 3D renderer arrives, you'll still be able to switch back to the original DOOM look from the main menu - both renderers ship in the same build.

- **Render the classic view at 640x400 (hi-res).** (DOOM-0027)
  The picture is now drawn at double the internal detail (640x400 instead of 320x200), so walls, floors and monsters look crisp instead of blocky when the window is enlarged - the classic DOOM look is unchanged, just sharper. Built from a design spec hardened through 6 cold-eyes review loops; the 320x200 UI art (status bar, menus, HUD, intermission, finale) is integer-doubled to match. (Code-complete and building clean; pending an in-game play-test.)

### Changed

- **Resample omnidirectional sprite lights with RIS so the path tracer casts one shadow ray instead of one per light.** (DOOM-0120)
  The Ultra path tracer's sprite-light loop now uses Resampled Importance Sampling (the cheap end of the ReSTIR family): it weighs every surviving candidate light, keeps one by weighted-reservoir, and casts a single shadow ray on the survivor — instead of a shadow ray per light. Frame time is now effectively independent of how many glowing props are on screen (measured ~flat from 22 to 40 sprites), and the worst-case is faster than before. No cross-frame reservoir is kept, so it stays light on AMD RDNA2 registers; the temporal upscaler and denoiser absorb the single ray's noise. The estimator is provably unbiased against the previous per-light sum, and the old dim-sprite cull (with its small one-sided bias) is removed.

- **REJECT-lump light culling for NEE (cheap-ladder step 1).** (DOOM-0119)
  Ultra path tracer skips glowing-sprite lights in rooms a surface
  can't see (via the WAD REJECT visibility lump), before casting their
  shadow rays. At 50% render scale in a light-heavy multi-room scene the
  megakernel now dips to ~9 ms (51 fps spikes) where cross-room lights
  are culled, up from the ~37-46 fps DOOM-0090 baseline.

- **Smooth camera between game tics in the 3D renderers — render rate is no longer capped at the 35 Hz game tick** (DOOM-0048)
  DOOM's world updates 35 times a second; the 3D view now interpolates the camera between those updates so walking and turning look smooth at any frame rate above 35 FPS, instead of stepping at 35. The simulation itself is untouched (so demos and multiplayer stay identical), and Classic mode still renders locked to the tick.

- **Ultra now boots playable by default (TAAU upscaler at 50% render scale) instead of at native resolution** (DOOM-0090)
  Fresh installs start Ultra the way it's actually playable (~35-42 FPS on a mid GPU) rather than ~8 FPS at native; existing saved settings are untouched and the scale is still adjustable in Options -> Renderer. Also trimmed the denoiser's coarsest filter pass (5->4) to shave its cost with minimal visual change.

- **Ultra path tracer runs markedly faster in light-heavy rooms (~20s → 35-42 FPS on an RX 6600 at the same 50% render scale)** (DOOM-0090)
  Skips shadow rays for sprite lights too far or dim to add visible light, instead of tracing one per glowing prop per pixel. The win grows with how many glowing props are in view. Plus a new on-screen per-pass GPU profiler (the \ key) used to find the hotspot.

- **Ultra now boots into the denoised path-traced view and remembers your view choice between sessions** (DOOM-0116)

- **Compact the static world BLAS to reclaim ray-tracing acceleration-structure memory** (DOOM-0091)
  The per-level world BLAS is now built with ALLOW_COMPACTION and copy-compacted into a right-sized acceleration structure at load, reclaiming the worst-case build padding (a 20-50% VRAM win that scales with large WADs). ALLOW_UPDATE is kept so moving doors/lifts still refit on the compacted structure.

- **Smooth (pixel-art-aware) upscaling of the 2D title/HUD/menu overlay in the 3D renderers.** (DOOM-0089)
  The menus and title screens now look smooth instead of blocky when shown over the 3D view.

- **Print clear WAD-placement guidance when no IWAD is found.** (DOOM-0040)
  If no game data file is found, the game now tells you exactly where to put a WAD instead of a cryptic error.

- **Launch in fullscreen by default, with -windowed to opt out.** (DOOM-0039)
  The game now opens fullscreen straight away; pass -windowed if you'd rather have a window.

- **Replace obsolete alloca() in r_data.c and w_wad.c with C99 VLAs (bounded buffers) and checked heap allocations (untrusted WAD-driven sizes), so a hostile lump count fails gracefully instead of overflowing the stack** (DOOM-0034)

- **Mark I_Error as _Noreturn.** (DOOM-0023)
  Tells the compiler that the fatal-error function never returns, so it can optimise better and reason correctly about the code that runs after a fatal-error guard.

### Fixed

- **M_QuickLoad sprintf can overflow tempstring[80] (-Wformat-overflow).** (DOOM-0097)
  A harmless-looking quick-load confirmation message could, with a long savegame name, write past the end of a fixed text buffer — a latent crash/corruption risk flagged by the compiler.

- **Draw the 2D presentation layer (HUD, menu, FPS, weapon) over the path-traced view.** (DOOM-0094)
  In the ray-traced view you currently see only the world — no menu, HUD, FPS counter or your gun. Bring those back so it's a real playable view, not a bare diagnostic.

- **3D renderer: use a per-swapchain-image present semaphore** (DOOM-0079)
  The Vulkan present-completion signal (renderFinished semaphore) was shared
  across frames, so a queue submit could re-signal it while a prior present
  still held it (a validation error caught once the Khronos validation layer
  was installed). Now one semaphore per swapchain image, indexed by the
  acquired image — no behaviour change on screen, but technically correct.

- **Fix the sky rendering a black band across distant outdoor views in the 3D back-ends.** (DOOM-0076)
  The sky shader squashed the panorama into the top half of the screen and clamped everything below centre to the texture's dark bottom row; it now maps the sky at DOOM's fixed scale with the horizon at screen centre and wraps instead of clamping, matching Classic.

- **Guard the Vulkan surface-format query against a zero count / dropped result.** (DOOM-0071)
  On an unusual graphics driver the renderer could read invalid memory while picking a display format at startup; now it checks properly and fails loudly instead.

- **Faithful 3D lighting — ceilings no longer render black in Solid/Ultra (DOOM-0069)**
  Removed a non-canonical directional key light from the mesh fragment shader. Classic DOOM shades by sector light + distance only, so floors and ceilings at the same light/distance now match; previously down-facing ceilings were darkened ~40% and went black in dim sectors (the courtyard-overhang band).

- **Stray distant floor/ceiling planes in the 3D outdoor view.** (DOOM-0065)
  In the 3D renderers a dark slab appears across the distance outdoors where Classic shows open sky.

- **Build flush lift/step shaft walls in the 3D mesh so they appear when the lift travels.** (DOOM-0068)
  In the 3D renderers a lift shaft wall was missing (you could see through it) until the lift moved; now it's there like Classic.

- **Stop door/lift textures stretching and squashing as they move in the 3D mesh.** (DOOM-0067)
  In the 3D renderers a moving door/lift stretched its texture open and squashed it closed; now the texture stays put as the surface slides, like Classic.

- **Automap renders blank/frozen in the 3D back-ends (Solid/Ultra).** (DOOM-0064)
  The in-game map is blank in the 3D renderers and only updates in Classic; needs a runtime probe to finish diagnosing.

- **Draw the upper wall above an outdoor doorway in the 3D mesh (sky-ceiling guard fix).** (DOOM-0062)
  In the 3D renderers the wall above DOOM's outdoor exit door was see-through; now the lintel renders like Classic.

- **Port DOOM wall texture pegging into the 3D mesh (fixes vertically-misaligned uppers and switches).** (DOOM-0061)
  In the 3D renderers a wall switch's button graphic was sliding out of view; now textures line up vertically like Classic.

- **Fix in-game menu text ghosting in the Classic renderer (hi-res).** (DOOM-0053)
  In Classic mode, menu pages smear over each other as you navigate.

- **Close black gaps in floor/wall geometry near doorways in the 3D view.** (DOOM-0052)
  Some doorways show black wedges where floor/wall should be.

- **Fix mid-game renderer switching (blank 3D world, Classic ghosting).** (DOOM-0051)
  Switching renderers during play broke the view; now rebuilt cleanly.

- **Animate moving sectors (doors, lifts, floors, crushers) in the 3D view.** (DOOM-0049)
  Doors and lifts don't visually move in 3D mode yet.

- **Classic hi-res floors no longer smear or crash (DOOM-0055)**
  At the sharper 640x400 resolution the floor and ceiling could smear during
  movement or close the game; the renderer's floor/ceiling row tracking is now
  widened to handle the taller picture (the classic 1997 limitation it warned
  about). Fixes the floor rendering and the windowed-mode crash.

- **Menu now shows when booting straight into a 3D renderer (DOOM-0045)**
  Launching directly into the Solid/Ultra 3D renderer previously showed a
  blank screen with no title or menu until a level loaded; the 2D menu now
  composites over the 3D view from the first frame.

- **Sound effects no longer drowned out by the music (DOOM-0047)**
  Music plays on a separate, louder audio device than the sound effects;
  its volume is now capped so effects stay audible underneath it.

- **Fix DOOM-0027 hi-res scaling regressions: small view window and mispositioned weapon sprite.** (DOOM-0041)
  In the new sharper (640x400) mode the 3D view was a small square in the middle of the screen and the gun drifted up to the centre; both now sit and scale correctly.

- **Initialise VulkanState::viewProj in the 3D renderer back-end.** (DOOM-0037)
  Make sure a camera matrix in the new 3D renderer always starts with a known value.

- **Fix the standalone sndserv build by adding the missing <string.h> include to soundsrv.c** (DOOM-0036)

- **Fix signed/unsigned printf/scanf format-specifier mismatches in the serial/IPX multiplayer drivers (flatadr, uart)** (DOOM-0035)

- **Pass a literal format string to printf for the dev/CD-ROM banners.** (DOOM-0033)
  Tidies up two startup messages so they print safely.

- **Guard unchecked heap allocations against out-of-memory null-deref.** (DOOM-0032)
  If the game can't get memory it now exits with a clear message instead of crashing.

- **Loading a savegame written by a different DOOM version no longer leaks the file buffer on the rejected-version path.** (DOOM-0031)

- **Playing back a demo recorded by a different DOOM version no longer crashes — it skips the demo and returns to the title screen (or exits cleanly for an explicit -playdemo/-timedemo).** (DOOM-0030)

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

### Security

- **Clamp atlas tile width so a crafted-WAD wide texture can't overrun the atlas.** (DOOM-0072)
  A specially-crafted level file with an unusually wide texture could corrupt memory while building the 3D texture sheet; the width is now capped so it crops instead.

- **Bound the -record demo-name to prevent a buffer overflow (DOOM-0070)**
  A -record command-line name longer than 27 characters overflowed the fixed 32-byte demoname buffer (strcpy+strcat). G_RecordDemo now uses snprintf to bound and NUL-terminate the name.

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

[Unreleased]: https://github.com/milnet01/DOOM_Ants/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/milnet01/DOOM_Ants/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/milnet01/DOOM_Ants/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/milnet01/DOOM_Ants/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/milnet01/DOOM_Ants/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/milnet01/DOOM_Ants/releases/tag/v0.1.0

---

DOOM_Ants is a GPL-v2 derivative of id Software's DOOM source code
(released 1997-12-23).
