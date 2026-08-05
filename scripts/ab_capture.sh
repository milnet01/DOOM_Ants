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
    sed -e 's/^rt_fog\t\t.*/rt_fog\t\t0/' ~/.doomrc > "$CFG"
fi

rm -rf dev-shots
# Ultra silently renders PALETTED art without this; the log line below is the check.
export DOOMASSETDIR="$REPO/assets/ultra/"
timeout -s TERM 40 "$REPO/linuxdoom-1.10/linux/linuxxdoom" \
    -iwad "$REPO/wads/doom.wad" -config "$CFG" \
    -warp 1 1 -warpto "$X" "$Y" "$DEG" \
    -inspect -freeze -noinput -devshot 150 "$@" > "$OUT/$NAME.log" 2>&1 || true

if [ ! -f dev-shots/shot-0001.png ]; then
    echo "FAIL $NAME — no shot written"; tail -5 "$OUT/$NAME.log"; exit 1
fi
mv dev-shots/shot-0001.png "$OUT/$NAME.png"
grep -m1 'HD load done' "$OUT/$NAME.log" \
    || { echo "!! $NAME: no HD load line — Ultra rendered PALETTED art"; exit 1; }
echo "OK $NAME.png"
