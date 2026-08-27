# Spec Format

The shape of a DOOM_Ants spec, and the file `spec_lint`'s `missing_section`
check reads. Whether a feature needs a spec at all is settled elsewhere — see
*Relationship to the global standard* below. This file starts once that answer
is yes.

## Where a spec lives

One spec per feature, at `docs/specs/<ID>-<topic>.md`, where `<ID>` is the
permanent `DOOM-NNNN` from the roadmap.

## The section run

A spec opens with an unnumbered `## Contents`, then runs its numbered `##`
sections in ascending order. Nothing checks either; they are the shape the
corpus settled on.

**Sections 1 to 10 are all required.** The ones in this block are checked
verbatim, and for those the number is part of the heading:

<!-- required-sections -->
```
## 1. Goal
## 2. Where this sits
## 4. Design
## 5. Data & resources
## 6. Performance budget
## 7. Build order
## 8. Invariants
```

**That block is read by machine.** `spec_lint` takes its list from it exactly as
written, so edit it in the same commit as any change to the headings here.
Nothing but blank lines may sit between the marker and the fence: put a sentence
there and the list parses empty, the check is skipped, and every spec reports
clean. So read a corpus-wide zero as the check being off, not as a clean
corpus — `sections_checked` says which.

The match is on the whole heading line, so each of those headings carries its
number and its name and nothing else. A qualifier appended to one —
`## 4. Design — the two-pass form` — reads as a different section and is
reported missing. Put the qualifier in the body.

**Slots 3, 9 and 10 are required too, and nothing checks them.** The corpus does
not name them consistently, so no verbatim list can. Keep the number and take
the name from a recent spec: 3 is the problem or the scope decisions agreed with
the user, 9 is the alternatives rejected, 10 is the open questions. A qualifier
on these three is harmless, because nothing is matching them.

A section with nothing in it still gets its heading, with `none` under it — an
omitted section reads as an oversight, an explicit `none` reads as a decision.
That applies to sections 1 to 10 and to nothing else.

Sections 11 to 13 — `What checks this`, `Cross-doc impact`, `Cold-eyes loop
log` — are the author's, and a good default. The review gate requires a loop
log, so a spec that has been through it carries one wherever it lands.

`documentation.md` requires a spec to cover how it will be verified and which
files it touches. Neither has a slot of its own, so a spec carries them where it
can — but one that omits them is short of what that standard asks, whatever
`spec_lint` says.

## Invariants

Number them `INV-1` upward with no gaps. A spec split out of another inherits
the parent's ids unchanged instead, leaving the low numbers unused, so the
citations already aimed at them still land.

Each invariant carries a `*Test:*` clause naming what would catch a breach;
`spec_lint` reports one that does not. It matches that marker literally, with
single asterisks.

An invariant that is withdrawn or has moved stays in place rather than being
renumbered. Write the tombstone in one of the two forms `spec_lint` exempts from
the `*Test:*` rule, at the start of the invariant's body:

```
*moved to DOOM-NNNN*
*withdrawn — <reason>*
```

Single asterisks, a bare id for the first, an em dash for the second. Anything
else — bold, a link, a section reference, a hyphen, or the words inside a
sentence — is a live invariant that still owes a test.

## What this does not cover

`spec_lint` reads every document in `docs/specs/`, so residence decides what
gets linted while genre decides what the run governs. A build plan and a fix
ledger are not specs. Plans belong in `docs/plans/`; two of this project's sit
in `docs/specs/` beside its fix ledger, and all three report against the run
permanently.

Two earlier eras of spec cannot conform either. The pre-numbering specs use
unnumbered headings — Goal, Background, Approach, Components / affected files,
Verification, Out of scope (YAGNI), Cold-eyes loop log. DOOM-0009 and DOOM-0170
are numbered but assign the numbers differently. Neither era is being rewritten,
so `spec_lint` reports both against the run permanently. Read a
`missing_section` finding against the era its document comes from before acting
on it.

## Relationship to the global standard

`~/.claude/standards/spec-format.md` decides whether a feature needs a spec at
all (§ 1), and owns the writing conventions (§ 5) and the review gate (§ 6).
`documentation.md` says what a spec must cover. This file owns the layout alone,
and `spec_lint` prefers it because it is in-project.
