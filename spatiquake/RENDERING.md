# SpatiQuake: modern rendering plan

The owner wants SpatiQuake to look modern ("may be like ray tracing"). The constraint is the
Steam Deck drawing two eyes for the glasses, with Spatiand compositing the room on the same GPU.
So every step here is measured on the Deck before it is kept, and the order puts the steps that
free GPU time ahead of the steps that spend it.

## What there is to spend

* **The Deck's GPU** (Van Gogh): 8 RDNA2 compute units at up to 1.6 GHz (about 1.6 TFLOPS), with
  ray accelerators; 88 GB/s of memory bandwidth shared with the CPU. The Vulkan driver is Mesa's
  RADV, which has `VK_KHR_ray_query`, `VK_KHR_multiview` and `VK_KHR_fragment_shading_rate` on
  RDNA2. (To be confirmed with `vulkaninfo` on the owner's Deck: which shading-rate modes it has,
  and the attachment texel size.)
* **The pictures**: two eyes at the size Spatiand asks for (1920 x 1080 each), about 4.1 million
  pixels a frame, plus the 2D layer, at the display's rate: 16.7 ms a frame at 60 Hz.
  Spatiand's own compositing (the room, reprojection) comes out of the same 16.7 ms.
* **Today's cost**: not known yet. Each eye is drawn as a full vkQuake frame at 1080p with 4x MSAA,
  GTAO (`r_ssao`), order-independent transparency (`r_oit`) and ray-query shadows for dynamic
  lights (`r_rtshadows 2`). Step R0 measures it.

## What vkQuake already has

* **Ray queries** (`r_rtshadows`): the lightmaps are updated on the GPU (`update_lightmap.comp`),
  and dynamic lights cast ray-traced shadows into them. The acceleration structures hold the world
  and its moving parts (BLAS per model, a TLAS a frame). Lightmaps don't depend on the view, so
  this work can serve both eyes; the TLAS is already built once a frame for both.
* **GTAO** ambient occlusion, MSAA, two OIT modes, soft particles, indirect draws, a task system
  on the CPU.
* **A render scale** (`r_width`, `r_height`) with an upscale pass; it is fixed (changing it
  restarts the renderer), and the upscale is bilinear or sharp-nearest.

## The steps

Each step ends with numbers from `xr_bench` on the Deck (`lq_e1m1` and one heavy map, same spot),
and is kept only if it stays inside the budget.

### R0. Measure (begun)

* `xr_bench N` (in the OpenXR mode): over the next N display frames, the GPU time of each picture
  (left eye, right eye, 2D) from timestamp queries, the CPU time of a frame's work, the time
  between frames, and how many frames were late for the display. It prints a summary and appends
  a line with the settings to `xr_bench.txt` in the game folder, so runs can be compared.
* Presets to compare (as `exec` files): today's defaults; MSAA off or 2x; SSAO off; OIT off;
  `r_rtshadows 0`; render scale 0.75.
* **Done so far**: `xr_bench` (`Quake/xr_bench.c`; the GPU times come from the timestamps
  `scr_speeds` uses, each tagged with the picture it is of, as they come back two renders late).
  CI runs it in `lq_e1m1` on every push and keeps its line (`ci-screenshots`, `xr/xr_bench.txt`).
* **On the Deck**: in a Spatiand session with the glasses, start SpatiQuake, load a map
  (`map lq_e1m1`), stand still somewhere typical, and type `xr_bench 600` in the console (ten
  seconds at 60 Hz). The summary is on the console and the line is in
  `~/.local/share/spatiquake/id1/xr_bench.txt`.
* **Seen so far** (lavapipe, so only the shape): the 2D layer costs about as much GPU time as an
  eye. It is drawn through the same frame as an eye (the scene's buffers, MSAA and post-process
  for a picture that holds only the 2D), which R1 can cut out.
* **Done when**: the owner has run it on the Deck in the glasses and the numbers are in this file.

### R1. Don't do per-eye what is per-frame

* Update the lightmaps (and their ray-traced shadows) once a display frame, for the surfaces
  either eye can see, not once per eye. Today the right eye's render repeats much of the left's.
* Cull once for a view that covers both eyes; record what both eyes share once.
* Particles: simulate once a frame (check that the second render doesn't step them again).
* The 2D layer: draw it straight into its swapchain image, without the scene's colour and depth
  buffers, MSAA resolve and post-process that an eye needs.
* **Expected**: the right eye's GPU and CPU time drops well below the left's.

### R2. Shade less where the glasses can't show it

* **Fixed foveated rendering** with `VK_KHR_fragment_shading_rate` (a shading-rate attachment):
  full rate in the middle of each eye, 2x2 towards the edges, where the glasses' optics are soft
  anyway. Per eye, centred on the lens (the eye's asymmetric field tells where that is).
* **Dynamic resolution**: render into the full-size image with a smaller viewport when frames run
  late, and back up when there is room, with no renderer restart; upscale with AMD's FSR 1 (EASU
  for the upscale, RCAS for sharpening; MIT-licensed, plain compute shaders, cheap on RDNA2).
* **Expected**: a large part of the pixel work back, spent in R3.

### R3. The modern look, sized to what R1 and R2 freed

In order of value for the cost on the Deck:

1. **HDR scene and a filmic tonemap, with bloom**: scene colour in 16-bit float (the eyes need no
   alpha; the 2D layer keeps its own), Quake's overbright light kept as light, not clipped, and a
   small bloom from the bright parts (lava, slime, lights, muzzle flash). Cheap, and the most
   visible change.
2. **Ray-traced shadows for models**: monsters and items lit by dynamic lights (muzzle flashes,
   rockets) are shadowed with a ray from the model to the light; today models take the light
   without shadows. One ray per model per light, not per pixel.
3. **Ray-traced bounce light in the lightmaps**: one bounce of light (and sky light) gathered with
   ray queries into the lightmaps, a few rays per texel a frame, converging over a second or so
   and refreshed where lights change. View-independent, so both eyes get it at the cost of one,
   and the cost is spread over frames: global illumination the Deck can carry.
4. **Ray-traced reflections on water** (one ray per pixel on water surfaces only, at reduced rate),
   if R2 freed enough.

Not planned: temporal anti-aliasing (smears with head motion in the glasses), full path tracing
(not at two 1080p eyes on 1.6 TFLOPS).

### R4. One pass for both eyes, if the CPU is what limits

* `VK_KHR_multiview`: both eyes drawn in one render pass into a two-layer image, each draw call
  issued once (`gl_ViewIndex` picks the eye's matrix in the shaders). It halves the draw calls and
  the CPU's recording work; the GPU's pixel work is unchanged. It touches every render pass and
  shader, so it is done only if R0 and R1 show the Deck's CPU is the limit.

## Order and how it is checked

R0, then R1, R2, R3 (1, 2, 3, then 4), and R4 if the numbers call for it. Here, without a GPU,
each step is built, run in CI on lavapipe (which shows the pictures are right, not how fast they
are), and given an `xr_bench` line for the Deck. The owner's runs on the Deck decide what stays.
