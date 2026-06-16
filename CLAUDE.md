# DOOM_Ants

A GPL v2 fork of id Software's original **DOOM** source (`linuxdoom-1.10`,
released 1997-12-23), being modernised in two movements:

1. **Make it run today** — compile and play the 1997 engine on modern
   64-bit Linux, swapping the legacy X11 video/sound layer for SDL2.
2. **The spin** — evolve the renderer toward true 3D with hardware
   ray/path tracing (global illumination + ray-traced shadows), dynamic and
   volumetric lighting, and a 60 FPS floor — while keeping it feeling like the
   original DOOM.

The roadmap (`ROADMAP.md`) is the source of truth for what's planned,
in progress, and shipped. Finished items graduate into `CHANGELOG.md`.

## Repository layout

- `linuxdoom-1.10/` — the DOOM engine (the main game code; most work lands here)
- `sndserv/` — standalone sound server
- `sersrc/` — serial/modem multiplayer driver
- `ipx/` — IPX LAN multiplayer driver
- `docs/standards/` — the house rules (read before contributing)
- `docs/specs/` — one design doc per large feature
- `LICENSE.TXT` / `README.TXT` — id Software's GPL licence and original release notes (keep verbatim)

## House rules

These are the project's standards. Read the relevant one before working:

- **Coding** — `docs/standards/coding.md`
- **Commits** — `docs/standards/commits.md`
- **Documentation** — `docs/standards/documentation.md`
- **Roadmap format** — `docs/standards/roadmap-format.md`

Quick summary of the load-bearing ones:

- Commit messages use `<ID>: <description>` — imperative subject, one concern per commit.
- Every actionable roadmap item carries a permanent `[DOOM-NNNN]` ID. IDs are
  append-only: never renumber, never reuse.
- Shortest correct implementation; reuse before rewriting.
- Keep `CHANGELOG.md` and any version line in lockstep when releasing.

## Licence

This is a derivative of GPL-v2 DOOM, so DOOM_Ants is **GPL v2** as well.
`LICENSE.TXT` stays in place and id Software keeps its credit in the git
history and `README.TXT`.

## Game data

The source is the engine only. Running it needs a DOOM `.wad` data file
(e.g. the shareware `doom1.wad`), which is **not** in this repo for
licensing reasons.
