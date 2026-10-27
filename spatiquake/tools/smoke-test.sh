#!/usr/bin/env bash
# Start vkQuake, let it draw, take a screenshot and quit; fail if it did not get that far.
#
#   spatiquake/tools/smoke-test.sh VKQUAKE BASEDIR OUT.png [ARGS...]
#
# BASEDIR holds id1/ (LibreQuake's paks). ARGS go to the engine before the test's own commands,
# e.g. "+map lq_e1m1". Without a DISPLAY an Xvfb is started; without a GPU Mesa's lavapipe draws.
# FRAMES (default 60) is how many frames are drawn before the screenshot. SHOT (default
# "screenshot png") is the command that takes it; with "xr_screenshot" (the OpenXR mode) there are
# three pictures, saved as OUT with -left, -right and -2d before the .png. MAP loads that map
# first, from the test's own commands, so the frames counted are frames drawn in it (a +map given
# in ARGS loads while they are being counted, and the screenshot would find nothing drawn).
set -euo pipefail

VKQUAKE="$(realpath "$1")"
BASEDIR="$(realpath "$2")"
OUT="$3"
shift 3
FRAMES="${FRAMES:-60}"
SHOT="${SHOT:-screenshot png}"
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
    [[ -n "${MAP:-}" ]] && printf 'map %s\n' "$MAP"
    for ((i = 0; i < FRAMES; i++)); do printf 'wait\n'; done
    # Then frames enough for it to be written (it is taken from the first frame that gets an image
    # to draw into).
    printf '%s\n' "$SHOT"
    for ((i = 0; i < 30; i++)); do printf 'wait\n'; done
    # In a game, quit asks first (a menu); disconnected, it does not.
    printf 'disconnect\nwait\nquit\n'
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
mapfile -t SHOTS < <(find "$BASEDIR/id1" -maxdepth 1 -name 'vkQuake-*.png' -newer "$STAMP" | sort)
rm -f "$STAMP"
want=1
[[ "$SHOT" == xr_screenshot* ]] && want=3
if (( ${#SHOTS[@]} < want )); then
    (( status == 124 )) && echo "timed out after ${TIMEOUT:-120} s" >&2
    echo "${#SHOTS[@]} screenshots, wanted $want (exit $status); the end of the log:" >&2
    tail -40 "$LOG" >&2
    exit 1
fi
if (( want == 1 )); then
    mv "${SHOTS[0]}" "$OUT"
else
    mv "${SHOTS[0]}" "${OUT%.png}-left.png"
    mv "${SHOTS[1]}" "${OUT%.png}-right.png"
    mv "${SHOTS[2]}" "${OUT%.png}-2d.png"
fi
grep -E "QUAKE ERROR|Host_Error" "$LOG" && { echo "the engine reported an error" >&2; exit 1; }
echo "ok: $OUT (exit $status)"
