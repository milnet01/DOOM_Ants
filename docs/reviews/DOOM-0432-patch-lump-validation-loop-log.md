# DOOM-0432 patch-lump validation — review loop log

The record `docs/specs/DOOM-0432-patch-lump-validation.md` §13 points at.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-30 | 2 | 1 | 0 | 2 | 2 | Five verified, all fixed: the 16-bit stored column offset (both lanes; table widened, §4.5 and INV-9), asking through the cache demoted a held block's tag, the short-lump case admitted two builds, INV-3's anchor did not exist in `R_InitSpriteLumps`, and INV-4's slot comparison had no test. Three open questions resolved clean by reading: every patch pointer's origin, `V_DrawPatchDirect` is a wrapper, the Vulkan side handles a missing menu picture. Loop 2 runs cold. |
