# Active work

Updated: 2026-10-14. Gates: [VK_PLAN.md](VK_PLAN.md); evidence: [VK_LOG.md](VK_LOG.md).
The PR keeps only the final all-model performance record at
`performance/2026-10-14/pr27-all-models-3x-final.json`; earlier raw reports were pruned.

## PR27 user fixes — 2026-10-14

Test slot PPSA99023 runs `pr27-ace-staged`. Main PPSA99004 untouched, no reboot.
Every installed model now loads after any other: Granite (hybrid KV estimate), BoHF and
Bonsai (budget total free direct memory, not the largest block), Chatterbox (T3 runner
buffers cleared; stale memory after llama.cpp made decode NaN) and ACE-Step (stages loaded
on demand, Vulkan peak 7.39 → 4.53 GiB). `tools/benchmark-all-models-ps5.py` measures all
models with a switch on every request. `ps5ctl.py trace` reads the app debug trace.
Details and known limitations: [PR27_HANDOFF.md](PR27_HANDOFF.md).

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
  noise. The detailed run records were pruned from the PR.
- PR #29's PS5 Pro settings are now available as an explicit `ps5pro` tuning profile. On
  PPSA99023 (base PS5), the same Mistral 7B Q4_0 4K/256-token benchmark measured 72.27 and
  72.31 tok/s with the `ps5` profile before and after the Pro run, versus 69.17 tok/s using
  `DMMV_WG=1` and subgroup 32. Keep `ps5` as the default; the Pro profile is not validated on
  Pro hardware. See `docs/PS5_VULKAN_PROFILES.md`; the detailed comparison record was pruned.

## Speech on ggml/Vulkan

- `inference/adapters/media/kokoro_ps5.cpp` drives
  kokoro.cpp (pinned `kokoro-cpp`) on the shared ggml Vulkan backend. The catalog preset is now
  `kokoro-82m-vulkan`; it downloads the GGUF model, Heart and Bella voice packs, and espeak-ng assets.
  The shared runtime setting selects the Kokoro voice in native and web Settings. The retired
  `kokoro-82m-fp16` directory is ignored by text-model discovery and left intact for users.
- The PS5 adapter caps Kokoro's scheduler/metadata reservation at 131072 nodes. On 2026-10-12,
  repeated Kokoro requests passed (first 3.97 s, warm median 0.268 s) and text → Kokoro → text
  succeeded. The utterance was valid mono 24 kHz WAV. SD-Turbo likewise passed repeated
  text → SD PNG → text checks with verified 512×512 PNGs. Detailed reports were pruned from the PR.
- The adjustment is an explicit PS5 dependency adapter (`tools/prepare-kokoro-ps5.py`) invoked by
  `tools/build-vulkan-runtime.sh`, so a fresh dependency checkout gets the same guarded patch.
  Host reference comparison remains `0.9985` envelope correlation. The old Kokoro/OpenCL
  archive and its unused GGML archives have been removed from the repository.

## PNG output

- SD-Turbo writes a PNG alongside its TGA preview; HTTP media responses use `image/png`,
  and the web UI displays/downloads the PNG directly. The native PS5 UI retains its TGA preview
  and archives it alongside the direct PNG path. Console verification now confirms the PNG
  signature and 512×512 dimensions on `PPSA99023`.

## Stable Audio 3 Vulkan adapter

- Pinned audio.cpp cross-builds as a static C API/runtime plus ggml Vulkan and links through the
  native RADV recipe. The staged smoke harness verified registry/model/session/generation steps;
  production inference and repeat generation are now verified below.
- Stage 2 (load the 1.68 GB Q8 GGUF, no Vulkan session) initially stalled because the tensor
  source retained a whole-file mapping. The PS5 adapter now stores GGUF metadata only and reads
  tensor byte ranges on demand, with a 512 MiB per-tensor bound. This covers the largest SA3
  tensor (375 MiB); it is a bounded host-read limit, not a VRAM budget. Host regression checks for
  exact reads, release/re-read, truncated input and oversized tensor pass; metadata-only SA3 model
  load stayed below 128 MiB RSS.
- The production app on `PPSA99023` loads the new preset and generates a non-silent,
  unclipped 4-second stereo WAV (44.1 kHz PCM16) in about 19.1 seconds. The output is in
  `/data/prosperoai/work/audio/`.
- The post-response SIGSEGV was traced to audio.cpp C API/model-spec `thread_local` objects with
  non-trivial destructors on the short-lived PS5 HTTP worker. The pinned-source preparation now
  uses process-static cache objects; calls remain serialized by the inference mutex.
- The fix passed two randomized complete modality cycles, each with two requests per media model
  and text round-trips before/between/after modalities. Orders: Kokoro → SD → SA3, then SA3 → SD →
  Kokoro. Detailed reports were pruned from the PR.
- Isolated SA3 on the final clean build: 4-second 44.1 kHz stereo PCM16 WAV, 19.09 s cold / 5.83 s warm; repeated
  output hash identical. Full cycle and text switch verified on PPSA99023. Earlier crash attempt
  remains in `vulkan-runtime-media.json` as regression history.
- Original eboot is preserved at
  `build/audio-cpp-sa3-ps5-link-smoke/eboot-original.bin` (SHA-256
  `298326198b243711adae6476d7dbebb0c04e7d8e461cfb0639a18686b65a75cc`). It is retained for
  recovery; do not replace the active test eboot while the title remains mounted.
- RADV NEEDED includes the PS5 AGC platform-driver imports; AGC inference is removed, but this
  Vulkan stack is not AGC-platform-free. See [SA3/OpenCL research](OPENCL_TO_VULKAN_RESEARCH.md).

## ACE-Step 1.5 Vulkan adapter

- Added a dedicated adapter using the ACE-Step family in the already-pinned audio.cpp C API.
  The PS5 build links `stable_audio` and `ace_step` against the same audio.cpp/ggml-Vulkan
  runtime. Its router path, WAV output, release hook and a PS5 cross-build pass.
- The curated Turbo Q8_0 bundle pins the GGUF and upstream tokenizer/config sidecars. After the
  1 GiB tensor-read fix, PPSA99023 generated a valid 8-second stereo 48 kHz WAV in 118.28 s,
  then returned to Qwen 9B and answered a text prompt. The detailed report and generated artifact
  were pruned from the PR.
- The decoder compatibility patch uses a pinned-GGML BF16 cast round-trip where this audio.cpp
  revision's fused round operator is unavailable. Console quality/performance validation is a
  single smoke generation; repeatability and parity still need a controlled comparison.

## PR #27 curated adapter follow-up

- Stable Diffusion XL Base 1.0 now has a pinned ungated FP16 preset and a 768×768, 20-step image profile.
  The corrected 6.94 GB standalone checkpoint downloaded and passed bundle verification on PPSA99023.
  Generation is not viable with the current 11,712 MiB Vulkan heap: klog reports 6,450.88 MiB
  tracked weights and then fails UNet workspace checks (needs 843.51 MiB device / 331.51 MiB budget,
  available 0). The title stayed alive. Keep SDXL marked unsupported on this hardware pending a
  lower-memory profile or checkpoint.
- FLUX.2 Klein 4B now has a pinned Q4_0 diffusion model, Q4_K_M Qwen3 encoder and official VAE
  preset. The Q4_K_M Qwen3 encoder failed allocation on PPSA99023: it requested about 5.94 GB while
  the largest free direct-memory block was 5,245 MiB. After the idle SDXL context released, that
  block measured 9,839 MiB. The failure therefore reflects the memory state at the time, not a
  fixed 5 GiB Vulkan allocator limit. Retry FLUX with no idle media context before judging it.
- BoHF 12B MoE 3A Q3_K_M was added as a pinned text preset and verified on PPSA99023. Its 4.70 GB
  GGUF passed the preset SHA check, loaded with the standard `llama` architecture, and returned a
  correct short answer plus a 48-token warm response at about 66 tokens/s. The model is
  experimental; this confirms runtime compatibility, not general answer quality. The detailed
  console run record was pruned from the PR.
- Internal vs `/mnt/ext1/prosperoai` storage comparison on PPSA99023 found no meaningful warm
  inference difference. Qwen3.5 0.8B warm requests were 0.118–0.121 s on both paths. Kokoro warm
  requests had medians of 0.301 s internal and 0.303 s external. Cold process/model setup was about
  2.7–3.0 s for Qwen and 3.96–4.04 s for Kokoro. External test models remain in the requested
  directory. Detailed samples were pruned from the PR.
- The image adapter has a 768px/28-step SD3 profile for a locally supplied compatible model.
  The official Stability AI SD3.5 Medium checkpoint is gated and is not included as a preset.
- Fish Speech V1.5 remains out of scope for this runtime: its autoregressive model and DAC codec
  lack a compatible project C++/Vulkan implementation. Do not substitute Kokoro voice packs.

## PR #27 preset and KV-cache verification

- PPSA99023 verified short text generation for Mistral 7B Instruct Q4_0, Qwen3.5 9B Q4_0,
  MiniCPM5 2B Q8_0, Ternary Bonsai 2 27B PTQ1_0, Granite 4.0 H-Tiny Q4_K_M and BoHF 12B MoE
  3A Q3_K_M. Granite downloaded 4,230,976,352 bytes, passed preset integrity verification, and
  answered `84 multiplied by 3 equals 252`.
- Existing console runs verified SD-Turbo, Stable Audio 3 Small SFX, ACE-Step 1.5 Turbo and
  Kokoro. New retries still failed for FLUX.2 Klein (Qwen encoder could not allocate a 5,939.93
  MiB CPU buffer) and SDXL (sampling needed 636.55 MiB of Vulkan device workspace and reported
  zero available). Both remain unusable on this PS5 profile.
- The text adapter uses llama.cpp KV memory and contains longest-common-prefix reuse across
  requests. Three identical Qwen prompts on the console all logged `reused=0`; cross-request
  prefix reuse is therefore not demonstrated in the live path, even though normal per-token KV
  decoding is active. Diagnose cache eligibility/state before treating prefix caching as working.

## Model downloader throughput

- PPSA99023 completed and SHA-verified the same 4.03 GB Granite preset at 8 and 16 HTTP range
  connections. Poll samples were about 28–30 MiB/s at 8 and 37–42 MiB/s at 16; 16 is the
  current setting. A 32-connection run ended with a `CRASH KILL_END` event before a reliable
  speed or resource sample. The PS5 `shsrv` monitor was unavailable, so peak CPU/RSS is unknown.
- Full run details, Granite inference and the ACE-Step retest were pruned from the PR.
- One-minute idle unload was verified in klog; the next Qwen request reloaded successfully in
  about 4.1 s. The configured two-minute timeout was restored.
- Workspace text export now saves the conversation before inference, archives plain `.txt`, and
  serves it as `text/plain`; deletion removes the export. Audio/image HTTP paths return WAV/PNG
  directly. New per-output JSON metadata sidecars are not written; session metadata stays in
  `session.json`. The 2026-10-12 follow-up report records the PS5 checks.

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
- Adapter compatibility pass started against an isolated upstream v0.6.0 worktree. The PS5 I/O
  and Vulkan timing/UMA hooks apply after adapting to upstream's out-of-class Vulkan perf logger.
  Host `llama`, Vulkan, SD and Kokoro targets built; both runtime ABI and media Vulkan tests pass
  with llvmpipe explicitly selected (`GGML_VK_VISIBLE_DEVICES=0`). This does not prove PS5
  cross-build/runtime behavior. The active app still uses Prism for private Bonsai
  `PQ2_0`/`PTQ1_0` types, so do not advance its runtime pin until these types/kernels are ported
  or a compatible upstream path is proven.

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

The detailed historical runtime evidence files were pruned from the PR. The retained current
summary is `performance/2026-10-14/pr27-all-models-3x-final.json`.
See [runtime details](VULKAN_RUNTIME.md) and [media comparison](MEDIA_VULKAN_COMPARISON.md).
