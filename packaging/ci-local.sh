#!/usr/bin/env bash
#
# ci-local.sh -- run the SAME gate that the GitHub Actions "build" workflow
# (.github/workflows/build.yml) runs, on your machine, so a red CI is caught BEFORE
# pushing.
#
# It mirrors BOTH of the workflow's jobs:
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
#     path. The two jobs run at the same time, each in its own container, as on GitHub.
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
# commands below are the SAME ones build.yml uses. The image is read from build.yml's
# runs-on and the package list is a shared file, so neither can drift; if you change the
# make commands or the smoke step, change them in build.yml too.
#
# Usage:
#   packaging/ci-local.sh              # container if available, else native
#   packaging/ci-local.sh --native     # force native (fast, approximate)
#   packaging/ci-local.sh --container  # force container (errors if none installed)
#   packaging/ci-local.sh --docs       # documentation-only push: skip if the last CI
#                                      # run passed (needs ANTS_PUSH_REFS), else run all
#   packaging/ci-local.sh --classify-docs < paths   # exit 0 if every path is docs
#
# The pre-push hook passes --docs and --classify-docs itself (.ants/gate.conf); run
# by hand, the script always runs the whole gate.
#
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

# Background priority for everything this script starts, containers included: the
# gate shares the machine with games and other sessions, and losing a few seconds
# to them is better than stealing their CPU.
renice -n 10 $$ >/dev/null 2>&1 || true

# --- keep in lockstep with .github/workflows/build.yml ----------------------------
# The image is READ from build.yml's runs-on, so the two cannot drift: every job
# must run on the same pinned `ubuntu-NN.NN` runner, and that names the image.
# Anything else (ubuntu-latest, two different runners) stops the gate, because no
# fixed image could then be said to match.
RUNNER="$(awk '/^ *runs-on:/ {print $2}' .github/workflows/build.yml | sort -u)"
if ! [[ $RUNNER =~ ^ubuntu-[0-9]+\.[0-9]+$ ]]; then
  echo "ERROR: build.yml must run every job on one pinned ubuntu-NN.NN runner;" >&2
  echo "       found: $(echo "$RUNNER" | tr '\n' ' ')" >&2
  exit 1
fi
CI_IMAGE="docker.io/library/ubuntu:${RUNNER#ubuntu-}"
DEPS_FILE="packaging/ci-deps.txt"           # shared apt package list (build.yml reads it too)
# The build + test commands are the two `make` lines in the gate below.
# ----------------------------------------------------------------------------------

# Local-only speed-up, NOT part of the job (DOOM-0452): the containers reuse the
# .deb files earlier runs downloaded. apt-get update and install still run fresh
# every time, and apt checks each cached file's hash against today's index, so what
# gets installed is what GitHub installs. Kept in SCRATCH, off the system disk,
# which is short of space; autoclean drops packages the
# index no longer offers. One cache per job: the two containers run at the same
# time, and apt locks its cache directory, so a shared one would fail the second.
# SCRATCH: this machine's per-project scratch folder where it exists, else the
# repo's gitignored scratch/. DOOM_ANTS_SCRATCH overrides both.
SCRATCH_HOME="/mnt/Emulators/.claude/Scratchpad/DOOM_Ants"
if [ -n "${DOOM_ANTS_SCRATCH:-}" ]; then SCRATCH="$DOOM_ANTS_SCRATCH"
elif [ -d "$SCRATCH_HOME" ];       then SCRATCH="$SCRATCH_HOME/scratch"
else                                    SCRATCH="$REPO/scratch"; fi
APT_CACHE_LINUX="$SCRATCH/cache/ci-apt-linux"
APT_CACHE_WINDOWS="$SCRATCH/cache/ci-apt-windows"

MODE="auto"
DOCS_MODE=0
for arg in "$@"; do
  case "$arg" in
    --native)    MODE="native" ;;
    --container) MODE="container" ;;
    --docs)      DOCS_MODE=1 ;;
    --classify-docs) ;;           # handled below, before anything else runs
    *) echo "usage: $0 [--native|--container] [--docs] | --classify-docs" >&2; exit 2 ;;
  esac
done

# --- documentation-only pushes ------------------------------------------------------
# ONE path list: build.yml's paths-ignore on its push trigger. GitHub skips the
# whole workflow when every pushed file matches it, so this reads that list rather
# than keeping a second copy. Nothing the two jobs run reads those paths (the
# tests only name docs in comments), so for this pipeline the documentation checks
# are the empty set.
docs_patterns() {
  awk '
    /^  push:/                   { push = 1; next }
    /^  [a-z_]+:/                { push = 0 }
    push && /paths-ignore:/      { list = 1; next }
    list && /^ *- /              { sub(/^ *- */, ""); gsub(/\047|"/, ""); print; next }
    list                         { list = 0 }
  ' .github/workflows/build.yml
}

# --classify-docs: pushed paths on stdin, one per line. Exit 0 only when EVERY
# path matches a pattern, and there is at least one path and one pattern. The
# shared pre-push hook calls this (.ants/gate.conf docsCommand); any other exit,
# a crash included, makes the push a code push.
if [ "${1:-}" = "--classify-docs" ]; then
  mapfile -t pats < <(docs_patterns)
  [ "${#pats[@]}" -gt 0 ] || exit 1
  seen=0
  while IFS= read -r f; do
    [ -n "$f" ] || continue
    seen=1
    hit=0
    for p in "${pats[@]}"; do
      # shellcheck disable=SC2053  # the right side is a pattern on purpose;
      # in [[ ]] a * also matches /, which is what GitHub's ** means here.
      [[ $f == $p ]] && { hit=1; break; }
    done
    [ "$hit" = 1 ] || exit 1
  done
  [ "$seen" = 1 ]
  exit
fi

# --docs: the hook passes this on a documentation-only push. It takes the complete
# skip only when the last push-triggered run of this workflow on EVERY branch being
# pushed succeeded (~/.claude/standards/local-gate.md § 7, condition 1). Anything
# it cannot confirm -- a tag, an unknown push, no gh, a red or unfinished run --
# runs the full gate instead. It never skips on doubt.
if [ "$DOCS_MODE" = 1 ]; then
  docs_skip_ok() {
    local repo remote_ref branch conclusion
    [ -n "${ANTS_PUSH_REFS:-}" ] || { echo "    (no pushed refs given)"; return 1; }
    command -v gh >/dev/null 2>&1 || { echo "    (gh is not installed)"; return 1; }
    # gh would otherwise ask the fork parent (id-Software/DOOM), not this repo.
    repo="$(git remote get-url origin 2>/dev/null | sed -E 's#^.*github\.com[:/]##; s#\.git$##')"
    [ -n "$repo" ] || { echo "    (no GitHub origin)"; return 1; }
    while read -r _ _ remote_ref _; do
      [ -n "$remote_ref" ] || continue
      case "$remote_ref" in
        refs/heads/*) branch="${remote_ref#refs/heads/}" ;;
        *) echo "    ($remote_ref is not a branch)"; return 1 ;;
      esac
      conclusion="$(gh run list -R "$repo" --workflow build.yml --branch "$branch" \
        --event push --limit 1 --json conclusion -q '.[0].conclusion' 2>/dev/null)" || conclusion=""
      [ "$conclusion" = success ] || {
        echo "    (last push run on $branch: ${conclusion:-none or unfinished})"; return 1; }
      echo "    last push run of build.yml on $branch: success"
    done <<< "$ANTS_PUSH_REFS"
  }
  echo "==> Documentation-only push: GitHub runs nothing for these paths (paths-ignore)."
  if docs_skip_ok; then
    echo "==> SKIPPED the gate: nothing in the pipeline reads these paths, and the"
    echo "    last CI run passed. This is not a green run; no check was needed."
    exit 0
  fi
  echo "==> Cannot confirm the last CI run passed, so running the full gate."
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

# DOOM-0375: every check this run could not perform, so the banner can say so.
# A gate that reports PASSED for what it skipped is worse than no gate: the
# pre-push hook reads the exit status, and the skips are silent.
SKIPPED_JOBS=()

# CI only ever sees committed code; this script builds HEAD. Warn on a dirty tree.
if [ -n "$(git status --porcelain)" ]; then
  echo "NOTE: uncommitted changes present -- CI (and this script) build committed"
  echo "      code only, so those changes are NOT exercised here. Commit first."
  echo
fi

# In SCRATCH, not /tmp: /tmp is RAM on this machine, and the two build
# trees are a few hundred MB.
mkdir -p "$SCRATCH"
WORK_ROOT="$(mktemp -d "$SCRATCH/ci-local.XXXXXX")"
WIN_PID=""
trap '[ -n "$WIN_PID" ] && kill "$WIN_PID" 2>/dev/null; wait; rm -rf "$WORK_ROOT"' EXIT

# A clean export of HEAD per job. The container jobs run at the same time and
# both generate the shader headers inside their tree, so they must not share one.
export_head() {
  mkdir -p "$1"
  git archive HEAD | tar -x -C "$1"
}
echo "==> Exporting HEAD to a clean tree (mimics CI's fresh checkout)..."

if [ "$MODE" = "native" ]; then
  WORK="$WORK_ROOT/src"
  export_head "$WORK"
  echo "==> MODE: native -- $(gcc --version | head -1)"
  echo "    (APPROXIMATES CI; use the default container mode for an exact match)"
  echo "==> Build (Linux): make"
  make -C "$WORK/linuxdoom-1.10"
  echo "==> Unit tests: make test"
  make -C "$WORK/linuxdoom-1.10" test
  # DOOM-0203: headless boot smoke (native only — needs a local IWAD + SDL
  # runtime). CI runs this against Freedoom; locally we reuse whatever IWAD is
  # around. DOOM-0375: a WAD-less box no longer passes silently — the skip is
  # recorded and the run reports PARTIAL rather than PASSED.
  SMOKE_WAD=""
  for w in "$REPO/wads/doom.wad" "$REPO/wads/doom1.wad" \
           /usr/share/games/doom/freedoom1.wad /usr/share/games/doom/doom1.wad; do
    [ -f "$w" ] && { SMOKE_WAD="$w"; break; }
  done
  if [ -n "$SMOKE_WAD" ]; then
    echo "==> Headless boot smoke: -bootsmoke against $(basename "$SMOKE_WAD")"
    # The engine's own exit status decides, as in build.yml: a crash or hang
    # after the OK line still fails. Only the display filter may find nothing.
    smoke_rc=0
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      timeout 120 "$WORK/linuxdoom-1.10/linux/linuxxdoom" \
      -iwad "$SMOKE_WAD" -warp 1 1 -bootsmoke 105 2>&1 \
      | tee "$WORK/smoke.log" | { grep -E 'bootsmoke|error|Error' || true; } \
      || smoke_rc=$?
    [ "$smoke_rc" = 0 ] || { echo "    boot smoke FAILED: the engine exited $smoke_rc" >&2; exit 1; }
    grep -q "bootsmoke: .* tics simulated OK" "$WORK/smoke.log"
    echo "    boot smoke PASSED (engine booted + rendered 105 tics, exit 0)"
  else
    echo "==> Headless boot smoke SKIPPED (no IWAD found; CI runs it against Freedoom)"
    SKIPPED_JOBS+=("linux: headless boot smoke -- no IWAD found")
  fi
else
  echo "==> MODE: container ($ENGINE, $CI_IMAGE) -- faithful mirror of GitHub CI"
  # Refresh the image first, as GitHub refreshes its runner image: a local copy
  # pulled months ago carries older base packages than the runner has. Only a
  # changed digest downloads anything. Offline, the local copy is used and the
  # run says so.
  if "$ENGINE" pull -q "$CI_IMAGE" >/dev/null 2>&1; then
    echo "    image is current"
  elif "$ENGINE" image inspect "$CI_IMAGE" >/dev/null 2>&1; then
    echo "    WARNING: could not refresh $CI_IMAGE (offline?) -- using the local copy"
  else
    echo "ERROR: cannot pull $CI_IMAGE and there is no local copy." >&2
    exit 1
  fi

  # The apt set-up both jobs share. Local-only speed-ups, none part of the job:
  # downloads stay in the mounted cache (docker-clean would delete them); apt
  # downloads as root, because under rootless podman an _apt-owned partial/ maps
  # to a host uid the user cannot read or delete; and dpkg skips its fsync calls
  # (force-unsafe-io), which only protect against a power cut mid-install, and
  # the container is thrown away either way.
  APT_SETUP='
    export DEBIAN_FRONTEND=noninteractive
    rm -f /etc/apt/apt.conf.d/docker-clean
    echo "APT::Sandbox::User \"root\";" > /etc/apt/apt.conf.d/99doom-cache
    echo force-unsafe-io > /etc/dpkg/dpkg.cfg.d/99doom-unsafe-io
    apt-get update
  '

  # Runs one job's script in a fresh container over its own HEAD export. As root,
  # as GitHub's steps are; the mounted export is throwaway. ':Z' relabels the bind
  # mounts for SELinux and is a no-op where SELinux is off.
  run_job() {  # <tree> <apt cache> <script>
    mkdir -p "$2"
    "$ENGINE" run --rm -v "$1":/src:Z -v "$2":/var/cache/apt/archives:Z \
        -w /src "$CI_IMAGE" bash -euc "$APT_SETUP$3"
  }

  # --- job 1 of 2: linux -- step-for-step mirror of build.yml's `linux` job ------
  # The shared dep list plus freedoom (the boot smoke's IWAD), then the two make
  # commands and the DOOM-0203 boot smoke.
  # shellcheck disable=SC2016  # the $(...) expand inside the container, not here
  LINUX_JOB='
    set -o pipefail
    apt-get install -y --no-install-recommends \
      $(awk "NF && \$1 !~ /^#/ {print \$1}" '"$DEPS_FILE"') freedoom
    apt-get autoclean -y >/dev/null
    echo "container: $(gcc --version | head -1)"
    make -C linuxdoom-1.10
    make -C linuxdoom-1.10 test
    export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
    timeout 120 ./linuxdoom-1.10/linux/linuxxdoom \
      -iwad /usr/share/games/doom/freedoom1.wad \
      -warp 1 1 -bootsmoke 105 2>&1 | tee smoke.log
    grep -q "bootsmoke: .* tics simulated OK" smoke.log
  '

  # --- job 2 of 2: windows-syntax -- mirror of build.yml's `windows-syntax` job --
  # Cross-compiles every translation unit with -fsyntax-only and never links.
  # Deliberately NOT ci-deps.txt: as in the workflow, only the cross-compilers and
  # the two header generators. windows-smoke.sh stages the upstream SDL2 /
  # SDL2_mixer / Vulkan headers itself (packaging/mingw-deps.sh), so a fresh
  # export downloads them -- which is what the runner does too.
  #
  # The last four packages are what the GitHub runner image already has and a bare
  # ubuntu image does not -- make drives the header generation, curl fetches the
  # upstream headers. Adding them keeps the JOB identical, not the image.
  # pkg-config is here only so the Makefile emits the same expected "Package sdl2
  # was not found" noise the workflow documents, rather than a different
  # "pkg-config: No such file or directory"; nothing here links against SDL2.
  WINDOWS_JOB='
    apt-get install -y --no-install-recommends \
      gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 glslc xxd \
      make curl ca-certificates pkg-config
    apt-get autoclean -y >/dev/null
    packaging/windows-smoke.sh --syntax-only
  '

  # Both jobs at once, as GitHub runs them. Each has its own tree and apt cache,
  # so neither can see the other's files or locks. The Windows job's output is
  # held in a file and printed after the Linux job's, so the two never interleave.
  export_head "$WORK_ROOT/linux"
  export_head "$WORK_ROOT/windows"
  echo "==> Running both jobs in parallel; the Windows job's output follows the Linux job's."
  echo
  echo "==> Linux build + tests (container, $CI_IMAGE)"
  run_job "$WORK_ROOT/windows" "$APT_CACHE_WINDOWS" "$WINDOWS_JOB" \
    > "$WORK_ROOT/windows.log" 2>&1 &
  WIN_PID=$!
  LINUX_RC=0
  run_job "$WORK_ROOT/linux" "$APT_CACHE_LINUX" "$LINUX_JOB" || LINUX_RC=$?
  WIN_RC=0
  wait "$WIN_PID" || WIN_RC=$?
  WIN_PID=""
  echo
  echo "==> Windows cross-compile check (container, $CI_IMAGE)"
  cat "$WORK_ROOT/windows.log"

  if [ "$LINUX_RC" != 0 ] || [ "$WIN_RC" != 0 ]; then
    echo
    echo "==================================================================="
    echo " ci-local: FAILED"
    [ "$LINUX_RC" = 0 ] || echo "   linux          exit $LINUX_RC -- see its output above"
    [ "$WIN_RC" = 0 ]   || echo "   windows-syntax exit $WIN_RC -- see its output above"
    echo "==================================================================="
    exit 1
  fi
fi

# --- native job 2 of 2: windows-syntax ---------------------------------------------
# The container mode ran this job above, beside the Linux one. Natively it runs
# here, after the Linux job, with this machine's cross-compiler.
if [ "$MODE" = "native" ]; then
  echo
  echo "==> Windows cross-compile check (native toolchain)"
  if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    ( cd "$WORK" && packaging/windows-smoke.sh --syntax-only )
  else
    echo "    SKIPPED -- no mingw-w64 cross-compiler installed."
    echo "    CI runs this job; install gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64,"
    echo "    or use the default container mode, to cover it locally."
    SKIPPED_JOBS+=("windows-syntax: cross-compile sweep -- no mingw-w64")
  fi
fi

echo
echo "==================================================================="
if [ ${#SKIPPED_JOBS[@]} -eq 0 ]; then
  echo " ci-local: PASSED -- both CI jobs green on a clean HEAD checkout"
  echo "   linux          build + unit tests + headless boot smoke"
  echo "   windows-syntax cross-compile syntax sweep"
  echo " (MODE=$MODE$( [ "$MODE" = container ] && echo '; this is exactly what GitHub Actions runs' ))"
  echo "==================================================================="
else
  # DOOM-0375: what ran was green, but the gate did not cover this push. Exit 2
  # rather than 0 or 1, so the pre-push hook can tell "could not check" from
  # "checked and failed".
  echo " ci-local: PARTIAL -- ${#SKIPPED_JOBS[@]} check(s) did not run here"
  for job in "${SKIPPED_JOBS[@]}"; do
    echo "   NOT RUN: $job"
  done
  echo " Everything that did run was green. CI will run the rest, unchecked by you."
  echo " (MODE=$MODE)"
  echo "==================================================================="
  exit 2
fi
