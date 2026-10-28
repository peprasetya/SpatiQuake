# Building and running SpatiQuake

SpatiQuake ships as one file, `SpatiQuake-<version>.spatiapp`: the engine, the libraries it needs
and LibreQuake's game data. Spatiand installs it when it is dropped into the room. There are three
ways to get one, from quickest to most work.

## 1. Take the one CI built (quickest, and the way for the Deck)

Every push to `master` builds the package.

1. On GitHub, open the repository's **Actions** tab, then the **SpatiQuake** workflow, then the
   latest run on `master` with a green tick.
2. Under **Artifacts**, download **spatiquake-spatiapp** (you need to be signed in to GitHub). It is
   a zip; inside is `SpatiQuake-<version>.spatiapp`.
3. Install it (see "Installing" below).

Artifacts are kept for 90 days. A tagged release (`v0.1.0`, say) also puts the package on the
**Releases** page for good.

## 2. Build the package yourself, on a Linux PC or on the Deck

Everything happens in a container made from vkQuake's own build image (Ubuntu 22.04, so the result
runs on SteamOS), the same way CI's release job does it. Nothing is installed on the machine apart
from the container image.

You need git, around 10 GB of free disk space, a network connection, and **podman** or **docker**:

* **Steam Deck** (desktop mode, Konsole): SteamOS 3.5 and later come with podman; check with
  `podman --version`. Work in your home folder (`/home/deck`), which is writable.
* **Linux PC**: install podman (`sudo apt install podman`, `sudo dnf install podman`,
  `sudo pacman -S podman`) or use docker if you have it.

Then:

```sh
git clone https://github.com/peprasetya/SpatiQuake.git
cd SpatiQuake
spatiquake/tools/build-spatiapp.sh
```

It ends with the path of the package, `dist/SpatiQuake-<version>.spatiapp`.

The first run is the long one. It builds the image (it fetches the Vulkan SDK and builds SDL3, a
few minutes), then LibreQuake's data from source: the maps are compiled one by one, which takes
something like half an hour on a fast PC and longer on the Deck. Both are kept: the image by
podman or docker, the data in `build/librequake/id1`. After that a run only rebuilds the engine,
in a few minutes. To update: `git pull`, then run the script again.

If you already have LibreQuake's data built (an `id1` folder with `pak0.pak`, inside the
checkout), `ID1=path/to/id1 spatiquake/tools/build-spatiapp.sh` skips building it.

## 3. A developer build of the engine (Linux PC)

This is for working on the engine itself. It builds just `vkquake`, linked against the machine's
own libraries, so it isn't meant to be copied to another machine.

You need meson, ninja, a C compiler, glslang, spirv-tools, SDL2 or SDL3, the Vulkan headers and
loader, the OpenXR loader and headers, and vorbis, mpg123, flac and opusfile. For example:

* **Arch, or SteamOS with a distrobox**: `meson ninja glslang spirv-tools sdl2 vulkan-headers
  vulkan-icd-loader openxr libvorbis mpg123 flac opusfile`
* **Fedora**: `meson ninja-build glslang spirv-tools SDL2-devel vulkan-headers vulkan-loader-devel
  openxr-devel libvorbis-devel libmpg123-devel flac-devel opusfile-devel`
* **Ubuntu 24.04 / Debian**: `meson ninja-build glslang-tools spirv-tools libsdl2-dev
  libvulkan-dev libopenxr-dev libvorbis-dev libmpg123-dev libflac-dev libopusfile-dev`.
  Ubuntu's Vulkan headers and `spirv-opt` are older than the engine wants: build with
  `-Dbuildtype=debugoptimized` (which skips `spirv-opt`), and if the headers are too old, put
  newer ones from https://github.com/KhronosGroup/Vulkan-Headers first in `PKG_CONFIG_PATH`.

```sh
meson setup build && ninja -C build
```

The engine needs the game data beside it: an `id1` folder with LibreQuake's paks, from a package
(unpack any `.spatiapp` with `tar xzf`, and it is `spatiquake/id1`) or from
`spatiquake/tools/build-librequake.sh` (which needs cmake, unzip, libpng and zlib headers). Then:

```sh
build/vkquake -basedir path/to/folder-with-id1
```

## Installing

* **Into Spatiand**: drag the `.spatiapp` from a file manager into the room and press the card
  that asks "Install SpatiQuake ...?". Or, from a terminal: `spatiand --install
  SpatiQuake-<version>.spatiapp`. It is then listed behind the Spatiand bubble.
* **For spatiand-host** (a PC streaming it to the glasses): unpack the package and run
  `spatiquake/add-to-host.sh`.
* **Without Spatiand**: unpack it anywhere (`tar xzf SpatiQuake-<version>.spatiapp`) and run
  `spatiquake/spatiquake.sh`. Without an OpenXR runtime it is flat vkQuake in a window.

Saves and settings live in `~/.local/share/spatiquake`, so installing a new version keeps them.

## Checking it

* **Flat** (no glasses): start it, and the LibreQuake menu comes up.
* **In the glasses**: start it from the Spatiand bubble. It runs in the OpenXR mode when
  Spatiand's OpenXR runtime is there. `-xr` makes it insist on the OpenXR mode, and `-noxr` keeps it
  flat.
* **What it costs** (for `spatiquake/RENDERING.md`): in the glasses, load a map
  (`map lq_e1m1` in the console), stand somewhere typical, and type `xr_bench 600`. The numbers are
  printed on the console and added to `~/.local/share/spatiquake/id1/xr_bench.txt`.
