# vulkan-runtime

One inference runtime for PS5 text, images, audio and voice. The app links the complete
Prism llama/ggml pin `e8fd209c975c80aa7fb36a8423571ff3b1b7e955`, one ggml ABI
(`GGML_MAX_NAME=160`), source-built SD `f89d9b13d730eabeede7314ce49dacd18d3c90c2`
and the Vulkan media bridge. No AGC inference backend, private ggml copies or legacy
scratch allocator are linked. CPU operations remain where the media bridge requires
them; RADV still links the console AGC platform driver.

## Build and compatibility

`make vulkan-runtime` produces `build/vulkan-runtime/<TEST-TITLE-ID>`; `make app` packages
the configured release title. Neither command deploys. `tools/build-vulkan-runtime.sh` builds the libraries and
`tools/build-vulkan-release.sh` packages the app. The CMake project, CI job and test
binaries use the same `vulkan-runtime` name. Historical make targets remain aliases. All previous app Vulkan/Bonsai
folder aliases now use this runtime. The pinned Bonsai PTQ1_0 preset is included.

Existing GGUF models continue to work. A legacy preset ID can alias its installed GGUF;
the router forwards the actual GGUF ID to llama. Old `.ps5lm` data is preserved but is
not executed: download the matching GGUF preset before using that legacy ID. Console
regressions cover Mistral, Qwen 3.5, MiniCPM5 and exact Bonsai 2 27B.

## Evidence and limits

The common-ABI console Bonsai benchmark generates 94 identical greedy tokens on three
warm runs: **11.202328 tokens/s median**. Thinking, 4K/16K context changes, older GGUFs
and return to Bonsai pass. See `performance/2026-10-11/vulkan-runtime-bonsai.json`.

The [AGC/media comparison](MEDIA_VULKAN_COMPARISON.md) records earlier comparison
builds: SD repeat median 46.048 → 1.001 seconds; Stable Audio 368.103 → 124.106;
Kokoro 84.740 → 85.152. This is a complete-implementation comparison, not an isolated
GPU-driver test. Kokoro is approximately 0.5% slower. The unified build's separate
functional media receipt is `performance/2026-10-11/vulkan-runtime-media.json`.

Host common-ABI tests perform actual Vulkan llama decode and media numerical checks.
The bridge parity fixture makes 30 Vulkan calls and zero CPU-kernel fallback calls.
ASan/UBSan real-model prefix checks verify cached/cold equivalence and error recovery.
The signed ELF has entry 0x120, 14 program headers and the working 15-library NEEDED
list; its symbol table contains one `ggml_init` and no AGC inference/private ggml symbols.

Peak memory, thermals, extended switching, voice optimization and AirLLM/Strata S1
are follow-up work. No streaming, universal perceptual parity or real-time audio claim.
