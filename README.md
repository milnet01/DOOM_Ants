# DOOM_Ants

A modern, ray-traced reimagining of id Software's **DOOM**, built on the
original GPL source code.

> Still plays like DOOM — just with the lights turned on.

## What this is

In 1997 id Software released the source code to DOOM. It's a brilliant
piece of 1990s engineering: it fakes 3D with a clever 2D trick and draws
every pixel on the CPU. **DOOM_Ants** takes that code and modernises it in
two stages:

1. **Make it run today.** Get the 1997 engine compiling and playing on
   modern 64-bit Linux (and Windows), replacing the ancient X11 graphics /
   OSS sound code with SDL2. ✅ *Done — the engine builds and plays.*
2. **The spin.** Evolve the renderer toward *true* 3D with hardware ray
   tracing (and path tracing where it's feasible), dynamic and volumetric
   lighting, HD materials, and a solid 60 FPS floor — without losing the
   feel of the original. 🚧 *In progress.*

See [`ROADMAP.md`](ROADMAP.md) for the plan and [`CHANGELOG.md`](CHANGELOG.md)
for what's shipped.

## Status

🎮 **Playable.** The 1997 engine runs on modern 64-bit Linux and Windows via
SDL2, and you pick how the world looks from the in-game menu.

There are three tiers, and **what separates Solid from Ultra is the artwork,
not the lighting**:

- **Classic** — the original 1997 software renderer, pixel-for-pixel, drawn
  on the CPU exactly as it was released. No added effects. Widescreen is the
  one concession, and it's optional.
- **Solid** — DOOM's **own** textures, upscaled, with modern surface detail
  layered on top. Pick this if you want the game to still *look like DOOM*.
- **Ultra** — the artwork **replaced** with HD material, and first in line
  for anything new.

Solid and Ultra get the same effects: dynamic and volumetric lighting, fog,
shadows, ambient occlusion, a moving flashlight, glowing nukage and lava, and
grimier, de-tiled surfaces. Solid fakes an effect cheaply wherever a cheap
fake holds up; Ultra does it properly.

Separately from the tier, Solid and Ultra each offer two ways of drawing that
world — a fast **rasterised** view, and a **ray-traced** view that computes
light by following it around the room. The two choices are independent: you
can have original art ray-traced, or HD art rasterised. Ray tracing needs a
GPU that supports it (developed and tested on an AMD RX 6600); rasterised
Solid is currently the smoothest way to play.

Latest release: **0.7.2**.
Grab a build from the [Releases](https://github.com/milnet01/DOOM_Ants/releases)
page, or build it yourself below.

## Building & running

You'll need a DOOM `.wad` data file (e.g. the shareware `doom1.wad`, or
retail `doom.wad` / `doom2.wad`), which is **not** included here for
licensing reasons.

**Dependencies (Linux):** a C++23 compiler (GCC/Clang), `make`, the dev
packages for **SDL2**, **SDL2_mixer** and the **Vulkan** loader + headers,
plus **`glslc`** and **`xxd`** — the build compiles the shaders and embeds
them, so it needs both. `packaging/ci-deps.txt` is the authoritative list and
names the Debian/Ubuntu package for each.

The [`mold`](https://github.com/rui314/mold) linker is optional — the build
uses it automatically when it's installed and falls back to the default
linker when it isn't.

```sh
cd linuxdoom-1.10
make                     # builds linux/linuxxdoom
make test                # builds and runs the unit tests (plain `make` does not)
./linux/linuxxdoom -iwad /path/to/doom.wad
```

Handy flags: `-iwad <file>` picks the game data explicitly. `-warp` jumps
straight into a level — one number for DOOM 2 (`-warp 7`), two for DOOM 1
(`-warp 1 7` for episode 1, map 7). The Solid and Ultra views need a working
Vulkan driver; Classic runs anywhere SDL2 does.

**For music**, you'll also want a General MIDI soundfont installed — the
game looks for `/usr/share/sounds/sf2/FluidR3_GM.sf2` by default, and
`$DOOM_SOUNDFONT` overrides that path. Without one the game runs fine with
sound effects only.

Windows builds are produced with a mingw-w64 cross-compile toolchain and
published on the Releases page.

## Licence

GPL v2, inherited from the original DOOM source. See [`LICENSE.TXT`](LICENSE.TXT).
Original code © id Software; see [`README.TXT`](README.TXT) for the 1997
release notes.
