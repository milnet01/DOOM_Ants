# Packaging

## The local CI gate

`ci-local.sh` runs the same gate as `.github/workflows/build.yml` — both jobs
(Linux build + unit tests + headless boot smoke, and the Windows cross-compile
syntax sweep) — from a clean `git archive` export of HEAD, so a red CI is caught
before the push rather than after.

```sh
packaging/ci-local.sh              # container if podman/docker is present, else native
packaging/ci-local.sh --native     # force native (fast, approximates CI's toolchain)
packaging/ci-local.sh --container  # force container (exactly what GitHub runs)
packaging/ci-local.sh --force      # run even on a docs-only change
```

It mirrors the workflow's `paths-ignore` too: a docs-only change exits 0 at once,
because GitHub skips the workflow for one as well.

**Run it before every push.** Install it as a pre-push hook, once per clone (git
hooks are not themselves version-controlled):

```sh
git config core.hooksPath packaging/hooks
```

Then `git push` runs the gate first and aborts on failure; `git push --no-verify`
bypasses it for a one-off.

## Linux AppImage (DOOM-0007)

A single self-contained file that runs on a fresh Linux install with no
dependency setup — SDL2, SDL2_mixer, FluidSynth and a compact General-MIDI
soundfont are bundled, so the game and its music work out of the box. Only a
DOOM `.wad` is needed (not redistributable).

```sh
packaging/build-appimage.sh 0.5.0     # -> packaging/build/doom_ants-0.5.0-x86_64.AppImage
```

- `doom_ants.desktop`, `doom_ants.png` — committed launcher entry + icon.
- `build-appimage.sh` — builds the binary, bundles libraries (linuxdeploy) and
  packages the AppImage (appimagetool). The toolchain is cached under
  `packaging/tools/` and the staging tree under `packaging/build/` (both
  git-ignored).
- `SOUNDFONT=/path/to.sf2 packaging/build-appimage.sh ...` swaps the bundled
  soundfont (e.g. the full 142 MB FluidR3_GM for higher-quality music).

**Portability note:** an AppImage bundles libraries but not the system glibc,
so it inherits the *build host's* glibc floor. Build on the oldest distro you
want to support for the widest reach.

## Windows (DOOM-0006)

Cross-compiled with mingw-w64 — see [`../mingw-deps/README.md`](../mingw-deps/README.md)
and `make windows` in `../linuxdoom-1.10/`.

### Testing the Windows build without cutting a release (DOOM-0324)

The mingw target used to be exercised exactly once per release — long enough for
two Windows-only compile errors to accumulate between 0.5.0 and 0.6.0. CI now
runs the `--syntax-only` half on every push (the `windows-syntax` job), and
`ci-local.sh` mirrors it; the Wine boot half below stays local, needing a display.

```sh
packaging/windows-smoke.sh --syntax-only   # ~15 s, no Wine: does the tree compile for Windows?
packaging/windows-smoke.sh                 # + link, then actually BOOT the .exe under Wine
```

The full run stages the `.exe` and its DLLs in a temp sandbox, starts a
private `Xvfb` display, boots the game under Wine in a throwaway `WINEPREFIX`,
and asserts the engine reached its `-bootsmoke` line. Exit codes are
deliberately distinct — `2` never booted, `3` booted but never exited — so a
shutdown hang cannot be normalised into "the smoke passes".

Needs `wine` and `xorg-x11-server-Xvfb`; without them the script still runs the
compile sweep and says plainly that the engine was not run. It never touches
your desktop: the game is confined to the virtual display, and teardown kills
only its own `wineserver`, never `wine` by name.
