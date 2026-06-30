#!/usr/bin/env bash
#
# release.sh — build (and optionally publish) a DOOM_Ants release for both
# Linux and Windows in one shot. This is the "just run it" release tool.
#
# Usage:
#   packaging/release.sh 0.3.0                     # build both artifacts locally
#   packaging/release.sh 0.3.0 --publish           # also tag, push, GitHub release
#   packaging/release.sh 0.3.0 --publish --theme="ray-traced sky + flashlight"
#   packaging/release.sh 0.3.0 --rebuild           # force a clean rebuild
#
# What it does, in order (any failure stops the release — a broken build never
# gets tagged or published):
#   1. Runs the unit tests (make test) as a gate.
#   2. Builds the Linux AppImage  -> packaging/build/doom_ants-<ver>-x86_64.AppImage
#   3. Builds + zips the Windows build -> packaging/build/doom_ants-<ver>-windows-x86_64.zip
#   With --publish, additionally:
#   4. Promotes CHANGELOG [Unreleased] -> [<ver>] - <today> and commits it.
#   5. Tags v<ver>, pushes the branch + tag.
#   6. Creates the GitHub release (notes = the CHANGELOG section, both artifacts
#      attached). A version with a "-" suffix (e.g. 0.3.0-pre.1) is published as
#      a pre-release; otherwise it is marked Latest.
#
# Requirements: the mingw-w64 cross toolchain + staged libs (see
# mingw-deps/README.md), the AppImage toolchain (auto-fetched by
# build-appimage.sh), `zip`, and `gh` (authenticated) for --publish.
#
set -euo pipefail

# ---- parse arguments ----
PUBLISH=0
REBUILD=0
THEME=""
VERSION=""
for arg in "$@"; do
  case "$arg" in
    --publish)   PUBLISH=1 ;;
    --rebuild)   REBUILD=1 ;;
    --theme=*)   THEME="${arg#--theme=}" ;;
    -*)          echo "release.sh: unknown flag '$arg'" >&2; exit 2 ;;
    *)           if [ -z "$VERSION" ]; then VERSION="$arg"
                 else echo "release.sh: unexpected argument '$arg'" >&2; exit 2; fi ;;
  esac
done

if [ -z "$VERSION" ]; then
  echo "usage: packaging/release.sh <X.Y.Z> [--publish] [--rebuild] [--theme=\"...\"]" >&2
  exit 2
fi
if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9.]+)?$ ]]; then
  echo "release.sh: '$VERSION' is not a valid X.Y.Z[-pre.N] version" >&2
  exit 2
fi

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

TAG="v$VERSION"
DIST="$REPO/packaging/build"
APPIMAGE="$DIST/doom_ants-$VERSION-x86_64.AppImage"
WINZIP="$DIST/doom_ants-$VERSION-windows-x86_64.zip"
WIN_PREFIX="$REPO/mingw-deps/prefix"
WINPTHREAD="/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll"
case "$VERSION" in *-*) PRERELEASE=1 ;; *) PRERELEASE=0 ;; esac

# ---- 1. test gate ----
echo "==> Running unit tests (make test)..."
make -C "$REPO/linuxdoom-1.10" test

# ---- 2. Linux AppImage ----
if [ "$REBUILD" = 1 ] || [ ! -f "$APPIMAGE" ]; then
  echo "==> Building Linux AppImage..."
  "$REPO/packaging/build-appimage.sh" "$VERSION"
else
  echo "==> Reusing existing AppImage: $APPIMAGE"
fi
[ -f "$APPIMAGE" ] || { echo "release.sh: AppImage was not produced" >&2; exit 1; }

# ---- 3. Windows build + zip ----
if [ "$REBUILD" = 1 ] || [ ! -f "$WINZIP" ]; then
  echo "==> Building Windows binary (make windows)..."
  make -C "$REPO/linuxdoom-1.10" windows
  EXE="$REPO/linuxdoom-1.10/mingw/doom_ants.exe"
  [ -f "$EXE" ] || { echo "release.sh: doom_ants.exe was not produced" >&2; exit 1; }

  echo "==> Packaging Windows zip..."
  for f in "$WIN_PREFIX/bin/SDL2.dll" "$WIN_PREFIX/bin/SDL2_mixer.dll" "$WINPTHREAD"; do
    [ -f "$f" ] || { echo "release.sh: missing runtime DLL '$f'" >&2; exit 1; }
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
  rm -rf "$STAGE"
  trap - EXIT
else
  echo "==> Reusing existing Windows zip: $WINZIP"
fi
[ -f "$WINZIP" ] || { echo "release.sh: Windows zip was not produced" >&2; exit 1; }

echo
echo "Artifacts:"
echo "  Linux:   $APPIMAGE"
echo "  Windows: $WINZIP"

if [ "$PUBLISH" = 0 ]; then
  echo
  echo "Build-only run complete. Re-run with --publish to tag and release."
  exit 0
fi

# ---- 4+. publish ----
command -v gh >/dev/null || { echo "release.sh: gh CLI not found; cannot --publish" >&2; exit 1; }

# Repo slug from origin (gh otherwise defaults to the fork parent id-Software/DOOM).
SLUG="$(git remote get-url origin | sed -E 's#^(git@github\.com:|https://github\.com/)##; s#\.git$##')"
BASE_URL="https://github.com/$SLUG"

if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
  echo "release.sh: tag $TAG already exists; refusing to re-release" >&2; exit 1
fi
if gh release view "$TAG" -R "$SLUG" >/dev/null 2>&1; then
  echo "release.sh: a GitHub release $TAG already exists; refusing to overwrite" >&2; exit 1
fi
# Don't fold stray edits into the release commit.
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  echo "release.sh: working tree has uncommitted tracked changes; commit or stash first" >&2
  git status --short
  exit 1
fi

# Promote CHANGELOG [Unreleased] -> [<ver>] - <today>, including the link refs.
if grep -q "^## \[$VERSION\]" CHANGELOG.md; then
  echo "==> CHANGELOG already has a [$VERSION] section; leaving it as-is"
else
  echo "==> Promoting CHANGELOG [Unreleased] -> [$VERSION]..."
  TODAY="$(date +%F)"
  PREV="$(grep -m1 '^\[Unreleased\]:' CHANGELOG.md | sed -E 's#.*/compare/(.+)\.\.\.HEAD.*#\1#')"
  if [ -z "$PREV" ]; then echo "release.sh: could not find [Unreleased] compare link" >&2; exit 1; fi
  UL_NEW="[Unreleased]: $BASE_URL/compare/$TAG...HEAD"
  VER_LINK="[$VERSION]: $BASE_URL/compare/$PREV...$TAG"
  awk -v V="$VERSION" -v D="$TODAY" -v UL="$UL_NEW" -v NL="$VER_LINK" '
    /^## \[Unreleased\]$/ && !h { print; print ""; print "## [" V "] - " D; h=1; next }
    /^\[Unreleased\]:/         { print UL; print NL; next }
    { print }
  ' CHANGELOG.md > CHANGELOG.md.tmp && mv CHANGELOG.md.tmp CHANGELOG.md
  git add CHANGELOG.md
  git commit -m "$VERSION: ${THEME:-release}"
fi

echo "==> Tagging $TAG and pushing..."
git tag -a "$TAG" -m "$VERSION"
git push origin HEAD
git push origin "$TAG"

# Release notes = the CHANGELOG section for this version.
NOTES="$(awk -v v="$VERSION" '
  $0 ~ "^## \\[" v "\\]" { grab=1; next }
  grab && /^## \[/       { exit }
  grab                   { print }
' CHANGELOG.md)"

echo "==> Creating GitHub release $TAG..."
TITLE="DOOM_Ants $VERSION"
[ -n "$THEME" ] && TITLE="$TITLE — $THEME"
LATEST_FLAG="--latest"
[ "$PRERELEASE" = 1 ] && LATEST_FLAG="--prerelease"
gh release create "$TAG" -R "$SLUG" \
  --title "$TITLE" \
  $LATEST_FLAG \
  --notes "$NOTES" \
  "$APPIMAGE" "$WINZIP"

echo
echo "Released $TAG:"
gh release view "$TAG" -R "$SLUG" --json url -q .url
