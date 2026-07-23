# Assets & Licensing Standard

DOOM_Ants is a GPL-v2 derivative of id Software's DOOM, and it now bundles
third-party art, fonts, and vendored code. This standard keeps the licensing
clean: every file in the repo has a known, compatible licence and a recorded
source, and nothing that can't be here gets committed.

## Licence

- **The whole project is GPL v2**, inherited from the DOOM source. New source
  files are GPL v2.
- **id Software's files stay verbatim** — `LICENSE.TXT`, `README.TXT`,
  `DOOMLIC.TXT`. They carry the original licence and credit; don't reword or
  reformat them.

## Game data is never committed

The repo is the **engine only**. WADs and game data are copyrighted and never
enter the repo — `.gitignore` enforces it (`*.wad`, `*.WAD`, `/wads/`). Players
supply their own `doom1.wad` / `doom.wad`. Save games (`*.dsg`) are per-player
runtime state, not source — also ignored.

## Third-party assets: licence + provenance, always

Any asset or vendored code that isn't ours must be **permissively licensed** and
its **source recorded**. The rule of thumb: no asset enters the repo without a
known licence *and* a recorded origin. When in doubt, don't commit it — that is
why the copyrighted WidePix / Bethesda widescreen art was rejected in favour of
runtime edge-extension for DOOM-0151.

What's in the tree today, and how each is recorded:

- **HD material "hero" textures (DOOM-0042)** — CC0. Palette-locked to DOOM's
  colours on derivation so the HD detail keeps the original look.
- **Menu font `Oxanium-SemiBold.ttf` (DOOM-0206)** — OFL. Ships **with its
  licence** alongside it (`linuxdoom-1.10/assets/OFL.txt`) and is embedded into
  the binary at build time.
- **Vendored single-header code** — `stb_image.h`, `stb_truetype.h` — public
  domain. Tracked with their versions in the dependencies standard.

Every one of these has its version, upstream URL, and licence recorded in
`docs/standards/dependencies.md`. Bundled binary assets (fonts) additionally
ship their licence file next to them. That is the provenance trail — keep it
current when you add or bump an asset.

## The sidecar / derived pattern (ADR 0002)

HD materials use a committed **`materials.csv` sidecar** to record each source
asset (and its licence), while the **derived, regenerable** outputs live under a
gitignored `derived/` tree — generated artifacts are not source. The same logic
covers other generated files (`*.spv`, `*.spv.h`, `*.ttf.h`): they are built from
committed inputs, so they are ignored, not tracked. See ADR
`docs/decisions/0002-ultra-material-sidecar-and-loader.md` for the why.

## Adding an asset — checklist

1. Confirm the licence is compatible (CC0 / OFL / public-domain / GPL-compatible).
2. Commit the source input; keep derived output gitignored if it's regenerable.
3. Record version + upstream + licence in `dependencies.md`; ship the licence
   file next to a bundled binary.
4. If it's a hard call (a format or loader choice), write or reference an ADR.
