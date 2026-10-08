# Native UI

ProsperoAI uses the `ps5-homebrew-ui` renderer and components at revision
`4bd942579dd981b3df9c740438489ca6a614ddc0`. The approved Midnight / Amber design
is implemented in `src/native_ui.cpp`. The console entry point is `src/main.cpp`.
The old RmlUi document renderer and bitmap-font engine are removed from the build.
SDL remains for the existing media decoder, input clock, and system-keyboard helpers.

These are captures of the production frontend with deterministic host fixtures,
not console captures or model-inference results:

![Model library](images/native-models.png)
![Conversation workspace](images/native-workspace.png)
![Daylight settings](images/native-daylight.png)

## Screens and input

- **Workspace:** saved conversations, streamed text, image previews, and saved audio.
  Cross writes a message or sends the current USB-keyboard draft. The system keyboard's
  Send action submits its text; Cancel returns to the app. Square starts a conversation.
  Left focuses the session rail; Right or Circle returns to the composer. Up/Down and
  the existing scroll inputs move through the response. Triangle retries a failed job
  or plays the latest saved audio. A focused session can be deleted with Triangle,
  after a confirmation dialog.
- **Models:** a scrolling, virtualized model grid. Square opens search; a USB keyboard
  can also type a query directly. Triangle cycles capability filters. Cross confirms a
  model change. The selected model and installed model IDs come from the runtime.
- **Settings:** appearance, text-generation defaults, interface volume, reduced motion,
  high contrast, and larger reading text. Circle focuses the categories; Up/Down changes
  category, and Cross/Right enters its controls. Changes apply immediately and save in
  the background.
- L1/R1 change screens. Options opens the close dialog when background work has finished
  and the conversation has been saved.

The frontend uses the framework's spring focus, scrolling, card lift, staggered arrivals,
tab indicator, native dialog transitions, blur, and sound cues. Screen entrances fade
and rise over 420 ms. Procedural artwork uses the native draw list. Reduced motion
disables ambient movement and snaps component motion according to the kit's policy.
Both Midnight and Daylight apply to the entire interface, with Amber, Mist, and Sage accents.

## State and model scalability

`native_app.cpp` owns the application state. One worker serializes model discovery,
model selection/preparation, inference, media decoding, session persistence, and settings
writes. The renderer does not access the runtime or filesystem. Worker results are
published with release/acquire synchronization and consumed on the rendering thread.
Streaming updates are coalesced and copied without blocking the render thread.

Model discovery reads every directory batch and stores descriptors in a vector; the
former eight-model limit is removed. The grid draws visible cards, and search/filter
operate on descriptors rather than hard-coded model names or positions. Saved preferences
and conversations reference stable model IDs. The legacy numeric preference file is
read and migrated to the version-2 key/value format on the next save.

Text, image, audio, and speech capabilities map to the existing runtime adapters. Adding
a compatible installed model does not require a frontend change. Adding a new model
architecture still requires a runtime adapter; a matching filename alone does not make
an unsupported architecture usable. One model job runs at a time. No unmeasured RAM or
VRAM estimates are shown, and selecting a media model is not represented as fully loading
its weights before the runtime does so.

Conversations restore their recorded model. A missing model leaves a conversation readable.
Generation failure preserves the user's message for retry. Failed saves retain the in-memory
conversation and prevent model/session changes; retrying an unsaved response saves it
without generating a duplicate. Conversation storage keeps its existing 64-message ceiling,
reserving a slot for the assistant response. The session rail shows the storage layer's
64 most recently updated sessions. Older sessions are retained on disk. Message storage
now accommodates the runtime's complete 4095-byte response buffer.

## Graphics and media integration

OpenGL is initialized before inference. `agc_lifecycle.cpp` makes the process-wide AGC
initialization shared between the two clients. Link-only AGC import declarations combine
the graphics SDK's exports with the inference wait-command declaration. These stubs are
not included in the app package.

The existing application allocator now implements `realloc` and `malloc_usable_size` for
its fallback allocations, so Mesa does not pass a mapped allocation to the libc heap.
The framework's libc shims and delayed splash release are included. The title exits through
the system service after UI/audio resources have been released.

Generated images decode off the rendering thread. TGA headers, dimensions, and payloads
are checked; previews are downsampled to at most 1024 pixels on their longest side. Texture
uploads happen for visible images, at most one per update. Textures are released when the
conversation changes. Saved audio uses the existing player, while interface cues use the
framework mixer and their own volume setting.

The bundled multilingual bitmap atlas is converted at build time to a coverage `.huifont`.
The four display fonts share this fallback atlas, preserving 17,854 fallback glyphs.
Measurement, wrapping, and rendering use the same resolved face and metrics. Long strings
without spaces wrap at UTF-8 codepoint boundaries. This preserves the existing glyph
coverage; it does not introduce bidirectional layout or complex-script shaping.

## Build and verification

The normal Linux/WSL build remains:

```sh
BUILD_JOBS=6 make app
```

This fetches and verifies the framework's pinned OpenGL SDK, converts the fallback font,
builds the native frontend and existing inference adapters, and assembles the app folder.
The package includes the new fonts and sound cues; obsolete RmlUi assets are not packaged.
Model weights are not included.

Host checks and production-frontend captures:

```sh
python3 -m unittest discover -s tests -p 'test_native_controller.py' -v
python3 tools/host-ui-preview.py
PROSPERO_STRESS=1 python3 tools/host-ui-preview.py build/ui-preview/stress
python3 tools/host-ui-preview.py build/ui-preview/reel --reel
```

The preview requires Clang, Mesa EGL/OpenGL, Python/Pillow, and FFmpeg for a reel. It compiles
the production controller and frontend with deterministic runtime and storage fixtures.
It does not run model inference, connect to a console, or deploy anything. Its images and
reels are host captures, not evidence of console boot or console frame rate.

The checks cover 500 model descriptors, real discovery across multiple directory batches,
serialized runtime ownership, switching, session restore, missing models, persistence,
generation retry, save retry, multilingual layout, bounded image decoding, fallback
allocator resizing, and concurrent AGC initialization.
The host renderer also verifies that reduced-motion library frames remain identical
after two seconds at rest. The 500-model catalog uses 22 draw calls normally and 49
with the blurred dialog in the captured host run; these are not console frame-rate results.

## Upstream changes

The vendored component/renderer subset includes upstream license notices. Local changes
add a fallback face to `gfx::Font`, coverage-atlas rendering, per-glyph texture selection,
and codepoint wrapping for long unbroken text. These are localized under `gfx/`; application
behavior and styling remain outside the vendor directory.

Hardware boot, controller feel, native IME behavior, mixed graphics/inference scheduling,
and sustained frame rate still need a separately authorized console validation run.
