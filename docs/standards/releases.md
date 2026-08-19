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
rewrites the CHANGELOG, rewrites README's "Latest release" line and commits both
inside a single branch, taken only when no `## [<ver>]` heading exists yet. Where
one already does — a section dated by hand, or a second publishing run of the
same version — it skips all of that and still tags. So the tag moves and README
does not. Until DOOM-0357 lands, either leave the section as `[Unreleased]` and
let the tool promote it, or bump and commit README yourself before tagging.

## CHANGELOG

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com). Shipped
roadmap items graduate into it under `## [Unreleased]`, grouped by category
(Added / Changed / Fixed / Security / …). Use the `changelog_log` Ants tool
(it is Keep-a-Changelog-aware) rather than hand-editing. At release,
`[Unreleased]` is promoted to `[<ver>] - <today>`.

## Cutting a release

One command does it all — don't tag or upload by hand:

```sh
packaging/release.sh <ver>                      # build both artifacts locally, no publish
packaging/release.sh <ver> --publish --rebuild  # + promote changelog, tag, push, GitHub release
```

`--rebuild` is mandatory on the publishing run until DOOM-0356 lands — see the
stale-artifact warning below.

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

> **Until DOOM-0356 lands, pass `--rebuild` on the publishing run — and after
> publishing, download the released artifact and confirm it contains the change
> the release claims.** `release.sh` decides an artifact is already built by
> testing for a file of that name (lines 71 and 80), and the name carries only
> the version. So running the two commands above in order — a build-only run,
> then `--publish` — uploads the **first** build, and any commit made in
> between is missing from what ships. Nothing else catches it: `make test` and
> `packaging/ci-local.sh` both check the tree, not the artifact, and the tag,
> the CHANGELOG and the release all come out correct. Observed on 0.7.1, where
> the published binaries predated the fix the release was cut for.
>
> Unzip the asset and check for something only the new code has — an imported
> symbol, a new string — rather than trusting the file's timestamp.
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
> tag where it is. This is the one sanctioned exception to "don't tag or upload
> by hand" above; it is what 0.7.1 used.

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
