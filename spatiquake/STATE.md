# SpatiQuake: state

What is done, how it was checked, and what is not done. Newest first within each section.

## Done

### Step 1: vkQuake builds and runs on Linux, in CI

* `meson setup build && ninja -C build` builds vkQuake as it is (SDL2 here; SDL3 when present).
  vkQuake wants Vulkan headers >= 1.4.341 and, for a release build, a `spirv-opt` with
  `--canonicalize-ids`; Ubuntu 24.04 has neither. CI (`.github/workflows/spatiquake.yml`) takes them
  from the LunarG SDK, as upstream's AppImage image does. In the development container the SDK host is
  not reachable, so there the headers are Khronos' `Vulkan-Headers` v1.4.341 with a `vulkan.pc` of our
  own, and the build is `-Dbuildtype=debugoptimized` (which skips spirv-opt).
* `spatiquake/tools/smoke-test.sh VKQUAKE BASEDIR OUT.png [ARGS]` starts the engine (an Xvfb of its own
  if there is no display; Mesa's lavapipe if there is no GPU), draws 60 frames, takes vkQuake's own
  screenshot and quits; it fails without a screenshot or on a `QUAKE ERROR`/`Host_Error`.
* **Checked** in the container: the engine reached LibreQuake's main menu (its own menu art) on
  lavapipe under Xvfb, three runs out of three.

### Step 2: LibreQuake's paks

* `spatiquake/tools/build-librequake.sh [OUT]` fetches LibreQuake at a pinned commit, the tools its own
  CI uses (fteqcc, ericw-tools 2.0.0-alpha9, qpakman), and runs its `build.py`: wads, progs, maps,
  then its `full` release, copied to `OUT` (`build/librequake/id1`). `LQ_STAGE` runs one stage, so CI
  (`.github/workflows/librequake.yml`) compiles the map directories on separate machines as
  LibreQuake's own CI does, and caches the result per script version (the script pins the commit).
* `spatiquake/tools/mkpak.py` writes a .pak from a directory; it is for test data only.
* **Not checked in the container**: building third-party tools there was refused by the session's
  permission policy, so the local tests use LibreQuake's loose 2D files (gfx.wad, gfx/, configs)
  packed into a test `pak0.pak`. That reaches the menu and console, not a map. The full data is built
  and run into a map in CI.

## Not done

* Steps 3 to 7 of the brief: the OpenXR mode, the 2D on a quad layer, OpenXR input (with aim held
  apart from the view, see PLAN.md), testing against a headless Spatiand, packaging.
