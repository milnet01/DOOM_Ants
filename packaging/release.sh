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
#   7. Re-downloads both published assets and checks they are byte-identical to
#      what was built.
#
# An artifact is only reused when it was built from the commit being released
# (each build stamps <artifact>.commit) -- so --rebuild is for forcing a clean
# rebuild, never for correctness.
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

# ---- artifact freshness (DOOM-0356) ----
# An artifact counts as "already built" only if it was built from the commit we
# are about to release. The filename carries the version alone, which says
# nothing about the source, so testing for the file shipped a stale binary
# whenever one of that name happened to exist. Each build therefore writes
# <artifact>.commit, and the test below compares that against HEAD.
HEAD_SHA="$(git rev-parse HEAD)"
TREE_DIRTY=0
[ -n "$(git status --porcelain --untracked-files=no)" ] && TREE_DIRTY=1

# A dirty tree never matches: what it built is not identified by any commit.
artifact_is_fresh() {
  [ "$REBUILD" = 0 ]     || return 1
  [ "$TREE_DIRTY" = 0 ]  || return 1
  [ -f "$1" ] && [ -f "$1.commit" ] && [ "$(cat "$1.commit")" = "$HEAD_SHA" ]
}
stamp_artifact() {
  if [ "$TREE_DIRTY" = 1 ]; then rm -f "$1.commit"
  else printf '%s\n' "$HEAD_SHA" > "$1.commit"; fi
}

# ---- 1. test gate ----
echo "==> Running unit tests (make test)..."
make -C "$REPO/linuxdoom-1.10" test

# ---- 2. Linux AppImage ----
if artifact_is_fresh "$APPIMAGE"; then
  echo "==> Reusing existing AppImage, built from $HEAD_SHA: $APPIMAGE"
else
  echo "==> Building Linux AppImage..."
  "$REPO/packaging/build-appimage.sh" "$VERSION"
  [ -f "$APPIMAGE" ] || { echo "release.sh: AppImage was not produced" >&2; exit 1; }
  stamp_artifact "$APPIMAGE"
fi

# ---- 3. Windows build + zip ----
if artifact_is_fresh "$WINZIP"; then
  echo "==> Reusing existing Windows zip, built from $HEAD_SHA: $WINZIP"
else
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
  [ -f "$WINZIP" ] || { echo "release.sh: Windows zip was not produced" >&2; exit 1; }
  stamp_artifact "$WINZIP"
fi

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

  # docs/standards/releases.md: the version lives in THREE places in lockstep --
  # the tag, the CHANGELOG heading, and README's "Latest release" line -- and they
  # move together, driven by this tool. The README leg was missing, so every
  # release since shipped with a stale "Latest release" line.
  if ! grep -q 'Latest release: \*\*[0-9]\+\.[0-9]\+\.[0-9]\+\*\*\.' README.md; then
    echo "release.sh: could not find README 'Latest release' line to bump" >&2; exit 1
  fi
  sed -i -E "s/Latest release: \*\*[0-9]+\.[0-9]+\.[0-9]+\*\*\./Latest release: **$VERSION**./" README.md

  git add CHANGELOG.md README.md
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

# Post-publish assertion (DOOM-0356). The stamp above proves the local artifacts
# were built from HEAD; this proves the published assets are those artifacts. A
# release that passes both carries the commit it claims.
echo "==> Verifying the published assets are the ones just built..."
VERIFY_DIR="$(mktemp -d)"
trap 'rm -rf "$VERIFY_DIR"' EXIT
gh release download "$TAG" -R "$SLUG" -D "$VERIFY_DIR" --clobber
for f in "$APPIMAGE" "$WINZIP"; do
  REMOTE="$VERIFY_DIR/$(basename "$f")"
  [ -f "$REMOTE" ] || { echo "release.sh: published release is missing $(basename "$f")" >&2; exit 1; }
  if [ "$(sha256sum <"$f" | cut -d' ' -f1)" != "$(sha256sum <"$REMOTE" | cut -d' ' -f1)" ]; then
    echo "release.sh: published $(basename "$f") does not match the local build" >&2; exit 1
  fi
done
rm -rf "$VERIFY_DIR"
trap - EXIT
echo "    both assets match the local build of $HEAD_SHA"

echo
echo "Released $TAG:"
gh release view "$TAG" -R "$SLUG" --json url -q .url
