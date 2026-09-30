# Close-findings ledger, 2026-09-30

Working ledger for the open security items and the Vulkan findings that share
their subject. One row per finding. An empty `disposition` means not closed.

## Group A: the Vulkan backend against untrusted data and invalid state

Source items: DOOM-0093, DOOM-0221, DOOM-0311, DOOM-0390, DOOM-0224.

| finding | source | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|
| A1 tile HEIGHT from a texture or sprite header is unbounded; an image taller than the device limit is an invalid `vkCreateImage` | DOOM-0221 | yes. `tile_size` in `r_mesh.c` clamps width to `ATLAS_WIDTH` and height only to >= 1 | fixed | `r_mesh.c`, `atlas_bounds.h` | height only kept >= 1 → `AtlasClampTile` crops to `RB_ATLAS_MAX_TILE_H` (4096, the smallest image limit a device may report). Red: `atlas_bounds_test` against a stand-in clamping as before; and the `textall` fixture on the pre-fix game drew the validation message `extent.height (32767) exceeds the maximum image extent height (16384)`, none after | `make_wad_fixture.py textall`; DOOM-0042 HD path is unaffected (its images are not atlas tiles) |
| A2 material count is never compared with the device's sampled-image limits in `CreateDescriptors` | DOOM-0221 | yes. `binds[2].descriptorCount = matCount` with no limit read; `CreateHdSetLayout` does read them (DOOM-0410) | fixed | `r_vulkan.cpp` `CreateDescriptors` | no comparison → refuses with both numbers when textures + flats + sprites + 2 exceed the device's sampled-image limits. No red run: both GPUs to hand report over a million, so no real WAD reaches it | `CreateHdSetLayout` (the HD array's own clamp) |
| A3 atlas row total is summed in `int` and stored in `float` rects | DOOM-0221 | yes. `RB_BuildAtlas` pass 1, `y += shelf` | fixed | `r_mesh.c` `RB_BuildAtlas`, `atlas_bounds.h` | shelf rows summed in `int` with no ceiling → `AtlasRowsFit` refuses past `RB_ATLAS_MAX_ROWS` before the tile is counted. Red: `atlas_bounds_test`. The overflowing fixture was not run on the pre-fix game: its symptom is a multi-gigabyte allocation and a write before the buffer (no red run: the symptom is the harm) | the blitters' `(oy + y) * dstw` index, which the ceiling keeps inside `int` |
| A4 `tile_size` reads a sprite patch header without checking the lump holds one | found while verifying A1 | yes. `SHORT(p->width)` with no `W_LumpLength` check; `blit_tile` checks, `tile_size` does not | fixed | `r_mesh.c` `tile_size`, `patch_bounds.h` | header read unchecked → `PatchHasHeader`; a short lump is a 1x1 tile. Red: `patch_bounds_test` and `bounds_wiring_test` | L7, the two other readers of the same header |
| A5 width part of DOOM-0221 | DOOM-0221 | stale. Width is clamped in `tile_size`; patch reads are bounded by `patch_bounds.h` (DOOM-0228) | dismissed: already fixed | | | |
| A6 the weapon overlay pushes 24 of the layout's 31 push-constant floats | DOOM-0390, DOOM-0224 | yes. `RecordRtOverlay`: `float pcData[24]`, pushed as `24 * sizeof(float)`; layout is 31 | fixed | `r_vulkan.cpp` `RecordRtOverlay` | 24 of 31 floats pushed → all 31, the address and count lanes zeroed. No red run: the validation layer reports nothing for it on this driver, before or after | `mesh.frag`'s push block; DOOM-0224, the same finding filed twice |
| A7 the empty-map early return in `RB_Vulkan_BuildLevel` skips the four rebuild calls, one of which is the only place `g.rejectCPU` is cleared | DOOM-0390 | yes. `if (size == 0) return;` sits above `BuildAccelerationStructures`, `BuildEmitterList`, `BuildFogLightGrid`, `BuildProbes` | fixed | `r_vulkan.cpp` `RB_Vulkan_BuildLevel` | early return above the rebuild calls → frees the mesh and runs the teardown half of each before returning. No red run: needs a map with no drawable geometry and a previous level | `BuildProbes`, `BuildEmitterList`, `BuildFogLightGrid` each return after their teardown when there is no mesh |
| A8 no zero-extent guard on swapchain (re)creation | DOOM-0390; root of DOOM-0311's aspect and extent parts | yes. `CreateSwapchain` takes `caps.currentExtent` as is | fixed | `r_vulkan.cpp` `SurfaceHasArea`, `RB_Vulkan_Present` | recreate with whatever extent the surface reports → skip the frame while the surface has no area, and rebuild when it has. No red run here: X11 and Wayland do not report a zero extent on minimise. Not yet exercised on Windows, where it occurs | DOOM-0311's aspect and 1/extent parts, which had the same zero as their only source |
| A9 `devShotBuf` is destroyed only inside `if (g.rtEnabled)` | DOOM-0390 | yes. Created in `RB_Vulkan_Present` under `DOOM_DEV` with no `rtEnabled` test; destroyed in `RB_Vulkan_Shutdown` inside `if (g.rtEnabled)` | fixed | `r_vulkan.cpp` `RB_Vulkan_Shutdown` | destroyed inside `if (g.rtEnabled)` → destroyed whatever the GPU. No red run: needs a DEV build on a GPU without ray tracing |  |
| A10 the fog bake passes an unclamped static count to `ClusterStaticFogLights` | DOOM-0390 | yes, by a narrower path than reported. `g.emitCap` is the static count at level load plus `SPR_EMIT_MAX`, so the read is in range then. `BuildStaticEmitterSet` re-runs on a texture change and can grow `g.staticWgt` past that capacity; `BuildFogLightGrid` then reads `em[e*14]` past the mapped buffer. `BuildRasterPointLights` clamps to `g.emitCount`. To check with it: `RecordRtTrace` and `RB_RtVerify` hand the same unclamped count to the shader as `omniStart` | fixed | `r_vulkan.cpp` `ClusterStaticFogLights` | walked `staticN` records of the mapped emitter buffer → reads `g.staticEmit`, which always holds exactly the static set, and clamps to its size. No red run: needs a map whose static light set grows by more than `SPR_EMIT_MAX` after load | L2, the same growth reaching the shader |
| A11 exposure part of DOOM-0311 | DOOM-0311 | stale. `rb_exposure` is clamped to 0..15 where `spc.misc3[0]` is filled, so the value is always finite | dismissed: already bounded at the source | | | |
| A12 DOOM-0093's three axes: shader indices that rely on the CPU for their bound, non-finite values reaching stored state, acceleration-structure limits | DOOM-0093 | three cold lanes dispatched 2026-09-30 (shaders, `r_vulkan.cpp`, `r_mesh.c`); results not yet read | closed by rows L1 to L23 below |  |  |  |

### What the three cold lanes returned for A12

Lanes read `r_vulkan.cpp`, `r_mesh.c` with its headers, and the path-tracer
shaders, each whole, with no project context. Every row below was checked
against the source by this session before it got a disposition.

| finding | lane | verified | disposition |
|---|---|---|---|
| L1 static point-light cache survives a level change; `RefreshStaticPointLightLe` then indexes the new level's `g.staticEmit` with the old level's indices | vk | yes. `BuildEmitterList` clears `g.staticEmit` before `BuildStaticEmitterSet` swaps it into `prevStaticEmit`, so an emitter-less level compares equal and only the Le-refresh flag is set | fixed: `BuildEmitterList` now sets `g.staticLightsDirty`, so a new level always rebuilds the cache. No red run: needs two maps with equal subsector counts, the second with no static lights |
| L2 `omniStart` is pushed as `g.staticWgt.size()`, which can exceed `g.emitCount`; the shader's static search then reads past the emitter buffer | vk, shaders | yes. Same growth path as A10; `nee_merge_emitters` clamps the merged count to the cap, the push does not | fixed: `nee_omni_start` clamps the split to the records written, at both push sites. Red: `nee_sampling_test` and `bounds_wiring_test` |
| L3 `UploadSeepField` frees a field it calls unusable and then reads `f->originX`, `f->cell` | vk | yes. The free is on `!haveField`; the reads test `f` | fixed: the transform reads test `haveField`. No red run: needs a seep field with no cells |
| L4 the menu skull and logo images take their extent from a patch header with only a `<= 0` test | vk | yes. `M_DecodePatchRGBA` returns the header's width and height; nothing compares them with the device's image limit | fixed: both images are refused past `DeviceMaxImage2D()` and the menu falls back as it does for a failed decode. No red run: needs a replaced M_SKULL1 or M_DOOM larger than the device limit |
| L5 a BSP node may name itself or an ancestor as a child; `carve_caps` then recurses without end | mesh | yes. `P_LoadNodes` range-checks each child and nothing checks the nodes form a tree. The software renderer's own BSP walks loop the same way | fixed: `LevelBspIsTree` in `level_bounds.h`, called from `P_LoadNodes`. Red: `level_bounds_test`; and the `bspcycle` fixture hung the pre-fix game at full CPU until it was killed, and is refused by name after |
| L6 a level with no subsectors reads `subsectors[0]` | mesh | yes. `RB_BuildLevelMesh` uses subsector 0 as the root when there are no nodes, and the loader accepts an empty SSECTORS lump | fixed: `P_LoadSubsectors` refuses a map with none. Red: the `nosubsectors` fixture crashed the pre-fix game with a segmentation fault and is refused by name after |
| L7 `R_InitSpriteLumps` and `ensure_sprite_heights` read a patch header with no length check | mesh | yes. Same shape as A4 | fixed: `PatchHasHeader` in `ensure_sprite_heights` and `R_InitSpriteLumps`; a short lump is an empty sprite. Red: `bounds_wiring_test` |
| L8 shadow ray `tMax` can fall below `tMin` | shaders | yes. `occluded` passes `tMin 1e-3` and `tMax dist - 2e-3`; the only guard upstream is `dist2 < 1e-6` | fixed: `max(dist - 2e-3, 1e-3)`. No red run: the validation layer does not see inside a shader; `-rtverify` passes after |
| L9 wall-texture patches are composited into the atlas with no length checks; `M_DecodePatchRGBA` walks a patch the same way | found verifying A3 and L4 | yes. `R_RenderTextureToAtlas` follows `columnofs[]` and the post chain on the lump's own say-so | queued: same root cause as DOOM-0432, recorded on that item |
| L10 subsector, node-child, seg, sidedef, sector, flat and wall-texture indices used or emitted unchecked by the mesh builder | mesh, vk | no. `p_setup.c` refuses each at load (`P_WadIndex`, the subsector seg-range loop), and `R_FlatNumForName` / `R_TextureNumForName` refuse a bad name | dismissed: bounded at the loader |
| L11 a seg with a NULL front sector or NULL linedef | mesh | no. `P_LoadSegs` sets both from range-checked indices | dismissed: cannot occur |
| L12 sprite lump numbers and the sky texture number have no upper bound | mesh | no. Sprite lumps are numbered from the marker range itself; the sky texture comes from a name lookup that refuses a miss | dismissed: bounded by construction |
| L13 REJECT size comes from the sector count, not the lump; 32-bit products overflow above 46340 sectors | mesh, vk | no. `P_SetupLevel` allocates the full matrix and zero-fills a short lump (DOOM-0370). The level arrays live in the fixed zone heap, which refuses a sector count anywhere near that | dismissed: bounded at the loader |
| L14 PLAYPAL read as 768 bytes with no length check | mesh, vk | no. `D_DoomMain` refuses a short PLAYPAL with `PlayPalFits` | dismissed: bounded at startup |
| L15 `tri_ss` and subsector-to-sector ids reach the GPU with `-1` | vk | no harm. Each shader read is behind `subId < probeCount` or `hitSec < numSectors`, and `-1` fails both | dismissed: guarded in the shaders |
| L16 subsector count drives large GPU allocations | vk | bounded. The count is limited by what the zone heap can hold; a failed GPU allocation ends in a clean `I_Error` | dismissed: bounded, and fails cleanly |
| L17 fog march and sky-fog results stored with no finite guard | shaders | the inputs named (haze, torch list, seep field, fog floor) are host-built from finite map data, and `fogImg` is rewritten every frame with no history | dismissed: no non-finite source, no persistence |
| L18 zero normals reaching `normalize` | shaders | `emit_wall` drops a zero-length seg; caps carry a constant normal; the sky walls that can carry a zero normal are not shaded through that path | dismissed: cannot reach the normalise |
| L19 `mesh.frag` reads `triSs[gl_PrimitiveID]` for blob decals | shaders | no. Blobs draw with their own pipeline, not `mesh.frag` | dismissed: different shader |
| L20 verify-mode loops and the missing guard on the verify accumulator | shaders | deliberate. The host sets the sample count and counts non-finite texels (DOOM-0407) | dismissed: by design |
| L21 the sprite-emitter loop has no bound in the shader | shaders | bounded on the host by `SPR_EMIT_MAX` | dismissed: bounded on the host |
| L22 a primary ray visits every non-opaque triangle it crosses, so stacked masked walls set the cost per pixel | shaders | true as a cost. A map built for it can stall a frame long enough for the driver to reset the device | queued: needs a candidate cap and a frame-time measurement |
| L23 non-finite SH coefficients are counted after the bake and left in place | vk | no source. Each sample is guarded before it is summed and probe positions come from fixed-point map data | dismissed: the count is a tripwire with nothing to trip it |

Run-level fields for group A:

- **cited_by**: `omniStart` is named by `docs/standards/renderer.md` (the push
  constant table), the DOOM-0009 and DOOM-0011 specs and their research notes.
  `RecordRtOverlay` is named by the DOOM-0345 spec. Nothing in `docs/` names
  the bounds headers, the fixture generators' modes, or the atlas width.
- **swept**:
  - `docs/standards/renderer.md` `misc4` row: agrees. It names the slot, not
    how the value is derived.
  - DOOM-0011 spec INV-2 (fog scatters the static set only): agrees. The fog
    bake now reads the static records from RAM, the same set.
  - DOOM-0009 spec INV-8 (validation-clean): agrees; zero messages in Ultra
    and Solid on the fixed build.
  - DOOM-0345 spec's check that `RecordRtOverlay` holds no tone-map dispatch:
    agrees.
  - `docs/standards/security.md`, *GPU path*: agrees. It already asks for
    host-side bounds.
  - The comment above `ClusterStaticFogLights`: fixed, it named the mapped
    buffer as the source.
  - `CHANGELOG.md` `[Unreleased]`: fixed, entries added.
  - `scripts/make_map_fixture.py` and `make_wad_fixture.py` mode tables:
    fixed, the three new modes are listed.
  - `.claude/code-pairs.json`: absent, so no pair list was walked.
- **collateral**: none found.
- **surfaced**: none.
- **out_of_scope**: the profiler print in `RB_Vulkan_Present` computes
  `emitCount - staticWgt.size()` and can show a negative count in the same
  growth case; it is a printed number only. Spec and plan citations of the
  form `r_vulkan.cpp:NNNN` have moved again (DOOM-0472 already owns that).
- **falsified**: none.
- **Gates**, on the fixed build:
  - `make`: no warnings. `make test`: all tests pass, the two new test files
    present in the run.
  - Boot sweep of every map in both IWADs, and the five demo fixtures at
    their expected lengths (30 / 30 / 30 / 70 / 350).
  - Vulkan validation, Ultra ray-traced and Solid raster, private display:
    zero messages, layer confirmed loaded.
  - `-rtverify`: PASS (direct light 0.2058%, furnace exact, no non-finite
    texels) at the capture view used here, which is not the spec's gate view.
  - Screenshot comparison with the pre-fix build at the E1M1 start: Ultra's
    deterministic textured view is identical pixel for pixel. Solid raster
    differs by as many pixels between two runs of one build as between the
    two builds, so it shows no change and proves no identity.
  - Mutation probe over the four decision headers and their call sites:
    every mutant caught, after one test was strengthened (the BSP range
    check had been passing by reading a guard byte).
  - The pre-push hook runs the Windows syntax build.
  - Not run: anything on Windows itself (A8 is unexercised there); the
    overflowing-atlas fixture on the pre-fix build, for the reason in A3.

## Group B: DOOM-0250, the unbounded text-copy sites

The item named 28 `strcpy`/`strcat` sites. The tree holds fewer today; every
one still present is a row. `sprintf` was not in the item's scope.

| finding | verified | disposition | was → now |
|---|---|---|---|
| B1 `hu_stuff.c` `HU_Responder`: `strcpy(lastmessage, chat_macros[c])`. A macro read from the config file can be 97 characters; `lastmessage` holds `HU_MAXLINELENGTH+1` | yes. `M_LoadDefaults` reads `%99[^\n]` and strips two quotes | fixed | `strcpy` into `lastmessage` → `HU_CopyMessage(lastmessage, sizeof lastmessage, …)` from the new `hu_bounds.h`, at both copy sites. Red run: with a stand-in header copying as the game did, `tests/hu_bounds_test.cpp` failed on "a 97-character source writes nothing outside an 81-byte destination" and on the wiring checks |
| B2 same function: `c = c - '0'; if (c > 9) return false;` lets a key below `'0'` index `chat_macros[]` negatively | no. `c` is `unsigned char`, so a key below `'0'` wraps above 9 and the existing check refuses it | dismissed: not a defect (my own reading was wrong; the test author caught it) | |
| B3 `rb_materials.h` `rb_asset_set_exe_dir`: `strcpy(rb_asset_exe_root, cand)` | no. Both are 512 bytes and `cand` was filled by a checked `snprintf` | dismissed: bounded by construction | |
| B4 `m_misc.c` `M_LoadDefaults`: `strcpy(newstring, strparm+1)` | no. `newstring` is `malloc(len)` and the copy is `len-1` bytes with its terminator | dismissed: bounded by construction | |
| B5 `hu_stuff.c` `HU_Responder`: `strcpy(lastmessage, w_chat.l.l)` | no. Source and destination are both `HU_MAXLINELENGTH+1` | dismissed: bounded by construction | |
| B6 `g_game.c` `G_LoadGame`: `strcpy(savename, name)` | no. Every caller passes a name built from a fixed format and a slot digit; `savename` is 256 | dismissed: bounded by construction | |
| B7 `g_game.c` `G_SaveGame`: `strcpy(savedescription, description)` | no. Callers pass a `SAVESTRINGSIZE` (24) slot string that `M_ReadSaveStrings` terminates, or a literal; the destination is 32 | dismissed: bounded by construction | |
| B8 `m_menu.c`: copies between `savegamestrings[]` and `saveOldString`, and `EMPTYSTRING` into a slot | no. All are `SAVESTRINGSIZE`, terminated at read and capped at typing | dismissed: bounded by construction | |
| B9 `m_menu.c` `M_Drawer`: `strcpy(string, messageString+start)` into `string[40]` | no WAD or file path reaches it. `messageString` is compiled-in text, or that text with a 23-character save name inserted; no line reaches 40 | dismissed: not reachable from outside data | |
| B10 `d_main.c` `D_AddFile`: `strcpy(newfile, file)` | no. `newfile` is `malloc(strlen(file)+1)` | dismissed: bounded by construction | |
| B11 literal sources: `m_misc.c` `M_ScreenShot`, `g_game.c` `G_Ticker`, `wi_stuff.c` `WI_loadData`, `d_main.c` (four), `d_net.c` `GetPackets` | no. Each copies a string literal shorter than its destination | dismissed: literal shorter than the buffer | |
| B12 `i_sound.c` `I_InitSound`: `strcat(buffer, " -quiet")` after an unbounded `sprintf` of `$DOOMWADDIR` | not compiled. It sits under `#ifdef SNDSERV`, which `doomdef.h` leaves commented out | dismissed: dead code in this build | |
| B13 `w_wad.c`, `r_data.c`, `sndserv/wadread.c` sites the item listed | `w_wad.c` and `r_data.c` hold no `strcpy`/`strcat` today; `sndserv/` is built by nothing (project `CLAUDE.md`) | dismissed: already gone, or not built | |

Run-level fields for group B:

- **cited_by**: `lastmessage` and `HU_CopyMessage` are named by no document
  outside this ledger.
- **swept**: `docs/standards/security.md` trust-boundary table: agrees (it lists
  the config file's consumers by example and was not made false). `CHANGELOG.md`
  `[Unreleased]`: fixed, entry added. `.ants_review_falsepos.jsonl`: fixed, a
  correction appended, because two earlier blanket dismissals covered the site
  that turned out to be real.
- **collateral**: none.
- **surfaced**: none.
- **out_of_scope**: the `sprintf` sites were not in DOOM-0250 and were not
  re-triaged. `i_sound.c` under `SNDSERV` formats `$DOOMWADDIR` into a
  256-byte buffer unbounded; it is not compiled.
- **falsified**: none.
- **Gates**: `make` clean; `make test` green with `hu_bounds_test` present in
  the run. Mutation probe on the fix: off-by-one bound, missing terminator,
  cap 0, NULL source, either copy reverted to `strcpy`, and a wrong size
  argument were each caught. The verb could not parse this runner's output, so
  the verdicts were read from the exit status: the test binary's own failure,
  not a build failure. Not run: the Windows build and the demo fixtures; the
  change adds one header using `string.h` only.

## Group C: DOOM-0432, patch column offsets in the software blitters

Not started. The bullet asks for a choice between three designs first.
