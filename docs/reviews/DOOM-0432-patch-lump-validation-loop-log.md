# DOOM-0432 patch-lump validation — review loop log

The record `docs/specs/DOOM-0432-patch-lump-validation.md` §13 points at.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|------|------|-------|----|----|----|----|---------|
| 1 | 2026-09-30 | 2 | 1 | 0 | 2 | 2 | Five verified, all fixed: the 16-bit stored column offset (both lanes; table widened, §4.5 and INV-9), asking through the cache demoted a held block's tag, the short-lump case admitted two builds, INV-3's anchor did not exist in `R_InitSpriteLumps`, and INV-4's slot comparison had no test. Three open questions resolved clean by reading: every patch pointer's origin, `V_DrawPatchDirect` is a wrapper, the Vulkan side handles a missing menu picture. Loop 2 runs cold. |
| 2 | 2026-09-30 | 2 | 0 | 3 | 1 | 2 | Cap reached. Six verified, all fixed: `blit_tile` drew part of a picture §3 says is refused whole (both lanes; it now asks the verdict), a refused patch in a see-through wall fell through to the deferred composite walk (that walk is now in the build: `R_GetPostColumn`, INV-10), INV-3's sentence covered header readers its table did not, INV-3's scrape anchor matched `texturecolumnofs`, the scrape's file list was not said to be enumerated, and the refusal line's text was not pinned. One open question verified as a defect and fixed: `M_DecodePatchRGBA` cached `PLAYPAL` after the patch. These fixes were not re-read cold. No tail to file. |
