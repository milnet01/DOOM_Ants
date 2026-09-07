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
#
# DOOM-0259: pinned to release tags with recorded hashes. These are downloaded
# and then EXECUTED, so the build has to know what it is running; the rolling
# `continuous` tag it used before could change under us between two builds of
# the same commit, and nothing would notice.
#
# To move a pin: change the tag, run this script, and copy the sha256 it prints
# on the mismatch. Verify the new hash against the upstream release page before
# committing it -- copying whatever came down defeats the point.
LINUXDEPLOY_TAG=1-alpha-20251107-1
LINUXDEPLOY_SHA=c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d

APPIMAGETOOL_TAG=1.9.1
APPIMAGETOOL_SHA=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0

RUNTIME_TAG=20251108
RUNTIME_SHA=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

mkdir -p "$TOOLS"

# fetch <name> <url> <sha256>
#
# The hash is checked on EVERY run, not only after a download: the cache is a
# plain directory, so a file already sitting there is exactly as untrusted as
# one arriving now. A mismatch removes the file, so the next run re-fetches
# rather than failing forever on a bad cache entry.
fetch() {
    dest="$TOOLS/$1"

    if [ ! -f "$dest" ]; then
        curl -fsSL -o "$dest" "$2" || { rm -f "$dest"; exit 1; }
    fi

    got="$(sha256sum "$dest" | cut -d' ' -f1)"
    if [ "$got" != "$3" ]; then
        echo "build-appimage.sh: $1 is not the pinned build -- refusing to run it." >&2
        echo "  expected $3" >&2
        echo "  got      $got" >&2
        echo "  from     $2" >&2
        rm -f "$dest"
        exit 1
    fi

    chmod +x "$dest"
}

fetch linuxdeploy.AppImage \
      "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-x86_64.AppImage" \
      "$LINUXDEPLOY_SHA"
fetch appimagetool.AppImage \
      "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-x86_64.AppImage" \
      "$APPIMAGETOOL_SHA"
fetch runtime-x86_64 \
      "https://github.com/AppImage/type2-runtime/releases/download/$RUNTIME_TAG/runtime-x86_64" \
      "$RUNTIME_SHA"

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
