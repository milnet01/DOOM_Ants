# Roadmap Format

`ROADMAP.md` is the project's plan and the single source of truth for what's
planned, in progress, shipped, or merely being considered. It is read and
written by the **Ants MCP** roadmap tools (`roadmap_query`, `roadmap_log`), so
its format must stay exactly as described here or the tooling stops parsing it.

## Sections

Phases are `##` headings. Each heading's **slug** is its title lowercased with
non-alphanumerics turned into hyphens — e.g. `## Phase 1 — Build, Modernise &
Share` → `phase-1-build-modernise-share`. The Ants tools address sections by
slug; get the canonical slug from `roadmap_query` with `mode:section_index`.

## Items

Each actionable item is a top-level bullet in this exact shape:

```
- 🚧 [DOOM-0001] **Headline ending in a period.**
  **Layman:** One plain-English sentence for non-technical readers.
  Kind: doc.
  Source: in-session-2026-06-11.
```

- **Status emoji** (first token): `📋` planned · `🚧` in progress · `✅` shipped ·
  `💭` considered.
- **`[DOOM-NNNN]`** — the permanent ID (see below).
- **Headline** — bold, one line, ends with a period.
- **`Layman:`** — optional but encouraged; a one-sentence plain summary.
- **`Kind:`** — work category: `doc`, `fix`, `refactor`, `feature`, `test`,
  `chore`, `release`, `perf`, `security`, etc.
- **`Source:`** — where the item came from (`in-session-<date>`,
  `user-request-<date>`, …).

## IDs

- Every actionable item carries a `[DOOM-NNNN]` ID, zero-padded to four digits.
- IDs are **append-only**: never renumber, never reuse, even if an item is
  dropped. The next ID is one higher than the largest ever used.
- `.roadmap-counter` holds the highest number allocated so far. Keep it in sync.

## Ants MCP usage — IMPORTANT (don't deviate)

The Ants roadmap tools have a few sharp edges this project standardises around:

1. **Always use the `DOOM-` prefix.** The default `roadmap_log op:append`
   (counter strategy) emits **`ANTS-NNNN`** IDs — it ignores the project name.
   Do **not** accept those. To append through the tool, pass
   `id_strategy:"stable_prefix"` with an explicit `stable_id:"DOOM-NNNN"`. If a
   tool call produces an `ANTS-` ID, correct it to `DOOM-` immediately (the ID
   is not yet "established" the instant it's created).

2. **Case-sensitive slugs and IDs.** `roadmap_query` / `roadmap_log` reject
   off-case slugs and IDs with `bad_case`. Always use the exact lowercase slug
   (e.g. `phase-2-the-spin`) and the exact `DOOM-NNNN` casing.

3. **Bulk authoring.** `append_batch` does not take a per-item `stable_id`, so
   adding several `DOOM-` items at once is done by editing this file directly in
   the format above, then confirming it parses with
   `roadmap_query mode:headline_only`.

4. **Status changes** use `roadmap_log op:flip` (locate by `id:"DOOM-NNNN"`),
   which also injects a `^doom-nnnn` anchor on first touch — that's expected.

When the tooling and this standard disagree, **this standard wins** — log the
discrepancy in the Ants MCP feedback file rather than silently following the
tool.
