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
SDL2, and you can pick between three ways of drawing the world:

- **Classic** — the original 1997 software renderer, pixel-for-pixel, now
  with widescreen support.
- **Solid** — a hardware (Vulkan) rasteriser: the same DOOM world drawn on
  the GPU, with dynamic lights and contact shadows.
- **Ultra** — a hardware **path tracer**: ray-traced lighting and shadows,
  HD PBR materials, a moving flashlight, ambient occlusion, and grimier,
  de-tiled surfaces. (Needs a ray-tracing-capable GPU; developed and tested
  on an AMD RX 6600.)

You switch between them from the in-game menu. Latest release: **0.6.0**.
Grab a build from the [Releases](https://github.com/milnet01/DOOM_Ants/releases)
page, or build it yourself below.

## Building & running

You'll need a DOOM `.wad` data file (e.g. the shareware `doom1.wad`, or
retail `doom.wad` / `doom2.wad`), which is **not** included here for
licensing reasons.

**Dependencies (Linux):** a C++23 compiler (GCC/Clang), `make`, and the dev
packages for **SDL2**, **SDL2_mixer**, and the **Vulkan** loader + headers.
The [`mold`](https://github.com/rui314/mold) linker is optional — the build
uses it automatically when it's installed and falls back to the default
linker when it isn't.

```sh
cd linuxdoom-1.10
make                     # builds linux/linuxxdoom
./linux/linuxxdoom -iwad /path/to/doom.wad
```

Handy flags: `-iwad <file>` picks the game data explicitly; `-warp <map>`
jumps straight into a level. The Solid and Ultra views need a working
Vulkan driver; Classic runs anywhere SDL2 does.

Windows builds are produced with a mingw-w64 cross-compile toolchain and
published on the Releases page.

## Licence

GPL v2, inherited from the original DOOM source. See [`LICENSE.TXT`](LICENSE.TXT).
Original code © id Software; see [`README.TXT`](README.TXT) for the 1997
release notes.
