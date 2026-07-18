# DOOM-0206 — Redesigned in-game menu for the 3D tiers (crisp font, HUD-safe, dimmed backdrop)

Status: DRAFT (awaiting /cold-eyes loop, then user review).
Kind: feature. Tier: Solid + Ultra only — Classic is untouched.
Depends on / reuses: DOOM-0205 (Render Effects toggles + handlers), the Vulkan
2D overlay composite (r_vulkan.cpp), the classic menu engine (m_menu.c).

## Contents

1. Goal
2. Where this sits
3. The problem, precisely
4. Design
   - 4.1 Two menu skins, one menu engine
   - 4.2 Crisp text — a display-resolution glyph pass
   - 4.3 Dimmed backdrop + the HUD-safe bound (hard rule)
   - 4.4 Scrolling when the list is taller than the safe region
   - 4.5 The consolidated Video menu (every render toggle)
5. Data & resources
6. Performance budget
7. Build order
8. Invariants
9. Alternatives considered
10. Open questions

## 1. Goal

Give the **Solid** and **Ultra** tiers an in-game menu that reads as clean and
modern instead of the chunky, cluttered classic overlay:

- **Crisp text** at the display's real resolution (not the 320×200 upscale).
- **A dimmed backdrop** so no 3D scene or HUD clutter shows through.
- **Never overlaps the bottom HUD / status bar** — the load-bearing user
  requirement. Scroll the list if it does not fit above the status bar.
- **Surfaces every render toggle** (including Ray Tracing, currently reachable
  only by the `~` hotkey with no menu entry), grouped for scannability.
- **Classic tier keeps its authentic 1997 red menu, byte-for-byte unchanged.**

Non-goals (YAGNI): no new navigation paradigm (tabs/side-panels), no menu
restructuring beyond grouping the render settings, no re-theming of the big red
graphic-lump title art (the `DOOM` logo + `New Game`/`Options` art stay).

## 2. Where this sits

The classic menu engine `m_menu.c` draws every menu by writing into the
software framebuffer `screens[0]` — sub-menu rows via `M_WriteText` (the small
`hu_font` bitmap) and title art via `V_DrawPatch` (menu-graphic lumps). In the
3D tiers, `r_vulkan.cpp` composites `screens[0]` as a 320×200 (widescreen-
extended) 2D overlay, upscaled to the display. So all menu text inherits the
320×200 → 4K blow-up that makes it chunky, and the menu draws wherever
`m_menu.c` positions it — including over the status bar (`ST_HEIGHT` band).

The redesign adds a **second, display-resolution draw path** for the 3D tiers
and routes the affected menus through it, leaving Classic on the existing path.

## 3. The problem, precisely

Confirmed on hardware (user screenshots, 2026-07-18, Ultra):

1. **Chunky text** — the `hu_font` bitmap upscaled from 320×200 to 3840×2160.
2. **HUD overlap** — the Options menu's *Sound Volume* slider and the Renderer
   menu's *Brightness* slider render on top of the status bar; the whole menu
   shows the live 3D scene through its transparent background → cluttered.
3. **Hidden/scattered toggles** — render settings are split three levels deep
   (Options → Renderer → Render Effects), and *Ray Tracing* (`rb_rtdebug`, the
   `~` key) has no menu entry at all.

## 4. Design

### 4.1 Two menu skins, one menu engine

Keep `m_menu.c`'s state machine (item lists, cursor, input, persistence) as the
single source of truth — no second menu system. Add a **skin** decision at draw
time:

- **Classic tier** (`rendermode == RB_CLASSIC`): the existing `M_Drawer` /
  `M_WriteText` / `V_DrawPatch` path, unchanged.
- **Solid/Ultra** (`RB_RASTER3D` / `RB_RT3D`): the affected menus draw through
  the new crisp path (§4.2) on a dimmed backdrop (§4.3).

Scope of the crisp skin v1: the **text-based** menus (`OptionsDef`, the new
consolidated Video menu, Sound, Load/Save name rows). The big red graphic-lump
menus (main, episode, skill) keep their art in every tier for v1 — they are
already large and iconic; restyling them is a follow-up. The crisp path is a
strict superset draw for the same item data, so a menu that is not yet skinned
falls back to the classic path with no missing rows.

### 4.2 Crisp text — a display-resolution glyph pass

Render menu text as textured quads sampled from a glyph atlas, at display res:

- **Bundle one freely-licensed font** (SIL OFL, GPL-compatible — see §5 / §10).
- **Bake a glyph atlas once** with `stb_truetype` (single-header, public-domain,
  same vendored-stb pattern as `stb_image` per ADR 0002) at a display-appropriate
  pixel height, into an `R8` (coverage) image uploaded as a Vulkan sampled image.
  Re-bake only if the target glyph height changes (e.g. swapchain recreate).
- **A 2D textured-quad text pass** in `r_vulkan.cpp` — an orthographic,
  alpha-blended pipeline (extend the existing `overlay` pipeline family) that
  draws a per-frame vertex buffer of glyph quads in **display pixel coordinates**.
- **A small text API** the menu skin calls: draw string at (x,y) in display
  coords with a scale/colour; plus a `rb_text_width(str)` for centering and
  right-aligned value columns. `m_menu.c`'s crisp skin builds its rows through
  this API instead of `M_WriteText`.

Colour: near-white with a subtle dark drop-shadow / outline for legibility over
any dimmed scene (readability is the stated priority). Selected row highlighted
(brighter / accent colour) in place of the bobbing skull, or the skull redrawn
crisp — decided in the plan.

### 4.3 Dimmed backdrop + the HUD-safe bound (hard rule)

When a skinned menu is active in a 3D tier:

- Draw a **full-screen dim quad** (scene darkened to ~20–25% alpha-over-black)
  before the text, so neither the 3D scene nor the HUD reads as clutter.
- **The menu content is confined to a safe rectangle that excludes the status-
  bar band.** The safe region is the display above the status bar's top edge
  (the display-space image of the `ST_HEIGHT` band at the current scale/aspect).
  No glyph, slider, or backdrop-panel element is ever positioned inside that
  band. This is INV-2 and is the user's non-negotiable requirement.
- The status bar itself may stay visible (undimmed) below the safe region or be
  covered by the dim — either is acceptable as long as **no menu element draws
  over it**. Chosen behaviour fixed in the plan; INV-2 holds regardless.

### 4.4 Scrolling when the list is taller than the safe region

The consolidated Video menu (§4.5) has ~17 rows + group headings and will not
fit the safe region at a crisp, readable size on a 4:3-height budget. So the
crisp skin supports a **scrolling viewport**:

- Rows are laid out from a `scrollTop` offset; only rows inside the safe region
  are drawn (clipped), the rest are skipped.
- Moving the cursor past the visible edge advances `scrollTop` so the **selected
  row is always kept in view** (auto-scroll), and a small up/down indicator
  shows when more rows exist off-screen.
- Scrolling is a **draw-time concern only** — it does not change `m_menu.c`'s
  item indices, input handling, or persistence. INV-1 (Classic unchanged) and
  the existing key handling are untouched.

### 4.5 The consolidated Video menu (every render toggle)

Replace the three-level split with one grouped **Video** menu (reusing the
existing item handlers, incl. DOOM-0205's `M_Change*`), plus the missing
**Ray Tracing** row (`rb_rtdebug` 6↔0). Note both 3D tiers are ray-traced —
**Ultra** is the HD/PBR look, **Solid** is the classic flat DOOM look (also
RT-driven); only **Classic** is the pure software renderer. So the Ray Tracing
row applies to both 3D tiers, not Ultra alone:

```
                    V I D E O

   Renderer          Ultra (Ray Traced)
   Ray Tracing       On
   Upscaler          TAAU
   Render Scale      50%
   Brightness        <slider>

   —  Effects  —
   Flashlight  On       SSAO          On
   De-tile     4-tap    Dirt & Grime  On
   Wet Liquid  On

   —  Display  —
   Widescreen  On (restart)   Fill Screen  On
   FPS Counter Top-Right

   —  Developer  —
   Debug Views Off      Profiler      Off

                     Back
```

Every row maps to an existing config-bound `rb_*`/engine variable (§ the
DOOM-0205 inventory), so menu, hotkey and `~/.doomrc` stay in lockstep. The
`Ray Tracing` row is enabled in any tier whose backend supports RT (Ultra and
Solid — both 3D tiers are RT-driven; Solid just wears the classic flat look),
and greyed only in Classic (software renderer, no RT). `rb_wireframe` (dev
wireframe) is intentionally omitted.

## 5. Data & resources

- **Font file**: one bundled `.ttf`/`.otf` under a GPL-compatible licence (SIL
  OFL). Candidate sci-fi-but-readable sans: Oxanium, Chakra Petch, Rajdhani
  (all OFL). Final pick rendered as samples for the user before locking (§10).
  Committed to the repo (small, ~50–200 KB) with its licence file.
- **`stb_truetype.h`**: vendored single-header (public domain), compiled in one
  small TU like `rb_image.c` (ADR 0002 pattern).
- **Glyph atlas**: an `R8` Vulkan image baked at startup; a CPU-side glyph
  metrics table (advance, bearing, uv rect) for layout + `rb_text_width`.
- **Text pipeline**: one orthographic textured-quad pipeline + a per-frame
  dynamic vertex buffer (reuse the overlay pipeline plumbing).

## 6. Performance budget

Negligible. The atlas bakes once at startup (or on swapchain recreate). The text
pass runs **only while a menu is open** (gameplay frames are unaffected), draws a
few hundred glyph quads, and the dim quad is one fullscreen blend. No path-tracer
or per-gameplay-frame cost. Target: no measurable FPS change in gameplay; menu
frames stay ≥ 60 FPS. `-rtverify` byte-prefix and numeric result unaffected
(no push-constant or RT-resource change).

## 7. Build order

- **L1** — `stb_truetype` vendored + glyph atlas bake + the display-res text
  pipeline & API. Verify: a hard-coded test string renders crisp at display res.
- **L2** — dim backdrop + the HUD-safe bound (INV-2). Verify: a screenshot shows
  the dim + zero pixels drawn in the status-bar band.
- **L3** — route the crisp skin for `OptionsDef` + build the consolidated Video
  menu (all toggles, incl. Ray Tracing). Verify: every §4.5 row present, values
  live, changes persist to `~/.doomrc`.
- **L4** — scrolling viewport + auto-scroll + indicators. Verify: the full Video
  list is reachable by cursor with the selection always visible and never in the
  HUD band.
- **L5** — font selection (user picks from samples) + polish (selection
  highlight, drop-shadow, spacing). Verify: user look sign-off.

## 8. Invariants

- **INV-1** — Classic tier (`RB_CLASSIC`) menu rendering is byte-for-byte
  unchanged; the crisp path is never taken when `rendermode == RB_CLASSIC`.
- **INV-2** — In a skinned 3D-tier menu, **no menu element (glyph, slider,
  backdrop panel, indicator) is ever drawn inside the status-bar band.** The
  list scrolls rather than overrun it. (The user's hard requirement.)
- **INV-3** — Every render toggle in the DOOM-0205 inventory appears in the
  consolidated Video menu, each bound to the same variable its hotkey flips, so
  menu/hotkey/`~/.doomrc` stay consistent.
- **INV-4** — The redesign touches only menu **rendering/layout**; `m_menu.c`
  item lists, cursor logic, input handling and persistence are unchanged (the
  crisp skin and scrolling are draw-time only).
- **INV-5** — No change to any path-tracer push-constant, RT resource, or the
  `-rtverify` prefix; `-rtverify` still PASSES unchanged.
- **INV-6** — The bundled font ships under a GPL-compatible licence with its
  licence file committed (dependencies standard).

## 9. Alternatives considered

- **Pre-baked bitmap font atlas (offline PNG)** instead of `stb_truetype`
  runtime bake — rejected: adds an offline asset pipeline and a fixed glyph size;
  runtime bake matches the display and the vendored-stb ADR with less tooling.
- **Keep drawing into 320×200 and just reposition** — rejected: the user
  explicitly wants crisp text, which the 320×200 upscale cannot give.
- **Per-frame CPU rasterization of the whole menu to a display-res texture** —
  rejected: a large per-open texture upload and no real benefit over a glyph
  atlas + quads.
- **Restructure into tabbed/side-panel navigation (option C)** — rejected by the
  user; keep the existing menu engine, reskin only.

## 10. Open questions

- **Font pick** — Oxanium vs Chakra Petch vs Rajdhani (or another OFL sans);
  resolve by rendering samples for the user in L5. Default working pick: Oxanium.
- **HUD in the backdrop** — leave the status bar visible (undimmed) below the
  safe region, or cover it with the dim? Cosmetic; INV-2 holds either way.
- **Selection cue** — brighter/accent highlight on the selected row vs a
  crisp-redrawn skull cursor. Decide in L5 with the font.
- **Main/episode/skill art menus** — keep the red graphic lumps in v1 (current
  plan) or restyle later as a follow-up item.
