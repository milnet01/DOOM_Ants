# Spec Format

The shape of a DOOM_Ants spec. `documentation.md` says *when* a spec is needed
and what it must cover; this file owns its layout, and it is what `spec_lint`'s
`missing_section` check reads.

## Where a spec lives

One spec per feature, at `docs/specs/<ID>-<topic>.md`, where `<ID>` is the
permanent `DOOM-NNNN` from the roadmap.

## The section run

A spec is a numbered run of `##` sections. These are required, and are checked
verbatim — the number is part of the heading:

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

The match is on the whole heading line, so a required heading carries its number
and its name and nothing else. A qualifier appended to the heading —
`## 4. Design — the two-pass form` — reads as a different section and is
reported missing. Put the qualifier in the body.

Three more slots belong to the run and are *not* machine-checked, because the
corpus spells each of them two ways. Keep the number; either spelling is fine:

- **3** — `The problem, precisely`, or `Scope decisions (agreed with the user)`
- **9** — `Alternatives considered`, with or without `(and rejected)`
- **10** — `Open questions`, plain or qualified

Anything after 10 is the author's. Recent specs add `11. What checks this`,
`12. Cross-doc impact` and `13. Cold-eyes loop log`, and that is a good default.

A section with nothing in it still gets its heading, with `none` under it — an
omitted section reads as an oversight, an explicit `none` reads as a decision.

## Invariants

Number them `INV-1` upward with no gaps. Each carries a *Test:* clause naming
what would catch a breach; `spec_lint` reports one that does not. A withdrawn
invariant stays in place as a tombstone rather than being renumbered, so the
numbers other documents cite keep pointing at the same thing.

## What this does not cover

A build plan and a fix ledger are not specs, and the run above does not apply to
them. Some sit in `docs/specs/` from before this rule and `spec_lint` reports
them against it until they move.

Specs written before the numbering convention use unnumbered headings — Goal,
Background, Approach, Components / affected files, Verification, Out of scope
(YAGNI), Cold-eyes loop log. They are accurate records of what was built and are
not being rewritten, so `spec_lint` reports each of them as missing every
required section. Read a `missing_section` finding against the era the spec
comes from before acting on it.

## Relationship to the global standard

`~/.claude/standards/spec-format.md` covers the wider question — whether a
feature needs a spec at all, the review gate, and the writing conventions. This
file overrides it on structure alone, and `spec_lint` prefers it because it is
in-project.
