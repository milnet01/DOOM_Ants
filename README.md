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
   modern 64-bit Linux, replacing the ancient X11 graphics/sound code with
   SDL2.
2. **The spin.** Evolve the renderer toward *true* 3D with hardware ray
   tracing (and path tracing where it's feasible), dynamic lighting,
   volumetric "god-ray" lighting, and a solid 60 FPS floor — without
   losing the feel of the original.

See [`ROADMAP.md`](ROADMAP.md) for the plan and [`CHANGELOG.md`](CHANGELOG.md)
for what's shipped.

## Status

🚧 **Early days — building the foundations.** The repo currently holds id's
original source plus the project's documentation and standards. The "make it
run today" work comes next.

## Building & running

⏳ Not yet — modernising the build is the first milestone (see the roadmap).
You will also need a DOOM `.wad` data file (e.g. the shareware `doom1.wad`),
which is **not** included here for licensing reasons.

## Licence

GPL v2, inherited from the original DOOM source. See [`LICENSE.TXT`](LICENSE.TXT).
Original code © id Software; see [`README.TXT`](README.TXT) for the 1997
release notes.
