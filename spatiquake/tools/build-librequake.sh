#!/usr/bin/env bash
# Build LibreQuake's game data from its source, with its own build.py, into an id1 directory.
#
#   spatiquake/tools/build-librequake.sh [OUT]     OUT defaults to build/librequake/id1
#
# Fetches LibreQuake at a pinned commit, the tools its own CI uses (fteqcc, ericw-tools, qpakman),
# compiles the texture wads, the progs and the maps, and assembles its "full" release: pak0.pak,
# pak1.pak and the loose files beside them. Nothing is committed to this repository; the paks are
# GPL-2 (progs.dat, pop.lmp) and BSD-3 (the rest), see LibreQuake's docs/README-IMPORTANT-LICENCE-INFO.
#
# Environment:
#   LQ_REPO   LibreQuake's git URL                    (default: its GitHub)
#   LQ_REV    the commit to build                     (default: the pinned one below)
#   LQ_WORK   where the checkout and the tools go     (default: build/librequake)
#   LQ_STAGE  all (default), or one of prepare (source and tools only), wads, progs, maps,
#             assemble: CI runs them as separate jobs
#   LQ_MAPS   with LQ_STAGE=maps: one directory of lq1/maps/src (e1, dm, ...); all when unset
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
LQ_REPO="${LQ_REPO:-https://github.com/lavenderdotpet/LibreQuake.git}"
LQ_REV="${LQ_REV:-dd4d5bffcabbcf9ceca32a1d83829109928c3f08}"
LQ_WORK="${LQ_WORK:-$ROOT/build/librequake}"
OUT="${1:-$LQ_WORK/id1}"

FTEQCC_URL="https://www.fteqcc.org/dl/fteqcc_linux64.zip"
ERICW_URL="https://github.com/ericwa/ericw-tools/releases/download/2.0.0-alpha9/ericw-tools-2.0.0-alpha9-Linux.zip"
QPAKMAN_REPO="https://github.com/LibreQuake/qpakman.git"

mkdir -p "$LQ_WORK"
SRC="$LQ_WORK/src"
TOOLS="$LQ_WORK/tools"

# The source, at the pinned commit.
if [[ ! -d "$SRC/.git" ]]; then
    git init -q "$SRC"
    git -C "$SRC" remote add origin "$LQ_REPO"
fi
git -C "$SRC" fetch -q --depth 1 origin "$LQ_REV"
git -C "$SRC" checkout -q --force FETCH_HEAD
[[ "${LQ_STAGE:-all}" == all || "${LQ_STAGE}" == wads ]] && git -C "$SRC" clean -qfdx

# The tools, as LibreQuake's .github/scripts/install_deps.sh gets them, but under LQ_WORK.
mkdir -p "$TOOLS/bin"
if [[ ! -x "$TOOLS/bin/fteqcc" ]]; then
    curl -sSfL -o "$TOOLS/fteqcc.zip" "$FTEQCC_URL"
    unzip -q -o "$TOOLS/fteqcc.zip" -d "$TOOLS/fteqcc"
    ln -sf "$TOOLS/fteqcc/fteqcc64" "$TOOLS/bin/fteqcc"
    chmod +x "$TOOLS/fteqcc/fteqcc64"
fi
if [[ ! -x "$TOOLS/bin/qbsp" ]]; then
    curl -sSfL -o "$TOOLS/ericw.zip" "$ERICW_URL"
    unzip -q -o "$TOOLS/ericw.zip" -d "$TOOLS/ericw"
    for t in qbsp vis light; do ln -sf "$TOOLS/ericw/$t" "$TOOLS/bin/$t"; done
fi
if [[ ! -x "$TOOLS/bin/qpakman" ]]; then
    rm -rf "$TOOLS/qpakman"
    git clone -q --depth 1 "$QPAKMAN_REPO" "$TOOLS/qpakman"
    cmake -S "$TOOLS/qpakman" -B "$TOOLS/qpakman/build" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$TOOLS/qpakman/build" -j"$(nproc)" >/dev/null
    ln -sf "$TOOLS/qpakman/build/qpakman" "$TOOLS/bin/qpakman"
fi
export PATH="$TOOLS/bin:$PATH"
export LD_LIBRARY_PATH="$TOOLS/ericw${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONUNBUFFERED=1

cd "$SRC"
wads() { echo "== wads"; python3 build.py --compile-wads; }
progs() { echo "== progs"; python3 build.py --compile-progs; }
# One directory of maps, or all of them. compile_maps.py moves every .bsp under src/ when it
# finishes, so two runs in one checkout must not overlap; CI runs directories on separate machines.
maps() {
    if [[ -n "${1:-}" ]]; then
        echo "== maps: $1"
        ( cd lq1/maps && python3 compile_maps.py -d "src/$1" )
    else
        echo "== maps"
        python3 build.py --compile-maps
    fi
}
assemble() {
    echo "== releases"
    ls lq1/maps/*.bsp >/dev/null
    python3 build.py --build
    mkdir -p "$OUT"
    cp -a releases/full/id1/. "$OUT/"
    echo "LibreQuake $LQ_REV" > "$OUT/LIBREQUAKE_VERSION"
    echo "built: $OUT"
    ls -la "$OUT"
}

case "${LQ_STAGE:-all}" in
    all) wads; progs; maps; assemble ;;
    wads) wads ;;
    progs) progs ;;
    maps) maps "${LQ_MAPS:-}" ;;
    assemble) assemble ;;
    prepare) ;;
    *) echo "unknown LQ_STAGE: $LQ_STAGE" >&2; exit 2 ;;
esac
