# DOOM-0206 Menu Redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Solid/Ultra tiers a crisp, HUD-safe, dimmed-backdrop menu with all render toggles in one consolidated Video menu, and apply the two shared fixes (HUD-safe + uniform font size) to Classic too — per spec `docs/specs/DOOM-0206-menu-redesign.md`.

**Architecture:** Keep `m_menu.c`'s one menu engine. Add (a) a display-resolution glyph-text pipeline in the Vulkan backend (`stb_truetype` atlas + textured-quad pass, templated on the existing `overlayPipeline`), (b) a new consolidated `VideoDef` menu_t reached tier-conditionally, (c) a draw-time crisp skin + `itemOn`-derived scrolling, and (d) two shared draw-time fixes (HUD-safe clip + uniform per-menu font size) that also apply to Classic. No changes to `M_Responder`, cursor-movement semantics, or persistence.

**Tech Stack:** C (engine / `m_menu.c`), C++ (`r_vulkan.cpp` Vulkan backend), `stb_truetype.h` (vendored single-header), Oxanium OFL font, the project Makefile + `make test` (hand-rolled C++ test TUs).

## Global Constraints

- **INV-1:** Classic keeps its bitmap/red rendering + menu structure; gets ONLY the two shared fixes (HUD-safe, uniform font). No glyph font / dim / VideoDef in Classic.
- **INV-2 (all tiers):** no menu element ever drawn inside the status-bar band; scroll instead. Hard user requirement.
- **INV-3:** every DOOM-0205 render toggle appears in VideoDef, bound to the same var its hotkey flips.
- **INV-4:** additions only (VideoDef menu_t + `M_ChangeRayTracing` + entry branch in `M_RendererMenu` + `currentMenu` re-route in `M_ChangeRenderer` + crisp skin + scroll). No edits to `M_Responder`, `itemOn` movement semantics, or persistence.
- **INV-5:** no path-tracer push-constant / RT-resource / `-rtverify`-prefix change; `-rtverify` still PASSES.
- **INV-6:** Oxanium + `stb_truetype.h` latest-stable, pinned at commit, recorded in the "Where this project's dependencies live" section of `docs/standards/dependencies.md` (NOT the Version Exception Ledger). Font ships with its OFL licence file.
- **INV-7 (all tiers):** one font size per menu; emphasis via weight/colour/caps, never size.
- **Coding standard:** shortest correct implementation; reuse before rewriting (`docs/standards/coding.md`). Commits `DOOM-0206: <desc>` (`docs/standards/commits.md`).
- **Always** `make` + `make test` after an engine change; never leave building to the user.
- **Ray Tracing row contract:** On ⇔ `rb_rtdebug==6`, Off ⇔ `rb_rtdebug==0`; handler no-ops when `rb_rtdebug_menu` set; greyed = visual only; RT-in-Solid is per-session (`RB_ApplyTierRt` resets at boot/switch).

---

## File structure

- `linuxdoom-1.10/stb_truetype.h` — **create**: vendored single-header glyph rasterizer.
- `linuxdoom-1.10/rb_text.c` — **create**: `#define STB_TRUETYPE_IMPLEMENTATION` in one small C TU (mirrors `rb_image.c`); exposes nothing itself — the impl is consumed by `r_vulkan.cpp`. (If the atlas bake lives in C++ it can include the header directly; keep the heavy impl in this TU to avoid recompiling it in the big `r_vulkan.cpp`.)
- `linuxdoom-1.10/rb_text.h` — **create**: the CPU-side glyph-atlas bake + metrics API shared with a unit test (pure logic: given font bytes + pixel height, produce atlas bitmap + per-glyph metrics). Header-only-testable like `rb_materials.h`.
- `linuxdoom-1.10/assets/Oxanium-SemiBold.ttf` + `OFL.txt` — **create**: bundled font + licence.
- `linuxdoom-1.10/r_vulkan.cpp` — **modify**: text pipeline (glyph atlas image + textured-quad pipeline + per-frame vertex buffer), the crisp menu-draw entry points, the dim quad, the HUD-safe clip, and the `rb_text_*` draw API exposed `extern "C"` to `m_menu.c`.
- `linuxdoom-1.10/m_menu.c` — **modify**: `VideoDef` menu_t + items + `M_DrawVideoMenu`; `M_ChangeRayTracing`; tier-conditional entry in `M_RendererMenu`; `currentMenu` re-route in `M_ChangeRenderer`; the crisp-skin draw dispatch in `M_Drawer`; `itemOn`-derived scroll; uniform-font + HUD-safe application (all tiers).
- `linuxdoom-1.10/m_misc.c` — **modify**: no new config keys needed (all toggles already bound; `rb_rtdebug` already `rt_view`). Confirm only.
- `linuxdoom-1.10/tests/rb_text_test.cpp` — **create**: unit test for the atlas-bake/metrics pure logic.
- `linuxdoom-1.10/Makefile` — **modify**: add `rb_text.o`, the test target, and the asset-embed step (font bytes → a `.h` byte array, same vendored pattern as the `.spv.h` shaders) OR load the `.ttf` from the asset path at runtime.
- `docs/decisions/0003-menu-text-rendering.md` — **create**: ADR for the display-res text path + bundled font.
- `docs/standards/dependencies.md` — **modify**: record Oxanium + stb_truetype versions.

---

## Task 1: Vendor stb_truetype + the atlas-bake pure logic (unit-tested)

**Files:**
- Create: `linuxdoom-1.10/stb_truetype.h`, `linuxdoom-1.10/rb_text.c`, `linuxdoom-1.10/rb_text.h`, `linuxdoom-1.10/tests/rb_text_test.cpp`
- Modify: `linuxdoom-1.10/Makefile`, `docs/standards/dependencies.md`, `docs/decisions/0003-menu-text-rendering.md`

**Interfaces:**
- Produces (`rb_text.h`, `extern "C"`):
  - `typedef struct { unsigned short x0,y0,x1,y1; float xoff,yoff,xadvance; } rb_glyph_t;` — atlas uv rect (pixels) + placement.
  - `typedef struct { unsigned char* pixels; int w, h; rb_glyph_t glyphs[96]; int px_height; int ascent, descent, line_gap; } rb_atlas_font_t;` — R8 atlas + metrics for printable ASCII 32..127.
  - `int rb_text_bake(const unsigned char* ttf, int ttf_len, int px_height, rb_atlas_font_t* out);` — 1 ok / 0 fail; allocates `out->pixels` (caller frees via `rb_text_free_font`).
  - `void rb_text_free_font(rb_atlas_font_t* f);`
  - `float rb_text_measure(const rb_atlas_font_t* f, const char* s);` — pixel width of `s` (sum of xadvance).

- [ ] **Step 1: Add stb_truetype + record the dep.** Download the latest stable `stb_truetype.h` into `linuxdoom-1.10/`. Add a line to the "Where this project's dependencies live" section of `docs/standards/dependencies.md`: `stb_truetype.h v<X.YY> — vendored single-header glyph rasterizer (menu text, DOOM-0206). Bump on the dependency sweep.` Also add Oxanium there once fetched in Task 5.

- [ ] **Step 2: Write the failing test** (`tests/rb_text_test.cpp`): bake a tiny known TTF (or the bundled Oxanium once present — for now a minimal built-in test font path) at px_height 48; assert `out.w>0 && out.h>0`, `out.px_height==48`, every printable glyph has `x1>=x0`, and `rb_text_measure("AB") == glyphs['A'-32].xadvance + glyphs['B'-32].xadvance` (within 0.5px).

- [ ] **Step 3: Run it, verify it fails** — Run: `make -C linuxdoom-1.10 test 2>&1 | grep rb_text`. Expected: link error / assertion (function not defined).

- [ ] **Step 4: Implement `rb_text_bake`/`measure`/`free`** in `rb_text.h` using `stbtt_BakeFontBitmap` (or `stbtt_PackFontRange` for tighter packing) into an `R8` buffer; fill `rb_glyph_t` from the baked chardata; read `stbtt_GetFontVMetrics` for ascent/descent/line_gap scaled to px_height. Put `#define STB_TRUETYPE_IMPLEMENTATION` + `#include "stb_truetype.h"` in `rb_text.c` only (scope-silence `-Wunused-function` like `rb_image.c`).

- [ ] **Step 5: Wire the Makefile** — add `rb_text.o` to the object list and `linux/rb_text_test` to the test target (mirror `rb_image_test`). Run: `make -C linuxdoom-1.10 && make -C linuxdoom-1.10 test`. Expected: build clean; `rb_text: all passed`.

- [ ] **Step 6: Write ADR 0003** (`docs/decisions/0003-menu-text-rendering.md`): decision = display-resolution glyph text via vendored stb_truetype + a bundled OFL font, drawn by a textured-quad pass templated on the overlay pipeline; alternatives (offline atlas, per-frame CPU raster) and why rejected (mirror spec §9). Commit.

- [ ] **Step 7: Commit** — `git add linuxdoom-1.10/stb_truetype.h linuxdoom-1.10/rb_text.* linuxdoom-1.10/tests/rb_text_test.cpp linuxdoom-1.10/Makefile docs/standards/dependencies.md docs/decisions/0003-menu-text-rendering.md && git commit -m "DOOM-0206: vendor stb_truetype + atlas-bake logic (L1a)"`

---

## Task 2: Display-resolution text pipeline in the Vulkan backend

**Files:**
- Modify: `linuxdoom-1.10/r_vulkan.cpp`
- Create: `linuxdoom-1.10/shaders/text.vert`, `text.frag` (+ their compiled `.spv.h`, via the existing shader-compile step)

**Interfaces:**
- Consumes: `rb_text.h` (Task 1).
- Produces (`extern "C"`, called by `m_menu.c` in later tasks):
  - `void rb_text_begin(void);` — start a menu frame's text batch (clears the quad buffer).
  - `void rb_text_draw(const char* s, int x, int y, float scale, unsigned rgba);` — queue a string at display-pixel (x,y) top-left.
  - `int  rb_text_width(const char* s, float scale);` — pixel width (wraps `rb_text_measure`).
  - `int  rb_text_line_height(float scale);` — px_height*scale, for row layout.
  - `void rb_menu_dim(void);` — queue the play-view dim quad (3D tiers only; no-op in Classic — but the gate lives in the caller).
  - The batch is flushed by the backend inside the existing overlay/composite present path, drawn AFTER `screens[0]`.

- [ ] **Step 1: Bake the atlas at init.** In the Vulkan init path (near the overlay image creation ~`r_vulkan.cpp:4720`), call `rb_text_bake(oxanium_ttf, oxanium_ttf_len, kMenuGlyphPx, &g.menuFont)` where `kMenuGlyphPx` is derived from the display height (e.g. `max(24, dispH/45)` → ~48 at 2160p). Upload `g.menuFont.pixels` into a new `R8_UNORM` `VkImage g.textAtlas`/`g.textAtlasView` (copy the overlay image create+upload block verbatim, swap format to `VK_FORMAT_R8_UNORM` and dimensions to atlas w/h). Store `g.menuFont`.

- [ ] **Step 2: Create the text pipeline** (template `CreateOverlayPipeline` ~`r_vulkan.cpp:4242`): shaders `text.vert` (in: vec2 pos in NDC-from-pixels, vec2 uv, uint rgba; a push-constant or UBO carries `vec2 invDisplay` = 2/dispW,2/dispH) + `text.frag` (sample R8 atlas → coverage → `outColor = vec4(rgba.rgb, rgba.a * coverage)`, premultiplied; a 1px dark-offset second draw gives the drop-shadow). Alpha blend ON. A dynamic vertex buffer `g.textVbuf` sized for ~4k glyph-quads (6 verts each). Add `g.textPipeline`, `g.textPipelineLayout`, `g.textDescSet` (samples `g.textAtlasView`).

- [ ] **Step 3: Implement the batch API.** `rb_text_begin` resets `g.textQuadCount`. `rb_text_draw` walks the string, and for each char appends a 2-triangle quad (pos from `x + glyph.xoff*scale`, advancing by `glyph.xadvance*scale`; uv from the glyph rect / atlas size; colour from `rgba`) into a host-visible staging array. `rb_text_width`/`rb_text_line_height` wrap Task-1 metrics. `rb_menu_dim` appends one fullscreen-minus-statusbar dark quad (a solid-colour path — either a white 1px atlas texel sampled, or a second tiny pipeline; simplest: reserve atlas texel (0,0)=full coverage and draw the dim as a quad sampling it with rgba=0x00000040).

- [ ] **Step 4: Flush in the present path.** Where the overlay is composited (~`r_vulkan.cpp:5428`+ present), after drawing `screens[0]`, upload `g.textVbuf` and `vkCmdDraw` the queued quads with `g.textPipeline`. Gate the whole text draw on `g.menuTextActive` (set by `m_menu.c` via a new `extern "C" int rb_menu_text_active;` each frame the crisp skin drew).

- [ ] **Step 5: Standalone smoke.** Temporarily, in the 3D present path, call `rb_text_begin(); rb_text_draw("DOOM-0206 TEXT TEST", 200, 200, 1.0f, 0xFFFFFFFF);` behind an env-var/parm. Build, run headless with `-shotverify /tmp/text.png` in Ultra, read the PNG: the string renders crisp (not upscaled-chunky). Remove the temp call.

- [ ] **Step 6: `-rtverify` regression** — Run: `DOOMWADDIR=../wads ./linux/linuxxdoom -warp 1 1 -rtverify`. Expected: `PASS` (INV-5 — no RT-resource/push-constant change).

- [ ] **Step 7: Commit** — `git commit -am "DOOM-0206: display-res glyph text pipeline (L1b)"`

---

## Task 3: The HUD-safe bound + dim (L2) — shared clip, 3D-only dim

**Files:** Modify `linuxdoom-1.10/r_vulkan.cpp`, `linuxdoom-1.10/m_menu.c`, `linuxdoom-1.10/st_stuff.h` (read `ST_HEIGHT`).

**Interfaces:**
- Consumes: Task 2's `rb_text_*` / `rb_menu_dim`.
- Produces: `extern "C" int rb_menu_safe_bottom(void);` — the display-pixel Y below which nothing may draw (the status-bar top edge; = full display height when no bar is shown). Used by the crisp skin AND the Classic clip.

- [ ] **Step 1:** Implement `rb_menu_safe_bottom()` in `r_vulkan.cpp`: if a game is in progress AND the status bar is drawn (`gamestate==GS_LEVEL && screenblocks<11` — always true in-game per DOOM-0148), return `dispH - (ST_HEIGHT_scaled)`; else return `dispH`. (Classic path computes the 320×200 equivalent: `200 - ST_HEIGHT` rows.)
- [ ] **Step 2:** In `M_Drawer`, when the crisp skin is active (3D tier), call `rb_menu_dim()` first (the dim spans `0..rb_menu_safe_bottom()` only). Verify by `-shotverify` in Ultra with the menu open (drive via the L5 harness or a temp "start with menu open" parm): the play area is dimmed, the status bar is not, and no menu pixel sits below `rb_menu_safe_bottom()`.
- [ ] **Step 3:** Assert INV-2 mechanically: add a temporary debug that logs the max Y of any queued glyph/quad vs `rb_menu_safe_bottom()`; confirm max ≤ safe bottom. Remove after.
- [ ] **Step 4:** Build + `make test` green. **Commit** `DOOM-0206: HUD-safe bound + 3D dim backdrop (L2)`.

---

## Task 4: Consolidated `VideoDef` menu + Ray Tracing row + tier routing (L3)

**Files:** Modify `linuxdoom-1.10/m_menu.c` (+ `extern` the toggle vars already declared for DOOM-0205).

**Interfaces:**
- Consumes: existing `M_Change*` handlers (DOOM-0205), `rb_text_*` (Task 2), `RB_SetMode`/`RB_ApplyTierRt`/`rb_rtdebug`/`rb_rtdebug_menu` (r_backend.c / r_vulkan.cpp).
- Produces: `VideoDef` (menu_t), `M_ChangeRayTracing`, the tier-conditional entry in `M_RendererMenu`, the re-route in `M_ChangeRenderer`.

- [ ] **Step 1:** Add `extern int rb_rtdebug; extern int rb_rtdebug_menu;` (if not already), and a `videoitem_e` enum + `VideoMenu[]` array in single-column order matching spec §4.5: Renderer(tier, `M_ChangeRenderer`), Ray Tracing(`M_ChangeRayTracing`), Upscaler, Render Scale, Brightness(status 2), spacer `— Effects —`(status -1), Flashlight, SSAO, De-tile, Dirt&Grime, Wet Liquid, spacer `— Display —`(status -1), Widescreen, Fill Screen, FPS Counter, spacer `— Developer —`(status -1), Debug Views, Profiler, Back(`M_VideoBack`). Reuse the DOOM-0205 `M_Change*` routines.
- [ ] **Step 2:** `M_ChangeRayTracing(int choice){ choice=0; extern int rb_rtdebug_menu, rb_rtdebug; if (rb_rtdebug_menu) return; rb_rtdebug = (rb_rtdebug==6)?0:6; }` (no-op when Debug Views owns it). `M_VideoBack` returns to Options.
- [ ] **Step 3:** In `M_RendererMenu` (Options entry row, relabelled "Video"): `if (rendermode==RB_CLASSIC) M_SetupNextMenu(&RendererDef); else M_SetupNextMenu(&VideoDef);`
- [ ] **Step 4:** In `M_ChangeRenderer`, after `RB_SetMode(next)`: if the skin class crossed Classic↔3D and `currentMenu` is Video/Renderer, swap `currentMenu` to the matching menu_t and set `itemOn` to the tier row index (VideoDef/RendererDef). Skip on Solid↔Ultra.
- [ ] **Step 5:** `M_DrawVideoMenu`: draw title + each row's label (left) and value (right, via a small helper mapping each row to its live var → string: `rb_upscaler?"TAAU":"Off"`, `detileNames[rb_detile]`, On/Off, `RB_ModeName(rendermode)`, `rb_rtdebug==6?"On":"Off"` greyed if `rb_rtdebug_menu`, etc.) through `rb_text_draw` at one glyph size (INV-7); Brightness via the bar primitive; headings via `rb_text_draw` (weight/caps, same size).
- [ ] **Step 6:** Build + run (drive via L5 harness / user): in Ultra, Options→Video shows all §4.5 rows with live values; toggling each flips the var + persists to `~/.doomrc`; cycling Renderer to Classic swaps to RendererDef. `make test` green. **Commit** `DOOM-0206: consolidated Video menu + Ray Tracing row + tier routing (L3)`.

---

## Task 5: `itemOn`-derived scrolling + font selection + polish (L4+L5)

**Files:** Modify `linuxdoom-1.10/m_menu.c`, `linuxdoom-1.10/r_vulkan.cpp`; add `linuxdoom-1.10/assets/Oxanium-*.ttf` + `OFL.txt`.

- [ ] **Step 1:** Fetch **Oxanium** (latest stable, OFL) → `linuxdoom-1.10/assets/Oxanium-SemiBold.ttf` + `OFL.txt`; embed as bytes (a `.h` array via the shader-embed step) or load from the asset path; point Task-2's bake at it. Record the version in `docs/standards/dependencies.md`.
- [ ] **Step 2:** Implement scroll in the crisp draw: `int rows = safe_region_height / rb_text_line_height(scale); int scrollTop = clamp(itemOn - rows/2, 0, total-rows);` draw only visual rows `[scrollTop, scrollTop+rows)`; skull at `(itemOn - scrollTop)` row; up/down arrows when `scrollTop>0` / more below. Derived from `itemOn` only — no `M_Responder` change.
- [ ] **Step 3:** Render font samples (Oxanium vs Chakra Petch vs Rajdhani) via `-shotverify` for the user to confirm; lock the pick.
- [ ] **Step 4:** Polish: drop-shadow (second offset text draw), spacing, skull position, value-column alignment (`rb_text_width` right-align). Verify the full Video list scrolls with selection always visible and never below `rb_menu_safe_bottom()`.
- [ ] **Step 5:** User look sign-off. **Commit** `DOOM-0206: scrolling + Oxanium + polish (L4/L5)`.

---

## Task 6: Classic shared fixes — HUD-safe + uniform font (L6)

**Files:** Modify `linuxdoom-1.10/m_menu.c`.

- [ ] **Step 1:** Uniform font (Classic): in `M_Drawer`'s classic path, render each menu's item labels at one size. For menus that use graphic-lump labels mixed with text (`OptionsMenu`, `SoundMenu`), draw the label via `M_WriteText` (uniform `hu_font`) at the row size instead of `V_DrawPatch`-ing the oversized lump, so every row in a menu is one size. (Titles: keep the menu's header lump as-is per §1 non-goals, OR render the title with `M_WriteText` too if the user wants strict uniformity — confirm at review.)
- [ ] **Step 2:** HUD-safe (Classic): clip/scroll the classic menu to `rb_menu_safe_bottom()`'s 320×200 equivalent — reuse Task-5's `itemOn`-derived scroll for the classic path when a menu's drawn height exceeds `200 - ST_HEIGHT`.
- [ ] **Step 3:** Verify: launch Classic (renderer 0), open Options/Sound — no overlap with the status bar, each menu one font size, red styling otherwise intact. `make test` green.
- [ ] **Step 4: Commit** `DOOM-0206: Classic HUD-safe + uniform font (L6)`.

---

## Task 7: Wrap-up — roadmap flip, changelog, cold-eyes of the L6/INV-7 additions

- [ ] **Step 1:** Run `/cold-eyes docs/specs/DOOM-0206-menu-redesign.md` to cover the Classic-caveat + INV-7 additions that post-date the earlier loops; fix any verified findings.
- [ ] **Step 2:** `mcp__ants__roadmap_log` flip DOOM-0206 → shipped; `changelog_log` add the entry.
- [ ] **Step 3:** Final `-rtverify` PASS + `-shotcompare` gate PASS + `make test` green. Commit + push.

---

## Self-review notes

- **Spec coverage:** L1→Task1/2, L2→Task3, L3→Task4, L4/L5→Task5, L6→Task6; INV-5 verified in Task2/7; INV-6 in Task1/5; ADR in Task1. All spec build-order steps mapped.
- **Known softness:** Task 2's exact Vulkan boilerplate is developed against the compiler (templated on the existing `overlayPipeline` create/upload/flush blocks — cited line ranges given); the interface contract (`rb_text_*`) is fixed so Tasks 3–6 are stable regardless. The dim-quad primitive (Step 3) has two viable implementations — pick the atlas-texel approach unless a solid-fill pipeline is already present.
- **Type consistency:** `rb_text_draw/width/line_height`, `rb_menu_safe_bottom`, `rb_menu_dim`, `rb_menu_text_active`, `M_ChangeRayTracing`, `VideoDef` are used with identical names/signatures across tasks.
