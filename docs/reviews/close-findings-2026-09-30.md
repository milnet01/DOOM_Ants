# Close-findings ledger, 2026-09-30

Working ledger for the open security items and the Vulkan findings that share
their subject. One row per finding. An empty `disposition` means not closed.

## Group A: the Vulkan backend against untrusted data and invalid state

Source items: DOOM-0093, DOOM-0221, DOOM-0311, DOOM-0390, DOOM-0224.

| finding | source | verified | disposition | files | was → now | must_agree |
|---|---|---|---|---|---|---|
| A1 tile HEIGHT from a texture or sprite header is unbounded; an image taller than the device limit is an invalid `vkCreateImage` | DOOM-0221 | yes. `tile_size` in `r_mesh.c` clamps width to `ATLAS_WIDTH` and height only to >= 1 | | | | |
| A2 material count is never compared with the device's sampled-image limits in `CreateDescriptors` | DOOM-0221 | yes. `binds[2].descriptorCount = matCount` with no limit read; `CreateHdSetLayout` does read them (DOOM-0410) | | | | |
| A3 atlas row total is summed in `int` and stored in `float` rects | DOOM-0221 | yes. `RB_BuildAtlas` pass 1, `y += shelf` | | | | |
| A4 `tile_size` reads a sprite patch header without checking the lump holds one | found while verifying A1 | yes. `SHORT(p->width)` with no `W_LumpLength` check; `blit_tile` checks, `tile_size` does not | | | | |
| A5 width part of DOOM-0221 | DOOM-0221 | stale. Width is clamped in `tile_size`; patch reads are bounded by `patch_bounds.h` (DOOM-0228) | dismissed: already fixed | | | |
| A6 the weapon overlay pushes 24 of the layout's 31 push-constant floats | DOOM-0390, DOOM-0224 | yes. `RecordRtOverlay`: `float pcData[24]`, pushed as `24 * sizeof(float)`; layout is 31 | | | | |
| A7 the empty-map early return in `RB_Vulkan_BuildLevel` skips the four rebuild calls, one of which is the only place `g.rejectCPU` is cleared | DOOM-0390 | yes. `if (size == 0) return;` sits above `BuildAccelerationStructures`, `BuildEmitterList`, `BuildFogLightGrid`, `BuildProbes` | | | | |
| A8 no zero-extent guard on swapchain (re)creation | DOOM-0390; root of DOOM-0311's aspect and extent parts | yes. `CreateSwapchain` takes `caps.currentExtent` as is | | | | |
| A9 `devShotBuf` is destroyed only inside `if (g.rtEnabled)` | DOOM-0390 | yes. Created in `RB_Vulkan_Present` under `DOOM_DEV` with no `rtEnabled` test; destroyed in `RB_Vulkan_Shutdown` inside `if (g.rtEnabled)` | | | | |
| A10 the fog bake passes an unclamped static count to `ClusterStaticFogLights` | DOOM-0390 | yes, by a narrower path than reported. `g.emitCap` is the static count at level load plus `SPR_EMIT_MAX`, so the read is in range then. `BuildStaticEmitterSet` re-runs on a texture change and can grow `g.staticWgt` past that capacity; `BuildFogLightGrid` then reads `em[e*14]` past the mapped buffer. `BuildRasterPointLights` clamps to `g.emitCount`. To check with it: `RecordRtTrace` and `RB_RtVerify` hand the same unclamped count to the shader as `omniStart` | | | | |
| A11 exposure part of DOOM-0311 | DOOM-0311 | stale. `rb_exposure` is clamped to 0..15 where `spc.misc3[0]` is filled, so the value is always finite | dismissed: already bounded at the source | | | |
| A12 DOOM-0093's three axes: shader indices that rely on the CPU for their bound, non-finite values reaching stored state, acceleration-structure limits | DOOM-0093 | three cold lanes dispatched 2026-09-30 (shaders, `r_vulkan.cpp`, `r_mesh.c`); results not yet read | | | | |

Run-level fields: not yet produced. No fix has landed.

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
