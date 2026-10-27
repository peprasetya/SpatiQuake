#!/bin/sh
# Start SpatiQuake from its folder (as Spatiand installs it, or as unpacked): the engine in bin/,
# the libraries it was built with in lib/, LibreQuake's data in id1/. Arguments go to the engine.
#
# The OpenXR mode is on when an OpenXR runtime is found: inside a Spatiand session Spatiand names
# its own (XR_RUNTIME_JSON), and spatiand-host's catalogue entry does the same. Elsewhere the
# engine is an ordinary flat vkQuake. -xr insists on the OpenXR mode, -noxr refuses it.
here="$(cd "$(dirname "$0")" && pwd)"

# Saves, configs and screenshots go in the user's own folder, not the application's, which an
# update replaces: there the game folder holds links to the data and whatever the game writes.
user="${XDG_DATA_HOME:-$HOME/.local/share}/spatiquake"
mkdir -p "$user/id1"
for f in "$here"/id1/*; do
    name="$(basename "$f")"
    case "$name" in *.cfg | *.sav | *.png) continue ;; esac # what the game writes is the user's own
    [ -e "$user/id1/$name" ] && [ ! -L "$user/id1/$name" ] && continue # the user's own file wins
    ln -sfn "$f" "$user/id1/$name"
done
for link in "$user"/id1/*; do # links to data an update no longer has
    [ -L "$link" ] && [ ! -e "$link" ] && rm -f "$link"
done

export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$here/bin/vkquake" -basedir "$user" "$@"
