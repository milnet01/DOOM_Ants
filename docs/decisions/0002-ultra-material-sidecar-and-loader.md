# 0002 — Ultra HD material sidecar format + PNG loader

**Status:** Accepted (2026-07-14) · Feature: DOOM-0042 (`docs/specs/DOOM-0042-ultra-hd-pbr-materials.md`) — the two decisions below are design-settled (spec §A/§B; the user approved the DOOM-0042 direction) but cold-eyes-pending — revisit if the DOOM-0042 cold-eyes pass alters the schema or loader choice.

## Context

The Ultra tier gains a high-fidelity PBR material set (DOOM-0042). Two hard
choices fall out of that, both of which the documentation standard
(`docs/standards/documentation.md` §"hard architectural choice") wants recorded
as decisions, not just design:

1. **How the original paletted texture names map to PBR map sets** — an on-disk
   storage format the engine parses at map load.
2. **How PNG map files are decoded** — a new file-format dependency (the engine
   has no image decoder today; all art arrives as paletted WAD lumps).

## Decision

1. **A `materials.csv` sidecar**, following the Q2RTX `materials.csv` pattern
   (`docs/research/3d-renderer-approaches.md:33,182-190,200`): one row per DOOM
   texture/flat/sprite name → PBR map set. Comma-delimited, positional columns,
   `#` comments, `|`-separated `flags`. The map geometry, sectors and UVs are
   never touched — only the material a `texnum` resolves to. Full schema lives in
   the DOOM-0042 spec §A; this ADR owns the *why*.

2. **Vendored `stb_image.h`** (public-domain single-header) for PNG decode — not
   SDL2_image. The engine links SDL2 + SDL2_mixer + the Vulkan loader + libm
   (`linuxdoom-1.10/Makefile:16,38`); SDL2_image would be a *new* link
   dependency, whereas `stb_image.h` adds none. Its vendored version **will be
   recorded** in `docs/standards/dependencies.md` §"Where this project's
   dependencies live" (as part of DOOM-0042 — that section has no
   vendored-single-header slot today) so it can't silently rot.

## Consequences

- Extending HD coverage = adding sidecar rows; no map edits, no code changes.
- The sidecar is the single hand-maintained artifact; the offline
  `scripts/pbr_derive.py` generator fills the long tail from the original WAD art.
- A vendored header is one more file to keep current on a sweep cadence
  (dependencies standard), traded for zero new link surface.

## Alternatives rejected

- **Editing the maps to point at new textures** — breaks vanilla-WAD
  compatibility and couples art to level data. The sidecar keeps them separate.
- **SDL2_image** — a heavier, new link dependency for what one PD header covers.
- **A pre-baked binary material pack** — deferred (DOOM-0042 "Out of scope");
  v1 loads/downscales PNGs at map load, compression is a later optimisation.
