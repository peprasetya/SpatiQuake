#!/bin/bash
# Inside upstream's AppImage build image (Packaging/AppImage/docker, Ubuntu 22.04): build the
# engine and gather it with the libraries it needs into an AppDir, without making an AppImage.
# SpatiQuake ships that folder in its .spatiapp (spatiquake/tools/make-release.sh).
#
#   docker run --rm -v "$PWD:/usr/src/vkQuake" build-vkquake /usr/src/vkQuake/spatiquake/tools/build-appdir-in-docker.sh
set -euo pipefail
cd /usr/src/vkQuake
rm -rf build/appdir build/AppDir
python3 /opt/meson/meson.py setup build/appdir -Ddebug=true -Dstrip=false -Dmp3_lib=mad
ninja -C build/appdir
cd Packaging/AppImage
# The OpenXR loader is opened at run time (the OpenXR mode), so linuxdeploy would not see it.
NO_STRIP=1 ./linuxdeploy-x86_64.AppImage --appimage-extract-and-run \
    -e ../../build/appdir/vkquake --appdir=../../build/AppDir -d ../../Misc/vkquake.desktop \
    -i ../../Misc/vkQuake_256.png --icon-filename=vkquake \
    -l /usr/lib/x86_64-linux-gnu/libopenxr_loader.so.1
