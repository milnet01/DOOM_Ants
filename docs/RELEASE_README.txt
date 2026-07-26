DOOM_Ants — Linux (x86-64)
==========================

id Software's 1997 DOOM engine, modernised to build and run on today's
64-bit Linux through SDL2, with the music turned back on and the save
system fixed. GPL v2 (see LICENSE.TXT).

The release version is in the download's file name and in CHANGELOG.md.
Please report anything broken.


What you need
-------------

1. A DOOM game data file (a ".wad"). This is NOT included for licensing
   reasons. The free shareware "doom1.wad" works, as do retail doom.wad
   (Ultimate Doom) and doom2.wad.

2. Runtime libraries: SDL2, SDL2_mixer (with MIDI/FluidSynth support),
   FluidSynth, and a General-MIDI soundfont for the music.

   openSUSE:
     sudo zypper install libSDL2-2_0-0 libSDL2_mixer-2_0-0 \
                         libfluidsynth3 fluid-soundfont-gm

   Debian / Ubuntu:
     sudo apt install libsdl2-2.0-0 libsdl2-mixer-2.0-0 \
                      libfluidsynth3 fluid-soundfont-gm

   Fedora:
     sudo dnf install SDL2 SDL2_mixer fluidsynth fluid-soundfont-gm

   (If SDL2_mixer or FluidSynth is missing, the game still runs — it just
    plays without music.)


How to run
----------

   ./linuxxdoom -iwad /path/to/doom1.wad

Useful options:
   -iwad <file>     choose which WAD to load (DOOM 1, DOOM 2, etc.)
   -fullscreen      start in fullscreen

Controls:
   Move        W / A / S / D   or the arrow keys
   Fire        Ctrl
   Use/open    Space
   Run         Shift
   Menu        Esc

Music:
   The soundtrack plays automatically when a soundfont is available. To use
   a specific soundfont, set DOOM_SOUNDFONT before launching:
     DOOM_SOUNDFONT=/path/to/your.sf2 ./linuxxdoom -iwad doom1.wad

Saving:
   In-game save/load works (Esc -> Save Game / Load Game). Saves are
   written next to the working directory as doomsav*.dsg.


Notes
-----

- Three view modes ship: Classic (the original software renderer, now at a
  higher internal resolution and widescreen-aware), Solid (a Vulkan-rasterised
  3D view), and Ultra (Vulkan ray tracing on hardware that supports it). Pick
  one in Options -> Video.
- Source code: https://github.com/milnet01/DOOM_Ants
