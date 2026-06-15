# DOOM-0026 — Selectable renderer back-ends (Classic / 3D)

**Status:** Reviewed — `/cold-eyes` loops 1–5 to clean (see loop log); ready to
implement. Architecture approved by user
(2026-06-15): function-pointer back-end seam at the world/UI boundary;
Vulkan hybrid (raster + hardware-RT effects) as the eventual 3D back-end with
graceful auto-detected tiers; menu toggle built with 3D shown unavailable.
3D back-end language = C++ (the engine stays C); shaders = GLSL compiled to
SPIR-V. The rationale for those three decisions is owned by ADR
`docs/decisions/0001-renderer-language-and-api.md`; `docs/standards/coding.md`
carries the one-line summary.

This spec covers two roadmap concerns that are deliberately designed together,
because the second shapes the first:

- **DOOM-0026** (implemented in the change this spec gates): keep the classic
  2.5D software renderer selectable alongside a future 3D renderer — a runtime
  back-end interface, a config-persisted choice, and a menu toggle. Default
  Classic, exact parity.
- **DOOM-0008..0012** (architecture recorded here, *not* built; they remain
  `💭 considered` on the roadmap): the true-3D / hardware ray-traced renderer,
  dynamic + volumetric lighting, and the 60 FPS floor. Recorded so the DOOM-0026
  seam is informed by where it must lead, not retrofitted later. Each gets its
  own implementation spec when committed to and picked up.

## Contents

- [Goal](#goal)
- [Background](#background)
- [Approach](#approach)
- [The back-end interface](#the-back-end-interface)
- [Classic back-end (built now)](#classic-back-end-built-now)
- [3D hybrid back-end (designed, not built)](#3d-hybrid-back-end-designed-not-built)
- [UI compositing](#ui-compositing)
- [Config and menu](#config-and-menu)
- [Components / affected files](#components--affected-files)
- [Invariants](#invariants)
- [Verification](#verification)
- [Out of scope (YAGNI)](#out-of-scope-yagni)
- [Cold-eyes loop log](#cold-eyes-loop-log)

## Goal

Let a player choose, at runtime from the menu, between the original DOOM
software renderer ("Classic") and a future hardware-accelerated 3D renderer,
with the choice persisted in the config file. Classic must remain the default
and behave exactly as it does today. The change that lands this session is
purely structural: it introduces the seam and routes the existing renderer
through it, with **zero** change to what Classic draws.

## Background

**DOOM is 2.5D.** The map is a set of 2D `sector`s (each with a floor and
ceiling height) bounded by `linedef`s; the software renderer (`r_*.c`) projects
this into columns and spans. There is no real 3D geometry — you cannot look up
or down, and rooms cannot stack. Converting to true 3D (DOOM-0008) means
generating actual meshes from this data.

**The current frame path** (verified against `d_main.c` `D_Display`, line 195).
All of it draws into the one 8-bit paletted framebuffer `screens[0]`:

1. A `switch (gamestate)` draws the per-state 2D layer *first*: for `GS_LEVEL`,
   the automap if active (`AM_Drawer`, d_main.c:243) and the status bar
   (`ST_Drawer`, d_main.c:248).
2. `R_RenderPlayerView` (d_main.c:270) draws the 3D *world* into the view-window
   region — but only for `GS_LEVEL` when the automap is **not** active (the
   automap replaces the world view).
3. `HU_Drawer` (d_main.c:273, heads-up messages), the pause patch
   (`V_DrawPatchDirect`, d_main.c:313), and the menu (`M_Drawer`, d_main.c:319 —
   "drawn even on top of everything") draw *last*, over whatever is there.
4. `I_FinishUpdate()` (d_main.c:326) maps the paletted `screens[0]` to an SDL
   texture and presents it (SDL Window/Renderer/Texture at i_video.c:39-41).
   Screen melts capture/replay `screens[0]` via `wipe_StartScreen` /
   `wipe_EndScreen`.

Note the order: the status bar and automap draw *before* the world, the HUD /
pause / menu *after*. In Classic this is harmless — the world renders into the
view window while the status bar sits below it (disjoint regions), and the menu
and pause patch deliberately overlay everything.

Per `docs/specs/DOOM-0027-hires.md` (shipped — see `doomdef.h:107-113`),
`screens[0]` is physical resolution `SCREENWIDTH`×`SCREENHEIGHT` (640×400); UI
code draws in logical `ORIGWIDTH`×`ORIGHEIGHT` (320×200) coordinates that
`V_DrawPatch` scales up. The world is rendered at physical resolution.

**Why a seam here.** The *world view* (step 2) is the only thing that differs
between Classic and 3D; every 2D element (status bar, automap, HUD, pause, menu)
is part of DOOM's identity and stays shared. So the natural, minimal abstraction
boundary is the line between "render the world view" and "draw the 2D layer /
present". The one wrinkle a 3D back-end must handle — that some 2D drawing
happens *before* the world — is addressed in [UI compositing](#ui-compositing).

## Approach

A **function-pointer back-end interface** — a `renderer_backend_t` struct of
function pointers, with a global `active` pointer the frame loop calls through.
This matches DOOM's own C idioms (it already drives mob behaviour through the
`actionf_t` function table in `info.c`), keeps the diff small, and lets the
existing software renderer become the first back-end with no behavioural change:
`D_Display` calls `RB_RenderPlayerView(player)` (one indirection) instead of
`R_RenderPlayerView(player)` directly.

Back-end selection is a small `rendermode` enum resolved once at startup by
auto-detection (see [tiers](#3d-hybrid-back-end-designed-not-built)), overridable
by the persisted config value and the menu.

### Alternatives considered

- **Compile-time `#ifdef` selection.** Rejected: DOOM-0026 explicitly requires a
  *runtime* switch in the menu, persisted in config, with both back-ends in one
  build.
- **A full abstract "scene description" API** where back-ends consume a list of
  visible surfaces + things rather than a `RenderPlayerView` call. Rejected for
  now: it is more interface than the present need justifies and risks designing
  the wrong abstraction before the 3D renderer exists to inform its shape
  (YAGNI). The function-pointer seam can be widened into this later without
  disturbing call-sites, if the 3D back-end proves it necessary.

## The back-end interface

New `r_backend.h` / `r_backend.c`. Shape (illustrative, not final signatures):

```c
typedef enum { RB_CLASSIC, RB_RT3D, RB_RASTER3D, RB_NUMMODES } rendermode_t;

typedef struct
{
    const char* name;                 // "Classic", "3D (ray traced)", ...
    boolean (*Available)(void);       // can this back-end init on this machine?
    void    (*Init)(void);            // allocate state / device
    void    (*SetResolution)(int w, int h);
    void    (*RenderPlayerView)(player_t* player);   // the world
    void    (*Present)(void);         // composite UI overlay + world, page-flip
    void    (*Shutdown)(void);
} renderer_backend_t;
```

- `RB_Init(void)` — picks the back-end (config value, clamped to what
  `Available()` reports; see tiers), calls its `Init`, stores `active`.
- `RB_RenderPlayerView(player)` / `RB_Present(void)` — thin wrappers calling
  through `active`. `D_Display` uses these.
- `RB_SetMode(rendermode_t)` — used by the menu; tears down the old back-end and
  inits the new one. Out of scope to make seamless mid-game this session (only
  Classic exists), but the entry point is defined.

`Present()` subsumes today's `I_FinishUpdate` so each back-end controls
compositing (Classic just calls the existing path; 3D composites the UI plane
over its own image — see [UI compositing](#ui-compositing)).

## Classic back-end (built now)

`R_Classic` implements the interface over the **existing, unchanged** code:

- `Available` → always `true`.
- `Init` → no-op (the engine already calls `R_Init` in `D_DoomMain`; the Classic
  back-end does not duplicate it).
- `RenderPlayerView` → calls the existing `R_RenderPlayerView`.
- `Present` → calls the existing `I_FinishUpdate`.
- `SetResolution` / `Shutdown` → no-ops for parity.

The result is byte-identical to today's output; the only runtime difference is
that the call is dispatched through `active`. This is the entirety of the
behavioural surface that ships this session.

## 3D hybrid back-end (designed, not built)

The eventual `R_Vulkan` back-end. **No code lands for this in DOOM-0026** — this
section is the architecture the seam must accommodate, fulfilling DOOM-0026's
"shapes how DOOM-0008 is architected" intent.

- **API / language / shaders:** Vulkan (hybrid raster + hardware RT), the engine
  stays C with the back-end in C++, and shaders in GLSL compiled to SPIR-V. These
  three decisions and their full rationale live in ADR
  `docs/decisions/0001-renderer-language-and-api.md` — not restated here. The
  load-bearing fact *for the seam* is that the back-end speaks only the plain-C
  `renderer_backend_t` interface, so the C/C++ boundary is the seam itself.
  (Hardware-RT availability on the target hardware is recorded in ADR 0001.)
- **Geometry (DOOM-0008):** convert the 2.5D map to real 3D meshes once per
  level — sector floors/ceilings become capped polygons at their stored heights,
  linedefs extrude into wall quads. Upload to Vulkan vertex/index buffers and
  build the GPU ray-tracing **acceleration structure** — the spatial index the
  hardware traverses to find what a ray hits (a per-mesh bottom-level "BLAS"
  gathered into one scene-wide top-level "TLAS") — for ray queries.
- **Things (enemies, items, decorations):** stay **billboarded sprites** drawn
  with the original art and 256-colour palette. This is load-bearing for "still
  feels like DOOM" — voxelising or replacing sprites is explicitly out of scope.
- **Lighting:** a rasterised G-buffer pass (a screen-sized buffer recording each
  pixel's surface position, normal, and material — the inputs lighting needs) for
  primary visibility (fast), then
  hardware ray tracing for shadows and reflections (DOOM-0009, DOOM-0010), and a
  ray-marched volumetric pass for god-rays (DOOM-0011). Sector light levels seed
  the base lighting so brightness matches vanilla expectations.
- **60 FPS floor (DOOM-0012):** held via the tier system below plus standard
  levers (render-scale, RT effect quality, denoiser). The floor is a back-end
  responsibility, not a seam concern.
- **Tiers — auto-detected at startup, in `Available()` + `RB_Init`:**
  - **RT-capable GPU** → `RB_RT3D`: full hybrid with ray-traced effects.
  - **Vulkan but no RT** → `RB_RASTER3D`: raster-only 3D, shadow maps instead of
    ray-traced shadows, screen-space approximations for volumetrics.
  - **No usable Vulkan** → only `RB_CLASSIC` is offered.
  The persisted config choice is clamped to what is actually available, so a
  config naming an unavailable mode silently falls back (and the menu reflects
  reality).

## UI compositing

The one genuinely new problem. Today the world and the UI share the single
paletted `screens[0]` buffer. Under a 3D back-end the world is a Vulkan-rendered
RGBA image, while the UI is still software-drawn paletted patches.

**Design:** keep `screens[0]` as a dedicated **UI overlay plane**. Each frame it
is cleared to a reserved transparent sentinel index, then all existing UI
drawing (HUD, status bar, menu, pause, wipes) runs **unchanged** into it. The 3D back-end's `Present()` uploads `screens[0]`
as a texture and composites it over its world image (sentinel index → fully
transparent), then presents. The Classic back-end's `Present()` ignores the
sentinel (it drew the world into the same buffer, as today).

**Timing matters:** because the status bar and automap draw *before*
`RB_RenderPlayerView` (see Background), the sentinel-clear must run at the very
start of the frame — before the `switch (gamestate)` — not inside
`RB_RenderPlayerView` / `Present`, or it would erase the pre-world UI writes. At
`Present` the world image composites *under* the `screens[0]` UI plane: view-window
pixels left at the sentinel let the world show through, while status-bar / HUD /
menu / pause pixels (and the automap, which fully replaces the view) stay opaque
on top. Wiring this clear-and-composite ordering is 3D-back-end work (DOOM-0008).

Payoff: **every existing `V_DrawPatch` / HUD / menu call stays byte-for-byte
unchanged** across both back-ends — the seam at the world/UI boundary is what
makes the UI back-end-agnostic. Choosing the sentinel index and the exact
upload/composite path is 3D-back-end work (DOOM-0008), not DOOM-0026.

**Classic-path guard:** the per-frame sentinel-clear of `screens[0]` is a
3D-back-end behaviour only. It must never be added to the Classic or shared
path — Classic draws the world *into* `screens[0]`, so clearing it to a sentinel
would erase the frame and break INV-1 (byte-identical Classic output).

## Config and menu

- **Config:** add `{"renderer", &rendermode, RB_CLASSIC}` to the `defaults[]`
  table in `m_misc.c` (the `default_t` table at m_misc.c:235; persisted in
  `~/.doomrc` by `M_SaveDefaults` / loaded by `M_LoadDefaults`). Default
  `RB_CLASSIC` (0) → exact parity for anyone who never touches the option.
  On load the value is clamped to an available mode by `RB_Init`.
- **Menu:** add a **"Renderer:"** item to the options menu (`m_menu.c`). It
  draws its label and current value with `M_WriteText` (the surrounding options
  items use pre-baked menu-art lumps via `M_DrawOptions`, which won't exist for
  this item). Until the Vulkan back-end exists, the only selectable value is
  **Classic**; the 3D entries render as **"3D (unavailable — not yet built)"**
  and cannot be chosen. This satisfies DOOM-0026's "switch exposed in the main menu" honestly
  rather than presenting a control with one real option dressed up as two. When
  DOOM-0008 lands, the same item gains live 3D choices with no menu rework.

## Components / affected files

What lands this session (DOOM-0026):

| File | Change |
|---|---|
| `r_backend.h` (new) | `rendermode_t`, `renderer_backend_t`, `RB_*` prototypes, `extern int rendermode` (holds an `RB_*` value; typed `int` for the config table). |
| `r_backend.c` (new) | `rendermode` definition, the `R_Classic` back-end, `RB_Init` (auto-detect + config clamp), `RB_RenderPlayerView` / `RB_Present` / `RB_SetMode` wrappers. |
| `d_main.c` | Call `RB_RenderPlayerView` / `RB_Present` in `D_Display` instead of `R_RenderPlayerView` / `I_FinishUpdate`. Call `RB_Init` once at the top of `D_DoomLoop`, immediately after `I_InitGraphics` (`d_main.c:371`) — *after* the SDL/graphics device exists, so a back-end's `Available()` / `Init()` can probe the GPU. (`R_Init` at `d_main.c:1124` still runs earlier, in `D_DoomMain`; the Classic back-end relies on it being done.) |
| `m_misc.c` | One `defaults[]` entry: `{"renderer", &rendermode, RB_CLASSIC}` (i.e. `0`) — a 3-field initializer like the other rows; the `intptr_t defaultvalue` (`m_misc.c:230`) holds the `0`. `rendermode` is declared `int`, so `&rendermode` is already an `int*` and needs no cast for the `int*`-typed `default_t.location` field (`m_misc.c:229`) — unlike the string rows (e.g. `m_misc.c:265`, `LINUX`-gated) that cast a `char*` target. The `RB_*` enum values assign straight into the `int`. |
| `m_menu.c` | A "Renderer:" options item, drawn via `M_WriteText` (no menu-art lump); cycles available back-ends; 3D shown unavailable. |
| `Makefile` | Add `r_backend.o` to `OBJS`. |

Not touched / not built: any Vulkan code, `i_video.c` SDL path (Classic
`Present` calls the existing `I_FinishUpdate` unchanged), all `r_*.c` software
renderer internals, all UI drawing code.

## Invariants

- **INV-1** With `renderer` unset or `0`, the engine selects Classic and renders
  byte-identically to the pre-DOOM-0026 build. *Test:* dump `screens[0]` to PNG
  at a fixed point in `-timedemo demo1` before and after the change (Classic
  selected); the two images hash-compare equal.
- **INV-2** `D_Display` performs no direct `R_RenderPlayerView` or
  `I_FinishUpdate` call after the change — both go through `RB_*`. *Test:* grep
  `d_main.c` finds no `R_RenderPlayerView` / `I_FinishUpdate`; those symbols
  appear only in `r_backend.c`.
- **INV-3** A config `renderer` value naming an unavailable back-end resolves to
  the best available one (never errors, never a blank screen). *Test:* set
  `renderer` to an RB_RT3D index in `~/.doomrc` on a build with no 3D back-end →
  engine runs Classic.
- **INV-4** The 3D menu entries are non-selectable while no 3D back-end reports
  `Available()`. *Test:* options menu shows "3D (unavailable …)"; activating it
  is a no-op.
- **INV-5** The seam adds at most one function-pointer indirection per world
  frame and per present — no measurable Classic-path regression. *Test:* median
  frame time over a fixed `-timedemo demo1` run differs by < 1% versus the
  pre-DOOM-0026 build.

## Verification

- Build clean: `make` in `linuxdoom-1.10/` links `linux/linuxxdoom` with no new
  warnings.
- Run Classic (default): play a level, confirm identical rendering, confirm a
  fresh `~/.doomrc` gains a `renderer 0` line after exit.
- Set `renderer 1` (an unavailable 3D index) in `~/.doomrc` → engine still
  starts in Classic (INV-3).
- Options menu shows "Renderer: Classic" and an unavailable 3D entry (INV-4).
- A feature-conformance test (`tests/`) is desirable for INV-1/INV-3 but gated
  on the project having a WAD available in CI; record as follow-up if not.

## Out of scope (YAGNI)

- Any Vulkan / ray-tracing / 3D rendering code (DOOM-0008..0012, future specs).
- Seamless mid-game back-end hot-swap beyond defining `RB_SetMode` (only one
  back-end exists to swap to).
- The exact UI-overlay sentinel index and composite shader (3D-back-end work).
- Automap-over-3D, look up/down, and any gameplay-visible 3D capability.
- Changing the Classic renderer's output in any way.

## Cold-eyes loop log

**Loop 1 (2026-06-15)** — 2 lanes (spec-vs-cited-code; cross-doc consistency),
briefed cold. Verified + fixed:

- *CRITICAL ×1:* `RB_Init` placement claimed "in `D_DoomMain` after `R_Init` /
  `I_InitGraphics`" — impossible, since `I_InitGraphics` runs inside `D_DoomLoop`
  (`d_main.c:371`), reached only at the end of `D_DoomMain`. Fixed: `RB_Init` at
  the top of `D_DoomLoop`, after `I_InitGraphics`.
- *HIGH ×3:* (a) `d_main.c:243,248` mis-cited as `V_DrawPatch` calls (they are
  `AM_Drawer` / `ST_Drawer`) — re-attributed to the real drawer lines; (b) the
  API/language/shaders rationale was triplicated across spec + ADR + coding.md —
  ADR 0001 made the canonical rationale home, spec and coding.md reduced to the
  decision + a reference; (c) status conflict (spec "built this session" vs
  ROADMAP `📋`) — spec reworded to "implemented in the change this spec gates",
  ROADMAP DOOM-0026 flipped to `🚧` with a `Design:` line.
- *MEDIUM ×4:* INV-2 test named the wrong file → `r_backend.c`; INV-5 had no
  numeric budget → "< 1% median frame time over `-timedemo`"; DOOM-0008..0012
  noted as `💭 considered`; the DOOM-0027 reference given an explicit path +
  `doomdef.h:107-113`; documentation.md gained the spec-plus-ADR rule.
- *LOW ×4:* "Both" → "These" decisions; config literal standardised to
  `RB_CLASSIC` and `rendermode` typed `int` (resolves the `int*` `default_t`
  mismatch); reference path-forms standardised; removed an awkward
  "automap-over-3D" aside duplicating Out-of-scope.
- *Unverified / dismissed:* none.

**Loop 2 (2026-06-15)** — same 2 lanes, cold. In-scope pair (this spec + ADR)
came back clean of CRITICAL/HIGH; the Loop-1 fixes (RB_Init placement, drawer
citations, `int` typing, rationale dedup) all held. Verified + fixed:

- *MEDIUM ×3:* the menu item needs `M_WriteText` (the surrounding options use
  pre-baked art lumps via `M_DrawOptions`) — noted; the config row should note
  the `intptr_t defaultvalue` field — added; `coding.md`'s "work has begun"
  overstated DOOM-0008..0012 — tightened to "seam designed, 3D still considered".
- *Cross-reference doc `DOOM-0027-hires.md` (stale):* its header still read
  "Ready to implement / 📋 planned" though it shipped (commit 5ad6cb1, ROADMAP
  `✅`) — flipped to "Shipped / ✅"; the "roadmap to be trimmed" pending-action
  reworded to past tense.
- *Surfaced, not auto-rewritten:* DOOM-0027's "Alternative considered — runtime
  variables" section retains internal stale line-cites — left for its own
  cleanup pass (non-trivial rewrite, out of this spec's scope).
- *Unverified / dismissed:* none.

**Loop 3 (2026-06-15)** — same 2 lanes, cold. In-scope pair clean of
CRITICAL/HIGH again. Verified + fixed:

- *MEDIUM ×2 (this spec):* the `m_misc.c` cast rationale reworded — `&rendermode`
  is already `int*` so it needs no cast (unlike the `char*` string rows), not an
  enum matter; added a **Classic-path guard** that the 3D sentinel-clear of
  `screens[0]` must never run on the shared/Classic path or INV-1 breaks.
- *Cross-reference `DOOM-0027-hires.md` — the cross-doc conflicts fixed:* its
  status header (`📋 planned` though shipped → `✅ shipped`), its
  "Alternative considered — runtime variables" section (quoted `ROADMAP.md:175`
  text that no longer exists, framed the already-done roadmap trim as pending →
  reworded to past tense, dead quote + cite removed), and the DOOM-0014 quote
  cite (→ `CHANGELOG.md:63` / `ROADMAP.md:66`, was `:52` / `:65`). *Not* touched:
  DOOM-0027's Background `doomdef.h` line-cites describing *pre*-DOOM-0027 source
  (`SCREEN_MUL`, the id "futile" note, `INV_ASPECT_RATIO`) — these are historical
  "as-was" context internal to DOOM-0027, out of this spec's scope; flagged for a
  separate DOOM-0027 cleanup, not fixed here.
- *Unverified / dismissed:* none.

**Loop 4 (2026-06-15)** — same 2 lanes, cold. Verified + fixed:

- *HIGH ×1 (this spec):* the Background framed `AM_Drawer` (d_main.c:243) and
  `ST_Drawer` (d_main.c:248) as "drawn on top" of the world, but both run
  *before* `R_RenderPlayerView` (d_main.c:270) — and the world is skipped
  entirely when the automap is active. Corrected the frame-path order and added
  the **sentinel-clear timing** constraint to UI compositing (the clear must run
  at frame start, before the `gamestate` switch, so it can't erase the pre-world
  status-bar / automap writes; the world composites *under* the UI plane at
  `Present`).
- *Scope:* DOOM-0027's Background `doomdef.h` cites are historical pre-change
  context — declared out of scope; the Loop-3 log was corrected to stop
  overclaiming they were fixed. Flagged for a separate DOOM-0027 cleanup.
- *Unverified / dismissed:* none.

**Loop 5 (2026-06-15)** — in-scope pair (this spec + ADR), cold; DOOM-0027's
internal historical cites scoped out. Lane A clean (the frame-order and RB_Init
placement fixes held); one LOW cite-precision nit fixed (`m_misc.c` string-row
cite → the `LINUX`-active row 265). Lane B polish:

- *Dedup:* removed the duplicated GPU-verification evidence from the spec — it
  now lives only in ADR 0001.
- *Grammar:* fixed a broken sentence in `coding.md`'s decision summary.
- *Conflict:* the ADR listed "global illumination" as a planned RT effect, but
  the spec's lighting plan and the ROADMAP do not — dropped GI from the ADR and
  `coding.md` so the planned effects match (shadows + reflections + volumetric).
- *Status:* the spec header still read "Draft — pending /cold-eyes" despite the
  completed loops — updated to "Reviewed … ready to implement".
- *Unverified / dismissed:* none.
