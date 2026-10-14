# PR 27 remaining work

This checklist reflects the shared upstream GGML v0.26 build and the PS5 results recorded
on 2026-10-13. Keep the PR open until the items below are either completed and verified or
explicitly removed from scope with the reason recorded here.

## Chatterbox direction (2026-10-14)

Keep the upstream `audio.cpp` GGUF adapter and run it on shared Vulkan device 0. The app's
CPU adapter crashed while reserving a 192–256 MiB CPU arena, and the probed ONNX Runtime path
has no PS5 Vulkan execution provider, so it does not meet the requirement that model inference
use the GPU. The Chatterbox adapter now requests Vulkan explicitly and logs the selected
backend. The PS5 is currently unreachable from the development host, so this backend change
still needs two successful console generations, WAV validation, a memory check, and a text
round-trip after switching back.

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

- [x] Pin ONNX Runtime v1.22.1 and its source/build inputs in `tools/deps.json`; apply the PS5
  source changes through `tools/patches/onnxruntime-1.22.1-ps5.patch`.
- [x] Pin a reproducible PS5 CPU build probe via `tools/build-onnxruntime-ps5.sh`; retain the
  existing shared GGML build for llama.cpp, Stable Diffusion, Kokoro and audio.cpp. Keep this
  probe separate from the common runtime build until ORT is linkable into the title.
- [x] Produce an app-linkable relocatable ORT object from the static component archives with
  `tools/package-onnxruntime-ps5.py`. The PS5 title loader does not load custom shared
  libraries; the DSO remains a build probe only.
- [x] Build the native app with that object, confirm `OrtGetApiBase` remains in the linked
  `llvm-pie.elf`, confirm no ORT DSO appears in `NEEDED`, and produce the signed test app.
- [ ] Launch that app on PS5 and confirm startup. The app currently retains the ORT entry point
  but has no Chatterbox ONNX adapter calling it yet.
- [x] Confirm that the selected release compiles the CPU `MatMulNBits` kernel with the PS5
  FreeBSD toolchain. This is compile evidence only; do not claim Vulkan acceleration or a
  runnable ONNX adapter until ORT is statically linked and verified.
- [ ] Add a Chatterbox ONNX adapter and register it in `inference/adapters/registry.cpp` and
  `tools/build-vulkan-native.sh`; implement tokenizer, speech encoder, autoregressive language
  model sampling, conditional decoder, output WAV, cancellation/progress, and selected voice.
- [ ] Replace the current Chatterbox entries in `model-tools/presets.json` with the pinned
  ONNX graphs and required tokenizer/config/default voice assets, then regenerate
  `include/model_presets.hpp` with `tools/generate-model-presets.py`.
- [ ] Update `tools/benchmark-media-ps5.py`, adapter documentation and the README benchmark
  table for the new model ID and multilingual CPU baseline.
- [ ] Add host coverage for manifest/adapter routing and run two successful Chatterbox
  generations on the PS5 test title. Record duration, memory behavior and WAV validation in
  `docs/performance/` before removing the old GGUF path.

Expected tracked files for this work:

```text
tools/deps.json
Makefile
tools/build-vulkan-runtime.sh
tools/build-onnxruntime-ps5.sh
tools/package-onnxruntime-ps5.py
tools/patches/onnxruntime-1.22.1-ps5.patch
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

The ONNX Runtime checkout, model downloads, and build products belong under ignored `build/`;
generated user media remains under the console's persistent `work/` folders. Neither dependency
source nor model binaries should be committed.

### ONNX Runtime investigation (2026-10-13)

- The existing ignored checkout under `build/onnxruntime-upstream` is ONNX Runtime `v1.22.1`
  (`89746dc19a0a1ae59ebf4b16df9acab8f99f3925`). Its CPU contrib registry includes
  `MatMulNBits`, and `onnxruntime/contrib_ops/cpu/quantization/matmul_nbits.cc` implements
  the kernel. This establishes source-level CPU operator support for that candidate only.
- The existing `build/onnxruntime-ps5-probe/Release` directory initially had only a partial
  CMake cache. A later local-only probe completed all 817 Ninja steps, produced ORT component
  archives containing CPU `matmul_nbits` objects, and linked `libonnxruntime.so.1.22.1` for the
  PS5 target after enabling ORT's shared-library target. The DSO exports `OrtGetApiBase` and
  depends on `libkernel_web.sprx`, `libSceLibcInternal.sprx`, and `libSceNet.sprx`. This does not
  verify that the app loader can load that DSO, runtime instruction safety, session creation, or
  inference on the pinned Chatterbox graphs. Keep the release/build-input pin and adapter work
  open until those checks pass.
- A renewed cross-build initially failed in `sqnbitgemm_kernel_avx2.cpp` because ORT's generic
  FreeBSD configuration requested AVX-VNNI/AVX-512 intrinsics that this PS5 Clang toolchain
  does not expose in that translation unit. The pinned patch below now isolates the PS5 n-bit
  kernel to AVX2 and carries the other platform fixes needed by the clean build.
- The PS5 adjustments are now captured as the pinned, reproducible patch
  `tools/patches/onnxruntime-1.22.1-ps5.patch`, and `tools/build-onnxruntime-ps5.sh` builds the
  pinned checkout into `build/onnxruntime-ps5`. The patch includes the FreeBSD/POSIX compatibility
  changes, 32-byte Protobuf/Eigen TLS alignment, and PS5-only AVX2 dispatch. Its reverse-apply
  check passes against the locally patched checkout. The clean scripted configure and full build
  completed on 2026-10-13, including `matmul_nbits.cc`. This build is a DSO probe only and is
  not included in the common runtime build because the title loader cannot load custom DSOs.
- The PS5-only `sqnbitgemm_kernel_avx2.cpp` now compiles with AVX2/FMA/F16C only; its object
  disassembly contains neither ZMM-register use nor `vpdp` instructions. With that change, the
  probe MLAS archive and full ORT DSO relink completed. This clears the specific unsafe object
  found in the first review, but is not yet a complete ISA audit of every linked ORT object and
  does not establish runtime safety or inference correctness. Keep deployment and regression
  testing gated on a clean scripted build, full DSO audit, app integration, and console checks.
- The component archives and generated ORT API source also combine with PS5 `ld.lld-18 -r`
  into a relocatable object (`libonnxruntime-ps5.o`), currently about 69 MB before final app
  section garbage collection. The packager localizes ORT implementation symbols and retains
  only the public C API entry points, preventing its C++ and TLS helpers from binding across
  runtime boundaries. The app link uses `--no-dynamic-linker` for the ORT weak TLS helper and retains `OrtGetApiBase`; its linked PIE is 212 MB and the signed
  test `eboot.bin` is 125 MB. No ORT DSO is listed in `NEEDED`. The console has not yet booted
  this build, and no adapter calls ORT yet.
- A separate local v1.31.0 source checkout (`402979ac1bef89dcd55add41eaa7277c407599eb`)
  contains the same AVX2/VNNI helper references. The v1.22.1 probe advanced past MLAS after
  temporary compile adjustments, then exposed more PS5 platform gaps: ORT's logging and thread
  pool code assumes FreeBSD/Linux headers and affinity APIs, and Protobuf's 64-byte-aligned
  thread-local cache exceeds the PS5 toolchain's 32-byte TLS alignment limit. The probe only
  completed after applying a pthread TLS fallback, reducing Eigen thread-cache alignment on
  FreeBSD, skipping unsupported CPU affinity/core queries, and making build-tree-only fixes to
  platform/ISA sources. The first disassembly review found AVX-VNNI and ZMM instructions in the
  AVX2 n-bit kernel object because the temporary compiler flags enabled those ISA extensions
  for the whole translation unit. CPU feature dispatch may guard some explicit VNNI kernels,
  but the AVX-512 register copies in generated helper/lambda code have no demonstrated guard.
  That was the first, unsafe probe DSO. The current clean scripted DSO has the AVX2-only n-bit
  fix. A broad disassembly finds VNNI and ZMM instructions in explicitly named MLAS feature
  kernels; MLAS source selects those paths after CPUID and, for AVX512, XCR0 checks. The PS5-only
  n-bit VNNI dispatch aliases baseline AVX2. The console feature bits and app DSO loading have
  not yet been verified, so do not deploy the current DSO or claim runtime safety yet.
- `make test` on 2026-10-13 ran 33 host integration tests, with 1 skipped and 10 errors. Several
  harnesses could not link because the host lacks LLVM 14 AddressSanitizer runtimes; the HTTP API
  harness also lacked `gpt_runtime_prepare`, the session-store test compile lacked the
  `std::filesystem` feature setup. The Vulkan router harness lacked the Chatterbox/storage stubs
  and had stale Kokoro and Mistral signatures/IDs; those are corrected and the targeted router
  test now passes in both build modes. Rerun the full host suite after fixing the remaining
  toolchain/harness errors before treating it as green.

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
