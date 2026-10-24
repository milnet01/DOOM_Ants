# DOOM-0455 fixed colormap in 3D — review loop log

The record `docs/specs/DOOM-0455-fixed-colormap-3d.md` §12 points at.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-10-24 | 2 | 5 | 3 | 0 | 4 | 12 findings, 12 verified / 0 dismissed, all 12 fixed. 4 from building the packet (Q1: composite host write `coPush`/`lastCoPush` unnamed; Makefile include dependency unnamed. Q4: INV-6's fixture named E1M1, which `make_map_fixture.py` does not read; INV-2 scraped a header literal with no name). 7 from the lanes, both lanes holding every question: mesh push calls pass a literal size (Q1), `rt_tonemap` layout range literal 16 (Q1), visor headroom claim vs point lights (Q2), TAAU fallback double-inverting (Q2), debug-view weapon getting the powerups (Q2), INV-5 scrape order unmeetable at `svgf_composite` (Q4), INV-9 test unable to catch its own breach (Q4). 1 from a lane open question: §6 named the traced composite bucket only (Q1). Two NEEDS MEASUREMENT items (F12 screenshot key, `idbehold` cheats) resolved clean; one open question (bilinear `viewZ` at sky edges) left to the builder. One false clause (facing-term lag "by the angle the camera turns") narrowed by deletion, outside the tally. Loop 2 dispatched. |
