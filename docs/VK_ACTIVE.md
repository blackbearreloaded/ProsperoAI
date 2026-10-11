# Active work

Updated: 2026-10-11. Gates: [VK_PLAN.md](VK_PLAN.md); evidence: [VK_LOG.md](VK_LOG.md).

## Verified vulkan-runtime

- One complete Prism llama/ggml pin e8fd209, GGML_MAX_NAME=160, source-built SD and
  Vulkan media bridge. AGC inference, routing, scratch allocator and private ggml
  copies removed. RADV still requires the console AGC platform driver.
- Signed test eboot 10758bc0…; entry 0x120, 14 headers, baseline 15 NEEDED; one ggml_init,
  no AGC inference/private ggml symbols. Test title PPSA99023 booted successfully.
- Exact Bonsai PTQ1_0: 94 identical tokens on three warm runs, median **11.202328 t/s**.
  Thinking off/on, 4K/16K, Mistral/MiniCPM5/Qwen GGUF and return to Bonsai pass.
- Unified SD initial 29.787 s, Audio 153.245 s, Kokoro 89.337 s. Valid image/WAVs,
  byte-identical to prior shared Vulkan fixtures; return to text after each passes.
  These initial requests include loading, not three-run warm comparison medians.
- Earlier AGC versus Vulkan repeat medians: SD 46.048 → 1.001 s; Audio 368.103 → 124.106;
  voice 84.740 → 85.152 (approximately 0.5% slower, explicitly accepted).
- Host common-ABI actual Vulkan decode/media parity and real-model ASan/UBSan prefix
  equivalence pass. Complete host regression and lint pass; release folder/ZIP built.
- Console remains on test23. Auto/Auto/Thinking off restored; models retained. Main
  PPSA99004 untouched and no reboot. Legacy ID aliases require matching installed GGUF.

## PR scope

PR #15 carries the verified runtime migration and completed compatibility/download work.
Upstream #19/#17/#18 are merged dependencies. The subsequent draft carries S0 research
and the remaining AirLLM/Strata S1, voice profiling, peak memory, extended switching and
capacity gates. No streaming inference, universal quality or real-time audio claim.

Evidence: performance/2026-10-11/vulkan-runtime-{build,bonsai,media,legacy-alias}.json.
See [runtime](VULKAN_RUNTIME.md) and [comparison](MEDIA_VULKAN_COMPARISON.md).
