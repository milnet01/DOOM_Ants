# DOOM-0042 — Ultra tier: HD PBR material set (DOOM 3 / sci-fi-horror look)

**Status:** Draft — design approved by user 2026-07-14 (via brainstorming); **pending `/cold-eyes` (CLAUDE.md rule 14) and user spec review before implementation.** No code written yet.
**Roadmap:** 💭→🚧 `ROADMAP.md` DOOM-0042 (Phase 2 — "the spin"). This spec consolidates the DOOM-0042 decisions recorded on the roadmap (2026-06-24 sourcing, 2026-06-27 Ultra binding, 2026-06-28 POM/height requirement) and adds two 2026-07-14 refinements: the **hybrid** asset pipeline and **RT-view-first** sequencing.
**Kind:** feature.
**Depends on:** DOOM-0009 (path tracer + bindless material pipeline — the Ultra RT view), DOOM-0008 (3D level mesh + material seam), DOOM-0043 (Ultra ambient floor — shipped), DOOM-0044 (flashlight — shipped). These provide the lighting the PBR maps react to.
**Does NOT include:** 3D enemy models (separate, far-out roadmap item — enemies stay billboards here), Solid-tier HD art, hi-res title/menu art (DOOM-0053-area item).

## Goal

Give the **Ultra** tier its own **high-fidelity art set** — a modern, DOOM-3 / sci-fi-horror surface look — layered on the *same levels* the classic game ships. Only the **materials** change (wall/flat/sprite textures and their PBR maps); map geometry, sectors, and UVs are untouched.

**For a player:** pick **Ultra** and the walls, floors and props stop being flat 1990s stickers. A metal wall reads as real, grimy, wet-shiny metal that catches the flashlight; concrete is deep and matte; brick and tech panels have grooves that visibly recess and self-shadow as you move (parallax depth, DOOM-3 style). Pick **Solid** or **Classic** and you get the original art exactly as today. Same maps, new skin — chosen by the render tier.

**Technically:** every surface material gains a full **PBR map set** — base colour (albedo), normal, roughness, metallic, ambient-occlusion (AO), emissive, and **height/displacement** — instead of the single 8-bit paletted albedo the engine uses now. The path tracer already computes real light transport (DOOM-0009) plus scene lights (DOOM-0043) and the flashlight (DOOM-0044), so these maps have light to react to. Height drives **parallax occlusion mapping (POM)** in the hit shader for recessed relief.

This spec scopes a **first vertical slice**: the whole pipeline proven on **one map (E1M1)** in **Ultra's ray-traced view**, with a handful of hand-picked "hero" materials plus auto-derived maps for the rest. Scaling to the full game is then just adding rows to the material sidecar.

## Scope decisions (settled — do not re-litigate)

| Decision | Choice | Source |
|----------|--------|--------|
| Which tier | **Ultra only** (`rendermode == RB_RT3D`). Solid/Classic keep paletted art. | roadmap 2026-06-27; user 2026-07-14 |
| Which Ultra view first | **Ray-traced view first** (`rb_rtdebug==6`, Ultra's default). Ultra's raster sub-view (`~`) gets HD as a fast-follow. | user 2026-07-14 |
| Art direction | **Hybrid** — hand-picked CC0 "hero" materials for high-traffic surfaces + auto-derived PBR maps for the long tail. | user 2026-07-14 |
| Sourcing / licence | **CC0 / free only** (ambientCG, Poly Haven, user library). No id DOOM-3 assets. GPL-v2-distributable. | roadmap 2026-06-24; user 2026-07-14 |
| Height/relief method | **POM option (a): primary-hit POM only** — relief shades in the directly-viewed image; it does *not* cast self-shadows in RT shadow/GI rays (deferred: options b/c). | roadmap 2026-06-28 note; user 2026-07-14 |
| First map | **E1M1** (shareware `doom1.wad`, the standard test map). | this spec |
| New asset downloads | Placed in **`/mnt/Games/3D Engine Assets/`** under its existing category tree; CC0/free only. | user 2026-07-14 |

## Background — what exists, what's greenfield

Grounded in a source survey (r_vulkan.cpp ≈7086 lines; r_mesh.c; r_things.c; pathtrace.comp). Every claim below is a citation the cold-eyes pass must re-verify.

1. **Textures are 8-bit paletted, one image per material, no mipmaps.** Each material uploads as `VK_FORMAT_R8_UNORM` — one byte = a **palette index**, not colour (`r_vulkan.cpp:4403,4482`). Colour comes from sampling that index into a 256×1 PLAYPAL LUT in the shader (`pathtrace.comp:317-318`, mirrored in `mesh.frag`). The sampler is nearest / REPEAT / single-mip (`r_vulkan.cpp:440,3242,3654`).

2. **The material array is already bindless and per-texture.** The CPU packs a paletted atlas (`RB_BuildAtlas`, `r_mesh.c:909`) but the uploader slices it back into **N separate images** bound as a variable-count descriptor array (`r_vulkan.cpp:4398-4495`). `texnum → GPU image` is a plain index into `materialTex[]` (`pathtrace.comp:66`, sampled `nonuniformEXT(id)`). Material order is walls, then flats, then sprites: `numtextures + numflats + numspritelumps` (`r_mesh.c:964-968`); the sprite base `numWall+numFlat` reaches the tracer in `misc4.x` (`pathtrace.comp:91`). **This bindless array is the seam HD plugs into — no atlas repack needed.**

3. **No external-asset loading exists — fully greenfield.** No OBJ/glTF/PNG loader anywhere; all art comes through `W_CacheLumpName` from the WAD. Adding HD means new file I/O + an image decoder + new upload code.

4. **Geometry reaches the tracer as `rb_vertex_t` triangles.** World → static BLAS (`RB_BuildLevelMesh` `r_mesh.c:489`; `BuildAccelerationStructures` `r_vulkan.cpp:1503`); the megakernel decodes hits through the 18-float `rb_vertex_t` layout (`pathtrace.comp:385-388`). A vertex carries `texnum` as an `R32_SINT` attribute (`r_vulkan.cpp:3849`) and UVs. **No BVH change is needed for HD materials** (POM option (a) is a shader effect on existing triangles).

5. **Tier + view selection.** `rendermode` ∈ {`RB_CLASSIC`, `RB_RASTER3D`="Solid", `RB_RT3D`="Ultra"} (`r_backend.c:246-248`). Solid pins `rb_rtdebug=0` (raster), Ultra pins `rb_rtdebug=6` (ray-traced) (`r_backend.c:306-307`); `~` toggles RT within a tier. **No art-set selection exists** — the DOOM-0042 gap.

6. **Reusable prior art.** The **Q2RTX `materials.csv` sidecar** pattern (paletted-name → PBR set, no map edits) is documented in `docs/research/3d-renderer-approaches.md:33,183-200` and is the model for our sidecar. The user's library `/mnt/Games/3D Engine Assets/` already holds CC0 PBR sets (`Textures/Metal/`, `Wall/`, `Grunge/`, `Wood/` …) categorised per `ASSET_CATEGORIES.md`.

## Approach

Four layers: **(A) asset authoring** (offline), **(B) load & upload** (engine start / map load), **(C) shading** (the tracer), **(D) tier hook**. Design each so it can be built and tested on its own.

### A. Asset layer — the material sidecar + derive-generator (offline)

- **Material sidecar** `assets/ultra/materials.csv` (repo-tracked, tiny). One row per DOOM texture/flat/sprite **name**:
  `doom_name, source, albedo, normal, roughness, metallic, ao, emissive, height, uv_scale, flags`
  where `source ∈ {hero, derive}`. A **hero** row names PNG map files (a curated CC0 set from the library); a **derive** row leaves the map columns blank and the generator fills them. `flags` carries per-material switches (`pom`, `emissive`, `noPom` for flat decals). This is the *only* hand-maintained mapping; extending coverage = adding rows.
- **Hero materials:** hand-match the ~1–2 dozen highest-traffic DOOM names (STARTAN/BROWN/TEKWALL tech panels, brick, metal, floors) to a CC0 set from `/mnt/Games/3D Engine Assets/Textures/…`. New downloads go into that library under its categories (CC0/free only).
- **Derive-generator** `scripts/pbr_derive.py` (offline tool, **not** in the engine build): for every `derive` row, generate a believable PBR set **from the original WAD image** exported as PNG — height from luminance, normal from the height gradient (Sobel), roughness/metallic from simple per-family heuristics, AO from local occlusion of the height field. Keeps the classic look but adds relief. Output PNGs land beside the sidecar (or in the library) and are referenced back into the `derive` row so runtime never runs the generator.
- **Licence hygiene:** every hero material's CC0/free provenance is recorded (a `LICENSES` note in `assets/ultra/`). No proprietary art enters the repo.

### B. Load & upload layer (engine — new code)

- **PNG decode via a single-header public-domain loader** (`stb_image.h`, PD/MIT — zero new link dependency; SDL2_image is the fallback if we prefer the existing SDL stack). Reuse-before-rewrite: only add the loader we actually need.
- **Load only the current map's materials.** At map load the engine already knows which texnums the level uses; load HD sets for *those* only, and **downscale to a VRAM budget** (target ≈1–2K per map, not raw 4–8K). Bounds memory and load time. Log what was loaded/skipped (no silent truncation).
- **Upload as a parallel bindless PBR array** alongside the existing R8 array: per HD material, RGBA8 images — **albedo sRGB, all others linear** — with **mipmaps** and a new **linear-filtering, REPEAT** sampler (distinct from the nearest paletted sampler). A per-material flag (`usePBR`) tells the shader which path to take, so paletted and HD materials coexist in one scene (a `derive`-less texture just stays paletted).

### C. Shading layer (pathtrace.comp — the core new shader work)

On a surface hit, branch on `usePBR[matId]`:
- **Albedo:** sample the RGBA8 albedo directly (skip the index→PLAYPAL lookup). The transparency/alpha-test helpers (`spriteCandidateOpaque` `pathtrace.comp:184-196`) currently key on "palette index 0" — HD sprites key on the albedo **alpha channel** instead.
- **Normal mapping:** decode the tangent-space normal and rotate it into world space via a **tangent frame built from the surface**. Walls are axis-aligned quads → tangent from the wall direction + UV; flats (floors/ceilings) → world-XY tangent. (No per-vertex tangent attribute needed for v1; derive from geometry in the hit shader.)
- **Roughness / metallic:** feed the sampled values into the existing GGX/BRDF path (pairs with the DOOM-0009 §4.4 specular lobe).
- **Height / POM (option a):** before sampling the other maps, ray-march the height field along the **view direction in tangent space** and offset the UV so relief recesses and self-occludes *in the primary-hit shade only*. **Known limit (accepted for v1):** the displaced relief is invisible to RT shadow rays and the GI bake — a groove shades but casts no self-shadow. Options (b) height-field occlusion in NEE shadow rays and (c) true displacement-mapped BLAS are explicitly deferred. POM runs only on `flags:pom` materials; flat decals skip it.

### D. Tier hook (small)

- Gate the whole HD path on **`rendermode == RB_RT3D`** (Ultra). Entering Ultra ⇒ ensure the current map's HD sets are loaded and `usePBR` is set; leaving Ultra ⇒ the shaders fall back to the paletted path (the R8 array is always present). The RT toggle (`rb_rtdebug`) stays orthogonal — v1 shows HD in Ultra's **ray-traced** view; Ultra's raster sub-view (`~`) is the documented fast-follow (mesh.frag gains the same albedo/normal path later, POM last).

## Components / affected files

| Area | File(s) | Change |
|------|---------|--------|
| Sidecar + heroes | `assets/ultra/materials.csv`, `assets/ultra/LICENSES`, hero PNGs (library) | new — the hand-maintained mapping + provenance |
| Derive tool | `scripts/pbr_derive.py` | new — offline PBR-from-original generator (not built into the engine) |
| Image loader | new `rb_image.*` (or vendored `stb_image.h`) | new — PNG decode |
| Load/upload | `r_vulkan.cpp` material-upload loop (`:4398-4495`), new linear+mip sampler (cf. `:3242`), new bindless PBR descriptor array | parallel RGBA8 PBR path beside the R8 path; per-map current-set loading + downscale |
| Sidecar parse + map | `r_mesh.c` (near `RB_MaterialCount` `:964`) | parse `materials.csv`; resolve each texnum → HD set or paletted |
| Shading | `pathtrace.comp` (hit decode `:385-399`, sample `:317`, alpha `:184-196`) | `usePBR` branch: RGBA albedo, normal-map + tangent frame, roughness/metallic to BRDF, POM UV march |
| Tier hook | `r_backend.c` / `r_vulkan.cpp` present path | load/enable HD when `rendermode==RB_RT3D` |

**No change needed (follow existing seams):** the BLAS/TLAS build (POM is a shader effect, no new geometry), the level mesh, sprite billboard building, the WAD art path (paletted stays the fallback), Classic/Solid tiers.

## Verification

- **Look, Ultra RT view, E1M1:** hero surfaces read as modern PBR (wet metal shines, concrete is matte-deep, brick/tech relief recesses and self-shades under the flashlight as you strafe); derived surfaces keep the DOOM look but gain bump/roughness depth. Switch to **Solid**/**Classic** → original art, pixel-identical to today.
- **Parallax:** on a `flags:pom` wall, grooves visibly deepen with view angle and don't swim; at grazing angles POM degrades gracefully (no gaping holes) — if it doesn't, that's the trigger to consider option (c) later, not to ship the artefact.
- **Coexistence:** a map mixing hero + derive + still-paletted materials renders all three correctly in one frame (the `usePBR` branch).
- **Memory/load:** loading E1M1's HD set stays within the stated VRAM budget; the load log lists what loaded and what was skipped/downscaled. No silent truncation.
- **Licence:** every shipped hero material traces to a CC0/free source in `assets/ultra/LICENSES`; no proprietary art in the repo.
- **No regression off-Ultra:** Solid and Classic are byte-for-byte unchanged (HD path is `RB_RT3D`-gated).
- **Build:** `make` + `make test` clean, no new warnings; shaders compile (glslc) with 0 warnings.

## Out of scope (YAGNI / deferred)

- **3D enemy models** — enemies stay billboards; real monster models are a separate far-out roadmap item (sourcing + rigging problems).
- **Solid-tier HD art** and **hi-res title/menu art** — different tiers/items.
- **Ultra raster-view POM/PBR** — the raster sub-view (`~`) gets HD as a fast-follow (albedo/normal first, POM last); v1 is the ray-traced view.
- **POM self-shadowing in RT (options b/c)** and **true geometric displacement** — deferred; revisit only if primary-hit POM's flat silhouettes / grazing artefacts prove unacceptable.
- **Runtime GPU texture compression (BCn/KTX2) and a prebaked material pack** — v1 loads/downscales PNGs at map load; compression is a later size/perf optimisation.

## Cold-eyes loop log

*(to be filled by the `/cold-eyes` run — CLAUDE.md rule 14 — before implementation)*
