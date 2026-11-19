# DOOM-0632 packed HD art: loop log

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-11-19 | 2 | 2 | 5 | 2 | 3 | 12 findings: 10 verified, 2 dismissed. 3 of the verified came from the packet pass and were fixed before dispatch, so the lanes read them; 1 came from settling a lane's open question. Every lane held every question; both lanes found the staging-read fallback, the incremental-reuse and the 260 MB observable findings. Dismissed: list-file and `--list` format (local detail, Q3) and the blit sentence (true but immaterial, Q1; the false sentence was deleted). All 10 fixed; 0 landed on text an earlier loop added. One-loop cap set by the user (2026-10-26), so the run stops here: capped, not converged. The 7 fixes made after the lanes returned are read by no lane; the build is their reader. |
| amend-1 | 2026-11-19 | 0 | - | - | - | - | Not a review; no reviewer dispatched. § 5 states the encoder's licence, read from the upstream `LICENSE` and file headers the same day (MIT or public domain), and B1 no longer carries the licence check. |
| impl-B1 | 2026-11-19 | 0 | - | - | - | - | Not a review; no reviewer dispatched. Building B1 set three limits § 4.1 did not state: a file under 2 GiB, at most 65536 entries, a name of 1 to 191 bytes. § 4.1 now states them. |
| impl-B2 | 2026-11-19 | 0 | - | - | - | - | Not a review; no reviewer dispatched. Building B2 changed three statements: BC4 is encoded by the project's own `rb_bc4.h`, not upstream's `rgbcx.h` (§ 5); a picture's kind comes from its name alone, and one whose name gives none stays a PNG, where the draft read a `materials.csv` column (§ 4.2); the override list is named (§ 4.2). § 5's disk figure is now the measured pack. |
