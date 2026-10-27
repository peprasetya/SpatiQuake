#!/usr/bin/env bash
# SpatiQuake in a test Spatiand, without glasses: a picture of the room as the compositor composes
# it, and the pictures the engine drew for each eye and for the 2D layer.
#
#   spatiquake/tools/test-with-spatiand.sh SPATIAND_REPO VKQUAKE BASEDIR OUT_DIR [ENGINE ARGS...]
#
# SPATIAND_REPO is a checkout of peprasetya/spatiand with `spatiand` and `spatiand-openxr` built
# (`cargo build --release -p spatiand -p spatiand-openxr`); its runtime is installed with its own
# tools/install-openxr.sh. Spatiand runs with its snapshot backend, no headset and no input
# (SPATIAND_HMD=null SPATIAND_INPUT=off), in a configuration of its own (an empty XDG_CONFIG_HOME,
# so it does not connect to a host), starts the engine as its client with -xr, waits WAIT seconds
# (default 40) and writes OUT_DIR/room.png. The engine takes xr_screenshot after FRAMES frames
# (default 200): OUT_DIR/eyes-left.png, -right.png and -2d.png.
#
# Needs a GPU: the runtime hands its pictures to the compositor as dmabufs, which Mesa's software
# Vulkan cannot export (in a container without /dev/dri the session runs and the room stays empty).
# ENGINE ARGS go to the engine, e.g. "+map lq_e1m1".
set -euo pipefail
SPATIAND="$(realpath "$1")"
VKQUAKE="$(realpath "$2")"
BASEDIR="$(realpath "$3")"
OUT="$(mkdir -p "$4" && realpath "$4")"
shift 4
FRAMES="${FRAMES:-200}"
WAIT="${WAIT:-40}"

"$SPATIAND/tools/install-openxr.sh" >/dev/null
RUNTIME="$HOME/.local/share/spatiand-openxr/spatiand_openxr.json"

WORK="$(mktemp -d "$HOME/.spatiquake-test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/config" "$WORK/runtime"
chmod 700 "$WORK/runtime"

# The engine's commands: draw, take the three pictures, keep running until Spatiand photographs.
CFG="$BASEDIR/id1/spatiquake_spatiand.cfg"
{
    for ((i = 0; i < FRAMES; i++)); do printf 'wait\n'; done
    printf 'xr_screenshot\n'
} > "$CFG"
STAMP="$WORK/stamp"
touch "$STAMP"

cat > "$WORK/client.sh" <<EOF
#!/bin/sh
export XR_RUNTIME_JSON="$RUNTIME" SPATIAND_OPENXR_STDERR=1
exec "$VKQUAKE" -basedir "$BASEDIR" -window -xr $* +exec spatiquake_spatiand.cfg > "$OUT/engine.log" 2>&1
EOF
chmod +x "$WORK/client.sh"

set +e
XDG_CONFIG_HOME="$WORK/config" XDG_RUNTIME_DIR="$WORK/runtime" \
    SPATIAND_BACKEND=snapshot SPATIAND_HMD=null SPATIAND_INPUT=off \
    SPATIAND_SNAPSHOT="$OUT/room.png" SPATIAND_CLIENT="$WORK/client.sh" SPATIAND_CLIENT_WAIT="$WAIT" \
    timeout $((WAIT + 120)) "$SPATIAND/target/release/spatiand" > "$OUT/spatiand.log" 2>&1
status=$?
set -e
rm -f "$CFG"

mapfile -t SHOTS < <(find "$BASEDIR/id1" -maxdepth 1 -name 'vkQuake-*.png' -newer "$STAMP" | sort)
if ((${#SHOTS[@]} >= 3)); then
    mv "${SHOTS[0]}" "$OUT/eyes-left.png"
    mv "${SHOTS[1]}" "$OUT/eyes-right.png"
    mv "${SHOTS[2]}" "$OUT/eyes-2d.png"
fi
echo "spatiand exited $status"
grep -E "became the environment|never committed|wrote" "$OUT/spatiand.log" || true
grep -E "OpenXR: .*failed|could not|ERROR" "$OUT/engine.log" | sort | uniq -c | head || true
ls -la "$OUT"
