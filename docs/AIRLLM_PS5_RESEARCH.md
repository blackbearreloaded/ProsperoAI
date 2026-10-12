# AirLLM on ProsperoAI: research and decision — 2026-10-11

## Decision

AirLLM/Strata-style bounded weight residency is a priority capability for models larger
than available console memory. Keep resident quantized inference as the correctness and
speed reference while implementing the native capacity path. Replace
Phase 4c's speculative speed promise with the measured streaming feasibility gates below.
The useful transfer is bounded weight residency and asynchronous prefetch, implemented
inside our native runtime only if storage/memory/correctness measurements justify it.
No AirLLM package or PyTorch runtime was installed. The optional MiniCPM model test
belongs to PR #18 integration, not the streaming experiment.

## What was inspected

AirLLM main was pinned for this review to commit
`8df7eebe974a2c7cd120b334cd211cd25624cfdc` (2026-10-10), package version 4.0.0.
The live README and pinned implementation were inspected separately; the README's
small-VRAM claims are not PS5 benchmarks.

- [README](https://github.com/lyogavin/airllm): capacity claims and disk/compression caveats.
- [Runtime hooks](https://github.com/lyogavin/airllm/blob/8df7eebe974a2c7cd120b334cd211cd25624cfdc/air_llm/airllm/airllm_base.py):
  `_install_streaming_hooks`, `_pre_hook`, `_post_hook`, `_setup_expert_streaming`.
- [Shard I/O](https://github.com/lyogavin/airllm/blob/8df7eebe974a2c7cd120b334cd211cd25624cfdc/air_llm/airllm/utils.py):
  splitting/loading, tensor subsets and compression.
- [Dependencies](https://github.com/lyogavin/airllm/blob/8df7eebe974a2c7cd120b334cd211cd25624cfdc/air_llm/setup.py):
  torch >=2.4, transformers >=4.49,<6, accelerate, safetensors and other Python packages.
  Compression optionally uses bitsandbytes; it is not GGUF-native. AirLLM is Apache-2.0;
  copied source would require attribution/licence handling. This task only adapts the plan.

The runtime builds an empty Transformers model, splits checkpoint weights into layer
shards and hooks each layer: load immediately before compute, evict afterwards, optionally
prefetch the next layer with one worker. Its default device is CUDA. Selected MoE experts
can be loaded independently when model/module naming and shard storage support that path.
Tied embeddings and special modules can stay resident; KV/state is not removed by weight
eviction. At the inspected commit, explicit compression disables prefetching. This is a
memory-capacity technique with storage/transfer overhead; a small GPU figure does not
specify total RAM, disk size, first-token latency or decode rate.

## Why a direct port does not fit this app

Our native PS5 title uses C++/GGUF/ggml, RADV and shared CPU/GPU memory. No supported
PS5-native implementation of the above Python inference stack is present here. Moving
weights from GPU to CPU does not add physical capacity on this console. A useful native
adaptation must actually evict weights to SSD and bound all staging buffers, not merely
change their device. Retain quantized GGUF data and existing Vulkan kernels instead of
expanding every streamed layer to f16.

Our pinned llama.cpp is `cb7934c52ca8710994b2ecc19775ebefcfdb8d01`. Local inspection of
`src/llama-model-loader.cpp` shows model weight buffers are allocated/populated for the
resident graph; `ggml/include/ggml-backend.h` describes the evaluation callback as node
observation. Adding a callback alone does not make all weight allocation disappear.
Streaming requires explicit loader, graph/scheduler and Vulkan-buffer lifetime changes.
The Vulkan `MUL_MAT_ID` path also expects expert tensors; routed expert IDs alone do not
provide a storage-backed expert cache.

## Capacity and speed: calculate before building a runtime fork

Let B be measured sustained bytes/s for the actual SSD/staging/upload path, and D be the
weight bytes that must be read from disk for each generated token after cache hits.
Then decode is at most B/D tokens/s; compute, synchronization and seeks lower it further.
A single chat cannot batch future output tokens before their predecessors exist.
Prefill can amortize a weight read over a batch of known prompt tokens, but that does not
remove the autoregressive decode limit. Overlap hides some latency, not required bytes.

For illustration only, use B = 1.4 GB/s, the approximate historical raw-folder load
throughput in [our loading report](VK_LOAD_BENCHMARK_2026-10-09.md). This is not a fresh,
uncached layer-streaming SSD measurement. GB here is decimal.

| Illustrative workload | Bytes read/token | I/O-only upper bound | I/O-only time/token |
| --- | ---: | ---: | ---: |
| Dense 70B, ideal 4 bits/weight, all weights streamed | 35 GB | 0.040 tokens/s | 25 s |
| Dense 70B, f16, all weights streamed | 140 GB | 0.010 tokens/s | 100 s |
| Dense 30B, ideal 4 bits/weight, all weights streamed | 15 GB | 0.093 tokens/s | 10.7 s |
| MoE illustrative active bytes 1.7 GB, no cache | 1.7 GB | 0.82 tokens/s | 1.21 s |
| Same MoE with 90% byte cache hits | 0.17 GB | 8.24 tokens/s | 0.12 s |

These are arithmetic bounds, not model support or performance claims. Quantization
metadata, embeddings, KV, recurrent state and activation/scratch allocations add memory.
An expert's active-byte estimate and cache hit rate must come from actual routing traces.
For 1.7 GB/token at B=1.4 GB/s, an I/O-only target of 5 tokens/s needs at least 83.5% byte
hits; 10 tokens/s needs 91.8%, before compute overhead. The previous plan's 85% hit/5–10
speed suggestion therefore had inadequate evidence and is removed.

## Revised Phase 4c: native streaming feasibility, gated

1. **S0 — measurement without runtime changes.** On a test title, read raw GGUF extents
   using the existing four-worker positional-read implementation. Compare serial and
   prefetched layer-sized reads; record three runs, warm/cold limitations, read bandwidth,
   upload time, peak staging/direct memory and UI responsiveness. Inspect GGUF tensor
   sizes/offsets on the host; count resident non-streamed tensors, largest layers, KV and
   recurrent state. Reject a proposed interactive target when its B/D bound already fails.
2. **S1 — host correctness prototype.** Start with a supported dense transformer whose
   resident outputs are known. Build a GGUF offset index without splitting/copying the
   model. Defer streamed weight allocation/loading in a pinned llama fork. Execute layer
   subgraphs with two bounded slots and fences, keeping KV/activations and tied shared
   tensors resident. Only reuse a slot after its GPU work has completed. Validate packed
   quantization alignment, views, error/cancel cleanup and short reads with sanitizers.
   Compare finite logits and greedy tokens against the resident path, including identical,
   growing, edited and shortened prompts. Keep S1 a separate experimental target.
3. **S2 — PS5 dense proof.** Demonstrate a deliberately constrained memory budget with
   the same model, then one model that cannot load resident. Measure first-token latency,
   decode, staging/upload, memory and cancellation across three runs. A dense proof is
   capacity evidence; label slow results honestly. Interactive acceptance requires at
   least 5 tokens/s and useful first-token latency (initial target <=10 s for a short
   prompt), plus a quality benefit over resident Bonsai/smaller models. Otherwise retain
   the experiment only as research, not a default chat option.
4. **S3 — routed expert cache.** Only after S0/S1 pass, evaluate a supported MoE: retain
   shared attention/router/embeddings, use a bounded expert pool and byte-based LRU
   measurements, load quantized expert slices before `MUL_MAT_ID`, preserve expert IDs and
   collect hit/miss traces. Integrate via the pinned ggml/Vulkan scheduler or split the
   affected graph explicitly. Prefetch only justified routes; never assume 85–90% hits.
   Re-run context/state, prefix, cancellation and resident-reference correctness gates.
5. **S4 — product decision.** Keep the same runtime ABI, downloader integrity and explicit
   model selection. Report disk requirement and measured latency in a model's capability
   metadata only when supported. No automatic large model download. Accept the path only
   after PS5 correctness, speed and repeated switching gates; do not replace resident
   inference or remove AGC/media paths based on AirLLM's capacity claims.

S0 host GGUF indexing and bounded console disk probes are now implemented and measured
on Mistral and Ternary Bonsai 2. Vulkan uploads and streamed inference remain unmeasured.
The user-selected Ternary Bonsai 2 native inference is the top priority: establish its
Prism runtime baseline, then apply bounded residency. S1–S4 remain unimplemented. Training, CUDA kernels, Python packaging and mmap-based
large CPU lookup tables are outside this native app task.

## Relationship to Strata and current priority

Strata and AirLLM share the bounded-residency goal. Strata additionally places selected
MoE experts in a budgeted GPU cache and adapts placement to routing; its low-RAM paths
can keep the other experts in RAM or fetch them from SSD. A PC's separate RAM/VRAM
and resident experts are different from PS5 unified memory. We adapt bounded expert
placement and measured routing, with actual SSD eviction when physical capacity is
exceeded. Dense layer streaming and MoE expert caching are related, separate cases.
Source: [Strata details, pinned review](https://github.com/Niko1221/Strata/blob/61b3fb5dd3f1e8ec09cf7e4e05208bc6d3c46406/docs/DETAILS.md).

The selected [Ternary Bonsai 2](BONSAI2_PS5.md) is a dense hybrid `qwen35` model, not
an MoE. It provides the first correctness reference and a constrained-budget layer
streaming target. Its PTQ1_0 weights require Prism's quantization plus Hadamard graph
operations; our stock ggml runtime cannot be used as a reference for that checkpoint.
Its 64 layers total 5.379 GB; serial/parallel reads measured 1.384/1.778 GB/s medians
with 16 MiB staging and uncontrolled cache state. A packed pair of largest layers
needs 169.822 MB; other weights need 556.257 MB, plus unmeasured state/work buffers.
Mistral's tensors are interleaved: gather explicit extents rather than using a layer's
whole first-to-last file span. These findings guide S1; they do not implement it.


The resident reference is now verified on PS5: the exact Bonsai 2 PTQ1_0 produces
correct output, Thinking off/on, 4K/16K contexts and existing-model switching. Three
94-token warm runs give 11.208 tokens/s median. See the pinned build, full model
readback and raw timings in [Bonsai 2 results](BONSAI2_PS5.md). This unlocks S1
comparison; it does not implement streamed inference or establish upload/memory
measurements. Preserve Prism's Hadamard operations when adding bounded layers.
