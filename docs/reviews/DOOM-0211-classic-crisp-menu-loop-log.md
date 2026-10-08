# DOOM-0211 classic crisp menu — review loop log

The record `docs/specs/DOOM-0211-classic-crisp-menu.md` §13 points at.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-10-08 | 2 (every lane held every question; neutral-lane, denials 0) | 0 | 1 | 2 | 3 | Six verified, six fixed, none dismissed. Q3: the skull and logo now depend on the font bake under SDL as under Vulkan, so a failed font or -nocrispmenu cannot leave a crisp skull with no flush (lane 1; lane 2 as an open question); the Vulkan presenter now gives the module g.extent at init and in RecreateSwapchain (lane 2; lane 1 as an open question). Q4: INV-3 could pass with a gate reading Vulkan state, since Classic never initialises Vulkan, so both captures must now be crisp against base (both lanes); INV-4's breach named a window-vs-picture height mix-up its capture cannot show, so the breach is restated and the limit said (lane 1); INV-2's screens[0] half had no falsifying test and is deleted (lane 1). Q2: §12 kept DOOM-0206 INV-1's menu structure for the fallback while §4.4 changes it (lane 2). Four open questions resolved clean: RB_SetMode shuts the old back-end down before the new Init; the "nearest" hint is set in i_video.c; no test reads the moved menu code; and the NEEDS MEASUREMENT on SDL held (SDL 2.32, 1280x800 output, logical 640x480: viewport 64,0 640x480 x scale 1.6656 gives the 106.6,0 1066x800 picture; a one-output-pixel geometry quad lit one pixel; logical 0x0 gives scale 1 and the full output). Loop 2 runs cold. |
