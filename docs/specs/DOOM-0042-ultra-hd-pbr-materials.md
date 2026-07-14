# DOOM-0042 — Ultra tier: HD PBR material set (DOOM 3 / sci-fi-horror look)

**Status:** Draft — design approved by user 2026-07-14 (via brainstorming); **pending `/cold-eyes` (CLAUDE.md rule 14) and user spec review before implementation.** No code written yet.
**Roadmap:** 💭→🚧 `ROADMAP.md` DOOM-0042 (Phase 2 — "the spin"). This spec consolidates the DOOM-0042 decisions recorded on the roadmap (2026-06-24 sourcing, 2026-06-27 Ultra binding, 2026-06-28 POM/height requirement) and adds two 2026-07-14 refinements: the **hybrid** asset pipeline and **RT-view-first** sequencing.
**Kind:** feature.
**Depends on:** DOOM-0009 (path tracer + bindless material pipeline — the Ultra RT view; in progress 🚧, but its NEE integrator that DOOM-0043/0044 build on is functional), DOOM-0008 (3D level mesh + material seam — shipped), DOOM-0043 (Ultra ambient floor — shipped), DOOM-0044 (flashlight — shipped). These provide the lighting the PBR maps react to. **Also depends on DOOM-0103** (GGX/VNDF specular lobe — planned) for the wet-metal/glossy response the roughness & metallic maps drive: DOOM-0042 supplies those maps, DOOM-0103 delivers the lobe (see Approach C). Until DOOM-0103 lands, roughness/metallic ride the diffuse term only — albedo/normal/AO/POM are still a clear upgrade.
**Does NOT include:** 3D enemy models (separate, far-out roadmap item — enemies stay billboards here), Solid-tier HD art, hi-res title/menu art (DOOM-0086).

## Goal

Give the **Ultra** tier its own **high-fidelity art set** — a modern, DOOM-3 / sci-fi-horror surface look — layered on the *same levels* the classic game ships. Only the **materials** change (wall/flat/sprite textures and their PBR maps); map geometry, sectors, and UVs are untouched.

**For a player:** pick **Ultra** and the walls, floors and props stop being flat 1990s stickers. A metal wall reads as real, grimy, wet-shiny metal that catches the flashlight; concrete is deep and matte; brick and tech panels have grooves that visibly recess and self-shadow as you move (parallax depth, DOOM-3 style). Pick **Solid** or **Classic** and you get the original art exactly as today. Same maps, new skin — chosen by the render tier.

**Technically:** every surface material gains a full **PBR map set** — base colour (albedo), normal, roughness, metallic, ambient-occlusion (AO), emissive, and **height/displacement** — instead of the single 8-bit paletted albedo the engine uses now. The path tracer already computes real light transport (DOOM-0009) plus scene lights (DOOM-0043) and the flashlight (DOOM-0044), so these maps have light to react to. Height drives **parallax occlusion mapping (POM)** in the hit shader for recessed relief. The specular/glossy highlight that roughness & metallic feed is delivered by **DOOM-0103**'s GGX lobe (§C) — this spec provides the maps and depends on that lobe for the "wet metal" look.

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

1. **Textures are 8-bit paletted, one image per material, no mipmaps.** Each material uploads as `VK_FORMAT_R8_UNORM` — one byte = a **palette index**, not colour (`r_vulkan.cpp:4403,4482`). Colour comes from sampling that index into a 256×1 PLAYPAL LUT; the shared albedo decode is `decodeAlbedo` in `shaders/pt_common.glsl:87-93` (`#include`d by both the tracer and the GI bake), and `mesh.frag` mirrors it for the raster view (`pathtrace.comp:317-318` is the sky-panorama variant of the same sample, not the canonical hit path). The material sampler is nearest / REPEAT / single-mip (`g.texSampler`, declared `r_vulkan.cpp:440`, created `:3636-3644`).

2. **The material array is already bindless and per-texture.** The CPU packs a paletted atlas (`RB_BuildAtlas`, `r_mesh.c:909`) but the uploader slices it back into **N separate images** bound as a variable-count descriptor array (`r_vulkan.cpp:4398-4495`). `texnum → GPU image` is a plain index into `materialTex[]` (`pathtrace.comp:66`, sampled `nonuniformEXT(id)`). Material order is walls, then flats, then sprites: `numtextures + numflats + numspritelumps` (`r_mesh.c:964-968`); the sprite base `numWall+numFlat` reaches the tracer in `misc4.x` (`pathtrace.comp:91`). **This bindless array is the seam HD plugs into — no atlas repack needed.**

3. **No external-asset loading exists — fully greenfield.** No OBJ/glTF/PNG loader anywhere; all art comes through `W_CacheLumpName` from the WAD. Adding HD means new file I/O + an image decoder + new upload code.

4. **Geometry reaches the tracer as `rb_vertex_t` triangles.** World → static BLAS (`RB_BuildLevelMesh` `r_mesh.c:489`; `BuildAccelerationStructures` `r_vulkan.cpp:1503`); the megakernel decodes hits through the 18-float `rb_vertex_t` layout (`pathtrace.comp:385-399`). A vertex carries `texnum` as an `R32_SINT` attribute (`r_vulkan.cpp:3849`) and UVs. **No BVH change is needed for HD materials** (POM option (a) is a shader effect on existing triangles).

5. **Tier + view selection.** `rendermode` ∈ {`RB_CLASSIC`, `RB_RASTER3D`="Solid", `RB_RT3D`="Ultra"} (labels in the `modeNames[]` array at `r_backend.c:246-248`). Solid pins `rb_rtdebug=0` (raster), Ultra pins `rb_rtdebug=6` (ray-traced) (`r_backend.c:306-307`); `~` toggles RT within a tier. **No art-set selection exists** — the DOOM-0042 gap.

6. **Reusable prior art.** The **Q2RTX `materials.csv` sidecar** pattern (paletted-name → PBR set, no map edits) is documented in `docs/research/3d-renderer-approaches.md:33,182-190,200` and is the model for our sidecar. The user's library `/mnt/Games/3D Engine Assets/` already holds CC0 PBR sets (`Textures/Metal/`, `Wall/`, `Grunge/`, `Wood/` …) categorised per `/mnt/Games/3D Engine Assets/ASSET_CATEGORIES.md` (external to the repo, like the WAD).

## Approach

Four layers: **(A) asset authoring** (offline), **(B) load & upload** (engine start / map load), **(C) shading** (the tracer), **(D) tier hook**. Design each so it can be built and tested on its own.

### A. Asset layer — the material sidecar + derive-generator (offline)

- **Material sidecar** `assets/ultra/materials.csv` (repo-tracked, tiny; format + loader rationale in ADR `docs/decisions/0002-ultra-material-sidecar-and-loader.md`). **Format:** comma-delimited, one **header row** naming the columns (itself a `#` comment — for humans; the parser keys columns by **position**), other `#`-prefixed lines ignored as comments. Cells are whitespace-trimmed; no quoting — a path must not contain a comma (rename such files). One data row per DOOM texture/flat/sprite **name**:
  `doom_name,source,albedo,normal,roughness,metallic,ao,emissive,height,uv_scale,flags`
  - `source ∈ {hero, derive}`. A **hero** row names its PNG map files explicitly (a curated CC0 set). A **derive** row leaves the map columns **blank** in the committed CSV; the loader resolves each map by the fixed naming convention **`derived/<doom_name>_<suffix>.png`** (`suffix ∈ alb,nrm,rgh,met,ao,emis,hgt`) that `scripts/pbr_derive.py` writes — so the (un-committed) derived PNGs never appear as paths in the tracked file.
  - **Hero** map columns hold a PNG path **relative to the asset root `assets/ultra/`** (e.g. `heroes/metal/tek_nrm.png`); an empty hero cell = **"no map"** → the shader uses a sensible default (flat normal, mid roughness, non-metal, no AO/emissive, no height). **Derive** map columns are always blank (resolved by the `derived/…` convention above).
  - `uv_scale` — a float applied as a UV multiplier in the hit shader (tiles the material across its surface; `1.0` = one map span across the surface's native UVs; default `1.0` if blank).
  - `flags` — a **`|`-separated** list (pipe, so it never collides with the CSV comma): `pom` (march POM on this material), `noPom` (force-skip POM — flat decals; `noPom` wins if both appear), `sprite` (alpha-keyed cutout, see §C). **Emissive is driven by the `emissive` map column, not a flag** — a non-empty `emissive` path means the material self-emits; there is no `emissive` flag.
  - This is the *only* hand-maintained mapping; extending coverage = adding rows. Example:
    ```
    #doom_name,source,albedo,normal,roughness,metallic,ao,emissive,height,uv_scale,flags
    TEKWALL1,hero,heroes/metal/tek_alb.png,heroes/metal/tek_nrm.png,heroes/metal/tek_rgh.png,heroes/metal/tek_met.png,heroes/metal/tek_ao.png,heroes/metal/tek_emis.png,heroes/metal/tek_hgt.png,1.0,pom
    FLOOR4_8,derive,,,,,,,,1.0,noPom
    ```
- **Hero materials:** hand-match the ~1–2 dozen highest-traffic DOOM names (STARTAN/BROWN/TEKWALL tech panels, brick, metal, floors) to a CC0 set. Downloads are **staged** in the user library `/mnt/Games/3D Engine Assets/Textures/…` under its categories (CC0/free only); the maps actually used are then **copied into the repo at `assets/ultra/heroes/` and committed** (CC0, tiny), so a fresh clone builds Ultra without the external drive.
- **Derive-generator** `scripts/pbr_derive.py` (offline tool, **not** in the engine build) — **input:** it reads the IWAD (`doom1.wad`) directly, a standalone Python lump-directory + PLAYPAL parser (the engine's C `W_CacheLumpName` isn't reusable offline), exporting each `derive` texture/flat to an RGB PNG albedo source keyed by `doom_name`. For every `derive` row it then generates a believable PBR set **from that WAD image** — height from luminance (**brighter = raised**, which fixes both the Sobel normal orientation and the POM march direction), normal from the height gradient (Sobel, default strength ×2.0, emitting OpenGL **Y+**), AO from horizon-based local occlusion of the height field (default 4-px radius) — all three tunable in the script — and roughness/metallic from a **name-prefix → (roughness, metallic) family table** (a plain data map in the script, editable without code):

  | Family (longest matching `doom_name` prefix) | roughness | metallic |
  |----------------------------------------------|-----------|----------|
  | `METAL` `TEK` `SILVER` `SHAWN` `SUPPORT`      | 0.35      | 1.0      |
  | `BROWN` `BRONZE` `COMP` `PIPE`                | 0.55      | 1.0      |
  | `BRICK` `STONE` `ROCK` `GRAY` `MARB`          | 0.85      | 0.0      |
  | `WOOD` `PANEL` `DOOR`                         | 0.75      | 0.0      |
  | `FLOOR` `FLAT` `CEIL` `RROCK` `MFLR` (flats)  | 0.80      | 0.0      |
  | *(no prefix match — default)*                | 0.70      | 0.0      |

  For `sprite`-flagged rows the generator carries the WAD's index-0 transparency into the albedo **alpha channel**, so derived sprites cut out like hero sprites. The generator emits **all seven maps as PNGs** into `derived/` — roughness/metallic as **flat fills** of the family-table value — so every derive material reaches the shader through the same texture path as a hero (no scalar material channel needed). Keeps the classic look but adds relief.
- **Derive outputs are NOT committed.** Generated PNGs are derivative works of id's WAD art, so — exactly like the WAD itself (see project `CLAUDE.md`) — they stay **out of the repo**, git-ignored under `assets/ultra/derived/`; a fresh clone regenerates them by running `pbr_derive.py` once. The committed CSV holds **no** derived paths (the loader resolves them by the naming convention above), so the tracked file carries no WAD-derived filenames. Runtime loads the pre-generated PNGs; it never runs the generator in-engine.
- **Licence hygiene:** every hero material's CC0/free provenance is recorded (a `LICENSES` note in `assets/ultra/`). No proprietary art — original **or WAD-derived** — is committed to the repo; only the tiny CC0-hero PNGs + the sidecar + `LICENSES` are tracked.

### B. Load & upload layer (engine — new code)

- **PNG decode via a vendored single-header public-domain loader — `stb_image.h`** (PD/MIT, **no new *link* dependency**). Preferred over SDL2_image because the only SDL-family / image libs linked today are SDL2 + SDL2_mixer (`linuxdoom-1.10/Makefile:16`; the Vulkan loader + libm also link at `:38`) — SDL2_image would be a *new* link dep. Add a **vendored-header bullet** to `docs/standards/dependencies.md` §"Where this project's dependencies live" recording the `stb_image.h` version so it doesn't silently go stale (that section has no vendored-single-header slot today). Reuse-before-rewrite: only add the loader we actually need.
- **Locating the assets.** The engine resolves the asset root `assets/ultra/` via a `DOOMASSETDIR` env var (default: `assets/ultra/` relative to the executable/repo root), mirroring how the WAD is found through `DOOMWADDIR` (`d_main.c:740`, default `.`). A fresh clone finds the committed heroes there with no external drive.
- **Load only the current map's materials.** At map load the engine already knows which texnums the level uses; load HD sets for *those* only, under two **independent, configurable** bounds:
  - **(i) Per-texture resolution** — clamp each map's longest edge to a max (**default 1024 px**; this is what the earlier "1–2K" shorthand meant — pixels, not MB). Source art larger than that is box-downscaled on load.
  - **(ii) Per-map memory** — track total material VRAM as sets upload; on exceeding a soft ceiling (**default 768 MB**, comfortably within the ≥8 GB the RT path already requires) drop the **lowest-traffic** remaining materials back to paletted rather than growing unbounded. *Traffic* = total world surface-area of that texnum in the current map, computed from the level mesh at load; hero materials are pinned above derived ones regardless of area. Upload proceeds in **descending-traffic order** (so the victims really are the lowest-traffic ones), the MB total counts the **full mip chain** (~1.33× the base image), and a dropped material has its control-SSBO `usePBR` forced to **0** — the sidecar sets `usePBR`, but the load outcome overrides it.
  - The load log prints each material's loaded resolution + the running MB total and lists everything skipped/downscaled/dropped — **no silent truncation**.
- **Failure handling (never crash).** A missing/undecodable hero PNG, a sidecar row that names a file that isn't there, or a malformed row (wrong column count, unknown `source`, or an unrecognised `flags` token) → that material **falls back to the paletted R8 path** and logs a one-line warning; the map still loads. The paletted array is always present, so fallback is always available. **No `materials.csv` at all** ⇒ every material stays paletted (Ultra shows Solid's art until the sidecar exists); a **duplicate `doom_name`** ⇒ the last row wins, logged.
- **Upload as a parallel bindless PBR array** alongside the existing R8 array: per HD material, RGBA8 images — **albedo sRGB, all others linear** — with **mipmaps** and a new **linear-filtering, REPEAT** sampler (distinct from the nearest paletted sampler). A **per-material control SSBO** (one struct per matId, indexed by the same `texnum` the hit shader already decodes) carries `usePBR`, the seven map indices into the PBR array, `uv_scale`, and `flags`; it is uploaded at map load. Struct (std430): `int maps[7]` — indices into the PBR array in **CSV map-column order** (`[0]`albedo, `[1]`normal, `[2]`roughness, `[3]`metallic, `[4]`ao, `[5]`emissive, `[6]`height); `-1` = no map. `float uvScale`; `uint flags` (bit0 `pom`, bit1 `noPom`, bit2 `sprite`); `uint usePBR` (0 = paletted fallback). **v1 upload set = the five maps something samples** (albedo, normal, ao, emissive, height); **roughness/metallic (`maps[2]`,`[3]`) stay `-1` and are not uploaded** until DOOM-0103's lobe samples them (the generator still bakes them offline, so DOOM-0103 adds only the upload + sample, no regen) — keeping the 768 MB budget on active maps. So paletted and HD materials coexist in one scene (a material with no HD set keeps `usePBR = 0` and stays paletted).

### C. Shading layer (pathtrace.comp — the core new shader work)

On a surface hit, branch on the material's `usePBR` (read from the control SSBO, §B). `uv_scale` (§A) multiplies the raw UVs **first** (so the height field marches in tiled space); the Height/POM bullet below then computes the UV offset on those scaled UVs; every map sample uses that offset UV.
- **Height / POM (option a):** for `flags:pom` materials, ray-march the height field along the **view direction in tangent space** and offset the UV so relief recesses and self-occludes *in the primary-hit shade only*. **Bounded march:** **16 linear steps at normal incidence, scaling up to 32 at grazing angles** (step count ramps linearly in `1 − N·V`), then one binary-search refinement; the marched UV is **clamped to [0,1]** so an overshoot samples the edge texel, never tiles garbage. "Degrades gracefully at grazing angles" = that clamp + the step cap (no gaping holes), measured by the parallax check in Verification. `noPom`/flat-decal materials skip the march entirely. **Known limit (accepted for v1):** the displaced relief is invisible to RT shadow rays and the GI bake — a groove shades but casts no self-shadow. Options (b) height-field occlusion in NEE shadow rays and (c) true displacement-mapped BLAS are explicitly deferred.
- **Albedo:** sample the RGBA8 albedo directly (skip the index→PLAYPAL lookup). The transparency/alpha-test helpers (`spriteCandidateOpaque` `pathtrace.comp:184-196`) currently key on "palette index 0" — HD `flags:sprite` materials key on the albedo **alpha channel** instead (`alpha < 0.5` = transparent).
- **Normal mapping:** decode the tangent-space normal and rotate it into world space via a **tangent frame built from the surface**. Walls are axis-aligned quads → tangent from the wall direction + UV; flats (floors/ceilings) → world-XY tangent. (No per-vertex tangent attribute needed for v1; derive from geometry in the hit shader.) **Sprites are excluded in v1** — a camera-facing billboard has no stable tangent frame, so `flags:sprite` materials use **albedo + alpha only** (normal/roughness/metallic/height not sampled); real sprite relief waits for the DOOM-0080 3D models. **Convention:** OpenGL **Y+** green-channel, right-handed tangent, `n.y` un-flipped (ambientCG's default); the derive generator MUST emit the same Y+ convention so hero and derived normals agree.
- **Roughness / metallic → the GGX specular lobe (DOOM-0103).** The RT tracer is **diffuse-only** today — Lambertian NEE with explicitly **no MIS** (`docs/specs/DOOM-0009-path-tracer.md` §4.4: "the shipped integrator is pure-Lambert … MIS returns only if the GGX/VNDF specular path below lands"; the textured shading path is `pathtrace.comp` `mode==3` at `:426`, while `:422` is only the white-furnace diagnostic). The GGX/VNDF specular lobe these maps drive is its **own** roadmap item — **DOOM-0103** ("Specular / glossy highlights (GGX) on metal, wet floors and nukage"), which records that DOOM-0042 depends on it. **Ownership split:** DOOM-0042 provides the roughness/metallic *maps* and the material plumbing that feeds them to the lobe; **DOOM-0103 delivers the lobe itself** (F0 / dielectric split, VNDF importance sampling, and the MIS weight that §4.4 says arrives *with* the specular path — none of it exists to inherit today). So the "wet metal shines" goal and the specular-highlight verification are **gated on DOOM-0103**; until it lands, roughness/metallic have no effect and the other maps (albedo/normal/AO/POM) carry the upgrade.
- **Ambient occlusion (AO):** multiply the sampled AO into the **indirect/ambient** term only — the GI-bake bounce + sky/sector ambient — **never** the direct flashlight/NEE contribution. AO darkens crevices under ambient light without dimming a surface the flashlight is directly lighting.
- **Emissive:** if the material has an `emissive` map, add `emissive.rgb × kEmissiveScale` as self-emitted radiance on the **primary hit only** (v1). It is **not** registered as an NEE emitter / area light this slice — promoting hot emissives to real light sources is deferred alongside options (b)/(c). `kEmissiveScale` is a single tunable constant — match the switch-glow scale (the value `40.0` from `emissive_derive.h:52`), redeclared as a shader constant / push value (the C++ `constexpr` can't be `#include`d into GLSL, so this is value reuse, not symbol reuse).

### D. Tier hook (small)

- Gate the whole HD path on **`rendermode == RB_RT3D`** (Ultra). Entering Ultra ⇒ ensure the current map's HD sets are loaded and `usePBR` is set; leaving Ultra ⇒ the shaders fall back to the paletted path (the R8 array is always present). The RT toggle (`rb_rtdebug`) stays orthogonal — v1 shows HD in Ultra's **ray-traced** view; Ultra's raster sub-view (`~`) is the documented fast-follow (mesh.frag gains the same albedo/normal path later, POM last).

## Components / affected files

| Area | File(s) | Change |
|------|---------|--------|
| Sidecar + heroes | `assets/ultra/materials.csv`, `assets/ultra/LICENSES`, hero PNGs (`assets/ultra/heroes/`) | new — the hand-maintained mapping + provenance |
| Derive tool | `scripts/pbr_derive.py` | new — offline PBR-from-original generator (not built into the engine) |
| Image loader | vendored `stb_image.h` + thin `rb_image.*` wrapper | new — PNG decode |
| Load/upload | `r_vulkan.cpp` material-upload loop (`:4398-4495`), new linear+mip sampler (cf. the paletted `g.texSampler` at `:3636-3644`), new bindless PBR descriptor array, per-material control SSBO | parallel RGBA8 PBR path beside the R8 path; per-map current-set loading + downscale |
| Sidecar parse + map | `r_mesh.c` (near `RB_MaterialCount` `:964`) | parse `materials.csv`; resolve each texnum → HD set or paletted |
| Shading | `pathtrace.comp` (hit decode `:385-399`, albedo via `pt_common.glsl` `decodeAlbedo` `:87-93`, alpha `:184-196`, textured path `mode==3` `:426`) | `usePBR` branch: RGBA albedo, normal-map + tangent frame, AO on ambient, emissive, POM UV march. Roughness/metallic **feed DOOM-0103's GGX lobe** (that item owns the BRDF math) |
| Tier hook | `r_backend.c` / `r_vulkan.cpp` present path | load/enable HD when `rendermode==RB_RT3D` |

**No change needed (follow existing seams):** the BLAS/TLAS build (POM is a shader effect, no new geometry), the level mesh, sprite billboard building, the WAD art path (paletted stays the fallback), Classic/Solid tiers.

## Verification

- **Look, Ultra RT view, E1M1** (qualitative goal): hero surfaces read as modern PBR (wet metal shines, concrete is matte-deep, brick/tech relief recesses and self-shades under the flashlight as you strafe); derived surfaces keep the DOOM look but gain bump/roughness depth. Observable proxies for the subjective look:
  - **Specular response (gated on DOOM-0103's GGX lobe):** a flashlight sweep across a hero metal wall produces a specular highlight that tracks the light — before/after screenshot pair shows the highlight move; the same wall in Solid shows none. *Deferred until DOOM-0103 lands; before then, roughness/metallic have no visible effect and this check is N/A.*
  - **Normal/relief:** a normal-mapped wall shows shading change between two camera angles with the geometry fixed (screenshot pair) — proving the normal map, not geometry, drives it.
  - **Hard gate:** switch to **Solid**/**Classic** → original art, **pixel-identical** to today (frame-diff = 0).
- **Parallax:** on a `flags:pom` wall, a screenshot pair at two view angles shows grooves shift/deepen (the UV offset) without swimming; at grazing angles the clamp holds (no gaping holes / no garbage tiling) — a visible hole is the trigger to reconsider option (c), not to ship.
- **Coexistence:** a map mixing hero + derive + still-paletted materials renders all three correctly in one frame (the `usePBR` branch).
- **Memory/load:** loading E1M1's HD set stays within the §B bounds — no texture exceeds the resolution clamp (default 1024 px longest edge) and total material VRAM stays under the ceiling (default 768 MB); the load log prints per-material resolution + running MB and lists everything skipped/downscaled/dropped. No silent truncation.
- **Licence:** every shipped hero material traces to a CC0/free source in `assets/ultra/LICENSES`; no proprietary art — original or WAD-derived — committed to the repo (derive outputs resolve only to the gitignored `assets/ultra/derived/`).
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
