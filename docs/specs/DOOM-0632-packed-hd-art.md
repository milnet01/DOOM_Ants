<!-- ants-spec-format: 1 -->
# DOOM-0632 — Ship Ultra's HD art packed for the graphics card, so a level loads without unpacking PNGs

**Status:** accepted (2026-11-19).
**Kind:** feature.
**Source:** ROADMAP DOOM-0632 (user requests 2026-11-19).
**Serves:** DOOM-0279 (frame cost: the art takes less video memory).
**Pairs with:** DOOM-0628 (streaming reads this file's levels one at a time), DOOM-0631 (level of
detail), DOOM-0042 (the HD material loader this changes), DOOM-0539 (model art takes the same
path).

**Layman:** Today every HD picture is stored as a PNG. Loading a level means unpacking each one,
and that is most of the wait. This stores the pictures in one file, already in the form the
graphics card uses, with their smaller copies included. A level then loads in a fraction of the
time, and the art takes about a fifth of the video memory. The PNGs stay in the project as the
originals; the download carries the packed file instead.

## Contents

1. Goal · 2. Where this sits · 3. Scope decisions · 4. Design · 5. Data & resources ·
6. Performance budget · 7. Build order · 8. Invariants · 9. Alternatives · 10. Open questions ·
11. What checks this · 12. Cross-doc impact · 13. Loop log

## 1. Goal

Ultra's HD load stops decoding PNGs. One packed file, `ultra.hdpack`, holds every committed HD
picture block-compressed for the GPU with its mip levels, and the loader copies those bytes
straight to the card. The PNGs remain the source the pack is baked from, and the loader's
fallback when a picture is not in the pack.

## 2. Where this sits

- **The loader.** `EnsureHdMaterials` in `r_vulkan.cpp` probes each map's PNG header
  (`rb_image_info`), applies the budget (`rb_apply_budget`, `kHdBudgetMB`), decodes what the budget
  kept (`rb_image_load`, then `rb_image_downscale_max` to `kHdMaxEdge`), and hands RGBA8 pixels to
  `BuildHdSet`. `UploadImageBatch` uploads level 0 and makes each further level with
  `vkCmdBlitImage`.
- **The pictures.** `kHdV1Maps` names seven kinds per material: `alb` and `emis` (sRGB colour),
  `nrm` (linear, three channels), and `rgh`, `met`, `ao`, `hgt` (one channel). Two overlays ride the
  same array: `overlays/grunge.png` (one channel) and `overlays/dirt.png` (sRGB colour). Every one
  is uploaded as four bytes a pixel today.
- **What the shaders read.** Colour maps as `.rgb`, normals as `.xyz`, the single-channel kinds and
  the grime overlay as `.r`; no read takes alpha. Checked with
  `grep -n "HD_TEX\(_W\)\?(" shaders/formulas/*.glsl shaders/pathtrace.comp shaders/mesh.frag`.
- **Measured, 2026-11-19** (roadmap DOOM-0632; release build at acf7510, private display, kit
  `Scratchpad/DOOM_Ants/d0628/run.sh`): E2M2 Ultra with files cached, 4.9 s of HD load, 4.3 s of it
  PNG decode and 0.5 s upload with mip blits. E1M1 cached, 2.2 s. E1M1 first read from the hard
  drive, 13.6 s. E2M2's set is 814.4 MB of video memory.
- **The download.** `packaging/ultra-art.sh` ships exactly the committed files under `assets/ultra`
  (`ultra_art_files`), checks each PNG against `assets/ultra/LICENSES`, and `check_ultra_art`
  verifies the staged folder. `derived/` is git-ignored and never ships.
- **Precedent.** `rb_model.h` (DOOM-0539) is the house pattern for a binary file read from
  `DOOMASSETDIR`: header-only, every count capped before it sizes anything, a test reaches it
  without the engine. This spec follows it.

## 3. Scope decisions

- **Pack first, then stream** (user, 2026-11-19): this is built before DOOM-0628, and shown with
  before-and-after shots and load times.
- **Fast compression** (user, 2026-11-19): "compression that is fast to compress/decompress."
  Taken as: a fast general compressor may sit on top of the packed bytes if it measures as a win
  (§ 4.5); nothing slow is put in the load path.
- **The quality check is the user's eye** on before-and-after shots (roadmap DOOM-0632).
- The rest are the author's calls, 2026-11-19, open to the user at B6:
  - **One pack file, not one file per picture.** A hard drive pays for every file opened; one file
    reads in order.
  - **PNG stays the source and the fallback.** The pack is baked from the PNGs and is never
    committed. A picture missing from the pack loads from its PNG exactly as today, so `derived/`
    art and a developer's fresh PNG keep working.
  - **The download carries the pack and not the PNGs.** Carrying both would roughly double the art.
  - **A card without block compression gets paletted art in Ultra from a download.** Every desktop
    card the 3D tiers target supports it; the case is logged, not silent.
  - **Normals stay three-channel.** The two-channel format is sharper but needs the shaders to
    rebuild the third channel, which would also change the PNG path's picture and spoil the
    before-and-after comparison. § 9.
  - **No shader changes at all.** Follows from the two lines above and § 2's channel reads.

## 4. Design

### 4.1 The file

`ultra.hdpack` sits at the asset root beside `materials.csv`. Little-endian, fixed-width fields,
no padding beyond what is listed.

**Header**, 32 bytes:

| Field | Type | Meaning |
|---|---|---|
| `magic` | 4 bytes | `DHDP` |
| `version` | u32 | 1 |
| `entryCount` | u32 | pictures in the pack |
| `mipCount` | u32 | records in the mip table |
| `nameBytes` | u32 | length of the name block |
| `reserved` | u32 | 0 |
| `fileBytes` | u64 | the whole file's length |

The reader refuses a file of 2 GiB or more, so every offset fits a 32-bit seek on Windows, and
a pack of more than 65536 entries.

Then, in this order: the entry table, the mip table, the name block, the data. The data begins
where the name block ends.

**Entry**, 32 bytes, one per picture, sorted by name (byte order) so a lookup is a binary search:

| Field | Type | Meaning |
|---|---|---|
| `nameOffset` | u32 | into the name block |
| `nameLen` | u16 | bytes, no terminator; 1 to 191, what the loader's path buffers hold |
| `format` | u8 | § 4.2 |
| `mipLevels` | u8 | levels stored, level 0 first |
| `width`, `height` | u16, u16 | level 0, in pixels |
| `firstMip` | u32 | index of level 0's record in the mip table |
| `srcBytes` | u64 | the source PNG's length when baked |
| `srcMtime` | i64 | the source PNG's modification time when baked, seconds |

The name is the path the loader already asks for, relative to the asset root with forward
slashes: `heroes/…/x_alb.png`, `models/SOUL_alb.png`, `overlays/grunge.png`.

**Mip record**, 16 bytes; a picture's levels are consecutive records from `firstMip`:

| Field | Type | Meaning |
|---|---|---|
| `offset` | u64 | from the start of the file |
| `storedBytes` | u32 | bytes on disk |
| `codec` | u8 | § 4.5 |
| `pad` | 3 bytes | 0 |

A level's unpacked size is never stored. The reader computes it from `format`, `width`, `height`
and the level number, and a `codec` 0 record whose `storedBytes` differs is refused.

**Each level is addressed on its own, and the reader assumes nothing about where levels sit in
the data.** That is the hook DOOM-0628 needs: it can read every picture's small levels first, and
the baker can later group them together, with no change to this format.

### 4.2 Formats

| `format` | Vulkan format | Bytes | Used for |
|---|---|---|---|
| 1 | `BC7_SRGB_BLOCK` | 16 per 4×4 block | `alb`, `emis`, `overlays/dirt.png` |
| 2 | `BC7_UNORM_BLOCK` | 16 per block | `nrm` |
| 3 | `BC4_UNORM_BLOCK` | 8 per block | `rgh`, `met`, `ao`, `hgt`, `overlays/grunge.png` |
| 4 | `R8_UNORM` | 1 per pixel | a single-channel map that block compression spoils |
| 5 | `R8G8B8A8_SRGB` | 4 per pixel | a colour map that block compression spoils |
| 6 | `R8G8B8A8_UNORM` | 4 per pixel | a linear map that block compression spoils |

A block format's level is `ceil(w/4) × ceil(h/4)` blocks. Formats 4 to 6 are the escape hatch:
the baker uses 1 to 3 by kind, and a per-file override list in the baker moves one picture to an
uncompressed format if the user's eye finds it damaged at B6.
The kind comes from the file name's
suffix, as `kHdV1Maps` spells them, or from the two overlay names. A picture whose name gives
neither is left out of the pack and loads from its PNG; every committed picture carries one.
The override list is `hdpack_overrides.csv` at the asset root, one `path,plain` row a picture.

A BC4 picture returns its value in red, which is the channel every single-channel read takes
(§ 2).

### 4.3 The baker

A host tool, `hdpack`, built by `make hdpack` into the build folder and never linked into the
game.

- **Input:** an asset root. It packs every PNG under it except `derived/`; a list file narrows it
  to named paths, which is how the release passes exactly the committed art.
- **Per picture:** decode with `rb_image_load`; refuse over `kHdSourceMaxEdge`; shrink with
  `rb_image_downscale_max` to `kHdMaxEdge`. Those are the loader's own steps, so level 0 is the
  picture the PNG path uploads.
- **Mips:** the full chain to 1×1, the count `UploadImageBatch`'s `levelsOf` gives. Each level is
  a 2×2 box filter of the one above; sRGB kinds are filtered in linear light.
- **Encode:** a vendored single-file encoder, compiled into the baker only (§ 5).
- **Incremental:** a picture whose `srcBytes`, `srcMtime` and chosen `format` match the existing
  pack's entry keeps its encoded bytes, so an override-list change re-encodes that picture. A
  rebake after one edit re-encodes one picture.
- **Deterministic:** the same files with the same modification times give the same bytes.
  Encoding may use several threads; the output may not depend on how many.
- **`hdpack --list <pack>`** prints each entry's name, format, size and levels, and exits non-zero
  on a file the reader would refuse.

The bake never runs at load. The game does not bake a missing pack.

### 4.4 The loader

A header-only reader, `rb_hdpack.h`, shared by the game, the baker and the tests.

- **Open.** Once per HD load, `EnsureHdMaterials` opens `ultra.hdpack` through `rb_asset_path` and
  validates the header and all three tables before any entry is used (INV-2). A file that fails
  is ignored whole, with one log line, and the load proceeds from PNGs.
- **Probe.** Where the loader calls `rb_image_info` on a map or an overlay today, it asks the pack
  first. An entry is used when all of these hold; otherwise that picture takes the PNG path, as
  today:
  - its `format` has the sRGB-ness the caller wants (`HdMapSpec::srgb`, or the overlay's);
  - the device can sample the format: 4 to 6 always, 1 to 3 only when the device reports
    `textureCompressionBC`. Device creation enables that feature where it is reported; today it
    requests it nowhere;
  - the loose PNG is absent, or its length and modification time equal `srcBytes` and `srcMtime`.
- **Budget.** A packed picture counts its exact unpacked bytes across its levels, in place of
  `mbOf`'s four-bytes-a-pixel estimate. `kHdBudgetMB` and `rb_apply_budget` are unchanged, so a
  level whose art was over budget keeps more of it.
- **Upload.** `HdSrc` and `BatchImage` gain a format and prebuilt levels. `UploadImageBatch`
  copies every level of such an image from staging and blits none; an image without prebuilt
  levels uploads and blits as today, so packed and PNG pictures share one set.
- **Read.** A `codec` 0 level is read from the file straight into the staging buffer. A
  compressed level is unpacked into ordinary memory first and then copied: a decompressor reads
  back what it has written, and the mapped staging buffer is write-combined memory (the DOOM-0170
  trap, noted in `r_vulkan.cpp`). Pictures are read in the order they sit in the file, not the
  budget's order, so a hard drive reads forward.
- **A level that fails to read or unpack** during the upload makes `UploadImageBatch`, and so
  `BuildHdSet`, report failure. `EnsureHdMaterials` then repeats the HD load with the pack ignored,
  as it retries today after refused video memory: each picture takes its PNG, and if there is
  none, the existing rule: no albedo means the material is paletted, any other map stays default.
  In a developer build `-hdpackfail` makes the first read report failure, to exercise this.
- **Logging.** The `HD load done` line gains the count of pictures from the pack and from PNGs.
  Its total counts each picture's uploaded bytes, where today it assumes four bytes a pixel.
  A stale entry (PNG present and different) is counted and logged once per load, not per picture.

### 4.5 The general compressor

Each level carries a `codec`: 0 stored as is, 1 LZ4 block, 2 Zstandard frame. The baker stores a
level compressed only when that is at least 5% smaller. The reader refuses a codec it was not
built with, and the unpacked length must equal the computed size exactly.

The baker emits codec 1, compressed with LZ4's high-compression mode, which the ordinary LZ4
unpacker reads. Codec 2 keeps its number and has no reader. § 10's rule chose LZ4 on these
figures, measured 2026-11-19 (roadmap DOOM-0632 holds the runs): E2M2's HD load from a cold hard
drive took 1.0 to 1.2 s with LZ4 against 1.4 to 1.7 s stored, and 0.14 s against 0.10 s with
the file cached. Zstandard was not built into the game: read and unpacked outside it, it was
slower than LZ4 both cold and cached. The AppImage and the Windows zip already
compress what they carry, so this affects the installed size and the hard-drive read, not the
download.

### 4.6 The download

`packaging/ultra-art.sh` changes in two places:

- `stage_ultra_art` stages the committed art to a scratch folder as today, with the `LICENSES`
  check unchanged, bakes it with a list file of exactly `ultra_art_files`' PNGs, and copies to the
  destination the pack plus every committed file that is not a PNG.
- `check_ultra_art` expects exactly those non-PNG files and `ultra.hdpack`, and compares
  `hdpack --list`'s names against the committed PNG list.

Both build scripts build `hdpack` for the build host before staging, and each staging bakes its
own pack; INV-9 makes the two identical. Both downloads carry LZ4's licence notice, which its
BSD licence requires of a binary; B5 adds it, and a release cut before B5 needs it added first.
`assets/ultra/LICENSES` still ships and still names each picture by its repository path; the
pack's entry name is that path less `assets/ultra/`.

In a source tree the pack is git-ignored. Neither `make` nor `make hdpack` bakes it; running
`hdpack` on `assets/ultra` does. Without it the game loads PNGs as today.

## 5. Data & resources

- **New file format:** `ultra.hdpack`, § 4.1. Version 1.
- **New vendored code, baker only:** `bc7enc.cpp` and `bc7enc.h` from
  `github.com/richgel999/bc7enc_rdo`, the BC7 encoder. They are offered under MIT or public
  domain, the taker's choice (the repository's `LICENSE`, and each file's own header; read
  2026-11-19), so they are GPL-compatible. `docs/standards/dependencies.md` records the commit
  and checksums. BC4 is encoded by the project's own `rb_bc4.h`: upstream's BC4 encoder sits in
  a second, larger source file, and a BC4 block is small enough to search directly.
- **New vendored code, game and baker:** `lz4.c` and `lz4.h` from LZ4 unpack a level in the
  game, the baker and the test. The baker adds `lz4hc.c` and `lz4hc.h` to compress. BSD
  2-Clause, so GPL-compatible, and a binary must carry its notice (§ 4.6).
  `docs/standards/dependencies.md` records the release and checksums. Vendored, so neither the
  Linux nor the Windows build needs a library staged.
- **Video memory.** The committed, shipped art is 538 PNGs holding 126.5 megapixels of colour and
  normal maps and 120.9 megapixels of single-channel maps. As RGBA8 with mips that is about
  1258 MiB; packed, about 238 MiB. Both are computed from the PNG headers, not measured on a card
  (`git ls-files assets/ultra`, less `heroes/bringup/`; kinds by file suffix; a 4/3 mip factor).
- **Disk.** Those 538 PNGs are 156.2 MiB. The pack baked from them on 2026-11-19 is
  249,397,058 bytes (237.8 MiB) with no general compression. That
  bake took 1 min 52 s on the development machine at low priority. With LZ4 (§ 4.5) the same
  art is 137,978,258 bytes (131.6 MiB), less than the PNGs.
- **Host memory at load:** one level's packed and unpacked bytes at a time when compressed; none
  beyond the staging buffer when stored as is.

## 6. Performance budget

Measured with the kit in § 2, release build, `render_scale_dynamic 0`, same machine and display
as the before figures:

- **E2M2 Ultra, files cached:** HD load at most 1.0 s (4.9 s today).
- **E1M1 Ultra, first read from the hard drive:** HD load at most 3.0 s (13.6 s today).
- **E2M2's HD set:** at most 260 MB, as the `HD load done` total reports it (814.4 MB today).
- **Frame time does not grow** in either view: `-frameledger` at E2M2's start, interleaved A/B,
  pack against PNGs, per `docs/standards/performance.md`.

The three limits are design targets, not measurements.

## 7. Build order

- **B1** `rb_hdpack.h` (reader and writer) and `tests/hd_pack_test.cpp`. Check: `make test`.
- **B2** The baker and `make hdpack`, with the encoder vendored. Check: it packs `assets/ultra`;
  `hdpack --list` names every committed PNG; two bakes of the same tree are byte-identical; a
  rebake after touching one PNG re-encodes one.
- **B3** LZ4 in the reader and the baker (§ 4.5). Check: `make test`; the rebaked pack's levels
  unpack to the stored pack's bytes; HD load times, stored against LZ4, cold and cached.
- **B4** The loader: probe, budget, upload. Check: the kit's times against § 6; a capture pair,
  pack against PNGs, through `scripts/ab_capture.sh`; `-hdallocfail` still falls back; `-hdpackfail`
  loads from PNGs; a run with the pack deleted matches today.
- **B5** The download: `ultra-art.sh`, both build scripts, `packaging/windows-smoke.sh`. Check:
  `release.sh`'s asset check passes and a staged AppImage loads E1M1 from the pack.
- **B6** Show the user before-and-after shots and the load times. Any picture they find damaged
  goes on the baker's override list (§ 4.2).

## 8. Invariants

- **INV-1** — A written pack reads back as what was written: every entry's name, format, size,
  level count and level bytes. Broken by a writer and reader that disagree on a field's width or
  order. *Test:* `tests/hd_pack_test.cpp`, a round trip over a pack holding each format and a
  non-multiple-of-four size.
- **INV-2** — The reader refuses, without reading out of bounds, a pack whose header or tables
  are wrong: bad magic, unknown version, `fileBytes` unequal to the real length, tables or names
  past the end, an entry with zero or over-`kHdMaxEdge` size, an unknown format, a level count
  that is not the full chain, a `firstMip` run past the mip table, a level whose bytes fall
  outside the data or overlap the tables, a `codec` 0 level of the wrong length, names out of
  order. Broken by any size taken from the file and used before it is checked. *Test:*
  `tests/hd_pack_test.cpp` builds one valid pack and one mutation per case; each must be refused
  and the valid one accepted. Run once under the address sanitiser by hand; `make test` builds without it.
- **INV-3** — A level that unpacks to any length but the computed one is refused, and the
  unpacker is never given room for more. Broken by trusting a compressed stream's own length.
  *Test:* `tests/hd_pack_test.cpp`, a level whose stream unpacks long and one that unpacks short.
- **INV-4** — An entry is used only when its PNG is absent or matches `srcBytes` and `srcMtime`;
  an edited PNG wins. Broken by a loader that prefers the pack on name alone. *Test:*
  `tests/hd_pack_test.cpp` on the decision function, with a matching, a longer, a re-dated and an
  absent file.
- **INV-5** — An entry whose format's sRGB-ness differs from what the caller wants is not used.
  Broken by looking up on name alone, which would show a colour map un-gamma'd. *Test:*
  `tests/hd_pack_test.cpp` on the decision function.
- **INV-6** — With no pack, the HD load is today's: the same pictures and sizes.
  Broken by a loader change that leaks into the PNG path. *Test:* a capture pair by
  `scripts/ab_capture.sh`, this build with the pack removed against the commit before B4.
- **INV-7** — A picture's level 0 in the pack has the size the PNG path uploads. Broken by a
  baker that clamps differently from `rb_image_downscale_max`. *Test:* `hdpack --list` sizes
  against `rb_image_fit_max` of each PNG's header size, in the B2 check script.
- **INV-8** — The download's pack holds every committed PNG `ultra_art_files` lists and nothing
  else. Broken by a bake that skips or adds a picture. *Test:*
  `check_ultra_art`, which `release.sh` runs on both downloads.
- **INV-9** — Baking the same files twice gives the same bytes. Broken by thread-order-dependent
  output or an uninitialised pad. *Test:* B2's double bake and `cmp`.
- **INV-10** — The § 6 budgets hold. Broken by a decode, a blit or a per-picture file open left in
  the packed path. *Test:* the timing kit, the `HD load done` total and `-frameledger`, by hand
  at B4.

## 9. Alternatives

- **One packed file per picture.** Simpler to rebuild one at a time, but a cold hard drive pays a
  seek per file, and the first-read time is the worst figure in § 2. The incremental bake gives the
  same convenience.
- **KTX2 or DDS files.** Standard containers, but each holds one picture, and reading them needs a
  library or a parser as long as this format's. One file with an index is what the hard drive and
  DOOM-0628 want.
- **A universal format transcoded at load** (Basis Universal). Works on every card, but puts a
  transcode back in the load path, which is the cost being removed.
- **Two-channel normals (BC5).** Better normals for the same bytes, but every normal read would
  rebuild the third channel, changing the PNG path too. Left as a later item; `format` has room.
- **Carrying the PNGs in the download as well.** A fallback for a card without block compression
  that no targeted card needs, at the price of the largest part of the download twice.
- **Baking on the player's machine at first run.** A long first launch, and an encoder shipped in
  the game. The bake is the build's job.
- **Trusting the pack without the PNG check.** One fewer file lookup per picture, but an edited
  PNG would silently show the old art in a source tree.

## 10. Open questions

- **Which general compressor: LZ4** (§ 4.5). The rule it was chosen by: the lowest HD load time
  cold from the hard drive, provided the cached time is within 0.1 s of stored and the cold time
  beats stored by 10%.
- **Whether `hgt` survives BC4.** Parallax steps would show as terracing. The user's eye at B6
  decides; format 4 is the remedy.
- **The download's size.** The packed art is smaller than the PNGs on disk (§ 5), but the
  AppImage and the zip compress what they carry, and PNGs barely compress further. Put to the
  user at B6 with the measured download sizes.
- **`derived/` art.** It loads from PNGs. Packing it on the player's machine is not in this spec.

## 11. What checks this

| Rule | What catches a breach |
|------|----------------------|
| INV-1 | `tests/hd_pack_test.cpp` |
| INV-2 | `tests/hd_pack_test.cpp` |
| INV-3 | `tests/hd_pack_test.cpp` |
| INV-4 | `tests/hd_pack_test.cpp` |
| INV-5 | `tests/hd_pack_test.cpp` |
| INV-6 | **nothing** automatic — `scripts/ab_capture.sh` by hand at B4 |
| INV-7 | **nothing** automatic — the B2 check script by hand |
| INV-8 | `check_ultra_art`, on every release |
| INV-9 | **nothing** automatic — the B2 double bake by hand |
| INV-10 | **nothing** automatic — the timing kit, the `HD load done` total and `-frameledger` by hand at B4 |
| Picture quality | **nothing** automatic — the user's eye at B6 |

Files this touches: `linuxdoom-1.10/r_vulkan.cpp`, a new `linuxdoom-1.10/rb_hdpack.h`, the new
baker source, the vendored `lz4.c`, `lz4.h`, `lz4hc.c` and `lz4hc.h`, `linuxdoom-1.10/Makefile`, `linuxdoom-1.10/tests/hd_pack_test.cpp`,
`packaging/ultra-art.sh`, `packaging/build-appimage.sh`, `packaging/windows-build.sh`,
`packaging/windows-smoke.sh`, `assets/ultra/.gitignore`.

## 12. Cross-doc impact

- `docs/standards/security.md` § The trust boundaries: a row for `ultra.hdpack`, parsed in
  `rb_hdpack.h`.
- `docs/standards/dependencies.md` § Where this project's dependencies live: the encoder, and the
  compressor if one is adopted.
- `docs/standards/assets.md` § The sidecar / derived pattern: the pack is a derived, ignored file.
- `docs/RELEASE_README.txt`: the art folder holds a pack, not PNGs.
- `docs/specs/DOOM-0042-ultra-hd-pbr-materials.md`: the loader takes a picture from the pack
  first.
- `CHANGELOG.md` on shipping.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0632-packed-hd-art-loop-log.md`.
