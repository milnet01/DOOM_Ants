#!/usr/bin/env bash
#
# ci-local.sh -- run the SAME build + unit-test gate that the GitHub Actions "build"
# workflow runs (.github/workflows/build.yml), but on your machine, so a red CI can
# be caught BEFORE pushing.
#
# It builds from a CLEAN export of HEAD (git archive) into a throwaway temp dir, so
# it reproduces CI's fresh checkout exactly -- catching problems that only appear
# there, e.g. a missing output directory, a source file you forgot to `git add`, or
# a shader that doesn't compile. (Your normal `make` reuses old build products and
# hides those.)
#
# It does NOT apt-install anything: it assumes your machine already has the toolchain
# (it must, to build locally at all). The packages CI installs are listed here so the
# two stay in sync -- if you change one, change the other:
#     build-essential libsdl2-dev libsdl2-mixer-dev libvulkan-dev glslc xxd
#
# The build/test COMMANDS below must match the workflow's steps exactly.
#
# Usage:
#   packaging/ci-local.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

# CI only ever sees committed code; this script builds HEAD. Warn if the working
# tree has changes that therefore won't be tested here.
if [ -n "$(git status --porcelain)" ]; then
  echo "NOTE: uncommitted changes present -- CI (and this script) build committed"
  echo "      code only, so those changes are NOT exercised here. Commit first to"
  echo "      test them."
  echo
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Exporting HEAD to a clean tree (mimics CI's fresh checkout)..."
git archive HEAD | tar -x -C "$WORK"

# --- mirror of .github/workflows/build.yml, "Build (Linux)" + "Unit tests" ---
echo "==> Build (Linux): make -j$(nproc)"
make -C "$WORK/linuxdoom-1.10" -j"$(nproc)"

echo "==> Unit tests: make test"
make -C "$WORK/linuxdoom-1.10" test

echo
echo "==================================================================="
echo " ci-local: PASSED -- build + tests are green on a clean HEAD checkout"
echo " (this is what GitHub Actions will run; safe to push)"
echo "==================================================================="
