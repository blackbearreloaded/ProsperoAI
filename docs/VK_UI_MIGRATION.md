# Native UI Vulkan migration

Keep upstream's layout, colors, fonts, animations, backdrops and glass effects.
Port only rendering and presentation. The default AGC/OpenGL app remains usable
while the Vulkan variant is developed in the existing test slots.

## Stages

1. Independent Vulkan display probe: VK_KHR_display, graphics queue, swapchain,
   color-attachment render pass, presentation and synchronization. Prove it on PS5
   before integrating UI resources.
2. Render the existing draw-list geometry and font atlases with Vulkan pipelines;
   compare against upstream's OpenGL output. Move texture lifetime and screenshots
   behind backend interfaces, eliminating direct GL calls from application code.
3. Port procedural backdrop shaders, offscreen canvases and glass blur. Retain
   upstream composition order, texture sampling, alpha blending and clipping.
4. Combine presentation with llama.cpp Vulkan generation. Establish ownership of
   devices/queues, resource lifetimes and synchronization; do not assume different
   Vulkan instances automatically share resources or queues. Verify interactive
   frames during loading and generation, memory headroom and sustained runs.
5. Wire the native application to the Vulkan renderer and validate all screens,
   controller/IME, image textures, resize/display modes and screenshot output.

## First-stage build

```bash
UI_PROBE=1 bash tools/build-vulkan-smoke.sh
```

Output: `build/vulkan-ui-probe/PPSA99019.ffpfsc`. No model, EGL or OpenGL is
included. The probe draws a dark clear background and a rectangular attachment
clear, and presents 120 frames, then holds the last frame. This diagnostic scene
is not a product redesign. Results go to
`/mnt/sandbox/PPSA99019_000/download0/prospero-vulkan-ui.txt` while running.
The first probe uses a deliberately serialized queue and fixed display extent;
it does not yet implement UI geometry, text, display recreation or concurrent
inference. Current evidence belongs in VK_ACTIVE.md and VK_LOG.md.


## Current implementation

The current native variant compiles the pinned upstream native app and UI-kit
sources unchanged. `vulkan/ui/` implements the narrow GL-shaped draw API with
Vulkan images, buffers, render passes, pipelines, SPIR-V shaders and swapchain
presentation; no OpenGL or EGL library is linked. Shader source is generated from
the pinned upstream GLSL. The same layout, glass, clipping, atlas sampling and
backdrop effects are retained. `tools/host-ui-preview.py --vulkan` renders the
actual native screens using lavapipe for host comparison. Pixel comparison across
21 screens was within a mean absolute channel difference of 0.56/255; Vulkan
validation passed, including stable reduced-motion frames.

`assets/web/` serves a responsive browser UI using the same design tokens,
Montserrat and Inter typefaces, torus artwork, model list, settings, conversation
history and streaming chat API. The API key stays in session storage. Static UI
assets are whitelisted and public; generation and model APIs retain bearer-key
authorization.

Both runtimes look for their respective formats in
`/data/homebrew/prosperoai/models`. A nullfs mount helper exposes that directory
inside the active test-title sandbox; it finds the current sandbox suffix, mounts
only the model directory, and removes the empty mount directories on close. Build
it with `bash tools/build-model-mount.sh`; the default test target is PPSA99023.

The PS5 test title previously booted and rendered the full native Vulkan UI,
loaded the shared Mistral GGUF and generated a response. A later browser request
with decimal temperature exposed a PS5 `strtod` abort on HTTP worker threads. A
local decimal parser is now included in the build. The console stopped answering
on its HTTP, FTP and control ports after that crash. The final parser fix is built
locally but its console run is pending console recovery. Do not claim live web
generation after that change until it has been rechecked on hardware. PPSA99004
was not touched.
