# Review & QA Standard

This project leans on a few review disciplines to stay correct as it changes.
They live here so they survive independently of any one contributor's tooling
setup. The short version: **specs are reviewed before they're built, code is
audited on a cadence, and every finding is verified against the source before
anyone acts on it.**

## Cold-eyes gate for specs

Every new or edited **spec / design doc / ADR** (`docs/specs/`,
`docs/decisions/`, named design docs) runs through the `/cold-eyes` review
**before implementation**, and loops until a pass returns zero verified
findings. A spec is the contract the implementation must satisfy — if the
contract is wrong, the code is wrong by construction, so cold-eyes-then-build is
one round-trip where build-then-discover is two.

- **Run it before implementing, not after.**
- **Loop 2+ runs cold** — don't brief the reviewer on the previous loop's
  findings or fixes. An issue that isn't raised again is the proof the fix held;
  one that resurfaces proves it didn't.
- **Verify and fix every actionable severity** (critical → low). Only INFO is
  left for follow-up. A low finding that turns out to be wrong is dropped
  *explicitly*, with the reason — never silently filtered.
- **Convergence:** stop when a pass yields zero verified fixes, or only verified
  polish. Keep looping while any structural or mechanical fix remains.

This gate is for multi-file design docs. Tiny per-feature test contracts
(`tests/features/<name>/spec.md`) don't need it — a self-read is enough.

## Audit & independent review on a cadence

Two complementary sweeps, run periodically (a good trigger is the start of a
release cycle):

- **Static audit** — cppcheck / semgrep / ruff / bandit and friends, via the
  Ants `audit_run` tool. Cheap; catches mechanical defects.
- **Independent review** — a multi-lane read of the codebase (Ants
  `indie-review`), weighted toward the code that matters most (here, the Vulkan /
  RT backend and the untrusted-input parsers). This is token-heavy — run it a
  measured number of passes, not on a tight loop.

For both: **verify every finding against the current source before fixing it.**
Fold in the verified fixes across all severities; **defer the rest to the
roadmap** rather than dropping them.

## Verify, don't recall

Any claim in a spec, a review, or a fix description that names a function, file,
line, constant, or version-specific behaviour must be backed by a grep/read
against the *current* source — not memory, not "this is probably how it works".
The cost of one extra grep is dwarfed by the cost of a spec built on a wrong
assumption. When the answer isn't on disk (it's about intent or scope), stop and
ask rather than guess.

## Log false positives

A finding you verify is **not** a real bug gets recorded — with the reason — in
`.ants_review_falsepos.jsonl` (via `audit_falsepos_log`). This stops the next
sweep from re-raising the same noise. Vanilla-DOOM quirks that are deliberately
preserved are the common case here.

## Found an issue? Roadmap it

Whenever a review — or ordinary work — turns up a bug or a rough edge that you
aren't fixing right now, **add it to the roadmap** (with the right `Kind:`) so it
is tracked. Don't silently fix things outside the task's lane, and don't let a
found issue evaporate because nobody wrote it down. Bug fixes follow
reproduce-before-fix (see the testing standard).
