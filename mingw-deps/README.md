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

Versions below were current at last update (SDL2 2.32.10, SDL2_mixer 2.8.2,
Vulkan-Headers 1.4.350.0); bump to the latest stable when refreshing.

```sh
cd mingw-deps

# Download official upstream mingw dev packages + Vulkan headers/loader def
curl -L -o SDL2-mingw.tar.gz        https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
curl -L -o SDL2_mixer-mingw.tar.gz  https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.2/SDL2_mixer-devel-2.8.2-mingw.tar.gz
curl -L -o vulkan-headers.tar.gz    https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.350.0.tar.gz
curl -L -o vulkan-1.def             https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/vulkan-sdk-1.4.350.0/loader/vulkan-1.def
tar xzf SDL2-mingw.tar.gz && tar xzf SDL2_mixer-mingw.tar.gz && tar xzf vulkan-headers.tar.gz

# Assemble the unified prefix the Makefile expects
PREFIX=$PWD/prefix
mkdir -p $PREFIX/include $PREFIX/lib $PREFIX/bin
cp -r SDL2-2.32.10/x86_64-w64-mingw32/include/SDL2          $PREFIX/include/
cp    SDL2-2.32.10/x86_64-w64-mingw32/lib/*.a               $PREFIX/lib/
cp    SDL2-2.32.10/x86_64-w64-mingw32/bin/SDL2.dll          $PREFIX/bin/
cp -r SDL2_mixer-2.8.2/x86_64-w64-mingw32/include/SDL2/*    $PREFIX/include/SDL2/
cp    SDL2_mixer-2.8.2/x86_64-w64-mingw32/lib/*.a           $PREFIX/lib/
cp    SDL2_mixer-2.8.2/x86_64-w64-mingw32/bin/*.dll         $PREFIX/bin/
cp -r Vulkan-Headers-vulkan-sdk-1.4.350.0/include/vulkan    $PREFIX/include/
cp -r Vulkan-Headers-vulkan-sdk-1.4.350.0/include/vk_video  $PREFIX/include/

# The Vulkan loader (vulkan-1.dll) ships with the user's GPU driver, so we only
# need an import library to link against — generate it from the official .def.
x86_64-w64-mingw32-dlltool -d vulkan-1.def -l $PREFIX/lib/libvulkan-1.a -D vulkan-1.dll
```

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
