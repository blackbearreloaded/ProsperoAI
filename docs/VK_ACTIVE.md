# Active work

Updated: 2026-10-12. Gates: [VK_PLAN.md](VK_PLAN.md); evidence: [VK_LOG.md](VK_LOG.md).

## Verified vulkan-runtime

- One complete Prism llama/ggml pin `e8fd209`, `GGML_MAX_NAME=160`, source-built SD and
  Vulkan media bridge. AGC inference, routing, scratch allocation and private ggml copies
  are removed. RADV still requires the console AGC platform driver.
- The signed `PPSA99004` release candidate boots with the expected ELF foundation. The
  console was not rebooted.
- Fresh post-update Bonsai verification passed: the cold correctness prompt, three
  identical warm runs at 11.205240 tokens/s median, Thinking mode, 16K context,
  Mistral/MiniCPM5 regression and return to Bonsai.
- Fresh media verification passed with valid artifacts and a text round trip after every
  model: SD-Turbo 30.324 s, Stable Audio 148.888 s and Kokoro 89.014 s. Audio has the
  expected sample rates, frame counts and no clipping.
- Shared web/native conversation create, read and delete passed against the console HTTP
  server. Host self-update integration passes outside the managed sandbox; its Unix socket
  transport is blocked by the sandbox itself.
- Long-generation plumbing now accepts up to 131072 output tokens and 262144 context tokens.
  A full 32712-token MiniCPM5 run reached the 32K context boundary with `length`; a 0.8B
  Qwen3.5 smoke run loaded and generated successfully at 256K context. A full 128K output
  run is not yet claimed because that model emitted EOS after one token.
- An omitted output-length request now fills the remaining context (verified 8192- and
  32768-token full fills on `PPSA99023`) instead of a fixed 512-token default; native
  Response length gained a matching "Auto" default. The downloader's fit badge now reads
  the Vulkan backend's real reported heap instead of a hardcoded figure. The web UI can now
  view/play/download generated images and audio inline (new authenticated `/download0/<path>`
  route plus client-side TGA decode), matching what the native app already did. PR #22.

## Decode performance tuning (branch `perf/ps5-vulkan-profiling`)

- GPU timestamps now reach klog, which showed Q4_0 `MUL_MAT_VEC` (m=14336,k=4096) and the
  fused `MUL_MAT_ADD` as the Mistral 7B decode hotspots, about 15.8 ms GPU time per token.
- Two PS5-only mat-vec patches in `tools/prepare-llama-ps5-io.py` are committed and verified
  on the console: four standard-quant rows per workgroup, and the large hybrid-reduction
  workgroup for narrow projections. Mistral 7B Q4_0 decode went from `62.93` to `66.62`
  tokens/s (about `+5.9%`), with correct greedy output. Eight rows, large workgroups for all
  m, and forced wave32 were measured and rejected.
- Run-to-run spread under this protocol is about `0.5` tokens/s; treat smaller deltas as
  noise. Evidence: `performance/2026-10-12/rdna2-*.json`.

## Speech on ggml/Vulkan

- Kokoro is no longer the prebuilt OpenCL archive: `src/kokoro_runtime_ps5.cpp` drives
  kokoro.cpp (pinned `kokoro-cpp`) on the shared ggml Vulkan backend. The catalog preset is now
  `kokoro-82m-vulkan`; it downloads the GGUF model, voice and espeak-ng assets. The retired
  `kokoro-82m-fp16` directory is ignored by text-model discovery and left intact for users.
- Switching failure was caused by Kokoro's 262144-node scheduler/metadata reservation. The
  PS5 adapter now caps both at 131072; a full text → SD PNG → text → Stable Audio → text →
  Kokoro → text cycle passed on `PPSA99023`. Kokoro emitted valid mono 24 kHz WAV; Stable Audio
  emitted valid stereo 44.1 kHz WAV; SD emitted a verified 512×512 PNG and TGA preview. The
  benchmark record and artifacts are in `/tmp/prospero-final-all-modalities.json` and its
  sibling artifact directory for this session. The shared router's one-model-at-a-time
  serialization is now verified across these web API transitions.
- The adjustment is an explicit PS5 dependency adapter (`tools/prepare-kokoro-ps5.py`) invoked by
  `tools/build-vulkan-runtime.sh`, so a fresh dependency checkout gets the same guarded patch.
  Host reference comparison remains `0.9985` envelope correlation. `vendor/lib/libkokoro-tts.a`
  remains unused and can be removed separately.
  `vendor/lib/libkokoro-tts.a` remains unused and can be removed separately.

## PNG output

- SD-Turbo writes a PNG alongside its TGA preview; HTTP media responses use `image/png`,
  and the web UI displays/downloads the PNG directly. The native PS5 UI retains its TGA preview
  and archives it alongside the direct PNG path. Console verification now confirms the PNG
  signature and 512×512 dimensions on `PPSA99023`.

## Upstream llama.cpp update strategy

- Upstream's latest release checked 2026-10-12 is llama.cpp `v0.6.0` (`d812350`, 2026-10-05),
  with ggml `v0.26.0`; this checkout's shared llama pin is `cb7934c` / ggml `v0.21.0`. The
  upgrade is researched but not yet applied.
- Keep upstream pinned and project integration in narrow adapters: runtime wrappers for text,
  image, audio and TTS; guarded PS5 source-preparation scripts only for unavoidable platform
  hooks; and a compatibility test matrix before advancing the pin. The upstream backend table
  includes Vulkan and HIP, but generic HIP support does not establish a usable PS5 ROCm/HIP tool
  chain; Vulkan remains the supported console path. Recheck the upstream op-support matrix when
  moving ggml, since backend coverage is operation-specific.

## Release preparation

- The `01.003.000` Vulkan app initiated a real console self-update from a temporary GitHub
  prerelease asset. It verified and applied the exact `01.004.000` candidate ZIP; the
  installed metadata matches the candidate and the updated app passed Bonsai and media.
- PR #15 targets version `01.004.000`. Release notes credit Quatrixone, Bilawal Riaz and
  BlackBearReloaded. Documentation and tools use configured test-title variables and do not
  publish the private console address or test-title ID.
- The release folder/ZIP, lint and full host suite pass. CI fixes install OpenGL headers and
  discover Mesa's software Vulkan ICD; the repaired PR checks still need to pass.

## Deferred scope

Draft #20 carries AirLLM/Strata bounded-residency research, voice profiling, peak memory,
extended switching and capacity gates. PR #15 does not claim streaming inference, universal
quality or real-time audio.

Evidence: `performance/2026-10-11/vulkan-runtime-{build,bonsai,media,legacy-alias}.json`.
See [runtime details](VULKAN_RUNTIME.md) and [media comparison](MEDIA_VULKAN_COMPARISON.md).
