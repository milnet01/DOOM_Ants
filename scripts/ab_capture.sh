#!/bin/bash
# Headless A/B look capture — one deterministic frame at a map coordinate.
#
# Built for DOOM-0330 (does the goo light the pool walls?) but nothing in it is
# specific to that: it is the general shape of "toggle one thing, photograph the
# same view twice, and be able to trust the difference".
#
# The two flags that make it a MEASUREMENT rather than a screenshot are
# -inspect -freeze (needs `make DEV=1`): monsters ignore you, nothing moves, and
# nothing takes health off you. With them the same-build noise floor is
# 0.01/255 with 0.0% of pixels moving; without them a walking zombie alone moves
# ~15% of the frame, which is indistinguishable from the effect under test.
#
# Always capture a same-build CONTROL (run it twice with identical settings) —
# it is the cheapest validity check there is, and a control that cannot move
# proves the harness before it proves the effect.
#
#   usage: ab_capture.sh <outdir> <name> <x> <y> <deg> [extra doom args...]
#   env:   any experiment gate you have compiled in, e.g. RB_NOLIQUIDLE=1
#          DOOMCFG=<path>  override the temp config (default: ~/.doomrc, fog off)
#          ONSCREEN=1      run on the user's real display instead of a private one
#
# By default the engine runs on a PRIVATE GPU-backed display (xwfb-run + cage, the
# same recipe as demoreel --gpu), so no window opens on the user's desktop and the
# Vulkan tiers still get the real graphics card. WAYLAND_DISPLAY is pointed at a name
# that cannot resolve rather than unset: unset, SDL falls back to wayland-0 and the
# window lands on the real desktop. The window there is the engine's own size, not
# the display's, so compare private captures only with private captures.
#
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"; NAME="$2"; X="$3"; Y="$4"; DEG="$5"; shift 5
mkdir -p "$OUT"; cd "$OUT"

# NEVER point the engine at ~/.doomrc — it rewrites the user's live config on
# exit. Copy it and override there. Fog off by default so a surface-lighting A/B
# is not contaminated by the fog term.
CFG="${DOOMCFG:-}"
if [ -z "$CFG" ]; then
    CFG="$OUT/doomrc-ab"
    # Drop any rt_fog line and append one, rather than rewrite it in place: a
    # ~/.doomrc with no rt_fog line left the substitution matching nothing, and
    # the capture ran at the engine default while this said fog was off (DOOM-0413).
    grep -v '^rt_fog[[:space:]]' ~/.doomrc > "$CFG" || true
    printf 'rt_fog\t\t0\n' >> "$CFG"
fi

rm -rf dev-shots
# Ultra silently renders PALETTED art without this; the log line below is the check.
export DOOMASSETDIR="$REPO/assets/ultra/"
WRAP=()
if [ -z "${ONSCREEN:-}" ]; then
    if ! command -v xwfb-run >/dev/null || ! command -v cage >/dev/null; then
        echo "FAIL $NAME — private display needs xwfb-run and cage (or set ONSCREEN=1)"; exit 1
    fi
    # The engine's own timeout sits INSIDE the wrap. Killing xwfb-run from outside
    # leaves cage, Xwayland and the engine running; letting the engine exit first
    # makes xwfb-run tear its display down. The outer timeout is only a backstop.
    WRAP=(timeout -k 5 -s TERM 60
          xwfb-run -c cage -s '\-geometry' -s 1920x1080 --
          env WAYLAND_DISPLAY=doom-ab-no-wayland SDL_VIDEODRIVER=x11)
fi
"${WRAP[@]}" timeout -s TERM 40 "$REPO/linuxdoom-1.10/linux/linuxxdoom" \
    -iwad "$REPO/wads/doom.wad" -config "$CFG" \
    -warp 1 1 -warpto "$X" "$Y" "$DEG" \
    -inspect -freeze -noinput -devshot 150 "$@" > "$OUT/$NAME.log" 2>&1 || true

if [ ! -f dev-shots/shot-0001.png ]; then
    echo "FAIL $NAME — no shot written"; tail -5 "$OUT/$NAME.log"; exit 1
fi
mv dev-shots/shot-0001.png "$OUT/$NAME.png"
# Only Ultra (renderer 1) loads HD art, so only Ultra owes the log line — EnsureHdMaterials
# returns immediately on any other tier, so this guard is fatal to a Solid capture unless it
# is conditional. The tier comes from the config the caller chose (DOOMCFG), never from a
# flag: without an explicit DOOMCFG a capture inherits whatever tier was last played in,
# which is how a "Solid" measurement silently becomes an Ultra one.
if grep -qE '^renderer[[:space:]]+1$' "$CFG"; then
    grep -m1 'HD load done' "$OUT/$NAME.log" \
        || { echo "!! $NAME: no HD load line — Ultra rendered PALETTED art"; exit 1; }
fi
echo "OK $NAME.png"
