# DOOM-0539 pickup models: loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-11-02 | 2 | 4 | 4 | 10 | 3 | 21 findings: 11 from the packet pass (fixed before dispatch, so the lanes read them), 9 from the lanes, 1 from settling a lane's open question. 21 verified, 21 fixed, 0 dismissed. Both lanes held every question. Capped at one loop by the user's 2026-10-26 ruling for this project. The final loop's 10 fixes are read by no lane; implementation is their reader. Calm cap: 2 of the 10 final-loop findings landed on text this run wrote (the `ctrl[]` sentence and the budget sentence); the armed change is the whole document, so the share is read by substance: defects the draft exposed, not repairs of repairs. Second share: not recorded, the span is the whole document. One question left with the user: INV-3's footprint against the eight sprites wider than `radius`. |
| impl-1 | 2026-11-02 | 0 | - | - | - | - | Not a review; no reviewer dispatched. User's answer on INV-3: footprint within the larger of radius and half the sprite's width. |
| review-fix | 2026-11-05 | 0 | - | - | - | - | Not a review; no reviewer dispatched. §4.4: the pool roll-off for a light with a reach no longer needs kHdPipeline, per the user's 2026-11-04 ruling that Solid gets pickup light pools (DOOM-0551, review-code 2026-11-04). |
| review-fix | 2026-11-05 | 0 | - | - | - | - | Not a review; no reviewer dispatched. DOOM-0547: §4.2 now names the range each .dmdl field must keep, and INV-4's test list covers a finite value out of range. |
| impl | 2026-11-12 | 0 | - | - | - | - | Not a review; no reviewer dispatched. DOOM-0569: the passage on where RB_BuildSprites writes now names g.sprWorldCpu, the RAM copy the emitters read; the traced view copies it to g.sprWorldBuf. |
