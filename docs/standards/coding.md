# Coding Standard

The rules below keep the codebase legible as it modernises. They are
deliberately short — add to them only when a real decision forces the issue,
not pre-emptively.

## Guiding principles

1. **Shortest correct implementation.** 50 lines that work and read clearly
   beat 250 lines of scaffolding for a future that may never arrive.
2. **Reuse before rewriting.** Before adding code, look for existing code that
   already does the job — call it, or extend it, before duplicating it.
   (Rule of Three: extract a shared helper on the *third* copy, not the first.)
3. **No silent workarounds.** Don't paper over a warning, a crash, or a failing
   build with a `// TODO`, a disabled check, or a commented-out block. Fix the
   root cause. If a workaround is genuinely unavoidable, leave a comment naming
   the constraint so it reads as deliberate.
4. **The six-month test.** Could someone (including you) open this file in six
   months and understand *why* it looks this way without asking the author? If
   not, it's too clever or too long.

## Working in the legacy engine (C)

The original DOOM is C from 1997. While we still live in that code:

- **Match the surrounding style.** Brace placement, naming (`R_DrawColumn`,
  `p_mobj`, the `*_t` type suffix), and file layout follow the existing file —
  even where you'd personally write it differently. Consistency beats taste.
- **Touch only what the task needs.** No drive-by reformatting, renaming, or
  "while I'm here" cleanups. Unrelated changes hide the real diff.
- **Preserve original behaviour** unless the task is explicitly to change it.
  DOOM's feel is the product; gameplay-affecting changes are deliberate, never
  accidental.
- **Modern toolchain, original logic.** It's fine — expected — to fix code that
  no longer compiles under a modern 64-bit compiler (integer sizes, implicit
  declarations, headers). That's repair, not rewriting.

## The new renderer (Phase 2)

The renderer-backend seam (DOOM-0026) is designed and being built
(2026-06-15); the 3D renderer itself (DOOM-0008..0012) remains
`💭 considered` — not begun. The foundational decisions for it are below;
**why** each went the way it did is owned by ADR
`docs/decisions/0001-renderer-language-and-api.md`, and the full architecture is
in `docs/specs/DOOM-0026-renderer-backend.md`:

- **Graphics API:** Vulkan, as a hybrid (rasterise, then hardware ray tracing
  for shadows and reflections).
- **Language:** the engine stays **C**; the Vulkan back-end is **C++**, isolated
  behind the plain-C `renderer_backend_t` seam.
- **Shading language:** **GLSL**, compiled ahead-of-time to SPIR-V.

These hold for all Phase-2 renderer work; revisit only with a new ADR. See
ADR 0001 for the rationale and alternatives.

## External libraries

- Prefer the **latest stable** release of any library we pull in (SDL2/3,
  Vulkan headers, etc.) unless there's an explicit, written reason to pin older.
- Use the **current idioms** for that version — not the API you remember from
  three years ago, even if it still compiles.
