# Review loop log — DOOM-0652 shared light grid

No automatic review is owed: nothing in the spec is hard to undo (rule 14). An empty log means
nobody has reviewed it.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| impl (B2) | 2026-11-22 | none: no reviewer dispatched | - | - | - | - | Amended to what B2 built: § 4.4 polar maps drawn on the CPU, front-facing one-sided rule, normal-offset test, flip-only redraw, wall-map cell cull; § 5 storage; § 7 B2 line |
| impl (B3) | 2026-11-22 | none: no reviewer dispatched | - | - | - | - | Amended to what B3 built: § 4.5 layers within one spacing move into a low room's air, the bake's inside-solid flag, the reach slack and the unmasked fallback; § 5 probe storage |
| impl (B4) | 2026-11-22 | none: no reviewer dispatched | - | - | - | - | Amended to what B4 built: § 4.5 void columns dropped by the seep field, back-hit early stop, bounce passes reuse pass 1's direct light; § 5 light and probe buffers in the card's memory, check reads a cached copy; § 6 MAP24 bake time |
