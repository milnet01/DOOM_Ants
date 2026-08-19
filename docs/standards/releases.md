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

**The tool moves all three only when it promotes `[Unreleased]`.** `release.sh`
rewrites the CHANGELOG, rewrites README's "Latest release" line, adds the
`[Unreleased]:` / `[<ver>]:` compare links and commits the lot inside a single
branch, taken only when no `## [<ver>]` heading exists yet. Where one already
does — a section dated by hand — it skips all of that and still tags, so the tag
moves and README does not. Until DOOM-0357 lands, **leave the section as
`[Unreleased]` and let the tool promote it**; that is the only route that moves
every leg. Bumping README by hand instead still leaves the compare links unwritten.

**Do not cut a `-`-suffixed pre-release until DOOM-0357 lands.** The README
rewrite matches a plain `X.Y.Z` only, so cutting `0.6.0-pre.1` writes a line the
guard no longer recognises and the *next* release aborts with "could not find
README 'Latest release' line to bump". If one has already been cut, restoring
that line by hand to the last plain `X.Y.Z` is a **named exception** to the
never-by-hand rule above — it is the only way to unblock the next release.
DOOM-0357 covers this leg as well as the skipped-branch one.

**Do not date the CHANGELOG section ahead of the release, by hand or by tool.**
`changelog_log` has a `release` operation that closes `[Unreleased]` into
`## [<ver>] - <date>`; running it before `release.sh` produces exactly the state
above, from the tool this standard otherwise recommends. Let `release.sh` do the
promotion.

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
packaging/release.sh <ver> --rebuild                                # build both artifacts locally, no publish
packaging/release.sh <ver> --publish --rebuild --theme="<one line>" # + promote changelog, tag, push, GitHub release
```

`--rebuild` is mandatory on **every** run until DOOM-0356 lands — the
build-only run reuses a stale artifact exactly as the publishing one does (it
prints "Reusing existing AppImage"), so a local build made to check a fix can
silently not contain it. See the stale-artifact warning below.

`--theme` sets the release commit subject (`<ver>: <theme>`). It does **not**
set the GitHub release title: that is always `DOOM_Ants <ver>`, with the theme
appended after an em dash. Without it the commit reads `<ver>: release` and the
title is unsuffixed.

In order, `release.sh`:

1. runs `make test` as a gate — a failing test stops the release before anything
   is built or tagged;
2. builds the **Linux AppImage** (`packaging/build-appimage.sh`);
3. builds and zips the **Windows** cross-build;
4. with `--publish`: promotes `CHANGELOG [Unreleased]` → `[<ver>] - <today>`,
   rewrites README's "Latest release" line, commits both, tags `v<ver>`, pushes
   branch + tag, and creates the GitHub release with both artifacts attached and
   the CHANGELOG section as notes. The first three of those are skipped when a
   `## [<ver>]` heading already exists — see the lockstep section above.

Requirements: the mingw-w64 cross toolchain + staged libs (`mingw-deps/`), the
AppImage toolchain (auto-fetched), `zip`, and an authenticated `gh`.

> **Until DOOM-0356 lands, pass `--rebuild` on every run — and after
> publishing, download the released artifact and confirm it contains the change
> the release claims.** `release.sh` decides an artifact is already built by
> testing for a file of that name (lines 71 and 80), and the name carries only
> the version. So a build-only run followed by a `--publish` run **without
> `--rebuild`** uploads the *first* build, and any commit made in between is
> missing from what ships. Nothing else catches it: `make test` and
> `packaging/ci-local.sh` both check the tree, not the artifact, and the tag,
> the CHANGELOG and the release all come out correct. Observed on 0.7.1, where
> the published binaries predated the fix the release was cut for.
>
> Check **both** assets for something only the new code has — an imported
> symbol, a new string — rather than trusting the file's timestamp. They open
> differently: `unzip` the Windows zip, but an AppImage is not a zip, so use
> `./<name>.AppImage --appimage-extract` and inspect what lands in
> `squashfs-root/`. Not `--appimage-extract-and-run`, which unpacks to a temp
> directory, *runs* the game and cleans up — it leaves nothing to inspect and
> takes over the display. 0.7.1 shipped **both** artifacts stale, so checking
> only the Windows one would have missed half of it.
>
> **This applies to every publishing route, not just a hand-typed one.**
> `.claude/bump.json`'s `release_command` records the canonical publish
> invocation and carries no `--rebuild`, so publishing through the recipe ships
> the stale artifact while looking compliant. Add the flag there too until
> DOOM-0356 lands.
>
> **When the check fails, replace the assets — do not re-tag.** `release.sh`
> refuses a second `--publish` of an existing tag, and the tag is not the thing
> that is wrong. Rebuild with `--rebuild`, then
> `gh release upload <tag> --repo <owner>/<repo> --clobber <assets>`, leaving the
> tag where it is. This is a sanctioned exception to "don't tag or upload by
> hand" above — the other is the pre-release README repair — and it is what
> 0.7.1 used.

> **`gh` repo gotcha:** this repo is a fork of `id-Software/DOOM`, so `gh`
> defaults to the parent. **Hand-run `gh` commands** — the asset replacement
> above, `gh release view` — must target the fork explicitly with
> `-R milnet01/DOOM_Ants` (or `--repo`). `release.sh` needs no such flag and
> accepts none: it derives the slug from `origin` itself (line 127) and passes
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
