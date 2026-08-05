#!/usr/bin/env bash
#
# mingw-deps.sh — stage the Windows cross-build's third-party dependencies into
# mingw-deps/prefix/.
#
# The distro ships the mingw-w64 toolchain but not mingw builds of SDL2,
# SDL2_mixer or the Vulkan loader, so those three come from upstream. This
# script is the SINGLE SOURCE OF TRUTH for which versions: mingw-deps/README.md
# and the Windows CI job (.github/workflows/build.yml) both call it rather than
# repeating the URLs, so the two cannot drift apart.
#
# Usage:
#   packaging/mingw-deps.sh              # stage; no-op if already staged
#   packaging/mingw-deps.sh --force      # discard and re-stage
#   packaging/mingw-deps.sh --headers-only   # skip the import libraries + DLLs
#
# --headers-only exists for the CI syntax gate, which compiles every
# translation unit but never links: it needs the headers and nothing else, so
# it skips the dlltool step and the runtime DLLs.
#
set -euo pipefail

# Latest stable within the SDL2 line at the last refresh (2026-08-05). The
# engine is SDL2, not SDL3, so "latest" here means latest 2.x. Bumping any of
# these means re-running packaging/windows-smoke.sh before committing; see
# docs/standards/dependencies.md.
SDL2_VER=2.32.10
SDL2_MIXER_VER=2.8.2
VULKAN_VER=1.4.350.0

REPO="$(cd "$(dirname "$0")/.." && pwd)"
PREFIX="$REPO/mingw-deps/prefix"

FORCE=0
HEADERS_ONLY=0
for arg in "$@"; do
  case "$arg" in
    --force)        FORCE=1 ;;
    --headers-only) HEADERS_ONLY=1 ;;
    *) echo "mingw-deps.sh: unknown flag '$arg'" >&2; exit 1 ;;
  esac
done

if [ "$FORCE" = 1 ]; then rm -rf "$PREFIX"; fi

# Already staged? Test for what THIS mode produces, not just the headers -- a
# previous --headers-only run leaves the headers in place with no import
# libraries, and a full run must not mistake that for a complete prefix.
STAGED=0
if [ -f "$PREFIX/include/SDL2/SDL.h" ] && [ -f "$PREFIX/include/vulkan/vulkan.h" ]; then
  if [ "$HEADERS_ONLY" = 1 ] || [ -f "$PREFIX/lib/libvulkan-1.a" ]; then STAGED=1; fi
fi
if [ "$STAGED" = 1 ]; then
  echo "mingw-deps: already staged at $PREFIX (--force to re-stage)."
  exit 0
fi

WORK="$(mktemp -d -t doom-ants-mingw-deps-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Downloading SDL2 $SDL2_VER, SDL2_mixer $SDL2_MIXER_VER, Vulkan-Headers $VULKAN_VER..."
curl -fsSL -o "$WORK/sdl2.tar.gz" \
  "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VER/SDL2-devel-$SDL2_VER-mingw.tar.gz"
curl -fsSL -o "$WORK/sdl2_mixer.tar.gz" \
  "https://github.com/libsdl-org/SDL_mixer/releases/download/release-$SDL2_MIXER_VER/SDL2_mixer-devel-$SDL2_MIXER_VER-mingw.tar.gz"
curl -fsSL -o "$WORK/vulkan-headers.tar.gz" \
  "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-$VULKAN_VER.tar.gz"

tar xzf "$WORK/sdl2.tar.gz"          -C "$WORK"
tar xzf "$WORK/sdl2_mixer.tar.gz"    -C "$WORK"
tar xzf "$WORK/vulkan-headers.tar.gz" -C "$WORK"

SDL2_SRC="$WORK/SDL2-$SDL2_VER/x86_64-w64-mingw32"
MIXER_SRC="$WORK/SDL2_mixer-$SDL2_MIXER_VER/x86_64-w64-mingw32"
VULKAN_SRC="$WORK/Vulkan-Headers-vulkan-sdk-$VULKAN_VER/include"

echo "==> Assembling $PREFIX..."
mkdir -p "$PREFIX/include" "$PREFIX/lib" "$PREFIX/bin"
cp -r "$SDL2_SRC/include/SDL2"   "$PREFIX/include/"
cp -r "$MIXER_SRC/include/SDL2/." "$PREFIX/include/SDL2/"
cp -r "$VULKAN_SRC/vulkan"       "$PREFIX/include/"
cp -r "$VULKAN_SRC/vk_video"     "$PREFIX/include/"

if [ "$HEADERS_ONLY" = 1 ]; then
  echo "    headers only — import libraries and DLLs skipped (no link stage)."
  exit 0
fi

cp "$SDL2_SRC"/lib/*.a    "$PREFIX/lib/"
cp "$MIXER_SRC"/lib/*.a   "$PREFIX/lib/"
cp "$SDL2_SRC"/bin/SDL2.dll "$PREFIX/bin/"
cp "$MIXER_SRC"/bin/*.dll "$PREFIX/bin/"

# The Vulkan loader (vulkan-1.dll) ships with the user's GPU driver, so only an
# import library is needed — generate it from Khronos' official .def rather than
# bundling a DLL we would then have to keep current.
command -v x86_64-w64-mingw32-dlltool >/dev/null || {
  echo "mingw-deps.sh: x86_64-w64-mingw32-dlltool not found (install the mingw-w64 binutils)" >&2
  exit 1; }
curl -fsSL -o "$WORK/vulkan-1.def" \
  "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Loader/vulkan-sdk-$VULKAN_VER/loader/vulkan-1.def"
x86_64-w64-mingw32-dlltool -d "$WORK/vulkan-1.def" -l "$PREFIX/lib/libvulkan-1.a" -D vulkan-1.dll

echo "    staged: headers, import libraries and runtime DLLs."
