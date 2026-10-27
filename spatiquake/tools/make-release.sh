#!/usr/bin/env bash
# Put a SpatiQuake release together: the engine's AppImage, LibreQuake's data, the launcher, the
# catalogue entry for spatiand-host, and the licences.
#
#   spatiquake/tools/make-release.sh APPIMAGE ID1_DIR OUT_DIR
#
# Makes OUT_DIR/SpatiQuake-<version>/ and OUT_DIR/SpatiQuake-<version>-linux-x86_64.tar.gz.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APPIMAGE="$(realpath "$1")"
ID1="$(realpath "$2")"
OUT="$3"
VERSION="${VERSION:-$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo dev)}"
NAME="SpatiQuake-$VERSION"
DEST="$OUT/$NAME"
[[ -f "$ID1/pak0.pak" ]] || { echo "no pak0.pak in $ID1" >&2; exit 1; }

rm -rf "$DEST"
mkdir -p "$DEST/licences"
cp "$APPIMAGE" "$DEST/vkQuake-$VERSION.AppImage"
chmod +x "$DEST/vkQuake-$VERSION.AppImage"
cp -a "$ID1" "$DEST/id1"
cp "$ROOT"/spatiquake/packaging/{spatiquake.sh,add-to-host.sh,spatiand-host-app.toml,spatiquake.png} "$DEST/"
cp "$ROOT/LICENSE.txt" "$DEST/licences/SpatiQuake-GPL-2.0.txt"
# LibreQuake's own licence notes travel with its data (they are in its docs component).
find "$DEST/id1" -maxdepth 3 \( -iname 'COPYING*' -o -iname 'README-IMPORTANT-LICENCE-INFO*' -o -iname 'CREDITS*' \) \
    -exec cp {} "$DEST/licences/" \;
cat > "$DEST/README.txt" <<TXT
SpatiQuake $VERSION -- Quake in the Spatiand room

  ./spatiquake.sh          start it (the room inside a Spatiand session; a flat window elsewhere)
  ./add-to-host.sh         add it to spatiand-host's catalogue on this machine

The engine is vkQuake with an OpenXR mode (GPL-2.0, licences/SpatiQuake-GPL-2.0.txt; source:
https://github.com/peprasetya/SpatiQuake). The game data is LibreQuake's
(https://github.com/lavenderdotpet/LibreQuake): models, textures and sounds BSD-3, its QuakeC,
progs.dat and pop.lmp GPL-2; see licences/.

In the OpenXR mode: left stick moves, right stick turns, right trigger fires, A jumps, B changes
weapon, Start is the menu. Aim with the gyro (Spatiand's "VR game" controller layout) or the
pointer; hold the left trigger to keep the aim where it is while you look round.
TXT
tar -C "$OUT" -czf "$OUT/$NAME-linux-x86_64.tar.gz" "$NAME"
echo "$OUT/$NAME-linux-x86_64.tar.gz"
