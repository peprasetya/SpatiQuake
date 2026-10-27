#!/usr/bin/env bash
# Start vkQuake, let it draw, take a screenshot and quit; fail if it did not get that far.
#
#   spatiquake/tools/smoke-test.sh VKQUAKE BASEDIR OUT.png [ARGS...]
#
# BASEDIR holds id1/ (LibreQuake's paks). ARGS go to the engine before the test's own commands,
# e.g. "+map lq_e1m1". Without a DISPLAY an Xvfb is started; without a GPU Mesa's lavapipe draws.
# FRAMES (default 60) is how many frames are drawn before the screenshot.
set -euo pipefail

VKQUAKE="$(realpath "$1")"
BASEDIR="$(realpath "$2")"
OUT="$3"
shift 3
FRAMES="${FRAMES:-60}"
mkdir -p "$(dirname "$OUT")"

if [[ -z "${DISPLAY:-}" ]]; then
    export DISPLAY=:$((90 + RANDOM % 9))
    Xvfb "$DISPLAY" -screen 0 1280x720x24 >/dev/null 2>&1 &
    XVFB=$!
    trap 'kill $XVFB 2>/dev/null || true' EXIT
    sleep 1
fi

# The commands go in a config under the game directory: a screenshot needs frames drawn first, and
# `wait` is the only way to ask for a frame from a script.
CFG="$BASEDIR/id1/spatiquake_smoke.cfg"
{
    for ((i = 0; i < FRAMES; i++)); do printf 'wait\n'; done
    printf 'screenshot png\nwait\nwait\nquit\n'
} > "$CFG"
STAMP="$BASEDIR/id1/.spatiquake_smoke_start"
touch "$STAMP"

LOG="${OUT%.png}.log"
set +e
timeout "${TIMEOUT:-120}" "$VKQUAKE" -basedir "$BASEDIR" -window -width 960 -height 540 "$@" +exec spatiquake_smoke.cfg >"$LOG" 2>&1
status=$?
set -e
rm -f "$CFG"
# vkQuake names its screenshots itself (vkQuake-<map>-<time>.png); take the one this run wrote.
SHOT="$(find "$BASEDIR/id1" -maxdepth 1 -name 'vkQuake-*.png' -newer "$STAMP" | head -1)"
rm -f "$STAMP"
if [[ -z "$SHOT" ]]; then
    echo "no screenshot (exit $status); the end of the log:" >&2
    tail -40 "$LOG" >&2
    exit 1
fi
mv "$SHOT" "$OUT"
grep -E "QUAKE ERROR|Host_Error" "$LOG" && { echo "the engine reported an error" >&2; exit 1; }
echo "ok: $OUT (exit $status)"
