# DOOM-0617 ray-tracing effect switches: loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-11-15 | 2 | 4 | 2 | 4 | 2 | 12 verified (both lanes held every question; 8 found by both), 0 dismissed. 11 fixed; 1 surfaced to the user in § 10 (what the traced shadow pass multiplies by: no target carries albedo, `diffW` or the highlight inputs). Fixed: TLAS rebuild after the refit; subsector in an R32UI target with a sentinel; recombination in `scene_recombine.glsl`; AO weights on the direct term; INV-4 allows no source when the player turns corner shading off; INV-2 and B1 before-arms pinned with `-rtview`; Custom derived from the rows; a hand change records the GPU identity; the test runs when a 3D tier starts, never in Classic; compute passes own their layouts; Low when no preset meets the target. One-loop cap set by the user (2026-10-26): these fixes are read by no lane. Own-fix share unreadable: the whole document was new. **Capped; shipped as accepted.** |
| amend-1 | 2026-11-15 | 0 | - | - | - | - | Not a review; no reviewer dispatched. The user answered the open questions: test target 30 traced / 60 not, "for a start"; the shadow pass leaves highlights unshadowed, and the surface factor goes in its own target; the test measures with Ultra's art where supported; the switch list stands as proposed. Presets past Ultra noted for DOOM-0279. |
