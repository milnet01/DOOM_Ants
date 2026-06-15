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
| `docs/decisions/` | ADR-style notes recording *why* a hard architectural choice went the way it did (numbered `NNNN-topic.md`). |

## Writing rules

- **Plain language first.** Lead with what a change means for a player or a
  reader, then the mechanism if it matters. Define jargon inline on first use.
- **Concrete over abstract.** Name the file, the function, the button. Show a
  short example rather than describing one.
- **Short sentences, short paragraphs.** No walls of text.
- **State the why.** A doc that records a decision must say *why* the decision
  went that way, so a future reader doesn't reopen a settled question.

## Specs (design docs)

Any large or non-obvious feature gets a spec in `docs/specs/` **before**
implementation — the spec is the contract the implementation must satisfy. A
spec covers: the goal, the approach (with at least one alternative considered),
the affected files/subsystems, and how it will be verified. Keep claims about
the existing code honest — check the source, don't rely on memory.

A *hard architectural choice* (language, API, protocol, storage format) is
recorded in **both** places: the spec captures the **design** (how it works);
an ADR in `docs/decisions/` captures the **decision** (what was chosen and
*why*, with the alternatives rejected). The ADR is the canonical home for the
rationale — the spec references it rather than restating it, so the two can't
drift.

## Keeping docs true

Documentation rots faster than code. When a change makes a doc wrong, fix the
doc in the **same** change. A confidently wrong doc is worse than no doc.
