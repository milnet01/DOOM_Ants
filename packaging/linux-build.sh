#!/usr/bin/env bash
#
# linux-build.sh — build the Linux release artifact (a self-contained AppImage).
#
# Standalone Linux build — use it to build (or rebuild) just the Linux AppImage
# without a full release.sh run, e.g. to ship a Linux-only update. The AppImage
# assembly (dependency bundling, soundfont, runtime hooks) lives in
# build-appimage.sh; this is the plainly-named front door to it, mirroring
# windows-build.sh. Env overrides (e.g. SOUNDFONT) pass straight through. The
# output path matches what release.sh expects, so release.sh --publish reuses an
# artifact built here.
#
# Usage:  packaging/linux-build.sh [VERSION]
#   VERSION    version label for the output filename (default: dev)
#
set -euo pipefail
exec "$(dirname "$0")/build-appimage.sh" "$@"
