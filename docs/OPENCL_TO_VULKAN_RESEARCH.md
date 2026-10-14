# Stable Audio: OpenCL to PS5 Vulkan research

Status: OpenCL compatibility was removed from the Vulkan runtime on 2026-10-12. Stable Audio 3
runs through the pinned audio.cpp ggml/Vulkan adapter. A PS5 crash after response delivery was
fixed by removing non-trivial TLS destructors from the pinned C API/model-spec caches; two
randomized full modality cycles then passed on PPSA99023.

## Executive recommendation

The retired OpenCL dependency was not a PS5/Adreno hardware requirement. It came from the
prebuilt Stable Audio runtime: the model package provenance identified
[`a8nova/adreno-llms`](https://github.com/a8nova/adreno-llms), whose project describes
itself as C++/OpenCL inference tuned for Qualcomm Adreno 6xx. The PS5 GPU is AMD RDNA
based and this project reaches it through RADV/Vulkan. The old OpenCL ABI shim, headers,
model preset and static audio archives have now been removed from the active tree/build.

The migration uses the existing ggml ABI rather than translating individual OpenCL calls.
Stable Audio 3 is expressed as ggml graphs and dispatched to Vulkan; the adapter keeps one
selected model/session alive for repeated calls and releases it when modality changes. audio.cpp
is called from short-lived HTTP connection threads, so its thread-local `std::string` error
object and model-spec cache attempted C++ TLS destruction after the response. On PS5 this caused
a SIGSEGV after successful generation/HTTP delivery. The pinned preparation patch changes those
caches to process-static state; ProsperoAI serializes model operations with its inference mutex.

## Current PS5 evidence

- The new preset is `stable-audio-3-small-sfx-vulkan`, a Q8_0 GGUF from the pinned
  `audio-cpp/audio.cpp-gguf` revision. The old Stable Audio Open Small preset and `.cl`
  assets are no longer selected or downloaded.
- On PPSA99023, SA3 generates a non-silent, unclipped four-second 44.1 kHz stereo PCM16 WAV in
  about 19.15 seconds cold and 5.84 seconds warm.
- Before the TLS fix, klog showed SIGSEGV only after the audio response was serialized and sent,
  as the short-lived connection worker exited. The process-static cache workaround fixed this.
- Two complete modality cycles passed with different orders, repeated media requests and text
  round-trips. SD-Turbo writes PNG under `work/image/`; Kokoro writes WAV under `work/voice/`;
  SA3 writes WAV under `work/audio/`.
The detailed historical cycle reports were removed from this PR. Current all-model results
are summarized in the [retained three-round record](performance/2026-10-14/pr27-all-models-3x-final.json).

Historical OpenCL ABI, Adreno kernel and old model timing observations below explain why the
compatibility runtime was replaced; they are not a description of the current build.

The `cpu_kernel_calls` diagnostic only counts fallback paths routed through that counter;
it does not prove that all non-GPU work in the prebuilt runtime is absent. Profile CPU,
GPU, transfer, allocation, and end-to-end time separately before making that claim.

## Candidate approaches

| Approach | OpenCL removal | Performance potential on PS5 | Cost / risk | Recommendation |
| --- | --- | --- | --- | --- |
| Keep current compatibility shim | No | Current known baseline; repeated host staging limits headroom | Low; known behavior | Temporary migration safety net only |
| Translate each `.cl` kernel to GLSL/SPIR-V and keep the old runtime ABI | Could remove OpenCL implementation, but still needs an ABI adapter unless the runtime is also changed | Medium to high if kernels are retuned for PS5, persistent buffers are added, and launches are fused; literal translation may inherit Adreno-specific workgroup choices | Medium-high; API and memory-model rewrite, kernel-by-kernel parity | Useful fallback if model runtime source is obtainable and ggml graph port proves impractical |
| Reimplement model stages on ggml/Vulkan | Yes; no OpenCL runtime/archive required | Highest architectural ceiling: resident tensors, graph scheduling, fewer host round-trips, and shared Vulkan kernels with text/SD | High; T5, DiT, Oobleck VAE/decoder and sampling pipeline need parity work | Preferred target |
| Replace with Stable Audio 3 using `audio.cpp` | Yes, if its runtime is integrated and the old archive/preset are retired | Unknown on PS5; the upstream model is expressed as ggml graphs and exposes a Vulkan backend option, but no PS5/RADV run has been proven | High; different checkpoints and pipeline, integration/API compatibility, and quality/performance validation required | Best concrete prototype candidate; do not silently substitute it for Stable Audio Open Small |

## Proposed fastest-path implementation

1. **Freeze a reproducible baseline.** Record exact model/profile revision, prompt, seed,
   steps, latent length and output hash. Collect cold/warm wall time, stage timings, CPU
   time, Vulkan GPU time, bytes uploaded/read back, allocation count, and peak memory.
   Keep the current WAV/cosine comparison as the parity reference.
2. **Recover/own the runtime source before replacing the archive.** The model bundle
   contains kernels and assets, not the engine source. Establish the exact upstream
   source revision and build recipe for the archive, its license/provenance, and whether
   its code can be obtained. Without source, reimplement the runtime rather than trying
   to binary-patch its OpenCL calls.
3. **Split the model into explicit adapter stages:** T5 prompt encoder; eight-step DiT
   denoising/sampler; Oobleck VAE waveform decoder and final audio formatting. First
   reproduce the current fixed profile and output format. Preserve the current model
   package/weights initially; weight conversion or new presets should be separate,
   validated changes.
4. **Build ggml graphs on the shared pinned ABI.** Keep weight and intermediate tensors
   backend-resident across layers and denoising steps. Avoid per-kernel context creation,
   allocations, FP16→FP32 host expansion, and upload/readback cycles. Read back only data
   required by CPU orchestration or the final waveform. Confirm tensor lifetime and the
   one-model-at-a-time lifecycle with the unified router.
5. **Use Vulkan-native fused ops where profiles justify them.** Likely candidates to
   investigate include attention/softmax, projections, 1D and transposed convolution,
   decoder/ISTFT. These are hypotheses from the packaged kernel set, not proven PS5
   bottlenecks. Prefer ggml graph fusion/custom ops where possible so buffers and
   scheduling remain inside the backend. If writing direct SPIR-V kernels, tune subgroup,
   workgroup, shared-memory tiling, and dispatch sizes against the PS5 RADV device rather
   than copying Adreno's WG=64/texture-cache assumptions.
6. **Keep a temporary dual-path test harness, not a user-facing permanent backend.** Run
   the old shim and new Vulkan graph on the same PS5 model assets and deterministic
   fixtures. Remove the old archive and shim only after correctness, lifecycle and
   performance gates below pass.

## Stable Audio 3 prototype findings

- The candidate is [`0xShug0/audio.cpp`](https://github.com/0xShug0/audio.cpp), pinned for this
  investigation at `886076fc89c09b9902592aa4bfbd12922d4a9e26` (2026-10-12). Its model loader
  identifies Stable Audio 3 separately from Stable Audio Foundation/Open v1, and its CMake
  can select only `stable_audio` while enabling `ENGINE_ENABLE_VULKAN`.
- The `engine_model_stable_audio` target compiled successfully against ProsperoAI's current
  pinned ggml 0.21 with Vulkan enabled. This is source/model-target compatibility evidence;
  it does not prove that the full engine, C ABI, model inference, or PS5 build works.
- The first full-runtime build exposed `ggml_graph_set_n_nodes`, absent from pinned ggml 0.21.
  `tools/prepare-ggml-graph-setter.py` provides a guarded, idempotent backport for an
  experimental source tree under `build/`; it is not part of the production build.
- A model-scoped host experiment compiled `engine_core` and the Stable Audio model target after
  excluding unrelated audio models/modules and optional fast-path calls that use newer ggml
  APIs. The SA3 model code does not itself use the missing interleaved-RoPE op. These exclusions
  were experimental edits under `build/audio-cpp-upstream`, not production changes and not yet
  a maintainable upstream integration.
- The first `audiocpp` C API build reached the Vulkan backend but failed because system
  Vulkan-Hpp 1.3.239 lacks `LayerSettingEXT` / `LayerSettingsCreateInfoEXT`. Reconfiguring the
  experiment to use existing Vulkan-Headers 1.4.365 under `.deps/src/vulkan-headers-spike`
  resolved that host dependency; `libaudiocpp.so.0.1.0` then linked successfully with ggml
  Vulkan enabled. Its host NEEDED list includes `libvulkan.so.1` and `libgomp.so.1`; this is not
  evidence those dependencies or the ELF are suitable for PS5.
- `tools/prepare-audio-cpp-sa3.py` applies the pinned source selection and compatibility edits
  to the disposable checkout under `build/`; two consecutive runs succeeded.
  `tools/prepare-ggml-graph-setter.py` separately applies the guarded ggml backport. A host C API
  experiment loaded the SA3 Q8 GGUF and generated a valid, non-silent WAV on CPU. The PS5 static
  C API plus ggml Vulkan archives and RADV-linked signed eboot also build, but no PS5 inference
  has completed, so this is repeatable prototype integration, not a production adapter.
- The pinned `stable_audio` model spec offers a Medium Q8_0 GGUF package and gated safetensors
  packages for Medium, Small Music and Small SFX. Its GGUF route still declares model config,
  T5Gemma config/tokenizer files and model/T5 tensor resources; it is not just a lone GGUF blob.
  The loader identifies SA3 from the nested pretransform config and reports the shared family
  `stable_audio`, so our catalog needs a distinct preset/variant while the adapter maps both
  `stable_audio` and its GGUF weight package into the same single-model lifecycle.
- `tools/build-audio-cpp-sa3-ps5.sh` now reproducibly builds the selected engine core, Stable
  Audio model, ggml Vulkan backend and static C API archive with the PS5 clang target, libc++
  sysroot and ggml 0.21. A fresh build completed all 548 Ninja steps and produced
  `build/audio-cpp-sa3-ps5-repro/bin/libaudiocpp.a` (244 KiB), `libengine_runtime.a` (14 MiB) and
  `libggml-vulkan.a` (39 MiB). A build-only `isascii` compatibility include was needed for SDK
  libc++ locale headers.
- A host C API registry smoke test exposes `family[0]=stable_audio` and exits successfully. The
  PS5 C API smoke source was linked with the existing RADV/native recipe into a 140 MiB PIE; the
  PS5 native converter accepted it and emitted a 65 MiB `eboot.elf`. Its NEEDED set includes
  `libSceAgc.prx` and `libSceAgcDriver.prx`, because the PS5 RADV winsys imports the platform AGC
  driver; the small stub DSOs only describe those imports to the converter. This is not AGC
  inference, and it confirms the current Vulkan stack is not AGC-platform-free. The signed
  stage-1 registry-only eboot launched on `PPSA99023` and stopped cleanly. A stage-2 run reached
  `audiocpp_model_load` for the Q8 GGUF but did not log completion; klog showed severe low-memory
  pressure (`LOW FMEM`, roughly 1,000 free system pages, over 700 MB swap) and frozen ShellCore
  heartbeat. No Vulkan session or PS5 graph execution was reached. Next gate: recover the console,
  restore the original test eboot, then reduce checkpoint-load memory and verify model loading
  before creating the Vulkan session. Do not
  introduce a second ggml copy into the app or remove the Stable Audio Open Small compatibility
  route until runtime, packaging and modality-switch checks pass. Current Stable Audio Open Small
  weights are not interchangeable with SA3.

## Remaining acceptance gates for broader SA3 release readiness

- Current app build and preset contain no OpenCL compatibility implementation or `.cl` model assets.
- Fixed-seed audio has expected WAV format/length, is non-silent and unclipped, and passes
  a defined numerical/perceptual parity threshold against the current output.
- The repeat/thread-teardown crash is fixed and text → audio → text has passed in randomized
  cycles. Still characterize long-run model residency and run a longer endurance soak.
- Report cold and warm end-to-end/stage timings, CPU/GPU utilization, host/device transfer
  bytes, allocation counts and peak memory on the console.
- Do not claim “faster” from GPU kernel time alone. The end-to-end repeat median must beat
  the current Vulkan baseline of 124.106 s at the same prompt, seed, latent length and
  output duration, with output quality held within the agreed gate.

## Sources

- [Adreno-LLMs upstream](https://github.com/a8nova/adreno-llms): states the implementation
  is C++/OpenCL for Adreno 6xx and documents architecture-specific kernel tuning.
- [ProsperoAI Stable Audio model card](https://huggingface.co/blackbearreloaded/ProsperoAI-Stable-Audio-Open-Small-FP16-PS5): records the Adreno compatibility assets and PS5 packaging/provenance.
- [Stable Audio Open Small model](https://huggingface.co/stabilityai/stable-audio-open-small): source model/checkpoint context.
- [PS5 Vulkan/RADV project](https://github.com/mihawk-99/PS5_Vulkan): documents the PS5 Vulkan driver path and GPU capability validation.
- [Vulkan shared-memory/LDS guide](https://docs.vulkan.org/tutorial/latest/Advanced_Vulkan_Compute/03_Memory_Models/03_shared_memory_lds.html): Vulkan workgroup-local memory and synchronization concepts relevant to compute-kernel tuning.
- [Stable Audio 3 upstream](https://github.com/Stability-AI/stable-audio-3): alternative model-family documentation; its listed hardware paths do not establish PS5 Vulkan support.
- [`audio.cpp` Stable Audio implementation](https://github.com/0xShug0/audio.cpp): ggml-based Stable Audio 3/Foundation loader and Vulkan-enabled CMake path; the pinned revision and PS5 compatibility patch are recorded above.
- Local historical measurements: [`docs/MEDIA_VULKAN_COMPARISON.md`](MEDIA_VULKAN_COMPARISON.md); current adapter: `inference/adapters/media/stable_audio_ps5.cpp`.
