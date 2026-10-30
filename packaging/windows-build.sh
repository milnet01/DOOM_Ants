#!/usr/bin/env bash
#
# windows-build.sh — cross-build the Windows (x86_64) binary with the mingw-w64
# toolchain and package it as a ready-to-run zip: doom_ants.exe plus the SDL2 /
# SDL2_mixer / winpthread runtime DLLs, Ultra's HD art and a short README. vulkan-1.dll comes
# from the player's GPU driver, so it is deliberately not bundled.
#
# The one Windows packaging recipe: packaging/release.sh calls this script for
# its Windows artifact, and it also runs standalone to build (or rebuild) just
# the Windows zip, e.g. for a Windows-only update. The output path is the one
# release.sh expects, so release.sh --publish reuses an artifact built here.
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
# shellcheck source=packaging/windows-dlls.sh
source "$REPO/packaging/windows-dlls.sh"   # WIN_RUNTIME_DLLS
# shellcheck source=packaging/ultra-art.sh
source "$REPO/packaging/ultra-art.sh"

mkdir -p "$BUILD"
# The stamp release.sh writes beside the zip names the commit it was built from.
# This run replaces the zip, so the old stamp would describe another build; only
# release.sh, which knows the commit, writes a new one (DOOM-0392).
rm -f "$WINZIP.commit"

# 1. Cross-compile the Windows binary.
echo "==> Building Windows binary (make windows)..."
make -C "$REPO/linuxdoom-1.10" windows
EXE="$REPO/linuxdoom-1.10/mingw/doom_ants.exe"
[ -f "$EXE" ] || { echo "windows-build.sh: doom_ants.exe was not produced" >&2; exit 1; }

# 2. Stage the .exe + required runtime DLLs, then zip.
echo "==> Packaging Windows zip..."
for f in "${WIN_RUNTIME_DLLS[@]}"; do
  [ -f "$f" ] || { echo "windows-build.sh: missing runtime DLL '$f'" >&2; exit 1; }
done
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
NAME="doom_ants-$VERSION-windows-x86_64"
mkdir -p "$STAGE/$NAME"
cp "$EXE" "${WIN_RUNTIME_DLLS[@]}" "$STAGE/$NAME/"
stage_ultra_art "$STAGE/$NAME/assets/ultra"
cat > "$STAGE/$NAME/README.txt" <<EOF
DOOM_Ants $VERSION — Windows (x86_64)

Run doom_ants.exe. You must supply a DOOM .wad data file in the same folder:
Freedoom's freedoom1.wad or freedoom2.wad (free, from freedoom.github.io), the
shareware doom1.wad, or doom.wad / doom2.wad. No WAD is included. The bundled
DLLs are required; vulkan-1.dll is provided by your GPU driver. The assets folder
holds the Ultra tier's HD art; keep it beside doom_ants.exe. Its sources and CC0
licences are listed in assets/ultra/LICENSES.

We do not condone piracy. DOOM Ants includes no DOOM game data. Use Freedoom,
or the doom.wad or doom2.wad from a copy you own. DOOM is a trademark of
id Software LLC, a ZeniMax Media company. DOOM Ants is not affiliated with or
endorsed by id Software, ZeniMax or Bethesda.
EOF
rm -f "$WINZIP"
( cd "$STAGE" && zip -r -q "$WINZIP" "$NAME" )

echo "Built: $WINZIP"
