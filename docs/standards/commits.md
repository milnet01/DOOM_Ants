# Commit Standard

## The format

Every commit subject is:

```
<ID>: <description>
```

- **`<ID>`** — the roadmap item the commit serves, e.g. `DOOM-0003`. For work
  with no roadmap item (small chores, docs typo), use a short kind tag instead:
  `chore`, `docs`, `fix`.
- **`<description>`** — imperative mood, lower-case start, no trailing period.
  "add SDL2 video backend", not "Added SDL2 video backend." or "adds…".

Examples:

```
DOOM-0003: fix 64-bit integer truncation in r_data.c
DOOM-0004: replace X11 framebuffer with SDL2 surface
docs: correct WAD path in README
```

## One concern per commit

Each commit does **one** thing. If the subject needs an "and", it's probably two
commits. Small, focused commits make history readable and reverts surgical.

## Body (optional)

Add a body when the *why* isn't obvious from the subject. Wrap at ~72 columns,
separated from the subject by a blank line. Explain the reason and any
trade-off, not the mechanics the diff already shows.

## Attribution

Commits made with AI assistance end with a trailer:

```
Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>
```

## Pushing

Commit locally freely. This is a **public** repo, so pushing is fine after each
logical batch (public repos get free CI minutes). Push when a unit of work is
complete and the tree is clean.
