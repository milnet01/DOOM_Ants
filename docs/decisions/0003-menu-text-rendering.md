# 0003 — Menu text rendering: display-resolution glyph atlas via vendored stb_truetype

**Status:** Accepted (2026-07-19) — the DOOM-0206 spec cleared its cold-eyes gate (5 loops) · Feature: DOOM-0206 (`docs/specs/DOOM-0206-menu-redesign.md`).

## Context

The menu redesign (DOOM-0206) wants crisp, HUD-safe text on the Solid/Ultra
tiers instead of the legacy 320×200 software-rendered bitmap font stretched to
display resolution (blurry/blocky at 1080p+). That needs some way to turn TTF
glyph outlines into pixels the Vulkan back-end can draw, which is a new
rendering dependency the documentation standard's hard-architectural-choice
rule wants recorded here, not just in the spec.

## Decision

**Vendored `stb_truetype.h`** (public-domain single-header, v1.26) baked at
**display resolution** into an R8 glyph atlas at menu-open time, drawn by a
textured-quad pass templated on the existing overlay pipeline (`overlay.vert`/
`overlay.frag`) — same vendored-single-header pattern as `stb_image.h` (ADR
0002): the engine already links no font/text library, and `stb_truetype.h`
adds no new link surface (source-only, header-only implementation TU).

Task 1 (this ADR) delivers only the CPU-side, engine-independent half: the
vendored header plus a pure-logic atlas-bake API (`rb_text.h`/`rb_text.c`,
mirroring `rb_image.h`/`rb_image.c`'s shape — a declarations-only public
header plus one small C translation unit that defines
`STB_TRUETYPE_IMPLEMENTATION`, scope-silencing the vendored library's
`-Wunused-function` noise the same way `rb_image.c` does for `stb_image.h`).
The GPU upload, textured-quad pass, and menu-code call sites are later DOOM-0206
tasks; this ADR's decision covers the rendering *approach*, which those tasks
implement.

Font choice (a bundled OFL font, Oxanium) is fetched in Task 5 and is a
separate, lower-stakes decision (asset choice, not architecture) — not
repeated here.

## Consequences

- One more vendored header to keep current on the dependency sweep cadence
  (recorded in `docs/standards/dependencies.md`), traded for zero new link
  dependency.
- Baking at display resolution (not a fixed offline size) means the atlas is
  rebuilt when the resolution or render scale changes — cheap (menu-open-time,
  not per-frame) and keeps text crisp at any display size, matching the user's
  requirement.
- The atlas-bake logic is pure CPU logic (no engine/GPU deps), so it is
  unit-testable in isolation (`tests/rb_text_test.cpp`) the same way
  `rb_image_test.cpp`/`rb_materials_test.cpp` already are.

## Alternatives rejected

(The first three carry forward the text-rendering-relevant subset of DOOM-0206
spec §9 — its fourth item, restructuring into tabbed/side-panel navigation, is
a menu-*structure* alternative out of scope for this rendering-approach ADR.
The fourth below is specific to this decision, paralleling ADR 0002's
SDL2_image rejection.)

- **Pre-baked bitmap font atlas (offline PNG)** instead of a runtime
  `stb_truetype` bake — rejected: adds an offline asset pipeline and freezes
  the glyph size; a runtime bake matches the display resolution with less
  tooling.
- **Keep drawing into 320×200 and just reposition** — rejected: the user
  explicitly wants crisp text, which the 320×200 upscale cannot give.
- **Per-frame CPU rasterization of the whole menu to a display-res texture** —
  rejected: a large per-open texture upload and no real benefit over a glyph
  atlas + quads.
- **SDL2_ttf** — rejected for the same reason `stb_image.h` beat SDL2_image in
  ADR 0002: a heavier, new link dependency for what one public-domain header
  covers with zero link surface.
