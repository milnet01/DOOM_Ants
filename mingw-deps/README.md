# Windows cross-build dependencies (DOOM-0006)

The Windows build (`make windows` in `linuxdoom-1.10/`) cross-compiles with
mingw-w64 and links against SDL2, SDL2_mixer and the Vulkan loader. openSUSE
ships the mingw-w64 toolchain but **not** mingw builds of those three libraries,
so they are staged here from upstream. Everything under `mingw-deps/` except
this README is git-ignored — regenerate it with the steps below.

## 1. Toolchain (one-off, via the distro)

```sh
sudo zypper install mingw64-cross-gcc mingw64-cross-gcc-c++
# optional, for smoke-testing the .exe without a Windows box:
sudo zypper install wine
```

## 2. Libraries → `mingw-deps/prefix/`

```sh
packaging/mingw-deps.sh          # from the repo root; --force to re-stage
```

It downloads the official upstream mingw dev packages for SDL2 and SDL2_mixer
plus the Vulkan headers, assembles the unified prefix the Makefile expects, and
generates the `vulkan-1` import library from Khronos' own `.def` (the loader
DLL itself ships with the user's GPU driver, so it is linked but not bundled).
Re-running it is a no-op once the prefix is complete.

**That script is the single source of truth for the versions** — they are three
variables at the top of it, deliberately not repeated here, so this README and
the CI job cannot drift out of step with what actually gets downloaded. Bump
them to the latest stable within the SDL**2** line (the engine is not SDL3) and
re-run `packaging/windows-smoke.sh` before committing the change.

Everything the script downloads is unpacked in a temporary directory and thrown
away; only `prefix/` is left behind.

## 3. Build + package

```sh
cd ../linuxdoom-1.10
make windows                       # -> mingw/doom_ants.exe
```

Ship `doom_ants.exe` alongside these runtime DLLs (the rest are system DLLs
present on every Windows install, and `vulkan-1.dll` comes with the GPU driver):

- `mingw-deps/prefix/bin/SDL2.dll`
- `mingw-deps/prefix/bin/SDL2_mixer.dll`
- `/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll`
