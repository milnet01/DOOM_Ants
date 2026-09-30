# DOOM-0322 sky lid height — review loop log

The record `docs/specs/DOOM-0322-sky-lid-height.md` §13 points at.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-10-01 | 2 (every lane held every question; neutral-lane, denials 0) | 1 | 0 | 0 | 2 | Three verified, three fixed: § 4.4 said the seal faces the other way, but emit_sky_wall's normal points into the back sector, so the offset direction is now stated (lane 2, and lane 1 as an open question); INV-4's capture named no view and Solid reads 0 either way, so it now names Ultra (lane 1; a wrong-direction build measured 563 lintel pixels there); § 6's frame-time and any-stock-map budget had no check, now bounded to the nine starts with frame time stated as unbudgeted (both lanes). Not in the tally: § 2's reason for dropping the gap wall was half false (Classic's taller-side view depends on light level, markceiling); corrected under documentation.md § 2.2 with a measured replacement (raised lids plus the gap wall still cut the facade off, 1151 px against 33170). Two open questions resolved clean: build-time heights (the tying wall exists only when its ceiling is higher at build, the offset's own condition) and corner slits (E3M9 sector 12 captures, edge pixels only). Loop 2 runs cold. |
