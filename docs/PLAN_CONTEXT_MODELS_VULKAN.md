# vulkan-runtime PR scope

The current PR completes one pinned Prism/Vulkan inference runtime and removes AGC
inference and private ggml copies. It includes exact Ternary Bonsai 2 27B PTQ1_0,
GGUF compatibility for Mistral, Qwen and MiniCPM5, source-built SD-Turbo, Stable Audio
and Kokoro through the Vulkan bridge. Existing download-resume work remains included.

Upstream #19 (context/output limits), #17 (transactional prompt-prefix reuse) and #18
(MiniCPM5 support) are merged dependencies. Preserve their defaults and regression
coverage when integrating upstream main. Test title is <TEST-TITLE-ID>; PPSA99004 is untouched.

Completion requires host regression tests, one common ggml ABI, static ELF checks,
console text/Thinking/context/model-switch checks, and valid media outputs followed by
successful return to text. Publish actual timings and regression limits in the PR.
Kokoro's measured approximately 0.5% slowdown is accepted; do not claim every path is
faster. RADV's platform AGC driver is required and remains linked.

Draft [#20](https://github.com/blackbearreloaded/ProsperoAI/pull/20) carries the remaining work: AirLLM/Strata S1 bounded layer slots,
prefetch and upload fences; output equivalence and physical-memory measurements;
voice CPU convolution/LSTM and allocation/upload profiling; extended switching and
capacity benchmarks. Expert caching and speculative decoding require their own gates.
These are follow-up deliverables, not merge requirements of this runtime PR.

See [runtime implementation](VULKAN_RUNTIME.md), [media comparison](MEDIA_VULKAN_COMPARISON.md)
and [active console evidence](VK_ACTIVE.md). The detailed follow-up plan and S0 research
are published on the subsequent draft branch.
