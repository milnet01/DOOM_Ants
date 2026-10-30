#!/usr/bin/env bash
#
# ultra-art.sh — which of Ultra's HD art goes in a download, and the check that it
# did (DOOM-0498). Sourced by build-appimage.sh, windows-build.sh and release.sh;
# needs REPO set.
#
# Only committed files can ship, because they come from `git archive`. That keeps out
# assets/ultra/derived/, which is made from the player's own WAD and may not be
# redistributed. heroes/bringup/ is a test material no row of materials.csv names, and
# LICENSES does not list it.

ultra_art_files() {
  git -C "$REPO" ls-files assets/ultra |
    grep -v -e '^assets/ultra/\.gitignore$' -e '^assets/ultra/heroes/bringup/'
}

# stage_ultra_art <dest>: the art, into <dest>/. Refuses a map LICENSES does not list.
stage_ultra_art() {
  local files missing
  files="$(ultra_art_files)"
  missing="$(grep '\.png$' <<<"$files" | grep -v -x -F -f <(awk '{print $1}' "$REPO/assets/ultra/LICENSES") || true)"
  if [ -n "$missing" ]; then
    echo "ultra-art.sh: not listed in assets/ultra/LICENSES:" >&2
    echo "$missing" >&2
    return 1
  fi
  mkdir -p "$1"
  # shellcheck disable=SC2086  # the paths have no spaces, and git refuses a bad one
  git -C "$REPO" archive HEAD -- $files | tar -x --strip-components=2 -C "$1"
}

# check_ultra_art <dir>: <dir> holds exactly the art, nothing missing and nothing extra.
check_ultra_art() {
  local want got
  want="$(ultra_art_files | sed 's#^assets/ultra/##' | sort)"
  got="$(cd "$1" && find . -type f | sed 's#^\./##' | sort)"
  if [ "$want" != "$got" ]; then
    echo "ultra-art.sh: the art in $1 is wrong:" >&2
    diff <(echo "$want") <(echo "$got") >&2 || true
    return 1
  fi
}
