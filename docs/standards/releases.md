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

## CHANGELOG

`CHANGELOG.md` follows [Keep a Changelog](https://keepachangelog.com). Shipped
roadmap items graduate into it under `## [Unreleased]`, grouped by category
(Added / Changed / Fixed / Security / …). Use the `changelog_log` Ants tool
(it is Keep-a-Changelog-aware) rather than hand-editing. At release,
`[Unreleased]` is promoted to `[<ver>] - <today>`.

## Cutting a release

One command does it all — don't tag or upload by hand:

```sh
packaging/release.sh <ver>            # build both artifacts locally, no publish
packaging/release.sh <ver> --publish  # + promote changelog, tag, push, GitHub release
```

In order, `release.sh`:

1. runs `make test` as a gate — a failing test stops the release before anything
   is built or tagged;
2. builds the **Linux AppImage** (`packaging/build-appimage.sh`);
3. builds and zips the **Windows** cross-build;
4. with `--publish`: promotes `CHANGELOG [Unreleased]` → `[<ver>] - <today>` and
   commits it, tags `v<ver>`, pushes branch + tag, and creates the GitHub
   release with both artifacts attached and the CHANGELOG section as notes.

Requirements: the mingw-w64 cross toolchain + staged libs (`mingw-deps/`), the
AppImage toolchain (auto-fetched), `zip`, and an authenticated `gh`.

> **`gh` repo gotcha:** this repo is a fork of `id-Software/DOOM`, so `gh`
> defaults to the parent. Release commands must target the fork explicitly:
> `-R milnet01/DOOM_Ants`.

## What ships, and when

- **Artifacts:** a Linux AppImage and a Windows zip. The end-user runtime library
  list is canonical in `docs/RELEASE_README.txt` — keep it current (see the
  dependencies standard).
- **No game data.** WADs are never bundled (licensing) — players bring their own.
- **When:** cut a release when a coherent batch of shipped roadmap items is worth
  putting in players' hands. This is a **public** repo, so pushing and publishing
  are free — release after each clean, complete batch rather than hoarding.
