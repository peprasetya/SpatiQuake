#!/usr/bin/env bash
# Build SpatiQuake from source into SpatiQuake-<version>.spatiapp, on any x86-64 Linux machine
# with podman or docker: a Linux PC, or the Steam Deck in desktop mode. See spatiquake/BUILDING.md.
#
#   spatiquake/tools/build-spatiapp.sh            -> dist/SpatiQuake-<version>.spatiapp
#
# Everything is built inside a container made from upstream's build image (Ubuntu 22.04, so the
# engine runs on SteamOS too), the same way CI's release job builds it: the engine and the
# libraries it needs, LibreQuake's game data from its source, then the package. Nothing is
# installed on the machine itself. The first run takes a while (the image, then LibreQuake's maps:
# tens of minutes); the image and the data are kept and reused, so later runs only rebuild the
# engine.
#
# Environment:
#   ID1        an id1 folder already built (LibreQuake's paks): skips building the data
#   ENGINE     podman or docker (default: whichever is found, podman first)
#   JOBS       parallel jobs for the build (default: all cores)
#   BASE_IMAGE an image of Packaging/AppImage/docker already built (CI's, from its cache): not
#              built again
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

ENGINE="${ENGINE:-$(command -v podman || command -v docker || true)}"
[[ -n "$ENGINE" ]] || {
    echo "needs podman or docker (on the Steam Deck, podman comes with SteamOS 3.5 and later)" >&2
    exit 1
}
ENGINE="$(basename "$ENGINE")"
IMAGE=spatiquake-build

# The image: upstream's build image, and what LibreQuake's build needs on top.
echo "== the build image ($ENGINE)"
BASE="${BASE_IMAGE:-spatiquake-build-base}"
[[ -n "${BASE_IMAGE:-}" ]] || "$ENGINE" build -t "$BASE" Packaging/AppImage/docker
"$ENGINE" build -t "$IMAGE" - <<EOF
FROM $BASE
RUN apt-get update \
 && apt-get install -y git curl unzip python3 libpng-dev \
 && rm -rf /var/lib/apt/lists/*
EOF

# The data: given, or built once (kept in build/librequake, keyed by the script that builds it).
if [[ -n "${ID1:-}" ]]; then
    ID1="$(realpath "$ID1")"
    [[ -f "$ID1/pak0.pak" ]] || { echo "no pak0.pak in $ID1" >&2; exit 1; }
    case "$ID1" in "$ROOT"/*) ;; *) echo "ID1 must be inside $ROOT (the container sees only it)" >&2; exit 1 ;; esac
    ID1_IN="/src/${ID1#"$ROOT"/}"
else
    ID1_IN=/src/build/librequake/id1
fi

# Inside: as the caller's user with docker (podman's rootless containers map root to the caller
# already), so what is built belongs to them.
# On SELinux machines (Fedora) podman needs the checkout relabelled for the container to use it.
USER_ARGS=()
VOLUME="$ROOT:/src"
if [[ "$ENGINE" == docker ]]; then
    USER_ARGS=(--user "$(id -u):$(id -g)" -e HOME=/tmp)
else
    VOLUME="$VOLUME:Z"
fi

echo "== building"
"$ENGINE" run --rm "${USER_ARGS[@]}" -v "$VOLUME" -w /src -e ID1_IN="$ID1_IN" -e JOBS="${JOBS:-}" \
    -e VERSION="$(git describe --tags --always 2>/dev/null || echo dev)" "$IMAGE" bash -euo pipefail -c '
    stamp=build/librequake/id1/.built-by
    want="$(sha256sum spatiquake/tools/build-librequake.sh | cut -c1-16)"
    if [[ "$ID1_IN" == /src/build/librequake/id1 && "$(cat "$stamp" 2>/dev/null)" != "$want" ]]; then
        echo "== LibreQuake (its maps take the longest)"
        rm -rf build/librequake/id1
        spatiquake/tools/build-librequake.sh
        echo "$want" > "$stamp"
    fi

    echo "== the engine"
    rm -rf build/appdir build/AppDir
    python3 /opt/meson/meson.py setup build/appdir -Ddebug=true -Dstrip=false -Dmp3_lib=mad
    ninja -C build/appdir ${JOBS:+-j "$JOBS"}
    # The OpenXR loader is opened at run time (the OpenXR mode), so linuxdeploy would not see it.
    ( cd Packaging/AppImage && NO_STRIP=1 ./linuxdeploy-x86_64.AppImage --appimage-extract-and-run \
        -e ../../build/appdir/vkquake --appdir=../../build/AppDir -d ../../Misc/vkquake.desktop \
        -i ../../Misc/vkQuake_256.png --icon-filename=vkquake \
        -l /usr/lib/x86_64-linux-gnu/libopenxr_loader.so.1 >/dev/null )

    echo "== the package"
    spatiquake/tools/make-release.sh build/AppDir "$ID1_IN" dist
'
echo
ls -l dist/SpatiQuake-*.spatiapp
cat <<EOF

To install it into Spatiand: drag the .spatiapp into the room and press the card that asks, or
  spatiand --install $(ls -t dist/SpatiQuake-*.spatiapp | head -1)
Without Spatiand, unpack it anywhere and run spatiquake/spatiquake.sh.
EOF
