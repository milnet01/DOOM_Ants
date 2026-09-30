# DOOM-0432 — Validate a patch lump once; every reader asks

**Status:** accepted.
**Kind:** security.
**Source:** ROADMAP DOOM-0432 (`review-code 2026-09-01`, lane sw-renderer; split
out of DOOM-0402 on 2026-09-12; scope widened 2026-09-30 by the DOOM-0093 pass,
row L9 of `docs/reviews/close-findings-2026-09-30.md`).

**Layman:** The game draws pictures out of the game-data file while trusting the
file's own description of where each piece of the picture is. A crafted file can
point that anywhere. After this, each picture is checked once, and a bad one is
simply not drawn.

**Depends on:**

- **DOOM-0228** — `patch_bounds.h`, the per-read decisions the Vulkan atlas
  builder already uses for flats and sprites. This spec adds a whole-lump
  decision beside them.
- **DOOM-0447** — `V_BlitPatch`, the one patch blitter the five `V_DrawPatch*`
  wrappers call. The roadmap item predates it and speaks of three blitters.
- **DOOM-0402** — `R_GenerateLookup`'s treatment of a column no patch covers,
  which this spec leans on (§4.4).

**Delivers / subsumes:** DOOM-0432.

**Defers (explicitly NOT in this build):**

- A reader that takes only a patch's header: a caller that reads `width` to
  centre a picture, the atlas tile size, the sprite-height cache. A header read
  reaches at most eight bytes, and the last two already test the lump's length
  (DOOM-0228). `R_InitSpriteLumps` is the one header reader in this build.
- `R_DrawColumn`'s read past a short column (§10 Q2).

## Contents

1. Goal
2. Where this sits
3. Scope decisions
4. Design
5. Data & resources
6. Performance budget
7. Build order
8. Invariants
9. Alternatives considered (and rejected)
10. Open questions
11. What checks this
12. Cross-doc impact
13. Cold-eyes loop log

## 1. Goal

No reader of patch column data reads outside the lump the patch came from,
whatever the lump says about itself. A malformed patch is not drawn and does not
end the game.

## 2. Where this sits

A patch is DOOM's picture format: a header (`width`, `height`, `leftoffset`,
`topoffset`), a table of one 32-bit offset per column (`columnofs[]`), and for
each column a chain of posts. A post is `topdelta`, `length`, a pad byte,
`length` texels and a pad byte; a `topdelta` of `0xff` ends the chain.
`patch_t` and `column_t` in `r_defs.h` are the structures.

Every offset and every length is the lump's own. These functions follow them
with no knowledge of how long the lump is:

| Reader | File | What it holds |
|---|---|---|
| `V_BlitPatch` | `v_video.c` | a `patch_t*` only |
| `F_DrawPatchCol` | `f_finale.c` | a `patch_t*` and a column number from its caller |
| `M_DecodePatchRGBA` | `m_menu.c` | the lump's name |
| `R_GenerateLookup` | `r_data.c` | the lump number (`texpatch_t.patch`) |
| `R_GenerateComposite`, through `R_DrawColumnInCache` | `r_data.c` | the lump number |
| `R_RenderTextureToAtlas` | `r_data.c` | the lump number |
| `R_DrawVisSprite`, through `R_DrawMaskedColumn` | `r_things.c` | the lump number (`vis->patch + firstspritelump`) |

`V_BlitPatch` and `F_DrawPatchCol` check each post against the patch's declared
height with `V_PostInBounds` (DOOM-0254). That bounds where they **write**. It
says nothing about where they **read**.

One more reader takes no lump at all. `R_RenderMaskedSegRange` (`r_segs.c`)
walks the posts of a see-through wall texture from the pointer `R_GetColumn`
returns. For a column one patch covers, that pointer is the lump plus an offset
`R_GenerateLookup` stored earlier, in a table of 16-bit entries. For any other
column the pointer is into the texture's composite, which holds raw texels and
no posts. §4.5 deals with both.

`blit_tile` in `r_mesh.c` copies a sprite into the Vulkan tiers' atlas. It
bounds every read it makes (DOOM-0228) and skips a column that does not fit,
so today it draws the rest of a picture the other readers would refuse.

`W_CacheLumpNum` loads a lump into a zone block whose `user` field is the
address of that lump's slot in `lumpcache[]` (`Z_Malloc` stores the `user` it is
given, and `W_CacheLumpNum` passes `&lumpcache[lump]`). So a pointer returned by
`W_CacheLumpNum` leads back to its lump number in constant time, through the
block header in front of it. The roadmap item assumed that lookup was a scan of
every lump; it is not.

## 3. Scope decisions

Taken in this spec; none was put to the user.

- **Validate once per lump, not once per read.** §9 has the alternatives.
- **A bad patch is refused whole.** No reader draws part of it.
- **Refusal is never fatal.** A PWAD with one broken picture still runs.
- **A see-through wall draws only the columns one patch covers.** §4.5. No
  two-sided line in either IWAD uses a middle texture with any other kind of
  column (measured 2026-09-30 over every map in both files), so stock maps
  draw as before.
- **The rule accepts every patch in both IWADs.** Measured 2026-09-30 with a
  Python transcription of §4.1's rule over `wads/doom.wad` and `wads/doom2.wad`
  (every lump between `S_START` and `S_END`, every lump `PNAMES` names, and the
  status-bar, menu, intermission and finale pictures by name prefix): no lump
  refused in either file. The script is scratch and is not shipped; INV-2 is
  what holds this after the build.

## 4. Design

### 4.1 The rule: `PatchLumpValid`

A pure function in `patch_bounds.h`:

```c
static inline int PatchLumpValid (const unsigned char* data, int len);
```

It returns 1 when all of these hold, and 0 otherwise. It reads multi-byte
fields little-endian, byte by byte, so it needs no `m_swap.h` and can be held
against byte arrays in a test.

1. `len >= 8`.
2. `width >= 1` and `height >= 1`.
3. `width <= (len - 8) / 4`, so the whole column table is inside the lump.
4. For each column, walking from its table offset `pos`:
   - `0 <= pos <= len - 1`, so the byte at `pos` can be read;
   - if that byte is `0xff` the column ends;
   - otherwise `pos + 1 <= len - 1`, so `length` can be read, and
     `pos + length + 4 <= len - 1`, so the texels, both pad bytes and the next
     post's first byte are all inside the lump;
   - then `pos` advances by `length + 4`.

`pos` strictly increases, so the walk ends. A column whose terminator is the
lump's last byte is valid: stock patches end that way.

The rule does not compare a post with the patch's declared height. That is a
question about the destination, and each reader already answers it for its own
destination.

### 4.2 The verdict: `W_PatchLumpOk` and `W_PatchOk`

In `w_wad.c`, declared in `w_wad.h`:

```c
boolean W_PatchLumpOk (int lump);         // by lump number
boolean W_PatchOk (const void* patch);    // by a pointer W_CacheLump* returned
```

A byte per lump holds the verdict: not yet asked, good, or refused. It is
allocated beside `lumpcache` and starts as not yet asked.

- `W_PatchLumpOk` returns the stored verdict if there is one. Otherwise it runs
  `PatchLumpValid` with `W_LumpLength`, stores the verdict and returns it. When
  `lumpcache[lump]` already holds the lump it validates those bytes where they
  are; only a lump not yet in memory is loaded, as `PU_CACHE`. An out-of-range
  lump number is refused.
- `W_PatchOk` finds the lump behind the pointer (§4.3) and returns the stored
  verdict if there is one. Otherwise it runs `PatchLumpValid` over the bytes it
  was handed, with that lump's `W_LumpLength`, and stores the verdict. It does
  not call `W_PatchLumpOk`. A pointer that is not the start of a cached lump is
  refused. `NULL` is refused.
- **Neither function changes the tag of a block that is already cached, and
  `W_PatchOk` allocates nothing.** `W_CacheLumpNum` on a cached lump calls
  `Z_ChangeTag`, so asking through it would turn a picture its holder cached as
  `PU_STATIC` or `PU_LEVEL` into a purgeable one on its first draw.
- The first refusal of a lump prints one line that begins
  `W_Patch: refusing lump ` and then names the lump. Later asks are silent,
  because the verdict is stored.
- A lump too short to hold a patch header is refused without a line, and so is
  an out-of-range lump number. PWADs carry empty marker lumps among their
  sprites, and those are not malformed pictures.
- `W_Reload` resets the verdict of every lump it reloads.

### 4.3 From pointer to lump

Two small pieces, so neither module reaches into the other's structures:

- `z_zone.c` gains `void** Z_BlockUser (const void* ptr)`: the `user` field of
  the zone block that starts at `ptr`, or `NULL` when the header in front of
  `ptr` does not carry the zone id.
- `wad_bounds.h` gains a pure function that turns that `user` into a lump index.
  It takes the `user`, the cache array, the lump count and the patch pointer.
  It returns the index when `user` lies inside `lumpcache[0 .. numlumps)` on a
  slot boundary and that slot's current value is the pointer, and -1 otherwise.

An unowned zone block carries the sentinel `(void *)2` as its `user`
(`Z_Malloc`); that is outside `lumpcache[]` and is refused.

Every patch the readers in §2 receive today comes from `W_CacheLumpNum` or
`W_CacheLumpName`. `Z_BlockUser` reads the header in front of the pointer, so a
patch pointer that is not a zone block start is outside this design; the engine
creates none. A pointer kept after its block was purged is refused: `Z_Free`
clears the block's id and its cache slot.

### 4.4 What each reader does

| Reader | Asks | On a refused patch |
|---|---|---|
| `V_BlitPatch` | `W_PatchOk(patch)`, before it reads `topoffset` | returns without drawing, for every wrapper including the flipped one |
| `F_DrawPatchCol` | `W_PatchOk(patch)`, and `0 <= col < width` | returns without drawing |
| `M_DecodePatchRGBA` | `W_PatchLumpOk` on the lump it names | returns `NULL`; its callers already fall back |
| `blit_tile` (sprite branch) | `W_PatchLumpOk` on the sprite lump | returns; the tile stays transparent. Its per-read bounds stay |
| `R_GenerateLookup`, `R_GenerateComposite`, `R_RenderTextureToAtlas` | one shared helper over `W_PatchLumpOk(patch->patch)` | skip that patch, as if the texture did not list it |
| `R_DrawVisSprite` | `W_PatchLumpOk(vis->patch + firstspritelump)` | returns without drawing |
| `R_InitSpriteLumps` | `W_PatchLumpOk` on each sprite lump, in place of its own header-length test | records zero width and offsets |

The three texture readers must agree. `R_GenerateLookup` records, per column,
either the one lump that covers it or that the column is composited. If it
counted a patch that `R_GenerateComposite` then skipped, `R_GetColumn` would
hand out a pointer into the refused lump. One helper, called by all three, is
what makes that impossible.

A texture column whose only patch is refused becomes a column no patch covers.
`R_GenerateLookup` already gives such a column a slot in the composite
(DOOM-0402), so nothing new is needed there.

`M_DecodePatchRGBA` caches the patch and then `PLAYPAL`, both purgeable, so the
second load can free the first and the walk then reads a freed block. It
caches `PLAYPAL` first instead, as `blit_tile` does (DOOM-0406).

`F_DrawPatchCol` needs the column check as well as the verdict: its caller
passes a screen column, and a replacement picture narrower than the screen
would otherwise index `columnofs[]` past the columns the rule validated.

`R_InitSpriteLumps` asking at startup is what makes a refused sprite visible in
the boot log on every run, not only when that sprite is first drawn.

### 4.5 The see-through wall walk

`R_GenerateLookup` stores, for a column one patch covers, that column's offset
into the lump plus three. `texturecolumnofs` holds it in an `unsigned short`.
A wall patch longer than that range can pass §4.1 with every offset inside the
lump and still have the stored offset cut to 16 bits. `R_GetColumn` then hands
out a pointer to the middle of the lump, and `R_RenderMaskedSegRange` walks it
as a post chain the rule never looked at.

`texturecolumnofs` and the two `colofs` locals that write and read it become
`unsigned int`, and its allocation in `R_InitTextures` is sized to match. The
stored offset is then exact, so the walk starts at a post the rule validated.
The composite's own size limit is unchanged.

A column that is not covered by exactly one patch has no posts to walk. That
includes a column whose only patch was refused, so without this a refused
patch in a see-through wall would trade one crash for another. `r_data.c`
gains:

```c
byte* R_GetPostColumn (int tex, int col);
```

It returns the start of the column's post chain when one patch covers the
column, and `NULL` otherwise. `R_RenderMaskedSegRange` calls it in place of
`R_GetColumn` and draws nothing for `NULL`.

No stock wall patch is near the limit: the largest is `WALL24_1` in `doom.wad`
and `RSKY2` in `doom2.wad`, each under 40 000 bytes. A refusal by size was the
alternative and is rejected in §9.

## 5. Data & resources

One byte per lump for the verdict, and two more bytes per texture column for
the wider offset table. No file format, config key or command-line
flag changes. No new dependency.

## 6. Performance budget

After a lump's first ask, a verdict costs one array read for `W_PatchLumpOk`,
and for `W_PatchOk` one block-header read, a range test and that array read.
Neither loops, allocates or touches the lump's data again.

The first ask walks the lump once. `R_InitSpriteLumps` and `R_GenerateLookup`
already cache every sprite and every wall patch at startup, so the walk adds a
pass over bytes that were just read.

No frame-time figure is set. The check is that the draw path contains no loop
over patch data that was not there before (INV-3's scrape reads the same
functions), and that the demo fixtures still run their recorded lengths.

## 7. Build order

Each step ends with `make` and `make test` green.

- **B1 — the rule.** `PatchLumpValid` in `patch_bounds.h`, with its cases in
  `tests/patch_bounds_test.cpp`. *Verify:* INV-1's cases pass, and fail against
  a stand-in that returns 1 for everything.
- **B2 — the verdict.** `Z_BlockUser`, the `wad_bounds.h` index function,
  `W_PatchLumpOk`, `W_PatchOk`, the verdict bytes and the `W_Reload` reset.
  *Verify:* INV-4's unit cases pass; the boot sweep of every map in both IWADs
  is unchanged.
- **B3 — interface readers.** `V_BlitPatch`, `F_DrawPatchCol`,
  `M_DecodePatchRGBA`. *Verify:* the `badpatch-ui` fixture (INV-5) on the
  pre-change and post-change builds.
- **B4 — texture readers.** The shared helper and its three callers, and the
  wider offset table and `R_GetPostColumn` of §4.5.
  *Verify:* the `badpatch-wall` fixture in Solid; the five demo fixtures at
  their recorded lengths.
- **B5 — sprites.** `R_DrawVisSprite`, `R_InitSpriteLumps` and `blit_tile`.
  *Verify:* the `badpatch-sprite` fixture, in Classic and in Solid.
- **B6 — the wiring test and the sweep.** INV-3's scrape in
  `tests/bounds_wiring_test.cpp`; the boot sweep with its output searched for a
  refusal line (INV-2); the cross-doc edits in §12.

## 8. Invariants

- **INV-1** — `PatchLumpValid` returns 1 exactly when every byte §4.1 lists lies
  inside the lump. *Breaks when:* the terminator test asks for two readable
  bytes, which refuses a stock patch whose last byte is its terminator; or a
  post's trailing pad and the next post's first byte are left out of the bound,
  which accepts a post whose successor is past the end.
  *Test:* `tests/patch_bounds_test.cpp` builds byte arrays and expects: a
  one-column patch whose terminator is the final byte is accepted; the same
  array one byte shorter is refused; a column offset equal to `len` is refused;
  a negative column offset is refused; a post whose `length` puts the next
  post's first byte one past the end is refused and one byte shorter is
  accepted; `width` one more than the table holds is refused; `width` 0,
  `height` 0 and `len` 7 are each refused.

- **INV-2** — no lump in `doom.wad` or `doom2.wad` is refused. *Breaks when:*
  the rule is tightened past what the format's own tools write, or a reader
  starts asking about a lump that is not a patch.
  *Test:* the boot sweep of every map in both IWADs prints no refusal line.
  That covers every sprite and every wall patch, which are asked at startup.
  Interface pictures are asked when first drawn and the sweep draws few of
  them; §11 names that gap.

- **INV-3** — every function that reads a patch lump's `columnofs` or walks
  its posts from the lump asks for the verdict before its first such read, and
  so does `R_InitSpriteLumps` before it reads the header. *Breaks when:* a new reader is
  added, or one of the readers in §2 is changed to cache a lump by name and
  walk it directly.
  *Test:* `tests/bounds_wiring_test.cpp` takes each function named in §4.4 and
  requires a call to `W_PatchOk` or `W_PatchLumpOk` (for the three texture
  readers, their shared helper) ahead of the first `->columnofs[` in its body.
  `R_InitSpriteLumps` reads only the header, so there the call must come ahead
  of its first read of `->width`. It
  also requires that every use of `->columnofs[` in the engine sources sits in
  one of those functions, so a reader added elsewhere fails the test by
  existing. The engine sources are every `.c`, `.cpp` and `.h` file directly
  in `linuxdoom-1.10/`, found by listing the directory, not from a fixed list.

- **INV-4** — `W_PatchOk` is true only for the start of a cached lump whose
  verdict is good. *Breaks when:* the index function accepts a `user` outside
  `lumpcache[]`, such as the unowned-block sentinel, or one that is inside the
  array but not on a slot boundary.
  It also breaks when the slot is not compared with the pointer, which accepts
  a stale pointer whose lump has since been loaded somewhere else; or when
  `W_PatchOk` asks through the cache and demotes the block's tag.
  *Test:* `tests/wad_bounds_test.cpp` builds a small array of pointers and calls
  the index function with a `user` below the array, at its first slot, at its
  last slot, one slot past the end, one byte into a slot, and `(void *)2`. It
  expects an index for the two real slots and -1 for the rest, and -1 for a
  real slot whose value is a different pointer. The wiring scrape requires that
  `W_PatchOk`'s body names neither `W_CacheLump` nor `Z_ChangeTag`, and that
  `W_PatchLumpOk`'s cache call sits under a test of `lumpcache[`.

- **INV-5** — a refused patch is drawn by no reader and does not end the game.
  *Breaks when:* a reader turns a refusal into `I_Error`, or a reader is left
  out of §4.4.
  *Test:* three modes of `scripts/make_wad_fixture.py`, each replacing one lump
  with a copy whose first column offset is far past the end of the lump:
  `badpatch-ui` (`STBAR`), `badpatch-sprite` (`PISGA0`, the pistol's first
  weapon frame, carried with every other sprite between the PWAD's own
  markers) and `badpatch-wall` (the first patch `PNAMES` lists, replaced by
  name). On the pre-change build each ends in a segmentation fault; all three
  were run and did, on 2026-09-30, against `doom.wad`. On the post-change build
  each boots the map, prints one refusal line naming the lump, and exits 0
  under `-bootsmoke`. The rule each fixture isolates is the column-offset bound
  of §4.1: the header and the table are intact, so nothing earlier refuses it.
  `badpatch-wall` is run in Solid, where the atlas builder reads every texture
  at level load; in Classic a wall patch is read only when its texture is
  drawn. `badpatch-sprite` is run in Solid as well as Classic, for `blit_tile`.
  No fixture puts a refused patch in a see-through wall; INV-10 covers that
  path by reading.

- **INV-6** — `R_GenerateLookup`, `R_GenerateComposite` and
  `R_RenderTextureToAtlas` use or skip exactly the same patches of a texture.
  *Breaks when:* one of them asks the verdict in its own words, or not at all.
  *Test:* the wiring scrape of INV-3 requires the same helper name in all
  three bodies.

- **INV-7** — a reloaded lump loses its verdict. *Breaks when:* `W_Reload`
  frees and re-reads a lump and leaves "good" standing for bytes that changed.
  *Test:* the wiring scrape requires `W_Reload`'s per-lump loop to reset the
  verdict. There is nothing to run: `W_Reload` needs the `-wart` development
  path and a file that changes between two loads.

- **INV-8** — `F_DrawPatchCol` reads `columnofs[col]` only for
  `0 <= col < width`. *Breaks when:* the verdict is asked and the column is
  not, so a valid but narrow replacement picture is indexed past its table.
  *Test:* the wiring scrape requires a comparison of `col` with the patch's
  width ahead of the `columnofs` read in that body. The scene that calls it is
  the end of the third episode and is not reachable from a boot fixture.

- **INV-9** — the offset `R_GenerateLookup` stores for a single-patch column
  is the offset `R_GetColumn` reads back. *Breaks when:* the table's entries
  are narrower than a lump offset, which truncates the offset of a large patch.
  *Test:* the wiring scrape requires that the declaration of
  `texturecolumnofs` and both `colofs` locals are not `short`, and that the
  table's allocation in `R_InitTextures` multiplies by a `sizeof`. There is
  nothing to run without a wall patch over 64 KB in a masked texture; no
  fixture builds one.

- **INV-10** — `R_RenderMaskedSegRange` walks posts only in a column one patch
  covers. *Breaks when:* it takes `R_GetColumn`'s pointer for a composite
  column and walks raw texels, or heap bytes no patch wrote, as posts.
  *Test:* the wiring scrape requires `R_RenderMaskedSegRange`'s body to name
  `R_GetPostColumn` and not `R_GetColumn`, and `R_GetPostColumn`'s body to
  return `NULL` under a test of `texturecolumnlump`. Nothing runs it: no stock
  map has such a wall, and no fixture builds one.

## 9. Alternatives considered (and rejected)

- **Check every read where it happens.** What `blit_tile` does. It needs the
  lump length at each reader, which `V_BlitPatch` and `F_DrawPatchCol` do not
  have, and it puts comparisons inside the sprite and status-bar draw loops on
  every frame. It also leaves each reader to get the same rule right alone;
  the blitters had already drifted once (DOOM-0447).
- **A checked cache call that every caller must switch to.** The roadmap item's
  first option. Callers keep their signature, but a caller that goes on using
  `W_CacheLumpName` is unprotected and looks the same as one that switched.
  This design reaches the same once-per-lump cost without depending on which
  call the caller made.
- **Find the lump by scanning `lumpcache[]`.** The roadmap item's third option.
  Linear in the lump count on every draw; DOOM-0427 files one such scan as a
  defect. The block header makes the scan unnecessary.
- **Bound reads by the zone block's size.** Also constant-time, and needs no
  verdict store. But a block can be larger than its lump by the allocator's
  rounding and an absorbed fragment, so the bound would let a reader run past
  the lump into stale heap bytes, and the walk would be repeated on every draw.
- **Refuse a wall patch too long for the 16-bit offset table.** Closes §4.5's
  hole without touching the table. But the Vulkan tiers read `columnofs`
  directly and draw such a patch correctly today, and the three texture readers
  must agree (INV-6), so the refusal would take a working picture away from
  Solid and Ultra.
- **Refuse the WAD.** Simplest, and wrong for the player: PWADs carry odd lumps
  between the markers, and one broken picture should not stop a map loading.

## 10. Open questions

- **Q1 — closed.** The composite column walked as posts is now §4.5 and
  INV-10.
- **Q2 — `R_DrawColumn` reads past a short column.** It samples its source
  through a mask wider than many columns are tall, so it can read a little
  past the texels of a valid patch. Stock data does this. Out of scope; it
  needs a decision about how a short texture should tile, not a bound.
- **Q3 — other IWADs.** The continuous-integration runner boots Freedoom, which
  the survey in §3 did not cover. A refusal there is not fatal and is printed,
  so B6 reads that log once.

## 11. What checks this

| Claim | What catches it |
|---|---|
| INV-1 the rule | `tests/patch_bounds_test.cpp` |
| INV-2 stock sprites and wall patches accepted | the boot sweep's output, searched for a refusal line |
| INV-2 stock interface pictures accepted | **partial:** §3's survey, run once by hand with a transcription of the rule. Nothing re-runs it |
| INV-3 every reader asks | `tests/bounds_wiring_test.cpp` |
| INV-4 pointer to lump | `tests/wad_bounds_test.cpp`; the no-tag-change half by `tests/bounds_wiring_test.cpp`, by reading only |
| INV-5 refused, not fatal | the three fixtures, run by hand at B3 to B5. Nothing in `make test` boots the engine |
| INV-6 the texture readers agree | `tests/bounds_wiring_test.cpp` |
| INV-7 reload resets | `tests/bounds_wiring_test.cpp`, by reading only |
| INV-8 column bound | `tests/bounds_wiring_test.cpp`, by reading only |
| INV-9 exact stored offset | `tests/bounds_wiring_test.cpp`, by reading only |
| A short lump is refused without a line (§4.2) | **nothing.** Read at B2 |
| INV-10 see-through walls | `tests/bounds_wiring_test.cpp`, by reading only |
| `M_DecodePatchRGBA` caches `PLAYPAL` first (§4.4) | **nothing.** Read at B3 |
| Every patch pointer is a zone block start (§4.3) | **nothing.** It is true of the callers today |
| §6's budget | INV-3's scrape and the demo fixtures' lengths; no timing |

## 12. Cross-doc impact

- **`docs/standards/security.md`** — the trust-boundary table has a row for the
  WAD directory and lumps and none for patch graphics. A row naming
  `W_PatchLumpOk` is owed once it exists; B6 carries it.
- **`linuxdoom-1.10/patch_bounds.h`** — its header comment says the software
  renderer reaches the same data through a path that is bounded elsewhere.
  That is not so until this ships. B1 corrects the comment.
- **ROADMAP DOOM-0432** — the item speaks of three blitters; there is one
  since DOOM-0447. No edit: the resolution note will say what was built.
- No other spec's invariants change. `invariant_check` over the files this
  touches returned four specs by file name alone (the 3D renderer, the raised
  resolution, the HD materials and widescreen); each is about how a picture is
  scaled or shaded, and none constrains how a patch is read.

## 13. Cold-eyes loop log

The rows are in
[`docs/reviews/DOOM-0432-patch-lump-validation-loop-log.md`](../reviews/DOOM-0432-patch-lump-validation-loop-log.md).
