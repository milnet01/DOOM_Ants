# DOOM-0103 — cold-eyes loop log

Review record for `docs/specs/DOOM-0103-specular-lobe.md`.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-10-26 | 2 (`review-lane` via `neutral-lane`, every lane held every question) | 0 | 3 | 5 | 2 | Verified 10, fixed 10, dismissed 2. Q3: `specOn` could not read the setting in a shared include, so callers now pass it; mode 5's `RtPC` does not reach `misc7`; roughness and normal named for surfaces with no surface map, and the lobe runs in Solid and Ultra; `gspec` history made a ping-ponged `hspec[2]`, read by the composite; the raster highlight now takes `plight`'s roll-off. Q2: §12 now records that DOOM-0183 INV-3 and INV-5 hold with `specular` 0 only; goo-stained floors that take the sheen skip the lobe; mode 4's black-texel RIS gap is stated and accepted. Q4: INV-5 measures a `specular` 1 minus 0 difference at the test scene's `AT`; INV-6 checks the `FLAG_PSPRITE` skip instead of an unscrapable build. Dismissed: pinning binding numbers, and the `dev-shots` path. Capped at 1 loop by the user; this loop's fixes are read by no lane. No own-fix share at loop 1; no second share, as the armed change (bf801f4) created the whole document. No engine runs or captures; INV-4, INV-5 and §6 judged for consistency only. |
