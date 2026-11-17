# DOOM-0042 — Ultra HD PBR materials: loop log

The review and amendment record for [`docs/specs/DOOM-0042-ultra-hd-pbr-materials.md`](../specs/DOOM-0042-ultra-hd-pbr-materials.md).

## Cold-eyes loop log

`/cold-eyes` run 2026-07-14 (3 lanes/loop: code-citation accuracy · cross-doc/roadmap/asset integrity · spec-as-implementer-contract). Verified verified-findings per loop (INFO not counted); every actionable severity fixed each pass; loops 2+ briefed cold (no prior-fix list).

| Loop | C | H | M | L | Headline fixes |
|------|---|---|---|---|----------------|
| 1 | 0 | 7 | 7 | 3 | sampler/PLAYPAL citations; roadmap ID; licence (derive art out of repo); AO/emissive/POM contracts; CSV grammar; VRAM budget; usePBR SSBO; failure modes |
| 2 | 0 | 1 | 6 | 5 | RT tracer is Lambert-only ("existing GGX" false); asset-root; normal Y+; derive polarity; uv_scale×POM order; lowest-traffic key |
| 3 | 0 | 2 | 2 | 5 | GGX lobe → **depend on DOOM-0103** (not owned here); MIS wording; :422 white-furnace anchor; example `heroes/` prefix; SSBO order; roadmap dep edge |
| 4 | 0 | 1 | 4 | 4 | derive-row resolution (blank cells + naming convention); maps[7] order + v1 upload set; missing-CSV/dup-name; record 0042↔0103 in both roadmap bullets |
| 5 | 0 | 1 | 2 | 5 | pin `pbr_derive.py` WAD input; exclude sprites from v1 normal/POM; **ADR 0002** for the storage-format/loader decision; DOOMASSETDIR |
| 6 | 0 | 1 | 0 | 6 | **POM UV clamp bug** (bound the offset, not the coord — REPEAT tiling); ceiling bitangent; emissive sRGB; std430 |
| 7 | 0 | 1 | 2 | 3 | define `kPomHeightScale`; LOD-0 height taps; mip-gen method; re-attribute bindless seam |
| 8 | 0 | 0 | 1 | 3 | `doom_name` namespace resolution; generic `∂P/∂U` tangent frame |
| 9 | 0 | 0 | 1 | 3 | correct bindless=**DOOM-0009** / atlas=DOOM-0008; pom-no-height guard; single-channel `.r`; degenerate-UV fallback |
| 10 | 0 | 0 | 1 | 3 | scope **HD sprite albedo out of v1** (walls+flats only); bitangent from `∂P/∂V` (mirror sign) |
| 11 | 0 | 0 | 1 | 4 | specify **derived emissive** (black-except-fullbright via `emissive_derive.h`); dedup stb rationale to ADR |
| 12 | 0 | 0 | 0 | 4 | **converged** — polish only (citation ranges, ADR anchor, namespace-unique note, tool input) |
| 13-amend | – | – | – | – | 2026-10-26, amended in place for DOOM-0273 and DOOM-0103: the tier scope row, the Solid hard gate, "No regression off-Ultra", the Solid specular proxy and the in-engine derive note. A document edit; no reviewer was dispatched. |
| 14 | – | – | – | – | 2026-10-26, `review-contract` on the 13-amend change only. 2 lanes, each holding every question; lane 2 was re-dispatched because its first dispatch never started. Q1 0 · Q2 2 · Q3 0 · Q4 1: 3 findings, 3 verified, 0 dismissed, 3 fixed, each raised by both lanes. **Q2** the Solid hard gate asked frame-diff = 0, where DOOM-0273 INV-5 and DOOM-0103 INV-4 allow a same-build control pair. **Q2** §B and §C described DOOM-0103 as VNDF, MIS and metal, and as adding the roughness upload; its draft does none of that, and no spec owns HD roughness for `usePBR == 1` (corrected; the owner is marked open). **Q4** "a broader one" in Solid depended on unpinned hero art (dropped). Deletion carve-out: the Goal's "exactly as today". Two open questions, neither became a finding. Capped at 1 loop by the user; this loop's 3 fixes are read by no lane. All 3 findings sit in the amendment or in text it relies on. |
| impl 2026-11-17 | - | - | - | - | Not a review; no reviewer dispatched. The per-map ceiling's default is 1024 MB (user 2026-11-17, DOOM-0626), so DOOM-0539's models fit E2M2. The loop log moved here from the spec, verbatim. |

**Converged at loop 12** (polish convergence: no substantive finding — all three lanes returned "build-ready", citation accuracy 100%). Signed off by Claude per the user's standing delegation ("specs live in your domain — considered signed off once cold-eyes converges and I'm confident"). Ready for implementation (writing-plans).
