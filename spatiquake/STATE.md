# SpatiQuake: state

What is done, how it was checked, and what is not done.

## How things are checked here

The development container has no GPU (Mesa's lavapipe draws, under Xvfb), no `/dev/dri`, and no
network access to some hosts (LunarG's SDK, ffmpeg.org, fteqcc.org). So:

* **The engine** is built with meson (`-Dbuildtype=debugoptimized`, which skips `spirv-opt`; Vulkan
  headers 1.4.341 from Khronos' `Vulkan-Headers`). CI builds the release way with the LunarG SDK.
* **Spatiand's OpenXR runtime** (`crates/spatiand-openxr` of peprasetya/spatiand, cloned read-only)
  is built and installed with its own `tools/install-openxr.sh`. With no compositor it still runs a
  session (its log: "running without a compositor") and validates every call: swapchain order, frame
  loop order and times, layers, actions, bindings.
* **What each eye and the 2D layer were drawn into** is saved by `xr_screenshot` (three PNGs of one
  frame, the 2D with its alpha): that is exactly the picture the runtime copies out.
* **The composited room** (headless Spatiand) needs a GPU: see "Not done".
* `spatiquake/tools/smoke-test.sh` runs any of this to a screenshot; CI runs it with LibreQuake's
  real data, flat and in the OpenXR mode. CI's pictures and logs are pushed to the `ci-screenshots`
  branch (`run/` and `xr/`), as GitHub's artifact storage is out of this container's reach.
* Notes from the smoke test: `wait` counts frames that pass while a map loads and the client joins
  over the loopback; on CI's runners those ran out before the client had the server's first
  message, so the picture was taken (or not) before the game began, depending on timing. The
  engine has a command for it, `waitforgame` (in `Quake/cmd.c`): the rest of the script is set
  aside until the client has spawned in the map (or no game is being joined), while the command
  buffer goes on running -- the server's part of joining arrives as commands added to it, so a
  script that blocked the buffer would never see the game begin (the first try did just that). In a game `quit` opens the quit menu, which dims the screen (and the OpenXR mode's 2D
  layer): the test disconnects first. A screenshot asked for in a frame that got no image to draw
  into is now kept for the next frame, not dropped.

## Done

### Step 1: vkQuake builds and runs on Linux, in CI

* `.github/workflows/spatiquake.yml`: native meson build on ubuntu-24.04 with the LunarG SDK, then
  the engine on lavapipe under Xvfb with LibreQuake's data, to the menu and into `lq_e1m1`.
  Upstream's Windows workflows are removed (SpatiQuake targets Linux); the macOS one is kept for a
  later Mac build but runs only when started by hand. Upstream's AppImage build (`build-linux.yml`)
  and formatting check stay.
* **Checked** in the container: LibreQuake's main menu on lavapipe, three runs out of three.

### Step 2: LibreQuake's paks

* `spatiquake/tools/build-librequake.sh` (pinned LibreQuake commit, its own `build.py` and the tools
  its CI uses); `.github/workflows/librequake.yml` builds the map directories on separate runners
  and caches the result per script version. Not committed anywhere.
* **Checked in CI** (the first run built wads, progs and seven of eight map directories in about
  12 minutes; e2 took longest). Building the third-party tools in the container was refused by the
  session's permission policy, so local tests use LibreQuake's loose 2D files packed into a test
  pak (`spatiquake/tools/mkpak.py`): menus and console, no maps.

### Step 3: the OpenXR mode (`Quake/xr.c`, `Quake/xr.h`)

* On when the OpenXR loader finds a runtime with `XR_KHR_vulkan_enable2` and a head-mounted system;
  `-xr` insists, `-noxr` refuses. The loader is opened at run time, so a machine without it is flat.
  Built without OpenXR headers (`-Duse_openxr=disabled`) the engine is the flat vkQuake.
* The Vulkan instance and device are made through the runtime (`xrCreateVulkanInstanceKHR`,
  `xrGetVulkanGraphicsDevice2KHR`, `xrCreateVulkanDeviceKHR`), the session with the engine's queue;
  the queue mutex is held for every OpenXR call that may submit.
* Three OpenXR swapchains (left, right, 2D; `B8G8R8A8_UNORM`) stand in for the window's swap
  chain: vkQuake's last pass draws into the current one, which is then turned back to
  `COLOR_ATTACHMENT_OPTIMAL`; nothing is presented to the window. `vid.width/height` are the eye
  size the runtime recommends (1920x1080 from Spatiand), not the window's.
* Each frame: `xrWaitFrame`, `xrBeginFrame`, the eyes located in `LOCAL` space; the world drawn
  once per eye from the eye's pose turned by the body's yaw, `xr_worldscale` (32) Quake units a
  metre, with the eye's own asymmetric field (vkQuake's reversed infinite projection); then the 2D;
  `xrEndFrame` with a projection layer and a quad layer.
* **Checked**: against Spatiand's runtime with no compositor, with `-xr`: session begun, three
  swapchains, frames ended with both layers and no OpenXR call failing; the runtime logs the quad
  as 2.15 x 1.21 m, 1.5 m in front of the head. Flat without a runtime, three runs out of three.
  **Seen in CI** (commit b69d40f, `ci-screenshots` branch, `xr/`): both eyes in `lq_e1m1`, the
  weapon in view and the two eyes' parallax right; the flat engine in the same map (`run/e1m1.png`)
  with its status bar.
* Ray tracing: vkQuake's ray-query shadows (`r_rtshadows`) work in the OpenXR mode; the TLAS is
  built once a frame, for the left eye, and the right eye uses it.

### Step 4: the 2D on a quad layer

* The 2D (menus, console, status bar, centre prints) is drawn alone on a clear picture into the third
  swapchain and shown as a quad in front of the head (`VIEW` space), `xr_hud_distance` (1.5 m) away
  and `xr_hud_size` (0.85) of the eyes' field across, so all of it is in view on the glasses. The
  post-process keeps alpha for it (a push constant), and blending now accumulates coverage in alpha
  (`srcAlphaBlendFactor = ONE`; nothing on the screen reads alpha).
* **Checked**: the 2D picture of the console at the eye size (`xr_screenshot`); in CI, in
  `lq_e1m1`, the 2D picture holds the status bar and the console's notify lines, and is clear
  (alpha 0) everywhere else. An earlier veil over the whole layer (alpha 0.67
  where nothing is drawn) was the quit menu's fade, opened by the test's own `quit`.
* Limits: the scene colour buffer is `A2B10G10R10`, so the layer's alpha has four levels;
  translucent 2D (a half-transparent console) is quantised.
* The 2D crosshair is not drawn in the OpenXR mode; a small cross is drawn in the world where the
  aim meets it (`R_DrawXRAimPoint`), the same size in view at any distance. Seen in CI's left eye in `lq_e1m1`, on the crate ahead.

### Step 5: input through OpenXR actions, aim apart from the view

* Actions bound on `/interaction_profiles/oculus/touch_controller` (how Spatiand presents the
  gamepad) and `khr/simple_controller`: move (left stick), turn (right stick), aim (right hand's
  aim pose), fire (right trigger), aim hold (left trigger), A, B, X, Y, menu, grips, stick clicks.
  Buttons arrive as vkQuake's gamepad keys, so binds and menus work as on a pad; in a menu the left
  stick is the arrows. `xr_bindings 1` (default) puts the OpenXR mode's binds on at start.
* The view is the head turned by the body's yaw. The right stick turns the body (`xr_snapturn`
  degrees, or smooth at `xr_turnspeed` when 0); so does the engine's own turning (mouse, keys).
* The aim (what the server and the weapon use) is the right hand's aim pose (`xr_aim 1`: Spatiand
  points it with the gyro or the pointer, and otherwise where the head points) or the head
  (`xr_aim 0`). **`+aimhold`** (left trigger) keeps it where it is in the world while the head looks
  round: the owner's "aim at one point and look elsewhere". The left stick moves relative to where
  the head looks, turned into the aim's frame for the server. The weapon is held at the head,
  pointing along the aim.
* SDL's own gamepad is ignored while the runtime's controllers are in use (they are the same pad).
* **Checked**: the runtime accepted all 14 Touch and 3 simple-controller bindings and gave the
  application the Touch profile. **Not checked**: anything with a pad in hand.

### Step 7: packaging, as a Spatiand application

The owner wants SpatiQuake to be a Spatiand application -- listed behind the Spatiand bubble, and
installed by dragging it into the room -- not an AppImage.

* **Spatiand side** (peprasetya/spatiand, PR #1, merged into its master at the owner's word):
  applications installed into `~/.local/share/spatiand/apps/<id>/` with a `spatiand-app.toml`,
  listed after Spatiand's own; a `.spatiapp` (that folder as a gzipped tar) dropped onto the room
  is offered on the notice card and installed when the card is pressed; `spatiand --install FILE`
  does the same from a terminal. See that repository's `docs/spatiand-apps.md`.
* **SpatiQuake side**: `spatiquake/tools/make-release.sh ENGINE ID1 OUT` makes
  `SpatiQuake-<version>.spatiapp`: one folder `spatiquake/` with `spatiand-app.toml` (`kind = "vr"`,
  icon, version), the engine in `bin/` and the libraries it was built with in `lib/` (from
  linuxdeploy's AppDir, made by `spatiquake/tools/build-appdir-in-docker.sh` in upstream's
  Ubuntu 22.04 build image, with the OpenXR loader), LibreQuake's `id1/`, `spatiquake.sh` (the
  launcher: saves and configs in `~/.local/share/spatiquake`, where the game folder holds links to
  the data, so an update never touches them), README and licences, and `add-to-host.sh` with
  `spatiand-host-app.toml` for a machine that streams it with spatiand-host. CI's `release` job
  builds it; a `v*` tag attaches it to the GitHub release.
* **Checked**: a package made from the dev build, dropped (a GTK drag source, in Spatiand's
  snapshot backend built from that branch) onto the room: Spatiand offered "Install SpatiQuake
  test?", the press installed it, and the launcher showed SpatiQuake behind the Spatiand bubble with
  its icon. The installed copy started (flat, no runtime) with its saves in the user's folder.
  `spatiand --install` on the same package; the host entry parses with spatiand-stream's own
  `Catalog`.
* **Checked in CI**: the `release` job builds the `.spatiapp` (artifact `spatiquake-spatiapp`).
* **Not checked**: the CI-built package (the AppDir path) on SteamOS.

## Not done

* **Step 6, the composited room, without glasses.** The headless and snapshot Spatiands were built
  from the spatiand repo (FFmpeg 9.0.2 from its GitHub mirror, as ffmpeg.org is blocked here). The
  headless backend needs a render node (`/dev/dri/renderD128`), which this container has not got.
  The snapshot backend runs in software and took SpatiQuake as its client: the runtime connected,
  and Spatiand made it the environment ("an application became the environment"). But the runtime
  could not allocate exportable (dmabuf) memory on lavapipe, so no picture reached the compositor.
  Needed: a machine with a GPU (the Deck itself, in desktop mode, as spatiand's own conformance runs
  are done). `spatiquake/tools/test-with-spatiand.sh SPATIAND_REPO VKQUAKE BASEDIR OUT +map lq_e1m1`
  does the whole run (snapshot backend, `SPATIAND_HMD=null SPATIAND_INPUT=off`, its own empty
  config) and writes `room.png` (what the compositor composed) and the three `eyes-*.png`. Here it
  ran as far as the GPU allows: "an application became the environment", then every frame's export
  failed (and each took about a second, so the eye pictures were not reached).
* Running on the Deck in a Spatiand session, and through `spatiand-host` to the glasses: needs the
  hardware.
* Comfort and feel: world scale, snap angle, the 2D's size and distance, the gun's place, have not
  been judged by a wearer.
* The weapon model is drawn at the head's position with the aim's angles; a hand position for it
  (the right hand's grip pose) would be better when the gyro aims.
* A swapchain re-creation in the middle of a frame (a video restart from a cvar change while in the
  OpenXR mode) is not handled carefully.
* Modern rendering: planned in `spatiquake/RENDERING.md` (measure on the Deck; per-frame work
  done once for both eyes; foveated shading and dynamic resolution with FSR 1; then HDR with
  bloom, ray-traced model shadows and ray-traced bounce light in the lightmaps). Begun: `xr_bench`,
  run in CI; the Deck's numbers are needed from the owner.
