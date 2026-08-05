#!/usr/bin/env bash
#
# windows-smoke.sh — run the *Windows* build on this Linux box, without cutting
# a release. Cross-builds the .exe, then boots it under Wine on a virtual
# screen and asserts the engine actually reached the end of its boot smoke.
#
# Why this exists: CI builds Linux only, so the mingw target used to be
# compiled exactly once per release (packaging/release.sh). Two Windows-only
# compile errors accumulated over the 193 commits between v0.5.0 and v0.6.0 and
# surfaced only when 0.6.0 was cut. See DOOM-0324.
#
# Usage:
#   packaging/windows-smoke.sh                 # build + boot + assert
#   packaging/windows-smoke.sh --no-build      # reuse mingw/doom_ants.exe
#   packaging/windows-smoke.sh --syntax-only   # compile check alone, no Wine
#   packaging/windows-smoke.sh --iwad path.wad # pick the data file
#   packaging/windows-smoke.sh --keep          # keep the sandbox for poking at
#
# On a clean checkout this stages mingw-deps/prefix (packaging/mingw-deps.sh,
# which downloads) and generates the embedded shader/font headers first. Both
# are no-ops on a tree that has been built before.
#
# Exit codes — deliberately distinct, so a partial pass can't read as a pass:
#   0  the engine booted, simulated its tics and exited cleanly
#   1  the build (or syntax sweep) failed
#   2  the engine never reached the boot-smoke line — it died or hung early
#   3  the engine booted fine but never exited (shutdown hang; DOOM-0325)
#
# SAFETY: everything runs on a private Xvfb display and in a throwaway
# WINEPREFIX under the sandbox dir, so the game can never appear on the user's
# screen or steal focus, and teardown kills only this run's wineserver -- never
# `wine` by name, which would end a game the user is playing.
#
set -uo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
ENG="$REPO/linuxdoom-1.10"
WIN_PREFIX="$REPO/mingw-deps/prefix"
WINPTHREAD="/usr/x86_64-w64-mingw32/sys-root/mingw/bin/libwinpthread-1.dll"

BUILD=1
SYNTAX_ONLY=0
KEEP=0
TICS=35
IWAD=""
for arg in "$@"; do
  case "$arg" in
    --no-build)    BUILD=0 ;;
    --syntax-only) SYNTAX_ONLY=1 ;;
    --keep)        KEEP=1 ;;
    --tics=*)      TICS="${arg#--tics=}" ;;
    --iwad)        echo "windows-smoke.sh: --iwad needs =path, e.g. --iwad=../wads/doom.wad" >&2; exit 1 ;;
    --iwad=*)      IWAD="${arg#--iwad=}" ;;
    *) echo "windows-smoke.sh: unknown flag '$arg'" >&2; exit 1 ;;
  esac
done

# ---- 0. preconditions --------------------------------------------------------
# Both of these exist on a machine that has built the tree before, which is why
# they went unnoticed until this ran on a clean checkout (a CI runner):
#   - the upstream SDL2 / SDL2_mixer / Vulkan headers the sweep compiles against
#   - the generated *.spv.h / *.ttf.h headers r_vulkan.cpp #includes
# Both steps are no-ops once satisfied, so this costs nothing on a warm tree.
command -v x86_64-w64-mingw32-gcc >/dev/null || {
  echo "windows-smoke.sh: mingw cross-compiler not found" >&2; exit 1; }

if [ "$SYNTAX_ONLY" = 1 ]; then
  "$REPO/packaging/mingw-deps.sh" --headers-only || exit 1
else
  "$REPO/packaging/mingw-deps.sh" || exit 1
fi

echo "==> Generating the embedded shader / font headers..."
make -C "$ENG" generated >/dev/null || { echo "FAIL: could not generate the embedded headers"; exit 1; }

# ---- 1. compile sweep --------------------------------------------------------
# -fsyntax-only over every TU catches the whole class that bit us (a POSIX-only
# call, a header glibc supplies transitively and mingw does not) in seconds,
# with no linking and no staged import libraries. Worth running even when the
# full build follows, because it reports EVERY broken file rather than stopping
# at the first one make happens to schedule.
echo "==> Syntax-checking every source file against the Windows compiler..."
INC=(-I"$WIN_PREFIX/include" -I"$WIN_PREFIX/include/SDL2" -I"$ENG")
SWEEP_FAIL=0
for f in "$ENG"/*.c; do
  out=$(x86_64-w64-mingw32-gcc -fsyntax-only -Wall -std=gnu11 -DNORMALUNIX -DLINUX \
          "${INC[@]}" "$f" 2>&1) || { echo "--- $(basename "$f")"; echo "$out" | grep error | head -5; SWEEP_FAIL=1; }
done
for f in "$ENG"/*.cpp; do
  out=$(x86_64-w64-mingw32-g++ -fsyntax-only -std=gnu++20 -DNORMALUNIX -DLINUX \
          "${INC[@]}" "$f" 2>&1) || { echo "--- $(basename "$f")"; echo "$out" | grep error | head -5; SWEEP_FAIL=1; }
done
[ "$SWEEP_FAIL" = 0 ] || { echo "FAIL: the Windows compiler rejects the tree (see above)"; exit 1; }
echo "    all translation units compile for Windows."
[ "$SYNTAX_ONLY" = 1 ] && { echo "PASS (syntax only)."; exit 0; }

# ---- 2. link -----------------------------------------------------------------
EXE="$ENG/mingw/doom_ants.exe"
if [ "$BUILD" = 1 ]; then
  echo "==> Cross-building doom_ants.exe..."
  make -C "$ENG" windows >/dev/null || { echo "FAIL: the Windows build did not link"; exit 1; }
fi
[ -f "$EXE" ] || { echo "FAIL: $EXE missing (drop --no-build?)" >&2; exit 1; }

# ---- 3. can we actually run it? ----------------------------------------------
for t in wine Xvfb; do
  command -v "$t" >/dev/null || {
    echo "SKIP: '$t' not installed — compile check passed, but the engine was not run."
    echo "      (openSUSE: zypper install wine xorg-x11-server-Xvfb)"
    exit 0; }
done

# Resolve the data file. The engine needs one; it is not in the repo.
if [ -z "$IWAD" ]; then
  for cand in "$REPO/wads/doom.wad" "$REPO/wads/doom2.wad" "$REPO/wads/doom1.wad" \
              /usr/share/games/doom/freedoom1.wad /usr/share/doom/freedoom1.wad; do
    [ -f "$cand" ] && { IWAD="$cand"; break; }
  done
fi
[ -n "$IWAD" ] && [ -f "$IWAD" ] || {
  echo "SKIP: no IWAD found — compile + link passed, but the engine was not run."
  echo "      Pass one with --iwad=/path/to/doom.wad"
  exit 0; }

# ---- 4. sandbox --------------------------------------------------------------
SANDBOX="$(mktemp -d -t doom-ants-winsmoke-XXXXXX)"
XVFB_PID=""
cleanup() {
  # Kill ONLY this run's wineserver, addressed through our own throwaway
  # prefix. Never `pkill wine` -- that would end a game the user is playing.
  [ -n "${WINEPREFIX:-}" ] && WINEPREFIX="$WINEPREFIX" timeout 20 wineserver -k >/dev/null 2>&1
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  if [ "$KEEP" = 1 ]; then echo "    sandbox kept: $SANDBOX"; else rm -rf "$SANDBOX"; fi
}
trap cleanup EXIT

STAGE="$SANDBOX/game"
mkdir -p "$STAGE"
cp "$EXE" "$STAGE/" || exit 1
for dll in "$WIN_PREFIX/bin/SDL2.dll" "$WIN_PREFIX/bin/SDL2_mixer.dll" "$WINPTHREAD"; do
  [ -f "$dll" ] || { echo "FAIL: missing runtime DLL '$dll'" >&2; exit 1; }
  cp "$dll" "$STAGE/"
done
cp "$IWAD" "$STAGE/game.wad"

# Classic (software) renderer: the boot smoke is about the game loop, and this
# keeps the check meaningful on a machine with no GPU (a CI runner) as well as
# on this one. `renderer 0` is RB_CLASSIC.
printf '%s\t%s\n' renderer 0 screenblocks 10 use_mouse 0 > "$STAGE/smoke.cfg"

# A private display. Pick a free number so a concurrent run cannot collide.
DISP=""
for n in $(seq 90 120); do
  [ -e "/tmp/.X11-unix/X$n" ] || { DISP=":$n"; break; }
done
[ -n "$DISP" ] || { echo "FAIL: no free X display number" >&2; exit 1; }
Xvfb "$DISP" -screen 0 1280x800x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 2
kill -0 "$XVFB_PID" 2>/dev/null || { echo "FAIL: Xvfb did not start on $DISP" >&2; exit 1; }

export WINEPREFIX="$SANDBOX/wineprefix"
export DISPLAY="$DISP"
export WINEDEBUG=-all
echo "==> Booting the Windows build under Wine (display $DISP, throwaway prefix)..."
WINEDEBUG=-all timeout 60 wineboot -i >/dev/null 2>&1

LOG="$SANDBOX/smoke.log"
START=$(date +%s)
( cd "$STAGE" && timeout 90 wine doom_ants.exe -iwad game.wad -config smoke.cfg -bootsmoke "$TICS" ) > "$LOG" 2>&1
RUN_RC=$?
ELAPSED=$(( $(date +%s) - START ))

# ---- 5. verdict --------------------------------------------------------------
# Two independent facts, reported separately: did the engine finish its work,
# and did the process then go away. Collapsing them into one boolean is how a
# shutdown hang gets normalised into "the smoke passes".
if ! grep -q "tics simulated OK" "$LOG"; then
  echo "FAIL: the engine never reached the end of its boot smoke (rc=$RUN_RC, ${ELAPSED}s)."
  tail -20 "$LOG" | sed 's/^/    /'
  exit 2
fi
echo "    $(grep 'tics simulated OK' "$LOG")"

if [ "$RUN_RC" -eq 124 ]; then
  echo "FAIL: booted and simulated $TICS tics, but the process never exited (${ELAPSED}s)."
  echo "      Windows-only shutdown hang in I_QuitTeardown — see DOOM-0325."
  exit 3
fi
if [ "$RUN_RC" -ne 0 ]; then
  echo "FAIL: booted and simulated $TICS tics, then exited with code $RUN_RC."
  exit 3
fi

echo "PASS: the Windows build boots, simulates $TICS tics and exits cleanly (${ELAPSED}s)."
exit 0
