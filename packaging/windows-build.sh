#!/usr/bin/env bash
#
# windows-build.sh — cross-build the Windows (x86_64) binary with the mingw-w64
# toolchain and package it as a ready-to-run zip: doom_ants.exe plus the SDL2 /
# SDL2_mixer / winpthread runtime DLLs and a short README. vulkan-1.dll comes
# from the player's GPU driver, so it is deliberately not bundled.
#
# Standalone Windows build — use it to build (or rebuild) just the Windows
# artifact without a full release.sh run, e.g. to ship a Windows-only update.
# It mirrors the Windows-packaging steps inside packaging/release.sh; if you
# change the DLL set or staging layout, update both. The output path matches
# what release.sh expects, so release.sh --publish reuses an artifact built here.
#
# Usage:  packaging/windows-build.sh [VERSION]
#   VERSION    version label for the output filename (default: dev)
#
# Requirements: the mingw-w64 cross toolchain + staged runtime DLLs under
# mingw-deps/prefix (see mingw-deps/README.md), and `zip`.
#
set -euo pipefail

VERSION="${1:-dev}"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$REPO/packaging/build"
WINZIP="$BUILD/doom_ants-$VERSION-windows-x86_64.zip"
WIN_PREFIX="$REPO/mingw-deps/prefix"
WINPTHREAD="/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll"

mkdir -p "$BUILD"

# 1. Cross-compile the Windows binary.
echo "==> Building Windows binary (make windows)..."
make -C "$REPO/linuxdoom-1.10" windows
EXE="$REPO/linuxdoom-1.10/mingw/doom_ants.exe"
[ -f "$EXE" ] || { echo "windows-build.sh: doom_ants.exe was not produced" >&2; exit 1; }

# 2. Stage the .exe + required runtime DLLs, then zip.
echo "==> Packaging Windows zip..."
for f in "$WIN_PREFIX/bin/SDL2.dll" "$WIN_PREFIX/bin/SDL2_mixer.dll" "$WINPTHREAD"; do
  [ -f "$f" ] || { echo "windows-build.sh: missing runtime DLL '$f'" >&2; exit 1; }
done
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
NAME="doom_ants-$VERSION-windows-x86_64"
mkdir -p "$STAGE/$NAME"
cp "$EXE" "$WIN_PREFIX/bin/SDL2.dll" "$WIN_PREFIX/bin/SDL2_mixer.dll" "$WINPTHREAD" "$STAGE/$NAME/"
cat > "$STAGE/$NAME/README.txt" <<EOF
DOOM_Ants $VERSION — Windows (x86_64)

Run doom_ants.exe. You must supply a DOOM .wad data file (for example the
shareware doom1.wad) in the same folder — WADs are not included for licensing
reasons. The bundled DLLs are required; vulkan-1.dll is provided by your GPU
driver.
EOF
rm -f "$WINZIP"
( cd "$STAGE" && zip -r -q "$WINZIP" "$NAME" )

echo "Built: $WINZIP"
