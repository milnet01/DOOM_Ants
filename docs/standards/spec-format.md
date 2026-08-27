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
sections in ascending order. Nothing checks either of those — they are the
shape the corpus settled on, not rules with an observable.

**Sections 1 to 10 are all required.** Seven of them are checked verbatim, and
for those the number is part of the heading:

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

The match is on the whole heading line, so each of those seven carries its
number and its name and nothing else. A qualifier appended to one —
`## 4. Design — the two-pass form` — reads as a different section and is
reported missing. Put the qualifier in the body.

**Slots 3, 9 and 10 are required too, and nothing checks them.** The corpus
does not name them consistently, so no verbatim list can. Keep the number and
take the name from a recent spec: 3 is the problem or the scope decisions
agreed with the user, 9 is the alternatives rejected, 10 is the open questions.
A qualifier on these three is harmless, because nothing is matching them.

A section with nothing in it still gets its heading, with `none` under it — an
omitted section reads as an oversight, an explicit `none` reads as a decision.
That applies to sections 1 to 10 and to nothing else.

Anything after 10 is the author's. The two newest specs add
`11. What checks this`, `12. Cross-doc impact` and `13. Cold-eyes loop log`,
and that is a good default.

`documentation.md` requires a spec to cover how it will be verified and which
files it touches. Neither has a slot of its own in the run above, so a spec
carries them where it can — but a spec that omits them is short of what that
standard asks, whatever `spec_lint` says.

## Invariants

Number them `INV-1` upward with no gaps. Each carries a *Test:* clause naming
what would catch a breach; `spec_lint` reports one that does not.

An invariant that is withdrawn or has moved elsewhere stays in place rather
than being renumbered, so the numbers other documents cite keep pointing at the
same thing. Write it as a tombstone whose text says *moved to* and where, or
*withdrawn* and why: `spec_lint` recognises those two phrasings and exempts
them from the *Test:* rule. Other wording is not recognised and draws a
finding.

## What this does not cover

A build plan and a fix ledger are not specs, and the run above does not apply
to them. Plans live in `docs/plans/`. A fix ledger has no declared home, and
the one this project has sits in `docs/specs/`, so it reports against the run
and will keep doing so.

Two earlier eras of spec cannot conform either. The pre-numbering specs use
unnumbered headings — Goal, Background, Approach, Components / affected files,
Verification, Out of scope (YAGNI), Cold-eyes loop log. DOOM-0009 and
DOOM-0170 are numbered but assign the numbers differently. Neither era is
being rewritten, so `spec_lint` reports both against the run above
permanently. Read a `missing_section` finding against the era its document
comes from before acting on it.

## Relationship to the global standard

`~/.claude/standards/spec-format.md` § 1 decides whether a feature needs a spec
at all, and owns the review gate and the writing conventions. `documentation.md`
says what a spec must cover. This file owns the layout alone, and `spec_lint`
prefers it because it is in-project.
