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

Scope of the crisp skin v1: the **new consolidated Video menu (`VideoDef`)
only**. `OptionsDef` and `SoundDef` keep the classic path in v1 — their row
labels are big-red graphic lumps (`M_MESSG`, `M_SVOL`, `M_MSENS`, `M_SCRNSZ`, …)
drawn as patches, and several rows are thermometer sliders (`status == 2`), none
of which the crisp text/slider API renders — and re-theming those lumps is a
non-goal. `VideoDef` is built fresh from text rows plus one Brightness slider, so
it needs no lump conversion; it is the menu that carries the render toggles the
redesign is about. `OptionsDef`/`SoundDef` (and Load/Save name rows) are a
follow-up. The big red graphic-lump menus (main, episode, skill) likewise keep
their art in every tier for v1 — already large and iconic; restyling them is a
follow-up. The crisp path is a
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
- **A small text + slider API** the menu skin calls: draw string at (x,y) in
  display coords with a scale/colour; `rb_text_width(str)` for centering and
  right-aligned value columns; and a horizontal **bar/thermo primitive** for the
  one `status == 2` slider row (Brightness) — `VideoDef` has a slider, so a
  glyph-only API is insufficient. `m_menu.c`'s crisp skin builds `VideoDef`'s rows
  through this API instead of `M_WriteText` / `M_DrawThermo`.

All text within a menu uses **one glyph size** (INV-7): the title (`V I D E O`)
and the group headings are the *same size* as the rows, set apart by weight /
caps / letter-spacing, not by a bigger font.

Colour: near-white with a subtle dark drop-shadow / outline for legibility over
the dimmed scene (readability is the stated priority). The **bobbing skull
cursor is kept** as the selection cue (user decision) — not an accent-colour row
highlight. The skull stays its existing paletted patch lump (`M_SKULL*` via
`V_DrawPatch`), positioned by the crisp path at the selected row's scrolled
coordinates; only the menu *text* is glyph-rendered (the skull is not converted
to a glyph).

### 4.3 Dimmed backdrop + the HUD-safe bound (hard rule)

When a skinned menu is active in a 3D tier:

- Draw a **dim quad over the play-view area only** (the region above the status
  bar), darkening the 3D scene behind the menu to ~20–25% alpha-over-black so it
  does not read as clutter. **The status bar is left undimmed and fully visible**
  (user decision) — the dim never covers it.
- **The menu content is confined to a safe rectangle that excludes the current
  status-bar band.** The safe region is the display above the status bar's top
  edge (the display-space image of the `ST_HEIGHT` band at the current
  `screenSize`/aspect). No glyph, slider, cursor, or backdrop element is ever
  positioned inside that band. This is INV-2 and is the user's non-negotiable
  requirement.
- **When no status bar is drawn** — the title/main menu (no game in progress) —
  the excluded band is empty and the safe region is the full screen (with a small
  margin). (In-game the bar is always present: DOOM-0148 caps the view at
  `screenblocks` 10, so it cannot be slid away.) This case is real — the menu is
  reachable from the main menu — so the bound tracks the *current* bar, not a
  fixed offset.

### 4.4 Scrolling when the list is taller than the safe region

The consolidated Video menu (§4.5) has 15 toggle rows + a Back row (16
selectable) + 3 group headings = 19 visual rows, and will not fit the safe region
at a crisp, readable size on a 4:3-height budget. So the crisp skin supports a
**scrolling viewport**:

- `scrollTop` is **derived each draw from `itemOn`** (the current cursor index)
  and the safe region's row capacity — it is NOT stored state updated on input,
  so `M_Responder` (the key/cursor handler) is never touched. Only rows inside
  the safe region are drawn (clipped); the rest are skipped.
- Because `scrollTop` tracks `itemOn`, the **selected row is always kept in
  view** (auto-scroll), and a small up/down indicator shows when more rows exist
  off-screen.
- The skull cursor, drawn in `M_Drawer` at `y - 5 + itemOn*LINEHEIGHT` today, is
  repositioned into scrolled display coords by the crisp draw path (still
  draw-time, per INV-4). Scrolling changes **no** item indices, input handling,
  or persistence.
- **Group headings (`— Effects —`, …) are `status == -1` spacer `menuitem_t`
  rows.** The cursor already skips them (`m_menu.c` does `while(status==-1)` on
  up/down), and each occupies exactly one visual row — so the layout is a uniform
  one-row-per-entry list and the `itemOn`→visual-row map (hence `scrollTop`)
  stays a simple index. No separate heading-positioning logic is needed.

### 4.5 The consolidated Video menu (every render toggle)

Add one grouped **Video** `menu_t` (`VideoDef`) for the 3D tiers that reuses the
existing item handlers (incl. DOOM-0205's `M_Change*`) and adds the missing
**Ray Tracing** row. **Entry is tier-conditional:** the Options menu's entry row
— relabelled **"Video"** (from today's "Renderer") to avoid clashing with the
tier-selector row inside — opens `VideoDef` in the 3D tiers and the existing
`RendererDef` in Classic, via a `rendermode` branch in that row's handler
(`M_RendererMenu`). This branch and the tier re-route below are the navigation
edits; the classic menus stay intact for Classic (INV-1).

**Tier changes re-route the menu.** The in-menu tier-selector row reuses
`M_ChangeRenderer` (→ `RB_SetMode`, cycling Classic→Solid→Ultra). Because the
*skin* is tied to the tier, a tier change that crosses the **Classic↔3D**
boundary re-routes `currentMenu`: cycling to Classic from `VideoDef` swaps to
`RendererDef` (classic skin); cycling to a 3D tier from `RendererDef` swaps to
`VideoDef`. A **Solid↔Ultra** change keeps the same `VideoDef` (no re-route —
avoids a needless cursor jump). `M_ChangeRenderer` is shared with Classic's
`RendererDef` tier row, so this edit touches a Classic-reachable handler — but
the swap only fires on the Classic↔3D transition, so INV-1 (VideoDef never shown
under `RB_CLASSIC`) holds. The cursor is kept on the tier row across the swap by
setting `itemOn` to the destination's tier-row index — `M_SetupNextMenu` alone
would restore the destination menu's `lastOn`, which need not be that row.

Ray tracing is an **independent on/off toggle within both 3D tiers**, not a
property of the tier: `RB_ApplyTierRt` (r_backend.c) sets `rb_rtdebug = 0` for
**Solid** (which therefore defaults to the raster / classic flat look) and
`rb_rtdebug = 6` for **Ultra** (defaults to RT + the HD/PBR look); Classic is
the software renderer with no RT. Enabling RT in Solid yields ray tracing with
the classic look. So the Ray Tracing row applies to both 3D tiers, greyed only
in Classic:

Layout is a **single column** — the DOOM cursor is one-dimensional (`itemOn ± 1`
on up/down; left/right are consumed by sliders and never move the cursor), and
the skull is drawn in one column. A two-column grid would need `M_Responder` and
skull-layout changes (violating INV-4), so the list is one item per row and
scrolls (§4.4):

```
                    V I D E O

   Renderer          Ultra
   Ray Tracing       On
   Upscaler          TAAU
   Render Scale      50%
   Brightness        <slider>

   —  Effects  —
   Flashlight        On
   SSAO              On
   De-tile           4-tap
   Dirt & Grime      On
   Wet Liquid        On

   —  Display  —
   Widescreen        On (restart)
   Fill Screen       On
   FPS Counter       Top-Right

   —  Developer  —
   Debug Views       Off
   Profiler          Off

                     Back
```

(The `Back` row is a `status == 1` item returning to Options; Esc also returns,
per classic behaviour.)

Every row maps to an existing config-bound `rb_*`/engine variable (§ the
DOOM-0205 inventory), so menu, hotkey and `~/.doomrc` stay in lockstep — except
the new Ray Tracing row, which needs a **new `M_ChangeRayTracing` handler**
(there is none today — `rb_rtdebug` is currently written by config load
(`rt_view`), the `RB_Init` clamp, `RB_ApplyTierRt`, `M_ChangeDebugViews`, and the
`~` key, but by no dedicated RT on/off row). Ray Tracing row contract:

- **On ⇔ `rb_rtdebug == 6`, Off ⇔ `rb_rtdebug == 0`**; `M_ChangeRayTracing`
  toggles between those two values.
- **Greyed while Debug Views (`rb_rtdebug_menu`) is On.** The engine's
  `menuitem_t.status` has no "disabled" state (0 = inert/no-cursor, 1 = normal,
  2 = slider, -1 = skipped spacer; only -1 is skipped by cursor movement). The
  row stays `status == 1` (so the cursor still lands and it reads as
  interactive), and Enter still fires the routine (INV-4 forbids touching
  `M_Responder`). Therefore "greyed" is **visual only**, and `M_ChangeRayTracing`
  **no-ops (returns early) when `rb_rtdebug_menu` is set** — that developer mode
  owns `rb_rtdebug` (the `~` key then cycles it through the diagnostic set
  `{0,1,2,3,4,6}`).
- **Ray Tracing in Solid is a per-session choice.** Although `rb_rtdebug`
  persists (`rt_view`), `RB_ApplyTierRt` runs at boot (`RB_Init`) and on every
  tier switch and reclaims the tier default (`0` Solid, `6` Ultra) unless Debug
  Views is on — so RT enabled in Solid does **not** survive a restart (Ultra
  stays on). INV-3's persistence guarantee applies to the other rows; this one
  row inherits the engine's existing tier-reset behaviour, noted here so it is
  not mistaken for a bug.

`rb_wireframe` (dev wireframe) is intentionally omitted.

## 5. Data & resources

- **Font file**: **Oxanium** (SIL OFL, GPL-compatible) — user-selected. A clean,
  slightly techy sci-fi sans. Use the **latest stable release**; commit the
  `.ttf` (~50–200 KB) with its OFL licence file, and **record its version in the
  "Where this project's dependencies live" section of
  `docs/standards/dependencies.md`** (where stb_image is recorded, and re-checked
  on the dependency sweep — NOT the Version Exception Ledger, which is only for
  temporary older-version holds).
  Samples still rendered at L5 for final on-hardware confirmation; Oxanium is the
  locked default.
- **`stb_truetype.h`**: vendored single-header (public domain), latest stable
  (pinned at commit), compiled in one small TU like `rb_image.c` (ADR 0002
  pattern), and **recorded in the same "Where dependencies live" section**
  alongside stb_image.
- **ADR**: a display-resolution text-render path + a bundled font is an
  architectural decision on the scale of ADR 0002 (stb_image). Write
  `docs/decisions/0003-menu-text-rendering.md` alongside implementation.
- **Glyph atlas**: an `R8` Vulkan image baked at startup; a CPU-side glyph
  metrics table (advance, bearing, uv rect) for layout + `rb_text_width`.
- **Text pipeline**: one orthographic textured-quad pipeline + a per-frame
  dynamic vertex buffer (reuse the overlay pipeline plumbing).

## 6. Performance budget

Negligible. The atlas bakes once at startup. The text pass runs **only while a
menu is open** (gameplay frames are unaffected), draws a few hundred glyph quads,
and the dim quad is one fullscreen blend. No path-tracer or per-gameplay-frame
cost. Target: no measurable FPS change in gameplay; menu frames stay ≥ 60 FPS.
`-rtverify` byte-prefix and numeric result unaffected (no push-constant or
RT-resource change).

Concrete figures: the glyph atlas is one `R8` image — printable ASCII at a ~48px
glyph height packs into ≤ 1024×1024 (≤ 1 MB VRAM). The one-time bake
(stb_truetype rasterize + upload) is a few ms at startup, off the render path. A
swapchain recreate (window resize) that changes the target glyph height triggers
a re-bake that may cost one stalled frame — acceptable, and off the gameplay hot
path (it happens only on resize, never during play).

## 7. Build order

- **L1** — `stb_truetype` vendored + glyph atlas bake + the display-res text
  pipeline & API. Verify: a hard-coded test string renders crisp at display res.
- **L2** — dim backdrop + the HUD-safe bound (INV-2), gated on "a skinned menu is
  active in a 3D tier" **independently of the crisp-text routing** (so it is
  testable before L3 wires the text path). Verify: a screenshot shows the dim +
  zero pixels drawn in the status-bar band.
- **L3** — build the consolidated `VideoDef` `menu_t` (all toggles, incl. the new
  Ray Tracing row + its `M_ChangeRayTracing` handler) + the tier-conditional
  entry branch + the tier re-route, and route the crisp skin for `VideoDef`.
  Verify: every §4.5 row present in the 3D tiers, Classic still opens
  `RendererDef` unchanged, `OptionsDef`/`SoundDef` still render classic, values
  live, changes persist to `~/.doomrc`.
- **L4** — `itemOn`-derived scrolling viewport + indicators. Verify: the full
  Video list is reachable by cursor with the selection always visible and never
  in the HUD band.
- **L5** — font selection (user picks from samples) + polish (crisp skull cursor,
  drop-shadow, spacing). Verify: user look sign-off.

## 8. Invariants

- **INV-1** — Classic tier (`RB_CLASSIC`) menu **rendering and navigation** are
  unchanged: the crisp skin, the dim backdrop, and the `VideoDef` consolidation
  are never reached when `rendermode == RB_CLASSIC`. Classic keeps the existing
  `RendererDef`/`EffectsDef` menus and the classic draw path, byte-for-byte (the
  tier-conditional entry row routes Classic to `RendererDef`, §4.5).
- **INV-2** — In a skinned 3D-tier menu, **no menu element (glyph, slider,
  backdrop panel, indicator) is ever drawn inside the status-bar band.** The
  list scrolls rather than overrun it. (The user's hard requirement.)
- **INV-3** — Every render toggle in the DOOM-0205 inventory appears in the
  consolidated Video menu, each bound to the same variable its hotkey flips, so
  menu/hotkey/`~/.doomrc` stay consistent.
- **INV-4** — The redesign ADDS a new consolidated `VideoDef` `menu_t` (its item
  array + a new `M_ChangeRayTracing` handler + the tier-conditional entry branch
  in `M_RendererMenu` + a `currentMenu` re-route in `M_ChangeRenderer` on a
  Classic↔3D tier change) and a crisp draw skin + `itemOn`-derived scrolling. It
  does **not** change the existing classic menus' item lists, the `itemOn`
  cursor-*movement* semantics, `M_Responder`, or persistence — those are reused
  unchanged. The entry branch and the tier re-route act only through the existing
  `M_SetupNextMenu` routing (swapping which `menu_t` is `currentMenu`, plus one
  `itemOn =` to keep the cursor on the tier row), never by editing `M_Responder`.
  The crisp skin, the scroll, and the skull reposition are draw-time only.
- **INV-5** — No change to any path-tracer push-constant, RT resource, or the
  `-rtverify` prefix; `-rtverify` still PASSES unchanged.
- **INV-6** — The bundled font ships under a GPL-compatible licence with its
  licence file committed, and **both new vendored dependencies (Oxanium + `stb_
  truetype.h`) are latest-stable, pinned at commit, and recorded in the "Where
  this project's dependencies live" section of `docs/standards/dependencies.md`**
  (not the Version Exception Ledger — dependencies standard / ADR 0002 pattern).
- **INV-7** — **Uniform font size per menu (user requirement).** Within any one
  crisp (Solid/Ultra) menu or submenu, *all* text — title, group headings, row
  labels, and value columns — is rendered at a **single glyph size**. Emphasis
  (title, headings) is expressed with weight / colour / letter-spacing / caps,
  **never a different font size**. The size may differ *between* menus if their
  content densities differ, but is constant *within* one menu.

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

## 10. Resolved decisions & remaining open questions

Resolved with the user (2026-07-18):

- **Font** — **Oxanium** (OFL). Samples still shown at L5 for final confirmation.
- **Status bar** — left **visible and undimmed**; the dim covers only the
  play-view area above it. (INV-2 keeps the menu out of the status-bar band.)
- **Selection cue** — **keep the bobbing skull cursor** (its existing patch lump,
  repositioned by the crisp path; only text is glyph-rendered).

Remaining open (non-blocking, decide during build):

- **Main/episode/skill art menus** — keep the red graphic lumps in v1 (current
  plan) or restyle later as a follow-up item.
