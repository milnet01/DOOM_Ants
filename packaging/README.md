# Packaging

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

CI builds Linux only, so the mingw target used to be exercised exactly once
per release — long enough for two Windows-only compile errors to accumulate
between 0.5.0 and 0.6.0.

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
