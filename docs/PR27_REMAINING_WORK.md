# PR 27 remaining work

This checklist reflects the shared upstream GGML v0.26 build and the PS5 results recorded
on 2026-10-13. Keep the PR open until the items below are either completed and verified or
explicitly removed from scope with the reason recorded here.

## Chatterbox ONNX adapter

The current Chatterbox adapter calls audio.cpp and expects its GGUF model. The proposed
`ipsilondev/chatterbox-multilingual-ONNX-q4` model is four ONNX graphs using Q4
`MatMulNBits`, so changing only the preset would create a selectable model that cannot run.
The pinned Hugging Face revision is `aa4a29a4535badd08b38d55e17112a2bce9a84f0` (MIT).

Pinned files from that revision (downloaded at runtime; the model weights do not belong in Git):

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `onnx/conditional_decoder.onnx` | 225,572,798 | `0a7fae0f1c2a94f02e4a30e15242b9119be7b4b0428306e0dd9038e05328b85e` |
| `onnx/embed_tokens.onnx` | 68,420,479 | `b213e0cf547c2de64146de96781ae8742a130fe54e1f577bc7e5ec6cfb39be06` |
| `onnx/language_model.onnx` | 353,810,438 | `d4f8bde071758d8e869a5b78e16d78d17c55ec594d445781f29e1ebcf39f1865` |
| `onnx/speech_encoder.onnx` | 180,077,492 | `7d32d3d75572c76cb7cd9e0489932fc758e242780c85ecd16bf6a3c2f0744838` |
| `default_voice.wav` | 714,320 | `3ebc531cdaba358a327099c1c4f0448026719957bcf4d8e9868767f227e02f4e` |
| `tokenizer.json` | 71,798 | Pin by the Hugging Face revision above; calculate SHA-256 before adding to the downloader manifest. |
| `tokenizer_config.json` | 244 | Pin by the Hugging Face revision above; calculate SHA-256 before adding to the downloader manifest. |
| `generation_config.json` | 93 | Pin by the Hugging Face revision above; calculate SHA-256 before adding to the downloader manifest. |
| `Cangjie5_TC.json` | 1,920,163 | Pin by the Hugging Face revision above; calculate SHA-256 before adding to the downloader manifest. |

- [ ] Pin an ONNX Runtime release and its source/build inputs in `tools/deps.json`.
- [ ] Add a reproducible reduced static CPU build to the common runtime build in
  `tools/build-vulkan-runtime.sh` and `tools/vulkan-runtime/CMakeLists.txt`; retain the
  existing shared GGML build for llama.cpp, Stable Diffusion, Kokoro and audio.cpp.
- [ ] Confirm that the selected ONNX Runtime release includes CPU `MatMulNBits` kernels and
  can be built and linked with the PS5 FreeBSD toolchain. Do not claim Vulkan acceleration
  for this path until the ONNX Runtime backend itself is connected to RADV and verified.
- [ ] Add a Chatterbox ONNX adapter and register it in `inference/adapters/registry.cpp` and
  `tools/build-vulkan-native.sh`; implement tokenizer, speech encoder, autoregressive language
  model sampling, conditional decoder, output WAV, cancellation/progress, and selected voice.
- [ ] Replace the current Chatterbox entries in `model-tools/presets.json` with the pinned
  ONNX graphs and required tokenizer/config/default voice assets, then regenerate
  `include/model_presets.hpp` with `tools/generate-model-presets.py`.
- [ ] Update `tools/benchmark-media-ps5.py`, adapter documentation and the README benchmark
  table for the new model ID and multilingual CPU baseline.
- [ ] Add host coverage for manifest/adapter routing and run a PS5 build plus two successful
  Chatterbox generations on PPSA99023. Record duration, memory behavior and WAV validation in
  `docs/performance/` before removing the old GGUF path.

Expected tracked files for this work:

```text
tools/deps.json
tools/build-vulkan-runtime.sh
tools/vulkan-runtime/CMakeLists.txt
inference/adapters/media/chatterbox_onnx_ps5.cpp
inference/adapters/registry.cpp
tools/build-vulkan-native.sh
model-tools/presets.json
include/model_presets.hpp                 # generated
tools/benchmark-media-ps5.py
tests/                                      # focused adapter/preset coverage
docs/INFERENCE_ADAPTERS.md
docs/BUILDING.md
README.md
docs/performance/<dated ONNX PS5 result>.json
```

The ONNX Runtime checkout, generated reduced-op config, model downloads, and build products
belong under ignored `build/`; generated user media remains under the console's persistent
`work/` folders. Neither dependency source nor model binaries should be committed.

## Shared build and repository cleanup

- [x] Compose upstream GGML, llama.cpp, Stable Diffusion, Kokoro and audio.cpp in one runtime
  CMake build and link the common GGML archives once in the PS5 app.
- [x] Build audio.cpp's C API as part of that runtime rather than maintaining a second GGML
  build.
- [x] Remove nine unused tracked archives from `vendor/lib/`: the old ggml, llama.cpp and
  Stable Diffusion builds plus `mtmd`, Pocket TTS, UCD and vendor-hash. The production link now
  takes GGML, llama.cpp and Stable Diffusion from the shared build; `mtmd`, Pocket TTS, UCD and
  vendor-hash had no remaining source, Makefile, CI or documentation references.
- [x] Keep `tools/build-audio-cpp-sa3-ps5.sh` as a compatibility entry point; it delegates to
  the shared runtime build.
- [ ] After the ONNX integration, search tracked sources, Makefile targets, CI and docs again;
  remove only build scripts or source assets with no supported entry point or remaining use.
- [ ] Evaluate whether Bonsai's private Prism llama.cpp runtime and the regular upstream llama.cpp
  runtime can share one build without losing PTQ1_0/PQ2_0 support. Until then,
  `tools/build-bonsai-runtime.sh` and `tools/prepare-bonsai-ggml.py` remain required adapter
  inputs, not redundant copies of the common audio/text build.
- [ ] Preserve `tools/build-vulkan-runtime.sh` as the canonical runtime build and
  `tools/build-vulkan-native.sh` as the app/package build. Keep `tools/build-sd-vulkan.sh`,
  `tools/build-vulkan-smoke.sh`, `tools/build-vulkan-release.sh` and
  `tools/build-bonsai-runtime.sh` while their Makefile, CI, docs or user workflows call them.
- [ ] Remove legacy backend artifacts only after `rg` and Git history/reference review confirms
  they are not required by another supported build, test, release or recovery workflow.

## Existing PS5 results to resolve

- [x] Shared upstream GGML/audio.cpp build links for PPSA99023.
- [x] Text regression benchmarks and existing Stable Audio 3, Kokoro and Stable Diffusion
  shared-runtime checks are recorded in `docs/performance/`.
- [ ] Chatterbox's current GGUF adapter still fails on PPSA99023 with a large allocation error;
  the failure is recorded in `docs/performance/2026-10-13/pr27-shared-audio-chatterbox.json`.
  The ONNX replacement must pass before this item is closed.
