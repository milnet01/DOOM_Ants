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
sections in ascending order. Both are required, and nothing checks them.

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
there and the list parses empty and `missing_section` stops running. Every other
check keeps firing, so a broken block does not look quiet — `sections_checked`
coming back false is the only tell.

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
log` — are unchecked, and a good default. The review gate requires a loop log,
so a spec that has been through it carries one wherever it lands.

`documentation.md` requires a spec to cover how it will be verified and which
files it touches. Verification's home is section 11; which files it touches has
no slot at all. A spec that omits either is short of what that standard asks,
whatever `spec_lint` says.

## Invariants

Number them `INV-1` upward with no gaps. A spec split out of another inherits
the parent's ids instead and never renumbers, so the citations already aimed at
them still land — gaps included, wherever the inherited run is not contiguous.
Say so in the section's opening line, as DOOM-0310 does.

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

An earlier era of spec cannot conform either. The pre-numbering specs use
unnumbered headings — Goal, Background, Approach, Components / affected files,
Verification, Out of scope (YAGNI), Cold-eyes loop log. DOOM-0009 is numbered
but assigns the numbers differently. Neither is being rewritten, so `spec_lint`
reports both against the run permanently.

DOOM-0170 is not one of them: it keeps the numbers and fails on the qualifier
rule above plus renamed headings at 4 and 5, so most of its findings are the
ordinary fixable kind. Read a `missing_section` finding against the document it comes
from before deciding it is permanent.

## Relationship to the global standard

`~/.claude/standards/spec-format.md` decides whether a feature needs a spec at
all (§ 1), and owns the writing conventions (§ 5) and the review gate (§ 6).
`documentation.md` says what a spec must cover. This file owns the layout alone,
and `spec_lint` prefers it because it is in-project.
