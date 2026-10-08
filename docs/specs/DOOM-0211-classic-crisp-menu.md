# DOOM-0211 — Give Classic the Solid/Ultra menu look, drawn through SDL

**Status:** draft (2026-10-08).
**Kind:** feature.
**Source:** ROADMAP DOOM-0211 (user request 2026-07-21; scope widened
2026-07-26; restated 2026-10-08).

**Layman:** Classic's menus look blocky next to Solid and Ultra. After this,
Classic opens the same clean menu, with the same font, dimmed backdrop and
skull cursor, on any computer — including one with no 3D graphics support.
Its game picture does not change.

**Depends on:**

- **DOOM-0206** — the crisp menu skin: the glyph atlas (`rb_text_bake`), the
  queue API m_menu.c drives (`rb_text_draw`, `rb_menu_fill`, `rb_menu_dim`,
  `rb_menu_draw_cursor`, `rb_menu_draw_logo`), and the crisp registry
  (`crispMenus[]`). This spec changes its INV-1 (§12).
- **DOOM-0147** — Classic's present aspect (`I_SetAspect`, the `fillstretch`
  preference).
- **DOOM-0026** — the back-end seam (`Classic_Present` → `I_FinishUpdate`).
  The seam is not changed.

**Delivers:** DOOM-0211.

## Contents

1. Goal
2. Where this sits
3. Scope decisions (agreed with the user)
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

Under the Classic tier, every menu that Solid and Ultra draw crisp is drawn
crisp too — same font, same dim, same skull and logo — on top of Classic's
unchanged software frame. Nothing in it needs Vulkan. Classic's Video menu
lists only the settings Classic reads. Where the crisp resources cannot be
made, Classic draws today's bitmap menu.

## 2. Where this sits

**How the 3D tiers draw the crisp menu.** m_menu.c's `M_MenuIsCrisp` decides
per frame whether the current menu uses the crisp skin: the menu must be in
`crispMenus[]` and the tier must not be Classic. A crisp menu calls the queue
API, which appends quads to vectors in r_vulkan.cpp's `VulkanState` (`g.textVerts`,
`g.cursorVerts`, `g.logoVerts`) and sets `rb_menu_text_active`. `FlushMenuText`
draws the queue after the 2D overlay, at the swapchain's resolution, then
resets `rb_menu_text_active`. Nothing of the crisp skin is written into
`screens[0]`.

**Everything the queue needs is made on the CPU.** `CreateTextResources`
bakes the Oxanium atlas with `rb_text_bake` (rb_text.c, plain C) at a glyph
height of `g.extent.height / 45`, floored at 24. The skull and the logo come
from `M_CursorSkullRGBA` and `M_MenuLogoRGBA` (m_menu.c), which decode WAD
patches to straight-alpha RGBA whose alpha is 0 or 255 (`M_DecodePatchRGBA`).
Only the upload and the draw are Vulkan.

**How Classic presents.** `Classic_Present` is `I_FinishUpdate()`. It expands
`screens[0]` through `palette[]` (gamma and the damage/pickup tints are in
that palette, set by `I_SetPalette`) into a streaming texture, then
`SDL_RenderClear`, `SDL_RenderCopy`, `I_DevShotClassic`, `SDL_RenderPresent`.
`I_SetAspect` sets the renderer's logical size to `SCREENWIDTH` ×
`SCREENHEIGHT*6/5`, or none when `fillstretch` is on, so SDL letterboxes the
frame. The upscale filter is `SDL_HINT_RENDER_SCALE_QUALITY "nearest"`.

**Why routing Classic through Vulkan was not taken.** The roadmap bullet
proposed it. Read against the code (2026-10-08), the Vulkan present uses one
fixed palette built at init (`InitPaletteAndDescriptorSet` from
`RB_PlayPal`), with no gamma and no tints. It has no 6:5 letterbox, filters
sharp-bilinear rather than nearest, and keys out palette index 251
(`RB_OVERLAY_KEY`, `overlay.frag`). Each would have to be rebuilt for
Classic. And Classic is the no-Vulkan tier: the CI smoke run pins it
(`bootsmoke_tics` in `D_DoomLoop`), and no Vulkan path falls back once it
starts. SDL 2.32 (`sdl2-config --version`) has `SDL_RenderGeometry`, which
draws the same textured, coloured triangles the queue already holds.

**What Classic's menus show today.** Captured 2026-10-08 from a DEV build of
`0025375` (`scripts/ab_capture.sh`, `-warp 1 1`, `-devmenu options`, a
1280×800 private display): Classic draws the red bitmap Options menu, and
its banner overlaps the first row (filed as DOOM-0488). Solid draws the crisp
skin. Both captures are in `/home/ants/doom-scratch/p0211` (`menu-a.png`,
`solid-menu.png`).

**Which Video-menu settings Classic reads.** Each row variable, searched with
`grep -lw <var> *.c *.cpp *.h` excluding m_menu.c and m_misc.c:

| Row | Variable | Read outside the menu by |
|---|---|---|
| Renderer | `rendermode` | r_backend.c and others |
| Widescreen | `widescreen` | the software renderer (r_draw.c, v_video.c, …) |
| Fill Screen | `fillstretch` | i_video.c |
| FPS Counter | `fpsCorner` | hu_stuff.c |
| Upscaler, Render Scale, Brightness | `rb_upscaler`, `rb_renderscale`, `rb_exposure` | r_vulkan.cpp only |
| Debug Views | `rb_rtdebug_menu` | r_vulkan.cpp, and the `~` key handling in r_backend.c and i_video.c, which acts on the 3D view |
| Profiler | `rb_profile` | r_vulkan.cpp, and its hotkey toggle in i_video.c |
| Effects rows | `rb_flashlight`, `rb_ssao`, `rb_detile`, `rb_filth`, `rb_wet`, `rb_fog`, `rb_bloom` | r_vulkan.cpp, and hotkey toggles in i_video.c for all but SSAO and bloom |

A hotkey toggle flips the variable. It does not change what Classic draws.

The Classic "Brightness" row is `rb_exposure`, the 3D view's exposure.
Classic's own gamma is the F11 key (`usegamma`) and is not a menu row.

## 3. Scope decisions (agreed with the user)

Asked and answered 2026-10-08:

- **Route: draw the crisp menu with SDL over Classic's frame**, not through
  Vulkan. The user picked the recommended option.
- **Classic's Video menu shows only the settings that do something in
  Classic.** The user's words: *"Please have the menu show the relevant menu
  items for Classic."* The rows are those §2's table marks as read on the
  Classic path, plus Back.
- **Dim the game behind the menu, as Solid and Ultra do.** The status bar
  stays undimmed.

Taken from the roadmap bullet without a new question: the bitmap menu stays,
as the fallback when the crisp resources cannot be made.

## 4. Design

### 4.1 One queue, two presenters

The queue moves out of r_vulkan.cpp into a new C module, `menu_text.c` /
`menu_text.h`. It owns the baked font metrics, the three vertex lists, the
display size, and the ready flags for the font, skull and logo. Every
`rb_text_*` / `rb_menu_*` / `rb_display_*` entry point moves with it, its
logic unchanged, so m_menu.c's calls do not change.

- The vertex type keeps `TextVertex`'s layout — `float x, y, u, v; unsigned
  char r, g, b, a` — so the Vulkan vertex input description is unchanged.
- The lists have fixed capacities. The text list keeps today's
  `4096 * 6` vertices, and an over-cap frame drops the tail, as
  `FlushMenuText` does now.
- A presenter tells the module what it has: the display size, the baked
  font, and whether the skull and logo textures exist. It clears all of that
  when it shuts down (`mt_reset`), so the ready flags always describe the
  presenter that is live.
- r_vulkan.cpp keeps its GPU objects. `CreateTextResources` bakes through
  the module, and `FlushMenuText` reads the module's lists.
  `RB_Vulkan_Shutdown` calls `mt_reset`.

### 4.2 The SDL presenter

All in i_video.c, built when `CreateSoftwareWindow` makes the window and
destroyed with it (`I_ShutdownGraphicsForVulkan` and shutdown):

- **The image rectangle.** The rectangle in output pixels that the game frame
  covers, taken from SDL's own state: the viewport (`SDL_RenderGetViewport`,
  in logical units) times the scale (`SDL_RenderGetScale`). With
  `fillstretch` on it is the whole output. It is re-read whenever it can
  change: window creation, `I_SetAspect`, and a window size event. The
  module's display size is this rectangle's size, so `rb_display_width`,
  `rb_display_height` and `rb_menu_safe_bottom` describe the picture, not
  the window with its bars.
- **The font** is baked at `rect height / 45`, floored at 24 — the 3D tiers'
  rule. The atlas becomes an RGBA texture: white colour, alpha = coverage,
  texel (0,0) fully covered for the dim quad. Blend mode
  `SDL_BLENDMODE_BLEND` with the vertex colour as the modulation gives
  `rgb·a·cov + dst·(1 − a·cov)`, which is what `text.frag` and its
  premultiplied blend produce.
- **The skull and logo** are uploaded as they come from `M_CursorSkullRGBA`
  and `M_MenuLogoRGBA`, with `SDL_BLENDMODE_BLEND`. `cursor.frag`
  premultiplies straight alpha itself, so this matches.
- **Filtering.** Each of these textures sets `SDL_ScaleModeLinear` with
  `SDL_SetTextureScaleMode`. The global hint is `"nearest"` for the game
  frame and must stay so.
- **The flush** runs in `I_FinishUpdate` after `SDL_RenderCopy` and before
  `I_DevShotClassic`, so a `-devshot` capture includes the menu. It draws
  only when `rb_menu_text_active` is set and something is queued, then resets
  `rb_menu_text_active`, as `FlushMenuText` does. Order: text and dim, then
  skull, then logo. Vertex positions arrive in image-rectangle pixels. The
  flush maps them into the renderer's coordinate space by dividing by the
  scale. SDL then rasterises at output resolution, so the glyphs are crisp.
  The renderer's logical size, viewport and scale are the same after the
  flush as before it.
- **Failure.** If the bake, a texture, or `SDL_RenderGeometry` fails, the
  matching ready flag is cleared and stays cleared until the window is
  rebuilt. A failed skull or logo uses the existing fallbacks: the paletted
  skull and the crisp-text title. A failed font means no crisp menu.

### 4.3 The gate

`M_MenuIsCrisp` becomes: the menu is in `crispMenus[]`, and either the tier
is not Classic or the module's font is ready. The 3D tiers' half is
unchanged. The Game Select skull gate becomes `rb_menu_cursor_ready()` alone,
since the flag now describes the live presenter.

### 4.4 Classic's Video menu

`RendererDef` stays the menu the Options "Video" row opens under Classic.
`M_RendererMenu` and `M_ChangeRenderer`'s re-route are unchanged.

- Its rows become Renderer, Widescreen, Fill Screen, FPS Counter and Back.
  Upscaler, Render Scale, Debug Views, Render Effects and Brightness are
  removed. Back calls `M_VideoBack`.
- It joins `crispMenus[]` with the title `VIDEO`, its labels, and a value
  function giving the same values `M_VideoCrispValue` gives for those rows.
- `M_DrawRendererMenu` draws the same five rows for the bitmap fallback.
- `EffectsDef`, `EffectsMenu`, `M_UltraEffects`, `M_DrawEffectsMenu` and the
  `-devmenu effects` entry are deleted: Render Effects was the only way in.
  The removed rows' variables stay in `~/.doomrc` and stay reachable from
  `VideoDef` under Solid and Ultra.

Under Classic the crisp menus are the 3D tiers' registered set plus
`RendererDef`. Help screens, Game Select and Load/Save stay bitmap, as they
are under Solid and Ultra.

### 4.5 A DEV-build switch to force the fallback

`-nocrispmenu` (DEV builds only, beside `-devmenu`) skips the SDL presenter's
bake, so the bitmap fallback can be captured on demand. INV-5 uses it.

## 5. Data & resources

- **New files:** `menu_text.c`, `menu_text.h`, `tests/classic_menu_test.cpp`.
  `menu_text.o` joins the Makefile's object list.
- **Memory:** the SDL atlas is `atlas w × h × 4` bytes, four times the
  Vulkan R8 atlas. The skull and logo are their decoded sizes. Vertex lists
  are fixed arrays sized as §4.1 says.
- **No new config key, file format or save-game change.** The removed rows'
  config keys keep loading and saving.

## 6. Performance budget

`docs/standards/performance.md` makes the 60 FPS floor absolute for Classic.

- **Menu closed:** no added per-frame work beyond the flush's early return.
  The rectangle is re-read only on the events §4.2 names.
- **Menu open:** measured at B2 with the FPS counter, Classic, a 1920×1080
  window, Options menu open, recorded in the loop log's implementation row.
  It must hold the floor. Not budgeted beyond that.

## 7. Build order

Each step builds (`make`, `make DEV=1`) and passes `make test` before the next.

- **B1 — move the queue.** Create `menu_text.c/.h`, move the entry points and
  lists, point r_vulkan.cpp at them, add `mt_reset` to `RB_Vulkan_Shutdown`.
  No behaviour change. Check: INV-8.
- **B2 — the SDL presenter and the gate.** §4.2, §4.3, §4.5. Check: INV-1,
  INV-2, INV-3, INV-4, INV-5, INV-7, and the §6 measurement.
- **B3 — Classic's Video menu.** §4.4 and `tests/classic_menu_test.cpp`.
  Check: INV-6, and a capture of `-devmenu renderer` under Classic.
- **B4 — records.** DOOM-0206 INV-1 annotation, the CLAUDE.md tier row, the
  CHANGELOG entry (§12).

## 8. Invariants

The captures below use `scripts/ab_capture.sh` from a `make DEV=1` build of
each commit named, with a config holding `renderer 0` (`renderer 2` where a
clause says Solid) and `fps_corner 0`, at `1056 -3616 90`. "base" is commit
`0025375`. The FPS counter must be off: with it on, two identical runs
differed by 37 pixels.

- **INV-1** — With no menu open, Classic's presented frame is byte-identical
  to base. Breaks if the flush draws with an empty queue, or leaves the
  logical size, viewport or scale changed.
  *Test:* capture with no menu from base and from the built commit;
  `compare -metric AE base.png new.png null:` → `0`. Recipe proven on base
  2026-10-08: two runs gave AE `0`, and the same run with `-devmenu options`
  against it gave `21512`.
- **INV-2** — With the font ready, every menu in `crispMenus[]` is drawn by
  the crisp skin under Classic, at output resolution, and nothing of it is
  written into `screens[0]`. Breaks if the gate still excludes Classic, or if
  the flush draws at logical resolution, which shows as blocky glyphs.
  *Test:* capture `-devmenu options` under Classic. The rows are Oxanium text
  laid out as in Solid's capture of the same menu, and the log names the
  Classic font bake with its glyph height. Compared by eye against
  `solid-menu.png`.
- **INV-3** — The Classic crisp menu needs no Vulkan. Breaks if any part of
  §4.2 or the gate reads Vulkan state.
  *Test:* capture `-devmenu sound` under Classic twice, once normally and
  once with `VK_ICD_FILENAMES=/nonexistent`. The second log contains
  `RB_VulkanProbe: no Vulkan instance`. `compare -metric AE` between the two
  → `0`. Not the Options menu: its Video row reads `(no 3D)` without Vulkan
  (`RB_ModeMenuName`), and on base the two Options captures differed by 662
  pixels while the two Sound captures gave `0`.
- **INV-4** — No part of the crisp menu, dim included, is drawn in the status
  bar under Classic in a level (DOOM-0206 INV-2). Breaks if the dim or a row
  uses the window height rather than the image rectangle's safe bottom.
  *Test:* capture `-devmenu options` from base and from the built commit.
  Crop both to the status bar band (`magick <png> -crop 1280x128+0+672`, the
  bottom 32/200 of the 1280×800 frame) and compare → AE `0`. Menu-open
  against menu-closed is not a valid control here: the menu pauses the game,
  and the face and arms digits differed by 1272 pixels on base.
- **INV-5** — Where the font is not ready, Classic draws the bitmap menu, never
  a blank one. Breaks if the gate admits the crisp skin with nothing to draw
  it.
  *Test:* capture `-devmenu options -nocrispmenu` from the built commit and
  `-devmenu options` from base → AE `0`.
- **INV-6** — Classic's Video menu lists exactly Renderer, Widescreen, Fill
  Screen, FPS Counter and Back, and `EffectsDef` no longer exists. Breaks if
  a row the Classic path does not read is added back.
  *Test:* `tests/classic_menu_test.cpp` reads m_menu.c and checks the
  `renderer_e` members in order (`rm_renderer, rm_widescreen,
  rm_fillstretch, rm_fps, rm_back, rm_end`) and that `EffectsDef` does not
  appear.
- **INV-7** — The ready flags describe the live presenter. After Classic →
  Solid → Classic through the Video menu, Classic's crisp menu still draws,
  and the 3D tier's crisp menu draws while it is live. Breaks if a presenter
  skips `mt_reset` at shutdown, which leaves the other's flags set with no
  textures behind them.
  *Test:* manual. Play the switch in a DEV build with the Vulkan validation
  layer on. Each menu is crisp after its switch, and the layer reports no
  messages.
- **INV-8** — Solid's and Ultra's menus are unchanged by B1. Breaks if the
  move alters a vertex, a draw order or the flush's gate.
  *Test:* capture `-devmenu options` under Solid from base and from B1;
  `compare -metric AE -fuzz 2% base.png b1.png null:` → `0` (rounded).
  Solid is not bit-exact between runs: two base runs gave AE `1.0049`
  without fuzz and `0.0608` with `-fuzz 2%`.

## 9. Alternatives considered (and rejected)

- **Route Classic's whole frame through Vulkan.** The roadmap's original
  shape. Rejected by the user on 2026-10-08, on §2's grounds: palette, gamma,
  tints, aspect and filtering would all need rebuilding, and machines with no
  Vulkan would keep the bitmap menu, so two menus would still exist.
- **A higher-resolution paletted bitmap font for the software path.** Keeps
  everything in `screens[0]`, but cannot match the Solid/Ultra look, which is
  what the user asked for.
- **Tuning the bitmap menu's scale.** A partial fix to blockiness that does
  not give the same menu.
- **Classic opens `VideoDef` with the 3D-only rows greyed out.** Offered on
  2026-10-08; the user chose to show only the relevant rows.
- **Keep Classic's Renderer and Effects menus, drawn crisp.** Offered on
  2026-10-08; the same answer applies.

## 10. Open questions

- **A window resize does not re-bake the font**, under either presenter.
  The rectangle is re-read, so the layout follows the new size, but the
  glyph height stays at the first window's. Unchanged by this spec.
- **F12 under Classic in a DEV build** writes `screens[0]` to a .pcx
  (`G_ScreenShot`), which does not contain the crisp menu. `-devshot` does.

## 11. What checks this

| Invariant | Checked by |
|---|---|
| INV-1 | capture compare (manual run of `ab_capture.sh`) |
| INV-2 | capture, judged by eye |
| INV-3 | capture compare with no Vulkan driver |
| INV-4 | capture compare of the status bar band |
| INV-5 | capture compare with `-nocrispmenu` |
| INV-6 | `tests/classic_menu_test.cpp` under `make test` |
| INV-7 | nothing automated — a manual switch with the validation layer |
| INV-8 | capture compare under Solid |

## 12. Cross-doc impact

- **DOOM-0206-menu-redesign.md** — INV-1 and the §1 / §4.6 / header lines
  saying Classic keeps the bitmap skin, never gets the dim, and never sees
  the crisp renderer: annotate each as superseded by DOOM-0211 for a Classic
  run whose font is ready. INV-1's bitmap path remains true for the fallback.
  The `M_MenuIsCrisp` comment carrying INV-1 changes with the code.
- **DOOM-0206-implementation-plan.md** — its INV-1 line, the same
  annotation.
- **DOOM-0026 and DOOM-0008** — their Classic present clauses still hold:
  Classic still presents through `I_FinishUpdate` and the SDL renderer, and
  INV-1 here keeps its frame byte-identical with no menu open.
- **DOOM-0331-bloom.md** — its premise that no Vulkan pass runs under Classic
  still holds.
- **CLAUDE.md, Render tiers table** — Classic's row says widescreen is the
  one concession. The menu's look becomes a second one; the row is updated
  at B4.
- **CHANGELOG.md** — an entry at B4.

## 13. Cold-eyes loop log

Rows live in `../reviews/DOOM-0211-classic-crisp-menu-loop-log.md`.
