#!/usr/bin/env bash
#
# ci-local.sh -- run the SAME gate that the GitHub Actions "build" workflow
# (.github/workflows/build.yml) runs, on your machine, so a red CI is caught BEFORE
# pushing.
#
# It mirrors BOTH of the workflow's jobs, and its docs-only skip:
#   linux          -- build + unit tests + headless boot smoke
#   windows-syntax -- -fsyntax-only over every translation unit, cross-compiled
#
# Mirroring only the Linux job was the old gap, and it cost a red CI on 2026-08-12
# (run 31622988836): the Linux job passed, the Windows job failed, and nothing local
# had ever run the Windows job.
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
FORCE_RUN=0
for arg in "$@"; do
  case "$arg" in
    --native)    MODE="native" ;;
    --container) MODE="container" ;;
    --force)     FORCE_RUN=1 ;;   # run even when the change is docs-only
    *) echo "usage: $0 [--native|--container] [--force]" >&2; exit 2 ;;
  esac
done

# --- docs-only skip: mirrors build.yml's paths-ignore ------------------------------
# GitHub skips the whole workflow when every file changed in the push matches one of
# the workflow's paths-ignore patterns, so a docs push has no gate to pre-run. The
# patterns below are that list, and must be changed in both places together.
docs_only_change() {
  local upstream files f
  upstream="$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null)" || return 1
  files="$(git diff --name-only "$upstream..HEAD")" || return 1
  [ -n "$files" ] || return 1        # nothing to push -> nothing to conclude; run the gate
  while IFS= read -r f; do
    case "$f" in
      *.md|docs/*|.gitignore|LICENSE.TXT|README.TXT) ;;   # == build.yml paths-ignore
      *) return 1 ;;
    esac
  done <<< "$files"
  return 0
}

if [ "$FORCE_RUN" = 0 ] && docs_only_change; then
  echo "==> Docs-only change -- GitHub skips the workflow (paths-ignore), so there is"
  echo "    nothing to pre-run. Push away.  (--force runs the gate anyway.)"
  exit 0
fi
# ----------------------------------------------------------------------------------

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

# --- job 2 of 2: windows-syntax ----------------------------------------------------
# Mirrors build.yml's `windows-syntax` job, which cross-compiles every translation
# unit with -fsyntax-only and never links. Same one command the workflow runs.
#
# Deliberately NOT ci-deps.txt: as in the workflow, this job installs only the
# cross-compilers plus the two header generators. windows-smoke.sh stages the
# upstream SDL2 / SDL2_mixer / Vulkan headers itself (packaging/mingw-deps.sh), so a
# fresh export downloads them -- which is what the runner does too.
echo
if [ "$MODE" = "native" ]; then
  echo "==> Windows cross-compile check (native toolchain)"
  if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    ( cd "$WORK" && packaging/windows-smoke.sh --syntax-only )
  else
    echo "    SKIPPED -- no mingw-w64 cross-compiler installed."
    echo "    CI runs this job; install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64,"
    echo "    or use the default container mode, to cover it locally."
  fi
else
  echo "==> Windows cross-compile check (container, $CI_IMAGE)"
  "$ENGINE" run --rm -v "$WORK":/src:Z -w /src "$CI_IMAGE" bash -euc '
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    # The last four are what the GitHub runner image already has and a bare
    # ubuntu:24.04 does not -- make drives the header generation, curl fetches the
    # upstream headers. Adding them keeps the JOB identical, not the image.
    # pkg-config is here only so the Makefile emits the same expected "Package
    # sdl2 was not found" noise the workflow documents, rather than a different
    # "pkg-config: No such file or directory"; nothing here links against SDL2.
    apt-get install -y --no-install-recommends \
      gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 glslc xxd \
      make curl ca-certificates pkg-config
    packaging/windows-smoke.sh --syntax-only
  '
fi

echo
echo "==================================================================="
echo " ci-local: PASSED -- both CI jobs green on a clean HEAD checkout"
echo "   linux          build + unit tests + headless boot smoke"
echo "   windows-syntax cross-compile syntax sweep"
echo " (MODE=$MODE$( [ "$MODE" = container ] && echo '; this is exactly what GitHub Actions runs' ))"
echo "==================================================================="
