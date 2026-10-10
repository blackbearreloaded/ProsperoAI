# Plan: context and KV cache settings, larger models, everything on Vulkan

Status: proposal, 2026-10-10. Only phase 0 is implemented (in the pull request that adds this
file). Every later phase ends with a measurement on the console, and the next phase starts only
when that measurement shows a gain. Console tests use the PPSA99023 slot; PPSA99004 is never
touched. Figures marked *estimate* are calculations, not console measurements.

## Background

- **Context today.** `vulkan/gpt_runtime_vulkan.cpp` uses 4096 tokens, or a value from
  `/app0/context_size.txt` (512–16384) that the user cannot change from the app. The KV cache
  type and flash attention are never set, so the cache is f16. Every message clears the KV
  cache (`llama_memory_clear`) and prefills the whole conversation again, and
  `gpt_runtime_stats_t::reused_tokens` is never filled.
- **Memory.** RADV reports an 11,712 MiB heap with 11,574 MiB free before a model loads
  (`docs/VK_LOG.md`). The allocation ceiling has never been measured. CPU and GPU share the
  same memory, so offloading layers to the CPU adds no capacity.
- **KV per token (f16)**, from each model's `config.json`:

  | Model | Layers with KV | KV heads × head dim | Per token | 32K f16 | 32K q8_0 | 128K q8_0 |
  | --- | --- | --- | ---: | ---: | ---: | ---: |
  | Mistral 7B v0.3 | 32 | 8 × 128 | 128 KiB | 4 GiB | 2.1 GiB | (trained to 32K) |
  | Qwen3.5 9B | 8 of 32 | 4 × 256 | 32 KiB | 1 GiB | 0.53 GiB | 2.1 GiB |
  | Bonsai 27B (Qwen3.6 27B) | 16 of 64 | 4 × 256 | 64 KiB | 2 GiB | 1.06 GiB | 4.25 GiB |

- **Pinned llama.cpp** (`cb7934c`, 2026-10-03) already has `GGML_TYPE_Q1_0` (block 128) and
  `GGML_TYPE_Q2_0` (block 64) with Vulkan dequant and mat-vec shaders, the `qwen35`
  architecture, and Vulkan `GATED_DELTA_NET`/`SSM_CONV`. Vulkan flash attention accepts
  F16, BF16, Q8_0, Q5_x, Q4_x and IQ4_NL K/V caches. `common/fit.cpp` has a memory-fit
  estimator, but `LLAMA_BUILD_COMMON=OFF` and it assumes unlimited system memory.
- **Bonsai 27B** (PrismML, Apache 2.0): Qwen3.6 27B quantized end to end. `Bonsai-27B-Q1_0.gguf`
  is 3.8 GB. `Ternary-Bonsai-27B-Q2_g64.gguf` is 7.59 GB and is the upstream Q2_0 format that
  PrismML lists with Vulkan support (`PQ2_0` and the legacy `Q2_0` need their fork). PrismML
  measures 5.2 GB peak at 4K context with an f16 KV cache. The model thinks by default.
- **Strata** (Niko1221/Strata) runs a 125B MoE on a PC by keeping attention, hot experts and
  the KV cache in VRAM, all experts in 35–55 GB of pinned RAM computed by the CPU, and an
  n-gram table on SSD. It adds an MTP drafter, 8K-token prefill chunks, an 8-bit KV cache above
  4K, and a setup step that recommends a context size from free VRAM. Its code is CUDA. The
  PS5 has no separate RAM tier, so the code does not transfer, but its approach to sizing
  does.
- **AGC today.** Media models use AGC only for f16 GEMM and im2col
  (`src/ps5_agc_backend.cpp`, `supports_op`). Stable Audio and Kokoro are OpenCL libraries
  running through the `src/ps5_opencl.cpp` shim, whose kernels are CPU loops except GEMM.
  SD-Turbo is stable-diffusion.cpp on a second, symbol-renamed ggml. The legacy AGC text
  backends (`src/backends/*`) are slower than Vulkan text (47.96 vs 69.72 tokens/s for Mistral
  Q4_0).

## Phase 0. Downloads (this pull request)

- Release 01.002.000 moved downloads from the console's `sceHttp2` client to libcurl with a
  4 MB socket receive buffer, which removed the ~1 MB/s ceiling. The earlier pull request #9
  (ranged, pipelined downloads) was built on `sceHttp2` calls that no longer exist on `main`,
  so it cannot be merged as it stands.
- Kept from #9: **an interrupted download resumes** instead of starting again. When the
  connection breaks, the `.part` file stays. The next attempt for the same file hashes what is
  already there and asks only for the rest with an HTTP Range request
  (`prospero_https_get_from`, `CURLOPT_RANGE`). A server that ignores the range sends the whole
  file, which starts the file again. A finished `.part` or bundle file is checked again
  without a request. A cancelled download or a file that fails SHA-256 verification is still
  removed.
- Not kept: parallel ranged connections and a separate writer thread. Add them only if a
  measurement shows that one libcurl connection leaves bandwidth unused.
- **Console check:** download the Mistral preset on 01.003.000 and note MB/s; interrupt a
  download (unplug the network) and download it again to confirm it continues.

## Phase 1. Bonsai 27B manual test, no code

- Copy `Bonsai-27B-Q1_0.gguf` to `/data/prosperoai/models/` over FTP and send a short prompt at
  the current 4096 context.
- Measure: whether it loads, load time, decode and prefill tokens/s, memory from the log
  (Vulkan memory, KV, compute), and whether the answer makes sense (the Q1_0 shaders on RADV).
- **Gate:** sensible output and decode of roughly 15 tokens/s or more. Otherwise, set Bonsai
  aside and do phase 2 for Mistral and Qwen only.
- *Estimate:* 20–45 tokens/s decode (Mistral Q4_0, with a similar weight size, reaches
  69.7) and 40–70 tokens/s prefill. A 2,000-token prompt would then take 30–50 s, which is why
  phase 2 reuses the prefix.

## Phase 2. Context and KV cache in Settings

1. **Runtime** (`vulkan/gpt_runtime_vulkan.cpp`)
   - `gpt_runtime_configure(context, kv_type)`. A change recreates only the `llama_context`
     (about 73 ms on the console). The model stays loaded.
   - Set `type_k`, `type_v` and `flash_attn_type`. A quantized V cache requires flash
     attention. RDNA2 has no cooperative matrix, so the scalar path runs; measure it.
   - **Auto:** compute KV per token from GGUF metadata. Subtract weights, compute buffers and
     a reserve for the interface from `ggml_backend_dev_memory` free memory. Pick the largest
     step that fits, up to `n_ctx_train`. Use f16 up to 8K and q8_0 above.
   - **Prefix reuse:** prefill only the tokens after the first difference
     (`llama_memory_seq_rm`) and fill `reused_tokens`. Hybrid models (Qwen3.5/3.6, Bonsai)
     have recurrent state that cannot roll back to an arbitrary position. A growing chat
     works; an edited history needs a state checkpoint or a full prefill.
2. **Native interface:** in Settings › Generation, below Response length, add Context length
   (Auto, 4K, 8K, 16K, 32K, 64K, 128K, as far as the model allows), KV cache (Auto,
   Quality f16, Balanced q8_0, Compact q4_0) and Thinking (Off, On). In that category the
   right-hand card shows a memory bar (weights, KV, compute, free) for the active model
   instead of the live preview. It turns red when a choice does not fit, and Auto offers the
   nearest size that does. The chat status line shows `used / max ctx`.
3. **Storage:** `context=`, `kv=` and `thinking=` keys in the settings file.
   `/app0/context_size.txt` stays as a developer override.
4. **HTTP:** `GET/POST /api/settings/runtime`, and the same choices in the web Settings. The
   OpenAI-compatible API does not change the context per request.
5. **Later:** a per-model override from the model card in Models (`ctx.<model_id>=`).
6. **Console measurements (gate):** Mistral at 4K/16K/32K and Qwen3.5 at 32K/128K with f16,
   q8_0 and q4_0 (tokens/s, memory, output on a fixed reference text); time to first token
   for the second message of a long conversation before and after prefix reuse; the largest
   allocation RADV allows. **Gain** means 32K or more runs and a second message no longer
   waits for the whole history.

## Phase 3. Bonsai as a supported model

- Thinking: close it with an empty `<think></think>` block when off, or hide `<think>` in the
  interface. Raise the 512-token output cap when thinking is on.
- Sampling: top-k from model settings (PrismML recommends 20; the runtime fixes 40).
- A pinned Bonsai Q1_0 preset with SHA-256.
- **Gate:** Bonsai at 64K with q8_0 runs steadily in the app beside the interface.

*Estimated budget* (about 10.4 GiB usable: the heap minus about 1 GiB for the interface and a
margin; fixed part derived from PrismML's 4K figure):

| Variant | Fixed | KV | Total | Fits |
| --- | ---: | ---: | ---: | --- |
| Q1_0, 64K q8_0 | 4.6 | 2.1 | 6.7 GiB | yes, recommended |
| Q1_0, 128K q8_0 | 4.6 | 4.25 | 8.9 GiB | yes |
| Q1_0, 262K q4_0 | 4.6 | 4.5 | 9.1 GiB | tight |
| Q1_0, 128K f16 | 4.6 | 8.0 | 12.6 GiB | no |
| Q2_g64, 32K q8_0 | ~8.2 | 1.06 | 9.3 GiB | yes |
| Q2_g64, 64K q8_0 | ~8.2 | 2.1 | 10.3 GiB | at the limit |

## Phase 4. Larger models (Strata's approach, adapted to the PS5)

- **4a.** Replace the fixed 7 GiB download limit with a memory estimate (weights, 4K KV and
  compute). A model card shows fits, tight or does not fit. This admits Ternary Bonsai Q2_g64
  (7.07 GiB).
- **4b.** Ternary Bonsai Q2_g64 at 16K/32K q8_0. **Gate:** visibly better answers than Q1_0
  at an acceptable speed.
- **4c (research, not now).** Strata-style MoE expert streaming from SSD with a hot-expert
  cache. On the PS5 the only extra tier is the SSD (about 1.4 GB/s measured for a folder GGUF;
  mmap was reverted after FMEM stalls). For a model such as Qwen3.5-35B-A3B (`qwen35moe`) at
  Q4, about 20 GB with about 1.7 GB active per token, the *estimate* is 5–10 tokens/s at an
  85–90% cache hit rate. Start only if phases 1–4b show that dense 1–2-bit models are not
  enough.
- **4d.** A speculative drafter (Bonsai's DSpark) once upstream llama.cpp supports it outside
  CUDA.

## Phase 5. Everything on Vulkan

In order of expected gain. Each step's gate is faster than AGC with the same output.

1. **SD-Turbo:** pin the stable-diffusion.cpp source (only a prebuilt `.a` is in the
   repository today), build it against the ggml of llama.cpp `cb7934c` with `GGML_VULKAN`, and
   remove the second, renamed ggml. Compare 512 × 512 with today's ~76 s.
2. **Kokoro:** replace the OpenCL library with a ggml implementation on Vulkan. Failing that,
   back the OpenCL shim's ~25 named kernels with Vulkan compute shaders. Compare with ~79 s.
3. **Stable Audio Open Small:** the same as Kokoro, after finding and pinning the source of
   `libstable-audio.a`.
4. **Pocket TTS:** move its ggml copy to the shared Vulkan ggml, or retire it if unused.
5. **Legacy AGC text (`.ps5lm`):** do not port. Offer migration to GGUF and remove
   `src/backends/*` one release later.
6. After steps 1–3, remove `ps5_agc_backend`, `ps5_opencl` and the AGC scratch handling.

## Order

```
Phase 0 downloads ─┐
Phase 1 Bonsai test ┼─> Phase 2 context/KV ─> Phase 3 Bonsai ─> Phase 4a/4b
                    └─> Phase 5.1 SD on Vulkan (independent, can run in parallel)
```

Phases 2 and 5 do not touch the same code and can proceed at the same time. Phase 4c only
follows the measurements.

## Sources

- Strata: <https://github.com/Niko1221/Strata> and its `docs/DETAILS.md`.
- Bonsai 27B: <https://docs.prismml.com/models/bonsai-27b>,
  <https://huggingface.co/prism-ml/Bonsai-27B-gguf>,
  <https://huggingface.co/prism-ml/Ternary-Bonsai-27B-gguf>.
- Qwen3.6-27B and Qwen3.5-9B `config.json` on Hugging Face.
- llama.cpp `cb7934c52ca8710994b2ecc19775ebefcfdb8d01`: `ggml/include/ggml.h`,
  `ggml/src/ggml-common.h`, `ggml/src/ggml-vulkan/`, `src/llama-arch.cpp`, `common/fit.h`.
