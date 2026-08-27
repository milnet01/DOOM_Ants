# Documentation Standard

Good docs exist to remove ambiguity. Every document here should let a reader act
without having to ask the author what was meant.

## What lives where

| Document | Purpose |
|----------|---------|
| `README.md` | The front door — what the project is, for newcomers. |
| `CLAUDE.md` | Project conventions, read by every working session. |
| `ROADMAP.md` | The plan: what's planned, in progress, shipped, considered. |
| `CHANGELOG.md` | What actually shipped, per release (Keep a Changelog). |
| `docs/standards/` | The house rules (this folder). |
| `docs/specs/` | One design doc per large feature, written **before** building it. |
| `docs/plans/` | The build order for a spec, when it needs one written down separately. |
| `docs/decisions/` | ADR-style notes recording *why* a hard architectural choice went the way it did (numbered `NNNN-topic.md`). |

## Writing rules

Write so a tired reader gets it on the first pass.

**Plain language is not a courtesy — it is a bug-finding tool.** A muddled sentence
hides a muddled idea. Forcing the simple version is what makes the gap in the thinking
visible. Several of the worst defects caught in this project's specs were sitting inside
sentences nobody could parse; once the sentence was split, the error was obvious.

- **Plain language first.** Lead with what a change means for a player or a
  reader, then the mechanism if it matters. Define jargon inline on first use.
- **Concrete over abstract.** Name the file, the function, the button. Show a
  short example rather than describing one.
- **Short sentences, short paragraphs.** No walls of text. One idea per sentence.
  If a sentence has to be re-read to parse, split it.
- **Say it once.** State a rule in the place it belongs and cross-reference from
  everywhere else. Three copies of a rule become three *different* rules the first
  time one of them is edited.
- **Cut ornament, keep precision.** Drop the theorem name, the hedge stacked on a
  hedge, the parenthetical that breaks the sentence's spine. Never cut a number, a
  symbol name, or a condition to make prose shorter — if brevity would cost
  correctness, leave it long and say why.
- **A table cell holds a line, not a paragraph.** When a cell outgrows a line or two,
  the content wants a list underneath the table.
- **State the why.** A doc that records a decision must say *why* the decision
  went that way, so a future reader doesn't reopen a settled question.

**The read-aloud test.** Read the passage out loud. If you run out of breath, or have
to back up to find the subject, rewrite it — and check the idea underneath while you're
there, because that is usually what the tangle was hiding.

### Karpathy's principles, applied to documents

The same four rules the code follows. A doc is a contract; it earns the same discipline.

| Principle | What it means in a document |
|-----------|------------------------------|
| **Think before writing — no silent assumptions** | Every claim about the code is checked against the source *before* it is written. Where the doc genuinely can't know, it says so plainly instead of sounding confident. |
| **Simplicity first** | The shortest wording that is still exact. A worked example beats an abstract description. If a section can be a table, make it a table. |
| **Surgical changes** | Edit the passage the change touches. Don't reflow, re-title or re-argue the sections around it on the way past. |
| **Goal-driven — explicit verification** | Every requirement says how it will be checked. "Works correctly" is not a criterion; "fog-off renders byte-identical" is. |

## Specs (design docs)

Any large or non-obvious feature gets a spec in `docs/specs/` **before**
implementation — the spec is the contract the implementation must satisfy. A
spec covers: the goal, the approach (with at least one alternative considered),
the affected files/subsystems, and how it will be verified. Keep claims about
the existing code honest — check the source, don't rely on memory.

`spec-format.md` owns the layout — which `##` sections a spec carries, in which
order, and which of them `spec_lint` checks.

A *hard architectural choice* (language, API, protocol, storage format) is
recorded in **both** places: the spec captures the **design** (how it works);
an ADR in `docs/decisions/` captures the **decision** (what was chosen and
*why*, with the alternatives rejected). The ADR is the canonical home for the
rationale — the spec references it rather than restating it, so the two can't
drift.

## Citing code from docs

When a doc points at code, **name the thing — don't count the lines.** A symbol
name survives edits made above it; a line number doesn't.

- **Cite the symbol.** Function, struct, constant, shader entry point, config
  key: `R_DrawColumn()`, `RtPushConstants`, `kFogBaseDensity`, `marchFog()`.
- **No symbol at the site?** Quote a short, distinctive line of the code instead
  — `if (committed && !isSky)`. The quote is then the locator.
- **A line number is a hint, never the locator.** If one genuinely helps a reader
  find a site in a 9000-line file, write it as approximate and keep the symbol or
  quote beside it — `RB_Vulkan_BuildLevel` (`r_vulkan.cpp:~7169`). A bare
  `file:line` with nothing else is not a citation.
- **Never edit at a raw line number.** Search for the symbol or the quoted text,
  confirm it's the site you meant, *then* edit.
- **If a doc pins exact line numbers anyway**, name the commit they were verified
  against, so a later reader knows how far to trust them.
- **Re-anchor as you pass.** When you edit a passage that carries a bare line
  number, convert it to a symbol citation in that same change. No separate
  cleanup pass — those never happen.

*Why this is a rule and not a preference:* DOOM-0011's spec carried about 50
`file:line` citations. Two unrelated commits shifted `r_vulkan.cpp` by 4–6 lines
and `pathtrace.comp` by 2, and every one of the 50 had to be re-checked — three
had drifted onto entirely unrelated code. A citation that points confidently at
the wrong thing is worse than none, because a reader has no reason to doubt it.
Symbol names would have survived both commits untouched.

## Keeping docs true

Documentation rots faster than code. When a change makes a doc wrong, fix the
doc in the **same** change. A confidently wrong doc is worse than no doc.
