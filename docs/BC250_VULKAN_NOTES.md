# BC-250 Vulkan findings relevant to PS5

Research checked 2026-10-12 against [akandr/bc250](https://github.com/akandr/bc250)
and its [ROCm comparison](https://github.com/akandr/bc250-rocm).

## What transfers

The BC-250 and PS5 both use AMD APUs with unified memory and Vulkan compute through
RADV/ggml. The BC-250 report is useful as a tuning-method reference: pin the exact
driver/runtime, isolate GPU work, warm up before timing, use repeated paired runs and
medians, and verify correctness after every kernel change. It also shows that a Vulkan
matvec specialization can materially change decode performance, while flash attention
mainly affects prefill and long-context work. These are hypotheses to test on our own
PS5 build, not measured PS5 results.

The BC-250 reports themselves are stack-sensitive: its board README describes roughly a
10× Vulkan prompt-processing advantage over the earlier ROCm setup, while the companion's
later patched ROCm build reports 1.38–1.49× prefill versus Vulkan for selected tests.
Those results use different software and test configurations, so neither ratio predicts
PS5 performance; they reinforce the need for paired measurements on the target console.

ProsperoAI already has PS5-specific Vulkan matvec changes: four standard-quant rows per
workgroup and a large hybrid-reduction workgroup for narrow projections. On the console,
the four-row change improved Mistral 7B Q4_0 decode from 62.93 to 66.62 tokens/s (+5.9%).
The BC-250's GFX1013/RDNA1 kernel tables and its thirteen llama.cpp ROCm patches are not
drop-in replacements for the PS5's RDNA2-class GPU or Vulkan backend. In particular, do
not copy HIP/ROCm patches into this project.

## What does not transfer

- The BC-250 40-CU unlock changes that board's Linux kernel/firmware. It is not a PS5
  setting and cannot enable additional PS5 compute units.
- `ttm.pages_limit`, GTT sizing, its custom amdgpu flush fixes, SDMA firmware substitution,
  and the `oberon-governor` clock/voltage policy are Linux BC-250 controls. The PS5 title
  cannot apply them, and the console's memory/clock policy is different.
- The BC-250's ROCm/HIP setup required a patched Linux kernel, native gfx1013 rocBLAS and
  other device-specific changes. It provides no evidence that HIP/ROCm is a viable PS5
  backend. Vulkan remains the supported path here.
- Ollama environment options (`OLLAMA_FLASH_ATTENTION`, `OLLAMA_KV_CACHE_TYPE`, loaded-model
  limits) do not configure this app. ProsperoAI already enables flash attention automatically
  where supported and selects an available KV-cache policy; any further policy change needs
  an A/B benchmark on the PS5.

## Safe next experiments

1. Profile prompt processing and token decode separately with the same model, prompt,
   quantization, context and runtime build. Keep the current Mistral/Bonsai baselines.
2. Test one Vulkan kernel specialization at a time on the PS5, checking greedy output and
   at least three paired timing runs. Reject any change below observed run-to-run noise.
3. Benchmark flash-attention and KV-cache options only where the runtime exposes a real
   switch, recording peak memory and correctness as well as speed.
4. Avoid adopting BC-250 board tuning or HIP changes in PS5 build scripts.

## Additional model requests: adapter gate

The curated catalog is intentionally not just a download directory: a model is advertised
as usable only when the selected modality adapter understands its format and settings.
The requested additions are categorized as follows:

| Model | Modality | Current status |
| --- | --- | --- |
| Granite 4.0 H-Tiny Q4_K_M | Text | Added as a pinned GGUF preset. The current llama.cpp source recognizes `granitehybrid`; PS5 generated correct text and completed three warm benchmark runs. |
| Stable Diffusion 3.5 Medium | Image | SD3 sampling profile and local Diffusers-directory discovery are implemented. The official checkpoint is gated by Stability AI's license; it needs user-side acceptance before model installation and PS5 validation. |
| Stable Diffusion XL Base 1.0 | Image | PS5 verified the pinned checkpoint download, but 768px generation failed Vulkan UNet workspace allocation on the 11,712 MiB heap. Not currently usable. |
| FLUX.2 Klein 4B Q4_0 | Image | Pinned Q4_0 diffusion, Q4_K_M Qwen3 encoder and official VAE use the stable-diffusion.cpp Flux2 Klein path. PS5 encoder allocation failed with a 5.12 GiB largest free block; a smaller encoder remains to be tested. Apache 2.0. |
| ACE-Step 1.5 | Audio | The pinned audio.cpp C API includes an ACE-Step family loader. ProsperoAI builds that family with Stable Audio, routes a dedicated adapter, and pins the Q8 GGUF plus tokenizer/config resources. PS5 generated a valid 8-second stereo 48 kHz WAV in 118.28 s and passed a text round-trip after switching back; detailed run records were removed from this PR. |
| Fish Speech V1.5 | Voice | Not supported by the current C++/Vulkan stack: it requires its own autoregressive model and DAC codec runtime. It cannot be wired through Kokoro by changing voice packs. |

ACE-Step's build and download manifest are in place, and its first PS5 output and text-switch
tests pass; keep the model experimental until repeated quality/performance validation is done.
Keep SDXL Base and FLUX.2 Klein marked unusable on this hardware until their memory limits are addressed and image output is verified. SD3 remains unverified.
SD3 users must accept the model license and supply the gated weights themselves. Fish Speech
needs a separate compatible runtime before an adapter can be implemented.
