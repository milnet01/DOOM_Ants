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
