# PR 27 remaining work

This checklist reflects the shared upstream GGML v0.26 build and the PS5 results recorded
on 2026-10-13. Keep the PR open until the items below are either completed and verified or
explicitly removed from scope with the reason recorded here.

## Chatterbox and model switching (2026-10-14)

The upstream `audio.cpp` GGUF Chatterbox adapter passes on PPSA99023, including after text
models (T3 runner buffers cleared, explicit F32 decode attention). Granite, BoHF, Bonsai and
ACE-Step load after other models. Details: [PR27_HANDOFF.md](PR27_HANDOFF.md).

- [ ] Find the ~300 MB of direct memory each ACE-Step request leaves behind.

Completed user-facing work: native DualSense and web recording, native control hints/L3
voice deletion, fixed web voice switching, lazy model preparation on first typing,
keeping chat open after conversation deletion, and shared backend residency indicators.
`ps5ctl.py` now has local env setup and bundled verified service payloads; P5 Manager
is optional.

## Chatterbox ONNX adapter — out of scope

Removed from PR 27 scope on 2026-10-14 at the user's request: the GGUF adapter now works on
PS5 Vulkan, and ONNX Runtime has no PS5 Vulkan execution provider (CPU only). The pinned
ORT build scripts and patch remain in the repository as research; decide separately whether
to keep the embedded ORT object in the app link or remove it.

## Shared build and repository cleanup

- [x] Compose upstream GGML, llama.cpp, Stable Diffusion, Kokoro and audio.cpp in one runtime
  CMake build and link the common GGML archives once in the PS5 app.
- [x] Build audio.cpp's C API as part of that runtime rather than maintaining a second GGML
  build.
- [x] Remove nine unused tracked archives from `vendor/lib/`: the old ggml, llama.cpp and
  Stable Diffusion builds plus `mtmd`, Pocket TTS, UCD and vendor-hash. The production link now
  takes GGML, llama.cpp and Stable Diffusion from the shared build; `mtmd`, Pocket TTS, UCD and
  vendor-hash had no remaining source, Makefile, CI or documentation references.
- [x] Route the `llama-ps5` smoke-title link through `build/vulkan-runtime-upstream-ps5/` and
  make `make llama-ps5` an alias for the shared runtime build. Remove the duplicate
  `tools/build-llama-ps5.sh`; keep the smoke test and its generated model separate from production.
- [x] Keep `tools/build-audio-cpp-sa3-ps5.sh` as a compatibility entry point; it delegates to
  the shared runtime build.
- [ ] Search tracked sources, Makefile targets, CI and docs again;
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
  shared-runtime checks are summarized in the retained final all-model record.
- [x] Chatterbox's GGUF adapter passes on PPSA99023 (allocation, EOS and listener check).
  The final three-round record is retained at
  `docs/performance/2026-10-14/pr27-all-models-3x-final.json`.
