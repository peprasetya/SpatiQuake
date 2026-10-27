# SpatiQuake

Quake in the Spatiand room: [vkQuake](https://github.com/Novum/vkQuake) (GPL-2.0, its history is
this repository's, kept mergeable as the `upstream` remote) with
[LibreQuake](https://github.com/lavenderdotpet/LibreQuake)'s free game data, drawn in stereo for
the glasses -- on the Steam Deck itself, or on another machine through `spatiand-host` and
streamed to them.

## How it reaches the room: OpenXR, through Spatiand's own runtime

Spatiand has an OpenXR runtime (`crates/spatiand-openxr` in
[peprasetya/spatiand](https://github.com/peprasetya/spatiand), documented in its
`docs/openxr.md`). A game finds it through the ordinary OpenXR loader; its two eye views become the
room. The same library serves both places SpatiQuake is to run, so **SpatiQuake is vkQuake with an
OpenXR mode** and nothing Spatiand-specific in the engine:

| Where | How |
|---|---|
| Steam Deck | The runtime is installed for the user (`tools/install-openxr.sh` in spatiand) and is found from inside a Spatiand session (`XR_RUNTIME_JSON`). |
| Another machine | A `kind = "vr"` entry in `spatiand-host`'s catalogue with `XR_RUNTIME_JSON` in its `env`; the host hands the game the headset's head and eye size (`render_size`) and streams the `projection` window to the glasses (`docs/remote.md`). |
| Anywhere else | vkQuake as it is: a flat window (no OpenXR runtime found). |

What the runtime gives, and what to design for (from `docs/openxr.md`):

* `XR_KHR_vulkan_enable2` (and `enable`): the runtime makes the instance and device, the game renders
  into OpenXR swapchain images; one projection layer is the room, quad layers are drawn over it
  (menus, the HUD, the console).
* The glasses have about 40 degrees across, not a headset's 100: the menu, HUD and console belong on
  a quad layer in front of the head, sized to fit, not at the edges of the eye view.
* Rotation is corrected by the compositor (`set_frame_pose`), translation is not: render from the
  head pose for the predicted display time.
* No tracked hands: the gamepad (Touch layout) gives the sticks and buttons; the wearer's pointer aims
  the hands; the gyro can aim the right hand (the *VR game* controller template). Aim the weapon by
  the head first (or the right hand where it is given), move by the left stick, turn by the right
  (snap and smooth).
* No multisampled swapchains: resolve MSAA into the swapchain image.

## The data: LibreQuake

Models, textures and sounds BSD-3; QuakeC, `progs.dat` and `pop.lmp` GPL-2 (its
`docs/README-IMPORTANT-LICENCE-INFO`). Built from its source with its own `build.py` into
`id1/pak0.pak` and `pak1.pak`, by a script here and in CI, not committed. id Software's own data,
where the wearer has it, works the same.

## Steps

1. Build vkQuake as it is on Linux (meson) in CI; build LibreQuake's paks; run it with them.
2. An OpenXR mode (`-xr`, or on when a runtime is found): instance and session through the runtime,
   swapchains per eye, the frame loop (`xrWaitFrame`/`xrBeginFrame`/`xrEndFrame`), the world rendered
   once per eye from the head's pose (vkQuake's renderer drawn into each eye's image), and a world
   scale of Quake units to metres.
3. The 2D on a quad layer: menu, console, HUD, centre prints.
4. Input through OpenXR actions (Touch profile): move, turn, jump, fire, weapon change, menu.
5. Running on the Deck in a Spatiand session (the headless Spatiand with `SPATIAND_HMD=null` can test
   it without glasses, as spatiand's OpenXR conformance runs do), and through `spatiand-host`.
6. Packaging: a Deck release with the paks, and a catalogue entry for hosts.

## Aim apart from the view (the owner's request)

The camera follows the head; the weapon need not. As with a rifle in the hand, the wearer can keep the
aim on one point and look somewhere else for a moment. In the OpenXR mode:

* The view is the body's yaw (turned by the right stick) plus the head's pose.
* The aim is the right hand's *aim* pose where the runtime gives one that is in use (the gyro's
  *VR right hand*, or the pointer); otherwise the head.
* **Aim hold**: while a button is held, the aim stays fixed in the world at the direction it had when the
  button went down, and the head looks around freely; let go and it follows again.

This is all standard OpenXR (an action pose and a boolean). Something Spatiand-specific is only for what
OpenXR cannot say, and nothing here needs it.
