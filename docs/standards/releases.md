# Releases & Versioning Standard

A release is a build we hand to players. This standard covers how it is
numbered, what it ships, and how it is cut — so releases are reproducible and
the version number always means the same thing.

## Version numbers

DOOM_Ants uses a `MAJOR.MINOR.PATCH` marketing version (e.g. `0.5.0`).
Pre-1.0, the convention is:

- **MINOR** (`0.5.0` → `0.6.0`) — a batch of new player-facing features shipped.
- **PATCH** (`0.5.0` → `0.5.1`) — fixes and polish, no headline feature.
- **MAJOR** stays `0` until the engine is feature-complete against the roadmap's
  Phase 2 goals.
- **Pre-releases** carry a `-` suffix (`0.6.0-pre.1`); `release.sh` publishes any
  `-`-suffixed version as a GitHub pre-release, everything else as Latest.

> **Don't confuse it with the engine's internal `VERSION`.** `doomdef.h` defines
> `VERSION = 110` — that is id Software's original *DOOM 1.10* number, used for
> demo/netgame compatibility. It is **not** our release version and does not
> move when we release.

## The version lives in three places, in lockstep

- the **git tag** `v<ver>`,
- the **`CHANGELOG.md`** heading (`## [<ver>] - <date>`),
- the **`README.md`** "Latest release" line.

They move **together**, driven by the release tool — never by hand, one at a
time. Keeping them in lockstep is the rule `CLAUDE.md` refers to; this is its
home.

**The tool brings each leg up to date wherever it is not already**, and the
CHANGELOG's `[Unreleased]:` / `[<ver>]:` compare links travel with the heading as
part of that leg. So a section dated by hand ahead of the release — `changelog_log`'s
`release` operation does exactly that — no longer costs you the other three.
Letting `release.sh` promote `[Unreleased]` is still the simplest route; it is no
longer the only correct one (DOOM-0357).

**A leg that did not move blocks the tag.** Before tagging, `release.sh` checks
that the CHANGELOG heading, its compare link and README's line all read the
version being tagged, and refuses to tag if one lags — which also catches a leg
edited by hand to the wrong value.

**README's line tracks the latest *stable* version.** It is what a player
arriving at the repo should download, so a `-`-suffixed pre-release deliberately
leaves it pointing at the last plain `X.Y.Z`. The other legs still move for a
pre-release. Decided 2026-08-19 with DOOM-0357, which also removed the older
failure where a pre-release wrote a line the tool could no longer recognise and
aborted the *next* release.

## CHANGELOG

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com). Shipped
roadmap items graduate into it under `## [Unreleased]`, grouped by category
(Added / Changed / Fixed / Security / …). Use the `changelog_log` Ants tool
(it is Keep-a-Changelog-aware) rather than hand-editing. `release.sh` promotes
`[Unreleased]` to `[<ver>] - <today>` at release — do not do it yourself, and
see the lockstep section for why.

## Cutting a release

One command does it all — don't tag or upload by hand:

```sh
packaging/release.sh <ver>                                # build both artifacts locally, no publish
packaging/release.sh <ver> --publish --theme="<one line>" # + promote changelog, tag, push, GitHub release
```

`--rebuild` forces a clean rebuild. It is not needed for correctness: an
artifact is reused only when it was built from the commit being released. See
the stale-artifact note below for what that buys and how it was learned.

`--theme` sets the release commit subject (`<ver>: <theme>`). It does **not**
set the GitHub release title: that is always `DOOM_Ants <ver>`, with the theme
appended after an em dash. Without it the commit reads `<ver>: release` and the
title is unsuffixed.

In order, `release.sh`:

1. runs `make test` as a gate — a failing test stops the release before anything
   is built or tagged;
2. builds the **Linux AppImage** (`packaging/build-appimage.sh`);
3. builds and zips the **Windows** cross-build;
4. with `--publish`: brings the CHANGELOG heading, its compare links and
   README's "Latest release" line up to `<ver>` — each one only where it is not
   already — commits whatever moved, refuses to tag if any leg still lags or if
   the CHANGELOG section for `<ver>` is empty, then
   tags `v<ver>`, pushes branch + tag, creates the GitHub release with both
   artifacts attached and the CHANGELOG section as notes, and finally
   re-downloads both published assets and checks they are byte-identical to what
   it built.

Requirements: the mingw-w64 cross toolchain + staged libs (`mingw-deps/`), the
AppImage toolchain (auto-fetched), `zip`, and an authenticated `gh`.

> **A release can ship a binary that is not what you tagged, and almost nothing
> catches it.** `release.sh` used to decide an artifact was already built by
> testing for a file of that name, and the name carries only the version. So a
> build-only run followed by a `--publish` run uploaded the *first* build, and
> any commit made in between was missing from what shipped. Nothing else caught
> it: `make test` and `packaging/ci-local.sh` both check the tree, not the
> artifact, and the tag, the CHANGELOG and the release all came out correct.
> Observed on 0.7.1, where the published binaries predated the fix the release
> was cut for — **both** artifacts, not just one.
>
> **Two checks now stand in the way (DOOM-0356), and they are a pair.** Each
> build stamps `<artifact>.commit` with `git rev-parse HEAD`, and an artifact is
> reused only when that stamp equals the commit being released — so the local
> artifact provably comes from HEAD. Then, after publishing, `release.sh`
> re-downloads both assets and compares them byte-for-byte with what it built —
> so the published asset provably is that artifact. A dirty tree matches no
> stamp and always rebuilds.
>
> **0.7.1 is the only release this happened to.** Every earlier release was
> re-checked on 2026-08-20 and all ten are clean. Rebuilding a tag and comparing
> bytes proves nothing — both artifact formats embed timestamps, so a correct
> release fails that comparison months later. What works is opening the shipped
> binary and reading two things: its stored build time against the version
> commit the tag points at (every stable release built within ~1 minute of it),
> and whether it carries string literals the diff for its own commit window
> added, with the previous release's binary as the control. Evidence on
> DOOM-0356.
>
> **To inspect an artifact by hand anyway**, look for something only the new code
> has — an imported symbol, a new string — rather than trusting the timestamp.
> They open differently: `unzip` the Windows zip, but an AppImage is not a zip, so
> use `./<name>.AppImage --appimage-extract` and inspect what lands in
> `squashfs-root/`. Not `--appimage-extract-and-run`, which unpacks to a temp
> directory, *runs* the game and cleans up — it leaves nothing to inspect and
> takes over the display.
>
> **When an asset turns out wrong, replace it — do not re-tag.** `release.sh`
> refuses a second `--publish` of an existing tag, and the tag is not the thing
> that is wrong. Rebuild with `--rebuild`, then
> `gh release upload <tag> --repo <owner>/<repo> --clobber <assets>`, leaving the
> tag where it is. This is the one sanctioned exception to "don't tag or upload
> by hand" above, and it is what 0.7.1 used.

> **`gh` repo gotcha:** this repo is a fork of `id-Software/DOOM`, so `gh`
> defaults to the parent. **Hand-run `gh` commands** — the asset replacement
> above, `gh release view` — must target the fork explicitly with
> `-R milnet01/DOOM_Ants` (or `--repo`). `release.sh` needs no such flag and
> accepts none: it derives the slug from `origin` itself (line 158) and passes
> `-R` on every `gh` call it makes.

## What ships, and when

- **Artifacts:** a Linux AppImage and a Windows zip. The end-user runtime library
  list is canonical in `docs/RELEASE_README.txt` — keep it current (see the
  dependencies standard).
- **No game data.** WADs are never bundled (licensing) — players bring their own.
- **When:** cut a release when a coherent batch of shipped roadmap items is worth
  putting in players' hands. This is a **public** repo, so pushing and publishing
  are free — release after each clean, complete batch rather than hoarding.

## Cold-eyes loop log

- **2026-08-19 — first gate on this standard (`review-contract`, genre pinned
  `standard`; 3 cold lanes, one loop).** Triggered by adding the stale-artifact
  rule after the 0.7.1 release shipped pre-fix binaries. **Q1 2 · Q2 2 · Q3 1 —
  five verified, five fixed, none dismissed.** `doc_integrity` and
  `doc_citations` were clean before and after.
  - **All three lanes independently found the same Q1**, the strongest signal in
    the run: "they move **together**, driven by the release tool" is false on one
    of `release.sh`'s two paths — the README bump, the `git add` and the commit
    all sit inside the `else` of the CHANGELOG-already-promoted guard, while the
    tagging sits outside it. Filed as DOOM-0357 and qualified here; not hit on
    0.7.1, because the section was still `[Unreleased]` when the tool ran.
  - **Q2, two lanes:** the mandated `--rebuild` appeared only in a warning below
    the usage block a conformer copies from, and `.claude/bump.json`'s
    `release_command` — the recorded canonical publish invocation — carried
    neither. Both now say so.
  - **Q2, one lane:** the `gh` gotcha demanded `-R` on "release commands", but
    `release.sh` derives the slug from `origin` (line 127) and rejects unknown
    flags, so appending `-R` to it is an error. Scoped to hand-run `gh`.
  - **Q3, one lane:** the new check said what to verify and nothing about failing
    it, while "don't tag or upload by hand" forbade the only remedy and
    `release.sh` refuses a second publish of an existing tag. The
    `gh release upload --clobber` route 0.7.1 actually used is now written down
    as the sanctioned exception.
  - Two lane open questions resolved clean and are not findings: `set -euo
    pipefail` is in force (line 28), so the `make test` gate really does stop the
    run; and `--publish` / `--rebuild` are independent case arms, so combining
    them is safe in either order.

- **2026-08-19 — loop 2 (3 cold lanes, same brief, rebuilt from disk).**
  **Q1 2 · Q2 2 · Q3 1 — five verified, five fixed, none dismissed.**
  **Two of the five were loop 1's own additions**, which is the expected shape:
  new assertive text is the blast radius.
  - **All three lanes found the same Q2**, and it was self-inflicted: loop 1 put
    `--rebuild` into the usage block but left the warning below still describing
    "the two commands above" as the unsafe sequence — so the document said the
    mandated flag did not help. The unsafe invocation is now named as the one
    *without* the flag.
  - **Q1, two lanes:** loop 1's "or a second publishing run of the same version"
    cannot happen — the tag guard exits 1 before the promote branch is reached,
    which the document itself said twenty lines later. Clause deleted.
  - **Q1, one lane:** a `-`-suffixed pre-release writes a README line the guard
    no longer matches, so the *next* release aborts. Verified by running the
    guard's own grep against both forms. Folded into DOOM-0357.
  - **Q2, one lane:** `--theme` was absent from the usage block while
    `.claude/bump.json` treats it as canonical, so two conformers cutting one
    release produced different commit subjects.
  - **Q3, one lane:** the verification step said "unzip the asset", and an
    AppImage is not a zip — so the check covered one of the two shipped
    artifacts, and 0.7.1 shipped **both** stale. Confirmed by running `unzip`
    against the published AppImage (fails) and `--appimage-extract` (works).
  - Open questions resolved clean, not findings: `gh release create` does pass
    `-R` (line 189); a build-only run exits before the publish guards (line 117),
    so the documented `--rebuild` recovery is reachable; `changelog_log` exists
    and is Keep-a-Changelog-aware.

- **2026-08-19 — loop 3 (3 cold lanes, same brief). CAP REACHED (3 for a
  standard): the run files its tail and ships.** **Q1 3 · Q2 1 · Q3 1 — five
  verified, five fixed, one dismissed.**
  - **This was a VIOLENT cap, and that is the honest reading.** Four of the five
    findings — arguably all five — landed on text *this run* wrote, and the same
    was true of loop 2. Nothing in the run suggested a fourth loop would stop:
    each fix pass added assertive prose and the next loop found defects in it.
    The body grew 90 → 150 lines. The lesson is 4a-min's, learned the expensive
    way: **the cheapest fix is the one that writes no new text**, and this run
    kept reaching for explanation instead of deletion.
  - **All three lanes** found that the pre-release paragraph named a hazard and
    gave no recovery, while the document elsewhere both sanctions pre-releases
    and forbids the only repair. Now an explicit "do not cut one until DOOM-0357
    lands", with the hand-restore named as a second sanctioned exception.
  - **Q1, two lanes:** loop 2's claim that `--theme` "sets the GitHub release
    title" is false — `TITLE="DOOM_Ants $VERSION"` then `TITLE="$TITLE — $THEME"`
    appends it. A conformer writing a self-contained theme would have shipped a
    doubled title.
  - **Q1, one lane:** `--rebuild` was mandated for "the publishing run" only, but
    the freshness test never consults `PUBLISH` — the build-only run reuses a
    stale artifact too and says so ("Reusing existing AppImage"). So a local
    build made to check a fix could silently not contain it. Now mandated on
    every run.
  - **Q1, one lane:** loop 2's verification step offered
    `--appimage-extract-and-run` as an alternative to `--appimage-extract`. It
    unpacks to a temp directory, *runs* the game and cleans up, leaving nothing
    to inspect — so the one check that catches a stale AppImage would have
    passed while checking nothing. Parenthetical removed.
  - **Q2, orchestrator**, from a lane's open question: `changelog_log` has a
    `release` op that dates `[Unreleased]`, so the tool this standard recommends
    can itself produce the skip-branch state the lockstep section warns about.
    Both sections now say `release.sh` owns the promotion.
  - **One finding dismissed as unverified, and the fault was the packet's.** A
    lane read "Also DOOM-0357" as a false citation because the packet carried
    only that item's pre-annotation headline. The bullet body does cover the
    pre-release leg. Two other lanes hit the same trap and one of them checked
    the roadmap before filing. **Never summarise a cited item in a packet when
    the lane's job is to check the citation.**
