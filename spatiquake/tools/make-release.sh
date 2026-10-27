#!/usr/bin/env bash
# Put SpatiQuake together as a Spatiand application: SpatiQuake-<version>.spatiapp.
#
#   spatiquake/tools/make-release.sh ENGINE ID1_DIR OUT_DIR
#
# ENGINE is linuxdeploy's AppDir (usr/bin/vkquake and the libraries it needs in usr/lib, as
# spatiquake/tools/build-appdir-in-docker.sh makes it on Ubuntu 22.04, so it runs on SteamOS), or
# for a quick local package just the vkquake program. ID1_DIR is LibreQuake's built id1.
#
# The package is a gzipped tar of one folder, spatiquake/, with spatiand-app.toml at its top: the
# engine in bin/, its libraries in lib/, the data in id1/, the launcher, the icon, licences, and
# add-to-host.sh with spatiand-host-app.toml for a machine that streams it with spatiand-host.
# Dropped into the Spatiand room it installs into ~/.local/share/spatiand/apps/spatiquake and is
# listed behind the Spatiand bubble. Unpacked anywhere, spatiquake.sh runs it too.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ENGINE="$(realpath "$1")"
ID1="$(realpath "$2")"
OUT="$(mkdir -p "$3" && realpath "$3")"
VERSION="${VERSION:-$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo dev)}"
[[ -f "$ID1/pak0.pak" ]] || { echo "no pak0.pak in $ID1" >&2; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
APP="$WORK/spatiquake"
mkdir -p "$APP/bin" "$APP/lib" "$APP/licences"

if [[ -d "$ENGINE" ]]; then
    cp "$ENGINE/usr/bin/vkquake" "$APP/bin/"
    [[ -d "$ENGINE/usr/lib" ]] && cp -a "$ENGINE/usr/lib/." "$APP/lib/"
else
    cp "$ENGINE" "$APP/bin/vkquake"
fi
chmod +x "$APP/bin/vkquake"
cp -a "$ID1" "$APP/id1"
cp "$ROOT"/spatiquake/packaging/{spatiquake.sh,add-to-host.sh,spatiand-host-app.toml,spatiquake.png} "$APP/"
sed "s/@VERSION@/$VERSION/" "$ROOT/spatiquake/packaging/spatiand-app.toml.in" > "$APP/spatiand-app.toml"
cp "$ROOT/LICENSE.txt" "$APP/licences/SpatiQuake-GPL-2.0.txt"
# LibreQuake's own licence notes travel with its data (they are in its docs component).
find "$APP/id1" -maxdepth 3 \( -iname 'COPYING*' -o -iname 'README-IMPORTANT-LICENCE-INFO*' -o -iname 'CREDITS*' \) \
    -exec cp {} "$APP/licences/" \;
cat > "$APP/README.txt" <<TXT
SpatiQuake $VERSION -- Quake in the Spatiand room

Install: drop SpatiQuake-$VERSION.spatiapp into the Spatiand room and press the card that asks
whether to install it. It is then behind the Spatiand bubble in the launcher. It may also be
unpacked anywhere and started with ./spatiquake.sh (the room inside a Spatiand session, a flat
window elsewhere). On a machine that streams to the glasses with spatiand-host, ./add-to-host.sh
adds it to the host's applications.

Saves and configs are in ~/.local/share/spatiquake, kept across updates.

The engine is vkQuake with an OpenXR mode (GPL-2.0, licences/SpatiQuake-GPL-2.0.txt; source:
https://github.com/peprasetya/SpatiQuake). The game data is LibreQuake's
(https://github.com/lavenderdotpet/LibreQuake): models, textures and sounds BSD-3, its QuakeC,
progs.dat and pop.lmp GPL-2; see licences/.

In the room: left stick moves, right stick turns, right trigger fires, A jumps, B changes weapon,
Start is the menu. Aim with the gyro (Spatiand's "VR game" controller layout) or the pointer; hold
the left trigger to keep the aim where it is while you look round.
TXT

PACKAGE="$OUT/SpatiQuake-$VERSION.spatiapp"
tar -C "$WORK" -czf "$PACKAGE" spatiquake
echo "$PACKAGE"
