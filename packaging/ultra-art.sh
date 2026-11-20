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
#
# The pictures ship baked into one file, ultra.hdpack, and their PNGs stay behind
# (DOOM-0632). The baker is built for this machine by `make hdpack`; a caller runs
# that before staging or checking.

HDPACK="$REPO/linuxdoom-1.10/linux/hdpack"

ultra_art_files() {
  git -C "$REPO" ls-files assets/ultra |
    grep -v -e '^assets/ultra/\.gitignore$' -e '^assets/ultra/heroes/bringup/'
}

# stage_ultra_art <dest>: the pack and every committed file that is not a PNG, into
# <dest>/. Refuses a map LICENSES does not list.
stage_ultra_art() {
  local files missing
  files="$(ultra_art_files)"
  missing="$(grep '\.png$' <<<"$files" | grep -v -x -F -f <(awk '{print $1}' "$REPO/assets/ultra/LICENSES") || true)"
  if [ -n "$missing" ]; then
    echo "ultra-art.sh: not listed in assets/ultra/LICENSES:" >&2
    echo "$missing" >&2
    return 1
  fi
  [ -x "$HDPACK" ] || { echo "ultra-art.sh: $HDPACK is not built (make hdpack)" >&2; return 1; }
  mkdir -p "$1"
  # shellcheck disable=SC2086  # the paths have no spaces, and git refuses a bad one
  git -C "$REPO" archive HEAD -- $files | tar -x --strip-components=2 -C "$1" || return 1
  # The PNGs are baked where they were staged and then removed. `git archive` dates every
  # file by the commit, and the pack records each PNG's date, so two stagings of one
  # commit bake the same bytes (INV-9).
  grep '\.png$' <<<"$files" | sed 's#^assets/ultra/##' > "$1/ultra.hdpack.files"
  nice -n 19 "$HDPACK" "$1" --files "$1/ultra.hdpack.files" --out "$1/ultra.hdpack" || return 1
  rm "$1/ultra.hdpack.files"
  find "$1" -name '*.png' -delete
  find "$1" -type d -empty -delete
}

# same_list <what> <dir> <want> <got>: fails, showing the difference, unless the two match.
same_list() {
  [ "$3" = "$4" ] && return 0
  echo "ultra-art.sh: $1 in $2 is wrong:" >&2
  diff <(echo "$3") <(echo "$4") >&2 || true
  return 1
}

# check_ultra_art <dir>: <dir> holds exactly the pack and the committed non-PNG files,
# and the pack holds exactly the committed PNGs (INV-8).
check_ultra_art() {
  local files
  files="$(ultra_art_files | sed 's#^assets/ultra/##')"
  [ -x "$HDPACK" ] || { echo "ultra-art.sh: $HDPACK is not built (make hdpack)" >&2; return 1; }
  same_list "the art" "$1" \
    "$({ grep -v '\.png$' <<<"$files"; echo ultra.hdpack; } | sort)" \
    "$(cd "$1" && find . -type f | sed 's#^\./##' | sort)" || return 1
  same_list "the pack's pictures" "$1" \
    "$(grep '\.png$' <<<"$files" | sort)" \
    "$("$HDPACK" --list "$1/ultra.hdpack" | awk '{print $1}' | sort)"
}

# The game unpacks the pack with LZ4, whose BSD licence asks a binary to carry its
# notice. The notice is the comment that opens the vendored source.
# stage_lz4_notice <file> writes it; check_lz4_notice <file> fails unless it is there.
stage_lz4_notice() {
  sed '/^\*\//q' "$REPO/linuxdoom-1.10/lz4.c" > "$1"
  check_lz4_notice "$1"
}

check_lz4_notice() {
  grep -q 'Redistributions in binary form' "$1" 2>/dev/null ||
    { echo "ultra-art.sh: LZ4's licence notice is missing from $1" >&2; return 1; }
}
