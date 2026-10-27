#!/bin/sh
# Start SpatiQuake from its release folder: the engine (an AppImage) with the LibreQuake data
# beside it. Arguments go to the engine.
#
# The OpenXR mode is on when an OpenXR runtime is found -- inside a Spatiand session the session
# names Spatiand's (XR_RUNTIME_JSON), and spatiand-host's catalogue entry does the same -- and the
# engine is an ordinary flat vkQuake otherwise. -xr insists on the OpenXR mode, -noxr refuses it.
here="$(cd "$(dirname "$0")" && pwd)"
engine="$(ls "$here"/vkQuake-*.AppImage 2>/dev/null | head -1)"
[ -n "$engine" ] || { echo "no vkQuake-*.AppImage beside $0" >&2; exit 1; }
# The user's saves and configs go in the user's own folder, not the release's.
data="${XDG_DATA_HOME:-$HOME/.local/share}/spatiquake"
mkdir -p "$data"
[ -e "$data/id1" ] || ln -s "$here/id1" "$data/id1"
# An AppImage needs FUSE; without it (some containers), it can unpack and run instead.
command -v fusermount >/dev/null 2>&1 || command -v fusermount3 >/dev/null 2>&1 || export APPIMAGE_EXTRACT_AND_RUN=1
exec "$engine" -basedir "$data" "$@"
