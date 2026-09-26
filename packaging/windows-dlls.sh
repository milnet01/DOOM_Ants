# shellcheck shell=bash
# DOOM-0452: the runtime DLLs doom_ants.exe needs beside it, in ONE place.
# Sourced by windows-build.sh (which ships them in the zip) and windows-smoke.sh
# (which stages them to run the game under Wine); both used to carry a copy.
# vulkan-1.dll is deliberately absent: the GPU driver provides it.
#
# Requires WIN_PREFIX (the staged mingw-deps prefix) to be set by the caller.
# shellcheck disable=SC2034  # consumed by the scripts that source this file
WIN_RUNTIME_DLLS=(
  "$WIN_PREFIX/bin/SDL2.dll"
  "$WIN_PREFIX/bin/SDL2_mixer.dll"
  "/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll"
)
