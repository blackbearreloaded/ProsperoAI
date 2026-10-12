# Inference layers and adapters

The application has one generation entry point, exposed through
[`include/gpt_runtime.hpp`](../include/gpt_runtime.hpp) for compatibility with the native UI
and OpenAI-compatible HTTP API. Model selection and generation pass through
[`inference/router.cpp`](../inference/router.cpp), which serializes inference, releases the
previous model/backend before a switch, and preserves the public API while the internals evolve.

## Source map

| Path | Responsibility |
| --- | --- |
| `include/gpt_runtime.hpp` | Stable application-facing catalog, selection, generation and stats API. |
| `inference/runtime_adapter.hpp` | Typed modality, shared model descriptor, generation request and adapter contract. |
| `inference/router.cpp` | Model catalog aggregation, one-model-at-a-time lifecycle and API compatibility facade. |
| `inference/adapters/registry.cpp` | Adapter registry and request translation for text, image, audio and speech. |
| `inference/adapters/llama/` | Text generation adapter implementation using llama.cpp on Vulkan. |
| `inference/adapters/media/` | PS5 wrappers for Stable Diffusion, Stable Audio and Kokoro. Each wraps its model-specific API and output format. |
| `inference/backends/vulkan/` | Low-level shared ggml/Vulkan bridge used by media kernels; not a model adapter. |
| `vulkan/http_server.cpp`, `vulkan/openai_api.inc` | HTTP/OpenAI protocol and transport. These do not own model-specific inference. |
| `vulkan/storage*`, `vulkan/elevation/`, `vulkan/ui/` | PS5 storage/platform services, privilege boundary and Vulkan UI. |
| `tools/vulkan-runtime/` | CMake composition of llama.cpp/ggml, Stable Diffusion, Kokoro and shared ABI tests. |
| `tools/prepare-*-ps5.py` | Guarded, repeatable PS5-only dependency adjustments applied at build time. |
| `include/model_presets.hpp`, `tools/generate-model-presets.py` | Generated model catalog and its source generator. |

The root `src/` directory remains for application/UI/platform integration that is not an
inference backend. `runtime/` is reserved for the bundled PS5 libc runtime and its integrity
record; inference code must not be placed there.

## Adding support

1. If the new model is supported by an existing adapter, add a validated catalog preset and
   model-specific files/metadata there; do not add a second engine path just for a model.
2. If it requires another engine or modality, add a typed `Modality`, map the catalog's kind to
   it in `inference/router.cpp`, and register its generation and release callbacks in
   `inference/adapters/registry.cpp`. Keep backend-specific headers and conversion inside that
   adapter directory.
3. Keep HTTP and UI callers on the existing generation API. Translate their request once at
   the router/adapter boundary rather than leaking llama.cpp, SD, Stable Audio or Kokoro types
   into those callers.
4. Register the adapter's source in `tools/build-vulkan-native.sh` and add host router tests.
   All adapters must honor the shared model lifecycle: the router holds the inference lock and
   invokes the active adapter's release callback before switching to another modality.
5. Verify output format, memory cleanup and a text round-trip after switching through each
   modality on the PS5. A successful host build alone does not prove the device backend works.

The interface means the app can present one consistent model/generation API; it does not imply
that every GGUF, diffusion checkpoint or audio model is compatible. Compatibility remains a
per-model and per-backend capability that the catalog and adapter must declare honestly.

## Stable Audio 3 Vulkan adapter

`inference/adapters/media/stable_audio_ps5.cpp` calls the pinned `audio.cpp` C API with its
`stable_audio` model family and ggml Vulkan device 0. The catalog offers the Q8_0
`stable-audio-3-small-sfx-vulkan` preset; the old Stable Audio Open Small preset, OpenCL shim,
headers, and prebuilt OpenCL media archives are retired. An existing single-file SA3 GGUF in
the models root is accepted without copying 1.68 GB into a new folder. The bundled SD preset
remains `sd-turbo-fp16`; its Vulkan adapter writes PNG and preview files under `work/image/`.

On PS5 test title `PPSA99023`, the adapter generates a non-silent, unclipped 4-second stereo
PCM16 WAV at 44.1 kHz in about 19.15 seconds cold and 5.84 seconds warm. Two full modality
cycles passed with different orders, repeated requests for each media model, and text round-trips.
The failure was C++ TLS teardown on the short-lived PS5 HTTP worker: audio.cpp's C API error
string and model-spec cache had non-trivial thread-local destructors. The pinned-source
preparation now uses process-static cache objects; all audio.cpp calls/model loads are serialized
by the app inference mutex. See the successful cycle reports and the earlier crash record in
[`docs/performance/2026-10-12/`](performance/2026-10-12/).

The PS5 production link uses the app's RADV link recipe and one shared llama/SD/Kokoro ggml
stack plus the pinned audio.cpp SA3 runtime built by `tools/build-audio-cpp-sa3-ps5.sh`.
Build-time source adjustments are repeatable and confined to `build/` by
`tools/prepare-audio-cpp-sa3.py` and `tools/prepare-ggml-graph-setter.py`.
