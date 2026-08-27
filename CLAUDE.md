# DOOM_Ants

A GPL v2 fork of id Software's original **DOOM** source (`linuxdoom-1.10`,
released 1997-12-23), being modernised in two stages:

1. **Make it run today** — compile and play the 1997 engine on modern
   64-bit Linux and Windows, swapping the legacy X11 video/sound layer for SDL2.
2. **The spin** — evolve the renderer toward true 3D with hardware
   ray/path tracing (global illumination + ray-traced shadows), dynamic and
   volumetric lighting, and a 60 FPS floor — while keeping it feeling like the
   original DOOM.

The roadmap (`ROADMAP.md`) is the source of truth for what's planned,
in progress, and shipped. Finished items graduate into `CHANGELOG.md`.

## Render tiers

Three tiers, chosen in the menu. **What separates them is the ART, not the
lighting** — every tier below Classic gets the full effect set; only the source
of the textures changes. Each of Solid and Ultra has both a rasterised and a
ray-traced view (the `~` key / the menu's Ray Tracing row), so "which tier" and
"how it is traced" are independent choices.

| Tier | Art | Effects |
|---|---|---|
| **Classic** | The 1993/97 game exactly as released | None — this is the original renderer. Widescreen is the one concession, and it is optional |
| **Solid** | The **original** textures, upscaled, with PBR / POM added on top | The full set — fog, lighting, shadows — faked cheaply wherever a cheap fake holds up |
| **Ultra** | **Replaced** with HD art | The full set, done properly, and first in line for anything new |

Set by the user on 2026-07-27, revising an earlier position (Solid was previously
"same renderer as Ultra, minus the HD art"). The distinction to hold on to:
**Solid enhances DOOM's own art; Ultra substitutes for it.** A player who wants
the game to still *look like DOOM* picks Solid.

Two consequences worth stating, because they are easy to get backwards:

- **Effects are not a tier ladder.** Do not gate a feature on Ultra because it is
  expensive; gate it on the ray-traced view, or ship a cheap approximation for
  Solid. "Ultra-only" is correct only for things that need the HD art itself.
- **Performance is Solid's feature.** Solid in its rasterised view is currently
  the smoothest way to play, and that is a property worth protecting when adding
  to it.

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
- **Spec format** — `docs/standards/spec-format.md`
- **Dependencies** — `docs/standards/dependencies.md`
- **Testing** — `docs/standards/testing.md`
- **Security & untrusted input** — `docs/standards/security.md`
- **Releases & versioning** — `docs/standards/releases.md`
- **Renderer & shaders** — `docs/standards/renderer.md`
- **Assets & licensing** — `docs/standards/assets.md`
- **Review & QA** — `docs/standards/review.md`
- **Performance** — `docs/standards/performance.md`

Quick summary of the load-bearing ones:

- Commit messages use `<ID>: <description>` — imperative subject, one concern per commit.
- Every actionable roadmap item carries a permanent `[DOOM-NNNN]` ID. IDs are
  append-only: never renumber, never reuse.
- Shortest correct implementation; reuse before rewriting.
- Tests live in `linuxdoom-1.10/tests/*_test.cpp` — one file, no Makefile edit.
  **`make` does not build them; only `make test` does.** So running
  `linux/<name>_test` straight after editing its source runs the *previous*
  binary and passes, which is the most convincing false green there is. Rebuild
  with `make test` before trusting a test you just changed — and before trying
  to break one on purpose to check it fires.
- A shader constant applied to a value *before* a threshold comparison is a
  division of that threshold, and no check that reads the threshold's own table
  can see it. DOOM-0331 INV-4 shipped a wall-blooming raster view that way; the
  spec's INV-4 and INV-9 amendments carry the story.
- Keep `CHANGELOG.md` and any version line in lockstep when releasing.
- A release could once ship a **stale binary**, because `release.sh` reused any
  artifact with the right filename. It now stamps each build with the commit it
  came from and re-checks the published assets; the releases standard's "Cutting
  a release" section owns the story and what to do when an asset is wrong.
- Dependencies stay on the **latest stable** version (features *and* security).
  An older pin is a last resort — only when the newer version explicitly breaks
  a feature — and must be logged in the dependencies standard's Version
  Exception Ledger, naming the version that broke it so it can be re-tested later.

## Licence

This is a derivative of GPL-v2 DOOM, so DOOM_Ants is **GPL v2** as well.
`LICENSE.TXT` stays in place and id Software keeps its credit in the git
history and `README.TXT`.

## Game data

The source is the engine only. Running it needs a DOOM `.wad` data file
(e.g. the shareware `doom1.wad`), which is **not** in this repo for
licensing reasons.
