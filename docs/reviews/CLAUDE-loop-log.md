# Review loop log — CLAUDE.md

The `review-contract` rows for the project `CLAUDE.md`. They live here
because an always-loaded file keeps its review history outside itself.
Genre: standard. Partition: every lane held every question.

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-10-26 | 2 | 0 | 2 | 1 | 0 | Gated span: commit 7824abb, the Solid and Ultra Effects cells of the Render tiers table. Scope: the Render tiers section and what it contradicts. Lanes ran through `neutral-lane`, so neither arrived holding the project `CLAUDE.md`. 3 findings verified, 1 fixed, 2 surfaced to the user, 0 dismissed. Q2 (lane A): "Effects are not a tier ladder" still allowed gating an effect on the ray-traced view alone, against both tiers' "Rasterised view: faked cheaply" — fixed to point at the table's per-view rule. Q2 (both lanes), surfaced: Ultra's "Every effect we can think of, and first in line for anything new" against Solid's "The full set — fog, lighting, shadows", the "every tier below Classic gets the full effect set" sentence and "Effects are not a tier ladder"; whether Ultra's set is larger, and what "first in line" means, is the user's call. Q3 (both lanes), surfaced: Solid's ray-traced view has no frame-rate floor; `docs/standards/performance.md` covers Classic, the raster path and RT-engaged Ultra only. Two open questions resolved clean: "below Classic" reads as table order; a tier resetting its default view does not stop the view being chosen afterwards. **Capped at 1 loop by the user.** The loop's one fix is read by no lane. No own-fix share: no earlier loop. Second share: 3 of 3 findings anchor on or rely on the gated span. |
