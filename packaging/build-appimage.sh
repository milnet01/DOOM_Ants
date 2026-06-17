#!/usr/bin/env bash
#
# build-appimage.sh — package the Linux build as a single self-contained
# AppImage (DOOM-0007 follow-up). The result runs on a fresh distro with no
# dependency install: SDL2, SDL2_mixer, FluidSynth and a compact General-MIDI
# soundfont are bundled, so the game (and its music) work out of the box. Only
# a DOOM .wad is needed, which is not redistributable.
#
# Note: the AppImage inherits the build host's glibc floor, so build on the
# oldest distro you want to support for the widest reach.
#
# Usage:  packaging/build-appimage.sh [VERSION]
#   VERSION    version label for the output filename (default: dev)
#   SOUNDFONT  env override for the bundled .sf2 (default: FluidR3_GS, ~3 MB)
#
set -euo pipefail

VERSION="${1:-dev}"
SOUNDFONT="${SOUNDFONT:-/usr/share/sounds/sf2/FluidR3_GS.sf2}"

REPO="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$REPO/packaging"
BUILD="$PKG/build"
TOOLS="$PKG/tools"
APPDIR="$BUILD/AppDir"
OUT="$BUILD/doom_ants-$VERSION-x86_64.AppImage"

# 1. Build the native Linux binary.
make -C "$REPO/linuxdoom-1.10"

# 2. Fetch the AppImage toolchain (cached under packaging/tools, git-ignored).
mkdir -p "$TOOLS"
fetch() { [ -f "$TOOLS/$1" ] || curl -fsSL -o "$TOOLS/$1" "$2"; chmod +x "$TOOLS/$1"; }
fetch linuxdeploy.AppImage  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
fetch appimagetool.AppImage https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage
fetch runtime-x86_64        https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64

# 3. Assemble the AppDir.
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/soundfonts" \
         "$APPDIR/usr/share/applications" \
         "$APPDIR/usr/share/icons/hicolor/256x256/apps" "$APPDIR/apprun-hooks"
cp "$REPO/linuxdoom-1.10/linux/linuxxdoom" "$APPDIR/usr/bin/doom_ants"
cp "$SOUNDFONT"                            "$APPDIR/usr/share/soundfonts/"
cp "$PKG/doom_ants.desktop"                "$APPDIR/usr/share/applications/"
cp "$PKG/doom_ants.png"                    "$APPDIR/usr/share/icons/hicolor/256x256/apps/"

# Runtime hook: use the bundled soundfont for music, and look for the WAD next
# to the .AppImage file (so dropping a WAD beside it and double-clicking works,
# regardless of the launcher's working directory). Both respect a user override.
SF_NAME="$(basename "$SOUNDFONT")"
cat > "$APPDIR/apprun-hooks/doom_ants-env.sh" <<HOOK
if [ -z "\$DOOM_SOUNDFONT" ]; then
    export DOOM_SOUNDFONT="\$APPDIR/usr/share/soundfonts/$SF_NAME"
fi
if [ -z "\$DOOMWADDIR" ] && [ -n "\$APPIMAGE" ]; then
    export DOOMWADDIR="\$(dirname "\$APPIMAGE")"
fi
HOOK

# 4. Bundle dependent libraries, then package (explicit runtime = no network
#    fetch, which otherwise hangs appimagetool).
export NO_STRIP=1 ARCH=x86_64
"$TOOLS/linuxdeploy.AppImage" --appimage-extract-and-run \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/doom_ants" \
    --desktop-file "$APPDIR/usr/share/applications/doom_ants.desktop" \
    --icon-file "$PKG/doom_ants.png"
"$TOOLS/appimagetool.AppImage" --appimage-extract-and-run \
    --no-appstream --runtime-file "$TOOLS/runtime-x86_64" \
    "$APPDIR" "$OUT"

echo "Built: $OUT"
