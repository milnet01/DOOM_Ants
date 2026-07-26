#!/usr/bin/env bash
#
# ci-local.sh -- run the SAME build + unit-test gate that the GitHub Actions "build"
# workflow (.github/workflows/build.yml) runs, on your machine, so a red CI is caught
# BEFORE pushing.
#
# TWO MODES:
#   container (default when podman/docker is present) -- runs the gate INSIDE the same
#     Ubuntu image GitHub uses, with the exact apt install + make steps from build.yml.
#     This is a faithful mirror: it uses CI's compiler and library versions, not this
#     machine's (e.g. local gcc 15 vs CI's gcc 13 diverge), so it catches toolchain-
#     specific breakage a native build would miss. This is the "matches GitHub exactly"
#     path.
#   native (--native, or auto-fallback when no container runtime) -- builds with THIS
#     machine's toolchain. Fast, but only an APPROXIMATION of CI (different compiler/libs);
#     it still catches the fresh-checkout class of problems (see below).
#
# Both modes build from a CLEAN export of HEAD (git archive) into a throwaway dir, so
# they reproduce CI's fresh checkout exactly -- catching a file you forgot to `git add`,
# a missing output directory, or a shader that won't compile, which a reused local tree
# hides.
#
# The image, the apt package list (packaging/ci-deps.txt), and the build/test/boot-smoke
# commands below are the SAME ones build.yml uses. The package list is a shared file so it
# cannot drift; if you change the image, the make commands, or the smoke step, change them
# in build.yml too.
#
# Usage:
#   packaging/ci-local.sh              # container if available, else native
#   packaging/ci-local.sh --native     # force native (fast, approximate)
#   packaging/ci-local.sh --container  # force container (errors if none installed)
#
set -euo pipefail

# --- keep in lockstep with .github/workflows/build.yml ----------------------------
CI_IMAGE="docker.io/library/ubuntu:24.04"   # == GitHub 'ubuntu-latest' (currently 24.04);
                                            # bump both when GitHub moves ubuntu-latest.
DEPS_FILE="packaging/ci-deps.txt"           # shared apt package list (build.yml reads it too)
# The build + test commands are the two `make` lines in the gate below.
# ----------------------------------------------------------------------------------

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

MODE="auto"
case "${1:-}" in
  --native)    MODE="native" ;;
  --container) MODE="container" ;;
  "")          MODE="auto" ;;
  *) echo "usage: $0 [--native|--container]" >&2; exit 2 ;;
esac

ENGINE=""
if   command -v podman >/dev/null 2>&1; then ENGINE="podman"
elif command -v docker >/dev/null 2>&1; then ENGINE="docker"
fi

[ "$MODE" = "auto" ] && { [ -n "$ENGINE" ] && MODE="container" || MODE="native"; }
if [ "$MODE" = "container" ] && [ -z "$ENGINE" ]; then
  echo "ERROR: --container needs podman or docker, but neither is installed." >&2
  exit 1
fi

# CI only ever sees committed code; this script builds HEAD. Warn on a dirty tree.
if [ -n "$(git status --porcelain)" ]; then
  echo "NOTE: uncommitted changes present -- CI (and this script) build committed"
  echo "      code only, so those changes are NOT exercised here. Commit first."
  echo
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
echo "==> Exporting HEAD to a clean tree (mimics CI's fresh checkout)..."
git archive HEAD | tar -x -C "$WORK"

if [ "$MODE" = "native" ]; then
  echo "==> MODE: native -- $(gcc --version | head -1)"
  echo "    (APPROXIMATES CI; use the default container mode for an exact match)"
  echo "==> Build (Linux): make -j$(nproc)"
  make -C "$WORK/linuxdoom-1.10" -j"$(nproc)"
  echo "==> Unit tests: make test"
  make -C "$WORK/linuxdoom-1.10" test
  # DOOM-0203: best-effort headless boot smoke (native only — needs a local IWAD +
  # SDL runtime). CI runs this against Freedoom; locally we reuse whatever IWAD is
  # around. Skipped (not failed) when no WAD is found, so a WAD-less box still passes
  # the build+test gate.
  SMOKE_WAD=""
  for w in "$REPO/wads/doom.wad" "$REPO/wads/doom1.wad" \
           /usr/share/games/doom/freedoom1.wad /usr/share/games/doom/doom1.wad; do
    [ -f "$w" ] && { SMOKE_WAD="$w"; break; }
  done
  if [ -n "$SMOKE_WAD" ]; then
    echo "==> Headless boot smoke: -bootsmoke against $(basename "$SMOKE_WAD")"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      timeout 120 "$WORK/linuxdoom-1.10/linux/linuxxdoom" \
      -iwad "$SMOKE_WAD" -warp 1 1 -bootsmoke 105 2>&1 \
      | tee "$WORK/smoke.log" | grep -E 'bootsmoke|error|Error' || true
    grep -q "bootsmoke: .* tics simulated OK" "$WORK/smoke.log"
    echo "    boot smoke PASSED (engine booted + rendered 105 tics, exit 0)"
  else
    echo "==> Headless boot smoke SKIPPED (no IWAD found; CI runs it against Freedoom)"
  fi
else
  echo "==> MODE: container ($ENGINE, $CI_IMAGE) -- faithful mirror of GitHub CI"
  # Step-for-step mirror of build.yml inside the image: install the shared dep list,
  # then the two make commands. Runs as the image's root (as GitHub's steps do); the
  # mounted HEAD export is throwaway. ':Z' relabels the bind mount for SELinux and is
  # a no-op where SELinux is off.
  "$ENGINE" run --rm -v "$WORK":/src:Z -w /src "$CI_IMAGE" bash -euc '
    set -o pipefail
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends \
      $(awk "NF && \$1 !~ /^#/ {print \$1}" '"$DEPS_FILE"')
    echo "container: $(gcc --version | head -1)"
    make -C linuxdoom-1.10 -j"$(nproc)"
    make -C linuxdoom-1.10 test
    # DOOM-0203 boot smoke -- build.yml runs this too, so the container mirror has
    # to as well or "exactly what GitHub Actions runs" is false and a boot
    # regression passes here while CI goes red.
    apt-get install -y --no-install-recommends freedoom
    export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
    timeout 120 ./linuxdoom-1.10/linux/linuxxdoom \
      -iwad /usr/share/games/doom/freedoom1.wad \
      -warp 1 1 -bootsmoke 105 2>&1 | tee smoke.log
    grep -q "bootsmoke: .* tics simulated OK" smoke.log
  '
fi

echo
echo "==================================================================="
echo " ci-local: PASSED -- build + tests green on a clean HEAD checkout"
echo " (MODE=$MODE$( [ "$MODE" = container ] && echo '; this is exactly what GitHub Actions runs' ))"
echo "==================================================================="
