#!/bin/sh
# Add SpatiQuake to this machine's spatiand-host catalogue, as the room (kind = "vr"), with the
# paths of this release folder. Spatiand's OpenXR runtime must be installed for this user
# (tools/install-openxr.sh in the spatiand repository). Run it from the unpacked release.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
runtime="${XR_RUNTIME_JSON:-$HOME/.local/share/spatiand-openxr/spatiand_openxr.json}"
[ -f "$runtime" ] || echo "warning: no OpenXR runtime at $runtime yet (tools/install-openxr.sh in spatiand)" >&2
catalogue="${XDG_CONFIG_HOME:-$HOME/.config}/spatiand-host/apps.toml"
mkdir -p "$(dirname "$catalogue")"
touch "$catalogue"
if grep -q '^id = "spatiquake"' "$catalogue"; then
    echo "already in $catalogue"
    exit 0
fi
sed -e "s|RELEASE|$here|g" -e "s|HOME/.local/share/spatiand-openxr/spatiand_openxr.json|$runtime|" \
    "$here/spatiand-host-app.toml" | grep -v '^#' >> "$catalogue"
echo "added to $catalogue"
